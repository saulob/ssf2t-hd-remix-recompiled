#!/usr/bin/env bash
# Headless Linux checks: the codegen scanner self-test and the runtime and logic tests, built from the Release preset
# folder. Nothing opens a window, plays audio or needs the game files (the achievement metadata test skips without them).
#
#   tools/test.sh                   configure and build ssf2t_runtime_tests in out/build/linux-amd64-release, then run them
#   tools/test.sh --build-dir DIR   use an already configured Release build folder
#
# Needs the patched runtime in tools/rexglue/linux-amd64 (tools/build.sh builds it).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/out/build/linux-amd64-release"
if [ "${1:-}" = --build-dir ]; then
  [ $# -ge 2 ] || { echo "--build-dir needs a folder" >&2; exit 2; }
  BUILD="$(realpath "$2")"
fi

[ "$(uname -s)" = Linux ] || { echo "ERROR: runs on Linux only (tools/test.ps1 is the Windows script)" >&2; exit 1; }

run() {
  echo "== $1"
  shift
  "$@"
}

if command -v python3 >/dev/null; then
  run "scanner self-test" python3 -I "$ROOT/tools/find-missing-functions.py" --self-test
fi

if [ ! -f "$BUILD/build.ninja" ]; then
  (cd "$ROOT" && cmake --preset linux-amd64-release >/dev/null)
fi
cmake --build "$BUILD" --target ssf2t_runtime_tests

run "window placement" "$BUILD/ssf2t_window_placement_tests"
run "menu text" "$BUILD/ssf2t_menu_text_tests"
run "launcher settings" "$BUILD/ssf2t_launcher_settings_tests"
run "game files" "$BUILD/ssf2t_game_files_tests"
run "achievements" "$BUILD/ssf2t_achievement_tests"
run "achievement metadata" "$BUILD/ssf2t_achievement_metadata_tests"
run "physical heap stress" "$BUILD/ssf2t_physical_heap_stress"
run "title thread shutdown" "$BUILD/ssf2t_title_thread_shutdown_tests"
echo "All headless checks passed."
