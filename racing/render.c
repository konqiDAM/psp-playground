/*
 * render.c - direct sceGu renderer for Racing 3D.
 *
 * World: a runtime-generated curved two-lane road, a camera-forward grass fan,
 * shoulders, marker posts, a low-poly car and a road-relative chase camera.
 * The road surface is a single 64x64 RGBA texture repeated along arc length.
 *
 * Per-frame geometry is written into memory from sceGuGetMemory() (uncached
 * display-list memory, safe for the GE). The car mesh and the road texture are
 * static: they are written once and flushed with sceKernelDcacheWritebackRange.
 */

#include "render.h"

#include <math.h>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspuser.h>
#include <stdint.h>
#include <string.h>

#include "font.h"
#include "input.h"
#include "minimap.h"
#include "road.h"

#define SCREEN_W 480
#define SCREEN_H 272
#define BUF_W 512
#define FB_SIZE (BUF_W * SCREEN_H * 4)

#define MAX_HUD_VERTS 4096
#define MAX_MAP_VERTS 512

#define RGB(r, g, b)                                                           \
    (0xFF000000u | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) |             \
     (uint32_t)(r))

#define COL_SKY RGB(0x87, 0xCE, 0xEB)
#ifdef RACING_DIAG_GROUND
#define COL_GRASS RGB(0xFF, 0x00, 0xFF) /* diagnostic magenta */
#else
#define COL_GRASS RGB(0x2B, 0x5A, 0x2B)
#endif
#define COL_SHOULDER RGB(0x6A, 0x5A, 0x40)
#define COL_ASPHALT RGB(0x3C, 0x3C, 0x42)
#define COL_WHITE RGB(0xE8, 0xE8, 0xE8)
#define COL_POST_RED RGB(0xD0, 0x30, 0x20)
#define COL_BODY RGB(0xC0, 0x24, 0x20)
#define COL_GLASS RGB(0x20, 0x30, 0x60)
#define COL_TIRE RGB(0x1A, 0x1A, 0x1A)
#define COL_HUB RGB(0xA0, 0xA0, 0xA4)
#define COL_WING RGB(0x30, 0x30, 0x36)
#define COL_HEADLIGHT RGB(0xFF, 0xF0, 0xB0)
#define COL_TAILLIGHT RGB(0xFF, 0x30, 0x30)
#define COL_HUD_PANEL 0x90101418u
#define COL_HUD_STATUS_PANEL RGB(0x10, 0x14, 0x18)
#define COL_HUD_LABEL RGB(0xB0, 0xC0, 0xC8)
#define COL_HUD_TEXT RGB(0xF0, 0xF4, 0xF8)
#define COL_HUD_ACCENT RGB(0xFF, 0xE0, 0x40)
#define COL_HUD_WARN RGB(0xFF, 0x90, 0x20)
#define COL_HUD_SLIP RGB(0xFF, 0x50, 0x30)
#define COL_SHADOW 0x38000000u
#define COL_WELCOME_BG RGB(0x10, 0x18, 0x28)

#define V3D_FMT (GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D)
#define V3D_TEX_FMT                                                        \
    (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D)
#define V2D_FMT (GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D)

#define MAX_CAR_VERTS 2048

/*
 * Ground fan. The PSP GE does not clip laterally: a primitive with a vertex
 * outside the 0..4095 screen range is dropped whole (PPSSPP models this from
 * hardware probes). A world-axis-aligned ground grid therefore loses cells
 * whenever the chase camera yaws on a curve. The fan is built in camera space
 * instead: every vertex lies just outside the horizontal frustum (1.15x), so
 * it stays inside the guard band, and the union of the bands covers the whole
 * visible ground from the near plane to the fog end.
 */
#define GROUND_BANDS 20
#define GROUND_NEAR_DIST 1.0f
#define GROUND_FAR_DIST 700.0f
#define GROUND_HFOV_TAN 0.9186f /* tan(55/2 deg) * 480/272 */
#define GROUND_SPREAD 1.15f     /* outside the frustum edge, inside the guard */
#define GROUND_QUADS GROUND_BANDS

typedef struct {
    float u, v;
    uint32_t color;
    float x, y, z;
} TexVtx;

typedef struct {
    uint32_t color;
    float x, y, z;
} Vtx;

typedef struct {
    float x, y, z;
} P3;

static uint32_t display_list[65536] __attribute__((aligned(64)));
static uint32_t road_tex[ROAD_TEXTURE_X * ROAD_TEXTURE_Y]
    __attribute__((aligned(16)));
static Vtx car_mesh[MAX_CAR_VERTS] __attribute__((aligned(16)));
static int car_mesh_count;
static Vtx hud_verts[MAX_HUD_VERTS] __attribute__((aligned(16)));
static int hud_count;
static Vtx map_verts[MAX_MAP_VERTS] __attribute__((aligned(16)));
static float cam_lat;
static float view_x;
static float view_z;
static float view_fx;
static float view_fz;
static float view_s;

/* --------------------------------------------------------------- colours */

static uint32_t shade_color(uint32_t color, float factor)
{
    uint32_t r = color & 0xFFu;
    uint32_t g = (color >> 8) & 0xFFu;
    uint32_t b = (color >> 16) & 0xFFu;
    uint32_t a = (color >> 24) & 0xFFu;

    if (factor < 0.0f)
        factor = 0.0f;
    if (factor > 1.0f)
        factor = 1.0f;

    r = (uint32_t)((float)r * factor);
    g = (uint32_t)((float)g * factor);
    b = (uint32_t)((float)b * factor);
    return (a << 24) | (b << 16) | (g << 8) | r;
}

/* ------------------------------------------------------------ car meshes */

static int append_quad(Vtx *v, int n, int max, P3 a, P3 b, P3 c, P3 d,
                       uint32_t color, float factor)
{
    uint32_t shaded;

    if (n + 6 > max)
        return n;

    shaded = shade_color(color, factor);
    v[n++] = (Vtx){shaded, a.x, a.y, a.z};
    v[n++] = (Vtx){shaded, b.x, b.y, b.z};
    v[n++] = (Vtx){shaded, c.x, c.y, c.z};
    v[n++] = (Vtx){shaded, a.x, a.y, a.z};
    v[n++] = (Vtx){shaded, c.x, c.y, c.z};
    v[n++] = (Vtx){shaded, d.x, d.y, d.z};
    return n;
}

static int append_tri(Vtx *v, int n, int max, P3 a, P3 b, P3 c, uint32_t color)
{
    if (n + 3 > max)
        return n;

    v[n++] = (Vtx){color, a.x, a.y, a.z};
    v[n++] = (Vtx){color, b.x, b.y, b.z};
    v[n++] = (Vtx){color, c.x, c.y, c.z};
    return n;
}

static int append_box(Vtx *v, int n, int max, float cx, float cy, float cz,
                      float hx, float hy, float hz, uint32_t color)
{
    P3 p[8] = {
        {cx - hx, cy - hy, cz - hz}, {cx + hx, cy - hy, cz - hz},
        {cx + hx, cy - hy, cz + hz}, {cx - hx, cy - hy, cz + hz},
        {cx - hx, cy + hy, cz - hz}, {cx + hx, cy + hy, cz - hz},
        {cx + hx, cy + hy, cz + hz}, {cx - hx, cy + hy, cz + hz},
    };

    n = append_quad(v, n, max, p[3], p[2], p[6], p[7], color, 0.95f); /* +Z */
    n = append_quad(v, n, max, p[1], p[0], p[4], p[5], color, 0.72f); /* -Z */
    n = append_quad(v, n, max, p[2], p[1], p[5], p[6], color, 0.85f); /* +X */
    n = append_quad(v, n, max, p[0], p[3], p[7], p[4], color, 0.80f); /* -X */
    n = append_quad(v, n, max, p[7], p[6], p[5], p[4], color, 1.00f); /* +Y */
    n = append_quad(v, n, max, p[0], p[1], p[2], p[3], color, 0.45f); /* -Y */
    return n;
}

