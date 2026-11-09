/*
 * memtest.c - portable memory-test engine.
 *
 * Safety: every access is bounded by result->bytes/4 and goes through the
 * caller-owned buffer. The engine never computes or touches an address
 * outside that block. All test accesses are volatile so they cannot be
 * optimized away; the caller's flush hook makes verify passes read DDR
 * instead of dirty cache lines.
 */

#include "memtest.h"

#include <string.h>

#define MT_CHUNK_WORDS 16384u /* progress granularity: 64 KiB */
#define MT_PATTERN_COUNT 4u
#define MT_WALK_WINDOW_WORDS 16384u /* 64 KiB sampled windows */
#define MT_WALK_WINDOWS 3u
#define MT_SPEED_PATTERN 0xA5A5A5A5u
#define MT_RANDOM_SEED 0x12345678u
#define MT_FNV_OFFSET 2166136261u
#define MT_FNV_PRIME 16777619u

static const char *const mt_phase_names[MEMTEST_PHASE_COUNT] = {
    "ADDRESS", "FORWARD", "REVERSE", "WALKING", "RANDOM", "SPEED"
};

const char *memtest_phase_name(int phase)
{
    if (phase < 0 || phase >= MEMTEST_PHASE_COUNT)
        return "?";
    return mt_phase_names[phase];
}

const char *memtest_status_name(int status)
{
    switch (status) {
    case MEMTEST_PENDING:
        return "PEND";
    case MEMTEST_RUNNING:
        return "RUN";
    case MEMTEST_PASS:
        return "PASS";
    case MEMTEST_FAIL:
        return "FAIL";
    case MEMTEST_ABORTED:
        return "STOP";
    default:
        return "?";
    }
}

void memtest_init(MemtestResult *result, uint32_t *buf, size_t bytes)
{
    int i;

    if (!result)
        return;

    memset(result, 0, sizeof(*result));
    result->buf = buf;
    result->bytes = bytes;
    result->first_phase = -1;

    for (i = 0; i < MEMTEST_PHASE_COUNT; i++)
        result->phases[i].status = MEMTEST_PENDING;
}

static int mt_report(const MemtestHooks *hooks, MemtestResult *result,
                     int phase, uint32_t done, uint32_t total)
{
    if (hooks->progress &&
        hooks->progress(hooks->ctx, result, phase, done, total)) {
        result->aborted = 1;
        return 1;
    }
    return 0;
}

static void mt_flush(const MemtestHooks *hooks, void *p, size_t bytes)
{
    if (hooks->flush)
        hooks->flush(p, bytes);
}

static void mt_inject(const MemtestHooks *hooks, int phase)
{
    if (hooks->fault_inject)
        hooks->fault_inject(hooks->ctx, phase);
}

static void mt_record_error(MemtestResult *result, int phase,
                            uint32_t word_index, uint32_t expected,
                            uint32_t actual)
{
    MemtestPhaseResult *p = &result->phases[phase];

    if (p->errors == 0u) {
        p->first_offset = word_index * 4u;
        p->expected = expected;
        p->actual = actual;
    }
    p->errors++;

    if (result->errors == 0u) {
        result->first_phase = phase;
        result->first_offset = word_index * 4u;
        result->first_expected = expected;
        result->first_actual = actual;
        result->first_address = (uint32_t)(uintptr_t)&result->buf[word_index];
    }
    result->errors++;
}

static void mt_complete_phase(MemtestResult *result, int phase)
{
    if (result->aborted) {
        result->phases[phase].status = MEMTEST_ABORTED;
    } else if (result->phases[phase].errors) {
        result->phases[phase].status = MEMTEST_FAIL;
    } else {
        result->phases[phase].status = MEMTEST_PASS;
    }
}

/* ---------------------------------------------------------------- phases */

/* Write the address of each word and verify it in reverse order. Catches
 * address-line and address-decoder faults. */
