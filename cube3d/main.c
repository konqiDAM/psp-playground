/*
 * Cube3D - a rotating cube with text on every face.
 *
 * Direct PSPSDK + sceGu + pspgum. No SDL, no external assets. Each face is a
 * 64x64 RGBA texture generated at runtime by writing pixels with the same 3x5
 * bitmap font used by the Snake game.
 *
 * Controls: START exits; HOME is handled by the system callback.
 */

#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspuser.h>

#include <stdint.h>

#include "font.h"

PSP_MODULE_INFO("Cube3D", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

/* All user RAM minus a 4 MiB system reserve (same policy as Snake/Pong).
 * A negative PSP_HEAP_SIZE_KB means "all remaining" in this SDK. */
PSP_HEAP_SIZE_KB(-1);
PSP_HEAP_THRESHOLD_SIZE_KB(4096);

#define SCREEN_W 480
#define SCREEN_H 272
#define BUF_W 512
#define FB_SIZE (BUF_W * SCREEN_H * 4)

#define TEX_SIZE 64
#define FACE_COUNT 6
#define HALF 0.8f

#define WHITE 0xFFFFFFFFu

static uint32_t display_list[65536] __attribute__((aligned(64)));
static uint32_t face_tex[FACE_COUNT][TEX_SIZE * TEX_SIZE]
    __attribute__((aligned(16)));

typedef struct {
    float u, v;
    uint32_t color;
    float x, y, z;
} Vtx;

#define VTX_FMT                                                               \
    (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D)

static Vtx face_verts[FACE_COUNT][4] __attribute__((aligned(16)));

/* ------------------------------------------------------------------ faces */

static void put_pixel(uint32_t *tex, int x, int y, uint32_t color)
{
    if (x < 0 || x >= TEX_SIZE || y < 0 || y >= TEX_SIZE)
        return;
    tex[y * TEX_SIZE + x] = color;
}

static void fill_face(uint32_t *tex, uint32_t color)
{
    for (int i = 0; i < TEX_SIZE * TEX_SIZE; i++)
        tex[i] = color;
}

static void border_face(uint32_t *tex, uint32_t color)
{
    for (int i = 0; i < TEX_SIZE; i++) {
        put_pixel(tex, i, 0, color);
        put_pixel(tex, i, TEX_SIZE - 1, color);
        put_pixel(tex, 0, i, color);
        put_pixel(tex, TEX_SIZE - 1, i, color);
    }
}

static void text_face(uint32_t *tex, const char *text, int scale,
                      uint32_t color)
{
    int width = font_text_width(text, scale);
    int height = 5 * scale;
    int x0 = (TEX_SIZE - width) / 2;
    int y0 = (TEX_SIZE - height) / 2;
    int x = x0;

    for (const char *p = text; *p; p++) {
        const uint8_t *glyph = font_glyph(*p);

        if (glyph) {
            for (int row = 0; row < 5; row++) {
                for (int col = 0; col < 3; col++) {
                    if (glyph[row] & (1u << (2 - col))) {
                        for (int dy = 0; dy < scale; dy++) {
                            for (int dx = 0; dx < scale; dx++) {
                                put_pixel(tex, x + col * scale + dx,
                                          y0 + row * scale + dy, color);
                            }
                        }
                    }
                }
            }
        }
        x += 4 * scale;
    }
}

static void build_faces(void)
{
    static const struct {
        const char *label;
        uint32_t bg;
        int scale;
    } defs[FACE_COUNT] = {
        {"PSP", 0xFF802020u, 4},  /* blue */
        {"3D", 0xFF202080u, 4},   /* red */
        {"GU", 0xFF208020u, 4},   /* green */
        {"CUBE", 0xFF802080u, 3}, /* purple */
        {"SCE", 0xFF208080u, 4},  /* teal */
        {"2026", 0xFF404040u, 3}, /* grey */
    };

    for (int i = 0; i < FACE_COUNT; i++) {
        fill_face(face_tex[i], defs[i].bg);
        border_face(face_tex[i], WHITE);
        text_face(face_tex[i], defs[i].label, defs[i].scale, WHITE);
    }

    /* The GE reads through the uncached mirror; flush the CPU writes. */
    sceKernelDcacheWritebackRange(face_tex, sizeof(face_tex));
}

/* ------------------------------------------------------------------ cube */

static void set_quad(int face, float ax, float ay, float az, float bx,
                     float by, float bz, float cx, float cy, float cz,
                     float dx, float dy, float dz)
{
    Vtx *v = face_verts[face];

    v[0] = (Vtx){0.0f, 0.0f, WHITE, ax, ay, az};
    v[1] = (Vtx){1.0f, 0.0f, WHITE, bx, by, bz};
    v[2] = (Vtx){0.0f, 1.0f, WHITE, cx, cy, cz};
    v[3] = (Vtx){1.0f, 1.0f, WHITE, dx, dy, dz};
}

static void build_cube(void)
{
    /* +Z: viewed from +Z, screen right is +X. */
    set_quad(0, -HALF, HALF, HALF, HALF, HALF, HALF, -HALF, -HALF, HALF,
             HALF, -HALF, HALF);
    /* -Z: viewed from -Z, screen right is -X. */
    set_quad(1, HALF, HALF, -HALF, -HALF, HALF, -HALF, HALF, -HALF, -HALF,
             -HALF, -HALF, -HALF);
    /* +X */
    set_quad(2, HALF, HALF, HALF, HALF, HALF, -HALF, HALF, -HALF, HALF,
             HALF, -HALF, -HALF);
    /* -X */
    set_quad(3, -HALF, HALF, -HALF, -HALF, HALF, HALF, -HALF, -HALF, -HALF,
             -HALF, -HALF, HALF);
    /* +Y */
    set_quad(4, -HALF, HALF, -HALF, HALF, HALF, -HALF, -HALF, HALF, HALF,
             HALF, HALF, HALF);
    /* -Y */
    set_quad(5, -HALF, -HALF, HALF, HALF, -HALF, HALF, -HALF, -HALF, -HALF,
             HALF, -HALF, -HALF);

    /* The GE reads through the uncached mirror; flush the CPU writes. */
    sceKernelDcacheWritebackRange(face_verts, sizeof(face_verts));
}

/* ------------------------------------------------------------------ GU */

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
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuShadeModel(GU_SMOOTH);

    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(55.0f, (float)SCREEN_W / (float)SCREEN_H, 0.5f, 100.0f);
    sceGumUpdateMatrix();

    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    ScePspFVector3 eye = {0.0f, 0.0f, -3.6f};
    sceGumTranslate(&eye);
    sceGumUpdateMatrix();

    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumUpdateMatrix();

    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

static void draw_cube(void)
{
    for (int i = 0; i < FACE_COUNT; i++) {
        sceGuTexImage(0, TEX_SIZE, TEX_SIZE, TEX_SIZE, face_tex[i]);
        sceGuDrawArray(GU_TRIANGLE_STRIP, VTX_FMT, 4, NULL, face_verts[i]);
    }
}

static void render_frame(float seconds)
{
    sceGuStart(GU_DIRECT, display_list);
    sceGuClearColor(0xFF101018u);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumRotateX(0.60f * seconds);
    sceGumRotateY(1.00f * seconds);
    sceGumRotateZ(0.15f * seconds);
    sceGumUpdateMatrix();

    draw_cube();

    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

static float seconds_now(void)
{
    uint64_t us = (uint64_t)sceKernelGetSystemTimeWide();
    return (float)(us % 100000000ull) / 1000000.0f;
}

/* ------------------------------------------------------------------ main */

static int exit_callback(int arg1, int arg2, void *common)
{
    (void)arg1;
    (void)arg2;
    (void)common;
    sceKernelExitGame();
    return 0;
}

static int callback_thread(SceSize args, void *argp)
{
    (void)args;
    (void)argp;

    int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

static void setup_callbacks(void)
{
    int thid = sceKernelCreateThread("update_thread", callback_thread, 0x11,
                                     0xFA0, 0, NULL);
    if (thid >= 0)
        sceKernelStartThread(thid, 0, NULL);
}

int main(void)
{
    uint32_t previous_buttons = 0;

    setup_callbacks();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    build_faces();
    build_cube();
    gfx_init();

    for (;;) {
        SceCtrlData pad;
        sceCtrlPeekBufferPositive(&pad, 1);

        uint32_t pressed = pad.Buttons & ~previous_buttons;
        previous_buttons = pad.Buttons;

        if (pressed & PSP_CTRL_START)
            sceKernelExitGame();

        render_frame(seconds_now());
    }

    return 0;
}
