/*
 * Host tests for the portable Racing 3D UI modules:
 * settings, input shaping, telemetry, minimap and menu.
 *
 * Runs with a normal C99 compiler; no PSP SDK needed.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "input.h"
#include "menu.h"
#include "minimap.h"
#include "settings.h"
#include "telemetry.h"
#include "welcome.h"

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

/* ------------------------------------------------------------------ settings */

static void put_crc(uint8_t *buf)
{
    uint16_t crc = settings_crc16(buf, 10);

    buf[10] = (uint8_t)(crc & 0xFFu);
    buf[11] = (uint8_t)((crc >> 8) & 0xFFu);
}

static void test_settings_defaults(void)
{
    racing_settings_t s;

    settings_defaults(&s);
    CHECK(s.steer_mode == RACING_STEER_DPAD);
    CHECK(nearly(s.sensitivity, 1.0f, 1e-6f));
    CHECK(s.minimap);
    CHECK(s.gmeter);
    CHECK(s.steer_bar);
    CHECK(s.analog_centre == RACING_SETTINGS_CENTRE_DEFAULT);
    CHECK(s.analog_deadzone == RACING_SETTINGS_DEADZONE_DEFAULT);
}

static void test_settings_crc16_vector(void)
{
    CHECK(settings_crc16((const uint8_t *)"123456789", 9) == 0x29B1u);
}

static void test_settings_round_trip(void)
{
    racing_settings_t a;
    racing_settings_t b;
    uint8_t buf[RACING_SETTINGS_FILE_SIZE];

    settings_defaults(&a);
    a.steer_mode = RACING_STEER_ANALOG;
    a.sensitivity = 1.3f;
    a.minimap = false;
    a.gmeter = false;
    a.steer_bar = true;
    a.analog_centre = 143;
    a.analog_deadzone = 48;

    CHECK(settings_serialize(&a, buf, (int)sizeof(buf)) ==
          RACING_SETTINGS_FILE_SIZE);
    CHECK(settings_deserialize(&b, buf, (int)sizeof(buf)));
    CHECK(b.steer_mode == RACING_STEER_ANALOG);
    CHECK(nearly(b.sensitivity, 1.3f, 1e-6f));
    CHECK(!b.minimap);
    CHECK(!b.gmeter);
    CHECK(b.steer_bar);
    CHECK(b.analog_centre == 143);
    CHECK(b.analog_deadzone == 48);
}

static void test_settings_rejects_corruption(void)
{
    racing_settings_t a;
    racing_settings_t b;
    uint8_t valid[RACING_SETTINGS_FILE_SIZE];
    uint8_t bad[RACING_SETTINGS_FILE_SIZE];
    int i;
    int bit;

    settings_defaults(&a);
    a.sensitivity = 1.2f;
    CHECK(settings_serialize(&a, valid, (int)sizeof(valid)) ==
          RACING_SETTINGS_FILE_SIZE);

    for (i = 0; i < RACING_SETTINGS_FILE_SIZE; i++) {
        for (bit = 0; bit < 8; bit++) {
            memcpy(bad, valid, sizeof(bad));
            bad[i] = (uint8_t)(bad[i] ^ (1u << bit));
            settings_defaults(&b);
            b.sensitivity = 1.4f; /* prove the failure resets it */
            CHECK(!settings_deserialize(&b, bad, (int)sizeof(bad)));
            CHECK(b.steer_mode == RACING_STEER_DPAD);
            CHECK(nearly(b.sensitivity, 1.0f, 1e-6f));
            CHECK(b.minimap && b.gmeter && b.steer_bar);
            CHECK(b.analog_centre == RACING_SETTINGS_CENTRE_DEFAULT);
            CHECK(b.analog_deadzone == RACING_SETTINGS_DEADZONE_DEFAULT);
        }
    }

    CHECK(settings_serialize(&a, valid, RACING_SETTINGS_FILE_SIZE - 1) == -1);
}

static void test_settings_rejects_unknown_flags(void)
{
    racing_settings_t a;
    racing_settings_t b;
    uint8_t buf[RACING_SETTINGS_FILE_SIZE];

    settings_defaults(&a);
    CHECK(settings_serialize(&a, buf, (int)sizeof(buf)) ==
          RACING_SETTINGS_FILE_SIZE);

    buf[5] = (uint8_t)(buf[5] | 0x10u);
    put_crc(buf);
    CHECK(!settings_deserialize(&b, buf, (int)sizeof(buf)));
    CHECK(b.steer_mode == RACING_STEER_DPAD);
    CHECK(nearly(b.sensitivity, 1.0f, 1e-6f));

    CHECK(settings_serialize(&a, buf, (int)sizeof(buf)) ==
          RACING_SETTINGS_FILE_SIZE);
    buf[5] = (uint8_t)(buf[5] | 0x80u);
    put_crc(buf);
    CHECK(!settings_deserialize(&b, buf, (int)sizeof(buf)));
    CHECK(b.steer_mode == RACING_STEER_DPAD);
}

static void test_settings_validate_clamps(void)
{
    racing_settings_t s;

    settings_defaults(&s);
    s.sensitivity = 9.0f;
    CHECK(settings_validate(&s));
    CHECK(nearly(s.sensitivity, 1.5f, 1e-6f));

    settings_defaults(&s);
    s.sensitivity = 0.0f;
    CHECK(settings_validate(&s));
    CHECK(nearly(s.sensitivity, 0.5f, 1e-6f));

    settings_defaults(&s);
    s.analog_deadzone = 0;
    CHECK(settings_validate(&s));
    CHECK(s.analog_deadzone == 8);

    settings_defaults(&s);
    s.analog_deadzone = 99;
    CHECK(settings_validate(&s));
    CHECK(s.analog_deadzone == 56);

    settings_defaults(&s);
    s.analog_centre = -5;
    CHECK(settings_validate(&s));
    CHECK(s.analog_centre == 0);

    settings_defaults(&s);
    s.analog_centre = 300;
    CHECK(settings_validate(&s));
    CHECK(s.analog_centre == 255);

    settings_defaults(&s);
    s.steer_mode = (racing_steer_mode_t)7;
    CHECK(settings_validate(&s));
    CHECK(s.steer_mode == RACING_STEER_DPAD);

    settings_defaults(&s);
    CHECK(!settings_validate(&s));
}

