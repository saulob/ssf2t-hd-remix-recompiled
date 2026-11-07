#!/usr/bin/env bash
# The Linux release policy, shared by tools/package-linux.sh (which stages and checks the real AppDir) and tools/test.sh
# (which runs the same functions on a staged fixture AppDir, on the program's real build products and on canned
# readelf output). Source it after defining fail(); nothing runs until a function is called.
#
#   tools/linux-policy.sh --self-test     run the checks against fixtures that must pass and fixtures that must fail
MAX_GLIBC=2.31
BIN_NAME=ssf2t_hd_remix

policy_init() {  # root
  local root="$1"
  APP_ID="$(sed -nE 's/^[[:space:]]*constexpr char kApplicationId\[\] = "([^"]+)";.*/\1/p' "$root/src/platform.cpp")"
  [[ "$APP_ID" =~ ^[A-Za-z0-9_-]+(\.[A-Za-z0-9_-]+)+$ ]] ||
    fail "src/platform.cpp must declare kApplicationId exactly once, as a dotted name on one line (found: '$APP_ID')"
  [ -f "$root/packaging/linux/$APP_ID.desktop" ] || fail "packaging/linux/$APP_ID.desktop is missing: the desktop file is named after kApplicationId"
EXPECTED=".DirIcon
AppRun
$BIN_NAME.png
$APP_ID.desktop
usr/bin/$BIN_NAME
usr/bin/$BIN_NAME.toml
usr/bin/assets/launcher/header-art.png
usr/bin/assets/launcher/sponsor-heart.png
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

CHARACTER_TABLE="$root/src/character/character_packages.h"
CHARACTER_PACKAGES="$(character_package_lines "$CHARACTER_TABLE" | cut -d' ' -f1)"
[ "$(printf '%s\n' "$CHARACTER_PACKAGES" | sort -u | wc -l)" -gt 1 ] || fail "no character packages found in $CHARACTER_TABLE"
EXPECTED="$EXPECTED
$(printf 'usr/bin/assets/characters/%s\n' $CHARACTER_PACKAGES)"
EXPECTED="$(printf '%s\n' "$EXPECTED" | LC_ALL=C sort)"
}

# "<file> <size> <sha256>" per entry of src/character/character_packages.h.
character_package_lines() {  # table
  tr -d '\r' <"$1" | sed -nE 's/^[[:space:]]*\{"([a-z0-9_]+\.bin)", ([0-9]+), "([0-9a-f]{64})", PackageOwner::.*/\1 \2 \3/p'
}

# Fails when a folder does not hold exactly the file the table names for every package.
check_character_packages() {  # folder table
  local folder="$1" file size sha256 bad=0
  while read -r file size sha256; do
    if [ ! -f "$folder/$file" ]; then
      echo "  missing character package: $file" >&2; bad=1
    elif [ "$(wc -c <"$folder/$file")" != "$size" ] || [ "$(sha256sum "$folder/$file" | cut -d' ' -f1)" != "$sha256" ]; then
      echo "  character package differs from character_packages.h: $file" >&2; bad=1
    fi
  done < <(character_package_lines "$2")
  return $bad
}

# Checks a staged or extracted AppDir; extra_paths are build or home folders that must not appear in any file.
scan_appdir() {  # appdir label [extra_paths...]
  local appdir="$1" label="$2" item rel base path utf16 f glibc missing rpath problems=0
  shift 2
  problem() { echo "  $*" >&2; problems=$((problems + 1)); }
  while IFS= read -r -d '' item; do
    rel="${item#"$appdir"/}"
    base="$(basename "$item")"
    case "${base,,}" in
      default.xex | default.xexp | game | game-install-temp | game-install-backup | user_data | user_data.migrating | \
        content | cache | logs | settings.toml | 584108a4 | '$systemupdate' | thumbs.db | desktop.ini) problem "forbidden name: $rel" ;;
      *.xex | *.xexp | *.iso | *.fpg | *.log | *.dmp | *.pdb | *.debug | *.xsh | *.xpso | *.bak | *.tmp | *.sav | *.invalid | \
        *.pyc | *.o | *.obj | *.a | *.lib | *.ilk | *.orig | *.rej | *.rdc | .git* | *~ | settings.toml.*) problem "forbidden file type: $rel" ;;
    esac
    case "${base,,}" in
      *renderdoc* | *vklayer* | tracyclient* | *.json | implicit_layer.d | explicit_layer.d) problem "RenderDoc, Tracy or Vulkan layer file: $rel" ;;
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
    if readelf -S -W "$appdir/usr/bin/$f" | grep -F ' .debug_' >/dev/null; then problem "$f holds debug sections"; fi
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

