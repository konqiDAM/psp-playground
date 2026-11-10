/*
 * test_racing_signs.c - host tests for the portable roadside-sign model.
 *
 * The model lives in racing/assets/roadsigns/signs.c and must not depend on
 * PSP headers. scripts/test.sh links only this file and signs.c, so every
 * road.h function the model calls has a stub here. The road_next_narrow stub
 * is a tripwire: the model must never use it (its value is car_s-relative and
 * would flip a visible panel's face).
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "assets/roadsigns/signs.h"
#include "road.h"

static int failures;

#define CHECK(c)                                                               \
    do {                                                                       \
        if (!(c)) {                                                           \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);      \
            failures++;                                                        \
        }                                                                      \
    } while (0)
#define NEAR(a, b, e) (fabsf((a) - (b)) <= (e))

/* --- stub world ------------------------------------------------------ */

static road_segment_t g_seg[4];
static int g_seg_count;

static struct {
    float base_height; /* height at s = 0 */
    float grade;
    float half_width;
    bool curve;
    float curve_s, curve_k;
    bool crest;
    float crest_s, crest_rate;
    bool want_width_ramp; /* width scan sees a ramp at ramp_s */
    float ramp_s, ramp_to;
    bool block;
    float block_from, block_to, block_add; /* raised road slice */
} g;

road_sample_t road_sample(float s)
{
    road_sample_t p = {0};
    float w = g.half_width;

    if (g.want_width_ramp && s >= g.ramp_s && s < g.ramp_s + ROAD_WIDTH_RAMP)
        w = g.half_width + (g.ramp_to - g.half_width) * (s - g.ramp_s) /
                               ROAD_WIDTH_RAMP;
    else if (g.want_width_ramp && s >= g.ramp_s + ROAD_WIDTH_RAMP)
        w = g.ramp_to;
    p.x = 0.0f;
    p.z = s;
    p.heading = 0.0f;
    p.curvature = 0.0f;
    p.height = g.base_height + s * g.grade;
    if (g.block && s > g.block_from && s < g.block_to)
        p.height += g.block_add;
    p.grade = g.grade;
    p.grade_rate = 0.0f;
    p.half_width = w;
    p.edge_half_width = w + ROAD_SHOULDER_WIDTH;
    return p;
}

int road_segment_count(void)
{
    return g_seg_count;
}

const road_segment_t *road_segment_at(int i)
{
    return (i >= 0 && i < g_seg_count) ? &g_seg[i] : 0;
}

bool road_next_curve(float from_s, float ahead, float min_k, float *d, float *k)
{
    if (!g.curve || fabsf(g.curve_k) < min_k)
        return false;
    if ((g.curve_s - from_s) > ahead)
        return false;
    if (g.curve_s < from_s + 1.0f)
        return false;
    if (d)
        *d = g.curve_s - from_s;
    if (k)
        *k = g.curve_k;
    return true;
}

bool road_next_crest(float from_s, float ahead, float max_rate, float *d,
                     float *r)
{
    (void)max_rate;
    if (!g.crest)
        return false;
    if ((g.crest_s - from_s) > ahead)
        return false;
    if (g.crest_s < from_s + 1.0f)
        return false;
    if (d)
        *d = g.crest_s - from_s;
    if (r)
        *r = g.crest_rate;
    return true;
}

bool road_next_narrow(float from_s, float ahead, float max_half, float *d,
                      float *h)
{
    (void)from_s;
    (void)ahead;
    (void)max_half;
    (void)d;
    (void)h;
    fprintf(stderr, "FAIL: road_next_narrow called (car_s-relative type gate)\n");
    failures++;
    return false;
}

int road_lane_count(float half)
{
    if (half < ROAD_LANE_HALF_1)
        return 1;
    if (half < ROAD_LANE_HALF_2)
        return 2;
    if (half < ROAD_LANE_HALF_3)
        return 3;
    return 4;
}

/* --- Task 1: placement ------------------------------------------------ */

