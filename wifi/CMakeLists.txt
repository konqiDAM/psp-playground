add_library(psp_download_core STATIC
    catalog.c
    install_state.c
    download_state.c
    download.c
    download_policy.c
)

target_include_directories(psp_download_core PUBLIC . ../net)

add_library(psp_download_psp STATIC
    download_psp.c
    power.c
)

target_include_directories(psp_download_psp PUBLIC . ../net)

target_link_libraries(psp_download_psp PUBLIC
    psp_download_core
    psp_wifi
    psppower
)

add_executable(psp_wifi_app
    main.c
    app.c
    ui.c
    worker.c
    netconf.c
)

target_include_directories(psp_wifi_app PRIVATE . ../net)

target_link_libraries(psp_wifi_app PRIVATE
    pspdebug
    pspdisplay
    pspge
    pspgu
    pspctrl
    psp_download_psp
)

create_pbp_file(
    TARGET psp_wifi_app
    ICON_PATH ${CMAKE_SOURCE_DIR}/assets/icon_wifi.png
    BACKGROUND_PATH NULL
    PREVIEW_PATH NULL
    TITLE "WiFi Connection"
    VERSION 0.2.0
    MEMSIZE ${PSP_MEMSIZE}
)
