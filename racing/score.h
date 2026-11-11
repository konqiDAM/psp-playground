/*
 * score.h - portable Racing 3D Time Attack clock and forward-distance score.
 *
 * No PSP headers here: the state machine is host-tested. main.c owns the run
 * and feeds the physics centreline increment (car_state_t.ds) plus the
 * effective throttle flag once per unpaused frame.
 */

#ifndef RACING_SCORE_H
#define RACING_SCORE_H

#include <stdbool.h>

/*
 * The release clock is 60 s. The only override is the test-only compile
 * definition used by scripts/check-racing-score.py; it is never set in a
 * published EBOOT (racing/CMakeLists.txt option RACING_DIAG_SHORT_TA).
 */
#ifndef RACING_DIAG_TA_SECONDS
#define RACING_DIAG_TA_SECONDS 60
#endif

#define SCORE_RUN_SECONDS ((float)RACING_DIAG_TA_SECONDS)

typedef enum {
    SCORE_STATE_IDLE = 0, /* before the first throttle frame */
    SCORE_STATE_RUNNING,
    SCORE_STATE_FINISHED,
    SCORE_STATE_FREE      /* Free Drive: the module is inert */
} score_state_t;

typedef struct {
    score_state_t state;
    float elapsed_s;  /* running time only; never exceeds SCORE_RUN_SECONDS */
    float distance_m; /* forward progress, monotonic */
} score_run_t;

/* Start a run: IDLE for Time Attack, FREE for Free Drive. */
void score_reset(score_run_t *r, bool free_drive);

/*
 * Advance one unpaused frame. `throttle` is the effective input (after the
 * suppress and finished gates), `ds` the physics centreline increment of the
 * frame, `dt` the frame delta already clamped by the caller. A zero-length
 * frame does nothing. Only a positive `ds` adds to the score.
 */
void score_step(score_run_t *r, bool throttle, float ds, float dt);

/* 0 for Free Drive, SCORE_RUN_SECONDS while idle, else the time left. */
float score_remaining(const score_run_t *r);

bool score_finished(const score_run_t *r);

#endif /* RACING_SCORE_H */