static void test_settings_file_is_unchanged(void)
{
    racing_settings_t s;
    uint8_t buf[32];

    settings_defaults(&s);
    CHECK(RACING_SETTINGS_FILE_SIZE == 16);
    CHECK(RACING_SETTINGS_VERSION == 1);
    CHECK(settings_serialize(&s, buf, (int)sizeof(buf)) == 16);
    CHECK(buf[4] == 1);
}

static welcome_action_t welcome_press(welcome_state_t *w, road_config_t *c,
                                      menu_input_t in)
{
    return welcome_update(w, c, &in);
}

static void test_welcome_defaults_and_navigation(void)
{
    welcome_state_t w;
    road_config_t c;
    menu_input_t down = {false, true, false, false, false, false, false, false};
    menu_input_t up = {true, false, false, false, false, false, false, false};

    welcome_reset(&w, &c);
    CHECK(w.cursor == WELCOME_ITEM_START);
    CHECK(c.map == ROAD_MAP_DEFAULT);
    CHECK(nearly(c.elevation_pct, 100.0f, 1e-4f));
    CHECK(nearly(c.hill_pct, 100.0f, 1e-4f));
    CHECK(nearly(c.width_pct, 100.0f, 1e-4f));
    CHECK(nearly(c.one_lane_width, 4.4f, 1e-4f));
    welcome_press(&w, &c, down);
    CHECK(w.cursor == WELCOME_ITEM_ELEVATION);
    welcome_press(&w, &c, up);
    CHECK(w.cursor == WELCOME_ITEM_START);
    welcome_press(&w, &c, up);
    CHECK(w.cursor == WELCOME_ITEM_MAP);
    welcome_press(&w, &c, down);
    CHECK(w.cursor == WELCOME_ITEM_START);
}

static void test_welcome_sliders_adjust(void)
{
    welcome_state_t w;
    road_config_t c;
    menu_input_t down = {false, true, false, false, false, false, false, false};
    menu_input_t right = {false, false, false, true, false, false, false, false};
    menu_input_t left = {false, false, true, false, false, false, false, false};

    welcome_reset(&w, &c);
    welcome_press(&w, &c, down);
    welcome_press(&w, &c, right);
    CHECK(nearly(c.elevation_pct, 110.0f, 1e-4f));
    for (int i = 0; i < 20; i++) welcome_press(&w, &c, right);
    CHECK(nearly(c.elevation_pct, 150.0f, 1e-4f));
    for (int i = 0; i < 20; i++) welcome_press(&w, &c, left);
    CHECK(nearly(c.elevation_pct, 50.0f, 1e-4f));

    w.cursor = WELCOME_ITEM_ONE_LANE;
    welcome_press(&w, &c, right);
    CHECK(nearly(c.one_lane_width, 4.5f, 1e-4f));
    for (int i = 0; i < 30; i++) welcome_press(&w, &c, right);
    CHECK(nearly(c.one_lane_width, 6.0f, 1e-4f));
    for (int i = 0; i < 30; i++) welcome_press(&w, &c, left);
    CHECK(nearly(c.one_lane_width, 4.0f, 1e-4f));
}

static void test_welcome_start_action(void)
{
    welcome_state_t w;
    road_config_t c;
    menu_input_t accept = {false, false, false, false, true, false, false, false};
    menu_input_t down = {false, true, false, false, false, false, false, false};

    welcome_reset(&w, &c);
    CHECK(welcome_press(&w, &c, accept) == WELCOME_ACTION_START);
    welcome_press(&w, &c, down);
    CHECK(welcome_press(&w, &c, accept) == WELCOME_ACTION_NONE);
    w.cursor = WELCOME_ITEM_MAP;
    CHECK(welcome_press(&w, &c, accept) == WELCOME_ACTION_NONE);
}

static void test_welcome_sliders_keep_values_on_restart(void)
{
    welcome_state_t w;
    road_config_t c;
    menu_input_t down = {false, true, false, false, false, false, false, false};
    menu_input_t right = {false, false, false, true, false, false, false, false};

    welcome_reset(&w, &c);
    welcome_press(&w, &c, down);
    welcome_press(&w, &c, right);
    CHECK(nearly(c.elevation_pct, 110.0f, 1e-4f));
    c.map = ROAD_MAP_HIGHWAY;
    welcome_restart(&w);
    CHECK(w.cursor == WELCOME_ITEM_START);
    CHECK(nearly(c.elevation_pct, 110.0f, 1e-4f));
    CHECK(c.map == ROAD_MAP_HIGHWAY);

    welcome_reset(&w, &c);
    CHECK(nearly(c.elevation_pct, 100.0f, 1e-4f));
    CHECK(c.map == ROAD_MAP_DEFAULT);
}

static void test_welcome_map_row(void)
{
    welcome_state_t w;
    road_config_t c;
    menu_input_t up = {true, false, false, false, false, false, false, false};
    menu_input_t right = {false, false, false, true, false, false, false, false};
    menu_input_t left = {false, false, true, false, false, false, false, false};

    welcome_reset(&w, &c);
    CHECK(w.cursor == WELCOME_ITEM_START);
    CHECK(c.map == ROAD_MAP_DEFAULT);

    welcome_press(&w, &c, up);
    CHECK(w.cursor == WELCOME_ITEM_MAP);
    welcome_press(&w, &c, right);
    CHECK(c.map == ROAD_MAP_UPHILL);
    welcome_press(&w, &c, right);
    CHECK(c.map == ROAD_MAP_HIGHWAY);
    welcome_press(&w, &c, right);
    CHECK(c.map == ROAD_MAP_DEFAULT);
    welcome_press(&w, &c, left);
    CHECK(c.map == ROAD_MAP_HIGHWAY);
}

