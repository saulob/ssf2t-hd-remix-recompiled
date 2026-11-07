// Aggregate service metrics for the private dashboard (README.md, Telemetry): daily counters, daily counts of sessions
// per platform, version and country, daily counts of the transport each match chose, daily fight and character counts,
// 15-minute usage samples and a bounded list of recently closed matches. No row names a player, an address or a match,
// and nothing here keeps per-session state: live numbers come from the hub's sockets and tables.
import { SECRET_CHARACTER, SECRET_KEYS, characterName } from './characters.js';


export const METRICS_TIME_ZONE = 'America/Sao_Paulo';
export const SAMPLE_MS = 15 * 60 * 1000;
export const SAMPLE_DAYS = 90;
export const HISTORY_MAX_DAYS = 365;
export const DIMENSIONS = ['platform', 'version', 'country', 'reject', 'transport', 'fight_transport', 'fight_guest',
  'fight_platform', 'abuse', 'secret', 'secret_fight', 'secret_match'];
export const DIAGNOSTICS = new Set(['ws_upgrade_accepted', 'ws_upgrade_limited', 'ws_upgrade_ip_cap', 'hello_accepted',
  'hello_reconnect_deduped', 'hello_limited', 'pm_host_accepted', 'pm_host_limited', 'pm_join_limited', 'invite_limited',
  'match_resume_limited', 'relay_abuse_closed', 'fight_secret_rejected', 'lobby_list_limited',
  'incident_filed', 'incident_limited', 'incident_hourly_limited', 'incident_daily_limited', 'incident_lookup_limited']);
export const PLATFORMS = ['Windows', 'Linux', 'Steam Deck'];

const DAY_MS = 24 * 3600 * 1000;
// Bounds what a client can add to the table: further new versions in a day count as Other.
const VALUES_PER_DIMENSION_PER_DAY = 64;
const CLEANUP_ROWS = 500;
const COUNTERS = new Set(['sessions', 'advertised', 'matches_started', 'incompatible_rejects', 'fights_started',
  'fights_completed', 'fight_draws']);
export const RECENT_MATCHES_MAX = 50;
export const RECENT_MATCHES_DAYS = 30;
export const RECENT_MATCHES_DEFAULT = 20;
const DAILY_COLUMNS = `sessions, advertised, matches_started, incompatible_rejects, peak_online, peak_in_matches,
  peak_advertised, fights_started, fights_completed, fight_draws`;
const ADDED_DAILY_COLUMNS = ['fights_started', 'fights_completed', 'fight_draws'];

export const METRICS_SCHEMA = [
  `CREATE TABLE IF NOT EXISTS metrics_daily (day TEXT PRIMARY KEY, sessions INTEGER NOT NULL DEFAULT 0,
     advertised INTEGER NOT NULL DEFAULT 0, matches_started INTEGER NOT NULL DEFAULT 0,
     incompatible_rejects INTEGER NOT NULL DEFAULT 0, peak_online INTEGER NOT NULL DEFAULT 0,
     peak_in_matches INTEGER NOT NULL DEFAULT 0, peak_advertised INTEGER NOT NULL DEFAULT 0)`,
  `CREATE TABLE IF NOT EXISTS metrics_daily_dimensions (day TEXT NOT NULL, dimension TEXT NOT NULL, value TEXT NOT NULL,
     count INTEGER NOT NULL, PRIMARY KEY (day, dimension, value))`,
  `CREATE TABLE IF NOT EXISTS metrics_samples (bucket INTEGER PRIMARY KEY, online INTEGER NOT NULL,
     in_matches INTEGER NOT NULL, advertised INTEGER NOT NULL)`,
  `CREATE TABLE IF NOT EXISTS metrics_daily_characters (day TEXT NOT NULL, character INTEGER NOT NULL,
     picks INTEGER NOT NULL DEFAULT 0, wins INTEGER NOT NULL DEFAULT 0, losses INTEGER NOT NULL DEFAULT 0,
     draws INTEGER NOT NULL DEFAULT 0, PRIMARY KEY (day, character))`,
  `CREATE TABLE IF NOT EXISTS metrics_daily_matchups (day TEXT NOT NULL, a INTEGER NOT NULL, b INTEGER NOT NULL,
     fights INTEGER NOT NULL DEFAULT 0, a_wins INTEGER NOT NULL DEFAULT 0, b_wins INTEGER NOT NULL DEFAULT 0,
     draws INTEGER NOT NULL DEFAULT 0, PRIMARY KEY (day, a, b))`,
  `CREATE TABLE IF NOT EXISTS recent_matches (seq INTEGER PRIMARY KEY AUTOINCREMENT, ended INTEGER NOT NULL,
     duration_ms INTEGER NOT NULL, fights_started INTEGER, fights_completed INTEGER, transport TEXT NOT NULL,
     guest TEXT NOT NULL, platforms TEXT NOT NULL, state TEXT NOT NULL)`,
];

