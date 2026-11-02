import assert from 'node:assert/strict';
import { test } from 'node:test';

import { ADMIN_CSS, ADMIN_HTML, ADMIN_JS } from '../src/admin_page.js';
import { makeHub } from './harness.js';
import { openDashboard } from './dom.js';
import { playFight, startFightMatch } from './fight_support.js';

const ip = (n) => `203.0.113.${n}`;
const MINUTE = 60 * 1000;

function scenario() {
  const env = makeHub();
  const one = startFightMatch(env, { nA: 1, nB: 2, guest: 'tu023', platformA: 'Windows', platformB: 'Linux', ipA: ip(1), ipB: ip(2) });
  one.a.call('match.ready', { match: one.match, ready: true });
  one.b.call('match.ready', { match: one.match, ready: true });
  playFight(one, 0, { p1: 0, p2: 4, result: 'p1' });
  playFight(one, 1, { p1: 0, p2: 4, result: 'p1' });
  playFight(one, 2, { p1: 0, p2: 4, result: 'p2' });
  playFight(one, 3, { p1: 0, p2: 4, complete: false });
  env.advance(7 * MINUTE);
  one.a.call('match.leave', { match: one.match });
  const two = startFightMatch(env, { nA: 3, nB: 4, guest: 'base013', ipA: ip(3), ipB: ip(4) });
  two.a.call('match.transport', { match: two.match, transport: 'relay' });
  playFight(two, 0, { p1: 16, p2: 0, result: 'p1', transport: 'relay' });
  playFight(two, 1, { p1: 16, p2: 0, result: 'p1', transport: 'relay' });
  return { env, one, two };
}

const open = async (env, options = {}) => openDashboard({ summary: env.hub.metricsSummary(),
  history: env.hub.metricsHistory(30, 20), ...options });

test('live cards say now and the accumulated cards say today', () => {
  for (const label of ['Online now', 'In matches now', 'Advertised now', 'Direct now', 'Relay now', 'Pending now']) {
    assert.ok(ADMIN_HTML.includes(`>${label}<`), label);
  }
  for (const label of ['Peak online today', 'Sessions today', 'Matches advertised today', 'Matches started today',
    'Fights started today', 'Fights completed today', 'Direct matches today', 'Relay matches today', 'Direct fights today',
    'Relay fights today', 'Avg fights per match today']) {
    assert.ok(ADMIN_HTML.includes(`>${label}<`), label);
  }
  assert.ok(!ADMIN_HTML.includes('>Direct<') && !ADMIN_HTML.includes('>Pending<'));
});

test('the sections follow the documented page order', () => {
  const order = ['Live now', 'Today', 'Selected range', 'Current matches', 'Last matches', 'Usage over time', 'Fights by day',
    'Matches by day', '<h2>Platform</h2>', '<h2>Transport</h2>', 'Fights by platform', 'Most played characters', 'Most wins',
    'Most losses', 'Most played matchups', '<h2>Characters</h2>', 'Daily totals', 'Diagnostics'];
  let at = -1;
  for (const marker of order) {
    const next = ADMIN_HTML.indexOf(marker);
    assert.ok(next > at, marker);
    at = next;
  }
});

test('the live numbers are the current state', async () => {
  const { env } = scenario();
  const page = await open(env);
  assert.equal(page.text('c-online'), '4');
  assert.equal(page.text('c-in-matches'), '2');
  assert.equal(page.text('c-relay'), '1');
});

test('today shows accumulated matches, fights and the average over matches started', async () => {
  const { env } = scenario();
  const page = await open(env);
  assert.equal(page.text('t-matches'), '2');
  assert.equal(page.text('t-advertised'), '2');
  assert.equal(page.text('t-fights'), '6');
  assert.equal(page.text('t-completed'), '5');
  assert.equal(page.text('t-direct-matches'), '1');
  assert.equal(page.text('t-relay-matches'), '1');
  assert.equal(page.text('t-direct-fights'), '4');
  assert.equal(page.text('t-relay-fights'), '2');
  assert.equal(page.text('t-avg'), '3.0');
});

