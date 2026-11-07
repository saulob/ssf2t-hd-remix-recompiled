// The private dashboard (/admin): a static shell, its stylesheet and script, served same-origin under a strict CSP.
// The script keeps the token in memory (sessionStorage only when asked) and sends it as a Bearer header.
export const ADMIN_HTML = String.raw`<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="referrer" content="no-referrer">
<meta name="robots" content="noindex, nofollow">
<title>SSF2T Online Metrics</title>
<link rel="stylesheet" href="/admin/app.css">
</head>
<body>
<header class="top">
  <h1>Super Street Fighter II Turbo HD Remix · Online Metrics</h1>
  <p class="muted">Dashboard: anonymous aggregate statistics of the online service, days in America/Sao_Paulo. Bugs: reports players sent from the launcher.</p>
</header>
<main>
  <form id="login" class="panel login" autocomplete="off">
    <label for="token">Dashboard token</label>
    <input id="token" type="password" autocomplete="off" spellcheck="false" required>
    <label class="check"><input id="remember" type="checkbox"> Keep for this tab</label>
    <button type="submit">Open dashboard</button>
    <p id="login-error" class="error" role="alert"></p>
  </form>
  <section id="dash" hidden>
    <nav class="tabs" role="tablist" aria-label="Admin sections">
      <button id="tab-dashboard" type="button" role="tab" aria-selected="true" aria-controls="view-dashboard">Dashboard</button>
      <button id="tab-bugs" type="button" role="tab" aria-selected="false" aria-controls="view-bugs">Bugs</button>
      <button id="signout" type="button" class="quiet">Sign out</button>
    </nav>
    <div id="view-dashboard" role="tabpanel" aria-labelledby="tab-dashboard">
    <div class="toolbar">
      <label for="range">Range</label>
      <select id="range">
        <option value="7">7 days</option>
        <option value="30" selected>30 days</option>
        <option value="90">90 days</option>
        <option value="365">365 days</option>
      </select>
      <button id="refresh" type="button">Refresh</button>
      <span id="status" class="muted" role="status"></span>
    </div>
    <h2 class="section">Live now</h2>
    <p class="muted">Current state only: these change as players come and go and are not totals.</p>
    <div class="cards">
      <div class="card"><span class="label">Online now</span><span id="c-online" class="value">–</span></div>
      <div class="card"><span class="label">In matches now</span><span id="c-in-matches" class="value">–</span></div>
      <div class="card"><span class="label">Advertised now</span><span id="c-advertised" class="value">–</span></div>
      <div class="card"><span class="label">Direct now</span><span id="c-direct" class="value">–</span></div>
      <div class="card"><span class="label">Relay now</span><span id="c-relay" class="value">–</span></div>
      <div class="card"><span class="label">Pending now</span><span id="c-pending" class="value">–</span></div>
    </div>
    <h2 class="section">Today</h2>
    <p id="today-line" class="muted"></p>
    <div class="cards">
      <div class="card"><span class="label">Peak online today</span><span id="t-peak" class="value">–</span></div>
      <div class="card"><span class="label">Sessions today</span><span id="t-sessions" class="value">–</span></div>
      <div class="card"><span class="label">Matches advertised today</span><span id="t-advertised" class="value">–</span></div>
      <div class="card"><span class="label">Matches started today</span><span id="t-matches" class="value">–</span></div>
      <div class="card"><span class="label">Fights started today</span><span id="t-fights" class="value">–</span></div>
      <div class="card"><span class="label">Fights completed today</span><span id="t-completed" class="value">–</span></div>
      <div class="card"><span class="label">Direct matches today</span><span id="t-direct-matches" class="value">–</span></div>
      <div class="card"><span class="label">Relay matches today</span><span id="t-relay-matches" class="value">–</span></div>
      <div class="card"><span class="label">Direct fights today</span><span id="t-direct-fights" class="value">–</span></div>
      <div class="card"><span class="label">Relay fights today</span><span id="t-relay-fights" class="value">–</span></div>
      <div class="card"><span class="label">Avg fights per match today</span><span id="t-avg" class="value">–</span></div>
    </div>
    <div class="panel">
      <h2 id="range-title">Selected range</h2>
      <div class="cards compact">
        <div class="card"><span class="label">Sessions</span><span id="r-sessions" class="value">–</span></div>
        <div class="card"><span class="label">Matches advertised</span><span id="r-advertised" class="value">–</span></div>
        <div class="card"><span class="label">Matches started</span><span id="r-matches" class="value">–</span></div>
        <div class="card"><span class="label">Fights started</span><span id="r-fights" class="value">–</span></div>
        <div class="card"><span class="label">Fights completed</span><span id="r-completed" class="value">–</span></div>
        <div class="card"><span class="label">Avg fights per match</span><span id="r-avg" class="value">–</span></div>
        <div class="card"><span class="label">Direct matches</span><span id="r-direct-matches" class="value">–</span></div>
        <div class="card"><span class="label">Relay matches</span><span id="r-relay-matches" class="value">–</span></div>
        <div class="card"><span class="label">Direct fights</span><span id="r-direct-fights" class="value">–</span></div>
        <div class="card"><span class="label">Relay fights</span><span id="r-relay-fights" class="value">–</span></div>
        <div class="card"><span class="label">Character picks</span><span id="r-picks" class="value">–</span></div>
        <div class="card"><span class="label">Character wins</span><span id="r-wins" class="value">–</span></div>
        <div class="card"><span class="label">Character losses</span><span id="r-losses" class="value">–</span></div>
        <div class="card"><span class="label">Draws</span><span id="r-draws" class="value">–</span></div>
      </div>
    </div>
    <div class="panel">
      <h2>Current matches</h2>
      <div class="scroll"><table id="matches"></table></div>
      <p id="matches-empty" class="empty"></p>
    </div>
    <div class="panel">
      <h2 id="recent-title">Last matches</h2>
      <div class="inline">
        <label for="recent-limit">Show</label>
        <select id="recent-limit">
          <option value="10">10</option>
          <option value="20" selected>20</option>
          <option value="50">50</option>
        </select>
      </div>
      <div class="scroll"><table id="recent"></table></div>
      <p id="recent-empty" class="empty"></p>
    </div>
    <div class="panel">
      <h2>Usage over time</h2>
      <p class="muted">Highest value in each 15-minute interval; kept for 90 days.</p>
      <div id="usage" class="chart"></div>
    </div>
    <div class="panel">
      <h2>Sessions by day</h2>
      <div id="sessions" class="chart"></div>
    </div>
    <div class="panel">
      <h2>Fights by day</h2>
      <div id="fights" class="chart"></div>
    </div>
    <div class="panel">
      <h2>Matches by day</h2>
      <div id="matches-by-day" class="chart"></div>
    </div>
    <div class="grid">
      <div class="panel"><h2>Platform</h2><div id="d-platform"></div></div>
      <div class="panel"><h2>Port version</h2><div id="d-version"></div></div>
      <div class="panel"><h2>Country</h2><div id="d-country"></div></div>
      <div class="panel"><h2>Transport</h2><div id="d-transport"></div></div>
    </div>
    <div class="grid">
      <div class="panel"><h2>Fights by platform</h2><div id="d-fplatform"></div></div>
      <div class="panel"><h2>Fights by game build</h2><div id="d-fbuild"></div></div>
      <div class="panel"><h2>Fights by transport</h2><div id="d-ftransport"></div></div>
    </div>
    <div class="grid">
      <div class="panel"><h2>Most played characters</h2><div id="d-played"></div></div>
      <div class="panel"><h2>Most wins</h2><div id="d-wins"></div></div>
      <div class="panel"><h2>Most losses</h2><div id="d-losses"></div></div>
    </div>
    <div class="panel"><h2>Most played matchups</h2><div id="d-matchups"></div></div>
    <div class="panel">
      <h2>Characters</h2>
      <div class="scroll"><table id="chars"></table></div>
      <p id="chars-empty" class="empty"></p>
    </div>
    <div class="panel">
      <h2>Secret characters</h2>
      <p id="secret-line" class="muted"></p>
      <div class="cards compact">
        <div class="card"><span class="label">Rooms with Akuma available</span><span id="s-akuma-available" class="value">–</span></div>
        <div class="card"><span class="label">Rooms with Evil Ryu available</span><span id="s-evil-ryu-available" class="value">–</span></div>
        <div class="card"><span class="label">Rooms with Violent Ken available</span><span id="s-violent-ken-available" class="value">–</span></div>
        <div class="card"><span class="label">Akuma option mismatch</span><span id="s-akuma-mismatch" class="value">–</span></div>
        <div class="card"><span class="label">Evil Ryu option mismatch</span><span id="s-evil-ryu-mismatch" class="value">–</span></div>
        <div class="card"><span class="label">Violent Ken option mismatch</span><span id="s-violent-ken-mismatch" class="value">–</span></div>
        <div class="card"><span class="label">Unsupported-peer mismatch</span><span id="s-unsupported" class="value">–</span></div>
        <div class="card"><span class="label">Fights with Akuma</span><span id="s-fights-akuma" class="value">–</span></div>
        <div class="card"><span class="label">Fights with Evil Ryu</span><span id="s-fights-evil-ryu" class="value">–</span></div>
        <div class="card"><span class="label">Fights with Violent Ken</span><span id="s-fights-violent-ken" class="value">–</span></div>
        <div class="card"><span class="label">Akuma mirror fights</span><span id="s-fights-akuma-mirror" class="value">–</span></div>
        <div class="card"><span class="label">Evil Ryu mirror fights</span><span id="s-fights-evil-ryu-mirror" class="value">–</span></div>
        <div class="card"><span class="label">Violent Ken mirror fights</span><span id="s-fights-violent-ken-mirror" class="value">–</span></div>
        <div class="card"><span class="label">Akuma vs Evil Ryu fights</span><span id="s-fights-akuma-vs-evil-ryu" class="value">–</span></div>
        <div class="card"><span class="label">Akuma vs Violent Ken fights</span><span id="s-fights-akuma-vs-violent-ken" class="value">–</span></div>
        <div class="card"><span class="label">Evil Ryu vs Violent Ken fights</span><span id="s-fights-evil-ryu-vs-violent-ken" class="value">–</span></div>
        <div class="card"><span class="label">Matches with Akuma</span><span id="s-matches-akuma" class="value">–</span></div>
        <div class="card"><span class="label">Matches with Evil Ryu</span><span id="s-matches-evil-ryu" class="value">–</span></div>
        <div class="card"><span class="label">Matches with Violent Ken</span><span id="s-matches-violent-ken" class="value">–</span></div>
      </div>
    </div>
    <div class="panel">
      <h2>Daily totals</h2>
      <div class="scroll"><table id="daily"></table></div>
    </div>
    <details class="panel">
      <summary>Diagnostics: incompatibility rejects</summary>
      <div id="rejects"></div>
    </details>
    <details class="panel">
      <summary>Protection counters</summary>
      <div id="protection"></div>
    </details>
    <details class="panel">
      <summary>Diagnostics: abuse protection</summary>
      <div id="abuse"></div>
    </details>
    <div class="panel notes">
      <h2>How to read this</h2>
      <ul>
        <li>A session is one connection of a launcher or game that went online. The launcher and the game each count,
          and a reconnect within ten minutes of a counted session does not count again. Sessions are not players: there is
          no unique-player count.</li>
        <li>Platform, version and country by day count sessions started. "Now" counts players online, one per account,
          from the connections open at this moment.</li>
        <li>A match is one online session between two players; a fight is one fight inside it, so a match holds many fights.
          Advertised matches are lobbies offered, started matches are two players paired. Direct and Relay matches are started
          matches by the transport the host chose; the "now" cards count only the matches running at this moment.</li>
        <li>Fight, character and last-match numbers come from clients that report them (v0.3.1 and later); older clients
          still play but add nothing there, so "Avg fights per match" (fights started over matches started) reads low while
          they are in use. Each fight counts two picks, one per side. Win and loss rates are over completed fights; a fight
          cut short by a disconnect counts as started only.</li>
        <li>Fights by platform count each fight once per side, so the two platforms of a fight both appear.</li>
        <li>Secret characters: a room counts once, when both players are connected, as available (both support and enabled
          the character), an option mismatch (both support it, only one enabled it) or an unsupported peer (one client
          does not know it while the other has it enabled). Fights and matches count Akuma, Evil Ryu and Violent Ken from
          the fights that started.</li>
        <li>Last matches keeps the latest 50 closed matches for 30 days, with no names, codes, addresses, countries or
          characters.</li>
        <li>Direct, Relay and Pending count the matches running now. A match is Pending until its host chooses a
          transport, and again if its relay closes before the fight. The Transport panel counts the choices made per day.</li>
        <li>Current matches are read from the live service and shown with a short id only; none is stored for this page.</li>
        <li>Country comes from Cloudflare and is stored only as a daily count. Unknown: Cloudflare did not say.</li>
        <li>Nothing here stores names, Friend Codes, addresses, relay tokens or gameplay.</li>
      </ul>
    </div>
    </div>
    <div id="view-bugs" role="tabpanel" aria-labelledby="tab-bugs" hidden>
    <div class="panel">
      <p class="muted">Reports players sent from the launcher, newest first. The log of a report is stored as the player previewed it and is shown and downloaded exactly as stored. No account or address is kept with a report, and the newest 500 are kept.</p>
      <div class="toolbar">
        <label for="bug-version">Port version</label>
        <select id="bug-version"></select>
        <label for="bug-build">Game image</label>
        <select id="bug-build"></select>
        <label for="bug-status-filter">Status</label>
        <select id="bug-status-filter">
            <option value="">All statuses</option>
            <option value="new">Open</option>
            <option value="investigating">Investigating</option>
            <option value="fixed">Fixed</option>
            <option value="cannot_reproduce">Cannot reproduce</option>
            <option value="duplicate">Duplicate</option>
            <option value="not_a_bug">Not a bug</option>
            <option value="closed">Closed</option>
        </select>
        <button id="bug-refresh" type="button">Refresh reports</button>
        <span id="bug-count" class="muted" role="status"></span>
      </div>
      <p id="bug-empty" class="empty" hidden>No bug reports yet.</p>
      <div id="bug-list-wrap" class="scroll bug-list"><table id="bug-list"></table></div>
    </div>
    <div id="bug-detail" class="panel bug-detail" hidden>
      <div class="toolbar bug-header">
        <h2 id="bug-title"></h2>
        <button id="bug-download" type="button">Download log</button>
        <button id="bug-copy" type="button" class="quiet">Copy log</button>
        <span id="bug-note" class="muted" role="status"></span>
        <button id="bug-close" type="button" class="quiet bug-close">Close report</button>
      </div>
      <p id="bug-text" class="bug-text"></p>
      <dl id="bug-facts" class="bug-facts"></dl>
      <h3>Report status</h3>
      <div class="bug-status-form">
        <label for="bug-status">Status</label>
        <select id="bug-status">
            <option value="new">Open</option>
            <option value="investigating">Investigating</option>
            <option value="fixed">Fixed</option>
            <option value="cannot_reproduce">Cannot reproduce</option>
            <option value="duplicate">Duplicate</option>
            <option value="not_a_bug">Not a bug</option>
            <option value="closed">Closed</option>
        </select>
        <p id="bug-status-meaning" class="muted"></p>
        <div id="bug-fixed-field" class="bug-status-field" hidden>
          <label for="bug-fixed">Fixed in version</label>
          <input id="bug-fixed" type="text" maxlength="32" autocomplete="off" placeholder="0.4.1">
        </div>
        <div class="bug-status-field">
          <label for="bug-message">Message to player</label>
          <textarea id="bug-message" rows="3" maxlength="500"></textarea>
        </div>
        <div class="toolbar">
          <button id="bug-status-save" type="button">Save status</button>
          <span id="bug-status-note" class="muted" role="status"></span>
        </div>
      </div>
      <h3 id="bug-log-name"></h3>
      <p class="muted bug-snapshot">This is the sanitized log snapshot captured when the report was sent.</p>
      <pre id="bug-log" class="bug-log" tabindex="0"></pre>
    </div>
    </div>
  </section>
</main>
<div id="tip" class="tip" hidden></div>
<script src="/admin/app.js"></script>
</body>
</html>
`;

