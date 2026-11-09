/*
 * wifi.h - reusable PSP Wi-Fi connection helper (clean-room, MIT).
 *
 * Connects to a saved access-point profile via sceNetApctl, exposes the
 * complete link information, enumerates saved profiles, handles the async
 * connect/disconnect states, keeps a reconnect supervisor, and owns the
 * global WLAN power-save setting while it is active.
 *
 * Use wifi_poll() once per frame (or every ~50 ms) and wifi_supervise()
 * right after it when auto-reconnect is enabled. See docs/WIFI.md.
 */
#ifndef PSP_WIFI_H
#define PSP_WIFI_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Default saved access-point slot. Slot 0 is special and is never used. */
#define WIFI_AP_INDEX 1
#define WIFI_DEFAULT_TIMEOUT_MS 30000
#define WIFI_MAX_PROFILES 16
#define WIFI_STRENGTH_UNKNOWN (-1)

typedef enum {
    WIFI_STATUS_UNINITIALIZED = 0,
    WIFI_STATUS_IDLE,
    WIFI_STATUS_CONNECTING,
    WIFI_STATUS_CONNECTED,
    WIFI_STATUS_DISCONNECTING,
    WIFI_STATUS_RECONNECTING,
    WIFI_STATUS_ERROR
} WifiStatus;

enum {
    WIFI_OK = 0,
    WIFI_ERR_ARG = -1,          /* bad argument */
    WIFI_ERR_SWITCH_OFF = -2,   /* WLAN switch is off */
    WIFI_ERR_NOT_INIT = -3,     /* call wifi_init() first */
    WIFI_ERR_BUSY = -4,         /* a connect/disconnect is already running */
    WIFI_ERR_TIMEOUT = -5,      /* state 4 was not reached in time */
    WIFI_ERR_STATE = -6,        /* APCTL reported an error */
    WIFI_ERR_MODULE = -7,       /* network module load failed */
    WIFI_ERR_CONNECT = -8       /* sceNetApctlConnect failed */
};

typedef struct {
    char profile_name[65];
    char ssid[33];
    int ssid_valid;           /* 1 when GetInfo(SSID) succeeded */
    unsigned char bssid[6];
    int bssid_valid;          /* 0 when GetInfo(BSSID) failed */
    int channel;              /* 0 when unknown */
    int strength_percent;     /* -1 unknown, 0..100 */
    int security_type;        /* -1 unknown, 0 none, 1 WEP, 2 WPA */
    char ip[16];
    int ip_valid;
    char netmask[16];
    char gateway[16];
    int gateway_valid;
    char primary_dns[16];
    int dns_primary_valid;
    char secondary_dns[16];
    int dns_secondary_valid;
    int use_proxy;            /* 0/1, -1 unknown */
    int proxy_port;           /* 0 when unknown */
    int power_save;           /* -1 unknown, 0 off, 1 on (APCTL view) */
    uint32_t updated_ms;
} WifiLinkInfo;

typedef struct {
    int slot;                 /* PSP profile slot, 1..WIFI_MAX_PROFILES */
    char ssid[33];
    int security_type;        /* -1 unknown, 0 none, 1 WEP, 2 WPA */
} WifiProfile;

/* Initialise the network stack and load the COMMON/INET modules. Idempotent. */
int wifi_init(void);

/* Tear down owned resources, restore power-save and unload owned modules. */
void wifi_shutdown(void);

/* Asynchronous connection to the selected saved profile. */
int wifi_begin_connect(void);
int wifi_begin_reconnect(void);

/* Advance the async state machine. Never blocks. */
WifiStatus wifi_poll(void);

/* Blocking conveniences. timeout_ms <= 0 means WIFI_DEFAULT_TIMEOUT_MS. */
int wifi_connect(int timeout_ms);
int wifi_reconnect(int timeout_ms);

/* Blocking disconnect (default 5 s). Does NOT restore WLAN power-save;
 * only wifi_shutdown() or wifi_power_save_restore() do. */
int wifi_disconnect(int timeout_ms);

/* State/query helpers. */
bool wifi_is_connected(void);
WifiStatus wifi_status(void);
int wifi_raw_state(void);
int wifi_last_error(void);
const char *wifi_ip(void);
const char *wifi_ssid(void);
int wifi_rssi(void);               /* legacy 0..100, 0 when unknown */

/* Detailed link information; refreshed at most once per second by poll. */
void wifi_refresh_link(void);
const WifiLinkInfo *wifi_link_info(void);
int wifi_strength_percent(void);   /* -1 unknown, else 0..100 */

/* Saved-profile enumeration and selection. */
int wifi_profile_refresh(void);    /* count, or negative error */
int wifi_profile_count(void);
const WifiProfile *wifi_profile_at(int pos);
int wifi_selected_profile(void);   /* slot, 0 = none */
int wifi_set_profile(int slot);    /* validates 1..WIFI_MAX_PROFILES */

/* Reconnect supervisor. Enable it with wifi_set_auto_reconnect(true) and
 * call wifi_supervise() every frame after wifi_poll(). */
void wifi_set_auto_reconnect(bool enabled);
bool wifi_auto_reconnect(void);
void wifi_supervise(void);
void wifi_supervisor_reset(void);
bool wifi_supervisor_gave_up(void);
int wifi_supervisor_attempts(void);
uint32_t wifi_supervisor_next_ms(void);

/* Observe an external APCTL change (e.g. after sceUtilityNetconf).
 * Does not change link ownership. */
int wifi_resync(void);
/* Mark the current AP link as app-owned so wifi_shutdown() disconnects it.
 * Used only after a netconf CONNECTED/PENDING outcome. */
void wifi_take_link_ownership(void);

/* Global WLAN power-save. Restore is success-conditional; the snapshot is
 * kept until a restore succeeds. These are WLAN system settings (PSP
 * Settings), NOT the download CPU/backlight session in wifi/power.h. */
int wifi_power_save_off(void);
int wifi_power_save_restore(void);
int wifi_power_save_state(void);   /* system parameter, -1 unknown */
int wifi_power_save_pending(void); /* 1 = snapshot not restored yet */

/* Suspend/resume lifecycle (power callback). The callback only bumps an
 * epoch; all APCTL work happens in wifi_resume_revalidate() on the main
 * thread. */
int wifi_suspend_epoch(void);
int wifi_resume_pending(void);
int wifi_resume_revalidate(void);

/* Cheap link health: WLAN switch + chip power + APCTL state 4. */
int wifi_link_ready(void);
int wifi_link_trusted(void);       /* connected and joined after the last suspend */

/* WLAN switch 1/0. */
int wifi_wlan_switch(void);

/* APCTL event/error recorded by the handler. Polling remains the source of
 * truth; these are diagnostics only. */
int wifi_apctl_event(void);        /* last event, -1 unknown */
int wifi_apctl_event_error(void);
int wifi_apctl_event_state(void);  /* new_state from the last event, -1 unknown */
int wifi_apctl_event_seq(void);    /* monotonic event counter */

/* Human-readable names for logs/UI. */
const char *wifi_status_name(WifiStatus status);
const char *wifi_state_name(int state);
const char *wifi_event_name(int event);
const char *wifi_security_name(int type);
const char *wifi_error_name(int error);

#ifdef __cplusplus
}
#endif

#endif /* PSP_WIFI_H */