static void mt_phase_address(MemtestResult *result, const MemtestHooks *hooks)
{
    const uint32_t words = (uint32_t)(result->bytes / 4u);
    const uint32_t total = words * 2u;
    volatile uint32_t *v = (volatile uint32_t *)result->buf;
    uint32_t i;

    result->phases[MEMTEST_PHASE_ADDRESS].status = MEMTEST_RUNNING;
    if (mt_report(hooks, result, MEMTEST_PHASE_ADDRESS, 0u, total))
        return;

    for (i = 0; i < words; i++) {
        v[i] = (uint32_t)(uintptr_t)&result->buf[i];
        if ((i & (MT_CHUNK_WORDS - 1u)) == 0u &&
            mt_report(hooks, result, MEMTEST_PHASE_ADDRESS, i, total))
            return;
    }

    mt_flush(hooks, result->buf, result->bytes);
    mt_inject(hooks, MEMTEST_PHASE_ADDRESS);
    if (mt_report(hooks, result, MEMTEST_PHASE_ADDRESS, words, total))
        return;

    for (i = 0; i < words; i++) {
        uint32_t index = words - 1u - i;
        uint32_t expected = (uint32_t)(uintptr_t)&result->buf[index];
        uint32_t actual = v[index];

        if (actual != expected)
            mt_record_error(result, MEMTEST_PHASE_ADDRESS, index, expected,
                            actual);
        if ((i & (MT_CHUNK_WORDS - 1u)) == 0u &&
            mt_report(hooks, result, MEMTEST_PHASE_ADDRESS, words + i, total))
            return;
    }

    mt_report(hooks, result, MEMTEST_PHASE_ADDRESS, total, total);
}

/* Solid patterns 00/FF/55/AA, written and verified ascending or descending. */
static void mt_phase_patterns(MemtestResult *result, const MemtestHooks *hooks,
                              int phase, int reverse)
{
    static const uint32_t patterns[MT_PATTERN_COUNT] = {
        0x00000000u, 0xFFFFFFFFu, 0x55555555u, 0xAAAAAAAAu
    };
    const uint32_t words = (uint32_t)(result->bytes / 4u);
    const uint32_t total = words * MT_PATTERN_COUNT * 2u;
    volatile uint32_t *v = (volatile uint32_t *)result->buf;
    uint32_t done = 0u;
    uint32_t p;

    result->phases[phase].status = MEMTEST_RUNNING;
    if (mt_report(hooks, result, phase, 0u, total))
        return;

    for (p = 0; p < MT_PATTERN_COUNT; p++) {
        uint32_t pattern = patterns[p];
        uint32_t i;

        if (!reverse) {
            for (i = 0; i < words; i++) {
                v[i] = pattern;
                if ((i & (MT_CHUNK_WORDS - 1u)) == 0u &&
                    mt_report(hooks, result, phase, done + i, total))
                    return;
            }
        } else {
            for (i = 0; i < words; i++) {
                v[words - 1u - i] = pattern;
                if ((i & (MT_CHUNK_WORDS - 1u)) == 0u &&
                    mt_report(hooks, result, phase, done + i, total))
                    return;
            }
        }

        done += words;
        if (mt_report(hooks, result, phase, done, total))
            return;

        mt_flush(hooks, result->buf, result->bytes);
        if (p == 0u)
            mt_inject(hooks, phase);

        if (!reverse) {
            for (i = 0; i < words; i++) {
                uint32_t actual = v[i];

                if (actual != pattern)
                    mt_record_error(result, phase, i, pattern, actual);
                if ((i & (MT_CHUNK_WORDS - 1u)) == 0u &&
                    mt_report(hooks, result, phase, done + i, total))
                    return;
            }
        } else {
            for (i = 0; i < words; i++) {
                uint32_t index = words - 1u - i;
                uint32_t actual = v[index];

                if (actual != pattern)
                    mt_record_error(result, phase, index, pattern, actual);
                if ((i & (MT_CHUNK_WORDS - 1u)) == 0u &&
                    mt_report(hooks, result, phase, done + i, total))
                    return;
            }
        }

        done += words;
        if (mt_report(hooks, result, phase, done, total))
            return;
    }

    mt_report(hooks, result, phase, total, total);
}

/* Walking ones and walking zeros over three widely separated windows.
 * Full-memory walks would be O(64 * size); the other phases cover every
 * address, this one looks for bit coupling on representative cells. */
