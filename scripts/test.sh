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


