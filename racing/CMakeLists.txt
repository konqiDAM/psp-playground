add_executable(psp_racing
    main.c
    physics.c
    road.c
    render.c
    input.c
    settings.c
    score.c
    hiscore.c
    telemetry.c
    minimap.c
    menu.c
    welcome.c
    storage.c
    assets/roadsigns/signs.c
    assets/roadsigns/signs_gu.c
    assets/guardrail/guardrail.c
    assets/guardrail/guardrail_gu.c
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

# Diagnostic: shorten the Time Attack clock so the headless probe can reach the
# result panel and the retry flow in a few seconds. Test-only; default OFF, and
# the release EBOOT never defines RACING_DIAG_TA_SECONDS.
option(RACING_DIAG_SHORT_TA "Use a 6 s Time Attack clock for the headless probe" OFF)
if (RACING_DIAG_SHORT_TA)
    target_compile_definitions(psp_racing PRIVATE RACING_DIAG_TA_SECONDS=6)
endif()

target_link_libraries(psp_racing PRIVATE
    pspdebug
    pspdisplay
    pspge
    pspgu
    pspgum
    pspctrl
    psprtc
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
