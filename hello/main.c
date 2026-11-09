/*
 * hello/main.c - minimal PSP diagnostic EBOOT.
 *
 * Writes a boot trace to ms0:/hello_<variant>.log and mirrors it on the
 * debug screen. No GU, no heap and no VFPU: this is the control experiment
 * for the Snake/Cube 3D black-screen investigation.
 */

#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

PSP_MODULE_INFO("HelloLog", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);

#ifndef HELLO_VARIANT
#define HELLO_VARIANT "elf"
#endif

#define LOG_PATH "ms0:/hello_" HELLO_VARIANT ".log"

#ifdef HELLO_HEAP_KB
PSP_HEAP_SIZE_KB(HELLO_HEAP_KB);
#endif

static int log_ok;

static int log_probe(void)
{
    SceUID fd;
    const char *header = "\n==== HelloLog " HELLO_VARIANT " boot ====\n";

    fd = sceIoOpen(LOG_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd < 0)
        return 0;

    sceIoWrite(fd, header, strlen(header));
    sceIoClose(fd);
    log_ok = 1;
    return 1;
}

static void log_line(const char *fmt, ...)
{
    char msg[256];
    char line[320];
    va_list ap;
    int msg_len;
    int line_len;
    SceUID fd;

    if (!log_ok)
        return;

    va_start(ap, fmt);
    msg_len = vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    if (msg_len < 0)
        return;
    if (msg_len >= (int)sizeof(msg))
        msg_len = (int)sizeof(msg) - 1;

    line_len = snprintf(line, sizeof(line), "[%llu] %.*s\n",
                        (unsigned long long)sceKernelGetSystemTimeWide(),
                        msg_len, msg);
    if (line_len <= 0)
        return;
    if (line_len >= (int)sizeof(line))
        line_len = (int)sizeof(line) - 1;

    fd = sceIoOpen(LOG_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd < 0)
        return;
    sceIoWrite(fd, line, line_len);
    sceIoClose(fd);
}

static int exit_callback(int arg1, int arg2, void *common)
{
    (void)arg1;
    (void)arg2;
    (void)common;

    log_line("exit_callback: sceKernelExitGame");
    sceKernelExitGame();
    return 0;
}

static int callback_thread(SceSize args, void *argp)
{
    int cbid;

    (void)args;
    (void)argp;

    cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    log_line("callback_thread: cbid=%d", cbid);
    if (cbid >= 0)
        sceKernelRegisterExitCallback(cbid);

    sceKernelSleepThreadCB();
    return 0;
}

static void setup_callbacks(void)
{
    int thid = sceKernelCreateThread("update_thread", callback_thread, 0x11,
                                     0xFA0, 0, NULL);

    log_line("setup_callbacks: thid=%d", thid);
    if (thid >= 0) {
        int rc = sceKernelStartThread(thid, 0, NULL);
        log_line("setup_callbacks: start rc=%d", rc);
    }
}

int main(void)
{
    unsigned int frame = 0;
    int probe;

    probe = log_probe();
    log_line("main entered (log_probe=%d)", probe);

    pspDebugScreenInit();
    pspDebugScreenPrintf("HelloLog (%s)\n", HELLO_VARIANT);
    pspDebugScreenPrintf("main entered\n");

    if (probe) {
        pspDebugScreenPrintf("log: %s\n", LOG_PATH);
    } else {
        pspDebugScreenPrintf("LOG OPEN FAILED\n");
        log_line("log open failed");
    }

    log_line("before setup_callbacks");
    pspDebugScreenPrintf("before setup_callbacks\n");
    setup_callbacks();
    log_line("after setup_callbacks");
    pspDebugScreenPrintf("after setup_callbacks\n");

    log_line("entering loop");
    pspDebugScreenPrintf("entering loop\n");

    for (;;) {
        pspDebugScreenSetXY(0, 6);
        pspDebugScreenPrintf("alive %u\n", frame);

        if ((frame % 5) == 0)
            log_line("alive %u", frame);

        frame++;
        sceKernelDelayThread(1000000);
    }

    return 0;
}
