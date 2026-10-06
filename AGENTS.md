# Agent brief: wine-webapp

Container images that run one Windows application under Wine and serve it to
a browser. Start with [`README.md`](README.md) for the purpose and the map.
Shared conventions (git, docs, FIXME discipline) are in
[`../dev-commons/STYLE.md`](../dev-commons/STYLE.md); this file only adds what
is specific here.

## Layers, and what may depend on what

```text
app images (elsewhere, e.g. internal repos)
  FROM wine-webapp:<front end>, use only docs/app-contract.md
front ends (vnc, ...)
  FROM base, follow docs/frontends.md "Contract for a front end"
base
  WineHQ packages (pinned) + DLLs from the Naimor-OSS/wine fork
```

- Nothing application-specific belongs in this repo. If a need shows up for
  one app, add a general hook to the contract instead, and document it.
- Front ends must not change the app contract.

## Rules specific to this repo

- **Wine version pinning.** `WINE_PKG`, `WINE_FORK_REF` and
  `WINE_PATCHED_DLLS` in the `Dockerfile` change together; the patched modules
  (DLLs and `wineserver`) must be built from the same Wine release as the
  installed packages, or `wineserver` and its clients disagree on the protocol.
- **Every Wine change is listed** in [`docs/wine-patches.md`](docs/wine-patches.md)
  with symptom, cause, evidence and removal condition. A workaround file
  carries the same FIXME block (`dev-commons/STYLE.md` §10).
- **No LLM-written code goes to WineHQ.** WineHQ refuses it. Fixes written
  with an agent live only on the fork's `naimor/wine-<version>` branches, with
  a `Co-Authored-By` trailer; upstream gets bug reports that a human files.
- **Clean-room.** When investigating Wine behavior: black-box testing on
  Windows is fine; tracing native Microsoft components (`+relay` on native
  DLLs), reading Microsoft debug symbols, or reading ReactOS code is not.
- **Verify in a browser.** A change to a front end or to the app supervisor
  is not done until it has been used from a browser: type into a field, click
  elsewhere, move a window, quit and reconnect.

## Checks

```sh
docker build --target vnc -t wine-webapp:vnc .           # or with --build-context winesrc=../wine
docker build -t wine-webapp-example-notepad examples/notepad
docker run --rm -p 8080:8080 wine-webapp-example-notepad  # then use it from a browser
sh -n rootfs-base/usr/local/bin/wine-webapp-* rootfs-vnc/usr/local/bin/wine-webapp-frontend
python3 -c "import ast, sys; ast.parse(open(sys.argv[1]).read())" rootfs-vnc/usr/local/bin/wine-webapp-gateway
```

Wine needs an x86-64 Docker host; on Apple Silicon it aborts under both QEMU
and Rosetta (see `README.md`, "Quick start").