static void test_welcome_map_restart(void)
{
    welcome_state_t w;
    road_config_t c;
    menu_input_t up = {true, false, false, false, false, false, false, false};
    menu_input_t right = {false, false, false, true, false, false, false, false};

    welcome_reset(&w, &c);
    welcome_press(&w, &c, up);
    welcome_press(&w, &c, right);
    welcome_restart(&w);
    CHECK(w.cursor == WELCOME_ITEM_START);
    CHECK(c.map == ROAD_MAP_UPHILL);

    welcome_reset(&w, &c);
    CHECK(c.map == ROAD_MAP_DEFAULT);
}

static void test_welcome_highway_disabled_sliders(void)
{
    welcome_state_t w;
    road_config_t c;
    menu_input_t right = {false, false, false, true, false, false, false, false};

    welcome_reset(&w, &c);
    c.map = ROAD_MAP_HIGHWAY;
    w.cursor = WELCOME_ITEM_WIDTH;
    welcome_press(&w, &c, right);
    CHECK(nearly(c.width_pct, 100.0f, 1e-4f));
    w.cursor = WELCOME_ITEM_ONE_LANE;
    welcome_press(&w, &c, right);
    CHECK(nearly(c.one_lane_width, 4.4f, 1e-4f));

    c.map = ROAD_MAP_DEFAULT;
    w.cursor = WELCOME_ITEM_WIDTH;
    welcome_press(&w, &c, right);
    CHECK(nearly(c.width_pct, 110.0f, 1e-4f));
}

static void test_welcome_uphill_width_slider(void)
{
    welcome_state_t w;
    road_config_t c;
    menu_input_t right = {false, false, false, true, false, false, false, false};

    welcome_reset(&w, &c);
    c.map = ROAD_MAP_UPHILL;
    w.cursor = WELCOME_ITEM_WIDTH;
    welcome_press(&w, &c, right);
    CHECK(nearly(c.width_pct, 110.0f, 1e-4f));
}

/* -------------------------------------------------------------------- input */

static float shape_steps(input_shaper_t *shaper, const racing_raw_input_t *raw,
                         const racing_settings_t *settings, int steps)
{
    float steer = 0.0f;
    int i;

    for (i = 0; i < steps; i++)
        steer = input_step(shaper, raw, settings, 1.0f / 60.0f);
    return steer;
}

static void test_analog_mapping_calibrated(void)
{
    CHECK(nearly(input_analog_to_steer(128, 128, 32), 0.0f, 1e-6f));
    CHECK(nearly(input_analog_to_steer(160, 128, 32), 0.0f, 1e-6f));
    CHECK(input_analog_to_steer(161, 128, 32) > 0.0f);
    CHECK(nearly(input_analog_to_steer(96, 128, 32), 0.0f, 1e-6f));
    CHECK(nearly(input_analog_to_steer(255, 128, 32), 1.0f, 1e-4f));
    CHECK(nearly(input_analog_to_steer(0, 128, 32), -1.0f, 1e-4f));
    CHECK(nearly(input_analog_to_steer(200, 150, 16), 34.0f / 111.0f,
                 1e-4f));
    CHECK(nearly(input_analog_to_steer(184, 128, 56), 0.0f, 1e-6f));
    CHECK(input_analog_to_steer(185, 128, 56) > 0.0f);
}

static void test_dpad_mode_ignores_nub(void)
{
    racing_settings_t settings;
    input_shaper_t shaper;
    racing_raw_input_t raw = {false, false, 255, false};
    int i;

    settings_defaults(&settings);
    input_reset(&shaper);
    for (i = 0; i < 60; i++) {
        CHECK(nearly(input_step(&shaper, &raw, &settings, 1.0f / 60.0f),
                     0.0f, 1e-6f));
        CHECK(nearly(shaper.steer, 0.0f, 1e-6f));
    }
}

static void test_dpad_ramp_and_full_lock(void)
{
    racing_settings_t settings;
    input_shaper_t shaper;
    racing_raw_input_t right = {false, true, 128, false};
    float steer;

    settings_defaults(&settings);
    input_reset(&shaper);
    steer = shape_steps(&shaper, &right, &settings, 30);
    CHECK(steer > 0.99f);

    input_reset(&shaper);
    steer = shape_steps(&shaper, &right, &settings, 6);
    CHECK(steer > 0.30f);
    CHECK(steer < 0.50f);
}

static void test_dpad_autocentre(void)
{
    racing_settings_t settings;
    input_shaper_t shaper;
    racing_raw_input_t right = {false, true, 128, false};
    racing_raw_input_t none = {false, false, 128, false};
    int i;

    settings_defaults(&settings);
    input_reset(&shaper);
    CHECK(shape_steps(&shaper, &right, &settings, 30) > 0.99f);
    for (i = 0; i < 15; i++) {
        float steer = input_step(&shaper, &none, &settings, 1.0f / 60.0f);

        CHECK(steer >= 0.0f);
    }
    CHECK(nearly(shaper.steer, 0.0f, 1e-6f));
}

static void test_dpad_tap_nudge(void)
{
    racing_settings_t settings;
    input_shaper_t shaper;
    racing_raw_input_t right = {false, true, 128, false};
    racing_raw_input_t none = {false, false, 128, false};
    float steer;

    settings_defaults(&settings);
    input_reset(&shaper);
    shape_steps(&shaper, &right, &settings, 2);
    steer = input_step(&shaper, &none, &settings, 1.0f / 60.0f);
    CHECK(steer >= 0.15f);
    CHECK(steer <= 0.35f);
    CHECK(shape_steps(&shaper, &none, &settings, 10) == 0.0f);
}