const dayFormat = new Intl.DateTimeFormat('en-CA', { timeZone: METRICS_TIME_ZONE, year: 'numeric', month: '2-digit',
  day: '2-digit' });

export function metricsDay(ms) {
  const parts = Object.fromEntries(dayFormat.formatToParts(ms).map((p) => [p.type, p.value]));
  return `${parts.year}-${parts.month}-${parts.day}`;
}

function daysBefore(day, n) {
  const [y, m, d] = day.split('-').map(Number);
  return new Date(Date.UTC(y, m - 1, d - n)).toISOString().slice(0, 10);
}

export const sampleBucket = (ms) => ms - (ms % SAMPLE_MS);

export const platformCategory = (text) => (PLATFORMS.includes(text) ? text : 'Other');

export function versionCategory(text) {
  if (!text) return 'Unknown';
  return /^\d{1,4}(\.\d{1,4}){1,3}$/.test(text) ? text : 'Other';
}

// Cloudflare's country code, or Unknown (XX is Cloudflare's own unknown, T1 is Tor).
export function countryCode(text) {
  return typeof text === 'string' && /^[A-Z]{2}$/.test(text) && text !== 'XX' ? text : 'Unknown';
}

const sorted = (counts) => [...counts].map(([value, count]) => ({ value, count }))
  .sort((a, b) => b.count - a.count || a.value.localeCompare(b.value));

export class Metrics {
  constructor(sql, now) {
    this.sql = sql;
    this.now = now;
    for (const statement of METRICS_SCHEMA) sql.run(statement);
    for (const column of ADDED_DAILY_COLUMNS) {
      try { sql.run(`ALTER TABLE metrics_daily ADD COLUMN ${column} INTEGER NOT NULL DEFAULT 0`); } catch { /* already there */ }
    }
    this.peaks = null;
    this.sample = null;
    this.pending = new Map();
  }

  // Abuse diagnostics are counted in memory and written together by flushDiagnostics; true when this is the first
  // pending one.
  diagnose(name, amount = 1) {
    if (!DIAGNOSTICS.has(name)) throw new Error(`unknown diagnostic ${name}`);
    const first = this.pending.size === 0;
    this.pending.set(name, (this.pending.get(name) ?? 0) + amount);
    return first;
  }

  flushDiagnostics() {
    if (!this.pending.size) return;
    const day = metricsDay(this.now());
    const counts = [...this.pending];
    this.pending.clear();
    for (const [name, count] of counts) {
      this.sql.run(`INSERT INTO metrics_daily_dimensions (day, dimension, value, count) VALUES (?, 'abuse', ?, ?)
        ON CONFLICT(day, dimension, value) DO UPDATE SET count = count + excluded.count`, day, name, count);
    }
  }

  count(column, day = metricsDay(this.now())) {
    if (!COUNTERS.has(column)) throw new Error(`unknown counter ${column}`);
    this.sql.run(`INSERT INTO metrics_daily (day, ${column}) VALUES (?, 1)
      ON CONFLICT(day) DO UPDATE SET ${column} = ${column} + 1`, day);
  }

  addDimension(day, dimension, value) {
    const known = this.sql.get('SELECT 1 AS x FROM metrics_daily_dimensions WHERE day = ? AND dimension = ? AND value = ?',
      day, dimension, value);
    if (!known) {
      const values = this.sql.get('SELECT COUNT(*) AS n FROM metrics_daily_dimensions WHERE day = ? AND dimension = ?',
        day, dimension).n;
      if (values >= VALUES_PER_DIMENSION_PER_DAY) value = 'Other';
    }
    this.sql.run(`INSERT INTO metrics_daily_dimensions (day, dimension, value, count) VALUES (?, ?, ?, 1)
      ON CONFLICT(day, dimension, value) DO UPDATE SET count = count + 1`, day, dimension, value);
  }

