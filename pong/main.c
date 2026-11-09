/*
 * Pong - a deliberately small PSP game used as a real-hardware load test.
 *
 * Direct PSPSDK + sceGu, solid-colour rectangles, 3x5 bitmap font.
 * No textures, no assets, one display list, one draw call per frame.
 *
 * The GU init follows the pattern used by the hardware-verified ash engine:
 * aligned display list, all-user-RAM heap with a 4 MiB system reserve.
 *
 * Controls: D-pad up/down = move, X = restart after a match, START = exit.
 */

#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspkernel.h>
#include <stdbool.h>
#include <stdint.h>

#include "font.h"

PSP_MODULE_INFO("Pong", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);
PSP_HEAP_SIZE_KB(-1);
PSP_HEAP_THRESHOLD_SIZE_KB(4096);

#define SCREEN_W 480
#define SCREEN_H 272
#define BUF_W 512
#define FB_SIZE (BUF_W * SCREEN_H * 4)

#define PLAY_TOP 40
#define PLAY_BOTTOM (SCREEN_H - 8)
#define PADDLE_W 10
#define PADDLE_H 48
#define BALL 8
#define WIN_SCORE 5

#define MAX_VERTS 1024

#define COL_BG 0xFF101018u
#define COL_HEADER 0xFF202830u
#define COL_FIELD 0xFF101820u
#define COL_BORDER 0xFF506070u
#define COL_PLAYER 0xFF80FF80u
#define COL_CPU 0xFF4040FFu
#define COL_BALL 0xFFFFFFFFu
#define COL_TEXT 0xFFF0F0F0u
#define COL_OVERLAY 0xC0000000u
#define COL_WIN 0xFF80FF80u
#define COL_LOSE 0xFF4040FFu

typedef struct {
    uint32_t color;
    float x, y, z;
} Vtx;

static uint32_t display_list[65536] __attribute__((aligned(64)));
static Vtx verts[MAX_VERTS] __attribute__((aligned(16)));
static int vert_count;

static float player_y = (SCREEN_H - PADDLE_H) / 2.0f;
static float cpu_y = (SCREEN_H - PADDLE_H) / 2.0f;
static float ball_x, ball_y, ball_vx, ball_vy;
static int score_player, score_cpu;
static bool game_over, player_won;
static uint32_t rng_state = 1;

/* ------------------------------------------------------------------ util */

static uint32_t rng_next(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}

