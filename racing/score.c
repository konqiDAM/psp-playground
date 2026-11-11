/*
 * score.c - portable Racing 3D Time Attack clock and forward-distance score.
 *
 * The clock only exists during a run (never in Free Drive) and only starts on
 * a throttle frame, so idling on the welcome screen and sitting in the pause
 * menu cost nothing. The clock latches into COASTING (no more time, no more
 * score) and the result card waits for the rest latch: RESULT.
 */

#include "score.h"

#include <math.h>

void score_reset(score_run_t *r, bool free_drive)
{
    if (!r)
        return;
    r->state = free_drive ? SCORE_STATE_FREE : SCORE_STATE_IDLE;
    r->elapsed_s = 0.0f;
    r->distance_m = 0.0f;
    r->rest_s = 0.0f;
}

void score_step(score_run_t *r, bool throttle, float ds, float dt)
{
    if (!r)
        return;
    if (r->state == SCORE_STATE_FREE || r->state == SCORE_STATE_COASTING ||
        r->state == SCORE_STATE_RESULT)
        return;
    if (dt <= 0.0f)
        return;

    if (r->state == SCORE_STATE_IDLE) {
        if (!throttle)
            return;
        r->state = SCORE_STATE_RUNNING;
    }

    if (ds > 0.0f)
        r->distance_m += ds;
    r->elapsed_s += dt;
    if (r->elapsed_s >= SCORE_RUN_SECONDS) {
        r->elapsed_s = SCORE_RUN_SECONDS;
        r->state = SCORE_STATE_COASTING;
    }
}

void score_coast_step(score_run_t *r, float speed, bool airborne, float dt)
{
    if (!r || r->state != SCORE_STATE_COASTING)
        return;
    if (dt <= 0.0f)
        return;

    if (airborne || fabsf(speed) > SCORE_REST_SPEED_MPS)
        r->rest_s = 0.0f;
    else
        r->rest_s += dt;

    if (r->rest_s >= SCORE_REST_HOLD_S)
        r->state = SCORE_STATE_RESULT;
}

bool score_brake_allowed(const score_run_t *r, float speed)
{
    if (!r || r->state != SCORE_STATE_COASTING)
        return true;
    /* Strictly forward: an overshoot below the threshold must not keep the
     * brake enabled, or physics.c would reverse the car away from the latch. */
    return speed > SCORE_REST_SPEED_MPS;
}

float score_remaining(const score_run_t *r)
{
    if (!r || r->state == SCORE_STATE_FREE)
        return 0.0f;
    if (r->state == SCORE_STATE_IDLE)
        return SCORE_RUN_SECONDS;
    if (r->state == SCORE_STATE_COASTING || r->state == SCORE_STATE_RESULT)
        return 0.0f;
    return SCORE_RUN_SECONDS - r->elapsed_s;
}

bool score_finished(const score_run_t *r)
{
    return r && (r->state == SCORE_STATE_COASTING ||
                 r->state == SCORE_STATE_RESULT);
}

bool score_result_ready(const score_run_t *r)
{
    return r && r->state == SCORE_STATE_RESULT;
}
