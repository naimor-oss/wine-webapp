# App contract

What an application image built `FROM wine-webapp:<front end>` provides, and
what the base does with it.

| If you want to … | Section |
| --- | --- |
| Write a Dockerfile for a new app | [Minimal app image](#minimal-app-image) |
| Know which variables exist | [Runtime variables](#runtime-variables) |
| Add fonts or registry settings | [Build-time hooks](#build-time-hooks) |
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
| `WEBAPP_SCREEN` | `1600x900` | Size of the virtual screen and of the Wine desktop. The browser client scales it to the window. |

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

The base ships `prefix.d/10-disable-visual-theme.reg`, a workaround for a Wine
bug (see [`wine-patches.md`](wine-patches.md)). An app image that does not need
it can delete it before running `wine-webapp-init-prefix`, but the theme
setting already applied by the base stays until overridden.

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
it launched), then starts it again. A user who quits gets a fresh start screen
on the next connect rather than an empty desktop. If the app keeps exiting
within 10 seconds, restarts back off to every 30 seconds.

Anything the app launches (helper programs, report viewers, schedulers) runs
in the same Wine prefix and shows on the same desktop.

## Users and multi-user

One container is one desktop for one person at a time. For several users, run
one container per user against a shared data volume. Wine's byte-range file
locks are visible across containers on a local Linux file system, so file-share
databases that rely on Windows locking keep working;
[`tools/locktest`](../tools/locktest/) checks this on a given host and storage.
Network file systems (SMB, NFS) are not validated.
