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

# Diagnostic: paint the lane dashes magenta so the crest probe can count dash
# ink unambiguously (the only magenta in the scene). Same cull code path; only
# the vertex colour differs. Off by default.
option(RACING_DIAG_DASH "Paint Racing 3D lane dashes magenta for the crest probe" OFF)
if (RACING_DIAG_DASH)
    target_compile_definitions(psp_racing PRIVATE RACING_DIAG_DASH=1)
endif()

# Diagnostic: start the run at this centreline s (0 = normal). The dash/crest
# probe places the car just before a crest so the hidden dashes project at
# several pixels instead of sub-pixel. The define is only emitted when the
# value is positive, so a release build never sees it.
set(RACING_DIAG_START_S 0 CACHE STRING "Diagnostic: start the car at this centreline s")
if (RACING_DIAG_START_S GREATER 0)
    target_compile_definitions(psp_racing PRIVATE RACING_DIAG_START_S=${RACING_DIAG_START_S})
endif()

# Diagnostic: force every dash band visible (the crest probe's negative
# control). Same EBOOT + same captures as RACING_DIAG_DASH, so the probe can
# measure exactly what the cull removes. Off by default.
option(RACING_DIAG_DASH_UNCULLED "Disable the dash sight-line cull for the crest probe" OFF)
if (RACING_DIAG_DASH_UNCULLED)
    target_compile_definitions(psp_racing PRIVATE RACING_DIAG_DASH_UNCULLED=1)
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
    VERSION 0.9.0
    MEMSIZE ${PSP_MEMSIZE}
)
