/*
 * guardrail.h - portable guardrail model for Racing 3D.
 *
 * Span geometry, rise/sink envelope and post layout only; no PSP headers
 * here so the host tests in tests/test_racing_rail.c can build it with a
 * normal C99 compiler. The GU drawing lives in guardrail_gu.c.
 *
 * No road_* or car_* symbol may appear in guardrail.c: scripts/test.sh links
 * it on its own.
 */

#ifndef RACING_GUARDRAIL_H
#define RACING_GUARDRAIL_H

#include <stdbool.h>

#define GUARDRAIL_MAX_SPANS 48 /* room for the unmerged collector list */
#define GUARDRAIL_HEIGHT 0.75f        /* beam top above the road, mid-span */
#define GUARDRAIL_BEAM_DEPTH 0.30f    /* beam face height */
#define GUARDRAIL_END_DEPTH 0.35f     /* beam top below the road at the ends */
#define GUARDRAIL_RISE 12.0f
#define GUARDRAIL_SINK 12.0f
#define GUARDRAIL_ROW_STEP 5.0f       /* == ROAD_MESH_STEP */
#define GUARDRAIL_BEAM_THICKNESS 0.12f
#define GUARDRAIL_POST_SPACING 8.0f
#define GUARDRAIL_POST_MIN_TOP 0.05f
#define GUARDRAIL_VERTS_PER_BAND 12   /* inner face + top face, per side */
#define GUARDRAIL_VERTS_PER_POST 6
/* Mirrors render.c's setup_3d(): eye_s = car_s - (base + per_speed*|v|). */
#define GUARDRAIL_EYE_BEHIND_BASE 8.2f
#define GUARDRAIL_EYE_BEHIND_PER_SPEED 0.035f
#define GUARDRAIL_FLASH_VERTS 6
#define GUARDRAIL_MAX_VERTS 3456

/* One rail span, as handed to the selection helper. Layout matches the road
 * module's span record but is declared here so this file stays portable. */
typedef struct {
    float s0, s1;
    signed char side;
    unsigned char kind;
} guardrail_span_ref_t;

/* 0 at the span ends, 1 once the rise/sink envelope is fully open. */
float guardrail_envelope(float s, float s0, float s1);

/* Beam top / bottom as an offset from the road surface at s; negative means
 * the beam has sunk below the road (both ends). */
float guardrail_beam_top(float s, float s0, float s1);
float guardrail_beam_bottom(float s, float s0, float s1);

/* Number of 5 m bands needed to cover [s0, s1] exactly. */
int guardrail_band_count(float s0, float s1);

/* A post exists at s only when the beam top there clears the road. */
bool guardrail_post_ok(float s, float s0, float s1);
int guardrail_post_count(float s0, float s1);
float guardrail_post_s(float s0, float s1, int index);

/* Worst-case vertices for a range railed on both sides (beam + posts). */
int guardrail_vertex_cost(float s0, float s1);

/* Vertices one span costs on one side (beam bands + surviving posts). */
int guardrail_span_vertex_cost(float s0, float s1);

/*
 * Choose which spans to draw under (budget - reserve) vertices. Spans are
 * ranked by s0 ascending, i.e. nearest the car first, so when the budget
 * binds the whole spans at the FAR end are dropped and the ones around the
 * car survive (physics clamps on those regardless of what is drawn).
 * Spans that end at or behind eye_s are never visible and are ignored
 * entirely, so they can neither win the ranking nor spend budget.
 * keep[] is written in the caller's input order; returns the vertices used.
 */
int guardrail_select_spans(const guardrail_span_ref_t *spans, int count,
                           unsigned char *keep, int budget, int reserve,
                           float eye_s);

#endif /* RACING_GUARDRAIL_H */
