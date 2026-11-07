#!/usr/bin/env bash
# The headless parity gate on Linux. tools/test.ps1 runs it inside WSL (Ubuntu 20.04, glibc 2.31) and folds its results
# into the one Windows+Linux parity summary; it also works on its own on a Linux machine. Nothing opens a window, plays
# audio or starts the game: it compiles the real production sources, builds and runs the C++ suites, runs the Worker
# and a local lobby (also behind TLS), stages and scans a fixture AppDir, runs a fixture AppImage in extract-and-run
# mode, and inspects the built binaries. A check that cannot run fails; nothing is reported NOT RUN.
#
#   tools/test.sh                         --prepare, then --build-dir on the folder it prepared
#   tools/test.sh --prepare               sync this checkout into ~/ssf2t-linux-build (SSF2T_WORK), install the patched
#                                         runtime, configure out/build/parity there, and print @@BUILD <folder>
#   tools/test.sh --build-dir DIR         build the test targets in a configured folder (the sources are the ones its
#                                         CMakeCache names) and run everything
#   tools/test.sh --emit-vectors FILE     also write the wire vectors this Linux build produces
#   tools/test.sh --peer-vectors FILE     also check them, and the Worker, against vectors another platform printed
#   tools/test.sh --check-environment     only check that this machine can run the gate (Ubuntu 20.04, glibc 2.31, tools)
#
# The real game images in game/analysis are read in place (never copied). The patched ReXGlue runtime is built on the
# first run (git and network needed once), as are Node.js and the AppImage runtime when the machine has none.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${SSF2T_WORK:-$HOME/ssf2t-linux-build}"
BUILD_DIR=""
EMIT_VECTORS=""
PEER_VECTORS=""
CHECK_ENVIRONMENT=0
PREPARE=0
while [ $# -gt 0 ]; do
  case "$1" in
    --check-environment) CHECK_ENVIRONMENT=1 ;;
    --prepare) PREPARE=1 ;;
    --build-dir) [ $# -ge 2 ] || { echo "--build-dir needs a folder" >&2; exit 2; }; BUILD_DIR="$(realpath "$2")"; shift ;;
    --emit-vectors) [ $# -ge 2 ] || { echo "--emit-vectors needs a file" >&2; exit 2; }; EMIT_VECTORS="$(realpath -m "$2")"; shift ;;
    --peer-vectors) [ $# -ge 2 ] || { echo "--peer-vectors needs a file" >&2; exit 2; }; PEER_VECTORS="$(realpath "$2")"; shift ;;
    *) echo "Unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

[ "$(uname -s)" = Linux ] || { echo "ERROR: runs on Linux only (tools/test.ps1 is the Windows script)" >&2; exit 1; }

LOBBY_PID=""
TLS_LOBBY_PID=""
TLS_PID=""
TMP="$(mktemp -d)"
cleanup() {
  [ -n "$LOBBY_PID" ] && kill "$LOBBY_PID" 2>/dev/null
  [ -n "$TLS_LOBBY_PID" ] && kill "$TLS_LOBBY_PID" 2>/dev/null
  [ -n "$TLS_PID" ] && kill "$TLS_PID" 2>/dev/null
  rm -rf "$TMP"
}
trap cleanup EXIT

RESULTS=()  # "status|what|area;area;..."
fail() { echo "ERROR: $*" >&2; exit 1; }
. "$ROOT/tools/linux-policy.sh"
. "$ROOT/tools/linux-work.sh"

A_ENV="Linux environment"
A_PROD="Linux production compile paths"
A_B13="Linux Base013"
A_TU="Linux TU023"
A_PAIR="Linux Base013/TU023 pairs"
A_HOOK="Linux hooks and patches"
A_GFP="Linux global feature parity"
A_T_B13="Linux/Base013 feature set"
A_T_TU="Linux/TU023 feature set"
A_PLAT="Linux platform paths"
A_WS="Linux WebSocket/OpenSSL"
A_NET="Linux online protocol"
A_RELAY="Linux Direct/Relay"
A_PM="Linux Player Match"
A_RB="Linux Rollback"
A_FZ="Linux Frame Zero"
A_MULTI="Linux multiple fights/matches"
A_WORKER="Linux Worker and local lobby"
A_WIRE="Linux wire vectors"
A_CTRL="Linux launcher controller"
A_SET="Linux settings"
A_KEYS="Linux keyboard/hotkeys"
A_DISP="Linux display"
A_GPU="Linux graphics adapter"
A_ACH="Linux achievements"
A_CLIP="Linux clipboard runtime"
A_BDIR="Linux test.sh --build-dir"
A_CRASH="Linux crash reporting"
A_DATA="Linux user data/AppImage paths"
A_UPD="Linux self-updater"
A_PKG="Linux packaging policy"
A_GLIBC="Linux GLIBC <= 2.31"
A_RPATH="Linux RPATH/RUNPATH"
A_DEPS="Linux dynamic dependencies"
A_CORE="Linux core runtime"
A_LOC="Linux localization pt-BR"
A_FIGHT="Linux fight telemetry"
A_DASH="Linux dashboard"
A_RECENT="Linux recent matches"
A_ABUSE="Linux abuse protection"
A_CHARS="Linux character analytics"
A_MATCHUPS="Linux matchup analytics"
A_GATE="Linux test.sh gate"
AREAS=("$A_ENV" "$A_BDIR" "$A_PROD" "$A_B13" "$A_TU" "$A_PAIR" "$A_HOOK" "$A_GFP" "$A_T_B13" "$A_T_TU" "$A_PLAT" "$A_WS" "$A_NET"
  "$A_RELAY" "$A_PM" "$A_RB" "$A_FZ" "$A_MULTI" "$A_WORKER" "$A_WIRE" "$A_CTRL" "$A_SET" "$A_KEYS" "$A_DISP" "$A_GPU"
  "$A_ACH" "$A_CLIP" "$A_CRASH" "$A_DATA" "$A_UPD" "$A_PKG" "$A_GLIBC" "$A_RPATH" "$A_DEPS" "$A_CORE" "$A_LOC" "$A_FIGHT" "$A_DASH" "$A_RECENT" "$A_ABUSE" "$A_CHARS" "$A_MATCHUPS" "$A_GATE")
TARGETS=("linux/base013|$A_T_B13" "linux/tu023|$A_T_TU")

