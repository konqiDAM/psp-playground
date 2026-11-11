/*
 * guardrail_env.h - the rail's rise/sink envelope, as a header-only formula.
 *
 * road.c and guardrail.c must agree on `beam_top` to the last bit: the clamp
 * (road_rail_ok) and the drawn geometry are the same surface, so the formula
 * lives here once and both translation units inline it. The host tests in
 * tests/test_racing.c include this header and call guardrail_beam_top()
 * directly, which is why scripts/test.sh does not link guardrail.c there.
 *
 * No PSP headers, no road_* or car_* symbols: road.c includes this file and
 * guardrail.c is still linked on its own by tests/test_racing_rail.c.
 */

#ifndef RACING_GUARDRAIL_ENV_H
#define RACING_GUARDRAIL_ENV_H

#include <stdbool.h>

#define GUARDRAIL_HEIGHT 0.75f     /* beam top above the road, mid-span */
#define GUARDRAIL_BEAM_DEPTH 0.30f /* beam face height */
#define GUARDRAIL_END_DEPTH 0.35f  /* beam top below the road at the ends */
#define GUARDRAIL_RISE 12.0f       /* metres to reach full height */
#define GUARDRAIL_SINK 12.0f       /* metres to sink back below the road */
/* Equal to ROAD_RAIL_CLAMP_MIN_TOP: a post exists only where the beam is
 * visible, so no post can stand alone before the first drawn band. */
#define GUARDRAIL_POST_MIN_TOP 0.25f

static inline float guardrail_env_clamp01(float u)
{
    if (u < 0.0f)
        return 0.0f;
    if (u > 1.0f)
        return 1.0f;
    return u;
}

/* 3u^2 - 2u^3: smooth, monotone, zero slope at both ends. */
static inline float guardrail_env_smoothstep(float u)
{
    u = guardrail_env_clamp01(u);
    return u * u * (3.0f - 2.0f * u);
}

/* 0 at the span ends, 1 once the rise/sink envelope is fully open. */
static inline float guardrail_envelope(float s, float s0, float s1)
{
    if (s1 <= s0)
        return 0.0f;
    return guardrail_env_smoothstep((s - s0) / GUARDRAIL_RISE) *
           guardrail_env_smoothstep((s1 - s) / GUARDRAIL_SINK);
}

/* Beam top / bottom as an offset from the road surface at s; negative means
 * the beam has sunk below the road (both ends). */
static inline float guardrail_beam_top(float s, float s0, float s1)
{
    return -GUARDRAIL_END_DEPTH +
           (GUARDRAIL_HEIGHT + GUARDRAIL_END_DEPTH) *
               guardrail_envelope(s, s0, s1);
}

static inline float guardrail_beam_bottom(float s, float s0, float s1)
{
    return guardrail_beam_top(s, s0, s1) - GUARDRAIL_BEAM_DEPTH;
}

/* A post exists at s only when the beam top there clears the road. */
static inline bool guardrail_post_ok(float s, float s0, float s1)
{
    return guardrail_beam_top(s, s0, s1) >= GUARDRAIL_POST_MIN_TOP;
}

#endif /* RACING_GUARDRAIL_ENV_H */
