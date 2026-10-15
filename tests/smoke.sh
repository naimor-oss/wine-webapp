#!/bin/sh
# Headless smoke test of an app image built on wine-webapp:vnc. Starts the
# image, then checks through the gateway that the noVNC client is served and
# that the app's window is on the Wine desktop (GET /api/windows runs winlist
# under Wine, so this covers Xvnc, Wine, the supervisor and the gateway).
# It does not replace using the app from a browser (AGENTS.md).
#
#   tests/smoke.sh [IMAGE] [TITLE]
#   tests/smoke.sh wine-webapp-example-notepad Notepad    # the defaults
#
# Exit 0 when every check passes; on failure the container's logs are shown.
set -eu

image=${1:-wine-webapp-example-notepad}
title=${2:-Notepad}
timeout=${SMOKE_TIMEOUT:-120}
name=wine-webapp-smoke-$$

cleanup() { docker rm -f "$name" >/dev/null 2>&1 || true; }
trap cleanup EXIT INT TERM

fail() {
  echo "FAIL: $*" >&2
  docker exec "$name" sh -c 'tail -n 30 /tmp/*.log' >&2 2>/dev/null || true
  docker logs --tail 30 "$name" >&2 2>/dev/null || true
  exit 1
}

# Publish on a free host port, so the test can run next to a running app.
docker run -d --name "$name" -p 127.0.0.1::8080 "$image" >/dev/null
port=$(docker port "$name" 8080/tcp | sed -n 's/.*://p' | head -n 1)
[ -n "$port" ] || fail "no host port for 8080"
base=http://127.0.0.1:$port

echo "== image: $image"
docker image inspect -f '{{range $k, $v := .Config.Labels}}{{$k}}={{$v}}{{"\n"}}{{end}}' "$image" \
  | grep -E 'image\.(version|revision)|wine-' || true

i=0
until curl -fsS -o /dev/null "$base/vnc.html"; do
  i=$((i + 1)); [ "$i" -lt "$timeout" ] || fail "gateway did not serve /vnc.html in ${timeout}s"
  sleep 1
done
echo "ok   gateway serves the noVNC client (${i}s)"

i=0
while :; do
  windows=$(curl -fsS "$base/api/windows" 2>/dev/null || true)
  case $windows in *"$title"*) break ;; esac
  i=$((i + 1)); [ "$i" -lt "$timeout" ] || fail "no window titled '$title' in ${timeout}s: $windows"
  sleep 1
done
echo "ok   window '$title' is on the Wine desktop (${i}s)"

docker exec "$name" pgrep -x Xvnc >/dev/null || fail "Xvnc is not running"
echo "ok   Xvnc running"
echo "PASS"