static void test_crest_pair(void)
{
    sign_t s[SIGNS_MAX];
    int n;

    memset(&g, 0, sizeof g);
    g.half_width = 5.0f;
    g.crest = true;
    g.crest_s = 173.0f;
    g.crest_rate = -0.00923f;
    g.base_height = 1.3667f; /* real road height at s = 52.28 */
    g_seg_count = 1;
    g_seg[0].start_s = 172.28f;
    g_seg[0].grade_rate = -0.00923f;
    n = signs_place(0.0f, 0.0f, 0.0f, s, SIGNS_MAX);
    CHECK(n == 2);
    CHECK(s[0].type == SIGNS_CREST && s[1].type == SIGNS_CREST);
    CHECK(NEAR(s[0].s, 52.28f, 0.01f)); /* 172.28 - 120, the segment start */
    CHECK(NEAR(s[1].s, 112.28f, 0.01f)); /* 172.28 - 60 */
    CHECK(NEAR(s[0].lateral, 11.9f, 0.01f)); /* 9.5 + 2.4 */
    CHECK(NEAR(s[0].road_height, 1.3667f, 0.001f)); /* 52.28^2/2000 */
    CHECK(signs_place(0.0f, 0.0f, 0.0f, s, 1) == 1); /* truncates */
}

static void test_no_drift(void) /* sign positions are fixed while visible */
{
    sign_t a[SIGNS_MAX], b[SIGNS_MAX];
    float car;
    int n1, n2;

    memset(&g, 0, sizeof g);
    g.half_width = 5.0f;
    g.crest = true;
    g.crest_s = 173.0f;
    g_seg_count = 1;
    g_seg[0].start_s = 172.28f;
    g_seg[0].grade_rate = -0.00923f;
    for (car = 0.0f; car <= 100.0f; car += 5.0f) {
        int n = signs_place(car, 20.0f, 0.0f, a, SIGNS_MAX);
        int i;

        for (i = 0; i < n; i++)
            CHECK(NEAR(a[i].s, 52.28f, 0.01f) ||
                  NEAR(a[i].s, 112.28f, 0.01f));
    }
    /* the same car_s reached forward or backward gives the same list */
    n1 = signs_place(40.0f, 20.0f, 0.0f, a, SIGNS_MAX);
    n2 = signs_place(40.0f, -20.0f, 0.0f, b, SIGNS_MAX);
    CHECK(n1 == n2 && memcmp(a, b, (size_t)n1 * sizeof(sign_t)) == 0);
}

static void test_window_and_reverse(void)
{
    sign_t s[SIGNS_MAX];
    float car;

    memset(&g, 0, sizeof g);
    g.half_width = 5.0f;
    g.crest = true;
    g.crest_s = 173.0f;
    g_seg_count = 1;
    g_seg[0].start_s = 172.28f;
    g_seg[0].grade_rate = -0.00923f;
    CHECK(signs_place(110.0f, 20.0f, 0.0f, s, SIGNS_MAX) == 0); /* 2.3 ahead */
    CHECK(signs_place(100.0f, 20.0f, 0.0f, s, SIGNS_MAX) == 1); /* 12.3 ahead */
    CHECK(signs_place(100.0f, -8.0f, 0.0f, s, SIGNS_MAX) == 1);
    CHECK(s[0].s > 100.0f); /* never behind */
    for (car = 0.0f; car <= 1000.0f; car += 7.0f) {
        int n = signs_place(car, 20.0f, 0.0f, s, SIGNS_MAX);
        int i;

        for (i = 0; i < n; i++) {
            CHECK(s[i].s - car >= SIGNS_MIN_AHEAD - 0.01f);
            CHECK(s[i].s - car <= SIGNS_MAX_AHEAD + 0.01f);
        }
    }
}