A_CROSS="Windows/Linux wire compatibility"
[ -z "$BUILD_DIR" ] || for area in "${AREAS[@]}"; do echo "@@AREA $area"; done

record() {  # status what areas
  RESULTS+=("$1|$2|$3")
  echo "@@RESULT $1|$2|$3"
  [ "$1" = FAIL ] && echo "PARITY FAIL: $2 (${3//;/, })"
  return 0
}

check() {  # what areas command...
  local what="$1" areas="$2" code
  shift 2
  echo "== $what"
  "$@"
  code=$?
  if [ "$code" = 77 ]; then record FAIL "$what (its fixture is missing)" "$areas"
  elif [ "$code" != 0 ]; then record FAIL "$what" "$areas"
  else record PASS "$what" "$areas"
  fi
}

# The global feature parity suite prints one "TARGET <platform>/<build> PASS|FAIL <features>" line per target.
check_global() {  # what areas command...
  local what="$1" areas="$2" code out entry target area
  shift 2
  echo "== $what"
  out="$(mktemp)"
  "$@" | tee "$out"
  code=${PIPESTATUS[0]}
  if [ "$code" != 0 ]; then record FAIL "$what" "$areas"; else record PASS "$what" "$areas"; fi
  for entry in "${TARGETS[@]}"; do
    target="${entry%%|*}"
    area="${entry#*|}"
    if grep -q "^TARGET $target PASS " "$out"; then record PASS "$what ($area)" "$area"; else record FAIL "$what ($area)" "$area"; fi
  done
  rm -f "$out"
}

# ---- Environment -------------------------------------------------------------------------------------------------

environment_problems() {
  local problems="" tool glibc version
  glibc="$(getconf GNU_LIBC_VERSION 2>/dev/null | cut -d' ' -f2)"
  version="$(. /etc/os-release 2>/dev/null && echo "${NAME:-} ${VERSION_ID:-}")"
  [ "$glibc" = "$MAX_GLIBC" ] || problems="$problems glibc is '$glibc', the baseline machine has $MAX_GLIBC (Ubuntu 20.04);"
  [ "$version" = "Ubuntu 20.04" ] || problems="$problems this is '$version', not Ubuntu 20.04;"
  for tool in clang-20 clang++-20 cmake ninja rsync readelf ldd openssl patchelf python3 unzip sha256sum curl objcopy tar unshare; do
    command -v "$tool" >/dev/null || problems="$problems $tool is not installed (tools/setup-linux-build-env.sh prepares a machine);"
  done
  echo "$problems"
}

PROBLEMS="$(environment_problems)"
if [ "$CHECK_ENVIRONMENT" = 1 ]; then
  [ -z "$PROBLEMS" ] && exit 0
  echo "$PROBLEMS"
  exit 1
fi
echo "== Linux validation environment"
echo "@@ENV distro=$(. /etc/os-release && echo "$PRETTY_NAME")"
echo "@@ENV glibc=$(getconf GNU_LIBC_VERSION 2>/dev/null | cut -d' ' -f2)"
echo "@@ENV compiler=$(clang++-20 --version 2>/dev/null | sed -n 1p)"
echo "@@ENV cmake=$(cmake --version 2>/dev/null | sed -n 1p)"
echo "@@ENV ninja=$(ninja --version 2>/dev/null)"
if [ -n "$PROBLEMS" ]; then
  record FAIL "Linux validation environment is unavailable:$PROBLEMS" "$A_ENV;$A_GATE"
  echo "PARITY FAIL: Linux validation environment is unavailable:$PROBLEMS"
  exit 1
fi
[ -z "$BUILD_DIR" ] || record PASS "Linux validation environment (glibc $MAX_GLIBC, $(. /etc/os-release && echo "$PRETTY_NAME"))" "$A_ENV"

# ---- Work tree, runtime, build ----------------------------------------------------------------------------------

if [ -z "$BUILD_DIR" ]; then
  echo "== Source -> $WORK"
  sync_sources "$ROOT" "$WORK" || fail "cannot copy the sources to $WORK"
  echo "== ReXGlue SDK and patched runtime"
  ensure_sdk "$ROOT" "$WORK"
  echo "== Configure"
  (cd "$WORK" && cmake -S . -B "$WORK/out/build/parity" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-20     -DCMAKE_CXX_COMPILER=clang++-20 -DSSF2T_TITLE_UPDATE_IMAGE=ON >/dev/null) || fail "cmake could not configure $WORK"
  echo "@@WORK $WORK"
  echo "@@BUILD $WORK/out/build/parity"
  [ "$PREPARE" = 1 ] && exit 0
  rm -rf "$TMP"
  exec bash "$WORK/tools/test.sh" --build-dir "$WORK/out/build/parity" ${EMIT_VECTORS:+--emit-vectors "$EMIT_VECTORS"}     ${PEER_VECTORS:+--peer-vectors "$PEER_VECTORS"}
fi

BUILD="$BUILD_DIR"
[ -f "$BUILD/build.ninja" ] || fail "$BUILD is not a configured build folder"
SRC="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$BUILD/CMakeCache.txt")"
[ -f "$SRC/CMakeLists.txt" ] || fail "$BUILD names the source folder '$SRC', which has no CMakeLists.txt"
WORK="$SRC"
SDK="$SRC/tools/rexglue/linux-amd64"
echo "@@ENV build_dir=$BUILD"
echo "@@ENV source_dir=$SRC"
record PASS "tools/test.sh --build-dir used the configured build folder $BUILD (sources $SRC)" "$A_BDIR"
check "patched ReXGlue runtime in the build folder's SDK" "$A_BDIR;$A_ENV" bash "$SRC/tools/build-patched-rexruntime.sh" --check "$SDK"
ensure_node "$WORK"
echo "@@ENV node=$($NODE --version 2>/dev/null)"

echo "== Production sources (every file the program is built from)"
PROD_OK=0
cmake --build "$BUILD" --target ssf2t_linux_production_check >"$TMP/production.log" 2>&1
PROD_CODE=$?
tail -3 "$TMP/production.log"
[ "$PROD_CODE" = 0 ] && [ -x "$BUILD/ssf2t_linux_production_check" ] && PROD_OK=1
[ "$PROD_OK" = 1 ] || grep -E 'error|FAILED' "$TMP/production.log" | head -20
PROD_COUNT="$(ninja -C "$BUILD" -t commands ssf2t_linux_production_check | grep -c 'CMakeFiles/ssf2t_linux_production_check[.]dir/.*[.]o ')"
if [ "$PROD_OK" = 1 ]; then record PASS "production sources compile and link on Linux ($PROD_COUNT source files, no game images)" "$A_PROD"
else record FAIL "production sources do not compile on Linux" "$A_PROD"; fi

