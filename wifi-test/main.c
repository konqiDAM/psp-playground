/*
 * wifi-test - connect to the PSP's saved access point and show the result.
 *
 * Diagnostics-only target: no GU, just pspDebugScreen. Copy to
 * ms0:/PSP/GAME/HOMEBREW/WifiTest/EBOOT.PBP, turn the WLAN switch on and run.
 * Configure a network in the PSP's Settings > Network Settings first; the
 * module connects to saved access-point slot 1.
 *
 * Controls: Cross = connect/reconnect, Circle = exit.
 */
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspiofilemgr.h>
#include <pspuser.h>
#include <pspwlan.h>

#include <stdio.h>
#include <string.h>

#include "download_psp.h"
#include "install_state.h"
#include "wifi.h"
#include "worker.h"

PSP_MODULE_INFO("WifiTest", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);
PSP_MAIN_THREAD_STACK_SIZE_KB(256);

/* All user RAM minus a 4 MiB system reserve; the network stack has its own
 * 512 KiB pool, so the game heap stays free for content. */
PSP_HEAP_SIZE_KB(-1);
PSP_HEAP_THRESHOLD_SIZE_KB(4096);

static volatile int s_exit_requested;
static uint32_t s_cancel_ms;

static int exit_callback(int arg1, int arg2, void *common)
{
    (void)arg1;
    (void)arg2;
    (void)common;
    s_exit_requested = 1;   /* Home: let the main thread clean up */
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
    int thid = sceKernelCreateThread("update_thread", callback_thread,
                                     0x11, 0xFA0, 0, 0);
    if (thid >= 0)
        sceKernelStartThread(thid, 0, 0);
}

static void draw_screen(void)
{
    pspDebugScreenSetXY(0, 0);
    pspDebugScreenPrintf("WiFi Test\n");
    pspDebugScreenPrintf("-----------------------------\n");
    pspDebugScreenPrintf("WLAN switch : %d\n", sceWlanGetSwitchState());
    pspDebugScreenPrintf("status      : %s\n", wifi_status_name(wifi_status()));
    pspDebugScreenPrintf("raw state   : %s (%d)\n",
                        wifi_state_name(wifi_raw_state()), wifi_raw_state());
    pspDebugScreenPrintf("IP          : %s\n", wifi_ip());
    pspDebugScreenPrintf("SSID        : %s\n", wifi_ssid());
    pspDebugScreenPrintf("RSSI        : %d%%\n", wifi_rssi());
    pspDebugScreenPrintf("last error  : 0x%08X\n",
                        (unsigned)wifi_last_error());
    pspDebugScreenPrintf("-----------------------------\n");
    pspDebugScreenPrintf("X reconnect   O exit\n");
}

static int read_text_file(const char *path, char *buf, int size)
{
    SceUID fd;
    int n;

    fd = sceIoOpen(path, PSP_O_RDONLY, 0777);
    if (fd < 0)
        return -1;
    n = sceIoRead(fd, buf, (SceSize)size);
    sceIoClose(fd);
    return n;
}

static int parse_u32(const char *s, uint32_t *out)
{
    uint64_t v = 0;
    int digits = 0;

    while (*s >= '0' && *s <= '9') {
        v = v * 10u + (uint64_t)(*s - '0');
        s++;
        digits++;
    }
    if (!digits || *s != '\0' || v > 0xFFFFFFFFu)
        return -1;
    *out = (uint32_t)v;
    return 0;
}