static int append_tapered_box(Vtx *v, int n, int max, float cz_bottom,
                              float hx_bottom, float hz_bottom, float cz_top,
                              float hx_top, float hz_top, float y_bottom,
                              float y_top, uint32_t color, uint32_t top_color)
{
    P3 b0 = {-hx_bottom, y_bottom, cz_bottom - hz_bottom};
    P3 b1 = {hx_bottom, y_bottom, cz_bottom - hz_bottom};
    P3 b2 = {hx_bottom, y_bottom, cz_bottom + hz_bottom};
    P3 b3 = {-hx_bottom, y_bottom, cz_bottom + hz_bottom};
    P3 t0 = {-hx_top, y_top, cz_top - hz_top};
    P3 t1 = {hx_top, y_top, cz_top - hz_top};
    P3 t2 = {hx_top, y_top, cz_top + hz_top};
    P3 t3 = {-hx_top, y_top, cz_top + hz_top};

    n = append_quad(v, n, max, b3, b2, t2, t3, color, 0.95f); /* front */
    n = append_quad(v, n, max, b1, b0, t0, t1, color, 0.72f); /* back */
    n = append_quad(v, n, max, b2, b1, t1, t2, color, 0.82f); /* right */
    n = append_quad(v, n, max, b0, b3, t3, t0, color, 0.78f); /* left */
    n = append_quad(v, n, max, t3, t2, t1, t0, top_color, 1.00f); /* roof */
    return n;
}

static int append_wheel(Vtx *v, int n, int max, float cx, float cy, float cz,
                        float radius, float half_width, uint32_t tire,
                        uint32_t hub)
{
    const int segments = 8;
    const float step = 6.28318530718f / (float)segments;
    uint32_t hub_shaded = shade_color(hub, 0.90f);
    int i;

    for (i = 0; i < segments; i++) {
        float a0 = step * (float)i;
        float a1 = step * (float)(i + 1);
        float y0 = cy + radius * cosf(a0);
        float z0 = cz + radius * sinf(a0);
        float y1 = cy + radius * cosf(a1);
        float z1 = cz + radius * sinf(a1);
        P3 p0 = {cx - half_width, y0, z0};
        P3 p1 = {cx + half_width, y0, z0};
        P3 p2 = {cx + half_width, y1, z1};
        P3 p3 = {cx - half_width, y1, z1};

        n = append_quad(v, n, max, p0, p1, p2, p3, tire,
                        0.72f + 0.06f * (float)(i & 1));
    }

    for (i = 0; i < segments; i++) {
        float a0 = step * (float)i;
        float a1 = step * (float)(i + 1);
        P3 outer0 = {cx + half_width, cy + radius * cosf(a0),
                     cz + radius * sinf(a0)};
        P3 outer1 = {cx + half_width, cy + radius * cosf(a1),
                     cz + radius * sinf(a1)};
        P3 outer_c = {cx + half_width, cy, cz};
        P3 inner0 = {cx - half_width, cy + radius * cosf(a0),
                     cz + radius * sinf(a0)};
        P3 inner1 = {cx - half_width, cy + radius * cosf(a1),
                     cz + radius * sinf(a1)};
        P3 inner_c = {cx - half_width, cy, cz};

        n = append_tri(v, n, max, outer_c, outer0, outer1, hub_shaded);
        n = append_tri(v, n, max, inner_c, inner0, inner1,
                       shade_color(hub, 0.35f));
    }

    return n;
}

static void build_car_mesh(void)
{
    int n = 0;
    float wheel_z[2] = {-1.32f, 1.32f};
    int side;
    int w;

    n = append_box(car_mesh, n, MAX_CAR_VERTS, 0.0f, 0.63f, 0.0f, 0.90f,
                   0.25f, 2.00f, COL_BODY);
    n = append_tapered_box(car_mesh, n, MAX_CAR_VERTS, -0.15f, 0.82f, 0.95f,
                           -0.25f, 0.60f, 0.62f, 0.88f, 1.38f, COL_GLASS,
                           COL_BODY);
    n = append_box(car_mesh, n, MAX_CAR_VERTS, 0.0f, 1.10f, -1.95f, 0.85f,
                   0.045f, 0.22f, COL_WING);
    n = append_box(car_mesh, n, MAX_CAR_VERTS, -0.60f, 0.97f, -1.88f, 0.04f,
                   0.09f, 0.06f, COL_WING);
    n = append_box(car_mesh, n, MAX_CAR_VERTS, 0.60f, 0.97f, -1.88f, 0.04f,
                   0.09f, 0.06f, COL_WING);

    n = append_box(car_mesh, n, MAX_CAR_VERTS, -0.55f, 0.72f, 2.005f, 0.17f,
                   0.075f, 0.03f, COL_HEADLIGHT);
    n = append_box(car_mesh, n, MAX_CAR_VERTS, 0.55f, 0.72f, 2.005f, 0.17f,
                   0.075f, 0.03f, COL_HEADLIGHT);
    n = append_box(car_mesh, n, MAX_CAR_VERTS, -0.55f, 0.72f, -2.005f, 0.18f,
                   0.075f, 0.03f, COL_TAILLIGHT);
    n = append_box(car_mesh, n, MAX_CAR_VERTS, 0.55f, 0.72f, -2.005f, 0.18f,
                   0.075f, 0.03f, COL_TAILLIGHT);

    for (side = -1; side <= 1; side += 2) {
        for (w = 0; w < 2; w++) {
            n = append_wheel(car_mesh, n, MAX_CAR_VERTS,
                             (float)side * 0.90f, 0.37f, wheel_z[w], 0.37f,
                             0.14f, COL_TIRE, COL_HUB);
        }
    }

    car_mesh_count = n;
    sceKernelDcacheWritebackRange(car_mesh, sizeof(car_mesh));
}

/* --------------------------------------------------------- road texture */

static void build_road_texture(void)
{
    int x;
    int y;

    for (y = 0; y < ROAD_TEXTURE_Y; y++) {
        for (x = 0; x < ROAD_TEXTURE_X; x++)
            road_tex[y * ROAD_TEXTURE_X + x] = COL_ASPHALT;
    }

    for (y = 0; y < ROAD_TEXTURE_Y; y++) {
        for (x = 1; x <= 3; x++) {
            road_tex[y * ROAD_TEXTURE_X + x] = COL_WHITE;
            road_tex[y * ROAD_TEXTURE_X + (ROAD_TEXTURE_X - 1 - x)] = COL_WHITE;
        }
    }

    sceKernelDcacheWritebackRange(road_tex, sizeof(road_tex));
}

/* -------------------------------------------------------------- geometry */

static Vtx *alloc_verts(int count)
{
    if (count <= 0)
        return NULL;
    return (Vtx *)sceGuGetMemory((int)((size_t)count * sizeof(Vtx)));
}