echo "== Build the C++ suites"
cmake --build "$BUILD" --target ssf2t_runtime_tests -- -k 0 >"$TMP/suites.log" 2>&1
SUITES_CODE=$?
tail -3 "$TMP/suites.log"
if [ "$SUITES_CODE" = 0 ]; then record PASS "build the C++ suites" "$A_CORE"
else
  grep -E 'error|FAILED' "$TMP/suites.log" | head -20
  record FAIL "build the C++ suites" "$A_CORE"
fi

# ---- Scripts and the Worker --------------------------------------------------------------------------------------

check "policy self-test (GLIBC, RUNPATH, dependencies)" "$A_GLIBC;$A_RPATH;$A_DEPS;$A_PKG" policy_self_test
check "codegen scanner self-test" "$A_CORE" python3 -I "$SRC/tools/find-missing-functions.py" --self-test
check "generated guest code matches config/guest" "$A_B13;$A_TU" bash "$SRC/tools/check-guest-code.sh" "$SRC"
check "packaging scripts parse (package-linux.sh, test.sh, AppRun)" "$A_PKG" \
  bash -c "bash -n '$SRC/tools/package-linux.sh' && bash -n '$SRC/tools/test.sh' && bash -n '$SRC/tools/linux-policy.sh' && sh -n '$SRC/packaging/linux/AppRun'"
check "AppImage release pins (runtime source, patch and SHA-256, appimagetool, --runtime-file, wrong inputs refused)" "$A_PKG"   bash "$SRC/tools/test-appimage-release.sh" "$SRC"
check "online service (Worker, Node $($NODE --version))" "$A_WORKER;$A_NET;$A_RELAY;$A_PM;$A_MULTI" \
  bash -c "cd '$SRC/server/ssf2t-lobby' && '$NODE' --test 'test/*.test.js'"
check "fight telemetry (Worker, Node)" "$A_FIGHT;$A_WORKER;$A_NET;$A_MULTI" \
  bash -c "cd '$SRC/server/ssf2t-lobby' && '$NODE' --test test/fights.test.js test/wire.test.js"
check "recent matches (Worker, Node)" "$A_RECENT;$A_WORKER" \
  bash -c "cd '$SRC/server/ssf2t-lobby' && '$NODE' --test test/recent.test.js"
check "abuse protection (Worker, Node)" "$A_ABUSE;$A_WORKER" \
  bash -c "cd '$SRC/server/ssf2t-lobby' && '$NODE' --test test/abuse.test.js"
check "character and matchup analytics (Worker, Node)" "$A_CHARS;$A_MATCHUPS;$A_WORKER" \
  bash -c "cd '$SRC/server/ssf2t-lobby' && '$NODE' --test test/analytics.test.js"
check "dashboard (page and admin API, Node)" "$A_DASH;$A_WORKER" \
  bash -c "cd '$SRC/server/ssf2t-lobby' && '$NODE' --test test/dashboard.test.js test/admin.test.js"
check "fight telemetry mutations (Worker and dashboard)" "$A_FIGHT;$A_WORKER;$A_DASH;$A_RECENT" \
  bash -c "cd '$SRC/server/ssf2t-lobby' && '$NODE' test/mutations.mjs node"

# ---- C++ suites --------------------------------------------------------------------------------------------------

