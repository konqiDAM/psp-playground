/*
 * minimap.h - portable car-relative minimap projection for Racing 3D.
 *
 * The car is fixed at (car_x, car_y); the drivable edge of the road ribbon is
 * projected into the car-heading (road tangent) frame over the next 250 m and
 * 12 m behind, then filled as one triangle strip (minimap_ribbon).
 *
 * The projection is isotropic to within 0.2 %: MINIMAP_LATERAL is chosen so
 * sx (50/160 = 0.31250) and sy (78/250 = 0.31200) agree to 1.0016, which is
 * what makes the ribbon's width a true road width.
 */

#ifndef RACING_MINIMAP_H
#define RACING_MINIMAP_H

#include <stdbool.h>

#include "physics.h"

#define MINIMAP_AHEAD 250.0f       /* metres of road shown ahead */
#define MINIMAP_BEHIND 12.0f
#define MINIMAP_SAMPLES 24         /* 11.39 m step: 0.21 px chord error @25 m */
#define MINIMAP_LATERAL 160.0f     /* half-span making sx ~= sy (0.2 %); NOT a road limit */
#define MINIMAP_WARN_AHEAD 120.0f  /* HUD next-curve text only - NOT the map window */
#define MINIMAP_CURVE_MIN (1.0f / 120.0f)
#define MINIMAP_MIN_WIDTH 1.5f     /* px; a projected ribbon may never be thinner */

#define MINIMAP_VERTS (MINIMAP_SAMPLES * 2)
#define MINIMAP_MARKER_VERTS 3
#define MINIMAP_CHEVRON_VERTS 3

typedef struct {
    float x, y;
} minimap_point_t;

typedef struct {
    float left_x[MINIMAP_SAMPLES], left_y[MINIMAP_SAMPLES];
    float right_x[MINIMAP_SAMPLES], right_y[MINIMAP_SAMPLES];
    int count;
    float car_x, car_y;
    /* The PANEL rect's horizontal centre, published by minimap_build() from
     * the clamp arguments. minimap_ribbon() widens a too-thin pair toward
     * THIS point (the border it must not cross), not toward the map rect's
     * centre: the two rects are independent even though they share a centre
     * for today's call site. */
    float panel_cx;
    bool has_curve;
    float curve_distance;  /* m */
    float curve_curvature; /* 1/m, + = right */
} minimap_strip_t;

/*
 * Project the ribbon. `x0,y0,x1,y1` is the map rect that drives the scale;
 * `clamp_x0,clamp_y0,clamp_x1,clamp_y1` is the PANEL rect every projected
 * point is clamped to (draw_minimap passes 360,118,472,224). The two
 * rectangles differ, so the panel rect is passed rather than derived.
 */
void minimap_build(const car_state_t *car, float x0, float y0, float x1,
                   float y1, float clamp_x0, float clamp_y0, float clamp_x1,
                   float clamp_y1, minimap_strip_t *out);

/*
 * Interleaved L0,R0,L1,R1,... for GU_TRIANGLE_STRIP. Applies the minimum-width
 * rule: when |right_x - left_x| < MINIMAP_MIN_WIDTH the pair is widened
 * INWARD - the point nearer the panel border (measured from map->panel_cx)
 * stays put and the other moves - so the expansion can never push a sample
 * outside the panel. Orientation is
 * preserved: a U-turn sample whose edges swap stays swapped, but is never
 * thinner than 1.5 px. Writes exactly MINIMAP_VERTS points.
 */
int minimap_ribbon(const minimap_strip_t *map, minimap_point_t *out);

#endif /* RACING_MINIMAP_H */
