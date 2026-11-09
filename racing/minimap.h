/*
 * minimap.h - portable car-relative minimap projection for Racing 3D.
 *
 * The car is fixed at (car_x, car_y); the road band is projected into the
 * car-heading (road tangent) frame over the next 120 m and 12 m behind.
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
#define MINIMAP_PROFILE_Y0 225.0f
#define MINIMAP_PROFILE_Y1 230.0f

typedef struct {
    float left_x[MINIMAP_SAMPLES], left_y[MINIMAP_SAMPLES];
    float right_x[MINIMAP_SAMPLES], right_y[MINIMAP_SAMPLES];
    float asphalt_frac[MINIMAP_SAMPLES];
    float profile_y[MINIMAP_SAMPLES];
    int count;
    float car_x, car_y;
    bool has_curve;
    float curve_distance;  /* m */
    float curve_curvature; /* 1/m, + = right */
} minimap_strip_t;

void minimap_build(const car_state_t *car, float x0, float y0, float x1,
                   float y1, minimap_strip_t *out);

#endif /* RACING_MINIMAP_H */
