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

The browser shows one canvas: the whole virtual screen, the size of the
browser window (see "Screen size" below), or scaled to it. The Wine virtual
desktop fills that screen, so Wine draws title bars, dialogs
and window placement itself, and the browser only relays pointer and keyboard
events. There are no per-window grabs for the browser to get wrong.

- VNC listens on localhost inside the container only, without a password.
- `wine-webapp-gateway` serves the web client, bridges its WebSocket to VNC
  (websockify, used as a library) and answers the window list's API, all on
  port 8080.
- `index.html` holds the stock noVNC page (`vnc.html`) in a same-origin iframe,
  opened connected and reconnecting on its own, so a page reload or an app
  restart needs no user action. It adds the window list on top.

### Window list

The Wine virtual desktop has no taskbar. When an app's main window is
maximized and gets focus, other top-level windows (another program's window,
a dialog waiting for input) can end up behind it with no way back. A
"Windows" tab at the top centre of the page, over the app's title bar (the top
right corner holds a maximized window's own buttons), lists the top-level
windows a user can switch to, top of the z-order first, and brings the chosen
one to the front.

| Piece | What it does |
| --- | --- |
| `GET /api/windows` | Visible, titled, non-tool windows from `winlist.exe -t`: handle, owner, process, title, class, enabled/minimized/maximized, rectangle |
| `POST /api/windows/HWND/raise` | `winlist.exe raise HWND`: restore if minimized, bring to the front, activate. Refused when `Origin` matches neither `Host` nor `X-Forwarded-Host`; a reverse proxy that rewrites `Host` must send one of them |
| The panel | Fetches the list only when opened; nothing polls. A disabled window is waiting for a dialog, so choosing it raises the enabled dialog it owns, when one is listed. Keyboard focus goes back to the app when the panel closes |

Each request runs `winlist.exe` once, as user `webapp`. No helper stays
running under Wine: `wine-webapp-run` notices that the app has quit by waiting
for `wineserver` to exit, and a resident Wine process would keep it alive.

Checked from a browser 2026-10-05 with the Clarion app it was built for, by
its user: a report program's window that had gone behind the maximized main
window came back through the list, and quitting restarted the app.

Wine's own taskbar (`explorer /desktop=...` with the shell enabled) was the
alternative; it comes with a Start menu that lets a user launch other programs
in the container.

### Start-up screen

While the app starts, and after a user quits until it is back, there is no
Wine desktop on the X screen and the browser showed black for a few seconds.
The front end paints the X root window instead (`hsetroot`): the app image's
`splash.png` centred on `WEBAPP_BACKGROUND` (`app-contract.md`, "Build-time
hooks"). `wine-webapp-init-prefix` gives the Wine desktop the same colour, so
the hand-over from start-up screen to app doesn't flash.

The splash goes on the X root only, not as a Windows wallpaper: tried
2026-10-05, Wine painted the wallpaper over the app's window after a restart,
and a "starting" screen would stay visible behind every window.

### Installed as an app

The gateway serves a web app manifest (`/manifest.webmanifest`: `WEBAPP_NAME`,
the image's icons, `display: standalone`), so Chrome and Edge offer to install
the page: the app gets a desktop or Start-menu icon and opens in its own window
with no address bar or tabs, like a native program. Launching it again focuses
the open window (`launch_handler`). Installing needs a secure context: HTTPS
from the reverse proxy, or `http://localhost` for a test.

With a fixed screen (`WEBAPP_RESIZE=scale`), the page asks the installed
window to take the size of the screen (`WEBAPP_SCREEN`) plus its frame,
centred, so noVNC doesn't scale; on every start, only in the installed window,
and only when it fits on the monitor. With the screen following the window
(the default) there is nothing to do: the window keeps the size the user gave
it.

### Screen size

`WEBAPP_RESIZE=remote` (the default): the screen is the size of the browser
window, one browser pixel per screen pixel, so nothing is scaled, and it
follows when the window is resized. "Pixel" is the browser's: on a monitor set
to 125 %, the app is drawn at 125 %, as a non-DPI-aware program is by Windows.

1. noVNC (`resize=remote`) asks Xvnc (`-AcceptSetDesktopSize=1`) for a screen
   the size of its frame, half a second after the last resize.
2. The page then posts the same size to `POST /api/screen` (once settled, and
   after each connect, since a restarted container starts at `WEBAPP_SCREEN`).
3. The gateway runs `winlist.exe fit W H`: `ChangeDisplaySettingsEx` on the
   app's desktop, retried for up to three seconds, since Wine accepts only a
   size up to the X screen's, which may not have caught up. Then maximized
   windows are stretched to the new size, keeping their maximized state, and
   windows left partly off the screen are moved back onto it. Windows itself
   does that last step on a resolution change; Wine doesn't.

The Wine desktop is started at `WEBAPP_SCREEN_MAX` (3840x2160): the X screen
crops it, Wine reports the X screen's size to the app, and the desktop window
never ends up smaller than the screen when it grows. An app (re)started after
a resize starts at the current size. Below `WEBAPP_SCREEN_MIN` (1024x700) the
screen stops shrinking and the page scrolls, so the app's fixed-size dialogs
still fit; above `WEBAPP_SCREEN_MAX` the screen stops growing.

`WEBAPP_RESIZE=scale`: the screen stays `WEBAPP_SCREEN` and noVNC scales it to
the window, as the front end did at first.

Tested 2026-10-05 with Notepad maximized, from Chromium: shrinking, growing
past the starting size, the minimum, a quit and restart, a container restart;
clicks land where they should after a resize. Two dead ends: resizing the Wine desktop's X window alone leaves the Windows side
at the old size, and Wine without a virtual desktop (`Decorated=N`, no window
manager) disagreed with X about window sizes (a maximized window 1508x858 to
Windows, 855x641 on screen) and didn't draw its title bar. A program asking
for a new resolution must run on the app's desktop (`webapp`, as
`winlist.exe` does) and with `DISPLAY` set; otherwise it gets Wine's default
desktop or no display at all, and the answers make no sense.

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

- **wineserver crashes on a screen resize, now and then.** Seen 2026-10-05
  with the Clarion app: grow, shrink, shrink again, and the screen went black.
  Kernel log: `wineserver ... trap divide error`. The division is in
  `map_point_raw_to_virt` (`server/window.c`, Wine 11.0), which maps a pointer
  position onto the nearest monitor and divides by that monitor's raw width
  with no check for zero; a monitor list with an empty rectangle, probably
  sent mid-way through a display change, does it. Not reproduced on demand
  (the login screen alone, the pointer at the corner while shrinking).
  `wine-webapp-run` now notices that the server is gone and restarts the app
  (all Wine processes block forever on a dead server, so before that the
  supervisor waited for good). The fix belongs in the fork: skip monitors
  with an empty rectangle in `get_monitor_from_rect`, or return unmapped in
  `map_point_raw_to_virt`; that needs the image to build `wineserver` from
  the fork, not just the 32-bit DLLs.
- **Installed app: not yet verified in a browser.** The manifest, icons and
  the window sizing were written 2026-10-05; still to do per AGENTS.md
  "Verify in a browser": install from Chrome and from Edge on Windows, check
  the window opens unscaled and centred, launch it again and check it
  focuses the open window, and check which browser shortcuts (Ctrl+W, Ctrl+N,
  Ctrl+T) the app window still takes from the app.
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