static void test_analog_mode_uses_calibrated_nub(void)
{
    racing_settings_t settings;
    input_shaper_t shaper;
    racing_raw_input_t raw = {false, false, 255, false};

    settings_defaults(&settings);
    settings.steer_mode = RACING_STEER_ANALOG;
    input_reset(&shaper);
    CHECK(shape_steps(&shaper, &raw, &settings, 1) > 0.99f);

    raw.nub_lx = 160;
    input_reset(&shaper);
    CHECK(nearly(shape_steps(&shaper, &raw, &settings, 1), 0.0f, 1e-6f));

    settings.sensitivity = 0.5f;
    raw.nub_lx = 255;
    input_reset(&shaper);
    CHECK(nearly(shape_steps(&shaper, &raw, &settings, 1), 0.5f, 1e-4f));
}

static void test_sensitivity_scales_ramp(void)
{
    racing_settings_t settings;
    input_shaper_t shaper;
    racing_raw_input_t right = {false, true, 128, false};
    float steer_05;
    float steer_15;

    settings_defaults(&settings);
    settings.sensitivity = 0.5f;
    input_reset(&shaper);
    steer_05 = shape_steps(&shaper, &right, &settings, 12);

    settings.sensitivity = 1.5f;
    input_reset(&shaper);
    steer_15 = shape_steps(&shaper, &right, &settings, 12);

    CHECK(steer_15 > steer_05);
}

static void test_input_reset(void)
{
    racing_settings_t settings;
    input_shaper_t shaper;
    racing_raw_input_t right = {false, true, 128, false};
    racing_raw_input_t none = {false, false, 128, false};

    settings_defaults(&settings);
    input_reset(&shaper);
    shape_steps(&shaper, &right, &settings, 10);
    CHECK(shaper.command > 0.0f);
    CHECK(shaper.dir == 1);
    CHECK(shaper.held > 0.0f);

    input_reset(&shaper);
    CHECK(shaper.steer == 0.0f);
    CHECK(shaper.command == 0.0f);
    CHECK(shaper.dir == 0);
    CHECK(shaper.held == 0.0f);
    CHECK(shaper.brake_level == 0.0f);
    CHECK(shaper.boost_timer == 0.0f);
    CHECK(shaper.boost_cooldown == 0.0f);
    CHECK(shaper.brake_hold == 0.0f);
    CHECK(!shaper.brake_prev);
    CHECK(!shaper.tap_armed);
    CHECK(nearly(input_step(&shaper, &none, &settings, 1.0f / 60.0f),
                 0.0f, 1e-6f));
}

static void input_brake_frames(input_shaper_t *s,
                               const racing_settings_t *settings,
                               int frames, bool brake)
{
    racing_raw_input_t raw = {false, false, 128, brake};

    for (int i = 0; i < frames; i++)
        input_step(s, &raw, settings, 1.0f / 60.0f);
}

static void test_brake_tap_boosts(void)
{
    input_shaper_t s;
    racing_settings_t settings;

    settings_defaults(&settings);
    input_reset(&s);
    input_brake_frames(&s, &settings, 6, true);
    CHECK(nearly(s.brake_level, INPUT_BRAKE_LIGHT, 1e-5f));
    input_brake_frames(&s, &settings, 1, false);
    CHECK(s.boost_timer > 0.0f);
    CHECK(nearly(s.brake_level, INPUT_BRAKE_LIGHT, 1e-5f));
    input_brake_frames(&s, &settings, 6, false);
    CHECK(s.boost_timer > 0.0f);
    input_brake_frames(&s, &settings, 12, false);
    CHECK(s.boost_timer == 0.0f);
    CHECK(s.brake_level == 0.0f);
}

static void test_brake_hold_does_not_boost(void)
{
    input_shaper_t s;
    racing_settings_t settings;

    settings_defaults(&settings);
    input_reset(&s);
    input_brake_frames(&s, &settings, 12, true);
    CHECK(nearly(s.brake_level, 1.0f, 1e-5f));
    input_brake_frames(&s, &settings, 3, true);
    CHECK(nearly(s.brake_level, 1.0f, 1e-5f));
    input_brake_frames(&s, &settings, 1, false);
    CHECK(s.boost_timer == 0.0f);
    CHECK(s.brake_level == 0.0f);
}

static void test_boost_cooldown(void)
{
    input_shaper_t s;
    racing_settings_t settings;

    settings_defaults(&settings);
    input_reset(&s);
    input_brake_frames(&s, &settings, 6, true);
    input_brake_frames(&s, &settings, 1, false);
    CHECK(s.boost_timer > 0.0f);
    input_brake_frames(&s, &settings, 12, false);
    input_brake_frames(&s, &settings, 6, true);
    input_brake_frames(&s, &settings, 1, false);
    CHECK(s.boost_timer == 0.0f);
    input_brake_frames(&s, &settings, 15, false);
    input_brake_frames(&s, &settings, 6, true);
    input_brake_frames(&s, &settings, 1, false);
    CHECK(s.boost_timer > 0.0f);
}

static void test_second_tap_keeps_existing_boost(void)
{
    input_shaper_t s;
    racing_settings_t settings;

    settings_defaults(&settings);
    input_reset(&s);
    input_brake_frames(&s, &settings, 6, true);
    input_brake_frames(&s, &settings, 1, false);
    CHECK(s.boost_timer > 0.0f);
    input_brake_frames(&s, &settings, 1, true);
    CHECK(s.boost_timer > 0.0f);
}

