# Front ends

How the app's screen reaches the browser. Each front end is a build target
`FROM base` with its own entrypoint; the app contract
([`app-contract.md`](app-contract.md)) is the same for all of them, so switching
front ends means switching the base tag of an app image.

| Target | Status | What it is |
| --- | --- | --- |
| `vnc` | working | TigerVNC `Xvnc` + noVNC (websockify) |
| `xpra` | not built; tried, see below | Xpra HTML5 client |

## Contract for a front end

A front-end target must:

1. Start a display server and set `DISPLAY` for the app.
2. Serve the browser client on port 8080, with no authentication of its own
   (access control belongs in a reverse proxy in front of it).
3. Run `wine-webapp-run` as user `webapp` in the foreground, under `tini`.
4. Make the working directory (`APP_WORKDIR`) writable for `webapp`.

## `vnc`: TigerVNC + noVNC

The browser shows one canvas: the whole virtual screen, scaled to the window.
The Wine virtual desktop fills that screen, so Wine draws title bars, dialogs
and window placement itself, and the browser only relays pointer and keyboard
events. There are no per-window grabs for the browser to get wrong.

- VNC listens on localhost inside the container only, without a password.
- `wine-webapp-gateway` serves the web client, bridges its WebSocket to VNC
  (websockify, used as a library) and answers the window list's API, all on
  port 8080.
- `index.html` holds the stock noVNC page (`vnc.html`) in a same-origin iframe,
  opened connected, scaled, and reconnecting on its own, so a page reload or an
  app restart needs no user action. It adds the window list on top.

### Window list

The Wine virtual desktop has no taskbar. When an app's main window is
maximized and gets focus, other top-level windows (another program's window,
a dialog waiting for input) can end up behind it with no way back. A
"Windows" button at the top right of the page lists the top-level windows a
user can switch to, top of the z-order first, and brings the chosen one to the
front.

| Piece | What it does |
| --- | --- |
| `GET /api/windows` | Visible, titled, non-tool windows from `winlist.exe -t`: handle, owner, process, title, class, enabled/minimized/maximized, rectangle |
| `POST /api/windows/HWND/raise` | `winlist.exe raise HWND`: restore if minimized, bring to the front, activate. Refused when `Origin` matches neither `Host` nor `X-Forwarded-Host`; a reverse proxy that rewrites `Host` must send one of them |
| The panel | Fetches the list only when opened; nothing polls. A disabled window is waiting for a dialog, so choosing it raises the enabled dialog it owns, when one is listed. Keyboard focus goes back to the app when the panel closes |

Each request runs `winlist.exe` once, as user `webapp`. No helper stays
running under Wine: `wine-webapp-run` notices that the app has quit by waiting
for `wineserver` to exit, and a resident Wine process would keep it alive.

Wine's own taskbar (`explorer /desktop=...` with the shell enabled) was the
alternative; it comes with a Start menu that lets a user launch other programs
in the container.
- The screen size is fixed per container (`WEBAPP_SCREEN`); noVNC scales it.

## Xpra (tried, not included)

Xpra was the first choice because its HTML5 client can forward print jobs to
the browser as PDF and transfer files. In seamless mode (each Wine window as its
own browser window) users hit keyboard-capture and mouse problems: after typing
in a field, clicks stopped working.

That symptom was later traced to a Wine bug, not to Xpra: the edit control kept
the mouse capture (see [`wine-patches.md`](wine-patches.md)). With the patched
Wine DLLs, Xpra deserves a second look, in desktop mode (`start-desktop` with a
Wine virtual desktop) rather than seamless mode. Keep that in mind before
treating the earlier experience as a verdict on Xpra.

## Open items

- **Window list: not yet verified against Wine.** Tested 2026-10-05 without
  Wine: the API and panel with a stub in place of `winlist.exe` (list,
  filtering, raising a waiting window's dialog, cross-origin refusal), and
  the image's gateway against a real Xvnc (noVNC connects through it, a
  failing `winlist.exe` shows its error in the panel, closing the panel
  puts keyboard focus back on noVNC's canvas). To reproduce without an app
  that hides windows: run `examples/notepad`, maximize Notepad, then
  `docker exec -u webapp <container> wine explorer /desktop=webapp regedit`
  (same desktop name, or it opens outside the virtual desktop) and click
  Notepad; regedit goes behind it. `playwright-cli` (developer baseline)
  can drive the page; the button is `#win-button`. Still to do on an x86-64 host, per AGENTS.md "Verify in a
  browser": hide a window behind a maximized one and bring it back; type
  right after a raise; quit and check the supervisor restarts the app (no
  Wine process left behind by the gateway). First seen 2026-09-30: a report
  program's window and its pending dialog went behind a Clarion app's main
  window after one click on the main menu.
- **The supervisor stops on an app failure.** `wine-webapp-run` runs under
  `set -e`, so a non-zero exit from `wine explorer ...` (a crash, an abort)
  ends the script and the container instead of restarting the app as its
  header promises. Seen 2026-10-05 when Wine aborted at start. Not fixed yet.
- **Repaint lag after moving a window.** When a top-level window is dragged,
  the strip of desktop uncovered by the last move step stays stale until the
  next input event (a mouse move clears it). Cosmetic; seen with stock Wine
  11.0 64-bit processes too, so not caused by our patches.
- **First click on another program's window only activates it.** Seen with a
  report program started by the main app: the first click on a button in its
  (inactive) window activated the window, the second pressed the button. On
  Windows one click normally does both. Not investigated yet.
- **Clipboard** between the browser and the app: not yet checked for `vnc`.
- **Printing / file hand-off** to the browser user: not provided by `vnc`.
