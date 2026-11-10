/*
 * screen - the smallest possible PSP homebrew screen.
 *
 * No sceGu, no textures, no game logic: pspDebugScreen prints directly to the
 * framebuffer. This is the baseline used to separate "the EBOOT does not load"
 * from "the GU renderer has a bug".
 *
 * If this screen appears on a real PSP, the toolchain, PBP packaging and CFW
 * are working. If it does not, the problem is the PSP/CFW/path, not the game.
 */

#include <pspctrl.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspkernel.h>

PSP_MODULE_INFO("ScreenTest", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);
/* Deliberately minimal: this diagnostic keeps a 1 MiB heap. The games use
 * PSP_HEAP_SIZE_KB(-1) + a 4 MiB system reserve (PSP memory-partition notes). */
PSP_HEAP_SIZE_KB(1024);

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

    pspDebugScreenInit();
    pspDebugScreenClear();

    int frame = 0;

    for (;;) {
        SceCtrlData pad;
        sceCtrlPeekBufferPositive(&pad, 1);

        uint32_t pressed = pad.Buttons & ~previous_buttons;
        previous_buttons = pad.Buttons;

        if (pressed & PSP_CTRL_START)
            sceKernelExitGame();

        pspDebugScreenSetXY(0, 0);
        pspDebugScreenPrintf("SCREEN OK");
        pspDebugScreenSetXY(0, 2);
        pspDebugScreenPrintf("EBOOT loaded and executed.");
        pspDebugScreenSetXY(0, 4);
        pspDebugScreenPrintf("Toolchain: pspdev");
        pspDebugScreenSetXY(0, 6);
        pspDebugScreenPrintf("Display: 480x272 debug text");
        pspDebugScreenSetXY(0, 8);
        pspDebugScreenPrintf("START = exit to XMB");
        pspDebugScreenSetXY(0, 11);
        pspDebugScreenPrintf("frame %d", frame++);

        sceDisplayWaitVblankStart();
    }

    return 0;
}
