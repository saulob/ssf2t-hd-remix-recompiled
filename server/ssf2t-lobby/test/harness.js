// In-process hub clients for the protocol tests: no sockets, a clock the test moves.
import { Hub } from '../src/hub.js';
import { GAME_ID, PROTOCOL, playerIdFor } from '../src/protocol.js';
import { LocalConn, LocalHost, sqliteStore } from '../local/host.js';

export function identity(n) {
  const token = n.toString(16).padStart(64, 'a');
  return { token, player: playerIdFor(token) };
}

export function makeHub({ local = false, db } = {}) {
  const clock = { now: 1_700_000_000_000 };
  const logs = [];
  const host = new LocalHost(sqliteStore(db), { now: () => clock.now, log: (text) => logs.push(text) });
  const hub = new Hub(host, { local });
  return {
    hub,
    host,
    clock,
    logs,
    advance(ms) {
      clock.now += ms;
      if (host.alarmAt !== null && host.alarmAt <= clock.now) {
        host.alarmAt = null;
        hub.alarm();
      }
    },
    connect(who, options) {
      return new Client(hub, host, who, options);
    },
  };
}

export class Client {
  constructor(hub, host, who, { ip = '203.0.113.7', guest = 'base013', build = 1, name, version = '0.2.2', platform = 'Windows',
    presence = true } = {}) {
    this.hub = hub;
    this.host = host;
    this.who = who;
    this.fields = { guest, build, name: name ?? `P${who.player.slice(0, 3)}`, version, platform, presence };
    this.inbox = [];
    this.binary = [];
    this.closedWith = null;
    this.seq = 0;
    const first = hub.admit(ip);
    this.admitted = Boolean(first);
    if (!first) return;
    this.conn = new LocalConn({
      send: (text) => this.inbox.push(JSON.parse(text)),
      sendBinary: (bytes) => {
        if (this.failBinary) throw new Error('socket gone');
        this.binary.push(Uint8Array.from(bytes));
      },
      get bufferedAmount() {
        return 0;
      },
      close: (code, reason) => {
        this.closedWith = { code, reason };
      },
    });
    this.conn.setAttachment(first);
    host.conns.add(this.conn);
  }

  raw(text) {
    const before = this.inbox.length;
    this.hub.message(this.conn, text);
    const replies = this.inbox.splice(before);
    const response = replies.find((m) => 're' in m);
    this.inbox.push(...replies.filter((m) => m !== response));
    return response;
  }

  relay(bytes) {
    const before = this.inbox.length;
    this.hub.relay(this.conn, bytes);
    return this.inbox.splice(before);
  }

  call(op, body = {}) {
    return this.raw(JSON.stringify({ op, seq: ++this.seq, ...body }));
  }

  hello(extra = {}) {
    return this.call('hello', { proto: PROTOCOL, game: GAME_ID, player: this.who.player, token: this.who.token,
      ...this.fields, ...extra });
  }

  events(name) {
    const taken = this.inbox.filter((m) => m.ev === name);
    this.inbox = this.inbox.filter((m) => m.ev !== name);
    return taken;
  }

  drop() {
    this.hub.closed(this.conn);
    this.host.conns.delete(this.conn);
  }
}

export function greeted(env, n, options) {
  const client = env.connect(identity(n), options);
  client.hello();
  return client;
}

export function befriend(a, b) {
  a.call('friends.add', { code: b.who.player });
  b.call('friends.accept', { player: a.who.player });
  a.inbox = [];
  b.inbox = [];
}