static void test_analog_mode_brake_tap_boosts(void)
{
    input_shaper_t s;
    racing_settings_t settings;

    settings_defaults(&settings);
    settings.steer_mode = RACING_STEER_ANALOG;
    input_reset(&s);
    input_brake_frames(&s, &settings, 6, true);
    input_brake_frames(&s, &settings, 1, false);
    CHECK(s.boost_timer > 0.0f);
    CHECK(nearly(s.brake_level, INPUT_BRAKE_LIGHT, 1e-5f));

    input_reset(&s);
    input_brake_frames(&s, &settings, 12, true);
    input_brake_frames(&s, &settings, 1, false);
    CHECK(s.boost_timer == 0.0f);
    CHECK(nearly(s.brake_level, 0.0f, 1e-5f));
}

/* ----------------------------------------------------------------- telemetry */

static void telemetry_steps(telemetry_state_t *t, const car_state_t *car,
                            int count)
{
    int i;

    for (i = 0; i < count; i++)
        telemetry_step(t, car, 1.0f / 60.0f);
}

static void test_telemetry_reset(void)
{
    telemetry_state_t t;

    telemetry_reset(&t);
    CHECK(t.lat_g == 0.0f);
    CHECK(t.lon_g == 0.0f);
    CHECK(t.peak_g == 0.0f);
    CHECK(t.distance_m == 0.0f);
    CHECK(t.max_speed == 0.0f);
}

static void test_telemetry_lateral_g(void)
{
    telemetry_state_t t;
    car_state_t car = {0};

    car.speed = 30.0f;
    car.yaw_rate = 1.0f;
    telemetry_reset(&t);
    telemetry_steps(&t, &car, 60);
    CHECK(nearly(t.lat_g, 3.059f, 0.01f));
    CHECK(nearly(t.peak_g, 3.059f, 0.01f));
    CHECK(t.lon_g == 0.0f);
}

static void test_telemetry_longitudinal_g(void)
{
    telemetry_state_t t;
    car_state_t car = {0};

    car.long_accel = 9.80665f;
    telemetry_reset(&t);
    telemetry_steps(&t, &car, 60);
    CHECK(nearly(t.lon_g, 1.0f, 0.01f));
    CHECK(nearly(t.peak_g, 1.0f, 0.01f));
}

static void test_telemetry_peak_hold(void)
{
    telemetry_state_t t;
    car_state_t car = {0};
    float lat_before;

    car.speed = 30.0f;
    car.yaw_rate = 0.65378f;
    telemetry_reset(&t);
    telemetry_steps(&t, &car, 60);
    CHECK(nearly(t.lat_g, 2.0f, 0.01f));
    CHECK(nearly(t.peak_g, 2.0f, 0.01f));

    lat_before = t.lat_g;
    car.yaw_rate = 0.0f;
    telemetry_steps(&t, &car, 60);
    CHECK(nearly(t.peak_g, 2.0f, 0.01f));

    telemetry_reset_peak(&t);
    CHECK(t.peak_g == 0.0f);
    CHECK(t.lat_g < lat_before);
}

static void test_telemetry_distance_and_max(void)
{
    telemetry_state_t t;
    car_state_t car = {0};

    telemetry_reset(&t);
    car.speed = 10.0f;
    telemetry_steps(&t, &car, 60);
    CHECK(nearly(t.distance_m, 10.0f, 0.01f));
    CHECK(nearly(t.max_speed, 10.0f, 0.01f));

    car.speed = 20.0f;
    telemetry_steps(&t, &car, 60);
    CHECK(nearly(t.max_speed, 20.0f, 0.01f));
    CHECK(nearly(t.distance_m, 30.0f, 0.01f));

    car.speed = -5.0f;
    telemetry_steps(&t, &car, 60);
    CHECK(nearly(t.max_speed, 20.0f, 0.01f));
    CHECK(nearly(t.distance_m, 35.0f, 0.01f));
}

static void test_telemetry_dt_zero(void)
{
    telemetry_state_t t;
    car_state_t car = {0};

    car.speed = 30.0f;
    car.yaw_rate = 1.0f;
    car.long_accel = 9.80665f;
    telemetry_reset(&t);
    telemetry_step(&t, &car, 1.0f / 60.0f);
    {
        float lat = t.lat_g;
        float lon = t.lon_g;
        float peak = t.peak_g;
        float distance = t.distance_m;
        float max_speed = t.max_speed;

        telemetry_step(&t, &car, 0.0f);
        CHECK(t.lat_g == lat);
        CHECK(t.lon_g == lon);
        CHECK(t.peak_g == peak);
        CHECK(t.distance_m == distance);
        CHECK(t.max_speed == max_speed);
    }
}

/* ------------------------------------------------------------------- minimap */

static void test_minimap_projection(void)
{
    car_state_t car = {0};
    minimap_strip_t map;
    road_sample_t p0;
    float width;
    int i;

    road_reset(ROAD_SEED);
    car.s = 50.0f;
    car.lateral = 0.0f;
    minimap_build(&car, 366.0f, 132.0f, 466.0f, 218.0f, &map);
    p0 = road_sample(car.s - MINIMAP_BEHIND);

    CHECK(map.count == MINIMAP_SAMPLES);
    CHECK(nearly(map.car_x, 416.0f, 1e-4f));
    CHECK(nearly(map.car_y, 210.0f, 1e-4f));
    CHECK(map.left_y[0] > map.car_y);
    CHECK(map.left_y[MINIMAP_SAMPLES - 1] < map.car_y);
    CHECK(map.left_x[0] < map.car_x);
    CHECK(map.right_x[0] > map.car_x);

    width = map.right_x[0] - map.left_x[0];
    CHECK(nearly(width, 2.0f * p0.edge_half_width * 50.0f / 12.0f, 0.5f));

    for (i = 0; i < map.count; i++) {
        CHECK(map.left_x[i] >= 358.0f && map.left_x[i] <= 474.0f);
        CHECK(map.left_y[i] >= 124.0f && map.left_y[i] <= 226.0f);
        CHECK(map.right_x[i] >= 358.0f && map.right_x[i] <= 474.0f);
        CHECK(map.right_y[i] >= 124.0f && map.right_y[i] <= 226.0f);
    }
}

