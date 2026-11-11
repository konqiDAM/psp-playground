/*
 * telemetry.c - portable computed G-meter, odometer and max speed.
 */

#include "telemetry.h"

#include <math.h>

static float clampf(float value, float low, float high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

void telemetry_reset(telemetry_state_t *t)
{
    t->lat_g = 0.0f;
    t->lon_g = 0.0f;
    t->peak_g = 0.0f;
    t->distance_m = 0.0f;
    t->max_speed = 0.0f;
}

void telemetry_reset_peak(telemetry_state_t *t)
{
    t->peak_g = 0.0f;
}

void telemetry_step(telemetry_state_t *t, const car_state_t *car, float dt)
{
    float k;
    float lat_target;
    float lon_target;
    float magnitude;

    if (dt <= 0.0f)
        return;

    k = clampf(dt * TELEMETRY_SMOOTH, 0.0f, 1.0f);
    lat_target = car->speed * car->yaw_rate / TELEMETRY_G;
    /* 0.9.0: accelerating reads NEGATIVE. The convention is -d(speed)/dt so
     * the HUD dot, the numeric LON readout and any probe share one sign:
     * accelerating -> dot down, braking -> dot up, and reverse (a negative
     * speed change) reads positive - a documented consequence. */
    lon_target = -car->long_accel / TELEMETRY_G;

    t->lat_g += (lat_target - t->lat_g) * k;
    t->lon_g += (lon_target - t->lon_g) * k;

    magnitude = hypotf(t->lat_g, t->lon_g);
    if (magnitude > t->peak_g)
        t->peak_g = magnitude;

    t->distance_m += fabsf(car->speed) * dt;
    if (car->speed > t->max_speed)
        t->max_speed = car->speed;
}