test('the selected range sums sessions, matches, fights, transports and character results', async () => {
  const { env } = scenario();
  for (const days of [7, 30, 90]) {
    const page = await open(env, { range: String(days), historyFor: () => env.hub.metricsHistory(days, 20) });
    assert.match(page.text('range-title'), new RegExp(`last ${days} days`));
    assert.equal(page.text('r-sessions'), '4');
    assert.equal(page.text('r-advertised'), '2');
    assert.equal(page.text('r-matches'), '2');
    assert.equal(page.text('r-fights'), '6');
    assert.equal(page.text('r-completed'), '5');
    assert.equal(page.text('r-avg'), '3.0');
    assert.equal(page.text('r-direct-matches'), '1');
    assert.equal(page.text('r-relay-matches'), '1');
    assert.equal(page.text('r-direct-fights'), '4');
    assert.equal(page.text('r-relay-fights'), '2');
    assert.equal(page.text('r-picks'), '12');
    assert.equal(page.text('r-wins'), '5');
    assert.equal(page.text('r-losses'), '5');
    assert.equal(page.text('r-draws'), '0');
  }
});

test('the average is a dash when no match started', async () => {
  const env = makeHub();
  const page = await open(env);
  assert.equal(page.text('t-avg'), '–');
  assert.equal(page.text('r-avg'), '–');
});

test('current matches stay a live table with a short id only', async () => {
  const { env, two } = scenario();
  const page = await open(env);
  const rows = page.rows('matches');
  assert.equal(rows.length, 2);
  assert.deepEqual(rows[1].slice(0, 4), [two.match.slice(0, 6), 'Connecting', 'Original \u00b7 0.0.1.3', 'Relay']);
  assert.ok(!page.all().includes(two.match));
});

test('last matches shows a header and nothing when empty', async () => {
  const page = await open(makeHub());
  assert.equal(page.text('recent-title'), 'Last matches · showing 0 matches');
  assert.equal(page.text('recent-empty'), 'No finished matches in this range.');
  assert.equal(page.rows('recent').length, 0);
});

test('last matches with one row reads showing 1 match', async () => {
  const env = makeHub();
  const m = startFightMatch(env);
  m.a.call('match.ready', { match: m.match, ready: true });
  m.b.call('match.ready', { match: m.match, ready: true });
  for (let i = 0; i < 7; i++) playFight(m, i);
  env.advance(12 * MINUTE + 34 * 1000);
  m.a.call('match.leave', { match: m.match });
  const page = await open(env);
  assert.equal(page.text('recent-title'), 'Last matches · showing 1 match');
  const [head, row] = page.rows('recent');
  assert.deepEqual(head, ['Date and time', 'Duration', 'Fights started', 'Fights completed', 'Transport', 'Game build', 'Platforms',
    'Final state']);
  assert.deepEqual(row.slice(1), ['12m 34s', '7 fights', '7 completed', 'Direct', 'Original \u00b7 0.0.1.3', 'Windows x Windows', 'Completed']);
});

test('last matches lists several matches with their fight counts, newest first', async () => {
  const { env } = scenario();
  env.hub.metrics.recordMatch({ duration_ms: 4 * MINUTE + 12000, fights_started: 2, fights_completed: 1, transport: 'relay',
    guest: 'base013', platforms: 'Windows x Linux', state: 'Disconnected' });
  const page = await open(env);
  const rows = page.rows('recent').slice(1);
  assert.equal(rows.length, 2);
  assert.deepEqual(rows[0].slice(1), ['4m 12s', '2 fights', '1 completed', 'Relay', 'Original \u00b7 0.0.1.3', 'Windows x Linux', 'Disconnected']);
  assert.deepEqual(rows[1].slice(1), ['7m 0s', '4 fights', '3 completed', 'Direct', 'Title Update #1 \u00b7 0.0.2.3', 'Windows x Linux', 'Abandoned']);
});

test('a match from a client without telemetry shows dashes for its fights', async () => {
  const env = makeHub();
  env.hub.metrics.recordMatch({ duration_ms: 90000, fights_started: null, fights_completed: null, transport: 'direct',
    guest: 'base013', platforms: 'Windows x Windows', state: 'Ended' });
  const row = (await open(env)).rows('recent')[1];
  assert.deepEqual(row.slice(2, 4), ['–', '–']);
});

test('last matches asks for 20 by default and shows how many the range holds', async () => {
  const env = makeHub();
  for (let n = 0; n < 43; n++) {
    env.hub.metrics.recordMatch({ duration_ms: 1000 * (n + 1), fights_started: 1, fights_completed: 1, transport: 'direct',
      guest: 'tu023', platforms: 'Windows x Windows', state: 'Completed' });
  }
  const page = await openDashboard({ summary: env.hub.metricsSummary(), historyFor: (path) => {
    const recent = Number(/recent=(\d+)/.exec(path)[1]);
    return env.hub.metricsHistory(30, recent);
  } });
  assert.ok(page.requests.some((path) => path.includes('recent=20')));
  assert.equal(page.text('recent-title'), 'Last matches · showing 20 of 43 in selected range');
  assert.equal(page.rows('recent').length, 21);
  const wide = await openDashboard({ summary: env.hub.metricsSummary(), recent: '50', historyFor: (path) => {
    const recent = Number(/recent=(\d+)/.exec(path)[1]);
    return env.hub.metricsHistory(30, recent);
  } });
  assert.equal(wide.text('recent-title'), 'Last matches · showing 43 matches');
});

