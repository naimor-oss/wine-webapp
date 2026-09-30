# winlist

Lists the top-level windows on the app's Wine desktop, top to bottom, and can
bring one to the front. The Wine virtual desktop has no taskbar, so a window
that ends up behind a maximized main window is otherwise unreachable.

```sh
# build the 32-bit program (any machine with Docker)
docker run --rm -v "$PWD/tools/winlist:/w" -w /w debian:trixie-slim sh -c \
  'apt-get update >/dev/null && apt-get install -y --no-install-recommends gcc-mingw-w64-i686 >/dev/null && i686-w64-mingw32-gcc -O2 -Wall -o winlist.exe winlist.c'
docker cp tools/winlist/winlist.exe <container>:/tmp/
docker exec -u webapp <container> wine /tmp/winlist.exe            # list
docker exec -u webapp <container> wine /tmp/winlist.exe raise 2017c   # bring forward
```

Output columns: window handle, process id, `V` visible / `E` enabled,
screen rectangle, window class, title. `-d NAME` picks a Wine desktop other
than `webapp`.

A diagnostic tool today; the likely basis for a browser-side window list
(see `docs/frontends.md`, "Hidden windows").
