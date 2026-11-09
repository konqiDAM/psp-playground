/*
 * Host tests for the portable Wi-Fi reconnect supervisor policy.
 */
#include <stdint.h>
#include <stdio.h>

#include "wifi_logic.h"

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static WifiSupInput input(uint32_t now, uint32_t jitter, int connected,
                          int in_flight, int switch_on)
{
    WifiSupInput in;
    in.now_ms = now;
    in.jitter_ms = jitter;
    in.connected = connected;
    in.in_flight = in_flight;
    in.switch_on = switch_on;
    return in;
}

static void test_reset(void)
{
    WifiSupervisor s;
    wifi_sup_reset(&s);
    CHECK(s.attempts == 0);
    CHECK(s.backoff_ms == WIFI_SUP_BACKOFF_MIN_MS);
    CHECK(!s.gave_up);
    CHECK(!s.started);
    CHECK(s.next_attempt_ms == 0);
}

static void test_first_step(void)
{
    WifiSupervisor s;
    WifiSupInput in = input(1000, 0, 0, 0, 1);
    wifi_sup_reset(&s);
    CHECK(wifi_sup_step(&s, &in) == WIFI_SUP_RECONNECT);
    CHECK(s.attempts == 1);
    CHECK(s.next_attempt_ms == 2000);
    CHECK(s.backoff_ms == 2000);
}

static void test_backoff_sequence(void)
{
    WifiSupervisor s;
    WifiSupInput in;
    uint32_t now = 1000;
    const uint32_t expected[8] = { 2000, 4000, 8000, 16000,
                                   30000, 30000, 30000, 30000 };
    int i;

    wifi_sup_reset(&s);
    for (i = 0; i < 8; i++) {
        in = input(now, 0, 0, 0, 1);
        CHECK(wifi_sup_step(&s, &in) == WIFI_SUP_RECONNECT);
        CHECK(s.backoff_ms == expected[i]);
        now = s.next_attempt_ms;
    }
    CHECK(s.attempts == 8);
}

static void test_attempt_cap(void)
{
    WifiSupervisor s;
    WifiSupInput in;
    uint32_t now = 1000;
    int i;

    wifi_sup_reset(&s);
    for (i = 0; i < WIFI_SUP_MAX_ATTEMPTS; i++) {
        in = input(now, 0, 0, 0, 1);
        CHECK(wifi_sup_step(&s, &in) == WIFI_SUP_RECONNECT);
        now = s.next_attempt_ms;
    }

    in = input(now, 0, 0, 0, 1);
    CHECK(wifi_sup_step(&s, &in) == WIFI_SUP_GAVE_UP);
    CHECK(s.attempts == WIFI_SUP_MAX_ATTEMPTS);
    CHECK(s.gave_up);

    in = input(now + 100000, 0, 0, 0, 1);
    CHECK(wifi_sup_step(&s, &in) == WIFI_SUP_GAVE_UP);
}

static void test_jitter(void)
{
    WifiSupervisor s;
    WifiSupInput in = input(5000, 999, 0, 0, 1);
    wifi_sup_reset(&s);
    CHECK(wifi_sup_step(&s, &in) == WIFI_SUP_RECONNECT);
    CHECK(s.next_attempt_ms == 5000 + 1000 + 999);
}

static void test_switch_off_does_not_burn_attempts(void)
{
    WifiSupervisor s;
    WifiSupInput in = input(5000, 0, 0, 0, 0);
    wifi_sup_reset(&s);
    CHECK(wifi_sup_step(&s, &in) == WIFI_SUP_WAIT);
    CHECK(s.attempts == 0);
    CHECK(s.next_attempt_ms == 5000 + WIFI_SUP_SWITCH_RECHECK_MS);
}

static void test_in_flight_waits(void)
{
    WifiSupervisor s;
    WifiSupInput in = input(1000, 0, 0, 1, 1);
    wifi_sup_reset(&s);
    CHECK(wifi_sup_step(&s, &in) == WIFI_SUP_WAIT);
    CHECK(s.attempts == 0);
    CHECK(!s.started);
}

static void test_connected_resets(void)
{
    WifiSupervisor s;
    WifiSupInput in = input(1000, 0, 0, 0, 1);
    wifi_sup_reset(&s);
    CHECK(wifi_sup_step(&s, &in) == WIFI_SUP_RECONNECT);

    in = input(2000, 0, 1, 0, 1);
    CHECK(wifi_sup_step(&s, &in) == WIFI_SUP_WAIT);
    CHECK(s.attempts == 0);
    CHECK(!s.started);
}