# what | executable and arguments | areas
SUITES=(
  "guest parity: counterparts in source|ssf2t_guest_parity_tests source|$A_B13;$A_TU;$A_PAIR;$A_HOOK"
  "guest parity: address pairs on the real images|ssf2t_guest_parity_tests pairs|$A_PAIR;$A_HOOK"
  "guest parity: Base013 fingerprints on the real image|ssf2t_guest_parity_tests base013|$A_B13;$A_HOOK"
  "guest parity: TU023 fingerprints on the real image|ssf2t_guest_parity_tests tu023|$A_TU;$A_HOOK"
  "global feature parity: source (every build and platform)|ssf2t_global_feature_parity_tests static|$A_GFP;$A_B13;$A_TU;$A_HOOK;$A_PLAT"
  "global feature parity: real images|ssf2t_global_feature_parity_tests images|$A_GFP;$A_B13;$A_TU;$A_HOOK"
  "guest build (both images in one program)|ssf2t_guest_build_tests|$A_B13;$A_TU"
  "game files|ssf2t_game_files_tests|$A_B13;$A_TU"
  "game files, base-only build|ssf2t_game_files_base_only_tests|$A_B13"
  "menu text|ssf2t_menu_text_tests|$A_HOOK;$A_PM"
  "localization pt-BR: registration, tables, placeholders, encoding (source)|ssf2t_localization_tests source|$A_LOC;$A_SET;$A_PLAT"
  "localization pt-BR: real game string tables and fonts|ssf2t_localization_tests game|$A_LOC;$A_B13;$A_TU;$A_HOOK"
  "localization pt-BR: runtime literals of both guest images and their reachability|ssf2t_localization_tests runtime|$A_LOC;$A_B13;$A_TU;$A_HOOK"
  "fight area|ssf2t_fight_area_tests|$A_HOOK;$A_DISP"
  "infinite time|ssf2t_infinite_time_tests|$A_HOOK;$A_KEYS;$A_SET"
  "wire bytes (tests/wire-vectors.txt)|ssf2t_wire_parity_tests|$A_WIRE;$A_NET;$A_RELAY"
  "platform parity|ssf2t_platform_parity_tests|$A_PLAT;$A_PKG;$A_CTRL"
  "user data|ssf2t_user_data_tests|$A_DATA"
  "relaunch|ssf2t_relaunch_tests|$A_DATA;$A_PLAT"
  "AppImage paths and the Linux self-updater (real folders)|ssf2t_linux_appimage_tests|$A_DATA;$A_UPD"
  "clipboard (src/clipboard_sdl.cpp on SDL's offscreen video driver)|ssf2t_clipboard_sdl_tests|$A_CLIP;$A_NET"
  "netplay packets|ssf2t_netplay_packet_tests|$A_NET;$A_RB"
  "netplay rollback|ssf2t_netplay_rollback_tests|$A_RB"
  "netplay session|ssf2t_netplay_session_tests|$A_RB;$A_FZ;$A_MULTI"
  "netplay guest|ssf2t_netplay_guest_tests|$A_RB;$A_FZ;$A_B13;$A_TU"
  "fight telemetry (reporter, guest reads on both images, queue, wire)|ssf2t_fight_telemetry_tests|$A_FIGHT;$A_B13;$A_TU;$A_NET;$A_RELAY;$A_MULTI;$A_HOOK"
  "netplay input|ssf2t_netplay_input_tests|$A_RB"
  "netplay state|ssf2t_netplay_state_tests|$A_RB;$A_FZ"
  "netplay online game socket (UDP loopback)|ssf2t_netplay_online_game_socket_tests|$A_RELAY;$A_NET"
  "netplay setting|ssf2t_netplay_setting_tests|$A_SET"
  "online protocol|ssf2t_online_protocol_tests|$A_NET"
  "online identity|ssf2t_online_identity_tests|$A_NET;$A_DATA"
  "online service (Find Match, Create Match, Invite, friends)|ssf2t_online_service_tests|$A_NET;$A_RELAY;$A_PM;$A_MULTI"
  "secret characters (negotiation rule, select gating, rollback host state, validation)|ssf2t_secret_select_tests|$A_NET;$A_RB;$A_B13;$A_TU;$A_MULTI"
  "violent ken (identity, navigation, negotiation, moves, gameplay package)|ssf2t_violent_ken_tests|$A_NET;$A_RB;$A_B13;$A_TU;$A_MULTI"
  "local join select (defender keeps Ultra cell, guest join path on both images)|ssf2t_local_join_select_tests|$A_B13;$A_TU"
  "evil ryu moves (recognizers, ground, render state)|ssf2t_evil_ryu_moves_tests|$A_B13;$A_TU"
  "debug hotkeys removed (F1, F4-F8 dispatch gone, real inputs wired)|ssf2t_debug_hotkeys_removed_tests|$A_B13;$A_TU"
  "console removed (Backtick bind, key reservation, logging kept)|ssf2t_console_removed_tests|$A_KEYS"
  "skip intro (setting, launcher row, gameflow patch, hooks of both builds)|ssf2t_skip_intro_tests|$A_B13;$A_TU;$A_HOOK;$A_SET"
  "akuma bug fixes (setting, Remix frame patches on both builds, Classic and online safety, rejection)|ssf2t_akuma_bug_fixes_tests|$A_B13;$A_TU;$A_HOOK;$A_SET"
  "versus cpu (opponent model, lobby patches, guest AI path on both images, hooks)|ssf2t_versus_cpu_tests|$A_B13;$A_TU;$A_HOOK"
  "character moves page, winner quotes and legacy text lines|ssf2t_character_moves_tests|$A_B13;$A_TU"
  "roster cells (Akuma and Ultra cells inside the native roster pass: lifetime, fade order, geometry, plan)|ssf2t_roster_cells_tests|$A_B13;$A_TU;$A_HOOK"
  "ultra endings (Evil Ryu and Violent Ken: package, text, routing, patches, guest switch)|ssf2t_ultra_ending_tests|$A_B13;$A_TU;$A_HOOK"
  "character packages (table, consumers, required sets, fixture validation)|ssf2t_character_packages_tests|$A_PKG;$A_B13;$A_TU"
  "violent ken audio (package, Ken event map, side routing, announcer)|ssf2t_violent_ken_audio_tests|$A_B13;$A_TU"
  "online rendezvous (STUN, UDP punch, relay selection)|ssf2t_online_rendezvous_tests|$A_RELAY;$A_NET"
  "online connection test|ssf2t_online_connection_tests|$A_NET;$A_RELAY"
  "friend code field|ssf2t_friend_field_tests|$A_NET"
  "bug report: log privacy scrubber|ssf2t_log_scrubber_tests|$A_NET;$A_DATA"
  "bug report: logs, preview, tickets, My reports, service calls|ssf2t_bug_report_tests|$A_NET;$A_DATA;$A_SET"
  "online setting|ssf2t_online_setting_tests|$A_SET"
  "player match|ssf2t_player_match_tests|$A_PM;$A_B13;$A_TU"
  "guest ui|ssf2t_guest_ui_tests|$A_PM"
  "player match net|ssf2t_player_match_net_tests|$A_PM"
  "online menu|ssf2t_online_menu_tests|$A_PM"
  "player match lobby|ssf2t_player_match_lobby_tests|$A_PM;$A_FZ;$A_MULTI"
  "launcher settings|ssf2t_launcher_settings_tests|$A_SET"
  "keyboard remapping and hotkeys|ssf2t_keyboard_tests|$A_KEYS;$A_CTRL"
  "player names|ssf2t_player_name_tests|$A_SET"
  "launcher quit|ssf2t_launcher_quit_tests|$A_CTRL"
  "launcher controller (issue #9: XInput on Windows, SDL on Linux)|ssf2t_launcher_gamepad_tests|$A_CTRL;$A_PLAT"
  "window placement|ssf2t_window_placement_tests|$A_DISP"
  "display scaling|ssf2t_dpi_tests|$A_DISP"
  "window host|ssf2t_window_host_tests|$A_DISP;$A_KEYS"
  "graphics card (Vulkan enumeration through a stand-in driver)|ssf2t_graphics_adapter_tests|$A_GPU;$A_SET"
  "D3D12 transfer short-circuit|ssf2t_d3d12_transfer_short_circuit_tests|$A_DISP"
  "D3D12 sampler memo|ssf2t_d3d12_sampler_memo_tests|$A_DISP"
  "shared memory fast path|ssf2t_shared_memory_fast_path_tests|$A_DISP"
  "GPU draw state memo|ssf2t_gpu_draw_state_memo_tests|$A_DISP"
  "D3D12 async submission|ssf2t_d3d12_async_submission_tests|$A_DISP"
  "GPU register wake|ssf2t_gpu_register_wake_tests|$A_DISP"
  "GPU fence wait|ssf2t_gpu_fence_wait_tests|$A_DISP"
  "achievements|ssf2t_achievement_tests|$A_ACH"
  "achievement metadata (real game files)|ssf2t_achievement_metadata_tests|$A_ACH"
  "updater (release JSON, ZIP, staging, rollback, AppImage target)|ssf2t_update_tests|$A_UPD;$A_DATA"
  "log path|ssf2t_log_path_tests|$A_DATA"
  "guest clock|ssf2t_guest_clock_tests|$A_CORE"
  "file dialog|ssf2t_file_dialog_tests|$A_CORE"
  "diagnostics|ssf2t_diagnostics_tests|$A_CORE"
  "crash report (SIGSEGV, SIGABRT, hard exit and normal exit in child processes)|ssf2t_crash_report_tests|$A_CRASH"
  "active log|ssf2t_active_log_tests|$A_CORE"
  "log filter and session log|ssf2t_log_filter_tests|$A_CORE;$A_CRASH"
  "lifecycle|ssf2t_lifecycle_tests|$A_CORE"
  "physical heap stress|ssf2t_physical_heap_stress|$A_CORE"
  "title thread shutdown|ssf2t_title_thread_shutdown_tests|$A_CORE"
)

