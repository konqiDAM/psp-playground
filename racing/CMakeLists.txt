add_executable(psp_racing
    main.c
    physics.c
    road.c
    render.c
    input.c
    settings.c
    telemetry.c
    minimap.c
    menu.c
    welcome.c
    storage.c
    ../src/font.c
)

target_include_directories(psp_racing PRIVATE ../src .)

# Diagnostic: paint the ground magenta so scripts/check-racing-road.py can
# spot any ground pixel over the asphalt unambiguously. Off by default.
option(RACING_DIAG_GROUND "Paint the Racing 3D ground magenta for the road probe" OFF)
if (RACING_DIAG_GROUND)
    target_compile_definitions(psp_racing PRIVATE RACING_DIAG_GROUND=1)
endif()

# Diagnostic: force the first crest early on the start section so the AIR
# probe can launch without needing to survive the first curve at speed.
option(RACING_DIAG_CREST "Force an early deterministic crest for the Racing 3D air probe" OFF)
if (RACING_DIAG_CREST)
    target_compile_definitions(psp_racing PRIVATE RACING_DIAG_CREST=1)
endif()

# Diagnostic: force an early one-lane span for the lane-marking probe.
option(RACING_DIAG_NARROW "Force an early one-lane span for the Racing 3D lane probe" OFF)
if (RACING_DIAG_NARROW)
    target_compile_definitions(psp_racing PRIVATE RACING_DIAG_NARROW=1)
endif()

target_link_libraries(psp_racing PRIVATE
    pspdebug
    pspdisplay
    pspge
    pspgu
    pspgum
    pspctrl
    m
)

create_pbp_file(
    TARGET psp_racing
    ICON_PATH ${CMAKE_SOURCE_DIR}/assets/icon_racing.png
    BACKGROUND_PATH NULL
    PREVIEW_PATH NULL
    TITLE "Racing 3D"
    VERSION 05.00
    MEMSIZE ${PSP_MEMSIZE}
)