static int load_diag_job(const char *text, WifiDownloadJob *job)
{
    char line[128];
    int pos = 0;
    int len = (int)strlen(text);

    memset(job, 0, sizeof(*job));
    while (pos < len) {
        int start = pos;
        int end;
        char *eq;

        while (pos < len && text[pos] != '\n')
            pos++;
        end = pos;
        if (pos < len)
            pos++;
        if (end > start && text[end - 1] == '\r')
            end--;
        if (end - start <= 0 || end - start >= (int)sizeof(line))
            continue;
        memcpy(line, text + start, (size_t)(end - start));
        line[end - start] = '\0';
        eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq++ = '\0';
        if (strcmp(line, "url") == 0)
            snprintf(job->url, sizeof(job->url), "%s", eq);
        else if (strcmp(line, "target") == 0)
            snprintf(job->target, sizeof(job->target), "%s", eq);
        else if (strcmp(line, "sha256") == 0)
            snprintf(job->sha256, sizeof(job->sha256), "%s", eq);
        else if (strcmp(line, "id") == 0)
            snprintf(job->id, sizeof(job->id), "%s", eq);
        else if (strcmp(line, "version") == 0)
            snprintf(job->version, sizeof(job->version), "%s", eq);
        else if (strcmp(line, "size") == 0) {
            if (parse_u32(eq, &job->size) != 0)
                return -1;
        } else if (strcmp(line, "cancel_after") == 0) {
            if (parse_u32(eq, &job->cancel_after) != 0)
                return -1;
        } else if (strcmp(line, "cancel_ms") == 0) {
            if (parse_u32(eq, &s_cancel_ms) != 0)
                return -1;
        }
    }
    if (!job->url[0] || !job->target[0] || !job->sha256[0] ||
        job->size == 0)
        return -1;
    if (!job->id[0])
        snprintf(job->id, sizeof(job->id), "%s", "diag");
    if (!job->version[0])
        snprintf(job->version, sizeof(job->version), "%s", "1.0.0");
    return 0;
}

static const char *diag_result_name(DlError err)
{
    switch (err) {
    case DL_OK:                return "OK";
    case DL_ERR_CANCELLED:     return "CANCELLED";
    case DL_ERR_RANGE_REJECTED:return "RANGE_REJECTED";
    case DL_ERR_CHECKSUM:      return "CHECKSUM";
    case DL_ERR_NO_SPACE:      return "NO_SPACE";
    default:                   return dl_error_name(err);
    }
}

static void write_result_file(const char *result, uint32_t bytes,
                              const char *sha, int attempts, int http_status)
{
    char text[256];
    SceUID fd;
    int n;

    n = snprintf(text, sizeof(text),
                 "result=%s\nbytes=%u\nsha256=%s\nattempts=%d\nhttp=%d\n",
                 result, (unsigned)bytes, sha, attempts, http_status);
    fd = sceIoOpen("ms0:/WIFI_DL_RESULT.TXT",
                   PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0)
        return;
    (void)sceIoWrite(fd, text, (SceSize)n);
    sceIoClose(fd);
    (void)sceIoSync("ms0:", 0);
}

static WifiDownloadJob s_diag_job;
static WifiDownloadProgress s_diag_progress;
static DlError s_diag_err;

static int diag_cancel_thread(SceSize args, void *argp)
{
    (void)args;
    (void)argp;
    sceKernelDelayThread(s_cancel_ms * 1000);
    wifi_worker_cancel_download();
    return 0;
}

static int diag_download_thread(SceSize args, void *argp)
{
    (void)args;
    (void)argp;
    s_diag_err = download_psp_run(&s_diag_job, &s_diag_progress, NULL);
    return 0;
}

