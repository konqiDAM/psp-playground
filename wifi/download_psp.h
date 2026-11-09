/*
 * download_psp.h - PSP adapter for the portable download engine.
 */
#ifndef PSP_WIFI_DOWNLOAD_PSP_H
#define PSP_WIFI_DOWNLOAD_PSP_H

#include <stdint.h>

#include "download_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char url[192];
    char target[176];
    uint32_t size;
    char sha256[65];
    char id[32];
    char version[16];
    uint32_t cancel_after; /* diagnostic only; 0 = never */
} WifiDownloadJob;

typedef struct {
    int active;
    int phase;
    uint32_t done;
    uint32_t total;
    uint32_t speed_bps;
    uint32_t eta_s;
    int attempt;
    int http_status;
    int error;
    uint32_t seq;
} WifiDownloadProgress;

typedef void (*DownloadPspProgressFn)(void *ctx,
                                     const WifiDownloadProgress *progress);
void download_psp_set_progress_hook(DownloadPspProgressFn fn, void *ctx);

int download_psp_init(void);
void download_psp_shutdown(void);
int download_psp_free_bytes(uint32_t *out); /* 0 ok, 1 unknown, <0 error */
/* Move a damaged registry to INSTALLED.BAD, preserving any earlier .BAD.
 * Returns 0 on success; nonzero means the caller must not overwrite it. */
int download_psp_quarantine_registry(const char *path);
DlError download_psp_run(const WifiDownloadJob *job,
                         WifiDownloadProgress *progress,
                         const volatile int *stop);

#ifdef __cplusplus
}
#endif

#endif /* PSP_WIFI_DOWNLOAD_PSP_H */
