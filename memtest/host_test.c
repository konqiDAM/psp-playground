/*
 * host_test.c - host unit tests for the portable memory-test engine.
 *
 * Build/run with ./scripts/test.sh. No PSP SDK required.
 */

#include "memtest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TEST_BYTES (8u * 1024u * 1024u)

typedef struct {
    uint32_t *buf;
    int armed;
    int injected;
    int injection_phase;
    uint32_t injection_index;
    int progress_calls;
    int last_phase;
    uint32_t last_done;
    uint32_t last_total;
} TestCtx;

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static void noop_flush(void *p, size_t bytes)
{
    (void)p;
    (void)bytes;
}

static uint64_t host_now_us(void *ctx)
{
    (void)ctx;
    return (uint64_t)clock() * 1000000ull / (uint64_t)CLOCKS_PER_SEC;
}

static int progress_cb(void *ctx, const MemtestResult *result, int phase,
                       uint32_t done, uint32_t total)
{
    TestCtx *t = (TestCtx *)ctx;

    (void)result;
    t->progress_calls++;
    t->last_phase = phase;
    t->last_done = done;
    t->last_total = total;
    return 0;
}

static void inject_cb(void *ctx, int phase)
{
    TestCtx *t = (TestCtx *)ctx;

    if (!t->armed || t->injected || phase != t->injection_phase)
        return;

    t->buf[t->injection_index] ^= 0x00000001u;
    t->injected = 1;
}

static int abort_progress_cb(void *ctx, const MemtestResult *result, int phase,
                             uint32_t done, uint32_t total)
{
    int *calls = (int *)ctx;

    (void)result;
    (void)phase;
    (void)done;
    (void)total;
    (*calls)++;
    return (*calls >= 2) ? 1 : 0;
}

static uint32_t *align64(void *raw)
{
    return (uint32_t *)(((uintptr_t)raw + 63u) & ~(uintptr_t)63u);
}

static void test_clean_run(void)
{
    void *raw = malloc(TEST_BYTES + 64u);
    uint32_t *buf;
    MemtestResult result;
    MemtestHooks hooks;
    TestCtx ctx;
    int rc;
    int i;

    CHECK(raw != NULL);
    if (!raw)
        return;
    buf = align64(raw);

    memset(buf, 0xA5, TEST_BYTES);
    memset(&ctx, 0, sizeof(ctx));
    hooks.flush = noop_flush;
    hooks.now_us = host_now_us;
    hooks.progress = progress_cb;
    hooks.fault_inject = NULL;
    hooks.ctx = &ctx;

    memtest_init(&result, buf, TEST_BYTES);
    rc = memtest_run(&result, &hooks);

    CHECK(rc == 0);
    CHECK(result.pass == 1);
    CHECK(result.errors == 0u);
    CHECK(result.aborted == 0);
    CHECK(result.elapsed_us > 0u);
    CHECK(result.write_mbps10 > 0u);
    CHECK(result.read_mbps10 > 0u);
    CHECK(result.checksum_expected == result.checksum_actual);
    CHECK(result.first_phase == -1);
    CHECK(ctx.progress_calls > 0);
    CHECK(ctx.last_phase == MEMTEST_PHASE_SPEED);
    CHECK(ctx.last_done == ctx.last_total);

    for (i = 0; i < MEMTEST_PHASE_COUNT; i++) {
        CHECK(result.phases[i].status == MEMTEST_PASS);
        CHECK(result.phases[i].errors == 0u);
    }

    free(raw);
}

/* Inject one wrong bit after the write pass of `phase` and require that the
 * phase notices it, keeps going, and leaves every other phase clean. */
