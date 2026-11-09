/*
 * worker.c - one background thread for probes, catalog and downloads.
 */
#include "worker.h"

#include <pspkernel.h>
#include <pspthreadman.h>

#include <stdio.h>
#include <string.h>

#include "http_client.h"
#include "install_state.h"
#include "wifi.h"

static SceUID s_thread = -1;
static SceUID s_sema = -1;
static SceUID s_lock = -1;
static int s_started;
static volatile int s_stop;
static volatile int s_job_seq;
static volatile int s_done_seq;
static volatile int s_taken_seq;
static volatile int s_download_cancel;
static volatile int s_dl_seq;
static volatile int s_dl_taken_seq;
static volatile int s_cat_seq;
static volatile int s_cat_taken_seq;
static WifiProbeJob s_job = WIFI_JOB_NONE;
static char s_gateway[16];
static char s_catalog_url[192];
static NetProbeReport s_report;
static WifiCatalogReport s_catalog;
static WifiDownloadJob s_dl_job;
static WifiDownloadProgress s_dl_progress;

static void publish_progress(void *ctx, const WifiDownloadProgress *p)
{
    (void)ctx;
    sceKernelWaitSema(s_lock, 1, NULL);
    s_dl_progress = *p;
    s_dl_seq++;
    sceKernelSignalSema(s_lock, 1);
}

static int catalog_stop(void *ctx)
{
    (void)ctx;
    return s_stop ? DL_ERR_CANCELLED : 0;
}

static int fetch_catalog(const char *url, WifiCatalogReport *out)
{
    HttpResponseHead head;
    void *handle = NULL;
    int err = 0;
    int total = 0;
    uint32_t start;

    memset(out, 0, sizeof(*out));
    http_client_set_stop_check(catalog_stop, NULL);
    if (http_client_open_ex(url, 0, 0, NULL, 3000, &head, &err, &handle) != 0) {
        http_client_set_stop_check(NULL, NULL);
        if (err > 0)
            return err;
        return err == HTTP_CLIENT_ERR_TIMEOUT ? DL_ERR_TIMEOUT
               : err == HTTP_CLIENT_ERR_BAD_RESPONSE ? DL_ERR_BAD_RESPONSE
                                                     : DL_ERR_NET;
    }
    if (head.status != 200 || head.chunked || head.encoded) {
        http_client_close(handle);
        http_client_set_stop_check(NULL, NULL);
        return DL_ERR_BAD_RESPONSE;
    }
    start = (uint32_t)(sceKernelGetSystemTimeLow() / 1000u);
    for (;;) {
        int n;
        if ((uint32_t)(sceKernelGetSystemTimeLow() / 1000u) - start >= 10000u) {
            http_client_close(handle);
            http_client_set_stop_check(NULL, NULL);
            return DL_ERR_TIMEOUT;
        }
        if (total >= (int)sizeof(out->text)) {
            http_client_close(handle);
            http_client_set_stop_check(NULL, NULL);
            return DL_ERR_BAD_RESPONSE;
        }
        n = http_client_read(handle, out->text + total,
                             (int)sizeof(out->text) - total, &err);
        if (n < 0) {
            http_client_close(handle);
            http_client_set_stop_check(NULL, NULL);
            if (err > 0)
                return err;
            return err == HTTP_CLIENT_ERR_TIMEOUT ? DL_ERR_TIMEOUT
                   : err == HTTP_CLIENT_ERR_BAD_RESPONSE ? DL_ERR_BAD_RESPONSE
                                                         : DL_ERR_NET;
        }
        if (n == 0)
            break;
        total += n;
    }
    http_client_close(handle);
    http_client_set_stop_check(NULL, NULL);
    out->len = total;
    {
        uint32_t free_bytes = 0;
        int frc = download_psp_free_bytes(&free_bytes);
        out->free_bytes = install_free_or_unknown(frc, free_bytes);
    }
    return DL_OK;
}