for suite in "${SUITES[@]}"; do
  IFS='|' read -r what command areas <<<"$suite"
  read -r -a argv <<<"$command"
  if [ ! -x "$BUILD/${argv[0]}" ]; then
    record FAIL "$what (${argv[0]} was not built)" "$areas"
  elif [ "${argv[0]}" = ssf2t_global_feature_parity_tests ]; then
    check_global "$what" "$areas" "$BUILD/${argv[0]}" "${argv[@]:1}"
  else
    check "$what" "$areas" "$BUILD/${argv[0]}" "${argv[@]:1}"
  fi
done

# ---- Local lobby: plain, then behind TLS --------------------------------------------------------------------------

wait_for_port() {  # port
  for _ in $(seq 100); do
    (exec 3<>"/dev/tcp/127.0.0.1/$1") 2>/dev/null && return 0
    sleep 0.1
  done
  return 1
}

INTEGRATION="online integration (C++ client against the local lobby)"
INTEGRATION_AREAS="$A_NET;$A_RELAY;$A_PM;$A_MULTI;$A_WORKER"
PORT=$((18000 + RANDOM % 2000))
"$NODE" "$SRC/server/ssf2t-lobby/local/server.js" --host 127.0.0.1 --port "$PORT" --db :memory: >"$TMP/lobby.log" 2>&1 &
LOBBY_PID=$!
if wait_for_port "$PORT" && [ -x "$BUILD/ssf2t_online_integration_tests" ]; then
  echo "== $INTEGRATION"
  SSF2T_LOBBY_TEST_URL="ws://127.0.0.1:$PORT/" "$BUILD/ssf2t_online_integration_tests"
  [ $? = 0 ] && record PASS "$INTEGRATION" "$INTEGRATION_AREAS" || record FAIL "$INTEGRATION" "$INTEGRATION_AREAS"
else
  record FAIL "$INTEGRATION (the local lobby did not start: $(tr '\n' ' ' <"$TMP/lobby.log"))" "$INTEGRATION_AREAS"
fi

TLS_AREAS="$A_WS;$A_NET"
openssl req -x509 -newkey rsa:2048 -nodes -keyout "$TMP/key.pem" -out "$TMP/cert.pem" -days 2 -subj "/CN=localhost" \
  -addext "subjectAltName=DNS:localhost" >/dev/null 2>&1
TLS_PORT=$((20000 + RANDOM % 2000))
TLS_LOBBY_PORT=$((22000 + RANDOM % 2000))
"$NODE" "$SRC/server/ssf2t-lobby/local/server.js" --host 127.0.0.1 --port "$TLS_LOBBY_PORT" --db :memory: >"$TMP/tls-lobby.log" 2>&1 &
TLS_LOBBY_PID=$!
wait_for_port "$TLS_LOBBY_PORT"
"$NODE" "$SRC/tests/tls_front.mjs" "$TMP/key.pem" "$TMP/cert.pem" "$TLS_LOBBY_PORT" "$TLS_PORT" >"$TMP/tls.log" 2>&1 &
TLS_PID=$!
if [ -s "$TMP/cert.pem" ] && wait_for_port "$TLS_PORT"; then
  check "WebSocket over TLS: libssl loaded at run time, system certificate configuration honoured, bad certificates refused" \
    "$TLS_AREAS" env SSF2T_TLS_TEST_URL="wss://localhost:$TLS_PORT/" SSF2T_TLS_TEST_CA="$TMP/cert.pem" \
    "$BUILD/ssf2t_online_websocket_tls_tests"
  TLS_INTEGRATION="online integration over wss:// (C++ client, system OpenSSL, local lobby behind TLS)"
  echo "== $TLS_INTEGRATION"
  SSL_CERT_FILE="$TMP/cert.pem" SSL_CERT_DIR=/nonexistent SSF2T_LOBBY_TEST_URL="wss://localhost:$TLS_PORT/" \
    "$BUILD/ssf2t_online_integration_tests"
  [ $? = 0 ] && record PASS "$TLS_INTEGRATION" "$TLS_AREAS;$A_RELAY;$A_PM" || record FAIL "$TLS_INTEGRATION" "$TLS_AREAS;$A_RELAY;$A_PM"
else
  record FAIL "WebSocket over TLS (the TLS front or its certificate could not be created: $(tr '\n' ' ' <"$TMP/tls.log"))" "$TLS_AREAS"
fi

# ---- Wire vectors against the other platform and the Worker --------------------------------------------------------

VECTORS="$TMP/linux-vectors.txt"
"$BUILD/ssf2t_wire_parity_tests" --print >"$VECTORS"
[ -n "$EMIT_VECTORS" ] && cp "$VECTORS" "$EMIT_VECTORS"
check "Worker reads the vectors this Linux build produced" "$A_WIRE;$A_WORKER;$A_RELAY" \
  bash -c "cd '$SRC/server/ssf2t-lobby' && SSF2T_WIRE_VECTORS='$VECTORS' '$NODE' --test test/wire.test.js"
if [ -n "$PEER_VECTORS" ]; then
  check "Linux reads and reproduces the vectors the Windows build produced" "$A_WIRE;$A_NET;$A_RELAY;$A_CROSS" \
    "$BUILD/ssf2t_wire_parity_tests" --peer "$PEER_VECTORS"
  check "Worker reads the vectors the Windows build produced" "$A_WIRE;$A_WORKER;$A_RELAY;$A_CROSS" \
    bash -c "cd '$SRC/server/ssf2t-lobby' && SSF2T_WIRE_VECTORS='$PEER_VECTORS' '$NODE' --test test/wire.test.js"