static void test_dedupe(void)
{
    sign_t s[SIGNS_MAX];
    int n, i;

    memset(&g, 0, sizeof g);
    g.half_width = 5.0f;
    g.curve = true;
    g.curve_s = 140.0f;
    g.curve_k = 0.03f;
    g.crest = true;
    g.crest_s = 131.0f;
    g_seg_count = 1;
    g_seg[0].start_s = 130.0f;
    g_seg[0].grade_rate = -0.00923f;
    n = signs_place(0.0f, 20.0f, 0.0f, s, SIGNS_MAX);
    /* candidates 10, 70 (crest) and 20, 80 (curve); 20 and 80 are 10 m from a
     * kept sign, so the overlapping pair collapses to one warning pair. */
    CHECK(n == 2);
    CHECK(NEAR(s[0].s, 10.0f, 0.01f));
    CHECK(NEAR(s[1].s, 70.0f, 0.01f));
    for (i = 1; i < n; i++)
        CHECK(s[i].s - s[i - 1].s >= SIGNS_MIN_GAP - 0.01f);
}

static void test_curve_and_onelane(void)
{
    sign_t s[SIGNS_MAX];
    sign_t a[SIGNS_MAX], b[SIGNS_MAX];
    int n, na, nb, i;

    memset(&g, 0, sizeof g);
    g_seg_count = 0;
    g.half_width = 5.0f;
    g.curve = true;
    g.curve_s = 300.0f;
    g.curve_k = 0.03f;
    CHECK(signs_place(100.0f, 20.0f, 0.0f, s, SIGNS_MAX) == 2);
    CHECK(s[0].type == SIGNS_CURVE_RIGHT && NEAR(s[0].s, 180.0f, 0.01f));
    CHECK(NEAR(s[1].s, 240.0f, 0.01f));
    g.curve_k = -0.03f;
    signs_place(100.0f, 20.0f, 0.0f, s, SIGNS_MAX);
    CHECK(s[0].type == SIGNS_CURVE_LEFT);

    /* 5.0 -> 2.2 m over 30 m starting at 470: the ramp start is the width
     * event (fixed 2 m lattice, 470), so the panels are 350 / 410. Plateau
     * half 2.2 -> road_lane_count == 1 and 2.2 <= 3.0 -> ONELANE. The type
     * comes from the event only; the stub road_next_narrow would increment
     * `failures` if the model ever called it. */
    memset(&g, 0, sizeof g);
    g.half_width = 5.0f;
    g.want_width_ramp = true;
    g.ramp_s = 470.0f;
    g.ramp_to = 2.2f;
    n = signs_place(300.0f, 20.0f, 0.0f, s, SIGNS_MAX);
    CHECK(n == 2);
    CHECK(s[0].type == SIGNS_ONELANE && s[1].type == SIGNS_ONELANE);
    CHECK(NEAR(s[0].s, 350.0f, 0.01f));
    CHECK(NEAR(s[1].s, 410.0f, 0.01f));

    /* the same event seen from two car_s values emits the same positions AND
     * the same type: no face pop while the panel is on screen */
    na = signs_place(300.0f, 20.0f, 0.0f, a, SIGNS_MAX);
    nb = signs_place(360.0f, 20.0f, 0.0f, b, SIGNS_MAX);
    CHECK(na >= 1 && nb >= 1);
    CHECK(a[0].type == b[0].type); /* ONELANE both times */
    for (i = 0; i < na; i++)
        CHECK(a[i].s > 300.0f);
    for (i = 0; i < nb; i++)
        CHECK(b[i].s > 360.0f);

    /* 6.0 m one-lane setting: total 6.0 -> half 3.0, road_lane_count(3.0) == 2.
     * A two-lane road must never show a one-lane panel, even though 3.0 m is
     * exactly ROAD_NARROW_WARN_HALF. */
    g.ramp_to = 3.0f;
    n = signs_place(300.0f, 20.0f, 0.0f, s, SIGNS_MAX);
    CHECK(n == 2);
    CHECK(s[0].type == SIGNS_MERGE && s[0].lanes == 2);
}

