import assert from 'node:assert/strict';
import { after, before, test } from 'node:test';

import { handleAdmin, internalIncidentRoute } from '../src/admin.js';
import { ADMIN_HTML } from '../src/admin_page.js';
import { INCIDENT, isTicket, readClaims, readIncident, readStatusChange, ticketCode } from '../src/incidents.js';
import { startLobbyServer } from '../local/server.js';
import { CAPABILITIES, GAME_ID, LIMITS, PROTOCOL, WORKER_BUILD, tokenHash } from '../src/protocol.js';
import { openDashboard } from './dom.js';
import { identity, makeHub } from './harness.js';

const TOKEN = 'dashboard-token-0123456789';
const ENV = { SSF2T_DASHBOARD_TOKEN: TOKEN };
const MARKER = 'UNIQUE-REPORT-MARKER-4711';

const report = (extra = {}) => ({
  description: `The game froze on the title screen ${MARKER}`,
  launcher_version: '0.4.0',
  guest: 'tu023',
  platform: 'Windows',
  operating_system: 'Windows 11 (build 26200)',
  runtime: 'ReXGlue SDK 0.7.0',
  log_file: 'ssf2t_hd_remix_012.log',
  log_clipped: 'no',
  log_text: 'line one\nline two\n',
  ...extra,
});

const HOUR = 3600 * 1000;
const signedIn = (env, n, options) => {
  const client = env.connect(identity(n), options);
  assert.equal(client.hello({ presence: false }).code, 'ok');
  return client;
};
const counted = (env, name) => (env.hub.metrics.pending.get(name) ?? 0) + (env.host.sql.get(
  "SELECT SUM(count) AS n FROM metrics_daily_dimensions WHERE dimension = 'abuse' AND value = ?", name)?.n ?? 0);
const bare = (env, n, options) => env.connect(identity(n), options);
const file = (client, extra) => client.call('incident.file', report(extra));

function adminSource(env) {
  return {
    summary: async () => JSON.parse(JSON.stringify(env.hub.metricsSummary())),
    history: async (days) => JSON.parse(JSON.stringify(env.hub.metricsHistory(days))),
    incidents: async () => JSON.parse(JSON.stringify(env.hub.incidentList())),
    incident: async (ticket) => {
      const found = env.hub.incidentForAdmin(ticket);
      return found ? JSON.parse(JSON.stringify(found)) : null;
    },
    setIncidentStatus: async (ticket, change) => env.hub.setIncidentStatus(ticket, change),
  };
}
const admin = (env, path, { method = 'GET', body, token = TOKEN } = {}) => handleAdmin(
  new Request(`https://ssf2t-lobby.example${path}`, {
    method,
    headers: token ? { Authorization: `Bearer ${token}` } : {},
    body: method === 'POST' && body !== undefined ? JSON.stringify(body) : undefined,
  }), ENV, adminSource(env));

function allRows(env) {
  const tables = env.host.sql.all("SELECT name FROM sqlite_master WHERE type = 'table'").map((r) => r.name);
  return JSON.stringify(Object.fromEntries(tables.map((t) => [t, env.host.sql.all(`SELECT * FROM ${t}`)])));
}

// ---- capability and compatibility ----

test('the service announces bug_reports and the protocol number stays 3', () => {
  assert.equal(CAPABILITIES.bug_reports, true);
  assert.equal(CAPABILITIES.protocol_version, 3);
  assert.equal(PROTOCOL, 3);
  assert.equal(WORKER_BUILD, '2026-11-07-bug-reports');
  const env = makeHub();
  const reply = env.connect(identity(1)).hello();
  assert.equal(reply.capabilities.bug_reports, true);
  assert.equal(reply.capabilities.public_lobby, true);
  assert.equal(reply.capabilities.secret_characters, true);
});

test('a client that never files a report plays, befriends and reads the Lobby as before', () => {
  const env = makeHub();
  const a = signedIn(env, 1);
  const b = env.connect(identity(2));
  b.hello();
  assert.equal(a.call('friends.add', { code: identity(2).player }).code, 'ok');
  assert.equal(b.call('lobby.list').code, 'ok');
  assert.equal(b.call('nothing.such').code, 'bad_request');
});

