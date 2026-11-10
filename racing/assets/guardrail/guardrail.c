/*
 * guardrail.c - portable guardrail model for Racing 3D.
 *
 * Beam envelope, band and post layout, and the vertex budget. No PSP headers
 * and no road_* symbols: tests/test_racing_rail.c links this file alone.
 */

#include "assets/guardrail/guardrail.h"

#include <math.h>

static float clamp01(float u)
{
    if (u < 0.0f)
        return 0.0f;
    if (u > 1.0f)
        return 1.0f;
    return u;
}

/* 3u^2 - 2u^3: smooth, monotone, zero slope at both ends. */
static float smoothstep01(float u)
{
    u = clamp01(u);
    return u * u * (3.0f - 2.0f * u);
}

float guardrail_envelope(float s, float s0, float s1)
{
    if (s1 <= s0)
        return 0.0f;
    return smoothstep01((s - s0) / GUARDRAIL_RISE) *
           smoothstep01((s1 - s) / GUARDRAIL_SINK);
}

float guardrail_beam_top(float s, float s0, float s1)
{
    return -GUARDRAIL_END_DEPTH +
           (GUARDRAIL_HEIGHT + GUARDRAIL_END_DEPTH) *
               guardrail_envelope(s, s0, s1);
}

float guardrail_beam_bottom(float s, float s0, float s1)
{
    return guardrail_beam_top(s, s0, s1) - GUARDRAIL_BEAM_DEPTH;
}

int guardrail_band_count(float s0, float s1)
{
    float len = s1 - s0;

    if (len <= 0.0f)
        return 0;
    return (int)ceilf(len / GUARDRAIL_ROW_STEP);
}

bool guardrail_post_ok(float s, float s0, float s1)
{
    return guardrail_beam_top(s, s0, s1) >= GUARDRAIL_POST_MIN_TOP;
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

    /* Stable insertion sort by s0 ascending (nearest first) over the spans
     * that can actually be seen: one ending at or behind the eye is never on
     * screen, and letting it win the ranking would spend the budget on
     * invisible geometry while the visible span goes undrawn. */
    for (i = 0; i < count; i++) {
        if (spans[i].s1 <= eye_s)
            continue;
        j = ranked - 1;
        while (j >= 0 && spans[order[j]].s0 > spans[i].s0) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = i;
        ranked++;
    }

    for (i = 0; i < ranked; i++) {
        int idx = order[i];
        int cost = guardrail_span_vertex_cost(spans[idx].s0, spans[idx].s1);

        if (used + cost > limit)
            break; /* whole-span truncation from the far end */
        keep[idx] = 1;
        used += cost;
    }
    return used;
}
