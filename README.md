# PSP Playground

A collection of PSP homebrew targets written in **C99** with the **PSPSDK**,
rendered directly with **sceGu**. No SDL, no engine — the only external assets
are the optional 144×80 PBP menu icons.

## Targets

| Target | What it is |
|---|---|
| **Snake** | Complete 2D game, solid-colour rectangles |
| **Cube 3D** | Rotating textured cube (`sceGu` + `pspgum`) |
| **Racing 3D** | Third-person endless driving game with selectable road maps, terrain sliders, crest jumps, HUD, minimap and settings persistence |
| **WiFi Connection** | Saved-profile picker, full link status, DNS/TCP/gateway/HTTP probes, netconf, reconnect supervisor, and a **DOWNLOADS** page (LAN catalog, resumable SHA-256-verified installs) |
| **WiFi Test** | Low-level connect/status diagnostic |
| **Memory Test** | Start-to-end user-RAM diagnostic with PASS/FAIL and first bad address |
| **Pong** | Minimal 2D game (GU load test) |
| **Screen** | Smallest `pspDebugScreen` app (no GU) |
| **HelloLog** | Standalone boot diagnostic with a log file |

All share a hand-coded 3×5 bitmap font. Snake, Cube 3D, Pong, Racing 3D and
WiFi Connection also carry 144×80 PBP menu icons.

## Screenshots

| Snake | Cube 3D | Racing 3D |
|---|---|---|
| ![Snake](docs/screenshots/snake.png) | ![Cube 3D](docs/screenshots/cube3d.png) | ![Racing 3D](docs/screenshots/racing.png) |

| WiFi Connection | Memory Test | Pong |
|---|---|---|
| ![WiFi Connection](docs/screenshots/wifi-downloads.png) | ![Memory Test](docs/screenshots/memtest.png) | ![Pong](docs/screenshots/pong.png) |

## Build

Requires the [PSPSDK](https://github.com/pspdev/pspdev) toolchain.

```sh
./scripts/test.sh          # host unit tests (C99, -Werror)
./scripts/build.sh         # psp-cmake + make (0 warnings)
./scripts/test-ppsspp.sh   # render check in PPSSPPHeadless
```

`build.sh` defaults to `PSP_MEMSIZE=1` (extended ~55 MiB partition on
PSP-2000/3000/Go/Street under ARK 5.1+). Use `PSP_MEMSIZE=0` for a
PSP-1000 / universal (24 MB) build.

Verify no `*ForKernel` imports leaked into the ELF:

```sh
/home/agent/pspdev/bin/psp-readelf -x .rodata.sceResident build/psp/psp_snake
```

## Install on a real PSP

Copy the built `EBOOT.PBP` to the Memory Stick:

```
ms0:/PSP/GAME/HOMEBREW/<App>/EBOOT.PBP
```

Requires custom firmware (ARK-5 or compatible) on PSP-2000/3000/Go/Street,
or PSP-1000 with a universal (MEMSIZE=0) build.

## Development

See [`docs/PSP-DEV-GUIDE.md`](docs/PSP-DEV-GUIDE.md) for the mandatory real-PSP
build rules (never link `pspkernel` in user mode, always use `sceGuGetMemory` /
D-cache writeback and 64-byte aligned display lists), the memory partition
(`MEMSIZE`) policy, and documented pitfalls.

Multi-agent build pipeline: [`docs/PIPELINE.md`](docs/PIPELINE.md).

## License

MIT — see [LICENSE](LICENSE).