test('only a report may be larger than an ordinary frame', () => {
  const env = makeHub();
  const a = signedIn(env, 1);
  const padding = 'x'.repeat(LIMITS.frameBytes + 10);
  assert.equal(a.call('ping', { padding }).code, 'too_large');
  assert.equal(a.call('lobby.list', { padding }).code, 'too_large');
  const swapped = JSON.stringify({ seq: 9, op: 'incident.file', ...report({ log_text: padding }) });
  assert.equal(a.raw(swapped).code, 'too_large');
  assert.equal(file(a, { log_text: `${padding}\n` }).code, 'ok');
  assert.equal(a.raw(`{"op":"incident.file","seq":10,"description":"${'y'.repeat(INCIDENT.frameChars)}"}`).code, 'too_large');
});

// ---- filing ----

test('a valid report is stored and answered with a ticket, a receipt and the time', () => {
  const env = makeHub();
  const a = signedIn(env, 1);
  const reply = file(a);
  assert.equal(reply.code, 'ok');
  assert.match(reply.ticket, /^SSF2-[A-HJ-NP-Z2-9]{6}$/);
  assert.match(reply.receipt, /^[0-9a-f]{64}$/);
  assert.equal(reply.submitted_at, env.clock.now);
  const stored = env.hub.incidentForAdmin(reply.ticket);
  assert.deepEqual(Object.keys(stored).sort(), ['description', 'fixed_version', 'guest', 'launcher_version', 'log_clipped',
    'log_file', 'log_text', 'ms', 'operating_system', 'platform', 'player_message', 'runtime', 'status', 'status_updated_at',
    'ticket']);
  assert.equal(stored.description, report().description);
  assert.equal(stored.guest, 'tu023');
  assert.equal(stored.platform, 'Windows');
  assert.equal(stored.log_text, 'line one\nline two\n');
  assert.equal(stored.status, 'new');
});

test('tickets are unique and carry the SSF2 prefix', () => {
  const env = makeHub();
  const a = signedIn(env, 1);
  const tickets = new Set();
  for (let i = 0; i < 2; i++) {
    tickets.add(file(a).ticket);
    env.advance(HOUR + 1000);
  }
  assert.equal(tickets.size, 2);
  assert.ok([...tickets].every(isTicket));
  assert.equal(isTicket('DCP-ABCDEF'), false);
  assert.equal(isTicket('SSF2-ABCDE0'), false);
  assert.equal(ticketCode(new Uint8Array([0, 1, 2, 3, 4, 255])), 'SSF2-ABCDE9');
});

test('a report keeps no address, token, token hash or friend code', () => {
  const env = makeHub();
  const who = identity(7);
  const a = env.connect(who, { ip: '198.51.100.23' });
  a.hello({ presence: false });
  const reply = file(a);
  const everything = `${allRows(env)}${JSON.stringify(env.hub.incidentList())}${JSON.stringify(env.hub.incidentForAdmin(reply.ticket))}`;
  const incidentRows = JSON.stringify([env.host.sql.all('SELECT * FROM incidents'), env.host.sql.all('SELECT * FROM incident_quota'),
    env.host.sql.all('SELECT * FROM incident_tracking')]);
  for (const secret of ['198.51.100.23', who.token, tokenHash(who.token), who.player, reply.receipt]) {
    assert.ok(!incidentRows.includes(secret), `incident tables hold ${secret.slice(0, 8)}`);
  }
  assert.ok(!JSON.stringify(env.hub.incidentForAdmin(reply.ticket)).includes(reply.receipt));
  assert.ok(everything.includes(tokenHash(who.token)), 'the account table still holds its own hash');
});

