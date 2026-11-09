/*
 * download_psp.c - PSP storage/transport adapters and the download retry loop.
 *
 * Design follows Skiff (MIT, github.com/chiva/skiff) for the .part/.resume,
 * .new replacement and retry shape; reimplemented in our own C99.
 */
#include "download_psp.h"

#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <pspiofilemgr_devctl.h>
#include <pspwlan.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "checksum.h"
#include "download.h"
#include "download_state.h"
#include "http_client.h"
#include "install_state.h"

#include "net_probe.h"
#include "power.h"
#include "wifi.h"
#include "worker.h"

#define DLPSP_INSTALLED "ms0:/PSP/SAVEDATA/WIFICONN/INSTALLED.TXT"
#define DLPSP_INSTALLED_BAD "ms0:/PSP/SAVEDATA/WIFICONN/INSTALLED.BAD"
#define DLPSP_INSTALLED_TMP "ms0:/PSP/SAVEDATA/WIFICONN/INSTALLED.TMP"
#define DLPSP_BUFFER_BYTES (1024u * 1024u)

typedef struct {
    SceUID fd;
} DpspFile;

static uint8_t s_buffer[DLPSP_BUFFER_BYTES];
static const volatile int *s_stop_ptr;
static WifiDownloadProgress *s_progress;
static DlRate s_rate;
static uint32_t s_seq;
static int s_job_epoch;
static uint32_t s_cancel_after;
static int s_last_http_status;
static DownloadPspProgressFn s_progress_hook;
static void *s_progress_hook_ctx;

static uint32_t now_ms(void)
{
    return (uint32_t)sceKernelGetSystemTimeLow() / 1000u;
}

static int sce_low_errno(int ret)
{
    return ret & 0xFFFF;
}

static int dpsp_open(void *ctx, const char *path, int mode, void **file)
{
    DpspFile *f;
    int flags;

    (void)ctx;
    if (mode == DL_OPEN_READ)
        flags = PSP_O_RDONLY;
    else if (mode == DL_OPEN_WRITE)
        flags = PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC;
    else
        flags = PSP_O_WRONLY | PSP_O_CREAT;

    f = malloc(sizeof(*f));
    if (!f)
        return -1;
    f->fd = sceIoOpen(path, flags, 0777);
    if (f->fd < 0) {
        free(f);
        return -1;
    }
    *file = f;
    return 0;
}

static int dpsp_read(void *file, void *buf, int len)
{
    DpspFile *f = file;
    return sceIoRead(f->fd, buf, (SceSize)len);
}

static int dpsp_write(void *file, const void *buf, int len)
{
    DpspFile *f = file;
    return sceIoWrite(f->fd, buf, (SceSize)len);
}

static int dpsp_seek(void *file, uint32_t offset)
{
    DpspFile *f = file;
    return sceIoLseek(f->fd, (SceOff)offset, PSP_SEEK_SET) < 0 ? -1 : 0;
}

static int dpsp_sync(void *file)
{
    (void)file;
    return sceIoSync("ms0:", 0) < 0 ? -1 : 0;
}

static int dpsp_close(void *file)
{
    DpspFile *f = file;
    int r = sceIoClose(f->fd);
    free(f);
    return r < 0 ? -1 : 0;
}

static int dpsp_size(void *ctx, const char *path, uint32_t *out)
{
    SceIoStat st;
    int r;

    (void)ctx;
    r = sceIoGetstat(path, &st);
    if (r < 0)
        return sce_low_errno(r) == ENOENT ? 1 : -1;
    if (st.st_size < 0)
        return -1;
    *out = (uint32_t)st.st_size;
    return 0;
}

static int dpsp_remove(void *ctx, const char *path)
{
    int r;

    (void)ctx;
    r = sceIoRemove(path);
    if (r >= 0)
        return 0;
    return sce_low_errno(r) == ENOENT ? 1 : -1;
}

static int dpsp_rename(void *ctx, const char *from, const char *to)
{
    int r;

    (void)ctx;
    r = sceIoRename(from, to);
    if (r < 0)
        return -1;
    if (sceIoSync("ms0:", 0) < 0)
        return -1;
    return 0;
}

static int dpsp_mkdir(void *ctx, const char *path)
{
    int r;

    (void)ctx;
    r = sceIoMkdir(path, 0777);
    if (r >= 0)
        return 0;
    return sce_low_errno(r) == EEXIST ? 1 : -1;
}

