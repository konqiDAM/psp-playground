/*
 * wifi_logic.h - portable PSP Wi-Fi reconnect supervisor policy.
 *
 * No PSP headers: this file is compiled for the host tests and for the
 * PSP static library. It contains no I/O and never blocks.
 */
#ifndef PSP_WIFI_LOGIC_H
#define PSP_WIFI_LOGIC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_SUP_BACKOFF_MIN_MS     1000u
#define WIFI_SUP_BACKOFF_MAX_MS    30000u
#define WIFI_SUP_MAX_ATTEMPTS          8
#define WIFI_SUP_JITTER_MAX_MS      1000u
#define WIFI_SUP_SWITCH_RECHECK_MS 30000u

typedef enum {
    WIFI_SUP_WAIT = 0,
    WIFI_SUP_RECONNECT,
    WIFI_SUP_GAVE_UP
} WifiSupAction;

typedef struct {
    uint32_t now_ms;
    uint32_t jitter_ms;   /* caller supplies 0..WIFI_SUP_JITTER_MAX_MS */
    int connected;
    int in_flight;        /* a connect/disconnect/abort phase is active */
    int switch_on;
} WifiSupInput;

typedef struct {
    uint32_t next_attempt_ms;
    uint32_t backoff_ms;
    int attempts;
    int gave_up;
    int started;
} WifiSupervisor;

#define WIFI_HEALTH_INTERVAL_MS 5000u

typedef enum {
    WIFI_LINK_OK = 0,
    WIFI_LINK_NO_SWITCH,
    WIFI_LINK_CHIP_OFF,
    WIFI_LINK_NOT_CONNECTED,
    WIFI_LINK_UNKNOWN
} WifiLinkVerdict;

typedef struct {
    int saved_value;
    int have_snapshot;
    int restore_failures;
} WifiPowerSaveState;

typedef struct {
    int slot;                 /* < 0 = not registered */
} WifiPowerCallback;

WifiLinkVerdict wifi_link_verdict(int switch_on, int chip_on, int raw_state);
int wifi_health_should_drop(WifiLinkVerdict verdict, int consecutive_failures);
int wifi_health_due(uint32_t now_ms, uint32_t last_ms, uint32_t interval_ms);
int wifi_resume_needs_rejoin(uint32_t joined_epoch, uint32_t current_epoch,
                             WifiLinkVerdict verdict);

void wifi_power_save_state_reset(WifiPowerSaveState *s);
int  wifi_power_save_state_snapshot(WifiPowerSaveState *s, int current);
void wifi_power_save_state_restore_ok(WifiPowerSaveState *s);
int  wifi_power_save_state_restore_failed(WifiPowerSaveState *s);

int  wifi_power_cb_needs_register(const WifiPowerCallback *s);
void wifi_power_cb_registered(WifiPowerCallback *s, int slot);
void wifi_power_cb_unregistered(WifiPowerCallback *s);

void wifi_sup_reset(WifiSupervisor *s);
WifiSupAction wifi_sup_step(WifiSupervisor *s, const WifiSupInput *in);
int wifi_sup_ms_until(const WifiSupervisor *s, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* PSP_WIFI_LOGIC_H */
