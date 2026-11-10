/*
 * guardrail_gu.c - sceGu drawing hook for the Racing 3D guardrail.
 *
 * All geometry is written into one sceGuGetMemory() block per frame: the beam
 * rows (inner face + top face) and the posts of every visible span, plus the
 * six vertices of the contact flash when the latch is lit. Nothing is
 * allocated on the heap and nothing here touches state the caller owns beyond
 * the flash's depth-test/fog disable, which is restored immediately.
 */

#include "assets/guardrail/guardrail_gu.h"

#include <pspgu.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "assets/guardrail/guardrail.h"

/* Just inside the 420 m fog end: beyond it the rail is sky-coloured. */
#define GUARDRAIL_AHEAD 410.0f
#define GUARDRAIL_POST_OFFSET 0.02f
#define GUARDRAIL_POST_HALF_WIDTH 0.05f

/*
 * GU_COLOR_8888 is ABGR with the alpha byte on top, exactly like render.c's
 * RGB() macro: 0xFF | b<<16 | g<<8 | r. A bare 24-bit literal would carry
 * alpha 0 and render fully transparent under the frame-wide GU_BLEND, so
 * every colour here is built through this macro and is always opaque.
 */
#define GUARDRAIL_RGB(r, g, b)                                                 \
    (0xFF000000u | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) |              \
     (uint32_t)(r))

/*
 * Both colours stay outside every frozen probe predicate, including after fog
 * pulls them toward COL_SKY (135, 206, 235):
 *   beam (70,96,120): b-g = 50, g-r = 26.
 *   post (56,78,98):  b-g = 20, g-r = 22.
 * `is_asphalt` rule 2 needs b-g <= 6 and |r-g| < 25; fog can only push b-g up
 * toward 29, so both stay out. `is_bright` needs every channel > 160 (false),
 * `is_amber` needs g > 80 with r > 200 (false), `is_ground` needs g-b > 15
 * (false). The post used to be (110,106,102), which is one unit from rule 2's
 * r > 110 and becomes asphalt-classified once 0 < fog <= 0.303.
 */
#define GUARDRAIL_BEAM_COLOR GUARDRAIL_RGB(70, 96, 120) /* painted steel */
#define GUARDRAIL_POST_COLOR GUARDRAIL_RGB(56, 78, 98)  /* dark steel */
#define GUARDRAIL_FLASH_COLOR GUARDRAIL_RGB(216, 40, 24)  /* crimson */

#define GUARDRAIL_V3D (GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D)
#define GUARDRAIL_V2D (GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D)

typedef struct {
    uint32_t color;
    float x, y, z;
} GuVtx;

static void put(GuVtx *v, int *n, uint32_t color, float x, float y, float z)
{
    v[*n].color = color;
    v[*n].x = x;
    v[*n].y = y;
    v[*n].z = z;
    (*n)++;
}

/* A 5 m band of the beam: inner face plus a thin top face. */
/*
 * One band between two already-sampled row boundaries. Keeping the samples
 * out here lets the caller sample each boundary once instead of twice per
 * band (the dominant per-frame cost on the PSP).
 */
static int emit_band(GuVtx *v, int n, int max, const road_rail_span_t *span,
                     const road_sample_t *psa, const road_sample_t *psb,
                     float a, float b)
{
    road_sample_t pa = *psa;
    road_sample_t pb = *psb;
    float side = (float)span->side;
    float ax = pa.x + side * pa.edge_half_width * -cosf(pa.heading);
    float az = pa.z + side * pa.edge_half_width * -sinf(pa.heading);
    float bx = pb.x + side * pb.edge_half_width * -cosf(pb.heading);
    float bz = pb.z + side * pb.edge_half_width * -sinf(pb.heading);
    float a_bot = pa.height + guardrail_beam_bottom(a, span->s0, span->s1);
    float a_top = pa.height + guardrail_beam_top(a, span->s0, span->s1);
    float b_bot = pb.height + guardrail_beam_bottom(b, span->s0, span->s1);
    float b_top = pb.height + guardrail_beam_top(b, span->s0, span->s1);
    float a_out_x = ax + side * GUARDRAIL_BEAM_THICKNESS * -cosf(pa.heading);
    float a_out_z = az + side * GUARDRAIL_BEAM_THICKNESS * -sinf(pa.heading);
    float b_out_x = bx + side * GUARDRAIL_BEAM_THICKNESS * -cosf(pb.heading);
    float b_out_z = bz + side * GUARDRAIL_BEAM_THICKNESS * -sinf(pb.heading);
    uint32_t col = GUARDRAIL_BEAM_COLOR;

    if (n + GUARDRAIL_VERTS_PER_BAND > max)
        return n;

    /* inner face */
    put(v, &n, col, ax, a_bot, az);
    put(v, &n, col, ax, a_top, az);
    put(v, &n, col, bx, b_top, bz);
    put(v, &n, col, ax, a_bot, az);
    put(v, &n, col, bx, b_top, bz);
    put(v, &n, col, bx, b_bot, bz);
    /* top face, outwards from the inner plane */
    put(v, &n, col, ax, a_top, az);
    put(v, &n, col, a_out_x, a_top, a_out_z);
    put(v, &n, col, b_out_x, b_top, b_out_z);
    put(v, &n, col, ax, a_top, az);
    put(v, &n, col, b_out_x, b_top, b_out_z);
    put(v, &n, col, bx, b_top, bz);
    return n;
}