int download_psp_quarantine_registry(const char *path)
{
    SceIoStat st;
    uint8_t used[100];
    char old[96];
    unsigned stamp = (unsigned)sceKernelGetSystemTimeLow();
    int slot;
    int i;

    if (sceIoGetstat(DLPSP_INSTALLED_BAD, &st) >= 0) {
        memset(used, 0, sizeof(used));
        for (i = 0; i < 100; i++) {
            snprintf(old, sizeof(old),
                     DLPSP_INSTALLED_BAD ".%08x-%02d", stamp, i);
            if (sceIoGetstat(old, &st) >= 0)
                used[i] = 1;
        }
        slot = install_bad_slot(used, 100);
        if (slot < 0)
            return -1; /* every archive name is used: keep all of them */
        snprintf(old, sizeof(old),
                 DLPSP_INSTALLED_BAD ".%08x-%02d", stamp, slot);
        if (sceIoRename(DLPSP_INSTALLED_BAD, old) < 0)
            return -1;
    }
    return sceIoRename(path, DLPSP_INSTALLED_BAD) < 0 ? -1 : 0;
}

static int dpsp_free(void *ctx, uint32_t *out)
{
    SceDevInf dev;
    uint64_t free_bytes;

    (void)ctx;
    SceDevInf *dev_ptr = &dev;

    memset(&dev, 0, sizeof(dev));
    if (sceIoDevctl("ms0:", SCE_PR_GETDEV, &dev_ptr, sizeof(dev_ptr),
                    NULL, 0) < 0)
        return 1;
    if (dev.sectorSize <= 0 || dev.sectorCount <= 0)
        return 1; /* PPSSPP/unknown device: no pre-check */
    free_bytes = (uint64_t)dev.freeClusters * (uint64_t)dev.sectorSize *
                 (uint64_t)dev.sectorCount;
    if (free_bytes == 0)
        return 1;
    *out = free_bytes > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)free_bytes;
    return 0;
}

static void dpsp_storage(DlStorage *st)
{
    memset(st, 0, sizeof(*st));
    st->open = dpsp_open;
    st->read = dpsp_read;
    st->write = dpsp_write;
    st->seek = dpsp_seek;
    st->sync = dpsp_sync;
    st->close = dpsp_close;
    st->size = dpsp_size;
    st->remove = dpsp_remove;
    st->rename = dpsp_rename;
    st->mkdir = dpsp_mkdir;
    st->free_bytes = dpsp_free;
}

/* The transport vtable has one ctx; keep the live HttpClient handle in it. */
static void *s_http_handle;

static int dpsp_topen2(void *ctx, const char *url, int has_range,
                       uint32_t range_start, const char *if_range,
                       DlResponse *out, int *net_err)
{
    HttpResponseHead head;
    int err = 0;

    (void)ctx;
    if (http_client_open(url, has_range, range_start, if_range,
                         &head, &err, &s_http_handle) != 0) {
        if (net_err) {
            if (err > 0)
                *net_err = err;
            else
                *net_err = err == HTTP_CLIENT_ERR_TIMEOUT ? DL_ERR_TIMEOUT
                           : err == HTTP_CLIENT_ERR_BAD_RESPONSE ? DL_ERR_BAD_RESPONSE
                                                                 : DL_ERR_NET;
        }
        return -1;
    }
    *out = head;
    s_last_http_status = head.status;
    return 0;
}

static int dpsp_tread(void *ctx, void *buf, int max, int *net_err)
{
    int n;
    int err = 0;

    (void)ctx;
    n = http_client_read(s_http_handle, buf, max, &err);
    if (n < 0 && net_err) {
        if (err > 0)
            *net_err = err; /* a DlError from the stop hook */
        else
            *net_err = err == HTTP_CLIENT_ERR_TIMEOUT ? DL_ERR_TIMEOUT
                       : err == HTTP_CLIENT_ERR_BAD_RESPONSE ? DL_ERR_BAD_RESPONSE
                                                             : DL_ERR_NET;
    }
    return n;
}

static void dpsp_tclose(void *ctx)
{
    (void)ctx;
    http_client_close(s_http_handle);
    s_http_handle = NULL;
}

static void dpsp_transport(DlTransport *tr)
{
    memset(tr, 0, sizeof(*tr));
    tr->open = dpsp_topen2;
    tr->read = dpsp_tread;
    tr->close = dpsp_tclose;
}

static int wait_online(int timeout_ms)
{
    int elapsed = 0;

    while (elapsed < timeout_ms) {
        if (s_stop_ptr && *s_stop_ptr)
            return DL_ERR_CANCELLED;
        if (wifi_worker_download_cancelled())
            return DL_ERR_CANCELLED;
        if (wifi_link_trusted())
            return 0;
        sceKernelDelayThread(100 * 1000);
        elapsed += 100;
    }
    return -1;
}

static void sleep_interruptible(uint32_t ms)
{
    while (ms > 0) {
        uint32_t slice = ms > 100 ? 100 : ms;
        if (s_stop_ptr && *s_stop_ptr)
            return;
        if (wifi_worker_download_cancelled())
            return;
        sceKernelDelayThread(slice * 1000);
        ms -= slice;
    }
}

