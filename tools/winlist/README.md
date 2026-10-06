# winlist

Lists the top-level windows on the app's Wine desktop, top to bottom, and can
bring one to the front. The Wine virtual desktop has no taskbar, so a window
that ends up behind a maximized main window is otherwise unreachable. It also
fits the desktop to a new screen size (`fit W H`).

The image builds it (`Dockerfile` target `tools`) and installs it as
`/usr/local/lib/wine-webapp/winlist.exe`; the `vnc` front end's window list
runs it per request (`docs/frontends.md`, "Window list"), and its gateway runs
`fit` when the browser window has been resized ("Screen size"). By hand
(`fit` needs `-e DISPLAY=:1`, or Wine has no screen to size):

```sh
docker exec -u webapp <container> wine /usr/local/lib/wine-webapp/winlist.exe
docker exec -u webapp <container> wine /usr/local/lib/wine-webapp/winlist.exe raise 2017c
docker exec -u webapp -e DISPLAY=:1 <container> wine /usr/local/lib/wine-webapp/winlist.exe fit 1280 720
```

To build and try a changed copy without rebuilding the image:

```sh
docker run --rm -v "$PWD/tools/winlist:/w" -w /w debian:trixie-slim sh -c \
  'apt-get update >/dev/null && apt-get install -y --no-install-recommends gcc-mingw-w64-i686 >/dev/null && i686-w64-mingw32-gcc -O2 -Wall -o winlist.exe winlist.c'
docker cp tools/winlist/winlist.exe <container>:/tmp/
docker exec -u webapp <container> wine /tmp/winlist.exe            # list
docker exec -u webapp <container> wine /tmp/winlist.exe raise 2017c   # bring forward
```

Output columns: window handle, process id, `V` visible / `E` enabled,
screen rectangle, window class, title. `-d NAME` picks a Wine desktop other
than `webapp`.

`-t` prints one tab-separated line per window, for programs: handle, owner
handle (`00000000` if none), process id, flags, left, top, width, height,
class, title. Flags: `V` visible, `E` enabled, `I` minimized, `Z` maximized,
`T` tool window, `P` topmost. Tabs and line breaks in titles become spaces.
`raise` exits with 2 if the handle is not a window.

`fit W H` sets the desktop's screen to WxH and prints what it did: the size,
then each maximized window it stretched and each window it moved back onto
the screen. Wine accepts a size only up to the X screen's, so resize the X
screen first; `fit` retries for three seconds, then exits with 3.
