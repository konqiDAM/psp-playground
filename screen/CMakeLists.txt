add_executable(psp_screen main.c)

target_link_libraries(psp_screen PRIVATE
    pspdebug
    pspdisplay
    pspge
    pspctrl
    pspkernel
)

create_pbp_file(
    TARGET psp_screen
    ICON_PATH NULL
    BACKGROUND_PATH NULL
    PREVIEW_PATH NULL
    TITLE "Screen Test"
    VERSION 01.00
    MEMSIZE ${PSP_MEMSIZE}
)