static int dpsp_should_stop(void *ctx)
{
    (void)ctx;
    if (s_stop_ptr && *s_stop_ptr)
        return DL_ERR_CANCELLED;
    if (wifi_worker_download_cancelled())
        return DL_ERR_CANCELLED;
    if (!sceWlanGetSwitchState())
        return DL_ERR_WIFI_OFF;
    if (wifi_suspend_epoch() != s_job_epoch)
        return DL_ERR_SUSPENDED;
    if (!wifi_is_connected())
        return DL_ERR_CONNECTION_LOST;
    return 0;
}

static void dpsp_progress(void *ctx, uint32_t done, uint32_t total, int phase)
{
    (void)ctx;
    if (!s_progress)
        return;
    s_progress->done = done;
    s_progress->total = total;
    s_progress->phase = phase;
    s_progress->seq = ++s_seq;
    dl_rate_update(&s_rate, done, now_ms());
    s_progress->speed_bps = s_rate.rate;
    s_progress->eta_s = dl_eta_seconds(done, total, s_rate.rate);
    if (s_progress_hook)
        s_progress_hook(s_progress_hook_ctx, s_progress);
    if (s_cancel_after && done >= s_cancel_after)
        wifi_worker_cancel_download();
}

void download_psp_set_progress_hook(DownloadPspProgressFn fn, void *ctx)
{
    s_progress_hook = fn;
    s_progress_hook_ctx = ctx;
}

static int read_file(const char *path, char *buf, int size)
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

static void write_registry(const WifiDownloadJob *job)
{
    InstallRegistry reg;
    char text[INSTALLED_MAX_BYTES + 1];
    char out[INSTALLED_MAX_BYTES + 1];
    char target[INSTALLED_PATH_MAX];
    int n;
    int idx;
    SceUID fd;

    if (!dl_success_allows_registry(dpsp_should_stop(NULL)))
        return; /* stop landed between the success gate and this write */
    sceIoMkdir("ms0:/PSP/SAVEDATA", 0777);
    sceIoMkdir("ms0:/PSP/SAVEDATA/WIFICONN", 0777);

    n = read_file(DLPSP_INSTALLED, text, (int)sizeof(text) - 1);
    if (n > 0) {
        text[n] = '\0';
        if (installed_parse(text, n, &reg) != 0) {
            if (download_psp_quarantine_registry(DLPSP_INSTALLED) != 0)
                return; /* keep the damaged registry; do not overwrite */
            memset(&reg, 0, sizeof(reg));
        }
    } else {
        memset(&reg, 0, sizeof(reg));
    }

    snprintf(target, sizeof(target), "%s", job->target);
    idx = installed_find(&reg, job->id);
    if (idx < 0) {
        if (reg.count >= INSTALLED_MAX_RECORDS)
            return;
        idx = reg.count++;
        memset(&reg.records[idx], 0, sizeof(reg.records[idx]));
    }
    snprintf(reg.records[idx].id, sizeof(reg.records[idx].id), "%s", job->id);
    snprintf(reg.records[idx].version, sizeof(reg.records[idx].version), "%s",
             job->version);
    reg.records[idx].size = job->size;
    snprintf(reg.records[idx].sha256, sizeof(reg.records[idx].sha256), "%s",
             job->sha256);
    snprintf(reg.records[idx].path, sizeof(reg.records[idx].path), "%s",
             target);

    n = installed_render(&reg, out, (int)sizeof(out));
    if (n <= 0)
        return;
    fd = sceIoOpen(DLPSP_INSTALLED_TMP,
                   PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0)
        return;
    if (sceIoWrite(fd, out, (SceSize)n) != n) {
        sceIoClose(fd);
        return;
    }
    sceIoClose(fd);
    (void)sceIoSync("ms0:", 0);
    (void)sceIoRemove(DLPSP_INSTALLED);
    (void)sceIoRename(DLPSP_INSTALLED_TMP, DLPSP_INSTALLED);
    (void)sceIoSync("ms0:", 0);
}

int download_psp_init(void)
{
    return net_probe_init() == 0 ? 0 : -1;
}

void download_psp_shutdown(void)
{
    net_probe_shutdown();
}

int download_psp_free_bytes(uint32_t *out)
{
    return dpsp_free(NULL, out);
}

