/*
 * wifi.c - reusable PSP Wi-Fi connection helper (clean-room, MIT).
 *
 * Sequence: load COMMON+INET modules -> sceNetInit -> sceNetInetInit ->
 * sceNetApctlInit -> sceNetApctlConnect(selected slot) -> poll
 * sceNetApctlGetState() until STATE_GOT_IP. Disconnect is asynchronous, so
 * reconnect waits for state 0, settles 750 ms, then connects again.
 *
 * See docs/WIFI.md for the contract and pitfalls.
 */
#include "wifi.h"
#include "wifi_logic.h"

#include <pspkernel.h>
#include <pspnet.h>
#include <pspnet_inet.h>
#include <pspnet_apctl.h>
#include <psppower.h>
#include <pspthreadman.h>
#include <psputility.h>
#include <psputility_netmodules.h>
#include <psputility_netparam.h>
#include <psputility_sysparam.h>
#include <pspwlan.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define WIFI_ALREADY_LOADED_1  ((int)0x80110F01)
#define WIFI_ALREADY_LOADED_2  ((int)0x80110802)
#define WIFI_NET_ALREADY       ((int)0x80410201)
#define WIFI_INET_ALREADY      ((int)0x80410701)
#define WIFI_APCTL_ALREADY     ((int)0x80410B01)

#define CONNECT_TIMEOUT_US     (30u * 1000u * 1000u)
#define RECONNECT_TIMEOUT_US   (15u * 1000u * 1000u)
#define DISCONNECT_TIMEOUT_US  (5u * 1000u * 1000u)
#define ABORT_TIMEOUT_US       (3u * 1000u * 1000u)
#define SETTLE_US              (750u * 1000u)
#define REINIT_SETTLE_US       (500u * 1000u)
#define LINK_REFRESH_MS        1000u

enum {
    PHASE_NONE = 0,
    PHASE_CONNECTING,
    PHASE_DISCONNECTING,
    PHASE_SETTLE,
    PHASE_ABORTING
};

static int s_initialized;
static int s_loaded_common;
static int s_loaded_inet;
static int s_net_ready, s_net_owned;
static int s_inet_ready, s_inet_owned;
static int s_apctl_ready, s_apctl_owned;
static int s_ap_owned;

static WifiStatus s_status = WIFI_STATUS_UNINITIALIZED;
static int s_phase = PHASE_NONE;
static int s_last_error;
static int s_abort_cause;
static int s_raw_state = -1;
static int s_reinit_tried;
static uint32_t s_deadline;
static uint32_t s_settle_at;
static uint32_t s_last_link_refresh_ms;

static WifiLinkInfo s_link;
static char s_ip[16];
static char s_ssid[33];
static int s_rssi;

static WifiProfile s_profiles[WIFI_MAX_PROFILES];
static int s_profile_count;
static int s_profile = WIFI_AP_INDEX;
static int s_profile_valid;

static WifiPowerSaveState s_power_save;

static bool s_auto_reconnect;
static WifiSupervisor s_sup;

static int s_apctl_handler_id = -1;
static volatile int s_apctl_event = -1;
static volatile int s_apctl_event_error;
static volatile int s_apctl_event_state = -1;
static volatile int s_apctl_event_seq;
static volatile int s_last_apctl_event = -1;
static volatile int s_last_apctl_event_error;
static volatile int s_last_apctl_event_state = -1;
static volatile int s_last_apctl_event_seq;

static WifiPowerCallback s_power_cb = { -1 };
static SceUID s_power_cb_id = -1;
static volatile int s_suspend_epoch;
static volatile int s_resume_pending;
static uint32_t s_joined_suspend_epoch;

static uint32_t s_last_health_ms;
static int s_health_failures;

static uint32_t now_us(void)
{
    return (uint32_t)sceKernelGetSystemTimeLow();
}