test('malformed and oversized reports are refused and store nothing', () => {
  const env = makeHub();
  const a = signedIn(env, 1);
  const bad = (extra, code) => {
    env.advance(61000);
    const reply = file(a, extra);
    assert.equal(reply.code, code, JSON.stringify(Object.keys(extra)));
  };
  bad({ description: 'too short' }, 'bad_request');
  bad({ description: '          ' }, 'bad_request');
  bad({ description: 'x'.repeat(INCIDENT.descriptionChars + 1) }, 'too_large');
  bad({ description: 'bell\u0007 in the text' }, 'bad_request');
  bad({ description: 'carriage\r\nreturn' }, 'bad_request');
  bad({ description: 42 }, 'bad_request');
  bad({ log_text: 'x'.repeat(INCIDENT.logBytes + 1) }, 'too_large');
  bad({ log_text: 'é'.repeat(INCIDENT.logBytes / 2 + 1) }, 'too_large');
  bad({ log_text: '   \n\t' }, 'bad_request');
  bad({ log_text: 'a\u0000b' }, 'bad_request');
  bad({ log_text: 'a\r\nb' }, 'bad_request');
  bad({ log_text: 7 }, 'bad_request');
  bad({ log_file: '../../etc/passwd' }, 'bad_request');
  bad({ log_file: 'dragons_crown_pro_001.log' }, 'bad_request');
  bad({ guest: 'base014' }, 'bad_request');
  bad({ launcher_version: '' }, 'bad_request');
  bad({ launcher_version: ' 0.4.0' }, 'bad_request');
  bad({ platform: 'Win<dows>' }, 'bad_request');
  bad({ log_clipped: 'maybe' }, 'bad_request');
  bad({ operating_system: 'x'.repeat(INCIDENT.metadataChars + 1) }, 'too_large');
  bad({ extra_field: 'hello' }, 'bad_request');
  const missing = report();
  delete missing.runtime;
  assert.equal(a.call('incident.file', missing).code, 'bad_request');
  assert.equal(a.raw('{"op":"incident.file","seq":50,').code, 'bad_request');
  assert.equal(env.host.sql.get('SELECT COUNT(*) AS n FROM incidents').n, 0);
  assert.equal(env.host.sql.get('SELECT COUNT(*) AS n FROM incident_quota').n, 0);
  assert.equal(env.host.sql.get('SELECT COUNT(*) AS n FROM incident_tracking').n, 0);
});

test('a report at the exact limits is accepted', () => {
  const env = makeHub();
  const a = signedIn(env, 1);
  const reply = file(a, { description: 'ñ'.repeat(INCIDENT.descriptionChars), log_text: `${'a'.repeat(INCIDENT.logBytes - 1)}\n` });
  assert.equal(reply.code, 'ok');
  assert.equal(env.hub.incidentForAdmin(reply.ticket).description.length, INCIDENT.descriptionChars);
});

test('filing needs a signed-in player', () => {
  const env = makeHub();
  assert.equal(file(bare(env, 1)).code, 'no_session');
});

// ---- receipts, status and reading ----

test('only the receipt of a report reads its status and its content', () => {
  const env = makeHub();
  const owner = signedIn(env, 1);
  const other = signedIn(env, 2);
  const mine = file(owner);
  const theirs = file(other);
  const stranger = bare(env, 3);
  const listed = stranger.call('incident.status', { reports: [
    { ticket: mine.ticket, receipt: mine.receipt },
    { ticket: mine.ticket, receipt: theirs.receipt },
    { ticket: theirs.ticket, receipt: mine.receipt },
    { ticket: 'SSF2-AAAAAA', receipt: mine.receipt },
  ] });
  assert.equal(listed.code, 'ok');
  assert.deepEqual(listed.reports.map((r) => r.ticket), [mine.ticket]);
  assert.equal(listed.reports[0].status, 'new');
  const read = stranger.call('incident.read', { ticket: mine.ticket, receipt: mine.receipt });
  assert.equal(read.code, 'ok');
  assert.equal(read.report.log_text, 'line one\nline two\n');
  assert.equal(read.report.submitted_at, mine.submitted_at);
  assert.ok(!('ms' in read.report));
  assert.equal(stranger.call('incident.read', { ticket: mine.ticket, receipt: theirs.receipt }).code, 'not_found');
  assert.equal(stranger.call('incident.read', { ticket: 'SSF2-AAAAAA', receipt: mine.receipt }).code, 'not_found');
});

