import { SECRET_KEYS, isCharacterId } from './characters.js';
import { PLATFORMS } from './metrics.js';
import { sha256Hex } from './sha256.js';

export const SERVICE = 'ssf2t-lobby';
export const PROTOCOL = 3;
export const GAME_ID = 'ssf2t-hd-remix-pc';
export const GUESTS = ['base013', 'tu023'];

export const LIMITS = {
  frameBytes: 4096,
  nameChars: 16,
  versionChars: 16,
  platformChars: 16,
  lanCandidates: 4,
  friends: 200,
  pendingOut: 50,
  pendingIn: 50,
  requestDays: 14,
  registrationsPerIpPerDay: 20,
  socketsPerIp: 16,
  graceMs: 30000,
  playerMatchMs: 180000,
  inviteMs: 60000,
  ruleFields: 8,
  searchResults: 20,
  lobbyPlayers: 50,
  matchMaxMs: 4 * 3600 * 1000,
  fightsPerMatch: 200,
};

export const BUCKETS = {
  any: { burst: 60, perSecond: 20 },
  social: { burst: 10, perSecond: 1 },
};

export const WORKER_BUILD = '2026-11-06-lobby';

// Binary relay frames: magic "SR", version, type, token (u32), sequence (u32), payload length (u16), payload.
export const RELAY = {
  version: 1,
  data: 1,
  headerBytes: 14,
  maxPayload: 1472,
  connection: { packets: { burst: 800, perSecond: 400 }, bytes: { burst: 256 * 1024, perSecond: 128 * 1024 } },
  match: { packets: { burst: 1600, perSecond: 800 }, bytes: { burst: 512 * 1024, perSecond: 256 * 1024 } },
  overrunsToClose: 256,
  invalid: { burst: 200, perSecond: 5 },
  bufferedBytes: 256 * 1024,
  loggedForwards: 3,
};

export const CAPABILITIES = {
  protocol_version: PROTOCOL,
  worker_build: WORKER_BUILD,
  public_endpoints: true,
  relay_supported: true,
  relay_max_payload: RELAY.maxPayload,
  fight_telemetry: true,
  secret_characters: true,
  public_lobby: true,
};

export { SECRET_KEYS };
// Mask bit per flag. Masks are stored in the matches table, so assigned bits never move and rows from before a character
// read as false for it.
const SECRET_BITS = { akuma: { supports: 0, enabled: 2 }, evil_ryu: { supports: 1, enabled: 3 },
  violent_ken: { supports: 4, enabled: 5 } };

const isPlainObject = (v) => v !== null && typeof v === 'object' && !Array.isArray(v);

// Anything that is not exactly true is false, and a character is enabled only if it is supported.
export function normalizeSecret(value) {
  const section = (name) => (isPlainObject(value) && isPlainObject(value[name]) ? value[name] : {});
  const supports = section('supports');
  const enabled = section('enabled');
  const out = { supports: {}, enabled: {} };
  for (const key of SECRET_KEYS) {
    out.supports[key] = supports[key] === true;
    out.enabled[key] = out.supports[key] && enabled[key] === true;
  }
  return out;
}

export function secretMask(secret) {
  const s = normalizeSecret(secret);
  let mask = 0;
  for (const key of SECRET_KEYS) {
    if (s.supports[key]) mask |= 1 << SECRET_BITS[key].supports;
    if (s.enabled[key]) mask |= 1 << SECRET_BITS[key].enabled;
  }
  return mask;
}

export function secretFromMask(mask) {
  const bit = (n) => Number.isInteger(mask) && (mask & (1 << n)) !== 0;
  const out = { supports: {}, enabled: {} };
  for (const key of SECRET_KEYS) {
    out.supports[key] = bit(SECRET_BITS[key].supports);
    out.enabled[key] = out.supports[key] && bit(SECRET_BITS[key].enabled);
  }
  return out;
}

// What both seats can use in a match; the same for either of them.
export function effectiveSecret(a, b) {
  const x = normalizeSecret(a);
  const y = normalizeSecret(b);
  return Object.fromEntries(SECRET_KEYS.map((key) => [key,
    x.supports[key] && y.supports[key] && x.enabled[key] && y.enabled[key]]));
}

// The `secret` dimension values for one match that both seats are ready in: per character available or an option
// mismatch, and unsupported_peer once when an old client meets a peer that has the character on.
export function secretEstablishment(a, b) {
  const x = normalizeSecret(a);
  const y = normalizeSecret(b);
  const values = [];
  let unsupported = false;
  for (const key of SECRET_KEYS) {
    if (x.supports[key] && y.supports[key]) {
      if (x.enabled[key] && y.enabled[key]) values.push(`${key}_available`);
      else if (x.enabled[key] !== y.enabled[key]) values.push(`${key}_option_mismatch`);
    } else if (x.supports[key] !== y.supports[key] && (x.supports[key] ? x : y).enabled[key]) {
      unsupported = true;
    }
  }
  if (unsupported) values.push('unsupported_peer');
  return values;
}

