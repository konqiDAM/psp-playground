cmake_minimum_required(VERSION 3.11)
project(psp_snake C)

# PARAM.SFO MEMSIZE: which user-memory partition to request.
#   1 = extended: ~55 MiB on a 64 MB PSP (2000/3000/Go/Street) under ARK 5.1+,
#       56 MB in PPSSPP. Recommended for our PSP-2000.
#   0 = 24 MB partition: universal, including PSP-1000.
#   2 = ARK gives +16 MiB (40 MiB total); pspdev's Vita-friendly default.
# Build a PSP-1000/universal EBOOT with: psp-cmake -DPSP_MEMSIZE=0 ...
# See docs/PSP-DEV-GUIDE.md section 1.4 for the measured/verified details.
set(PSP_MEMSIZE 1 CACHE STRING "PARAM.SFO MEMSIZE: 0=24MB universal, 1=extended (2000+), 2=Vita-friendly")

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-O2 -G0 -Wall -Wextra)

add_executable(psp_snake
    src/main.c
    src/game.c
    src/font.c
    src/render.c
)

target_include_directories(psp_snake PRIVATE src)

target_link_libraries(psp_snake PRIVATE
    pspdebug
    pspdisplay
    pspge
    pspgu
    pspctrl
)

create_pbp_file(
    TARGET psp_snake
    ICON_PATH ${CMAKE_SOURCE_DIR}/assets/icon_snake.png
    BACKGROUND_PATH NULL
    PREVIEW_PATH NULL
    TITLE "Snake"
    VERSION 01.00
    MEMSIZE ${PSP_MEMSIZE}
)

add_subdirectory(net)
add_subdirectory(wifi)
add_subdirectory(cube3d)
add_subdirectory(racing)
add_subdirectory(screen)
add_subdirectory(pong)
add_subdirectory(wifi-test)
add_subdirectory(memtest)