test('malformed claims and receipts are refused', () => {
  const env = makeHub();
  const a = signedIn(env, 1);
  const mine = file(a);
  const c = bare(env, 4);
  for (const reports of [undefined, 'x', [{ ticket: mine.ticket }], [{ ticket: mine.ticket, receipt: 'zz' }],
    [{ ticket: 'DCP-ABCDEF', receipt: mine.receipt }], [null], Array(INCIDENT.statusBatch + 1).fill({ ticket: mine.ticket, receipt: mine.receipt })]) {
    assert.equal(c.call('incident.status', { reports }).code, 'bad_request');
  }
  assert.equal(c.call('incident.status', { reports: [], more: 1 }).code, 'bad_request');
  assert.equal(c.call('incident.read', { ticket: mine.ticket }).code, 'bad_request');
  assert.equal(c.call('incident.read', { ticket: mine.ticket, receipt: 'AB'.repeat(32) }).code, 'bad_request');
  assert.equal(c.call('incident.read', { ticket: mine.ticket, receipt: mine.receipt, extra: 1 }).code, 'bad_request');
  assert.equal(readClaims({ reports: [] }).length, 0);
  assert.equal(readClaims({ reports: [], extra: 1 }), null);
});

test('a status written by the administrator reaches the receipt holder only', async () => {
  const env = makeHub();
  const a = signedIn(env, 1);
  const mine = file(a);
  const saved = await admin(env, `/admin/api/incidents/${mine.ticket}/status`, { method: 'POST',
    body: { status: 'fixed', fixed_version: '0.4.1', player_message: ' Update the port. ' } });
  assert.equal(saved.status, 200);
  const status = bare(env, 9).call('incident.status', { reports: [{ ticket: mine.ticket, receipt: mine.receipt }] });
  assert.deepEqual(status.reports[0], { ticket: mine.ticket, status: 'fixed', status_updated_at: env.clock.now,
    fixed_version: '0.4.1', player_message: 'Update the port.' });
  const none = bare(env, 9).call('incident.status', { reports: [{ ticket: mine.ticket, receipt: 'f'.repeat(64) }] });
  assert.deepEqual(none.reports, []);
});

test('lookups work without a session and are limited per address', () => {
  const env = makeHub();
  const c = bare(env, 5);
  let last;
  for (let i = 0; i < INCIDENT.statusBatch; i++) {
    last = c.call('incident.status', { reports: [] });
    if (last.code !== 'ok') break;
  }
  assert.equal(last.code, 'limit');
  assert.ok((env.hub.metrics.pending.get('incident_lookup_limited') ?? 0) >= 1);
});

// ---- limits ----

test('two reports an hour, ten a day for one player; the window rolls', () => {
  const env = makeHub();
  const a = signedIn(env, 1);
  assert.equal(file(a).code, 'ok');
  assert.equal(file(a).code, 'ok');
  assert.equal(file(a).code, 'incident_hourly_limit');
  assert.equal(env.host.sql.get('SELECT COUNT(*) AS n FROM incidents').n, 2);
  let filed = 2;
  while (filed < INCIDENT.perAccountPerDay) {
    env.advance(HOUR + 1000);
    assert.equal(file(a).code, 'ok', `report ${filed + 1}`);
    filed += 1;
  }
  env.advance(HOUR + 1000);
  assert.equal(file(a).code, 'incident_daily_limit');
  env.advance(24 * HOUR);
  assert.equal(file(a).code, 'ok');
  assert.ok(counted(env, 'incident_hourly_limited') >= 1);
  assert.ok(counted(env, 'incident_daily_limited') >= 1);
});

test('one address is limited above a single account, and the quota holds only hashes', () => {
  const env = makeHub();
  const results = [];
  for (let n = 1; n <= 16; n++) {
    const client = signedIn(env, n, { ip: '203.0.113.50' });
    for (let k = 0; k < 2; k++) {
      results.push(file(client).code);
      env.advance(31000);
    }
  }
  assert.equal(results.filter((c) => c === 'ok').length, INCIDENT.perAddressPerDay);
  assert.deepEqual([...new Set(results.slice(INCIDENT.perAddressPerDay))], ['limit']);
  const quota = JSON.stringify(env.host.sql.all('SELECT * FROM incident_quota'));
  assert.ok(!quota.includes('203.0.113.50'));
  assert.ok(!quota.includes(identity(1).player));
  assert.ok(env.host.sql.all('SELECT account_key FROM incident_quota').every((r) => /^[0-9a-f]{32}$/.test(r.account_key)));
});

