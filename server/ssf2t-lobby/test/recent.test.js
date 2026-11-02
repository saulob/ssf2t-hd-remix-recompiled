import assert from 'node:assert/strict';
import { test } from 'node:test';

import { RECENT_MATCHES_DAYS, RECENT_MATCHES_MAX } from '../src/metrics.js';
import { handleAdmin } from '../src/admin.js';
import { makeHub } from './harness.js';
import { fightEvent, playFight, send, startFightMatch } from './fight_support.js';

const MINUTE = 60 * 1000;
const recentOf = (env, limit = 20, days = 30) => env.hub.metricsHistory(days, limit).recent;
const ip = (n) => `203.0.113.${n}`;

function finished(env, n, { fights = 2, completed = fights, guest = 'base013', platformA = 'Windows', platformB = 'Windows',
  transport = 'direct', leave = true } = {}) {
  const m = startFightMatch(env, { nA: 100 + n * 2, nB: 101 + n * 2, guest, platformA, platformB, ipA: ip(2 * n + 1), ipB: ip(2 * n + 2) });
  m.a.call('match.ready', { match: m.match, ready: true });
  m.b.call('match.ready', { match: m.match, ready: true });
  for (let i = 0; i < fights; i++) playFight(m, i, { transport, complete: i < completed });
  env.advance(5 * MINUTE);
  if (leave) m.a.call('match.leave', { match: m.match });
  return m;
}

test('a closed match leaves one anonymous summary', () => {
  const env = makeHub();
  finished(env, 1, { fights: 7, completed: 7, guest: 'tu023', platformA: 'Windows', platformB: 'Linux' });
  const recent = recentOf(env);
  assert.equal(recent.total, 1);
  const [row] = recent.matches;
  assert.equal(row.fights_started, 7);
  assert.equal(row.fights_completed, 7);
  assert.equal(row.transport, 'direct');
  assert.equal(row.guest, 'tu023');
  assert.equal(row.platforms, 'Windows x Linux');
  assert.equal(row.state, 'Completed');
});

test('the fight counts of a recent match are started and completed apart', () => {
  const env = makeHub();
  finished(env, 1, { fights: 5, completed: 3 });
  const [row] = recentOf(env).matches;
  assert.equal(row.fights_started, 5);
  assert.equal(row.fights_completed, 3);
  assert.equal(row.state, 'Abandoned');
});

test('the duration runs from both players ready to the close', () => {
  const env = makeHub();
  const m = startFightMatch(env);
  env.advance(20 * 1000);
  m.a.call('match.ready', { match: m.match, ready: true });
  m.b.call('match.ready', { match: m.match, ready: true });
  env.advance(12 * MINUTE + 34 * 1000);
  m.a.call('match.leave', { match: m.match });
  assert.equal(recentOf(env).matches[0].duration_ms, 12 * MINUTE + 34 * 1000);
});

test('the final state is only what the service can tell', () => {
  const env = makeHub();
  finished(env, 1, { fights: 3, completed: 3 });
  const never = startFightMatch(env, { nA: 50, nB: 51, ipA: ip(50), ipB: ip(51) });
  never.a.call('match.leave', { match: never.match });
  const early = startFightMatch(env, { nA: 52, nB: 53, ipA: ip(52), ipB: ip(53) });
  early.a.call('match.ready', { match: early.match, ready: true });
  early.b.call('match.ready', { match: early.match, ready: true });
  early.a.call('match.leave', { match: early.match });
  const lost = startFightMatch(env, { nA: 54, nB: 55, ipA: ip(54), ipB: ip(55) });
  lost.a.drop();
  lost.b.drop();
  env.advance(31 * 1000);
  const states = recentOf(env).matches.map((r) => r.state);
  assert.deepEqual(states, ['Disconnected', 'Abandoned', 'Failed', 'Completed']);
});

test('a match that outlives its limit is a timeout', () => {
  const env = makeHub();
  const m = startFightMatch(env);
  m.a.call('match.ready', { match: m.match, ready: true });
  m.b.call('match.ready', { match: m.match, ready: true });
  env.advance(4 * 3600 * 1000 + 1000);
  assert.equal(recentOf(env).matches[0].state, 'Timeout');
});