test('last matches is bounded to fifty even when more were recorded', async () => {
  const env = makeHub();
  for (let n = 0; n < 70; n++) {
    env.hub.metrics.recordMatch({ duration_ms: 1000, fights_started: 1, fights_completed: 1, transport: 'direct', guest: 'tu023',
      platforms: 'Windows x Windows', state: 'Completed' });
  }
  const page = await openDashboard({ summary: env.hub.metricsSummary(), history: env.hub.metricsHistory(30, 50), recent: '50' });
  assert.equal(page.text('recent-title'), 'Last matches · showing 50 matches');
});

test('most played characters are sorted by picks with their share', async () => {
  const { env } = scenario();
  const page = await open(env);
  const list = page.nodes.get('d-played').find((n) => n.className === 'row').map((r) => r.children.map((c) => c.textContent));
  assert.deepEqual(list.map((r) => [r[0], r[2]]), [['Ryu', '6 · 50%'], ['Ken', '4 · 33%'], ['Akuma', '2 · 17%']]);
});

test('most wins and most losses show counts with win and loss rates over completed fights', async () => {
  const { env } = scenario();
  const page = await open(env);
  const read = (id) => page.nodes.get(id).find((n) => n.className === 'row').map((r) => [r.children[0].textContent, r.children[2].textContent]);
  assert.deepEqual(read('d-wins'), [['Ryu', '2 wins · 40%'], ['Akuma', '2 wins · 100%'], ['Ken', '1 win · 33%']]);
  assert.deepEqual(read('d-losses'), [['Ryu', '3 losses · 60%'], ['Ken', '2 losses · 67%']]);
});

test('the character table lists picks, wins, losses, draws and both rates by picks', async () => {
  const { env } = scenario();
  const page = await open(env);
  const rows = page.rows('chars');
  assert.deepEqual(rows[0], ['Character', 'Picks', 'Wins', 'Losses', 'Draws', 'Win rate', 'Loss rate']);
  assert.deepEqual(rows[1], ['Ryu', '6', '2', '3', '0', '40%', '60%']);
  assert.deepEqual(rows[2], ['Ken', '4', '1', '2', '0', '33%', '67%']);
  assert.deepEqual(rows[3], ['Akuma', '2', '2', '0', '0', '100%', '0%']);
});

test('a draw lands in the draws column and out of both rates numerators', async () => {
  const env = makeHub();
  const m = startFightMatch(env);
  playFight(m, 0, { p1: 1, p2: 2, result: 'draw' });
  const rows = (await open(env)).rows('chars');
  assert.deepEqual(rows[1], ['E. Honda', '1', '0', '0', '1', '0%', '0%']);
});

test('panels with no results say so', async () => {
  const page = await open(makeHub());
  assert.match(page.text('d-played'), /No fights reported/);
  assert.match(page.text('d-wins'), /No completed fights/);
  assert.match(page.text('d-losses'), /No completed fights/);
  assert.equal(page.text('chars-empty'), 'No fights reported in this range.');
});

test('direct and relay totals show in the transport panels', async () => {
  const { env } = scenario();
  const page = await open(env);
  assert.match(page.text('d-transport'), /Direct.*1/);
  assert.match(page.text('d-ftransport'), /Direct.*4/);
  assert.match(page.text('d-ftransport'), /Relay.*2/);
});

