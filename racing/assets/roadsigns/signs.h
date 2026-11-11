/*
 * signs.h - portable roadside-sign model for Racing 3D.
 *
 * Placement and panel geometry only; no PSP headers here so the host tests
 * in tests/test_racing_signs.c can build it with a normal C99 compiler.
 * The GU drawing lives in signs_gu.c and is driven from render.c.
 */

#ifndef RACING_SIGNS_H
#define RACING_SIGNS_H

#include <stdbool.h>

#include "road.h"

#define SIGNS_MAX 32

/* Event search, emission window and spacing (metres). */
#define SIGNS_LEAD_FAR 120.0f
#define SIGNS_LEAD_NEAR 60.0f
#define SIGNS_LOOKAHEAD 260.0f
#define SIGNS_MIN_AHEAD 8.0f
#define SIGNS_MAX_AHEAD 240.0f
#define SIGNS_MIN_GAP 18.0f
#define SIGNS_MAX_DRAWN 6 /* 3 event kinds x 2 signs */

/* Event detection. */
#define SIGNS_CURVE_MIN ROAD_TIGHT_CURVATURE
#define SIGNS_WIDTH_STEP 2.0f
#define SIGNS_WIDTH_SLOPE 0.05f
#define SIGNS_SIGHT_STEP 10.0f

/* Panel placement (metres). Anchored at the verge, not centred: the inner
 * edge sits SIGNS_PANEL_GAP from the drivable edge at EVERY scale, so the
 * post and car clearance are scale-invariant. */
#define SIGNS_PANEL_W 3.6f        /* 100 % width  (was 2.4) */
#define SIGNS_PANEL_H 2.7f        /* 100 % height (was 1.8) */
#define SIGNS_PANEL_BOTTOM 0.70f  /* fixed at every scale (was TOP - H = 0.60) */
#define SIGNS_PANEL_GAP 0.80f     /* inner edge from the drivable edge (was 1.2) */
#define SIGNS_SCALE_MIN 0.60f
#define SIGNS_SCALE_MAX 1.60f
#define SIGNS_SCALE_DEFAULT 1.00f
#define SIGNS_POST_W 0.14f
#define SIGNS_MAX_VERTS 96

typedef enum {
    SIGNS_NONE = 0,
    SIGNS_CURVE_LEFT,
    SIGNS_CURVE_RIGHT,
    SIGNS_MERGE,
    SIGNS_WIDEN,
    SIGNS_ONELANE,
    SIGNS_CREST
} signs_type_t;

typedef struct {
    signs_type_t type;
    int lanes; /* target lane count 1..4 for MERGE/WIDEN/ONELANE, else 0 */
    float s;
    float lateral; /* signed, + = driver's right */
    float road_height;
    float heading;
    float grade;
    float panel_scale; /* 0.60..1.60, clamped by signs_place() */
} sign_t;

typedef struct {
    float x, y, z;
} sign_vec3_t;

typedef struct {
    sign_vec3_t a, b, c, d;
} sign_quad_t;

/*
 * Panel frame in world space. hu/hv are the scale-derived panel half-width
 * and half-height, so the panel corners AND the symbol quads are built from
 * one frame and cannot drift apart.
 */
typedef struct {
    sign_vec3_t centre, right, up, normal;
    float hu, hv; /* panel half-width / half-height, scale-derived */
} panel_frame_t;

typedef panel_frame_t sign_panel_t; /* the public name */

/* The ONLY writer of centre/right/up/normal/hu/hv. */
void signs_panel_frame(const sign_t *sign, sign_panel_t *out);

/*
 * Place and cull the signs visible from car_s. Pure function of car_s; speed
 * and suspension only raise the visibility eye, exactly as setup_3d() does.
 * `panel_scale` is clamped to SIGNS_SCALE_MIN..SIGNS_SCALE_MAX (non-finite ->
 * SIGNS_SCALE_DEFAULT) and stored on every sign. Returns the number written
 * (<= max), sorted by ascending s.
 */
int signs_place(float car_s, float car_speed, float car_suspension,
                float panel_scale, sign_t *out, int max);

/* Thin wrapper over signs_panel_frame(): the pre-0.8.0 call shape. */
void signs_panel_axes(const sign_t *sign, sign_vec3_t *centre,
                      sign_vec3_t *right, sign_vec3_t *up, sign_vec3_t *normal);

/* Panel corners TL, TR, BR, BL. False when the sign data is not finite. */
bool signs_panel_corners(const sign_t *sign, sign_vec3_t out[4]);

/* Symbol quads in world space, already offset 0.02 m toward the driver. */
int signs_symbol_quads(const sign_t *sign, sign_quad_t *out, int max);

/*
 * Vertex budget, host-testable: one 6-vertex quad each. The post is 4 side
 * quads (24), the panel face 1 (6), so a sign is signs_base_verts() = 30
 * depth-tested vertices plus signs_ink_verts(quads) = 6*quads decal ones.
 */
int signs_panel_verts(void);  /* 6 */
int signs_post_verts(void);   /* 24 */
int signs_base_verts(void);   /* 30 */
int signs_ink_verts(int quads);

#endif /* RACING_SIGNS_H */
