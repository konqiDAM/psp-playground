# Development guide for real PSP and documented errors

This guide summarizes **how to create and build games in C99 + PSPSDK + `sceGu`**
that work on the first try on a **real PSP** (verified on PSP-2000 with CFW
ARK-5.2), and documents the real errors that caused black screens during
development.

---

## 1. Golden rules (mandatory template)

### 1.1 `CMakeLists.txt` — NEVER link `pspkernel` in user mode

In user-mode applications (`PSP_MODULE_INFO("...", 0, 1, 0)`), **`psp-gcc`
already links `-lpspuser` automatically** through its internal *specs*.

```cmake
cmake_minimum_required(VERSION 3.11)
project(my_game C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-O2 -G0 -Wall -Wextra)

add_executable(my_game
    src/main.c
)

# CORRECT: only user-subsystem libraries.
# NEVER add "pspkernel" here in a user EBOOT.
target_link_libraries(my_game PRIVATE
    pspdebug
    pspdisplay
    pspge
    pspgu
    pspctrl
)

create_pbp_file(
    TARGET mi_juego
    ICON_PATH NULL
    BACKGROUND_PATH NULL
    PREVIEW_PATH NULL
    TITLE "My Game"
    VERSION 01.00
)
```

- If you use 3D matrices (`sceGum*`), add `pspgum` and `m`.
- **Forbidden to put `pspkernel`** unless you are writing a kernel-mode PRX
  (`0x1000` + `USE_KERNEL_LIBS=1`).

### 1.2 Headers and startup macros (`main.c`)

Use `<pspuser.h>` (as in the official examples at
<https://pspdev.github.io/basic_programs.html>) and the standard main-thread
attributes:

```c
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspuser.h>

PSP_MODULE_INFO("MyGame", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
```

The SDK default heap works for simple apps, but our GU/heap-heavy games
explicitly use `PSP_HEAP_SIZE_KB(-1)` + `PSP_HEAP_THRESHOLD_SIZE_KB(4096)` to
claim the extended partition while keeping a 4 MiB system reserve (§1.4).
Use the default only for a minimal diagnostic with no large allocations.

### 1.3 Video memory, display list and GE cache (`sceGu`)

The graphics chip (**GE**) reads RAM directly, bypassing the CPU **D-cache**.
If the CPU writes vertices or textures into normal RAM (`.bss` / `.data` /
heap) and calls `sceGuDrawArray`, the GE will read stale or zero data on a real
PSP (even though it looks fine in PPSSPP).

Mandatory rules for `sceGu`:

1. **Display list aligned to 64 bytes:**
   ```c
   static uint32_t display_list[65536] __attribute__((aligned(64)));
   ```
2. **Vertices generated per frame → use `sceGuGetMemory()`:**
   `sceGuStart()` converts the display list pointer to the *uncached* mirror
   (`0x488xxxxx`). Allocating the frame vertices with `sceGuGetMemory()` writes
   directly into uncached memory:
   ```c
   size_t bytes = (size_t)vert_count * sizeof(Vertex);
   Vertex *gu_verts = (Vertex *)sceGuGetMemory((int)bytes);
   if (gu_verts) {
       memcpy(gu_verts, verts, bytes);
       sceGuDrawArray(GU_SPRITES,
                      GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
                      vert_count, 0, gu_verts);
   }
   ```
3. **Textures or static geometry in RAM → flush D-cache before drawing:**
   If you generate or modify a texture or vertex table in `.bss`/heap, align
   the buffer to 16 bytes and write back to RAM before the GE uses it:
   ```c
   sceKernelDcacheWritebackRange(buffer, sizeof(buffer));
   ```

### 1.4 Memory partition (`MEMSIZE`) and heap

The PSP-1000 has **32 MB**; the PSP-2000/3000/Go/Street have **64 MB**. The
extra 32 MB is not given to an application automatically: the EBOOT's
`PARAM.SFO` contains a `MEMSIZE` dword, and the loader/CFW decides how much
of the user partition to expose.

| `MEMSIZE` | pspdev intent | PPSSPP | ARK 5.1+ on a 64 MB PSP (extra RAM on) | When to use |
|---|---|---|---|---|
| `0` | opt out | 24 MB | no merge, 24 MB user partition | PSP-1000 / universal build |
| `1` | all available | 56 MB partition | **Force MAX: ~55 MiB user RAM** | our default (PSP-2000) |
| `2` | Vita compatibility | 24 MB (no expansion) | Force +16 MiB: ~40 MiB user RAM | Vita-friendly compromise |

Verified against the pspdev/pspsdk sources, PPSSPP and the ARK 5.1 release
notes (links at the end of this section). In particular:

- pspdev's `create_pbp_file` has defaulted to `MEMSIZE=2` since v20260801.
  That is why an older EBOOT of ours showed `2`: we never passed the value.
- `MEMSIZE=0` could not be set from CMake until pspsdk PR #392 (merged
  2026-09-22, included in our v20261001 SDK).
