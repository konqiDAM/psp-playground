cmake_minimum_required(VERSION 3.11)
project(hello_log C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-O2 -G0 -Wall -Wextra)

add_executable(hello_log main.c)

target_link_libraries(hello_log PRIVATE
    pspdebug
    pspdisplay
    pspge
    pspkernel
)

# Optional: reproduce the games' 1 MiB heap with -DHELLO_HEAP_KB=1024.
set(HELLO_HEAP_KB "" CACHE STRING "Optional PSP heap size in KB")
if(HELLO_HEAP_KB)
    target_compile_definitions(hello_log PRIVATE HELLO_HEAP_KB=${HELLO_HEAP_KB})
endif()

# BUILD_PRX is defined by pspdev's CreatePBP.cmake. Use it to label the
# variant and to select a distinct log file on the memory stick.
if(BUILD_PRX)
    set(HELLO_VARIANT "prx")
    set(HELLO_TITLE "HelloLog PRX")
else()
    set(HELLO_VARIANT "elf")
    set(HELLO_TITLE "HelloLog ELF")
endif()

target_compile_definitions(hello_log PRIVATE
    HELLO_VARIANT="${HELLO_VARIANT}"
)

create_pbp_file(
    TARGET hello_log
    ICON_PATH NULL
    BACKGROUND_PATH NULL
    PREVIEW_PATH NULL
    TITLE "${HELLO_TITLE}"
    VERSION 01.00
    MEMSIZE 1   # same extended partition as the games; harmless on a 1000 (CFW guards it)
)
