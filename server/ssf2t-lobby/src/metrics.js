// Aggregate service metrics for the private dashboard (README.md, Telemetry): daily counters, daily counts of sessions
// per platform, version and country, daily counts of the transport each match chose, and 15-minute usage samples. No row
// names a player, an address or a match, and nothing here keeps per-session state: live numbers come from the hub's
// sockets and tables.
export const METRICS_TIME_ZONE = 'America/Sao_Paulo';
export const SAMPLE_MS = 15 * 60 * 1000;
export const SAMPLE_DAYS = 90;
export const HISTORY_MAX_DAYS = 365;
export const DIMENSIONS = ['platform', 'version', 'country', 'reject', 'transport'];
export const PLATFORMS = ['Windows', 'Linux', 'Steam Deck'];

const DAY_MS = 24 * 3600 * 1000;
// Bounds what a client can add to the table: further new versions in a day count as Other.
const VALUES_PER_DIMENSION_PER_DAY = 64;
const CLEANUP_ROWS = 500;
const COUNTERS = new Set(['sessions', 'advertised', 'matches_started', 'incompatible_rejects']);

export const METRICS_SCHEMA = [
  `CREATE TABLE IF NOT EXISTS metrics_daily (day TEXT PRIMARY KEY, sessions INTEGER NOT NULL DEFAULT 0,
     advertised INTEGER NOT NULL DEFAULT 0, matches_started INTEGER NOT NULL DEFAULT 0,
     incompatible_rejects INTEGER NOT NULL DEFAULT 0, peak_online INTEGER NOT NULL DEFAULT 0,
     peak_in_matches INTEGER NOT NULL DEFAULT 0, peak_advertised INTEGER NOT NULL DEFAULT 0)`,
  `CREATE TABLE IF NOT EXISTS metrics_daily_dimensions (day TEXT NOT NULL, dimension TEXT NOT NULL, value TEXT NOT NULL,
     count INTEGER NOT NULL, PRIMARY KEY (day, dimension, value))`,
  `CREATE TABLE IF NOT EXISTS metrics_samples (bucket INTEGER PRIMARY KEY, online INTEGER NOT NULL,
     in_matches INTEGER NOT NULL, advertised INTEGER NOT NULL)`,
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
    this.peaks = null;
    this.sample = null;
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
    const today = this.sql.get(`SELECT sessions, advertised, matches_started, incompatible_rejects, peak_online,
      peak_in_matches, peak_advertised FROM metrics_daily WHERE day = ?`, day) ?? emptyDay();
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

  history(days) {
    if (!Number.isInteger(days) || days < 1 || days > HISTORY_MAX_DAYS) throw new RangeError('days out of range');
    const now = this.now();
    const last = metricsDay(now);
    const first = daysBefore(last, days - 1);
    const rows = new Map(this.sql.all(`SELECT day, sessions, advertised, matches_started, incompatible_rejects, peak_online,
      peak_in_matches, peak_advertised FROM metrics_daily WHERE day >= ? AND day <= ? ORDER BY day`, first, last)
      .map((r) => [r.day, r]));
    const daily = [];
    for (let i = days - 1; i >= 0; i--) {
      const day = daysBefore(last, i);
      daily.push(rows.get(day) ?? { day, ...emptyDay() });
    }
    const dimensions = Object.fromEntries(DIMENSIONS.map((d) => [d, []]));
    for (const r of this.sql.all(`SELECT dimension, value, SUM(count) AS count FROM metrics_daily_dimensions
      WHERE day >= ? AND day <= ? GROUP BY dimension, value ORDER BY count DESC, value`, first, last)) {
      dimensions[r.dimension]?.push({ value: r.value, count: r.count });
    }
    const since = Math.max(now - days * DAY_MS, now - SAMPLE_DAYS * DAY_MS);
    const samples = this.sql.all('SELECT bucket, online, in_matches, advertised FROM metrics_samples WHERE bucket >= ? ORDER BY bucket',
      sampleBucket(since));
    return { generated: now, time_zone: METRICS_TIME_ZONE, days, first, last, sample_ms: SAMPLE_MS, daily, dimensions,
      samples };
  }
}

const emptyDay = () => ({ sessions: 0, advertised: 0, matches_started: 0, incompatible_rejects: 0, peak_online: 0,
  peak_in_matches: 0, peak_advertised: 0 });

const grew = (seen, live) => live.online > seen.online || live.in_matches > seen.in_matches || live.advertised > seen.advertised;

function raise(seen, live) {
  seen.online = Math.max(seen.online, live.online);
  seen.in_matches = Math.max(seen.in_matches, live.in_matches);
  seen.advertised = Math.max(seen.advertised, live.advertised);
}
