/*
 * input.c - portable steering input shaping for Racing 3D.
 *
 * The D-pad is the default. It ramps progressively while held, adds a small
 * nudge on a short tap, and auto-centres smoothly on release. The analog nub
 * is only read in the calibrated ANALOG mode.
 */

#include "input.h"

#include <math.h>

#define INPUT_MAX_DT 0.05f

static float clampf(float value, float low, float high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

static int clampi(int value, int low, int high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

void input_reset(input_shaper_t *shaper)
{
    shaper->steer = 0.0f;
    shaper->command = 0.0f;
    shaper->dir = 0;
    shaper->held = 0.0f;
    shaper->brake_prev = false;
    shaper->tap_armed = false;
    shaper->brake_hold = 0.0f;
    shaper->brake_level = 0.0f;
    shaper->boost_timer = 0.0f;
    shaper->boost_cooldown = 0.0f;
}

static void input_update_brake(input_shaper_t *shaper, bool brake, float dt)
{
    if (brake && !shaper->brake_prev) {
        shaper->brake_hold = dt;
        shaper->tap_armed = true;
        shaper->brake_level = INPUT_BRAKE_LIGHT;
        if (shaper->brake_hold >= INPUT_BRAKE_TAP_MAX) {
            shaper->tap_armed = false;
            shaper->brake_level = 1.0f;
        }
    } else if (brake) {
        shaper->brake_hold += dt;
        if (shaper->brake_hold >= INPUT_BRAKE_TAP_MAX) {
            shaper->tap_armed = false;
            shaper->brake_level = 1.0f;
        } else {
            shaper->brake_level = INPUT_BRAKE_LIGHT;
        }
    } else {
        if (shaper->brake_prev && shaper->tap_armed &&
            shaper->brake_hold < INPUT_BRAKE_TAP_MAX &&
            shaper->boost_cooldown <= 0.0f) {
            shaper->boost_timer = INPUT_BOOST_TIME;
            shaper->boost_cooldown = INPUT_BOOST_COOLDOWN;
        }
        shaper->tap_armed = false;
        shaper->brake_level =
            shaper->boost_timer > 0.0f ? INPUT_BRAKE_LIGHT : 0.0f;
    }
    shaper->brake_prev = brake;

    if (shaper->boost_timer > 0.0f) {
        shaper->boost_timer -= dt;
        if (shaper->boost_timer < 0.0f)
            shaper->boost_timer = 0.0f;
        shaper->brake_level = INPUT_BRAKE_LIGHT;
    }
    if (shaper->boost_cooldown > 0.0f) {
        shaper->boost_cooldown -= dt;
        if (shaper->boost_cooldown < 0.0f)
            shaper->boost_cooldown = 0.0f;
    }
}

float input_analog_to_steer(int raw, int centre, int deadzone)
{
    int delta;
    int dz = clampi(deadzone, 0, 120);
    int range = 127 - dz;

    raw = clampi(raw, 0, 255);
    centre = clampi(centre, 0, 255);
    if (range < 1)
        range = 1;

    delta = raw - centre;
    if (delta > dz)
        return clampf((float)(delta - dz) / (float)range, 0.0f, 1.0f);
    if (delta < -dz)
        return clampf((float)(delta + dz) / (float)range, -1.0f, 0.0f);
    return 0.0f;
}

float input_step(input_shaper_t *shaper, const racing_raw_input_t *raw,
                 const racing_settings_t *settings, float dt)
{
    float sensitivity = clampf(settings->sensitivity, 0.5f, 1.5f);
    int dir = 0;

    if (dt < 0.0f)
        dt = 0.0f;
    if (dt > INPUT_MAX_DT)
        dt = INPUT_MAX_DT;

    input_update_brake(shaper, raw->brake, dt);

    if (settings->steer_mode == RACING_STEER_ANALOG) {
        float analog = input_analog_to_steer(raw->nub_lx,
                                             settings->analog_centre,
                                             settings->analog_deadzone);

        shaper->dir = 0;
        shaper->held = 0.0f;
        shaper->command = clampf(analog * sensitivity, -1.0f, 1.0f);
        shaper->steer = shaper->command;
        return shaper->steer;
    }

    if (raw->right && !raw->left)
        dir = 1;
    else if (raw->left && !raw->right)
        dir = -1;

    if (dir != 0) {
        float rate;

        if (dir != shaper->dir) {
            shaper->dir = dir;
            shaper->held = 0.0f;
        }
        rate = (fabsf(shaper->command) < INPUT_RAMP_SPLIT ? INPUT_RAMP_NEAR
                                                         : INPUT_RAMP_FAR) *
               sensitivity;
        shaper->command += (float)dir * rate * dt;
        shaper->command = clampf(shaper->command, -1.0f, 1.0f);
        shaper->held += dt;
    } else {
        if (shaper->dir != 0) {
            if (shaper->held < INPUT_TAP_MAX &&
                fabsf(shaper->command) < INPUT_TAP_NUDGE)
                shaper->command = (float)shaper->dir * INPUT_TAP_NUDGE;
            shaper->dir = 0;
            shaper->held = 0.0f;
        }
        if (shaper->command > 0.0f) {
            shaper->command -= INPUT_CENTER_RATE * sensitivity * dt;
            if (shaper->command < 0.0f)
                shaper->command = 0.0f;
        } else if (shaper->command < 0.0f) {
            shaper->command += INPUT_CENTER_RATE * sensitivity * dt;
            if (shaper->command > 0.0f)
                shaper->command = 0.0f;
        }
    }

    shaper->steer = shaper->command;
    return shaper->steer;
}
