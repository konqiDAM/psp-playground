/*
 * download_policy.h - portable retry, progress and power policy.
 */
#ifndef PSP_WIFI_DOWNLOAD_POLICY_H
#define PSP_WIFI_DOWNLOAD_POLICY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DL_BACKOFF_FIRST_MS 1000u
#define DL_BACKOFF_MAX_MS 16000u
#define DL_IDLE_ATTEMPTS_MAX 6
#define DL_WRITE_CHUNK_BYTES (1024u * 1024u)
#define DL_CHECKPOINT_BYTES (4u * 1024u * 1024u)
#define DL_STALL_TIMEOUT_MS 30000u
#define DL_CONNECT_TIMEOUT_MS 10000u
#define DL_CATALOG_CONNECT_TIMEOUT_MS 3000u
#define DL_CATALOG_TOTAL_TIMEOUT_MS 10000u
#define DL_REJOIN_WAIT_MS 15000u
#define DL_POWER_TICK_MS 5000u
#define DL_BACKLIGHT_DOWNLOAD_S 60

typedef enum {
    DL_OK = 0,
    DL_ERR_NET = 1,
    DL_ERR_DNS = 2,
    DL_ERR_TIMEOUT = 3,
    DL_ERR_STALL = 4,
    DL_ERR_CONNECTION_LOST = 5,
    DL_ERR_REJOIN = 6,
    DL_ERR_WIFI_OFF = 7,
    DL_ERR_SUSPENDED = 8,
    DL_ERR_CANCELLED = 9,
    DL_ERR_HTTP_STATUS = 10,
    DL_ERR_BAD_RESPONSE = 11,
    DL_ERR_RANGE_REJECTED = 12,
    DL_ERR_CHECKSUM = 13,
    DL_ERR_NO_SPACE = 14,
    DL_ERR_STORAGE = 15,
    DL_ERR_PATH = 16,
    DL_ERR_SELF = 17,
    DL_ERR_ARG = 18
} DlError;

typedef enum {
    DL_PHASE_IDLE = 0,
    DL_PHASE_CONNECTING,
    DL_PHASE_RECEIVING,
    DL_PHASE_VERIFYING,
    DL_PHASE_INSTALLING,
    DL_PHASE_WAIT_WIFI,
    DL_PHASE_RETRY,
    DL_PHASE_DONE,
    DL_PHASE_FAILED,
    DL_PHASE_CANCELLED
} DlPhase;

typedef enum {
    DL_RETRY_CONTINUE = 0,
    DL_RETRY_WAIT,
    DL_RETRY_REJOIN,
    DL_RETRY_GIVE_UP,
    DL_RETRY_STOP
} DlRetryAction;

typedef struct {
    int idle_attempts;
    int gave_up;
} DlRetryState;

uint32_t dl_backoff_ms(int idle_attempts);
int dl_error_retryable(DlError error);
/* Success may record the install only when no stop/cancel/suspend is pending. */
int dl_success_allows_registry(int stop);
DlRetryAction dl_retry_step(DlRetryState *state, DlError error,
                            uint32_t bytes_this_attempt, uint32_t *wait_ms);

typedef struct {
    uint32_t total;
    uint32_t rate;
    uint32_t window_done;
    uint32_t window_start_ms;
    int has_rate;
} DlRate;

void dl_rate_reset(DlRate *rate, uint32_t now_ms);
void dl_rate_update(DlRate *rate, uint32_t done, uint32_t now_ms);
int dl_percent(uint32_t done, uint32_t total);
uint32_t dl_eta_seconds(uint32_t done, uint32_t total, uint32_t rate);

typedef struct {
    int saved_cpu;
    int saved_bus;
    int boosted;
    int applied;
} DlClock;

int dl_clock_begin(DlClock *clock, int cpu, int bus);
int dl_clock_end(DlClock *clock, int *cpu, int *bus);

typedef struct {
    int saved_seconds;
    int changed;
} DlBacklight;

int dl_backlight_begin(DlBacklight *b, int current_s, int target_s);
int dl_backlight_end(DlBacklight *b, int *restore_s);

int dl_due(uint32_t now_ms, uint32_t last_ms, uint32_t interval_ms);
const char *dl_error_name(DlError error);
const char *dl_phase_name(DlPhase phase);

#ifdef __cplusplus
}
#endif

#endif /* PSP_WIFI_DOWNLOAD_POLICY_H */