test('a refused report costs no storage and leaves no row behind', () => {
  const env = makeHub();
  const a = signedIn(env, 1);
  file(a);
  file(a);
  const before = allRows(env);
  assert.equal(file(a).code, 'incident_hourly_limit');
  assert.equal(file(a, { description: 'short' }).code, 'bad_request');
  assert.equal(allRows(env), before);
});

test('the newest 500 reports are kept and old quota rows are cleaned', () => {
  const env = makeHub();
  const a = signedIn(env, 1);
  for (let i = 0; i < 3; i++) {
    env.host.sql.run(`INSERT INTO incidents (ticket, ms, launcher_version, guest, platform, operating_system, runtime,
      description, log_file, log_clipped, log_text) VALUES (?, ?, 'v', 'tu023', 'Windows', 'os', '', 'd', 'f', 'no', 't')`,
    `SSF2-OLD00${i}`.replace('0', 'A'), 1000 + i);
  }
  file(a);
  assert.equal(env.hub.incidentList().incidents.length, 4);
  env.advance(25 * HOUR);
  file(a);
  assert.equal(env.host.sql.get('SELECT COUNT(*) AS n FROM incident_quota').n, 1);
});

test('report contents never reach the Worker log or the metrics', () => {
  const env = makeHub();
  const a = signedIn(env, 1);
  file(a);
  file(a);
  file(a);
  file(a, { description: 'x'.repeat(INCIDENT.descriptionChars + 1) });
  assert.equal(env.hub.metrics.pending.get('incident_filed'), 2);
  const metrics = JSON.stringify([env.hub.metricsSummary(), env.hub.metricsHistory(30, 20),
    env.host.sql.all('SELECT * FROM metrics_daily_dimensions')]);
  assert.ok(!env.logs.join('\n').includes(MARKER));
  assert.ok(!metrics.includes(MARKER));
  assert.ok(!metrics.includes('line one'));
});

// ---- admin ----

test('the administrator needs the dashboard token for every report route', async () => {
  const env = makeHub();
  const a = signedIn(env, 1);
  const { ticket } = file(a);
  for (const [path, method] of [['/admin/api/incidents', 'GET'], [`/admin/api/incidents/${ticket}`, 'GET'],
    [`/admin/api/incidents/${ticket}/log`, 'GET'], [`/admin/api/incidents/${ticket}/status`, 'POST']]) {
    for (const token of ['', 'wrong-token-wrong-token-wrong', `${TOKEN}x`]) {
      const response = await admin(env, path, { method, token, body: { status: 'closed' } });
      assert.equal(response.status, 401, `${method} ${path}`);
      assert.ok(!(await response.text()).includes(MARKER));
    }
  }
  assert.equal((await handleAdmin(new Request('https://x.example/admin/api/incidents'), {}, adminSource(env))).status, 503);
});

test('the administrator lists, reads, downloads and sets the status of a report', async () => {
  const env = makeHub();
  const a = signedIn(env, 1);
  const { ticket } = file(a);
  const listed = await (await admin(env, '/admin/api/incidents')).json();
  assert.equal(listed.kept, INCIDENT.kept);
  assert.equal(listed.incidents.length, 1);
  assert.equal(listed.incidents[0].ticket, ticket);
  assert.equal(listed.incidents[0].status, 'new');
  assert.ok(!('log_text' in listed.incidents[0]));
  assert.ok(listed.incidents[0].preview.startsWith('The game froze'));
  const one = await (await admin(env, `/admin/api/incidents/${ticket}`)).json();
  assert.equal(one.log_text, 'line one\nline two\n');
  assert.equal(one.guest, 'tu023');
  assert.ok(!JSON.stringify(one).includes('203.0.113.7'));
  const log = await admin(env, `/admin/api/incidents/${ticket}/log`);
  assert.equal(log.status, 200);
  assert.equal(await log.text(), 'line one\nline two\n');
  assert.equal(log.headers.get('Content-Disposition'), `attachment; filename="${ticket}-ssf2t_hd_remix_012.log"`);
  assert.match(log.headers.get('Content-Type'), /^text\/plain/);
  assert.equal(log.headers.get('X-Content-Type-Options'), 'nosniff');
  const saved = await admin(env, `/admin/api/incidents/${ticket}/status`, { method: 'POST', body: { status: 'investigating' } });
  assert.equal((await saved.json()).status, 'investigating');
  assert.equal((await (await admin(env, '/admin/api/incidents')).json()).incidents[0].status, 'investigating');
});

