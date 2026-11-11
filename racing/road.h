/*
 * road.h - runtime-generated curved road for Racing 3D.
 *
 * The road is an arc-spline chain described in (s, lateral) coordinates:
 * s is the distance along the centreline and heading 0 points along +Z.
 * road_sample() is the single source of truth for physics and rendering.
 */

#ifndef RACING_ROAD_H
#define RACING_ROAD_H

#include <stdbool.h>
#include <stdint.h>

#define ROAD_HALF_WIDTH 5.0f      /* flat/default test asphalt half-width */
#define ROAD_LANES 2
#define ROAD_SHOULDER_WIDTH 4.5f  /* drivable tan strip on each side */
#define ROAD_EDGE_X (ROAD_HALF_WIDTH + ROAD_SHOULDER_WIDTH) /* 9.5 */

#define ROAD_NORMAL_TOTAL 10.0f
#define ROAD_WIDE_TOTAL 16.0f
#define ROAD_WIDE_FLAT 500.0f
#define ROAD_ONE_LANE_TOTAL_MIN 4.0f
#define ROAD_ONE_LANE_TOTAL_MAX 6.0f
#define ROAD_ONE_LANE_TOTAL_DEFAULT 4.4f
#define ROAD_WIDTH_CYCLE_BASE 1200.0f
#define ROAD_WIDTH_RAMP 30.0f
#define ROAD_WIDTH_CURVE_MARGIN 35.0f
#define ROAD_TIGHT_CURVATURE (1.0f / 50.0f)
#define ROAD_NARROW_WARN_HALF 3.0f
#define ROAD_CREST_WARN_RATE 0.003f
#define ROAD_WARN_AHEAD 100.0f
#define ROAD_WARN_STEP 2.0f

/* 4-lane anchor: flat span starts 30 m after the ramp and is 500 m long;
 * the forced tight curve must start 150..270 m into the flat span. */
#define ROAD_TIGHT_WINDOW_OFFSET 150.0f
#define ROAD_TIGHT_WINDOW_LEN 120.0f
#define ROAD_NARROW_AVOID_BEFORE 160.0f
#define ROAD_NARROW_AVOID_AFTER 35.0f

/* Lane-count thresholds on the asphalt half-width (metres). */
#define ROAD_LANE_HALF_1 2.75f
#define ROAD_LANE_HALF_2 5.5f
#define ROAD_LANE_HALF_3 7.25f

#define ROAD_TERRAIN_MIN_PCT 50.0f
#define ROAD_TERRAIN_MAX_PCT 150.0f
#define ROAD_TERRAIN_DEFAULT_PCT 100.0f
#define ROAD_GRADE_BASE 0.12f
#define ROAD_GRADE_MAX 0.18f
#define ROAD_CREST_LENGTH 26.0f
#define ROAD_VERT_RUN_BASE 7
#define ROAD_HIGHWAY_RUN_MULT 48
#define ROAD_HIGHWAY_GRADE_SCALE 0.25f
#define ROAD_LANDING_FLATTEN 26.0f
#define ROAD_LANDING_RUNWAY 150.0f
#define ROAD_LANDING_GRADE_MAX 0.015f

#define ROAD_TEXTURE_X 64
#define ROAD_TEXTURE_Y 64
#define ROAD_TEXTURE_PERIOD 16.0f /* world metres covered by one V repeat */
#define ROAD_DASH_PERIOD 10.0f
#define ROAD_DASH_LEN 5.0f
#define ROAD_DASH_HALF_WIDTH 0.075f
#define ROAD_DASH_LIFT 0.015f

/*
 * Lane dashes are drawn as depth-less decals (0.8.0, Part C), so a dash beyond
 * a crest would paint over the crest. Emitting only the nearest 120 m bounds
 * that: a 0.15 m dash is 0.33 px wide at 120 m (f = 261.3 px), so the residual
 * is sub-pixel. 0.9.0 adds a sight-line cull (ROAD_SIGHT_*) for what is left.
 * Defined here (not in render.c) because the host budget test needs it.
 */
#define ROAD_DASH_AHEAD 120.0f

/* Sight-line cull for the depth-less dash decals (0.9.0). */
#define ROAD_SIGHT_STEP ROAD_MESH_STEP /* the road/dash lattice step */
#define ROAD_SIGHT_MARGIN 0.05f        /* same margin the warning signs use */
#define ROAD_SIGHT_MAX_POINTS 32       /* measured worst 28, +14 % */

#define ROAD_POST_SPACING 40.0f
#define ROAD_POST_X (ROAD_EDGE_X + 0.6f) /* 10.1 */