- ARK 5.1+ interprets `1` as Force MAX and `2` as Force +16 MiB, unless the
  CFW's "extra RAM" option is Off.
- PPSSPP only expands memory for `MEMSIZE=1`, and ignores it in PSP-1000
  model mode.
- Modern CFW guards PSP-1000 against expanding the partition, so a
  `MEMSIZE=1` build normally still runs there with 24 MB — but for anything
  public or guaranteed, build a `0` variant.

**Our policy.** The root CMake defines a cache variable defaulting to `1`:

```cmake
set(PSP_MEMSIZE 1 CACHE STRING "PARAM.SFO MEMSIZE: 0=24MB universal, 1=extended (2000+), 2=Vita-friendly")
...
create_pbp_file(TARGET psp_snake ... MEMSIZE ${PSP_MEMSIZE})
```

Build commands:

```sh
./scripts/build.sh                  # default: MEMSIZE=1 (PSP-2000/3000)
PSP_MEMSIZE=0 ./scripts/build.sh    # PSP-1000 / universal build
./scripts/check-eboot-memsize.py    # verify what the EBOOT actually asks for
```

`scripts/check-eboot-memsize.py` parses the PBP's `PARAM.SFO` and prints
`MEMSIZE` plus the expected partition; run it after every packaging change.

**Heap.** Requesting the larger partition is only half the job; the program
must also claim it. Our games use:

```c
PSP_HEAP_SIZE_KB(-1);              /* all remaining user memory ... */
PSP_HEAP_THRESHOLD_SIZE_KB(4096);  /* ... minus 4 MiB kept for the system */
```

Why not `PSP_HEAP_SIZE_KB(-1024)`? In pspsdk's `_sbrk`, a negative
`sce_newlib_heap_kb_size` ignores its magnitude and falls back to
`sceKernelMaxFreeMemSize() - threshold`; the default threshold is **512 KiB**.
So `-1024` leaves 512 KiB, not 1 MiB. `-1` is the canonical "all remaining"
spelling, and the threshold is the only thing that controls the reserve.

Expected heap with a 4 MiB reserve:

| Build | Partition | Heap available |
|---|---:|---:|
| `MEMSIZE=1`, PSP-2000/ARK extra RAM on | ~55 MiB | ~51 MiB |
| `MEMSIZE=2`, PSP-2000/ARK extra RAM on | ~40 MiB | ~36 MiB |
| `MEMSIZE=2`, PPSSPP | 24 MB | ~20 MiB |
| `MEMSIZE=0`, any model | 24 MB | ~20 MiB |

At runtime, `sceKernelTotalFreeMemSize()` and `sceKernelMaxFreeMemSize()`
report the partition the loader actually granted; print them once at boot if
you need to confirm a build on hardware. (The SFO says what was requested;
these say what was received.)

The 4 MiB reserve matters when using `sceUtilitySavedata`: its utility
modules and thread stacks load into the user partition. A game with no
savedata can use the default 512 KiB, but 4 MiB is the safe default.

**Exceptions in this repo:** `screen/` deliberately keeps a 1 MiB heap (it
is the minimal `pspDebugScreen` diagnostic); `hello/` keeps its optional
`HELLO_HEAP_KB` test; `basic/` are the official unmodified pspdev samples
and are not repackaged by our CMake.