static void test_fault_case(int phase)
{
    const uint32_t index = 1234u;
    void *raw = malloc(TEST_BYTES + 64u);
    uint32_t *buf;
    MemtestResult result;
    MemtestHooks hooks;
    TestCtx ctx;
    int rc;
    int i;

    CHECK(raw != NULL);
    if (!raw)
        return;
    buf = align64(raw);

    memset(buf, 0x5A, TEST_BYTES);
    memset(&ctx, 0, sizeof(ctx));
    ctx.buf = buf;
    ctx.armed = 1;
    ctx.injection_phase = phase;
    ctx.injection_index = index;

    hooks.flush = noop_flush;
    hooks.now_us = host_now_us;
    hooks.progress = progress_cb;
    hooks.fault_inject = inject_cb;
    hooks.ctx = &ctx;

    memtest_init(&result, buf, TEST_BYTES);
    rc = memtest_run(&result, &hooks);

    CHECK(rc == 0);
    CHECK(ctx.injected == 1);
    CHECK(result.pass == 0);
    CHECK(result.errors >= 1u);
    CHECK(result.first_phase == phase);
    CHECK(result.phases[phase].status == MEMTEST_FAIL);
    CHECK(result.phases[phase].errors >= 1u);
    CHECK(result.first_offset == index * 4u);
    CHECK(result.first_address == (uint32_t)(uintptr_t)&buf[index]);
    CHECK(result.first_expected != result.first_actual);

    /* The run continues after a failing phase; nothing else is corrupted. */
    for (i = 0; i < MEMTEST_PHASE_COUNT; i++) {
        if (i != phase)
            CHECK(result.phases[i].status == MEMTEST_PASS);
    }

    free(raw);
}

static void test_fault_detection(void)
{
    int phase;

    for (phase = 0; phase < MEMTEST_PHASE_COUNT; phase++)
        test_fault_case(phase);
}

static void test_abort(void)
{
    void *raw = malloc(TEST_BYTES + 64u);
    uint32_t *buf;
    MemtestResult result;
    MemtestHooks hooks;
    int abort_ctx = 0;
    int rc;
    int i;

    CHECK(raw != NULL);
    if (!raw)
        return;
    buf = align64(raw);

    hooks.flush = noop_flush;
    hooks.now_us = host_now_us;
    hooks.progress = abort_progress_cb;
    hooks.fault_inject = NULL;
    hooks.ctx = &abort_ctx;

    memtest_init(&result, buf, TEST_BYTES);
    rc = memtest_run(&result, &hooks);

    CHECK(rc == 0);
    CHECK(result.aborted == 1);
    CHECK(result.pass == 0);
    CHECK(result.phases[MEMTEST_PHASE_ADDRESS].status == MEMTEST_ABORTED);
    for (i = 1; i < MEMTEST_PHASE_COUNT; i++)
        CHECK(result.phases[i].status == MEMTEST_PENDING);

    free(raw);
}

static void test_invalid_arguments(void)
{
    MemtestResult result;
    MemtestHooks hooks;
    uint32_t small[256];
    uint8_t *raw;
    uint32_t *misaligned;

    memset(&hooks, 0, sizeof(hooks));
    hooks.now_us = host_now_us;

    memtest_init(&result, small, sizeof(small));
    CHECK(memtest_run(&result, &hooks) == -1);

    raw = (uint8_t *)malloc(MEMTEST_MIN_BYTES + 8u);
    CHECK(raw != NULL);
    if (raw) {
        misaligned = (uint32_t *)(void *)(raw + 1);
        memtest_init(&result, misaligned, MEMTEST_MIN_BYTES);
        CHECK(memtest_run(&result, &hooks) == -1);
        free(raw);
    }

    memtest_init(&result, small, MEMTEST_MAX_BYTES + 4u);
    CHECK(memtest_run(&result, &hooks) == -1);
}

static void test_small_buffer(void)
{
    const size_t bytes = MEMTEST_MIN_BYTES;
    void *raw = malloc(bytes + 64u);
    uint32_t *buf;
    MemtestResult result;
    MemtestHooks hooks;
    int rc;

    CHECK(raw != NULL);
    if (!raw)
        return;
    buf = align64(raw);

    hooks.flush = noop_flush;
    hooks.now_us = host_now_us;
    hooks.progress = NULL;
    hooks.fault_inject = NULL;
    hooks.ctx = NULL;

    memtest_init(&result, buf, bytes);
    rc = memtest_run(&result, &hooks);
    CHECK(rc == 0);
    CHECK(result.pass == 1);
    CHECK(result.elapsed_us > 0u);

    free(raw);
}

int main(void)
{
    test_clean_run();
    test_fault_detection();
    test_abort();
    test_invalid_arguments();
    test_small_buffer();

    if (failures) {
        fprintf(stderr, "memtest host tests: %d failure(s)\n", failures);
        return 1;
    }

    printf("memtest host tests: OK (clean pass, per-phase fault detection, abort, invalid args, min buffer)\n");
    return 0;
}
