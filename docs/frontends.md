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
- `index.html` opens noVNC connected, scaled, and reconnecting on its own, so
  a page reload or an app restart needs no user action.
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

- **Hidden windows.** The Wine virtual desktop has no taskbar. When an app's
  main window is maximized and gets focus, other top-level windows (another
  program's window, a dialog waiting for input) end up behind it and there is
  no way to bring them back. Seen 2026-09-30: a report program's window and
  its pending dialog went behind a Clarion app's main window after one click
  on the main menu. `tools/winlist` lists the windows and `winlist raise`
  brings one back (verified); a browser-side "windows" control built on it is
  the likely fix. Wine's own taskbar is an alternative, but it comes with a
  Start menu that lets a user launch other programs in the container.
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