// The header of one relay frame, or the reason it is not one. The payload is never looked at.
export function parseRelayFrame(bytes) {
  if (!(bytes instanceof Uint8Array)) return { error: 'not_binary' };
  if (bytes.length < 4) return { error: 'truncated' };
  if (bytes[0] !== 0x53 || bytes[1] !== 0x52) return { error: 'bad_magic' };
  if (bytes[2] !== RELAY.version) return { error: 'bad_version' };
  if (bytes[3] !== RELAY.data) return { error: 'bad_type' };
  if (bytes.length < RELAY.headerBytes) return { error: 'truncated' };
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const length = view.getUint16(12);
  if (length > RELAY.maxPayload || bytes.length > RELAY.headerBytes + RELAY.maxPayload) return { error: 'oversized' };
  if (length === 0 || bytes.length !== RELAY.headerBytes + length) return { error: 'bad_length' };
  return { type: bytes[3], token: view.getUint32(4), seq: view.getUint32(8), length };
}

export function encodeRelayFrame(token, seq, payload) {
  const bytes = new Uint8Array(RELAY.headerBytes + payload.length);
  const view = new DataView(bytes.buffer);
  bytes.set([0x53, 0x52, RELAY.version, RELAY.data]);
  view.setUint32(4, token >>> 0);
  view.setUint32(8, seq >>> 0);
  view.setUint16(12, payload.length);
  bytes.set(payload, RELAY.headerBytes);
  return bytes;
}

const PLAYER_ALPHABET = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';

export class ProtocolError extends Error {
  constructor(code, detail = '') {
    super(detail || code);
    this.code = code;
  }
}

export const fail = (code, detail) => {
  throw new ProtocolError(code, detail);
};

export const tokenHash = (token) => sha256Hex(`ssf2t-token:${token}`);

// The public id is also the friend code. Must match DerivePlayerId in src/online/identity.cpp.
export function playerIdFor(token) {
  const digest = sha256Hex(`ssf2t-player-id:${token}`);
  let id = '';
  for (let i = 0; i < 8; i++) {
    id += PLAYER_ALPHABET[parseInt(digest.slice(i * 2, i * 2 + 2), 16) % PLAYER_ALPHABET.length];
    if (i === 3) id += '-';
  }
  return id;
}

export function normalizeFriendCode(value) {
  if (typeof value !== 'string') fail('bad_request', 'code expected');
  const compact = value.toUpperCase().replace(/[\s-]/g, '');
  if (compact.length !== 8 || [...compact].some((c) => !PLAYER_ALPHABET.includes(c))) fail('bad_request', 'malformed code');
  return `${compact.slice(0, 4)}-${compact.slice(4)}`;
}

export function cleanName(value) {
  if (typeof value !== 'string') fail('bad_request', 'name expected');
  const name = value.replace(/[^A-Za-z0-9_-]/g, '').slice(0, LIMITS.nameChars);
  return name || 'Player';
}

export function cleanLabel(value, maxChars) {
  if (value === undefined) return '';
  if (typeof value !== 'string') fail('bad_request', 'text expected');
  return value.replace(/[^A-Za-z0-9 ._+-]/g, '').slice(0, maxChars);
}

export function parseHello(message) {
  if (message.proto !== PROTOCOL) fail('proto', 'protocol version not supported');
  if (message.game !== GAME_ID) fail('bad_game', 'unknown game');
  if (!GUESTS.includes(message.guest)) fail('bad_guest', 'unknown guest version');
  if (!Number.isInteger(message.build) || message.build < 0 || message.build > 0xffff) {
    fail('bad_request', 'build expected');
  }
  if (typeof message.token !== 'string' || !/^[0-9a-f]{64}$/.test(message.token)) {
    fail('bad_request', 'token expected');
  }
  if (message.player !== playerIdFor(message.token)) fail('unauthorized', 'player id does not match');
  return {
    player: message.player,
    hash: tokenHash(message.token),
    name: cleanName(message.name ?? 'Player'),
    guest: message.guest,
    build: message.build,
    version: cleanLabel(message.version, LIMITS.versionChars),
    platform: cleanLabel(message.platform, LIMITS.platformChars),
    presence: message.presence !== false,
    relay: message.relay === true,
    telemetry: message.telemetry === true,
    secret: normalizeSecret(message.secret),
  };
}