fi

# ---- Binaries: GLIBC, RUNPATH, dependencies --------------------------------------------------------------------------

inspect_binaries() {
  local file label failures=0 highest="" glibc local_hits path
  echo "== Shipped runtime libraries and the production link"
  for file in "$SDK/lib/librexruntime.so" "$SDK/lib/librexgpu-xenos.so" "$BUILD/ssf2t_linux_production_check"; do
    [ -f "$file" ] || { echo "PARITY FAIL: missing $file"; failures=$((failures + 1)); continue; }
    check_elf_file "$(basename "$file")" "$file" ' libX11.so.6 libX11-xcb.so.1 libxcb.so.1 libwayland-client.so.0 ' ||
      failures=$((failures + $?))
  done
  echo "== Every test program (GLIBC versions)"
  for file in "$BUILD"/ssf2t_*; do
    [ -f "$file" ] && [ -x "$file" ] && [ "$(head -c 4 "$file")" = $'\x7fELF' ] || continue
    glibc="$(readelf -V -W "$file" 2>/dev/null | elf_glibc_text)"
    check_elf_glibc "$(basename "$file")" "$glibc" || failures=$((failures + 1))
    [ -n "$glibc" ] && highest="$(printf '%s\n%s\n' "$highest" "$glibc" | sort -V | tail -1)"
  done
  for file in "$SDK/lib/librexruntime.so" "$SDK/lib/librexgpu-xenos.so" "$BUILD/ssf2t_linux_production_check"; do
    glibc="$(readelf -V -W "$file" 2>/dev/null | elf_glibc_text)"
    [ -n "$glibc" ] && highest="$(printf '%s\n%s\n' "$highest" "$glibc" | sort -V | tail -1)"
  done
  echo "@@ENV highest_glibc=GLIBC_$highest"
  echo "  highest GLIBC symbol version required by any Linux binary built or shipped here: GLIBC_$highest (baseline GLIBC_$MAX_GLIBC)"
  echo "== Local paths in the production link"
  for path in "$ROOT" "$WORK" "$HOME" 'S:\' "/mnt/s/"; do
    if grep -qaF -- "$path" "$BUILD/ssf2t_linux_production_check"; then
      echo "PARITY FAIL: the production link contains the local path $path"
      failures=$((failures + 1))
    fi
  done
  return $failures
}

echo "== Binary inspection"
BIN_LOG="$TMP/binaries.log"
inspect_binaries 2>&1 | tee "$BIN_LOG"
BIN_FAILURES=${PIPESTATUS[0]}
glibc_fail="$(grep -c 'requires GLIBC' "$BIN_LOG")"
if [ "$glibc_fail" = 0 ] && [ "$PROD_OK" = 1 ]; then record PASS "no Linux binary needs a glibc newer than $MAX_GLIBC ($(grep -o 'GLIBC_[0-9.]*  *(baseline' "$BIN_LOG" | head -1 | sed 's/ (baseline//'))" "$A_GLIBC"
else record FAIL "a Linux binary needs a glibc newer than $MAX_GLIBC, or none could be inspected" "$A_GLIBC"; fi
if ! grep -q 'PARITY FAIL: Linux RPATH/RUNPATH' "$BIN_LOG" && [ "$PROD_OK" = 1 ]; then
  record PASS "RPATH/RUNPATH of the production link and the shipped runtime libraries is \$ORIGIN only" "$A_RPATH"
else record FAIL "RPATH/RUNPATH leaks a path or could not be inspected" "$A_RPATH"; fi
if ! grep -q 'PARITY FAIL: Linux binary\|contains the local path' "$BIN_LOG" && [ "$PROD_OK" = 1 ]; then
  record PASS "dynamic dependencies of the production link and the shipped runtime libraries are as intended (no libssl, no RenderDoc, no layers, no local paths)" "$A_DEPS"
else record FAIL "unexpected dynamic dependencies or local paths in a Linux binary" "$A_DEPS"; fi
[ "$BIN_FAILURES" = 0 ] || echo "  ($BIN_FAILURES binary check(s) failed)"

# ---- Packaging: a fixture AppDir staged and scanned by the release policy, then run as an AppImage ---------------------

package_fixture() {
  local fix="$TMP/package" bin="$TMP/package/bin" appdir="$TMP/package/AppDir" id package
  rm -rf "$fix"
  mkdir -p "$bin/assets/launcher"
  policy_init "$SRC"
  id="$APP_ID"
  cat >"$fix/stub.c" <<STUB
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static const char* const kApplicationId = "$id";
int main(int argc, char** argv) {
  const char* image = getenv("APPIMAGE");
  const char* appdir = getenv("APPDIR");
  char exe[4096] = {0};
  if (readlink("/proc/self/exe", exe, sizeof exe - 1) < 0) exe[0] = 0;
  printf("STUB id=%s appimage=%s appdir=%s exe=%s argc=%d\n", kApplicationId, image ? image : "", appdir ? appdir : "", exe, argc);
  if (image) {
    char copy[4096];
    snprintf(copy, sizeof copy, "%s", image);
    char folder[4096];
    snprintf(folder, sizeof folder, "%s/user_data", dirname(copy));
    mkdir(folder, 0755);
    char file[4200];
    snprintf(file, sizeof file, "%s/stub.txt", folder);
    FILE* out = fopen(file, "w");
    if (out) { fprintf(out, "%s\n", appdir ? appdir : ""); fclose(out); }
  }
  return argc > 1 && strcmp(argv[1], "--exit7") == 0 ? 7 : 0;
}
STUB
  printf 'void ssf2t_stub_library(void) {}\n' >"$fix/lib.c"
  clang-20 -O1 -g -Wl,-rpath,'$ORIGIN' "$fix/stub.c" -o "$bin/$BIN_NAME" || return 1
  clang-20 -shared -fPIC -O1 -Wl,-rpath,'$ORIGIN' "$fix/lib.c" -o "$bin/librexruntime.so" || return 1
  clang-20 -shared -fPIC -O1 -Wl,-rpath,'$ORIGIN' "$fix/lib.c" -o "$bin/librexgpu-xenos.so" || return 1
  cp "$SRC/config/ssf2t_hd_remix.toml" "$bin/$BIN_NAME.toml"
  cp "$SRC/assets/launcher/header-art.png" "$bin/assets/launcher/header-art.png"
  cp "$SRC/assets/launcher/sponsor-heart.png" "$bin/assets/launcher/sponsor-heart.png"
  mkdir -p "$bin/assets/characters"
  for package in $CHARACTER_PACKAGES; do printf 'placeholder' >"$bin/assets/characters/$package"; done
  stage_appdir "$appdir" "$bin" "$fix/symbols" "$SRC"
}

