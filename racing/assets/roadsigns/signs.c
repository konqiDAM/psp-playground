/*
 * signs.c - portable roadside-sign model for Racing 3D.
 *
 * Every event property (position and face type) is a pure function of fixed
 * road data: a crest segment start or the fixed 2 m width-scan lattice. The
 * road helpers that scan on a car_s-relative grid (road_next_crest's
 * distance, road_next_narrow) are never used for a position or a type.
 */

#include "assets/roadsigns/signs.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ events */

typedef struct {
    float s;
    signs_type_t type;
    int lanes;
} sign_event_t;

static int event_priority(signs_type_t type)
{
    switch (type) {
    case SIGNS_ONELANE:
        return 5;
    case SIGNS_CREST:
        return 4;
    case SIGNS_CURVE_LEFT:
    case SIGNS_CURVE_RIGHT:
        return 3;
    case SIGNS_MERGE:
        return 2;
    case SIGNS_WIDEN:
        return 1;
    default:
        return 0;
    }
}

static float lattice_start(float car_s)
{
    float s = ceilf((car_s + 1.0f) / SIGNS_WIDTH_STEP) * SIGNS_WIDTH_STEP;

    return s < car_s + 1.0f ? s + SIGNS_WIDTH_STEP : s;
}

/* Nearest width ramp ahead; -1 when none. */
static bool width_event(float car_s, sign_event_t *out)
{
    float s = lattice_start(car_s);
    float limit = car_s + SIGNS_LOOKAHEAD;
    float prev = road_sample(s).half_width;

    for (; s + SIGNS_WIDTH_STEP <= limit && s <= limit;
         s += SIGNS_WIDTH_STEP) {
        float cur = road_sample(s + SIGNS_WIDTH_STEP).half_width;
        float delta = cur - prev;

        if (fabsf(delta) >= SIGNS_WIDTH_SLOPE) {
            float target = road_sample(s + ROAD_WIDTH_RAMP).half_width;
            int lanes = road_lane_count(target);

            out->s = s;
            out->lanes = lanes;
            if (delta < 0.0f)
                out->type = (lanes == 1 && target <= ROAD_NARROW_WARN_HALF)
                                ? SIGNS_ONELANE
                                : SIGNS_MERGE;
            else
                out->type = SIGNS_WIDEN;
            return true;
        }
        prev = cur;
    }
    return false;
}

/* Crest position from the segment ring (fixed start_s, not the helper's grid). */
static bool crest_event(float car_s, sign_event_t *out)
{
    float distance = 0.0f;
    float rate = 0.0f;
    float limit;
    int count;
    int i;

    if (!road_next_crest(car_s, SIGNS_LOOKAHEAD, ROAD_CREST_WARN_RATE,
                         &distance, &rate))
        return false;
    limit = car_s + distance + 5.0f;
    count = road_segment_count();
    for (i = 0; i < count; i++) {
        const road_segment_t *seg = road_segment_at(i);

        if (!seg)
            continue;
        if (seg->start_s < car_s + 1.0f || seg->start_s > limit)
            continue;
        if (seg->grade_rate > -ROAD_CREST_WARN_RATE)
            continue;
        out->s = seg->start_s;
        out->type = SIGNS_CREST;
        out->lanes = 0;
        return true;
    }
    return false;
}

static int collect_events(float car_s, sign_event_t *out, int max)
{
    float distance = 0.0f;
    float curvature = 0.0f;
    int n = 0;

    if (n < max && road_next_curve(car_s, SIGNS_LOOKAHEAD, SIGNS_CURVE_MIN,
                                   &distance, &curvature)) {
        out[n].s = car_s + distance;
        out[n].type = curvature < 0.0f ? SIGNS_CURVE_LEFT : SIGNS_CURVE_RIGHT;
        out[n].lanes = 0;
        n++;
    }
    if (n < max && crest_event(car_s, &out[n]))
        n++;
    if (n < max && width_event(car_s, &out[n]))
        n++;
    return n;
}

