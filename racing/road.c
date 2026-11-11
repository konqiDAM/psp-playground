/*
 * road.c - deterministic runtime road generator for Racing 3D.
 *
 * The road is a chain of constant-curvature arc segments. A dual-ended ring
 * holds the ~2.4 km window around the car; forward and backward generators
 * extend it, and an 80 m-aligned re-base keeps float precision bounded.
 */

#include "road.h"
#include "physics.h"
/* Header-only rise/sink envelope: the clamp predicate (road_rail_ok) and the
 * drawn beam must be the same surface, so both sides inline this formula. */
#include "assets/guardrail/guardrail_env.h"

#include <math.h>
#include <stddef.h>

#define RNG_SCALE (1.0f / 16777216.0f)

typedef struct {
    int gentle;
    int sign;
} curve_choice_t;

/* 3 segment windows (i-1..i+1) + 3 narrow windows (k0-1..k0+1). The sampler
 * writes into arrays of exactly this length, so pin it at compile time. */
#define RAIL_CAND_MAX 6
typedef char rail_cand_max_is_6[(RAIL_CAND_MAX == 6) ? 1 : -1];

static road_segment_t segments[ROAD_MAX_SEGMENTS];
static int head;  /* oldest segment */
static int count; /* live segments */

static uint32_t fwd_rng;
static uint32_t bwd_rng;
static uint32_t rail_fwd_rng;
static uint32_t rail_bwd_rng;
static uint32_t fwd_vert_rng;
static uint32_t bwd_vert_rng;
static int fwd_vert_dir;
static int fwd_vert_run_left;
static int fwd_vert_pending;
static int bwd_vert_dir;
static int bwd_vert_run_left;
static int bwd_vert_first;
static curve_choice_t fwd_bag[4];
static curve_choice_t bwd_bag[4];
static int fwd_bag_index = 4;
static int bwd_bag_index = 4;

static int forward_first;
static int forward_next_curve;
static int forward_first_curve;
static int backward_first;
static int backward_next_curve;
static int backward_first_curve;
static int fwd_chain_left;
static int fwd_chain_sign;
static int fwd_chain_started;
static int bwd_chain_left;
static int bwd_chain_sign;
static int bwd_chain_started;
static int fwd_landing_state;

static road_config_t active_config;
static float world_offset;
static uint32_t width_seed;

typedef enum {
    WIDTH_EVENT_NONE = 0,
    WIDTH_EVENT_WIDE,
    WIDTH_EVENT_NARROW
} width_event_kind_t;

typedef struct {
    int valid;
    int k;
    int kind;
    float s0;
    float ramp_start; /* offset from s0 */
    float flat_start;
    float flat_len;
    float ramp_end;
    float narrow_half;
    int suppressed;
} width_cycle_t;

static width_cycle_t width_cache[16];

/* --------------------------------------------------------------- helpers */