test('report routes refuse bad ids, bad methods and bad status changes', async () => {
  const env = makeHub();
  const a = signedIn(env, 1);
  const { ticket } = file(a);
  assert.equal((await admin(env, '/admin/api/incidents/DCP-ABCDEF')).status, 400);
  assert.equal((await admin(env, '/admin/api/incidents/..%2Fsecret')).status, 400);
  assert.equal((await admin(env, '/admin/api/incidents/SSF2-AAAAAA')).status, 404);
  assert.equal((await admin(env, `/admin/api/incidents/${ticket}/other`)).status, 404);
  assert.equal((await admin(env, `/admin/api/incidents/${ticket}/status`)).status, 405);
  assert.equal((await admin(env, '/admin/api/incidents', { method: 'POST', body: {} })).status, 405);
  const post = (body) => admin(env, `/admin/api/incidents/${ticket}/status`, { method: 'POST', body });
  assert.equal((await post({ status: 'nope' })).status, 400);
  assert.equal((await post({ status: 'fixed', fixed_version: 'bad version' })).status, 400);
  assert.equal((await post({ status: 'closed', extra: 1 })).status, 400);
  assert.equal((await post({ status: 'closed', player_message: 'x'.repeat(INCIDENT.messageChars + 1) })).status, 400);
  assert.equal((await post([])).status, 400);
  assert.equal((await admin(env, '/admin/api/incidents/SSF2-AAAAAA/status', { method: 'POST', body: { status: 'closed' } })).status, 404);
  assert.deepEqual(readStatusChange({ status: 'closed', fixed_version: '9.9' }).change,
    { status: 'closed', fixed_version: '', player_message: '' });
});

test('an unreachable Durable Object answers 502 without the report text', async () => {
  const response = await handleAdmin(new Request('https://x.example/admin/api/incidents', { headers: { Authorization: `Bearer ${TOKEN}` } }),
    ENV, { incidents: async () => { throw new Error(`lobby failed ${MARKER}`); } }, () => {});
  assert.equal(response.status, 502);
  assert.ok(!(await response.text()).includes(MARKER));
});

test('the Durable Object serves report routes to the Worker only', () => {
  const route = (path, init) => internalIncidentRoute(new Request(`https://lobby.internal${path}`, init));
  assert.deepEqual(route('/incidents'), { kind: 'list' });
  assert.deepEqual(route('/incidents/SSF2-ABCDEF'), { kind: 'one', ticket: 'SSF2-ABCDEF' });
  assert.deepEqual(route('/incidents/SSF2-ABCDEF/status', { method: 'POST' }), { kind: 'status', ticket: 'SSF2-ABCDEF' });
  assert.equal(route('/incidents/SSF2-ABCDEF/status'), null);
  assert.equal(route('/incidents/not-a-ticket'), null);
  assert.equal(route('/incidents', { headers: { Upgrade: 'websocket' } }), null);
});

test('the dashboard page has the Bugs section and asks the report routes only', () => {
  for (const id of ['bug-list', 'bug-detail', 'bug-log', 'bug-status', 'bug-status-save', 'bug-download', 'bug-copy']) {
    assert.ok(ADMIN_HTML.includes(`id="${id}"`), id);
  }
});

test('readIncident rejects keys the page could not have shown', () => {
  assert.deepEqual(readIncident({ op: 'incident.file', seq: 1, ...report() }).incident.guest, 'tu023');
  assert.equal(readIncident({ ...report(), cusa: 'CUSA00000' }).code, 'bad_request');
});

const plain = (value) => JSON.parse(JSON.stringify(value));
const settle = async () => {
  for (let i = 0; i < 30; i++) await Promise.resolve();
};