test('the newest match comes first and the header counts the whole range', () => {
  const env = makeHub();
  for (let n = 1; n <= 5; n++) finished(env, n, { fights: n, completed: n });
  const recent = recentOf(env, 3);
  assert.equal(recent.total, 5);
  assert.equal(recent.matches.length, 3);
  assert.deepEqual(recent.matches.map((r) => r.fights_started), [5, 4, 3]);
});

test('the recent list is bounded to the configured number of matches', () => {
  const env = makeHub();
  for (let n = 1; n <= RECENT_MATCHES_MAX + 6; n++) {
    const m = startFightMatch(env, { nA: 1000 + 2 * n, nB: 1001 + 2 * n, ipA: ip(1 + (n % 200)), ipB: ip(2 + (n % 200)) });
    m.a.call('match.leave', { match: m.match });
    env.advance(1000);
    m.a.drop();
    m.b.drop();
  }
  assert.equal(env.host.sql.all('SELECT * FROM recent_matches').length, RECENT_MATCHES_MAX);
  assert.equal(recentOf(env, RECENT_MATCHES_MAX).total, RECENT_MATCHES_MAX);
});

test('matches older than the retention window expire', () => {
  const env = makeHub();
  finished(env, 1);
  env.advance((RECENT_MATCHES_DAYS + 1) * 24 * 3600 * 1000);
  assert.equal(recentOf(env, 20, 90).matches.length, 0);
  assert.equal(recentOf(env, 20, 90).total, 0);
  finished(env, 2);
  assert.equal(env.host.sql.all('SELECT * FROM recent_matches').length, 1);
});

test('a short range lists only the matches inside it', () => {
  const env = makeHub();
  finished(env, 1);
  env.advance(3 * 24 * 3600 * 1000);
  finished(env, 2);
  assert.equal(recentOf(env, 20, 1).total, 1);
  assert.equal(recentOf(env, 20, 7).total, 2);
});

test('a recent match carries nothing that identifies a player, an address or the match', () => {
  const env = makeHub();
  const m = finished(env, 1, { platformA: 'Windows', platformB: 'Linux' });
  const [row] = recentOf(env).matches;
  assert.deepEqual(Object.keys(row).sort(),
    ['duration_ms', 'ended', 'fights_completed', 'fights_started', 'guest', 'platforms', 'state', 'transport']);
  const columns = env.host.sql.all("SELECT name FROM pragma_table_info('recent_matches')").map((r) => r.name).sort();
  assert.deepEqual(columns, ['duration_ms', 'ended', 'fights_completed', 'fights_started', 'guest', 'platforms', 'seq', 'state',
    'transport']);
  const text = JSON.stringify(env.hub.metricsHistory(30, 50));
  for (const secret of [m.match, m.a.who.player, m.b.who.player, m.a.who.token, m.a.fields.name, ip(1), ip(2), 'relay_token',
    'token_hash', 'cid']) {
    assert.ok(!text.includes(secret), secret);
  }
});

test('the recent list is part of the history answer and the limit is validated', async () => {
  const env = makeHub();
  finished(env, 1);
  const source = { summary: async () => env.hub.metricsSummary(), history: async (d, r) => env.hub.metricsHistory(d, r) };
  const env2 = { SSF2T_DASHBOARD_TOKEN: 'dashboard-token-0123456789' };
  const headers = { Authorization: 'Bearer dashboard-token-0123456789' };
  const get = (path) => handleAdmin(new Request(`https://x.example${path}`, { headers }), env2, source);
  const ok = await (await get('/admin/api/history?days=7&recent=10')).json();
  assert.equal(ok.recent.limit, 10);
  assert.equal(ok.recent.matches.length, 1);
  assert.equal((await (await get('/admin/api/history?days=7')).json()).recent.limit, 20);
  for (const bad of ['0', '51', '-1', 'x', '1.5', '100']) assert.equal((await get(`/admin/api/history?days=7&recent=${bad}`)).status, 400, bad);
});

test('a fight reported late for a closed match leaves the recent list as it was', () => {
  const env = makeHub();
  const m = finished(env, 1, { fights: 1, completed: 1 });
  assert.equal(send(m.a, fightEvent(m, 'start', 5)).code, 'not_found');
  assert.equal(recentOf(env).matches[0].fights_started, 1);
});
