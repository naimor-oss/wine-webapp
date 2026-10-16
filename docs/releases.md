# Releases: how images are built, tagged and kept current

How the published images are made, which tag an app image should build on,
and how a base change reaches running apps.

## Where do I start?

| If you want to … | Read |
| --- | --- |
| Pick a tag to build an app image on | [Tags](#tags) |
| Cut a release | [Cutting a release](#cutting-a-release) |
| Move to a new Wine release | [`wine-patches.md`](wine-patches.md), "How patches are carried" |
| See what a running image was built from | [Provenance](#provenance) |

## Inputs are pinned

A rebuild of the same commit gives the same image, except for Debian
packages, which take whatever security updates are current. The Dockerfile pins:

| Input | Pinned by |
| --- | --- |
| Debian base | `DEBIAN` digest (`debian:trixie-slim@sha256:…`) |
| WineHQ packages | exact `WINE_PKG` version |
| WineHQ signing key | `WINEHQ_SIGNER_FPR`; the build fails if the downloaded key differs |
| Patched Wine modules | `WINE_FORK_REF`, a commit on the fork's `naimor/wine-<version>` branch |

Not pinned: the other Debian packages (TigerVNC, noVNC, CUPS, fonts), and
updates to the base image's own packages (`apt-get upgrade`). Pinning those
would freeze security fixes too; the weekly rebuild picks them up instead.

WineHQ has so far kept old package versions in its repository (8.0 to 11.0
stable were all present in October 2026). If one disappears, the published
images still work, but rebuilding that release fails at `apt-get install`.

## Tags

`ghcr.io/naimor-oss/wine-webapp`, built by `.github/workflows/image.yml`:

| Tag | Changes? | Use |
| --- | --- | --- |
| `vnc-X.Y.Z`, `base-X.Y.Z` | rebuilt weekly with Debian security updates, newest release only | Build app images on these |
| `vnc-X.Y`, `base-X.Y` | as above | Same, if you take patch releases automatically |
| `vnc-X.Y.Z-YYYYMMDD`, `base-X.Y.Z-YYYYMMDD` | no | One weekly build of a release, to pin or roll back to |
| `vnc-YYYYMMDD-<sha>`, `base-YYYYMMDD-<sha>` | no | Every build of `main` |
| `vnc`, `base` | every build of `main` | Trying things out only |

Every Monday the workflow rebuilds `main` and the newest `vX.Y.Z` tag
without build cache. The `base` stage runs `apt-get upgrade`, so the
rebuilt images carry the current Debian security fixes. Only the newest
release is rebuilt: move app images to it, rather than staying on an older
one.

App images should build `FROM ghcr.io/naimor-oss/wine-webapp:vnc-X.Y.Z` and
be rebuilt weekly too, or pin `vnc-X.Y.Z-YYYYMMDD@sha256:…` and bump the pin
on purpose. Pull requests only build and test; nothing is pushed.

## Cutting a release

1. On `main`, check that the `image` workflow passed for the commit, and use
   `examples/notepad` from a browser (AGENTS.md, "Verify in a browser").
2. Tag it: `git tag -a vX.Y.Z -m "…"` and push the tag. The workflow pushes
   `vnc-X.Y.Z`, `vnc-X.Y` and `vnc-X.Y.Z-<date>` (and the same for `base`).
3. Versions: bump the minor version for a new Wine release or a change to the
   app contract, and the patch version for fixes and rebuilds. Until 1.0, the
   app contract may change in a minor release; the release notes say how.

## Getting a base change to running apps

The Wine prefix is created when the image is built (`wine-webapp-init-prefix`),
so an app image only picks up a new base when it is rebuilt from it. App repositories should
rebuild on every new base release they accept, and on a schedule for
Debian updates, then roll containers one user at a time. Data lives in
volumes (`docs/app-contract.md`) and survives the swap.

## Provenance

Every image carries OCI labels: `org.opencontainers.image.version` and
`.revision` (this repository's commit), plus `io.github.naimor-oss.wine-webapp.wine-pkg`,
`.wine-fork` and `.wine-fork-ref`:

```sh
docker image inspect -f '{{json .Config.Labels}}' ghcr.io/naimor-oss/wine-webapp:vnc
```

## Smoke test

`tests/smoke.sh [IMAGE] [TITLE]` starts an app image and checks, through the
gateway, that the noVNC client is served and that a window with that title is
on the Wine desktop. CI runs it on `examples/notepad`; app repositories can run
it on their own image. It doesn't replace using the app from a browser.