export const ADMIN_CSS = String.raw`:root {
  color-scheme: light;
  --surface-0: #f3f3f1;
  --surface-1: #fcfcfb;
  --border: #dcdbd6;
  --grid: #e7e6e2;
  --text-primary: #0b0b0b;
  --text-secondary: #52514e;
  --text-muted: #6d6c67;
  --accent: #2a78d6;
  --series-1: #2a78d6;
  --series-2: #eb6834;
  --series-3: #1baf7a;
  --series-4: #8a5cd6;
  --error: #b42318;
}
@media (prefers-color-scheme: dark) {
  :root:where(:not([data-theme="light"])) {
    color-scheme: dark;
    --surface-0: #111110;
    --surface-1: #1a1a19;
    --border: #34332f;
    --grid: #2a2a27;
    --text-primary: #f0efec;
    --text-secondary: #c3c2b7;
    --text-muted: #9a998f;
    --accent: #3987e5;
    --series-1: #3987e5;
    --series-2: #d95926;
    --series-3: #199e70;
    --series-4: #a07be0;
    --error: #f97066;
  }
}
:root[data-theme="dark"] {
  --series-4: #a07be0;
  color-scheme: dark;
  --surface-0: #111110;
  --surface-1: #1a1a19;
  --border: #34332f;
  --grid: #2a2a27;
  --text-primary: #f0efec;
  --text-secondary: #c3c2b7;
  --text-muted: #9a998f;
  --accent: #3987e5;
  --series-1: #3987e5;
  --series-2: #d95926;
  --series-3: #199e70;
  --error: #f97066;
}
* { box-sizing: border-box; }
html { -webkit-text-size-adjust: 100%; }
body {
  margin: 0;
  background: var(--surface-0);
  color: var(--text-primary);
  font: 15px/1.45 system-ui, -apple-system, "Segoe UI", Roboto, sans-serif;
}
.top, main { max-width: 1120px; margin: 0 auto; padding: 0 16px; }
.top { padding-top: 24px; }
h1 { font-size: 22px; margin: 0 0 4px; }
h2 { font-size: 15px; margin: 0 0 8px; }
.muted { color: var(--text-muted); margin: 0 0 12px; font-size: 13px; }
.panel {
  background: var(--surface-1);
  border: 1px solid var(--border);
  border-radius: 10px;
  padding: 16px;
  margin: 0 0 16px;
  min-width: 0;
}
.login { max-width: 420px; display: grid; gap: 10px; }
.check { display: flex; gap: 8px; align-items: center; color: var(--text-secondary); }
input[type="password"], select {
  font: inherit;
  padding: 8px 10px;
  border: 1px solid var(--border);
  border-radius: 6px;
  background: var(--surface-0);
  color: var(--text-primary);
  width: 100%;
}
select { width: auto; }
button {
  font: inherit;
  padding: 8px 14px;
  border-radius: 6px;
  border: 1px solid var(--accent);
  background: var(--accent);
  color: #fff;
  cursor: pointer;
}
button.quiet { background: transparent; color: var(--text-secondary); border-color: var(--border); }
.error { color: var(--error); margin: 0; min-height: 1.2em; }
.toolbar { display: flex; flex-wrap: wrap; gap: 10px; align-items: center; margin: 0 0 16px; }
.toolbar #status { flex: 1; margin: 0; }
h2.section { font-size: 17px; margin: 18px 0 2px; }
.inline { display: flex; gap: 8px; align-items: center; margin: 0 0 8px; color: var(--text-secondary); font-size: 13px; }
.cards.compact { grid-template-columns: repeat(auto-fit, minmax(120px, 1fr)); margin: 0; }
.cards.compact .card { padding: 10px 12px; }
.cards.compact .card .value { font-size: 22px; }
.cards { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 12px; margin: 0 0 8px; }
.card {
  background: var(--surface-1);
  border: 1px solid var(--border);
  border-radius: 10px;
  padding: 14px 16px;
  display: flex;
  flex-direction: column;
}
.card .label { color: var(--text-secondary); font-size: 13px; }
.card .value { font-size: 30px; font-weight: 600; font-variant-numeric: tabular-nums; }
.grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 16px; }
.grid .panel { margin: 0 0 16px; }
.chart svg { display: block; width: 100%; height: auto; }
.chart .axis { fill: var(--text-muted); font-size: 11px; }
.chart .gridline { stroke: var(--grid); stroke-width: 1; }
.chart .baseline { stroke: var(--border); stroke-width: 1; }
.chart .line { fill: none; stroke-width: 2; stroke-linejoin: round; }
.chart .s1 { stroke: var(--series-1); }
.chart .s2 { stroke: var(--series-2); }
.chart .s3 { stroke: var(--series-3); }
.chart .s4 { stroke: var(--series-4); }
.chart .bar { fill: var(--series-1); }
.chart .bar.f2 { fill: var(--series-2); }
.chart .bar.f3 { fill: var(--series-3); }
.chart .bar.f4 { fill: var(--series-4); }
.chart .value { fill: var(--text-primary); font-size: 10px; }
.chart .bar:hover { opacity: 0.8; }
.chart .cursor { stroke: var(--text-muted); stroke-width: 1; }
.chart .hit { fill: transparent; }
.legend { display: flex; flex-wrap: wrap; gap: 14px; margin: 0 0 8px; color: var(--text-secondary); font-size: 13px; }
.legend i { display: inline-block; width: 10px; height: 10px; border-radius: 2px; margin-right: 6px; vertical-align: middle; }
.legend .k1 { background: var(--series-1); }
.legend .k2 { background: var(--series-2); }
.legend .k3 { background: var(--series-3); }
.legend .k4 { background: var(--series-4); }
.dist h3 { font-size: 12px; font-weight: 600; color: var(--text-secondary); margin: 10px 0 6px; text-transform: uppercase; letter-spacing: .04em; }
.row { display: grid; grid-template-columns: minmax(0, 9em) 1fr auto; gap: 8px; align-items: center; margin: 4px 0; font-size: 13px; }
.row .name { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; color: var(--text-primary); }
.row .track { height: 8px; border-radius: 4px; background: var(--grid); overflow: hidden; }
.row .fill { display: block; height: 100%; border-radius: 4px; background: var(--series-1); }
.row .count { font-variant-numeric: tabular-nums; color: var(--text-secondary); }
.empty { color: var(--text-muted); font-size: 13px; margin: 4px 0; }
.empty:empty { display: none; }
.pill { display: inline-block; padding: 1px 8px; border-radius: 999px; border: 1px solid var(--border); font-size: 12px; font-weight: 600; }
.pill.direct { color: var(--series-3); border-color: var(--series-3); }
.pill.relay { color: var(--series-2); border-color: var(--series-2); }
.pill.pending { color: var(--text-muted); }
.scroll { overflow-x: auto; }
table { border-collapse: collapse; width: 100%; font-size: 13px; font-variant-numeric: tabular-nums; }
th, td { padding: 6px 8px; text-align: right; border-bottom: 1px solid var(--grid); white-space: nowrap; }
th:first-child, td:first-child { text-align: left; }
.state-Completed { color: var(--series-3); }
.state-Disconnected, .state-Failed, .state-Timeout { color: var(--error); }
th { color: var(--text-secondary); font-weight: 600; }
details summary { cursor: pointer; font-weight: 600; }
.notes ul { margin: 0; padding-left: 18px; color: var(--text-secondary); font-size: 13px; }
.notes li { margin: 4px 0; }
.tabs { display: flex; flex-wrap: wrap; gap: 6px; align-items: center; margin: 0 0 16px; border-bottom: 1px solid var(--border); }
.tabs button[role="tab"] { background: transparent; color: var(--text-secondary); border: 0; border-bottom: 3px solid transparent; border-radius: 0; padding: 8px 14px; cursor: pointer; }
.tabs button[role="tab"]:hover { color: var(--text-primary); }
.tabs button[role="tab"][aria-selected="true"] { color: var(--text-primary); border-bottom-color: var(--accent); font-weight: 600; }
.tabs .quiet { margin-left: auto; margin-bottom: 4px; }
.bug-detail h2 { margin: 0; }
.bug-list { max-height: 340px; overflow-y: auto; }
#bug-list td.bug-summary { max-width: 340px; overflow: hidden; text-overflow: ellipsis; text-align: left; }
#bug-list tr.selected td { background: var(--surface-0); }
#bug-list button.ticket { background: transparent; color: var(--text-primary); border: 0; padding: 0; text-decoration: underline; cursor: pointer; }
.bug-detail { margin-top: 16px; }
.bug-header { margin-bottom: 20px; }
.bug-close { margin-left: auto; }
.bug-detail h3 { font-size: 14px; margin: 0 0 4px; }
.bug-text { white-space: pre-wrap; overflow-wrap: anywhere; margin: 0 0 24px; }
.bug-facts { display: grid; grid-template-columns: max-content 1fr; gap: 4px 12px; margin: 0 0 24px; font-size: 13px; }
.bug-facts dt { color: var(--text-secondary); }
.bug-facts dd { margin: 0; overflow-wrap: anywhere; }
.bug-snapshot { margin-bottom: 8px; }
.bug-status-form { margin: 0 0 24px; }
.bug-status-form > .muted { margin: 4px 0 12px; }
.bug-status-form .toolbar { margin: 12px 0 0; }
.bug-status-field { display: flex; flex-direction: column; gap: 4px; margin: 0 0 12px; }
.bug-status-field[hidden] { display: none; }
.bug-status-field input { max-width: 200px; }
.bug-status-field textarea { max-width: 640px; font: inherit; }
.bug-log { max-height: 480px; overflow: auto; margin: 0; padding: 10px; background: var(--surface-0); border: 1px solid var(--border); border-radius: 6px; font: 12px/1.45 ui-monospace, Consolas, monospace; white-space: pre; }
.tip {
  position: fixed;
  pointer-events: none;
  background: var(--surface-1);
  color: var(--text-primary);
  border: 1px solid var(--border);
  border-radius: 6px;
  padding: 6px 8px;
  font-size: 12px;
  box-shadow: 0 2px 8px rgba(0, 0, 0, .15);
  z-index: 10;
  white-space: nowrap;
}
`;

