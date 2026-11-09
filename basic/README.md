# pspdev basic samples (unmodified)

Exact copies of the official examples from
<https://pspdev.github.io/basic_programs.html>, used as a known-good baseline
before touching the games.

| Directory | Official sample | What it tests |
|---|---|---|
| `hello/` | Hello world | `pspDebugScreen` text output |
| `shape/` | Drawing shapes | `sceGu` (red rectangle, 2D sprites) |
| `controls/` | Input | `sceCtrl` analog + buttons on the debug screen |

Sources:
<https://github.com/pspdev/pspdev.github.io/tree/master/_includes/samples>

No source changes were made. The only additions are this README and the build
commands below.

## Build

Each directory is a standalone CMake project:

```sh
export PSPDEV=$HOME/pspdev
export PATH=$PATH:$PSPDEV/bin

cd /path/to/psp-playground
for app in hello shape controls; do
    mkdir -p "build/basic-$app"
    ( cd "build/basic-$app" &&
      psp-cmake "../../basic/$app" -DCMAKE_BUILD_TYPE=Release &&
      make -j"$(nproc)" )
done
```

Outputs:

```text
build/basic-hello/EBOOT.PBP
build/basic-shape/EBOOT.PBP
build/basic-controls/EBOOT.PBP
```

## Test in PPSSPP (headless)

```sh
# Set PPSSPP_DIR to your PPSSPPHeadless build directory.
for app in hello shape controls; do
    d="/tmp/opencode/ppsspp-basic-$app"
    mkdir -p "$d"
    cp "build/basic-$app/EBOOT.PBP" "$d/"
    ( cd "$PPSSPP_DIR" &&
      ./PPSSPPHeadless "$d/EBOOT.PBP" --graphics=software --root="$d" \
        --timeout-emulated=2 --timeout-wall=60 --frame-stats \
        --screenshot-save="$d/screen.png" -l )
done
```

All three run at 60 fps emulated in PPSSPPHeadless and produce the expected
screens:

- `hello`: `Hello World!` at the top-left.
- `shape`: white background with a red rectangle.
- `controls`: `Analog X = 128, Analog Y = 128` (buttons appear when pressed).

## Install on a real PSP

```text
build/basic-hello/EBOOT.PBP     -> ms0:/PSP/GAME/HOMEBREW/BasicHello/EBOOT.PBP
build/basic-shape/EBOOT.PBP     -> ms0:/PSP/GAME/HOMEBREW/BasicShape/EBOOT.PBP
build/basic-controls/EBOOT.PBP  -> ms0:/PSP/GAME/HOMEBREW/BasicControls/EBOOT.PBP
```

Published to the Samba backup share as v0.1.0:

| App | SHA-256 |
|---|---|
| `basic-hello` | `868a183321ce45c5b390481be0d7c634285bf2d8842fafd8eb36011586ce9b82` |
| `basic-shape` | `6b8fac46f8e8744f43744b4eee7166f88f6e1edf6f69fa887fc48248a0dc715f` |
| `basic-controls` | `e3e2cc1ac052598ad7b6da24010abc0323e2a87ad4b15f3a4465319630045d4c` |

## Why these

`hello` and `controls` avoid `sceGu`; `shape` uses `sceGu` exactly as the
official sample does, including the same `char list[0x20000]
__attribute__((aligned(64)))` display list. If `shape` runs on the real PSP but
our Pong/Snake do not, the difference is in our code, not in `sceGu` itself.
If `hello` does not run, the problem is the loader/CFW/path, not the code.
