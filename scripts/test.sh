#!/usr/bin/env bash
# Host tests for the portable game logic. No PSP SDK required.
set -euo pipefail
cd "$(dirname "$0")/.."

mkdir -p build/host
cc -std=c99 -Wall -Wextra -Werror -Isrc \
   tests/test_game.c src/game.c -o build/host/test_game
./build/host/test_game

cc -std=c99 -Wall -Wextra -Werror -Imemtest \
   memtest/host_test.c memtest/memtest.c -o build/host/test_memtest
./build/host/test_memtest

cc -std=c99 -Wall -Wextra -Werror -Iracing \
   tests/test_racing.c racing/physics.c racing/road.c -lm \
   -o build/host/test_racing
./build/host/test_racing

cc -std=c99 -Wall -Wextra -Werror -Inet \
   tests/test_checksum.c net/checksum.c -o build/host/test_checksum
./build/host/test_checksum

cc -std=c99 -Wall -Wextra -Werror -Inet \
   tests/test_http_logic.c net/http_logic.c -o build/host/test_http_logic
./build/host/test_http_logic

cc -std=c99 -Wall -Wextra -Werror -Inet -Iwifi \
   tests/test_catalog.c wifi/catalog.c net/http_logic.c -o build/host/test_catalog
./build/host/test_catalog

cc -std=c99 -Wall -Wextra -Werror -Iwifi \
   tests/test_install_state.c wifi/install_state.c -o build/host/test_install_state
./build/host/test_install_state

cc -std=c99 -Wall -Wextra -Werror -Inet -Iwifi \
   tests/test_download_state.c wifi/download_state.c net/checksum.c \
   -o build/host/test_download_state
./build/host/test_download_state

cc -std=c99 -Wall -Wextra -Werror -Iwifi \
   tests/test_download_policy.c wifi/download_policy.c -o build/host/test_download_policy
./build/host/test_download_policy

cc -std=c99 -Wall -Wextra -Werror -Inet -Iwifi \
   tests/test_download.c wifi/download.c wifi/download_state.c \
   wifi/download_policy.c net/checksum.c -o build/host/test_download
./build/host/test_download

cc -std=c99 -Wall -Wextra -Werror -Inet \
   tests/test_wifi_logic.c net/wifi_logic.c -o build/host/test_wifi_logic
./build/host/test_wifi_logic

cc -std=c99 -Wall -Wextra -Werror -Inet \
   tests/test_net_probe.c net/net_probe_logic.c -o build/host/test_net_probe
./build/host/test_net_probe

cc -std=c99 -Wall -Wextra -Werror -Iwifi \
   tests/test_wifi_app.c wifi/app.c -o build/host/test_wifi_app
./build/host/test_wifi_app

cc -std=c99 -Wall -Wextra -Werror -Iracing \
   tests/test_racing_score.c racing/score.c racing/hiscore.c \
   racing/settings.c racing/road.c -lm \
   -o build/host/test_racing_score
./build/host/test_racing_score

cc -std=c99 -Wall -Wextra -Werror -Iracing \
   tests/test_racing_ui.c racing/settings.c racing/input.c racing/telemetry.c racing/minimap.c racing/menu.c racing/road.c racing/welcome.c racing/hiscore.c -lm \
   -o build/host/test_racing_ui
./build/host/test_racing_ui

cc -std=c99 -Wall -Wextra -Werror -Iracing \
   tests/test_racing_signs.c racing/assets/roadsigns/signs.c -lm \
   -o build/host/test_racing_signs
./build/host/test_racing_signs

# racing/storage.c is PSP-only I/O, so it is tested with a TEST-ONLY shim
# (tests/host/psp/) that maps ms0: onto a temporary directory and injects the
# failures a memory stick produces. -Itests/host is for "psp/psp_stub.h"; the
# stub's own headers come from -Itests/host/psp.
cc -std=c99 -Wall -Wextra -Werror -Iracing -Itests/host -Itests/host/psp \
   tests/test_racing_storage.c racing/storage.c racing/settings.c \
   racing/hiscore.c tests/host/psp/psp_stub.c \
   -o build/host/test_racing_storage
./build/host/test_racing_storage

cc -std=c99 -Wall -Wextra -Werror -Iracing \
   tests/test_racing_rail.c racing/assets/guardrail/guardrail.c -lm \
   -o build/host/test_racing_rail
./build/host/test_racing_rail



# 0.9.0 E1: only TERRAIN may disqualify a run. The predicate's signature is the
# guarantee, so it is checked, not assumed - and checked on COMMENT-STRIPPED
# text, because prose must never satisfy a gate (the 0.8.0 decal checker's
# standard). The stripper is inlined rather than imported from
# scripts/check-racing-decal-order.py: that file is private (absent from
# scripts/public-manifest.txt) and public/scripts/test.sh runs this same gate.
python3 - <<'E1_GATE'
import re


def strip_comments(text):
    """Blank /* */ and // comments. Quote-aware: a // or /* inside a string
    literal is code, and a quote inside a comment must not open a string."""
    out = []
    i, n = 0, len(text)
    quote = None
    while i < n:
        ch = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if quote:
            out.append(ch)
            if ch == "\\" and nxt:
                out.append(nxt)
                i += 2
                continue
            if ch == quote:
                quote = None
            i += 1
            continue
        if ch == "/" and nxt == "*":
            end = text.find("*/", i + 2)
            if end < 0:
                break
            out.append(" ")
            i = end + 2
            continue
        if ch == "/" and nxt == "/":
            end = text.find("\n", i)
            if end < 0:
                break
            i = end
            continue
        if ch in ("'", '"'):
            quote = ch
        out.append(ch)
        i += 1
    return "".join(out)


hiscore_c = strip_comments(open("racing/hiscore.c").read())
main_c = strip_comments(open("racing/main.c").read())
hiscore_h = strip_comments(open("racing/hiscore.h").read())

if "hiscore_run_eligible" not in hiscore_c:
    raise SystemExit("E1 gate: racing/hiscore.c has no hiscore_run_eligible "
                     "outside comments")
if len(re.findall(r"\bhiscore_commit_run\s*\(", main_c)) != 1:
    raise SystemExit("E1 gate: racing/main.c must call hiscore_commit_run() "
                     "exactly once outside comments")
decl = re.search(r"\bbool\s+hiscore_run_eligible\s*\(([^)]*)\)", hiscore_h)
if not decl:
    raise SystemExit("E1 gate: no hiscore_run_eligible() declaration in "
                     "racing/hiscore.h outside comments")
params = [p.strip() for p in decl.group(1).split(",") if p.strip()]
if params != ["const road_config_t *terrain"]:
    raise SystemExit(f"E1 gate: hiscore_run_eligible takes {params}; only "
                     f"terrain may disqualify a run")
E1_GATE