References:
- pspdev `CreatePBP.cmake`: <https://github.com/pspdev/pspsdk/blob/master/src/base/CreatePBP.cmake>
- pspsdk `_sbrk` (heap/threshold logic): <https://github.com/pspdev/pspsdk/blob/master/src/libcglue/glue.c>
- pspsdk PR #392 (`MEMSIZE=0` fix): <https://github.com/pspdev/pspsdk/pull/392>
- PPSSPP `UseLargeMem`: <https://github.com/hrydgard/ppsspp/blob/master/Core/PSPLoaders.cpp>
- ARK 5.1 extra-RAM options: <https://github.com/PSP-Arkfive/FasterARK/releases/tag/ARK-5.1.0>

### 1.5 Wi-Fi

Use the reusable module in `net/wifi.c` / `net/wifi.h` and the new
`net/net_sock.c`, `net/net_probe.c` helpers. The full app is `wifi/`
(“WiFi Connection 0.1.0”).

Rules:

- **Never link `pspkernel`.** `sceNet*`, `sceWlan*` and `sceUtility*` are user
  libraries; `psp-gcc` adds `-lpspuser` automatically.
- Load `PSP_NET_MODULE_COMMON` + `INET` before `sceNetInit`; treat
  `0x80110F01` / `0x80110802` / `0x80410201` / `0x80410701` / `0x80410B01` as
  "already ready", not as errors. Track **ready vs owned** and never Term or
  unload a stack another component owns.
- Enumerate saved profiles **1..16** with `sceUtilityCheckNetParam`; slot 0 is
  special and never used. Connect to the selected slot, then poll
  `sceNetApctlGetState()` until `STATE_GOT_IP` (4).
- **Disconnect is asynchronous**: wait for state 0, settle ~750 ms, then
  connect again. APCTL may need one `Term`/`Init` retry, only when owned.
- `SceNetApctlInfo` is a union: one `GetInfo` call per field. Never read
  `ssidLength` from an SSID reply.
- Use **`SceNetInetTimeval` (8 bytes)**, not newlib's 16-byte `struct timeval`,
  for `sceNetInetSelect` and socket timeouts. Check `SO_ERROR` after a
  nonblocking connect.
- Resolver `Delete`/`Term` before `sceNetInetTerm`; the probe worker owns it.
- Disable WLAN power-save while the app is running and restore it in
  `wifi_shutdown()` or an explicit `wifi_power_save_restore()`.
  `wifi_disconnect()` does **not** restore it.
- Netconf (`sceUtilityNetconf`) needs a GU frame pump and utility thread
  priorities 40. Tear GU down, re-init `pspDebugScreen`, and do **two full
  text repaints**; the text/GU transition is a hardware smoke-test item.
- Build/test: `build/psp/wifi/EBOOT.PBP` (WiFi Connection) and
  `build/psp/wifi-test/EBOOT.PBP` (diagnostic). PPSSPP fakes APCTL and proxies
  sockets; only a real PSP validates the driver.

Download rules (WiFi Connection 0.2.0 DOWNLOADS):

- **Plain HTTP only**; no HTTPS/TLS, curl or Mbed TLS. One request per
  connection, no redirects, no chunked/gzip, no arbitrary URLs.
- **Portable SHA-256** (`net/checksum.c`); the SDK has no SHA-256. Verify the
  complete `.part` before the rename, never trust a partial file.
- **FAT rename does not replace.** Use `.part` → `.new` → remove target →
  rename, sync the device, and recover an interrupted `.new` at startup.
- **No open handles across suspend.** A suspend may invalidate Memory Stick
  handles. The engine checks the flag before each transport read, before
  opening/appending the part, before each checkpoint write+sync, before each
  verify chunk, and around every install/recovery remove or rename. An
  already-started card syscall may finish; the install/recovery path starts
  no new write/sync/rename after the flag and keeps `.part`/`.resume`/`.new`
  for recovery. After a recovery install, recheck the stop hook before
  returning success and before writing the registry. Error-path cleanup may
  still discard a file that failed validation. Never Term the network stack
  inside the power callback.
