// Mutation check for the fight telemetry: each mutation breaks one thing in a source file, the tests must fail, and the
// file is put back byte for byte. Not a *.test.js file, so `node --test test/*.test.js` does not run it.
//   node test/mutations.mjs node    the Worker and the dashboard (Node only)
//   node test/mutations.mjs cpp     the client reporter, guest reads and roster (needs a C++ compiler)
// Exit 0 when every mutation was detected and every file restored, 1 otherwise, 77 when the compiler is missing.
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { delimiter, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const lobby = fileURLToPath(new URL('..', import.meta.url));
const repo = join(lobby, '..', '..');
const file = (path) => join(repo, path);
const sha = (text) => createHash('sha256').update(text).digest('hex');

const WORKER_TESTS = ['test/fights.test.js', 'test/recent.test.js', 'test/dashboard.test.js', 'test/admin.test.js', 'test/wire.test.js',
  'test/analytics.test.js'];

const NODE_MUTATIONS = [
  { id: 'remove Base013 telemetry', file: 'server/ssf2t-lobby/src/protocol.js',
    find: "if (!GUESTS.includes(message.build)) fail('bad_request', 'build expected');",
    replace: "if (message.build !== 'tu023') fail('bad_request', 'build expected');" },
  { id: 'remove TU023 telemetry', file: 'server/ssf2t-lobby/src/protocol.js',
    find: "if (!GUESTS.includes(message.build)) fail('bad_request', 'build expected');",
    replace: "if (message.build !== 'base013') fail('bad_request', 'build expected');" },
  { id: 'duplicate start increments twice', file: 'server/ssf2t-lobby/src/hub.js',
    find: "if (fight) return { counted: false, reason: 'duplicate' };\n      if (tele.fights_started",
    replace: "if (fight) { this.track(() => this.metrics.count('fights_started')); return { counted: true }; }\n      if (tele.fights_started" },
  { id: 'duplicate completion increments twice', file: 'server/ssf2t-lobby/src/hub.js',
    find: "if (fight.completed) return { counted: false, reason: 'duplicate' };",
    replace: "if (fight.completed) { this.track(() => this.metrics.count('fights_completed')); return { counted: true }; }" },
  { id: 'invert winner/loser', file: 'server/ssf2t-lobby/src/metrics.js',
    find: "const winner = result === 'p1' ? p1 : p2;",
    replace: "const winner = result === 'p1' ? p2 : p1;" },
  { id: 'remove character mapping', file: 'server/ssf2t-lobby/src/characters.js',
    find: 'export const characterName = (id) => CHARACTERS[id] ??', replace: 'export const characterName = (id) => undefined ??' },
  { id: 'accept unknown match', file: 'server/ssf2t-lobby/src/hub.js',
    find: "const row = att.match && this.matchRow(att.match);\n    if (!row || row.id !== msg.match) fail('not_found');\n    const tele = this.telemetryOf(row.id);\n    if (!tele) fail('not_found');",
    replace: "const row = att.match && this.matchRow(att.match);\n    if (!row) return { counted: true };\n    const tele = this.telemetryOf(row.id);\n    if (!tele) return { counted: true };" },
  { id: 'remove Direct aggregation', file: 'server/ssf2t-lobby/src/metrics.js',
    find: 'if (column in entry) entry[column] = r.count;', replace: "if (column in entry && !column.startsWith('direct')) entry[column] = r.count;" },
  { id: 'remove Relay aggregation', file: 'server/ssf2t-lobby/src/metrics.js',
    find: 'if (column in entry) entry[column] = r.count;', replace: "if (column in entry && !column.startsWith('relay')) entry[column] = r.count;" },
  { id: 'remove Fights today', file: 'server/ssf2t-lobby/src/admin_page.js',
    find: "    $('t-fights').textContent = fmt(t.fights_started);\n", replace: '' },
  { id: 'remove Most wins', file: 'server/ssf2t-lobby/src/admin_page.js',
    find: '<div class="panel"><h2>Most wins</h2><div id="d-wins"></div></div>', replace: '' },
  { id: 'remove Most losses', file: 'server/ssf2t-lobby/src/admin_page.js',
    find: '<div class="panel"><h2>Most losses</h2><div id="d-losses"></div></div>', replace: '' },
  { id: 'remove Last matches', file: 'server/ssf2t-lobby/src/admin_page.js',
    find: '    recentTable(history.recent);\n', replace: '' },
  { id: 'remove recent-history retention limit', file: 'server/ssf2t-lobby/src/metrics.js',
    find: "    this.sql.run(`DELETE FROM recent_matches WHERE seq NOT IN\n      (SELECT seq FROM recent_matches ORDER BY seq DESC LIMIT ?)`, RECENT_MATCHES_MAX);\n",
    replace: '' },
  { id: 'leaving with no fight is Abandoned', file: 'server/ssf2t-lobby/src/hub.js',
    find: "else if (!reported || !tele.fights_started) state = 'Ended';", replace: "else if (!reported) state = 'Ended';" },
  { id: 'matchup pair is not canonical', file: 'server/ssf2t-lobby/src/metrics.js',
    find: 'day, Math.min(p1, p2), Math.max(p1, p2));', replace: 'day, p1, p2);' },
  { id: 'duplicate start increments matchup', file: 'server/ssf2t-lobby/src/hub.js',
    find: "if (fight) return { counted: false, reason: 'duplicate' };\n      if (tele.fights_started",
    replace: "if (fight) { this.track(() => this.metrics.fightStarted({ p1: event.p1, p2: event.p2, transport: 'direct', guest: row.guest, platforms: [] })); return { counted: false, reason: 'duplicate' }; }\n      if (tele.fights_started" },
  { id: 'duplicate completion increments matchup winner', file: 'server/ssf2t-lobby/src/hub.js',
    find: "if (fight.completed) return { counted: false, reason: 'duplicate' };",
    replace: "if (fight.completed) { this.track(() => this.metrics.fightCompleted({ p1: event.p1, p2: event.p2, result: event.result })); return { counted: false, reason: 'duplicate' }; }" },
  { id: 'most played shows 9 instead of 10', file: 'server/ssf2t-lobby/src/admin_page.js',
    find: 'var TOP = 10;', replace: 'var TOP = 9;' },
  { id: 'matchup sort reversed', file: 'server/ssf2t-lobby/src/metrics.js',
    find: 'ORDER BY fights DESC, a, b`', replace: 'ORDER BY fights ASC, a, b`' },
  { id: 'matchup win mapping inverted', file: 'server/ssf2t-lobby/src/metrics.js',
    find: "winner === Math.min(p1, p2) ? 'a_wins' : 'b_wins'", replace: "winner === Math.min(p1, p2) ? 'b_wins' : 'a_wins'" },
  { id: 'matchup selected range ignored', file: 'server/ssf2t-lobby/src/metrics.js',
    find: 'FROM metrics_daily_matchups WHERE day >= ? AND day <= ? GROUP BY a, b',
    replace: 'FROM metrics_daily_matchups WHERE day >= ? AND day <= ? OR 1 GROUP BY a, b' },
  { id: 'expose forbidden player-identifying field in recent matches', file: 'server/ssf2t-lobby/src/hub.js',
    find: 'platforms: `${tele.host_platform} x ${tele.join_platform}`,',
    replace: 'platforms: `${tele.host_platform} x ${tele.join_platform} ${row.id}`,' },
];

const CPP_MUTATIONS = [
  { id: 'remove Base013 telemetry (client)', file: 'src/netplay/guest_netplay.h',
    find: 'kFighterBlockPointer = 0x83E09CC0;', replace: 'kFighterBlockPointer = 0x83E09CC4;' },
  { id: 'remove TU023 telemetry (client)', file: 'src/netplay/guest_netplay.h',
    find: 'kFighterBlockPointer = 0x83E0A1D8;', replace: 'kFighterBlockPointer = 0x83E0A1DC;' },
  { id: 'duplicate start emits twice (client)', file: 'src/online/fight_telemetry.cpp',
    find: '  if (has_last_ && generation == last_generation_ && fight == last_fight_) return;\n', replace: '' },
  { id: 'invert winner/loser (client)', file: 'src/online/fight_telemetry.cpp',
    find: '  if (first) return FightResult::kPlayer1;\n  if (second) return FightResult::kPlayer2;',
    replace: '  if (first) return FightResult::kPlayer2;\n  if (second) return FightResult::kPlayer1;' },
  { id: 'remove character mapping (client)', file: 'src/online/fight_telemetry.cpp',
    find: 'const char* CharacterName(int id) { return IsCharacterId(id) ? kRoster[id] : ""; }',
    replace: 'const char* CharacterName(int id) { return (void)id, ""; }' },
  { id: 'report offline fights (client)', file: 'src/online/fight_telemetry.h',
    find: 'return online_driven && game_mode == online_mode;', replace: 'return true;' },
  { id: 'report an unfinished fight as complete (client)', file: 'src/online/fight_telemetry.cpp',
    find: '  if (result == FightResult::kNone) return;\n  result_ = result;', replace: '  result_ = result;' },
  { id: 'require the end characters to equal the start (client)', file: 'src/online/fight_telemetry.cpp',
    find: 'const FightResult result = ResultOf(snapshot);',
    replace: 'const FightResult result = snapshot.character[0] == character_[0] && snapshot.character[1] == character_[1] ? ResultOf(snapshot) : FightResult::kNone;' },
  { id: 'report characters before the presentation (client)', file: 'src/online/fight_telemetry.cpp',
    find: 'if (!identity_ready || !snapshot.valid ||', replace: 'if (!snapshot.valid ||' },
  { id: 'accept an unconfirmed result as final (client)', file: 'src/online/fight_telemetry.cpp',
    find: 'candidate_ != FightResult::kNone && confirmed >= candidate_frame_', replace: 'candidate_ != FightResult::kNone' },
];

function snapshot(mutations) {
  return new Map([...new Set(mutations.map((m) => m.file))].map((path) => [path, readFileSync(file(path), 'utf8')]));
}

function withMutation(mutation, originals, run) {
  const original = originals.get(mutation.file);
  const eol = original.includes('\r\n') ? '\r\n' : '\n';
  const text = original.replaceAll('\r\n', '\n');
  if (!text.includes(mutation.find)) throw new Error(`mutation "${mutation.id}": text not found in ${mutation.file}`);
  writeFileSync(file(mutation.file), text.replace(mutation.find, () => mutation.replace).replaceAll('\n', eol));
  try {
    return run();
  } finally {
    writeFileSync(file(mutation.file), original);
  }
}

function report(results, originals) {
  let bad = 0;
  for (const [id, detected] of results) {
    console.log(`MUTATION ${detected ? 'DETECTED' : 'SURVIVED'} ${id}`);
    if (!detected) bad++;
  }
  for (const [path, text] of originals) {
    if (sha(readFileSync(file(path), 'utf8')) !== sha(text)) {
      console.log(`MUTATION RESTORE FAILED ${path}`);
      bad++;
    }
  }
  console.log(`${results.length} mutations, ${bad} not detected or not restored`);
  return bad ? 1 : 0;
}

function runNode() {
  const originals = snapshot(NODE_MUTATIONS);
  const test = (tests) => spawnSync(process.execPath, ['--test', ...tests], { cwd: lobby, encoding: 'utf8' });
  const baseline = test(WORKER_TESTS);
  if (baseline.status !== 0) {
    console.log(baseline.stdout.slice(-2000));
    console.log('MUTATION baseline tests fail without any mutation');
    return 1;
  }
  const results = NODE_MUTATIONS.map((m) => [m.id, withMutation(m, originals, () => test(WORKER_TESTS).status !== 0)]);
  return report(results, originals);
}

function findCompiler() {
  const candidates = [process.env.SSF2T_MUTATION_CXX, 'C:/msys64/clang64/bin/clang++.exe', 'C:/msys64/mingw64/bin/g++.exe', 'g++', 'c++',
    'clang++'].filter(Boolean);
  for (const candidate of candidates) {
    const probe = spawnSync(candidate, ['--version'], { encoding: 'utf8' });
    if (probe.status === 0) return candidate;
  }
  return null;
}

function onlineSources() {
  const lines = readFileSync(file('CMakeLists.txt'), 'utf8').split('\n');
  const start = lines.findIndex((l) => l.startsWith('set(SSF2T_ONLINE_SOURCES'));
  const sources = [];
  for (let i = start + 1; lines[i] !== undefined && !lines[i].startsWith(')'); i++) {
    const match = /(src\/\S+\.cpp)/.exec(lines[i]);
    if (match) sources.push(match[1]);
  }
  sources.push(process.platform === 'win32' ? 'src/online/websocket_win.cpp' : 'src/online/websocket_posix.cpp');
  return sources;
}

function runCpp() {
  const cxx = findCompiler();
  if (!cxx) {
    console.log('MUTATION NOT RUN: no C++ compiler found (set SSF2T_MUTATION_CXX)');
    return 77;
  }
  const originals = snapshot(CPP_MUTATIONS);
  const work = mkdtempSync(join(tmpdir(), 'ssf2t-mutation-'));
  const flags = ['-std=c++23', '-O0', `-I${file('src')}`, `-I${file('tests')}`, `-DSSF2T_SOURCE_DIR="${repo.replaceAll('\\', '/')}"`];
  const libs = process.platform === 'win32' ? ['-lwinhttp', '-lws2_32', '-lbcrypt'] : ['-ldl', '-lpthread'];
  const env = { ...process.env, PATH: `${dirname(cxx)}${delimiter}${process.env.PATH}` };
  const run = (exe) => spawnSync(exe, { encoding: 'utf8', env });
  const compile = (source, out) => spawnSync(cxx, [...flags, '-c', file(source), '-o', out], { encoding: 'utf8' });
  try {
    const objects = new Map();
    const sources = [...onlineSources(), 'tests/fight_telemetry_tests.cpp'];
    for (const source of sources) {
      const out = join(work, `${objects.size}.o`);
      const built = compile(source, out);
      if (built.status !== 0) {
        console.log(built.stderr.slice(0, 2000));
        console.log(`MUTATION baseline does not compile: ${source}`);
        return 1;
      }
      objects.set(source, out);
    }
    const link = (objs, exe) => spawnSync(cxx, [...objs, '-o', exe, ...libs], { encoding: 'utf8' });
    const baselineExe = join(work, 'baseline.exe');
    const linked = link([...objects.values()], baselineExe);
    const ran = linked.status === 0 ? run(baselineExe) : linked;
    if (ran.status !== 0) {
      console.log(`${cxx}: ${(ran.stdout ?? '').slice(-1500)}${(ran.stderr ?? '').slice(0, 1500)}`);
      console.log('MUTATION baseline C++ tests fail without any mutation');
      return 1;
    }
    const results = CPP_MUTATIONS.map((m) => [m.id, withMutation(m, originals, () => {
      // Headers reach every object that includes them: rebuild the test and the mutated source.
      const rebuilt = new Map(objects);
      const touched = new Set(['tests/fight_telemetry_tests.cpp', ...(m.file.endsWith('.cpp') ? [m.file] : [])]);
      if (m.file === 'src/online/fight_telemetry.h') for (const s of sources) if (s.includes('online/')) touched.add(s);
      for (const source of touched) {
        const out = join(work, `m-${rebuilt.size}-${source.replaceAll('/', '_')}.o`);
        const built = compile(source, out);
        if (built.status !== 0) return true;
        rebuilt.set(source, out);
      }
      const exe = join(work, 'mutant.exe');
      if (link([...rebuilt.values()], exe).status !== 0) return true;
      return run(exe).status !== 0;
    })]);
    return report(results, originals);
  } finally {
    rmSync(work, { recursive: true, force: true });
    for (const [path, text] of originals) if (existsSync(file(path))) writeFileSync(file(path), text);
  }
}

const mode = process.argv[2];
if (mode === 'node') process.exit(runNode());
else if (mode === 'cpp') process.exit(runCpp());
else {
  console.log('usage: node test/mutations.mjs node|cpp');
  process.exit(2);
}
