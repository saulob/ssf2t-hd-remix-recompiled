#!/usr/bin/env bash
# The Linux work tree that tools/test.sh builds in, on the Linux filesystem (a Windows drive under /mnt is slow and
# its files all look executable). Source it after defining fail(); nothing runs until a function is called.
#
#   sync_sources ROOT WORK    copy the repository into WORK (the game files stay in ROOT and are linked)
#   ensure_sdk ROOT WORK      the extracted ReXGlue SDK in WORK, with the runtime and GPU plugin built from the patches
#   ensure_node WORK          sets NODE to a Node.js that has node:sqlite (the local lobby needs it)
#   ensure_appimage_tools ROOT WORK   sets APPIMAGETOOL and APPIMAGE_RUNTIME (pinned appimagetool, patched runtime)

APPIMAGETOOL_VERSION=1.9.1
APPIMAGETOOL_SHA256=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0
NODE_VERSION=24.18.1
NODE_SHA256=d6c664df3f3f61458e8c277585571328522d705166723a7c7823a9253a4d15a0

sync_sources() {
  local root="$1" work="$2"
  command -v rsync >/dev/null || fail "rsync not found"
  mkdir -p "$work"
  rsync -a --delete --exclude .git --exclude node_modules --exclude /.claude \
    --exclude /out --exclude /AppDir --exclude /rexglue-sdk-src --exclude /appimage-runtime \
    --exclude /tools/rexglue --exclude /tools/appimage --exclude /tools/node \
    --exclude /game --exclude /dist --exclude /build --exclude /build-release "$root/" "$work/"
  # A Windows checkout has CRLF sources; the TRG patches are applied with git apply and need matching endings.
  find "$work/third_party/trg-launcher" "$work/patches/trg-launcher" -type f \
    \( -name '*.patch' -o -name '*.h' -o -name '*.cpp' -o -name '*.c' -o -name '*.txt' -o -name '*.cmake' \) \
    -exec sed -i 's/\r$//' {} +
  # Fixtures stay where they are: the real game images are read in place, never copied.
  [ -L "$work/game" ] || rm -rf "$work/game"
  ln -sfn "$root/game" "$work/game"
}

ensure_sdk() {
  local root="$1" work="$2" baseline="$1/patches/rexglue-sdk/baseline.env" version sha zip sdk="$2/tools/rexglue/linux-amd64"
  version="$(sed -n 's|^SDK_VERSION=||p' "$baseline" | tr -d '\r')"
  sha="$(sed -n 's|^SDK_LINUX_ZIP_SHA256=||p' "$baseline" | tr -d '\r' | tr 'A-F' 'a-f')"
  if [ ! -f "$sdk/lib/cmake/rexglue/rexglueConfigVersion.cmake" ]; then
    zip="$root/tools/rexglue/rexglue-sdk-$version-linux-amd64.zip"
    [ -f "$zip" ] || fail "missing $zip (tools/codegen.ps1 downloads it)"
    echo "$sha  $zip" | sha256sum -c --status - || fail "$zip does not match its pinned SHA-256"
    mkdir -p "$work/tools/rexglue"
    rm -rf "$sdk"
    unzip -q -o "$zip" -d "$work/tools/rexglue" || fail "cannot extract $zip"
  fi
  grep -qF "set(PACKAGE_VERSION \"$version\")" "$sdk/lib/cmake/rexglue/rexglueConfigVersion.cmake" ||
    fail "$sdk is not ReXGlue $version"
  if ! bash "$work/tools/build-patched-rexruntime.sh" --check "$sdk" >/dev/null 2>&1; then
    echo "== Building the patched ReXGlue runtime and GPU plugin (needs git and network the first time)"
    bash "$work/tools/build-patched-rexruntime.sh" "$sdk" "$work/rexglue-sdk-src" ||
      fail "the patched ReXGlue runtime could not be built"
  fi
  bash "$work/tools/build-patched-rexruntime.sh" --check "$sdk" >/dev/null || fail "the patched ReXGlue runtime check failed"
}

has_sqlite_node() {  # node
  "$1" -e "require('node:sqlite')" >/dev/null 2>&1
}

ensure_node() {
  local work="$1" dir="$1/tools/node/$NODE_VERSION" tar
  NODE=""
  if command -v node >/dev/null && [ "$(head -c 2 "$(command -v node)")" != MZ ] && has_sqlite_node node; then
    NODE=node
    return 0
  fi
  if [ ! -x "$dir/bin/node" ]; then
    mkdir -p "$dir"
    tar="$work/tools/node/node-v$NODE_VERSION-linux-x64.tar.xz"
    if [ ! -f "$tar" ] || ! echo "$NODE_SHA256  $tar" | sha256sum -c --status -; then
      rm -f "$tar"
      curl -fsSL -o "$tar" "https://nodejs.org/dist/v$NODE_VERSION/node-v$NODE_VERSION-linux-x64.tar.xz" ||
        fail "Node.js $NODE_VERSION could not be downloaded (the local lobby needs Node 22 or newer with node:sqlite)"
    fi
    echo "$NODE_SHA256  $tar" | sha256sum -c --status - || fail "$tar does not match its pinned SHA-256"
    tar -xJf "$tar" -C "$dir" --strip-components=1 || fail "cannot extract $tar"
  fi
  has_sqlite_node "$dir/bin/node" || fail "$dir/bin/node has no node:sqlite"
  NODE="$dir/bin/node"
}

download() {  # url file sha256
  if [ ! -f "$2" ] || ! echo "$3  $2" | sha256sum -c --status -; then
    rm -f "$2"
    if command -v wget >/dev/null; then wget -q -O "$2" "$1"; else curl -fsSL -o "$2" "$1"; fi
  fi
  echo "$3  $2" | sha256sum -c --status - || fail "$2 does not match its pinned SHA-256"
}

ensure_appimage_tools() {
  local root="$1" work="$2" tools="$2/tools/appimage"
  mkdir -p "$tools"
  APPIMAGETOOL="$tools/appimagetool-$APPIMAGETOOL_VERSION-x86_64.AppImage"
  APPIMAGE_RUNTIME="$tools/runtime-ssf2t-x86_64"
  download "https://github.com/AppImage/appimagetool/releases/download/$APPIMAGETOOL_VERSION/appimagetool-x86_64.AppImage" \
    "$APPIMAGETOOL" "$APPIMAGETOOL_SHA256"
  bash "$root/tools/build-appimage-runtime.sh" "$APPIMAGE_RUNTIME" "$work/appimage-runtime" ||
    fail "the patched AppImage runtime could not be built (tools/build-appimage-runtime.sh needs root and network the first time)"
  chmod +x "$APPIMAGETOOL"
}