- Check **8 MiB free space** + remaining bytes; `ENOSPC` is still fatal.
- **0.2.0 assumes the Memory Stick is `ms0:`.** Paths in the catalog/registry
  and `sceIoDevctl`/`sceIoSync` calls hard-code that device. A future revision
  should derive the device root from `argv[0]` so a PSP Go `ef0:` install
  works.
- Keep one worker (priority `0x30`, 32 KiB) for probes and downloads; the main
  thread owns APCTL and rejoin. Record `worker stack: N B free` on hardware.
- The download socket is explicitly nonblocking after connect; reads use
  bounded `MSG_DONTWAIT` polling (the PPSSPP build crashes in `select` on the
  download thread). Never rely on a blocking `recv` and never hold a Memory
  Stick handle across a suspend.
- While downloading: `scePowerTick` every 5 s, 60 s backlight with restore,
  333 MHz with restore; restore all three on every exit path.

Full API, state machine and pitfalls: [`docs/WIFI.md`](WIFI.md).

### 1.6 App icons (`ICON0.PNG`)

The XMB menu icon is embedded in `EBOOT.PBP`, not copied separately. Use a
**144 × 80** PNG (8-bit RGB or RGBA). The pspdev `CreatePBP.cmake` comment
says “144x82”, but the PSP `ICON0.PNG` format is 144×80.

```cmake
create_pbp_file(
    TARGET psp_racing
    ICON_PATH ${CMAKE_SOURCE_DIR}/assets/icon_racing.png
    BACKGROUND_PATH NULL
    PREVIEW_PATH NULL
    TITLE "Racing 3D"
    VERSION ${PSP_VERSION}
    MEMSIZE ${PSP_MEMSIZE}
)
```

Rules:

- Keep the PNG in `assets/icon_<app>.png` and commit it with the CMake
  change; a fresh clone without `assets/` cannot configure or package.
- Generate or update icons with `python3 scripts/gen_icons.py` (pure Python,
  no Pillow). Edit the drawing functions there and re-run.
- Leave `ICON_PATH NULL` if an app intentionally has no icon.
- After packaging, verify that PBP section 1 starts with the PNG signature;
  the EBOOT grows by roughly the PNG size. A snippet and the full workflow
  are in [`docs/ICONS.md`](ICONS.md).

### 1.7 Test-first implementation (TDD)

Every new behavior starts with a test that is written first and watched
failing. This is the Iron Law, not a suggestion. The pipeline builder loads
`test-driven-development` and records the **RED** failure and **GREEN** pass
for every task.

| Code type | Test-first artifact | RED evidence | GREEN evidence |
|---|---|---|---|
| Portable C logic | host test in `tests/` or `host_test.c` | test fails/compile error for the missing API | `./scripts/test.sh` passes |
| GU/render/UI | headless probe with pixel/behavior assertions | probe fails on the old EBOOT | probe + suite pass |
| Hardware-only | documented manual probe/checklist | wrong behavior recorded on the PSP | checklist passes |
| Packaging/integration | `build.sh`, `psp-readelf`, `check-eboot-memsize.py` | the new assertion/gate fails before the fix | all gates pass |

Rules:

- Use host tests for portable logic, headless probes for GU/UI, and manual
  checklists only for hardware-only behavior.
- A “non-blank screenshot” is not a behavior test; the probe must assert the
  changed behavior (pixel, text, speed, position, state).
- Hardware-only work needs a recorded red observation; never claim TDD from a
  manual “it looks right” after the change.
- If a task has no testable behavior, it is not a task; stop and report.
- Run the full `./scripts/test.sh` after each task, not just the new test.
- Reviewers verify **red proof** on the base commit or a targeted revert, and
  use `requesting-code-review`; the fix loop uses `receiving-code-review`.

## 2. Step-by-step procedure for each change or new game

For a non-trivial change, run the **`psp-build-pipeline`** skill
([`docs/PIPELINE.md`](PIPELINE.md)); it wraps these steps with a worktree,
plan gate, independent review and integration.

1. **Host logic tests** (if the game separates logic from rendering):
   ```sh
   ./scripts/test.sh
   ```
2. **Build with `pspdev` (0 warnings):**
   ```sh
   ./scripts/build.sh
   ```
