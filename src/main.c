/*
 * main.c - Snake for the Sony PSP.
 *
 * Direct PSPSDK + sceGu. No SDL, no textures, no assets: every sprite is a
 * solid-colour rectangle.
 *
 * Controls: d-pad steers, X restarts after death/win, START exits.
 * HOME is handled by the system exit callback.
 */

#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspuser.h>

#include "game.h"
#include "render.h"

PSP_MODULE_INFO("Snake", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

/* Use the whole user partition minus 4 MiB kept for the system (savedata
 * utility modules, thread stacks). In this SDK a negative heap size means
 * "all remaining", not "leave N MiB": PSP_HEAP_SIZE_KB(-1024) would leave
 * only the threshold (512 KiB by default). See docs/PSP-DEV-GUIDE.md 1.4. */
PSP_HEAP_SIZE_KB(-1);
PSP_HEAP_THRESHOLD_SIZE_KB(4096);

#define GRID_COLS 30
#define GRID_ROWS 15

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
    snake_game_t game;
    uint32_t previous_buttons = 0;
    int frame_counter = 0;

    setup_callbacks();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    render_init();

    snake_init(&game, GRID_COLS, GRID_ROWS,
               (uint32_t)sceKernelGetSystemTimeLow());

    for (;;) {
        SceCtrlData pad;
        sceCtrlPeekBufferPositive(&pad, 1);

        uint32_t pressed = pad.Buttons & ~previous_buttons;
        previous_buttons = pad.Buttons;

        if (pressed & PSP_CTRL_START) {
            sceKernelExitGame();
        }

        if (pressed & PSP_CTRL_CROSS) {
            if (!game.alive || game.won) {
                snake_init(&game, GRID_COLS, GRID_ROWS,
                           (uint32_t)sceKernelGetSystemTimeLow());
                frame_counter = 0;
            }
        }

        if (game.alive && !game.won) {
            if (pressed & PSP_CTRL_UP)
                snake_set_dir(&game, SNAKE_DIR_UP);
            if (pressed & PSP_CTRL_DOWN)
                snake_set_dir(&game, SNAKE_DIR_DOWN);
            if (pressed & PSP_CTRL_LEFT)
                snake_set_dir(&game, SNAKE_DIR_LEFT);
            if (pressed & PSP_CTRL_RIGHT)
                snake_set_dir(&game, SNAKE_DIR_RIGHT);

            /* Start at ~6.7 moves/s, speed up every five foods. */
            int move_frames = 9 - game.score / 5;
            if (move_frames < 4)
                move_frames = 4;

            if (++frame_counter >= move_frames) {
                frame_counter = 0;
                snake_step(&game);
            }
        }

        render_draw(&game);
    }

    return 0;
}