  // One connection that became an online session; `client` is its platform, version and country as reported.
  session(client) {
    const day = metricsDay(this.now());
    this.count('sessions', day);
    this.addDimension(day, 'platform', platformCategory(client.platform));
    this.addDimension(day, 'version', versionCategory(client.version));
    this.addDimension(day, 'country', countryCode(client.country));
  }

  transport(choice) {
    this.addDimension(metricsDay(this.now()), 'transport', choice);
  }

  fightStarted({ p1, p2, transport, guest, platforms }) {
    const day = metricsDay(this.now());
    this.count('fights_started', day);
    this.addDimension(day, 'fight_transport', transport);
    this.addDimension(day, 'fight_guest', guest);
    for (const platform of platforms) this.addDimension(day, 'fight_platform', platformCategory(platform));
    for (const character of [p1, p2]) this.character(day, character, 'picks');
    this.matchup(day, p1, p2, 'fights');
    for (const value of secretFightValues(p1, p2)) this.addDimension(day, 'secret_fight', value);
  }

  // `values`: secretEstablishment of the match's two seats, counted when the match is first established.
  secretEstablished(values) {
    const day = metricsDay(this.now());
    for (const value of values) this.addDimension(day, 'secret', value);
  }

  // `key`: a secret character that appeared in a fight of the match for the first time.
  secretMatch(key) {
    this.addDimension(metricsDay(this.now()), 'secret_match', key);
  }

  // `result`: p1, p2 or draw.
  fightCompleted({ p1, p2, result }) {
    const day = metricsDay(this.now());
    this.count('fights_completed', day);
    if (result === 'draw') {
      this.count('fight_draws', day);
      this.matchup(day, p1, p2, 'draws');
      for (const character of [p1, p2]) this.character(day, character, 'draws');
      return;
    }
    const winner = result === 'p1' ? p1 : p2;
    this.matchup(day, p1, p2, winner === Math.min(p1, p2) ? 'a_wins' : 'b_wins');
    this.character(day, winner, 'wins');
    this.character(day, result === 'p1' ? p2 : p1, 'losses');
  }

  matchup(day, p1, p2, column) {
    this.sql.run(`INSERT INTO metrics_daily_matchups (day, a, b, ${column}) VALUES (?, ?, ?, 1)
      ON CONFLICT(day, a, b) DO UPDATE SET ${column} = ${column} + 1`, day, Math.min(p1, p2), Math.max(p1, p2));
  }

  character(day, id, column) {
    this.sql.run(`INSERT INTO metrics_daily_characters (day, character, ${column}) VALUES (?, ?, 1)
      ON CONFLICT(day, character) DO UPDATE SET ${column} = ${column} + 1`, day, id);
  }

  // A closed match; the fight counts are null when its host reported no fight telemetry.
  recordMatch(match) {
    const now = this.now();
    this.sql.run(`INSERT INTO recent_matches (ended, duration_ms, fights_started, fights_completed, transport, guest,
      platforms, state) VALUES (?, ?, ?, ?, ?, ?, ?, ?)`, now, match.duration_ms, match.fights_started,
    match.fights_completed, match.transport, match.guest, match.platforms, match.state);
    this.trimRecent(now);
  }

  trimRecent(now) {
    this.sql.run('DELETE FROM recent_matches WHERE ended < ?', now - RECENT_MATCHES_DAYS * DAY_MS);
    this.sql.run(`DELETE FROM recent_matches WHERE seq NOT IN
      (SELECT seq FROM recent_matches ORDER BY seq DESC LIMIT ?)`, RECENT_MATCHES_MAX);
  }

  reject(reason) {
    const day = metricsDay(this.now());
    this.count('incompatible_rejects', day);
    this.addDimension(day, 'reject', reason);
  }

