/*
 * test_racing_rail.c - host tests for the portable guardrail model.
 *
 * Runs with a normal C99 compiler and links only
 * racing/assets/guardrail/guardrail.c: no road_* or car_* symbols.
 */

#include <math.h>
#include <stdio.h>

#include "assets/guardrail/guardrail.h"

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static int nearly(float a, float b, float epsilon)
{
    return fabsf(a - b) <= epsilon;
}

static void test_envelope_ends_and_plateau(void)
{
    const float s0 = 100.0f, s1 = 200.0f;

    CHECK(nearly(guardrail_envelope(s0, s0, s1), 0.0f, 1e-6f));
    CHECK(nearly(guardrail_envelope(s1, s0, s1), 0.0f, 1e-6f));
    CHECK(nearly(guardrail_envelope(s0 + 12.0f, s0, s1), 1.0f, 1e-6f));
    CHECK(nearly(guardrail_envelope(s1 - 12.0f, s0, s1), 1.0f, 1e-6f));
    CHECK(nearly(guardrail_envelope(150.0f, s0, s1), 1.0f, 1e-6f));
    CHECK(nearly(guardrail_beam_top(s0, s0, s1), -0.35f, 1e-5f));
    CHECK(nearly(guardrail_beam_top(150.0f, s0, s1), 0.75f, 1e-5f));
    CHECK(nearly(guardrail_beam_bottom(150.0f, s0, s1), 0.45f, 1e-5f));
    CHECK(guardrail_beam_top(s0, s0, s1) < 0.0f);
    CHECK(guardrail_beam_bottom(s0, s0, s1) < 0.0f);
}

static void test_envelope_is_monotone_and_bounded(void)
{
    const float s0 = 0.0f, s1 = 100.0f;
    const float step = 0.1f;
    const float max_delta = 0.1375f * step + 1e-4f;
    float s;
    float prev;

    prev = guardrail_beam_top(s0, s0, s1);
    for (s = s0 + step; s <= s0 + 12.0f; s += step) {
        float top = guardrail_beam_top(s, s0, s1);

        CHECK(top >= prev - 1e-5f);        /* rising */
        CHECK(top - prev <= max_delta);    /* never steeper than 13.75 % */
        prev = top;
    }
    /* The 0.1 m float grid stops at 11.9 m, where top is 0.74978; the exact
     * 12 m value (0.75) is asserted in test_envelope_ends_and_plateau. */
    CHECK(prev > 0.7495f);                 /* essentially full height */

    prev = guardrail_beam_top(s1 - 12.0f, s0, s1);
    for (s = s1 - 12.0f + step; s <= s1; s += step) {
        float top = guardrail_beam_top(s, s0, s1);

        CHECK(top <= prev + 1e-5f);        /* sinking */
        CHECK(prev - top <= max_delta);
        prev = top;
    }
    CHECK(prev < 0.0f);                    /* closed: below the road */
}

static void test_posts_and_bands(void)
{
    CHECK(guardrail_band_count(0.0f, 100.0f) == 20);
    CHECK(guardrail_band_count(0.0f, 74.7f) == 15);
    CHECK(!guardrail_post_ok(0.0f, 0.0f, 100.0f));   /* buried end */
    CHECK(guardrail_post_ok(8.0f, 0.0f, 100.0f));
    CHECK(!guardrail_post_ok(96.0f, 0.0f, 100.0f));  /* sinking end */
    CHECK(guardrail_post_count(0.0f, 100.0f) == 11);
    CHECK(nearly(guardrail_post_s(0.0f, 100.0f, 0), 0.0f, 1e-6f));
    CHECK(nearly(guardrail_post_s(0.0f, 100.0f, 1), 8.0f, 1e-6f));
}

