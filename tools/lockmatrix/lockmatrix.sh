#!/bin/sh
# Windows file-sharing semantics between two Wine processes, in one topology.
# See README.md for the topologies and what the results mean.
#
#   tools/lockmatrix/lockmatrix.sh TOPOLOGY [IMAGE]
#     same-wineserver   two processes in one container (one wineserver)
#     two-containers    one container each, same Docker named volume
#     cifs              one container each, the same SMB share mounted with
#                       CIFS once per container (nosharesock): each has its
#                       own SMB session, so every conflict comes from the server
#     cifs-shared-mount one container each on ONE CIFS mount of the share, where
#                       the local kernel can catch a conflict first; for comparison
#                     (both cifs topologies: a Samba container serves the share;
#                     needs Docker on Linux with the cifs kernel module)
#
# Prints one line per case: the case, what Windows gives, what Wine gave.
# Exit 0 when every case matches Windows.
set -eu

topo=${1:?usage: lockmatrix.sh same-wineserver|two-containers|cifs|cifs-shared-mount [IMAGE]}
image=${2:-wine-webapp:vnc}
here=$(cd "$(dirname "$0")" && pwd)
exe=$here/lockmatrix.exe
[ -f "$exe" ] || { echo "build lockmatrix.exe first (README.md)" >&2; exit 2; }
id=lockmatrix-$$
vol=$id-data
net=$id-net
a=$id-a
b=$a

cleanup() {
  docker rm -f "$id-a" "$id-b" "$id-smb" >/dev/null 2>&1 || true
  docker volume rm "$vol" "$vol-b" >/dev/null 2>&1 || true
  docker network rm "$net" >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM

start() {   # start NAME VOLUME-SPEC
  docker run -d --name "$1" --network "$net" --entrypoint sleep \
    -v "$2" -v "$exe:/opt/lockmatrix.exe:ro" "$image" infinity >/dev/null
  docker exec "$1" chown webapp:webapp /shared 2>/dev/null || true
}

docker network create "$net" >/dev/null
case $topo in
  same-wineserver)
    docker volume create "$vol" >/dev/null
    start "$id-a" "$vol:/shared" ;;
  two-containers)
    docker volume create "$vol" >/dev/null
    start "$id-a" "$vol:/shared"
    start "$id-b" "$vol:/shared"
    b=$id-b ;;
  cifs|cifs-shared-mount)
    # Samba stand-in for a Windows file server. Docker mounts the share for
    # the volume in the host's namespace, so it goes to the server's address.
    docker run -d --name "$id-smb" --network "$net" debian:trixie-slim sh -c '
      apt-get update >/dev/null && apt-get install -y --no-install-recommends samba >/dev/null &&
      mkdir -p /srv/share && chmod 777 /srv/share &&
      printf "[global]\nserver min protocol = SMB2_02\nmap to guest = Bad User\n[share]\npath = /srv/share\nread only = no\nguest ok = yes\nforce user = nobody\nnt acl support = no\ncreate mask = 0666\nforce create mode = 0666\ndirectory mask = 0777\n" > /etc/samba/smb.conf &&
      exec smbd --foreground --no-process-group' >/dev/null
    smb_ip=$(docker inspect -f "{{(index .NetworkSettings.Networks \"$net\").IPAddress}}" "$id-smb")
    i=0
    until docker exec "$id-smb" sh -c 'command -v smbd >/dev/null && pgrep smbd >/dev/null'; do
      i=$((i + 1)); [ "$i" -lt 120 ] || { echo "samba did not start" >&2; exit 2; }; sleep 1
    done
    sleep 2
    cifs_volume() {   # cifs_volume NAME: the share as a Docker volume, its own SMB session
      docker volume create --driver local --opt type=cifs \
        --opt "device=//$smb_ip/share" \
        --opt "o=guest,uid=1000,gid=1000,vers=3.0,nosharesock,${CIFS_OPTS:-cache=strict}" "$1" >/dev/null
    }
    cifs_volume "$vol"
    start "$id-a" "$vol:/shared"
    if [ "$topo" = cifs ]; then
      cifs_volume "$vol-b"
      start "$id-b" "$vol-b:/shared"
    else
      start "$id-b" "$vol:/shared"
    fi
    b=$id-b ;;
  *) echo "unknown topology: $topo" >&2; exit 2 ;;
esac
a=$id-a

lm() {   # lm CONTAINER ARGS... (Windows path of the shared folder is Z:/shared)
  c=$1; shift
  docker exec -u webapp "$c" wine /opt/lockmatrix.exe "$@" 2>/dev/null | tr -d '\r' | tail -n 1
}

hold() {   # hold ARGS... : run A in the background until it prints READY
  docker exec -u webapp -d "$a" sh -c "wine /opt/lockmatrix.exe $* >/tmp/hold.out 2>/dev/null"
  i=0
  until docker exec "$a" grep -q READY /tmp/hold.out 2>/dev/null; do
    i=$((i + 1)); [ "$i" -lt 60 ] || { echo "A never held: $(docker exec "$a" cat /tmp/hold.out 2>/dev/null)" >&2; exit 2; }
    sleep 0.5
  done
}

release() {   # end A's hold, and wait until its process is gone
  docker exec "$a" sh -c 'pkill -f lockmatrix.exe; rm -f /tmp/hold.out' || true
  sleep 1
}

fails=0
case_() {   # case_ DESCRIPTION EXPECTED ACTUAL
  mark="ok  "
  [ "$2" = "$3" ] || { mark="DIFF"; fails=$((fails + 1)); }
  printf '%s %-58s windows=%-18s wine=%s\n' "$mark" "$1" "$2" "$3"
}

f='Z:/shared/data.tps'   # forward slashes survive the sh -c in hold()
lm "$a" try-write "$f" 0 init >/dev/null    # create the file

echo "== $topo ($image)"

hold hold-open "$f" none 20
case_ "share: A opens deny-all; B opens rw" SHARING_VIOLATION "$(lm "$b" try-open "$f" rw rw)"
release

hold hold-open "$f" read 20
case_ "share: A opens deny-write; B opens for write" SHARING_VIOLATION "$(lm "$b" try-open "$f" w rw)"
case_ "share: A opens deny-write; B opens read-only" OK "$(lm "$b" try-open "$f" r rw)"
release

hold hold-lock "$f" 100 10 20
case_ "lock: A locks 100+10; B locks the same range" LOCK_VIOLATION "$(lm "$b" try-lock "$f" 100 10)"
case_ "lock: A locks 100+10; B locks 200+10" OK "$(lm "$b" try-lock "$f" 200 10)"
case_ "lock: A locks 100+10; B writes into it" LOCK_VIOLATION "$(lm "$b" try-write "$f" 104 X)"
case_ "lock: A locks 100+10; B reads from it" LOCK_VIOLATION "$(lm "$b" try-read "$f" 104 2)"
release

hold hold-lock "$f" 0x1000000F0 1 20
case_ "lock: A locks above 4 GiB; B locks the same byte" LOCK_VIOLATION "$(lm "$b" try-lock "$f" 0x1000000F0 1)"
release

token=w$$
lm "$a" try-write "$f" 300 "$token" >/dev/null
case_ "coherence: A writes; B reads it at once" "$token" "$(lm "$b" read "$f" 300 ${#token})"

echo "$fails difference(s) from Windows"
[ "$fails" -eq 0 ]