static float clampf(float value, float low, float high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

static uint32_t hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static float width_phase(void)
{
    return (float)(width_seed & 0xFFFFu) / 65536.0f *
           road_width_cycle_length(&active_config);
}

void road_config_defaults(road_config_t *cfg)
{
    if (!cfg)
        return;
    cfg->elevation_pct = ROAD_TERRAIN_DEFAULT_PCT;
    cfg->hill_pct = ROAD_TERRAIN_DEFAULT_PCT;
    cfg->width_pct = ROAD_TERRAIN_DEFAULT_PCT;
    cfg->one_lane_width = ROAD_ONE_LANE_TOTAL_DEFAULT;
    cfg->map = ROAD_MAP_DEFAULT;
    cfg->rail_enabled = true;
    cfg->rail_density = ROAD_RAIL_DENSITY_DEFAULT;
}

static float config_value(float value, float low, float high, float def,
                          bool *changed)
{
    float result;

    if (!isfinite(value)) {
        result = def;
    } else {
        result = clampf(value, low, high);
    }
    if (result != value)
        *changed = true;
    return result;
}

bool road_config_validate(road_config_t *cfg)
{
    bool changed = false;

    if (!cfg)
        return false;
    cfg->elevation_pct = config_value(cfg->elevation_pct,
                                      ROAD_TERRAIN_MIN_PCT,
                                      ROAD_TERRAIN_MAX_PCT,
                                      ROAD_TERRAIN_DEFAULT_PCT, &changed);
    cfg->hill_pct = config_value(cfg->hill_pct, ROAD_TERRAIN_MIN_PCT,
                                 ROAD_TERRAIN_MAX_PCT,
                                 ROAD_TERRAIN_DEFAULT_PCT, &changed);
    cfg->width_pct = config_value(cfg->width_pct, ROAD_TERRAIN_MIN_PCT,
                                  ROAD_TERRAIN_MAX_PCT,
                                  ROAD_TERRAIN_DEFAULT_PCT, &changed);
    cfg->one_lane_width = config_value(cfg->one_lane_width,
                                       ROAD_ONE_LANE_TOTAL_MIN,
                                       ROAD_ONE_LANE_TOTAL_MAX,
                                       ROAD_ONE_LANE_TOTAL_DEFAULT,
                                       &changed);
    if (cfg->map < ROAD_MAP_DEFAULT || cfg->map >= ROAD_MAP_COUNT) {
        cfg->map = ROAD_MAP_DEFAULT;
        changed = true;
    }
    cfg->rail_density = config_value(cfg->rail_density, 0.0f, 1.0f,
                                     ROAD_RAIL_DENSITY_DEFAULT, &changed);
    return changed;
}

float road_width_cycle_length(const road_config_t *cfg)
{
    float pct = ROAD_TERRAIN_DEFAULT_PCT;

    if (cfg && isfinite(cfg->width_pct))
        pct = clampf(cfg->width_pct, ROAD_TERRAIN_MIN_PCT,
                     ROAD_TERRAIN_MAX_PCT);
    return ROAD_WIDTH_CYCLE_BASE * 100.0f / pct;
}

int road_vertical_run_max(const road_config_t *cfg)
{
    float pct = ROAD_TERRAIN_DEFAULT_PCT;
    int base = ROAD_VERT_RUN_BASE;
    int result;

    if (cfg) {
        if (isfinite(cfg->hill_pct))
            pct = clampf(cfg->hill_pct, ROAD_TERRAIN_MIN_PCT,
                         ROAD_TERRAIN_MAX_PCT);
        if (cfg->map == ROAD_MAP_HIGHWAY)
            base *= ROAD_HIGHWAY_RUN_MULT;
    }
    result = (int)floorf((float)base * 100.0f / pct + 0.5f);
    if (result < 1)
        result = 1;
    return result;
}

int road_lane_count(float half_width)
{
    if (!isfinite(half_width) || half_width < ROAD_LANE_HALF_1)
        return 1;
    if (half_width < ROAD_LANE_HALF_2)
        return 2;
    if (half_width < ROAD_LANE_HALF_3)
        return 3;
    return 4;
}

int road_lane_dashes(float half_width)
{
    int lanes = road_lane_count(half_width);

    return lanes > 1 ? lanes - 1 : 0;
}

bool road_dash_on(float s)
{
    float phase = fmodf(s, ROAD_DASH_PERIOD);

    if (phase < 0.0f)
        phase += ROAD_DASH_PERIOD;
    return phase < ROAD_DASH_LEN;
}

uint32_t road_seed_next(uint32_t *state)
{
    uint32_t x = (state && *state) ? *state : 1u;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    if (x == 0)
        x = 1u;
    if (state)
        *state = x;
    return x;
}

uint32_t road_next_start_seed(uint32_t *state)
{
    uint32_t seed = (state && *state) ? *state : 1u;

    if (state)
        *state = road_seed_next(state);
    return seed;
}

static void width_cache_clear(void)
{
    int i;

    for (i = 0; i < 16; i++)
        width_cache[i].valid = 0;
}

static uint32_t rng_next(uint32_t *state)
{
    uint32_t x = *state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static float rng_range(uint32_t *state, float lo, float hi)
{
    float unit = (float)(rng_next(state) >> 8) * RNG_SCALE;

    return lo + (hi - lo) * unit;
}

static float normalize_angle(float a)
{
    return atan2f(sinf(a), cosf(a));
}

static road_segment_t *at(int i)
{
    return &segments[(head + i) % ROAD_MAX_SEGMENTS];
}

static int push_back(road_segment_t seg)
{
    if (count == ROAD_MAX_SEGMENTS)
        return 0;
    segments[(head + count) % ROAD_MAX_SEGMENTS] = seg;
    count++;
    return 1;
}

static int push_front(road_segment_t seg)
{
    if (count == ROAD_MAX_SEGMENTS)
        return 0;
    head = (head + ROAD_MAX_SEGMENTS - 1) % ROAD_MAX_SEGMENTS;
    segments[head] = seg;
    count++;
    return 1;
}

static void drop_front(void)
{
    head = (head + 1) % ROAD_MAX_SEGMENTS;
    count--;
}

static void drop_back(void)
{
    count--;
}

static void make_room(int extending_back)
{
    if (count < ROAD_MAX_SEGMENTS)
        return;
    if (extending_back)
        drop_front();
    else
        drop_back();
}

/* ------------------------------------------------------- arc evaluation */

static road_sample_t eval_segment(const road_segment_t *seg, float t)
{
    road_sample_t p;
    float h = seg->start_heading + seg->curvature * t;
    float crest = seg->crest_len;

    if (fabsf(seg->curvature) < 1e-5f) {
        p.x = seg->start_x - t * sinf(seg->start_heading);
        p.z = seg->start_z + t * cosf(seg->start_heading);
    } else {
        p.x = seg->start_x +
              (cosf(h) - cosf(seg->start_heading)) / seg->curvature;
        p.z = seg->start_z +
              (sinf(h) - sinf(seg->start_heading)) / seg->curvature;
    }
    p.heading = normalize_angle(h);
    p.curvature = seg->curvature;

    if (crest < 0.0f)
        crest = 0.0f;
    if (crest > seg->length)
        crest = seg->length;
    if (t <= crest && crest > 0.0f) {
        p.height = seg->start_height + seg->start_grade * t +
                   0.5f * seg->grade_rate * t * t;
        p.grade = seg->start_grade + seg->grade_rate * t;
        p.grade_rate = seg->grade_rate;
    } else {
        float end_grade = seg->start_grade + seg->grade_rate * crest;
        float hc = seg->start_height + seg->start_grade * crest +
                   0.5f * seg->grade_rate * crest * crest;

        p.grade = end_grade;
        p.height = hc + end_grade * (t - crest);
        p.grade_rate = 0.0f;
    }
    p.half_width = 0.0f;
    p.edge_half_width = 0.0f;
    p.rail_limit_left = 0.0f;
    p.rail_limit_right = 0.0f;
    return p;
}

/* ------------------------------------------------------- width profile */

static float horizontal_max_curvature(float from, float to)
{
    float result = 0.0f;
    int i;

    for (i = 0; i < count; i++) {
        const road_segment_t *seg = at(i);
        float end = seg->start_s + seg->length;

        if (end < from || seg->start_s > to)
            continue;
        if (fabsf(seg->curvature) > result)
            result = fabsf(seg->curvature);
    }
    return result;
}

static int width_event_kind(uint32_t h)
{
    if (active_config.map == ROAD_MAP_HIGHWAY)
        return WIDTH_EVENT_NONE;
    if (active_config.map == ROAD_MAP_UPHILL)
        return (((h >> 16) & 3u) == 0u) ? WIDTH_EVENT_NARROW
                                        : WIDTH_EVENT_NONE;
    return ((h % 10u) < 7u) ? WIDTH_EVENT_WIDE : WIDTH_EVENT_NARROW;
}

static width_cycle_t *width_cycle_get(int k)
{
    int slot = (int)((unsigned int)k & 15u);
    width_cycle_t *c = &width_cache[slot];
    float cycle = road_width_cycle_length(&active_config);
    uint32_t h;
    float u2, u3;

    if (c->valid && c->k == k)
        return c;

    h = hash32((uint32_t)k * 2654435761u ^ width_seed);
    u2 = (float)((h >> 16) & 0xFFu) / 255.0f;
    u3 = (float)((h >> 24) & 0xFFu) / 255.0f;

    c->valid = 1;
    c->k = k;
    c->kind = width_event_kind(h);
    c->s0 = width_phase() + (float)k * cycle;
    c->ramp_start = 0.0f;
    c->flat_start = 0.0f;
    c->flat_len = 0.0f;
    c->ramp_end = 0.0f;
    c->narrow_half = active_config.one_lane_width * 0.5f;
    c->suppressed = 0;

    if (c->kind == WIDTH_EVENT_WIDE) {
        c->ramp_start = cycle * (0.10f + 0.10f * u3);
        c->flat_start = c->ramp_start + ROAD_WIDTH_RAMP;
        c->flat_len = ROAD_WIDE_FLAT;
        c->ramp_end = c->flat_start + c->flat_len + ROAD_WIDTH_RAMP;
    } else if (c->kind == WIDTH_EVENT_NARROW) {
        float local_from;
        float local_to;

        c->ramp_start = cycle * (0.40f + 0.15f * u3);
        c->flat_start = c->ramp_start + ROAD_WIDTH_RAMP;
        c->flat_len = 160.0f + 80.0f * u2;
        c->ramp_end = c->flat_start + c->flat_len + ROAD_WIDTH_RAMP;
        local_from = c->s0 + c->ramp_start - ROAD_WIDTH_CURVE_MARGIN -
                     world_offset;
        local_to = c->s0 + c->ramp_end + ROAD_WIDTH_CURVE_MARGIN -
                   world_offset;
        c->suppressed =
            active_config.map == ROAD_MAP_DEFAULT &&
            horizontal_max_curvature(local_from, local_to) >
                ROAD_TIGHT_CURVATURE;
    }
    return c;
}

static float width_at(float world_s)
{
#ifdef RACING_DIAG_NARROW
    {
        float dnormal = ROAD_NORMAL_TOTAL * 0.5f;
        float dnarrow = active_config.one_lane_width * 0.5f;

        if (world_s >= 0.0f && world_s <= 290.0f) {
            if (world_s < 260.0f)
                return dnarrow;
            return dnarrow + (dnormal - dnarrow) *
                                 (world_s - 260.0f) / 30.0f;
        }
    }
#endif
    float cycle = road_width_cycle_length(&active_config);
    float phase = width_phase();
    int k = (int)floorf((world_s - phase) / cycle);
    width_cycle_t *c = width_cycle_get(k);
    float q = world_s - c->s0;
    float normal = ROAD_NORMAL_TOTAL * 0.5f;
    float wide = ROAD_WIDE_TOTAL * 0.5f;

    if (c->kind == WIDTH_EVENT_NONE)
        return active_config.map == ROAD_MAP_HIGHWAY ? wide : normal;

    if (c->kind == WIDTH_EVENT_WIDE) {
        if (q < c->ramp_start)
            return normal;
        if (q < c->flat_start) {
            float t = (q - c->ramp_start) / ROAD_WIDTH_RAMP;

            return normal + (wide - normal) * t;
        }
        if (q < c->flat_start + c->flat_len)
            return wide;
        if (q < c->ramp_end) {
            float t = (q - (c->flat_start + c->flat_len)) / ROAD_WIDTH_RAMP;

            return wide + (normal - wide) * t;
        }
        return normal;
    }

    if (c->suppressed)
        return normal;
    if (q < c->ramp_start)
        return normal;
    if (q < c->flat_start) {
        float t = (q - c->ramp_start) / ROAD_WIDTH_RAMP;

        return normal + (c->narrow_half - normal) * t;
    }
    if (q < c->flat_start + c->flat_len)
        return c->narrow_half;
    if (q < c->ramp_end) {
        float t = (q - (c->flat_start + c->flat_len)) / ROAD_WIDTH_RAMP;

        return c->narrow_half + (normal - c->narrow_half) * t;
    }
    return normal;
}

/*
 * Guardrail narrow corridor: a one-lane width event contributes a rail on
 * both sides over [plateau - LEAD, plateau + flat_len + TRAIL], in *world*
 * distance. Suppressed events and a disabled rail produce nothing.
 */
static int narrow_rail_window(int k, float *world_s0, float *world_s1)
{
    width_cycle_t *c;

    if (!active_config.rail_enabled)
        return 0;
    c = width_cycle_get(k);
    if (c->kind != WIDTH_EVENT_NARROW || c->suppressed)
        return 0;
    if (world_s0)
        *world_s0 = c->s0 + c->flat_start - ROAD_RAIL_LEAD;
    if (world_s1)
        *world_s1 = c->s0 + c->flat_start + c->flat_len + ROAD_RAIL_TRAIL;
    return 1;
}

static int width_tight_due(float world_s)
{
    float cycle;
    float phase;
    int k;
    width_cycle_t *c;
    float q;
    float lo;

    if (active_config.map != ROAD_MAP_DEFAULT)
        return 0;
    cycle = road_width_cycle_length(&active_config);
    phase = width_phase();
    k = (int)floorf((world_s - phase) / cycle);
    c = width_cycle_get(k);
    if (c->kind != WIDTH_EVENT_WIDE)
        return 0;
    q = world_s - c->s0;
    lo = c->flat_start + ROAD_TIGHT_WINDOW_OFFSET;
    return q >= lo && q <= lo + ROAD_TIGHT_WINDOW_LEN;
}

static int width_narrow_zone(float world_s)
{
    float cycle;
    float phase;
    int k0;
    int i;
    int narrow = 0;

    if (active_config.map != ROAD_MAP_DEFAULT)
        return 0;
    cycle = road_width_cycle_length(&active_config);
    phase = width_phase();
    k0 = (int)floorf((world_s - phase) / cycle);
    for (i = 0; i <= 1; i++) {
        int k = k0 + i;
        width_cycle_t *c = width_cycle_get(k);
        float lo;
        float hi;

        if (c->kind != WIDTH_EVENT_NARROW)
            continue;
        lo = c->s0 + c->ramp_start - ROAD_NARROW_AVOID_BEFORE;
        hi = c->s0 + c->ramp_end + ROAD_NARROW_AVOID_AFTER;
        if (world_s >= lo && world_s <= hi)
            narrow = 1;
    }
    return narrow;
}


static void bag_refill(uint32_t *rng, curve_choice_t *bag, int *index)
{
    int i;

    bag[0] = (curve_choice_t){1, 1};
    bag[1] = (curve_choice_t){1, -1};
    bag[2] = (curve_choice_t){0, 1};
    bag[3] = (curve_choice_t){0, -1};

    for (i = 3; i > 0; i--) {
        int j = (int)(rng_next(rng) % (uint32_t)(i + 1));
        curve_choice_t tmp = bag[i];

        bag[i] = bag[j];
        bag[j] = tmp;
    }
    *index = 0;
}

static curve_choice_t bag_next(uint32_t *rng, curve_choice_t *bag, int *index)
{
    if (*index >= 4)
        bag_refill(rng, bag, index);
    return bag[(*index)++];
}

static void choose_curve(uint32_t *rng, curve_choice_t *bag, int *bag_index,
                         int first_curve, int *first_flag, int force_type,
                         road_segment_t *seg)
{
    int gentle;
    int sign;
    float radius;
    float arc;

    if (first_curve) {
        gentle = 1;
        sign = (rng_next(rng) & 1u) ? 1 : -1;
        *first_flag = 0;
    } else {
        curve_choice_t choice = bag_next(rng, bag, bag_index);

        gentle = choice.gentle;
        sign = choice.sign;
    }
    if (force_type == 1)
        gentle = 1;
    else if (force_type == 2)
        gentle = 0;

    if (gentle) {
        radius = rng_range(rng, ROAD_GENTLE_RADIUS_MIN,
                           ROAD_GENTLE_RADIUS_MAX);
        arc = rng_range(rng, ROAD_GENTLE_ARC_MIN, ROAD_GENTLE_ARC_MAX);
    } else {
        radius = rng_range(rng, ROAD_TIGHT_RADIUS_MIN,
                           ROAD_TIGHT_RADIUS_MAX);
        arc = rng_range(rng, ROAD_TIGHT_ARC_MIN, ROAD_TIGHT_ARC_MAX);
    }
    seg->length = arc;
    seg->curvature = (float)sign / radius;
}

static void choose_uphill_curve(uint32_t *rng, int sign, int force_gentle,
                                road_segment_t *seg)
{
    int tight = ((rng_next(rng) % 4u) != 0u); /* 75 % tight */
    float radius;
    float arc;

    if (force_gentle)
        tight = 0;
    if (tight) {
        radius = rng_range(rng, ROAD_TIGHT_RADIUS_MIN,
                           ROAD_TIGHT_RADIUS_MAX);
        arc = rng_range(rng, ROAD_TIGHT_ARC_MIN, ROAD_TIGHT_ARC_MAX);
    } else {
        radius = rng_range(rng, ROAD_GENTLE_RADIUS_MIN,
                           ROAD_GENTLE_RADIUS_MAX);
        arc = rng_range(rng, ROAD_GENTLE_ARC_MIN, ROAD_GENTLE_ARC_MAX);
    }
    seg->curvature = (float)sign / radius;
    seg->length = arc;
}

static void choose_uphill_forward(road_segment_t *seg)
{
    if (fwd_chain_left > 0) {
        int sign = -fwd_chain_sign;

        choose_uphill_curve(&fwd_rng, sign, 0, seg);
        fwd_chain_sign = sign;
        fwd_chain_left--;
        return;
    }
    if (fwd_chain_started) {
        seg->length = rng_range(&fwd_rng, ROAD_STRAIGHT_MIN,
                                ROAD_STRAIGHT_MAX);
        seg->curvature = 0.0f;
        fwd_chain_started = 0;
        return;
    }

    fwd_chain_left = 2 + (int)(rng_next(&fwd_rng) % 4u);
    {
        int sign = (rng_next(&fwd_rng) & 1u) ? 1 : -1;

        choose_uphill_curve(&fwd_rng, sign, forward_first_curve, seg);
        forward_first_curve = 0;
        fwd_chain_sign = sign;
    }
    fwd_chain_left--;
    fwd_chain_started = 1;
}

static void choose_uphill_backward(road_segment_t *seg)
{
    if (bwd_chain_left > 0) {
        int sign = -bwd_chain_sign;

        choose_uphill_curve(&bwd_rng, sign, 0, seg);
        bwd_chain_sign = sign;
        bwd_chain_left--;
        return;
    }
    if (bwd_chain_started) {
        seg->length = rng_range(&bwd_rng, ROAD_STRAIGHT_MIN,
                                ROAD_STRAIGHT_MAX);
        seg->curvature = 0.0f;
        bwd_chain_started = 0;
        return;
    }

    bwd_chain_left = 2 + (int)(rng_next(&bwd_rng) % 4u);
    {
        int sign = (rng_next(&bwd_rng) & 1u) ? 1 : -1;

        choose_uphill_curve(&bwd_rng, sign, backward_first_curve, seg);
        backward_first_curve = 0;
        bwd_chain_sign = sign;
    }
    bwd_chain_left--;
    bwd_chain_started = 1;
}

static void choose_forward(road_segment_t *seg)
{
    if (forward_first) {
#ifdef RACING_DIAG_CREST
        seg->length = 60.0f;
#else
        seg->length = ROAD_START_STRAIGHT;
#endif
        seg->curvature = 0.0f;
        forward_first = 0;
        forward_next_curve = 1;
        return;
    }

    if (fwd_landing_state == 1) {
        seg->length = ROAD_LANDING_FLATTEN;
        seg->curvature = 0.0f;
        return;
    }
    if (fwd_landing_state == 2) {
        seg->length = ROAD_LANDING_RUNWAY;
        seg->curvature = 0.0f;
        return;
    }

#ifdef RACING_DIAG_CREST
    /* The 0.4.0 diagnostic put the first crest on a gentle curve; 0.5.0
     * launches only on straights, so force a short second straight. */
    if (fwd_vert_pending && seg->start_s < 130.0f) {
        seg->length = 60.0f;
        seg->curvature = 0.0f;
        forward_next_curve = 1;
        return;
    }
#endif

    if (active_config.map == ROAD_MAP_UPHILL) {
        choose_uphill_forward(seg);
        return;
    }

    if (active_config.map == ROAD_MAP_DEFAULT &&
        width_tight_due(seg->start_s + world_offset)) {
        choose_curve(&fwd_rng, fwd_bag, &fwd_bag_index, forward_first_curve,
                     &forward_first_curve, 2, seg);
        forward_next_curve = 0;
        return;
    }

    if (forward_next_curve) {
        int force = (active_config.map == ROAD_MAP_DEFAULT &&
                     width_narrow_zone(seg->start_s + world_offset))
                        ? 1
                        : 0;

        choose_curve(&fwd_rng, fwd_bag, &fwd_bag_index, forward_first_curve,
                     &forward_first_curve, force, seg);
        forward_next_curve = 0;
    } else {
        seg->length = rng_range(&fwd_rng, ROAD_STRAIGHT_MIN,
                                ROAD_STRAIGHT_MAX);
        seg->curvature = 0.0f;
        forward_next_curve = 1;
    }
}

static void choose_backward(road_segment_t *seg)
{
    if (backward_first) {
        seg->length = ROAD_START_STRAIGHT;
        seg->curvature = 0.0f;
        backward_first = 0;
        backward_next_curve = 1;
        return;
    }

    if (active_config.map == ROAD_MAP_UPHILL) {
        choose_uphill_backward(seg);
        return;
    }

    if (backward_next_curve) {
        choose_curve(&bwd_rng, bwd_bag, &bwd_bag_index, backward_first_curve,
                     &backward_first_curve, 0, seg);
        backward_next_curve = 0;
    } else {
        seg->length = rng_range(&bwd_rng, ROAD_STRAIGHT_MIN,
                                ROAD_STRAIGHT_MAX);
        seg->curvature = 0.0f;
        backward_next_curve = 1;
    }
}

static float vertical_grade_amp(void)
{
    float pct = active_config.elevation_pct;
    float amp;

    if (!isfinite(pct))
        pct = ROAD_TERRAIN_DEFAULT_PCT;
    pct = clampf(pct, ROAD_TERRAIN_MIN_PCT, ROAD_TERRAIN_MAX_PCT);
    amp = ROAD_GRADE_BASE * pct / 100.0f;
    if (active_config.map == ROAD_MAP_HIGHWAY)
        amp *= ROAD_HIGHWAY_GRADE_SCALE;
    return amp;
}

static int vertical_run_max(void)
{
    return road_vertical_run_max(&active_config);
}

static int vertical_next_run(void)
{
    int rmax = vertical_run_max();

    if (active_config.map == ROAD_MAP_UPHILL && fwd_vert_dir < 0) {
        (void)rng_next(&fwd_vert_rng);
        return 1;
    }
    if (active_config.map == ROAD_MAP_UPHILL) {
        int climb = (rmax * 2) / 3;

        if (climb < 1)
            climb = 1;
        return 1 + (int)(rng_next(&fwd_vert_rng) % (uint32_t)climb);
    }
    return 1 + (int)(rng_next(&fwd_vert_rng) % (uint32_t)rmax);
}

static int launch_capable(void)
{
    float rate = 2.0f * vertical_grade_amp() / ROAD_CREST_LENGTH;

    return rate > CAR_GRAVITY * CAR_LAUNCH_FACTOR /
                      (CAR_MAX_SPEED * CAR_MAX_SPEED);
}

static int wide_anchor_near(float world_s, float ahead)
{
    float cycle;
    float phase;
    int k0;
    int i;

    if (active_config.map != ROAD_MAP_DEFAULT)
        return 0;
    cycle = road_width_cycle_length(&active_config);
    phase = width_phase();
    k0 = (int)floorf((world_s - phase) / cycle);
    for (i = 0; i <= 1; i++) {
        width_cycle_t *c = width_cycle_get(k0 + i);
        float lo;
        float hi;

        if (c->kind != WIDTH_EVENT_WIDE)
            continue;
        lo = c->s0 + c->flat_start + ROAD_TIGHT_WINDOW_OFFSET;
        hi = lo + ROAD_TIGHT_WINDOW_LEN;
        if (!(hi + 20.0f < world_s || lo > world_s + ahead))
            return 1;
    }
    return 0;
}

static void set_forward_vertical(road_segment_t *seg, float start_h,
                                 float start_g)
{
    float amp = vertical_grade_amp();
    int straight = fabsf(seg->curvature) < 1e-6f;
    float end_g;
    float crest_len;

    if (fwd_landing_state == 1) {
        seg->start_height = start_h;
        seg->start_grade = start_g;
        seg->crest_len = seg->length;
        seg->grade_rate = (0.0f - start_g) / seg->length;
        fwd_landing_state = 2;
        return;
    }
    if (fwd_landing_state == 2) {
        seg->start_height = start_h;
        seg->start_grade = 0.0f;
        seg->crest_len = seg->length;
        seg->grade_rate = 0.0f;
        fwd_landing_state = 0;
        fwd_vert_pending = 0;
        fwd_vert_run_left = vertical_next_run();
        return;
    }

    if (fwd_vert_pending && straight) {
        if (launch_capable() &&
            wide_anchor_near(seg->start_s + world_offset, 210.0f)) {
            end_g = start_g;
            crest_len = seg->length;
        } else {
            if (launch_capable())
                seg->length = ROAD_CREST_LENGTH;
            end_g = -(float)fwd_vert_dir * amp;
            crest_len = fminf(ROAD_CREST_LENGTH, seg->length);
            fwd_vert_pending = 0;
            fwd_vert_dir = -fwd_vert_dir;
            fwd_vert_run_left = vertical_next_run();
            if (launch_capable() && end_g < 0.0f)
                fwd_landing_state = 1;
        }
    } else if (fwd_vert_pending && !straight) {
        end_g = start_g;
        crest_len = seg->length;
    } else {
        fwd_vert_run_left--;
        if (fwd_vert_run_left <= 0)
            fwd_vert_pending = 1;
        end_g = straight ? (float)fwd_vert_dir * amp : start_g;
        crest_len = seg->length;
    }

    if (crest_len < 0.0f)
        crest_len = 0.0f;
    if (crest_len > seg->length)
        crest_len = seg->length;
    seg->start_height = start_h;
    seg->start_grade = start_g;
    seg->crest_len = crest_len;
    seg->grade_rate = (crest_len > 1e-6f) ? (end_g - start_g) / crest_len
                                          : 0.0f;
}

static void set_backward_vertical(road_segment_t *seg, float end_h,
                                  float end_g)
{
    float amp = vertical_grade_amp();
    int tight = fabsf(seg->curvature) > (1.0f / 60.0f) + 1e-6f;
    float start_g;

    if (bwd_vert_first) {
        start_g = end_g;
        bwd_vert_first = 0;
    } else if (tight) {
        start_g = end_g;
    } else {
        start_g = (float)bwd_vert_dir * amp;
        bwd_vert_run_left--;
        if (bwd_vert_run_left <= 0) {
            bwd_vert_dir = -bwd_vert_dir;
            bwd_vert_run_left = 1 + (int)(rng_next(&bwd_vert_rng) %
                                          (uint32_t)vertical_run_max());
        }
    }

    seg->start_grade = start_g;
    seg->crest_len = seg->length;
    seg->grade_rate = (seg->length > 1e-6f)
                          ? (end_g - start_g) / seg->length
                          : 0.0f;
    seg->start_height = end_h - 0.5f * (start_g + end_g) * seg->length;
}

/* ------------------------------------------------------------- guardrail */

/*
 * Dedicated rail stream. The draw is taken even when rails are disabled so
 * that enabling/disabling the feature cannot shift any later decision.
 */
static int rail_draw(uint32_t *state, float density)
{
    uint32_t r = rng_next(state);

    return (float)(r >> 8) * RNG_SCALE < clampf(density, 0.0f, 1.0f);
}

/*
 * Decide the rail sides of a new segment. Outside of a tight curve is the
 * drawn case (density); inside of a blind curve is unconditional. The two
 * rules always land on opposite sides, so a side's kind is unambiguous.
 */
static void assign_rail(road_segment_t *seg, uint32_t *rng)
{
    int side = 0;
    int tight = fabsf(seg->curvature) >= ROAD_TIGHT_CURVATURE;
    int blind = fabsf(seg->curvature) > 1e-5f &&
                seg->start_grade <= -ROAD_RAIL_BLIND_GRADE;

    if (tight && rail_draw(rng, active_config.rail_density))
        side |= (seg->curvature > 0.0f) ? ROAD_RAIL_SIDE_LEFT
                                        : ROAD_RAIL_SIDE_RIGHT;
    if (blind)
        side |= (seg->curvature > 0.0f) ? ROAD_RAIL_SIDE_RIGHT
                                        : ROAD_RAIL_SIDE_LEFT;
    if (!active_config.rail_enabled)
        side = 0;
    seg->rail_side = (signed char)side;
}

static int rail_span_kind(const road_segment_t *seg, int side)
{
    int inside_side = (seg->curvature > 0.0f) ? 1 : -1;

    return (side == inside_side) ? ROAD_RAIL_KIND_INSIDE
                                 : ROAD_RAIL_KIND_OUTSIDE;
}

static void append_forward(void)
{
    road_segment_t seg;
    float start_h = 0.0f;
    float start_g = 0.0f;

    if (count == 0) {
        seg.start_s = 0.0f;
        seg.start_x = 0.0f;
        seg.start_z = 0.0f;
        seg.start_heading = 0.0f;
    } else {
        road_segment_t *prev = at(count - 1);
        road_sample_t end = eval_segment(prev, prev->length);

        seg.start_s = prev->start_s + prev->length;
        seg.start_x = end.x;
        seg.start_z = end.z;
        seg.start_heading = end.heading;
        start_h = end.height;
        start_g = end.grade;
    }

    choose_forward(&seg);
    set_forward_vertical(&seg, start_h, start_g);
    assign_rail(&seg, &rail_fwd_rng);
    push_back(seg);
}

static void prepend_backward(void)
{
    road_segment_t *first = at(0);
    float jx = first->start_x;
    float jz = first->start_z;
    float jh = first->start_heading;
    road_segment_t seg;

    choose_backward(&seg);
    if (active_config.map == ROAD_MAP_DEFAULT && seg.curvature != 0.0f &&
        (width_narrow_zone(first->start_s - seg.length + world_offset) ||
         width_narrow_zone(first->start_s + world_offset))) {
        int sign = seg.curvature > 0.0f ? 1 : -1;
        float radius = rng_range(&bwd_rng, ROAD_GENTLE_RADIUS_MIN,
                                 ROAD_GENTLE_RADIUS_MAX);
        float arc = rng_range(&bwd_rng, ROAD_GENTLE_ARC_MIN,
                              ROAD_GENTLE_ARC_MAX);

        seg.curvature = (float)sign / radius;
        seg.length = arc;
    }
    set_backward_vertical(&seg, first->start_height, first->start_grade);
    seg.start_heading = jh - seg.curvature * seg.length;

    if (fabsf(seg.curvature) < 1e-5f) {
        seg.start_x = jx + seg.length * sinf(seg.start_heading);
        seg.start_z = jz - seg.length * cosf(seg.start_heading);
    } else {
        seg.start_x =
            jx - (cosf(jh) - cosf(seg.start_heading)) / seg.curvature;
        seg.start_z =
            jz - (sinf(jh) - sinf(seg.start_heading)) / seg.curvature;
    }
    seg.start_s = first->start_s - seg.length;
    assign_rail(&seg, &rail_bwd_rng);
    push_front(seg);
}

/* ---------------------------------------------------------------- public */

void road_reset(uint32_t seed)
{
    road_reset_config(seed, NULL);
}

void road_reset_config(uint32_t seed, const road_config_t *cfg)
{
    road_config_t validated;

    if (cfg) {
        validated = *cfg;
    } else {
        road_config_defaults(&validated);
    }
    (void)road_config_validate(&validated);
    active_config = validated;

    head = 0;
    count = 0;
    world_offset = 0.0f;
    width_seed = seed ? seed : 1u;
    width_cache_clear();
    fwd_rng = seed ? seed : 1u;
    bwd_rng = seed ^ 0xA5A5A5A5u;
    if (bwd_rng == 0)
        bwd_rng = 1u;
    rail_fwd_rng = seed ^ 0x2C1B3C6Du;
    if (rail_fwd_rng == 0)
        rail_fwd_rng = 1u;
    rail_bwd_rng = seed ^ 0x5C6E9A31u;
    if (rail_bwd_rng == 0)
        rail_bwd_rng = 1u;
    fwd_vert_rng = seed ^ 0x9E3779B9u;
    if (fwd_vert_rng == 0)
        fwd_vert_rng = 1u;
    bwd_vert_rng = seed ^ 0x6D2B79F5u;
    if (bwd_vert_rng == 0)
        bwd_vert_rng = 1u;
    fwd_vert_dir = 1;
#ifdef RACING_DIAG_CREST
    fwd_vert_run_left = 1;
#else
    fwd_vert_run_left = 2;
#endif
    fwd_vert_pending = 0;
    bwd_vert_dir = 1;
    bwd_vert_run_left = 2;
    bwd_vert_first = 1;
    fwd_bag_index = 4;
    bwd_bag_index = 4;
    fwd_chain_left = 0;
    fwd_chain_sign = 0;
    fwd_chain_started = 0;
    bwd_chain_left = 0;
    bwd_chain_sign = 0;
    bwd_chain_started = 0;
    fwd_landing_state = 0;
    forward_first = 1;
    forward_next_curve = 0;
    forward_first_curve = 1;
    backward_first = 1;
    backward_next_curve = 0;
    backward_first_curve = 1;

    append_forward();
    road_update(0.0f);
}

float road_update(float car_s)
{
    /* Drop outside the required window, then fill both ends. */
    while (count > 0 &&
           at(0)->start_s + at(0)->length < car_s - ROAD_WINDOW_BEHIND)
        drop_front();
    while (count > 0 &&
           at(count - 1)->start_s > car_s + ROAD_WINDOW_AHEAD)
        drop_back();

    while (count == 0 ||
           at(count - 1)->start_s + at(count - 1)->length <
               car_s + ROAD_WINDOW_AHEAD) {
        make_room(1);
        append_forward();
    }
    while (count > 0 && at(0)->start_s > car_s - ROAD_WINDOW_BEHIND) {
        make_room(0);
        prepend_backward();
    }

    while (fabsf(car_s) > ROAD_REBASE_LIMIT) {
        float origin_s;
        road_sample_t a;
        int i;

        if (car_s > 0.0f) {
            origin_s = floorf((car_s - ROAD_REBASE_STEP) /
                              ROAD_REBASE_ALIGN) * ROAD_REBASE_ALIGN;
        } else {
            origin_s = ceilf((car_s + ROAD_REBASE_STEP) /
                             ROAD_REBASE_ALIGN) * ROAD_REBASE_ALIGN;
        }

        a = road_sample(origin_s);

        for (i = 0; i < count; i++) {
            road_segment_t *seg = at(i);
            float dx = seg->start_x - a.x;
            float dz = seg->start_z - a.z;

            seg->start_x = dx * cosf(a.heading) + dz * sinf(a.heading);
            seg->start_z = -dx * sinf(a.heading) + dz * cosf(a.heading);
            seg->start_heading = normalize_angle(seg->start_heading -
                                                 a.heading);
            seg->start_s -= origin_s;
            seg->start_height -= a.height;
        }
        car_s -= origin_s;
        world_offset += origin_s;
        width_cache_clear();

        while (count > 0 &&
               at(0)->start_s + at(0)->length <
                   car_s - ROAD_WINDOW_BEHIND)
            drop_front();
        while (count > 0 &&
               at(0)->start_s > car_s - ROAD_WINDOW_BEHIND) {
            make_room(0);
            prepend_backward();
        }
    }

    return car_s;
}

road_sample_t road_sample(float s)
{
    int i = 0;
    road_segment_t *seg;
    road_sample_t p;
    float t;

    if (count == 0) {
        p.x = p.z = p.heading = p.curvature = 0.0f;
        p.height = p.grade = p.grade_rate = 0.0f;
        p.half_width = p.edge_half_width = 0.0f;
        p.rail_limit_left = p.rail_limit_right = 0.0f;
        return p;
    }

    while (i < count - 1 && s >= at(i)->start_s + at(i)->length)
        i++;

    seg = at(i);
    t = s - seg->start_s;
    p = eval_segment(seg, t);
    p.half_width = width_at(s + world_offset);
    p.edge_half_width = p.half_width + ROAD_SHOULDER_WIDTH;

    /* Per-side rail limits. The windows come from the segments i-1..i+1 (the
     * grammar's 25 m minimum segment makes any further one unreachable) plus
     * the world-anchored narrow corridors k0-1..k0+1: at most six candidates
     * per side. They are UNIONED first and the shared predicate is evaluated
     * on that merged interval, because a per-window envelope has already sunk
     * where two railed windows overlap while the drawn beam is at full height
     * (a drive-through hole). No call back into road_sample() from here. */
    p.rail_limit_left = 0.0f;
    p.rail_limit_right = 0.0f;
    if (active_config.rail_enabled && road_rail_allowed(p.half_width)) {
        float la[RAIL_CAND_MAX], lb[RAIL_CAND_MAX];
        float ra[RAIL_CAND_MAX], rb[RAIL_CAND_MAX];
        float rail = p.half_width - road_rail_stop(p.half_width);
        float world_s = s + world_offset;
        float u0;
        float u1;
        int nl = 0;
        int nr = 0;
        int j;

        for (j = i - 1; j <= i + 1; j++) {
            const road_segment_t *near;
            float a;
            float b;

            if (j < 0 || j >= count)
                continue;
            near = at(j);
            a = near->start_s - ROAD_RAIL_LEAD;
            b = near->start_s + near->length + ROAD_RAIL_TRAIL;
            if (near->rail_side & ROAD_RAIL_SIDE_LEFT) {
                la[nl] = a;
                lb[nl] = b;
                nl++;
            }
            if (near->rail_side & ROAD_RAIL_SIDE_RIGHT) {
                ra[nr] = a;
                rb[nr] = b;
                nr++;
            }
        }
        {
            float cycle = road_width_cycle_length(&active_config);
            int k0 = (int)floorf((world_s - width_phase()) / cycle);
            int k;

            for (k = k0 - 1; k <= k0 + 1; k++) {
                float a;
                float b;

                if (!narrow_rail_window(k, &a, &b))
                    continue;
                a -= world_offset;
                b -= world_offset;
                la[nl] = a;
                lb[nl] = b;
                nl++;
                ra[nr] = a;
                rb[nr] = b;
                nr++;
            }
        }
        if (nl > 0 && road_rail_union(s, la, lb, nl, &u0, &u1) &&
            road_rail_ok(&p, s, u0, u1))
            p.rail_limit_left = rail;
        if (nr > 0 && road_rail_union(s, ra, rb, nr, &u0, &u1) &&
            road_rail_ok(&p, s, u0, u1))
            p.rail_limit_right = rail;
    }
    return p;
}

float road_rail_stop(float half_width)
{
    (void)half_width; /* the wheel face does not narrow with the road */
    return ROAD_RAIL_STOP_FULL;
}

bool road_rail_allowed(float half_width)
{
    return (half_width - ROAD_RAIL_STOP_FULL) >= ROAD_RAIL_MIN_CENTRE;
}

bool road_rail_ok(const road_sample_t *p, float s, float s0, float s1)
{
    if (!p)
        return false;
    /* Eligibility, NOT "stop > 0": the stop is a constant, so testing it
     * would be always true and would draw a rail the car is not clamped by. */
    return road_rail_allowed(p->half_width) &&
           guardrail_beam_top(s, s0, s1) >= ROAD_RAIL_CLAMP_MIN_TOP;
}

bool road_rail_band_ok(const road_sample_t *a, float a_s,
                       const road_sample_t *b, float b_s, float s0, float s1)
{
    /* Both endpoints, never the midpoint: no band may straddle the
     * eligibility or visibility boundary. */
    return road_rail_ok(a, a_s, s0, s1) && road_rail_ok(b, b_s, s0, s1);
}

bool road_rail_union(float s, const float *a, const float *b, int n, float *u0,
                     float *u1)
{
    int i;
    int changed = 1;
    bool found = false;

    if (!a || !b || !u0 || !u1 || n <= 0)
        return false;
    for (i = 0; i < n; i++) {
        if (s >= a[i] && s <= b[i]) {
            *u0 = a[i];
            *u1 = b[i];
            found = true;
            break;
        }
    }
    if (!found)
        return false;
    while (changed) { /* absorb every window touching the union */
        changed = 0;
        for (i = 0; i < n; i++) {
            if (a[i] <= *u1 && b[i] >= *u0) {
                if (a[i] < *u0) {
                    *u0 = a[i];
                    changed = 1;
                }
                if (b[i] > *u1) {
                    *u1 = b[i];
                    changed = 1;
                }
            }
        }
    }
    return true;
}

bool road_rail_merge(float a0, float b0, float a1, float b1, float *o0,
                     float *o1)
{
    if (!o0 || !o1)
        return false;
    if (b0 < a1 || b1 < a0)
        return false; /* disjoint (touching windows merge) */
    *o0 = (a0 < a1) ? a0 : a1;
    *o1 = (b0 > b1) ? b0 : b1;
    return true;
}

float road_rail_limit(const road_sample_t *p, int side)
{
    float plain;
    float rail;

    if (!p || p->edge_half_width <= 0.0f)
        return 0.0f;
    plain = p->edge_half_width - CAR_EDGE_MARGIN;
    rail = (side > 0) ? p->rail_limit_right : p->rail_limit_left;
    return (rail > 0.0f && rail < plain) ? rail : plain;
}

float road_world_offset(void)
{
    return world_offset;
}

static void rail_sort(road_rail_span_t *out, int n)
{
    int i;

    for (i = 1; i < n; i++) {
        road_rail_span_t key = out[i];
        int j = i - 1;

        while (j >= 0 && (out[j].s0 > key.s0 ||
                          (out[j].s0 == key.s0 && out[j].side > key.side))) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = key;
    }
}

static int rail_merge(road_rail_span_t *src, int n, road_rail_span_t *out)
{
    int m = 0;
    int s;
    int i;

    for (s = 0; s < 2; s++) {
        int side = (s == 0) ? -1 : 1;
        int start = m;

        for (i = 0; i < n; i++) {
            float o0;
            float o1;

            if (src[i].side != side)
                continue;
            /* One overlap rule for the collector and the sampler. The 1 mm
             * slack absorbs float noise between two windows that touch; the
             * union then takes the earlier start (always the open span here,
             * because src is sorted by s0) and the later end. */
            if (m > start &&
                road_rail_merge(out[m - 1].s0, out[m - 1].s1,
                                src[i].s0 - 1e-3f, src[i].s1, &o0, &o1)) {
                out[m - 1].s0 = o0;
                out[m - 1].s1 = o1;
                out[m - 1].kind |= src[i].kind;
            } else {
                out[m++] = src[i];
            }
        }
    }
    rail_sort(out, m);
    return m;
}

/* ------------------------------------------------------- visible interval */

/*
 * The clamp span is the VISIBLE interval [v0, v1], not the grammar span: the
 * 12 m rise/sink buries the beam at every span end, so a clamp on the grammar
 * span would be an invisible wall. road_rail_ok() itself never calls
 * road_sample(); this helper is where the collector supplies the sample.
 */
static bool rail_visible(float s, float s0, float s1)
{
    road_sample_t p = road_sample(s);

    return road_rail_ok(&p, s, s0, s1);
}

/* ROAD_RAIL_BISECT_STEPS over a bracket of at most ROAD_RAIL_SCAN_STEP: the
 * emitted boundary is the predicate boundary to 10/2^8 = 0.039 m, never a
 * grid point, and the whole pass stays inside ROAD_RAIL_SAMPLE_BUDGET on the
 * reference frame (car_s 1000: 192 calls), on the repository's 64-seed sweep
 * (228) and on the sampled random-seed sweeps (279-290); the constant is a
 * cost target, not a proof over every orbit. */
static float rail_bisect_in(float a, float b, float s0, float s1,
                            road_rail_visible_fn visible)
{
    int i;

    for (i = 0; i < ROAD_RAIL_BISECT_STEPS; i++) {
        float m = 0.5f * (a + b);

        if (visible(m, s0, s1))
            b = m;
        else
            a = m;
    }
    return b;
}

static float rail_bisect_out(float a, float b, float s0, float s1,
                             road_rail_visible_fn visible)
{
    int i;

    for (i = 0; i < ROAD_RAIL_BISECT_STEPS; i++) {
        float m = 0.5f * (a + b);

        if (visible(m, s0, s1))
            a = m;
        else
            b = m;
    }
    return a;
}

int road_rail_visible_runs(float s0, float s1, road_rail_visible_fn visible,
                           float *runs, int max_runs)
{
    const float step = ROAD_RAIL_SCAN_STEP;
    float prev = s0;
    float start = s0;
    bool prev_ok;
    bool open = false;
    int n = 0;
    float s;

    if (!visible || !runs || max_runs <= 0 || s1 <= s0)
        return 0;
    prev_ok = visible(s0, s0, s1);
    if (prev_ok) {
        start = s0;
        open = true;
    }
    for (s = s0 + step;; s += step) {
        bool ok;

        if (s > s1)
            s = s1;
        ok = visible(s, s0, s1);
        if (ok && !prev_ok) {
            /* false -> true: a NEW run, never a continuation of the previous
             * one. Bridging the two would draw bands inside the gap, where
             * rail_limit_* is 0: a drive-through rail of exactly the class
             * the merged-union work exists to prevent. */
            start = rail_bisect_in(prev, s, s0, s1, visible);
            open = true;
        } else if (!ok && prev_ok && open) {
            float end = rail_bisect_out(prev, s, s0, s1, visible);

            if (end > start && n < max_runs) {
                runs[2 * n] = start;
                runs[2 * n + 1] = end;
                n++;
            }
            open = false;
        }
        prev = s;
        prev_ok = ok;
        if (s >= s1)
            break;
    }
    if (open && n < max_runs) {
        runs[2 * n] = start;
        runs[2 * n + 1] = s1;
        n++;
    }
    return n;
}

int road_rail_collect(float s0, float s1, road_rail_span_t *out, int max)
{
    road_rail_span_t tmp[ROAD_RAIL_MAX_SPANS];
    int cap = (max < ROAD_RAIL_MAX_SPANS) ? max : ROAD_RAIL_MAX_SPANS;
    int n = 0;
    int m;
    int k;
    int i;
    float cycle;

    if (!out || cap <= 0)
        return 0;
    if (s0 > s1) {
        float swap = s0;

        s0 = s1;
        s1 = swap;
    }

    /* Keep room for the narrow intervals appended below: a dense curve
     * stretch must never starve the one-lane corridor rails that physics
     * still clamps (an invisible wall). At most 4 cycles x 2 sides reach the
     * cap for the renderer's 520 m query; 8 slots cover wider ones. */
    {
        int curve_cap = cap - 8;

        if (curve_cap < 0)
            curve_cap = 0;
        for (i = 0; i < count && n < curve_cap; i++) {
            const road_segment_t *seg = at(i);
            float a = seg->start_s - ROAD_RAIL_LEAD;
            float b = seg->start_s + seg->length + ROAD_RAIL_TRAIL;

            if (b < s0 || a > s1)
                continue;
            if (seg->rail_side & ROAD_RAIL_SIDE_LEFT) {
                tmp[n].s0 = a;
                tmp[n].s1 = b;
                tmp[n].side = -1;
                tmp[n].kind = (unsigned char)rail_span_kind(seg, -1);
                n++;
            }
            if ((seg->rail_side & ROAD_RAIL_SIDE_RIGHT) && n < curve_cap) {
                tmp[n].s0 = a;
                tmp[n].s1 = b;
                tmp[n].side = 1;
                tmp[n].kind = (unsigned char)rail_span_kind(seg, 1);
                n++;
            }
        }
    }

    cycle = road_width_cycle_length(&active_config);
    {
        float world_s0 = s0 + world_offset;
        float world_s1 = s1 + world_offset;
        int k0 = (int)floorf((world_s0 - width_phase()) / cycle);
        int k1 = (int)floorf((world_s1 - width_phase()) / cycle);
        int k;

        for (k = k0 - 1; k <= k1 + 1; k++) {
            float a;
            float b;
            int side;

            if (!narrow_rail_window(k, &a, &b))
                continue;
            if (b < world_s0 || a > world_s1)
                continue;
            for (side = 0; side < 2 && n < cap; side++) {
                tmp[n].s0 = a - world_offset;
                tmp[n].s1 = b - world_offset;
                tmp[n].side = (side == 0) ? -1 : 1;
                tmp[n].kind = ROAD_RAIL_KIND_NARROW;
                n++;
            }
        }
    }

    rail_sort(tmp, n);
    m = rail_merge(tmp, n, out);
    /*
     * MERGE FIRST, CLIP SECOND (M-3). Clipping before merging would leave the
     * two pieces of a merged pair carrying their original s0/s1, so a later
     * merge step would re-collapse or duplicate them and the count would
     * silently go back to the pre-fix value. A merged span with no visible
     * run has no clamp and is not drawn at all; one with SEVERAL runs (a false
     * gap in the middle) becomes one emitted span per run, each keeping the
     * grammar [s0, s1] for the envelope and its own [v0, v1]. tmp[] is free
     * here (the window list has already been merged) and the pieces must not
     * be written into out[] in place: a split can move k past i.
     */
    k = 0;
    for (i = 0; i < m && k < cap; i++) {
        float runs[2 * ROAD_RAIL_MAX_VISIBLE_RUNS];
        int rn = road_rail_visible_runs(out[i].s0, out[i].s1, rail_visible,
                                        runs, ROAD_RAIL_MAX_VISIBLE_RUNS);
        int r;

        for (r = 0; r < rn && k < cap; r++) {
            tmp[k] = out[i];
            tmp[k].v0 = runs[2 * r];
            tmp[k].v1 = runs[2 * r + 1];
            k++;
        }
    }
    for (i = 0; i < k; i++)
        out[i] = tmp[i];
    return k;
}

bool road_next_curve(float from_s, float ahead, float min_curvature,
                     float *distance, float *curvature)
{
    int i;

    for (i = 0; i < count; i++) {
        const road_segment_t *seg = at(i);
        float d;

        if (seg->start_s < from_s + 1.0f)
            continue;
        if (fabsf(seg->curvature) < min_curvature)
            continue;
        d = seg->start_s - from_s;
        if (d > ahead)
            continue;
        if (distance)
            *distance = d;
        if (curvature)
            *curvature = seg->curvature;
        return true;
    }
    return false;
}

bool road_next_narrow(float from_s, float ahead, float max_half,
                      float *distance, float *half_width)
{
    float s;

    for (s = from_s + 1.0f; s <= from_s + ahead; s += ROAD_WARN_STEP) {
        road_sample_t p = road_sample(s);

        if (p.half_width <= max_half) {
            if (distance)
                *distance = s - from_s;
            if (half_width)
                *half_width = p.half_width;
            return true;
        }
    }
    return false;
}

bool road_next_crest(float from_s, float ahead, float max_rate,
                     float *distance, float *grade_rate)
{
    float s;

    for (s = from_s + 1.0f; s <= from_s + ahead; s += ROAD_WARN_STEP) {
        road_sample_t p = road_sample(s);

        if (p.grade_rate <= -max_rate) {
            if (distance)
                *distance = s - from_s;
            if (grade_rate)
                *grade_rate = p.grade_rate;
            return true;
        }
    }
    return false;
}

void road_warnings(float car_s, road_warning_t *out)
{
    float distance;
    float value;

    if (!out)
        return;
    out->narrow = road_next_narrow(car_s, ROAD_WARN_AHEAD,
                                   ROAD_NARROW_WARN_HALF, &distance, &value);
    out->narrow_distance = out->narrow ? distance : 0.0f;
    out->crest = road_next_crest(car_s, ROAD_WARN_AHEAD,
                                 ROAD_CREST_WARN_RATE, &distance, &value);
    out->crest_distance = out->crest ? distance : 0.0f;
}

int road_segment_count(void)
{
    return count;
}

const road_segment_t *road_segment_at(int index)
{
    if (index < 0 || index >= count)
        return 0;
    return at(index);
}
