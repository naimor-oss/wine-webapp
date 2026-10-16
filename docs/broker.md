# Several users: the session broker

One app container per signed-in user, all behind one address, so that each
person gets their own Windows desktop and the app sees each of them as a
separate workstation. `broker/wine-webapp-broker` does it; a sign-in gate in
front of it decides who the user is.

## Where do I start?

| If you want to … | Read |
| --- | --- |
| Understand the pieces | [Shape](#shape) |
| Configure it | [Configuration](#configuration), `broker/example.toml` |
| Put the Kerberos sign-in gate in front | [The sign-in gate](#the-sign-in-gate), `gate/` |
| Sign the user in to the app itself | [`app-contract.md`](app-contract.md), "Login" |
| Close idle sessions | [Idle sessions](#idle-sessions) |
| Run something that needs everyone out | [Maintenance](#maintenance) |
| Know what is trusted with what | [Security](#security) |

## Shape

```text
browser ── HTTPS ──► sign-in gate ── X-Remote-User ──► broker ──► app-alice  (container)
                     (Kerberos,      (internal          │     ──► app-bob
                      OIDC, …)        network only)      └── Docker socket
```

- **The gate** authenticates the user (for example Kerberos/SPNEGO with an
  OIDC fallback) and passes the name in a header. It must remove that
  header from what the browser sends. The broker trusts the header only
  from the addresses in `trusted_proxies`.
- **The broker** looks the name up in the users file. A name not in it is
  refused (403) before anything starts. For a known user it starts their
  container on the first page load (never for anything else), shows a
  "signing in" page until the app's login is done, then passes everything,
  WebSocket included, to that container.
- **Each container** is the app image (`FROM wine-webapp:vnc`) with a stable
  host name, `<name_prefix>-<app user>`. Windows apps see it as the computer
  name, upper-cased and cut to 15 characters; the broker refuses a users
  file where two users would get the same one. Apps that count licences or
  sessions per workstation then see one workstation per user.

Data the users share (a file-share database, documents) is a Docker volume
mounted into every container; see `tools/lockmatrix/README.md` for what
Wine's file locking does and doesn't give across containers.

## Configuration

`broker.toml`, see `broker/example.toml`:

| Key | Meaning |
| --- | --- |
| `[broker] identity_header` | header the gate sets (default `X-Remote-User`) |
| `[broker] trusted_proxies` | addresses the header is trusted from |
| `[broker] admins` | identities that may use `/_broker/status` and `/_broker/maintenance` |
| `[broker] max_sessions` | sessions at once; the next user gets "all sessions in use" |
| `[broker] users_file` | the users file (below) |
| `[session] image`, `volumes`, `env`, `memory_mb` | how each user's container is made |
| `[session] name_prefix` | container and host name prefix, at most 9 characters |
| `[idle]` | see [Idle sessions](#idle-sessions) |

The users file holds credentials: keep it readable by root only.

```toml
["alice@EXAMPLE.COM"]
app_user = "ALICE"           # the app's own user name
password = "…"               # the app's own password for that user
expect_title = "(Alice Doe)" # text the app's main window shows when she is signed in
```

Run the broker in its own container on the network the session containers
use, with the Docker socket and the configuration mounted, and the gate in
front: see `broker/Dockerfile`.

## The sign-in gate

`gate/` is a gate for a Windows domain or any Kerberos realm: Apache with
`mod_auth_gssapi`. A browser that has a ticket and is allowed to use it for
the site signs in silently (SPNEGO). Any other browser gets its own sign-in
prompt, and the gate checks the user name and password against the KDC with
the same keytab. Either way the broker receives the Kerberos principal,
`alice@EXAMPLE.COM`, the users file's key. A typed name without a realm gets
the default realm; `DOMAIN\alice` is not understood.

```sh
docker build -t wine-webapp-gate gate/
docker run -d --name gate --network <the sessions' network> -p 443:443 \
  -e GATE_SERVER_NAME=app.example.com -e GATE_REALM=EXAMPLE.COM \
  -v /path/to/gate-files:/etc/wine-webapp-gate:ro wine-webapp-gate
sh gate/test.sh    # end to end against a throwaway KDC; leaves nothing behind
```

`/etc/wine-webapp-gate` holds `tls.crt` (the chain), `tls.key`,
`http.keytab` (for `HTTP/<GATE_SERVER_NAME>@<GATE_REALM>`) and optionally
`krb5.conf`. The keytab and the key are secrets: keep the folder readable by
root only.

| Variable | Default | Meaning |
| --- | --- | --- |
| `GATE_SERVER_NAME` | (required) | the host name browsers use; the service principal is `HTTP/` that name |
| `GATE_REALM` | (required) | the Kerberos realm, upper case |
| `GATE_KDC` | from DNS | KDC host(s), when DNS `_kerberos` records can't be used |
| `GATE_BACKEND` | `http://broker:8000` | the broker |
| `GATE_IDENTITY_HEADER` | `X-Remote-User` | as `[broker] identity_header` |
| `GATE_AUTH_NAME` | `Sign in` | the realm name shown in the password prompt |
| `GATE_SESSION_SECONDS` | `43200` | how long a sign-in lasts before the browser is asked again |
| `GATE_PROXY_TIMEOUT` | `3600` | seconds an idle request or WebSocket is kept |
| `GATE_LOG_LEVEL` | `warn` | Apache `LogLevel`, e.g. `warn auth_gssapi:debug` |

What the domain side needs:

- **A service account and keytab:** an account with the SPN
  `HTTP/<GATE_SERVER_NAME>` and AES keys, and a keytab exported for it.
- **The name as an A record, not a CNAME.** Browsers build the SPN from the
  canonical name.
- **Browser policy.** Chrome and Edge offer Kerberos only to sites in their
  allowlist (`AuthServerAllowlist`). Without it, the user gets the password
  prompt.
- **The gate reaches a KDC for typed passwords.** A silent Kerberos sign-in
  needs only the keytab.

After a sign-in the browser holds a session cookie (`GssapiUseSessions`), so
the screen's WebSocket doesn't negotiate again. The cookie's key is new on
each start of the gate, so a restart asks everyone again: silently with
Kerberos, with a prompt for password users.

## Signing in to the app

When a container starts, the broker writes that user's credentials into it
as a root-only file and starts `wine-webapp-login`, which fills the app's own
login window through the app's login hook ([`app-contract.md`](app-contract.md),
"Login"). Until it reports success the browser only sees "Signing in":
the login is filled at the container's fixed screen size, before the browser
can resize the screen or type into the form. A login that fails is not
retried for a minute, so a wrong password can't trip an app that closes
after a few failures. If the app's main window later shows another user
(someone quit and signed in as someone else), the app is restarted and signed
in again as the session's user.

While the app is signed in again inside a running session (after a crash,
or after the wrong user was detected), the page covers the screen with
"Signing in…" and the app can't be clicked or typed into, until the login
watcher reports success. The signing-in page and that cover look the same:
the app's name (`WEBAPP_NAME`) on its colour (`WEBAPP_BACKGROUND`), read
from the session image and `[session] env`, and opaque, so the login form
being filled never shows.

## Closing the app

By default the app is started again whenever it exits, so a user who quits
finds a fresh start screen. Behind the broker, set `WEBAPP_EXIT_ENDS_SESSION=1`
in the app image instead: when the user closes the app, the session ends,
the app gives up what it holds (licences, locks) at once, and the page says
it was closed, with an "Open again" button that goes through the signing-in
page like a first visit. The page then stops talking to the session (noVNC
included), so nothing of the old screen is left behind the message.

Closing the window or tab works too: reopening it always starts from the
signing-in page. Neither the broker nor the gateway lets the browser cache
the page (`Cache-Control: no-store`), since a cached copy would come back
without asking the broker and find its session gone; a page that was kept
anyway (the back/forward cache) loads itself again.

## Closed windows

When the user closes the page (its tab or the installed app's window), it
tells the broker on its way out (a beacon to `/_session/closing`, which the
broker answers itself). If no page connects again within `detached_minutes`,
the session is closed the way a user would close it, so the app gives up its
licence and the next visit starts afresh; reopened sooner, the app is as it
was left. A reload sends the beacon too but is back within a second.

A page that goes away without saying so (a computer asleep, a network
drop) is not counted as closed: the idle limits below apply, so someone
interrupted for an hour during the working day still finds the app. So does
a window left open but unattended.

A page load while a session is being stopped waits on the signing-in page
and then gets a new session, never the app that is quitting.

## Idle sessions

A session is idle when nobody has pressed a key or moved the mouse in it
(measured in the session's X server, `xprintidle`), whether or not the
browser is still open. How long is too long depends on the time of day:

```toml
[idle]
default_minutes = 30          # outside the windows below
warn_minutes = 10             # warn this long before closing
detached_minutes = 10         # page closed by the user this long: close the session (0: never)
[[idle.window]]
days = "Mon-Fri"
from = "06:00"
to = "18:30"
minutes = 120                 # an interrupted user keeps the session for 2 hours
```

The limit is the one in force when the user stopped: someone who leaves at
18:00 still gets two hours. Times are the broker's local time (set `TZ` on
its container). `warn_minutes` before the limit, the page shows a notice
with the closing time and, when the window isn't in front, a system
notification (allowed by the user once). Any input clears it. At the
limit the session is closed the way a user would close it (the app's quit
hook) and the container removed. An open page then says the session was
closed after a time without activity, with "Open again"; its background
requests don't start the session again.

## Maintenance

`POST /_broker/maintenance {"on": true}` (as an admin) refuses new sessions
and closes every running one gracefully; `{"on": false}` reopens. Use it
before anything that needs every user out, such as a database repair tool
that relies on exclusive file access, which Wine can't enforce across
containers. `GET /_broker/status` lists sessions and the mode.

## Security

| Who | Holds | Why |
| --- | --- | --- |
| The gate | the user's proof of identity | only it decides who someone is |
| The broker | the Docker socket, the users file | starts containers; the socket is root on the host, so nothing else gets it |
| A session container | its own user's credentials, once, root-only, deleted after reading | the browser user can browse the container's files through Wine's `Z:` drive, so nothing else is in there |

Session containers don't publish ports; only the broker reaches them. The
app's own security is unchanged: a user can still do inside the app what
their app account allows.
