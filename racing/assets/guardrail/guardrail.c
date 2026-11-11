/*
 * guardrail.c - portable guardrail model for Racing 3D.
 *
 * Beam envelope, band and post layout, and the vertex budget. No PSP headers
 * and no road_* symbols: tests/test_racing_rail.c links this file alone.
 */

#include "assets/guardrail/guardrail.h"

#include <math.h>

int guardrail_band_count(float s0, float s1)
{
    float len = s1 - s0;

    if (len <= 0.0f)
        return 0;
    return (int)ceilf(len / GUARDRAIL_ROW_STEP);
}

void guardrail_band_range(const guardrail_span_ref_t *span, float *from,
                          float *to)
{
    float a;
    float b;

    if (!span)
        return;
    a = span->v0;
    b = span->v1;
    if (a < span->s0)
        a = span->s0;
    if (b > span->s1)
        b = span->s1;
    if (b < a)
        b = a;
    if (from)
        *from = a;
    if (to)
        *to = b;
}

bool guardrail_span_continues(const guardrail_span_ref_t *prev,
                              const guardrail_span_ref_t *cur)
{
    if (!prev || !cur)
        return false;
    return prev->s0 == cur->s0 && prev->s1 == cur->s1 &&
           prev->side == cur->side;
}

/* The beam plane is the asphalt edge (the white line) and the post is 0.02 m
 * outboard of it: Part D's whole point is that the rail sits on the asphalt,
 * not out on the shoulder edge. */
float guardrail_rail_lateral(float half_width)
{
    return half_width;
}

float guardrail_post_lateral(float half_width)
{
    return half_width + GUARDRAIL_POST_OFFSET;
}

float guardrail_post_s(float s0, float s1, int index)
{
    (void)s1; /* the grid is anchored at s0 */

    if (index < 0)
        return s0;
    return s0 + (float)index * GUARDRAIL_POST_SPACING;
}

int guardrail_post_count(float s0, float s1)
{
    int last;
    int i;
    int n = 0;

    if (s1 <= s0)
        return 0;
    last = (int)floorf((s1 - s0) / GUARDRAIL_POST_SPACING);
    for (i = 0; i <= last; i++) {
        if (guardrail_post_ok(guardrail_post_s(s0, s1, i), s0, s1))
            n++;
    }
    return n;
}

int guardrail_span_vertex_cost(float s0, float s1)
{
    return guardrail_band_count(s0, s1) * GUARDRAIL_VERTS_PER_BAND +
           guardrail_post_count(s0, s1) * GUARDRAIL_VERTS_PER_POST;
}

int guardrail_vertex_cost(float s0, float s1)
{
    return guardrail_span_vertex_cost(s0, s1) * 2; /* both sides */
}

int guardrail_select_spans(const guardrail_span_ref_t *spans, int count,
                           unsigned char *keep, int budget, int reserve,
                           float eye_s)
{
    int order[GUARDRAIL_MAX_SPANS];
    int ranked = 0;
    int limit;
    int used = 0;
    int i;
    int j;

    if (!spans || !keep || count <= 0)
        return 0;
    /* Clear before any early return: a caller must never read a stale flag. */
    for (i = 0; i < count && i < GUARDRAIL_MAX_SPANS; i++)
        keep[i] = 0;
    if (count > GUARDRAIL_MAX_SPANS)
        return 0;

    limit = budget - reserve;
    if (limit < 0)
        limit = 0;

    /* Stable insertion sort by v0 ascending (nearest first) over the spans
     * that can actually be seen: one whose visible interval ends at or behind
     * the eye is never on screen, and letting it win the ranking would spend
     * the budget on invisible geometry while the visible span goes undrawn.
     * The cost stays on the GRAMMAR span: the drawn band range is a sub-range
     * of it, so the budget over-counts rather than truncating the geometry. */
    for (i = 0; i < count; i++) {
        if (spans[i].v1 <= eye_s)
            continue;
        j = ranked - 1;
        while (j >= 0 && spans[order[j]].v0 > spans[i].v0) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = i;
        ranked++;
    }

    for (i = 0; i < ranked; i++) {
        int idx = order[i];
        /* Bands are per RUN, posts per GRAMMAR SPAN: a split emits several
         * runs of one span and charging the posts on each would count the
         * same posts twice and shrink the budget for no geometry. */
        int cost = guardrail_band_count(spans[idx].v0, spans[idx].v1) *
                   GUARDRAIL_VERTS_PER_BAND;

        if (i == 0 || !guardrail_span_continues(&spans[order[i - 1]],
                                                &spans[idx]))
            cost += guardrail_post_count(spans[idx].s0, spans[idx].s1) *
                    GUARDRAIL_VERTS_PER_POST;
        if (used + cost > limit)
            break; /* whole-span truncation from the far end */
        keep[idx] = 1;
        used += cost;
    }
    return used;
}
