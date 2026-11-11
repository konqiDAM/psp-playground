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

/* 0.9.0: the clock latches into COASTING and the card waits for the car to be
 * at rest. 2 km/h for 0.3 s - see docs/racing-090-PLAN.md section 5.3. */
#define SCORE_REST_SPEED_MPS 0.556f /* 2 km/h */
#define SCORE_REST_HOLD_S 0.3f

typedef enum {
    SCORE_STATE_IDLE = 0, /* before the first throttle frame */
    SCORE_STATE_RUNNING,
    SCORE_STATE_COASTING, /* clock latched: throttle dead, score frozen */
    SCORE_STATE_RESULT,   /* at rest for SCORE_REST_HOLD_S: card may show */
    SCORE_STATE_FREE      /* Free Drive: the module is inert */
} score_state_t;

typedef struct {
    score_state_t state;
    float elapsed_s;  /* running time only; never exceeds SCORE_RUN_SECONDS */
    float distance_m; /* forward progress, monotonic */
    float rest_s;     /* seconds held at/below the rest speed, this coast */
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

/*
 * Advance the coast latch: called once per unpaused frame with the car's
 * forward speed and airborne flag. COASTING only: airborne resets the latch,
 * |speed| <= SCORE_REST_SPEED_MPS accumulates rest_s, and rest_s >=
 * SCORE_REST_HOLD_S sets RESULT. Never returns to RUNNING.
 */
void score_coast_step(score_run_t *r, float speed, bool airborne, float dt);

/*
 * May the brake act this frame? True everywhere except COASTING with a speed
 * at or below the rest threshold: physics.c turns SQUARE into reverse once
 * speed <= 0, so a brake held at rest would push the car backwards and the
 * latch could never fire (docs/racing-090-PLAN.md section 3.4).
 */
bool score_brake_allowed(const score_run_t *r, float speed);

/* 0 for Free Drive, SCORE_RUN_SECONDS while idle, else the time left. */
float score_remaining(const score_run_t *r);

bool score_finished(const score_run_t *r);      /* COASTING or RESULT */
bool score_result_ready(const score_run_t *r);  /* RESULT */

#endif /* RACING_SCORE_H */