test('fights by platform and by game build are listed', async () => {
  const { env } = scenario();
  const page = await open(env);
  assert.match(page.text('d-fbuild'), /Title Update #1 \u00b7 0.0.2.3.*4/);
  assert.match(page.text('d-fbuild'), /Original \u00b7 0.0.1.3.*2/);
  assert.match(page.text('d-fplatform'), /Windows/);
  assert.match(page.text('d-fplatform'), /Linux/);
});

test('game builds show their friendly names with the executable version', async () => {
  const { env } = scenario();
  const page = await open(env);
  const live = page.rows('matches');
  assert.equal(live[0][2], 'Game build');
  assert.deepEqual(live.slice(1).map((r) => r[2]), ['Original \u00b7 0.0.1.3']);
  assert.ok(page.rows('recent').some((r) => r.includes('Title Update #1 \u00b7 0.0.2.3')));
  assert.ok(!/Base013|TU023|base013|tu023/.test(page.all()));
});

test('a historical or future build value still has a readable label and storage is unchanged', async () => {
  const { env } = scenario();
  const history = env.hub.metricsHistory(30, 20);
  history.dimensions.fight_guest = [{ value: 'tu024', count: 3 }, { value: 'base013', count: 1 }];
  history.recent.matches[0].guest = 'tu099';
  const page = await open(env, { history });
  assert.match(page.text('d-fbuild'), /tu024.*3/);
  assert.match(page.text('d-fbuild'), /Original \u00b7 0\.0\.1\.3.*1/);
  assert.ok(page.rows('recent')[1].includes('tu099'));
  assert.deepEqual(env.hub.metricsHistory(30, 20).dimensions.fight_guest.map((d) => d.value).sort(), ['base013', 'tu023']);
});

test('the daily table has the fight columns and per-day values', async () => {
  const { env } = scenario();
  const page = await open(env);
  const rows = page.rows('daily');
  assert.deepEqual(rows[0], ['Day', 'Sessions', 'Peak online', 'Peak in matches', 'Peak advertised', 'Advertised', 'Matches', 'Rejects',
    'Fights started', 'Fights completed', 'Avg fights/match', 'Direct matches', 'Relay matches', 'Direct fights', 'Relay fights']);
  assert.deepEqual(rows[1].slice(8), ['6', '5', '3.0', '1', '1', '4', '2']);
});

test('the two day charts are drawn apart from the concurrency chart', async () => {
  const { env } = scenario();
  const page = await open(env);
  const bars = (id) => page.nodes.get(id).find((n) => n.tag === 'path' && /\bbar\b/.test(n.attrs.class)).length;
  assert.equal(bars('fights'), 2);
  assert.equal(bars('matches-by-day'), 4);
  assert.match(page.text('fights'), /StartedCompleted/);
  assert.match(page.text('matches-by-day'), /AdvertisedStartedDirectRelay/);
  assert.ok(!/Fights/.test(page.text('usage')));
});

const values = (page, id) => page.nodes.get(id).find((n) => n.tag === 'text' && n.attrs.class === 'value').map((n) => n.textContent);
const fakeDay = (i, extra) => ({ day: new Date(Date.UTC(2026, 0, 1 + i)).toISOString().slice(0, 10), sessions: 0, advertised: 0,
  matches_started: 0, incompatible_rejects: 0, peak_online: 0, peak_in_matches: 0, peak_advertised: 0, fights_started: 0,
  fights_completed: 0, fight_draws: 0, direct_matches: 0, relay_matches: 0, direct_fights: 0, relay_fights: 0, ...extra });

async function rangeOf(env, days, extra) {
  const base = env.hub.metricsHistory(30, 20);
  const daily = Array.from({ length: days }, (_, i) => fakeDay(i, extra(i)));
  return open(env, { history: { ...base, days, daily } });
}

test('the bar charts print the value over each non-zero bar and nothing over empty ones', async () => {
  const { env } = scenario();
  const page = await rangeOf(env, 7, (i) => ({ sessions: [169, 0, 19, 3, 0, 1204, 0][i], fights_started: i === 2 ? 188 : 0,
    fights_completed: i === 2 ? 19 : 0, advertised: i === 4 ? 3 : 0 }));
  assert.deepEqual(values(page, 'sessions'), ['169', '19', '3', '1,204']);
  assert.deepEqual(values(page, 'fights'), ['188', '19']);
  assert.deepEqual(values(page, 'matches-by-day'), ['3']);
  for (const id of ['sessions', 'fights', 'matches-by-day']) assert.ok(!values(page, id).includes('0'));
});

test('each series value sits over its own bar in the legend colour', async () => {
  const { env } = scenario();
  const page = await rangeOf(env, 7, (i) => (i === 3 ? { fights_started: 8, fights_completed: 5 } : {}));
  const root = page.nodes.get('fights');
  const bars = root.find((n) => n.tag === 'path' && /\bbar\b/.test(n.attrs.class));
  assert.deepEqual(bars.map((n) => n.attrs.class), ['bar f1', 'bar f3']);
  const [a, b] = root.find((n) => n.tag === 'text' && n.attrs.class === 'value');
  assert.ok(a.attrs.x < b.attrs.x);
  assert.ok(a.attrs.y < b.attrs.y);
});

test('the value labels thin out on long ranges instead of overlapping', async () => {
  const { env } = scenario();
  const shown = async (days) => {
    const page = await rangeOf(env, days, () => ({ sessions: 120, fights_started: 120, matches_started: 120, advertised: 120 }));
    return ['sessions', 'fights', 'matches-by-day'].map((id) => values(page, id).length);
  };
  assert.deepEqual(await shown(7), [7, 7, 14]);
  assert.deepEqual(await shown(30), [30, 0, 0]);
  assert.deepEqual(await shown(90), [0, 0, 0]);
  assert.deepEqual(await shown(365), [0, 0, 0]);
  const wide = await rangeOf(env, 90, () => ({ sessions: 5, fights_started: 5, advertised: 5 }));
  assert.equal(values(wide, 'sessions').length, 90);
  assert.equal(values(wide, 'fights').length, 0);
});

test('hovering a day still shows its date and every series value', async () => {
  const { env } = scenario();
  const page = await rangeOf(env, 30, (i) => (i === 0 ? { fights_started: 8, fights_completed: 5 } : {}));
  const hit = page.nodes.get('fights').find((n) => n.tag === 'rect' && n.attrs.class === 'hit')[0];
  assert.ok(hit.listeners.mousemove.length > 0 && hit.listeners.mouseleave.length > 0);
  hit.listeners.mousemove[0]({ clientX: 10, clientY: 10 });
  assert.match(page.text('tip'), /1 Jan.*Started: 8.*Completed: 5/s);
});

const section = (page, id) => page.nodes.get(id).textContent;

test('protection counters hold the accepted and merged events and the diagnostics hold the refusals', async () => {
  const { env } = scenario();
  const history = env.hub.metricsHistory(30, 20);
  history.dimensions.abuse = [
    { value: 'hello_accepted', count: 9 }, { value: 'ws_upgrade_accepted', count: 8 }, { value: 'hello_reconnect_deduped', count: 2 },
    { value: 'pm_host_accepted', count: 3 }, { value: 'invite_limited', count: 4 }, { value: 'ws_upgrade_limited', count: 5 },
    { value: 'relay_abuse_closed', count: 1 }, { value: 'hello_limited', count: 6 },
  ];
  const page = await open(env, { history });
  assert.deepEqual(page.rows('protection').map((r) => r[0]).sort(), ['Player Matches advertised', 'Reconnects not counted as new sessions',
    'Sign-ins accepted', 'WebSocket upgrades accepted']);
  assert.deepEqual(page.rows('abuse').map((r) => r[0]).sort(), ['Invites refused', 'Relay connections or relays closed for abuse',
    'Sign-ins refused: too many', 'WebSocket upgrades refused at the edge']);
  assert.ok(!/accepted|advertised|Reconnects/.test(section(page, 'abuse')));
  assert.ok(!/refused|closed/.test(section(page, 'protection')));
});

test('an empty abuse panel says so and the panels name the selected range', async () => {
  const { env } = scenario();
  const history = env.hub.metricsHistory(30, 20);
  history.dimensions.abuse = [{ value: 'hello_accepted', count: 9 }];
  const page = await open(env, { history });
  assert.match(section(page, 'abuse'), /No abuse events in this range/);
  assert.match(section(page, 'abuse'), /last 30 days/);
  assert.match(section(page, 'protection'), /last 30 days/);
  assert.ok(!/None/.test(section(page, 'abuse')));
  history.dimensions.abuse = [];
  const none = await open(env, { history });
  assert.match(section(none, 'protection'), /No protection activity in this range/);
  const week = await openDashboard({ summary: env.hub.metricsSummary(), history: { ...history, days: 7 } });
  assert.match(section(week, 'abuse'), /last 7 days/);
});

test('stored abuse rows of an older label set stay readable', async () => {
  const { env } = scenario();
  const history = env.hub.metricsHistory(30, 20);
  history.dimensions.abuse = [{ value: 'some_future_counter', count: 2 }];
  const page = await open(env, { history });
  assert.deepEqual(page.rows('abuse'), [['some_future_counter', '2']]);
});

test('nothing player-identifying reaches the page', async () => {
  const { env, one, two } = scenario();
  const page = await open(env);
  const text = page.all();
  for (const secret of [one.match, two.match, one.a.who.player, one.b.who.player, one.a.who.token, one.a.fields.name, ip(1), ip(2),
    'token_hash', 'relay_token']) {
    assert.ok(!text.includes(secret), secret);
  }
  assert.ok(!/innerHTML|eval\(/.test(ADMIN_JS));
  assert.ok(/series-4/.test(ADMIN_CSS));
});
