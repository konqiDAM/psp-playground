#!/usr/bin/env bash
# Build the PSP EBOOT. Requires pspdev (PSPDEV, default: ~/pspdev).
set -euo pipefail
cd "$(dirname "$0")/.."

export PSPDEV="${PSPDEV:-$HOME/pspdev}"
export PATH="$PATH:$PSPDEV/bin"

# 1 = extended memory (PSP-2000/3000/Go/Street, ~55 MiB under ARK 5.1+),
# 0 = 24 MB universal (PSP-1000), 2 = pspdev's Vita-friendly +16 MiB mode.
# See docs/PSP-DEV-GUIDE.md section 1.4.
export PSP_MEMSIZE="${PSP_MEMSIZE:-1}"

rm -rf build/psp
mkdir -p build/psp
cd build/psp

psp-cmake ../.. -DCMAKE_BUILD_TYPE=Release -DPSP_MEMSIZE="$PSP_MEMSIZE"
make -j"$(nproc)"

echo
ls -l EBOOT.PBP cube3d/EBOOT.PBP racing/EBOOT.PBP memtest/EBOOT.PBP \
      screen/EBOOT.PBP pong/EBOOT.PBP wifi-test/EBOOT.PBP wifi/EBOOT.PBP