static bool passed(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static int elapsed_ms(uint32_t now, uint32_t start)
{
    return (int)((now - start) / 1000u);
}

static int profile_slot_valid(int slot)
{
    if (slot < 1 || slot > WIFI_MAX_PROFILES)
        return 0;
    return sceUtilityCheckNetParam(slot) == 0;
}

static void clear_link(void)
{
    memset(&s_link, 0, sizeof(s_link));
    s_link.strength_percent = WIFI_STRENGTH_UNKNOWN;
    s_link.security_type = -1;
    s_link.use_proxy = -1;
    s_link.power_save = -1;
    s_ip[0] = '\0';
    s_ssid[0] = '\0';
    s_rssi = 0;
}

static void set_error(int error)
{
    if (s_last_error == 0 ||
        (s_last_error == WIFI_ERR_TIMEOUT && error != WIFI_ERR_TIMEOUT))
        s_last_error = error;

    s_status = WIFI_STATUS_ERROR;
    s_phase = PHASE_NONE;
}

static void copy_string(char *dst, size_t size, const char *src)
{
    size_t n = 0;

    if (!dst || size == 0)
        return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    /* Bound the source read too: firmware strings may fill the field. */
    while (n + 1 < size && src[n] != '\0')
        n++;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void refresh_link(void)
{
    SceNetApctlInfo info;
    WifiLinkInfo *l = &s_link;

    memset(l, 0, sizeof(*l));
    l->strength_percent = WIFI_STRENGTH_UNKNOWN;
    l->security_type = -1;
    l->use_proxy = -1;
    l->power_save = -1;
    l->updated_ms = now_us() / 1000u;

    memset(&info, 0, sizeof(info));
    if (sceNetApctlGetInfo(PSP_NET_APCTL_INFO_PROFILE_NAME, &info) >= 0)
        copy_string(l->profile_name, sizeof(l->profile_name), info.name);

    memset(&info, 0, sizeof(info));
    if (sceNetApctlGetInfo(PSP_NET_APCTL_INFO_SSID, &info) >= 0) {
        memcpy(l->ssid, info.ssid, 32);
        l->ssid[32] = '\0';
        l->ssid_valid = 1;
    }

    memset(&info, 0, sizeof(info));
    if (sceNetApctlGetInfo(PSP_NET_APCTL_INFO_BSSID, &info) >= 0) {
        memcpy(l->bssid, info.bssid, 6);
        l->bssid_valid = 1;
    }

    memset(&info, 0, sizeof(info));
    if (sceNetApctlGetInfo(PSP_NET_APCTL_INFO_CHANNEL, &info) >= 0)
        l->channel = info.channel;

    memset(&info, 0, sizeof(info));
    if (sceNetApctlGetInfo(PSP_NET_APCTL_INFO_STRENGTH, &info) >= 0)
        l->strength_percent = info.strength;

    memset(&info, 0, sizeof(info));
    if (sceNetApctlGetInfo(PSP_NET_APCTL_INFO_SECURITY_TYPE, &info) >= 0)
        l->security_type = (int)info.securityType;

    memset(&info, 0, sizeof(info));
    if (sceNetApctlGetInfo(PSP_NET_APCTL_INFO_IP, &info) >= 0) {
        copy_string(l->ip, sizeof(l->ip), info.ip);
        l->ip_valid = l->ip[0] != '\0';
    }

    memset(&info, 0, sizeof(info));
    if (sceNetApctlGetInfo(PSP_NET_APCTL_INFO_SUBNETMASK, &info) >= 0)
        copy_string(l->netmask, sizeof(l->netmask), info.subNetMask);

    memset(&info, 0, sizeof(info));
    if (sceNetApctlGetInfo(PSP_NET_APCTL_INFO_GATEWAY, &info) >= 0) {
        copy_string(l->gateway, sizeof(l->gateway), info.gateway);
        l->gateway_valid = l->gateway[0] != '\0';
    }

    memset(&info, 0, sizeof(info));
    if (sceNetApctlGetInfo(PSP_NET_APCTL_INFO_PRIMDNS, &info) >= 0) {
        copy_string(l->primary_dns, sizeof(l->primary_dns), info.primaryDns);
        l->dns_primary_valid = l->primary_dns[0] != '\0';
    }

    memset(&info, 0, sizeof(info));
    if (sceNetApctlGetInfo(PSP_NET_APCTL_INFO_SECDNS, &info) >= 0) {
        copy_string(l->secondary_dns, sizeof(l->secondary_dns),
                    info.secondaryDns);
        l->dns_secondary_valid = l->secondary_dns[0] != '\0';
    }

    memset(&info, 0, sizeof(info));
    if (sceNetApctlGetInfo(PSP_NET_APCTL_INFO_USE_PROXY, &info) >= 0)
        l->use_proxy = (int)info.useProxy;

    memset(&info, 0, sizeof(info));
    if (sceNetApctlGetInfo(PSP_NET_APCTL_INFO_PROXY_PORT, &info) >= 0)
        l->proxy_port = (int)info.proxyPort;

    memset(&info, 0, sizeof(info));
    if (sceNetApctlGetInfo(PSP_NET_APCTL_INFO_POWER_SAVE, &info) >= 0)
        l->power_save = (int)info.powerSave;

    copy_string(s_ip, sizeof(s_ip), l->ip);
    copy_string(s_ssid, sizeof(s_ssid), l->ssid);
    s_rssi = (l->strength_percent < 0) ? 0 : l->strength_percent;
    s_last_link_refresh_ms = (uint32_t)(l->updated_ms);
}

static int power_save_off(void)
{
    int current = -1;
    int ret;

    ret = sceUtilityGetSystemParamInt(
        PSP_SYSTEMPARAM_ID_INT_WLAN_POWERSAVE, &current);
    if (ret != 0)
        return ret;

    wifi_power_save_state_snapshot(&s_power_save, current);

    if (current == PSP_SYSTEMPARAM_WLAN_POWERSAVE_OFF)
        return WIFI_OK;

    ret = sceUtilitySetSystemParamInt(
        PSP_SYSTEMPARAM_ID_INT_WLAN_POWERSAVE,
        PSP_SYSTEMPARAM_WLAN_POWERSAVE_OFF);
    return ret;
}

int wifi_power_save_off(void)
{
    return power_save_off();
}

int wifi_power_save_restore(void)
{
    int attempt;
    int ret = WIFI_OK;

    if (!s_power_save.have_snapshot)
        return WIFI_OK;

    for (attempt = 0; attempt < 3; attempt++) {
        ret = sceUtilitySetSystemParamInt(
            PSP_SYSTEMPARAM_ID_INT_WLAN_POWERSAVE,
            s_power_save.saved_value);
        if (ret == 0) {
            wifi_power_save_state_restore_ok(&s_power_save);
            return WIFI_OK;
        }
        if (!wifi_power_save_state_restore_failed(&s_power_save))
            break;
        sceKernelDelayThread(100 * 1000);
    }
    return ret != 0 ? ret : WIFI_ERR_STATE;
}

int wifi_power_save_pending(void)
{
    return s_power_save.have_snapshot ? 1 : 0;
}

int wifi_power_save_state(void)
{
    int value = -1;

    if (sceUtilityGetSystemParamInt(
            PSP_SYSTEMPARAM_ID_INT_WLAN_POWERSAVE, &value) != 0)
        return -1;
    return value;
}

void wifi_supervisor_reset(void)
{
    wifi_sup_reset(&s_sup);
}

bool wifi_supervisor_gave_up(void)
{
    return s_sup.gave_up != 0;
}

int wifi_supervisor_attempts(void)
{
    return s_sup.attempts;
}

uint32_t wifi_supervisor_next_ms(void)
{
    return s_sup.next_attempt_ms;
}

void wifi_set_auto_reconnect(bool enabled)
{
    s_auto_reconnect = enabled;
}

bool wifi_auto_reconnect(void)
{
    return s_auto_reconnect;
}

static void on_connected(void)
{
    s_phase = PHASE_NONE;
    s_status = WIFI_STATUS_CONNECTED;
    s_raw_state = PSP_NET_APCTL_STATE_GOT_IP;
    s_last_error = 0;
    s_joined_suspend_epoch = (uint32_t)s_suspend_epoch;
    s_health_failures = 0;
    s_last_health_ms = now_us() / 1000u;
    refresh_link();
    power_save_off();
    wifi_supervisor_reset();
}

static void apctl_event_handler(int old_state, int new_state, int event,
                                int error, void *arg)
{
    (void)old_state;
    (void)arg;

    s_apctl_event = event;
    s_apctl_event_error = error;
    s_apctl_event_state = new_state;
    s_apctl_event_seq++;
    s_last_apctl_event = event;
    s_last_apctl_event_error = error;
    s_last_apctl_event_state = new_state;
    s_last_apctl_event_seq = s_apctl_event_seq;
}

static int on_power(int count, int flags, void *arg)
{
    (void)count;
    (void)arg;

    if (flags & (PSP_POWER_CB_SUSPENDING | PSP_POWER_CB_STANDBY)) {
        s_suspend_epoch++;
        s_resume_pending = 0;
    }
    if (flags & (PSP_POWER_CB_RESUMING | PSP_POWER_CB_RESUME_COMPLETE))
        s_resume_pending = 1;
    return 0;
}

static int install_power_callback(void)
{
    int slot;

    if (!wifi_power_cb_needs_register(&s_power_cb))
        return WIFI_OK;
    s_power_cb_id = sceKernelCreateCallback("wifi_power", on_power, NULL);
    if (s_power_cb_id < 0)
        return WIFI_ERR_MODULE;
    slot = scePowerRegisterCallback(-1, s_power_cb_id);
    if (slot < 0) {
        sceKernelDeleteCallback(s_power_cb_id);
        s_power_cb_id = -1;
        return WIFI_ERR_MODULE;
    }
    wifi_power_cb_registered(&s_power_cb, slot);
    return WIFI_OK;
}

static void remove_power_callback(void)
{
    if (s_power_cb.slot >= 0)
        scePowerUnregisterCallback(s_power_cb.slot);
    if (s_power_cb_id >= 0)
        sceKernelDeleteCallback(s_power_cb_id);
    s_power_cb_id = -1;
    wifi_power_cb_unregistered(&s_power_cb);
}

static int install_apctl_handler(void)
{
    if (s_apctl_handler_id >= 0)
        return WIFI_OK;

    s_apctl_handler_id = sceNetApctlAddHandler(apctl_event_handler, NULL);
    return s_apctl_handler_id >= 0 ? WIFI_OK : WIFI_ERR_MODULE;
}

static void remove_apctl_handler(void)
{
    if (s_apctl_handler_id < 0)
        return;

    sceNetApctlDelHandler(s_apctl_handler_id);
    s_apctl_handler_id = -1;
    s_apctl_event = -1;
    s_apctl_event_error = 0;
}

static int take_apctl_event(void)
{
    int event = s_apctl_event;

    if (event != PSP_NET_APCTL_EVENT_ERROR)
        return 0;

    s_apctl_event = -1;
    s_apctl_event_error = 0;
    set_error(s_apctl_event_error != 0 ? s_apctl_event_error : WIFI_ERR_STATE);
    return 1;
}

static int read_apctl_state(void)
{
    int state = -1;

    if (!s_apctl_ready)
        return -1;
    if (sceNetApctlGetState(&state) < 0)
        return -1;
    return state;
}

static int note_init_result(int ret, int already, int *ready, int *owned)
{
    if (ret >= 0) {
        *ready = 1;
        *owned = 1;
        return WIFI_OK;
    }
    if (ret == already) {
        *ready = 1;
        *owned = 0;
        return WIFI_OK;
    }
    return ret;
}

static void wifi_teardown(int keep_error)
{
    remove_power_callback();

    if (!s_initialized && !s_loaded_common && !s_loaded_inet) {
        wifi_power_save_restore();
        if (!keep_error) {
            s_last_error = 0;
            s_status = WIFI_STATUS_UNINITIALIZED;
        }
        return;
    }

    if (s_ap_owned && s_apctl_ready)
        wifi_disconnect(5000);

    /* wifi_disconnect() deliberately leaves the global setting alone.
     * Shutdown is the single owner that restores it, on every path. */
    wifi_power_save_restore();

    remove_apctl_handler();

    if (s_apctl_owned) {
        sceNetApctlTerm();
        s_apctl_owned = 0;
        s_apctl_ready = 0;
    }
    if (s_inet_owned) {
        sceNetInetTerm();
        s_inet_owned = 0;
        s_inet_ready = 0;
    }
    if (s_net_owned) {
        sceNetTerm();
        s_net_owned = 0;
        s_net_ready = 0;
    }
    if (s_loaded_inet) {
        sceUtilityUnloadNetModule(PSP_NET_MODULE_INET);
        s_loaded_inet = 0;
    }
    if (s_loaded_common) {
        sceUtilityUnloadNetModule(PSP_NET_MODULE_COMMON);
        s_loaded_common = 0;
    }

    s_initialized = 0;
    s_phase = PHASE_NONE;
    s_raw_state = -1;
    s_ap_owned = 0;
    clear_link();

    if (keep_error) {
        s_last_error = keep_error;
        s_status = WIFI_STATUS_ERROR;
    } else {
        s_last_error = 0;
        s_status = WIFI_STATUS_UNINITIALIZED;
    }
}

static int fail_init(int cause)
{
    wifi_teardown(cause);
    if (cause == WIFI_ERR_SWITCH_OFF)
        return WIFI_ERR_SWITCH_OFF;
    if (cause == WIFI_ERR_ARG)
        return WIFI_ERR_ARG;
    return WIFI_ERR_MODULE;
}

int wifi_init(void)
{
    int ret;
    int state;

    if (s_initialized)
        return WIFI_OK;

    if (sceWlanGetSwitchState() == 0) {
        s_status = WIFI_STATUS_ERROR;
        s_last_error = WIFI_ERR_SWITCH_OFF;
        return WIFI_ERR_SWITCH_OFF;
    }

    s_initialized = 1;
    s_status = WIFI_STATUS_IDLE;
    s_phase = PHASE_NONE;
    s_raw_state = -1;
    s_last_error = 0;
    s_profile = WIFI_AP_INDEX;
    s_profile_valid = 0;
    clear_link();

    ret = sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON);
    if (ret >= 0)
        s_loaded_common = 1;
    else if (ret != WIFI_ALREADY_LOADED_1 && ret != WIFI_ALREADY_LOADED_2)
        return fail_init(ret);

    ret = sceUtilityLoadNetModule(PSP_NET_MODULE_INET);
    if (ret >= 0)
        s_loaded_inet = 1;
    else if (ret != WIFI_ALREADY_LOADED_1 && ret != WIFI_ALREADY_LOADED_2)
        return fail_init(ret);

    ret = note_init_result(sceNetInit(512 * 1024, 42, 4096, 42, 4096),
                           WIFI_NET_ALREADY, &s_net_ready, &s_net_owned);
    if (ret != WIFI_OK)
        return fail_init(ret);

    ret = note_init_result(sceNetInetInit(), WIFI_INET_ALREADY,
                           &s_inet_ready, &s_inet_owned);
    if (ret != WIFI_OK)
        return fail_init(ret);

    ret = note_init_result(sceNetApctlInit(0x2000, 42), WIFI_APCTL_ALREADY,
                           &s_apctl_ready, &s_apctl_owned);
    if (ret != WIFI_OK)
        return fail_init(ret);

    install_apctl_handler(); /* diagnostics only; failure is non-fatal */
    install_power_callback(); /* diagnostics/lifecycle; failure is non-fatal */
    wifi_power_save_state_reset(&s_power_save);

    state = read_apctl_state();
    if (state >= 0)
        s_raw_state = state;

    s_status = WIFI_STATUS_IDLE;
    s_phase = PHASE_NONE;
    return WIFI_OK;
}

void wifi_shutdown(void)
{
    wifi_teardown(0);
}

int wifi_profile_refresh(void)
{
    int i;
    int count = 0;

    s_profile_count = 0;

    for (i = 1; i <= WIFI_MAX_PROFILES; i++) {
        WifiProfile *p;
        netData data;

        if (sceUtilityCheckNetParam(i) != 0)
            continue;

        p = &s_profiles[count];
        memset(p, 0, sizeof(*p));
        p->slot = i;
        p->security_type = -1;

        memset(&data, 0, sizeof(data));
        if (sceUtilityGetNetParam(i, PSP_NETPARAM_SSID, &data) == 0) {
            data.asString[sizeof(data.asString) - 1] = '\0';
            copy_string(p->ssid, sizeof(p->ssid), data.asString);
        }

        memset(&data, 0, sizeof(data));
        if (sceUtilityGetNetParam(i, PSP_NETPARAM_SECURE, &data) == 0)
            p->security_type = (int)data.asUint;

        count++;
    }

    s_profile_count = count;
    s_profile_valid = (s_profile > 0 && profile_slot_valid(s_profile));
    return count;
}

int wifi_profile_count(void)
{
    return s_profile_count;
}

const WifiProfile *wifi_profile_at(int pos)
{
    if (pos < 0 || pos >= s_profile_count)
        return NULL;
    return &s_profiles[pos];
}

int wifi_selected_profile(void)
{
    return s_profile_valid ? s_profile : 0;
}

int wifi_set_profile(int slot)
{
    if (slot < 1 || slot > WIFI_MAX_PROFILES)
        return WIFI_ERR_ARG;
    if (sceUtilityCheckNetParam(slot) != 0)
        return WIFI_ERR_ARG;

    s_profile = slot;
    s_profile_valid = 1;
    return WIFI_OK;
}

void wifi_refresh_link(void)
{
    refresh_link();
}

const WifiLinkInfo *wifi_link_info(void)
{
    return &s_link;
}

int wifi_strength_percent(void)
{
    return s_link.strength_percent;
}

int wifi_begin_connect(void)
{
    int ret;
    int state;

    if (!s_initialized)
        return WIFI_ERR_NOT_INIT;

    if (s_status == WIFI_STATUS_CONNECTED) {
        state = read_apctl_state();
        if (state == PSP_NET_APCTL_STATE_GOT_IP)
            return WIFI_OK;
        s_status = WIFI_STATUS_IDLE;
        s_phase = PHASE_NONE;
    }

    if (s_phase != PHASE_NONE)
        return WIFI_ERR_BUSY;

    state = read_apctl_state();
    if (state == PSP_NET_APCTL_STATE_GOT_IP) {
        s_ap_owned = 0;
        on_connected();
        return WIFI_OK;
    }
    if (state > PSP_NET_APCTL_STATE_DISCONNECTED)
        return WIFI_ERR_BUSY;

    if (!s_profile_valid || !profile_slot_valid(s_profile)) {
        int chk = sceUtilityCheckNetParam(s_profile);
        s_last_error = chk != 0 ? chk : WIFI_ERR_ARG;
        s_status = WIFI_STATUS_ERROR;
        return WIFI_ERR_ARG;
    }

    s_last_error = 0;
    s_apctl_event = -1;
    s_apctl_event_error = 0;

    ret = sceNetApctlConnect(s_profile);
    if (ret < 0) {
        s_last_error = ret;
        s_status = WIFI_STATUS_ERROR;
        return WIFI_ERR_CONNECT;
    }

    s_ap_owned = 1;
    s_phase = PHASE_CONNECTING;
    s_status = WIFI_STATUS_CONNECTING;
    s_deadline = now_us() + CONNECT_TIMEOUT_US;
    s_raw_state = -1;
    return WIFI_OK;
}

int wifi_begin_reconnect(void)
{
    int state;
    int ret;

    if (!s_initialized)
        return WIFI_ERR_NOT_INIT;
    if (s_phase != PHASE_NONE)
        return WIFI_ERR_BUSY;

    s_last_error = 0;
    s_apctl_event = -1;
    s_apctl_event_error = 0;
    s_reinit_tried = 0;

    state = read_apctl_state();
    s_raw_state = state;

    if (state > PSP_NET_APCTL_STATE_DISCONNECTED) {
        ret = sceNetApctlDisconnect();
        if (ret < 0)
            s_last_error = ret;
        s_phase = PHASE_DISCONNECTING;
        s_status = WIFI_STATUS_RECONNECTING;
        s_deadline = now_us() + DISCONNECT_TIMEOUT_US;
        return WIFI_OK;
    }

    s_phase = PHASE_SETTLE;
    s_status = WIFI_STATUS_RECONNECTING;
    s_settle_at = now_us() + SETTLE_US;
    return WIFI_OK;
}

WifiStatus wifi_poll(void)
{
    uint32_t now;
    int state = -1;
    int ret;

    if (!s_initialized)
        return s_status;

    if (take_apctl_event())
        return s_status;

    now = now_us();

    switch (s_phase) {
    case PHASE_CONNECTING:
        ret = sceNetApctlGetState(&state);
        if (ret < 0) {
            set_error(ret);
            break;
        }

        s_raw_state = state;
        if (state == PSP_NET_APCTL_STATE_GOT_IP) {
            on_connected();
        } else if (passed(now, s_deadline)) {
            s_abort_cause = s_last_error != 0 ? s_last_error : WIFI_ERR_TIMEOUT;
            ret = sceNetApctlDisconnect();
            if (ret < 0) {
                set_error(s_abort_cause);
                break;
            }
            s_phase = PHASE_ABORTING;
            s_status = WIFI_STATUS_DISCONNECTING;
            s_deadline = now + ABORT_TIMEOUT_US;
        }
        break;

    case PHASE_ABORTING:
        ret = sceNetApctlGetState(&state);
        if (ret >= 0)
            s_raw_state = state;
        if (ret < 0 || state <= PSP_NET_APCTL_STATE_DISCONNECTED ||
            passed(now, s_deadline)) {
            set_error(s_abort_cause != 0 ? s_abort_cause : WIFI_ERR_TIMEOUT);
        }
        break;

    case PHASE_DISCONNECTING:
        ret = sceNetApctlGetState(&state);
        if (ret < 0) {
            set_error(ret);
            break;
        }
        s_raw_state = state;
        if (state <= PSP_NET_APCTL_STATE_DISCONNECTED) {
            s_phase = PHASE_SETTLE;
            s_status = WIFI_STATUS_RECONNECTING;
            s_settle_at = now + SETTLE_US;
        } else if (passed(now, s_deadline)) {
            set_error(WIFI_ERR_TIMEOUT);
        }
        break;

    case PHASE_SETTLE:
        if (!passed(now, s_settle_at))
            break;
        if (!s_profile_valid || !profile_slot_valid(s_profile)) {
            set_error(WIFI_ERR_ARG);
            break;
        }
        ret = sceNetApctlConnect(s_profile);
        if (ret < 0) {
            if (!s_reinit_tried && s_apctl_owned) {
                s_reinit_tried = 1;
                if (s_apctl_owned) {
                    /* Term drops the handler list; re-register after Init. */
                    remove_apctl_handler();
                    sceNetApctlTerm();
                    s_apctl_owned = 0;
                    s_apctl_ready = 0;
                }
                ret = sceNetApctlInit(0x2000, 42);
                if (ret < 0 && ret != WIFI_APCTL_ALREADY) {
                    set_error(ret);
                    break;
                }
                s_apctl_owned = 1;
                s_apctl_ready = 1;
                install_apctl_handler(); /* diagnostics only */
                s_settle_at = now + REINIT_SETTLE_US;
                break;
            }
            set_error(ret);
            break;
        }
        s_ap_owned = 1;
        s_phase = PHASE_CONNECTING;
        s_status = WIFI_STATUS_RECONNECTING;
        s_deadline = now + RECONNECT_TIMEOUT_US;
        s_raw_state = -1;
        break;

    default:
        if (s_status == WIFI_STATUS_CONNECTED) {
            ret = sceNetApctlGetState(&state);
            if (ret >= 0) {
                uint32_t now_ms = now / 1000u;
                s_raw_state = state;
                if (state != PSP_NET_APCTL_STATE_GOT_IP) {
                    s_status = WIFI_STATUS_IDLE;
                    s_link.strength_percent = WIFI_STRENGTH_UNKNOWN;
                    s_rssi = 0;
                    s_joined_suspend_epoch = 0xFFFFFFFFu;
                } else {
                    if (wifi_health_due(now_ms, s_last_health_ms,
                                        WIFI_HEALTH_INTERVAL_MS)) {
                        WifiLinkVerdict v = wifi_link_verdict(
                            sceWlanGetSwitchState() != 0,
                            sceWlanDevIsPowerOn() != 0, state);
                        s_last_health_ms = now_ms;
                        if (v == WIFI_LINK_OK)
                            s_health_failures = 0;
                        else
                            s_health_failures++;
                        if (wifi_health_should_drop(v, s_health_failures)) {
                            s_status = WIFI_STATUS_IDLE;
                            s_link.strength_percent = WIFI_STRENGTH_UNKNOWN;
                            s_rssi = 0;
                            s_joined_suspend_epoch = 0xFFFFFFFFu;
                            break;
                        }
                    }
                    if ((uint32_t)(now_ms) - s_last_link_refresh_ms >=
                        LINK_REFRESH_MS) {
                        refresh_link();
                    }
                }
            }
        }
        break;
    }

    return s_status;
}

int wifi_connect(int timeout_ms)
{
    uint32_t start;
    int ret;

    if (s_status == WIFI_STATUS_CONNECTED)
        return WIFI_OK;

    if (timeout_ms <= 0)
        timeout_ms = WIFI_DEFAULT_TIMEOUT_MS;

    ret = wifi_begin_connect();
    if (ret != WIFI_OK)
        return ret;

    start = now_us();
    for (;;) {
        if (wifi_poll() == WIFI_STATUS_CONNECTED)
            return WIFI_OK;
        if (s_status == WIFI_STATUS_ERROR)
            return s_last_error != 0 ? s_last_error : WIFI_ERR_CONNECT;
        if (elapsed_ms(now_us(), start) >= timeout_ms)
            return WIFI_ERR_TIMEOUT;
        sceKernelDelayThread(50 * 1000);
    }
}

int wifi_reconnect(int timeout_ms)
{
    uint32_t start;
    int ret;

    if (timeout_ms <= 0)
        timeout_ms = WIFI_DEFAULT_TIMEOUT_MS;

    ret = wifi_begin_reconnect();
    if (ret != WIFI_OK)
        return ret;

    start = now_us();
    for (;;) {
        if (wifi_poll() == WIFI_STATUS_CONNECTED)
            return WIFI_OK;
        if (s_status == WIFI_STATUS_ERROR)
            return s_last_error != 0 ? s_last_error : WIFI_ERR_CONNECT;
        if (elapsed_ms(now_us(), start) >= timeout_ms)
            return WIFI_ERR_TIMEOUT;
        sceKernelDelayThread(50 * 1000);
    }
}

int wifi_disconnect(int timeout_ms)
{
    uint32_t start;
    int state = -1;
    int ret;

    if (!s_initialized)
        return WIFI_ERR_NOT_INIT;

    if (timeout_ms <= 0)
        timeout_ms = 5000;

    if (s_apctl_ready) {
        ret = sceNetApctlDisconnect();
        if (ret < 0)
            s_last_error = ret;

        start = now_us();
        while (elapsed_ms(now_us(), start) < timeout_ms) {
            if (sceNetApctlGetState(&state) >= 0 &&
                state <= PSP_NET_APCTL_STATE_DISCONNECTED)
                break;
            sceKernelDelayThread(10 * 1000);
        }
    } else {
        state = PSP_NET_APCTL_STATE_DISCONNECTED;
    }

    s_phase = PHASE_NONE;
    s_raw_state = state;
    clear_link();

    if (state > PSP_NET_APCTL_STATE_DISCONNECTED) {
        s_status = WIFI_STATUS_ERROR;
        return WIFI_ERR_TIMEOUT;
    }

    s_ap_owned = 0;
    s_status = WIFI_STATUS_IDLE;
    s_last_error = 0;
    return WIFI_OK;
}

void wifi_supervise(void)
{
    WifiSupInput in;
    WifiSupAction action;

    if (!s_auto_reconnect || !s_initialized || s_phase != PHASE_NONE)
        return;
    if (!s_profile_valid)
        return;

    in.now_ms = now_us() / 1000u;
    in.jitter_ms = now_us() % (WIFI_SUP_JITTER_MAX_MS + 1u);
    in.connected = (s_status == WIFI_STATUS_CONNECTED);
    in.in_flight = (s_phase != PHASE_NONE);
    in.switch_on = (sceWlanGetSwitchState() != 0);

    action = wifi_sup_step(&s_sup, &in);
    if (action == WIFI_SUP_RECONNECT) {
        if (wifi_begin_reconnect() != WIFI_OK)
            wifi_begin_connect();
    }
}

int wifi_resync(void)
{
    int state;

    if (!s_initialized || !s_apctl_ready)
        return WIFI_ERR_NOT_INIT;

    s_apctl_event = -1;
    s_apctl_event_error = 0;

    state = read_apctl_state();
    if (state < 0)
        return s_last_error != 0 ? s_last_error : WIFI_ERR_STATE;

    s_raw_state = state;

    if (state == PSP_NET_APCTL_STATE_GOT_IP) {
        on_connected();
        return WIFI_OK;
    }

    if (state == PSP_NET_APCTL_STATE_DISCONNECTED) {
        s_ap_owned = 0;
        s_phase = PHASE_NONE;
        s_status = WIFI_STATUS_IDLE;
        s_last_error = 0;
        clear_link();
        return WIFI_OK;
    }

    s_phase = PHASE_CONNECTING;
    s_status = WIFI_STATUS_CONNECTING;
    s_deadline = now_us() + RECONNECT_TIMEOUT_US;
    return WIFI_OK;
}

void wifi_take_link_ownership(void)
{
    int state;

    if (!s_initialized || !s_apctl_ready)
        return;

    state = read_apctl_state();
    if (state > PSP_NET_APCTL_STATE_DISCONNECTED &&
        state <= PSP_NET_APCTL_STATE_KEY_EXCHANGE) {
        s_ap_owned = 1;
    }
}

bool wifi_is_connected(void)
{
    return s_status == WIFI_STATUS_CONNECTED;
}

int wifi_link_ready(void)
{
    int state = -1;

    if (sceWlanGetSwitchState() == 0)
        return 0;
    if (sceWlanDevIsPowerOn() == 0)
        return 0;
    if (sceNetApctlGetState(&state) < 0)
        return 0;
    s_raw_state = state;
    return state == PSP_NET_APCTL_STATE_GOT_IP;
}

int wifi_link_trusted(void)
{
    return s_status == WIFI_STATUS_CONNECTED &&
           s_joined_suspend_epoch == (uint32_t)s_suspend_epoch;
}

int wifi_suspend_epoch(void)
{
    return s_suspend_epoch;
}

int wifi_resume_pending(void)
{
    return s_resume_pending;
}

int wifi_resume_revalidate(void)
{
    WifiLinkVerdict verdict;
    int state = -1;

    if (!s_initialized)
        return WIFI_ERR_NOT_INIT;
    if (s_phase != PHASE_NONE)
        return WIFI_ERR_BUSY;

    s_resume_pending = 0;
    refresh_link();
    if (sceNetApctlGetState(&state) >= 0)
        s_raw_state = state;

    verdict = wifi_link_verdict(sceWlanGetSwitchState() != 0,
                                sceWlanDevIsPowerOn() != 0, state);
    if (wifi_resume_needs_rejoin(s_joined_suspend_epoch,
                                 (uint32_t)s_suspend_epoch, verdict)) {
        s_joined_suspend_epoch = 0xFFFFFFFFu;
        return wifi_begin_reconnect();
    }
    s_joined_suspend_epoch = (uint32_t)s_suspend_epoch;
    return WIFI_OK;
}

int wifi_apctl_event_state(void)
{
    return s_last_apctl_event_state;
}

int wifi_apctl_event_seq(void)
{
    return s_last_apctl_event_seq;
}

WifiStatus wifi_status(void)
{
    return s_status;
}

int wifi_raw_state(void)
{
    return s_raw_state;
}

int wifi_last_error(void)
{
    return s_last_error;
}

const char *wifi_ip(void)
{
    return s_ip;
}

const char *wifi_ssid(void)
{
    return s_ssid;
}

int wifi_rssi(void)
{
    return s_rssi;
}

int wifi_wlan_switch(void)
{
    return sceWlanGetSwitchState() != 0 ? 1 : 0;
}

int wifi_apctl_event(void)
{
    return s_last_apctl_event;
}

int wifi_apctl_event_error(void)
{
    return s_last_apctl_event_error;
}

const char *wifi_status_name(WifiStatus status)
{
    switch (status) {
    case WIFI_STATUS_UNINITIALIZED: return "uninitialized";
    case WIFI_STATUS_IDLE:          return "idle";
    case WIFI_STATUS_CONNECTING:    return "connecting";
    case WIFI_STATUS_CONNECTED:     return "connected";
    case WIFI_STATUS_DISCONNECTING: return "disconnecting";
    case WIFI_STATUS_RECONNECTING:  return "reconnecting";
    case WIFI_STATUS_ERROR:         return "error";
    default:                        return "?";
    }
}

const char *wifi_state_name(int state)
{
    switch (state) {
    case PSP_NET_APCTL_STATE_DISCONNECTED: return "disconnected";
    case PSP_NET_APCTL_STATE_SCANNING:     return "scanning";
    case PSP_NET_APCTL_STATE_JOINING:      return "joining";
    case PSP_NET_APCTL_STATE_GETTING_IP:   return "getting IP";
    case PSP_NET_APCTL_STATE_GOT_IP:       return "got IP";
    case PSP_NET_APCTL_STATE_EAP_AUTH:     return "EAP auth";
    case PSP_NET_APCTL_STATE_KEY_EXCHANGE: return "key exchange";
    default:                               return "?";
    }
}

const char *wifi_event_name(int event)
{
    switch (event) {
    case PSP_NET_APCTL_EVENT_CONNECT_REQUEST:    return "connect request";
    case PSP_NET_APCTL_EVENT_SCAN_REQUEST:       return "scan request";
    case PSP_NET_APCTL_EVENT_SCAN_COMPLETE:      return "scan complete";
    case PSP_NET_APCTL_EVENT_ESTABLISHED:        return "established";
    case PSP_NET_APCTL_EVENT_GET_IP:             return "get IP";
    case PSP_NET_APCTL_EVENT_DISCONNECT_REQUEST: return "disconnect request";
    case PSP_NET_APCTL_EVENT_ERROR:              return "error";
    case PSP_NET_APCTL_EVENT_INFO:               return "info";
    case PSP_NET_APCTL_EVENT_EAP_AUTH:           return "EAP auth";
    case PSP_NET_APCTL_EVENT_KEY_EXCHANGE:       return "key exchange";
    case PSP_NET_APCTL_EVENT_RECONNECT:          return "reconnect";
    default:                                     return "?";
    }
}

const char *wifi_security_name(int type)
{
    switch (type) {
    case PSP_NET_APCTL_INFO_SECURITY_TYPE_NONE: return "none";
    case PSP_NET_APCTL_INFO_SECURITY_TYPE_WEP:  return "WEP";
    case PSP_NET_APCTL_INFO_SECURITY_TYPE_WPA:  return "WPA";
    case -1:                                    return "?";
    default:                                    return "other";
    }
}

const char *wifi_error_name(int error)
{
    switch (error) {
    case 0:                     return "ok";
    case WIFI_ERR_ARG:          return "bad argument";
    case WIFI_ERR_SWITCH_OFF:   return "WLAN switch off";
    case WIFI_ERR_NOT_INIT:     return "not initialized";
    case WIFI_ERR_BUSY:         return "busy";
    case WIFI_ERR_TIMEOUT:      return "timeout";
    case WIFI_ERR_STATE:        return "APCTL state error";
    case WIFI_ERR_MODULE:       return "net module load failed";
    case WIFI_ERR_CONNECT:      return "APCTL connect failed";
    default:                    return "firmware error";
    }
}