static void world_quad4(Vtx *v, int *n, P3 a, P3 b, P3 c, P3 d, uint32_t color)
{
    v[(*n)++] = (Vtx){color, a.x, a.y, a.z};
    v[(*n)++] = (Vtx){color, b.x, b.y, b.z};
    v[(*n)++] = (Vtx){color, c.x, c.y, c.z};
    v[(*n)++] = (Vtx){color, a.x, a.y, a.z};
    v[(*n)++] = (Vtx){color, c.x, c.y, c.z};
    v[(*n)++] = (Vtx){color, d.x, d.y, d.z};
}

/*
 * Camera-forward ground fan (see GROUND_BANDS). Bands grow geometrically in
 * distance so the near field gets the resolution where perspective compresses
 * least; the lateral extent is always 1.15x the frustum edge at that distance.
 */
/*
 * Background layer. This must be the first 3D draw of the frame (only the
 * colour/depth clear may precede it) and it must never write depth: the GE's
 * interpolated depth on these huge fan quads is not precise enough for the
 * 5 cm road offset, and a depth-tested ground punches grass through the
 * asphalt. Anything that needs to be occluded by the ground would break this.
 */
static void draw_ground(void)
{
    const float ratio =
        powf(GROUND_FAR_DIST / GROUND_NEAR_DIST, 1.0f / (float)GROUND_BANDS);
    float rx = -view_fz; /* screen right in the XZ plane */
    float rz = view_fx;
    float dist = GROUND_NEAR_DIST;
    Vtx *v = alloc_verts(GROUND_QUADS * 6);
    int n = 0;
    int i;

    if (!v)
        return;

    for (i = 0; i < GROUND_BANDS; i++) {
        float d0 = dist;
        float d1 = dist * ratio;
        float w0 = d0 * GROUND_HFOV_TAN * GROUND_SPREAD;
        float w1 = d1 * GROUND_HFOV_TAN * GROUND_SPREAD;
        float y0 = road_sample(view_s + d0).height - 0.05f;
        float y1 = road_sample(view_s + d1).height - 0.05f;
        P3 a = {view_x + view_fx * d0 - rx * w0, y0,
                view_z + view_fz * d0 - rz * w0};
        P3 b = {view_x + view_fx * d0 + rx * w0, y0,
                view_z + view_fz * d0 + rz * w0};
        P3 c = {view_x + view_fx * d1 + rx * w1, y1,
                view_z + view_fz * d1 + rz * w1};
        P3 d = {view_x + view_fx * d1 - rx * w1, y1,
                view_z + view_fz * d1 - rz * w1};

        world_quad4(v, &n, a, b, c, d, COL_GRASS);
        dist = d1;
    }

    sceGuDrawArray(GU_TRIANGLES, V3D_FMT, n, NULL, v);
}

static void draw_road(float s0, float s1)
{
    int bands = (int)((s1 - s0) / ROAD_MESH_STEP);
    int rows = bands + 1;
    TexVtx *v;
    int i;

    if (bands < 1)
        return;

    v = (TexVtx *)sceGuGetMemory((int)((size_t)rows * 2 * sizeof(TexVtx)));
    if (!v)
        return;

    for (i = 0; i < rows; i++) {
        float s = s0 + (float)i * ROAD_MESH_STEP;
        road_sample_t p = road_sample(s);
        float rx = -cosf(p.heading);
        float rz = -sinf(p.heading);
        float tv = s / ROAD_TEXTURE_PERIOD;

        v[i * 2 + 0] = (TexVtx){0.0f, tv, COL_WHITE,
                                p.x - p.half_width * rx, p.height,
                                p.z - p.half_width * rz};
        v[i * 2 + 1] = (TexVtx){1.0f, tv, COL_WHITE,
                                p.x + p.half_width * rx, p.height,
                                p.z + p.half_width * rz};
    }

    sceGuTexImage(0, ROAD_TEXTURE_X, ROAD_TEXTURE_Y, ROAD_TEXTURE_X, road_tex);
    sceGuDrawArray(GU_TRIANGLE_STRIP, V3D_TEX_FMT, rows * 2, NULL, v);
}

static void draw_lane_dashes(float s0, float s1)
{
    float dash_s0 = floorf(s0 / ROAD_DASH_PERIOD) * ROAD_DASH_PERIOD;
    int bands = (int)((s1 - dash_s0) / ROAD_MESH_STEP);
    int max_verts = bands * 3 * 6;
    Vtx *v;
    int n = 0;
    int i;

    if (bands < 1 || max_verts <= 0)
        return;
    v = alloc_verts(max_verts);
    if (!v)
        return;

    for (i = 0; i < bands; i++) {
        float s = dash_s0 + (float)i * ROAD_MESH_STEP;
        road_sample_t p0 = road_sample(s);
        road_sample_t p1 = road_sample(s + ROAD_MESH_STEP);
        int lanes = road_lane_count(p0.half_width);
        float r0x;
        float r0z;
        float r1x;
        float r1z;
        int k;

        if (lanes != road_lane_count(p1.half_width) || lanes < 2)
            continue;
        if (!road_dash_on(s))
            continue;

        r0x = -cosf(p0.heading);
        r0z = -sinf(p0.heading);
        r1x = -cosf(p1.heading);
        r1z = -sinf(p1.heading);
        for (k = 1; k < lanes; k++) {
            float lat0 = -p0.half_width +
                         2.0f * p0.half_width * (float)k / (float)lanes;
            float lat1 = -p1.half_width +
                         2.0f * p1.half_width * (float)k / (float)lanes;
            P3 a;
            P3 b;
            P3 c;
            P3 d;

            if (n + 6 > max_verts)
                break;
            a = (P3){p0.x + (lat0 - ROAD_DASH_HALF_WIDTH) * r0x,
                     p0.height + ROAD_DASH_LIFT,
                     p0.z + (lat0 - ROAD_DASH_HALF_WIDTH) * r0z};
            b = (P3){p0.x + (lat0 + ROAD_DASH_HALF_WIDTH) * r0x,
                     p0.height + ROAD_DASH_LIFT,
                     p0.z + (lat0 + ROAD_DASH_HALF_WIDTH) * r0z};
            c = (P3){p1.x + (lat1 + ROAD_DASH_HALF_WIDTH) * r1x,
                     p1.height + ROAD_DASH_LIFT,
                     p1.z + (lat1 + ROAD_DASH_HALF_WIDTH) * r1z};
            d = (P3){p1.x + (lat1 - ROAD_DASH_HALF_WIDTH) * r1x,
                     p1.height + ROAD_DASH_LIFT,
                     p1.z + (lat1 - ROAD_DASH_HALF_WIDTH) * r1z};
            world_quad4(v, &n, a, b, c, d, COL_WHITE);
        }
    }

    if (n > 0)
        sceGuDrawArray(GU_TRIANGLES, V3D_FMT, n, NULL, v);
}