static void test_wrap_safe_deadline(void)
{
    WifiSupervisor s;
    WifiSupInput in = input(0xFFFFFF00u, 0, 0, 0, 1);
    wifi_sup_reset(&s);
    CHECK(wifi_sup_step(&s, &in) == WIFI_SUP_RECONNECT);
    CHECK(s.next_attempt_ms == 0x000002E8u);
    CHECK(wifi_sup_ms_until(&s, 0x00000100u) == 488);
}

static void test_ms_until(void)
{
    WifiSupervisor s;
    WifiSupInput in = input(1000, 0, 0, 0, 1);
    wifi_sup_reset(&s);
    CHECK(wifi_sup_ms_until(&s, 1000) == 0);
    CHECK(wifi_sup_step(&s, &in) == WIFI_SUP_RECONNECT);
    CHECK(wifi_sup_ms_until(&s, 1500) == 500);
    CHECK(wifi_sup_ms_until(&s, 2000) == 0);
}

static void test_link_verdict(void)
{
    CHECK(wifi_link_verdict(0, 1, 4) == WIFI_LINK_NO_SWITCH);
    CHECK(wifi_link_verdict(1, 0, 4) == WIFI_LINK_CHIP_OFF);
    CHECK(wifi_link_verdict(1, 1, 0) == WIFI_LINK_NOT_CONNECTED);
    CHECK(wifi_link_verdict(1, 1, 2) == WIFI_LINK_NOT_CONNECTED);
    CHECK(wifi_link_verdict(1, 1, 5) == WIFI_LINK_NOT_CONNECTED);
    CHECK(wifi_link_verdict(1, 1, 4) == WIFI_LINK_OK);
    CHECK(wifi_link_verdict(-1, -1, -1) == WIFI_LINK_UNKNOWN);
}

static void test_health_policy(void)
{
    CHECK(wifi_health_should_drop(WIFI_LINK_NO_SWITCH, 1));
    CHECK(wifi_health_should_drop(WIFI_LINK_CHIP_OFF, 1));
    CHECK(!wifi_health_should_drop(WIFI_LINK_NOT_CONNECTED, 1));
    CHECK(wifi_health_should_drop(WIFI_LINK_NOT_CONNECTED, 2));
    CHECK(!wifi_health_should_drop(WIFI_LINK_OK, 5));
    CHECK(wifi_health_due(5000, 0, 5000));
    CHECK(!wifi_health_due(4999, 0, 5000));
    CHECK(wifi_health_due(0x00000100u, 0xFFFFFF00u, 512u));
}

static void test_resume_rejoin(void)
{
    CHECK(!wifi_resume_needs_rejoin(3, 3, WIFI_LINK_OK));
    CHECK(wifi_resume_needs_rejoin(2, 3, WIFI_LINK_OK));
    CHECK(wifi_resume_needs_rejoin(3, 3, WIFI_LINK_NOT_CONNECTED));
    CHECK(wifi_resume_needs_rejoin(3, 3, WIFI_LINK_NO_SWITCH));
}

static void test_power_save_state(void)
{
    WifiPowerSaveState s;

    wifi_power_save_state_reset(&s);
    CHECK(wifi_power_save_state_snapshot(&s, 1) == 1);
    CHECK(s.saved_value == 1 && s.have_snapshot);
    CHECK(wifi_power_save_state_snapshot(&s, 0) == 0);
    CHECK(s.saved_value == 1);
    CHECK(wifi_power_save_state_restore_failed(&s) == 1);
    CHECK(wifi_power_save_state_restore_failed(&s) == 1);
    CHECK(wifi_power_save_state_restore_failed(&s) == 0);
    wifi_power_save_state_restore_ok(&s);
    CHECK(!s.have_snapshot);
    CHECK(s.restore_failures == 0);
}

static void test_power_callback_idempotent(void)
{
    WifiPowerCallback cb;

    cb.slot = -1;
    CHECK(wifi_power_cb_needs_register(&cb) == 1);
    wifi_power_cb_registered(&cb, 3);
    CHECK(cb.slot == 3);
    CHECK(wifi_power_cb_needs_register(&cb) == 0);
    wifi_power_cb_registered(&cb, 4);
    CHECK(cb.slot == 3);
    wifi_power_cb_unregistered(&cb);
    CHECK(cb.slot == -1);
    CHECK(wifi_power_cb_needs_register(&cb) == 1);
    wifi_power_cb_registered(&cb, -1);
    CHECK(cb.slot == -1);
}

int main(void)
{
    test_reset();
    test_first_step();
    test_backoff_sequence();
    test_attempt_cap();
    test_jitter();
    test_switch_off_does_not_burn_attempts();
    test_in_flight_waits();
    test_connected_resets();
    test_wrap_safe_deadline();
    test_ms_until();
    test_link_verdict();
    test_health_policy();
    test_resume_rejoin();
    test_power_save_state();
    test_power_callback_idempotent();

    if (failures) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    printf("all wifi supervisor tests passed\n");
    return 0;
}
