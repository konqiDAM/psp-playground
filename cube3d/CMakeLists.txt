add_executable(psp_cube3d
    main.c
    ../src/font.c
)

target_include_directories(psp_cube3d PRIVATE ../src)

target_link_libraries(psp_cube3d PRIVATE
    pspdebug
    pspdisplay
    pspge
    pspgu
    pspgum
    pspctrl
    m
)

create_pbp_file(
    TARGET psp_cube3d
    ICON_PATH ${CMAKE_SOURCE_DIR}/assets/icon_cube3d.png
    BACKGROUND_PATH NULL
    PREVIEW_PATH NULL
    TITLE "Cube 3D"
    VERSION 01.00
    MEMSIZE ${PSP_MEMSIZE}
)