static void draw_shoulders(float s0, float s1)
{
    int bands = (int)((s1 - s0) / ROAD_MESH_STEP);
    int rows = bands + 1;
    int side;

    if (bands < 1)
        return;

    for (side = -1; side <= 1; side += 2) {
        Vtx *v = alloc_verts(rows * 2);
        int i;

        if (!v)
            return;

        for (i = 0; i < rows; i++) {
            float s = s0 + (float)i * ROAD_MESH_STEP;
            road_sample_t p = road_sample(s);
            float rx = -cosf(p.heading);
            float rz = -sinf(p.heading);
            float inner = (float)side * p.half_width;
            float outer = (float)side * p.edge_half_width;

            v[i * 2 + 0] = (Vtx){COL_SHOULDER,
                                 p.x + inner * rx, p.height - 0.01f,
                                 p.z + inner * rz};
            v[i * 2 + 1] = (Vtx){COL_SHOULDER,
                                 p.x + outer * rx, p.height - 0.01f,
                                 p.z + outer * rz};
        }

        sceGuDrawArray(GU_TRIANGLE_STRIP, V3D_FMT, rows * 2, NULL, v);
    }
}

static void draw_posts(float car_s)
{
    float start = floorf((car_s - 80.0f) / ROAD_POST_SPACING) *
                  ROAD_POST_SPACING;
    float end = car_s + 400.0f;
    int count = (int)floorf((end - start) / ROAD_POST_SPACING) + 1;
    int total = count * 2 * 108;
    Vtx *v = alloc_verts(total);
    int n = 0;
    int i;
    int side;

    if (!v)
        return;

    for (i = 0; i < count; i++) {
        float s = start + (float)i * ROAD_POST_SPACING;
        road_sample_t p = road_sample(s);
        float rx = -cosf(p.heading);
        float rz = -sinf(p.heading);

        for (side = -1; side <= 1; side += 2) {
            float lateral = (float)side * (p.edge_half_width + 0.6f);
            float x = p.x + lateral * rx;
            float z = p.z + lateral * rz;
            uint32_t top = (p.half_width <= ROAD_NARROW_WARN_HALF)
                               ? COL_HUD_WARN
                               : COL_POST_RED;

            n = append_box(v, n, total, x, p.height + 0.45f, z, 0.08f,
                           0.45f, 0.08f, COL_WHITE);
            n = append_box(v, n, total, x, p.height + 1.05f, z, 0.08f,
                           0.15f, 0.08f, top);
            if (p.half_width <= ROAD_NARROW_WARN_HALF) {
                float mx = p.x +
                           (lateral - (float)side * 0.25f) * rx;
                float mz = p.z +
                           (lateral - (float)side * 0.25f) * rz;

                n = append_box(v, n, total, mx, p.height + 0.78f, mz,
                               0.13f, 0.13f, 0.13f, COL_HUD_WARN);
            }
        }
    }

    sceGuDrawArray(GU_TRIANGLES, V3D_FMT, n, NULL, v);
}

static void draw_shadow(float car_s, float car_x, float car_z, float yaw)
{
    const float s = sinf(yaw);
    const float c = cosf(yaw);
    const float lx[4] = {-1.00f, 1.00f, 1.00f, -1.00f};
    const float lz[4] = {-2.10f, -2.10f, 2.10f, 2.10f};
    Vtx *v = alloc_verts(6);
    int i;

    if (!v)
        return;

    for (i = 0; i < 4; i++) {
        float wx = car_x + lx[i] * c + lz[i] * s;
        float wz = car_z - lx[i] * s + lz[i] * c;

        v[i] = (Vtx){COL_SHADOW, wx,
                     road_sample(car_s + lz[i]).height + 0.02f, wz};
    }
    v[4] = v[0];
    v[5] = v[2];

    sceGuDepthMask(GU_TRUE);
    sceGuDrawArray(GU_TRIANGLES, V3D_FMT, 6, NULL, v);
    sceGuDepthMask(GU_FALSE);
}

static void draw_car(const car_state_t *car, float car_x, float car_y,
                     float car_z, float yaw)
{
    ScePspFVector3 position = {car_x, car_y, car_z};

    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumTranslate(&position);
    sceGumRotateY(yaw);
    sceGumRotateZ(-car->steer * 0.05f);
    sceGumRotateX(-car->pitch);
    sceGumUpdateMatrix();

    sceGuDrawArray(GU_TRIANGLES, V3D_FMT, car_mesh_count, NULL, car_mesh);
}

/* ------------------------------------------------------------------- HUD */

static void hud_rect(float x, float y, float w, float h, uint32_t color)
{
    if (hud_count + 2 > MAX_HUD_VERTS)
        return;
    hud_verts[hud_count++] = (Vtx){color, x, y, 0.0f};
    hud_verts[hud_count++] = (Vtx){color, x + w, y + h, 0.0f};
}

static void hud_text(const char *text, float x, float y, int scale,
                     uint32_t color)
{
    float step = (float)(4 * scale);

    for (const char *p = text; *p; p++) {
        const uint8_t *glyph = font_glyph(*p);

        if (glyph) {
            for (int row = 0; row < 5; row++) {
                for (int col = 0; col < 3; col++) {
                    if (glyph[row] & (1u << (2 - col))) {
                        hud_rect(x + (float)(col * scale),
                                 y + (float)(row * scale), (float)scale,
                                 (float)scale, color);
                    }
                }
            }
        }
        x += step;
    }
}

