// Cloudflare Worker with one SQLite-backed Durable Object holding the online hub (README.md). WebSockets use the
// Hibernation API: "ping" is answered by the runtime without waking the object, and every socket's state is its
// attachment. Matches between two friends are rows in the hub, not objects of their own. Text frames are signalling;
// binary frames are relay frames of the sender's match.
import { DurableObject } from 'cloudflare:workers';

import { Hub } from './hub.js';
import { LIMITS, PROTOCOL, RELAY, SERVICE, WORKER_BUILD } from './protocol.js';

class WorkerConn {
  constructor(ws) {
    this.ws = ws;
  }

  attachment() {
    return this.ws.deserializeAttachment() ?? null;
  }

  setAttachment(value) {
    try {
      this.ws.serializeAttachment(value);
    } catch {
      // A socket that already closed keeps no state.
    }
  }

  send(text) {
    this.ws.send(text);
  }

  sendBinary(bytes) {
    this.ws.send(bytes);
  }

  get bufferedAmount() {
    return this.ws.bufferedAmount;
  }

  close(code, reason) {
    this.ws.close(code, reason);
  }
}

export class SSF2TLobby extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env);
    ctx.setWebSocketAutoResponse(new WebSocketRequestResponsePair('ping', 'pong'));
    const sql = ctx.storage.sql;
    this.hub = new Hub({
      sql: {
        all: (query, ...args) => sql.exec(query, ...args).toArray(),
        get: (query, ...args) => sql.exec(query, ...args).toArray()[0],
        run: (query, ...args) => {
          sql.exec(query, ...args);
        },
      },
      sockets: () => ctx.getWebSockets().map((ws) => new WorkerConn(ws)),
      now: () => Date.now(),
      setAlarm: (ms) => ctx.storage.setAlarm(ms),
      log: (text) => console.log(text),
    });
  }

  async fetch(request) {
    const first = this.hub.admit(request.headers.get('CF-Connecting-IP') ?? '');
    if (!first) return new Response('Too many connections from this address', { status: 429 });
    const [client, server] = Object.values(new WebSocketPair());
    this.ctx.acceptWebSocket(server);
    server.serializeAttachment(first);
    return new Response(null, { status: 101, webSocket: client });
  }

  webSocketMessage(ws, message) {
    if (message instanceof ArrayBuffer) {
      this.hub.relay(new WorkerConn(ws), new Uint8Array(message));
      return;
    }
    if (typeof message !== 'string' || message.length > LIMITS.frameBytes) {
      ws.close(1009, 'text frames up to the size limit only');
      return;
    }
    this.hub.message(new WorkerConn(ws), message);
  }

  webSocketClose(ws, code) {
    this.hub.closed(new WorkerConn(ws));
    try {
      ws.close(code === 1005 ? 1000 : code, 'closed');
    } catch {
      // Already closed.
    }
  }

  webSocketError(ws) {
    this.hub.closed(new WorkerConn(ws));
  }

  alarm() {
    this.hub.alarm();
  }
}

const json = (body, status = 200) =>
  Response.json(body, { status, headers: { 'X-Content-Type-Options': 'nosniff', 'Cache-Control': 'no-store' } });

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (request.headers.get('Upgrade')?.toLowerCase() === 'websocket') {
      return env.LOBBY.get(env.LOBBY.idFromName('global')).fetch(request);
    }
    if (request.method === 'GET' && (url.pathname === '/' || url.pathname === '/health')) {
      return json({ service: SERVICE, protocol: PROTOCOL, status: 'ok', worker_build: WORKER_BUILD,
        relay_supported: true, relay_max_payload: RELAY.maxPayload });
    }
    return json({ error: 'not_found' }, 404);
  },
};
