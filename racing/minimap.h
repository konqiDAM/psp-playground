/*
 * minimap.h - portable car-relative minimap projection for Racing 3D.
 *
 * The car is fixed at (car_x, car_y); the drivable edge of the road ribbon is
 * projected into the car-heading (road tangent) frame over the next 120 m and
 * 12 m behind. The renderer draws it as a closed outline of thin quads.
 */

#ifndef RACING_MINIMAP_H
#define RACING_MINIMAP_H

#include <stdbool.h>

#include "physics.h"

#define MINIMAP_AHEAD 120.0f
#define MINIMAP_BEHIND 12.0f
#define MINIMAP_LATERAL 12.0f
#define MINIMAP_SAMPLES 24
#define MINIMAP_CURVE_MIN (1.0f / 120.0f)

/*
 * Closed outline of the drivable edge: 2 px thick, 6 vertices per closed
 * segment (PSP GU line primitives are unreliable at 1 px).
 */
#define MINIMAP_EDGE_THICKNESS 2.0f
#define MINIMAP_CONTOUR_POINTS (MINIMAP_SAMPLES * 2)
#define MINIMAP_OUTLINE_SEGMENTS MINIMAP_CONTOUR_POINTS
#define MINIMAP_OUTLINE_VERTS (MINIMAP_OUTLINE_SEGMENTS * 6)

typedef struct {
    float x, y;
} minimap_point_t;

typedef struct {
    float left_x[MINIMAP_SAMPLES], left_y[MINIMAP_SAMPLES];
    float right_x[MINIMAP_SAMPLES], right_y[MINIMAP_SAMPLES];
    int count;
    float car_x, car_y;
    bool has_curve;
    float curve_distance;  /* m */
    float curve_curvature; /* 1/m, + = right */
} minimap_strip_t;

void minimap_build(const car_state_t *car, float x0, float y0, float x1,
                   float y1, minimap_strip_t *out);

/*
 * Closed contour of the drivable edge: left edge 0..N-1, the far cap, then
 * the right edge N-1..0. The near cap closes the loop from the last point
 * back to the first.
 */
void minimap_contour(const minimap_strip_t *map, minimap_point_t *points);

/*
 * Expand the closed contour into thin quads. Writes exactly
 * MINIMAP_OUTLINE_VERTS vertices to `out` and returns that count.
 */
int minimap_outline(const minimap_strip_t *map, float thickness,
                    minimap_point_t *out);

#endif /* RACING_MINIMAP_H */
