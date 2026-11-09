/*
 * worker.h - one background thread for the blocking proxy probes.
 */
#ifndef PSP_WIFI_WORKER_H
#define PSP_WIFI_WORKER_H

#include "download_psp.h"
#include "net_probe.h"

typedef enum {
    WIFI_JOB_NONE = 0,
    WIFI_JOB_ALL,
    WIFI_JOB_DNS,
    WIFI_JOB_TCP,
    WIFI_JOB_HTTP,
    WIFI_JOB_CATALOG,
    WIFI_JOB_DOWNLOAD
} WifiProbeJob;

#define WIFI_WORKER_PRIORITY 0x30
#define WIFI_WORKER_STACK    0x8000
#define WIFI_WORKER_JOIN_MS  12000

typedef struct {
    char text[8192];
    int len;
    uint32_t free_bytes;
    int error;
} WifiCatalogReport;

int  wifi_worker_start(void);
int  wifi_worker_stop(void);   /* WIFI_OK or WIFI_ERR_TIMEOUT if join failed */
int  wifi_worker_submit(WifiProbeJob job, const char *gateway);
int  wifi_worker_busy(void);
int  wifi_worker_take_report(NetProbeReport *out);

int  wifi_worker_submit_catalog(const char *url);
int  wifi_worker_take_catalog(WifiCatalogReport *out);
int  wifi_worker_submit_download(const WifiDownloadJob *job);
int  wifi_worker_take_progress(WifiDownloadProgress *out);
int  wifi_worker_stack_free(void);

/* Download cancel flag shared with download_psp.c. Cleared by the worker
 * when the next download job starts; the accessor is safe from the
 * download thread. */
void wifi_worker_cancel_download(void);
int  wifi_worker_download_cancelled(void);

#endif /* PSP_WIFI_WORKER_H */
