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

/* Panel placement (metres). */
#define SIGNS_LATERAL 2.4f
#define SIGNS_PANEL_W 2.4f
#define SIGNS_PANEL_H 1.8f
#define SIGNS_PANEL_TOP 2.4f
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
} sign_t;

typedef struct {
    float x, y, z;
} sign_vec3_t;

typedef struct {
    sign_vec3_t a, b, c, d;
} sign_quad_t;

/*
 * Place and cull the signs visible from car_s. Pure function of car_s; speed
 * and suspension only raise the visibility eye, exactly as setup_3d() does.
 * Returns the number written (<= max), sorted by ascending s.
 */
int signs_place(float car_s, float car_speed, float car_suspension,
                sign_t *out, int max);

/* Panel frame in world space: centre plus right/up/normal unit vectors. */
void signs_panel_axes(const sign_t *sign, sign_vec3_t *centre,
                      sign_vec3_t *right, sign_vec3_t *up, sign_vec3_t *normal);

/* Panel corners TL, TR, BR, BL. False when the sign data is not finite. */
bool signs_panel_corners(const sign_t *sign, sign_vec3_t out[4]);

/* Symbol quads in world space, already offset 0.02 m toward the driver. */
int signs_symbol_quads(const sign_t *sign, sign_quad_t *out, int max);

#endif /* RACING_SIGNS_H */
