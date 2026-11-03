import assert from 'node:assert/strict';
import { test } from 'node:test';

import { openDashboard } from './dom.js';
import { fightEvent, playFight, send, startFightMatch } from './fight_support.js';
import { makeHub } from './harness.js';

const DAY = 24 * 3600 * 1000;
const RYU = 0;
const KEN = 4;
const AKUMA = 16;

const page = (env, days = 30) => openDashboard({ summary: env.hub.metricsSummary(), history: env.hub.metricsHistory(days, 20) });
const rowsOf = (view, id) => view.nodes.get(id).find((n) => n.className === 'row')
  .map((r) => [r.children[0].textContent, r.children[2].textContent]);
const firstNumber = (row) => Number(row[1].split(' ')[0].replace(',', ''));

function record(env, { p1, p2, result, daysAgo = 0, count = 1 }) {
  const saved = env.clock.now;
  env.clock.now -= daysAgo * DAY;
  for (let i = 0; i < count; i++) {
    env.hub.metrics.fightStarted({ p1, p2, transport: 'direct', guest: 'base013', platforms: ['Windows', 'Windows'] });
    if (result) env.hub.metrics.fightCompleted({ p1, p2, result });
  }
  env.clock.now = saved;
}

test('most played characters lists ten, in descending order, with the share of all picks', async () => {
  const env = makeHub();
  for (let id = 0; id < 12; id++) record(env, { p1: id, p2: (id + 1) % 12, count: 12 - id });
  const rows = rowsOf(await page(env), 'd-played');
  assert.equal(rows.length, 10);
  const counts = rows.map(firstNumber);
  assert.deepEqual(counts, [...counts].sort((a, b) => b - a));
  const picks = env.hub.metricsHistory(30).characters.reduce((sum, c) => sum + c.picks, 0);
  assert.equal(picks, 2 * 78);
  assert.equal(rows[0][1], `${counts[0]} · ${Math.round(100 * counts[0] / picks)}%`);
});

test('most wins and most losses list ten with rates over completed fights', async () => {
  const env = makeHub();
  for (let id = 0; id < 12; id++) {
    record(env, { p1: id, p2: (id + 1) % 12, result: 'p1', count: 12 - id });
    record(env, { p1: id, p2: (id + 1) % 12, result: 'p2', count: 1 });
  }
  const view = await page(env);
  const wins = rowsOf(view, 'd-wins');
  const losses = rowsOf(view, 'd-losses');
  assert.equal(wins.length, 10);
  assert.equal(losses.length, 10);
  assert.equal(wins[0][0], 'Ryu');
  assert.match(wins[0][1], /^13 wins · 87%$/);
  assert.match(losses[0][1], /losses? · \d+%$/);
  const winCounts = wins.map(firstNumber);
  const lossCounts = losses.map(firstNumber);
  assert.deepEqual(winCounts, [...winCounts].sort((a, b) => b - a));
  assert.deepEqual(lossCounts, [...lossCounts].sort((a, b) => b - a));
});

test('a matchup is the unordered pair: Ryu vs Ken and Ken vs Ryu are one row', async () => {
  const env = makeHub();
  record(env, { p1: RYU, p2: KEN, result: 'p1' });
  record(env, { p1: KEN, p2: RYU, result: 'p2' });
  record(env, { p1: KEN, p2: RYU, result: 'p1' });
  record(env, { p1: RYU, p2: KEN, result: 'draw' });
  const { matchups } = env.hub.metricsHistory(30);
  assert.equal(matchups.length, 1);
  assert.equal(matchups[0].fights, 4);
  assert.equal(matchups[0].a_wins, 2);
  assert.equal(matchups[0].b_wins, 1);
  assert.equal(matchups[0].draws, 1);
  assert.deepEqual(rowsOf(await page(env), 'd-matchups'), [['Ryu vs Ken', '4 · 100% · Ryu 50% / Ken 25% / Draw 25%']]);
});

test('matchups sort by fights, show their share, and stop at ten', async () => {
  const env = makeHub();
  for (let id = 1; id <= 12; id++) record(env, { p1: RYU, p2: id, count: 13 - id });
  const rows = rowsOf(await page(env), 'd-matchups');
  assert.equal(rows.length, 10);
  assert.equal(rows[0][0], 'Ryu vs E. Honda');
  const counts = rows.map(firstNumber);
  assert.deepEqual(counts, [...counts].sort((a, b) => b - a));
  const total = env.hub.metricsHistory(30).matchups.reduce((sum, m) => sum + m.fights, 0);
  assert.equal(rows[0][1], `${counts[0]} · ${Math.round(100 * counts[0] / total)}%`);
});