static void format_int(char *buf, int value)
{
    char tmp[12];
    int n = 0;

    if (value < 0) {
        *buf++ = '-';
        value = -value;
    }

    do {
        tmp[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value > 0);

    while (n > 0)
        *buf++ = tmp[--n];
    *buf = '\0';
}

static void format_fixed1(char *buf, float value)
{
    char tmp[12];
    char *p = tmp;
    int negative = value < 0.0f;
    float magnitude = fabsf(value);
    int whole = (int)magnitude;
    int tenth = (int)((magnitude - (float)whole) * 10.0f + 0.5f);

    if (tenth > 9) {
        tenth = 0;
        whole++;
    }
    format_int(tmp, whole);
    if (negative)
        *buf++ = '-';
    while (*p)
        *buf++ = *p++;
    *buf++ = '.';
    *buf++ = (char)('0' + tenth);
    *buf = '\0';
}

static void format_metric(char *buf, const char *label, int value,
                          const char *unit)
{
    char number[12];
    char *p = number;

    while (*label)
        *buf++ = *label++;
    *buf++ = ' ';
    format_int(number, value);
    while (*p)
        *buf++ = *p++;
    while (*unit)
        *buf++ = *unit++;
    *buf = '\0';
}

static void format_g(char *buf, const char *label, float value)
{
    while (*label)
        *buf++ = *label++;
    *buf++ = ' ';
    if (value >= 0.0f)
        *buf++ = '+';
    format_fixed1(buf, value);
}

static void draw_hud(const render_frame_args_t *args)
{
    const car_state_t *car = args->car;
    const racing_settings_t *settings = args->settings;
    const telemetry_state_t *telemetry = args->telemetry;
    const road_warning_t *warn = args->warnings;
    char speed_text[12];
    char text[32];
    char aux[24];
    int kmh = (int)(car_kmh(car) + 0.5f);
    int title_width = font_text_width("RACING 3D", 2);
    Vtx *v;

    format_int(speed_text, kmh);
    hud_count = 0;

    hud_rect(8.0f, 8.0f, 158.0f, 64.0f, COL_HUD_PANEL);
    hud_text("SPEED", 16.0f, 12.0f, 2, COL_HUD_LABEL);
    hud_text(speed_text, 16.0f, 25.0f, 4, COL_HUD_TEXT);
    hud_text("KM/H", 16.0f, 51.0f, 2, COL_HUD_LABEL);
    {
        size_t n;

        format_g(text, "GRADE", road_sample(car->s).grade * 100.0f);
        n = strlen(text);
        text[n] = '%';
        text[n + 1] = '\0';
        hud_text(text, 110.0f, 51.0f, 1, COL_HUD_ACCENT);
    }

    if (settings->steer_bar) {
        float steer = args->steer_bar;
        float end_x;

        if (steer < -1.0f)
            steer = -1.0f;
        if (steer > 1.0f)
            steer = 1.0f;
        hud_rect(16.0f, 76.0f, 142.0f, 6.0f, 0x80000000u);
        hud_rect(86.0f, 74.0f, 2.0f, 10.0f, COL_HUD_ACCENT);
        end_x = 87.0f + steer * 71.0f;
        if (end_x > 87.0f)
            hud_rect(87.0f, 76.0f, end_x - 87.0f, 6.0f, COL_HUD_ACCENT);
        else if (end_x < 87.0f)
            hud_rect(end_x, 76.0f, 87.0f - end_x, 6.0f, COL_HUD_ACCENT);
    }

    hud_text("RACING 3D", (float)(SCREEN_W - title_width - 12), 14.0f, 2,
             COL_HUD_TEXT);
    if (car->speed < -0.2f)
        hud_text("REVERSE", (float)(SCREEN_W - 7 * 8 - 12), 40.0f, 2,
                 COL_HUD_ACCENT);
    if (car_is_offroad(car))
        hud_text("OFF ROAD", 16.0f, 90.0f, 2, COL_HUD_WARN);
    if (car_is_slipping(car))
        hud_text("SLIP",
                 (float)(SCREEN_W - font_text_width("SLIP", 2) - 12),
                 62.0f, 2, COL_HUD_SLIP);

    if (args->boost_timer > 0.0f || car->airborne)
        hud_rect(10.0f, 100.0f, 150.0f, 16.0f, COL_HUD_STATUS_PANEL);
    if (args->boost_timer > 0.0f)
        hud_text("GRIP", 16.0f, 104.0f, 2, COL_HUD_ACCENT);
    if (car->airborne) {
        format_fixed1(aux, car->air_time);
        strcpy(text, "AIR ");
        strcat(text, aux);
        strcat(text, "S");
        hud_text(text, 60.0f, 104.0f, 2, COL_HUD_TEXT);
    }
    if (warn && warn->narrow) {
        int w = font_text_width("NARROW", 2);

        hud_text("NARROW", (float)(SCREEN_W - w) * 0.5f, 8.0f, 2,
                 COL_HUD_WARN);
    }
    if (warn && warn->crest) {
        int w = font_text_width("CREST", 2);

        hud_text("CREST", (float)(SCREEN_W - w) * 0.5f, 24.0f, 2,
                 COL_HUD_WARN);
    }

    format_metric(text, "DIST", (int)(telemetry->distance_m + 0.5f), "M");
    hud_text(text, (float)(468 - font_text_width(text, 1)), 86.0f, 1,
             COL_HUD_TEXT);
    format_metric(text, "MAX", (int)(telemetry->max_speed * 3.6f + 0.5f),
                  "KM/H");
    hud_text(text, (float)(468 - font_text_width(text, 1)), 96.0f, 1,
             COL_HUD_TEXT);

    if (settings->gmeter) {
        float lat = telemetry->lat_g;
        float lon = telemetry->lon_g;
        float dot_x;
        float dot_y;

        if (lat < -TELEMETRY_RANGE_G)
            lat = -TELEMETRY_RANGE_G;
        if (lat > TELEMETRY_RANGE_G)
            lat = TELEMETRY_RANGE_G;
        if (lon < -TELEMETRY_RANGE_G)
            lon = -TELEMETRY_RANGE_G;
        if (lon > TELEMETRY_RANGE_G)
            lon = TELEMETRY_RANGE_G;

        hud_rect(8.0f, 118.0f, 106.0f, 106.0f, COL_HUD_PANEL);
        hud_text("G", 14.0f, 122.0f, 1, COL_HUD_LABEL);
        hud_text("COMPUTED", 30.0f, 122.0f, 1, COL_HUD_LABEL);
        hud_rect(33.0f, 165.0f, 56.0f, 1.0f, COL_HUD_LABEL);
        hud_rect(60.0f, 138.0f, 1.0f, 56.0f, COL_HUD_LABEL);
        hud_rect(60.0f, 137.0f, 2.0f, 4.0f, COL_HUD_LABEL);
        hud_rect(60.0f, 192.0f, 2.0f, 4.0f, COL_HUD_LABEL);
        hud_rect(32.0f, 165.0f, 4.0f, 2.0f, COL_HUD_LABEL);
        hud_rect(85.0f, 165.0f, 4.0f, 2.0f, COL_HUD_LABEL);

        dot_x = 61.0f + lat / TELEMETRY_RANGE_G * 28.0f;
        dot_y = 166.0f - lon / TELEMETRY_RANGE_G * 28.0f;
        hud_rect(dot_x - 2.5f, dot_y - 2.5f, 5.0f, 5.0f, COL_HUD_ACCENT);

        format_g(text, "LAT", telemetry->lat_g);
        hud_text(text, 14.0f, 198.0f, 1, COL_HUD_TEXT);
        format_g(text, "LON", telemetry->lon_g);
        hud_text(text, 14.0f, 206.0f, 1, COL_HUD_TEXT);
        format_g(text, "PEAK", telemetry->peak_g);
        hud_text(text, 14.0f, 214.0f, 1, COL_HUD_TEXT);
    }

    if (settings->minimap) {
        float curve_distance;
        float curve_curvature;

        hud_rect(360.0f, 118.0f, 112.0f, 106.0f, COL_HUD_PANEL);
        hud_rect(360.0f, 224.0f, 112.0f, 8.0f, COL_HUD_PANEL);
        hud_text("MAP", 366.0f, 122.0f, 1, COL_HUD_LABEL);
        if (road_next_curve(car->s, MINIMAP_AHEAD, MINIMAP_CURVE_MIN,
                            &curve_distance, &curve_curvature)) {
            char number[12];
            char *p = number;

            format_int(number, (int)(curve_distance + 0.5f));
            text[0] = curve_curvature > 0.0f ? 'R' : 'L';
            text[1] = ' ';
            {
                int n = 2;

                while (*p)
                    text[n++] = *p++;
                text[n++] = 'M';
                text[n] = '\0';
            }
        } else {
            strcpy(text, "---");
        }
        hud_text(text, (float)(466 - font_text_width(text, 1)), 122.0f, 1,
                 COL_HUD_TEXT);
    }

    hud_rect(0.0f, 232.0f, (float)SCREEN_W, 40.0f, COL_HUD_PANEL);
    {
        const char *line1 = "X GAS   SQUARE BRAKE   D-PAD STEER";
        const char *line2 = "SELECT SETTINGS         START EXIT";

        hud_text(line1, (float)(SCREEN_W - font_text_width(line1, 2)) * 0.5f,
                 238.0f, 2, COL_HUD_TEXT);
        hud_text(line2, (float)(SCREEN_W - font_text_width(line2, 2)) * 0.5f,
                 254.0f, 2, COL_HUD_TEXT);
    }

    v = alloc_verts(hud_count);
    if (!v)
        return;
    memcpy(v, hud_verts, (size_t)hud_count * sizeof(Vtx));
    sceGuDrawArray(GU_SPRITES, V2D_FMT, hud_count, NULL, v);
}

static void map_set(Vtx *v, float x, float y, uint32_t color)
{
    v->color = color;
    v->x = x;
    v->y = y;
    v->z = 0.0f;
}

static void draw_minimap(const render_frame_args_t *args)
{
    minimap_strip_t map;
    Vtx *v;
    int i;
    int shoulder_count = MINIMAP_SAMPLES * 2;
    int asphalt_offset = shoulder_count;
    int marker_offset = shoulder_count * 2;
    int chevron_offset = marker_offset + 3;
    int profile_offset = chevron_offset + 3;
    int profile_count = (MINIMAP_SAMPLES - 1) * 6;
    int total;

    if (!args->settings->minimap)
        return;

    minimap_build(args->car, 366.0f, 132.0f, 466.0f, 218.0f, &map);
    if (map.count != MINIMAP_SAMPLES)
        return;

    for (i = 0; i < MINIMAP_SAMPLES; i++) {
        float width = map.right_x[i] - map.left_x[i];
        float frac = map.asphalt_frac[i];
        float inset = width * (1.0f - frac) * 0.5f;
        float inset_y = (map.right_y[i] - map.left_y[i]) *
                        (1.0f - frac) * 0.5f;

        map_set(&map_verts[i * 2 + 0], map.left_x[i], map.left_y[i],
                COL_SHOULDER);
        map_set(&map_verts[i * 2 + 1], map.right_x[i], map.right_y[i],
                COL_SHOULDER);
        map_set(&map_verts[asphalt_offset + i * 2 + 0],
                map.left_x[i] + inset, map.left_y[i] + inset_y, COL_ASPHALT);
        map_set(&map_verts[asphalt_offset + i * 2 + 1],
                map.right_x[i] - inset, map.right_y[i] - inset_y,
                COL_ASPHALT);
    }

    map_set(&map_verts[marker_offset + 0], map.car_x, map.car_y - 6.0f,
            car_is_offroad(args->car) ? COL_HUD_WARN : COL_HUD_TEXT);
    map_set(&map_verts[marker_offset + 1], map.car_x - 4.0f,
            map.car_y + 2.0f,
            car_is_offroad(args->car) ? COL_HUD_WARN : COL_HUD_TEXT);
    map_set(&map_verts[marker_offset + 2], map.car_x + 4.0f,
            map.car_y + 2.0f,
            car_is_offroad(args->car) ? COL_HUD_WARN : COL_HUD_TEXT);

    if (map.has_curve) {
        float x = map.car_x + (map.curve_curvature > 0.0f ? 18.0f : -18.0f);

        if (map.curve_curvature > 0.0f) {
            map_set(&map_verts[chevron_offset + 0], x - 5.0f, 134.0f,
                    COL_HUD_ACCENT);
            map_set(&map_verts[chevron_offset + 1], x - 5.0f, 142.0f,
                    COL_HUD_ACCENT);
            map_set(&map_verts[chevron_offset + 2], x + 4.0f, 138.0f,
                    COL_HUD_ACCENT);
        } else {
            map_set(&map_verts[chevron_offset + 0], x + 5.0f, 134.0f,
                    COL_HUD_ACCENT);
            map_set(&map_verts[chevron_offset + 1], x + 5.0f, 142.0f,
                    COL_HUD_ACCENT);
            map_set(&map_verts[chevron_offset + 2], x - 4.0f, 138.0f,
                    COL_HUD_ACCENT);
        }
    }

    for (i = 0; i < MINIMAP_SAMPLES - 1; i++) {
        float x0 = 366.0f + (float)i * 100.0f /
                               (float)(MINIMAP_SAMPLES - 1);
        float x1 = 366.0f + (float)(i + 1) * 100.0f /
                               (float)(MINIMAP_SAMPLES - 1);
        float y0 = map.profile_y[i];
        float y1 = map.profile_y[i + 1];
        int o = profile_offset + i * 6;

        map_set(&map_verts[o + 0], x0, y0, COL_HUD_ACCENT);
        map_set(&map_verts[o + 1], x1, y1, COL_HUD_ACCENT);
        map_set(&map_verts[o + 2], x1, y1 + 2.0f, COL_HUD_ACCENT);
        map_set(&map_verts[o + 3], x0, y0, COL_HUD_ACCENT);
        map_set(&map_verts[o + 4], x1, y1 + 2.0f, COL_HUD_ACCENT);
        map_set(&map_verts[o + 5], x0, y0 + 2.0f, COL_HUD_ACCENT);
    }

    total = profile_offset + profile_count;
    v = alloc_verts(total);
    if (!v)
        return;
    memcpy(v, map_verts, (size_t)total * sizeof(Vtx));
    sceGuDrawArray(GU_TRIANGLE_STRIP, V2D_FMT, shoulder_count, NULL, v);
    sceGuDrawArray(GU_TRIANGLE_STRIP, V2D_FMT, shoulder_count, NULL,
                   v + asphalt_offset);
    sceGuDrawArray(GU_TRIANGLES, V2D_FMT, 3, NULL, v + marker_offset);
    if (map.has_curve)
        sceGuDrawArray(GU_TRIANGLES, V2D_FMT, 3, NULL, v + chevron_offset);
    sceGuDrawArray(GU_TRIANGLES, V2D_FMT, profile_count, NULL,
                   v + profile_offset);
}

/* ------------------------------------------------------------------ menu */

static const char *const menu_labels[MENU_ITEM_COUNT] = {
    "STEER MODE", "SENSITIVITY", "MINIMAP", "G-METER",
    "STEER BAR", "CALIBRATE", "RESET PEAK G", "RESTART", "RESUME",
};

static void menu_value_string(const render_frame_args_t *args, int item,
                              char *buf)
{
    const racing_settings_t *s = args->settings;

    buf[0] = '\0';
    switch (item) {
    case MENU_ITEM_STEER_MODE:
        strcpy(buf, s->steer_mode == RACING_STEER_ANALOG ? "ANALOG" : "DPAD");
        break;
    case MENU_ITEM_SENSITIVITY:
        format_fixed1(buf, s->sensitivity);
        break;
    case MENU_ITEM_MINIMAP:
        strcpy(buf, s->minimap ? "ON" : "OFF");
        break;
    case MENU_ITEM_GMETER:
        strcpy(buf, s->gmeter ? "ON" : "OFF");
        break;
    case MENU_ITEM_STEER_BAR:
        strcpy(buf, s->steer_bar ? "ON" : "OFF");
        break;
    case MENU_ITEM_CALIBRATE:
    case MENU_ITEM_RESET_PEAK:
    case MENU_ITEM_RESTART:
        strcpy(buf, ">");
        break;
    default:
        break;
    }
}

static void draw_menu(const render_frame_args_t *args)
{
    const menu_state_t *menu = args->menu;
    const racing_settings_t *settings = args->settings;
    char text[32];
    char number[12];
    int i;
    Vtx *v;

    if (!menu->open)
        return;

    hud_count = 0;
    hud_rect(0.0f, 0.0f, SCREEN_W, SCREEN_H, 0x90000000u);
    hud_rect(90.0f, 30.0f, 300.0f, 196.0f, COL_HUD_PANEL);

    if (menu->screen == MENU_SCREEN_CALIBRATE) {
        float analog = input_analog_to_steer(args->raw_lx,
                                             settings->analog_centre,
                                             settings->analog_deadzone);

        hud_text("CALIBRATION", 197.0f, 36.0f, 2, COL_HUD_TEXT);
        hud_text("MOVE NUB TO CENTRE", 104.0f, 58.0f, 2, COL_HUD_LABEL);

        hud_text("RAW X", 104.0f, 78.0f, 2, COL_HUD_LABEL);
        format_int(number, args->raw_lx);
        hud_text(number, 160.0f, 78.0f, 2, COL_HUD_TEXT);
        hud_text("Y", 200.0f, 78.0f, 2, COL_HUD_LABEL);
        format_int(number, args->raw_ly);
        hud_text(number, 216.0f, 78.0f, 2, COL_HUD_TEXT);

        hud_text("CENTRE", 104.0f, 98.0f, 2, COL_HUD_LABEL);
        format_int(number, settings->analog_centre);
        hud_text(number, 168.0f, 98.0f, 2, COL_HUD_TEXT);

        hud_text("DEADZONE", 104.0f, 118.0f, 2, COL_HUD_LABEL);
        format_int(number, settings->analog_deadzone);
        hud_text(number, 200.0f, 118.0f, 2, COL_HUD_TEXT);

        hud_text("STEER", 104.0f, 138.0f, 2, COL_HUD_LABEL);
        format_fixed1(number, analog);
        hud_text(number, 168.0f, 138.0f, 2, COL_HUD_TEXT);

        hud_rect(120.0f, 156.0f, 240.0f, 10.0f, 0x80000000u);
        {
            float centre_x =
                120.0f + (float)settings->analog_centre * 240.0f / 255.0f;
            float raw_x = 120.0f + (float)args->raw_lx * 240.0f / 255.0f;

            hud_rect(centre_x - 1.0f, 154.0f, 2.0f, 14.0f, COL_HUD_ACCENT);
            hud_rect(raw_x - 1.5f, 154.0f, 3.0f, 14.0f, COL_HUD_TEXT);
        }

        hud_text("X CAPTURE  LEFT/RIGHT DEADZONE  SQUARE BACK", 100.0f, 210.0f,
                 1, COL_HUD_TEXT);
    } else {
        hud_text("SETTINGS", 209.0f, 36.0f, 2, COL_HUD_TEXT);
        for (i = 0; i < MENU_ITEM_COUNT; i++) {
            float y = 56.0f + (float)i * 18.0f;
            int value_width;

            if (i == menu->cursor)
                hud_rect(96.0f, y - 4.0f, 288.0f, 18.0f, 0x30FFFFFFu);
            hud_text(">", 98.0f, y, 2, COL_HUD_ACCENT);
            hud_text(menu_labels[i], 108.0f, y, 2, COL_HUD_TEXT);
            menu_value_string(args, i, text);
            value_width = font_text_width(text, 2);
            hud_text(text, (float)(366 - value_width), y, 2, COL_HUD_ACCENT);
        }
        hud_text("UP/DOWN SELECT  LEFT/RIGHT CHANGE  X OK  SQUARE BACK  SELECT RESUME",
                 100.0f, 214.0f, 1, COL_HUD_TEXT);
    }

    if (args->settings_save_failed)
        hud_text("NOT SAVED", 104.0f, 218.0f, 1, COL_HUD_WARN);

    v = alloc_verts(hud_count);
    if (!v)
        return;
    memcpy(v, hud_verts, (size_t)hud_count * sizeof(Vtx));
    sceGuDrawArray(GU_SPRITES, V2D_FMT, hud_count, NULL, v);
}

/* --------------------------------------------------------------- camera */

static float clamp_unit(float value)
{
    if (value < 0.0f)
        return 0.0f;
    if (value > 1.0f)
        return 1.0f;
    return value;
}

static void setup_3d(const car_state_t *car, float dt)
{
    float speed = fabsf(car->speed);
    float follow = clamp_unit(dt * 6.0f);
    float eye_s = car->s - (8.2f + 0.035f * speed);
    road_sample_t eye_p = road_sample(eye_s);
    road_sample_t focus_p = road_sample(car->s + 8.0f);
    float eye_rx = -cosf(eye_p.heading);
    float eye_rz = -sinf(eye_p.heading);
    float focus_rx = -cosf(focus_p.heading);
    float focus_rz = -sinf(focus_p.heading);
    ScePspFVector3 eye;
    ScePspFVector3 focus;
    ScePspFVector3 up = {0.0f, 1.0f, 0.0f};

    cam_lat += (0.55f * car->lateral - cam_lat) * follow;
    view_s = eye_s;

    eye.x = eye_p.x + cam_lat * eye_rx;
    eye.y = eye_p.height + 3.1f + 0.006f * speed +
            car->suspension * 0.6f;
    eye.z = eye_p.z + cam_lat * eye_rz;

    focus.x = focus_p.x + 0.90f * car->lateral * focus_rx;
    focus.y = focus_p.height + 1.1f + car->air_height * 0.6f +
              car->suspension * 0.6f;
    focus.z = focus_p.z + 0.90f * car->lateral * focus_rz;

    {
        float dx = focus.x - eye.x;
        float dz = focus.z - eye.z;
        float len = sqrtf(dx * dx + dz * dz);

        if (len < 1e-3f) {
            dx = 0.0f;
            dz = 1.0f;
            len = 1.0f;
        }
        view_fx = dx / len;
        view_fz = dz / len;
    }

    view_x = eye.x;
    view_z = eye.z;

    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(55.0f, (float)SCREEN_W / (float)SCREEN_H, 0.5f, 1200.0f);
    sceGumUpdateMatrix();

    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    sceGumLookAt(&eye, &focus, &up);
    sceGumUpdateMatrix();

    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumUpdateMatrix();
}

/* ------------------------------------------------------------------ init */

static const char *const welcome_labels[WELCOME_ITEM_COUNT] = {
    "MAP", "START GAME", "ELEVATION", "HILL FREQ", "WIDTH FREQ", "ONE LANE",
};

static void welcome_value_string(int item, const road_config_t *terrain,
                                 char *buf)
{
    float value = 0.0f;

    buf[0] = '\0';
    switch (item) {
    case WELCOME_ITEM_MAP:
        if (terrain->map == ROAD_MAP_UPHILL)
            strcpy(buf, "UPHILL");
        else if (terrain->map == ROAD_MAP_HIGHWAY)
            strcpy(buf, "HIGHWAY");
        else
            strcpy(buf, "DEFAULT");
        return;
    case WELCOME_ITEM_ELEVATION:
        value = terrain->elevation_pct;
        break;
    case WELCOME_ITEM_HILLS:
        value = terrain->hill_pct;
        break;
    case WELCOME_ITEM_WIDTH:
        if (terrain->map == ROAD_MAP_HIGHWAY) {
            strcpy(buf, "--");
            return;
        }
        value = terrain->width_pct;
        break;
    case WELCOME_ITEM_ONE_LANE:
        if (terrain->map == ROAD_MAP_HIGHWAY) {
            strcpy(buf, "--");
            return;
        }
        format_fixed1(buf, terrain->one_lane_width);
        strcat(buf, "M");
        return;
    default:
        return;
    }
    format_int(buf, (int)(value + 0.5f));
    strcat(buf, "%");
}

static float welcome_fraction(int item, const road_config_t *terrain)
{
    switch (item) {
    case WELCOME_ITEM_ELEVATION:
        return (terrain->elevation_pct - ROAD_TERRAIN_MIN_PCT) /
               (ROAD_TERRAIN_MAX_PCT - ROAD_TERRAIN_MIN_PCT);
    case WELCOME_ITEM_HILLS:
        return (terrain->hill_pct - ROAD_TERRAIN_MIN_PCT) /
               (ROAD_TERRAIN_MAX_PCT - ROAD_TERRAIN_MIN_PCT);
    case WELCOME_ITEM_WIDTH:
        if (terrain->map == ROAD_MAP_HIGHWAY)
            return 0.0f;
        return (terrain->width_pct - ROAD_TERRAIN_MIN_PCT) /
               (ROAD_TERRAIN_MAX_PCT - ROAD_TERRAIN_MIN_PCT);
    case WELCOME_ITEM_ONE_LANE:
        if (terrain->map == ROAD_MAP_HIGHWAY)
            return 0.0f;
        return (terrain->one_lane_width - ROAD_ONE_LANE_TOTAL_MIN) /
               (ROAD_ONE_LANE_TOTAL_MAX - ROAD_ONE_LANE_TOTAL_MIN);
    default:
        return 0.0f;
    }
}

void render_welcome(const welcome_state_t *welcome,
                    const road_config_t *terrain)
{
    char text[24];
    int title_width = font_text_width("RACING 3D", 4);
    int i;
    Vtx *v;

    sceGuStart(GU_DIRECT, display_list);
    sceGuClearColor(COL_WELCOME_BG);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_FOG);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDepthMask(GU_FALSE);

    hud_count = 0;
    hud_text("RACING 3D", (float)(SCREEN_W - title_width) * 0.5f, 20.0f, 4,
             COL_HUD_ACCENT);
    hud_text("MAPS", (float)(SCREEN_W - font_text_width("MAPS", 2)) * 0.5f,
             48.0f, 2, COL_HUD_LABEL);

    for (i = 0; i < WELCOME_ITEM_COUNT; i++) {
        float y = 76.0f + (float)i * 24.0f;
        float fraction = welcome_fraction(i, terrain);
        int disabled = (terrain->map == ROAD_MAP_HIGHWAY &&
                        (i == WELCOME_ITEM_WIDTH ||
                         i == WELCOME_ITEM_ONE_LANE));

        if (i == welcome->cursor)
            hud_rect(32.0f, y - 4.0f, 416.0f, 20.0f, 0x30FFFFFFu);
        hud_text(">", 40.0f, y, 2, COL_HUD_ACCENT);
        hud_text(welcome_labels[i], 56.0f, y, 2,
                 disabled ? COL_HUD_LABEL : COL_HUD_TEXT);
        welcome_value_string(i, terrain, text);
        if (text[0]) {
            int width = font_text_width(text, 2);

            hud_text(text, (float)(182 - width), y, 2,
                     disabled ? COL_HUD_LABEL : COL_HUD_ACCENT);
        }
        if (i != WELCOME_ITEM_START && i != WELCOME_ITEM_MAP &&
            !disabled) {
            hud_rect(196.0f, y + 3.0f, 250.0f, 8.0f, 0x80000000u);
            hud_rect(196.0f, y + 3.0f, 250.0f * fraction, 8.0f,
                     COL_HUD_ACCENT);
        }
    }

    hud_text("UP/DOWN SELECT  LEFT/RIGHT ADJUST  X START",
             (float)(SCREEN_W -
                     font_text_width("UP/DOWN SELECT  LEFT/RIGHT ADJUST  X START",
                                     1)) *
                 0.5f,
             244.0f, 1, COL_HUD_TEXT);

    v = alloc_verts(hud_count);
    if (v) {
        memcpy(v, hud_verts, (size_t)hud_count * sizeof(Vtx));
        sceGuDrawArray(GU_SPRITES, V2D_FMT, hud_count, NULL, v);
    }

    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

static void gfx_init(void)
{
    void *draw_buf = (void *)0;
    void *disp_buf = (void *)(uintptr_t)FB_SIZE;
    void *depth_buf = (void *)(uintptr_t)(2 * FB_SIZE);

    sceGuInit();
    sceGuStart(GU_DIRECT, display_list);
    sceGuDrawBuffer(GU_PSM_8888, draw_buf, BUF_W);
    sceGuDispBuffer(SCREEN_W, SCREEN_H, disp_buf, BUF_W);
    sceGuDepthBuffer(depth_buf, BUF_W);
    sceGuOffset(2048 - SCREEN_W / 2, 2048 - SCREEN_H / 2);
    sceGuViewport(2048, 2048, SCREEN_W, SCREEN_H);
    sceGuScissor(0, 0, SCREEN_W, SCREEN_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuDepthMask(GU_FALSE);
    sceGuDepthRange(65535, 0);
    sceGuClearDepth(0);
    sceGuDisable(GU_CULL_FACE);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_FOG);
    sceGuFog(70.0f, 420.0f, COL_SKY);

    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_REPEAT, GU_REPEAT);
    sceGuDisable(GU_TEXTURE_2D);

    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(55.0f, (float)SCREEN_W / (float)SCREEN_H, 0.5f, 1200.0f);
    sceGumUpdateMatrix();

    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    sceGumUpdateMatrix();

    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumUpdateMatrix();

    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

void render_init(void)
{
    build_road_texture();
    build_car_mesh();
    gfx_init();
    cam_lat = 0.0f;
    view_x = 0.0f;
    view_z = 0.0f;
}

/* ----------------------------------------------------------------- frame */

void render_frame(const render_frame_args_t *args, float dt)
{
    const car_state_t *car = args->car;
    float s0 = car->s - ROAD_NEAR_BEHIND;
    float s1 = car->s + 700.0f;
    road_sample_t p = road_sample(car->s);
    float rx = -cosf(p.heading);
    float rz = -sinf(p.heading);
    float car_x = p.x + car->lateral * rx;
    float car_z = p.z + car->lateral * rz;
    float car_y = p.height + car->air_height + car->suspension;
    float yaw = -(p.heading + car->slip + 0.08f * car->steer);

    sceGuStart(GU_DIRECT, display_list);
    sceGuClearColor(COL_SKY);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

    sceGuEnable(GU_DEPTH_TEST);
    sceGuEnable(GU_FOG);
    sceGuDepthMask(GU_FALSE);

    setup_3d(car, dt);

    sceGuDisable(GU_TEXTURE_2D);
    /* Background: no depth test/write, so the ground can never occlude the
     * road, shoulders, posts or car (see the draw_ground comment). */
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_TRUE);
    draw_ground(); /* may return early; the restore below always runs */
    sceGuDepthMask(GU_FALSE);
    sceGuEnable(GU_DEPTH_TEST);
    draw_shoulders(s0, s1);

    sceGuEnable(GU_TEXTURE_2D);
    draw_road(s0, s1);
    sceGuDisable(GU_TEXTURE_2D);
    draw_lane_dashes(s0, s1);

    draw_posts(car->s);
    draw_shadow(car->s, car_x, car_z, yaw);
    draw_car(car, car_x, car_y, car_z, yaw);

    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_FOG);
    sceGuDisable(GU_TEXTURE_2D);
    draw_hud(args);
    draw_minimap(args);
    if (args->menu->open)
        draw_menu(args);

    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}
