#!/usr/bin/env bash
# The normal Linux build. Makes sure tools/rexglue/linux-amd64 holds the ReXGlue runtime and GPU plugin built from this
# branch's patches, building them with tools/build-patched-rexruntime.sh when it does not (git and network access the
# first time), then configures and builds the matching preset into out/build/<preset>. The Debug libraries stay stock.
#
#   tools/build.sh                  linux-amd64-release
#   tools/build.sh relwithdebinfo   linux-amd64-relwithdebinfo (or debug)
#
# Needs the extracted linux-amd64 SDK in tools/rexglue and generated/default (tools/codegen.ps1 on Windows).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="$ROOT/tools/rexglue/linux-amd64"
PATCHED="$ROOT/tools/build-patched-rexruntime.sh"

fail() { echo "ERROR: $*" >&2; exit 1; }

case "${1:-release}" in
  release) CONFIG=Release ;;
  relwithdebinfo) CONFIG=RelWithDebInfo ;;
  debug) CONFIG=Debug ;;
  *) fail "usage: $0 [release|relwithdebinfo|debug]" ;;
esac
PRESET="linux-amd64-${1:-release}"
[ "$(uname -s)" = Linux ] || fail "builds on Linux only"
[ -d "$SDK/lib/cmake/rexglue" ] || fail "missing $SDK (extract rexglue-sdk-0.10.0-linux-amd64.zip into tools/rexglue)"
bash "$ROOT/tools/check-guest-code.sh" "$ROOT" || fail "generated/default is not current (run tools/codegen.ps1 on Windows first)"

if [ "$CONFIG" != Debug ] && ! bash "$PATCHED" --check "$SDK" >/dev/null 2>&1; then
  echo "== Building the patched ReXGlue runtime and GPU plugin"
  bash "$PATCHED" "$SDK"
fi

cd "$ROOT"
cmake --preset "$PRESET"
cmake --build --preset "$PRESET"
echo "Built out/build/$PRESET/ssf2t_hd_remix"