/* Guardrail: spans follow the road grammar and are deterministic from the seed. */
#define ROAD_RAIL_LEAD 20.0f        /* span starts before the curve / plateau */
#define ROAD_RAIL_TRAIL 15.0f       /* and ends after it */
#define ROAD_RAIL_BLIND_GRADE 0.06f /* entrance grade <= -6 % => blind corner */
#define ROAD_RAIL_DENSITY_DEFAULT 0.6f
/* 48, not 24: the collector caps the *unmerged* per-(segment, side) list in
 * ascending s order, so a dense two-sided stretch must not drop ahead spans
 * (physics would still clamp there: an invisible wall). */
#define ROAD_RAIL_MAX_SPANS 48
#define ROAD_RAIL_MAX_VISIBLE_RUNS 8 /* pieces one merged span may split into */
/* Visible-run scan: a bracket of this length, bisected this many times at
 * each crossing. 10 m / 2^8 = 0.039 m, well inside the 0.15 m the probes pin,
 * and the whole per-frame pass must stay inside ROAD_RAIL_SAMPLE_BUDGET
 * road_sample() calls.
 *
 * The budget is a SAMPLED bound, not a proof over every start seed. Measured
 * per-frame worst: 192 on the reference frame (ROAD_SEED, car_s 1000), 228
 * over the repository's 64-seed sweep (seeds i*7919+1, 3 maps, car_s 0..3000
 * step 50), and 279 / 281 over 12 000 seeds along the game's own
 * road_next_start_seed orbit / an LCG sequence (3 maps, step 50); the
 * reviewer's random-seed sweeps measured 288 (4 000 seeds) and 290 (12 000).
 * Real runs start on arbitrary xorshift32 orbits, which no finite sweep
 * enumerates, so 320 is a COST TARGET above the measured worst with headroom,
 * not an upper bound on every orbit. A budget regression is still caught: the
 * host sweep test fails if its measured worst frame exceeds this value. */
#define ROAD_RAIL_SCAN_STEP 10.0f
#define ROAD_RAIL_BISECT_STEPS 8
#define ROAD_RAIL_SAMPLE_BUDGET 320
/* The clamp is the car's outer wheel face (half-width 1.04) plus a 1 cm
 * margin, at EVERY road width: a stop that narrowed with the road put the
 * wheel face past the rail plane on a one-lane corridor. Eligibility is a
 * separate question, asked by road_rail_allowed(). */
#define ROAD_RAIL_STOP_FULL 1.05f
#define ROAD_RAIL_MIN_CENTRE 0.30f /* smallest clamp distance from the centre */
#define ROAD_RAIL_CLAMP_MIN_TOP 0.25f /* the beam must be this far above road */
#define ROAD_RAIL_SIDE_LEFT 1
#define ROAD_RAIL_SIDE_RIGHT 2
#define ROAD_RAIL_KIND_OUTSIDE 1
#define ROAD_RAIL_KIND_INSIDE 2
#define ROAD_RAIL_KIND_NARROW 4

#define ROAD_MESH_STEP 5.0f
#define ROAD_NEAR_BEHIND 90.0f
#define ROAD_WINDOW_BEHIND 1200.0f
#define ROAD_WINDOW_AHEAD 1200.0f
#define ROAD_MAX_SEGMENTS 160
#define ROAD_REBASE_LIMIT 3000.0f
#define ROAD_REBASE_STEP 960.0f
#define ROAD_REBASE_ALIGN 80.0f /* LCM(16,40,5): keeps phases aligned */
#define ROAD_SEED 0x5EED2A17u
#define ROAD_START_STRAIGHT 120.0f

/* Grammar (metres; radius for curves). */
#define ROAD_STRAIGHT_MIN 40.0f
#define ROAD_STRAIGHT_MAX 120.0f
#define ROAD_GENTLE_RADIUS_MIN 60.0f
#define ROAD_GENTLE_RADIUS_MAX 120.0f
#define ROAD_TIGHT_RADIUS_MIN 25.0f
#define ROAD_TIGHT_RADIUS_MAX 45.0f
#define ROAD_GENTLE_ARC_MIN 50.0f
#define ROAD_GENTLE_ARC_MAX 120.0f
#define ROAD_TIGHT_ARC_MIN 25.0f
#define ROAD_TIGHT_ARC_MAX 60.0f

typedef enum {
    ROAD_MAP_DEFAULT = 0,
    ROAD_MAP_UPHILL,
    ROAD_MAP_HIGHWAY,
    ROAD_MAP_COUNT
} road_map_t;

