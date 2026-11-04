// Two paired players and the host's fight events, for the fight telemetry tests.
import { metricsDay } from '../src/metrics.js';
import { befriend, identity } from './harness.js';

export const SECRET_ON = { supports: { akuma: true, evil_ryu: true }, enabled: { akuma: true, evil_ryu: true } };

// `secret` is both players' capabilities; secretA and secretB override one side.
export function startFightMatch(env, { nA = 1, nB = 2, guest = 'base013', platformA = 'Windows', platformB = 'Windows',
  telemetry = true, ipA = '203.0.113.1', ipB = '203.0.113.2', secret, secretA = secret, secretB = secret } = {}) {
  const a = env.connect(identity(nA), { guest, platform: platformA, ip: ipA });
  const b = env.connect(identity(nB), { guest, platform: platformB, ip: ipB });
  a.hello({ relay: true, ...(telemetry ? { telemetry: true } : {}), ...(secretA ? { secret: secretA } : {}) });
  b.hello({ relay: true, ...(telemetry ? { telemetry: true } : {}), ...(secretB ? { secret: secretB } : {}) });
  befriend(a, b);
  const hosted = a.call('pm.host', {});
  const joined = b.call('pm.join', { session: hosted.session });
  a.inbox = [];
  b.inbox = [];
  return { a, b, match: joined.match, guest };
}

export function fightEvent(m, kind, fight, extra = {}) {
  return { match: m.match, kind, fight, p1: 0, p2: 4, build: m.guest, transport: 'direct', platform: 'Windows',
    ...(kind === 'complete' ? { result: 'p1' } : {}), ...extra };
}

export const send = (client, event) => client.call('telemetry.fight', event);

export function playFight(m, fight, { p1 = 0, p2 = 4, result = 'p1', transport = 'direct', complete = true } = {}) {
  const start = send(m.a, fightEvent(m, 'start', fight, { p1, p2, transport }));
  const done = complete ? send(m.a, fightEvent(m, 'complete', fight, { p1, p2, result, transport })) : null;
  return { start, done };
}

export const daily = (env, column) => env.host.sql.get(`SELECT ${column} AS n FROM metrics_daily WHERE day = ?`,
  metricsDay(env.clock.now))?.n ?? 0;

export const dimension = (env, name) => Object.fromEntries(env.host.sql.all(
  'SELECT value, count FROM metrics_daily_dimensions WHERE day = ? AND dimension = ?', metricsDay(env.clock.now), name)
  .map((r) => [r.value, r.count]));

export const characters = (env) => Object.fromEntries(env.host.sql.all(
  'SELECT character, picks, wins, losses, draws FROM metrics_daily_characters WHERE day = ?', metricsDay(env.clock.now))
  .map((r) => [r.character, { picks: r.picks, wins: r.wins, losses: r.losses, draws: r.draws }]));