static void test_minimap_lateral_shift(void)
{
    car_state_t car = {0};
    minimap_strip_t centred;
    minimap_strip_t shifted;
    float centre_0;
    float centre_3;

    road_reset(ROAD_SEED);
    car.s = 50.0f;
    car.lateral = 0.0f;
    minimap_build(&car, 366.0f, 132.0f, 466.0f, 218.0f, &centred);

    car.lateral = 3.0f;
    minimap_build(&car, 366.0f, 132.0f, 466.0f, 218.0f, &shifted);

    centre_0 = (centred.left_x[0] + centred.right_x[0]) * 0.5f;
    centre_3 = (shifted.left_x[0] + shifted.right_x[0]) * 0.5f;
    CHECK(nearly(centre_3 - centre_0, -3.0f * 50.0f / 12.0f, 1.0f));
}

static void test_minimap_outline_counts(void)
{
    CHECK(MINIMAP_SAMPLES == 24);
    CHECK(MINIMAP_CONTOUR_POINTS == 48);
    CHECK(MINIMAP_OUTLINE_SEGMENTS == 48);
    CHECK(MINIMAP_OUTLINE_VERTS == 288);
    /* 288 outline + 3 marker + 3 chevron must fit MAX_MAP_VERTS (512). */
    CHECK(MINIMAP_OUTLINE_VERTS + 6 == 294);
    CHECK(MINIMAP_OUTLINE_VERTS + 6 <= 512);
}

static void test_minimap_outline_closed(void)
{
    car_state_t car = {0};
    minimap_strip_t map;
    minimap_point_t pts[MINIMAP_CONTOUR_POINTS];
    int i;

    road_reset(ROAD_SEED);
    car.s = 50.0f;
    minimap_build(&car, 366.0f, 132.0f, 466.0f, 218.0f, &map);
    minimap_contour(&map, pts);

    for (i = 0; i < MINIMAP_SAMPLES; i++) {
        CHECK(nearly(pts[i].x, map.left_x[i], 1e-4f));
        CHECK(nearly(pts[i].y, map.left_y[i], 1e-4f));
        CHECK(nearly(pts[MINIMAP_SAMPLES + i].x,
                     map.right_x[MINIMAP_SAMPLES - 1 - i], 1e-4f));
        CHECK(nearly(pts[MINIMAP_SAMPLES + i].y,
                     map.right_y[MINIMAP_SAMPLES - 1 - i], 1e-4f));
    }
    /* Far cap: pts[23] (left, 120 m ahead) -> pts[24] (right, 120 m ahead). */
    CHECK(pts[24].x - pts[23].x > 0.0f);
    /* Near cap closes the loop: pts[47] -> pts[0], both 12 m behind the car. */
    CHECK(nearly(pts[0].y, pts[MINIMAP_CONTOUR_POINTS - 1].y, 0.01f));
    CHECK(pts[0].x < pts[MINIMAP_CONTOUR_POINTS - 1].x);
    CHECK(nearly(pts[0].y, 217.80f, 0.01f));
}

static void test_minimap_outline_thickness(void)
{
    car_state_t car = {0};
    minimap_strip_t map;
    minimap_point_t verts[MINIMAP_OUTLINE_VERTS];
    float min_x, max_x, min_y, max_y;
    int i;

    road_reset(ROAD_SEED);
    car.s = 50.0f;
    minimap_build(&car, 366.0f, 132.0f, 466.0f, 218.0f, &map);
    CHECK(minimap_outline(&map, MINIMAP_EDGE_THICKNESS, verts) ==
          MINIMAP_OUTLINE_VERTS);
    CHECK(MINIMAP_EDGE_THICKNESS == 2.0f);

    /* Quad 0: first left-edge segment (pts[0] -> pts[1]), vertical. */
    min_x = max_x = verts[0].x;
    min_y = max_y = verts[0].y;
    for (i = 1; i < 6; i++) {
        if (verts[i].x < min_x) min_x = verts[i].x;
        if (verts[i].x > max_x) max_x = verts[i].x;
        if (verts[i].y < min_y) min_y = verts[i].y;
        if (verts[i].y > max_y) max_y = verts[i].y;
    }
    CHECK(nearly(max_x - min_x, 2.0f, 0.01f));            /* thickness */
    CHECK(nearly(min_x, 375.42f, 0.05f));                 /* centred on 376.42 */
    CHECK(nearly(max_y - min_y, 3.7333f + 2.0f, 0.02f));  /* len + 1 px/end */
    CHECK(nearly((min_y + max_y) * 0.5f, 215.935f, 0.01f));

    /* Quad 47: near cap pts[47] -> pts[0], horizontal, 79.17 px long. */
    min_x = max_x = verts[282].x;
    min_y = max_y = verts[282].y;
    for (i = 283; i < 288; i++) {
        if (verts[i].x < min_x) min_x = verts[i].x;
        if (verts[i].x > max_x) max_x = verts[i].x;
        if (verts[i].y < min_y) min_y = verts[i].y;
        if (verts[i].y > max_y) max_y = verts[i].y;
    }
    CHECK(nearly(max_x - min_x, 79.1667f + 2.0f, 0.02f));
    CHECK(nearly(max_y - min_y, 2.0f, 0.01f));

    for (i = 0; i < MINIMAP_OUTLINE_VERTS; i++) {
        /*
         * NaN-free and inside the projection clamp (358..474 / 124..226)
         * plus half a thickness along u and n: the exact worst case is
         * clamp -+ thickness/2 * sqrt(2) = 356.586..475.414, measured over
         * 1547 car poses; 356/476 leaves a 0.586 px margin.
         */
        CHECK(verts[i].x == verts[i].x && verts[i].y == verts[i].y);
        CHECK(verts[i].x >= 356.0f && verts[i].x <= 476.0f);
        CHECK(verts[i].y >= 122.0f && verts[i].y <= 228.0f);
    }
}