static void test_three_events_bound(void)
{
    sign_t s[SIGNS_MAX];
    int n, i;

    memset(&g, 0, sizeof g);
    g.half_width = 5.0f;
    g.curve = true;
    g.curve_s = 200.0f;
    g.curve_k = 0.03f;
    g.crest = true;
    g.crest_s = 250.0f;
    g_seg_count = 1;
    g_seg[0].start_s = 250.0f;
    g_seg[0].grade_rate = -0.00923f;
    g.want_width_ramp = true;
    g.ramp_s = 300.0f;
    g.ramp_to = 8.0f;
    n = signs_place(0.0f, 20.0f, 0.0f, s, SIGNS_MAX);
    CHECK(n > 0 && n <= SIGNS_MAX_DRAWN); /* 3 event kinds x 2 = 6 */
    for (i = 1; i < n; i++)
        CHECK(s[i].s > s[i - 1].s);
}

static void test_occlusion_cull(void)
{
    sign_t s[SIGNS_MAX];
    int n;

    memset(&g, 0, sizeof g);
    g.half_width = 5.0f;
    g.crest = true;
    g.crest_s = 350.0f;
    g_seg_count = 1;
    g_seg[0].start_s = 350.0f;
    g_seg[0].grade_rate = -0.00923f;
    /* car 100, speed 0: eye_s = 91.8, eye_y = 3.1; the crest pair is at 230
     * and 290, both inside 8..240. */
    CHECK(signs_place(100.0f, 0.0f, 0.0f, s, SIGNS_MAX) == 2);
    /* A 1.6 m rise over 175..195 m blocks ONLY the bottom-edge line of the
     * 230 m sign. Samples are at 101.8 + 10k, so the two inside the rise are
     * s = 181.8 and 191.8: the near sign's bottom line to (230, 0.6) is
     * 1.47 m / 1.29 m, its centre line to (230, 1.5) is 2.06 m / 1.94 m, and
     * the 290 m sign's bottom line is 1.97 m / 1.84 m; threshold = 1.65 m.
     * Bottom-edge culling must therefore drop the near sign only (margins
     * 0.18 m and 0.19 m). */
    g.block = true;
    g.block_from = 175.0f;
    g.block_to = 195.0f;
    g.block_add = 1.6f;
    n = signs_place(100.0f, 0.0f, 0.0f, s, SIGNS_MAX);
    CHECK(n == 1);
    CHECK(NEAR(s[0].s, 290.0f, 0.01f));
    /* a 100 m wall blocks every sight line -> nothing */
    g.block_add = 100.0f;
    CHECK(signs_place(100.0f, 0.0f, 0.0f, s, SIGNS_MAX) == 0);
}

static void test_width_events(void)
{
    sign_t s[SIGNS_MAX];
    int n;

    memset(&g, 0, sizeof g);
    g_seg_count = 0;
    g.half_width = 5.0f;
    g.want_width_ramp = true;
    g.ramp_s = 300.0f;
    g.ramp_to = 8.0f;
    /* car at 100: widening ramp 200 m ahead -> WIDEN, lanes 4, signs 180/240.
     * The scan is anchored on the fixed 2 m lattice, so the event is exactly
     * 300 and the panels exactly 180/240. */
    n = signs_place(100.0f, 20.0f, 0.0f, s, SIGNS_MAX);
    CHECK(n == 2 && s[0].type == SIGNS_WIDEN && s[0].lanes == 4);
    CHECK(NEAR(s[0].s, 180.0f, 0.01f) && NEAR(s[1].s, 240.0f, 0.01f));
    /* odd car_s: a car_s-relative scan would sample 101.5, 103.5, ... and
     * find the ramp at 299.5 (panels 179.5/239.5); the fixed lattice must
     * still give 300 -> 180/240. */
    n = signs_place(100.5f, 20.0f, 0.0f, s, SIGNS_MAX);
    CHECK(n == 2);
    CHECK(NEAR(s[0].s, 180.0f, 0.01f) && NEAR(s[1].s, 240.0f, 0.01f));
    g.ramp_s = 260.0f;
    g.ramp_to = 2.0f;
    /* a narrowing ramp at 260 -> event 260, t = w(290) = 2.0, lanes 1, so the
     * type is ONELANE (the same pure rule as the 470/2.2 case above). */
    n = signs_place(100.0f, 20.0f, 0.0f, s, SIGNS_MAX);
    CHECK(n >= 1 && s[0].type == SIGNS_ONELANE && s[0].lanes == 1);
}