static int worker_main(SceSize args, void *argp)
{
    (void)args;
    (void)argp;

    for (;;) {
        WifiProbeJob job;

        sceKernelWaitSema(s_sema, 1, NULL);
        if (s_stop)
            break;

        job = s_job;
        if (job == WIFI_JOB_CATALOG) {
            WifiCatalogReport cat;
            int err = fetch_catalog(s_catalog_url, &cat);
            sceKernelWaitSema(s_lock, 1, NULL);
            s_catalog = cat;
            s_catalog.error = err;
            s_cat_seq++;
            s_done_seq = s_job_seq;
            s_taken_seq = s_done_seq;
            sceKernelSignalSema(s_lock, 1);
        } else if (job == WIFI_JOB_DOWNLOAD) {
            WifiDownloadJob dl;
            WifiDownloadProgress progress;

            sceKernelWaitSema(s_lock, 1, NULL);
            dl = s_dl_job;
            sceKernelSignalSema(s_lock, 1);
            memset(&progress, 0, sizeof(progress));
            download_psp_set_progress_hook(publish_progress, NULL);
            (void)download_psp_run(&dl, &progress, &s_stop);
            download_psp_set_progress_hook(NULL, NULL);
            sceKernelWaitSema(s_lock, 1, NULL);
            s_done_seq = s_job_seq;
            s_taken_seq = s_done_seq;
            sceKernelSignalSema(s_lock, 1);
        } else {
            NetProbeReport report;
            char gateway[16];

            sceKernelWaitSema(s_lock, 1, NULL);
            memcpy(gateway, s_gateway, sizeof(gateway));
            gateway[sizeof(gateway) - 1] = '\0';
            sceKernelSignalSema(s_lock, 1);

            memset(&report, 0, sizeof(report));
            if (job == WIFI_JOB_ALL || job == WIFI_JOB_DNS ||
                job == WIFI_JOB_TCP || job == WIFI_JOB_HTTP) {
                net_probe_run(&report, gateway, &s_stop);
            } else {
                report.started_ms = (uint32_t)sceKernelGetSystemTimeLow() / 1000u;
                report.finished_ms = report.started_ms;
            }

            sceKernelWaitSema(s_lock, 1, NULL);
            s_report = report;
            s_done_seq = s_job_seq;
            sceKernelSignalSema(s_lock, 1);
        }
    }

    return 0;
}

int wifi_worker_start(void)
{
    if (s_started)
        return WIFI_OK;

    memset(&s_report, 0, sizeof(s_report));
    memset(&s_catalog, 0, sizeof(s_catalog));
    memset(&s_dl_job, 0, sizeof(s_dl_job));
    memset(&s_dl_progress, 0, sizeof(s_dl_progress));
    s_stop = 0;
    s_job_seq = 0;
    s_done_seq = 0;
    s_taken_seq = 0;
    s_dl_seq = 0;
    s_dl_taken_seq = 0;
    s_cat_seq = 0;
    s_cat_taken_seq = 0;
    s_download_cancel = 0;
    s_job = WIFI_JOB_NONE;
    s_gateway[0] = '\0';
    s_catalog_url[0] = '\0';

    s_sema = sceKernelCreateSema("wifi_probe", 0, 0, 1, NULL);
    if (s_sema < 0)
        return WIFI_ERR_MODULE;

    s_lock = sceKernelCreateSema("wifi_probe_lock", 0, 1, 1, NULL);
    if (s_lock < 0) {
        sceKernelDeleteSema(s_sema);
        s_sema = -1;
        return WIFI_ERR_MODULE;
    }

    /* Resolver failure is non-fatal: TCP probes still work. */
    net_probe_init();

    s_thread = sceKernelCreateThread("wifi_probe_thread", worker_main,
                                     WIFI_WORKER_PRIORITY, WIFI_WORKER_STACK,
                                     0, NULL);
    if (s_thread < 0) {
        net_probe_shutdown();
        sceKernelDeleteSema(s_lock);
        sceKernelDeleteSema(s_sema);
        s_lock = -1;
        s_sema = -1;
        return WIFI_ERR_MODULE;
    }

    if (sceKernelStartThread(s_thread, 0, NULL) < 0) {
        sceKernelDeleteThread(s_thread);
        s_thread = -1;
        net_probe_shutdown();
        sceKernelDeleteSema(s_lock);
        sceKernelDeleteSema(s_sema);
        s_lock = -1;
        s_sema = -1;
        return WIFI_ERR_MODULE;
    }

    s_started = 1;
    return WIFI_OK;
}

