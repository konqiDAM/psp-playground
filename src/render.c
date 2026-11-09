/*
 * render.c - direct sceGu renderer.
 *
 * Everything is a solid-colour rectangle: the playfield, the snake, the food,
 * the border and the 3x5 font pixels. No textures, no SDL.
 *
 * The whole frame is accumulated into one vertex array and drawn with a
 * single GU_SPRITES draw call.
 */

#include "render.h"

#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspuser.h>
#include <string.h>

#include "font.h"

#define SCREEN_W 480
#define SCREEN_H 272
#define BUF_W 512

#define HEADER_H 32
#define CELL_PX 16

#define MAX_VERTS 2048

#define COL_BG 0xFF101018u
#define COL_HEADER 0xFF202830u
#define COL_FIELD 0xFF101820u
#define COL_BORDER 0xFF506070u
#define COL_SNAKE 0xFF30C060u
#define COL_HEAD 0xFF90FF90u
#define COL_FOOD 0xFF2020FFu
#define COL_TEXT 0xFFF0F0F0u
#define COL_SCORE 0xFF80FF80u
#define COL_OVERLAY 0xC0000000u
#define COL_GAMEOVER 0xFF4040FFu

typedef struct {
    uint32_t color;
    float x, y, z;
} Vertex;

static uint32_t display_list[65536] __attribute__((aligned(64))); /* 256 KiB */
static Vertex verts[MAX_VERTS] __attribute__((aligned(16)));
static int vert_count;

static void push_rect(float x, float y, float w, float h, uint32_t color)
{
    if (vert_count + 2 > MAX_VERTS)
        return;

    verts[vert_count++] = (Vertex){color, x, y, 0.0f};
    verts[vert_count++] = (Vertex){color, x + w, y + h, 0.0f};
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

void render_init(void)
{
    void *draw_buf = (void *)0;
    void *disp_buf = (void *)(BUF_W * SCREEN_H * 4);

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

static void draw_cell(const snake_game_t *g, int cell, uint32_t color)
{
    int x = snake_cell_x(g, cell) * CELL_PX;
    int y = HEADER_H + snake_cell_y(g, cell) * CELL_PX;

    /* One-pixel gap gives a subtle grid without drawing 450 lines. */
    push_rect((float)x + 1.0f, (float)y + 1.0f,
              (float)CELL_PX - 2.0f, (float)CELL_PX - 2.0f, color);
}

void render_draw(const snake_game_t *g)
{
    char score[12];

    vert_count = 0;

    sceGuStart(GU_DIRECT, display_list);
    sceGuClearColor(COL_BG);
    sceGuClear(GU_COLOR_BUFFER_BIT);

    /* Header. */
    push_rect(0.0f, 0.0f, SCREEN_W, HEADER_H, COL_HEADER);
    draw_text("SCORE", 8.0f, 11.0f, 2.0f, COL_TEXT);
    format_int(score, g->score);
    draw_text(score, 8.0f + font_text_width("SCORE", 2) + 12.0f, 11.0f,
              2.0f, COL_SCORE);

    /* Playfield. */
    push_rect(0.0f, HEADER_H, SCREEN_W, SCREEN_H - HEADER_H, COL_FIELD);

    if (g->food >= 0)
        draw_cell(g, g->food, COL_FOOD);

    for (int i = g->length - 1; i >= 0; i--)
        draw_cell(g, g->snake[i], i == 0 ? COL_HEAD : COL_SNAKE);

    /* Border on top of the edge cells. */
    push_rect(0.0f, HEADER_H, SCREEN_W, 2.0f, COL_BORDER);
    push_rect(0.0f, SCREEN_H - 2.0f, SCREEN_W, 2.0f, COL_BORDER);
    push_rect(0.0f, HEADER_H, 2.0f, SCREEN_H - HEADER_H, COL_BORDER);
    push_rect(SCREEN_W - 2.0f, HEADER_H, 2.0f, SCREEN_H - HEADER_H, COL_BORDER);

    if (!g->alive) {
        const char *title = g->won ? "YOU WIN" : "GAME OVER";
        const char *hint = "PRESS X";
        float title_scale = 3.0f;
        float hint_scale = 2.0f;

        push_rect(0.0f, HEADER_H, SCREEN_W, SCREEN_H - HEADER_H, COL_OVERLAY);

        draw_text(title, (SCREEN_W - font_text_width(title, 3)) / 2.0f,
                  108.0f, title_scale, g->won ? COL_SCORE : COL_GAMEOVER);
        draw_text(hint, (SCREEN_W - font_text_width(hint, 2)) / 2.0f,
                  146.0f, hint_scale, COL_TEXT);
    }

    if (vert_count > 0) {
        size_t bytes = (size_t)vert_count * sizeof(Vertex);
        Vertex *gu_verts = (Vertex *)sceGuGetMemory((int)bytes);
        if (gu_verts) {
            memcpy(gu_verts, verts, bytes);
            sceGuDrawArray(GU_SPRITES,
                           GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
                           vert_count, 0, gu_verts);
        }
    }

    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}
