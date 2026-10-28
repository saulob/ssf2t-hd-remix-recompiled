#!/usr/bin/env bash
# Checks that generated/default was written by tools/codegen.ps1 for the supported default.xex and its Title Update
# default.xexp. Codegen itself only runs on Windows. Exit status: 0 current, 2 stale or unverifiable.
#
#   tools/check-guest-code.sh [ROOT]
set -euo pipefail

ROOT="$(cd "${1:-$(dirname "$0")/..}" && pwd)"
OUT="$ROOT/generated/default"
XEX_SHA256=6e1c2f9ed7d1102a311a5f5626563527eda5f85b2fbe8db8405b866ded3f31ee
XEXP_SHA256=a2e8e66b482c5d3a0d3a5d3f67a3cecb7a12ce1f6189196694fd3e765910606c

stale() { echo "Guest code: stale ($*)" >&2; echo "Run tools\codegen.ps1 on Windows, then build again." >&2; exit 2; }

[ -f "$OUT/sources.cmake" ] || stale "generated/default is missing"
[ -f "$OUT/codegen.stamp" ] || stale "no successful codegen record"
while IFS= read -r output; do
  [ -f "$OUT/$output" ] || stale "$output is missing"
done < <(sed -n '/"outputs"/,/\]/p' "$OUT/codegen.stamp" | grep -o '"[^"]*"' | tr -d '"' | grep -v '^outputs$')

xex="$(find "$ROOT/game/extracted" -maxdepth 1 -iname default.xex 2>/dev/null | head -n 1)"
[ -n "$xex" ] || stale "game/extracted/default.xex is missing"
[ "$(sha256sum "$xex" | cut -d' ' -f1)" = "$XEX_SHA256" ] || stale "default.xex is not the supported version"
xexp="$ROOT/game/extracted/default.xexp"
[ -f "$xexp" ] || stale "game/extracted/default.xexp (Title Update) is missing"
[ "$(sha256sum "$xexp" | cut -d' ' -f1)" = "$XEXP_SHA256" ] || stale "default.xexp is not the supported Title Update"
echo "Guest code: current"
