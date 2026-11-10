#!/usr/bin/env python3
"""Print TITLE and MEMSIZE from the PARAM.SFO inside EBOOT.PBP files.

Usage:
    ./scripts/check-eboot-memsize.py [EBOOT.PBP ...]

With no arguments, checks every EBOOT.PBP under build/ (recursive).

MEMSIZE values (PARAM.SFO, dword):
    0 = 24 MB user partition (universal, PSP-1000)
    1 = extended partition (PSP-2000/3000/Go/Street; ~55 MiB under ARK 5.1+)
    2 = pspdev's Vita-friendly mode (ARK: +16 MiB, ~40 MiB total)
See the PSP memory-partition notes.
"""

import glob
import struct
import sys

PARTITION_MB = {0: "24 MB (universal)", 1: "extended (~55 MiB on 64 MB ARK)", 2: "Vita-friendly (+16 MiB on ARK)"}


def read_sfo_from_pbp(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"\0PBP":
        raise ValueError("not a PBP file")
    sfo_off = struct.unpack_from("<I", data, 8)[0]
    if data[sfo_off:sfo_off + 4] != b"\0PSF":
        raise ValueError("PBP has no PARAM.SFO")
    _, _version, key_off, data_off, count = struct.unpack_from("<4sIIII", data, sfo_off)
    out = {}
    for i in range(count):
        key_rel, fmt, length, _maxlen, data_rel = struct.unpack_from(
            "<HHIII", data, sfo_off + 20 + i * 16
        )
        key_start = sfo_off + key_off + key_rel
        key_end = data.index(b"\0", key_start)
        key = data[key_start:key_end].decode("ascii", "replace")
        raw = data[sfo_off + data_off + data_rel:sfo_off + data_off + data_rel + length]
        if fmt == 0x0404 and len(raw) >= 4:          # uint32
            out[key] = struct.unpack("<I", raw[:4])[0]
        elif fmt == 0x0204:                          # UTF-8 string
            out[key] = raw.rstrip(b"\0").decode("utf-8", "replace")
        else:                                        # 0x0004 = binary
            out[key] = raw
    return out


def main(argv):
    paths = argv[1:]
    if not paths:
        paths = sorted(glob.glob("build/**/EBOOT.PBP", recursive=True))
    if not paths:
        print("no EBOOT.PBP found; run ./scripts/build.sh first", file=sys.stderr)
        return 1
    failed = False
    for path in paths:
        try:
            sfo = read_sfo_from_pbp(path)
        except (OSError, ValueError) as exc:
            print(f"{path}: ERROR: {exc}", file=sys.stderr)
            failed = True
            continue
        memsize = sfo.get("MEMSIZE", "missing")
        desc = PARTITION_MB.get(memsize, "unknown")
        print(f"{path:42s} MEMSIZE={memsize!s:7s} {desc:38s} TITLE={sfo.get('TITLE', '?')!r} APP_VER={sfo.get('APP_VER', '?')!r} VERSION={sfo.get('DISC_VERSION', '?')!r}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
