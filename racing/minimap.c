/*
 * minimap.c - car-relative minimap projection for Racing 3D.
 */

#include "minimap.h"

#include <math.h>

#include "road.h"

/* Shorter than this and a contour segment has no usable direction (two
 * samples clamped onto the same map-edge point): draw a dot instead. */
#define MINIMAP_MIN_SEGMENT 1e-4f

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
    }

    out->has_curve = road_next_curve(car->s, MINIMAP_AHEAD, MINIMAP_CURVE_MIN,
                                     &out->curve_distance,
                                     &out->curve_curvature);
    if (!out->has_curve) {
        out->curve_distance = 0.0f;
        out->curve_curvature = 0.0f;
    }
}

void minimap_contour(const minimap_strip_t *map, minimap_point_t *points)
{
    int i;

    for (i = 0; i < MINIMAP_SAMPLES; i++) {
        points[i].x = map->left_x[i];
        points[i].y = map->left_y[i];
    }
    for (i = 0; i < MINIMAP_SAMPLES; i++) {
        points[MINIMAP_SAMPLES + i].x = map->right_x[MINIMAP_SAMPLES - 1 - i];
        points[MINIMAP_SAMPLES + i].y = map->right_y[MINIMAP_SAMPLES - 1 - i];
    }
}

/*
 * One thin quad per closed segment, as two triangles. Each quad is extended
 * by half a thickness past both ends so neighbouring quads overlap at the
 * joints; a degenerate (clamped-away) segment falls back to a square dot
 * instead of dividing by a zero length.
 */
int minimap_outline(const minimap_strip_t *map, float thickness,
                    minimap_point_t *out)
{
    minimap_point_t pts[MINIMAP_CONTOUR_POINTS];
    float half = thickness * 0.5f;
    int i;

    minimap_contour(map, pts);
    for (i = 0; i < MINIMAP_OUTLINE_SEGMENTS; i++) {
        const minimap_point_t *a = &pts[i];
        const minimap_point_t *b = &pts[(i + 1) % MINIMAP_CONTOUR_POINTS];
        float dx = b->x - a->x;
        float dy = b->y - a->y;
        float len = sqrtf(dx * dx + dy * dy);
        float ux = 1.0f;
        float uy = 0.0f;
        float nx;
        float ny;
        float ax;
        float ay;
        float bx;
        float by;
        minimap_point_t *v = &out[i * 6];

        if (len > MINIMAP_MIN_SEGMENT) {
            ux = dx / len;
            uy = dy / len;
        }
        nx = -uy;
        ny = ux;
        /* Extend by half a thickness along the segment, at both ends. */
        ax = a->x - ux * half;
        ay = a->y - uy * half;
        bx = b->x + ux * half;
        by = b->y + uy * half;

        v[0].x = ax + nx * half;
        v[0].y = ay + ny * half;
        v[1].x = bx + nx * half;
        v[1].y = by + ny * half;
        v[2].x = bx - nx * half;
        v[2].y = by - ny * half;
        v[3] = v[0];
        v[4] = v[2];
        v[5].x = ax - nx * half;
        v[5].y = ay - ny * half;
    }
    return MINIMAP_OUTLINE_VERTS;
}