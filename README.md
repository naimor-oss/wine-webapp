# wine-webapp

Container images that run one Windows desktop application under Wine and serve
it to a web browser, one container per user. Built for older 32-bit
line-of-business software (first user: a Clarion/TopSpeed application), where
the alternative is keeping a Windows machine or terminal server around just to
run it. The images pin a specific Wine release and carry a small set of Wine
fixes we need but upstream hasn't shipped yet.

## Where do I start?

| If you want to … | Read |
| --- | --- |
| Package an application | [`docs/app-contract.md`](docs/app-contract.md) |
| Choose or add a browser front end | [`docs/frontends.md`](docs/frontends.md) |
| Know what we change in Wine, and why | [`docs/wine-patches.md`](docs/wine-patches.md) |
| Check file locking across containers on a host | [`tools/locktest/`](tools/locktest/) |
| Find or raise a window hidden behind others | The "Windows" button in the browser ([`docs/frontends.md`](docs/frontends.md)); [`tools/winlist/`](tools/winlist/) underneath |
| Work on this repo as an agent | [`AGENTS.md`](AGENTS.md) |

## Quick start

```sh
docker build --target vnc -t wine-webapp:vnc .
docker build -t wine-webapp-example-notepad examples/notepad
docker run --rm -p 8080:8080 wine-webapp-example-notepad
# open http://localhost:8080
```

Building needs network access to WineHQ's package repository and to our Wine
fork, [`Naimor-OSS/wine`](https://github.com/Naimor-OSS/wine). To build from a
local checkout of the fork instead, add
`--build-context winesrc=../wine`.

The images are `linux/amd64` and need an x86-64 Docker host. On Apple Silicon
the build fails at the first Wine start (`wine-webapp-init-prefix`), seen
2026-10-05 with Wine 11.0: OrbStack 2.2.3 runs Wine under QEMU, which aborts
(`anon_mmap_fixed: Assertion ... host_page_mask` failed); Apple's `container`
1.4.1 runs it under Rosetta, which aborts in `rt_sigreturn`
(`x86_avx_state_ptr->xsave_header.xfeatures`). Everything else in the image
(Xvnc, the gateway) runs there.

## Related repositories

- [`Naimor-OSS/wine`](https://github.com/Naimor-OSS/wine): our Wine fork, one
  `naimor/wine-<version>` branch per Wine release we ship on.
- [`Naimor-OSS/dev-commons`](https://github.com/Naimor-OSS/dev-commons):
  shared conventions (`STYLE.md`) followed here.

## Repository map

| Path | Purpose |
| --- | --- |
| `Dockerfile` | Targets `winedlls` (patched Wine DLLs), `tools` (helpers under Wine), `base` (Wine + app contract), `vnc` (front end) |
| `build/build-wine-dlls.sh` | Builds only the patched 32-bit Wine modules from the fork |
| `rootfs-base/` | App contract scripts and default prefix hooks |
| `rootfs-vnc/` | TigerVNC + noVNC front end, gateway with the window list |
| `examples/notepad/` | Smallest app image; also the smoke test |
| `tools/locktest/` | Cross-container Windows file-lock test |
| `tools/winlist/` | List / raise windows on the Wine desktop; built into the image |
| `docs/` | Contract, front ends, Wine patches |

## Status

Early. The `vnc` front end has been tested with one Clarion application, with
up to two users at a time. Not yet published to a container registry.

## License

MIT, see [`LICENSE`](LICENSE). Wine itself is LGPL-2.1-or-later; the images
contain WineHQ's binary packages plus DLLs built from our fork, whose source is
public in `Naimor-OSS/wine`.