echo "== Staging a fixture AppDir"
if package_fixture; then
  record PASS "fixture AppDir staged by the packaging script's own staging function" "$A_PKG"
else
  record FAIL "the fixture AppDir could not be staged" "$A_PKG"
fi

APPDIR_FIXTURE="$TMP/package/AppDir"
echo "== Release policy on the fixture AppDir"
if [ -d "$APPDIR_FIXTURE" ] && ( scan_appdir "$APPDIR_FIXTURE" "fixture AppDir" "$ROOT" "$WORK" "$HOME" ); then
  record PASS "release policy accepts the staged fixture AppDir (layout, licenses, README, desktop file, icons, AppRun, RUNPATH, glibc, no local paths)" "$A_PKG;$A_GLIBC;$A_RPATH"
else
  record FAIL "release policy rejects the staged fixture AppDir" "$A_PKG"
fi

forbidden_case() {  # description reason-the-scan-must-give setup-command (run inside a copy of the AppDir)
  local copy="$TMP/forbidden" report
  rm -rf "$copy"
  cp -a "$APPDIR_FIXTURE" "$copy"
  ( cd "$copy" && eval "$3" ) >/dev/null 2>&1
  report="$( ( scan_appdir "$copy" "fixture AppDir" "$ROOT" "$WORK" "$HOME" ) 2>&1 )"
  case "$report" in
    *"$2"*) ;;
    *) echo "PARITY FAIL: the release policy does not reject $1 for the right reason (expected '$2')"; return 1 ;;
  esac
}

# Each case adds one forbidden thing to a copy of the clean AppDir; the scan must name that rule, not only notice an unlisted file.
forbidden_policy() {
  local failed=0 name="forbidden name" type="forbidden file type" layer="RenderDoc, Tracy or Vulkan layer file"
  forbidden_case "default.xex" "$name" "touch usr/bin/default.xex" || failed=1
  forbidden_case "a title update default.xexp" "$name" "touch usr/bin/default.xexp" || failed=1
  forbidden_case "game data" "$name" "mkdir usr/bin/game && touch usr/bin/game/x" || failed=1
  forbidden_case "content" "$name" "mkdir usr/bin/content" || failed=1
  forbidden_case "a SystemUpdate folder" "$name" "mkdir 'usr/bin/\$SystemUpdate'" || failed=1
  forbidden_case "user_data" "$name" "mkdir usr/bin/user_data" || failed=1
  forbidden_case "a settings.toml leftover" "$name" "touch usr/bin/settings.toml" || failed=1
  forbidden_case "saves" "$type" "touch usr/bin/save.sav" || failed=1
  forbidden_case "logs" "$type" "touch usr/bin/ssf2t.log" || failed=1
  forbidden_case "an ISO image" "$type" "touch usr/bin/game.iso" || failed=1
  forbidden_case "an Xbox executable file" "$type" "touch usr/bin/image.xex" || failed=1
  forbidden_case "a RenderDoc capture" "$type" "touch usr/bin/capture.rdc" || failed=1
  forbidden_case "a git file" "$type" "touch usr/bin/.gitignore" || failed=1
  forbidden_case "an .orig leftover" "$type" "touch usr/bin/a.orig" || failed=1
  forbidden_case "a .rej leftover" "$type" "touch usr/bin/a.rej" || failed=1
  forbidden_case "a temporary file" "$type" "touch usr/bin/a.tmp" || failed=1
  forbidden_case "an editor backup" "$type" "touch 'usr/bin/a~'" || failed=1
  forbidden_case "a debug symbol file" "$type" "touch usr/bin/program.debug" || failed=1
  forbidden_case "TracyClient" "$layer" "touch usr/bin/TracyClient.so" || failed=1
  forbidden_case "a RenderDoc library" "$layer" "touch usr/bin/librenderdoc.so" || failed=1
  forbidden_case "a Vulkan layer manifest" "$layer" "touch usr/bin/VkLayer_test.json" || failed=1
  forbidden_case "an Xbox 360 package" "Xbox 360 package or executable" "printf 'LIVE0000' > usr/bin/package.bin" || failed=1
  forbidden_case "an Xbox 360 executable" "Xbox 360 package or executable" "printf 'XEX20000' > usr/bin/image.bin" || failed=1
  forbidden_case "a Windows binary" "Windows binary" "printf 'MZ000000' > usr/bin/tool.bin" || failed=1
  forbidden_case "a missing character package" "unexpected AppDir contents" "rm usr/bin/assets/characters/ultra_portraits.bin" || failed=1
forbidden_case "an unknown character package" "unexpected AppDir contents" "touch usr/bin/assets/characters/other.bin" || failed=1
forbidden_case "a package outside its folder" "unexpected AppDir contents" "cp usr/bin/assets/characters/ultra_portraits.bin usr/bin/" || failed=1
forbidden_case "DEBUG_SAULO diagnostics" "DEBUG_SAULO in file" "echo DEBUG_SAULO >> usr/share/doc/$BIN_NAME/LICENSE" || failed=1
  forbidden_case "a local path inside a file" "local path in file" "echo '$ROOT' >> usr/share/doc/$BIN_NAME/LICENSE" || failed=1
  forbidden_case "a symbolic link" "symbolic link" "ln -s usr/bin/$BIN_NAME link" || failed=1
  forbidden_case "an unlisted file" "unexpected AppDir contents" "touch usr/bin/extra.bin" || failed=1
  forbidden_case "an absolute RUNPATH on the program" "RPATH/RUNPATH" "patchelf --set-rpath /opt/lib usr/bin/$BIN_NAME" || failed=1
  forbidden_case "a build folder as RUNPATH of the runtime" "RPATH/RUNPATH" "patchelf --set-rpath '\$ORIGIN:$WORK/tools/rexglue/linux-amd64/lib' usr/bin/librexruntime.so" || failed=1
  forbidden_case "a program without an \$ORIGIN RUNPATH" "RPATH/RUNPATH" "patchelf --remove-rpath usr/bin/$BIN_NAME" || failed=1
  forbidden_case "a desktop file naming another window class" "StartupWMClass" "sed -i 's/^StartupWMClass=.*/StartupWMClass=other/' $APP_ID.desktop" || failed=1
  return $failed
}