3. **Verify that the ELF does NOT import `*ForKernel` libraries (mandatory):**
   ```sh
   psp-readelf -x .rodata.sceResident build/psp/psp_snake
psp-readelf -x .rodata.sceResident build/psp/wifi/psp_wifi_app
   ```
   The text output must contain **only** user libraries (`ThreadManForUser`,
   `IoFileMgrForUser`, `LoadExecForUser`, `SysMemUserForUser`, `StdioForUser`,
   `ModuleMgrForUser`, `Kernel_Library`, `sceDisplay`, `sceGe_user`, `sceCtrl`,
   `sceUtility`).
   **If any `*ForKernel` appears, the EBOOT will NOT boot on a real PSP.**
4. **Test rendering in `PPSSPPHeadless`:**
   ```sh
   ./scripts/test-ppsspp.sh
   ```
5. **Add or verify the menu icon (new/changed app):**
   - Put `assets/icon_<app>.png` (144×80) and set `ICON_PATH` as in §1.6.
   - Confirm the PNG is embedded in PBP section 1 and commit the asset.
6. **Publish the EBOOT** (optional, for your own backup share):
   ```sh
   PSP_APP=snake ./scripts/publish-eboot.sh build/psp/EBOOT.PBP <version> "<changes>"
   ```
   See `scripts/publish-eboot.sh` for the full layout and batch mode.

---

## 3. Errors that occurred and documented (post-mortem)

### Error 1 (Critical): `-lpspkernel` in `target_link_libraries` → the EBOOT dies in `_start` before `main()`

- **Symptom on real PSP (PSP-2000, ARK-5.2):**
  None of our binaries (`snake 0.1.0/0.1.1`, `cube3d 0.1.0-0.1.2`, `pong`,
  `screen`, not even `hello` / `hello-prx`) ran `main()`. `hello` did not even
  reach creating the file `ms0:/hello_elf.log` on the first line of `main()`.
  Inspecting memory over USB (`pspsh`), the stubs for `sceKernelCreateThread`
  and `sceKernelStartThread` contained `0x0000054C` (`syscall 0x15`).
- **Root cause:**
  In all our `CMakeLists.txt` we had put `pspkernel` inside
  `target_link_libraries(...)`. In PSPSDK (`build.mak`), `libpspkernel.a` is
  only for kernel modules (`USE_KERNEL_LIBS=1`). Because CMake passes
  `-lpspkernel` to the linker **before** the automatic `-lpspuser` from
  `psp-gcc`, the linker resolved the system symbols against the `*ForKernel`
  libraries (`ThreadManForKernel`, `IoFileMgrForKernel`, `LoadExecForKernel`,
  `UtilsForKernel`, `InterruptManagerForKernel`) instead of `*ForUser`.
  On a real PSP, `sceLoaderCore` forbids a user module (`attr = 0`) from linking
  against `*ForKernel` libraries and patches those stubs with `syscall 0x15`
  (invalid stub). When `_start` (in `crt0`) tries to create the `user_main`
  thread by calling `sceKernelCreateThread`, it jumps to `syscall 0x15`, fails,
  and ends `_start` without having created the `user_main` thread. That is why
  `main()` never ran.
- **Why it was not seen earlier:**
  The HLE loader in PPSSPP resolves NIDs without checking whether the table
  asks for `ThreadManForKernel` or `ThreadManForUser`. In the emulator everything
  ran at 60 fps.
- **How it was discovered:**
  Compiling the 3 official examples from
  <https://pspdev.github.io/basic_programs.html> (`basic-hello`, `basic-shape`,
  `basic-controls`) without changes, all 3 worked on the real PSP on the first
  try. Comparing their `.lib.stub` sections with ours, the official ones only
  imported `*ForUser` because their `CMakeLists.txt` did not include `pspkernel`.
- **Solution:**
  Remove `pspkernel` from `target_link_libraries(...)` and use `<pspuser.h>`.
  Applied in **Snake v0.1.2** and confirmed working on the real PSP.

### Error 2: Vertices in cached RAM without `sceGuGetMemory` or D-cache writeback

- **Potential symptom on real hardware:**
  Graphics corruption or invisible geometry when drawing with `sceGuDrawArray`.