typedef struct {
    float elevation_pct;  /* 50..150, default 100 */
    float hill_pct;       /* 50..150, default 100 */
    float width_pct;      /* 50..150, default 100 */
    float one_lane_width; /* total asphalt metres, 4.0..6.0, default 4.4 */
    road_map_t map;       /* appended last; default ROAD_MAP_DEFAULT */
    bool rail_enabled;    /* guardrail master switch; default true */
    float rail_density;   /* 0..1, fraction of tight curves railed; 0.6 */
} road_config_t;

typedef struct {
    float start_s;
    float start_x, start_z;
    float start_heading;
    float length;
    float curvature; /* 1/m, + = right */
    float start_height;
    float start_grade;
    float grade_rate;
    float crest_len;
    signed char rail_side; /* 0, or ROAD_RAIL_SIDE_LEFT|ROAD_RAIL_SIDE_RIGHT */
} road_segment_t;

typedef struct {
    float x, z;     /* centreline position in the current local frame */
    float heading;  /* radians, 0 = +Z, + = right */
    float curvature;/* 1/m */
    float height;   /* centreline y */
    float grade;    /* dy/ds, + uphill */
    float grade_rate;/* d(grade)/ds */
    float half_width;/* asphalt half-width */
    float edge_half_width; /* asphalt + shoulder half-width */
    float rail_limit_left;  /* max |lateral| for the left wall; 0 = no rail */
    float rail_limit_right; /* max |lateral| for the right wall; 0 = no rail */
} road_sample_t;

typedef struct {
    bool narrow;
    float narrow_distance;
    bool crest;
    float crest_distance;
} road_warning_t;

typedef struct {
    float s0, s1;        /* grammar span: the rise/sink envelope is evaluated
                          * on THIS, never clipped to the query */
    float v0, v1;        /* visible + clamped interval: iterate and emit on
                          * THIS (the beam is below the road outside it) */
    signed char side;    /* -1 left, +1 right */
    unsigned char kind;  /* ROAD_RAIL_KIND_* bit mask */
} road_rail_span_t;

/*
 * Sight-line visibility over a uniform height profile - the warning signs'
 * occlusion test (signs.c line_clear()) applied to a lattice, in ONE forward
 * pass so the per-frame cost is O(n) and not O(n^2).
 *
 * h[0..n-1] are the road heights at s = base + i*step (base is world-aligned:
 * a multiple of step). eye_pos is the eye's FRACTIONAL lattice index,
 * (eye_s - base) / step, and eye_y the camera height. A point i is visible
 * when the straight line from (eye_pos, eye_y) to (i, h[i] + lift) stays at or
 * above h[j] + margin for every j strictly between eye_pos and i; the
 * comparison is >=, so a target exactly on an occluder line is visible. Points
 * with i <= eye_pos are at or behind the camera and always visible. Returns
 * the number of visible points (0 and an all-false `out` on bad input).
 *
 * eye_pos is FRACTIONAL on purpose: rounding the origin down to the lattice
 * puts it up to one step (5 m) behind the camera at camera height, which drops
 * the test line below the true sight line near the eye and OVER-CULLS
 * (measured: 3.34 % of frames disagree, worst bias 0.688 m = 13.8x the margin).
 *
 * PURE: no road state, no globals - the host test feeds it a synthetic crest
 * and the renderer feeds it road_sample() heights. The lattice is anchored to
 * absolute s (multiples of 5 m, and ROAD_REBASE_ALIGN is 80 m), so the sample
 * set changes only at the occlusion boundary and a band's visibility does not
 * oscillate frame to frame (measured: 0 transitions over 240 frames).
 */
int road_sight_visible(const float *h, int n, float eye_pos, float step,
                       float eye_y, float lift, float margin, bool *out);

void road_config_defaults(road_config_t *cfg);
bool road_config_validate(road_config_t *cfg);
float road_width_cycle_length(const road_config_t *cfg);
int road_vertical_run_max(const road_config_t *cfg);
int road_lane_count(float half_width);
int road_lane_dashes(float half_width);
bool road_dash_on(float s);
uint32_t road_seed_next(uint32_t *state);
uint32_t road_next_start_seed(uint32_t *state);

void road_reset(uint32_t seed);
void road_reset_config(uint32_t seed, const road_config_t *cfg);

/* Ensure the window covers car_s; returns the (possibly re-based) car_s. */
float road_update(float car_s);

/* Centreline pose at distance s (clamped/extrapolated outside the window). */
road_sample_t road_sample(float s);

