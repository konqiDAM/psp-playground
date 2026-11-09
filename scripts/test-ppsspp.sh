#!/usr/bin/env bash
# Run both EBOOTs in PPSSPPHeadless (software GPU, no display) and check the
# screenshots are not blank.
#
#   ./scripts/test-ppsspp.sh [output_dir]
#
# PPSSPP_HEADLESS can point to the binary; default:
#   $HOME/ppsspp/build/PPSSPPHeadless  (or set PPSSPP_HEADLESS)
#
# Emulated FPS is NOT PSP hardware performance.
set -euo pipefail
cd "$(dirname "$0")/.."

PPSSPP="${PPSSPP_HEADLESS:-$HOME/ppsspp/build/PPSSPPHeadless}"
OUT="${1:-build/ppsspp}"

if [[ ! -x "$PPSSPP" ]]; then
    echo "error: PPSSPPHeadless not found at $PPSSPP" >&2
    echo "set PPSSPP_HEADLESS or see docs/HEADLESS-TESTING.md" >&2
    exit 1
fi

mkdir -p "$OUT"

check_png() {
    python3 - "$1" "${2:-5.0}" "${3:-4}" <<'PY'
import struct, sys, zlib

path = sys.argv[1]
data = open(path, 'rb').read()
assert data[:8] == b'\x89PNG\r\n\x1a\n'
pos, idat, w, h, ct = 8, b'', 0, 0, 0
while pos < len(data):
    ln, typ = struct.unpack('>I4s', data[pos:pos + 8])
    body = data[pos + 8:pos + 8 + ln]
    pos += 12 + ln
    if typ == b'IHDR':
        w, h, bd, ct = struct.unpack('>IIBB', body[:10])
    elif typ == b'IDAT':
        idat += body
raw = zlib.decompress(idat)
bpp = 4 if ct == 6 else 3
stride = w * bpp
prev = bytearray(stride)
nonblack = 0
colors = set()
for y in range(h):
    f = raw[y * (stride + 1)]
    line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
    for i in range(stride):
        a = line[i - bpp] if i >= bpp else 0
        b = prev[i]
        c = prev[i - bpp] if i >= bpp else 0
        if f == 1:
            line[i] = (line[i] + a) & 255
        elif f == 2:
            line[i] = (line[i] + b) & 255
        elif f == 3:
            line[i] = (line[i] + (a + b) // 2) & 255
        elif f == 4:
            p = a + b - c
            pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
            line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
    for x in range(0, stride, bpp):
        r, g, bb = line[x], line[x + 1], line[x + 2]
        if r or g or bb:
            nonblack += 1
        colors.add((r >> 4, g >> 4, bb >> 4))
    prev = line
pct = 100.0 * nonblack / (w * h)
min_pct = float(sys.argv[2])
min_colors = int(sys.argv[3])
print(f"  screenshot {w}x{h}: non-black {pct:.1f}%, colours(4bit) {len(colors)}")
sys.exit(0 if pct > min_pct and len(colors) >= min_colors else 1)
PY
}

run_app() {
    local app="$1" eboot="$2" secs="$3" label="$4"
    local min_pct="${5:-5.0}" min_colors="${6:-4}" press="${7:-}"
    local dir eboot_abs shot
    dir="$(realpath -m "$OUT/$app")"
    mkdir -p "$dir"
    eboot_abs="$(realpath "$eboot")"
    cp "$eboot_abs" "$dir/EBOOT.PBP"
    shot="$dir/screen.png"

    echo "== $label =="
    (
        cd "$(dirname "$PPSSPP")"
        cmd=(nice -n 10 "$PPSSPP" "$dir/EBOOT.PBP"
             --graphics=software
             --root="$dir"
             --timeout-emulated="$secs"
             --timeout-wall="$((secs * 6 + 30))"
             --frame-stats
             --screenshot-save="$shot"
             -l)
        if [[ -n "$press" ]]; then
            cmd+=(--press="$press")
        fi
        "${cmd[@]}"
    ) > "$dir/run.log" 2>&1

    grep -E 'Frame stats|Screenshot saved' "$dir/run.log" | tail -2 || true
    check_png "$shot" "$min_pct" "$min_colors"
    echo "  log: $dir/run.log"
}

run_app screen build/psp/screen/EBOOT.PBP 2 "Screen Test" 0.5 2
run_app pong build/psp/pong/EBOOT.PBP 4 "Pong"
run_app snake build/psp/EBOOT.PBP 2 "Snake"
run_app cube3d build/psp/cube3d/EBOOT.PBP 6 "Cube 3D"
run_app memtest build/psp/memtest/EBOOT.PBP 20 "Memory Test" 5.0 4
run_app racing build/psp/racing/EBOOT.PBP 8 "Racing 3D" 5.0 4 "0.2:0x4000:0.2"
run_app wifi build/psp/wifi/EBOOT.PBP 8 "WiFi Connection" 1.0 2

echo "PASS"
