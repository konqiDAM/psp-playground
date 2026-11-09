/*
 * wifi_logic.c - portable PSP Wi-Fi reconnect supervisor policy.
 */
#include "wifi_logic.h"

#include <stddef.h>

void wifi_sup_reset(WifiSupervisor *s)
{
    if (!s)
        return;

    s->next_attempt_ms = 0;
    s->backoff_ms = WIFI_SUP_BACKOFF_MIN_MS;
    s->attempts = 0;
    s->gave_up = 0;
    s->started = 0;
}

static int deadline_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

WifiSupAction wifi_sup_step(WifiSupervisor *s, const WifiSupInput *in)
{
    uint32_t jitter;

    if (!s || !in)
        return WIFI_SUP_WAIT;

    if (in->connected) {
        wifi_sup_reset(s);
        return WIFI_SUP_WAIT;
    }

    if (in->in_flight)
        return WIFI_SUP_WAIT;

    if (!in->switch_on) {
        s->started = 1;
        s->next_attempt_ms = in->now_ms + WIFI_SUP_SWITCH_RECHECK_MS;
        return WIFI_SUP_WAIT;
    }

    if (s->gave_up)
        return WIFI_SUP_GAVE_UP;

    if (s->started && !deadline_reached(in->now_ms, s->next_attempt_ms))
        return WIFI_SUP_WAIT;

    if (s->attempts >= WIFI_SUP_MAX_ATTEMPTS) {
        s->gave_up = 1;
        return WIFI_SUP_GAVE_UP;
    }

    jitter = in->jitter_ms;
    if (jitter > WIFI_SUP_JITTER_MAX_MS)
        jitter = WIFI_SUP_JITTER_MAX_MS;

    s->attempts++;
    s->started = 1;
    s->next_attempt_ms = in->now_ms + s->backoff_ms + jitter;

    s->backoff_ms *= 2u;
    if (s->backoff_ms > WIFI_SUP_BACKOFF_MAX_MS)
        s->backoff_ms = WIFI_SUP_BACKOFF_MAX_MS;

    return WIFI_SUP_RECONNECT;
}

int wifi_sup_ms_until(const WifiSupervisor *s, uint32_t now_ms)
{
    int32_t remaining;

    if (!s || !s->started)
        return 0;

    remaining = (int32_t)(s->next_attempt_ms - now_ms);
    return remaining > 0 ? (int)remaining : 0;
}

WifiLinkVerdict wifi_link_verdict(int switch_on, int chip_on, int raw_state)
{
    if (switch_on < 0 || chip_on < 0 || raw_state < 0)
        return WIFI_LINK_UNKNOWN;
    if (!switch_on)
        return WIFI_LINK_NO_SWITCH;
    if (!chip_on)
        return WIFI_LINK_CHIP_OFF;
    if (raw_state == 4)
        return WIFI_LINK_OK;
    return WIFI_LINK_NOT_CONNECTED;
}

int wifi_health_should_drop(WifiLinkVerdict verdict, int consecutive_failures)
{
    if (verdict == WIFI_LINK_OK)
        return 0;
    if (verdict == WIFI_LINK_NO_SWITCH || verdict == WIFI_LINK_CHIP_OFF)
        return 1;
    return consecutive_failures >= 2;
}

int wifi_health_due(uint32_t now_ms, uint32_t last_ms, uint32_t interval_ms)
{
    return (int32_t)(now_ms - last_ms) >= (int32_t)interval_ms;
}

int wifi_resume_needs_rejoin(uint32_t joined_epoch, uint32_t current_epoch,
                             WifiLinkVerdict verdict)
{
    if (joined_epoch != current_epoch)
        return 1;
    return verdict != WIFI_LINK_OK;
}

void wifi_power_save_state_reset(WifiPowerSaveState *s)
{
    if (!s)
        return;
    s->saved_value = -1;
    s->have_snapshot = 0;
    s->restore_failures = 0;
}

int wifi_power_save_state_snapshot(WifiPowerSaveState *s, int current)
{
    if (!s || current < 0 || s->have_snapshot)
        return 0;
    s->saved_value = current;
    s->have_snapshot = 1;
    return 1;
}

void wifi_power_save_state_restore_ok(WifiPowerSaveState *s)
{
    if (!s)
        return;
    s->have_snapshot = 0;
    s->restore_failures = 0;
    s->saved_value = -1;
}

int wifi_power_save_state_restore_failed(WifiPowerSaveState *s)
{
    if (!s || !s->have_snapshot)
        return 0;
    s->restore_failures++;
    return s->restore_failures < 3;
}

int wifi_power_cb_needs_register(const WifiPowerCallback *s)
{
    return !s || s->slot < 0;
}

void wifi_power_cb_registered(WifiPowerCallback *s, int slot)
{
    if (!s || slot < 0)
        return;
    if (s->slot < 0)
        s->slot = slot;
}

void wifi_power_cb_unregistered(WifiPowerCallback *s)
{
    if (s)
        s->slot = -1;
}