static void test_span_selection_keeps_the_near_end(void)
{
    guardrail_span_ref_t spans[4];
    unsigned char keep[4];
    int cost;
    int total;
    int i;

    cost = guardrail_span_vertex_cost(0.0f, 100.0f);
    CHECK(cost == 20 * 12 + 11 * 6);            /* 306 */

    /* Ascending s0: index 0 is nearest the car, index 3 the farthest ahead. */
    for (i = 0; i < 4; i++) {
        spans[i].s0 = 1000.0f + 120.0f * (float)i;
        spans[i].s1 = spans[i].s0 + 100.0f;
        spans[i].v0 = spans[i].s0;
        spans[i].v1 = spans[i].s1;
        spans[i].side = (i & 1) ? 1 : -1;
        spans[i].kind = 1;
    }

    /* Budget fits three spans plus the flash reserve: the far end must go. */
    total = guardrail_select_spans(spans, 4, keep, 3 * cost + 6, 6, 900.0f);
    CHECK(total == 3 * cost);
    CHECK(keep[0] == 1 && keep[1] == 1 && keep[2] == 1 && keep[3] == 0);

    /* Input order must not matter: the three lowest s0 survive. */
    for (i = 0; i < 4; i++) {
        spans[i].s0 = 1000.0f + 120.0f * (float)(3 - i); /* descending */
        spans[i].s1 = spans[i].s0 + 100.0f;
        spans[i].v0 = spans[i].s0;
        spans[i].v1 = spans[i].s1;
    }
    total = guardrail_select_spans(spans, 4, keep, 3 * cost + 6, 6, 900.0f);
    CHECK(total == 3 * cost);
    CHECK(keep[0] == 0); /* s0 1360, the far end */
    CHECK(keep[1] == 1 && keep[2] == 1 && keep[3] == 1);

    /* A list that fits comes back whole, every flag set. */
    total = guardrail_select_spans(spans, 4, keep, 4 * cost, 0, 900.0f);
    CHECK(total == 4 * cost);
    for (i = 0; i < 4; i++)
        CHECK(keep[i] == 1);

    /* Reserve is honoured: one vertex short of three spans leaves two. */
    for (i = 0; i < 4; i++) {
        spans[i].s0 = 1000.0f + 120.0f * (float)i;
        spans[i].s1 = spans[i].s0 + 100.0f;
        spans[i].v0 = spans[i].s0;
        spans[i].v1 = spans[i].s1;
    }
    total = guardrail_select_spans(spans, 4, keep, 3 * cost + 5, 6, 900.0f);
    CHECK(total == 2 * cost);
    CHECK(keep[0] == 1 && keep[1] == 1);
    CHECK(keep[2] == 0 && keep[3] == 0);

    /* Spans ending behind the eye are never visible and must never outrank a
     * visible one, even though their s0 is the lowest. The eye sits at
     * car_s - (8.2 + 0.035 v), well inside the collector's 90 m behind. */
    for (i = 0; i < 4; i++) {
        spans[i].s0 = 1000.0f + 120.0f * (float)i;
        spans[i].s1 = spans[i].s0 + 100.0f;
        spans[i].v0 = spans[i].s0;
        spans[i].v1 = spans[i].s1;
    }
    total = guardrail_select_spans(spans, 4, keep, 3 * cost + 6, 6, 1125.0f);
    CHECK(total == 3 * cost);
    CHECK(keep[0] == 0);                    /* ends at 1100, behind the eye */
    CHECK(keep[1] == 1 && keep[2] == 1 && keep[3] == 1);

    /* Nothing visible at all: no vertices, and every flag cleared. */
    total = guardrail_select_spans(spans, 4, keep, 4 * cost, 0, 1500.0f);
    CHECK(total == 0);
    for (i = 0; i < 4; i++)
        CHECK(keep[i] == 0);

    /* A span straddling the eye stays: only s1 <= eye_s is invisible. */
    total = guardrail_select_spans(spans, 4, keep, 4 * cost, 0, 1050.0f);
    CHECK(total == 4 * cost);               /* span 0 ends at 1100 > 1050 */
    CHECK(keep[0] == 1);

    /* Exactly at the eye still counts as behind (the camera plane). */
    total = guardrail_select_spans(spans, 4, keep, 4 * cost, 0, 1100.0f);
    CHECK(total == 3 * cost);
    CHECK(keep[0] == 0);
}

static void test_rail_plane_is_the_asphalt_edge(void)
{
    /* D2: the plane is the ASPHALT edge at half_width, not the shoulder edge
     * (edge_half_width) the beam used to sit on; the post is 0.02 m outboard.
     * A 0.1 m sweep over every width the grammar can produce. */
    float h;

    CHECK(nearly(guardrail_rail_lateral(5.0f), 5.0f, 1e-6f));
    CHECK(nearly(guardrail_post_lateral(5.0f) - guardrail_rail_lateral(5.0f),
                 0.02f, 1e-6f));
    for (h = 0.5f; h <= 20.0f + 1e-3f; h += 0.1f) {
        CHECK(nearly(guardrail_rail_lateral(h), h, 1e-6f));
        CHECK(nearly(guardrail_post_lateral(h) - guardrail_rail_lateral(h),
                     GUARDRAIL_POST_OFFSET, 1e-6f));
    }
}

