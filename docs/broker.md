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

## Signing in to the app

When a container starts, the broker writes that user's credentials into it
as a root-only file and starts `wine-webapp-login`, which fills the app's own
login window through the app's login hook ([`app-contract.md`](app-contract.md),
"Login"). Until it reports success the browser only sees "Signing you in":
the login is filled at the container's fixed screen size, before the browser
can resize the screen or type into the form. A login that fails is not
retried for a minute, so a wrong password can't trip an app that closes
after a few failures. If the app's main window later shows another user
(someone quit and signed in as someone else), the app is restarted and signed
in again as the session's user.

## Idle sessions

A session is idle when nobody has pressed a key or moved the mouse in it
(measured in the session's X server, `xprintidle`), whether or not the
browser is still open. How long is too long depends on the time of day:

```toml
[idle]
default_minutes = 30          # outside the windows below
warn_minutes = 10             # warn this long before closing
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
hook) and the container removed. An open page then shows "This session was
closed. Reload the page to continue."; its background requests don't start
the session again.

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
