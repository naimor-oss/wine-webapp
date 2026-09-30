# locktest

Checks that Windows byte-range locks taken by Wine in one container are seen by
Wine in another container sharing the same volume. Run it once per host and
storage type before putting a file-share database (Clarion TPS, Access MDB,
dBase, ...) on it.

```sh
# build the 32-bit test program (any machine with Docker)
docker run --rm -v "$PWD/tools/locktest:/w" -w /w debian:trixie-slim sh -c \
  'apt-get update >/dev/null && apt-get install -y --no-install-recommends gcc-mingw-w64-i686 >/dev/null && i686-w64-mingw32-gcc -O2 -o locktest.exe locktest.c'
sh tools/locktest/locktest.sh wine-webapp:vnc
```

Expected: every line reports `BUSY` for the second container. Offsets above
4 GiB are included because TopSpeed and similar engines lock far beyond the
end of the file.

Result on Docker Desktop 29.8 (WSL2 backend), named volume, 2026-09-29: all
`BUSY`, as expected. Not yet run on the Proxmox host or on network storage.
