/*
 * score.c - portable Racing 3D Time Attack clock and forward-distance score.
 *
 * The clock only exists during a run (never in Free Drive) and only starts on
 * a throttle frame, so idling on the welcome screen and sitting in the pause
 * menu cost nothing. FINISH latches: no more time and no more score.
 */

#include "score.h"

void score_reset(score_run_t *r, bool free_drive)
{
    if (!r)
        return;
    r->state = free_drive ? SCORE_STATE_FREE : SCORE_STATE_IDLE;
    r->elapsed_s = 0.0f;
    r->distance_m = 0.0f;
}

void score_step(score_run_t *r, bool throttle, float ds, float dt)
{
    if (!r)
        return;
    if (r->state == SCORE_STATE_FREE || r->state == SCORE_STATE_FINISHED)
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
        r->state = SCORE_STATE_FINISHED;
    }
}

float score_remaining(const score_run_t *r)
{
    if (!r || r->state == SCORE_STATE_FREE)
        return 0.0f;
    if (r->state == SCORE_STATE_IDLE)
        return SCORE_RUN_SECONDS;
    if (r->state == SCORE_STATE_FINISHED)
        return 0.0f;
    return SCORE_RUN_SECONDS - r->elapsed_s;
}

bool score_finished(const score_run_t *r)
{
    return r && r->state == SCORE_STATE_FINISHED;
}