/*
 * Lateral limit for one side of a sample: 0 for a NULL sample, a degenerate
 * edge (edge_half_width <= 0) or a side without a rail; otherwise the limit,
 * never wider than edge_half_width - CAR_EDGE_MARGIN.
 * side > 0 = right, side < 0 = left.
 */
float road_rail_limit(const road_sample_t *p, int side);

/* The stop is the wheel half-width plus margin, constant at every width. */
float road_rail_stop(float half_width);

/* Eligibility, separate from the stop: half - ROAD_RAIL_STOP_FULL >=
 * ROAD_RAIL_MIN_CENTRE. !allowed() means NO rail and NO limit at all. */
bool road_rail_allowed(float half_width);

/*
 * THE rail predicate, shared by the sampler, the collector and the renderer.
 * It takes the already-computed sample: it must NEVER call road_sample(), or
 * the road_sample() -> predicate path would be a cycle. `s0, s1` is the
 * MERGED union of every overlapping railed window touching s, never a single
 * window's own interval (a per-window interval leaves a drive-through hole
 * where the merged beam is drawn at full height but no clamp exists).
 */
bool road_rail_ok(const road_sample_t *p, float s, float s0, float s1);

/*
 * Merged union of the windows in a[]/b[] that contain s: u0/u1 cover every
 * window that overlaps the one containing s. Pure; O(n^2) worst case with
 * n <= 6 (the absorption loop runs until nothing changes). Returns false when
 * s is in no window. This is the sampler's mechanism (it unions its own at
 * most six candidate windows in place instead of calling the collector).
 */
bool road_rail_union(float s, const float *a, const float *b, int n,
                     float *u0, float *u1);

/* Pairwise merge of two windows; false when they are disjoint. */
bool road_rail_merge(float a0, float b0, float a1, float b1, float *o0,
                     float *o1);

/*
 * The renderer's band decision, host-tested so the PSP-only draw cannot
 * drift from the clamp: a 5 m band [a_s, b_s] is emitted only when BOTH of
 * its endpoint samples satisfy road_rail_ok() over the grammar span
 * [s0, s1]. guardrail_gu.c must call this (a source gate in test.sh).
 */
bool road_rail_band_ok(const road_sample_t *a, float a_s,
                       const road_sample_t *b, float b_s, float s0, float s1);

/* The predicate the visible-interval splitter bisects on. */
typedef bool (*road_rail_visible_fn)(float s, float s0, float s1);

/*
 * Split [s0, s1] into the DISJOINT runs where `visible` holds. A false gap
 * inside one grammar span must never be bridged into a single [v0, v1]: the
 * bands inside the gap would be drawn while rail_limit_* is 0 there. Coarse
 * brackets of ROAD_RAIL_SCAN_STEP (10 m), bisected ROAD_RAIL_BISECT_STEPS (8)
 * times at each crossing; runs[2i] and runs[2i+1] are the start and end of run
 * i in ascending s. Returns the number of runs written (0..max_runs). Pure: it
 * never reads road state itself, which is what lets a host test feed it a
 * synthetic gap.
 */
int road_rail_visible_runs(float s0, float s1, road_rail_visible_fn visible,
                           float *runs, int max_runs);

/* world_s = local_s + road_world_offset(). */
float road_world_offset(void);

/*
 * Full rail intervals overlapping [s0, s1], one entry per side, sorted by
 * (s0, side) with left before right and merged per side. Emitted spans are
 * never clipped to the query, so the rise/sink envelope stays intact, and
 * each carries its visible interval [v0, v1] (see road_rail_visible_runs):
 * the drawn span is the clamped span. A merged span whose predicate has a
 * false gap in the middle is emitted as one entry PER RUN, so the pieces
 * share (s0, side) and are adjacent in ascending v0. Returns the number
 * written (0..max).
 */
int road_rail_collect(float s0, float s1, road_rail_span_t *out, int max);

/*
 * First curve strictly ahead of from_s (start_s >= from_s + 1 m) whose
 * |curvature| >= min_curvature and whose entrance is within ahead metres.
 * Returns false when there is no such curve.
 */
bool road_next_curve(float from_s, float ahead, float min_curvature,
                     float *distance, float *curvature);
bool road_next_narrow(float from_s, float ahead, float max_half,
                      float *distance, float *half_width);
bool road_next_crest(float from_s, float ahead, float max_rate,
                     float *distance, float *grade_rate);
void road_warnings(float car_s, road_warning_t *out);

int road_segment_count(void);
const road_segment_t *road_segment_at(int index); /* 0 = oldest */

#endif /* RACING_ROAD_H */