static void mt_phase_walking(MemtestResult *result, const MemtestHooks *hooks)
{
    const uint32_t words = (uint32_t)(result->bytes / 4u);
    uint32_t window_words = MT_WALK_WINDOW_WORDS;
    uint32_t starts[MT_WALK_WINDOWS];
    uint32_t window_count;
    uint32_t total;
    uint32_t done = 0u;
    uint32_t w;

    if (words < MT_WALK_WINDOWS * window_words)
        window_words = words / MT_WALK_WINDOWS;
    if (window_words == 0u)
        window_words = words;

    if (words >= MT_WALK_WINDOWS * window_words) {
        window_count = MT_WALK_WINDOWS;
        starts[0] = 0u;
        starts[1] = (words - window_words) / 2u;
        starts[2] = words - window_words;
    } else {
        window_count = 1u;
        starts[0] = 0u;
    }

    total = window_count * 32u * 2u * window_words * 2u;
    result->phases[MEMTEST_PHASE_WALKING].status = MEMTEST_RUNNING;
    if (mt_report(hooks, result, MEMTEST_PHASE_WALKING, 0u, total))
        return;

    for (w = 0; w < window_count; w++) {
        uint32_t *base = &result->buf[starts[w]];
        volatile uint32_t *v = (volatile uint32_t *)base;
        uint32_t bit;

        for (bit = 0; bit < 32u; bit++) {
            uint32_t ones = 1u << bit;
            uint32_t zeros = ~ones;
            uint32_t i;

            for (i = 0; i < window_words; i++) {
                v[i] = ones;
                if ((i & (MT_CHUNK_WORDS - 1u)) == 0u &&
                    mt_report(hooks, result, MEMTEST_PHASE_WALKING,
                              done + i, total))
                    return;
            }
            done += window_words;
            if (mt_report(hooks, result, MEMTEST_PHASE_WALKING, done, total))
                return;

            mt_flush(hooks, base, (size_t)window_words * 4u);
            if (w == 0u && bit == 0u)
                mt_inject(hooks, MEMTEST_PHASE_WALKING);

            for (i = 0; i < window_words; i++) {
                uint32_t actual = v[i];

                if (actual != ones)
                    mt_record_error(result, MEMTEST_PHASE_WALKING,
                                    starts[w] + i, ones, actual);
                if ((i & (MT_CHUNK_WORDS - 1u)) == 0u &&
                    mt_report(hooks, result, MEMTEST_PHASE_WALKING,
                              done + i, total))
                    return;
            }
            done += window_words;
            if (mt_report(hooks, result, MEMTEST_PHASE_WALKING, done, total))
                return;

            for (i = 0; i < window_words; i++) {
                v[i] = zeros;
                if ((i & (MT_CHUNK_WORDS - 1u)) == 0u &&
                    mt_report(hooks, result, MEMTEST_PHASE_WALKING,
                              done + i, total))
                    return;
            }
            done += window_words;
            if (mt_report(hooks, result, MEMTEST_PHASE_WALKING, done, total))
                return;

            mt_flush(hooks, base, (size_t)window_words * 4u);

            for (i = 0; i < window_words; i++) {
                uint32_t actual = v[i];

                if (actual != zeros)
                    mt_record_error(result, MEMTEST_PHASE_WALKING,
                                    starts[w] + i, zeros, actual);
                if ((i & (MT_CHUNK_WORDS - 1u)) == 0u &&
                    mt_report(hooks, result, MEMTEST_PHASE_WALKING,
                              done + i, total))
                    return;
            }
            done += window_words;
            if (mt_report(hooks, result, MEMTEST_PHASE_WALKING, done, total))
                return;
        }
    }

    mt_report(hooks, result, MEMTEST_PHASE_WALKING, total, total);
}