export function parseLobbyPublish(message) {
  if (typeof message.visible !== 'boolean') fail('bad_request', 'visible expected');
  return message.visible;
}

// The revision the client already holds, if it sent one.
export function parseLobbyRevision(message) {
  if (message.rev === undefined) return null;
  if (!Number.isSafeInteger(message.rev) || message.rev < 0) fail('bad_request', 'rev expected');
  return message.rev;
}

export function ipv4Kind(ip) {
  const m = typeof ip === 'string' ? /^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/.exec(ip) : null;
  if (!m) return null;
  const [a, b, c, d] = m.slice(1).map(Number);
  if ([a, b, c, d].some((n) => n > 255)) return null;
  if (a === 127) return 'loopback';
  if (a === 10 || (a === 172 && b >= 16 && b <= 31) || (a === 192 && b === 168) || (a === 169 && b === 254)) {
    return 'private';
  }
  return 'public';
}

export function parseEndpoint(message) {
  if (!Number.isInteger(message.port) || message.port < 1024 || message.port > 65535) {
    fail('bad_request', 'port expected');
  }
  const lan = message.lan ?? [];
  if (!Array.isArray(lan) || lan.length > LIMITS.lanCandidates) fail('bad_request', 'lan expected');
  for (const ip of lan) {
    const kind = ipv4Kind(ip);
    if (kind !== 'loopback' && kind !== 'private') fail('bad_request', 'lan address expected');
  }
  // The sender's gameplay socket as STUN saw it: the endpoint a peer elsewhere can punch. Only the pair means anything,
  // and this service never sees UDP, so it passes it on as the sender's own claim.
  let publicIp = '';
  let publicPort = 0;
  if (message.public_ip !== undefined || message.public_port !== undefined) {
    if (typeof message.public_ip !== 'string' || ipv4Kind(message.public_ip) !== 'public') {
      fail('bad_request', 'public_ip expected');
    }
    if (!Number.isInteger(message.public_port) || message.public_port < 1 || message.public_port > 65535) {
      fail('bad_request', 'public_port expected');
    }
    publicIp = message.public_ip;
    publicPort = message.public_port;
  }
  return { port: message.port, lan: [...lan], publicIp, publicPort };
}

// Per-address limits group IPv6 clients by their /64, whatever the spelling.
export function ipGroup(ip) {
  if (typeof ip !== 'string' || !ip.includes(':')) return String(ip);
  const mapped = /^::ffff:(\d{1,3}(?:\.\d{1,3}){3})$/i.exec(ip);
  if (mapped) return mapped[1];
  const [head, tail] = ip.split('::');
  const before = head ? head.split(':') : [];
  const after = tail ? tail.split(':') : [];
  const gap = tail === undefined ? [] : Array(Math.max(0, 8 - before.length - after.length)).fill('0');
  return [...before, ...gap, ...after].slice(0, 4).map((g) => g.toLowerCase().replace(/^0+(?=.)/, '')).join(':');
}
export const isLocalAddress = (ip) => ip === '::1' || ipv4Kind(ip) === 'loopback';

export function compatibleGuests(a, b) {
  return Boolean(a && b) && a.guest === b.guest && a.build === b.build;
}

const FIGHT_KINDS = ['start', 'complete'];
export const FIGHT_TRANSPORTS = ['direct', 'relay'];
export const FIGHT_RESULTS = ['p1', 'p2', 'draw'];

// One passive fight event from the host: validated here, matched with its match and fight by the hub.
export function parseFightEvent(message) {
  if (!FIGHT_KINDS.includes(message.kind)) fail('bad_request', 'kind expected');
  if (typeof message.match !== 'string') fail('bad_request', 'match expected');
  if (!Number.isInteger(message.fight) || message.fight < 0 || message.fight >= LIMITS.fightsPerMatch) {
    fail('bad_request', 'fight index expected');
  }
  if (!isCharacterId(message.p1) || !isCharacterId(message.p2)) fail('bad_request', 'character expected');
  if (!GUESTS.includes(message.build)) fail('bad_request', 'build expected');
  if (!FIGHT_TRANSPORTS.includes(message.transport)) fail('bad_request', 'transport expected');
  if (message.platform !== undefined && !PLATFORMS.includes(message.platform)) fail('bad_request', 'platform expected');
  if (message.kind === 'complete' && !FIGHT_RESULTS.includes(message.result)) fail('bad_request', 'result expected');
  return { kind: message.kind, match: message.match, fight: message.fight, p1: message.p1, p2: message.p2,
    build: message.build, transport: message.transport, result: message.kind === 'complete' ? message.result : '' };
}