/* -------------------------------------------------------------- visibility */

static bool line_clear(float eye_s, float eye_y, float sign_s, float target_y)
{
    float span = sign_s - eye_s;
    float s;

    if (span <= 12.0f || !isfinite(span) || !isfinite(target_y))
        return false;
    for (s = eye_s + SIGNS_SIGHT_STEP; s <= sign_s - 4.0f;
         s += SIGNS_SIGHT_STEP) {
        float t = (s - eye_s) / span;
        float y = eye_y + (target_y - eye_y) * t;

        if (y < road_sample(s).height + 0.05f)
            return false;
    }
    return true;
}

static bool sign_visible(float eye_s, float eye_y, const sign_t *sign)
{
    /* The lateral component does not change the terrain height: the asphalt
     * and the 4.5 m shoulders share the road height across the whole width.
     *
     * ONE sight-line target, the panel bottom: the bottom is fixed at
     * SIGNS_PANEL_BOTTOM at every scale and is the LOWEST line to the panel,
     * so if it is blocked the panel is culled and if it is clear no higher
     * line (centre or top) can be blocked. A third target would be dead code
     * (plan 5.5/M-7). */
    return line_clear(eye_s, eye_y, sign->s,
                      sign->road_height + SIGNS_PANEL_BOTTOM);
}

/* ------------------------------------------------------------------ public */

/* One clamp for every consumer: a non-finite scale is the default and any
 * other value is clamped into SIGNS_SCALE_MIN..SIGNS_SCALE_MAX, so the panel
 * can never be built from an out-of-range (or zeroed) scale. */
static float scale_clamped(float scale)
{
    if (!isfinite(scale))
        return SIGNS_SCALE_DEFAULT;
    if (scale < SIGNS_SCALE_MIN)
        return SIGNS_SCALE_MIN;
    if (scale > SIGNS_SCALE_MAX)
        return SIGNS_SCALE_MAX;
    return scale;
}

void signs_panel_frame(const sign_t *sign, sign_panel_t *out)
{
    road_sample_t p;
    sign_vec3_t t3;
    sign_vec3_t r3;
    sign_vec3_t u3;
    float theta;
    float ct;
    float st;
    float scale;

    if (!sign || !out)
        return;
    p = road_sample(sign->s);
    theta = atanf(sign->grade);
    ct = cosf(theta);
    st = sinf(theta);

    t3.x = -sinf(sign->heading) * ct;
    t3.y = st;
    t3.z = cosf(sign->heading) * ct;
    r3.x = -cosf(sign->heading);
    r3.y = 0.0f;
    r3.z = -sinf(sign->heading);
    /* U3 = R3 x T3: (0, 1, 0) at grade 0 */
    u3.x = r3.y * t3.z - r3.z * t3.y;
    u3.y = r3.z * t3.x - r3.x * t3.z;
    u3.z = r3.x * t3.y - r3.y * t3.x;

    scale = scale_clamped(sign->panel_scale);
    out->hu = SIGNS_PANEL_W * scale * 0.5f;
    out->hv = SIGNS_PANEL_H * scale * 0.5f;
    out->right = r3;
    out->up = u3;
    out->normal.x = -t3.x;
    out->normal.y = -t3.y;
    out->normal.z = -t3.z;
    out->centre.x = p.x + sign->lateral * r3.x;
    /* Fixed bottom at every scale: centre = bottom + half-height. */
    out->centre.y = sign->road_height + SIGNS_PANEL_BOTTOM + out->hv;
    out->centre.z = p.z + sign->lateral * r3.z;
}

void signs_panel_axes(const sign_t *sign, sign_vec3_t *centre,
                      sign_vec3_t *right, sign_vec3_t *up, sign_vec3_t *normal)
{
    sign_panel_t f;

    if (!sign)
        return;
    signs_panel_frame(sign, &f);
    if (centre)
        *centre = f.centre;
    if (right)
        *right = f.right;
    if (up)
        *up = f.up;
    if (normal)
        *normal = f.normal;
}

