// Cloudflare Worker with one SQLite-backed Durable Object holding the online hub (README.md). WebSockets use the
// Hibernation API: "ping" is answered by the runtime without waking the object, and every socket's state is its
// attachment. Matches between two friends are rows in the hub, not objects of their own. Text frames are signalling;
// binary frames are relay frames of the sender's match.
import { DurableObject } from 'cloudflare:workers';

import { COUNTRY_HEADER, handleAdmin, internalIncidentRoute, internalMetricsRoute, isAdminPath, limitedCount } from './admin.js';
import { handleUpgrade } from './edge.js';
import { Hub } from './hub.js';
import { INCIDENT } from './incidents.js';
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
    // Every wake builds the hub again; knowing the stored alarm keeps it from being written again each time.
    ctx.blockConcurrencyWhile(async () => {
      this.hub = new Hub({
        sql: {
          all: (query, ...args) => sql.exec(query, ...args).toArray(),
          get: (query, ...args) => sql.exec(query, ...args).toArray()[0],
          run: (query, ...args) => {
            sql.exec(query, ...args);
          },
          transaction: (work) => ctx.storage.transactionSync(work),
        },
        sockets: () => ctx.getWebSockets().map((ws) => new WorkerConn(ws)),
        now: () => Date.now(),
        setAlarm: (ms) => ctx.storage.setAlarm(ms),
        log: (text) => console.log(text),
      }, { alarmAt: await ctx.storage.getAlarm() });
    });
  }

  async fetch(request) {
    const incident = internalIncidentRoute(request);
    if (incident?.kind === 'list') return Response.json(this.hub.incidentList());
    if (incident?.kind === 'one') {
      const found = this.hub.incidentForAdmin(incident.ticket);
      return found ? Response.json(found) : new Response('Not found', { status: 404 });
    }
    if (incident?.kind === 'status') {
      const saved = this.hub.setIncidentStatus(incident.ticket, await request.json());
      return saved ? Response.json(saved) : new Response('Not found', { status: 404 });
    }
    const metrics = internalMetricsRoute(request);
    if (metrics?.kind === 'summary') return Response.json(this.hub.metricsSummary());
    if (metrics?.kind === 'history') return Response.json(this.hub.metricsHistory(metrics.days, metrics.recent));
    if (metrics) return new Response('Bad request', { status: 400 });
    this.hub.edgeLimited(limitedCount(request.headers));
    const first = this.hub.admit(request.headers.get('CF-Connecting-IP') ?? '', request.headers.get(COUNTRY_HEADER));
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
    if (typeof message !== 'string' || message.length > INCIDENT.frameChars) {
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
    const lobby = () => env.LOBBY.get(env.LOBBY.idFromName('global'));
    if (isAdminPath(url.pathname)) {
      const internal = (path) => lobby().fetch(`https://lobby.internal${path}`).then((r) => {
        if (!r.ok) throw new Error(`lobby answered ${r.status}`);
        return r.json();
      });
      const report = (path, init) => lobby().fetch(`https://lobby.internal${path}`, init).then((r) => {
        if (r.status === 404) return null;
        if (!r.ok) throw new Error(`lobby answered ${r.status}`);
        return r.json();
      });
      return handleAdmin(request, env, {
        summary: () => internal('/metrics/summary'),
        history: (days, recent) => internal(`/metrics/history?days=${days}&recent=${recent}`),
        incidents: () => internal('/incidents'),
        incident: (ticket) => report(`/incidents/${ticket}`),
        setIncidentStatus: (ticket, change) => report(`/incidents/${ticket}/status`, { method: 'POST', body: JSON.stringify(change) }),
      }, (text) => console.log(text));
    }
    if (request.headers.get('Upgrade')?.toLowerCase() === 'websocket') {
      return handleUpgrade(request, env, lobby());
    }
    if (request.method === 'GET' && (url.pathname === '/' || url.pathname === '/health')) {
      return json({ service: SERVICE, protocol: PROTOCOL, status: 'ok', worker_build: WORKER_BUILD,
        relay_supported: true, relay_max_payload: RELAY.maxPayload });
    }
    return json({ error: 'not_found' }, 404);
  },
};