test('a mirror matchup has no split and an unfinished matchup shows only its count', async () => {
  const env = makeHub();
  record(env, { p1: KEN, p2: KEN, result: 'p1' });
  record(env, { p1: RYU, p2: AKUMA });
  const rows = rowsOf(await page(env), 'd-matchups');
  assert.deepEqual(rows, [['Ryu vs Akuma', '1 · 50%'], ['Ken vs Ken', '1 · 50%']]);
});

test('no matchups and one matchup', async () => {
  const env = makeHub();
  assert.match((await page(env)).text('d-matchups'), /No matchups recorded in this range/);
  record(env, { p1: AKUMA, p2: RYU, result: 'p2' });
  assert.deepEqual(rowsOf(await page(env), 'd-matchups'), [['Ryu vs Akuma', '1 · 100% · Ryu 100% / Akuma 0%']]);
});

test('duplicate fight events do not count a matchup, a pick or a result twice', () => {
  const env = makeHub();
  const m = startFightMatch(env);
  for (let i = 0; i < 3; i++) {
    send(m.a, fightEvent(m, 'start', 0, { p1: KEN, p2: RYU }));
    send(m.b, fightEvent(m, 'start', 0, { p1: KEN, p2: RYU }));
  }
  for (let i = 0; i < 3; i++) {
    send(m.a, fightEvent(m, 'complete', 0, { p1: KEN, p2: RYU, result: 'p1' }));
    send(m.b, fightEvent(m, 'complete', 0, { p1: KEN, p2: RYU, result: 'p1' }));
  }
  const history = env.hub.metricsHistory(30);
  assert.equal(history.matchups.length, 1);
  assert.equal(history.matchups[0].fights, 1);
  assert.equal(history.matchups[0].b_wins, 1);
  assert.equal(history.matchups[0].a_wins, 0);
  assert.equal(history.characters.find((c) => c.id === KEN).picks, 1);
});

test('an invalid character or an unstarted completion creates no matchup', () => {
  const env = makeHub();
  const m = startFightMatch(env);
  for (const bad of [99, -1, 17, 1.5, '4']) {
    assert.notEqual(send(m.a, fightEvent(m, 'start', 0, { p1: bad, p2: RYU })).code, 'ok');
  }
  assert.notEqual(send(m.a, fightEvent(m, 'complete', 3, { p1: RYU, p2: KEN, result: 'p1' })).code, 'ok');
  assert.equal(env.hub.metricsHistory(30).matchups.length, 0);
});

test('matchups follow the selected range', async () => {
  const env = makeHub();
  for (const daysAgo of [0, 10, 60, 200]) record(env, { p1: RYU, p2: KEN, daysAgo });
  const fights = (days) => env.hub.metricsHistory(days).matchups[0]?.fights ?? 0;
  assert.deepEqual([7, 30, 90, 365].map(fights), [1, 2, 3, 4]);
  assert.deepEqual(rowsOf(await page(env, 7), 'd-matchups'), [['Ryu vs Ken', '1 · 100%']]);
  assert.deepEqual(rowsOf(await page(env, 365), 'd-matchups'), [['Ryu vs Ken', '4 · 100%']]);
});

test('every build and platform feeds the same character and matchup aggregates', () => {
  const env = makeHub();
  const combos = [['base013', 'Windows'], ['tu023', 'Windows'], ['base013', 'Linux'], ['tu023', 'Linux']];
  combos.forEach(([guest, platform], i) => {
    const m = startFightMatch(env, { nA: 10 + 2 * i, nB: 11 + 2 * i, guest, platformA: platform, platformB: platform,
      ipA: `203.0.113.${10 + 2 * i}`, ipB: `203.0.113.${11 + 2 * i}` });
    playFight(m, 0, { p1: i % 2 ? KEN : RYU, p2: i % 2 ? RYU : KEN, result: 'p1' });
  });
  const { matchups, characters } = env.hub.metricsHistory(30);
  assert.equal(matchups.length, 1);
  assert.equal(matchups[0].fights, 4);
  assert.equal(matchups[0].a_wins + matchups[0].b_wins, 4);
  assert.equal(characters.find((c) => c.id === RYU).picks, 4);
});

test('matchup rows hold only characters and counts', () => {
  const env = makeHub();
  const m = startFightMatch(env);
  playFight(m, 0, { p1: RYU, p2: KEN, result: 'p1' });
  const columns = env.host.sql.all('PRAGMA table_info(metrics_daily_matchups)').map((c) => c.name);
  assert.deepEqual(columns, ['day', 'a', 'b', 'fights', 'a_wins', 'b_wins', 'draws']);
  const dump = JSON.stringify(env.host.sql.all('SELECT * FROM metrics_daily_matchups')) +
    JSON.stringify(env.hub.metricsHistory(30).matchups);
  for (const secret of [m.match, m.a.who.player, m.a.who.token, m.a.fields.name, '203.0.113']) assert.ok(!dump.includes(secret), secret);
});