if [ -d "$APPDIR_FIXTURE" ]; then
  check "release policy rejects every forbidden kind of content, absolute RUNPATH and local path" "$A_PKG;$A_RPATH" forbidden_policy
fi

echo "== Fixture AppImage (pinned appimagetool and patched runtime; the stub program only prints its environment)"
appimage_fixture() {
  local out="$TMP/appimage" image failed=0 output status
  rm -rf "$out"
  mkdir -p "$out"
  ensure_appimage_tools "$SRC" "$WORK" || return 1
  image="$out/SSF2THDRemix-v0.0.0-x86_64.AppImage"
  ARCH=x86_64 "$APPIMAGETOOL" --appimage-extract-and-run --no-appstream --runtime-file "$APPIMAGE_RUNTIME" \
    "$APPDIR_FIXTURE" "$image" >/dev/null 2>&1 || { echo "PARITY FAIL: appimagetool could not build the fixture AppImage"; return 1; }
  chmod 755 "$image"
  bash "$SRC/tools/build-appimage-runtime.sh" --check-image "$image" || { echo "PARITY FAIL: the fixture AppImage does not start with the reviewed runtime"; failed=1; }
  cp "$image" "$image.tampered"
  printf 'X' | dd of="$image.tampered" bs=1 seek=5000 conv=notrunc 2>/dev/null
  if bash "$SRC/tools/build-appimage-runtime.sh" --check-image "$image.tampered" >/dev/null 2>&1; then
    echo "PARITY FAIL: an AppImage with an altered runtime was accepted"; failed=1
  fi
  rm -f "$image.tampered"
  bash "$SRC/tools/build-appimage-runtime.sh" --check "$image" || { echo "PARITY FAIL: the fixture AppImage does not carry the pinned runtime"; failed=1; }

  echo "-- extract-and-run"
  output="$(cd "$out" && APPIMAGE_EXTRACT_AND_RUN=1 "$image" one two 2>&1)"; status=$?
  echo "$output" | sed 's/^/   /'
  [ "$status" = 0 ] || { echo "PARITY FAIL: extract-and-run exited $status"; failed=1; }
  case "$output" in *"appimage=$image "*) ;; *) echo "PARITY FAIL: extract-and-run does not expose the original AppImage path"; failed=1 ;; esac
  case "$output" in *"appdir=/tmp/appimage_extracted_"*) ;; *) echo "PARITY FAIL: extract-and-run did not run from an extracted folder"; failed=1 ;; esac
  [ -f "$out/user_data/stub.txt" ] || { echo "PARITY FAIL: user data was not written beside the AppImage"; failed=1; }
  extracted="$(echo "$output" | sed -n 's/.* appdir=\([^ ]*\) .*/\1/p' | head -1)"
  [ -n "$extracted" ] && [ ! -e "$extracted" ] || { echo "PARITY FAIL: the extracted folder ${extracted:-?} was left behind"; failed=1; }
  rm -rf "$out/user_data"

  echo "-- exit status is passed through"
  (cd "$out" && APPIMAGE_EXTRACT_AND_RUN=1 "$image" --exit7 >/dev/null 2>&1); status=$?
  [ "$status" = 7 ] || { echo "PARITY FAIL: the program's exit status 7 became $status"; failed=1; }
  rm -rf "$out/user_data"

  echo "-- FUSE unavailable: the runtime falls back to extract-and-run"
  # /dev/fuse is replaced inside a private mount namespace, so the kernel refuses the mount as it does where FUSE is missing.
  output="$(cd "$out" && unshare -m bash -c 'mount --bind /dev/null /dev/fuse && exec "$0" fallback' "$image" 2>&1)"; status=$?
  echo "$output" | sed 's/^/   /'
  [ "$status" = 0 ] || { echo "PARITY FAIL: the AppImage did not start without FUSE (exit $status)"; failed=1; }
  case "$output" in *"argc=2"*) ;; *) echo "PARITY FAIL: the program did not run without FUSE"; failed=1 ;; esac
  case "$output" in *"appdir=/tmp/appimage_extracted_"*) ;; *) echo "PARITY FAIL: without FUSE the program did not run from an extracted folder"; failed=1 ;; esac
  [ -f "$out/user_data/stub.txt" ] || { echo "PARITY FAIL: user data was not written beside the AppImage without FUSE"; failed=1; }
  rm -rf "$out/user_data"
  return $failed
}
if [ -d "$APPDIR_FIXTURE" ]; then
  check "fixture AppImage: pinned runtime, extract-and-run, original AppImage path, external user_data, FUSE fallback" \
    "$A_PKG;$A_DATA" appimage_fixture
fi

# ---- Summary -----------------------------------------------------------------------------------------------------------

FAILED=0
for result in "${RESULTS[@]}"; do
  IFS='|' read -r status _ _ <<<"$result"
  [ "$status" = PASS ] || FAILED=$((FAILED + 1))
done
if [ "$FAILED" = 0 ]; then record PASS "every Linux check passed" "$A_GATE"; else record FAIL "$FAILED Linux check(s) failed" "$A_GATE"; fi

echo
echo "Linux parity ($(. /etc/os-release && echo "$PRETTY_NAME"), glibc $(getconf GNU_LIBC_VERSION | cut -d' ' -f2)):"
echo
for area in "${AREAS[@]}"; do
  pass=0 fail=0
  for result in "${RESULTS[@]}"; do
    IFS='|' read -r status _ areas <<<"$result"
    case ";$areas;" in *";$area;"*) ;; *) continue ;; esac
    [ "$status" = PASS ] && pass=$((pass + 1)) || fail=$((fail + 1))
  done
  if [ "$fail" -gt 0 ]; then text="FAIL ($fail of $((pass + fail)) checks)"
  elif [ "$pass" = 0 ]; then text="FAIL (no check ran)"
  else text="PASS ($pass checks)"
  fi
  printf '  %-34s%s\n' "$area" "$text"
done
echo
for result in "${RESULTS[@]}"; do
  IFS='|' read -r status what _ <<<"$result"
  [ "$status" = PASS ] || echo "  PARITY FAIL: $what"
done
[ "$FAILED" = 0 ] || exit 1
echo "All Linux parity checks passed."