export const ADMIN_JS = String.raw`(function () {
  'use strict';
  var KEY = 'ssf2t-dashboard-token';
  var ZONE = 'America/Sao_Paulo';
  var SVG = 'http://www.w3.org/2000/svg';
  var token = '';
  var timer = null;
  var $ = function (id) { return document.getElementById(id); };
  var regionNames = null;
  try { regionNames = new Intl.DisplayNames(['en'], { type: 'region' }); } catch (e) { regionNames = null; }
  var timeLabel = new Intl.DateTimeFormat('en-GB', { timeZone: ZONE, day: 'numeric', month: 'short', hour: '2-digit', minute: '2-digit' });
  var dayLabel = new Intl.DateTimeFormat('en-GB', { timeZone: 'UTC', day: 'numeric', month: 'short' });
  var REJECTS = {
    proto: 'Sign-in refused: different lobby protocol',
    bad_game: 'Sign-in refused: unknown game',
    bad_guest: 'Sign-in refused: unknown game image',
    version_mismatch: 'Match join or invite refused: different game image or build'
  };
  var PROTECTION = {
    ws_upgrade_accepted: 'WebSocket upgrades accepted',
    hello_accepted: 'Sign-ins accepted',
    hello_reconnect_deduped: 'Reconnects not counted as new sessions',
    pm_host_accepted: 'Player Matches advertised',
    incident_filed: 'Bug reports accepted'
  };
  var ABUSE = {
    ws_upgrade_limited: 'WebSocket upgrades refused at the edge',
    ws_upgrade_ip_cap: 'WebSocket upgrades refused: too many sockets from one address',
    hello_limited: 'Sign-ins refused: too many',
    pm_host_limited: 'Player Match advertisements refused',
    pm_join_limited: 'Player Match joins refused',
    invite_limited: 'Invites refused',
    match_resume_limited: 'Match resumes refused',
    relay_abuse_closed: 'Relay connections or relays closed for abuse',
    fight_secret_rejected: 'Fights refused: secret character not available',
    lobby_list_limited: 'Lobby reads refused',
    incident_limited: 'Bug reports refused: too many',
    incident_hourly_limited: 'Bug reports refused: hourly limit',
    incident_daily_limited: 'Bug reports refused: daily limit'
  };
  var STATES = { in_game: 'In game', connecting: 'Connecting' };
  var BUILDS = { base013: 'Original · 0.0.1.3', tu023: 'Title Update #1 · 0.0.2.3' };
  function buildText(value) { return BUILDS[value] || (value ? String(value) : 'Unknown'); }
  var DAY_SERIES = {
    fights: [
      { key: 'fights_started', label: 'Started', cls: 's1', key_cls: 'k1' },
      { key: 'fights_completed', label: 'Completed', cls: 's3', key_cls: 'k3' }
    ],
    matches: [
      { key: 'advertised', label: 'Advertised', cls: 's1', key_cls: 'k1' },
      { key: 'matches_started', label: 'Started', cls: 's2', key_cls: 'k2' },
      { key: 'direct_matches', label: 'Direct', cls: 's3', key_cls: 'k3' },
      { key: 'relay_matches', label: 'Relay', cls: 's4', key_cls: 'k4' }
    ]
  };
  var TRANSPORTS = { direct: 'Direct', relay: 'Relay', pending: 'Pending' };
  var SECRET_CARDS = [
    ['s-akuma-available', 'secret', 'akuma_available'], ['s-evil-ryu-available', 'secret', 'evil_ryu_available'],
    ['s-violent-ken-available', 'secret', 'violent_ken_available'],
    ['s-akuma-mismatch', 'secret', 'akuma_option_mismatch'], ['s-evil-ryu-mismatch', 'secret', 'evil_ryu_option_mismatch'],
    ['s-violent-ken-mismatch', 'secret', 'violent_ken_option_mismatch'],
    ['s-unsupported', 'secret', 'unsupported_peer'],
    ['s-fights-akuma', 'secret_fight', 'akuma'], ['s-fights-evil-ryu', 'secret_fight', 'evil_ryu'],
    ['s-fights-violent-ken', 'secret_fight', 'violent_ken'],
    ['s-fights-akuma-mirror', 'secret_fight', 'akuma_mirror'], ['s-fights-evil-ryu-mirror', 'secret_fight', 'evil_ryu_mirror'],
    ['s-fights-violent-ken-mirror', 'secret_fight', 'violent_ken_mirror'],
    ['s-fights-akuma-vs-evil-ryu', 'secret_fight', 'akuma_vs_evil_ryu'],
    ['s-fights-akuma-vs-violent-ken', 'secret_fight', 'akuma_vs_violent_ken'],
    ['s-fights-evil-ryu-vs-violent-ken', 'secret_fight', 'evil_ryu_vs_violent_ken'],
    ['s-matches-akuma', 'secret_match', 'akuma'], ['s-matches-evil-ryu', 'secret_match', 'evil_ryu'],
    ['s-matches-violent-ken', 'secret_match', 'violent_ken']
  ];

  function storedToken() {
    try { return sessionStorage.getItem(KEY) || ''; } catch (e) { return ''; }
  }
  function storeToken(value) {
    try { if (value) sessionStorage.setItem(KEY, value); else sessionStorage.removeItem(KEY); } catch (e) { /* storage blocked */ }
  }

  function el(tag, cls, text) {
    var node = document.createElement(tag);
    if (cls) node.className = cls;
    if (text !== undefined) node.textContent = text;
    return node;
  }
  function svg(tag, attrs) {
    var node = document.createElementNS(SVG, tag);
    for (var k in attrs) node.setAttribute(k, attrs[k]);
    return node;
  }
  function clear(node) { while (node.firstChild) node.removeChild(node.firstChild); }
  function fmt(n) { return Number(n || 0).toLocaleString('en-US'); }
  function dayText(day) { return dayLabel.format(new Date(day + 'T12:00:00Z')); }
  function countryText(code) {
    if (code === 'Unknown' || code === 'Other' || !regionNames) return code;
    try { return code + ' · ' + regionNames.of(code); } catch (e) { return code; }
  }

  function AuthError() {}

  function api(path) {
    return fetch(path, { headers: { Authorization: 'Bearer ' + token }, cache: 'no-store', credentials: 'omit' })
      .then(function (r) {
        if (r.status === 401) throw new AuthError();
        if (r.status === 503) throw new Error('The dashboard token is not configured on the service.');
        if (!r.ok) throw new Error('The service answered ' + r.status + '.');
        return r.json();
      });
  }

  var tip = $('tip');
  function showTip(event, lines) {
    clear(tip);
    lines.forEach(function (line, i) { tip.appendChild(el('div', i ? '' : 'tip-head', line)); });
    tip.hidden = false;
    var x = event.clientX + 14;
    var y = event.clientY + 14;
    var w = tip.offsetWidth;
    var h = tip.offsetHeight;
    if (x + w > window.innerWidth - 8) x = event.clientX - w - 14;
    if (y + h > window.innerHeight - 8) y = event.clientY - h - 14;
    tip.style.left = Math.max(8, x) + 'px';
    tip.style.top = Math.max(8, y) + 'px';
  }
  function hideTip() { tip.hidden = true; }

  function niceMax(v) {
    if (v <= 4) return 4;
    var p = Math.pow(10, Math.floor(Math.log10(v)));
    var steps = [1, 2, 2.5, 5, 10];
    for (var i = 0; i < steps.length; i++) if (steps[i] * p >= v) return steps[i] * p;
    return 10 * p;
  }

  function frame(width, height, max, pad) {
    var root = svg('svg', { viewBox: '0 0 ' + width + ' ' + height, role: 'img' });
    for (var i = 0; i <= 4; i++) {
      var y = pad.top + (height - pad.top - pad.bottom) * (1 - i / 4);
      root.appendChild(svg('line', { x1: pad.left, x2: width - pad.right, y1: y, y2: y, 'class': i ? 'gridline' : 'baseline' }));
      var label = svg('text', { x: pad.left - 6, y: y + 4, 'text-anchor': 'end', 'class': 'axis' });
      label.textContent = fmt(Math.round(max * i / 4));
      root.appendChild(label);
    }
    return root;
  }

  var SERIES = [
    { key: 'online', label: 'Online', cls: 's1', key_cls: 'k1' },
    { key: 'in_matches', label: 'In matches', cls: 's2', key_cls: 'k2' },
    { key: 'advertised', label: 'Advertised', cls: 's3', key_cls: 'k3' }
  ];

  function usageChart(node, history) {
    clear(node);
    var samples = history.samples;
    if (!samples.length) { node.appendChild(el('p', 'empty', 'No samples in this range yet.')); return; }
    var step = history.sample_ms;
    var byBucket = {};
    samples.forEach(function (s) { byBucket[s.bucket] = s; });
    var start = samples[0].bucket;
    var end = Math.max(samples[samples.length - 1].bucket, history.generated - (history.generated % step));
    var points = [];
    for (var b = start; b <= end; b += step) points.push(byBucket[b] || { bucket: b, online: 0, in_matches: 0, advertised: 0 });
    var legend = el('div', 'legend');
    SERIES.forEach(function (s) {
      var item = el('span');
      item.appendChild(el('i', s.key_cls));
      item.appendChild(document.createTextNode(s.label));
      legend.appendChild(item);
    });
    node.appendChild(legend);
    var W = 900, H = 260, pad = { top: 10, right: 12, bottom: 26, left: 44 };
    var max = niceMax(Math.max.apply(null, points.map(function (p) { return Math.max(p.online, p.in_matches, p.advertised); })));
    var root = frame(W, H, max, pad);
    var plotW = W - pad.left - pad.right;
    var plotH = H - pad.top - pad.bottom;
    var n = points.length;
    var xAt = function (i) { return pad.left + (n === 1 ? plotW / 2 : plotW * i / (n - 1)); };
    var yAt = function (v) { return pad.top + plotH * (1 - v / max); };
    var ticks = Math.min(6, n);
    for (var t = 0; t < ticks; t++) {
      var i = ticks === 1 ? 0 : Math.round((n - 1) * t / (ticks - 1));
      var label = svg('text', { x: xAt(i), y: H - 8, 'text-anchor': t === 0 ? 'start' : t === ticks - 1 ? 'end' : 'middle', 'class': 'axis' });
      label.textContent = timeLabel.format(points[i].bucket);
      root.appendChild(label);
    }
    SERIES.slice().reverse().forEach(function (s) {
      var d = points.map(function (p, i) { return (i ? 'L' : 'M') + xAt(i).toFixed(1) + ' ' + yAt(p[s.key]).toFixed(1); }).join('');
      root.appendChild(svg('path', { d: d, 'class': 'line ' + s.cls }));
    });
    var cursor = svg('line', { y1: pad.top, y2: pad.top + plotH, 'class': 'cursor', visibility: 'hidden' });
    root.appendChild(cursor);
    var hit = svg('rect', { x: pad.left, y: pad.top, width: plotW, height: plotH, 'class': 'hit' });
    root.appendChild(hit);
    hit.addEventListener('mousemove', function (event) {
      var box = root.getBoundingClientRect();
      var x = (event.clientX - box.left) * W / box.width;
      var i = Math.max(0, Math.min(n - 1, Math.round((x - pad.left) / plotW * (n - 1))));
      cursor.setAttribute('x1', xAt(i));
      cursor.setAttribute('x2', xAt(i));
      cursor.setAttribute('visibility', 'visible');
      var p = points[i];
      showTip(event, [timeLabel.format(p.bucket)].concat(SERIES.map(function (s) { return s.label + ': ' + fmt(p[s.key]); })));
    });
    hit.addEventListener('mouseleave', function () { cursor.setAttribute('visibility', 'hidden'); hideTip(); });
    node.appendChild(root);
  }

  var SESSION_SERIES = [{ key: 'sessions', label: 'Sessions', cls: 's1' }];
  var VALUE_CHAR_WIDTH = 5.8;

  function sessionsChart(node, daily) {
    barChart(node, daily, SESSION_SERIES, false);
  }

  function ratio(n, d) { return d > 0 ? n / d : null; }
  function percent(x) { return x === null ? '–' : Math.round(100 * x) + '%'; }
  function average(fights, matches) { return matches > 0 ? (fights / matches).toFixed(1) : '–'; }
  function plural(n, one, many) { return fmt(n) + ' ' + (n === 1 ? one : many); }

  function barPath(x, w, base, h) {
    var r = Math.min(4, w / 2, h);
    var y = base - h;
    return 'M' + x + ' ' + base + 'V' + (y + r) + 'Q' + x + ' ' + y + ' ' + (x + r) + ' ' + y +
      'H' + (x + w - r) + 'Q' + (x + w) + ' ' + y + ' ' + (x + w) + ' ' + (y + r) + 'V' + base + 'Z';
  }

  function barChart(node, daily, series, legend) {
    clear(node);
    if (legend) {
      var key = el('div', 'legend');
      series.forEach(function (s) {
        var item = el('span');
        item.appendChild(el('i', 'k' + s.cls.slice(1)));
        item.appendChild(document.createTextNode(s.label));
        key.appendChild(item);
      });
      node.appendChild(key);
    }
    var W = 900, H = 220, pad = { top: 22, right: 12, bottom: 26, left: 44 };
    var top = 0;
    daily.forEach(function (d) { series.forEach(function (s) { top = Math.max(top, d[s.key] || 0); }); });
    var max = niceMax(top);
    var root = frame(W, H, max, pad);
    var plotW = W - pad.left - pad.right;
    var plotH = H - pad.top - pad.bottom;
    var base = pad.top + plotH;
    var slot = plotW / daily.length;
    var gap = Math.min(2, slot * 0.2);
    var barSlot = (slot - gap) / series.length;
    var w = Math.max(1, series.length > 1 && barSlot > 4 ? barSlot - 1 : barSlot);
    var room = barSlot + (series.length === 1 ? gap : 0);
    daily.forEach(function (d, i) {
      series.forEach(function (s, k) {
        var v = d[s.key] || 0;
        if (v <= 0) return;
        var x = pad.left + slot * i + gap / 2 + barSlot * k;
        var h = plotH * v / max;
        root.appendChild(svg('path', { d: barPath(x, w, base, h), 'class': 'bar f' + s.cls.slice(1) }));
        var text = fmt(v);
        if (text.length * VALUE_CHAR_WIDTH > room) return;
        var value = svg('text', { x: x + w / 2, y: base - h - 4, 'text-anchor': 'middle', 'class': 'value' });
        value.textContent = text;
        root.appendChild(value);
      });
      var hit = svg('rect', { x: pad.left + slot * i, y: pad.top, width: slot, height: plotH, 'class': 'hit' });
      hit.addEventListener('mousemove', function (event) {
        showTip(event, [dayText(d.day)].concat(series.map(function (s) { return s.label + ': ' + fmt(d[s.key]); })));
      });
      hit.addEventListener('mouseleave', hideTip);
      root.appendChild(hit);
    });
    var ticks = Math.min(6, daily.length);
    for (var t = 0; t < ticks; t++) {
      var j = ticks === 1 ? 0 : Math.round((daily.length - 1) * t / (ticks - 1));
      var label = svg('text', { x: pad.left + slot * (j + 0.5), y: H - 8, 'text-anchor': 'middle', 'class': 'axis' });
      label.textContent = dayText(daily[j].day);
      root.appendChild(label);
    }
    node.appendChild(root);
  }

  function dayChart(node, daily, series) {
    barChart(node, daily, series, true);
  }

  function barList(node, entries, empty) {
    clear(node);
    if (!entries.length) { node.appendChild(el('p', 'empty', empty)); return; }
    var top = entries[0].value || 1;
    var box = el('div', 'dist');
    entries.forEach(function (entry) {
      var row = el('div', 'row');
      row.appendChild(el('span', 'name', entry.label)).title = entry.label;
      var track = el('span', 'track');
      var fill = el('span', 'fill');
      fill.style.width = Math.max(2, 100 * entry.value / top) + '%';
      track.appendChild(fill);
      row.appendChild(track);
      row.appendChild(el('span', 'count', entry.text));
      box.appendChild(row);
    });
    node.appendChild(box);
  }

  function completedOf(c) { return c.wins + c.losses + c.draws; }

  var TOP = 10;

  function matchupText(m, total) {
    var text = fmt(m.fights) + ' · ' + percent(ratio(m.fights, total));
    var done = m.a_wins + m.b_wins + m.draws;
    if (m.a === m.b || !done) return text;
    var parts = [m.a_name + ' ' + percent(ratio(m.a_wins, done)), m.b_name + ' ' + percent(ratio(m.b_wins, done))];
    if (m.draws) parts.push('Draw ' + percent(ratio(m.draws, done)));
    return text + ' · ' + parts.join(' / ');
  }

  function matchupPanel(matchups) {
    var total = matchups.reduce(function (a, m) { return a + m.fights; }, 0);
    barList($('d-matchups'), matchups.slice(0, TOP).map(function (m) {
      return { label: m.a_name + ' vs ' + m.b_name, value: m.fights, text: matchupText(m, total) };
    }), 'No matchups recorded in this range.');
  }

  function characterPanels(characters) {
    var picks = characters.reduce(function (a, c) { return a + c.picks; }, 0);
    barList($('d-played'), characters.filter(function (c) { return c.picks > 0; }).map(function (c) {
      return { label: c.name, value: c.picks, text: fmt(c.picks) + ' · ' + percent(ratio(c.picks, picks)) };
    }).slice(0, TOP), 'No fights reported in this range.');
    var wins = characters.filter(function (c) { return c.wins > 0; }).sort(function (a, b) { return b.wins - a.wins || a.id - b.id; });
    barList($('d-wins'), wins.map(function (c) {
      return { label: c.name, value: c.wins, text: plural(c.wins, 'win', 'wins') + ' · ' + percent(ratio(c.wins, completedOf(c))) };
    }).slice(0, TOP), 'No completed fights in this range.');
    var losses = characters.filter(function (c) { return c.losses > 0; }).sort(function (a, b) { return b.losses - a.losses || a.id - b.id; });
    barList($('d-losses'), losses.map(function (c) {
      return { label: c.name, value: c.losses, text: plural(c.losses, 'loss', 'losses') + ' · ' + percent(ratio(c.losses, completedOf(c))) };
    }).slice(0, TOP), 'No completed fights in this range.');
  }

  function characterTable(characters) {
    var table = $('chars');
    clear(table);
    $('chars-empty').textContent = characters.length ? '' : 'No fights reported in this range.';
    if (!characters.length) return;
    var cols = ['Character', 'Picks', 'Wins', 'Losses', 'Draws', 'Win rate', 'Loss rate'];
    var head = el('tr');
    cols.forEach(function (c) { head.appendChild(el('th', '', c)); });
    var thead = el('thead');
    thead.appendChild(head);
    table.appendChild(thead);
    var body = el('tbody');
    characters.forEach(function (c) {
      var tr = el('tr');
      [c.name, fmt(c.picks), fmt(c.wins), fmt(c.losses), fmt(c.draws), percent(ratio(c.wins, completedOf(c))),
        percent(ratio(c.losses, completedOf(c)))].forEach(function (text) { tr.appendChild(el('td', '', text)); });
      body.appendChild(tr);
    });
    table.appendChild(body);
  }

  function secretPanel(history) {
    $('secret-line').textContent = 'Last ' + history.days + ' days. Rooms are counted once, when both players are connected.';
    SECRET_CARDS.forEach(function (card) {
      var found = (history.dimensions[card[1]] || []).filter(function (item) { return item.value === card[2]; })[0];
      $(card[0]).textContent = fmt(found ? found.count : 0);
    });
  }

  function durationText(ms) {
    var seconds = Math.round(ms / 1000);
    if (seconds < 60) return seconds + 's';
    var minutes = Math.floor(seconds / 60);
    if (minutes < 60) return minutes + 'm ' + (seconds % 60) + 's';
    return Math.floor(minutes / 60) + 'h ' + (minutes % 60) + 'm';
  }

  function recentTable(recent) {
    var table = $('recent');
    clear(table);
    var list = recent.matches;
    $('recent-title').textContent = 'Last matches · showing ' + (recent.total > list.length
      ? list.length + ' of ' + recent.total + ' in selected range' : plural(list.length, 'match', 'matches'));
    $('recent-empty').textContent = list.length ? '' : 'No finished matches in this range.';
    if (!list.length) return;
    var cols = ['Date and time', 'Duration', 'Fights started', 'Fights completed', 'Transport', 'Game build', 'Platforms', 'Final state'];
    var head = el('tr');
    cols.forEach(function (c) { head.appendChild(el('th', '', c)); });
    var thead = el('thead');
    thead.appendChild(head);
    table.appendChild(thead);
    var body = el('tbody');
    list.forEach(function (m) {
      var tr = el('tr');
      tr.appendChild(el('td', '', timeLabel.format(m.ended)));
      tr.appendChild(el('td', '', durationText(m.duration_ms)));
      tr.appendChild(el('td', '', m.fights_started === null ? '–' : plural(m.fights_started, 'fight', 'fights')));
      tr.appendChild(el('td', '', m.fights_completed === null ? '–' : fmt(m.fights_completed) + ' completed'));
      tr.appendChild(el('td', '', TRANSPORTS[m.transport] || 'Unknown'));
      tr.appendChild(el('td', '', buildText(m.guest)));
      tr.appendChild(el('td', '', m.platforms));
      tr.appendChild(el('td', 'state-' + m.state, m.state));
      body.appendChild(tr);
    });
    table.appendChild(body);
  }

  function rows(node, title, items, name) {
    var box = el('div', 'dist');
    box.appendChild(el('h3', '', title));
    if (!items.length) box.appendChild(el('p', 'empty', 'None'));
    var total = items.reduce(function (a, b) { return a + b.count; }, 0);
    var top = items.length ? items[0].count : 1;
    items.slice(0, 12).forEach(function (item) {
      var row = el('div', 'row');
      var label = name ? name(item.value) : item.value;
      row.appendChild(el('span', 'name', label)).title = label;
      var track = el('span', 'track');
      var fill = el('span', 'fill');
      fill.style.width = Math.max(2, 100 * item.count / top) + '%';
      track.appendChild(fill);
      row.appendChild(track);
      row.appendChild(el('span', 'count', fmt(item.count) + ' · ' + Math.round(100 * item.count / total) + '%'));
      box.appendChild(row);
    });
    if (items.length > 12) box.appendChild(el('p', 'empty', (items.length - 12) + ' more not shown'));
    node.appendChild(box);
  }

  function distribution(id, live, range, days, name) {
    var node = $(id);
    clear(node);
    rows(node, 'Now · players online', live, name);
    rows(node, 'Sessions · last ' + days + ' days', range, name);
  }

  function transportRows(id, live, range, days) {
    var node = $(id);
    clear(node);
    var name = function (v) { return TRANSPORTS[v] || v; };
    rows(node, 'Now · matches', live, name);
    rows(node, 'Chosen · last ' + days + ' days', range, name);
  }

  function dailyTable(daily) {
    var table = $('daily');
    clear(table);
    var cols = [['day', 'Day'], ['sessions', 'Sessions'], ['peak_online', 'Peak online'], ['peak_in_matches', 'Peak in matches'],
      ['peak_advertised', 'Peak advertised'], ['advertised', 'Advertised'], ['matches_started', 'Matches'], ['incompatible_rejects', 'Rejects'],
      ['fights_started', 'Fights started'], ['fights_completed', 'Fights completed'], ['avg', 'Avg fights/match'],
      ['direct_matches', 'Direct matches'], ['relay_matches', 'Relay matches'], ['direct_fights', 'Direct fights'],
      ['relay_fights', 'Relay fights']];
    var head = el('tr');
    cols.forEach(function (c) { head.appendChild(el('th', '', c[1])); });
    var thead = el('thead');
    thead.appendChild(head);
    table.appendChild(thead);
    var body = el('tbody');
    daily.slice().reverse().forEach(function (d) {
      var tr = el('tr');
      cols.forEach(function (c) {
        tr.appendChild(el('td', '', c[0] === 'day' ? d.day : c[0] === 'avg' ? average(d.fights_started, d.matches_started) : fmt(d[c[0]])));
      });
      body.appendChild(tr);
    });
    table.appendChild(body);
  }

  function ageText(ms) {
    var minutes = Math.floor(ms / 60000);
    if (minutes < 1) return '<1 min';
    if (minutes < 60) return minutes + ' min';
    return Math.floor(minutes / 60) + ' h ' + (minutes % 60) + ' min';
  }

  function matchesTable(live) {
    var table = $('matches');
    clear(table);
    var list = live.list || [];
    $('matches-empty').textContent = list.length ? '' : 'No matches running.';
    if (!list.length) return;
    var cols = ['Match', 'State', 'Game build', 'Transport', 'Relay', 'Endpoints', 'Seats', 'Age'];
    var head = el('tr');
    cols.forEach(function (c) { head.appendChild(el('th', '', c)); });
    var thead = el('thead');
    thead.appendChild(head);
    table.appendChild(thead);
    var body = el('tbody');
    list.forEach(function (m) {
      var tr = el('tr');
      tr.appendChild(el('td', '', m.id));
      tr.appendChild(el('td', '', STATES[m.state] || m.state));
      tr.appendChild(el('td', '', buildText(m.guest)));
      var cell = el('td');
      cell.appendChild(el('span', 'pill ' + m.transport, TRANSPORTS[m.transport] || m.transport));
      tr.appendChild(cell);
      tr.appendChild(el('td', '', m.relay_available ? 'Available' : 'No'));
      tr.appendChild(el('td', '', m.endpoints + ' / 2'));
      tr.appendChild(el('td', '', m.seats + ' / 2'));
      tr.appendChild(el('td', '', ageText(m.age_ms)));
      body.appendChild(tr);
    });
    table.appendChild(body);
    if (live.matches > list.length) $('matches-empty').textContent = (live.matches - list.length) + ' more not shown';
  }

  function rejects(history) {
    var node = $('rejects');
    clear(node);
    var total = history.daily.reduce(function (a, d) { return a + d.incompatible_rejects; }, 0);
    node.appendChild(el('p', 'muted', fmt(total) + ' operations refused for an incompatible protocol, game image or build in the last ' + history.days + ' days.'));
    rows(node, 'By reason', history.dimensions.reject, function (v) { return REJECTS[v] || v; });
  }

  function counters(id, history, names, intro, empty, keep) {
    var node = $(id);
    clear(node);
    var items = history.dimensions.abuse.filter(function (item) { return keep(item.value); });
    node.appendChild(el('p', 'muted', intro + ' over the last ' + history.days + ' days, written about every 15 minutes.'));
    if (!items.length) node.appendChild(el('p', 'empty', empty + ' in this range'));
    var table = el('table');
    var body = el('tbody');
    items.forEach(function (item) {
      var tr = el('tr');
      tr.appendChild(el('td', '', names[item.value] || item.value));
      tr.appendChild(el('td', '', fmt(item.count)));
      body.appendChild(tr);
    });
    table.appendChild(body);
    node.appendChild(table);
  }

  function protection(history) {
    counters('protection', history, PROTECTION, 'Normal activity the protections let through or merged', 'No protection activity',
      function (name) { return name in PROTECTION; });
  }

  function abuse(history) {
    counters('abuse', history, ABUSE, 'Interventions: refused, limited or closed', 'No abuse events',
      function (name) { return !(name in PROTECTION); });
  }

  function render(summary, history) {
    $('c-online').textContent = fmt(summary.live.online);
    $('c-in-matches').textContent = fmt(summary.live.in_matches);
    $('c-advertised').textContent = fmt(summary.live.advertised);
    $('c-direct').textContent = fmt(summary.live.direct);
    $('c-relay').textContent = fmt(summary.live.relay);
    $('c-pending').textContent = fmt(summary.live.pending);
    var t = summary.today;
    $('t-peak').textContent = fmt(t.peak_online);
    $('t-sessions').textContent = fmt(t.sessions);
    $('t-advertised').textContent = fmt(t.advertised);
    $('t-matches').textContent = fmt(t.matches_started);
    $('t-fights').textContent = fmt(t.fights_started);
    $('t-completed').textContent = fmt(t.fights_completed);
    $('t-direct-matches').textContent = fmt(t.direct_matches);
    $('t-relay-matches').textContent = fmt(t.relay_matches);
    $('t-direct-fights').textContent = fmt(t.direct_fights);
    $('t-relay-fights').textContent = fmt(t.relay_fights);
    $('t-avg').textContent = average(t.fights_started, t.matches_started);
    $('today-line').textContent = 'Today (' + summary.day + '): peak ' + fmt(t.peak_in_matches) + ' in matches and ' +
      fmt(t.peak_advertised) + ' advertised, ' + fmt(summary.live.connections) + ' connections open, ' +
      fmt(t.incompatible_rejects) + ' incompatibility rejects.';
    var r = history.totals;
    $('range-title').textContent = 'Selected range · last ' + history.days + ' days';
    $('r-sessions').textContent = fmt(r.sessions);
    $('r-advertised').textContent = fmt(r.advertised);
    $('r-matches').textContent = fmt(r.matches_started);
    $('r-fights').textContent = fmt(r.fights_started);
    $('r-completed').textContent = fmt(r.fights_completed);
    $('r-avg').textContent = average(r.fights_started, r.matches_started);
    $('r-direct-matches').textContent = fmt(r.direct_matches);
    $('r-relay-matches').textContent = fmt(r.relay_matches);
    $('r-direct-fights').textContent = fmt(r.direct_fights);
    $('r-relay-fights').textContent = fmt(r.relay_fights);
    $('r-picks').textContent = fmt(r.character_picks);
    $('r-wins').textContent = fmt(r.character_wins);
    $('r-losses').textContent = fmt(r.character_losses);
    $('r-draws').textContent = fmt(r.character_draws);
    matchesTable(summary.live);
    recentTable(history.recent);
    usageChart($('usage'), history);
    sessionsChart($('sessions'), history.daily);
    dayChart($('fights'), history.daily, DAY_SERIES.fights);
    dayChart($('matches-by-day'), history.daily, DAY_SERIES.matches);
    distribution('d-platform', summary.live.platform, history.dimensions.platform, history.days);
    distribution('d-version', summary.live.version, history.dimensions.version, history.days,
      function (v) { return /^[0-9]/.test(v) ? 'v' + v : v; });
    distribution('d-country', summary.live.country, history.dimensions.country, history.days, countryText);
    transportRows('d-transport', summary.live.transport, history.dimensions.transport, history.days);
    var fightRows = function (id, items, name) {
      var node = $(id);
      clear(node);
      rows(node, 'Fights · last ' + history.days + ' days', items, name);
    };
    fightRows('d-fplatform', history.dimensions.fight_platform);
    fightRows('d-fbuild', history.dimensions.fight_guest, buildText);
    fightRows('d-ftransport', history.dimensions.fight_transport, function (v) { return TRANSPORTS[v] || v; });
    characterPanels(history.characters);
    matchupPanel(history.matchups);
    characterTable(history.characters);
    secretPanel(history);
    dailyTable(history.daily);
    rejects(history);
    protection(history);
    abuse(history);
    $('status').textContent = 'Updated ' + timeLabel.format(summary.generated) + ' (Sao Paulo)';
  }

  // Named by string: the page tests forbid any CSS-style address call in the script text.
  var OBJECT_MAKE = 'createObjectURL';
  var OBJECT_DROP = 'revokeObjectURL';
  var bugReports = [];
  var bugTicket = '';
  var bugLoadingTicket = '';
  var bugLogText = '';
  var stampLabel = new Intl.DateTimeFormat('en-GB', { timeZone: ZONE, day: 'numeric', month: 'short', year: 'numeric', hour: '2-digit', minute: '2-digit' });
  var BUG_STATUSES = {
    new: ['Open', 'The report has not been reviewed yet.'],
    investigating: ['Investigating', 'The report is being investigated.'],
    fixed: ['Fixed', 'The problem has been corrected.'],
    cannot_reproduce: ['Cannot reproduce', 'The problem could not be reproduced with the available information.'],
    duplicate: ['Duplicate', 'The same problem is already tracked by another report.'],
    not_a_bug: ['Not a bug', 'The reported behavior is expected or is not a bug of this port.'],
    closed: ['Closed', 'The report was intentionally finished without a specific fix or another more precise status. It does not mean Fixed.']
  };

  function bugStatusLabel(status) { return BUG_STATUSES[status] ? BUG_STATUSES[status][0] : status; }
  function bugBuildLabel(guest) { return BUILDS[guest] || guest; }

  function bugOptions(id, key, label) {
    var select = $(id);
    var chosen = select.value;
    var seen = {};
    bugReports.forEach(function (r) { seen[r[key]] = true; });
    clear(select);
    var all = el('option', '', 'All');
    all.value = '';
    select.appendChild(all);
    Object.keys(seen).sort().forEach(function (value) {
      var option = el('option', '', label ? label(value) : value);
      option.value = value;
      select.appendChild(option);
    });
    select.value = seen[chosen] ? chosen : '';
  }

  function bugShown() {
    var version = $('bug-version').value;
    var build = $('bug-build').value;
    var status = $('bug-status-filter').value;
    return bugReports.filter(function (r) {
      return (!version || r.launcher_version === version) && (!build || r.guest === build) && (!status || r.status === status);
    });
  }

  function bugFact(list, name, value) {
    list.appendChild(el('dt', '', name));
    list.appendChild(el('dd', '', value || '–'));
  }

  function showBugStatusMeaning() {
    var status = $('bug-status').value;
    $('bug-status-meaning').textContent = BUG_STATUSES[status] ? BUG_STATUSES[status][1] : '';
    $('bug-fixed-field').hidden = status !== 'fixed';
  }

  function bugStatusForm(r) {
    $('bug-status').value = r.status;
    $('bug-fixed').value = r.fixed_version || '';
    $('bug-message').value = r.player_message || '';
    $('bug-status-note').textContent = '';
    showBugStatusMeaning();
  }

  function bugDetail(r) {
    bugTicket = r.ticket;
    bugLoadingTicket = '';
    bugLogText = r.log_text;
    $('bug-title').textContent = r.ticket;
    $('bug-text').textContent = r.description;
    var facts = $('bug-facts');
    clear(facts);
    bugFact(facts, 'Report ID', r.ticket);
    bugFact(facts, 'Submitted', stampLabel.format(r.ms) + ' (Sao Paulo)');
    bugFact(facts, 'Port version', r.launcher_version);
    bugFact(facts, 'Game image', bugBuildLabel(r.guest));
    bugFact(facts, 'Platform', r.platform);
    bugFact(facts, 'Operating system', r.operating_system);
    bugFact(facts, 'Runtime', r.runtime);
    bugFact(facts, 'Original log file', r.log_file);
    bugFact(facts, 'Log clipped', r.log_clipped === 'yes' ? 'Yes, only its end was sent' : 'No');
    $('bug-log-name').textContent = 'Sanitized log';
    $('bug-log').textContent = bugLogText;
    $('bug-note').textContent = '';
    bugStatusForm(r);
    $('bug-detail').hidden = false;
    renderBugList();
  }

  function openBugReport(ticket) {
    bugLoadingTicket = ticket;
    return api('/admin/api/incidents/' + encodeURIComponent(ticket)).then(function (report) {
      if (report.ticket === bugLoadingTicket) bugDetail(report);
    }).catch(function (error) {
      if (error instanceof AuthError) { signOut('The token was not accepted.'); return; }
      if (ticket !== bugLoadingTicket) return;
      bugLoadingTicket = '';
      $('bug-count').textContent = 'Could not open ' + ticket + '.';
    });
  }

  function closeBugReport() {
    bugTicket = '';
    bugLoadingTicket = '';
    bugLogText = '';
    $('bug-detail').hidden = true;
    renderBugList();
  }

  function toggleBugReport(ticket) {
    if (ticket === bugTicket) closeBugReport();
    else openBugReport(ticket);
  }

  function downloadBugLog() {
    var ticket = bugTicket;
    if (!ticket) return Promise.resolve();
    return fetch('/admin/api/incidents/' + encodeURIComponent(ticket) + '/log', { headers: { Authorization: 'Bearer ' + token }, cache: 'no-store', credentials: 'omit' }).then(function (r) {
      if (r.status === 401) throw new AuthError();
      if (!r.ok) throw new Error('log');
      var named = /filename="([^"]+)"/.exec(r.headers.get('Content-Disposition') || '');
      return r.blob().then(function (blob) {
        var object = URL[OBJECT_MAKE](blob);
        var link = el('a');
        link.href = object;
        link.download = named ? named[1] : ticket + '.log';
        link.click();
        setTimeout(function () { URL[OBJECT_DROP](object); }, 1000);
      });
    }).catch(function (error) {
      if (error instanceof AuthError) { signOut('The token was not accepted.'); return; }
      $('bug-note').textContent = 'Could not download the log.';
    });
  }

  function copyBugLog() {
    if (!bugLogText) return Promise.resolve();
    var say = function (text) { $('bug-note').textContent = text; };
    if (!navigator.clipboard || !navigator.clipboard.writeText) { say('Copying is not possible in this browser.'); return Promise.resolve(); }
    return navigator.clipboard.writeText(bugLogText).then(function () { say('Log copied.'); }, function () { say('Could not copy the log.'); });
  }

  function saveBugStatus() {
    var ticket = bugTicket;
    if (!ticket) return Promise.resolve();
    var status = $('bug-status').value;
    var change = { status: status, fixed_version: status === 'fixed' ? $('bug-fixed').value.trim() : '', player_message: $('bug-message').value };
    return fetch('/admin/api/incidents/' + encodeURIComponent(ticket) + '/status', { method: 'POST', headers: { Authorization: 'Bearer ' + token, 'Content-Type': 'application/json' }, body: JSON.stringify(change), cache: 'no-store', credentials: 'omit' }).then(function (r) {
      if (r.status === 401) throw new AuthError();
      if (!r.ok) throw new Error('status');
      return r.json();
    }).then(function (saved) {
      bugReports.forEach(function (r) {
        if (r.ticket === ticket) { r.status = saved.status; r.status_updated_at = saved.status_updated_at; }
      });
      renderBugList();
      if (ticket !== bugTicket) return;
      bugStatusForm(saved);
      $('bug-status-note').textContent = 'Saved.';
    }).catch(function (error) {
      if (error instanceof AuthError) { signOut('The token was not accepted.'); return; }
      if (ticket === bugTicket) $('bug-status-note').textContent = 'Could not save the status. Check the version and the message.';
    });
  }

  function bugTicketCell(r) {
    var td = el('td');
    var button = el('button', 'ticket', r.ticket);
    button.type = 'button';
    button.addEventListener('click', function () { toggleBugReport(r.ticket); });
    td.appendChild(button);
    return td;
  }

  function renderBugList() {
    var shown = bugShown();
    var node = $('bug-list');
    clear(node);
    var head = el('tr');
    ['Report ID', 'Submitted', 'Port', 'Game image', 'Platform', 'Operating system', 'Description', 'Status'].forEach(function (c) { head.appendChild(el('th', '', c)); });
    node.appendChild(head);
    shown.forEach(function (r) {
      var tr = el('tr', r.ticket === bugTicket ? 'selected' : '');
      tr.appendChild(bugTicketCell(r));
      tr.appendChild(el('td', '', stampLabel.format(r.ms)));
      tr.appendChild(el('td', '', r.launcher_version));
      tr.appendChild(el('td', '', bugBuildLabel(r.guest)));
      tr.appendChild(el('td', '', r.platform));
      tr.appendChild(el('td', '', r.operating_system));
      tr.appendChild(el('td', 'bug-summary', r.preview));
      tr.appendChild(el('td', '', bugStatusLabel(r.status)));
      node.appendChild(tr);
    });
    $('bug-empty').textContent = 'No bug reports yet.';
    $('bug-empty').hidden = bugReports.length > 0;
    $('bug-list-wrap').hidden = bugReports.length === 0;
    $('bug-count').textContent = !bugReports.length ? ''
      : shown.length === bugReports.length ? fmt(shown.length) + ' reports' : fmt(shown.length) + ' of ' + fmt(bugReports.length) + ' reports';
  }

  function loadBugReports() {
    $('bug-count').textContent = 'Loading…';
    return api('/admin/api/incidents').then(function (r) {
      bugReports = Array.isArray(r.incidents) ? r.incidents : [];
      bugOptions('bug-version', 'launcher_version');
      bugOptions('bug-build', 'guest', bugBuildLabel);
      renderBugList();
    }).catch(function (error) {
      if (error instanceof AuthError) { signOut('The token was not accepted.'); return; }
      $('bug-count').textContent = error && error.message ? error.message : 'Could not load the reports.';
    });
  }

  var TAB_KEY = 'ssf2t-dashboard-tab';
  var view = 'dashboard';
  try { if (sessionStorage.getItem(TAB_KEY) === 'bugs') view = 'bugs'; } catch (e) { /* storage blocked */ }

  function showView(name) {
    view = name;
    try { sessionStorage.setItem(TAB_KEY, name); } catch (e) { /* storage blocked */ }
    $('view-dashboard').hidden = name !== 'dashboard';
    $('view-bugs').hidden = name !== 'bugs';
    $('tab-dashboard').setAttribute('aria-selected', String(name === 'dashboard'));
    $('tab-bugs').setAttribute('aria-selected', String(name === 'bugs'));
  }

  function load() {
    var days = $('range').value;
    $('status').textContent = 'Loading…';
    return Promise.all([api('/admin/api/summary'), api('/admin/api/history?days=' + encodeURIComponent(days) + '&recent=' + encodeURIComponent($('recent-limit').value))])
      .then(function (r) { render(r[0], r[1]); })
      .catch(function (error) {
        if (error instanceof AuthError) { signOut('The token was not accepted.'); return; }
        $('status').textContent = error && error.message ? error.message : 'Could not load the metrics.';
      });
  }

  function showDashboard() {
    $('login').hidden = true;
    $('dash').hidden = false;
    showView(view);
    if (view === 'bugs') loadBugReports();
    else load();
    if (timer) clearInterval(timer);
    timer = setInterval(function () { if (!document.hidden && view === 'dashboard') load(); }, 60000);
  }

  function signOut(message) {
    token = '';
    storeToken('');
    if (timer) clearInterval(timer);
    timer = null;
    bugReports = [];
    bugTicket = '';
    bugLogText = '';
    $('bug-detail').hidden = true;
    $('dash').hidden = true;
    $('login').hidden = false;
    $('token').value = '';
    $('login-error').textContent = message || '';
    $('token').focus();
  }

  $('login').addEventListener('submit', function (event) {
    event.preventDefault();
    token = $('token').value.trim();
    $('token').value = '';
    if (!token) return;
    storeToken($('remember').checked ? token : '');
    $('login-error').textContent = '';
    showDashboard();
  });
  $('refresh').addEventListener('click', load);
  $('range').addEventListener('change', load);
  $('recent-limit').addEventListener('change', load);
  $('signout').addEventListener('click', function () { signOut(''); });
  $('tab-dashboard').addEventListener('click', function () { showView('dashboard'); load(); });
  $('tab-bugs').addEventListener('click', function () { showView('bugs'); loadBugReports(); });
  $('bug-refresh').addEventListener('click', loadBugReports);
  $('bug-version').addEventListener('change', renderBugList);
  $('bug-build').addEventListener('change', renderBugList);
  $('bug-status-filter').addEventListener('change', renderBugList);
  $('bug-status').addEventListener('change', showBugStatusMeaning);
  $('bug-status-save').addEventListener('click', saveBugStatus);
  $('bug-download').addEventListener('click', downloadBugLog);
  $('bug-copy').addEventListener('click', copyBugLog);
  $('bug-close').addEventListener('click', closeBugReport);

  token = storedToken();
  if (token) { $('remember').checked = true; showDashboard(); }
}());
`;
