import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { test } from 'node:test';

import { COUNTRY_HEADER, handleAdmin, internalMetricsRoute, tokenMatches } from '../src/admin.js';
import { ADMIN_CSS, ADMIN_HTML, ADMIN_JS } from '../src/admin_page.js';
import { metricsDay } from '../src/metrics.js';
import { befriend, identity, makeHub } from './harness.js';

const TOKEN = 'dashboard-token-0123456789';
const ENV = { SSF2T_DASHBOARD_TOKEN: TOKEN };
const SRC = join(import.meta.dirname, '..', 'src');

const get = (path, headers = {}) => new Request(`https://ssf2t-lobby.example${path}`, { headers });
const bearer = (token) => ({ Authorization: `Bearer ${token}` });

function source(env) {
  return {
    summary: async () => JSON.parse(JSON.stringify(env.hub.metricsSummary())),
    history: async (days) => JSON.parse(JSON.stringify(env.hub.metricsHistory(days))),
  };
}

async function summaryOf(env) {
  const response = await handleAdmin(get('/admin/api/summary', bearer(TOKEN)), ENV, source(env));
  assert.equal(response.status, 200);
  return response.json();
}

function player(env, n, options = {}) {
  const client = env.connect(identity(n), options);
  assert.equal(client.hello({ relay: true }).code, 'ok');
  return client;
}

function startMatch(env, nA = 1, nB = 2, options = {}) {
  const a = player(env, nA, options);
  const b = player(env, nB, options);
  befriend(a, b);
  const hosted = a.call('pm.host', {});
  const joined = b.call('pm.join', { session: hosted.session });
  a.inbox = [];
  b.inbox = [];
  return { a, b, match: joined.match, token: joined.relay_token };
}

const dimension = (env, name) => Object.fromEntries(env.host.sql.all(
  'SELECT value, count FROM metrics_daily_dimensions WHERE day = ? AND dimension = ?', metricsDay(env.clock.now), name)
  .map((r) => [r.value, r.count]));

test('GET /admin serves the dashboard page without a token', async () => {
  for (const path of ['/admin', '/admin/']) {
    const response = await handleAdmin(get(path), ENV, source(makeHub()));
    assert.equal(response.status, 200);
    assert.equal(response.headers.get('Content-Type'), 'text/html; charset=utf-8');
    assert.equal(response.headers.get('Cache-Control'), 'no-store');
    const html = await response.text();
    assert.match(html, /<title>SSF2T Online Metrics<\/title>/);
    assert.match(html, /Super Street Fighter II Turbo HD Remix · Online Metrics/);
    for (const label of ['Online now', 'In matches', 'Advertised', 'Direct', 'Relay', 'Pending', 'Current matches', 'Transport',
      'Daily totals', 'How to read this']) {
      assert.ok(html.includes(label), label);
    }
    assert.ok(!html.includes(TOKEN));
  }
  for (const [path, type] of [['/admin/app.js', 'text/javascript'], ['/admin/app.css', 'text/css']]) {
    const response = await handleAdmin(get(path), ENV, source(makeHub()));
    assert.equal(response.status, 200);
    assert.ok(response.headers.get('Content-Type').startsWith(type));
    assert.match(response.headers.get('Content-Security-Policy'), /default-src 'none'.*script-src 'self'/);
  }
});