static uint32_t mt_rng(uint32_t *state)
{
    uint32_t x = *state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

/* Pseudo-random pattern with FNV-1a checksum over the generated words. */
static void mt_phase_random(MemtestResult *result, const MemtestHooks *hooks)
{
    const uint32_t words = (uint32_t)(result->bytes / 4u);
    const uint32_t total = words * 2u;
    volatile uint32_t *v = (volatile uint32_t *)result->buf;
    uint32_t seed = MT_RANDOM_SEED;
    uint32_t state = seed;
    uint32_t sum = MT_FNV_OFFSET;
    uint32_t i;

    result->phases[MEMTEST_PHASE_RANDOM].status = MEMTEST_RUNNING;
    if (mt_report(hooks, result, MEMTEST_PHASE_RANDOM, 0u, total))
        return;

    for (i = 0; i < words; i++) {
        uint32_t value = mt_rng(&state);

        v[i] = value;
        sum = (sum ^ value) * MT_FNV_PRIME;
        if ((i & (MT_CHUNK_WORDS - 1u)) == 0u &&
            mt_report(hooks, result, MEMTEST_PHASE_RANDOM, i, total))
            return;
    }
    result->checksum_expected = sum;

    mt_flush(hooks, result->buf, result->bytes);
    mt_inject(hooks, MEMTEST_PHASE_RANDOM);
    if (mt_report(hooks, result, MEMTEST_PHASE_RANDOM, words, total))
        return;

    state = seed;
    sum = MT_FNV_OFFSET;
    for (i = 0; i < words; i++) {
        uint32_t expected = mt_rng(&state);
        uint32_t actual = v[i];

        if (actual != expected)
            mt_record_error(result, MEMTEST_PHASE_RANDOM, i, expected, actual);
        sum = (sum ^ actual) * MT_FNV_PRIME;
        if ((i & (MT_CHUNK_WORDS - 1u)) == 0u &&
            mt_report(hooks, result, MEMTEST_PHASE_RANDOM, words + i, total))
            return;
    }
    result->checksum_actual = sum;

    mt_report(hooks, result, MEMTEST_PHASE_RANDOM, total, total);
}

static uint32_t mt_mbps10(size_t bytes, uint64_t us)
{
    uint64_t scaled;

    if (us == 0u)
        us = 1u;
    scaled = (uint64_t)bytes * 10000000ull / (1024ull * 1024ull);
    return (uint32_t)(scaled / us);
}

/* Timed fill and timed verify pass; no UI redraw during the timed loops. */
static void mt_phase_speed(MemtestResult *result, const MemtestHooks *hooks)
{
    const uint32_t words = (uint32_t)(result->bytes / 4u);
    const uint32_t total = words * 2u;
    volatile uint32_t *v = (volatile uint32_t *)result->buf;
    uint64_t t0;
    uint64_t t1;
    uint64_t write_us;
    uint64_t read_us;
    uint32_t i;

    result->phases[MEMTEST_PHASE_SPEED].status = MEMTEST_RUNNING;
    if (mt_report(hooks, result, MEMTEST_PHASE_SPEED, 0u, total))
        return;

    t0 = hooks->now_us(hooks->ctx);
    for (i = 0; i < words; i++)
        v[i] = MT_SPEED_PATTERN;
    t1 = hooks->now_us(hooks->ctx);
    write_us = (t1 > t0) ? (t1 - t0) : 1u;

    mt_flush(hooks, result->buf, result->bytes);
    mt_inject(hooks, MEMTEST_PHASE_SPEED);
    if (mt_report(hooks, result, MEMTEST_PHASE_SPEED, words, total))
        return;

    t0 = hooks->now_us(hooks->ctx);
    for (i = 0; i < words; i++) {
        uint32_t actual = v[i];

        if (actual != MT_SPEED_PATTERN)
            mt_record_error(result, MEMTEST_PHASE_SPEED, i, MT_SPEED_PATTERN,
                            actual);
    }
    t1 = hooks->now_us(hooks->ctx);
    read_us = (t1 > t0) ? (t1 - t0) : 1u;

    result->write_mbps10 = mt_mbps10(result->bytes, write_us);
    result->read_mbps10 = mt_mbps10(result->bytes, read_us);

    mt_report(hooks, result, MEMTEST_PHASE_SPEED, total, total);
}

/* ------------------------------------------------------------------ run */

int memtest_run(MemtestResult *result, const MemtestHooks *hooks)
{
    uint64_t start;

    if (!result || !hooks || !hooks->now_us || !result->buf)
        return -1;
    if ((uintptr_t)result->buf & 3u)
        return -1;
    if (result->bytes < MEMTEST_MIN_BYTES ||
        result->bytes > MEMTEST_MAX_BYTES)
        return -1;
    if (result->bytes & 3u)
        return -1;

    memtest_init(result, result->buf, result->bytes);
    start = hooks->now_us(hooks->ctx);

    mt_phase_address(result, hooks);
    mt_complete_phase(result, MEMTEST_PHASE_ADDRESS);

    if (!result->aborted) {
        mt_phase_patterns(result, hooks, MEMTEST_PHASE_FORWARD, 0);
        mt_complete_phase(result, MEMTEST_PHASE_FORWARD);
    }

    if (!result->aborted) {
        mt_phase_patterns(result, hooks, MEMTEST_PHASE_REVERSE, 1);
        mt_complete_phase(result, MEMTEST_PHASE_REVERSE);
    }

    if (!result->aborted) {
        mt_phase_walking(result, hooks);
        mt_complete_phase(result, MEMTEST_PHASE_WALKING);
    }

    if (!result->aborted) {
        mt_phase_random(result, hooks);
        mt_complete_phase(result, MEMTEST_PHASE_RANDOM);
    }

    if (!result->aborted) {
        mt_phase_speed(result, hooks);
        mt_complete_phase(result, MEMTEST_PHASE_SPEED);
    }

    result->elapsed_us = hooks->now_us(hooks->ctx) - start;
    result->pass = (!result->aborted && result->errors == 0u);
    return 0;
}