- **Root cause:**
  `src/render.c` wrote the per-frame vertices from the CPU into a static array
  `static Vertex verts[MAX_VERTS]` in `.bss` (cached RAM `0x088xxxxx`) and
  passed that pointer directly to `sceGuDrawArray` without flushing the D-cache
  or using display-list memory.
- **Solution:**
  Reserve the frame vertex block inside the display list with `sceGuGetMemory()`
  (which returns memory in the uncached segment `0x488xxxxx`), just like the
  official `basic-shape` example.

### Error 3: Display list without explicit alignment

- **Root cause:**
  `sceGuStart()` requires a pointer aligned to at least 16 bytes. Declaring
  `static uint32_t display_list[65536];` without an alignment attribute left it
  at an address `...a8` (8 mod 16).
- **Solution:**
  Always declare `__attribute__((aligned(64)))` on the display list (just like
  `char list[0x20000] __attribute__((aligned(64)))` in `basic-shape`).

### Error 4: Wasting time on blind hypotheses and complex USB debugging before trying the official examples

- **What went wrong:**
  Before trying the basic official examples from `pspdev.github.io`, secondary
  hypotheses were tested (`PSP_HEAP_SIZE_KB(-1)`, `MEMSIZE`, PRX with
  GDB/PSPLink) that did not address the root cause, and even the diagnostic
  test itself (`hello`) had the same bug (`-lpspkernel`) as the games.
- **Rule from now on:**
  The 3 official examples (`basic-hello`, `basic-shape`, `basic-controls`) are
  the baseline verified on hardware. Any new game must follow exactly their
  CMake structure, headers and imports.

### Error 5: a large flat ground plane beating the road in the 16-bit depth test

- **Symptom (Racing 3D 0.2.0):**
  Hard-edged patches/wedges of grass appear *inside* the asphalt, at 10–120 m,
  on some curves; they come and go with the chase camera. Not a road-mesh hole:
  disabling the ground shows clean asphalt at the same pixels.
- **Root cause:**
  The grass is a large camera-forward fan at `y = -0.05`, drawn before the road
  (`y = 0`). The GE's depth is a 16-bit fixed-point plane interpolated across
  each triangle; on those huge, shallow, near-degenerate fan quads the
  interpolated depth can be *closer* than the road's true depth by more than
  the 5 cm separation. `GU_GEQUAL` then lets the ground win in wedges that
  follow a fan triangle. PPSSPP models the GE's depth plane, so the artefact
  reproduces in the emulator.
- **Solution:**
  Draw the ground as a **background layer**: `sceGuDisable(GU_DEPTH_TEST)` +
  `sceGuDepthMask(GU_TRUE)` before `draw_ground()`, restore both immediately
  after the call (outside the function, so an allocation-failure early return
  cannot skip the restore). The ground never writes depth and can never occlude
  the road, shoulders, posts or car — no Y-offset margin and no 2D mask.
- **Detection:**
  `scripts/check-racing-road.py` captures 29 frames and fails if any ground
  pixel is enclosed by asphalt; `-DRACING_DIAG_GROUND=ON` paints the ground
  magenta for a stricter probe.

### Error 6: a worn analog nub drives the car by itself

- **Symptom (Racing 3D 0.2.1):**
  With no hands on the controls the car slowly steers to one side. A PSP-2000
  nub can rest at `Lx = 140` instead of 128; the old code read `Lx` every frame
  and used a fixed centre/deadzone, so the resting offset became a permanent
  steer.
- **Rule:**
  Never make the raw nub the default steering source. Racing 3D 0.3.0 uses the
  D-pad by default and only reads `Lx` in the opt-in analog mode, after a
  captured centre and a configurable deadzone (8…56). The calibration screen
  shows the raw `Lx`/`Ly` so drift is diagnosable.
- **Persistence:**
  Store the calibration/settings in a small CRC-checked file
  (`ms0:/PSP/SAVEDATA/RACING3D/RACING.CFG`) and treat a missing, short,
  bit-flipped or unknown-flag file as defaults. Never let a failed save or a
  missing Memory Stick block the game; keep the in-RAM settings and show a
  non-fatal notice.