/* --- Task 2: panel frame and corners --------------------------------- */

static sign_t flat_sign(void)
{
    sign_t s;

    memset(&s, 0, sizeof s);
    s.type = SIGNS_CREST;
    s.s = 53.0f;
    s.lateral = 11.9f;
    s.road_height = 1.4045f;
    s.heading = 0.0f;
    s.grade = 0.0f;
    return s;
}

static void test_panel_corners_flat(void)
{
    sign_t s = flat_sign();
    sign_vec3_t c, r, u, n, q[4];
    int i;

    signs_panel_axes(&s, &c, &r, &u, &n);
    CHECK(NEAR(c.x, -11.9f, 1e-4f));
    CHECK(NEAR(c.y, 2.9045f, 1e-4f));
    CHECK(NEAR(c.z, 53.0f, 1e-4f));
    CHECK(NEAR(r.x, -1.0f, 1e-4f));
    CHECK(NEAR(u.y, 1.0f, 1e-4f));
    CHECK(NEAR(n.z, -1.0f, 1e-4f)); /* faces the driver */
    CHECK(signs_panel_corners(&s, q));
    CHECK(NEAR(q[0].x, -10.7f, 1e-4f) && NEAR(q[0].y, 3.8045f, 1e-4f));
    CHECK(NEAR(q[1].x, -13.1f, 1e-4f) && NEAR(q[1].y, 3.8045f, 1e-4f));
    CHECK(NEAR(q[2].x, -13.1f, 1e-4f) && NEAR(q[2].y, 2.0045f, 1e-4f));
    CHECK(NEAR(q[3].x, -10.7f, 1e-4f) && NEAR(q[3].y, 2.0045f, 1e-4f));
    for (i = 0; i < 4; i++)
        CHECK(NEAR(q[i].z, 53.0f, 1e-4f));
}

static void test_panel_follows_grade(void)
{
    sign_t s = flat_sign();
    sign_vec3_t c, r, u, n, q[4];

    s.grade = 0.12f;
    signs_panel_axes(&s, &c, &r, &u, &n);
    CHECK(NEAR(u.y, 0.99288f, 1e-3f));
    CHECK(NEAR(u.z, -0.11912f, 1e-3f));
    /* normal = -T3; at psi=0 and grade 0.12 it points along -Z and down */
    CHECK(NEAR(n.y, -0.11912f, 1e-3f));
    CHECK(NEAR(n.z, -0.99288f, 1e-3f));
    CHECK(signs_panel_corners(&s, q));
    CHECK(NEAR(q[0].y - q[3].y, 1.7872f, 1e-3f));
    CHECK(NEAR(q[0].z - q[3].z, -0.2144f, 1e-3f));
}

/* --- Task 3: symbol geometry ----------------------------------------- */

static void local(const sign_t *s, sign_vec3_t p, float *u, float *v)
{
    sign_vec3_t c, r, up, n;
    float dx, dy, dz;

    signs_panel_axes(s, &c, &r, &up, &n);
    dx = p.x - c.x;
    dy = p.y - c.y;
    dz = p.z - c.z;
    *u = (dx * r.x + dy * r.y + dz * r.z) / 1.2f;
    *v = (dx * up.x + dy * up.y + dz * up.z) / 0.9f;
}

static float quad_extreme_u(const sign_t *s, const sign_quad_t *q, int n,
                            int want_max)
{
    float best = want_max ? -100.0f : 100.0f;
    int i;

    for (i = 0; i < n; i++) {
        const sign_vec3_t *pts[4];
        int k;

        pts[0] = &q[i].a;
        pts[1] = &q[i].b;
        pts[2] = &q[i].c;
        pts[3] = &q[i].d;
        for (k = 0; k < 4; k++) {
            float u, v;

            local(s, *pts[k], &u, &v);
            if (want_max ? u > best : u < best)
                best = u;
        }
    }
    return best;
}

