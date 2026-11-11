/*
 * signs_gu.c - sceGu drawing for the Racing 3D roadside signs.
 *
 * Every vertex carries the literal sign colour: the probes match the panel
 * face exactly, so render.c's per-face shading factors must not be reused.
 * Per-frame geometry comes from sceGuGetMemory() (uncached display-list
 * memory).
 *
 * GU state: draw_sign() disables the depth test and the depth mask around the
 * symbol-ink range only, and restores both itself before returning (the ink
 * is a co-planar decal). The panel and post stay depth-tested. Nothing else
 * here touches GU state.
 */

#include "assets/roadsigns/signs_gu.h"

#include <math.h>
#include <pspgu.h>
#include <stdint.h>

#include "assets/roadsigns/signs.h"

typedef struct {
    uint32_t color;
    float x, y, z;
} SignVtx;

/* _Static_assert is C11; the build is -std=c99. */
typedef char signs_vtx_size_check[sizeof(SignVtx) == 16 ? 1 : -1];

#define SIGNS_RGB(r, g, b)                                                     \
    (0xFF000000u | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) |             \
     (uint32_t)(r))

#define SIGNS_COL_FACE SIGNS_RGB(230, 180, 0)
#define SIGNS_COL_INK SIGNS_RGB(24, 24, 44)
#define SIGNS_COL_POST SIGNS_RGB(96, 96, 120)

#define SIGNS_VTX_FMT                                                          \
    (GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D)

static int append_quad(SignVtx *v, int n, int max, sign_vec3_t a,
                       sign_vec3_t b, sign_vec3_t c, sign_vec3_t d,
                       uint32_t color)
{
    if (n + 6 > max)
        return n;
    v[n++] = (SignVtx){color, a.x, a.y, a.z};
    v[n++] = (SignVtx){color, b.x, b.y, b.z};
    v[n++] = (SignVtx){color, c.x, c.y, c.z};
    v[n++] = (SignVtx){color, a.x, a.y, a.z};
    v[n++] = (SignVtx){color, c.x, c.y, c.z};
    v[n++] = (SignVtx){color, d.x, d.y, d.z};
    return n;
}

/* A square post from the road surface to the panel bottom, world-vertical. */
static int append_post(SignVtx *v, int n, int max, const sign_t *sign,
                       const sign_vec3_t *centre)
{
    float hw = SIGNS_POST_W;
    float y0 = sign->road_height;
    /* Post top = the panel BOTTOM at every scale. Deriving it as H*s would
     * put the post below the panel at 60 % (plan H1). Exact at grade 0: the
     * tilted panel's bottom-edge CENTRE sits up to 0.0154 m above this line at
     * the grammar's maximum grade (0.12) and 1.6x - sub-pixel at any distance
     * the panel is legible. */
    float y1 = sign->road_height + SIGNS_PANEL_BOTTOM;
    sign_vec3_t p0 = {centre->x - hw, y0, centre->z - hw};
    sign_vec3_t p1 = {centre->x + hw, y0, centre->z - hw};
    sign_vec3_t p2 = {centre->x + hw, y0, centre->z + hw};
    sign_vec3_t p3 = {centre->x - hw, y0, centre->z + hw};
    sign_vec3_t q0 = {centre->x - hw, y1, centre->z - hw};
    sign_vec3_t q1 = {centre->x + hw, y1, centre->z - hw};
    sign_vec3_t q2 = {centre->x + hw, y1, centre->z + hw};
    sign_vec3_t q3 = {centre->x - hw, y1, centre->z + hw};

    n = append_quad(v, n, max, p3, p2, q2, q3, SIGNS_COL_POST);
    n = append_quad(v, n, max, p1, p0, q0, q1, SIGNS_COL_POST);
    n = append_quad(v, n, max, p2, p1, q1, q2, SIGNS_COL_POST);
    n = append_quad(v, n, max, p0, p3, q3, q0, SIGNS_COL_POST);
    return n;
}

/*
 * One allocation, two draw ranges: the post + panel face are depth-tested,
 * then the ink is drawn with the depth test AND depth writes off, so the
 * 0.02 m co-planar lift can never fight the panel face (the 0.2.1
 * ground/road flicker class). Unlike the render.c decal block - whose state
 * is restored by its caller render_frame() - this state is restored HERE, by
 * the callee draw_sign(), around the ink range only, and the block has no
 * early return.
 */
static void draw_sign(const sign_t *sign)
{
    sign_panel_t frame;
    sign_vec3_t corner[4];
    sign_quad_t symbols[6];
    SignVtx *v;
    int n = 0;
    int n_base;
    int symbol_count;
    int i;

    /* Reject before allocating, so a bad sign costs no display-list memory. */
    if (!signs_panel_corners(sign, corner))
        return;
    v = (SignVtx *)sceGuGetMemory((int)(SIGNS_MAX_VERTS * sizeof(SignVtx)));
    if (!v)
        return;
    signs_panel_frame(sign, &frame);

    n = append_post(v, n, SIGNS_MAX_VERTS, sign, &frame.centre);
    n = append_quad(v, n, SIGNS_MAX_VERTS, corner[0], corner[1], corner[2],
                    corner[3], SIGNS_COL_FACE);
    n_base = n;

    symbol_count = signs_symbol_quads(sign, symbols,
                                      (int)(sizeof(symbols) / sizeof(symbols[0])));
    if (signs_base_verts() + signs_ink_verts(symbol_count) > SIGNS_MAX_VERTS)
        symbol_count = 0; /* defensive: 30 + 36 <= 96 can never happen */
    for (i = 0; i < symbol_count; i++)
        n = append_quad(v, n, SIGNS_MAX_VERTS, symbols[i].a, symbols[i].b,
                        symbols[i].c, symbols[i].d, SIGNS_COL_INK);

    if (n_base > 0)
        sceGuDrawArray(GU_TRIANGLES, SIGNS_VTX_FMT, n_base, NULL, v);
    if (n > n_base) {
        sceGuDisable(GU_DEPTH_TEST);
        sceGuDepthMask(GU_TRUE);
        sceGuDrawArray(GU_TRIANGLES, SIGNS_VTX_FMT, n - n_base, NULL,
                       v + n_base);
        sceGuDepthMask(GU_FALSE);
        sceGuEnable(GU_DEPTH_TEST);
    }
}

void signs_draw(const car_state_t *car, const racing_settings_t *settings)
{
    sign_t list[SIGNS_MAX];
    float scale;
    int count;
    int i;

    if (!car || !settings)
        return;
    /* The persisted SIGN SIZE setting (RACING.CFG v2, 60..160 %). The panel,
     * symbol, lateral and occlusion maths are all scale-aware in signs.c;
     * signs_place() clamps this to SIGNS_SCALE_MIN..MAX. */
    scale = (float)settings->sign_scale_pct / 100.0f;
    count = signs_place(car->s, car->speed, car->suspension,
                        scale, list, SIGNS_MAX);
    /* FAR -> NEAR, deliberately. Each sign's ink is a depth-less decal, so a
     * nearer sign drawn first would be overwritten by a farther sign's ink
     * wherever the two overlap on screen. Painting the farthest first and the
     * nearest last puts the nearer panel and post on top, which is what the
     * depth-tested pre-decal code did. (The plan's "draw all ink after all
     * panels" fallback is wrong: it makes the overlap worse.) Pinned by
     * scripts/check-racing-decal-order.py. */
    for (i = count - 1; i >= 0; i--)
        draw_sign(&list[i]);
}