DlError download_psp_run(const WifiDownloadJob *job,
                         WifiDownloadProgress *progress,
                         const volatile int *stop)
{
    DlEnv env;
    DlSpec spec;
    DlRetryState retry;
    DlError err = DL_ERR_NET;
    uint32_t wait_ms = 0;
    int attempt = 0;

    if (!job || !progress)
        return DL_ERR_ARG;
    memset(progress, 0, sizeof(*progress));
    progress->total = job->size;
    s_progress = progress;
    s_stop_ptr = stop;
    s_job_epoch = wifi_suspend_epoch();
    s_cancel_after = job->cancel_after;
    memset(&retry, 0, sizeof(retry));
    dl_rate_reset(&s_rate, now_ms());

    memset(&spec, 0, sizeof(spec));
    spec.url = job->url;
    spec.target_path = job->target;
    spec.expected_size = job->size;
    spec.expected_sha256 = job->sha256;
    spec.entry_id = job->id;

    memset(&env, 0, sizeof(env));
    dpsp_transport(&env.transport);
    dpsp_storage(&env.storage);
    env.buffer = s_buffer;
    env.buffer_size = sizeof(s_buffer);
    env.should_stop = dpsp_should_stop;
    env.progress = dpsp_progress;

    progress->active = 1;
    http_client_set_stop_check(dpsp_should_stop, NULL);
    wifi_power_download_begin();

    for (;;) {
        DlRetryAction action;
        int rejoin_waited = 0;

        if (stop && *stop) {
            err = DL_ERR_CANCELLED;
            break;
        }
        if (wifi_worker_download_cancelled()) {
            err = DL_ERR_CANCELLED;
            break;
        }
        if (!sceWlanGetSwitchState()) {
            progress->phase = DL_PHASE_WAIT_WIFI;
            progress->seq = ++s_seq;
            sleep_interruptible(1000);
            continue;
        }
        if (wifi_suspend_epoch() != s_job_epoch || !wifi_link_trusted()) {
            int wr = wait_online(DL_REJOIN_WAIT_MS);
            if (wr == DL_ERR_CANCELLED) {
                err = DL_ERR_CANCELLED;
                break;
            }
            if (wr != 0) {
                err = DL_ERR_REJOIN;
                action = dl_retry_step(&retry, err, 0, &wait_ms);
                if (action == DL_RETRY_STOP || action == DL_RETRY_GIVE_UP)
                    break;
                progress->phase = DL_PHASE_RETRY;
                progress->seq = ++s_seq;
                sleep_interruptible(wait_ms);
                continue;
            }
            s_job_epoch = wifi_suspend_epoch();
            continue;
        }

        attempt++;
        progress->attempt = attempt;
        progress->phase = DL_PHASE_CONNECTING;
        progress->seq = ++s_seq;
        {
            uint32_t attempt_done = 0;
            uint32_t received = 0;
            env.attempt_bytes = &received;
            err = dl_download_once(&env, &spec, &attempt_done);
            progress->http_status = s_last_http_status;
            s_last_http_status = 0;
            if (err == DL_ERR_REJOIN || err == DL_ERR_SUSPENDED) {
                action = DL_RETRY_REJOIN;
            } else {
                action = dl_retry_step(&retry, err, received, &wait_ms);
            }
        }
        if (err == DL_OK) {
            int stop = dpsp_should_stop(NULL);
            progress->phase = DL_PHASE_DONE;
            progress->active = 0;
            progress->error = DL_OK;
            progress->seq = ++s_seq;
            if (dl_success_allows_registry(stop))
                write_registry(job);
            break; /* target is installed; never re-enter the retry loop */
        }
        if (err == DL_ERR_SUSPENDED) {
            int wr = wait_online(DL_REJOIN_WAIT_MS);
            rejoin_waited = 1;
            if (wr == 0) {
                s_job_epoch = wifi_suspend_epoch();
                continue;
            }
            err = DL_ERR_REJOIN;
            action = dl_retry_step(&retry, err, 0, &wait_ms);
        }
        if (err == DL_ERR_WIFI_OFF) {
            progress->phase = DL_PHASE_WAIT_WIFI;
            progress->seq = ++s_seq;
            sleep_interruptible(1000);
            continue;
        }

        if (action == DL_RETRY_REJOIN && !rejoin_waited) {
            int wr = wait_online(DL_REJOIN_WAIT_MS);
            if (wr == 0) {
                s_job_epoch = wifi_suspend_epoch();
                continue;
            }
            err = DL_ERR_REJOIN;
            action = dl_retry_step(&retry, err, 0, &wait_ms);
        }
        if (action == DL_RETRY_CONTINUE)
            continue;
        if (action == DL_RETRY_STOP || action == DL_RETRY_GIVE_UP)
            break;
        progress->phase = DL_PHASE_RETRY;
        progress->seq = ++s_seq;
        sleep_interruptible(wait_ms);
    }

    http_client_set_stop_check(NULL, NULL);
    wifi_power_download_end();
    progress->active = 0;
    progress->error = err;
    progress->seq = ++s_seq;
    if (err == DL_ERR_CANCELLED)
        progress->phase = DL_PHASE_CANCELLED;
    else if (err != DL_OK)
        progress->phase = DL_PHASE_FAILED;
    if (s_progress_hook)
        s_progress_hook(s_progress_hook_ctx, progress);
    return err;
}
