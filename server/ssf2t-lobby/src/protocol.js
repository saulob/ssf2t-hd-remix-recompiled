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
  matchMaxMs: 4 * 3600 * 1000,
};

export const BUCKETS = {
  any: { burst: 60, perSecond: 20 },
  social: { burst: 10, perSecond: 1 },
};

export const WORKER_BUILD = '2026-10-31-relay1';

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
};

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
  };
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
