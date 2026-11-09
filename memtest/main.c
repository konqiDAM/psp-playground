/*
 * main.c - Memory Test for the Sony PSP.
 *
 * Tests all user-accessible RAM that the app can safely own: it allocates the
 * largest possible heap block, then runs address, pattern, walking-bit,
 * random and throughput tests over it. The app never writes outside that
 * block; the display list, UI texture, code and stack live in static memory.
 *
 * Controls: Cross = start / rerun, Start = exit (also aborts a running test),
 * HOME is handled by the system exit callback.
 */

#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspuser.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "font.h"
#include "memtest.h"

PSP_MODULE_INFO("MemoryTest", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

/* All user RAM minus a 4 MiB system reserve, exactly like the other apps.
 * A negative heap size means "all remaining" in this SDK; the threshold is
 * the only knob that controls the reserve (see docs/MEMTEST.md §1.3). */
PSP_HEAP_SIZE_KB(-1);
PSP_HEAP_THRESHOLD_SIZE_KB(4096);

#ifndef MEMTEST_MEMSIZE
#define MEMTEST_MEMSIZE 1
#endif

#define MEMTEST_VERSION "0.1.0"

#define SCREEN_W 480
#define SCREEN_H 272
#define UI_STRIDE 512
#define FB_SIZE (UI_STRIDE * SCREEN_H * 4)

#define HEAP_THRESHOLD_BYTES (4096u * 1024u)
#define APP_MARGIN_BYTES (1024u * 1024u)
#define ALLOC_STEP (512u * 1024u)
#define ALLOC_MIN (1024u * 1024u)
#define ALLOC_UPPER_FALLBACK (52u * 1024u * 1024u)
#define PART_OVERHEAD_BYTES (1280u * 1024u) /* ELF + stack + startup blocks */

#define COL_BG 0xFF0C1018u
#define COL_HEADER 0xFF1C2430u
#define COL_TEXT 0xFFE8EEF4u
#define COL_DIM 0xFF808A96u
#define COL_GREEN 0xFF40E060u
#define COL_RED 0xFFFF4040u
#define COL_YELLOW 0xFFF0D040u
#define COL_ORANGE 0xFFFF9020u
#define COL_BAR_BG 0xFF202A34u
#define COL_BAR_FILL 0xFF30A0FFu
#define COL_WHITE 0xFFFFFFFFu

#define BOOT_HOLD_US 2500000ull
#define RUN_REDRAW_US 150000ull

static uint32_t ui_tex[UI_STRIDE * SCREEN_H] __attribute__((aligned(64)));
static uint32_t display_list[65536] __attribute__((aligned(64)));

typedef struct {
    float u, v;
    uint32_t color;
    float x, y, z;
} UiVtx;

#define UI_VTX_FMT                                                             \
    (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D)

typedef struct {
    uint32_t *raw;       /* malloc() base, for free() */
    uint32_t *test;      /* 64-byte aligned buffer under test */
    size_t test_bytes;

    SceSize boot_total;  /* total free bytes when main() starts */
    SceSize boot_max;    /* largest free block when main() starts */
    SceSize heap_size;   /* estimated heap block size (test block + overhead) */
    int fw_version;

    MemtestResult result;
    int phase;
    uint32_t done;
    uint32_t total;

    uint64_t last_present_us;
    uint64_t run_start_us;
    int exit_requested;
} App;

/* ---------------------------------------------------------------- helpers */

static uint64_t psp_now_us(void)
{
    return (uint64_t)sceKernelGetSystemTimeWide();
}

static int start_held(void)
{
    SceCtrlData pad;

    if (sceCtrlPeekBufferPositive(&pad, 1) <= 0)
        return 0;
    return (pad.Buttons & PSP_CTRL_START) != 0;
}

static char *fmt_str(char *p, const char *s)
{
    while (*s)
        *p++ = *s++;
    *p = '\0';
    return p;
}

static char *fmt_dec(char *p, uint32_t value)
{
    char tmp[10];
    int n = 0;

    do {
        tmp[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u);

    while (n > 0)
        *p++ = tmp[--n];
    *p = '\0';
    return p;
}

static char *fmt_hex8(char *p, uint32_t value)
{
    static const char hex[] = "0123456789ABCDEF";
    int shift;

    *p++ = '0';
    *p++ = 'X';
    for (shift = 28; shift >= 0; shift -= 4)
        *p++ = hex[(value >> shift) & 0xFu];
    *p = '\0';
    return p;
}

static char *fmt_mb(char *p, uint64_t bytes)
{
    uint64_t mb10 = bytes * 10u / (1024u * 1024u);

    p = fmt_dec(p, (uint32_t)(mb10 / 10u));
    *p++ = '.';
    p = fmt_dec(p, (uint32_t)(mb10 % 10u));
    return fmt_str(p, " MB");
}

static char *fmt_speed(char *p, uint32_t mbps10)
{
    p = fmt_dec(p, mbps10 / 10u);
    *p++ = '.';
    p = fmt_dec(p, mbps10 % 10u);
    return fmt_str(p, " MBPS");
}

static char *fmt_time(char *p, uint64_t us)
{
    uint64_t ds = us / 100000u;

    p = fmt_dec(p, (uint32_t)(ds / 10u));
    *p++ = '.';
    p = fmt_dec(p, (uint32_t)(ds % 10u));
    return fmt_str(p, " S");
}

static char *fmt_percent(char *p, uint32_t done, uint32_t total)
{
    uint32_t pct = 0;

    if (total != 0u)
        pct = (uint32_t)(((uint64_t)done * 100u) / total);
    if (pct > 100u)
        pct = 100u;
    p = fmt_dec(p, pct);
    return fmt_str(p, "%");
}

/* ------------------------------------------------------------- framebuffer */

static void fb_clear(uint32_t color)
{
    int i;

    for (i = 0; i < UI_STRIDE * SCREEN_H; i++)
        ui_tex[i] = color;
}

static void fb_rect(int x, int y, int w, int h, uint32_t color)
{
    int row;

    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > SCREEN_W)
        w = SCREEN_W - x;
    if (y + h > SCREEN_H)
        h = SCREEN_H - y;
    if (w <= 0 || h <= 0)
        return;

    for (row = 0; row < h; row++) {
        uint32_t *dst = &ui_tex[(y + row) * UI_STRIDE + x];
        int col;

        for (col = 0; col < w; col++)
            dst[col] = color;
    }
}

static void fb_text(int x, int y, int scale, uint32_t color, const char *text)
{
    const char *p;

    for (p = text; *p; p++) {
        const uint8_t *glyph = font_glyph(*p);
        int row;

        if (glyph) {
            for (row = 0; row < 5; row++) {
                int col;

                for (col = 0; col < 3; col++) {
                    if (glyph[row] & (1u << (2 - col)))
                        fb_rect(x + col * scale, y + row * scale, scale, scale,
                                color);
                }
            }
        }
        x += 4 * scale;
    }
}

static int text_centered_x(const char *text, int scale)
{
    return (SCREEN_W - font_text_width(text, scale)) / 2;
}
static void fb_progress(int x, int y, int w, int h, uint32_t done,
                        uint32_t total)
{
    int fill = 0;

    fb_rect(x, y, w, h, COL_BAR_BG);
    if (total != 0u)
        fill = (int)(((uint64_t)w * done) / total);
    if (fill > 4)
        fb_rect(x + 2, y + 2, fill - 4, h - 4, COL_BAR_FILL);
}

static uint32_t status_color(int status)
{
    switch (status) {
    case MEMTEST_PASS:
        return COL_GREEN;
    case MEMTEST_FAIL:
        return COL_RED;
    case MEMTEST_RUNNING:
        return COL_YELLOW;
    case MEMTEST_ABORTED:
        return COL_ORANGE;
    default:
        return COL_DIM;
    }
}

/* ------------------------------------------------------------------- GU */

static void gu_init(void)
{
    void *draw_buf = (void *)0;
    void *disp_buf = (void *)(uintptr_t)FB_SIZE;

    sceGuInit();
    sceGuStart(GU_DIRECT, display_list);
    sceGuDrawBuffer(GU_PSM_8888, draw_buf, UI_STRIDE);
    sceGuDispBuffer(SCREEN_W, SCREEN_H, disp_buf, UI_STRIDE);
    sceGuOffset(2048 - SCREEN_W / 2, 2048 - SCREEN_H / 2);
    sceGuViewport(2048, 2048, SCREEN_W, SCREEN_H);
    sceGuScissor(0, 0, SCREEN_W, SCREEN_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_BLEND);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

static void ui_present(void)
{
    UiVtx *verts;

    /* The GE bypasses the CPU D-cache: flush the texture we just drew. */
    sceKernelDcacheWritebackRange(ui_tex, sizeof(ui_tex));

    sceGuStart(GU_DIRECT, display_list);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, UI_STRIDE, 512, UI_STRIDE, ui_tex);
    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);

    verts = (UiVtx *)sceGuGetMemory(2 * (int)sizeof(UiVtx));
    if (verts) {
        verts[0] = (UiVtx){0.0f, 0.0f, COL_WHITE, 0.0f, 0.0f, 0.0f};
        verts[1] = (UiVtx){(float)SCREEN_W, (float)SCREEN_H, COL_WHITE,
                           (float)SCREEN_W, (float)SCREEN_H, 0.0f};
        sceGuDrawArray(GU_SPRITES, UI_VTX_FMT, 2, NULL, verts);
    }

    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

/* -------------------------------------------------------------- allocation */

static SceSize partition_estimate(const App *app)
{
    return (SceSize)((uint64_t)app->heap_size + app->boot_total +
                     PART_OVERHEAD_BYTES);
}

static const char *model_label(SceSize part_bytes)
{
    if (part_bytes > 45u * 1024u * 1024u)
        return "PSP-2000/3000/GO 64MB EXT";
    if (part_bytes > 30u * 1024u * 1024u)
        return "64MB UNIT +16MB MODE";
    return "PSP-1000/STD 24MB UNIT";
}

static const char *partition_label(SceSize part_bytes)
{
    if (part_bytes > 45u * 1024u * 1024u)
        return "EXT ~55MB";
    if (part_bytes > 30u * 1024u * 1024u)
        return "+16MB ~40MB";
    return "STD 24MB";
}

static int allocate_test_block(App *app)
{
    size_t upper = ALLOC_UPPER_FALLBACK;
    size_t size;

    app->raw = NULL;
    app->test = NULL;
    app->test_bytes = 0;
    app->heap_size = 0;

    /* The C runtime creates the newlib heap before main() in this SDK, so the
     * partition APIs only show the ~4 MiB left outside it. If the APIs did
     * report the full partition, use it as a tighter upper bound. */
    if (app->boot_max > (SceSize)(HEAP_THRESHOLD_BYTES + ALLOC_MIN)) {
        size_t from_partition = (size_t)app->boot_max - HEAP_THRESHOLD_BYTES;

        if (from_partition < upper)
            upper = from_partition;
    }
    upper &= ~(size_t)63u;

    /* Descending probe: the first success is the biggest block we can own
     * while still leaving the full APP_MARGIN_BYTES free inside the heap. */
    for (size = upper; size >= ALLOC_MIN; size -= ALLOC_STEP) {
        void *raw = malloc(size + 64u);
        void *probe;
        uintptr_t aligned;

        if (!raw)
            continue;

        probe = malloc(APP_MARGIN_BYTES);
        if (!probe) {
            free(raw);
            continue;
        }

        /* Touch the probe before freeing it. At -O2 GCC knows that malloc and
         * free have no observable effect and deletes the whole probe when its
         * pointer is only tested for NULL (verified in the psp-gcc 15.2
         * disassembly: a once-shipped build allocated the entire heap and had
         * only ~32 KiB left). A volatile store keeps the allocation real. */
        {
            volatile uint8_t *touch = (volatile uint8_t *)probe;

            touch[0] = 0x5Au;
            touch[APP_MARGIN_BYTES - 1u] = 0xA5u;
        }
        free(probe);

        aligned = ((uintptr_t)raw + 63u) & ~(uintptr_t)63u;
        app->raw = (uint32_t *)raw;
        app->test = (uint32_t *)aligned;
        app->test_bytes = size;
        /* The heap still contains the untouched app margin, so add it to the
         * estimate reported on the boot screen. */
        app->heap_size = (SceSize)(size + APP_MARGIN_BYTES + 192u);
        return 0;
    }

    return -1;
}

static void release_test_block(App *app)
{
    if (app->raw) {
        free(app->raw);
        app->raw = NULL;
    }
    app->test = NULL;
    app->test_bytes = 0;
}

/* ----------------------------------------------------------------- screens */

static void draw_header(const char *title, const char *right,
                        uint32_t right_color)
{
    fb_rect(0, 0, SCREEN_W, 28, COL_HEADER);
    fb_text(12, 9, 2, COL_TEXT, title);
    if (right) {
        int x = SCREEN_W - 12 - font_text_width(right, 2);

        fb_text(x, 9, 2, right_color, right);
    }
}

static void draw_phase_grid(const App *app, int y)
{
    int i;

    for (i = 0; i < MEMTEST_PHASE_COUNT; i++) {
        char line[32];
        char *p;
        int col = i / 3;
        int row = i % 3;
        int x = 12 + col * 240;
        int yy = y + row * 14;

        p = fmt_dec(line, (uint32_t)(i + 1));
        p = fmt_str(p, " ");
        p = fmt_str(p, memtest_phase_name(i));
        fb_text(x, yy, 2, COL_TEXT, line);
        fb_text(x + 150, yy, 2,
                status_color(app->result.phases[i].status),
                memtest_status_name(app->result.phases[i].status));
    }
}

static void draw_boot(const App *app, uint64_t elapsed_us)
{
    char line[80];
    char *p;
    int y = 40;
    int left = 2 - (int)(elapsed_us / 1000000u);

    if (left < 0)
        left = 0;

    fb_clear(COL_BG);
    draw_header("MEMORY TEST", "V" MEMTEST_VERSION, COL_DIM);

    p = fmt_str(line, "MODEL ");
    p = fmt_str(p, model_label(partition_estimate(app)));
    fb_text(12, y, 2, COL_TEXT, line);
    y += 16;

    p = fmt_str(line, "FW ");
    p = fmt_hex8(p, (uint32_t)app->fw_version);
    fb_text(12, y, 2, COL_DIM, line);
    y += 16;

    p = fmt_str(line, "MEMSIZE REQ ");
    p = fmt_dec(p, (uint32_t)MEMTEST_MEMSIZE);
    p = fmt_str(p, "  ACT PART ~");
    p = fmt_mb(p, (uint64_t)partition_estimate(app));
    fb_text(12, y, 2, COL_TEXT, line);
    y += 16;

    p = fmt_str(line, "HEAP ~");
    p = fmt_mb(p, (uint64_t)app->heap_size);
    p = fmt_str(p, "  SYS FREE ");
    p = fmt_mb(p, (uint64_t)app->boot_total);
    fb_text(12, y, 2, COL_TEXT, line);
    y += 16;

    p = fmt_str(line, "SYS MAX BLOCK ");
    p = fmt_mb(p, (uint64_t)app->boot_max);
    p = fmt_str(p, "  ");
    p = fmt_str(p, partition_label(partition_estimate(app)));
    fb_text(12, y, 2, COL_DIM, line);
    y += 16;

    p = fmt_str(line, "TEST BLOCK ");
    p = fmt_mb(p, (uint64_t)app->test_bytes);
    fb_text(12, y, 2, COL_YELLOW, line);
    y += 16;


    fb_text(12, y, 2, COL_DIM, "RESERVE 4 MB OS + 1 MB APP");
    y += 22;

    fb_rect(12, y, 456, 1, COL_DIM);
    y += 12;

    p = fmt_str(line, "X START NOW   START EXIT");
    fb_text(12, y, 2, COL_TEXT, line);
    y += 18;

    p = fmt_str(line, "AUTO START IN ");
    p = fmt_dec(p, (uint32_t)left);
    fb_text(12, y, 2, COL_DIM, line);
}

static void draw_run(const App *app)
{
    char line[80];
    char *p;
    int y = 36;
    const MemtestPhaseResult *ph = &app->result.phases[app->phase];

    fb_clear(COL_BG);
    draw_header("MEMORY TEST", "RUNNING", COL_YELLOW);

    p = fmt_str(line, "PHASE ");
    p = fmt_dec(p, (uint32_t)(app->phase + 1));
    p = fmt_str(p, "/6 ");
    p = fmt_str(p, memtest_phase_name(app->phase));
    fb_text(12, y, 2, COL_TEXT, line);
    p = fmt_percent(line, app->done, app->total);
    fb_text(SCREEN_W - 12 - font_text_width(line, 2), y, 2, COL_YELLOW, line);
    y += 18;

    fb_progress(12, y, 456, 16, app->done, app->total);
    y += 24;

    p = fmt_str(line, "ERRORS ");
    p = fmt_dec(p, ph->errors);
    fb_text(12, y, 2, ph->errors ? COL_RED : COL_TEXT, line);
    y += 16;

    if (app->result.errors != 0u) {
        p = fmt_str(line, "FIRST ERR ");
        p = fmt_hex8(p, app->result.first_address);
        fb_text(12, y, 2, COL_RED, line);
        y += 16;

        p = fmt_str(line, "EXP ");
        p = fmt_hex8(p, app->result.first_expected);
        p = fmt_str(p, " GOT ");
        p = fmt_hex8(p, app->result.first_actual);
        fb_text(12, y, 2, COL_RED, line);
        y += 16;
    } else {
        fb_text(12, y, 2, COL_DIM, "FIRST ERR NONE");
        y += 16;
        if (app->result.phases[MEMTEST_PHASE_RANDOM].status == MEMTEST_PASS) {
            p = fmt_str(line, "CHECKSUM ");
            p = fmt_hex8(p, app->result.checksum_actual);
            fb_text(12, y, 2, COL_DIM, line);
        } else {
            fb_text(12, y, 2, COL_DIM, "CHECKSUM PENDING");
        }
        y += 16;
    }

    fb_rect(12, y, 456, 1, COL_DIM);
    y += 8;

    draw_phase_grid(app, y);
    y += 3 * 14 + 8;

    p = fmt_str(line, "TESTED ");
    p = fmt_mb(p, (uint64_t)app->test_bytes);
    p = fmt_str(p, "  TIME ");
    p = fmt_time(p, psp_now_us() - app->run_start_us);
    fb_text(12, y, 2, COL_TEXT, line);
    y += 18;

    fb_text(12, y, 2, COL_DIM, "START EXIT");
}

static void draw_done(const App *app)
{
    char line[80];
    char *p;
    int y = 34;
    const char *title;
    uint32_t color;

    if (app->result.aborted) {
        title = "ABORTED";
        color = COL_ORANGE;
    } else if (app->result.pass) {
        title = "PASS";
        color = COL_GREEN;
    } else {
        title = "FAIL";
        color = COL_RED;
    }

    fb_clear(COL_BG);
    draw_header("MEMORY TEST", "V" MEMTEST_VERSION, COL_DIM);

    fb_text(text_centered_x(title, 4), y, 4, color, title);
    p = fmt_str(line, "ERRORS ");
    p = fmt_dec(p, (uint32_t)app->result.errors);
    fb_text(SCREEN_W - 12 - font_text_width(line, 2), y + 6, 2,
            app->result.errors ? COL_RED : COL_GREEN, line);
    y += 30;

    fb_rect(12, y, 456, 1, COL_DIM);
    y += 8;

    draw_phase_grid(app, y);
    y += 3 * 14 + 8;

    p = fmt_str(line, "TESTED ");
    p = fmt_mb(p, (uint64_t)app->test_bytes);
    fb_text(12, y, 2, COL_TEXT, line);
    y += 16;

    p = fmt_str(line, "TIME ");
    p = fmt_time(p, app->result.elapsed_us);
    fb_text(12, y, 2, COL_TEXT, line);
    y += 16;

    if (app->result.phases[MEMTEST_PHASE_SPEED].status == MEMTEST_PASS) {
        p = fmt_str(line, "W ");
        p = fmt_speed(p, app->result.write_mbps10);
        p = fmt_str(p, "  R ");
        p = fmt_speed(p, app->result.read_mbps10);
        fb_text(12, y, 2, COL_TEXT, line);
    } else {
        fb_text(12, y, 2, COL_DIM, "SPEED NOT MEASURED");
    }
    y += 16;

    if (app->result.phases[MEMTEST_PHASE_RANDOM].status == MEMTEST_PASS) {
        p = fmt_str(line, "CHECKSUM ");
        p = fmt_hex8(p, app->result.checksum_actual);
        fb_text(12, y, 2, COL_DIM, line);
    }
    y += 18;

    if (app->result.errors != 0u) {
        p = fmt_str(line, "FIRST ERR ");
        p = fmt_hex8(p, app->result.first_address);
        fb_text(12, y, 2, COL_RED, line);
        y += 16;

        p = fmt_str(line, "EXP ");
        p = fmt_hex8(p, app->result.first_expected);
        p = fmt_str(p, " GOT ");
        p = fmt_hex8(p, app->result.first_actual);
        fb_text(12, y, 2, COL_RED, line);
    } else {
        fb_text(12, y, 2, COL_DIM, "FIRST ERR NONE");
    }

    fb_text(12, SCREEN_H - 22, 2, COL_TEXT, "X RERUN   START EXIT");
}

static void draw_noalloc(const App *app)
{
    char line[80];
    char *p;

    fb_clear(COL_BG);
    draw_header("MEMORY TEST", "V" MEMTEST_VERSION, COL_DIM);

    fb_text(12, 44, 2, COL_RED, "ALLOCATION FAILED");
    fb_text(12, 66, 2, COL_TEXT, "COULD NOT RESERVE 1 MB FOR TESTING");

    p = fmt_str(line, "FREE ");
    p = fmt_mb(p, (uint64_t)app->boot_total);
    p = fmt_str(p, "  MAX ");
    p = fmt_mb(p, (uint64_t)app->boot_max);
    fb_text(12, 92, 2, COL_DIM, line);

    fb_text(12, SCREEN_H - 44, 2, COL_TEXT, "X RETRY   START EXIT");
}

/* ------------------------------------------------------------- engine glue */

static void engine_flush(void *p, size_t bytes)
{
    sceKernelDcacheWritebackInvalidateRange(p, (unsigned int)bytes);
}

static uint64_t engine_now_us(void *ctx)
{
    (void)ctx;
    return psp_now_us();
}

static int engine_progress(void *ctx, const MemtestResult *result, int phase,
                           uint32_t done, uint32_t total)
{
    App *app = (App *)ctx;
    uint64_t now;

    (void)result;
    app->phase = phase;
    app->done = done;
    app->total = total;

    if (start_held()) {
        app->exit_requested = 1;
        return 1;
    }

    now = psp_now_us();
    if (now - app->last_present_us >= RUN_REDRAW_US) {
        app->last_present_us = now;
        draw_run(app);
        ui_present();
    }
    return 0;
}

static void run_tests(App *app)
{
    MemtestHooks hooks;
    int rc;

    memtest_init(&app->result, app->test, app->test_bytes);
    app->phase = 0;
    app->done = 0;
    app->total = 1;
    app->last_present_us = 0;

    hooks.flush = engine_flush;
    hooks.now_us = engine_now_us;
    hooks.progress = engine_progress;
    hooks.fault_inject = NULL;
    hooks.ctx = app;

    app->run_start_us = psp_now_us();
    draw_run(app);
    ui_present();

    rc = memtest_run(&app->result, &hooks);
    if (rc != 0) {
        /* Defensive: the buffer was validated by allocation. Treat as fail. */
        app->result.pass = 0;
        app->result.errors = 1;
    }

    draw_done(app);
    ui_present();
}

/* ------------------------------------------------------------------- main */

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
    int cbid;

    (void)args;
    (void)argp;

    cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
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

static void exit_app(App *app)
{
    release_test_block(app);
    sceGuTerm();
    sceKernelExitGame();
}

/* Wait on the boot screen; returns 0 to run, 1 to exit. */
static int boot_screen(App *app)
{
    uint64_t start = psp_now_us();
    uint32_t previous = 0;

    for (;;) {
        SceCtrlData pad;
        uint64_t elapsed;
        uint32_t pressed = 0;

        if (sceCtrlPeekBufferPositive(&pad, 1) > 0) {
            pressed = pad.Buttons & ~previous;
            previous = pad.Buttons;
        }

        if (start_held())
            return 1;
        if (pressed & PSP_CTRL_CROSS)
            return 0;

        elapsed = psp_now_us() - start;
        if (elapsed >= BOOT_HOLD_US)
            return 0;

        draw_boot(app, elapsed);
        ui_present();
        sceKernelDelayThread(50 * 1000);
    }
}

/* Wait on the result screen; returns 0 to rerun, 1 to exit. */
static int done_screen(App *app)
{
    uint32_t previous = 0;

    (void)app;
    for (;;) {
        SceCtrlData pad;
        uint32_t pressed = 0;

        if (sceCtrlPeekBufferPositive(&pad, 1) > 0) {
            pressed = pad.Buttons & ~previous;
            previous = pad.Buttons;
        }

        if (start_held())
            return 1;
        if (pressed & PSP_CTRL_CROSS)
            return 0;

        sceKernelDelayThread(30 * 1000);
    }
}

/* No-allocation error screen; returns 0 to retry, 1 to exit. */
static int noalloc_screen(App *app)
{
    uint32_t previous = 0;

    draw_noalloc(app);
    ui_present();

    for (;;) {
        SceCtrlData pad;
        uint32_t pressed = 0;

        if (sceCtrlPeekBufferPositive(&pad, 1) > 0) {
            pressed = pad.Buttons & ~previous;
            previous = pad.Buttons;
        }

        if (start_held())
            return 1;
        if (pressed & PSP_CTRL_CROSS)
            return 0;

        sceKernelDelayThread(30 * 1000);
    }
}

static void noalloc_loop(App *app)
{
    for (;;) {
        if (noalloc_screen(app))
            exit_app(app);
        if (allocate_test_block(app) == 0)
            return;
    }
}

int main(void)
{
    App app;

    memset(&app, 0, sizeof(app));

    /* Read as early as possible: with this SDK's CRT the newlib heap already
     * exists, so these are the exact system figures left outside it. The
     * actual partition is estimated from them (see docs/MEMTEST.md §1.3). */
    app.boot_total = sceKernelTotalFreeMemSize();
    app.boot_max = sceKernelMaxFreeMemSize();
    app.fw_version = sceKernelDevkitVersion();

    setup_callbacks();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    gu_init();

    if (allocate_test_block(&app) != 0)
        noalloc_loop(&app);

    for (;;) {
        int action;

        action = boot_screen(&app);
        if (action != 0)
            exit_app(&app);

        run_tests(&app);
        if (app.exit_requested)
            exit_app(&app);

        action = done_screen(&app);
        if (action != 0)
            exit_app(&app);

        release_test_block(&app);
        if (allocate_test_block(&app) != 0)
            noalloc_loop(&app);
    }

    return 0;
}
