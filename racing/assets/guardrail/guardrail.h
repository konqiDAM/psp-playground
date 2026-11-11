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

/* The rise/sink envelope is a header-only formula shared with road.c (the
 * clamp predicate); this header re-exports it so the drawing code and the
 * host tests see exactly one definition. */
#include "assets/guardrail/guardrail_env.h"

#define GUARDRAIL_MAX_SPANS 48 /* room for the unmerged collector list */
#define GUARDRAIL_ROW_STEP 5.0f       /* == ROAD_MESH_STEP */
#define GUARDRAIL_BEAM_THICKNESS 0.12f
#define GUARDRAIL_POST_SPACING 8.0f
#define GUARDRAIL_POST_OFFSET 0.02f   /* the post sits this far outboard */
#define GUARDRAIL_VERTS_PER_BAND 12   /* inner face + top face, per side */
#define GUARDRAIL_VERTS_PER_POST 6
/* Mirrors render.c's setup_3d(): eye_s = car_s - (base + per_speed*|v|). */
#define GUARDRAIL_EYE_BEHIND_BASE 8.2f
#define GUARDRAIL_EYE_BEHIND_PER_SPEED 0.035f
#define GUARDRAIL_FLASH_VERTS 6
#define GUARDRAIL_MAX_VERTS 3456

/* One rail span, as handed to the selection helper. Layout matches the road
 * module's span record (four floats, side, kind) but is declared here so this
 * file stays portable: s0/s1 carry the grammar span the envelope is evaluated
 * on, v0/v1 the visible interval the geometry is iterated and emitted on. */
typedef struct {
    float s0, s1;
    float v0, v1;
    signed char side;
    unsigned char kind;
} guardrail_span_ref_t;

/* Number of 5 m bands needed to cover [s0, s1] exactly. */
int guardrail_band_count(float s0, float s1);

/*
 * The iteration range of one span: its VISIBLE interval [v0, v1] clamped to
 * the grammar span. The renderer must iterate THIS, never [s0, s1]: the beam
 * is below the road outside it, so iterating the grammar span draws bands
 * where the clamp does not exist (an invisible wall) and no pixel probe can
 * see it. The envelope is still evaluated on [s0, s1], so a run never
 * re-buries its own ends. Host-tested; guardrail_gu.c must call it.
 */
void guardrail_band_range(const guardrail_span_ref_t *span, float *from,
                          float *to);

/* True when `cur` is another visible run of the SAME grammar span as `prev`
 * (the collector split one span at a false gap). The renderer charges and
 * draws the posts once per grammar span, not once per run. */
bool guardrail_span_continues(const guardrail_span_ref_t *prev,
                              const guardrail_span_ref_t *cur);

/*
 * Lateral placement of the beam plane and its posts, as a function of the
 * ASPHALT half-width. The plane is the asphalt edge itself (the white line),
 * not the shoulder edge the beam used to sit on; the post is 0.02 m outboard
 * so the two never fight for the same pixels. guardrail_gu.c must call these
 * with road_sample_t.half_width - a source gate in scripts/test.sh enforces
 * both the argument and the absence of edge_half_width there.
 */
float guardrail_rail_lateral(float half_width);
float guardrail_post_lateral(float half_width);

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
 *
 * `reserve` is part of the host contract (tests call it with a reserve) but is
 * DEAD in production since 0.8.0: guardrail_draw() passes 0, because the
 * contact flash has its own 6-vertex sceGuGetMemory block drawn at the end of
 * the frame instead of sharing the rail's allocation.
 */
int guardrail_select_spans(const guardrail_span_ref_t *spans, int count,
                           unsigned char *keep, int budget, int reserve,
                           float eye_s);

#endif /* RACING_GUARDRAIL_H */
