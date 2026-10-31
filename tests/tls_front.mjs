// A TLS front for a plain local lobby, for the wss:// tests: node tests/tls_front.mjs KEY CERT LOBBY_PORT [PORT]
// Prints "listening <port>" once it accepts connections on 127.0.0.1.
import { readFileSync } from 'node:fs';
import net from 'node:net';
import tls from 'node:tls';

const [keyFile, certFile, lobbyPort, listenPort = '0'] = process.argv.slice(2);
const server = tls.createServer({ key: readFileSync(keyFile), cert: readFileSync(certFile) }, (client) => {
  const upstream = net.connect(Number(lobbyPort), '127.0.0.1');
  client.pipe(upstream);
  upstream.pipe(client);
  for (const socket of [client, upstream]) socket.on('error', () => { client.destroy(); upstream.destroy(); });
});
server.listen(Number(listenPort), '127.0.0.1', () => console.log(`listening ${server.address().port}`));
