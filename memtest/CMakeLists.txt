add_executable(psp_memtest
    main.c
    memtest.c
    ../src/font.c
)

target_include_directories(psp_memtest PRIVATE ../src)

target_compile_definitions(psp_memtest PRIVATE
    MEMTEST_MEMSIZE=${PSP_MEMSIZE}
)

target_link_libraries(psp_memtest PRIVATE
    pspdebug
    pspdisplay
    pspge
    pspgu
    pspctrl
)

create_pbp_file(
    TARGET psp_memtest
    ICON_PATH NULL
    BACKGROUND_PATH NULL
    PREVIEW_PATH NULL
    TITLE "Memory Test"
    VERSION 01.00
    MEMSIZE ${PSP_MEMSIZE}
)
