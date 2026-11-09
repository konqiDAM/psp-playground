add_library(psp_wifi STATIC
    wifi.c
    wifi_logic.c
    net_sock.c
    net_probe.c
    net_probe_logic.c
    checksum.c
    http_logic.c
    http_client.c
)

target_include_directories(psp_wifi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(psp_wifi PUBLIC
    pspnet
    pspnet_inet
    pspnet_apctl
    pspnet_resolver
    psputility
    pspwlan
    psppower
)
