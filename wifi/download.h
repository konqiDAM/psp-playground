/*
 * download.h - resumable transfer with injected transport/storage.
 *
 * Design follows Skiff (MIT) for the .part/.resume and .new protocol;
 * reimplemented in our own C99. No PSP headers.
 */
#ifndef PSP_WIFI_DOWNLOAD_H
#define PSP_WIFI_DOWNLOAD_H

#include <stddef.h>
#include <stdint.h>

#include "download_policy.h"
#include "http_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DL_TARGET_MAX 176
#define DL_PATH_MAX 192
#define DL_RESUME_READ_MAX 512
#define DL_VERIFY_CHUNK 65536

/* One definition of the HTTP response head; see net/http_logic.h. */
typedef HttpResponseHead DlResponse;

typedef struct {
    /* 0 = response head received (any status, see out->status);
     * -1 = network/protocol failure with *net_err set. */
    int (*open)(void *ctx, const char *url, int has_range, uint32_t range_start,
                const char *if_range, DlResponse *out, int *net_err);
    /* >0 bytes, 0 EOF, <0 error with *net_err set. */
    int (*read)(void *ctx, void *buf, int max, int *net_err);
    void (*close)(void *ctx);
    void *ctx;
} DlTransport;

#define DL_OPEN_READ   0
#define DL_OPEN_WRITE  1   /* create/truncate */
#define DL_OPEN_APPEND 2   /* create for write, no truncate (caller seeks) */

typedef struct {
    void *ctx;
    int (*open)(void *ctx, const char *path, int mode, void **file);
    int (*read)(void *file, void *buf, int len);
    int (*write)(void *file, const void *buf, int len);
    int (*seek)(void *file, uint32_t offset);
    int (*sync)(void *file);
    int (*close)(void *file);
    int (*size)(void *ctx, const char *path, uint32_t *out); /* 0 ok, 1 missing */
    int (*remove)(void *ctx, const char *path);              /* 0 ok, 1 missing */
    int (*rename)(void *ctx, const char *from, const char *to);
    int (*mkdir)(void *ctx, const char *path);               /* 0 ok, 1 exists */
    int (*free_bytes)(void *ctx, uint32_t *out);             /* 0 ok, 1 unknown */
} DlStorage;

typedef struct {
    const char *url;
    const char *target_path;
    uint32_t expected_size;
    const char *expected_sha256;
    const char *entry_id;      /* compared with "wifi" (self) */
} DlSpec;

typedef void (*DlProgressFn)(void *ctx, uint32_t done, uint32_t total, int phase);

typedef struct {
    DlTransport transport;
    DlStorage storage;
    uint8_t *buffer;
    size_t buffer_size;
    int (*should_stop)(void *ctx);   /* 0 or a DlError */
    void *stop_ctx;
    DlProgressFn progress;
    void *progress_ctx;
    uint32_t *attempt_bytes;   /* new body bytes received this attempt */
} DlEnv;

typedef enum {
    DL_RECOVER_NONE = 0,
    DL_RECOVER_READY,
    DL_RECOVER_INSTALLED
} DlRecoverResult;

DlRecoverResult dl_recover(const DlEnv *env, const DlSpec *spec);

DlError dl_download_once(const DlEnv *env, const DlSpec *spec,
                         uint32_t *bytes_done);

#ifdef __cplusplus
}
#endif

#endif /* PSP_WIFI_DOWNLOAD_H */
