/*
 * memtest.h - portable memory-test engine for the Memory Test PSP app.
 *
 * The engine only ever touches the buffer it is given. Hardware-specific
 * operations (D-cache flush, clock, progress rendering) are injected through
 * MemtestHooks so the exact same code runs on the PSP and in host tests.
 */

#ifndef MEMTEST_ENGINE_H
#define MEMTEST_ENGINE_H

#include <stddef.h>
#include <stdint.h>

#define MEMTEST_PHASE_COUNT 6
#define MEMTEST_MIN_BYTES (64u * 1024u)
#define MEMTEST_MAX_BYTES (512u * 1024u * 1024u)

typedef enum {
    MEMTEST_PHASE_ADDRESS = 0,
    MEMTEST_PHASE_FORWARD,
    MEMTEST_PHASE_REVERSE,
    MEMTEST_PHASE_WALKING,
    MEMTEST_PHASE_RANDOM,
    MEMTEST_PHASE_SPEED
} MemtestPhase;

typedef enum {
    MEMTEST_PENDING = 0,
    MEMTEST_RUNNING,
    MEMTEST_PASS,
    MEMTEST_FAIL,
    MEMTEST_ABORTED
} MemtestStatus;

typedef struct {
    int status;
    uint32_t errors;
    uint32_t first_offset; /* byte offset inside the test buffer */
    uint32_t expected;
    uint32_t actual;
} MemtestPhaseResult;

typedef struct {
    uint32_t *buf;   /* buffer under test (owned by the caller) */
    size_t bytes;    /* its size in bytes */

    MemtestPhaseResult phases[MEMTEST_PHASE_COUNT];

    uint64_t errors; /* total error words across all phases */
    int first_phase;
    uint32_t first_offset;
    uint32_t first_expected;
    uint32_t first_actual;
    uint32_t first_address; /* absolute address of the first error */

    uint32_t checksum_expected;
    uint32_t checksum_actual;

    uint32_t write_mbps10; /* write throughput, tenths of MB/s */
    uint32_t read_mbps10;  /* read throughput, tenths of MB/s */

    uint64_t elapsed_us;
    int aborted;
    int pass;
} MemtestResult;

typedef struct {
    /* Write back and invalidate the D-cache for [p, p+bytes). Optional on
     * hosts; required on the PSP so verify passes really read DDR. */
    void (*flush)(void *p, size_t bytes);

    /* Monotonic clock in microseconds. Required. */
    uint64_t (*now_us)(void *ctx);

    /* Called every 16 384 words. Return non-zero to abort the run at the
     * next checkpoint. Optional (NULL = never abort, no UI updates). */
    int (*progress)(void *ctx, const MemtestResult *result, int phase,
                    uint32_t done, uint32_t total);

    /* Called after a write pass, before the verify pass. Host tests use it
     * to inject a known fault; shipping code leaves it NULL. */
    void (*fault_inject)(void *ctx, int phase);

    void *ctx;
} MemtestHooks;

/* Reset result and attach a buffer. Does not allocate or write the buffer. */
void memtest_init(MemtestResult *result, uint32_t *buf, size_t bytes);

/* Run all phases. Returns 0 when the run completed (with or without errors),
 * -1 for invalid arguments (NULL, unaligned, bad size). */
int memtest_run(MemtestResult *result, const MemtestHooks *hooks);

const char *memtest_phase_name(int phase);
const char *memtest_status_name(int status);

#endif /* MEMTEST_ENGINE_H */
