add_executable(psp_pong
    main.c
    ../src/font.c
)

target_include_directories(psp_pong PRIVATE ../src)

target_link_libraries(psp_pong PRIVATE
    pspdebug
    pspdisplay
    pspge
    pspgu
    pspctrl
    pspkernel
    m
)

create_pbp_file(
    TARGET psp_pong
    ICON_PATH ${CMAKE_SOURCE_DIR}/assets/icon_pong.png
    BACKGROUND_PATH NULL
    PREVIEW_PATH NULL
    TITLE "Pong"
    VERSION 01.00
    MEMSIZE ${PSP_MEMSIZE}
)