bool signs_panel_corners(const sign_t *sign, sign_vec3_t out[4])
{
    sign_panel_t f;

    if (!sign || !out)
        return false;
    if (!isfinite(sign->s) || !isfinite(sign->lateral) ||
        !isfinite(sign->road_height) || !isfinite(sign->heading) ||
        !isfinite(sign->grade))
        return false;
    signs_panel_frame(sign, &f);
    out[0].x = f.centre.x - f.hu * f.right.x + f.hv * f.up.x; /* TL */
    out[0].y = f.centre.y - f.hu * f.right.y + f.hv * f.up.y;
    out[0].z = f.centre.z - f.hu * f.right.z + f.hv * f.up.z;
    out[1].x = f.centre.x + f.hu * f.right.x + f.hv * f.up.x; /* TR */
    out[1].y = f.centre.y + f.hu * f.right.y + f.hv * f.up.y;
    out[1].z = f.centre.z + f.hu * f.right.z + f.hv * f.up.z;
    out[2].x = f.centre.x + f.hu * f.right.x - f.hv * f.up.x; /* BR */
    out[2].y = f.centre.y + f.hu * f.right.y - f.hv * f.up.y;
    out[2].z = f.centre.z + f.hu * f.right.z - f.hv * f.up.z;
    out[3].x = f.centre.x - f.hu * f.right.x - f.hv * f.up.x; /* BL */
    out[3].y = f.centre.y - f.hu * f.right.y - f.hv * f.up.y;
    out[3].z = f.centre.z - f.hu * f.right.z - f.hv * f.up.z;
    return true;
}

/* ------------------------------------------------------------- symbols */

typedef struct {
    float u, v;
} sign_uv_t;

typedef struct {
    sign_uv_t a, b, c, d;
} uv_quad_t;

static uv_quad_t bar(sign_uv_t p0, sign_uv_t p1, float half)
{
    uv_quad_t q;
    float dx = p1.u - p0.u;
    float dy = p1.v - p0.v;
    float len = sqrtf(dx * dx + dy * dy);
    float nx;
    float ny;

    if (len < 1e-6f) {
        nx = 0.0f;
        ny = 1.0f;
    } else {
        nx = -dy / len;
        ny = dx / len;
    }
    q.a = (sign_uv_t){p0.u + nx * half, p0.v + ny * half};
    q.b = (sign_uv_t){p1.u + nx * half, p1.v + ny * half};
    q.c = (sign_uv_t){p1.u - nx * half, p1.v - ny * half};
    q.d = (sign_uv_t){p0.u - nx * half, p0.v - ny * half};
    return q;
}

static sign_vec3_t panel_point(const panel_frame_t *f, sign_uv_t p)
{
    sign_vec3_t out;

    /* The half-extents come from the frame, never from frozen literals: with
     * 1.2/0.9 a 1.5x panel would ship with 1x symbols. */
    out.x = f->centre.x + p.u * f->hu * f->right.x + p.v * f->hv * f->up.x +
            0.02f * f->normal.x;
    out.y = f->centre.y + p.u * f->hu * f->right.y + p.v * f->hv * f->up.y +
            0.02f * f->normal.y;
    out.z = f->centre.z + p.u * f->hu * f->right.z + p.v * f->hv * f->up.z +
            0.02f * f->normal.z;
    return out;
}

static void emit_uv(sign_quad_t *out, int *count, int max,
                    const panel_frame_t *f, uv_quad_t q)
{
    sign_quad_t w;

    if (*count >= max)
        return;
    w.a = panel_point(f, q.a);
    w.b = panel_point(f, q.b);
    w.c = panel_point(f, q.c);
    w.d = panel_point(f, q.d);
    out[*count] = w;
    (*count)++;
}

