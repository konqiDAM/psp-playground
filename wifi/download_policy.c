/*
 * download_policy.c - pure retry, progress and power policy.
 */
#include "download_policy.h"

#include <stddef.h>

uint32_t dl_backoff_ms(int idle_attempts)
{
    uint32_t delay = DL_BACKOFF_FIRST_MS;
    int i;

    if (idle_attempts < 1)
        idle_attempts = 1;
    for (i = 1; i < idle_attempts && delay < DL_BACKOFF_MAX_MS; i++)
        delay *= 2u;
    return delay < DL_BACKOFF_MAX_MS ? delay : DL_BACKOFF_MAX_MS;
}

int dl_error_retryable(DlError error)
{
    switch (error) {
    case DL_ERR_NET:
    case DL_ERR_DNS:
    case DL_ERR_TIMEOUT:
    case DL_ERR_STALL:
    case DL_ERR_CONNECTION_LOST:
        return 1;
    default:
        return 0;
    }
}

int dl_success_allows_registry(int stop)
{
    return stop == 0;
}

DlRetryAction dl_retry_step(DlRetryState *state, DlError error,
                            uint32_t bytes_this_attempt, uint32_t *wait_ms)
{
    if (!state)
        return DL_RETRY_STOP;
    if (wait_ms)
        *wait_ms = 0;

    switch (error) {
    case DL_OK:
        state->idle_attempts = 0;
        state->gave_up = 0;
        return DL_RETRY_CONTINUE;
    case DL_ERR_WIFI_OFF:
        if (wait_ms)
            *wait_ms = 1000;
        return DL_RETRY_WAIT;
    case DL_ERR_SUSPENDED:
        return DL_RETRY_REJOIN;
    case DL_ERR_REJOIN:
        state->idle_attempts++;
        break;
    case DL_ERR_CANCELLED:
        return DL_RETRY_STOP;
    default:
        if (!dl_error_retryable(error))
            return DL_RETRY_STOP;
        if (bytes_this_attempt > 0)
            state->idle_attempts = 0;
        else
            state->idle_attempts++;
        break;
    }

    if (state->idle_attempts >= DL_IDLE_ATTEMPTS_MAX) {
        state->gave_up = 1;
        return DL_RETRY_GIVE_UP;
    }
    if (wait_ms)
        *wait_ms = dl_backoff_ms(state->idle_attempts);
    return DL_RETRY_WAIT;
}

void dl_rate_reset(DlRate *rate, uint32_t now_ms)
{
    if (!rate)
        return;
    rate->total = 0;
    rate->rate = 0;
    rate->window_done = 0;
    rate->window_start_ms = now_ms;
    rate->has_rate = 0;
}

void dl_rate_update(DlRate *rate, uint32_t done, uint32_t now_ms)
{
    uint32_t elapsed;
    uint32_t sample;

    if (!rate)
        return;
    elapsed = now_ms - rate->window_start_ms;
    if (elapsed < 1000u)
        return;
    if (done < rate->window_done)
        return;
    sample = (uint32_t)(((uint64_t)(done - rate->window_done) * 1000u) /
                        elapsed);
    if (!rate->has_rate)
        rate->rate = sample;
    else
        rate->rate = (rate->rate * 3u + sample) / 4u;
    rate->has_rate = 1;
    rate->window_done = done;
    rate->window_start_ms = now_ms;
}

int dl_percent(uint32_t done, uint32_t total)
{
    if (total == 0)
        return 0;
    if (done >= total)
        return 100;
    return (int)(((uint64_t)done * 100u) / total);
}

uint32_t dl_eta_seconds(uint32_t done, uint32_t total, uint32_t rate)
{
    uint64_t remaining;

    if (total == 0 || done >= total || rate < 1024u)
        return 0;
    remaining = (uint64_t)(total - done);
    return (uint32_t)((remaining + rate - 1u) / rate);
}

int dl_clock_begin(DlClock *clock, int cpu, int bus)
{
    if (!clock || clock->boosted)
        return 0;
    clock->saved_cpu = cpu;
    clock->saved_bus = bus;
    clock->boosted = 1;
    clock->applied = (cpu > 0 && bus > 0);
    return clock->applied ? 1 : 0;
}

int dl_clock_end(DlClock *clock, int *cpu, int *bus)
{
    if (!clock || !clock->boosted)
        return 0;
    clock->boosted = 0;
    if (!clock->applied || clock->saved_cpu <= 0 || clock->saved_bus <= 0)
        return 0;
    if (cpu)
        *cpu = clock->saved_cpu;
    if (bus)
        *bus = clock->saved_bus;
    return 1;
}

int dl_backlight_begin(DlBacklight *b, int current_s, int target_s)
{
    if (!b)
        return 0;
    b->changed = 0;
    if (current_s <= 0 || current_s <= target_s)
        return 0;
    b->saved_seconds = current_s;
    b->changed = 1;
    return 1;
}

int dl_backlight_end(DlBacklight *b, int *restore_s)
{
    if (!b || !b->changed)
        return 0;
    if (restore_s)
        *restore_s = b->saved_seconds;
    b->changed = 0;
    return 1;
}

int dl_due(uint32_t now_ms, uint32_t last_ms, uint32_t interval_ms)
{
    return (int32_t)(now_ms - last_ms) >= (int32_t)interval_ms;
}

const char *dl_error_name(DlError error)
{
    switch (error) {
    case DL_OK:                  return "ok";
    case DL_ERR_NET:             return "network error";
    case DL_ERR_DNS:             return "dns error";
    case DL_ERR_TIMEOUT:         return "timeout";
    case DL_ERR_STALL:           return "stalled";
    case DL_ERR_CONNECTION_LOST: return "connection lost";
    case DL_ERR_REJOIN:          return "rejoin failed";
    case DL_ERR_WIFI_OFF:        return "Wi-Fi off";
    case DL_ERR_SUSPENDED:       return "suspended";
    case DL_ERR_CANCELLED:       return "cancelled";
    case DL_ERR_HTTP_STATUS:     return "http status";
    case DL_ERR_BAD_RESPONSE:    return "bad response";
    case DL_ERR_RANGE_REJECTED:  return "range rejected";
    case DL_ERR_CHECKSUM:        return "checksum mismatch";
    case DL_ERR_NO_SPACE:        return "not enough free space";
    case DL_ERR_STORAGE:         return "storage error";
    case DL_ERR_PATH:            return "bad path";
    case DL_ERR_SELF:            return "running app cannot update itself";
    case DL_ERR_ARG:             return "invalid argument";
    default:                     return "?";
    }
}

const char *dl_phase_name(DlPhase phase)
{
    switch (phase) {
    case DL_PHASE_IDLE:       return "idle";
    case DL_PHASE_CONNECTING: return "connecting";
    case DL_PHASE_RECEIVING:  return "downloading";
    case DL_PHASE_VERIFYING:  return "verifying";
    case DL_PHASE_INSTALLING: return "installing";
    case DL_PHASE_WAIT_WIFI:  return "waiting for Wi-Fi";
    case DL_PHASE_RETRY:      return "retrying";
    case DL_PHASE_DONE:       return "done";
    case DL_PHASE_FAILED:     return "failed";
    case DL_PHASE_CANCELLED:  return "cancelled";
    default:                  return "?";
    }
}