test('the page labels Direct, Relay and Pending and writes values as text, never markup', () => {
  assert.match(ADMIN_JS, /direct: 'Direct', relay: 'Relay', pending: 'Pending'/);
  assert.match(ADMIN_CSS, /\.pill\.direct/);
  assert.match(ADMIN_CSS, /\.pill\.relay/);
  assert.match(ADMIN_CSS, /\.pill\.pending/);
  assert.ok(!/<script>|<style>| style=| on[a-z]+=/i.test(ADMIN_HTML));
  assert.ok(!/\beval\(|new Function|innerHTML|document\.cookie|[?&]token=/.test(ADMIN_JS));
  for (const text of [ADMIN_HTML, ADMIN_CSS, ADMIN_JS]) assert.ok(!/(src|href)=["']?https?:|@import|url\(/i.test(text));
  assert.doesNotThrow(() => new Function(ADMIN_JS));
});

test('the API needs the dashboard token and says so when none is configured', async () => {
  const env = makeHub();
  for (const headers of [{}, bearer('wrong-token-wrong-token-wrong'), bearer(`${TOKEN}x`), { Authorization: TOKEN }]) {
    for (const path of ['/admin/api/summary', '/admin/api/history?days=30']) {
      const response = await handleAdmin(get(path, headers), ENV, source(env));
      assert.equal(response.status, 401);
    }
  }
  assert.equal((await handleAdmin(get('/admin/api/summary', bearer(TOKEN)), {}, source(env))).status, 503);
  assert.equal((await handleAdmin(get('/admin/api/summary', bearer('short')), { SSF2T_DASHBOARD_TOKEN: 'short' }, source(env))).status, 503);
  assert.equal(tokenMatches(`Bearer ${TOKEN}`, TOKEN), true);
  assert.equal((await handleAdmin(get('/admin/api/history?days=366', bearer(TOKEN)), ENV, source(env))).status, 400);
  assert.equal((await handleAdmin(new Request('https://x.example/admin', { method: 'POST' }), ENV, source(env))).status, 405);
  assert.equal((await handleAdmin(get('/admin/nope', bearer(TOKEN)), ENV, source(env))).status, 404);
});

test('an unreachable Durable Object answers 502 without a stack or secret', async () => {
  const logs = [];
  const failing = { summary: async () => { throw new Error('lobby answered 500'); }, history: async () => ({}) };
  const response = await handleAdmin(get('/admin/api/summary', bearer(TOKEN)), ENV, failing, (text) => logs.push(text));
  assert.equal(response.status, 502);
  const text = await response.text();
  assert.equal(text, '{"error":"unavailable"}');
  assert.ok(!text.includes('lobby answered') && !text.includes(TOKEN));
  assert.ok(!logs.join('\n').includes(TOKEN));
});

test('an idle service reports zeros and an empty match list', async () => {
  const env = makeHub();
  const summary = await summaryOf(env);
  assert.equal(summary.day, metricsDay(env.clock.now));
  assert.deepEqual({ ...summary.live, platform: 0, version: 0, country: 0, transport: 0 },
    { online: 0, in_matches: 0, advertised: 0, connections: 0, matches: 0, direct: 0, relay: 0, pending: 0, list: [],
      platform: 0, version: 0, country: 0, transport: 0 });
  assert.deepEqual(summary.live.platform, []);
  assert.equal(summary.today.sessions, 0);
  const history = await (await handleAdmin(get('/admin/api/history?days=7', bearer(TOKEN)), ENV, source(env))).json();
  assert.equal(history.daily.length, 7);
  assert.deepEqual(history.samples, []);
  assert.deepEqual(history.dimensions.transport, []);
});

test('players online, advertisements and sessions are counted once per account', async () => {
  const env = makeHub();
  const a = player(env, 1, { ip: '203.0.113.1' });
  const again = env.connect(identity(1), { ip: '203.0.113.2' });
  assert.equal(again.hello({ relay: true }).code, 'ok');
  const b = player(env, 2, { ip: '203.0.113.3' });
  befriend(a, b);
  assert.equal(a.call('pm.host', {}).code, 'ok');
  assert.equal(a.call('pm.host', {}).code, 'ok');
  const summary = await summaryOf(env);
  assert.equal(summary.live.online, 2);
  assert.equal(summary.live.connections, 3);
  assert.equal(summary.live.advertised, 1);
  assert.equal(summary.today.sessions, 2);
  assert.equal(summary.today.advertised, 1);
  assert.equal(summary.today.peak_online, 2);
  assert.deepEqual(summary.live.platform, [{ value: 'Windows', count: 2 }]);
  assert.deepEqual(summary.live.version, [{ value: '0.2.2', count: 2 }]);
  const probe = env.connect(identity(3));
  assert.equal(probe.hello({ presence: false }).code, 'ok');
  assert.equal((await summaryOf(env)).today.sessions, 2);
});

test('a match shows as Pending, then Relay or Direct once the host chooses', async () => {
  const env = makeHub();
  const first = startMatch(env, 1, 2);
  let summary = await summaryOf(env);
  assert.deepEqual([summary.live.matches, summary.live.pending, summary.live.direct, summary.live.relay], [1, 1, 0, 0]);
  assert.equal(summary.live.in_matches, 2);
  assert.equal(summary.today.matches_started, 1);
  assert.equal(summary.live.list[0].transport, 'pending');
  assert.equal(summary.live.list[0].relay_available, true);
  assert.equal(summary.live.list[0].state, 'connecting');
  assert.equal(summary.live.list[0].seats, 2);

  assert.equal(first.a.call('match.transport', { match: first.match, transport: 'relay' }).code, 'ok');
  summary = await summaryOf(env);
  assert.deepEqual([summary.live.pending, summary.live.relay, summary.live.direct], [0, 1, 0]);
  assert.equal(summary.live.list[0].transport, 'relay');

  const second = startMatch(env, 3, 4);
  assert.equal(second.a.call('match.transport', { match: second.match, transport: 'direct' }).code, 'ok');
  summary = await summaryOf(env);
  assert.deepEqual([summary.live.matches, summary.live.relay, summary.live.direct, summary.live.pending], [2, 1, 1, 0]);
  assert.deepEqual(summary.live.transport, [{ value: 'direct', count: 1 }, { value: 'relay', count: 1 }]);
  assert.equal(summary.live.list.find((m) => m.transport === 'direct').relay_available, false);
  assert.deepEqual(dimension(env, 'transport'), { direct: 1, relay: 1 });
  const history = await (await handleAdmin(get('/admin/api/history?days=1', bearer(TOKEN)), ENV, source(env))).json();
  assert.deepEqual(history.dimensions.transport, [{ value: 'direct', count: 1 }, { value: 'relay', count: 1 }]);
});

test('a closed relay returns its match to Pending; ended matches leave the list', async () => {
  const env = makeHub();
  const m = startMatch(env);
  assert.equal(m.a.call('match.transport', { match: m.match, transport: 'relay' }).code, 'ok');
  m.b.drop();
  let summary = await summaryOf(env);
  assert.equal(summary.live.relay, 0);
  assert.equal(summary.live.pending, 1);
  assert.equal(summary.live.list[0].seats, 1);
  m.a.call('match.leave', { match: m.match });
  summary = await summaryOf(env);
  assert.equal(summary.live.matches, 0);
  assert.deepEqual(summary.live.list, []);
});

test('incompatible sign-ins and joins count as rejects, once per connection', async () => {
  const env = makeHub();
  const stranger = env.connect(identity(9));
  assert.equal(stranger.hello({ guest: 'nope' }).code, 'bad_guest');
  assert.equal(stranger.hello({ guest: 'nope' }).code, 'bad_guest');
  assert.equal(env.connect(identity(10)).hello({ proto: 1 }).code, 'proto');
  const summary = await summaryOf(env);
  assert.equal(summary.today.incompatible_rejects, 2);
  assert.deepEqual(dimension(env, 'reject'), { bad_guest: 1, proto: 1 });
  assert.equal(summary.today.sessions, 0);
});

test('the country is the edge header; a client never chooses it', async () => {
  const env = makeHub();
  const a = env.connect(identity(1), { country: 'BR' });
  a.hello();
  const b = env.connect(identity(2), { country: 'not a country' });
  b.hello();
  const summary = await summaryOf(env);
  assert.deepEqual(summary.live.country, [{ value: 'BR', count: 1 }, { value: 'Unknown', count: 1 }]);
  assert.ok(readFileSync(join(SRC, 'edge.js'), 'utf8').includes('withEdgeCountry(request, request.cf, limited)'));
  assert.equal(COUNTRY_HEADER, 'X-SSF2T-Edge-Country');
});

test('the dashboard exposes no relay token, connection id, address, name or friend code', async () => {
  const env = makeHub();
  const m = startMatch(env, 1, 2, { ip: '198.51.100.23' });
  assert.equal(m.a.call('match.transport', { match: m.match, transport: 'relay' }).code, 'ok');
  m.a.call('match.endpoint', { match: m.match, port: 40000, public_ip: '198.51.100.23', public_port: 40001 });
  const rows = env.host.sql.all('SELECT host_cid, join_cid, relay_token FROM matches');
  const summaryText = JSON.stringify(await summaryOf(env));
  const historyText = await (await handleAdmin(get('/admin/api/history?days=30', bearer(TOKEN)), ENV, source(env))).text();
  const pageText = ADMIN_HTML + ADMIN_JS + ADMIN_CSS;
  assert.ok(rows[0].relay_token > 0);
  for (const text of [summaryText, historyText, pageText]) {
    assert.ok(!text.includes(m.match), 'full match id');
    assert.ok(!text.includes(String(rows[0].relay_token)) || String(rows[0].relay_token).length < 4, 'relay token');
    assert.ok(!text.includes(rows[0].host_cid) && !text.includes(rows[0].join_cid), 'connection id');
    assert.ok(!text.includes('198.51.100.23'), 'address');
    assert.ok(!text.includes(m.a.who.player) && !text.includes(m.b.who.player), 'friend code');
    assert.ok(!text.includes(m.a.who.token), 'player token');
    assert.ok(!text.includes(m.a.fields.name), 'name');
    assert.ok(!/relay_token|token_hash|host_cid|join_cid|salt/.test(text.replace(/dashboard-token|relay tokens/g, '')), 'field names');
    assert.ok(!text.includes(TOKEN));
  }
  assert.deepEqual(Object.keys((await summaryOf(env)).live.list[0]).sort(),
    ['age_ms', 'endpoints', 'guest', 'id', 'relay_available', 'seats', 'state', 'transport']);
  const columns = env.host.sql.all("SELECT name FROM pragma_table_info('metrics_daily_dimensions')").map((r) => r.name);
  assert.deepEqual(columns.sort(), ['count', 'day', 'dimension', 'value']);
});

test('metrics write nothing per match or player and never fail a hub operation', () => {
  const env = makeHub();
  startMatch(env);
  const tables = env.host.sql.all("SELECT name FROM sqlite_master WHERE type = 'table' AND name LIKE 'metrics_%'")
    .map((r) => r.name).sort();
  assert.deepEqual(tables, ['metrics_daily', 'metrics_daily_characters', 'metrics_daily_dimensions', 'metrics_samples']);
  env.hub.metrics.count = () => { throw new Error('metrics down'); };
  env.hub.metrics.observe = () => { throw new Error('metrics down'); };
  const c = env.connect(identity(20));
  assert.equal(c.hello().code, 'ok');
  assert.ok(env.logs.some((l) => l.includes('metrics error')));
});

test('samples are written when counts grow and an alarm samples while anyone is connected', () => {
  const env = makeHub();
  player(env, 1);
  assert.equal(env.host.sql.all('SELECT * FROM metrics_samples').length, 1);
  assert.ok(env.host.alarmAt !== null);
  env.advance(16 * 60 * 1000);
  assert.ok(env.host.alarmAt === null || env.host.alarmAt > env.clock.now);
  player(env, 2);
  assert.equal(env.host.sql.all('SELECT * FROM metrics_samples').length, 2);
});

test('the Durable Object serves metrics only to the Worker, and /admin is routed before the WebSocket path', () => {
  const route = (path, headers = {}) => internalMetricsRoute(get(path, headers));
  assert.deepEqual(route('/metrics/summary'), { kind: 'summary' });
  assert.deepEqual(route('/metrics/history?days=90'), { kind: 'history', days: 90, recent: 20 });
  assert.deepEqual(route('/metrics/history?days=7&recent=50'), { kind: 'history', days: 7, recent: 50 });
  assert.deepEqual(route('/metrics/history?days=7&recent=51'), { kind: 'bad_request' });
  assert.deepEqual(route('/metrics/history?days=9999'), { kind: 'bad_request' });
  assert.equal(route('/metrics/summary', { Upgrade: 'websocket' }), null);
  assert.equal(route('/'), null);
  const worker = readFileSync(join(SRC, 'worker.js'), 'utf8');
  assert.ok(worker.indexOf('isAdminPath(url.pathname)') < worker.indexOf("=== 'websocket'"));
  assert.ok(worker.indexOf("url.pathname === '/health'") > worker.indexOf('isAdminPath(url.pathname)'));
  assert.ok(!/console\.log\([^)]*(token|Authorization)/i.test(worker));
  assert.ok(worker.includes('relay_supported: true, relay_max_payload: RELAY.maxPayload'));
});