test('the page has Dashboard and Bugs tabs, Dashboard first and selected, and Bugs content apart from it', () => {
  const at = (needle) => {
    const index = ADMIN_HTML.indexOf(needle);
    assert.ok(index >= 0, needle);
    return index;
  };
  assert.ok(at('id="tab-dashboard"') < at('id="tab-bugs"'));
  assert.match(ADMIN_HTML, /id="tab-dashboard"[^>]*aria-selected="true"/);
  assert.match(ADMIN_HTML, /id="tab-bugs"[^>]*aria-selected="false"/);
  assert.match(ADMIN_HTML, /<button id="tab-dashboard"[^>]*>Dashboard<\/button>\s*<button id="tab-bugs"[^>]*>Bugs<\/button>/);
  assert.ok(at('id="tab-bugs"') < at('id="view-dashboard"'), 'the tabs come first');
  assert.match(ADMIN_HTML, /id="view-bugs"[^>]*hidden/);
  assert.match(ADMIN_HTML, /id="bug-detail"[^>]*hidden/);
  assert.doesNotMatch(ADMIN_HTML, /id="view-dashboard"[^>]*hidden/);
  const bugs = at('id="view-bugs"');
  for (const id of ['c-online', 'range', 'refresh', 'chars', 'abuse', 'secret-line', 'daily']) assert.ok(at(`id="${id}"`) < bugs, id);
  for (const id of ['bug-list', 'bug-detail', 'bug-log', 'bug-status-save', 'bug-empty', 'bug-refresh']) assert.ok(at(`id="${id}"`) > bugs, id);
  assert.ok(at('class="panel notes"') < bugs);
  assert.ok(at('id="signout"') < at('id="view-dashboard"'));
});

test('Dashboard is the default view, Bugs loads only when chosen and works with no reports', async () => {
  const env = makeHub();
  const summary = plain(env.hub.metricsSummary());
  const history = plain(env.hub.metricsHistory(30, 20));
  const empty = plain(env.hub.incidentList());
  const page = await openDashboard({ summary, history, historyFor: (path) => (path === '/admin/api/incidents' ? empty : history) });
  const hidden = (id) => page.nodes.get(id).hidden;
  assert.equal(hidden('view-dashboard'), false);
  assert.equal(hidden('view-bugs'), true);
  assert.equal(page.nodes.get('tab-dashboard').attrs['aria-selected'], 'true');
  assert.equal(page.nodes.get('tab-bugs').attrs['aria-selected'], 'false');
  assert.ok(page.requests.some((path) => path.startsWith('/admin/api/summary')));
  assert.ok(!page.requests.includes('/admin/api/incidents'));
  assert.equal(page.text('c-online'), '0');

  page.nodes.get('tab-bugs').listeners.click[0]();
  await settle();
  assert.equal(hidden('view-dashboard'), true);
  assert.equal(hidden('view-bugs'), false);
  assert.equal(page.nodes.get('tab-bugs').attrs['aria-selected'], 'true');
  assert.equal(page.nodes.get('tab-dashboard').attrs['aria-selected'], 'false');
  assert.ok(page.requests.includes('/admin/api/incidents'));
  assert.equal(hidden('bug-empty'), false);
  assert.equal(page.text('bug-empty'), 'No bug reports yet.');

  page.nodes.get('tab-dashboard').listeners.click[0]();
  await settle();
  assert.equal(hidden('view-dashboard'), false);
  assert.equal(hidden('view-bugs'), true);
  assert.equal(page.nodes.get('tab-dashboard').attrs['aria-selected'], 'true');
});

test('saving a status keeps the Bugs tab selected', async () => {
  const env = makeHub();
  const a = signedIn(env, 1);
  const { ticket } = file(a);
  const summary = plain(env.hub.metricsSummary());
  const history = plain(env.hub.metricsHistory(30, 20));
  const list = plain(env.hub.incidentList());
  const detail = plain(env.hub.incidentForAdmin(ticket));
  const page = await openDashboard({ summary, history, historyFor: (path) => {
    if (path === '/admin/api/incidents') return list;
    if (path.startsWith('/admin/api/incidents/')) return { ...detail, status: 'fixed', status_updated_at: 5, fixed_version: '0.4.1', player_message: '' };
    return history;
  } });
  page.nodes.get('tab-bugs').listeners.click[0]();
  await settle();
  page.nodes.get('bug-list').find((n) => n.tag === 'button')[0].listeners.click[0]();
  await settle();
  page.nodes.get('bug-status').value = 'fixed';
  page.nodes.get('bug-status-save').listeners.click[0]();
  await settle();
  assert.equal(page.nodes.get('view-bugs').hidden, false);
  assert.equal(page.nodes.get('view-dashboard').hidden, true);
  assert.equal(page.text('bug-status-note'), 'Saved.');
  assert.equal(page.rows('bug-list')[1][7], 'Fixed');
});

