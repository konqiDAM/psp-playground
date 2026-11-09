/*
 * Host tests for the portable Racing 3D physics and road helpers.
 *
 * Runs with a normal C99 compiler; no PSP SDK needed.
 */

#include <math.h>
#include <stdio.h>

#include "physics.h"
#include "road.h"

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static int nearly(float a, float b, float epsilon)
{
    return fabsf(a - b) <= epsilon;
}

static float clampf(float value, float low, float high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

static road_sample_t flat_road(float curvature)
{
    road_sample_t r = {0};

    r.curvature = curvature;
    r.half_width = ROAD_HALF_WIDTH;   /* 5.0 */
    r.edge_half_width = ROAD_EDGE_X;  /* 7.0 */
    return r;
}

static void car_step_flat(car_state_t *car, const car_input_t *input,
                          float dt, float curvature)
{
    road_sample_t r = flat_road(curvature);

    car_step(car, input, dt, &r);
}

static void step_for_road(car_state_t *car, const car_input_t *input,
                          float seconds, const road_sample_t *road)
{
    float t = 0.0f;
    const float dt = 1.0f / 60.0f;

    while (t < seconds) {
        car_step(car, input, dt, road);
        t += dt;
    }
}

static void step_for(car_state_t *car, const car_input_t *input,
                     float seconds, float curvature)
{
    road_sample_t r = flat_road(curvature);

    step_for_road(car, input, seconds, &r);
}

/* Steering whose yaw request equals v/r for the current constants. */
static float steer_for(float speed, float radius)
{
    float ramp = clampf((speed - CAR_STEER_MIN_SPEED) /
                            (CAR_STEER_FULL_SPEED - CAR_STEER_MIN_SPEED),
                        0.0f, 1.0f);

    return (speed / radius) *
           (1.0f + speed * CAR_STEER_SPEED_SENSITIVITY +
            speed * speed * CAR_STEER_SPEED_SENSITIVITY2) /
           (CAR_STEER_YAW_RATE * ramp);
}

static float full_lock_yaw(float speed)
{
    float ramp = clampf((speed - CAR_STEER_MIN_SPEED) /
                            (CAR_STEER_FULL_SPEED - CAR_STEER_MIN_SPEED),
                        0.0f, 1.0f);

    return CAR_STEER_YAW_RATE * ramp /
           (1.0f + speed * CAR_STEER_SPEED_SENSITIVITY +
            speed * speed * CAR_STEER_SPEED_SENSITIVITY2);
}

/* Count narrow-section rising edges over a forward walk. The local
 * coordinates re-base; sampling around the returned car_s keeps the physical
 * position continuous. */
static int count_narrow_events_walk(const road_config_t *cfg, float span)
{
    road_config_t c = *cfg;
    float car_s = 0.0f;
    float travelled = 0.0f;
    int events = 0;
    int inside = 0;
    float s;

    road_reset_config(ROAD_SEED, &c);
    road_update(0.0f);
    for (s = 0.0f; s <= 100.0f && s <= span; s += 0.25f) {
        int narrow = road_sample(s).half_width <= ROAD_NARROW_WARN_HALF;

        if (narrow && !inside)
            events++;
        inside = narrow;
    }
    while (travelled + 100.0f < span) {
        float start;
        float end;

        car_s = road_update(car_s + 200.0f);
        travelled += 200.0f;
        start = car_s - 100.0f + 0.25f;
        end = car_s + 100.0f;
        if (travelled + 100.0f > span)
            end = car_s + (span - (travelled - 100.0f));
        for (s = start; s <= end; s += 0.25f) {
            int narrow = road_sample(s).half_width <= ROAD_NARROW_WARN_HALF;

            if (narrow && !inside)
                events++;
            inside = narrow;
        }
    }
    return events;
}

/* Accept a run only when it is fully inside [lo, hi]. */
static void accept_run(int start_ok, float run_end, int current, int hi,
                       int *runs, int *min_len, int *max_len)
{
    if (current > 0 && start_ok && run_end <= (float)hi) {
        (*runs)++;
        if (*min_len == 0 || current < *min_len)
            *min_len = current;
        if (current > *max_len)
            *max_len = current;
    }
}

/* Collect curve runs fully inside [lo, hi]; a run is consecutive curve
 * segments in road_segment_at() order. The trailing run is flushed too. */
static void collect_complete_runs(int lo, int hi, int *runs, int *min_len,
                                  int *max_len, int *alternating)
{
    int current = 0;
    int last_sign = 0;
    int start_index = -1;
    float run_end = 0.0f;
    int i;

    *runs = *min_len = *max_len = 0;
    *alternating = 1;
    for (i = 0; i < road_segment_count(); i++) {
        const road_segment_t *seg = road_segment_at(i);

        if (seg->curvature != 0.0f) {
            if (current == 0) {
                start_index = i;
                last_sign = seg->curvature > 0.0f ? 1 : -1;
            } else if ((seg->curvature > 0.0f ? 1 : -1) == last_sign) {
                *alternating = 0;
            } else {
                last_sign = seg->curvature > 0.0f ? 1 : -1;
            }
            current++;
            run_end = seg->start_s + seg->length;
        } else {
            accept_run(start_index >= 0 &&
                       road_segment_at(start_index)->start_s >= (float)lo,
                       run_end, current, hi, runs, min_len, max_len);
            current = 0;
            start_index = -1;
        }
    }
    accept_run(start_index >= 0 &&
               road_segment_at(start_index)->start_s >= (float)lo,
               run_end, current, hi, runs, min_len, max_len);
}

static car_input_t steer_only(float steer)
{
    car_input_t input = {false, false, steer, 0.0f};

    return input;
}

/* Steady-state cornering: pin the speed so only the lateral model acts. */
static void corner_for(car_state_t *car, const car_input_t *input,
                       float seconds, float curvature, float speed)
{
    float t = 0.0f;
    const float dt = 1.0f / 60.0f;

    while (t < seconds) {
        car->speed = speed;
        car_step_flat(car, input, dt, curvature);
        t += dt;
    }
}

static void test_initial_state(void)
{
    car_state_t car;

    car_reset(&car);
    CHECK(car.s == 0.0f);
    CHECK(car.lateral == 0.0f);
    CHECK(car.speed == 0.0f);
    CHECK(car.steer == 0.0f);
    CHECK(car.slip == 0.0f);
    CHECK(car.grip_use == 0.0f);
    CHECK(car.slip_hold == 0.0f);
    CHECK(car.yaw_rate == 0.0f);
    CHECK(car.long_accel == 0.0f);
    CHECK(car.assist_yaw == 0.0f);
    CHECK(nearly(car_kmh(&car), 0.0f, 0.001f));
}

static void test_throttle_accelerates(void)
{
    car_state_t car;
    car_input_t input = {true, false, 0.0f, 0.0f};

    car_reset(&car);
    step_for(&car, &input, 1.0f, 0.0f);
    CHECK(car.speed > 10.0f);
    CHECK(car.speed < CAR_MAX_SPEED);
    CHECK(car.s > 5.0f);
    CHECK(car_kmh(&car) > 36.0f);
}

static void test_speed_is_clamped(void)
{
    car_state_t car;
    car_input_t input = {true, false, 0.0f, 0.0f};

    car_reset(&car);
    step_for(&car, &input, 20.0f, 0.0f);
    CHECK(nearly(car.speed, CAR_MAX_SPEED, 0.01f));
    CHECK(nearly(car_kmh(&car), 198.0f, 0.1f));
}

static void test_brake_then_reverse(void)
{
    car_state_t car;
    car_input_t brake = {false, true, 0.0f, 0.0f};
    car_input_t coast = {false, false, 0.0f, 0.0f};

    car_reset(&car);
    car.speed = 30.0f;
    step_for(&car, &brake, 2.0f, 0.0f);
    CHECK(car.speed < 0.0f); /* braked through zero into reverse */
    CHECK(car.speed >= -CAR_MAX_REVERSE_SPEED);

    step_for(&car, &brake, 5.0f, 0.0f);
    CHECK(nearly(car.speed, -CAR_MAX_REVERSE_SPEED, 0.01f));

    car.speed = 0.5f;
    step_for(&car, &coast, 1.0f, 0.0f);
    CHECK(car.speed == 0.0f); /* coasting settles exactly at rest */
}

static void test_coasting_stops_quickly(void)
{
    car_state_t car;
    car_input_t coast = {false, false, 0.0f, 0.0f};

    car_reset(&car);
    car.speed = 10.0f;
    step_for(&car, &coast, 5.0f, 0.0f);
    CHECK(car.speed == 0.0f);
}

static void test_no_steering_at_standstill(void)
{
    car_state_t car;
    car_input_t input = {false, false, -1.0f, 0.0f};

    car_reset(&car);
    step_for(&car, &input, 2.0f, 0.0f);
    CHECK(car.lateral == 0.0f);
    CHECK(car.steer < -0.99f); /* the input itself is still smoothed in */
}

static void test_steering_moves_and_clamps(void)
{
    car_state_t car;
    car_input_t right = {true, false, 1.0f, 0.0f};
    car_input_t left = {true, false, -1.0f, 0.0f};

    car_reset(&car);
    car.speed = 30.0f;
    step_for(&car, &right, 1.0f, 0.0f);
    CHECK(car.lateral > 0.0f);
    CHECK(car.lateral <= CAR_LATERAL_LIMIT);

    step_for(&car, &right, 8.0f, 0.0f);
    CHECK(nearly(car.lateral, CAR_LATERAL_LIMIT, 0.0001f));

    step_for(&car, &left, 8.0f, 0.0f);
    CHECK(nearly(car.lateral, -CAR_LATERAL_LIMIT, 0.0001f));
}

static void test_reverse_inverts_steering(void)
{
    car_state_t car;
    car_input_t input = {false, false, -1.0f, 0.0f};

    car_reset(&car);
    car.speed = -5.0f;
    step_for(&car, &input, 0.1f, 0.0f);
    CHECK(car.lateral > 1e-3f); /* reversing with left lock moves right */
}

static void test_dt_is_clamped(void)
{
    car_state_t car;
    car_input_t input = {true, false, 0.0f, 0.0f};

    car_reset(&car);
    car_step_flat(&car, &input, 10.0f, 0.0f);
    CHECK(car.s <= CAR_MAX_SPEED * 0.05f + 0.001f);
    CHECK(car.speed <= CAR_ACCEL * 0.05f + 0.001f);
}

static void test_steering_is_speed_sensitive(void)
{
    car_state_t fast;
    car_state_t slow;
    car_input_t full = {true, false, 1.0f, 0.0f};

    car_reset(&fast);
    fast.speed = 55.0f;
    car_reset(&slow);
    slow.speed = 10.0f;

    step_for(&fast, &full, 0.25f, 0.0f);
    step_for(&slow, &full, 0.25f, 0.0f);

    CHECK(fabsf(fast.slip) < fabsf(slow.slip));
}

static void test_tight_curve_runs_wide_at_speed(void)
{
    car_state_t car;
    car_input_t steer = {true, false, 0.5f, 0.0f};
    car_input_t slow_steer = {false, false, 0.41f, 0.0f};

    car_reset(&car);
    car.speed = 55.0f;
    step_for(&car, &steer, 1.0f, 1.0f / 40.0f);
    CHECK(car.lateral < -1.0f);
    CHECK(car.grip_use > 1.0f);
    CHECK(car_is_slipping(&car));

    car_reset(&car);
    car.speed = 15.0f;
    step_for(&car, &slow_steer, 0.5f, 1.0f / 40.0f);
    CHECK(fabsf(car.lateral) < 0.5f);
    CHECK(!car_is_slipping(&car));
}

static void test_offroad_speed_cap(void)
{
    car_state_t car;
    car_input_t gas = {true, false, 0.0f, 0.0f};

    car_reset(&car);
    car.lateral = 5.5f;
    car.speed = 55.0f;
    step_for(&car, &gas, 3.0f, 0.0f);
    CHECK(car_is_offroad(&car));
    CHECK(car.speed >= 15.5f);
    CHECK(car.speed <= CAR_OFFROAD_MAX_SPEED + 0.1f);

    step_for(&car, &gas, 2.0f, 0.0f);
    CHECK(car.speed >= 15.5f);
    CHECK(car.speed <= CAR_OFFROAD_MAX_SPEED + 0.1f);
}

static void test_offroad_cap_is_upper_bound(void)
{
    car_state_t car;
    car_input_t coast = {false, false, 0.0f, 0.0f};

    car_reset(&car);
    car.lateral = 5.5f;
    car.speed = 16.0f;
    step_for(&car, &coast, 0.5f, 0.0f);
    CHECK(car.speed < 16.0f);
    CHECK(car.speed > 5.0f);
}

static void test_offroad_can_return_to_road(void)
{
    car_state_t car;
    car_input_t input = {true, false, -1.0f, 0.0f};

    car_reset(&car);
    car.lateral = 6.5f;
    car.speed = 10.0f;
    step_for(&car, &input, 1.5f, 0.0f);
    CHECK(!car_is_offroad(&car));
    CHECK(car.lateral < 5.0f);
}

static void test_hard_lateral_limit(void)
{
    car_state_t car;
    car_input_t right = {false, false, 1.0f, 0.0f};

    car_reset(&car);
    car.lateral = CAR_LATERAL_LIMIT - 0.1f;
    car.speed = 10.0f;
    step_for(&car, &right, 1.0f, 0.0f);
    CHECK(nearly(car.lateral, CAR_LATERAL_LIMIT, 1e-4f));
}

static void test_no_spin_out(void)
{
    car_state_t car;
    car_input_t full = {true, false, 1.0f, 0.0f};

    car_reset(&car);
    car.speed = 20.0f;
    step_for(&car, &full, 10.0f, 0.0f);
    CHECK(fabsf(car.slip) <= CAR_MAX_SLIP + 1e-4f);
}

static void test_grip_demand_on_tight_curve(void)
{
    car_state_t car;
    car_input_t coast = {false, false, 0.0f, 0.0f};

    car_reset(&car);
    car.speed = 55.0f;
    car_step_flat(&car, &coast, 1.0f / 60.0f, 1.0f / 40.0f);
    CHECK(car.grip_use > 1.0f);
    CHECK(car_is_slipping(&car));
}

static void test_steady_corner_110_on_45m(void)
{
    car_state_t car;
    car_input_t input = steer_only(steer_for(30.6f, 45.0f));

    car_reset(&car);
    car.speed = 30.6f;
    car.steer = input.steer;
    corner_for(&car, &input, 2.0f, 1.0f / 45.0f, 30.6f);
    CHECK(fabsf(car.lateral) < 0.5f);
    CHECK(fabsf(car.slip) < 0.05f);
    CHECK(!car_is_slipping(&car));
}

static void test_steady_corner_100_on_35m(void)
{
    car_state_t car;
    car_input_t input = steer_only(steer_for(27.8f, 35.0f));

    car_reset(&car);
    car.speed = 27.8f;
    car.steer = input.steer;
    corner_for(&car, &input, 2.0f, 1.0f / 35.0f, 27.8f);
    CHECK(fabsf(car.lateral) < 0.5f);
    CHECK(fabsf(car.slip) < 0.05f);
    CHECK(!car_is_slipping(&car));
}

static void test_steady_corner_108_on_40m(void)
{
    car_state_t car;
    car_input_t input = steer_only(steer_for(30.0f, 40.0f));

    car_reset(&car);
    car.speed = 30.0f;
    car.steer = input.steer;
    corner_for(&car, &input, 2.0f, 1.0f / 40.0f, 30.0f);
    CHECK(fabsf(car.lateral) < 0.5f);
    CHECK(fabsf(car.slip) < 0.05f);
    CHECK(!car_is_slipping(&car));
}

static void test_tight_25m_holds_at_86(void)
{
    car_state_t car;
    car_input_t input = steer_only(steer_for(24.0f, 25.0f));

    car_reset(&car);
    car.speed = 24.0f;
    car.steer = input.steer;
    corner_for(&car, &input, 2.0f, 1.0f / 25.0f, 24.0f);
    CHECK(fabsf(car.lateral) < 0.5f);
    CHECK(fabsf(car.slip) < 0.05f);
    CHECK(!car_is_slipping(&car));
}

static void test_25m_at_100kmh_must_run_wide(void)
{
    car_state_t car;
    car_input_t full = {false, false, 1.0f, 0.0f};

    car_reset(&car);
    car.speed = 27.8f;
    car.steer = 1.0f;
    corner_for(&car, &full, 1.0f, 1.0f / 25.0f, 27.8f);
    CHECK(car.lateral < -2.0f);
    CHECK(car_is_slipping(&car));
}

static void test_slip_is_quiet_in_normal_curves(void)
{
    static const float corners[][2] = {
        {28.0f, 60.0f},
        {30.0f, 45.0f},
        {20.0f, 40.0f},
    };
    int i;

    for (i = 0; i < 3; i++) {
        float v = corners[i][0];
        float r = corners[i][1];
        car_state_t car;
        car_input_t input;
        float t = 0.0f;

        car_reset(&car);
        input = steer_only(steer_for(v, r));
        car.speed = v;
        car.steer = input.steer;
        while (t < 2.0f) {
            car.speed = v;
            car_step_flat(&car, &input, 1.0f / 60.0f, 1.0f / r);
            CHECK(!car_is_slipping(&car));
            CHECK(car.grip_use < CAR_GRIP_WARN);
            t += 1.0f / 60.0f;
        }
    }
}

static void test_slip_latch_holds_200ms(void)
{
    car_state_t car;
    car_input_t full = {false, false, 1.0f, 0.0f};
    car_input_t released = {false, false, 0.0f, 0.0f};

    car_reset(&car);
    car.speed = 30.0f;
    car.steer = 1.0f;
    car_step_flat(&car, &full, 1.0f / 60.0f, 1.0f / 20.0f);
    CHECK(car_is_slipping(&car));

    step_for(&car, &released, 0.15f, 0.0f);
    CHECK(car_is_slipping(&car)); /* ~0.07 s of latch left */

    step_for(&car, &released, 0.10f, 0.0f);
    CHECK(!car_is_slipping(&car));
}

static void test_slip_and_grip_are_retuned(void)
{
    CHECK(CAR_GRIP_WARN > 1.0f);
    CHECK(CAR_SLIP_HOLD >= 0.15f && CAR_SLIP_HOLD <= 0.30f);
    CHECK(CAR_LAT_ACCEL_MAX >= 20.0f && CAR_LAT_ACCEL_MAX <= 24.0f);
}

static void test_near_geometry_margin(void)
{
    CHECK(ROAD_NEAR_BEHIND >= 8.2f + 0.035f * CAR_MAX_SPEED + 50.0f);
}

static void test_dt_is_clamped_with_curve(void)
{
    car_state_t car;
    car_input_t full = {true, false, 1.0f, 0.0f};

    car_reset(&car);
    car.lateral = 6.7f;
    car.speed = 55.0f;
    car_step_flat(&car, &full, 10.0f, 1.0f / 25.0f);
    CHECK(car.s <= CAR_MAX_SPEED * 0.05f + 1.0f);
    CHECK(fabsf(car.lateral) <= CAR_LATERAL_LIMIT + 1e-4f);
    CHECK(fabsf(car.slip) <= CAR_MAX_SLIP + 1e-4f);
}

static void test_speed_sensitivity_terms(void)
{
    CHECK(CAR_STEER_FULL_SPEED == 12.0f);
    CHECK(CAR_STEER_SPEED_SENSITIVITY2 == 0.00004f);
}

static void test_steer_ramp_saturates_at_12(void)
{
    CHECK(full_lock_yaw(10.0f) < full_lock_yaw(12.0f));
    CHECK(full_lock_yaw(12.0f) > 1.30f);
}

static void test_speed_sensitivity_above_12(void)
{
    CHECK(full_lock_yaw(12.0f) > full_lock_yaw(24.0f));
    CHECK(full_lock_yaw(24.0f) > full_lock_yaw(55.0f));
}

static void test_assist_only_while_steering(void)
{
    car_state_t car;
    car_input_t coast = {false, false, 0.0f, 0.0f};
    int i;

    car_reset(&car);
    car.speed = 28.0f;
    for (i = 0; i < 60; i++) {
        car.speed = 28.0f;
        car_step_flat(&car, &coast, 1.0f / 60.0f, 1.0f / 60.0f);
        CHECK(car.assist_yaw == 0.0f);
    }
    CHECK(car.lateral < -1.0f);
}

static void test_assist_direction_and_cap(void)
{
    car_state_t car;
    car_input_t right = {false, false, 0.3f, 0.0f};
    car_input_t left = {false, false, -0.3f, 0.0f};

    car_reset(&car);
    car.speed = 28.0f;
    car.steer = 0.3f;
    car_step_flat(&car, &right, 1.0f / 60.0f, 1.0f / 60.0f);
    CHECK(car.assist_yaw > 0.005f);
    CHECK(car.assist_yaw <= CAR_ASSIST_MAX_YAW + 1e-5f);

    car_reset(&car);
    car.speed = 28.0f;
    car.steer = -0.3f;
    car_step_flat(&car, &left, 1.0f / 60.0f, 1.0f / 60.0f);
    CHECK(car.assist_yaw == 0.0f);

    car_reset(&car);
    car.speed = 28.0f;
    car.steer = 0.3f;
    car_step_flat(&car, &right, 1.0f / 60.0f, -1.0f / 60.0f);
    CHECK(car.assist_yaw == 0.0f);
}

static void test_assist_never_exceeds_grip(void)
{
    car_state_t car;
    car_input_t input = {false, false, 0.5f, 0.0f};
    float base;

    car_reset(&car);
    car.speed = 30.0f;
    car.steer = 0.5f;
    car_step_flat(&car, &input, 1.0f / 60.0f, 1.0f / 20.0f);

    base = 0.5f * CAR_STEER_YAW_RATE /
           (1.0f + 30.0f * CAR_STEER_SPEED_SENSITIVITY +
            900.0f * CAR_STEER_SPEED_SENSITIVITY2);
    CHECK(car.assist_yaw > 0.0f);
    CHECK(car.assist_yaw <= CAR_ASSIST_MAX_YAW + 1e-5f);
    CHECK(car.assist_yaw <= 24.0f / 30.0f - base + 1e-5f);
    CHECK(fabsf(car.yaw_rate) <= 24.0f / 30.0f + 1e-5f);
}

static void test_assist_off_in_reverse(void)
{
    car_state_t car;
    car_input_t input = {false, false, 1.0f, 0.0f};

    car_reset(&car);
    car.speed = -5.0f;
    car.steer = 1.0f;
    car_step_flat(&car, &input, 1.0f / 60.0f, 1.0f / 60.0f);
    CHECK(car.assist_yaw == 0.0f);
}

static void test_yaw_rate_and_long_accel(void)
{
    car_state_t car;
    car_input_t throttle = {true, false, 0.0f, 0.0f};
    car_input_t steer = {false, false, 1.0f, 0.0f};

    car_reset(&car);
    car_step_flat(&car, &throttle, 1.0f / 60.0f, 0.0f);
    CHECK(car.long_accel > 10.0f);
    CHECK(car.long_accel < 12.5f);
    CHECK(car.yaw_rate == 0.0f);

    car_reset(&car);
    car.speed = 20.0f;
    car.steer = 1.0f;
    car_step_flat(&car, &steer, 1.0f / 60.0f, 0.0f);
    CHECK(car.yaw_rate > 0.0f);

    car_reset(&car);
    CHECK(car.yaw_rate == 0.0f);
    CHECK(car.long_accel == 0.0f);
    CHECK(car.assist_yaw == 0.0f);
}

static void test_road_start_and_back_straight(void)
{
    float s;

    road_reset(ROAD_SEED);
    for (s = 0.0f; s <= 119.0f; s += 17.0f) {
        road_sample_t p = road_sample(s);

        CHECK(nearly(p.x, 0.0f, 1e-4f));
        CHECK(nearly(p.z, s, 1e-4f));
        CHECK(nearly(p.heading, 0.0f, 1e-4f));
        CHECK(nearly(p.curvature, 0.0f, 1e-4f));
    }
    for (s = -119.0f; s <= -1.0f; s += 17.0f) {
        road_sample_t p = road_sample(s);

        CHECK(nearly(p.x, 0.0f, 1e-4f));
        CHECK(nearly(p.z, s, 1e-4f));
        CHECK(nearly(p.heading, 0.0f, 1e-4f));
    }
}

static void test_road_is_deterministic(void)
{
    float x1[101], z1[101], h1[101], w1[101];
    float y1[101], g1[101], v1[101];
    int i;

    road_reset(ROAD_SEED);
    for (i = 0; i <= 100; i++) {
        road_sample_t p = road_sample((float)i * 10.0f);
        x1[i] = p.x;
        z1[i] = p.z;
        h1[i] = p.heading;
        w1[i] = p.half_width;
        y1[i] = p.height;
        g1[i] = p.grade;
        v1[i] = p.grade_rate;
    }

    road_reset(ROAD_SEED);
    for (i = 0; i <= 100; i++) {
        road_sample_t p = road_sample((float)i * 10.0f);
        CHECK(nearly(p.x, x1[i], 1e-5f));
        CHECK(nearly(p.z, z1[i], 1e-5f));
        CHECK(nearly(p.heading, h1[i], 1e-5f));
        CHECK(nearly(p.half_width, w1[i], 1e-4f));
        CHECK(nearly(p.height, y1[i], 1e-4f));
        CHECK(nearly(p.grade, g1[i], 1e-5f));
        CHECK(nearly(p.grade_rate, v1[i], 1e-5f));
    }
}

static void test_road_grammar_and_mix(void)
{
    int straights = 0;
    int gentle = 0;
    int tight = 0;
    int forward = 0;
    int i;

    road_reset(ROAD_SEED);
    road_update(1200.0f);

    for (i = 0; i < road_segment_count(); i++) {
        const road_segment_t *seg = road_segment_at(i);

        if (seg->start_s < 0.0f)
            continue;
        forward++;

        if (seg->curvature == 0.0f) {
            if (seg->length == ROAD_CREST_LENGTH ||
                seg->length == ROAD_LANDING_FLATTEN ||
                seg->length == ROAD_LANDING_RUNWAY)
                continue; /* vertical landing-plan segments */
            CHECK(seg->length >= ROAD_STRAIGHT_MIN - 1e-3f);
            CHECK(seg->length <= ROAD_STRAIGHT_MAX + 1e-3f);
            straights++;
        } else {
            float radius = 1.0f / fabsf(seg->curvature);

            if (radius >= ROAD_GENTLE_RADIUS_MIN - 1e-3f &&
                radius <= ROAD_GENTLE_RADIUS_MAX + 1e-3f) {
                CHECK(seg->length >= ROAD_GENTLE_ARC_MIN - 1e-3f);
                CHECK(seg->length <= ROAD_GENTLE_ARC_MAX + 1e-3f);
                gentle++;
            } else if (radius >= ROAD_TIGHT_RADIUS_MIN - 1e-3f &&
                       radius <= ROAD_TIGHT_RADIUS_MAX + 1e-3f) {
                CHECK(seg->length >= ROAD_TIGHT_ARC_MIN - 1e-3f);
                CHECK(seg->length <= ROAD_TIGHT_ARC_MAX + 1e-3f);
                tight++;
            } else {
                CHECK(0 && "curve radius outside grammar");
            }
        }
    }

    CHECK(forward >= 16);
    CHECK(straights >= 8);
    CHECK(gentle >= 4);
    CHECK(tight >= 4);
}

static void check_road_continuity(float from, float to)
{
    float s;

    for (s = from; s < to; s += 1.0f) {
        road_sample_t a = road_sample(s);
        road_sample_t b = road_sample(s + 1.0f);
        float dx = b.x - a.x;
        float dz = b.z - a.z;
        float dh = b.heading - a.heading;

        dh = atan2f(sinf(dh), cosf(dh));
        CHECK(fabsf(sqrtf(dx * dx + dz * dz) - 1.0f) < 0.02f);
        CHECK(fabsf(dh) < 0.10f);
        CHECK(fabsf(b.half_width - a.half_width) < 0.18f);
    }
}

static void test_road_is_continuous(void)
{
    road_reset(ROAD_SEED);
    check_road_continuity(-1100.0f, 1100.0f);
}

static void test_road_is_continuous_after_rebase(void)
{
    float s;

    /* Forward re-base: drive to 3001, the origin becomes 2000. */
    road_reset(ROAD_SEED);
    s = road_update(3001.0f);
    CHECK(nearly(s, 1001.0f, 1e-3f));
    check_road_continuity(901.0f, 1101.0f);

    /* Reverse re-base: drive to -3001, the origin becomes -2000. */
    road_reset(ROAD_SEED);
    s = road_update(-3001.0f);
    CHECK(nearly(s, -1001.0f, 1e-3f));
    check_road_continuity(-1101.0f, -901.0f);
}

static void test_road_reverse_extends_backward(void)
{
    float s = 0.0f;
    int i;

    road_reset(ROAD_SEED);
    for (i = 0; i < 30; i++) {
        road_sample_t p;

        s = road_update(s - 300.0f);
        CHECK(fabsf(s) <= ROAD_REBASE_LIMIT + 1.0f);
        p = road_sample(s);
        CHECK(isfinite(p.x) && isfinite(p.z) && isfinite(p.heading));
        CHECK(road_segment_count() <= ROAD_MAX_SEGMENTS);
        CHECK(road_segment_at(0)->start_s <=
              s - ROAD_WINDOW_BEHIND + 1.0f);
    }
    CHECK(s < -1000.0f);
}

static void check_long_drive(road_map_t map)
{
    road_config_t c;
    float s = 0.0f;
    int i;

    road_config_defaults(&c);
    c.map = map;
    road_reset_config(ROAD_SEED, &c);
    for (i = 0; i < 200; i++) {
        int j;

        s = road_update(s + 500.0f);
        CHECK(fabsf(s) <= ROAD_REBASE_LIMIT + 1.0f);
        CHECK(road_segment_count() <= ROAD_MAX_SEGMENTS);
        for (j = 0; j < road_segment_count(); j++) {
            const road_segment_t *seg = road_segment_at(j);

            CHECK(fabsf(seg->start_x) < 4000.0f);
            CHECK(fabsf(seg->start_z) < 4000.0f);
            CHECK(isfinite(seg->start_height));
            CHECK(fabsf(seg->start_height) < 500.0f);
        }
    }
}

static void test_road_long_drive_keeps_coordinates_bounded(void)
{
    check_long_drive(ROAD_MAP_DEFAULT);
    check_long_drive(ROAD_MAP_UPHILL);
}

static void test_road_rebase_preserves_distance(void)
{
    float s;
    float before;
    float after;
    road_sample_t a;
    road_sample_t b;

    road_reset(ROAD_SEED);
    s = road_update(2999.0f);
    a = road_sample(s - 100.0f);
    b = road_sample(s + 50.0f);
    before = hypotf(b.x - a.x, b.z - a.z);

    s = road_update(3001.0f);
    a = road_sample(899.0f);
    b = road_sample(1049.0f);
    after = hypotf(b.x - a.x, b.z - a.z);

    CHECK(nearly(before, after, 1e-2f));
    /* A curved road's chord is shorter than its 150 m arc. */
    CHECK(before > 1.0f && before <= 150.5f);
    CHECK(nearly(s, 1001.0f, 1e-3f));
}

static void test_road_rebase_step_keeps_periods(void)
{
    CHECK(nearly(fmodf(ROAD_REBASE_ALIGN, ROAD_TEXTURE_PERIOD), 0.0f,
                 1e-3f));
    CHECK(nearly(fmodf(ROAD_REBASE_ALIGN, ROAD_POST_SPACING), 0.0f, 1e-3f));
    CHECK(nearly(fmodf(ROAD_REBASE_ALIGN, ROAD_MESH_STEP), 0.0f, 1e-3f));
    CHECK(nearly(fmodf(ROAD_REBASE_ALIGN, ROAD_DASH_PERIOD), 0.0f, 1e-3f));
    CHECK(nearly(fmodf(ROAD_REBASE_STEP, ROAD_REBASE_ALIGN), 0.0f, 1e-3f));
}

static void test_road_next_curve(void)
{
    float distance;
    float curvature;
    float first_start = -1.0f;
    int i;

    road_reset(ROAD_SEED);
    road_update(0.0f);

    CHECK(road_next_curve(0.0f, 200.0f, 1.0f / 120.0f, &distance,
                          &curvature));
    CHECK(distance >= 119.0f && distance <= 121.0f);
    CHECK(fabsf(curvature) >= 1.0f / 120.0f - 1e-6f);
    CHECK(fabsf(curvature) <= 1.0f / 25.0f + 1e-6f);
    CHECK(!road_next_curve(0.0f, 100.0f, 1.0f / 120.0f, &distance,
                           &curvature));

    for (i = 0; i < road_segment_count(); i++) {
        const road_segment_t *seg = road_segment_at(i);

        if (seg->start_s >= 0.0f && seg->curvature != 0.0f) {
            first_start = seg->start_s;
            break;
        }
    }
    CHECK(first_start >= 0.0f);
    CHECK(road_next_curve(first_start + 1.0f, 400.0f, 1.0f / 120.0f,
                          &distance, &curvature));
    CHECK(distance > 0.0f);

    CHECK(!road_next_curve(0.0f, 200.0f, 0.5f, &distance, &curvature));
}

static void test_road_curve_arc(void)
{
    int i;

    road_reset(ROAD_SEED);
    road_update(1200.0f);

    for (i = 0; i < road_segment_count(); i++) {
        const road_segment_t *seg = road_segment_at(i);
        road_sample_t a;
        road_sample_t b;
        float dpsi;
        float expected;
        float chord;
        float r;

        if (seg->curvature == 0.0f)
            continue;

        a = road_sample(seg->start_s);
        b = road_sample(seg->start_s + seg->length);
        dpsi = atan2f(sinf(b.heading - a.heading),
                      cosf(b.heading - a.heading));
        expected = atan2f(sinf(seg->curvature * seg->length),
                          cosf(seg->curvature * seg->length));
        chord = hypotf(b.x - a.x, b.z - a.z);
        r = fabsf(1.0f / seg->curvature);

        CHECK(nearly(dpsi, expected, 1e-3f));
        CHECK(nearly(chord, 2.0f * r * sinf(seg->length / (2.0f * r)),
                     0.02f));
        break;
    }
}

static void test_road_config_defaults_and_validate(void)
{
    road_config_t c;

    road_config_defaults(&c);
    CHECK(nearly(c.elevation_pct, 100.0f, 1e-4f));
    CHECK(nearly(c.hill_pct, 100.0f, 1e-4f));
    CHECK(nearly(c.width_pct, 100.0f, 1e-4f));
    CHECK(nearly(c.one_lane_width, 4.4f, 1e-4f));
    CHECK(!road_config_validate(&c));

    c.elevation_pct = 0.0f;
    c.hill_pct = 999.0f;
    c.width_pct = -5.0f;
    c.one_lane_width = 9.0f;
    CHECK(road_config_validate(&c));
    CHECK(c.elevation_pct == 50.0f);
    CHECK(c.hill_pct == 150.0f);
    CHECK(c.width_pct == 50.0f);
    CHECK(c.one_lane_width == 6.0f);

    road_config_defaults(&c);
    c.elevation_pct = NAN;
    CHECK(road_config_validate(&c));
    CHECK(nearly(c.elevation_pct, 100.0f, 1e-4f));
}

static void test_road_map_config(void)
{
    road_config_t c;

    road_config_defaults(&c);
    CHECK(c.map == ROAD_MAP_DEFAULT);
    CHECK(nearly(c.elevation_pct, 100.0f, 1e-4f));
    CHECK(nearly(c.one_lane_width, 4.4f, 1e-4f));
    CHECK(!road_config_validate(&c));

    c.map = (road_map_t)99;
    CHECK(road_config_validate(&c));
    CHECK(c.map == ROAD_MAP_DEFAULT);

    c.map = ROAD_MAP_HIGHWAY;
    c.width_pct = 150.0f;
    c.one_lane_width = 6.0f;
    CHECK(!road_config_validate(&c)); /* values stay valid and are ignored */
    CHECK(c.map == ROAD_MAP_HIGHWAY);
}

static void test_road_width_cycle_length(void)
{
    road_config_t c;

    road_config_defaults(&c);
    CHECK(nearly(road_width_cycle_length(&c), 1200.0f, 1e-3f));
    c.width_pct = 50.0f;
    CHECK(nearly(road_width_cycle_length(&c), 2400.0f, 1e-3f));
    c.width_pct = 150.0f;
    CHECK(nearly(road_width_cycle_length(&c), 800.0f, 1e-3f));
}

static void test_road_default_width_spans(void)
{
    road_config_t c;
    float s;
    float min_half = 99.0f;
    float max_half = 0.0f;

    road_config_defaults(&c);
    road_reset_config(ROAD_SEED, &c);
    road_update(1200.0f);
    for (s = 0.0f; s < 2300.0f; s += 0.5f) {
        road_sample_t p = road_sample(s);
        road_sample_t q = road_sample(s + 0.5f);

        CHECK(p.half_width >= 2.2f - 1e-3f);
        CHECK(p.half_width <= 8.0f + 1e-3f);
        CHECK(nearly(p.edge_half_width - p.half_width, 4.5f, 1e-5f));
        CHECK(fabsf(q.half_width - p.half_width) <= 0.09f);
        min_half = fminf(min_half, p.half_width);
        max_half = fmaxf(max_half, p.half_width);
    }
    CHECK(nearly(min_half, 2.2f, 1e-3f));
    CHECK(nearly(max_half, 8.0f, 1e-3f));

    CHECK(nearly(road_sample(50.0f).half_width, 5.0f, 1e-3f));
    CHECK(nearly(road_sample(850.0f).half_width, 2.2f, 1e-3f));
    CHECK(nearly(road_sample(1000.0f).half_width, 2.2f, 1e-3f));
    CHECK(nearly(road_sample(1800.0f).half_width, 8.0f, 1e-3f));
    CHECK(nearly(road_sample(2100.0f).half_width, 8.0f, 1e-3f));
}

static void check_wide_span(float flat_start, float flat_end)
{
    int tight = 0;
    int anchored = 0;
    int i;

    CHECK(flat_end - flat_start >= 500.0f - 0.5f);
    for (i = 0; i < road_segment_count(); i++) {
        const road_segment_t *seg = road_segment_at(i);
        float end = seg->start_s + seg->length;

        if (fabsf(seg->curvature) <= 1.0f / 50.0f)
            continue;
        if (end < flat_start || seg->start_s > flat_end)
            continue;
        tight++;
        if (seg->start_s - flat_start >= 150.0f - 1e-3f &&
            flat_end - end >= 150.0f - 1e-3f)
            anchored++;
    }
    CHECK(tight >= 1);
    CHECK(anchored >= 1);
}

static void test_road_default_wide_anchor(void)
{
    road_config_t c;
    float s;
    float flat_start = -1.0f;
    int spans = 0;

    road_config_defaults(&c);
    road_reset_config(ROAD_SEED, &c);
    road_update(2400.0f);
    for (s = 1200.0f; s < 3400.0f; s += 0.5f) {
        int wide = road_sample(s).half_width >= 7.99f;

        if (wide && flat_start < 0.0f) {
            flat_start = s;
        } else if (!wide && flat_start >= 0.0f) {
            check_wide_span(flat_start, s);
            spans++;
            flat_start = -1.0f;
        }
    }
    if (flat_start >= 0.0f) {
        check_wide_span(flat_start, 3400.0f);
        spans++;
    }
    CHECK(spans >= 2); /* first and second plateaus are both checked */
}

static void test_road_uphill_chains(void)
{
    road_config_t c;
    int runs, min_len, max_len, alternating;
    int i;

    road_config_defaults(&c);
    c.map = ROAD_MAP_UPHILL;
    road_reset_config(ROAD_SEED, &c);
    road_update(1200.0f);
    collect_complete_runs(0, 2200, &runs, &min_len, &max_len, &alternating);
    CHECK(runs >= 7);
    CHECK(min_len >= 2);
    CHECK(max_len <= 5);
    CHECK(alternating);

    for (i = 0; i < road_segment_count(); i++) {
        const road_segment_t *seg = road_segment_at(i);

        if (seg->start_s >= 0.0f && seg->start_s < 2200.0f)
            CHECK(seg->length > 0.0f);
    }
}

static void test_road_uphill_tight_gentle(void)
{
    road_config_t c;
    int tight = 0;
    int gentle = 0;
    int i;

    road_config_defaults(&c);
    c.map = ROAD_MAP_UPHILL;
    road_reset_config(ROAD_SEED, &c);
    road_update(1200.0f);
    for (i = 0; i < road_segment_count(); i++) {
        const road_segment_t *seg = road_segment_at(i);
        float r;

        if (seg->start_s < 0.0f || seg->start_s >= 2200.0f ||
            seg->curvature == 0.0f)
            continue;
        r = 1.0f / fabsf(seg->curvature);
        if (r >= ROAD_TIGHT_RADIUS_MIN - 1e-3f &&
            r <= ROAD_TIGHT_RADIUS_MAX + 1e-3f)
            tight++;
        else if (r >= ROAD_GENTLE_RADIUS_MIN - 1e-3f &&
                 r <= ROAD_GENTLE_RADIUS_MAX + 1e-3f)
            gentle++;
        else
            CHECK(0 && "uphill curve outside the grammar");
    }
    CHECK(gentle > 0);
    CHECK(tight >= 2 * gentle);
}

static void test_road_uphill_width(void)
{
    road_config_t c;
    float s;
    float min_half = 99.0f;
    float max_half = 0.0f;
    int found = 0;

    road_config_defaults(&c);
    c.map = ROAD_MAP_UPHILL;
    road_reset_config(ROAD_SEED, &c);
    road_update(3000.0f);
    for (s = 2800.0f; s < 3600.0f; s += 0.5f) {
        road_sample_t p = road_sample(s);

        min_half = fminf(min_half, p.half_width);
        max_half = fmaxf(max_half, p.half_width);
        CHECK(p.half_width <= 5.0f + 1e-4f);
        CHECK(nearly(p.edge_half_width - p.half_width, 4.5f, 1e-5f));
        if (p.half_width <= 2.21f)
            found = 1;
    }
    CHECK(found);
    CHECK(nearly(min_half, 2.2f, 1e-3f));
    CHECK(nearly(max_half, 5.0f, 1e-3f));
}

static void test_road_highway_mix(void)
{
    road_config_t c;
    int runs, min_len, max_len, alternating;
    int tight = 0;
    int gentle = 0;
    float s;
    int i;

    road_config_defaults(&c);
    c.map = ROAD_MAP_HIGHWAY;
    road_reset_config(ROAD_SEED, &c);
    road_update(1200.0f);
    for (s = 0.0f; s < 2200.0f; s += 0.5f) {
        road_sample_t p = road_sample(s);

        CHECK(nearly(p.half_width, 8.0f, 1e-4f));
        CHECK(nearly(p.edge_half_width, 12.5f, 1e-4f));
        CHECK(road_lane_count(p.half_width) == 4);
        CHECK(road_lane_dashes(p.half_width) == 3);
    }
    collect_complete_runs(0, 2200, &runs, &min_len, &max_len, &alternating);
    CHECK(runs >= 8);
    CHECK(min_len == 1 && max_len == 1);
    for (i = 0; i < road_segment_count(); i++) {
        const road_segment_t *seg = road_segment_at(i);

        if (seg->start_s < 0.0f || seg->start_s >= 2200.0f ||
            seg->curvature == 0.0f)
            continue;
        if (fabsf(seg->curvature) > 1.0f / 55.0f)
            tight++;
        else
            gentle++;
    }
    CHECK(tight > 0 && gentle > 0);
}

static void test_road_map_determinism(void)
{
    road_config_t a;
    road_config_t b;
    road_sample_t first[50];
    road_sample_t second[50];
    int different = 0;
    int i;

    road_config_defaults(&a);
    road_reset_config(ROAD_SEED, &a);
    for (i = 0; i < 50; i++)
        first[i] = road_sample((float)i * 40.0f);

    road_config_defaults(&b);
    road_reset_config(ROAD_SEED, &b);
    for (i = 0; i < 50; i++)
        second[i] = road_sample((float)i * 40.0f);
    for (i = 0; i < 50; i++) {
        CHECK(nearly(first[i].x, second[i].x, 1e-5f));
        CHECK(nearly(first[i].z, second[i].z, 1e-5f));
        CHECK(nearly(first[i].half_width, second[i].half_width, 1e-5f));
    }

    b.map = ROAD_MAP_UPHILL;
    road_reset_config(ROAD_SEED, &b);
    for (i = 0; i < 50; i++) {
        second[i] = road_sample((float)i * 40.0f);
        if (!nearly(first[i].x, second[i].x, 1e-4f) ||
            !nearly(first[i].z, second[i].z, 1e-4f))
            different = 1;
    }
    CHECK(different);
}

static void test_lane_dash_counts(void)
{
    CHECK(road_lane_count(2.2f) == 1);
    CHECK(road_lane_dashes(2.2f) == 0);
    CHECK(road_lane_count(5.0f) == 2);
    CHECK(road_lane_dashes(5.0f) == 1);
    CHECK(road_lane_count(6.0f) == 3);
    CHECK(road_lane_dashes(6.0f) == 2);
    CHECK(road_lane_count(8.0f) == 4);
    CHECK(road_lane_dashes(8.0f) == 3);
}

static void test_lane_dash_pattern(void)
{
    CHECK(road_dash_on(0.0f));
    CHECK(road_dash_on(4.9f));
    CHECK(!road_dash_on(5.0f));
    CHECK(!road_dash_on(9.9f));
    CHECK(road_dash_on(10.0f));
    CHECK(road_dash_on(-10.0f));
    CHECK(!road_dash_on(-5.0f));
}

static void test_shoulder_width_limits(void)
{
    car_state_t car;
    car_input_t coast = {false, 0.0f, 0.0f, 0.0f};
    road_sample_t normal = flat_road(0.0f);
    road_sample_t wide = flat_road(0.0f);
    road_sample_t narrow = flat_road(0.0f);

    CHECK(nearly(ROAD_SHOULDER_WIDTH, 4.5f, 1e-6f));
    CHECK(nearly(ROAD_EDGE_X, 9.5f, 1e-6f));
    CHECK(nearly(CAR_LATERAL_LIMIT, 9.1f, 1e-6f));

    wide.half_width = 8.0f;
    wide.edge_half_width = 12.5f;
    narrow.half_width = 2.2f;
    narrow.edge_half_width = 6.7f;

    car_reset(&car);
    car.speed = 10.0f;
    car.lateral = 4.9f;
    car_step(&car, &coast, 1.0f / 60.0f, &normal);
    CHECK(!car_is_offroad(&car));
    car.lateral = 5.1f;
    car_step(&car, &coast, 1.0f / 60.0f, &normal);
    CHECK(car_is_offroad(&car));
    car.lateral = 12.4f;
    car_step(&car, &coast, 1.0f / 60.0f, &normal);
    CHECK(nearly(car.lateral, 9.1f, 1e-4f));

    car.lateral = 12.4f;
    car_step(&car, &coast, 1.0f / 60.0f, &wide);
    CHECK(nearly(car.lateral, 12.1f, 1e-4f));

    car.lateral = 9.0f;
    car_step(&car, &coast, 1.0f / 60.0f, &narrow);
    CHECK(nearly(car.lateral, 6.3f, 1e-4f));
}

static void test_road_seed_stream(void)
{
    uint32_t state = ROAD_SEED;
    uint32_t a = road_seed_next(&state);
    uint32_t b = road_seed_next(&state);

    CHECK(a != 0);
    CHECK(b != 0);
    CHECK(a != b);
    CHECK(state == b);
}

static void test_road_next_start_seed(void)
{
    uint32_t state = ROAD_SEED;
    uint32_t a = road_next_start_seed(&state);
    uint32_t b = road_next_start_seed(&state);
    uint32_t c = road_next_start_seed(&state);
    road_config_t cfg;
    road_sample_t pa, pb;

    CHECK(a == ROAD_SEED);
    CHECK(b != a);
    CHECK(c != b);

    road_config_defaults(&cfg);
    road_reset_config(a, &cfg);
    pa = road_sample(500.0f);
    road_reset_config(b, &cfg);
    pb = road_sample(500.0f);
    CHECK(fabsf(pa.x - pb.x) > 1e-4f ||
          fabsf(pa.z - pb.z) > 1e-4f ||
          fabsf(pa.half_width - pb.half_width) > 1e-4f);
}

static void test_road_width_profile(void)
{
    road_config_t c;
    float s;
    float min_half = 99.0f;
    float max_half = 0.0f;

    road_config_defaults(&c);
    road_reset_config(ROAD_SEED, &c);

    for (s = -1100.0f; s < 1100.0f; s += 0.5f) {
        road_sample_t p = road_sample(s);
        road_sample_t q = road_sample(s + 0.5f);

        CHECK(p.half_width >= 2.0f - 1e-4f);
        CHECK(p.half_width <= 8.0f + 1e-4f);
        CHECK(nearly(p.edge_half_width - p.half_width,
                     ROAD_SHOULDER_WIDTH, 1e-4f));
        CHECK(fabsf(q.half_width - p.half_width) <= 0.09f);
        if (p.half_width < min_half) min_half = p.half_width;
        if (p.half_width > max_half) max_half = p.half_width;
    }
    CHECK(min_half <= 2.21f);
    CHECK(max_half >= 6.5f);
}

static void check_width_away_from_tight_curves(void)
{
    int i;

    for (i = 0; i < road_segment_count(); i++) {
        const road_segment_t *seg = road_segment_at(i);
        float s;

        if (fabsf(seg->curvature) <= ROAD_TIGHT_CURVATURE)
            continue;
        for (s = seg->start_s - 30.0f; s <= seg->start_s + seg->length + 30.0f;
             s += 1.0f)
            CHECK(road_sample(s).half_width >= 5.0f);
    }
}

static void test_road_width_avoids_tight_curves(void)
{
    road_config_t c;

    road_config_defaults(&c);
    road_reset_config(ROAD_SEED, &c);
    check_width_away_from_tight_curves();
}

static void test_road_width_avoids_tight_curves_after_rebase(void)
{
    road_config_t c;
    int tight = 0;
    int i;

    road_config_defaults(&c);
    road_reset_config(ROAD_SEED, &c);
    road_update(3001.0f); /* world_offset = 2000, local car_s = 1001 */
    for (i = 0; i < road_segment_count(); i++) {
        const road_segment_t *seg = road_segment_at(i);

        if (fabsf(seg->curvature) > ROAD_TIGHT_CURVATURE)
            tight++;
    }
    CHECK(tight > 0);
    check_width_away_from_tight_curves();
}

static void test_road_width_rebase_preserves_profile(void)
{
    road_config_t c;
    float s;
    float before[41];
    int n = 0;

    road_config_defaults(&c);
    road_reset_config(ROAD_SEED, &c);
    road_update(1800.0f); /* ring 600..3000, no re-base yet */
    for (s = 1800.0f; s <= 2200.0f; s += 10.0f)
        before[n++] = road_sample(s).half_width;

    n = 0;
    road_update(3001.0f); /* origin 2000; physical 1800..2200 is local -200..200 */
    for (s = 1800.0f; s <= 2200.0f; s += 10.0f) {
        float after = road_sample(s - 2000.0f).half_width;

        CHECK(nearly(after, before[n++], 1e-4f));
    }
}

static void test_road_width_sliders(void)
{
    road_config_t c;
    float s;
    float min_half;
    float cycle_50, cycle_100, cycle_150;
    int narrow_50, narrow_100, narrow_150;
    int uphill_50, uphill_100, uphill_150;

    road_config_defaults(&c);
    c.one_lane_width = 4.0f;
    road_reset_config(ROAD_SEED, &c);
    road_update(1200.0f);
    min_half = 99.0f;
    for (s = 0.0f; s < 2300.0f; s += 0.5f)
        min_half = fminf(min_half, road_sample(s).half_width);
    CHECK(nearly(min_half, 2.0f, 0.01f));

    c.one_lane_width = 6.0f;
    road_reset_config(ROAD_SEED, &c);
    road_update(1200.0f);
    min_half = 99.0f;
    for (s = 0.0f; s < 2300.0f; s += 0.5f)
        min_half = fminf(min_half, road_sample(s).half_width);
    CHECK(nearly(min_half, 3.0f, 0.01f));

    road_config_defaults(&c);
    c.width_pct = 50.0f;
    cycle_50 = road_width_cycle_length(&c);
    c.width_pct = 100.0f;
    cycle_100 = road_width_cycle_length(&c);
    c.width_pct = 150.0f;
    cycle_150 = road_width_cycle_length(&c);
    CHECK(nearly(cycle_50, 2400.0f, 1e-3f));
    CHECK(nearly(cycle_100, 1200.0f, 1e-3f));
    CHECK(nearly(cycle_150, 800.0f, 1e-3f));

    road_config_defaults(&c);
    c.width_pct = 50.0f;
    narrow_50 = count_narrow_events_walk(&c, 12000.0f);
    c.width_pct = 100.0f;
    narrow_100 = count_narrow_events_walk(&c, 12000.0f);
    c.width_pct = 150.0f;
    narrow_150 = count_narrow_events_walk(&c, 12000.0f);
    CHECK(narrow_50 == 2);
    CHECK(narrow_100 == 3);
    CHECK(narrow_150 == 5);
    CHECK(narrow_150 > narrow_50);

    road_config_defaults(&c);
    c.map = ROAD_MAP_UPHILL;
    c.width_pct = 50.0f;
    uphill_50 = count_narrow_events_walk(&c, 12000.0f);
    c.width_pct = 100.0f;
    uphill_100 = count_narrow_events_walk(&c, 12000.0f);
    c.width_pct = 150.0f;
    uphill_150 = count_narrow_events_walk(&c, 12000.0f);
    CHECK(uphill_50 == 1);
    CHECK(uphill_100 == 1);
    CHECK(uphill_150 == 4);
    CHECK(uphill_150 > uphill_50);
}

static void test_road_elevation_bounds_and_continuity(void)
{
    road_config_t c;
    float s;
    float max_grade = 0.0f;

    road_config_defaults(&c);
    road_reset_config(ROAD_SEED, &c);
    for (s = -1100.0f; s < 1100.0f; s += 0.5f) {
        road_sample_t p = road_sample(s);
        road_sample_t q = road_sample(s + 0.5f);

        CHECK(fabsf(p.grade) <= 0.1201f);
        CHECK(fabsf(q.grade - p.grade) <= 0.0101f);
        CHECK(fabsf(q.height - p.height) <= 0.101f);
        if (fabsf(p.grade) > max_grade) max_grade = fabsf(p.grade);
    }
    CHECK(max_grade >= 0.11f);
    CHECK(nearly(road_sample(0.0f).height, 0.0f, 1e-4f));
    CHECK(nearly(road_sample(0.0f).grade, 0.0f, 1e-4f));

    c.map = ROAD_MAP_HIGHWAY;
    c.elevation_pct = 100.0f;
    road_reset_config(ROAD_SEED, &c);
    max_grade = 0.0f;
    for (s = 0.0f; s < 2200.0f; s += 0.5f)
        max_grade = fmaxf(max_grade, fabsf(road_sample(s).grade));
    CHECK(max_grade <= 0.0301f);

    c.elevation_pct = 150.0f;
    road_reset_config(ROAD_SEED, &c);
    max_grade = 0.0f;
    for (s = 0.0f; s < 2200.0f; s += 0.5f)
        max_grade = fmaxf(max_grade, fabsf(road_sample(s).grade));
    CHECK(max_grade <= 0.0451f);
}

static void test_road_crests_on_straights(void)
{
    road_config_t c;
    int i;
    int crests = 0;

    road_config_defaults(&c);
    road_reset_config(ROAD_SEED, &c);
    road_update(1200.0f);
    for (i = 0; i < road_segment_count(); i++) {
        const road_segment_t *seg = road_segment_at(i);

        if (seg->crest_len > 0.0f && seg->grade_rate < -0.004f) {
            CHECK(seg->curvature == 0.0f);
            CHECK(seg->crest_len >= 25.0f - 1e-3f);
            CHECK(seg->crest_len <= ROAD_CREST_LENGTH + 1e-3f);
            crests++;
        }
    }
    CHECK(crests >= 3);
}

static void test_road_vertical_run_base(void)
{
    road_config_t c;

    CHECK(ROAD_VERT_RUN_BASE == 7);
    road_config_defaults(&c);
    CHECK(road_vertical_run_max(&c) == 7);
    c.hill_pct = 50.0f;
    CHECK(road_vertical_run_max(&c) == 14);
    c.hill_pct = 150.0f;
    CHECK(road_vertical_run_max(&c) == 5);
    c.map = ROAD_MAP_HIGHWAY;
    c.hill_pct = 100.0f;
    CHECK(road_vertical_run_max(&c) == 336);
}

static void check_landing_runways(const road_config_t *cfg, float horizon,
                                  int min_crests)
{
    int crests = 0;
    int bad = 0;
    int i;

    road_reset_config(ROAD_SEED, cfg);
    road_update(horizon);
    for (i = 0; i + 2 < road_segment_count(); i++) {
        const road_segment_t *crest = road_segment_at(i);
        const road_segment_t *flat = road_segment_at(i + 1);
        const road_segment_t *run = road_segment_at(i + 2);
        float s;

        if (crest->start_s < 0.0f || crest->start_s >= 2200.0f)
            continue;
        if (crest->curvature != 0.0f || crest->grade_rate >= -0.004f ||
            crest->crest_len != ROAD_CREST_LENGTH ||
            crest->length != ROAD_CREST_LENGTH)
            continue;
        crests++;
        if (flat->curvature != 0.0f ||
            flat->length != ROAD_LANDING_FLATTEN ||
            fabsf(flat->start_grade + flat->grade_rate * flat->length) >
                1e-4f ||
            run->curvature != 0.0f ||
            run->length != ROAD_LANDING_RUNWAY ||
            fabsf(run->start_grade) > 1e-4f ||
            fabsf(run->grade_rate) > 1e-6f) {
            bad++;
            continue;
        }
        for (s = run->start_s; s <= run->start_s + run->length; s += 2.0f)
            CHECK(fabsf(road_sample(s).grade) <=
                  ROAD_LANDING_GRADE_MAX + 1e-5f);
    }
    CHECK(crests >= min_crests);
    CHECK(bad == 0);
}

static void test_road_landing_runway(void)
{
    road_config_t c;

    road_config_defaults(&c);
    check_landing_runways(&c, 1200.0f, 3);

    c.map = ROAD_MAP_UPHILL;
    check_landing_runways(&c, 2200.0f, 1);
    /* HIGHWAY at 150 % has no crest-runway sequence in 0..2200 m (its
     * descents are gentler than the launch grade), so it has no pass. */
}

static void test_road_uphill_elevation(void)
{
    road_config_t def;
    road_config_t up;
    int def_crests = 0;
    int up_crests = 0;
    int climbs = 0;
    int descents = 0;
    int i;

    road_config_defaults(&def);
    road_reset_config(ROAD_SEED, &def);
    road_update(1200.0f);
    for (i = 0; i < road_segment_count(); i++) {
        const road_segment_t *seg = road_segment_at(i);

        if (seg->start_s >= 0.0f && seg->start_s < 2200.0f &&
            seg->crest_len > 0.0f && seg->grade_rate < -0.004f)
            def_crests++;
    }

    road_config_defaults(&up);
    up.map = ROAD_MAP_UPHILL;
    road_reset_config(ROAD_SEED, &up);
    road_update(1200.0f);
    for (i = 0; i < road_segment_count(); i++) {
        const road_segment_t *seg = road_segment_at(i);
        float mid;

        if (seg->start_s < 0.0f || seg->start_s >= 2200.0f)
            continue;
        if (seg->crest_len > 0.0f && seg->grade_rate < -0.004f)
            up_crests++;
        mid = road_sample(seg->start_s + seg->length * 0.5f).grade;
        if (mid > 0.02f)
            climbs++;
        if (mid < -0.02f)
            descents++;
    }
    CHECK(up_crests > def_crests);
    CHECK(climbs >= 2 * descents);
}

static void test_road_highway_crests(void)
{
    road_config_t c;
    float max_grade = 0.0f;
    int crests = 0;
    float s;
    int i;

    road_config_defaults(&c);
    c.map = ROAD_MAP_HIGHWAY;
    road_reset_config(ROAD_SEED, &c);
    road_update(1200.0f);
    for (s = 0.0f; s < 2200.0f; s += 0.5f)
        max_grade = fmaxf(max_grade, fabsf(road_sample(s).grade));
    for (i = 0; i < road_segment_count(); i++) {
        const road_segment_t *seg = road_segment_at(i);

        if (seg->start_s >= 0.0f && seg->start_s < 2200.0f &&
            seg->crest_len < seg->length - 1e-3f &&
            seg->grade_rate < -0.0015f)
            crests++;
    }
    CHECK(max_grade <= 0.0301f);
    CHECK(crests == 1); /* 0..2200 m ring; a 12 km walk measures 2 */
}

static void test_road_elevation_slider(void)
{
    road_config_t c;
    float s;
    float max_50 = 0.0f;
    float max_150 = 0.0f;

    road_config_defaults(&c);
    c.elevation_pct = 50.0f;
    road_reset_config(ROAD_SEED, &c);
    for (s = -1100.0f; s < 1100.0f; s += 0.5f)
        max_50 = fmaxf(max_50, fabsf(road_sample(s).grade));
    CHECK(max_50 <= 0.0601f);

    c.elevation_pct = 150.0f;
    road_reset_config(ROAD_SEED, &c);
    for (s = -1100.0f; s < 1100.0f; s += 0.5f)
        max_150 = fmaxf(max_150, fabsf(road_sample(s).grade));
    CHECK(max_150 >= 0.17f);
}

static int count_crests_walk(const road_config_t *cfg, float span)
{
    road_config_t c = *cfg;
    float car_s = 0.0f;
    float travelled = 0.0f;
    int crests = 0;
    int i;

    road_reset_config(ROAD_SEED, &c);
    road_update(0.0f);
    for (i = 0; i < road_segment_count(); i++) {
        const road_segment_t *seg = road_segment_at(i);

        if (seg->start_s >= 0.0f && seg->start_s < 100.0f &&
            seg->crest_len > 0.0f && seg->grade_rate < -0.004f)
            crests++;
    }
    while (travelled + 100.0f < span) {
        float lo;
        float hi;

        car_s = road_update(car_s + 200.0f);
        travelled += 200.0f;
        lo = car_s - 100.0f;
        hi = car_s + 100.0f;
        for (i = 0; i < road_segment_count(); i++) {
            const road_segment_t *seg = road_segment_at(i);

            if (seg->start_s >= lo && seg->start_s < hi &&
                seg->crest_len > 0.0f && seg->grade_rate < -0.004f)
                crests++;
        }
    }
    return crests;
}

static void test_road_hill_frequency(void)
{
    road_config_t c;
    int freq_50;
    int freq_100;
    int freq_150;

    road_config_defaults(&c);
    c.hill_pct = 50.0f;
    freq_50 = count_crests_walk(&c, 4000.0f);
    c.hill_pct = 100.0f;
    freq_100 = count_crests_walk(&c, 4000.0f);
    c.hill_pct = 150.0f;
    freq_150 = count_crests_walk(&c, 4000.0f);
    CHECK(freq_50 == 3);
    CHECK(freq_100 == 5);
    CHECK(freq_150 == 7);
    CHECK(freq_150 > freq_50);
}

static void test_road_height_rebase(void)
{
    road_config_t c;
    road_sample_t a, b, a2, b2;
    float dh, dg, dw;
    float s;

    road_config_defaults(&c);
    road_reset_config(ROAD_SEED, &c);
    road_update(2900.0f);
    a = road_sample(2900.0f);
    b = road_sample(3000.0f);
    dh = b.height - a.height;
    dg = b.grade - a.grade;
    dw = b.half_width - a.half_width;

    s = road_update(3001.0f);
    CHECK(nearly(s, 1001.0f, 1e-3f));
    a2 = road_sample(900.0f);
    b2 = road_sample(1000.0f);
    CHECK(nearly(b2.height - a2.height, dh, 1e-3f));
    CHECK(nearly(b2.grade - a2.grade, dg, 1e-5f));
    CHECK(nearly(b2.half_width - a2.half_width, dw, 1e-5f));

    road_reset_config(ROAD_SEED, &c);
    s = road_update(-3001.0f);
    CHECK(nearly(s, -1001.0f, 1e-3f));
    CHECK(isfinite(road_sample(s).height));
    CHECK(isfinite(road_sample(s).grade));
    CHECK(fabsf(road_sample(s).grade) <= 0.1201f);
}

static void test_road_next_narrow_and_crest(void)
{
    road_config_t c;
    float s;
    float first_narrow = -1.0f;
    float first_crest = -1.0f;
    float d, hw, rate;
    road_warning_t w;

    road_config_defaults(&c);
    road_reset_config(ROAD_SEED, &c);
    for (s = 1.0f; s < 1200.0f; s += 0.5f) {
        road_sample_t p = road_sample(s);

        if (first_narrow < 0.0f &&
            p.half_width <= ROAD_NARROW_WARN_HALF) first_narrow = s;
        if (first_crest < 0.0f &&
            p.grade_rate <= -ROAD_CREST_WARN_RATE) first_crest = s;
    }
    CHECK(first_narrow > 0.0f);
    CHECK(first_crest > 0.0f);

    CHECK(road_next_narrow(0.0f, 1200.0f, ROAD_NARROW_WARN_HALF, &d, &hw));
    CHECK(nearly(d, first_narrow, 2.5f));
    CHECK(hw <= ROAD_NARROW_WARN_HALF + 1e-4f);
    CHECK(!road_next_narrow(0.0f, first_narrow - 3.0f,
                            ROAD_NARROW_WARN_HALF, &d, &hw));

    CHECK(road_next_crest(0.0f, 400.0f, ROAD_CREST_WARN_RATE, &d, &rate));
    CHECK(nearly(d, first_crest, 2.5f));
    CHECK(rate <= -ROAD_CREST_WARN_RATE + 1e-6f);
    CHECK(!road_next_crest(0.0f, first_crest - 3.0f,
                           ROAD_CREST_WARN_RATE, &d, &rate));

    road_warnings(first_narrow - 50.0f, &w);
    CHECK(w.narrow);
    CHECK(nearly(w.narrow_distance, 50.0f, 2.5f));
    road_warnings(first_narrow - 150.0f, &w);
    CHECK(!w.narrow || w.narrow_distance > 100.0f);

    road_warnings(first_crest - 50.0f, &w);
    CHECK(w.crest);
    CHECK(nearly(w.crest_distance, 50.0f, 2.5f));
    road_warnings(first_crest - 150.0f, &w);
    CHECK(!w.crest || w.crest_distance > 100.0f);
}

static void test_slope_slows_climbs_and_speeds_descents(void)
{
    car_state_t up, flat, down;
    car_input_t gas = {true, 0.0f, 0.0f, 0.0f};
    road_sample_t r_up = flat_road(0.0f);
    road_sample_t r_flat = flat_road(0.0f);
    road_sample_t r_down = flat_road(0.0f);

    r_up.grade = 0.10f;
    r_down.grade = -0.10f;
    car_reset(&up);
    car_reset(&flat);
    car_reset(&down);
    step_for_road(&up, &gas, 2.0f, &r_up);
    step_for_road(&flat, &gas, 2.0f, &r_flat);
    step_for_road(&down, &gas, 2.0f, &r_down);
    CHECK(down.speed > flat.speed);
    CHECK(flat.speed > up.speed);
    CHECK(down.speed - up.speed > 1.5f);
}

static void test_car_width_uses_road_sample(void)
{
    car_state_t car;
    car_input_t coast = {false, 0.0f, 0.0f, 0.0f};
    road_sample_t narrow = flat_road(0.0f);

    narrow.half_width = 2.2f;
    narrow.edge_half_width = 4.2f;
    car_reset(&car);
    car.speed = 10.0f;
    car_step(&car, &coast, 1.0f / 60.0f, &narrow);
    CHECK(nearly(car.road_half_width, 2.2f, 1e-5f));
    CHECK(nearly(car.road_edge_half_width, 4.2f, 1e-5f));
    CHECK(!car_is_offroad(&car));

    car.lateral = 2.5f;
    car_step(&car, &coast, 1.0f / 60.0f, &narrow);
    CHECK(car_is_offroad(&car));

    car.lateral = 9.0f;
    car_step(&car, &coast, 1.0f / 60.0f, &narrow);
    CHECK(nearly(car.lateral, 3.8f, 1e-4f));
}

static road_sample_t crest_road(float t)
{
    road_sample_t r = flat_road(0.0f);

    if (t < ROAD_CREST_LENGTH) {
        r.grade = ROAD_GRADE_BASE -
                  (2.0f * ROAD_GRADE_BASE / ROAD_CREST_LENGTH) * t;
        r.grade_rate = -2.0f * ROAD_GRADE_BASE / ROAD_CREST_LENGTH;
    } else {
        r.grade = -ROAD_GRADE_BASE;
        r.grade_rate = 0.0f;
    }
    return r;
}

static void test_crest_launch_speed_threshold(void)
{
    car_state_t fast, slow;
    car_input_t coast = {false, 0.0f, 0.0f, 0.0f};
    road_sample_t r = crest_road(0.0f);

    car_reset(&fast);
    fast.speed = 40.0f;
    fast.steer = 0.0f;
    car_step(&fast, &coast, 1.0f / 60.0f, &r);
    CHECK(fast.airborne);
    CHECK(fast.vy > 0.0f);
    CHECK(fast.air_time == 0.0f);

    car_reset(&slow);
    slow.speed = 20.0f;
    slow.steer = 0.0f;
    car_step(&slow, &coast, 1.0f / 60.0f, &r);
    CHECK(!slow.airborne);
    CHECK(slow.air_height == 0.0f);
}

static void test_crest_flight_and_landing(void)
{
    car_state_t car;
    car_input_t coast = {false, 0.0f, 0.0f, 0.0f};
    float t = 0.0f;
    float s = 0.0f;
    float pre_land = 0.0f;
    int landed = 0;

    car_reset(&car);
    car.speed = 40.0f;
    for (int i = 0; i < 600 && !landed; i++) {
        road_sample_t r = crest_road(s);
        pre_land = car.speed;
        car_step(&car, &coast, 1.0f / 60.0f, &r);
        s += car.speed * (1.0f / 60.0f);
        t += 1.0f / 60.0f;
        if (car.airborne)
            CHECK(car.air_time > 0.0f || t < 0.05f);
        if (i > 2 && !car.airborne) {
            landed = 1;
            CHECK(car.air_time == 0.0f);
            CHECK(car.suspension < -0.05f);
            CHECK(car.speed < pre_land - 1.0f);
        }
    }
    CHECK(landed);
    CHECK(t >= 0.5f && t <= 3.0f);
    step_for(&car, &coast, 1.0f, 0.0f);
    CHECK(fabsf(car.suspension) < 0.005f);
}

static void test_airborne_steering_is_reduced(void)
{
    car_state_t air, ground;
    car_input_t full = {false, 0.0f, 1.0f, 0.0f};
    road_sample_t flat = flat_road(0.0f);

    car_reset(&air);
    air.speed = 40.0f;
    air.steer = 1.0f;
    air.airborne = true;
    air.air_height = 1.0f;
    air.vy = 4.0f;
    car_step(&air, &full, 1.0f / 60.0f, &flat);

    car_reset(&ground);
    ground.speed = 40.0f;
    ground.steer = 1.0f;
    car_step(&ground, &full, 1.0f / 60.0f, &flat);

    CHECK(air.yaw_rate < ground.yaw_rate * 0.5f);
    CHECK(air.grip_use == 0.0f);
    CHECK(fabsf(air.assist_yaw) < 1e-6f);
}

static void test_air_steering_five_percent(void)
{
    car_state_t air, ground;
    car_input_t full = {false, 0.0f, 1.0f, 0.0f};
    road_sample_t flat = flat_road(0.0f);

    CHECK(nearly(CAR_AIR_STEER, 0.05f, 1e-6f));

    car_reset(&air);
    air.speed = 40.0f;
    air.steer = 1.0f;
    air.airborne = true;
    air.air_height = 1.0f;
    air.vy = 4.0f;
    car_step(&air, &full, 1.0f / 60.0f, &flat);

    car_reset(&ground);
    ground.speed = air.speed;
    ground.steer = 1.0f;
    car_step(&ground, &full, 0.0f, &flat);

    CHECK(nearly(air.yaw_rate, ground.yaw_rate * CAR_AIR_STEER, 1e-5f));
    CHECK(air.yaw_rate < ground.yaw_rate * 0.10f);
    CHECK(air.grip_use == 0.0f);
    CHECK(fabsf(air.assist_yaw) < 1e-6f);
    CHECK(air.speed > 39.5f); /* momentum kept; only aero drag */
}

static void test_air_steering_has_no_curve_assist(void)
{
    car_state_t air;
    car_input_t full = {false, 0.0f, 1.0f, 0.0f};
    road_sample_t curve = flat_road(1.0f / 30.0f);

    car_reset(&air);
    air.speed = 10.0f;
    air.steer = 1.0f;
    air.airborne = true;
    air.air_height = 1.0f;
    air.vy = 4.0f;
    car_step(&air, &full, 1.0f / 60.0f, &curve);
    CHECK(fabsf(air.assist_yaw) < 1e-6f);
    CHECK(air.grip_use == 0.0f);
    CHECK(nearly(air.yaw_rate, full.steer * full_lock_yaw(air.speed) * 0.05f,
                 1e-5f));
}

static void test_pitch_follows_grade(void)
{
    car_state_t car;
    car_input_t coast = {false, 0.0f, 0.0f, 0.0f};
    road_sample_t r = flat_road(0.0f);

    r.grade = 0.10f;
    car_reset(&car);
    car.speed = 20.0f;
    car_step(&car, &coast, 1.0f / 60.0f, &r);
    CHECK(nearly(car.pitch, atanf(0.10f), 1e-3f));
}

static void test_boost_adds_grip_and_yaw(void)
{
    car_state_t base, boosted;
    car_input_t b0 = {false, 0.0f, 0.8f, 0.0f};
    car_input_t b1 = {false, 0.0f, 0.8f, 1.0f};
    road_sample_t r = flat_road(1.0f / 40.0f);
    float t = 0.0f;

    car_reset(&base);
    car_reset(&boosted);
    base.speed = boosted.speed = 30.0f;
    base.steer = boosted.steer = 0.8f;
    while (t < 1.0f) {
        base.speed = boosted.speed = 30.0f;
        car_step(&base, &b0, 1.0f / 60.0f, &r);
        car_step(&boosted, &b1, 1.0f / 60.0f, &r);
        t += 1.0f / 60.0f;
    }
    CHECK(boosted.yaw_rate > base.yaw_rate + 0.05f);
    CHECK(boosted.grip_use < base.grip_use);
    CHECK(nearly(base.yaw_rate, 0.803f, 0.01f));
    CHECK(nearly(boosted.yaw_rate, 0.964f, 0.01f));
    CHECK(nearly(base.grip_use, 1.147f, 0.01f));
    CHECK(nearly(boosted.grip_use, 1.051f, 0.01f));
}

static void test_throttle_and_brake_apply_together(void)
{
    car_state_t full, light, gas;
    car_input_t gas_in = {true, 0.0f, 0.0f, 0.0f};
    car_input_t full_in = {true, 1.0f, 0.0f, 0.0f};
    car_input_t light_in = {true, 0.25f, 0.0f, 0.0f};

    car_reset(&full);
    car_reset(&light);
    car_reset(&gas);
    full.speed = light.speed = gas.speed = 20.0f;
    step_for(&full, &full_in, 1.0f, 0.0f);
    step_for(&light, &light_in, 1.0f, 0.0f);
    step_for(&gas, &gas_in, 1.0f, 0.0f);
    CHECK(full.speed < gas.speed - 5.0f);
    CHECK(light.speed < gas.speed);
    CHECK(full.speed < light.speed - 5.0f);
}

int main(void)
{
    test_initial_state();
    test_throttle_accelerates();
    test_speed_is_clamped();
    test_brake_then_reverse();
    test_coasting_stops_quickly();
    test_no_steering_at_standstill();
    test_steering_moves_and_clamps();
    test_reverse_inverts_steering();
    test_dt_is_clamped();
    test_steering_is_speed_sensitive();
    test_tight_curve_runs_wide_at_speed();
    test_offroad_speed_cap();
    test_offroad_cap_is_upper_bound();
    test_offroad_can_return_to_road();
    test_hard_lateral_limit();
    test_no_spin_out();
    test_grip_demand_on_tight_curve();
    test_steady_corner_110_on_45m();
    test_steady_corner_100_on_35m();
    test_steady_corner_108_on_40m();
    test_tight_25m_holds_at_86();
    test_25m_at_100kmh_must_run_wide();
    test_slip_is_quiet_in_normal_curves();
    test_slip_latch_holds_200ms();
    test_slip_and_grip_are_retuned();
    test_near_geometry_margin();
    test_dt_is_clamped_with_curve();
    test_speed_sensitivity_terms();
    test_steer_ramp_saturates_at_12();
    test_speed_sensitivity_above_12();
    test_assist_only_while_steering();
    test_assist_direction_and_cap();
    test_assist_never_exceeds_grip();
    test_assist_off_in_reverse();
    test_yaw_rate_and_long_accel();
    test_slope_slows_climbs_and_speeds_descents();
    test_car_width_uses_road_sample();
    test_crest_launch_speed_threshold();
    test_crest_flight_and_landing();
    test_airborne_steering_is_reduced();
    test_air_steering_five_percent();
    test_air_steering_has_no_curve_assist();
    test_pitch_follows_grade();
    test_boost_adds_grip_and_yaw();
    test_throttle_and_brake_apply_together();
    test_road_start_and_back_straight();
    test_road_config_defaults_and_validate();
    test_road_map_config();
    test_road_width_cycle_length();
    test_road_default_width_spans();
    test_road_default_wide_anchor();
    test_road_uphill_chains();
    test_road_uphill_tight_gentle();
    test_road_uphill_width();
    test_road_highway_mix();
    test_road_map_determinism();
    test_lane_dash_counts();
    test_lane_dash_pattern();
    test_shoulder_width_limits();
    test_road_seed_stream();
    test_road_next_start_seed();
    test_road_width_profile();
    test_road_width_avoids_tight_curves();
    test_road_width_avoids_tight_curves_after_rebase();
    test_road_width_rebase_preserves_profile();
    test_road_width_sliders();
    test_road_vertical_run_base();
    test_road_landing_runway();
    test_road_uphill_elevation();
    test_road_highway_crests();
    test_road_elevation_bounds_and_continuity();
    test_road_crests_on_straights();
    test_road_elevation_slider();
    test_road_hill_frequency();
    test_road_height_rebase();
    test_road_next_narrow_and_crest();
    test_road_is_deterministic();
    test_road_grammar_and_mix();
    test_road_is_continuous();
    test_road_is_continuous_after_rebase();
    test_road_next_curve();
    test_road_curve_arc();
    test_road_reverse_extends_backward();
    test_road_long_drive_keeps_coordinates_bounded();
    test_road_rebase_preserves_distance();
    test_road_rebase_step_keeps_periods();

    if (failures) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    printf("all racing tests passed\n");
    return 0;
}
