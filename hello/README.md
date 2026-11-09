# HelloLog — PSP boot diagnostic

Minimal EBOOT used to find out whether the Snake/Cube 3D black screen is
caused by the loader, the runtime, or the games' GU code.

It does **not** touch GU, the heap (by default) or VFPU. It only:

1. Opens `ms0:/hello_elf.log` or `ms0:/hello_prx.log` and writes a header.
2. Initializes `pspDebugScreen` and prints the boot stages.
3. Installs the same exit callback used by the games.
4. Loops forever, printing a heartbeat every second and logging it every
   5 seconds.

Every log line is written and the file is closed immediately, so the last
line survives a hard freeze or a battery pull.

## Build

The directory is standalone (it is not added to the root `CMakeLists.txt`).

```sh
export PSPDEV=$HOME/pspdev
export PATH=$PATH:$PSPDEV/bin

# ELF build: same loader path as the current games.
mkdir -p build/psp-hello
cd build/psp-hello
psp-cmake ../../hello -DCMAKE_BUILD_TYPE=Debug
make -j"$(nproc)"

# PRX build: different loader path, and required for PSPLink.
mkdir -p ../psp-hello-prx
cd ../psp-hello-prx
psp-cmake ../../hello -DCMAKE_BUILD_TYPE=Debug -DBUILD_PRX=ON
make -j"$(nproc)"
```

Outputs:

```text
build/psp-hello/EBOOT.PBP
build/psp-hello-prx/EBOOT.PBP
```

## Install

Copy each EBOOT to its own folder on the Memory Stick:

```text
ms0:/PSP/GAME/HOMEBREW/HELLO/EBOOT.PBP     <- ELF build
ms0:/PSP/GAME/HOMEBREW/HELLOPRX/EBOOT.PBP  <- PRX build
```

## Read the log

After the PSP freezes (or after a few seconds if it works):

1. Power off the PSP (remove the battery if it is completely frozen).
2. Boot normally and use **Settings → USB Connection**.
3. Copy `hello_elf.log` or `hello_prx.log` from the root of the Memory Stick.

Delete the log before each test so runs do not mix.

## What each result means

| Observation | Interpretation |
|---|---|
| No log file, screen stays black | The EBOOT did not reach `main`; loader/CFW/packaging issue. Try the PRX build. |
| Log stops before `main entered` | Same as above. |
| `main entered`, but no text on screen | `pspDebugScreenInit` or display setup failed. |
| Log stops at `before setup_callbacks` | Freeze in callback/thread setup. |
| Reaches `entering loop` and screen shows `alive N` | HelloLog works; the problem is in the games' init/GU code. |
| Freezes after a few heartbeats | Something in the loop or the exit callback; less likely a loader issue. |

If the ELF build freezes but the PRX build works, the common cause is the
static ELF EBOOT path, not the game logic.

## Optional heap test

The games set `PSP_HEAP_SIZE_KB(1024)`. To check whether that is the common
factor, build HelloLog with the same heap:

```sh
psp-cmake ../../hello -DCMAKE_BUILD_TYPE=Debug -DHELLO_HEAP_KB=1024
```

The log name and screen variant stay the same; delete the old log first.
