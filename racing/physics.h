/*
 * physics.h - portable car state and controls for Racing 3D.
 *
 * No PSP headers here on purpose: tests/test_racing.c builds this module on
 * the host with a normal C99 compiler.
 */

#ifndef RACING_PHYSICS_H
#define RACING_PHYSICS_H

#include <stdbool.h>

#include "road.h"

/* Longitudinal model (metres, seconds): unchanged from v0.1. */
#define CAR_MAX_SPEED 55.0f      /* 198 km/h */
#define CAR_MAX_REVERSE_SPEED 8.0f
#define CAR_ACCEL 12.0f
#define CAR_BRAKE_DECEL 30.0f
#define CAR_REVERSE_ACCEL 7.0f
#define CAR_COAST_DECEL 4.0f
#define CAR_AERO_DRAG 0.10f      /* 1/s, v -= v*drag*dt */

/* Steering and lateral grip. */
#define CAR_STEER_RATE 9.0f          /* input smoothing, 1/s */
#define CAR_STEER_YAW_RATE 1.45f     /* rad/s at full lock before scaling */
#define CAR_STEER_MIN_SPEED 1.5f     /* no yaw below this */
#define CAR_STEER_FULL_SPEED 12.0f
#define CAR_STEER_SPEED_SENSITIVITY 0.0075f
#define CAR_STEER_SPEED_SENSITIVITY2 0.00004f
#define CAR_LAT_ACCEL_MAX 24.0f      /* lateral grip cap, m/s^2 */
#define CAR_MAX_SLIP 0.45f           /* no-spin heading clamp, rad */
#define CAR_GRIP_WARN 1.05f          /* SLIP when demand exceeds grip by 5 % */
#define CAR_SLIP_HOLD 0.20f          /* SLIP latch, seconds */

/* Soft curve-follow assist: only while steering into the curve. */
#define CAR_ASSIST_GAIN 0.35f        /* fraction of the missing yaw */
#define CAR_ASSIST_MAX_YAW 0.10f     /* rad/s */
#define CAR_ASSIST_MIN_STEER 0.02f
#define CAR_ASSIST_FADE_STEER 0.35f
#define CAR_ASSIST_DEADBAND 0.005f   /* rad/s */

/* Off-road: 2 m shoulder, ~30 % speed cap, hard wall. */
#define CAR_OFFROAD_LATERAL ROAD_HALF_WIDTH
#define CAR_LATERAL_LIMIT (ROAD_EDGE_X - 0.4f)
#define CAR_EDGE_MARGIN 0.4f
#define CAR_OFFROAD_MAX_SPEED (CAR_MAX_SPEED * 0.30f)
#define CAR_OFFROAD_DECEL 20.0f
#define CAR_OFFROAD_DRAG 0.5f
#define CAR_GRAVITY 9.80665f
#define CAR_SLOPE_ACCEL 0.6f
#define CAR_LAUNCH_FACTOR 0.9f
#define CAR_AIR_STEER 0.05f /* residual authority in the air; no assist */
#define CAR_LAND_SCRUB 0.010f
#define CAR_LAND_SCRUB_MAX 0.12f
#define CAR_SUSP_GAIN 0.030f
#define CAR_SUSP_MAX 0.20f
#define CAR_SUSP_K 120.0f
#define CAR_SUSP_C 14.0f
#define CAR_BOOST_GRIP 0.20f
#define CAR_BOOST_YAW 0.10f

/* Guardrail contact: rail-only effects, never applied at the plain road edge. */
#define CAR_RAIL_MARGIN ROAD_RAIL_STOP /* wheel half-width; clamp = edge - it */
#define CAR_RAIL_SCRUB 0.15f           /* one-off speed loss on first contact */
#define CAR_RAIL_HOLD 0.35f            /* flash latch, seconds */

typedef struct {
    float s;        /* distance along the centreline */
    float lateral;  /* offset from the road centreline, + = right */
    float speed;    /* m/s along the car heading, + = forward */
    float steer;    /* smoothed steering, -1 = left, +1 = right */
    float slip;     /* car heading relative to road tangent, rad, + = right */
    float grip_use; /* max(steer demand, road demand) / CAR_LAT_ACCEL_MAX */
    float slip_hold; /* seconds of SLIP indication left, >= 0 */
    float yaw_rate;  /* rad/s applied to the velocity heading */
    float long_accel; /* m/s^2 from the longitudinal model, this step */
    float assist_yaw; /* rad/s contributed by the assist, 0 when inactive */
    float road_half_width;      /* last sample's asphalt half-width */
    float road_edge_half_width; /* last sample's drivable edge half-width */
    float air_height;           /* m above the road surface, >= 0 */
    float vy;                   /* m/s, + up */
    float air_time;             /* s since launch; 0 when grounded */
    float suspension;           /* m, negative = compressed */
    float susp_vel;             /* m/s */
    float pitch;                /* rad, + nose up */
    bool airborne;
    float rail_hold;            /* seconds of rail-contact flash left, >= 0 */
    signed char rail_side;      /* last rail contact: -1 left, +1 right, 0 none */
    bool rail_contact;          /* rail contact on the previous step */
} car_state_t;

typedef struct {
    bool throttle;
    float brake;   /* 0..1, multiplies CAR_BRAKE_DECEL */
    float steer;   /* -1..1 */
    float boost;   /* 0..1 brake-tap grip boost */
} car_input_t;

void car_reset(car_state_t *car);
void car_step(car_state_t *car, const car_input_t *input, float dt,
              const road_sample_t *road);

float car_kmh(const car_state_t *car);

bool car_is_offroad(const car_state_t *car);
bool car_is_slipping(const car_state_t *car);

/* Rail-contact indication: latched for CAR_RAIL_HOLD after a real rail hit. */
bool car_is_rail_hit(const car_state_t *car);
int car_rail_side(const car_state_t *car); /* -1 left, 0 none, +1 right */

#endif /* RACING_PHYSICS_H */