static float clampf(float v, float lo, float hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static void reset_ball(int direction)
{
    ball_x = (SCREEN_W - BALL) / 2.0f;
    ball_y = (SCREEN_H - BALL) / 2.0f;
    ball_vx = 3.2f * (float)direction;
    ball_vy = (rng_next() & 1u) ? 2.0f : -2.0f;
}

static void reset_match(void)
{
    score_player = 0;
    score_cpu = 0;
    game_over = false;
    player_won = false;
    player_y = (SCREEN_H - PADDLE_H) / 2.0f;
    cpu_y = (SCREEN_H - PADDLE_H) / 2.0f;
    reset_ball(1);
}

/* ------------------------------------------------------------------ draw */

static void push_rect(float x, float y, float w, float h, uint32_t color)
{
    if (vert_count + 2 > MAX_VERTS)
        return;
    verts[vert_count++] = (Vtx){color, x, y, 0.0f};
    verts[vert_count++] = (Vtx){color, x + w, y + h, 0.0f};
}

static void draw_text(const char *text, float x, float y, float scale,
                      uint32_t color)
{
    for (const char *p = text; *p; p++) {
        const uint8_t *glyph = font_glyph(*p);
        if (glyph) {
            for (int row = 0; row < 5; row++) {
                for (int col = 0; col < 3; col++) {
                    if (glyph[row] & (1u << (2 - col))) {
                        push_rect(x + (float)col * scale,
                                  y + (float)row * scale,
                                  scale, scale, color);
                    }
                }
            }
        }
        x += 4.0f * scale;
    }
}

static void format_int(char *buf, int value)
{
    char tmp[12];
    int n = 0;

    do {
        tmp[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value > 0);

    while (n > 0)
        *buf++ = tmp[--n];
    *buf = '\0';
}

static void draw_int(int value, float x, float y, float scale, uint32_t color)
{
    char buf[12];
    format_int(buf, value);
    draw_text(buf, x, y, scale, color);
}

static void render_init(void)
{
    void *draw_buf = (void *)0;
    void *disp_buf = (void *)(uintptr_t)FB_SIZE;

    sceGuInit();
    sceGuStart(GU_DIRECT, display_list);
    sceGuDrawBuffer(GU_PSM_8888, draw_buf, BUF_W);
    sceGuDispBuffer(SCREEN_W, SCREEN_H, disp_buf, BUF_W);
    sceGuOffset(2048 - SCREEN_W / 2, 2048 - SCREEN_H / 2);
    sceGuViewport(2048, 2048, SCREEN_W, SCREEN_H);
    sceGuScissor(0, 0, SCREEN_W, SCREEN_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

static void render(void)
{
    vert_count = 0;

    sceGuStart(GU_DIRECT, display_list);
    sceGuClearColor(COL_BG);
    sceGuClear(GU_COLOR_BUFFER_BIT);

    push_rect(0.0f, 0.0f, SCREEN_W, PLAY_TOP, COL_HEADER);
    push_rect(0.0f, PLAY_TOP, SCREEN_W, SCREEN_H - PLAY_TOP, COL_FIELD);

    /* Playfield border. */
    push_rect(0.0f, PLAY_TOP, SCREEN_W, 2.0f, COL_BORDER);
    push_rect(0.0f, SCREEN_H - 2.0f, SCREEN_W, 2.0f, COL_BORDER);
    push_rect(0.0f, PLAY_TOP, 2.0f, SCREEN_H - PLAY_TOP, COL_BORDER);
    push_rect(SCREEN_W - 2.0f, PLAY_TOP, 2.0f, SCREEN_H - PLAY_TOP, COL_BORDER);

    push_rect(16.0f, player_y, PADDLE_W, PADDLE_H, COL_PLAYER);
    push_rect(SCREEN_W - 16.0f - PADDLE_W, cpu_y, PADDLE_W, PADDLE_H, COL_CPU);
    push_rect(ball_x, ball_y, BALL, BALL, COL_BALL);

    draw_text("YOU", 8.0f, 10.0f, 2.0f, COL_TEXT);
    draw_int(score_player, 8.0f + font_text_width("YOU", 2) + 10.0f, 10.0f,
             2.0f, COL_PLAYER);
    draw_text("CPU", 300.0f, 10.0f, 2.0f, COL_TEXT);
    draw_int(score_cpu, 300.0f + font_text_width("CPU", 2) + 10.0f, 10.0f,
             2.0f, COL_CPU);

    if (game_over) {
        const char *title = player_won ? "YOU WIN" : "CPU WINS";
        const char *hint = "PRESS X";
        push_rect(0.0f, PLAY_TOP, SCREEN_W, SCREEN_H - PLAY_TOP, COL_OVERLAY);
        draw_text(title, (SCREEN_W - font_text_width(title, 3)) / 2.0f,
                  108.0f, 3.0f, player_won ? COL_WIN : COL_LOSE);
        draw_text(hint, (SCREEN_W - font_text_width(hint, 2)) / 2.0f,
                  146.0f, 2.0f, COL_TEXT);
    }

    if (vert_count > 0) {
        sceGuDrawArray(GU_SPRITES,
                       GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
                       vert_count, 0, verts);
    }

    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

/* ------------------------------------------------------------------ game */

static void update(uint32_t pressed)
{
    if (game_over) {
        if (pressed & PSP_CTRL_CROSS)
            reset_match();
        return;
    }

    if (pressed & PSP_CTRL_UP)
        player_y -= 5.0f;
    if (pressed & PSP_CTRL_DOWN)
        player_y += 5.0f;
    player_y = clampf(player_y, PLAY_TOP, PLAY_BOTTOM - PADDLE_H);

    float target = ball_y + BALL / 2.0f - PADDLE_H / 2.0f;
    if (cpu_y < target)
        cpu_y += 3.2f;
    else
        cpu_y -= 3.2f;
    cpu_y = clampf(cpu_y, PLAY_TOP, PLAY_BOTTOM - PADDLE_H);

    ball_x += ball_vx;
    ball_y += ball_vy;

    if (ball_y < PLAY_TOP) {
        ball_y = PLAY_TOP;
        ball_vy = -ball_vy;
    }
    if (ball_y + BALL > PLAY_BOTTOM) {
        ball_y = PLAY_BOTTOM - BALL;
        ball_vy = -ball_vy;
    }

    /* Player paddle (left). */
    if (ball_vx < 0.0f && ball_x <= 16.0f + PADDLE_W &&
        ball_x + BALL >= 16.0f && ball_y + BALL >= player_y &&
        ball_y <= player_y + PADDLE_H) {
        ball_x = 16.0f + PADDLE_W;
        ball_vx = -ball_vx;
        ball_vy += ((ball_y + BALL / 2.0f) - (player_y + PADDLE_H / 2.0f)) *
                   0.08f;
    }

    /* CPU paddle (right). */
    float cpu_x = SCREEN_W - 16.0f - PADDLE_W;
    if (ball_vx > 0.0f && ball_x + BALL >= cpu_x && ball_x <= cpu_x + PADDLE_W &&
        ball_y + BALL >= cpu_y && ball_y <= cpu_y + PADDLE_H) {
        ball_x = cpu_x - BALL;
        ball_vx = -ball_vx;
        ball_vy += ((ball_y + BALL / 2.0f) - (cpu_y + PADDLE_H / 2.0f)) * 0.08f;
    }

    ball_vy = clampf(ball_vy, -4.5f, 4.5f);

    if (ball_x + BALL < 0.0f) {
        score_cpu++;
        reset_ball(1);
    } else if (ball_x > SCREEN_W) {
        score_player++;
        reset_ball(-1);
    }

    if (score_player >= WIN_SCORE || score_cpu >= WIN_SCORE) {
        game_over = true;
        player_won = score_player > score_cpu;
    }
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

    rng_state = (uint32_t)sceKernelGetSystemTimeLow() | 1u;
    reset_match();
    render_init();

    for (;;) {
        SceCtrlData pad;
        sceCtrlPeekBufferPositive(&pad, 1);

        uint32_t pressed = pad.Buttons & ~previous_buttons;
        previous_buttons = pad.Buttons;

        if (pressed & PSP_CTRL_START)
            sceKernelExitGame();

        update(pressed);
        render();
    }

    return 0;
}
