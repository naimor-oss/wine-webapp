#!/bin/sh
# gate/test.sh: check the sign-in gate end to end on any Docker host, against
# a throwaway KDC (realm EXAMPLE.TEST) and an echo backend in place of the
# broker. Builds wine-webapp-gate first. Leaves nothing behind.
#   sh gate/test.sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
p=wwg-test-$$
net=$p-net
realm=EXAMPLE.TEST
gate=gate.example.test
fail=0

cleanup() {
  docker rm -f "$p-kdc" "$p-backend" "$p-gate" >/dev/null 2>&1 || true
  docker volume rm "$p-files" >/dev/null 2>&1 || true
  docker network rm "$net" >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM

docker build -q -t wine-webapp-gate "$here" >/dev/null
docker build -q -t wine-webapp-gate-testkit - >/dev/null <<'EOF'
FROM debian:trixie-slim
RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      krb5-kdc krb5-admin-server krb5-user curl openssl python3 ca-certificates \
 && rm -rf /var/lib/apt/lists/*
EOF

docker network create "$net" >/dev/null
docker volume create "$p-files" >/dev/null
kit() { docker run --rm --network "$net" -v "$p-files:/files" "$@"; }

krb5conf="[libdefaults]
  default_realm = $realm
  dns_lookup_kdc = false
  rdns = false
  dns_canonicalize_hostname = false
[realms]
  $realm = {
    kdc = kdc.example.test
  }"

# The KDC: a user with a password, and the gate's service principal.
docker run -d --name "$p-kdc" --network "$net" --network-alias kdc.example.test \
  -v "$p-files:/files" wine-webapp-gate-testkit sh -c "
    set -e
    printf '%s\n' '$krb5conf' > /etc/krb5.conf
    cp /etc/krb5.conf /files/krb5.conf
    printf '[realms]\n  $realm = {\n    supported_enctypes = aes256-cts-hmac-sha1-96:normal\n  }\n' > /etc/krb5kdc/kdc.conf
    kdb5_util create -s -r $realm -P master >/dev/null
    kadmin.local -q 'addprinc -pw alice-pw alice' >/dev/null
    kadmin.local -q 'addprinc -randkey HTTP/$gate' >/dev/null
    kadmin.local -q 'ktadd -k /files/http.keytab HTTP/$gate' >/dev/null
    chmod 644 /files/http.keytab
    openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj /CN=$gate \
      -addext subjectAltName=DNS:$gate -keyout /files/tls.key -out /files/tls.crt 2>/dev/null
    chmod 644 /files/tls.key
    chmod 600 /files/krb5.conf   # as a real deployment keeps it: root only
    touch /files/ready
    exec krb5kdc -n" >/dev/null

# The backend: echoes the identity header and the browser's attempt at it;
# answers a WebSocket upgrade with 101.
docker run -d --name "$p-backend" --network "$net" --network-alias broker \
  wine-webapp-gate-testkit python3 -c '
import http.server
class H(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def do_GET(self):
        if self.headers.get("Upgrade", "").lower() == "websocket":
            self.send_response(101)
            self.send_header("Upgrade", "websocket")
            self.send_header("Connection", "Upgrade")
            self.end_headers()
            return
        body = ("user=%s\n" % self.headers.get("X-Remote-User")).encode()
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
    def log_message(self, *a):
        pass
http.server.ThreadingHTTPServer(("", 8000), H).serve_forever()' >/dev/null

i=0
until kit wine-webapp-gate-testkit test -e /files/ready; do
  i=$((i + 1)); [ $i -lt 30 ] || { echo "KDC did not start" >&2; docker logs "$p-kdc"; exit 1; }
  sleep 1
done

docker run -d --name "$p-gate" --network "$net" --network-alias $gate \
  -e GATE_SERVER_NAME=$gate -e GATE_REALM=$realm -e GATE_KDC=kdc.example.test \
  -v "$p-files:/etc/wine-webapp-gate:ro" wine-webapp-gate >/dev/null
sleep 2
docker ps -q --filter name="$p-gate" | grep -q . || { echo "gate did not start" >&2; docker logs "$p-gate"; exit 1; }

check() {  # check NAME EXPECTED ACTUAL
  if [ "$2" = "$3" ]; then echo "PASS  $1"; else echo "FAIL  $1: wanted [$2], got [$3]"; fail=1; fi
}
client() {  # client SHELL-SNIPPET: run in a client with krb5.conf and the gate's certificate
  kit wine-webapp-gate-testkit sh -c "cp /files/krb5.conf /etc/krb5.conf; $1"
}
url=https://$gate/
c="curl -s --cacert /files/tls.crt"

check "no credentials: 401" 401 "$(client "$c -o /dev/null -w '%{http_code}' $url")"
check "the 401 offers Negotiate and Basic" "Negotiate Basic" \
  "$(client "$c -D - -o /dev/null $url | sed -n 's/^WWW-Authenticate: \([A-Za-z]*\).*/\1/Ip' | sort -r | tr '\r\n' '  ' | sed 's/ *\$//'")"
check "password: the backend sees the principal" "user=alice@$realm" \
  "$(client "$c -u alice:alice-pw $url")"
check "wrong password: 401" 401 "$(client "$c -o /dev/null -w '%{http_code}' -u alice:wrong $url")"
check "a forged identity header is replaced" "user=alice@$realm" \
  "$(client "$c -u alice:alice-pw -H 'X-Remote-User: mallory@$realm' $url")"
check "Kerberos ticket: the backend sees the principal" "user=alice@$realm" \
  "$(client "echo alice-pw | kinit alice >/dev/null && $c --negotiate -u : $url")"
check "the session cookie alone is enough afterwards" "user=alice@$realm" \
  "$(client "$c -c /tmp/j -o /dev/null -u alice:alice-pw $url && $c -b /tmp/j $url")"
check "WebSocket upgrade reaches the backend" 101 \
  "$(client "$c -b /tmp/j -o /dev/null -w '%{http_code}' -u alice:alice-pw --http1.1 \
       -H 'Connection: Upgrade' -H 'Upgrade: websocket' -H 'Sec-WebSocket-Version: 13' \
       -H 'Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==' --max-time 5 $url")"
check "plain HTTP is not served" 000 \
  "$(client "curl -s -o /dev/null -w '%{http_code}' --max-time 5 http://$gate/ || true")"

[ $fail -eq 0 ] && echo "all passed" || { echo "--- gate log"; docker logs "$p-gate" 2>&1 | tail -20; exit 1; }
