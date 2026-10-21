#!/usr/bin/env bash
# Builds the Linux x86_64 Release and packages dist/SSF2THDRemix-v<version>-x86_64.AppImage.
# Local only: nothing is published, tagged or uploaded. Run on Linux or WSL.
#
#   tools/package-linux.sh            incremental Release build, then package
#   tools/package-linux.sh --clean    clean Release build first
#   tools/package-linux.sh --out-dir DIR    write the AppImage and its .sha256 to DIR instead of dist (test builds)
#   tools/package-linux.sh --verify-only FILE    check an existing AppImage (extracts it, scans it, checks the runtime)
#
# Needs clang-20, clang++-20, lld-20, cmake 3.25+, ninja, rsync, git, wget or curl, unzip or python3, binutils and the
# GCC 13+ C++ runtime. Release builds are made on Ubuntu 20.04 (tools/setup-linux-build-env.sh prepares one): the AppImage
# needs the build machine's glibc.
# The ReXGlue runtime and Vulkan GPU plugin are built from source with the project's patches
# (tools/build-patched-rexruntime.sh) the first time and whenever the patches change, which also needs network
# access and the packages tools/setup-linux-build-env.sh installs.
# The AppImage runtime (the part that mounts the AppImage before AppRun runs) is built from the pinned AppImage/type2-runtime
# source with the project's patch (tools/build-appimage-runtime.sh): that needs root or sudo, network access the first time,
# and is skipped while the pins and patches are unchanged.
# Needs generated/default from tools/codegen.ps1 (Windows) and the game's default.xex at game/extracted, which only the
# guest-code freshness check reads (tools/check-guest-code.sh); neither is packaged.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# On the Linux filesystem: much faster than a Windows drive under /mnt, and case-sensitive.
WORK="${SSF2T_WORK:-$HOME/ssf2t-linux-build}"
PRESET=linux-amd64-release
MAX_GLIBC=2.31
BIN_NAME=ssf2t_hd_remix

BASELINE="$ROOT/patches/rexglue-sdk/baseline.env"
baseline() { sed -n "s|^$1=||p" "$BASELINE" | tr -d '\r'; }
SDK_VERSION="$(baseline SDK_VERSION)"
SDK_SHA256="$(baseline SDK_LINUX_ZIP_SHA256 | tr 'A-F' 'a-f')"
APPIMAGETOOL_VERSION=1.9.1
APPIMAGETOOL_SHA256=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0

CLEAN=0
VERIFY_ONLY=""
OUT_DIR="$ROOT/dist"
while [ $# -gt 0 ]; do
  case "$1" in
    --clean) CLEAN=1 ;;
    --out-dir) [ $# -ge 2 ] || { echo "--out-dir needs a folder" >&2; exit 2; }; OUT_DIR="$(realpath -m "$2")"; shift ;;
    --verify-only) [ $# -ge 2 ] || { echo "--verify-only needs a file" >&2; exit 2; }; VERIFY_ONLY="$(realpath "$2")"; shift ;;
    *) echo "Unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

fail() { echo "ERROR: $*" >&2; exit 1; }

download() {  # url file sha256
  if [ ! -f "$2" ] || ! echo "$3  $2" | sha256sum -c --status -; then
    rm -f "$2"
    if command -v wget >/dev/null; then wget -q -O "$2" "$1"; else curl -fsSL -o "$2" "$1"; fi
  fi
  echo "$3  $2" | sha256sum -c --status - || fail "$2 does not match its pinned SHA-256"
}

