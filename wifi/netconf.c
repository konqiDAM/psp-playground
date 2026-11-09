/*
 * netconf.c - modal sceUtilityNetconf dialog with a GU frame pump.
 *
 * The main UI is pspDebugScreen. GU is created only for this modal and is
 * always torn down before returning to the text screen. main.c performs the
 * mandatory two-frame text repaint after every return.
 */
#include "netconf.h"

#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspkernel.h>
#include <psputility.h>
#include <psputility_netconf.h>
#include <psputility_sysparam.h>

#include <stdint.h>
#include <string.h>

static uint32_t s_netconf_list[4096] __attribute__((aligned(64)));

static void gu_init(void)
{
    sceGuInit();
    sceGuStart(GU_DIRECT, s_netconf_list);

    sceGuDrawBuffer(GU_PSM_8888, (void *)0, WIFI_FB_STRIDE);
    sceGuDispBuffer(480, 272, (void *)(uintptr_t)WIFI_FB_SIZE,
                    WIFI_FB_STRIDE);
    sceGuOffset(2048 - 240, 2048 - 136);
    sceGuViewport(2048, 2048, 480, 272);
    sceGuScissor(0, 0, 480, 272);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_BLEND);
    sceGuClearColor(NETCONF_BG_COLOR);
    sceGuClear(GU_COLOR_BUFFER_BIT);

    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

static void gu_term(void)
{
    sceGuDisplay(GU_FALSE);
    sceGuTerm();
}

static void gu_frame(void)
{
    sceGuStart(GU_DIRECT, s_netconf_list);
    sceGuClearColor(NETCONF_BG_COLOR);
    sceGuClear(GU_COLOR_BUFFER_BIT);
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
}

/* Pump frames until the utility reports FINISHED/NONE (or errors).
 * Returns 1 when the dialog is closed, 0 if the bounded pump expired. */
static int pump_shutdown(void)
{
    int i;

    for (i = 0; i < NETCONF_SHUTDOWN_PUMP_FRAMES; i++) {
        int status = sceUtilityNetconfGetStatus();

        if (status == PSP_UTILITY_DIALOG_NONE ||
            status == PSP_UTILITY_DIALOG_FINISHED ||
            status < 0)
            return 1;

        gu_frame();
        sceDisplayWaitVblankStart();
        sceGuSwapBuffers();
    }
    return 0;
}

NetconfResult netconf_run(volatile const int *exit_requested, int *raw_error)
{
    pspUtilityNetconfData data;
    NetconfResult result = NETCONF_RESULT_OK;
    int status = PSP_UTILITY_DIALOG_NONE;
    int language = PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
    int shutdown_started = 0;
    uint32_t start;

    if (raw_error)
        *raw_error = 0;

    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE, &language);

    memset(&data, 0, sizeof(data));
    data.base.size = sizeof(data);
    data.base.language = language;
    data.base.buttonSwap = PSP_UTILITY_ACCEPT_CROSS;
    data.base.graphicsThread = 40;
    data.base.accessThread = 40;
    data.base.fontThread = 40;
    data.base.soundThread = 40;
    data.action = PSP_NETCONF_ACTION_CONNECTAP;
    data.hotspot = 0;
    data.wifisp = 0;
    data.adhocparam = NULL;

    gu_init();

    status = sceUtilityNetconfInitStart(&data);
    if (status < 0) {
        gu_term();
        pspDebugScreenInit();
        pspDebugScreenClear();
        if (raw_error)
            *raw_error = status;
        return NETCONF_ERR_INIT;
    }

    start = (uint32_t)sceKernelGetSystemTimeLow();

    for (;;) {
        if (exit_requested && *exit_requested) {
            if (!shutdown_started) {
                sceUtilityNetconfShutdownStart();
                shutdown_started = 1;
            }
            if (!pump_shutdown()) {
                if (raw_error)
                    *raw_error = NETCONF_ERR_TIMEOUT;
            }
            gu_term();
            pspDebugScreenInit();
            pspDebugScreenClear();
            return NETCONF_RESULT_EXITED;
        }

        status = sceUtilityNetconfGetStatus();
        if (status < 0) {
            if (raw_error)
                *raw_error = status;
            result = NETCONF_ERR_STATUS;
            break;
        }
        if (status == PSP_UTILITY_DIALOG_NONE ||
            status == PSP_UTILITY_DIALOG_FINISHED)
            break;

        gu_frame();
        if (status == PSP_UTILITY_DIALOG_VISIBLE)
            sceUtilityNetconfUpdate(1);
        else if (status == PSP_UTILITY_DIALOG_QUIT && !shutdown_started) {
            sceUtilityNetconfShutdownStart();
            shutdown_started = 1;
        }

        sceDisplayWaitVblankStart();
        sceGuSwapBuffers();

        if ((uint32_t)(sceKernelGetSystemTimeLow() - start) >=
            (uint32_t)NETCONF_TIMEOUT_MS * 1000u) {
            if (!shutdown_started) {
                sceUtilityNetconfShutdownStart();
                shutdown_started = 1;
            }
            /* Let the utility finish closing before GU is torn down. */
            pump_shutdown();
            if (raw_error)
                *raw_error = NETCONF_ERR_TIMEOUT;
            result = NETCONF_ERR_TIMEOUT;
            break;
        }
    }

    gu_term();
    pspDebugScreenInit();
    pspDebugScreenClear();
    return result;
}
