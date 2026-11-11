/*
 * physics.c - portable arcade car model for Racing 3D.
 *
 * The state is road-relative: s is the distance along the centreline,
 * lateral the offset from it, and slip the car heading relative to the road
 * tangent. The caller supplies the road curvature for the current step.
 */

#include "physics.h"

#include <math.h>

#define MAX_DT 0.05f

static float clampf(float value, float low, float high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

void car_reset(car_state_t *car)
{
    car->s = 0.0f;
    car->lateral = 0.0f;
    car->speed = 0.0f;
    car->steer = 0.0f;
    car->slip = 0.0f;
    car->grip_use = 0.0f;
    car->slip_hold = 0.0f;
    car->yaw_rate = 0.0f;
    car->long_accel = 0.0f;
    car->assist_yaw = 0.0f;
    car->road_half_width = ROAD_HALF_WIDTH;
    car->road_edge_half_width = ROAD_EDGE_X;
    car->air_height = 0.0f;
    car->vy = 0.0f;
    car->air_time = 0.0f;
    car->suspension = 0.0f;
    car->susp_vel = 0.0f;
    car->pitch = 0.0f;
    car->airborne = false;
    car->rail_hold = 0.0f;
    car->rail_side = 0;
    car->rail_contact = false;
    car->ds = 0.0f;
}

float car_kmh(const car_state_t *car)
{
    return fabsf(car->speed) * 3.6f;
}

void car_step(car_state_t *car, const car_input_t *input, float dt,
              const road_sample_t *road)
{
    float target_steer;
    float steer_delta;
    float max_delta;
    float speed;
    float speed_before;
    float v;
    float ramp;
    float scale;
    float yaw_want;
    float grip_yaw;
    float yaw;
    float yaw_base;
    float ds;
    float boost;
    float lat_max;

    if (dt < 0.0f)
        dt = 0.0f;
    if (dt > MAX_DT)
        dt = MAX_DT;

    car->road_half_width = road->half_width;
    car->road_edge_half_width = road->edge_half_width;
    boost = car->airborne ? 0.0f : clampf(input->boost, 0.0f, 1.0f);
    lat_max = CAR_LAT_ACCEL_MAX * (1.0f + CAR_BOOST_GRIP * boost);
    speed_before = car->speed;

    /* Smooth the steering input so digital D-pad presses are not instant. */
    target_steer = clampf(input->steer, -1.0f, 1.0f);
    steer_delta = target_steer - car->steer;
    max_delta = CAR_STEER_RATE * dt;
    if (steer_delta > max_delta)
        steer_delta = max_delta;
    else if (steer_delta < -max_delta)
        steer_delta = -max_delta;
    car->steer += steer_delta;

    /* Longitudinal: throttle and brake can act together; wheel forces
     * only while grounded, aero always. */
    speed = car->speed;
    if (!car->airborne) {
        float brake = clampf(input->brake, 0.0f, 1.0f);

        if (input->throttle)
            speed += CAR_ACCEL * dt;
        if (brake > 0.0f && (car->speed > 0.0f || !input->throttle)) {
            if (car->speed > 0.0f)
                speed -= CAR_BRAKE_DECEL * brake * dt;
            else
                speed -= CAR_REVERSE_ACCEL * brake * dt;
        }
        if (!input->throttle && brake <= 0.0f) {
            if (speed > 0.0f) {
                speed -= CAR_COAST_DECEL * dt;
                if (speed < 0.0f)
                    speed = 0.0f;
            } else if (speed < 0.0f) {
                speed += CAR_COAST_DECEL * dt;
                if (speed > 0.0f)
                    speed = 0.0f;
            }
            if (speed > -0.02f && speed < 0.02f)
                speed = 0.0f;
        }
        speed += -CAR_GRAVITY * road->grade * CAR_SLOPE_ACCEL * dt;
    }

    speed -= speed * CAR_AERO_DRAG * dt;
    speed = clampf(speed, -CAR_MAX_REVERSE_SPEED, CAR_MAX_SPEED);

    /*
     * Off-road: CAR_OFFROAD_MAX_SPEED is an upper bound, not a target.
     * Extra drag applies always; deceleration only while above the cap.
     */
    if (!car->airborne && fabsf(car->lateral) > road->half_width) {
        speed -= speed * CAR_OFFROAD_DRAG * dt;
        if (speed > CAR_OFFROAD_MAX_SPEED) {
            speed = fmaxf(CAR_OFFROAD_MAX_SPEED,
                          speed - CAR_OFFROAD_DECEL * dt);
        } else if (speed < -CAR_OFFROAD_MAX_SPEED) {
            speed = fminf(-CAR_OFFROAD_MAX_SPEED,
                          speed + CAR_OFFROAD_DECEL * dt);
        }
    }
    car->speed = speed;

    /* Speed-sensitive yaw request, capped by available lateral grip. */
    v = fabsf(speed);
    ramp = clampf((v - CAR_STEER_MIN_SPEED) /
                      (CAR_STEER_FULL_SPEED - CAR_STEER_MIN_SPEED),
                  0.0f, 1.0f);
    scale = ramp / (1.0f + v * CAR_STEER_SPEED_SENSITIVITY +
                    v * v * CAR_STEER_SPEED_SENSITIVITY2);
    yaw_want = car->steer * CAR_STEER_YAW_RATE * scale *
               (1.0f + CAR_BOOST_YAW * boost);
    if (car->airborne)
        yaw_want *= CAR_AIR_STEER;
    grip_yaw = lat_max / fmaxf(v, CAR_STEER_MIN_SPEED);
    if (car->airborne)
        grip_yaw *= CAR_AIR_STEER;
    yaw = clampf(yaw_want, -grip_yaw, grip_yaw);
    yaw_base = yaw;

    /*
     * Soft curve-follow assist: only while the player steers into the curve.
     * It can never act with steer == 0, never opposes the player's direction,
     * and the total yaw stays within the grip cap.
     */
    if (!car->airborne && speed > CAR_STEER_MIN_SPEED &&
        fabsf(car->steer) >= CAR_ASSIST_MIN_STEER) {
        float want = speed * road->curvature;
        float mis = want - yaw;

        if (fabsf(mis) > CAR_ASSIST_DEADBAND &&
            ((car->steer > 0.0f) == (mis > 0.0f))) {
            float gate = clampf(fabsf(car->steer) / CAR_ASSIST_FADE_STEER,
                                0.0f, 1.0f);
            float add = clampf(mis * CAR_ASSIST_GAIN, -CAR_ASSIST_MAX_YAW,
                               CAR_ASSIST_MAX_YAW) *
                        gate;

            yaw = clampf(yaw + add, -grip_yaw, grip_yaw);
        }
    }
    car->assist_yaw = car->airborne ? 0.0f : (yaw - yaw_base);
    car->yaw_rate = yaw;

    if (car->airborne) {
        car->grip_use = 0.0f;
        car->slip_hold = fmaxf(0.0f, car->slip_hold - dt);
    } else {
        car->grip_use =
            fmaxf(fabsf(yaw_want) * v, fabsf(v * v * road->curvature)) /
            lat_max;
        if (car->grip_use >= CAR_GRIP_WARN)
            car->slip_hold = CAR_SLIP_HOLD;
        else
            car->slip_hold = fmaxf(0.0f, car->slip_hold - dt);
    }

    car->slip += yaw * dt;
    ds = speed * cosf(car->slip) * dt;
    car->slip -= road->curvature * ds;
    car->slip = clampf(car->slip, -CAR_MAX_SLIP, CAR_MAX_SLIP);

    car->s += ds;
    car->ds = ds;
    {
        float limit_left = road_rail_limit(road, -1);
        float limit_right = road_rail_limit(road, 1);
        float next = car->lateral + speed * sinf(car->slip) * dt;
        int rail_hit = 0;

        /* Clamp every step exactly as before; only a real rail may react. */
        if (next > limit_right) {
            next = limit_right;
            if (road->rail_limit_right > 0.0f)
                rail_hit = 1;
        } else if (next < -limit_left) {
            next = -limit_left;
            if (road->rail_limit_left > 0.0f)
                rail_hit = -1;
        }
        if (rail_hit > 0 && car->slip > 0.0f)
            car->slip = 0.0f; /* yaw into the wall */
        if (rail_hit < 0 && car->slip < 0.0f)
            car->slip = 0.0f;
        car->lateral = next;
        if (rail_hit != 0) {
            if (!car->rail_contact)
                car->speed *= 1.0f - CAR_RAIL_SCRUB;
            car->rail_hold = CAR_RAIL_HOLD;
            car->rail_side = (signed char)rail_hit;
            car->yaw_rate = 0.0f;
        } else {
            car->rail_hold = fmaxf(0.0f, car->rail_hold - dt);
            if (car->rail_hold <= 0.0f)
                car->rail_side = 0;
        }
        car->rail_contact = rail_hit != 0;
    }
    car->long_accel =
        (dt > 1e-6f) ? (car->speed - speed_before) / dt : 0.0f;

    /* Vertical: launch on a sharp crest, gravity in the air, land/settle. */
    if (!car->airborne) {
        float vy = v * road->grade;

        if (road->grade_rate < 0.0f && v > 10.0f &&
            v * v * (-road->grade_rate) >
                CAR_GRAVITY * CAR_LAUNCH_FACTOR) {
            car->airborne = true;
            car->air_height = 0.0f;
            car->air_time = 0.0f;
            car->vy = vy;
        } else {
            car->air_height = 0.0f;
            car->vy = vy;
        }
    } else {
        float relative;

        car->air_time += dt;
        relative = car->vy - road->grade * v;
        car->vy -= CAR_GRAVITY * dt;
        car->air_height += (car->vy - road->grade * v) * dt;
        if (car->air_height <= 0.0f && relative < 0.0f &&
            car->air_time > 0.05f) {
            float impact = -(car->vy - road->grade * v);

            car->airborne = false;
            car->air_height = 0.0f;
            car->vy = v * road->grade;
            car->speed *= 1.0f -
                          clampf(impact * CAR_LAND_SCRUB, 0.0f,
                                 CAR_LAND_SCRUB_MAX);
            car->suspension = -clampf(impact * CAR_SUSP_GAIN, 0.0f,
                                      CAR_SUSP_MAX);
            car->susp_vel = 0.0f;
            car->air_time = 0.0f;
        }
    }

    car->susp_vel += (-CAR_SUSP_K * car->suspension -
                      CAR_SUSP_C * car->susp_vel) * dt;
    car->suspension += car->susp_vel * dt;
    car->pitch = car->airborne ? atan2f(car->vy, fabsf(car->speed))
                               : atanf(road->grade);
}

bool car_is_offroad(const car_state_t *car)
{
    return fabsf(car->lateral) > car->road_half_width;
}

bool car_is_slipping(const car_state_t *car)
{
    return car->slip_hold > 0.0f;
}

bool car_is_rail_hit(const car_state_t *car)
{
    return car->rail_hold > 0.0f;
}

int car_rail_side(const car_state_t *car)
{
    return (int)car->rail_side;
}