  // Raises today's peaks and the current sample to `live` ({ online, in_matches, advertised }); writes only what grew.
  observe(live) {
    const now = this.now();
    const day = metricsDay(now);
    if (this.peaks?.day !== day) {
      const row = this.sql.get('SELECT peak_online, peak_in_matches, peak_advertised FROM metrics_daily WHERE day = ?', day);
      this.peaks = row ? { day, online: row.peak_online, in_matches: row.peak_in_matches, advertised: row.peak_advertised }
        : { day, online: -1, in_matches: -1, advertised: -1 };
    }
    if (grew(this.peaks, live)) {
      this.sql.run(`INSERT INTO metrics_daily (day, peak_online, peak_in_matches, peak_advertised) VALUES (?, ?, ?, ?)
        ON CONFLICT(day) DO UPDATE SET peak_online = MAX(peak_online, excluded.peak_online),
        peak_in_matches = MAX(peak_in_matches, excluded.peak_in_matches), peak_advertised = MAX(peak_advertised, excluded.peak_advertised)`,
      day, live.online, live.in_matches, live.advertised);
      raise(this.peaks, live);
    }
    const bucket = sampleBucket(now);
    if (this.sample?.bucket !== bucket) {
      const row = this.sql.get('SELECT online, in_matches, advertised FROM metrics_samples WHERE bucket = ?', bucket);
      this.sample = row ? { bucket, ...row } : { bucket, online: -1, in_matches: -1, advertised: -1 };
      this.dropOldSamples(now);
      this.trimRecent(now);
    }
    if (grew(this.sample, live)) {
      this.sql.run(`INSERT INTO metrics_samples (bucket, online, in_matches, advertised) VALUES (?, ?, ?, ?)
        ON CONFLICT(bucket) DO UPDATE SET online = MAX(online, excluded.online),
        in_matches = MAX(in_matches, excluded.in_matches), advertised = MAX(advertised, excluded.advertised)`,
      bucket, live.online, live.in_matches, live.advertised);
      raise(this.sample, live);
    }
  }

  dropOldSamples(now) {
    this.sql.run(`DELETE FROM metrics_samples WHERE bucket IN
      (SELECT bucket FROM metrics_samples WHERE bucket < ? ORDER BY bucket LIMIT ?)`, now - SAMPLE_DAYS * DAY_MS,
    CLEANUP_ROWS);
  }

  // `live`: the counts, Maps of platform, version and country to players online and of transport to matches, and the match list.
  summary(live) {
    const now = this.now();
    const day = metricsDay(now);
    const today = { ...emptyDay(), ...this.sql.get(`SELECT ${DAILY_COLUMNS} FROM metrics_daily WHERE day = ?`, day),
      ...this.transportCounts(day, day).get(day) };
    return {
      generated: now,
      time_zone: METRICS_TIME_ZONE,
      day,
      live: { ...live, platform: sorted(live.platform), version: sorted(live.version), country: sorted(live.country),
        transport: sorted(live.transport) },
      today: { ...today, peak_online: Math.max(today.peak_online, live.online),
        peak_in_matches: Math.max(today.peak_in_matches, live.in_matches), peak_advertised: Math.max(today.peak_advertised, live.advertised) },
    };
  }

  // Per day, the matches and fights each transport carried.
  transportCounts(first, last) {
    const days = new Map();
    for (const r of this.sql.all(`SELECT day, dimension, value, count FROM metrics_daily_dimensions
      WHERE day >= ? AND day <= ? AND dimension IN ('transport', 'fight_transport')`, first, last)) {
      const entry = days.get(r.day) ?? { direct_matches: 0, relay_matches: 0, direct_fights: 0, relay_fights: 0 };
      const column = `${r.value}_${r.dimension === 'transport' ? 'matches' : 'fights'}`;
      if (column in entry) entry[column] = r.count;
      days.set(r.day, entry);
    }
    return days;
  }

  // The closed matches of the range, newest first, with how many the range holds in all.
  recent(first, limit) {
    const since = Math.max(Date.parse(`${first}T00:00:00Z`) - DAY_MS, this.now() - RECENT_MATCHES_DAYS * DAY_MS);
    const rows = this.sql.all(`SELECT ended, duration_ms, fights_started, fights_completed, transport, guest, platforms,
      state FROM recent_matches WHERE ended >= ? ORDER BY seq DESC`, since)
      .filter((r) => metricsDay(r.ended) >= first);
    return { limit, retention: { matches: RECENT_MATCHES_MAX, days: RECENT_MATCHES_DAYS }, total: rows.length,
      matches: rows.slice(0, limit) };
  }

