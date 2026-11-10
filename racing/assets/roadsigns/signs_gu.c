/*
 * signs_gu.c - sceGu drawing for the Racing 3D roadside signs.
 *
 * Every vertex carries the literal sign colour: the probes match the panel
 * face exactly, so render.c's per-face shading factors must not be reused.
 * Per-frame geometry comes from sceGuGetMemory() (uncached display-list
 * memory); nothing here changes GU state.
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
    float y1 = sign->road_height + (SIGNS_PANEL_TOP - SIGNS_PANEL_H);
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

static void draw_sign(const sign_t *sign)
{
    sign_vec3_t centre, right, up, normal, corner[4];
    sign_quad_t symbols[6];
    SignVtx *v;
    int n = 0;
    int symbol_count;
    int i;

    /* Reject before allocating, so a bad sign costs no display-list memory. */
    if (!signs_panel_corners(sign, corner))
        return;
    v = (SignVtx *)sceGuGetMemory((int)(SIGNS_MAX_VERTS * sizeof(SignVtx)));
    if (!v)
        return;
    signs_panel_axes(sign, &centre, &right, &up, &normal);
    (void)right;
    (void)up;

    n = append_post(v, n, SIGNS_MAX_VERTS, sign, &centre);
    n = append_quad(v, n, SIGNS_MAX_VERTS, corner[0], corner[1], corner[2],
                    corner[3], SIGNS_COL_FACE);

    symbol_count = signs_symbol_quads(sign, symbols,
                                      (int)(sizeof(symbols) / sizeof(symbols[0])));
    for (i = 0; i < symbol_count; i++)
        n = append_quad(v, n, SIGNS_MAX_VERTS, symbols[i].a, symbols[i].b,
                        symbols[i].c, symbols[i].d, SIGNS_COL_INK);

    if (n > 0)
        sceGuDrawArray(GU_TRIANGLES, SIGNS_VTX_FMT, n, NULL, v);
}

void signs_draw(const car_state_t *car, const racing_settings_t *settings)
{
    sign_t list[SIGNS_MAX];
    int count;
    int i;

    (void)settings;
    if (!car)
        return;
    count = signs_place(car->s, car->speed, car->suspension, list, SIGNS_MAX);
    for (i = 0; i < count; i++)
        draw_sign(&list[i]);
}
