# App contract

What an application image built `FROM wine-webapp:<front end>` provides, and
what the base does with it.

| If you want to … | Section |
| --- | --- |
| Write a Dockerfile for a new app | [Minimal app image](#minimal-app-image) |
| Know which variables exist | [Runtime variables](#runtime-variables) |
| Add fonts or registry settings | [Build-time hooks](#build-time-hooks) |
| Change how text is drawn | [Text](#text) |
| Keep data on a volume | [Program and data in one folder](#program-and-data-in-one-folder) |
| Know what happens when the app exits | [Supervision](#supervision) |

## Minimal app image

```dockerfile
FROM wine-webapp:vnc
COPY app/ /opt/myapp/
ENV APP_EXE=/opt/myapp/myapp.exe
```

`examples/notepad/` is the smallest working example (Wine's own 32-bit Notepad, so
there is nothing to copy).

## Runtime variables

| Variable | Default | Meaning |
| --- | --- | --- |
| `APP_EXE` | required | Program to run: a Linux path (`/opt/myapp/app.exe`) or a Windows path (`C:\windows\notepad.exe`). |
| `APP_ARGS` | empty | Arguments, split on whitespace (no quoting). |
| `APP_WORKDIR` | directory of `APP_EXE`, or the home directory for a Windows path | Working directory. Usually a volume. The front end creates it and hands it to user `webapp`. |
| `APP_SYNC_FROM` | empty | If set, copy program files from this directory into `APP_WORKDIR` on every start. See below. |
| `APP_SYNC_KEEP` | `*.ini *.INI` | Globs synced only when missing (user settings). |
| `WEBAPP_SCREEN` | `1600x900` | Size of the virtual screen until a browser connects; with `WEBAPP_RESIZE=scale`, its size for good. |
| `WEBAPP_RESIZE` | `remote` | `remote`: the screen follows the browser window, unscaled. `scale`: it stays `WEBAPP_SCREEN`, scaled to the window. See [`frontends.md`](frontends.md), "Screen size". Set by the `vnc` front end. |
| `WEBAPP_SCREEN_MIN` | `1024x700` | Smallest screen with `remote`; a smaller browser window scrolls. Raise it if the app has larger fixed-size windows. |
| `WEBAPP_SCREEN_MAX` | `3840x2160` | Largest screen with `remote`, and the size the Wine desktop starts at. |
| `WEBAPP_NAME` | file name of `APP_EXE` | Name of the browser tab, and of the app when a user installs it from the browser. |
| `WEBAPP_BACKGROUND` | `#333333` | Colour of the start-up screen and of the Wine desktop. `wine-webapp-init-prefix` sets the desktop colour, so set this before running it in the app image. |

## Build-time hooks

The Wine prefix is created when the image is built, not when the container
starts, so containers start quickly and every container of an image starts
from the same prefix. An app image adds files and then re-runs the prefix
setup, which is idempotent:

```dockerfile
COPY fonts/*.ttf /etc/wine-webapp/fonts/
COPY 50-myapp.reg /etc/wine-webapp/prefix.d/
USER webapp
RUN wine-webapp-init-prefix
USER root
```

| Location | What happens |
| --- | --- |
| `/etc/wine-webapp/prefix.d/*.reg` | Imported with `regedit`, in name order. |
| `/etc/wine-webapp/prefix.d/*.sh` | Run with `sh` as `webapp`, in name order, `WINEPREFIX` set. |
| `/etc/wine-webapp/fonts/*.ttf, *.otf, *.ttc` | Copied to `C:\windows\Fonts` and registered under their full names, the way a Windows installer does. Some apps check the registry, not just the folder. |

Two more files are read by the front end, not by `wine-webapp-init-prefix`, so
they need no re-run:

| Location | What happens |
| --- | --- |
| `/etc/wine-webapp/splash.png` | Start-up screen: shown centred on `WEBAPP_BACKGROUND` while the app starts, and again after the user quits until it is back. Without it, the screen is plain `WEBAPP_BACKGROUND`. Say "starting" on it: it shows only then. |
| `/etc/wine-webapp/icons/icon-N.png` | Square icons, N pixels wide, for the browser tab and the installed app. Browsers want at least `icon-192.png` and `icon-512.png` before they offer to install. |

The base ships `prefix.d/10-disable-visual-theme.reg`, a workaround for a Wine
bug (see [`wine-patches.md`](wine-patches.md)). An app image that does not need
it can delete it before running `wine-webapp-init-prefix`, but the theme
setting already applied by the base stays until overridden.

## Text

The base draws text with ClearType (subpixel smoothing) at every size, as
Windows does by default: `/etc/fonts/conf.d/99-wine-webapp-cleartype.conf`,
with `prefix.d/20-cleartype.reg` saying so to programs that ask. Wine takes
each font's smoothing from fontconfig rather than from the registry, and it
obeys a font's "no smoothing at small sizes" table (`gasp`) for grayscale
smoothing but not for subpixel smoothing. With Debian's grayscale default,
8-10pt text came out unsmoothed (tested 2026-10-05 with Arial and Tahoma). An
app image that wants plain pixels deletes both files.

Fonts: Wine brings Tahoma and bitmap fonts (MS Sans Serif, Small Fonts,
System); the base adds Liberation, which fontconfig gives to Arial,
Microsoft Sans Serif, Segoe UI and the like. Liberation's hinting is poor at
small sizes. An app image that names Microsoft fonts can install the real ones
(Debian's `ttf-mscorefonts-installer` has Arial, Times New Roman, Courier New,
Verdana), under their licence; the base doesn't ship them.

## Program and data in one folder

Much older line-of-business software expects its program files and its data
files in the same folder, and misbehaves when they are split (for example, it
copies its help file from the working directory at start-up). The pattern:

- program files in the image (`/opt/myapp`)
- data on a volume mounted at `APP_WORKDIR`
- `APP_SYNC_FROM=/opt/myapp`: on each start, `wine-webapp-sync` copies program
  files into the volume, replacing a file only when its content changed, via a
  temp file and a rename. Other users' containers sharing the volume keep
  running on their mapped copy instead of seeing a DLL truncated under them.

An image upgrade therefore upgrades the program on the next start without
touching the data.

## Supervision

`wine-webapp-run` starts the app inside a Wine virtual desktop the size of the
screen and waits until **all** Wine processes have exited (the app and anything
it launched), then starts it again, whether it quit normally or crashed. A user
who quits gets a fresh start screen on the next connect rather than an empty
desktop, with the start-up screen in between. If the app keeps exiting within
10 seconds, restarts back off to every 30 seconds.

Anything the app launches (helper programs, report viewers, schedulers) runs
in the same Wine prefix and shows on the same desktop.

## Users and multi-user

One container is one desktop for one person at a time. For several users, run
one container per user against a shared data volume. Wine's byte-range file
locks are visible across containers on a local Linux file system, so file-share
databases that rely on Windows locking keep working;
[`tools/locktest`](../tools/locktest/) checks this on a given host and storage.
Network file systems (SMB, NFS) are not validated.