static void test_band_range_is_the_visible_interval(void)
{
    /* The renderer's iteration range, host-tested. A renderer that iterates
     * the grammar span [s0, s1] starts the beam 6.36 m before the clamp
     * exists (an invisible wall) and the pixel probe cannot see it: that is
     * ~3 % of the corridor's rail pixels, inside the probe's band. */
    guardrail_span_ref_t span;
    float from = -1.0f;
    float to = -1.0f;

    span.s0 = 100.0f;
    span.s1 = 400.0f;
    span.v0 = 106.36f;
    span.v1 = 393.64f;
    span.side = -1;
    span.kind = 1;
    guardrail_band_range(&span, &from, &to);
    CHECK(nearly(from, 106.36f, 1e-6f));
    CHECK(nearly(to, 393.64f, 1e-6f));
    CHECK(from > span.s0 + 6.0f);            /* not the grammar start */
    CHECK(to < span.s1 - 6.0f);              /* not the grammar end */
    CHECK(guardrail_band_count(from, to) == 58);   /* ceil(287.28 / 5) */

    /* a visible interval wider than the grammar span is clamped to it */
    span.v0 = 90.0f;
    span.v1 = 410.0f;
    guardrail_band_range(&span, &from, &to);
    CHECK(nearly(from, 100.0f, 1e-6f));
    CHECK(nearly(to, 400.0f, 1e-6f));

    /* an empty run collapses to a point, never to a negative range */
    span.v0 = 250.0f;
    span.v1 = 250.0f;
    guardrail_band_range(&span, &from, &to);
    CHECK(nearly(from, 250.0f, 1e-6f));
    CHECK(nearly(to, 250.0f, 1e-6f));
    CHECK(guardrail_band_count(from, to) == 0);
}

static void test_split_runs_share_one_grammar_span(void)
{
    /* A split emits one entry per visible run with the same (s0, s1, side):
     * the renderer must charge and draw the posts ONCE, not per run. */
    guardrail_span_ref_t a;
    guardrail_span_ref_t b;
    guardrail_span_ref_t c;

    a.s0 = 100.0f; a.s1 = 400.0f; a.v0 = 106.0f; a.v1 = 200.0f;
    a.side = -1; a.kind = 1;
    b = a;
    b.v0 = 260.0f;
    b.v1 = 393.0f;
    CHECK(guardrail_span_continues(&a, &b));
    /* a different grammar span, a different side, or a new list all restart */
    c = b;
    c.s0 = 500.0f;
    c.s1 = 700.0f;
    CHECK(!guardrail_span_continues(&b, &c));
    c = b;
    c.side = 1;
    CHECK(!guardrail_span_continues(&b, &c));
    CHECK(!guardrail_span_continues(NULL, &b));
    CHECK(!guardrail_span_continues(&a, NULL));
}

static void test_split_runs_charge_posts_once(void)
{
    /* Two visible runs of ONE grammar span: the bands are charged per run and
     * the posts once, because the renderer draws them once. Charging the
     * grammar span per run would count every post twice and shrink the
     * vertex budget for geometry that is never emitted. */
    guardrail_span_ref_t spans[2];
    unsigned char keep[2];
    int bands_a;
    int bands_b;
    int posts;
    int total;

    spans[0].s0 = 100.0f;
    spans[0].s1 = 400.0f;
    spans[0].v0 = 106.0f;
    spans[0].v1 = 200.0f;
    spans[0].side = -1;
    spans[0].kind = 1;
    spans[1] = spans[0];
    spans[1].v0 = 260.0f;
    spans[1].v1 = 393.0f;

    bands_a = guardrail_band_count(spans[0].v0, spans[0].v1);
    bands_b = guardrail_band_count(spans[1].v0, spans[1].v1);
    posts = guardrail_post_count(spans[0].s0, spans[0].s1);
    total = guardrail_select_spans(spans, 2, keep, 100000, 0, 0.0f);
    CHECK(keep[0] == 1 && keep[1] == 1);
    CHECK(total == (bands_a + bands_b) * GUARDRAIL_VERTS_PER_BAND +
                   posts * GUARDRAIL_VERTS_PER_POST);
    /* and it is NOT the two-grammar-span cost the split used to charge */
    CHECK(total < 2 * guardrail_span_vertex_cost(spans[0].s0, spans[0].s1));
}

static void test_vertex_budget(void)
{
    /* Independent constant; road.h is deliberately not included here. */
    CHECK(GUARDRAIL_MAX_SPANS == 48);
    CHECK(GUARDRAIL_FLASH_VERTS == 6);
    /* Worst visible range: car-90..car+410 plus 20 m lead / 15 m trail. */
    CHECK(guardrail_vertex_cost(-110.0f, 425.0f) == 3360);
    CHECK(guardrail_vertex_cost(-110.0f, 425.0f) + GUARDRAIL_FLASH_VERTS <=
          GUARDRAIL_MAX_VERTS);
    CHECK(guardrail_vertex_cost(0.0f, 100.0f) == 20 * 12 * 2 + 11 * 6 * 2);
}

int main(void)
{
    test_envelope_ends_and_plateau();
    test_envelope_is_monotone_and_bounded();
    test_posts_and_bands();
    test_span_selection_keeps_the_near_end();
    test_rail_plane_is_the_asphalt_edge();
    test_band_range_is_the_visible_interval();
    test_split_runs_share_one_grammar_span();
    test_split_runs_charge_posts_once();
    test_vertex_budget();

    if (failures) {
        fprintf(stderr, "%d guardrail test(s) failed\n", failures);
        return 1;
    }
    printf("all guardrail tests passed\n");
    return 0;
}
