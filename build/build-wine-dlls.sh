#!/bin/sh
# build-wine-dlls.sh <wine-src> <out-dir> <module>...
#
# Build only the named modules from a Wine source tree (out of tree, in
# /build) and put them under <out-dir> laid out like a WineHQ install
# (/opt/wine-stable), so they can be copied over a WineHQ binary install of
# the *same* release; building all of Wine for that would take far longer and
# replace much more than we change.
#
#   file.dll    a 32-bit PE module -> <out-dir>/lib/wine/i386-windows/file.dll
#               (its directory is the file name without the extension:
#               comctl32_v6.dll -> dlls/comctl32_v6)
#   wineserver  the native server  -> <out-dir>/bin/wineserver
set -eu
src=$1 out=$2
shift 2
[ $# -gt 0 ] || { echo "no modules given" >&2; exit 1; }

mkdir -p /build "$out/lib/wine/i386-windows" "$out/bin"
cd /build
"$src/configure" -q --enable-archs=i386 --without-x --without-freetype --disable-tests >/dev/null

target() {
  case $1 in
    wineserver) echo server/wineserver ;;
    *) echo "dlls/${1%.*}/i386-windows/$1" ;;
  esac
}

targets=
for f in "$@"; do targets="$targets $(target "$f")"; done
# shellcheck disable=SC2086  # word splitting of $targets is intended
make -j"$(nproc)" $targets >/dev/null

for f in "$@"; do
  case $f in
    wineserver) cp server/wineserver "$out/bin/" ;;
    *) cp "$(target "$f")" "$out/lib/wine/i386-windows/" ;;
  esac
  echo "built $f"
done
