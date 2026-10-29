#!/usr/bin/env bash
# Checks that generated/base and generated/tu, the recompiled game versions 0.0.1.3 and 0.0.2.3, were written by
# tools/codegen.ps1 for the current config/guest sources. Codegen itself only runs on Windows, and the game files are
# not needed to check. Exit status: 0 current, 2 stale or unverifiable.
#
#   tools/check-guest-code.sh [ROOT] [--all]
# generated/tu is checked when present; --all (releases) requires it.
set -euo pipefail

ROOT="$(cd "${1:-$(dirname "$0")/..}" && pwd)"
REQUIRE_ALL=false
[ "${2:-}" = --all ] && REQUIRE_ALL=true
SDK_VERSION=0.10.0
XEX_SHA256=6e1c2f9ed7d1102a311a5f5626563527eda5f85b2fbe8db8405b866ded3f31ee
XEXP_SHA256=a2e8e66b482c5d3a0d3a5d3f67a3cecb7a12ce1f6189196694fd3e765910606c

stale() { echo "Guest code: stale ($*)" >&2; echo "Run tools\\codegen.ps1 on Windows, then build again." >&2; exit 2; }

# Line endings are ignored, as in tools/guest-code.ps1.
sha() { tr -d '\r' < "$1" | sha256sum | cut -d' ' -f1; }

check_image() {
  local image="$1" prefix="$2" xexp="$3"
  local out="$ROOT/generated/$image" config="$ROOT/config/guest/$image"
  [ -f "$out/sources.cmake" ] || stale "generated/$image is missing"
  [ -f "$out/ssf2t-guest.stamp" ] || stale "generated/$image has no successful codegen record"
  local expected
  expected="sdk=$SDK_VERSION
prefix=$prefix
xex=$XEX_SHA256
xexp=$xexp
manifest=$(sha "$config/manifest.template.toml")
overrides=$(sha "$config/overrides.toml")
discovered=$(sha "$config/discovered-functions.toml")"
  local actual
  actual="$(tr -d '\r' < "$out/ssf2t-guest.stamp" | grep -v '^prefix-rules=')"
  [ "$actual" = "$expected" ] || stale "generated/$image does not match config/guest/$image"
  for fixed in ssf2t_hd_remix_pch.h ssf2t_hd_remix_init.h ssf2t_hd_remix_funcs.h ssf2t_hd_remix_init.cpp ssf2t_hd_remix_register.cpp; do
    [ -f "$out/$fixed" ] || stale "generated/$image/$fixed is missing"
  done
  while IFS= read -r source; do
    [ -f "$out/$source" ] || stale "generated/$image/$source is missing"
    case "$source" in *_recomp.*.cpp) part="${source%.cpp}"; [ -f "$out/$(echo "$part" | sed 's/_recomp\./_funcs./').h" ] || stale "generated/$image/$part declarations are missing" ;; esac
  done < <(grep -o '[A-Za-z0-9_.]*\.cpp' "$out/sources.cmake" | sort -u)
}

check_image base base013_ none
if [ -f "$ROOT/generated/tu/ssf2t-guest.stamp" ] || $REQUIRE_ALL; then check_image tu tu023_ "$XEXP_SHA256"; fi
echo "Guest code: current"