  history(days, recentLimit = RECENT_MATCHES_DEFAULT) {
    if (!Number.isInteger(days) || days < 1 || days > HISTORY_MAX_DAYS) throw new RangeError('days out of range');
    if (!Number.isInteger(recentLimit) || recentLimit < 1 || recentLimit > RECENT_MATCHES_MAX) {
      throw new RangeError('recent out of range');
    }
    const now = this.now();
    const last = metricsDay(now);
    const first = daysBefore(last, days - 1);
    const rows = new Map(this.sql.all(`SELECT day, ${DAILY_COLUMNS} FROM metrics_daily WHERE day >= ? AND day <= ? ORDER BY day`,
      first, last).map((r) => [r.day, r]));
    const transports = this.transportCounts(first, last);
    const daily = [];
    for (let i = days - 1; i >= 0; i--) {
      const day = daysBefore(last, i);
      daily.push({ day, ...emptyDay(), ...rows.get(day), ...transports.get(day) });
    }
    const dimensions = Object.fromEntries(DIMENSIONS.map((d) => [d, []]));
    for (const r of this.sql.all(`SELECT dimension, value, SUM(count) AS count FROM metrics_daily_dimensions
      WHERE day >= ? AND day <= ? GROUP BY dimension, value ORDER BY count DESC, value`, first, last)) {
      dimensions[r.dimension]?.push({ value: r.value, count: r.count });
    }
    const characters = this.sql.all(`SELECT character AS id, SUM(picks) AS picks, SUM(wins) AS wins, SUM(losses) AS losses,
      SUM(draws) AS draws FROM metrics_daily_characters WHERE day >= ? AND day <= ? GROUP BY character
      ORDER BY picks DESC, id`, first, last).map((r) => ({ ...r, name: characterName(r.id) }));
    const matchups = this.sql.all(`SELECT a, b, SUM(fights) AS fights, SUM(a_wins) AS a_wins, SUM(b_wins) AS b_wins,
      SUM(draws) AS draws FROM metrics_daily_matchups WHERE day >= ? AND day <= ? GROUP BY a, b
      ORDER BY fights DESC, a, b`, first, last).map((r) => ({ ...r, a_name: characterName(r.a), b_name: characterName(r.b) }));
    const since = Math.max(now - days * DAY_MS, now - SAMPLE_DAYS * DAY_MS);
    const samples = this.sql.all('SELECT bucket, online, in_matches, advertised FROM metrics_samples WHERE bucket >= ? ORDER BY bucket',
      sampleBucket(since));
    return { generated: now, time_zone: METRICS_TIME_ZONE, days, first, last, sample_ms: SAMPLE_MS, daily, dimensions,
      samples, totals: totalsOf(daily, characters), characters, matchups, recent: this.recent(first, recentLimit) };
  }
}

// Per fight start: the fight includes the character, and the mirror and cross pairings of the secret characters.
function secretFightValues(p1, p2) {
  const [a, b] = [SECRET_CHARACTER[p1], SECRET_CHARACTER[p2]];
  const values = [];
  for (const key of SECRET_KEYS) {
    if (a === key || b === key) values.push(key);
    if (a === key && b === key) values.push(`${key}_mirror`);
  }
  if (a && b && a !== b) values.push(SECRET_KEYS.filter((key) => key === a || key === b).join('_vs_'));
  return values;
}

const emptyDay = () => ({ sessions: 0, advertised: 0, matches_started: 0, incompatible_rejects: 0, peak_online: 0,
  peak_in_matches: 0, peak_advertised: 0, fights_started: 0, fights_completed: 0, fight_draws: 0, direct_matches: 0,
  relay_matches: 0, direct_fights: 0, relay_fights: 0 });

const SUMMED = ['sessions', 'advertised', 'matches_started', 'incompatible_rejects', 'fights_started', 'fights_completed',
  'fight_draws', 'direct_matches', 'relay_matches', 'direct_fights', 'relay_fights'];

function totalsOf(daily, characters) {
  const totals = Object.fromEntries(SUMMED.map((k) => [k, daily.reduce((sum, d) => sum + (d[k] ?? 0), 0)]));
  for (const key of ['picks', 'wins', 'losses', 'draws']) {
    totals[`character_${key}`] = characters.reduce((sum, c) => sum + c[key], 0);
  }
  return totals;
}

const grew = (seen, live) => live.online > seen.online || live.in_matches > seen.in_matches || live.advertised > seen.advertised;

function raise(seen, live) {
  seen.online = Math.max(seen.online, live.online);
  seen.in_matches = Math.max(seen.in_matches, live.in_matches);
  seen.advertised = Math.max(seen.advertised, live.advertised);
}
