# lockmatrix

Checks the Windows file-sharing semantics a file-share database relies on,
between two Wine processes placed the way a deployment would place users:

| Case | Windows behavior |
| --- | --- |
| Share modes | An open that conflicts with another process's share mode (deny-write, deny-all) fails with a sharing violation |
| Byte-range lock conflict | Locking a range another process has locked fails, also above 4 GiB |
| Mandatory locking | Reading or writing a range another process has locked fails with a lock violation |
| Coherence | A write by one process is seen by the next read of another |

```sh
# build the 32-bit test program (any machine with Docker)
docker run --rm -v "$PWD/tools/lockmatrix:/w" -w /w debian:trixie-slim sh -c \
  'apt-get update >/dev/null && apt-get install -y --no-install-recommends gcc-mingw-w64-i686 >/dev/null && i686-w64-mingw32-gcc -O2 -Wall -o lockmatrix.exe lockmatrix.c'
tools/lockmatrix/lockmatrix.sh same-wineserver   # two processes, one container
tools/lockmatrix/lockmatrix.sh two-containers    # a container each, one volume
tools/lockmatrix/lockmatrix.sh cifs              # a container each, one SMB share
CIFS_OPTS=cache=none tools/lockmatrix/lockmatrix.sh cifs
```

Each line says what Windows gives and what Wine gave. `lockmatrix.sh` exits
non-zero if anything differs from Windows.

## Results

Wine 11.0 (`wine-webapp:vnc`, fork `naimor/wine-11.0`), Docker 29.9 on WSL2,
2026-10-15. The SMB server was Samba 4.22 in a container, standing in for a
Windows file server, and both containers used one CIFS mount on one host.

| Case | same wineserver | two containers | CIFS, `cache=strict` | CIFS, `cache=none` |
| --- | --- | --- | --- | --- |
| Share mode: deny-all, then open | as Windows | **succeeds** | **succeeds** | **succeeds** |
| Share mode: deny-write, then open for write | as Windows | **succeeds** | **succeeds** | **succeeds** |
| Byte-range lock conflict (also above 4 GiB) | as Windows | as Windows | as Windows | as Windows |
| Write into another process's locked range | **succeeds** | **succeeds** | **succeeds** | **succeeds** |
| Read from another process's locked range | **succeeds** | **succeeds** | **succeeds** | **succeeds** |
| Coherence | as Windows | as Windows | as Windows | as Windows |

What this means:

- **Byte-range locks work everywhere.** Wine maps them to Linux locks on the
  same file, which the kernel (and a CIFS mount on the same host) shares
  between containers. A database that takes a lock before every change,
  such as TopSpeed's, sees other users' locks.
- **Share modes only hold inside one wineserver** in stock Wine. Each container runs its
  own wineserver, which is where Wine keeps share modes, and Linux has no
  equivalent to pass them on. A CIFS mount doesn't help: the Linux client
  doesn't send deny modes to the server. An app that opens a file
  exclusively (for an index rebuild, a counter or a lock file) is protected
  from users in its own wineserver only.
- **The fork's patch 4** ([`docs/wine-patches.md`](../../docs/wine-patches.md))
  records share modes as OFD locks that other wineservers test. Measured
  again with it (fork `naimor/wine-11.0` at `7c54557`, 2026-10-16): share
  modes then match Windows between two containers on one volume; over the
  CIFS mount they still don't, because the Linux client doesn't report other
  descriptions' locks on those bytes.
- **Wine has no mandatory locking**, not even inside one wineserver. On
  Windows, a process that reads or writes a range someone else has locked
  gets a lock violation and usually retries. Under Wine the read or write
  goes through: a reader can see a half-written record, and a writer that
  doesn't lock first isn't stopped.
- **Coherence held** in every topology tested. Across several hosts
  mounting one SMB share, it depends on the client's caching and oplocks.
  That case is not tested here.

`locktest/` is the earlier, narrower check of byte-range locks between
containers.
