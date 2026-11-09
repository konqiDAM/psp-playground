/*
 * Host tests for the download retry, progress and power policy.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "download_policy.h"

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static void test_backoff(void)
{
    CHECK(dl_backoff_ms(1) == 1000u);
    CHECK(dl_backoff_ms(2) == 2000u);
    CHECK(dl_backoff_ms(3) == 4000u);
    CHECK(dl_backoff_ms(4) == 8000u);
    CHECK(dl_backoff_ms(5) == 16000u);
    CHECK(dl_backoff_ms(6) == 16000u);
}

static void test_classify(void)
{
    CHECK(dl_error_retryable(DL_ERR_NET));
    CHECK(dl_error_retryable(DL_ERR_DNS));
    CHECK(dl_error_retryable(DL_ERR_TIMEOUT));
    CHECK(dl_error_retryable(DL_ERR_STALL));
    CHECK(dl_error_retryable(DL_ERR_CONNECTION_LOST));
    CHECK(!dl_error_retryable(DL_ERR_CHECKSUM));
    CHECK(!dl_error_retryable(DL_ERR_NO_SPACE));
    CHECK(!dl_error_retryable(DL_ERR_BAD_RESPONSE));
    CHECK(!dl_error_retryable(DL_ERR_RANGE_REJECTED));
    CHECK(!dl_error_retryable(DL_ERR_HTTP_STATUS));
    CHECK(!dl_error_retryable(DL_ERR_PATH));
    CHECK(!dl_error_retryable(DL_ERR_WIFI_OFF));
    CHECK(!dl_error_retryable(DL_ERR_SUSPENDED));
}

static void test_idle_cap(void)
{
    DlRetryState s;
    uint32_t wait = 0;
    const uint32_t expected[5] = { 1000, 2000, 4000, 8000, 16000 };
    int i;

    memset(&s, 0, sizeof(s));
    for (i = 0; i < 5; i++) {
        CHECK(dl_retry_step(&s, DL_ERR_NET, 0, &wait) == DL_RETRY_WAIT);
        CHECK(wait == expected[i]);
        CHECK(s.idle_attempts == i + 1);
    }
    CHECK(dl_retry_step(&s, DL_ERR_NET, 0, &wait) == DL_RETRY_GIVE_UP);
    CHECK(s.idle_attempts == 6);
}

static void test_bytes_reset(void)
{
    DlRetryState s;
    uint32_t wait = 0;

    memset(&s, 0, sizeof(s));
    s.idle_attempts = 3;
    CHECK(dl_retry_step(&s, DL_ERR_NET, 100, &wait) == DL_RETRY_WAIT);
    CHECK(s.idle_attempts == 0);
    CHECK(wait == 1000u);
    CHECK(dl_retry_step(&s, DL_ERR_NET, 0, &wait) == DL_RETRY_WAIT);
    CHECK(s.idle_attempts == 1);
    CHECK(wait == 1000u);
}

static void test_switch_off_no_attempts(void)
{
    DlRetryState s;
    uint32_t wait = 0;
    int i;

    memset(&s, 0, sizeof(s));
    for (i = 0; i < 100; i++) {
        CHECK(dl_retry_step(&s, DL_ERR_WIFI_OFF, 0, &wait) == DL_RETRY_WAIT);
        CHECK(s.idle_attempts == 0);
        CHECK(!s.gave_up);
    }
}

static void test_rejoin_counts(void)
{
    DlRetryState s;
    uint32_t wait = 0;

    memset(&s, 0, sizeof(s));
    s.idle_attempts = 0;
    CHECK(dl_retry_step(&s, DL_ERR_REJOIN, 500, &wait) == DL_RETRY_WAIT);
    CHECK(s.idle_attempts == 1);
}

static void test_rate(void)
{
    DlRate r;

    dl_rate_reset(&r, 0);
    dl_rate_update(&r, 102400, 2000);
    CHECK(r.rate == 51200u);
    dl_rate_update(&r, 307200, 4000);
    CHECK(r.rate == 64000u);
}

static void test_percent_eta(void)
{
    CHECK(dl_percent(0, 0) == 0);
    CHECK(dl_percent(0, 1000) == 0);
    CHECK(dl_percent(500, 1000) == 50);
    CHECK(dl_percent(1000, 1000) == 100);
    CHECK(dl_eta_seconds(500, 1000, 51200) == 1);
    CHECK(dl_eta_seconds(1000, 1000, 51200) == 0);
    CHECK(dl_eta_seconds(0, 1000, 0) == 0);
}

static void test_clock_policy(void)
{
    DlClock c;
    int cpu = 0, bus = 0;

    memset(&c, 0, sizeof(c));
    CHECK(dl_clock_begin(&c, 222, 111) == 1);
    CHECK(c.boosted && c.applied);
    CHECK(dl_clock_begin(&c, 100, 50) == 0);
    CHECK(c.saved_cpu == 222 && c.saved_bus == 111);
    CHECK(dl_clock_end(&c, &cpu, &bus) == 1);
    CHECK(cpu == 222 && bus == 111);
    CHECK(dl_clock_end(&c, &cpu, &bus) == 0);

    memset(&c, 0, sizeof(c));
    CHECK(dl_clock_begin(&c, 0, 0) == 0);
    CHECK(dl_clock_end(&c, &cpu, &bus) == 0);
}

static void test_backlight_policy(void)
{
    DlBacklight b;
    int restore = 0;

    memset(&b, 0, sizeof(b));
    CHECK(dl_backlight_begin(&b, -1, 60) == 0);
    CHECK(dl_backlight_end(&b, &restore) == 0);
    CHECK(dl_backlight_begin(&b, 0, 60) == 0);
    CHECK(dl_backlight_begin(&b, 30, 60) == 0);
    CHECK(dl_backlight_begin(&b, 60, 60) == 0);
    CHECK(dl_backlight_begin(&b, 120, 60) == 1);
    CHECK(dl_backlight_end(&b, &restore) == 1);
    CHECK(restore == 120);
    CHECK(dl_backlight_end(&b, &restore) == 0);
}

static void test_due(void)
{
    CHECK(dl_due(5000, 0, 5000));
    CHECK(!dl_due(4999, 0, 5000));
    CHECK(dl_due(1000, 0, 1000));
    CHECK(!dl_due(999, 0, 1000));
    CHECK(dl_due(0x00000100u, 0xFFFFFF00u, 512u));
    CHECK(!dl_due(0x000000FFu, 0xFFFFFF00u, 512u));
}

static void test_success_registry_gate(void)
{
    CHECK(dl_success_allows_registry(0) == 1);
    CHECK(dl_success_allows_registry(DL_ERR_SUSPENDED) == 0);
    CHECK(dl_success_allows_registry(DL_ERR_CANCELLED) == 0);
    CHECK(dl_success_allows_registry(DL_ERR_WIFI_OFF) == 0);
}

static void test_names(void)
{
    DlError e;
    DlPhase p;

    for (e = DL_OK; e <= DL_ERR_ARG; e = (DlError)(e + 1))
        CHECK(strcmp(dl_error_name(e), "?") != 0);
    for (p = DL_PHASE_IDLE; p <= DL_PHASE_CANCELLED; p = (DlPhase)(p + 1))
        CHECK(strcmp(dl_phase_name(p), "?") != 0);
}

int main(void)
{
    test_backoff();
    test_classify();
    test_idle_cap();
    test_bytes_reset();
    test_switch_off_no_attempts();
    test_rejoin_counts();
    test_rate();
    test_percent_eta();
    test_clock_policy();
    test_backlight_policy();
    test_due();
    test_success_registry_gate();
    test_names();

    if (failures) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    printf("all download policy tests passed\n");
    return 0;
}