test('the dashboard lists the reports and shows one with its log as text, never markup', async () => {
  const env = makeHub();
  const a = signedIn(env, 1);
  const hostile = '<script>alert(1)</script> & "quoted"\nsecond line\n';
  const { ticket } = file(a, { log_text: hostile, description: 'Description with <b>markup</b> in it' });
  const summary = plain(env.hub.metricsSummary());
  const history = plain(env.hub.metricsHistory(30, 20));
  const list = plain(env.hub.incidentList());
  const detail = plain(env.hub.incidentForAdmin(ticket));
  const page = await openDashboard({ summary, history, historyFor: (path) => {
    if (path === '/admin/api/incidents') return list;
    if (path.startsWith('/admin/api/incidents/')) return detail;
    return history;
  } });
  assert.ok(!page.requests.includes('/admin/api/incidents'));
  page.nodes.get('tab-bugs').listeners.click[0]();
  for (let i = 0; i < 30; i++) await Promise.resolve();
  const rows = page.rows('bug-list');
  assert.equal(rows.length, 2);
  assert.equal(rows[1][0], ticket);
  assert.equal(rows[1][7], 'Open');
  assert.equal(rows[1][3], 'Title Update #1 · 0.0.2.3');
  assert.equal(page.text('bug-count'), '1 reports');
  const button = page.nodes.get('bug-list').find((n) => n.tag === 'button')[0];
  button.listeners.click[0]();
  for (let i = 0; i < 30; i++) await Promise.resolve();
  assert.equal(page.text('bug-title'), ticket);
  assert.equal(page.text('bug-log'), hostile);
  assert.equal(page.text('bug-text'), 'Description with <b>markup</b> in it');
  assert.equal(page.nodes.get('bug-detail').hidden, false);
  assert.equal(page.nodes.get('bug-log').children.length, 0);
  assert.ok(page.text('bug-facts').includes('Windows 11 (build 26200)'));
  assert.ok(page.requests.includes('/admin/api/incidents'));
  assert.ok(page.requests.includes(`/admin/api/incidents/${ticket}`));
});

// ---- over a real WebSocket ----

let server;
before(async () => {
  server = await startLobbyServer({ port: 0 });
});
after(async () => {
  await server.close();
});

test('a full-size report crosses a real WebSocket and is answered', async () => {
  const who = identity(31);
  const ws = new WebSocket(`ws://127.0.0.1:${server.port}/`);
  const inbox = [];
  ws.addEventListener('message', (e) => inbox.push(JSON.parse(e.data)));
  await new Promise((resolve, reject) => {
    ws.addEventListener('open', resolve, { once: true });
    ws.addEventListener('error', () => reject(new Error('handshake failed')), { once: true });
  });
  const answer = async (seq) => {
    for (let i = 0; i < 200; i++) {
      const found = inbox.find((m) => m.re === seq);
      if (found) return found;
      await new Promise((resolve) => setTimeout(resolve, 10));
    }
    throw new Error('timed out');
  };
  ws.send(JSON.stringify({ op: 'hello', seq: 1, proto: PROTOCOL, game: GAME_ID, guest: 'tu023', build: 1, player: who.player,
    token: who.token, name: 'Net', version: '0.4.0', platform: 'Windows', presence: false }));
  assert.equal((await answer(1)).capabilities.bug_reports, true);
  const log = `${'a line of log text\n'.repeat(3000)}`.slice(0, INCIDENT.logBytes);
  ws.send(JSON.stringify({ op: 'incident.file', seq: 2, ...report({ log_text: log }) }));
  const reply = await answer(2);
  assert.equal(reply.code, 'ok');
  assert.match(reply.ticket, /^SSF2-/);
  ws.send(JSON.stringify({ op: 'incident.status', seq: 3, reports: [{ ticket: reply.ticket, receipt: reply.receipt }] }));
  assert.equal((await answer(3)).reports[0].status, 'new');
  ws.close();
});
