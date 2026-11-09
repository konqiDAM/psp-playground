/*
 * minimap.c - car-relative minimap projection for Racing 3D.
 */

#include "minimap.h"

#include <math.h>

#include "road.h"

static float clampf(float value, float low, float high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

static void project_point(float qx, float qz, float car_x, float car_z,
                          float tx, float tz, float rx, float rz, float cx,
                          float cy, float sx, float sy, float x0, float y0,
                          float x1, float y1, float *out_x, float *out_y)
{
    float dx = qx - car_x;
    float dz = qz - car_z;
    float side = dx * rx + dz * rz;
    float forward = dx * tx + dz * tz;

    *out_x = clampf(cx + side * sx, x0 - 8.0f, x1 + 8.0f);
    *out_y = clampf(cy - forward * sy, y0 - 8.0f, y1 + 8.0f);
}

void minimap_build(const car_state_t *car, float x0, float y0, float x1,
                   float y1, minimap_strip_t *out)
{
    road_sample_t car_p = road_sample(car->s);
    float h = car_p.heading;
    float tx = -sinf(h);
    float tz = cosf(h);
    float rx = -cosf(h);
    float rz = -sinf(h);
    float car_x = car_p.x + car->lateral * rx;
    float car_z = car_p.z + car->lateral * rz;
    float cx = (x0 + x1) * 0.5f;
    float cy = y1 - 8.0f;
    float sx = ((x1 - x0) * 0.5f) / MINIMAP_LATERAL;
    float sy = (cy - y0) / MINIMAP_AHEAD;
    float span = MINIMAP_BEHIND + MINIMAP_AHEAD;
    float heights[MINIMAP_SAMPLES];
    float hmin = 1e30f;
    float hmax = -1e30f;
    int i;

    out->count = MINIMAP_SAMPLES;
    out->car_x = cx;
    out->car_y = cy;

    for (i = 0; i < MINIMAP_SAMPLES; i++) {
        float s = car->s - MINIMAP_BEHIND +
                  span * (float)i / (float)(MINIMAP_SAMPLES - 1);
        road_sample_t p = road_sample(s);
        float rpx = -cosf(p.heading);
        float rpz = -sinf(p.heading);
        float lx = p.x - p.edge_half_width * rpx;
        float lz = p.z - p.edge_half_width * rpz;
        float rxw = p.x + p.edge_half_width * rpx;
        float rzw = p.z + p.edge_half_width * rpz;

        project_point(lx, lz, car_x, car_z, tx, tz, rx, rz, cx, cy, sx, sy,
                      x0, y0, x1, y1, &out->left_x[i], &out->left_y[i]);
        project_point(rxw, rzw, car_x, car_z, tx, tz, rx, rz, cx, cy, sx, sy,
                      x0, y0, x1, y1, &out->right_x[i], &out->right_y[i]);
        out->asphalt_frac[i] =
            (p.edge_half_width > 1e-4f)
                ? p.half_width / p.edge_half_width
                : 0.5f;
        heights[i] = p.height;
        if (p.height < hmin) hmin = p.height;
        if (p.height > hmax) hmax = p.height;
    }

    for (i = 0; i < MINIMAP_SAMPLES; i++) {
        if (hmax - hmin < 0.5f) {
            out->profile_y[i] = (MINIMAP_PROFILE_Y0 + MINIMAP_PROFILE_Y1) *
                                0.5f;
        } else {
            out->profile_y[i] =
                MINIMAP_PROFILE_Y1 -
                (heights[i] - hmin) / (hmax - hmin) *
                    (MINIMAP_PROFILE_Y1 - MINIMAP_PROFILE_Y0);
        }
    }

    out->has_curve = road_next_curve(car->s, MINIMAP_AHEAD, MINIMAP_CURVE_MIN,
                                     &out->curve_distance,
                                     &out->curve_curvature);
    if (!out->has_curve) {
        out->curve_distance = 0.0f;
        out->curve_curvature = 0.0f;
    }
}
