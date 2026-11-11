/*
 * telemetry.h - portable Racing 3D G-meter, odometer and max speed.
 *
 * The values are model-computed (v*yaw for lateral, dv/dt for longitudinal);
 * the PSP has no accelerometer, so the HUD labels them COMPUTED.
 */

#ifndef RACING_TELEMETRY_H
#define RACING_TELEMETRY_H

#include "physics.h"

#define TELEMETRY_G 9.80665f
#define TELEMETRY_SMOOTH 12.0f  /* 1/s display smoothing */
#define TELEMETRY_RANGE_G 2.5f  /* full-scale dot radius */

typedef struct {
    float lat_g;      /* smoothed, signed: + = right turn */
    float lon_g;      /* smoothed, signed: + = BRAKING (dot up),
                       *                   - = accelerating (dot down).
                       * One sign for the dot, the LON readout and probes:
                       * lon_g = -long_accel / TELEMETRY_G, so reverse (a
                       * negative speed change) reads positive. */
    float peak_g;     /* max hypot(lat, lon) since reset: a magnitude */
    float distance_m; /* odometer, + = driven distance */
    float max_speed;  /* m/s, forward maximum since reset */
} telemetry_state_t;

void telemetry_reset(telemetry_state_t *t);
void telemetry_reset_peak(telemetry_state_t *t);
void telemetry_step(telemetry_state_t *t, const car_state_t *car, float dt);

#endif /* RACING_TELEMETRY_H */