# Copies the files the AppImage holds from a build folder (the program, runtime and GPU plugin) and the repository.
stage_appdir() {  # appdir bin_dir symbols_dir source_tree
  local appdir="$1" bin="$2" symbols="$3" tree="$4" doc lib src size icons theme component package
  doc="$appdir/usr/share/doc/$BIN_NAME"
  theme="$appdir/usr/share/icons/hicolor"
  icons="$tree/packaging/icons"
  rm -rf "$appdir" "$symbols"
  mkdir -p "$appdir/usr/bin/assets/launcher" "$appdir/usr/bin/assets/characters" "$appdir/usr/lib" "$doc" "$symbols"
  cp "$bin/librexruntime.so" "$bin/librexgpu-xenos.so" "$bin/$BIN_NAME.toml" "$appdir/usr/bin/"
  strip --strip-unneeded "$appdir/usr/bin/librexruntime.so" "$appdir/usr/bin/librexgpu-xenos.so"
  cp "$bin/assets/launcher/header-art.png" "$bin/assets/launcher/sponsor-heart.png" "$appdir/usr/bin/assets/launcher/"
  for package in $CHARACTER_PACKAGES; do
    cp "$bin/assets/characters/$package" "$appdir/usr/bin/assets/characters/"
  done
  # The debug sections carry the build machine's folders, so they stay in $symbols.
  objcopy --only-keep-debug "$bin/$BIN_NAME" "$symbols/$BIN_NAME.debug"
  objcopy --strip-debug --add-gnu-debuglink="$symbols/$BIN_NAME.debug" "$bin/$BIN_NAME" "$appdir/usr/bin/$BIN_NAME"
  # ReXGlue needs GLIBCXX_3.4.32 (GCC 13); AppRun uses this copy only on systems without it.
  for lib in libstdc++.so.6 libgcc_s.so.1; do
    src="$(g++ -print-file-name=$lib 2>/dev/null || true)"
    [ -f "$src" ] || src="/usr/lib/x86_64-linux-gnu/$lib"
    cp -L "$src" "$appdir/usr/lib/$lib"
  done
  grep -q GLIBCXX_3.4.32 "$appdir/usr/lib/libstdc++.so.6" || fail "the build machine's libstdc++ is older than GCC 13"
  cp "$tree/LICENSE" "$tree/packaging/licenses/ReXGlue-LICENSE.txt" "$tree/packaging/licenses/THIRD-PARTY-LICENSES.txt" \
    "$tree/packaging/licenses/THIRD-PARTY-LICENSES-LINUX.txt" "$doc/"
  for component in type2-runtime libfuse squashfuse Zstandard zlib mimalloc musl libgcc; do
    grep -qF "AppImage runtime: $component" "$doc/THIRD-PARTY-LICENSES-LINUX.txt" ||
      fail "THIRD-PARTY-LICENSES-LINUX.txt has no notice for the AppImage runtime component $component"
  done
  [ -f /usr/share/doc/libstdc++6/copyright ] || fail "missing /usr/share/doc/libstdc++6/copyright (the license of the bundled C++ runtime)"
  cp /usr/share/doc/libstdc++6/copyright "$doc/libstdc++-copyright.txt"
  cp "$tree/packaging/linux/AppRun" "$tree/packaging/linux/$APP_ID.desktop" "$appdir/"
  # The icon is the root file the .desktop names, the .DirIcon file managers show, and the theme the desktop integrates.
  for size in 16 24 32 48 64 128 256 512; do
    mkdir -p "$theme/${size}x${size}/apps"
    cp "$icons/app-icon-$size.png" "$theme/${size}x${size}/apps/$BIN_NAME.png"
  done
  mkdir -p "$theme/scalable/apps"
  cp "$icons/app-icon.svg" "$theme/scalable/apps/$BIN_NAME.svg"
  cp "$icons/app-icon-256.png" "$appdir/$BIN_NAME.png"
  cp "$icons/app-icon-256.png" "$appdir/.DirIcon"
  chmod 755 "$appdir/AppRun" "$appdir/usr/bin/$BIN_NAME"
  chmod 644 "$appdir/"*.desktop "$appdir/"*.png "$appdir/.DirIcon" "$appdir/usr/bin/"*.so \
    "$appdir/usr/bin/$BIN_NAME.toml" "$appdir/usr/bin/assets/launcher/header-art.png" "$appdir/usr/bin/assets/launcher/sponsor-heart.png" \
    "$appdir/usr/bin/assets/characters/"* "$appdir/usr/lib/"* "$doc/"*
  find "$theme" -type f -exec chmod 644 {} +
}