static void test_minimap_grammar_width(void)
{
    car_state_t car = {0};
    minimap_strip_t def;
    minimap_strip_t hw;
    road_config_t c;
    float def_w;
    float hw_w;

    road_config_defaults(&c);
    road_reset_config(ROAD_SEED, &c);
    car.s = 50.0f;
    minimap_build(&car, 366.0f, 132.0f, 466.0f, 218.0f, &def);

    c.map = ROAD_MAP_HIGHWAY;
    road_reset_config(ROAD_SEED, &c);
    minimap_build(&car, 366.0f, 132.0f, 466.0f, 218.0f, &hw);

    def_w = def.right_x[0] - def.left_x[0];
    hw_w = hw.right_x[0] - hw.left_x[0];
    CHECK(nearly(def_w, 79.1667f, 0.5f));   /* 2-lane, edge 9.5 m */
    CHECK(nearly(hw_w, 104.1667f, 0.5f));   /* 4-lane, edge 12.5 m */
    CHECK(nearly(hw_w - def_w, 25.0f, 0.5f));
    CHECK(hw_w > def_w);
}

static void test_minimap_variable_width(void)
{
    car_state_t car = {0};
    minimap_strip_t map;
    road_config_t c;
    road_sample_t p0;
    float width;

    road_config_defaults(&c);
    road_reset_config(ROAD_SEED, &c);
    car.s = 50.0f;
    minimap_build(&car, 366.0f, 132.0f, 466.0f, 218.0f, &map);
    p0 = road_sample(car.s - MINIMAP_BEHIND);
    width = map.right_x[0] - map.left_x[0];
    CHECK(nearly(width, 2.0f * p0.edge_half_width * 50.0f / 12.0f, 0.5f));
}

/* ---------------------------------------------------------------------- menu */

static menu_action_t menu_press(menu_state_t *m, racing_settings_t *s,
                                menu_input_t in)
{
    return menu_update(m, s, &in, 143, 128);
}

static void test_menu_start_opens_and_closes(void)
{
    menu_state_t m;
    racing_settings_t s;
    menu_input_t start = {false, false, false, false, false, false, false,
                          true};
    menu_input_t select = {false, false, false, false, false, false, true,
                           false};

    menu_reset(&m);
    settings_defaults(&s);
    CHECK(menu_press(&m, &s, start) == MENU_ACTION_EXIT);
    CHECK(!m.open);

    CHECK(menu_press(&m, &s, select) == MENU_ACTION_NONE);
    CHECK(m.open);
    CHECK(menu_press(&m, &s, start) == MENU_ACTION_CLOSE);
    CHECK(!m.open);

    CHECK(menu_press(&m, &s, select) == MENU_ACTION_NONE);
    CHECK(m.open);
    CHECK(menu_press(&m, &s, select) == MENU_ACTION_CLOSE);
    CHECK(!m.open);
}

static void test_menu_navigation_wraps(void)
{
    menu_state_t m;
    racing_settings_t s;
    menu_input_t select = {false, false, false, false, false, false, true,
                           false};
    menu_input_t down = {false, true, false, false, false, false, false,
                         false};
    menu_input_t up = {true, false, false, false, false, false, false, false};

    menu_reset(&m);
    settings_defaults(&s);
    menu_press(&m, &s, select);
    m.cursor = MENU_ITEM_COUNT - 1;
    menu_press(&m, &s, down);
    CHECK(m.cursor == 0);
    menu_press(&m, &s, up);
    CHECK(m.cursor == MENU_ITEM_COUNT - 1);
}

static void test_menu_sensitivity(void)
{
    menu_state_t m;
    racing_settings_t s;
    menu_input_t select = {false, false, false, false, false, false, true,
                           false};
    menu_input_t down = {false, true, false, false, false, false, false,
                         false};
    menu_input_t right = {false, false, false, true, false, false, false,
                          false};
    menu_input_t left = {false, false, true, false, false, false, false,
                         false};
    int i;

    menu_reset(&m);
    settings_defaults(&s);
    menu_press(&m, &s, select);
    menu_press(&m, &s, down);
    CHECK(m.cursor == MENU_ITEM_SENSITIVITY);

    CHECK(menu_press(&m, &s, right) == MENU_ACTION_SAVE);
    CHECK(nearly(s.sensitivity, 1.1f, 1e-5f));
    for (i = 0; i < 10; i++)
        menu_press(&m, &s, right);
    CHECK(nearly(s.sensitivity, 1.5f, 1e-5f));
    for (i = 0; i < 20; i++)
        menu_press(&m, &s, left);
    CHECK(nearly(s.sensitivity, 0.5f, 1e-5f));
}

static void test_menu_toggles(void)
{
    menu_state_t m;
    racing_settings_t s;
    menu_input_t select = {false, false, false, false, false, false, true,
                           false};
    menu_input_t right = {false, false, false, true, false, false, false,
                          false};
    menu_input_t left = {false, false, true, false, false, false, false,
                         false};
    menu_input_t accept = {false, false, false, false, true, false, false,
                           false};
    int item;

    menu_reset(&m);
    settings_defaults(&s);
    menu_press(&m, &s, select);
    for (item = MENU_ITEM_MINIMAP; item <= MENU_ITEM_STEER_BAR; item++) {
        m.cursor = item;
        CHECK(menu_press(&m, &s, left) == MENU_ACTION_SAVE);
        CHECK(item == MENU_ITEM_MINIMAP ? !s.minimap
                                        : item == MENU_ITEM_GMETER
                                              ? !s.gmeter
                                              : !s.steer_bar);
        CHECK(menu_press(&m, &s, right) == MENU_ACTION_SAVE);
        CHECK(item == MENU_ITEM_MINIMAP ? s.minimap
                                        : item == MENU_ITEM_GMETER
                                              ? s.gmeter
                                              : s.steer_bar);
        CHECK(menu_press(&m, &s, accept) == MENU_ACTION_SAVE);
        CHECK(item == MENU_ITEM_MINIMAP ? !s.minimap
                                        : item == MENU_ITEM_GMETER
                                              ? !s.gmeter
                                              : !s.steer_bar);
    }
}

