/*
 * input.h - portable Racing 3D steering input shaping.
 *
 * D-pad is the default: progressive ramp, tap nudge and smooth auto-centre.
 * The analog nub is ignored in D-pad mode and, when the player opts in, is
 * mapped through a calibrated centre and deadzone.
 */

#ifndef RACING_INPUT_H
#define RACING_INPUT_H

#include <stdbool.h>

#include "settings.h"

#define INPUT_RAMP_NEAR 4.0f   /* 1/s while |command| < INPUT_RAMP_SPLIT */
#define INPUT_RAMP_FAR 2.2f    /* 1/s above the split */
#define INPUT_RAMP_SPLIT 0.50f
#define INPUT_CENTER_RATE 4.5f /* 1/s, release auto-centre */
#define INPUT_TAP_MAX 0.12f    /* s; shorter press = tap */
#define INPUT_TAP_NUDGE 0.30f  /* command magnitude after a tap */
#define INPUT_BRAKE_TAP_MAX 0.20f
#define INPUT_BOOST_TIME 0.25f
#define INPUT_BOOST_COOLDOWN 0.40f
#define INPUT_BRAKE_LIGHT 0.25f

typedef struct {
    bool left;
    bool right;
    int nub_lx; /* raw 0..255; ignored in D-pad mode */
    bool brake;
} racing_raw_input_t;

typedef struct {
    float steer;   /* shaped command, -1..1 */
    float command; /* internal command (same as steer after clamping) */
    int dir;       /* -1/0/+1 D-pad direction */
    float held;    /* seconds the current direction has been held */
    bool brake_prev;
    bool tap_armed;
    float brake_hold;
    float brake_level;    /* 0..1 output */
    float boost_timer;    /* seconds of boost left */
    float boost_cooldown; /* seconds until a new tap can boost */
} input_shaper_t;

void input_reset(input_shaper_t *shaper);

float input_step(input_shaper_t *shaper, const racing_raw_input_t *raw,
                 const racing_settings_t *settings, float dt);

/* Convert a raw Lx sample through a captured centre and deadzone. */
float input_analog_to_steer(int raw, int centre, int deadzone);

/*
 * Zero every steering/brake source when the run's controls are dead (the
 * Time Attack clock has finished). The nub is parked on the calibrated centre
 * so ANALOG mode maps to a zero steer instead of keeping the car turning.
 */
void input_gate_raw(racing_raw_input_t *raw, bool enabled, int centre);

#endif /* RACING_INPUT_H */
