#!/bin/sh
# build-wine-dlls.sh <wine-src> <out-dir> <file.dll>...
#
# Build only the named 32-bit PE modules from a Wine source tree (out of tree,
# in /build) and copy them to <out-dir>. Used to drop a few patched DLLs over a
# WineHQ binary install of the *same* release; building all of Wine for that
# would take far longer and replace much more than we change.
#
# A module's directory is its file name without the extension
# (comctl32_v6.dll -> dlls/comctl32_v6).
set -eu
src=$1 out=$2
shift 2
[ $# -gt 0 ] || { echo "no modules given" >&2; exit 1; }

mkdir -p /build "$out"
cd /build
"$src/configure" -q --enable-archs=i386 --without-x --without-freetype --disable-tests >/dev/null

targets=
for f in "$@"; do targets="$targets dlls/${f%.*}/i386-windows/$f"; done
# shellcheck disable=SC2086  # word splitting of $targets is intended
make -j"$(nproc)" $targets >/dev/null

for f in "$@"; do
  cp "dlls/${f%.*}/i386-windows/$f" "$out/"
  echo "built $f"
done