# ---- ELF checks: they read `readelf` output, so canned output can stand in for a binary --------------------------------

elf_glibc_text() { grep -oE 'GLIBC_[0-9]+(\.[0-9]+)*' | sed 's/^GLIBC_//' | sort -Vu | tail -1; }  # stdin: readelf -V -W
elf_needed_text() { sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'; }                                      # stdin: readelf -d
elf_rpath_text() { sed -n 's/.*(R\(UN\)\?PATH).*\[\(.*\)\]/\2/p'; }                                 # stdin: readelf -d

# Prints a problem line for each violation; the exit status is the number of violations (capped at 125).
check_elf_glibc() {  # label glibc_version_or_empty
  local glibc="$2"
  [ -z "$glibc" ] && return 0
  if [ "$(printf '%s\n' "$glibc" "$MAX_GLIBC" | sort -V | tail -1)" != "$MAX_GLIBC" ]; then
    echo "PARITY FAIL: Linux production target requires GLIBC_$glibc, baseline is GLIBC_$MAX_GLIBC ($1)"
    return 1
  fi
}

# Every entry must be $ORIGIN-relative: nothing absolute, no developer or build-machine folders.
check_elf_rpath() {  # label rpath_value
  local entry bad=0
  for entry in ${2//:/ }; do
    case "$entry" in
      '$ORIGIN' | '$ORIGIN/'*) ;;
      *) echo "PARITY FAIL: Linux RPATH/RUNPATH entry '$entry' is not \$ORIGIN-relative ($1)"; bad=1 ;;
    esac
    case "$entry" in
      */home/* | /home/* | */root/* | /root/* | /mnt/* | [A-Za-z]:* | *$'\134'* | /tmp/*) echo "PARITY FAIL: Linux RPATH/RUNPATH leaks a local path '$entry' ($1)"; bad=1 ;;
    esac
  done
  return $bad
}

# Libraries a program of this project may name. libssl and libcrypto are loaded at run time and must never appear.
ALLOWED_NEEDED=' libc.so.6 libm.so.6 libdl.so.2 libpthread.so.0 librt.so.1 libstdc++.so.6 libgcc_s.so.1 ld-linux-x86-64.so.2 librexruntime.so librexgpu-xenos.so '
check_elf_needed() {  # label needed_list extra_allowed
  local lib bad=0
  for lib in $2; do
    case "$lib" in
      libssl* | libcrypto*) echo "PARITY FAIL: Linux binary links OpenSSL at build time ($1: $lib); it is loaded at run time"; bad=1 ;;
      *renderdoc* | *RenderDoc* | libVkLayer* | *tracy* | *Tracy*) echo "PARITY FAIL: Linux binary depends on a development library ($1: $lib)"; bad=1 ;;
      *) case "$ALLOWED_NEEDED${3:-}" in *" $lib "*) ;; *) echo "PARITY FAIL: Linux binary has an unexpected dependency ($1: $lib)"; bad=1 ;; esac ;;
    esac
  done
  return $bad
}

# Runs the three checks above on a real file. Prints "<file> needs GLIBC_x, ..." and returns the number of failures.
check_elf_file() {  # label file extra_allowed
  local label="$1" file="$2" failures=0 glibc rpath needed
  glibc="$(readelf -V -W "$file" 2>/dev/null | elf_glibc_text)"
  rpath="$(readelf -d "$file" 2>/dev/null | elf_rpath_text)"
  needed="$(readelf -d "$file" 2>/dev/null | elf_needed_text | tr '\n' ' ')"
  echo "  $label: GLIBC_${glibc:-none}, RUNPATH [${rpath}], needs ${needed:-nothing}"
  check_elf_glibc "$label" "$glibc" || failures=$((failures + 1))
  check_elf_rpath "$label" "$rpath" || failures=$((failures + 1))
  check_elf_needed "$label" "$needed" "${3:-}" || failures=$((failures + 1))
  return $failures
}

policy_self_test() {
  local failed=0 text
  expect() {  # description ok|bad command...
    local what="$1" want="$2" got
    shift 2
    "$@" >/dev/null 2>&1
    got=$?
    if { [ "$want" = ok ] && [ "$got" != 0 ]; } || { [ "$want" = bad ] && [ "$got" = 0 ]; }; then
      echo "policy self-test FAILED: $what"
      failed=$((failed + 1))
    fi
  }
  text='Version needs section ".gnu.version_r"
  0x0010: Name: GLIBC_2.2.5  Flags: none  Version: 3
  0x0020: Name: GLIBC_2.31  Flags: none  Version: 2'
  expect "GLIBC_2.31 is the baseline" ok check_elf_glibc fixture "$(printf '%s\n' "$text" | elf_glibc_text)"
  expect "GLIBC_2.34 is above the baseline" bad check_elf_glibc fixture \
    "$(printf '%s\n%s\n' "$text" '  0x0030: Name: GLIBC_2.34  Flags: none  Version: 4' | elf_glibc_text)"
  expect "GLIBC_2.100 sorts above GLIBC_2.31" bad check_elf_glibc fixture "$(printf '%s\n' '  Name: GLIBC_2.100' | elf_glibc_text)"
  expect '$ORIGIN is allowed' ok check_elf_rpath fixture '$ORIGIN'
  expect '$ORIGIN/../lib is allowed' ok check_elf_rpath fixture '$ORIGIN:$ORIGIN/../lib'
  expect "an absolute RUNPATH is refused" bad check_elf_rpath fixture "/opt/lib"
  expect "a developer home in RUNPATH is refused" bad check_elf_rpath fixture '$ORIGIN:/home/dev/ssf2t/lib'
  expect "a build folder in RUNPATH is refused" bad check_elf_rpath fixture '$ORIGIN:/root/ssf2t-linux-build/rexglue/lib'
  expect "a Windows path in RUNPATH is refused" bad check_elf_rpath fixture 'S:/VisualStudio/repos/ssf2t/lib'
  expect "the system libraries are allowed" ok check_elf_needed fixture 'libc.so.6 libm.so.6 libstdc++.so.6 librexruntime.so'
  expect "libssl at build time is refused" bad check_elf_needed fixture 'libc.so.6 libssl.so.3'
  expect "libcrypto at build time is refused" bad check_elf_needed fixture 'libc.so.6 libcrypto.so.1.1'
  expect "RenderDoc is refused" bad check_elf_needed fixture 'libc.so.6 librenderdoc.so'
  expect "a Vulkan layer is refused" bad check_elf_needed fixture 'libc.so.6 libVkLayer_khronos_validation.so'
  expect "an unlisted library is refused" bad check_elf_needed fixture 'libc.so.6 libfoo.so.1'
  expect "an unlisted library can be allowed by name" ok check_elf_needed fixture 'libc.so.6 libX11.so.6' ' libX11.so.6 '
  text='Dynamic section at offset 0x1 contains 3 entries:
 0x0000000000000001 (NEEDED)             Shared library: [libdl.so.2]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
 0x000000000000001d (RUNPATH)            Library runpath: [$ORIGIN]'
  [ "$(printf '%s\n' "$text" | elf_needed_text | tr '\n' ' ')" = 'libdl.so.2 libc.so.6 ' ] ||
    { echo "policy self-test FAILED: readelf NEEDED parsing"; failed=$((failed + 1)); }
  [ "$(printf '%s\n' "$text" | elf_rpath_text)" = '$ORIGIN' ] ||
    { echo "policy self-test FAILED: readelf RUNPATH parsing"; failed=$((failed + 1)); }
  local fixture
  fixture="$(mktemp -d)"
  printf 'package' >"$fixture/a.bin"
  printf '    {"a.bin", 7, "%s", PackageOwner::kBoth, PackageNeed::kRequired},\r\n' "$(sha256sum "$fixture/a.bin" | cut -d' ' -f1)" >"$fixture/table.h"
  expect "a character package that matches its table entry is accepted" ok check_character_packages "$fixture" "$fixture/table.h"
  printf 'packagf' >"$fixture/a.bin"
  expect "a stale character package is refused" bad check_character_packages "$fixture" "$fixture/table.h"
  rm "$fixture/a.bin"
  expect "a missing character package is refused" bad check_character_packages "$fixture" "$fixture/table.h"
  rm -rf "$fixture"
  [ "$failed" = 0 ]
}

if [ "${BASH_SOURCE[0]}" = "$0" ]; then
  fail() { echo "ERROR: $*" >&2; exit 1; }
  [ "${1:-}" = --self-test ] || { echo "usage: $0 --self-test (source it from another script otherwise)" >&2; exit 2; }
  policy_self_test && echo "policy self-test passed"
fi
