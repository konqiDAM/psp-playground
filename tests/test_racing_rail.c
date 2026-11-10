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
    test_vertex_budget();

    if (failures) {
        fprintf(stderr, "%d guardrail test(s) failed\n", failures);
        return 1;
    }
    printf("all guardrail tests passed\n");
    return 0;
}