static void emit_bar(sign_quad_t *out, int *count, int max,
                     const panel_frame_t *f, sign_uv_t p0, sign_uv_t p1,
                     float half)
{
    emit_uv(out, count, max, f, bar(p0, p1, half));
}

int signs_symbol_quads(const sign_t *sign, sign_quad_t *out, int max)
{
    panel_frame_t f;
    int count = 0;
    int lanes;
    int k;

    if (!sign || !out || max <= 0)
        return 0;
    signs_panel_frame(sign, &f);

    switch (sign->type) {
    case SIGNS_CURVE_RIGHT:
        if (max < 2)
            return 0;
        emit_bar(out, &count, max, &f, (sign_uv_t){-0.55f, -0.10f},
                 (sign_uv_t){0.45f, 0.75f}, 0.15f);
        emit_bar(out, &count, max, &f, (sign_uv_t){-0.55f, 0.10f},
                 (sign_uv_t){0.45f, -0.75f}, 0.15f);
        break;
    case SIGNS_CURVE_LEFT:
        if (max < 2)
            return 0;
        emit_bar(out, &count, max, &f, (sign_uv_t){0.55f, -0.10f},
                 (sign_uv_t){-0.45f, 0.75f}, 0.15f);
        emit_bar(out, &count, max, &f, (sign_uv_t){0.55f, 0.10f},
                 (sign_uv_t){-0.45f, -0.75f}, 0.15f);
        break;
    case SIGNS_MERGE:
    case SIGNS_WIDEN:
        lanes = sign->lanes < 1 ? 1 : (sign->lanes > 4 ? 4 : sign->lanes);
        if (max < lanes + 2)
            return 0;
        for (k = 0; k < lanes; k++) {
            float ck = ((float)k - (float)(lanes - 1) * 0.5f) * 0.46f;

            emit_bar(out, &count, max, &f, (sign_uv_t){ck, -0.55f},
                     (sign_uv_t){ck, 0.55f}, 0.15f);
        }
        if (sign->type == SIGNS_MERGE) {
            emit_bar(out, &count, max, &f, (sign_uv_t){-0.88f, 0.75f},
                     (sign_uv_t){-0.52f, 0.12f}, 0.10f);
            emit_bar(out, &count, max, &f, (sign_uv_t){0.88f, 0.75f},
                     (sign_uv_t){0.52f, 0.12f}, 0.10f);
        } else {
            emit_bar(out, &count, max, &f, (sign_uv_t){-0.52f, 0.75f},
                     (sign_uv_t){-0.88f, 0.12f}, 0.10f);
            emit_bar(out, &count, max, &f, (sign_uv_t){0.52f, 0.75f},
                     (sign_uv_t){0.88f, 0.12f}, 0.10f);
        }
        break;
    case SIGNS_ONELANE:
        if (max < 3)
            return 0;
        emit_bar(out, &count, max, &f, (sign_uv_t){-0.55f, -0.70f},
                 (sign_uv_t){-0.55f, 0.70f}, 0.17f);
        emit_bar(out, &count, max, &f, (sign_uv_t){0.30f, 0.10f},
                 (sign_uv_t){0.30f, 0.80f}, 0.10f);
        emit_bar(out, &count, max, &f, (sign_uv_t){0.30f, -0.60f},
                 (sign_uv_t){0.30f, -0.40f}, 0.10f);
        break;
    case SIGNS_CREST:
        if (max < 3)
            return 0;
        emit_bar(out, &count, max, &f, (sign_uv_t){-0.85f, -0.63f},
                 (sign_uv_t){0.85f, -0.63f}, 0.07f);
        emit_bar(out, &count, max, &f, (sign_uv_t){-0.72f, -0.52f},
                 (sign_uv_t){-0.05f, 0.60f}, 0.14f);
        emit_bar(out, &count, max, &f, (sign_uv_t){0.72f, -0.52f},
                 (sign_uv_t){0.05f, 0.60f}, 0.14f);
        break;
    default:
        return 0;
    }
    return count;
}