VERSION="$(sed -nE 's/^[[:space:]]*project\([[:space:]]*ssf2t_hd_remix[[:space:]]+VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' "$ROOT/CMakeLists.txt")"
[ -n "$VERSION" ] || fail "project VERSION not found in CMakeLists.txt"
APP_ID="$(sed -nE 's/^[[:space:]]*constexpr char kApplicationId\[\] = "([^"]+)";.*/\1/p' "$ROOT/src/platform.cpp")"
[[ "$APP_ID" =~ ^[A-Za-z0-9_-]+(\.[A-Za-z0-9_-]+)+$ ]] ||
  fail "src/platform.cpp must declare kApplicationId exactly once, as a dotted name on one line (found: '$APP_ID')"
[ -f "$ROOT/packaging/linux/$APP_ID.desktop" ] || fail "packaging/linux/$APP_ID.desktop is missing: the desktop file is named after kApplicationId"
NAME="SSF2THDRemix-v$VERSION-x86_64"

EXPECTED=".DirIcon
AppRun
$BIN_NAME.png
$APP_ID.desktop
usr/bin/$BIN_NAME
usr/bin/$BIN_NAME.toml
usr/bin/assets/launcher/header-art.png
usr/bin/librexgpu-xenos.so
usr/bin/librexruntime.so
usr/lib/libgcc_s.so.1
usr/lib/libstdc++.so.6
usr/share/doc/$BIN_NAME/LICENSE
usr/share/doc/$BIN_NAME/ReXGlue-LICENSE.txt
usr/share/doc/$BIN_NAME/THIRD-PARTY-LICENSES-LINUX.txt
usr/share/doc/$BIN_NAME/THIRD-PARTY-LICENSES.txt
usr/share/doc/$BIN_NAME/libstdc++-copyright.txt
usr/share/icons/hicolor/128x128/apps/$BIN_NAME.png
usr/share/icons/hicolor/16x16/apps/$BIN_NAME.png
usr/share/icons/hicolor/24x24/apps/$BIN_NAME.png
usr/share/icons/hicolor/256x256/apps/$BIN_NAME.png
usr/share/icons/hicolor/32x32/apps/$BIN_NAME.png
usr/share/icons/hicolor/48x48/apps/$BIN_NAME.png
usr/share/icons/hicolor/512x512/apps/$BIN_NAME.png
usr/share/icons/hicolor/64x64/apps/$BIN_NAME.png
usr/share/icons/hicolor/scalable/apps/$BIN_NAME.svg"

EXPECTED="$(printf '%s\n' "$EXPECTED" | LC_ALL=C sort)"

# Checks a staged or extracted AppDir; extra_paths are build or home folders that must not appear in any file.
scan_appdir() {  # appdir label [extra_paths...]
  local appdir="$1" label="$2" item rel base path utf16 f glibc missing rpath problems=0
  shift 2
  problem() { echo "  $*" >&2; problems=$((problems + 1)); }
  while IFS= read -r -d '' item; do
    rel="${item#"$appdir"/}"
    base="$(basename "$item")"
    case "${base,,}" in
      default.xex | game | game-install-temp | game-install-backup | user_data | cache | logs | settings.toml | 584108a4 | \
        '$systemupdate') problem "forbidden name: $rel" ;;
      *.xex | *.iso | *.fpg | *.log | *.dmp | *.pdb | *.bak | *.tmp | *.sav | *.invalid | *.pyc | *~ | settings.toml.*) problem "forbidden file type: $rel" ;;
    esac
    case "${base,,}" in
      *renderdoc* | *vklayer* | *.json | implicit_layer.d | explicit_layer.d) problem "RenderDoc or Vulkan layer file: $rel" ;;
    esac
    [ -L "$item" ] && problem "symbolic link: $rel"
    if [ -f "$item" ]; then
      case "$(head -c 4 "$item" | tr -d '\0')" in LIVE | PIRS | "CON " | XEX2) problem "Xbox 360 package or executable: $rel" ;; esac
      for path in "$@"; do
        grep -qaF "$path" "$item" && problem "local path in file: $rel ($path)"
        utf16="$(printf '%s' "$path" | od -An -tx1 | tr -d ' \n' | sed 's/../\\x&\\x00/g')"
        LC_ALL=C grep -qaP "$utf16" "$item" && problem "UTF-16 local path in file: $rel ($path)"
      done
      grep -qaF DEBUG_SAULO "$item" && problem "DEBUG_SAULO in file: $rel"
      [ "$(head -c 2 "$item")" = MZ ] && problem "Windows binary: $rel"
    fi
  done < <(find "$appdir" -mindepth 1 -print0)
  local actual
  actual="$(cd "$appdir" && find . ! -type d | sed 's|^\./||' | LC_ALL=C sort)"
  [ "$actual" = "$EXPECTED" ] || { problem "unexpected AppDir contents:"; diff <(echo "$EXPECTED") <(echo "$actual") >&2 || true; }
  grep -qxF "StartupWMClass=$APP_ID" "$appdir/$APP_ID.desktop" || problem "StartupWMClass in $APP_ID.desktop is not $APP_ID"
  grep -qxF "Exec=$BIN_NAME" "$appdir/$APP_ID.desktop" || problem "Exec in $APP_ID.desktop is not $BIN_NAME"
  grep -qaF "$APP_ID" "$appdir/usr/bin/$BIN_NAME" || problem "the program does not contain the application id $APP_ID"
  for f in $BIN_NAME librexruntime.so librexgpu-xenos.so; do
    if readelf -d "$appdir/usr/bin/$f" | grep -i renderdoc >/dev/null; then problem "$f links RenderDoc"; fi
    rpath="$(readelf -d "$appdir/usr/bin/$f" | sed -n 's/.*R\(UN\)\?PATH.*\[\(.*\)\]/\2/p')"
    case "$f" in
      $BIN_NAME) [ "$rpath" = '$ORIGIN' ] || problem "$f RPATH/RUNPATH is '$rpath', not \$ORIGIN" ;;
      *) case "$rpath" in '' | '$ORIGIN' | '$ORIGIN:$ORIGIN/../lib') ;; *) problem "$f RPATH/RUNPATH is '$rpath'" ;; esac ;;
    esac
  done
  missing="$(LD_LIBRARY_PATH="$appdir/usr/lib" ldd "$appdir/usr/bin/$BIN_NAME" | grep 'not found' || true)"
  [ -z "$missing" ] || problem "unresolved libraries: $missing"
  [ "$problems" = 0 ] || fail "the $label holds files that must not be released"
  echo "  $label clean"
  for f in usr/bin/$BIN_NAME usr/bin/librexruntime.so usr/bin/librexgpu-xenos.so usr/lib/libstdc++.so.6 usr/lib/libgcc_s.so.1; do
    glibc="$(readelf --dyn-syms -W "$appdir/$f" | grep -oE 'GLIBC_[0-9.]+' | sort -Vu | tail -1)"
    echo "  $f needs $glibc"
    [ "$(printf '%s\n' "${glibc#GLIBC_}" "$MAX_GLIBC" | sort -V | tail -1)" = "$MAX_GLIBC" ] ||
      fail "$f needs $glibc, above the glibc $MAX_GLIBC baseline"
  done
}

if [ -n "$VERIFY_ONLY" ]; then
  [ -f "$VERIFY_ONLY" ] || fail "missing $VERIFY_ONLY"
  TMP="$(mktemp -d)"
  trap 'rm -rf "$TMP"' EXIT
  bash "$ROOT/tools/build-appimage-runtime.sh" --check "$VERIFY_ONLY"
  cp "$VERIFY_ONLY" "$TMP/verify.AppImage"
  chmod +x "$TMP/verify.AppImage"
  (cd "$TMP" && ./verify.AppImage --appimage-extract >/dev/null)
  scan_appdir "$TMP/squashfs-root" "extracted AppImage" "$ROOT" "$WORK" "$HOME"
  echo "Verified $VERIFY_ONLY"
  exit 0
fi

echo "Super Street Fighter II Turbo HD Remix PC port $VERSION - Linux AppImage (local only)"
SOURCE="$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"
if CHANGES="$(git -C "$ROOT" status --porcelain 2>/dev/null)"; then
  [ -z "$CHANGES" ] || SOURCE="$SOURCE plus uncommitted changes"
else
  SOURCE="$SOURCE, working tree state unknown"
fi
echo "Source: $SOURCE"

echo "== Tools"
for tool in clang-20 clang++-20 ld.lld-20 cmake ninja rsync sha256sum strip readelf objdump ldd; do
  command -v "$tool" >/dev/null || fail "$tool not found"
done
command -v wget >/dev/null || command -v curl >/dev/null || fail "wget or curl not found"
command -v unzip >/dev/null || command -v python3 >/dev/null || fail "unzip or python3 not found"
clang++-20 --version | sed -n 1p
cmake --version | sed -n 1p
HOST_GLIBC="$(getconf GNU_LIBC_VERSION | cut -d' ' -f2)"
echo "glibc $HOST_GLIBC"
[ "$(printf '%s\n' "$MAX_GLIBC" "$HOST_GLIBC" | sort -V | tail -1)" = "$MAX_GLIBC" ] ||
  fail "this machine has glibc $HOST_GLIBC; release builds need Ubuntu 20.04 (glibc $MAX_GLIBC), see tools/setup-linux-build-env.sh"

echo "== Inputs"
[ -f "$ROOT/third_party/trg-launcher/CMakeLists.txt" ] || fail "third_party/trg-launcher is empty"
bash "$ROOT/tools/check-guest-code.sh" "$ROOT" || fail "generated/default is not current (run tools/codegen.ps1 on Windows first)"

echo "== Source -> $WORK"
mkdir -p "$WORK/third_party" "$WORK/tools"
for f in CMakeLists.txt CMakePresets.json ssf2t_hd_remix_manifest.toml LICENSE tools/build-patched-rexruntime.sh; do
  rsync -a "$ROOT/$f" "$WORK/$f"
done
for d in assets config src tests generated packaging patches/rexglue-sdk patches/trg-launcher third_party/trg-launcher; do
  mkdir -p "$WORK/$d"
  rsync -a --delete --exclude .git "$ROOT/$d/" "$WORK/$d/"
done
# A Windows checkout has CRLF sources; the TRG patches are applied with git apply and need matching endings.
find "$WORK/third_party/trg-launcher" "$WORK/patches/trg-launcher" -type f \
  \( -name '*.patch' -o -name '*.h' -o -name '*.cpp' -o -name '*.c' -o -name '*.txt' -o -name '*.cmake' \) \
  -exec sed -i 's/\r$//' {} +

# The build re-runs codegen (Windows only here) when an input listed in codegen.d is missing or newer.
mkdir -p "$WORK/game/extracted"
cp -p "$(find "$ROOT/game/extracted" -maxdepth 1 -iname default.xex | head -n 1)" "$WORK/game/extracted/default.xex"
sed -i -E "s#[A-Za-z]:/[^ ]*/ssf2t-hd-remix-recompiled#$WORK#g" "$WORK/generated/default/codegen.d"
if grep -qE '[A-Za-z]:/' "$WORK/generated/default/codegen.d"; then fail "generated/default/codegen.d still names Windows paths"; fi

echo "== ReXGlue SDK $SDK_VERSION (linux-amd64)"
SDK="$WORK/tools/rexglue/linux-amd64"
if [ ! -f "$SDK/lib/cmake/rexglue/rexglueConfigVersion.cmake" ]; then
  mkdir -p "$WORK/tools/rexglue"
  ZIP="$WORK/tools/rexglue/rexglue-sdk-$SDK_VERSION-linux-amd64.zip"
  download "https://github.com/rexglue/rexglue-sdk/releases/download/v$SDK_VERSION/rexglue-sdk-$SDK_VERSION-linux-amd64.zip" \
    "$ZIP" "$SDK_SHA256"
  rm -rf "$SDK"
  if command -v unzip >/dev/null; then
    unzip -q -o "$ZIP" -d "$WORK/tools/rexglue"
  else
    python3 -I -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' "$ZIP" "$WORK/tools/rexglue"
  fi
fi
grep -qF "set(PACKAGE_VERSION \"$SDK_VERSION\")" "$SDK/lib/cmake/rexglue/rexglueConfigVersion.cmake" ||
  fail "tools/rexglue/linux-amd64 is not ReXGlue $SDK_VERSION"

# The SDK's codegen tool needs a newer glibc than Ubuntu 20.04 and codegen is a Windows step anyway: the build's codegen
# step only records the already checked generated/default.
cat > "$SDK/bin/rexglue" <<'STUB'
#!/bin/sh
[ "$1" = codegen ] || exit 2
exec touch "$(dirname "$2")/generated/default/codegen.build.stamp"
STUB
chmod 755 "$SDK/bin/rexglue"

echo "== Patched ReXGlue runtime and GPU plugin"
PATCHED="$ROOT/tools/build-patched-rexruntime.sh"
if ! bash "$PATCHED" --check "$SDK" >/dev/null 2>&1; then
  bash "$PATCHED" "$SDK" "$WORK/rexglue-sdk-src"
fi
bash "$PATCHED" --check "$SDK"

echo "== Build ($PRESET)"
BIN="$WORK/out/build/$PRESET"
[ "$CLEAN" = 1 ] && rm -rf "$BIN"
(cd "$WORK" && cmake --preset "$PRESET" >/dev/null && cmake --build --preset "$PRESET")
for f in $BIN_NAME librexruntime.so librexgpu-xenos.so $BIN_NAME.toml assets/launcher/header-art.png; do
  [ -f "$BIN/$f" ] || fail "build output is missing $f"
done
bash "$PATCHED" --check "$SDK" --staged "$BIN" || fail "the build folder does not hold the patched ReXGlue libraries"

echo "== AppDir"
APPDIR="$WORK/AppDir"
DOC="$APPDIR/usr/share/doc/$BIN_NAME"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib" "$DOC"
cp "$BIN/$BIN_NAME" "$BIN/librexruntime.so" "$BIN/librexgpu-xenos.so" "$BIN/$BIN_NAME.toml" "$APPDIR/usr/bin/"
mkdir -p "$APPDIR/usr/bin/assets/launcher"
cp "$BIN/assets/launcher/header-art.png" "$APPDIR/usr/bin/assets/launcher/"
# Debug sections carry the build machine's folders.
strip --strip-unneeded "$APPDIR/usr/bin/$BIN_NAME"
# ReXGlue needs GLIBCXX_3.4.32 (GCC 13); AppRun uses this copy only on systems without it.
for lib in libstdc++.so.6 libgcc_s.so.1; do
  src="$(g++ -print-file-name=$lib 2>/dev/null || true)"
  [ -f "$src" ] || src="/usr/lib/x86_64-linux-gnu/$lib"
  cp -L "$src" "$APPDIR/usr/lib/$lib"
done
grep -q GLIBCXX_3.4.32 "$APPDIR/usr/lib/libstdc++.so.6" || fail "the build machine's libstdc++ is older than GCC 13"
cp "$WORK/LICENSE" "$WORK/packaging/licenses/ReXGlue-LICENSE.txt" "$WORK/packaging/licenses/THIRD-PARTY-LICENSES.txt" \
  "$WORK/packaging/licenses/THIRD-PARTY-LICENSES-LINUX.txt" "$DOC/"
for component in type2-runtime libfuse squashfuse Zstandard zlib mimalloc musl libgcc; do
  grep -qF "AppImage runtime: $component" "$DOC/THIRD-PARTY-LICENSES-LINUX.txt" ||
    fail "THIRD-PARTY-LICENSES-LINUX.txt has no notice for the AppImage runtime component $component"
done
CXX_COPYRIGHT=/usr/share/doc/libstdc++6/copyright
[ -f "$CXX_COPYRIGHT" ] || fail "missing $CXX_COPYRIGHT (the license of the bundled C++ runtime)"
cp "$CXX_COPYRIGHT" "$DOC/libstdc++-copyright.txt"
cp "$WORK/packaging/linux/AppRun" "$WORK/packaging/linux/$APP_ID.desktop" "$APPDIR/"
# The icon is the root file the .desktop names, the .DirIcon file managers show, and the theme the desktop integrates.
ICONS="$WORK/packaging/icons"
THEME="$APPDIR/usr/share/icons/hicolor"
for size in 16 24 32 48 64 128 256 512; do
  mkdir -p "$THEME/${size}x${size}/apps"
  cp "$ICONS/app-icon-$size.png" "$THEME/${size}x${size}/apps/$BIN_NAME.png"
done
mkdir -p "$THEME/scalable/apps"
cp "$ICONS/app-icon.svg" "$THEME/scalable/apps/$BIN_NAME.svg"
cp "$ICONS/app-icon-256.png" "$APPDIR/$BIN_NAME.png"
cp "$ICONS/app-icon-256.png" "$APPDIR/.DirIcon"
chmod 755 "$APPDIR/AppRun" "$APPDIR/usr/bin/$BIN_NAME"
chmod 644 "$APPDIR/"*.desktop "$APPDIR/"*.png "$APPDIR/.DirIcon" "$APPDIR/usr/bin/"*.so \
  "$APPDIR/usr/bin/$BIN_NAME.toml" "$APPDIR/usr/bin/assets/launcher/header-art.png" "$APPDIR/usr/lib/"* "$DOC/"*
find "$THEME" -type f -exec chmod 644 {} +
bash "$PATCHED" --check "$SDK" --staged "$APPDIR/usr/bin" || fail "the AppDir does not hold the patched ReXGlue libraries"

echo "== Release check"
scan_appdir "$APPDIR" AppDir "$ROOT" "$WORK" "$HOME"

echo "== AppImage"
TOOLS="$WORK/tools/appimage"
mkdir -p "$TOOLS"
TOOL="$TOOLS/appimagetool-$APPIMAGETOOL_VERSION-x86_64.AppImage"
RUNTIME="$TOOLS/runtime-ssf2t-x86_64"
download "https://github.com/AppImage/appimagetool/releases/download/$APPIMAGETOOL_VERSION/appimagetool-x86_64.AppImage" \
  "$TOOL" "$APPIMAGETOOL_SHA256"
bash "$ROOT/tools/build-appimage-runtime.sh" "$RUNTIME" "$WORK/appimage-runtime"
chmod +x "$TOOL"
mkdir -p "$OUT_DIR"
OUT="$OUT_DIR/$NAME.AppImage"
rm -f "$OUT"
ARCH=x86_64 "$TOOL" --appimage-extract-and-run --no-appstream --runtime-file "$RUNTIME" "$APPDIR" "$OUT" >/dev/null
chmod 755 "$OUT"
bash "$ROOT/tools/build-appimage-runtime.sh" --check "$OUT"

echo "== Final AppImage check"
VERIFY="$(mktemp -d)"
trap 'rm -rf "$VERIFY"' EXIT
cp "$OUT" "$VERIFY/verify.AppImage"
(cd "$VERIFY" && ./verify.AppImage --appimage-extract >/dev/null)
scan_appdir "$VERIFY/squashfs-root" "extracted AppImage" "$ROOT" "$WORK" "$HOME"
[ "$(sha256sum "$VERIFY/squashfs-root/usr/bin/librexruntime.so" | cut -d' ' -f1)" = "$(sha256sum "$BIN/librexruntime.so" | cut -d' ' -f1)" ] ||
  fail "the AppImage does not hold the built runtime"
(cd "$OUT_DIR" && sha256sum "$NAME.AppImage" > "$NAME.sha256")

echo
echo "===================================================="
echo " APPIMAGE READY (nothing was published)"
echo "===================================================="
echo "  AppImage: $OUT ($(du -h "$OUT" | cut -f1))"
echo "  SHA-256:  $(cut -d' ' -f1 "$OUT_DIR/$NAME.sha256")"
echo "  Source:   $SOURCE"
echo "  Contents:"
echo "$EXPECTED" | sed 's/^/    /'