static int run_download_diagnostic(void)
{
    char text[512];
    WifiDownloadJob job;
    WifiDownloadProgress progress;
    DlError err;
    int n;
    int i;

    n = read_text_file("ms0:/WIFI_DL_TEST.TXT", text, (int)sizeof(text) - 1);
    if (n <= 0)
        return 0;
    text[n] = '\0';
    if (load_diag_job(text, &job) != 0) {
        write_result_file("BAD_TEST_FILE", 0, "", 0, 0);
        return 1;
    }

    wifi_profile_refresh();
    if (wifi_profile_count() > 0)
        wifi_set_profile(wifi_profile_at(0)->slot);
    (void)wifi_begin_connect();
    for (i = 0; i < 100 && !wifi_is_connected(); i++) {
        wifi_poll();
        sceKernelDelayThread(100 * 1000);
    }
    if (!wifi_is_connected()) {
        write_result_file("NO_LINK", 0, "", 0, 0);
        return 1;
    }

    download_psp_init();
    s_diag_job = job;
    memset(&s_diag_progress, 0, sizeof(s_diag_progress));
    s_diag_err = DL_ERR_NET;
    {
        SceUID cancel_th = -1;
        SceUID th;
        if (s_cancel_ms > 0) {
            cancel_th = sceKernelCreateThread("dl_cancel", diag_cancel_thread,
                                              0x30, 0x2000, 0, NULL);
            if (cancel_th >= 0)
                sceKernelStartThread(cancel_th, 0, NULL);
        }
        th = sceKernelCreateThread("dl_diag", diag_download_thread,
                                   0x30, 0x8000, 0, NULL);
        if (th >= 0) {
            sceKernelStartThread(th, 0, NULL);
            sceKernelWaitThreadEnd(th, NULL);
            sceKernelDeleteThread(th);
        }
        if (cancel_th >= 0) {
            sceKernelWaitThreadEnd(cancel_th, NULL);
            sceKernelDeleteThread(cancel_th);
        }
    }
    err = s_diag_err;
    write_result_file(diag_result_name(err), s_diag_progress.done, job.sha256,
                      s_diag_progress.attempt, s_diag_progress.http_status);
    download_psp_shutdown();
    (void)progress;
    return 1;
}

int main(void)
{    WifiStatus last_status = WIFI_STATUS_UNINITIALIZED;
    int last_state = -2;
    int rc;

    pspDebugScreenInit();
    setup_callbacks();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    pspDebugScreenPrintf("WiFi Test\n\n");
    pspDebugScreenPrintf("init...\n");
    rc = wifi_init();
    if (rc != WIFI_OK)
    {
        pspDebugScreenPrintf("wifi_init failed: %d (0x%08X)\n",
                            rc, (unsigned)wifi_last_error());
        if (rc == WIFI_ERR_SWITCH_OFF)
            pspDebugScreenPrintf("Turn the WLAN switch ON.\n");
        pspDebugScreenPrintf("\nPress O to exit.\n");
        while (!s_exit_requested)
        {
            SceCtrlData pad;
            sceCtrlPeekBufferPositive(&pad, 1);
            if (pad.Buttons & PSP_CTRL_CIRCLE)
                break;
            sceKernelDelayThread(50 * 1000);
        }
        /* Power-save is restored by wifi_shutdown(), not wifi_disconnect(). */
        wifi_shutdown();
        sceKernelExitGame();
        return 0;
    }

    if (run_download_diagnostic())
    {
        wifi_shutdown();
        sceKernelExitGame();
        return 0;
    }

    rc = wifi_begin_connect();
    if (rc != WIFI_OK)
    {
        pspDebugScreenPrintf("wifi_begin_connect failed: %d (0x%08X)\n",
                            rc, (unsigned)wifi_last_error());
    }

    while (!s_exit_requested)
    {
        WifiStatus st = wifi_poll();
        int raw = wifi_raw_state();
        SceCtrlData pad;

        if (st != last_status || raw != last_state)
        {
            draw_screen();
            last_status = st;
            last_state = raw;
        }

        sceCtrlPeekBufferPositive(&pad, 1);
        if (pad.Buttons & PSP_CTRL_CIRCLE)
            break;

        if (pad.Buttons & PSP_CTRL_CROSS)
        {
            if (st == WIFI_STATUS_CONNECTED)
                wifi_begin_reconnect();
            else if (st == WIFI_STATUS_ERROR || st == WIFI_STATUS_IDLE)
                wifi_begin_connect();
            /* Let the state change render on the next loop. */
            sceKernelDelayThread(150 * 1000);
        }

        sceKernelDelayThread(50 * 1000);
    }

    /* Power-save is restored by wifi_shutdown(), not wifi_disconnect(). */
    wifi_shutdown();
    sceKernelExitGame();
    return 0;
}
