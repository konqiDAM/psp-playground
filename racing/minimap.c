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
                          float cy, float sx, float sy, float clamp_x0,
                          float clamp_y0, float clamp_x1, float clamp_y1,
                          float *out_x, float *out_y)
{
    float dx = qx - car_x;
    float dz = qz - car_z;
    float side = dx * rx + dz * rz;
    float forward = dx * tx + dz * tz;

    *out_x = clampf(cx + side * sx, clamp_x0, clamp_x1);
    *out_y = clampf(cy - forward * sy, clamp_y0, clamp_y1);
}

void minimap_build(const car_state_t *car, float x0, float y0, float x1,
                   float y1, float clamp_x0, float clamp_y0, float clamp_x1,
                   float clamp_y1, minimap_strip_t *out)
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
    int i;

    out->count = MINIMAP_SAMPLES;
    out->car_x = cx;
    out->car_y = cy;
    out->panel_cx = (clamp_x0 + clamp_x1) * 0.5f;

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
                      clamp_x0, clamp_y0, clamp_x1, clamp_y1,
                      &out->left_x[i], &out->left_y[i]);
        project_point(rxw, rzw, car_x, car_z, tx, tz, rx, rz, cx, cy, sx, sy,
                      clamp_x0, clamp_y0, clamp_x1, clamp_y1,
                      &out->right_x[i], &out->right_y[i]);
    }

    /* The warning distance is the HUD's 120 m, not the 250 m map window: the
     * map must never move the `R/L nnnM` text out to 250 m. */
    out->has_curve = road_next_curve(car->s, MINIMAP_WARN_AHEAD,
                                     MINIMAP_CURVE_MIN, &out->curve_distance,
                                     &out->curve_curvature);
    if (!out->has_curve) {
        out->curve_distance = 0.0f;
        out->curve_curvature = 0.0f;
    }
}

/*
 * Widen one sample pair to the minimum drawn width, INWARD. The point nearer
 * the panel border is the pair's outer extreme and stays where the projection
 * put it; the other moves toward the panel centre, so the widened pair can
 * never leave the panel rect the projection clamped to. The sign of
 * (right_x - left_x) is preserved, so a U-turn sample whose edges swapped
 * stays swapped (culling is off, so a reversed pair still fills).
 */
static void widen_pair(float centre_x, minimap_point_t *left,
                       minimap_point_t *right)
{
    float width = right->x - left->x;
    float left_off;
    float right_off;

    if (fabsf(width) >= MINIMAP_MIN_WIDTH)
        return;
    left_off = fabsf(left->x - centre_x);
    right_off = fabsf(right->x - centre_x);
    if (width == 0.0f) {
        /* Both edges landed on one point: grow toward the panel centre. */
        if (left->x > centre_x)
            left->x -= MINIMAP_MIN_WIDTH;
        else
            right->x += MINIMAP_MIN_WIDTH;
        return;
    }
    if (left_off >= right_off)
        right->x = left->x + (width < 0.0f ? -MINIMAP_MIN_WIDTH
                                           : MINIMAP_MIN_WIDTH);
    else
        left->x = right->x - (width < 0.0f ? -MINIMAP_MIN_WIDTH
                                           : MINIMAP_MIN_WIDTH);
}

int minimap_ribbon(const minimap_strip_t *map, minimap_point_t *out)
{
    int i;

    for (i = 0; i < MINIMAP_SAMPLES; i++) {
        minimap_point_t left;
        minimap_point_t right;

        left.x = map->left_x[i];
        left.y = map->left_y[i];
        right.x = map->right_x[i];
        right.y = map->right_y[i];
        widen_pair(map->panel_cx, &left, &right);
        out[2 * i] = left;
        out[2 * i + 1] = right;
    }
    return MINIMAP_VERTS;
}