static void check_symbol_bounds(const sign_t *s, const sign_quad_t *q, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        const sign_vec3_t *pts[4];
        int k;

        pts[0] = &q[i].a;
        pts[1] = &q[i].b;
        pts[2] = &q[i].c;
        pts[3] = &q[i].d;
        for (k = 0; k < 4; k++) {
            float u, v;

            local(s, *pts[k], &u, &v);
            CHECK(isfinite(u) && isfinite(v));
            CHECK(fabsf(u) <= 1.001f);
            CHECK(fabsf(v) <= 1.001f);
        }
    }
}

static void test_symbols(void)
{
    sign_t s = flat_sign();
    sign_quad_t q[8];
    int n;

    s.type = SIGNS_CREST;
    n = signs_symbol_quads(&s, q, 8);
    CHECK(n == 3);
    check_symbol_bounds(&s, q, n);

    /* bar() gives CURVE_RIGHT u in [-0.64715, +0.54715]; the apex is +u */
    s.type = SIGNS_CURVE_RIGHT;
    n = signs_symbol_quads(&s, q, 8);
    CHECK(n == 2);
    check_symbol_bounds(&s, q, n);
    CHECK(quad_extreme_u(&s, q, n, 1) > 0.54f);
    CHECK(quad_extreme_u(&s, q, n, 0) < -0.64f);

    s.type = SIGNS_CURVE_LEFT;
    n = signs_symbol_quads(&s, q, 8);
    CHECK(n == 2);
    check_symbol_bounds(&s, q, n);
    CHECK(quad_extreme_u(&s, q, n, 1) > 0.64f);
    CHECK(quad_extreme_u(&s, q, n, 0) < -0.54f); /* mirrored */

    s.type = SIGNS_MERGE;
    s.lanes = 1;
    CHECK(signs_symbol_quads(&s, q, 8) == 3);
    s.lanes = 4;
    n = signs_symbol_quads(&s, q, 8);
    CHECK(n == 6);
    check_symbol_bounds(&s, q, n);

    s.type = SIGNS_WIDEN;
    s.lanes = 4;
    n = signs_symbol_quads(&s, q, 8);
    CHECK(n == 6);
    check_symbol_bounds(&s, q, n);

    s.type = SIGNS_ONELANE;
    n = signs_symbol_quads(&s, q, 8);
    CHECK(n == 3);
    check_symbol_bounds(&s, q, n);
    /* symbol quads sit 0.02 m in front of the panel plane */
    CHECK(q[0].a.z < 53.0f && q[0].a.z > 52.9f);

    /* a too-small output buffer is refused, not overflowed */
    CHECK(signs_symbol_quads(&s, q, 1) == 0);
}

static void test_bad_inputs(void)
{
    sign_t s = flat_sign();
    sign_t list[SIGNS_MAX];
    sign_vec3_t q[4];
    sign_quad_t quads[8];

    /* non-finite sign data is rejected by the corner builder */
    s.grade = NAN;
    CHECK(!signs_panel_corners(&s, q));
    s = flat_sign();
    s.lateral = NAN;
    CHECK(!signs_panel_corners(&s, q));
    s = flat_sign();
    CHECK(!signs_panel_corners(&s, NULL));

    /* a non-positive capacity is refused, not written to */
    CHECK(signs_symbol_quads(&s, quads, 0) == 0);
    CHECK(signs_symbol_quads(&s, quads, -1) == 0);
    CHECK(signs_symbol_quads(NULL, quads, 8) == 0);
    CHECK(signs_place(0.0f, 0.0f, 0.0f, list, 0) == 0);
    CHECK(signs_place(0.0f, 0.0f, 0.0f, NULL, SIGNS_MAX) == 0);
}

int main(void)
{
    test_crest_pair();
    test_curve_and_onelane();
    test_no_drift();
    test_window_and_reverse();
    test_dedupe();
    test_occlusion_cull();
    test_width_events();
    test_three_events_bound();
    test_panel_corners_flat();
    test_panel_follows_grade();
    test_symbols();
    test_bad_inputs();
    return failures ? 1 : 0;
}