int wifi_worker_stop(void)
{
    /* sceKernelWaitThreadEnd() takes microseconds, not milliseconds. */
    SceUInt timeout = WIFI_WORKER_JOIN_MS * 1000u;
    int ret;

    if (!s_started)
        return WIFI_OK;

    s_stop = 1;
    sceKernelSignalSema(s_sema, 1);

    ret = sceKernelWaitThreadEnd(s_thread, &timeout);
    if (ret < 0)
        return WIFI_ERR_TIMEOUT;

    sceKernelDeleteThread(s_thread);
    s_thread = -1;
    sceKernelDeleteSema(s_lock);
    s_lock = -1;
    sceKernelDeleteSema(s_sema);
    s_sema = -1;

    net_probe_shutdown();
    s_started = 0;
    return WIFI_OK;
}

int wifi_worker_busy(void)
{
    if (!s_started)
        return 0;
    return (s_job_seq != s_done_seq) || (s_done_seq != s_taken_seq) ||
           (s_cat_seq != s_cat_taken_seq) || (s_dl_seq != s_dl_taken_seq);
}

int wifi_worker_submit(WifiProbeJob job, const char *gateway)
{
    if (!s_started)
        return WIFI_ERR_NOT_INIT;
    if (wifi_worker_busy())
        return WIFI_ERR_BUSY;

    s_job = job;
    memset(s_gateway, 0, sizeof(s_gateway));
    if (gateway)
        strncpy(s_gateway, gateway, sizeof(s_gateway) - 1);

    s_job_seq++;
    sceKernelSignalSema(s_sema, 1);
    return WIFI_OK;
}

int wifi_worker_submit_catalog(const char *url)
{
    if (!s_started)
        return WIFI_ERR_NOT_INIT;
    if (!url || !url[0] || strlen(url) >= sizeof(s_catalog_url))
        return WIFI_ERR_ARG;
    if (wifi_worker_busy())
        return WIFI_ERR_BUSY;
    s_job = WIFI_JOB_CATALOG;
    snprintf(s_catalog_url, sizeof(s_catalog_url), "%s", url);
    s_job_seq++;
    sceKernelSignalSema(s_sema, 1);
    return WIFI_OK;
}

int wifi_worker_submit_download(const WifiDownloadJob *job)
{
    if (!s_started)
        return WIFI_ERR_NOT_INIT;
    if (!job)
        return WIFI_ERR_ARG;
    if (wifi_worker_busy())
        return WIFI_ERR_BUSY;
    sceKernelWaitSema(s_lock, 1, NULL);
    s_dl_job = *job;
    s_download_cancel = 0;
    s_job = WIFI_JOB_DOWNLOAD;
    s_job_seq++;
    sceKernelSignalSema(s_lock, 1);
    sceKernelSignalSema(s_sema, 1);
    return WIFI_OK;
}

int wifi_worker_take_report(NetProbeReport *out)
{
    int copied = 0;

    if (!out || !s_started)
        return 0;

    sceKernelWaitSema(s_lock, 1, NULL);
    if (s_done_seq != s_taken_seq) {
        *out = s_report;
        s_taken_seq = s_done_seq;
        copied = 1;
    }
    sceKernelSignalSema(s_lock, 1);

    return copied;
}

int wifi_worker_take_catalog(WifiCatalogReport *out)
{
    int copied = 0;

    if (!out || !s_started)
        return 0;
    sceKernelWaitSema(s_lock, 1, NULL);
    if (s_cat_seq != s_cat_taken_seq) {
        *out = s_catalog;
        s_cat_taken_seq = s_cat_seq;
        copied = 1;
    }
    sceKernelSignalSema(s_lock, 1);
    return copied;
}

int wifi_worker_take_progress(WifiDownloadProgress *out)
{
    int copied = 0;

    if (!out || !s_started)
        return 0;
    sceKernelWaitSema(s_lock, 1, NULL);
    if (s_dl_seq != s_dl_taken_seq) {
        *out = s_dl_progress;
        s_dl_taken_seq = s_dl_seq;
        copied = 1;
    }
    sceKernelSignalSema(s_lock, 1);
    return copied;
}

int wifi_worker_stack_free(void)
{
    if (!s_started || s_thread < 0)
        return -1;
    return sceKernelGetThreadStackFreeSize(s_thread);
}

void wifi_worker_cancel_download(void)
{
    s_download_cancel = 1;
}

int wifi_worker_download_cancelled(void)
{
    return s_download_cancel;
}
