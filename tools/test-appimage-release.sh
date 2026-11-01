#!/usr/bin/env bash
# Static and negative checks of the pinned AppImage release inputs: runtime source, runtime patch, appimagetool and the
# way they are combined. Needs no network and builds nothing.
#
#   tools/test-appimage-release.sh [ROOT]
set -uo pipefail

ROOT="$(cd "${1:-$(dirname "$0")/..}" && pwd)"
PATCH_DIR="$ROOT/patches/appimage-runtime"

EXPECTED_RUNTIME_TAG=20251108
EXPECTED_RUNTIME_COMMIT=dd6cebedcbddde9c82f89b011e8e1d40b6e43868
EXPECTED_RUNTIME_SHA256=9e458da0e30e2dd06a206af0f345d98a872bf7c1ce69eba31d44e139488e4ab8
EXPECTED_ALPINE_SHA256=55ea3e5a7c2c35e6268c5dcbb8e45a9cd5b0e372e7b4e798499a526834f7ed90
EXPECTED_PATCH_SHA256=9abff6b867bb89a228df024035f28b229ca1149ea1cc172ebc10b2744b798a4e
EXPECTED_APPIMAGETOOL_VERSION=1.9.1
EXPECTED_APPIMAGETOOL_SHA256=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0

failures=0
checks=0
ok() { checks=$((checks + 1)); echo "PASS: $1"; }
bad() { checks=$((checks + 1)); failures=$((failures + 1)); echo "FAIL: $1"; }
expect() {  # what condition-exit-status
  if [ "$2" = 0 ]; then ok "$1"; else bad "$1"; fi
}

# shellcheck source=../patches/appimage-runtime/baseline.env
. "$PATCH_DIR/baseline.env"
APPIMAGETOOL_VERSION=""
APPIMAGETOOL_SHA256=""
fail() { echo "ERROR: $*" >&2; exit 1; }
# shellcheck source=linux-work.sh
. "$ROOT/tools/linux-work.sh"

[ "$RUNTIME_TAG" = "$EXPECTED_RUNTIME_TAG" ] && [ "$RUNTIME_COMMIT" = "$EXPECTED_RUNTIME_COMMIT" ]
expect "the runtime source is pinned to type2-runtime $EXPECTED_RUNTIME_TAG (${EXPECTED_RUNTIME_COMMIT:0:7})" $?

[ "$RUNTIME_SHA256" = "$EXPECTED_RUNTIME_SHA256" ] && [ "$ALPINE_SHA256" = "$EXPECTED_ALPINE_SHA256" ]
expect "the reviewed runtime and Alpine root file system SHA-256 are pinned" $?

[ "$(sha256sum "$PATCH_DIR/0001-extract-and-run-when-fuse-fails.patch" | cut -d' ' -f1)" = "$EXPECTED_PATCH_SHA256" ] &&
  [ "$(find "$PATCH_DIR" -name '*.patch' | wc -l)" = 1 ]
expect "the runtime patch is the reviewed extract-and-run patch and the only one" $?

[ "$APPIMAGETOOL_VERSION" = "$EXPECTED_APPIMAGETOOL_VERSION" ] && [ "$APPIMAGETOOL_SHA256" = "$EXPECTED_APPIMAGETOOL_SHA256" ]
expect "appimagetool $EXPECTED_APPIMAGETOOL_VERSION is pinned by SHA-256" $?

grep -q 'releases/download/\$APPIMAGETOOL_VERSION/' "$ROOT/tools/linux-work.sh"
expect "appimagetool is downloaded from its versioned release" $?

if grep -n -i -E 'releases/download/(continuous|latest)|/latest/download' "$ROOT"/tools/*.sh | grep -v 'test-appimage-release.sh' | grep -q .; then
  bad "no script downloads a continuous or latest AppImage runtime"
else
  ok "no script downloads a continuous or latest AppImage runtime"
fi

calls="$(grep -h -E 'ARCH=x86_64 "\$(TOOL|APPIMAGETOOL)"' "$ROOT/tools/package-linux.sh" "$ROOT/tools/test.sh")"
[ "$(wc -l <<<"$calls")" = 2 ] && [ "$(grep -c -- '--runtime-file' <<<"$calls")" = 2 ]
expect "the release build and the fixture AppImage are the only appimagetool calls and both pass --runtime-file" $?

grep -q -- '--check-image' "$ROOT/tools/package-linux.sh"
expect "the release build checks the runtime at the front of the finished AppImage" $?

grep -q 'APPIMAGE_EXTRACT_AND_RUN\|extract-and-run\|extract_and_run' "$PATCH_DIR/0001-extract-and-run-when-fuse-fails.patch"
expect "the runtime patch keeps the extract-and-run fallback" $?

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

echo content > "$work/tool"
good="$(sha256sum "$work/tool" | cut -d' ' -f1)"
( download "file://$work/missing" "$work/tool" "$good" ) >/dev/null 2>&1
expect "a download that already matches its pinned SHA-256 is accepted" $?

( download "file://$work/missing" "$work/tool" "$(printf 'x%.0s' {1..64})" ) >/dev/null 2>&1
[ $? -ne 0 ]
expect "appimagetool with the wrong SHA-256 is refused" $?

bash "$ROOT/tools/build-appimage-runtime.sh" --check /bin/true >/dev/null 2>&1
[ $? -ne 0 ]
expect "a program that is not the patched runtime is refused" $?

bash "$ROOT/tools/build-appimage-runtime.sh" --check-image /bin/true >/dev/null 2>&1
[ $? -ne 0 ]
expect "an AppImage that does not start with the reviewed runtime is refused" $?

echo "$checks checks, $failures failures"
[ "$failures" = 0 ]