int signs_panel_verts(void)
{
    return 6;
}

int signs_post_verts(void)
{
    return 24;
}

int signs_base_verts(void)
{
    return signs_post_verts() + signs_panel_verts();
}

int signs_ink_verts(int quads)
{
    return quads > 0 ? 6 * quads : 0;
}

int signs_place(float car_s, float car_speed, float car_suspension,
                float panel_scale, sign_t *out, int max)
{
    sign_event_t events[3];
    sign_t candidates[SIGNS_MAX_DRAWN * 2];
    float scale = scale_clamped(panel_scale);
    float eye_s;
    float eye_y;
    float leads[2];
    int event_count;
    int count = 0;
    int i;
    int j;

    if (!out || max <= 0)
        return 0;
    if (max > (int)(sizeof(candidates) / sizeof(candidates[0])))
        max = (int)(sizeof(candidates) / sizeof(candidates[0]));
    if (!isfinite(car_s))
        return 0;

    eye_s = car_s - (8.2f + 0.035f * fabsf(car_speed));
    eye_y = road_sample(eye_s).height + 3.1f + 0.006f * fabsf(car_speed) +
            0.6f * car_suspension;

    event_count = collect_events(car_s, events, 3);
    leads[0] = SIGNS_LEAD_FAR;
    leads[1] = SIGNS_LEAD_NEAR;

    for (i = 0; i < event_count; i++) {
        for (j = 0; j < 2; j++) {
            float sign_s = events[i].s - leads[j];
            road_sample_t p;

            if (count >=
                (int)(sizeof(candidates) / sizeof(candidates[0])))
                break; /* defensive: three event kinds can never fill this */
            if (sign_s - car_s < SIGNS_MIN_AHEAD ||
                sign_s - car_s > SIGNS_MAX_AHEAD)
                continue;
            p = road_sample(sign_s);
            candidates[count].type = events[i].type;
            candidates[count].lanes = events[i].lanes;
            candidates[count].s = sign_s;
            /* Anchored, not centred: the INNER edge sits SIGNS_PANEL_GAP
             * from the drivable edge at every scale, so post and car
             * clearance never move with the sign size. */
            candidates[count].lateral = p.edge_half_width + SIGNS_PANEL_GAP +
                                        SIGNS_PANEL_W * scale * 0.5f;
            candidates[count].panel_scale = scale;
            candidates[count].road_height = p.height;
            candidates[count].heading = p.heading;
            candidates[count].grade = p.grade;
            count++;
        }
    }

    /* insertion sort by s, ties by type priority (descending) */
    for (i = 1; i < count; i++) {
        sign_t key = candidates[i];

        j = i - 1;
        while (j >= 0 &&
               (candidates[j].s > key.s ||
                (candidates[j].s == key.s &&
                 event_priority(candidates[j].type) <
                     event_priority(key.type)))) {
            candidates[j + 1] = candidates[j];
            j--;
        }
        candidates[j + 1] = key;
    }

    /* dedupe: keep the nearest of any pair closer than SIGNS_MIN_GAP */
    {
        int kept = 0;

        for (i = 0; i < count; i++) {
            if (kept > 0 &&
                candidates[i].s - candidates[kept - 1].s < SIGNS_MIN_GAP)
                continue;
            candidates[kept++] = candidates[i];
        }
        count = kept;
    }

    if (count > SIGNS_MAX_DRAWN)
        count = SIGNS_MAX_DRAWN;
    if (count > max)
        count = max;

    /* visibility cull, compacting in place */
    {
        int kept = 0;

        for (i = 0; i < count; i++) {
            if (!sign_visible(eye_s, eye_y, &candidates[i]))
                continue;
            out[kept++] = candidates[i];
        }
        return kept;
    }
}
