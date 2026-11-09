add_executable(psp_wifi_test
    main.c
    ../wifi/worker.c
)

target_include_directories(psp_wifi_test PRIVATE ../wifi ../net)

target_link_libraries(psp_wifi_test PRIVATE
    pspdebug
    pspdisplay
    pspge
    pspctrl
    psp_download_psp
    psp_wifi
)

create_pbp_file(
    TARGET psp_wifi_test
    ICON_PATH NULL
    BACKGROUND_PATH NULL
    PREVIEW_PATH NULL
    TITLE "WiFi Test"
    VERSION 01.00
    MEMSIZE ${PSP_MEMSIZE}
)