/* One post: a small vertical quad facing the road, at the beam plane. */
static int emit_post(GuVtx *v, int n, int max, const road_rail_span_t *span,
                     float s)
{
    road_sample_t p = road_sample(s);
    float side = (float)span->side;
    float rx = -cosf(p.heading);
    float rz = -sinf(p.heading);
    float tx = -sinf(p.heading);
    float tz = cosf(p.heading);
    float lat = side * (p.edge_half_width + GUARDRAIL_POST_OFFSET);
    float cx = p.x + lat * rx;
    float cz = p.z + lat * rz;
    float top = guardrail_beam_top(s, span->s0, span->s1);
    float x0 = cx - GUARDRAIL_POST_HALF_WIDTH * tx;
    float z0 = cz - GUARDRAIL_POST_HALF_WIDTH * tz;
    float x1 = cx + GUARDRAIL_POST_HALF_WIDTH * tx;
    float z1 = cz + GUARDRAIL_POST_HALF_WIDTH * tz;
    uint32_t col = GUARDRAIL_POST_COLOR;

    if (n + GUARDRAIL_VERTS_PER_POST > max)
        return n;

    put(v, &n, col, x0, p.height, z0);
    put(v, &n, col, x1, p.height, z1);
    put(v, &n, col, x1, p.height + top, z1);
    put(v, &n, col, x0, p.height, z0);
    put(v, &n, col, x1, p.height + top, z1);
    put(v, &n, col, x0, p.height + top, z0);
    return n;
}

void guardrail_draw(const car_state_t *car, const racing_settings_t *settings)
{
    road_rail_span_t spans[GUARDRAIL_MAX_SPANS];
    guardrail_span_ref_t refs[GUARDRAIL_MAX_SPANS];
    unsigned char keep[GUARDRAIL_MAX_SPANS];
    int span_count;
    int flash;
    int rail_len;
    int total;
    int i;
    int n = 0;
    GuVtx *v;

    (void)settings;
    span_count = road_rail_collect(car->s - ROAD_NEAR_BEHIND,
                                   car->s + GUARDRAIL_AHEAD, spans,
                                   GUARDRAIL_MAX_SPANS);
    flash = car_is_rail_hit(car) ? GUARDRAIL_FLASH_VERTS : 0;

    /*
     * Count first so the block is exactly as large as the frame needs.
     * Spans are emitted whole (never clipped: a clipped span would cut the
     * rise/sink envelope), so a span that only overlaps the query edge still
     * costs its full length. The selection helper keeps the spans nearest the
     * car and drops whole spans from the FAR end when the cap binds — physics
     * clamps on the near span whatever is drawn, so an invisible wall at the
     * car is the one failure mode this order exists to prevent.
     */
    for (i = 0; i < span_count; i++) {
        refs[i].s0 = spans[i].s0;
        refs[i].s1 = spans[i].s1;
        refs[i].side = spans[i].side;
        refs[i].kind = spans[i].kind;
    }
    {
        float eye_s = car->s - (GUARDRAIL_EYE_BEHIND_BASE +
                                GUARDRAIL_EYE_BEHIND_PER_SPEED *
                                    fabsf(car->speed));

        rail_len = guardrail_select_spans(refs, span_count, keep,
                                          GUARDRAIL_MAX_VERTS, flash, eye_s);
    }
    total = rail_len + flash;
    if (total <= 0)
        return;
    v = (GuVtx *)sceGuGetMemory((int)((size_t)total * sizeof(GuVtx)));
    if (!v)
        return;

    for (i = 0; i < span_count && n < rail_len; i++) {
        int bands;
        int posts;
        float len;
        int b;
        int k;

        if (!keep[i])
            continue;
        bands = guardrail_band_count(spans[i].s0, spans[i].s1);
        posts = guardrail_post_count(spans[i].s0, spans[i].s1);
        len = spans[i].s1 - spans[i].s0;

        {
            float a = spans[i].s0;
            road_sample_t prev = road_sample(a);

            for (b = 0; b < bands; b++) {
                float e = a + GUARDRAIL_ROW_STEP;
                road_sample_t cur;

                if (e > spans[i].s1)
                    e = spans[i].s1;
                cur = road_sample(e);
                n = emit_band(v, n, rail_len, &spans[i], &prev, &cur, a, e);
                prev = cur;
                a = e;
            }
        }
        k = 0;
        for (b = 0; k < posts; b++) {
            float s = guardrail_post_s(spans[i].s0, spans[i].s1, b);

            if (b * GUARDRAIL_POST_SPACING > len)
                break;
            if (!guardrail_post_ok(s, spans[i].s0, spans[i].s1))
                continue;
            n = emit_post(v, n, rail_len, &spans[i], s);
            k++;
        }
    }

    if (n > 0)
        sceGuDrawArray(GU_TRIANGLES, GUARDRAIL_V3D, n, NULL, v);

    if (flash > 0) {
        int f = rail_len;
        float x0 = (car_rail_side(car) < 0) ? 0.0f : 474.0f;
        float x1 = x0 + 6.0f;
        uint32_t col = GUARDRAIL_FLASH_COLOR;

        put(v, &f, col, x0, 76.0f, 0.0f);
        put(v, &f, col, x1, 76.0f, 0.0f);
        put(v, &f, col, x1, 230.0f, 0.0f);
        put(v, &f, col, x0, 76.0f, 0.0f);
        put(v, &f, col, x1, 230.0f, 0.0f);
        put(v, &f, col, x0, 230.0f, 0.0f);

        sceGuDisable(GU_DEPTH_TEST);
        sceGuDisable(GU_FOG);
        sceGuDisable(GU_TEXTURE_2D);
        sceGuDrawArray(GU_TRIANGLES, GUARDRAIL_V2D, GUARDRAIL_FLASH_VERTS,
                       NULL, v + rail_len);
        sceGuEnable(GU_DEPTH_TEST);
        sceGuEnable(GU_FOG);
    }
}