static void test_menu_mode_enters_calibration(void)
{
    menu_state_t m;
    racing_settings_t s;
    menu_input_t select = {false, false, false, false, false, false, true,
                           false};
    menu_input_t right = {false, false, false, true, false, false, false,
                          false};
    menu_input_t left = {false, false, true, false, false, false, false,
                         false};
    menu_input_t cancel = {false, false, false, false, false, true, false,
                           false};

    menu_reset(&m);
    settings_defaults(&s);
    menu_press(&m, &s, select);
    CHECK(menu_press(&m, &s, right) == MENU_ACTION_SAVE);
    CHECK(s.steer_mode == RACING_STEER_ANALOG);
    CHECK(m.screen == MENU_SCREEN_CALIBRATE);

    CHECK(menu_press(&m, &s, cancel) == MENU_ACTION_NONE);
    CHECK(m.screen == MENU_SCREEN_MAIN);
    CHECK(s.steer_mode == RACING_STEER_ANALOG);

    CHECK(menu_press(&m, &s, left) == MENU_ACTION_SAVE);
    CHECK(s.steer_mode == RACING_STEER_DPAD);
}

static void test_menu_calibration_capture(void)
{
    menu_state_t m;
    racing_settings_t s;
    menu_input_t select = {false, false, false, false, false, false, true,
                           false};
    menu_input_t right = {false, false, false, true, false, false, false,
                          false};
    menu_input_t left = {false, false, true, false, false, false, false,
                         false};
    menu_input_t accept = {false, false, false, false, true, false, false,
                           false};
    int i;

    menu_reset(&m);
    settings_defaults(&s);
    menu_press(&m, &s, select);
    menu_press(&m, &s, right); /* enters calibration */

    CHECK(menu_press(&m, &s, accept) == MENU_ACTION_SAVE);
    CHECK(s.analog_centre == 143);
    CHECK(menu_press(&m, &s, left) == MENU_ACTION_SAVE);
    CHECK(s.analog_deadzone == 24);
    for (i = 0; i < 10; i++)
        menu_press(&m, &s, left);
    CHECK(s.analog_deadzone == 8);
    for (i = 0; i < 10; i++)
        menu_press(&m, &s, right);
    CHECK(s.analog_deadzone == 56);
}

static void test_menu_reset_peak_and_resume(void)
{
    menu_state_t m;
    racing_settings_t s;
    menu_input_t select = {false, false, false, false, false, false, true,
                           false};
    menu_input_t accept = {false, false, false, false, true, false, false,
                           false};

    menu_reset(&m);
    settings_defaults(&s);
    menu_press(&m, &s, select);

    m.cursor = MENU_ITEM_RESET_PEAK;
    CHECK(menu_press(&m, &s, accept) == MENU_ACTION_RESET_PEAK);
    CHECK(m.open);

    m.cursor = MENU_ITEM_RESUME;
    CHECK(menu_press(&m, &s, accept) == MENU_ACTION_CLOSE);
    CHECK(!m.open);
}

static void test_menu_restart(void)
{
    menu_state_t m;
    racing_settings_t s;
    menu_input_t select = {false, false, false, false, false, false, true, false};
    menu_input_t accept = {false, false, false, false, true, false, false, false};

    menu_reset(&m);
    settings_defaults(&s);
    menu_press(&m, &s, select);
    m.cursor = MENU_ITEM_RESTART;
    CHECK(menu_press(&m, &s, accept) == MENU_ACTION_RESTART);
    CHECK(!m.open);
    CHECK(m.screen == MENU_SCREEN_MAIN);
    CHECK(MENU_ITEM_CALIBRATE == 5);
}

int main(void)
{
    test_settings_defaults();
    test_settings_crc16_vector();
    test_settings_round_trip();
    test_settings_rejects_corruption();
    test_settings_rejects_unknown_flags();
    test_settings_validate_clamps();
    test_settings_file_is_unchanged();

    test_welcome_defaults_and_navigation();
    test_welcome_sliders_adjust();
    test_welcome_start_action();
    test_welcome_sliders_keep_values_on_restart();
    test_welcome_map_row();
    test_welcome_map_restart();
    test_welcome_highway_disabled_sliders();
    test_welcome_uphill_width_slider();

    test_analog_mapping_calibrated();
    test_dpad_mode_ignores_nub();
    test_dpad_ramp_and_full_lock();
    test_dpad_autocentre();
    test_dpad_tap_nudge();
    test_analog_mode_uses_calibrated_nub();
    test_sensitivity_scales_ramp();
    test_input_reset();
    test_brake_tap_boosts();
    test_brake_hold_does_not_boost();
    test_boost_cooldown();
    test_second_tap_keeps_existing_boost();
    test_analog_mode_brake_tap_boosts();

    test_telemetry_reset();
    test_telemetry_lateral_g();
    test_telemetry_longitudinal_g();
    test_telemetry_peak_hold();
    test_telemetry_distance_and_max();
    test_telemetry_dt_zero();

    test_minimap_projection();
    test_minimap_lateral_shift();
    test_minimap_variable_width();
    test_minimap_outline_counts();
    test_minimap_outline_closed();
    test_minimap_outline_thickness();
    test_minimap_grammar_width();

    test_menu_start_opens_and_closes();
    test_menu_navigation_wraps();
    test_menu_sensitivity();
    test_menu_toggles();
    test_menu_mode_enters_calibration();
    test_menu_calibration_capture();
    test_menu_reset_peak_and_resume();
    test_menu_restart();

    if (failures) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    printf("all racing UI tests passed\n");
    return 0;
}
