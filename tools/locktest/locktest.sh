#!/bin/sh
# Cross-container Windows byte-range lock test.
#
# Container A takes an exclusive lock on one byte of a file on a shared volume
# and holds it; container B (a separate Wine instance) tries the same byte.
# BUSY from B means Wine's locks are visible across containers on this host
# and storage, which is what file-share databases rely on.
#
#   sh tools/locktest/locktest.sh [image] [volume]
# Needs tools/locktest/locktest.exe (see README.md). Removes nothing but the
# scratch file it creates in the volume.
set -eu
img=${1:-wine-webapp:vnc}
vol=${2:-wine-webapp-locktest}
here=$(cd "$(dirname "$0")" && pwd)
export MSYS_NO_PATHCONV=1   # Git Bash on Windows: don't rewrite /paths

run() {
  docker run --rm -u webapp -v "$vol:/lk" -v "$here:/t:ro" --entrypoint sh "$img" -c "$1" 2>&1 \
    | grep -E 'LOCKED|BUSY|OPENFAIL' || true
}

docker volume create "$vol" >/dev/null
docker run --rm -u root -v "$vol:/lk" --entrypoint chown "$img" webapp:webapp /lk

for off in 10 FFFFFFF0 7FFFFFFFF0; do
  echo "== offset $off (expect: other container BUSY)"
  run "wine /t/locktest.exe 'Z:\lk\t.dat' $off 12" &
  sleep 6
  printf '  other container: '
  run "wine /t/locktest.exe 'Z:\lk\t.dat' $off 0"
  wait
done

echo "== control: two processes in one container (expect BUSY)"
run "wine /t/locktest.exe 'Z:\lk\t.dat' 10 8 & sleep 5; wine /t/locktest.exe 'Z:\lk\t.dat' 10 0; wait"
