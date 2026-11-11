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

#include "hiscore.h"
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

/* A hand-built v1 file: version 1, byte 9 reserved (0) or deliberately set. */
static void build_v1(uint8_t *buf, uint8_t byte9)
{
    memset(buf, 0, RACING_SETTINGS_FILE_SIZE);
    memcpy(buf, "R3DS", 4);
    buf[4] = 1;
    buf[5] = 0x07u;
    buf[6] = 110;
    buf[7] = 32;
    buf[8] = 128;
    buf[9] = byte9;
    put_crc(buf);
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

/* G1: RACING.CFG v2 carries the sign scale in the old reserved byte 9. */
static void test_settings_defaults_v2(void)
{
    racing_settings_t s;

    settings_defaults(&s);
    CHECK(RACING_SETTINGS_VERSION == 2);
    CHECK(RACING_SETTINGS_SIGN_SCALE_MIN == 60);
    CHECK(RACING_SETTINGS_SIGN_SCALE_MAX == 160);
    CHECK(RACING_SETTINGS_SIGN_SCALE_DEFAULT == 100);
    CHECK(s.sign_scale_pct == RACING_SETTINGS_SIGN_SCALE_DEFAULT);
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
    a.sign_scale_pct = 130;

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
    CHECK(b.sign_scale_pct == 130);
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
            b.sign_scale_pct = 160;
            CHECK(!settings_deserialize(&b, bad, (int)sizeof(bad)));
            CHECK(b.steer_mode == RACING_STEER_DPAD);
            CHECK(nearly(b.sensitivity, 1.0f, 1e-6f));
            CHECK(b.minimap && b.gmeter && b.steer_bar);
            CHECK(b.analog_centre == RACING_SETTINGS_CENTRE_DEFAULT);
            CHECK(b.analog_deadzone == RACING_SETTINGS_DEADZONE_DEFAULT);
            CHECK(b.sign_scale_pct == RACING_SETTINGS_SIGN_SCALE_DEFAULT);
        }
    }

    CHECK(settings_serialize(&a, valid, RACING_SETTINGS_FILE_SIZE - 1) == -1);
}

/* G1: version 2 writes the sign scale into byte 9, CRC over bytes 0..9. */
static void test_settings_v2_round_trip(void)
{
    racing_settings_t a;
    racing_settings_t b;
    uint8_t buf[RACING_SETTINGS_FILE_SIZE];
    int i;

    settings_defaults(&a);
    a.sign_scale_pct = 160;
    CHECK(settings_serialize(&a, buf, (int)sizeof(buf)) ==
          RACING_SETTINGS_FILE_SIZE);
    CHECK(buf[4] == 2);
    CHECK(buf[9] == 160);
    for (i = 12; i < RACING_SETTINGS_FILE_SIZE; i++)
        CHECK(buf[i] == 0);
    CHECK(settings_deserialize(&b, buf, (int)sizeof(buf)));
    CHECK(b.sign_scale_pct == 160);

    /* byte 9 is inside the CRC: the stored value must match bytes 0..9. */
    CHECK((buf[10] | (buf[11] << 8)) == settings_crc16(buf, 10));
}

/* G1: a v1 file still loads; its reserved byte 9 must be 0. */
static void test_settings_v1_still_loads(void)
{
    racing_settings_t b;
    uint8_t buf[RACING_SETTINGS_FILE_SIZE];

    build_v1(buf, 0);
    CHECK(settings_deserialize(&b, buf, (int)sizeof(buf)));
    CHECK(b.sign_scale_pct == RACING_SETTINGS_SIGN_SCALE_DEFAULT);
    CHECK(nearly(b.sensitivity, 1.1f, 1e-6f));

    /* A v1 file with the reserved byte set is not a v1 file. */
    build_v1(buf, 40);
    settings_defaults(&b);
    b.sign_scale_pct = 160;
    CHECK(!settings_deserialize(&b, buf, (int)sizeof(buf)));
    CHECK(b.sign_scale_pct == RACING_SETTINGS_SIGN_SCALE_DEFAULT);
}

/* G1: v2 rejects an out-of-range scale and an unknown version. */
static void test_settings_v2_rejects_bad_scale(void)
{
    racing_settings_t a;
    racing_settings_t b;
    uint8_t buf[RACING_SETTINGS_FILE_SIZE];

    settings_defaults(&a);
    CHECK(settings_serialize(&a, buf, (int)sizeof(buf)) ==
          RACING_SETTINGS_FILE_SIZE);

    buf[9] = 59;
    put_crc(buf);
    settings_defaults(&b);
    b.sign_scale_pct = 160;
    CHECK(!settings_deserialize(&b, buf, (int)sizeof(buf)));
    CHECK(b.sign_scale_pct == RACING_SETTINGS_SIGN_SCALE_DEFAULT);

    buf[9] = 161;
    put_crc(buf);
    b.sign_scale_pct = 160;
    CHECK(!settings_deserialize(&b, buf, (int)sizeof(buf)));
    CHECK(b.sign_scale_pct == RACING_SETTINGS_SIGN_SCALE_DEFAULT);

    buf[9] = 100;
    buf[4] = 3;
    put_crc(buf);
    b.sign_scale_pct = 160;
    CHECK(!settings_deserialize(&b, buf, (int)sizeof(buf)));
    CHECK(b.sign_scale_pct == RACING_SETTINGS_SIGN_SCALE_DEFAULT);
}

/* G1: serialize clamps byte 9, so the game can never write a file its own
 * deserializer rejects (a validate-only clamp is not enough). */
static void test_settings_serialize_clamps_sign_scale(void)
{
    racing_settings_t a;
    racing_settings_t b;
    uint8_t buf[RACING_SETTINGS_FILE_SIZE];

    settings_defaults(&a);
    a.sign_scale_pct = 999;
    CHECK(settings_serialize(&a, buf, (int)sizeof(buf)) ==
          RACING_SETTINGS_FILE_SIZE);
    CHECK(buf[9] == RACING_SETTINGS_SIGN_SCALE_MAX);
    CHECK(settings_deserialize(&b, buf, (int)sizeof(buf)));
    CHECK(b.sign_scale_pct == RACING_SETTINGS_SIGN_SCALE_MAX);

    a.sign_scale_pct = 0;
    CHECK(settings_serialize(&a, buf, (int)sizeof(buf)) ==
          RACING_SETTINGS_FILE_SIZE);
    CHECK(buf[9] == RACING_SETTINGS_SIGN_SCALE_MIN);
    CHECK(settings_deserialize(&b, buf, (int)sizeof(buf)));
    CHECK(b.sign_scale_pct == RACING_SETTINGS_SIGN_SCALE_MIN);
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
    s.sign_scale_pct = 50;
    CHECK(settings_validate(&s));
    CHECK(s.sign_scale_pct == RACING_SETTINGS_SIGN_SCALE_MIN);

    settings_defaults(&s);
    s.sign_scale_pct = 200;
    CHECK(settings_validate(&s));
    CHECK(s.sign_scale_pct == RACING_SETTINGS_SIGN_SCALE_MAX);

    settings_defaults(&s);
    CHECK(!settings_validate(&s));
}

static void test_settings_file_is_v2_and_16_bytes(void)
{
    racing_settings_t s;
    uint8_t buf[32];

    settings_defaults(&s);
    CHECK(RACING_SETTINGS_FILE_SIZE == 16);
    CHECK(RACING_SETTINGS_VERSION == 2);
    CHECK(settings_serialize(&s, buf, (int)sizeof(buf)) == 16);
    CHECK(buf[4] == 2);
}

static welcome_action_t welcome_press_book(welcome_state_t *w,
                                           road_config_t *c,
                                           const hiscore_book_t *book,
                                           menu_input_t in)
{
    return welcome_update(w, c, book, &in);
}

static welcome_action_t welcome_press(welcome_state_t *w, road_config_t *c,
                                      menu_input_t in)
{
    hiscore_book_t book;

    hiscore_clear(&book);
    return welcome_press_book(w, c, &book, in);
}

static void test_welcome_defaults_and_navigation(void)
{
    welcome_state_t w;
    road_config_t c;
    menu_input_t down = {false, true, false, false, false, false, false, false, false, false};
    menu_input_t up = {true, false, false, false, false, false, false, false, false, false};

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
    menu_input_t down = {false, true, false, false, false, false, false, false, false, false};
    menu_input_t right = {false, false, false, true, false, false, false, false, false, false};
    menu_input_t left = {false, false, true, false, false, false, false, false, false, false};

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
    menu_input_t accept = {false, false, false, false, true, false, false, false, false, false};
    menu_input_t down = {false, true, false, false, false, false, false, false, false, false};

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
    menu_input_t down = {false, true, false, false, false, false, false, false, false, false};
    menu_input_t right = {false, false, false, true, false, false, false, false, false, false};

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
    menu_input_t up = {true, false, false, false, false, false, false, false, false, false};
    menu_input_t right = {false, false, false, true, false, false, false, false, false, false};
    menu_input_t left = {false, false, true, false, false, false, false, false, false, false};

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
    menu_input_t up = {true, false, false, false, false, false, false, false, false, false};
    menu_input_t right = {false, false, false, true, false, false, false, false, false, false};

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
    menu_input_t right = {false, false, false, true, false, false, false, false, false, false};

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
    menu_input_t right = {false, false, false, true, false, false, false, false, false, false};

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
    minimap_build(&car, 366.0f, 132.0f, 466.0f, 218.0f, 360.0f, 118.0f,
                  472.0f, 224.0f, &map);
    p0 = road_sample(car.s - MINIMAP_BEHIND);

    CHECK(map.count == MINIMAP_SAMPLES);
    CHECK(nearly(map.car_x, 416.0f, 1e-4f));
    /* the panel rect (360..472) and the map rect (366..466) share a centre */
    CHECK(nearly(map.panel_cx, 416.0f, 1e-4f));
    CHECK(nearly(map.car_y, 210.0f, 1e-4f));
    CHECK(map.left_y[0] > map.car_y);
    CHECK(map.left_y[MINIMAP_SAMPLES - 1] < map.car_y);
    CHECK(map.left_x[0] < map.car_x);
    CHECK(map.right_x[0] > map.car_x);

    width = map.right_x[0] - map.left_x[0];
    CHECK(nearly(width, 2.0f * p0.edge_half_width * 50.0f / MINIMAP_LATERAL,
                 0.5f));

    /* M-9: the clamp is the PANEL rect (360,118,472,224), not map +- 8. */
    for (i = 0; i < map.count; i++) {
        CHECK(map.left_x[i] >= 360.0f && map.left_x[i] <= 472.0f);
        CHECK(map.left_y[i] >= 118.0f && map.left_y[i] <= 224.0f);
        CHECK(map.right_x[i] >= 360.0f && map.right_x[i] <= 472.0f);
        CHECK(map.right_y[i] >= 118.0f && map.right_y[i] <= 224.0f);
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
    minimap_build(&car, 366.0f, 132.0f, 466.0f, 218.0f, 360.0f, 118.0f,
                  472.0f, 224.0f, &centred);

    car.lateral = 3.0f;
    minimap_build(&car, 366.0f, 132.0f, 466.0f, 218.0f, 360.0f, 118.0f,
                  472.0f, 224.0f, &shifted);

    centre_0 = (centred.left_x[0] + centred.right_x[0]) * 0.5f;
    centre_3 = (shifted.left_x[0] + shifted.right_x[0]) * 0.5f;
    /* 3 m = 0.9375 px at sx = 50/160; the old 1.0 px tolerance would pass a
     * map that did not shift at all. */
    CHECK(nearly(centre_3 - centre_0, -3.0f * 50.0f / MINIMAP_LATERAL, 1e-3f));
}

static void test_minimap_ribbon_counts(void)
{
    CHECK(MINIMAP_AHEAD == 250.0f);
    CHECK(MINIMAP_SAMPLES == 24);
    CHECK(MINIMAP_VERTS == 48);
    CHECK(MINIMAP_WARN_AHEAD == 120.0f);
    CHECK(MINIMAP_MARKER_VERTS == 3); /* A1 owns all three macros */
    CHECK(MINIMAP_CHEVRON_VERTS == 3);
    CHECK(MINIMAP_VERTS + MINIMAP_MARKER_VERTS + MINIMAP_CHEVRON_VERTS == 54);
    CHECK(MINIMAP_VERTS + 6 <= 64);
}

static void test_minimap_widths_at_the_new_scale(void)
{
    /* H-4: the new MINIMAP_LATERAL breaks two existing pins. With sx = 0.3125
     * the 2-lane ribbon is 5.9375 px and the 4-lane one 7.8125 px (the old
     * values were 79.1667 / 104.1667 at 50/12). */
    CHECK(nearly(2.0f * 9.5f * 50.0f / MINIMAP_LATERAL, 5.9375f, 1e-4f));
    CHECK(nearly(2.0f * 12.5f * 50.0f / MINIMAP_LATERAL, 7.8125f, 1e-4f));
}

static void test_minimap_isotropic(void)
{
    /* sx and sy must agree within 1 % or the map is not a map */
    CHECK(nearly(50.0f / MINIMAP_LATERAL, 78.0f / MINIMAP_AHEAD, 0.002f));
}

static void test_minimap_min_width_documented_poses(void)
{
    static const road_map_t maps[] = {ROAD_MAP_DEFAULT, ROAD_MAP_UPHILL,
                                      ROAD_MAP_HIGHWAY};
    static const float poses[] = {20.0f, 60.0f, 120.0f};
    car_state_t car = {0};
    minimap_strip_t strip;
    road_config_t cfg;
    int m, k, i;

    /* measured: DEFAULT 4.70, HIGHWAY 6.19, UPHILL 2.55 px at these poses, so
     * the 1.5 px floor is inert here and this asserts the raw projection */
    for (m = 0; m < 3; m++) {
        for (k = 0; k < 3; k++) {
            road_config_defaults(&cfg);
            cfg.map = maps[m];
            road_reset_config(ROAD_SEED, &cfg);
            car.s = poses[k];
            minimap_build(&car, 366.0f, 132.0f, 466.0f, 218.0f,
                          360.0f, 118.0f, 472.0f, 224.0f, &strip);
            for (i = 0; i < MINIMAP_SAMPLES; i++) {
                CHECK(fabsf(strip.right_x[i] - strip.left_x[i]) >= 2.0f);
                CHECK(strip.left_x[i] >= 360.0f && strip.left_x[i] <= 472.0f);
                CHECK(strip.left_y[i] >= 118.0f && strip.left_y[i] <= 224.0f);
            }
        }
    }
}

static void test_minimap_min_width_u_turn(void)
{
    /* M-8: the WORST measured pose is UPHILL 170 (0.000 px with the shipped
     * panel clamp; 0.065 px unclamped, 16 of 24 samples below 1.5 px). 200 is
     * 0.000 px too (0.565 unclamped) but is where the edges swap (14 pairs).
     * Test both: the floor at 170, the orientation at 200. */
    static const float poses[] = {170.0f, 200.0f};
    car_state_t car = {0};
    minimap_strip_t strip;
    minimap_point_t v[MINIMAP_VERTS];
    road_config_t cfg;
    int k, i;
    int swapped_at_200 = 0;

    road_config_defaults(&cfg);
    cfg.map = ROAD_MAP_UPHILL;
    for (k = 0; k < 2; k++) {
        road_reset_config(ROAD_SEED, &cfg);
        car.s = poses[k];
        minimap_build(&car, 366.0f, 132.0f, 466.0f, 218.0f,
                      360.0f, 118.0f, 472.0f, 224.0f, &strip);
        CHECK(minimap_ribbon(&strip, v) == MINIMAP_VERTS);
        for (i = 0; i + 1 < MINIMAP_VERTS; i += 2) {
            CHECK(fabsf(v[i + 1].x - v[i].x) >= MINIMAP_MIN_WIDTH - 1e-4f);
            /* the inward expansion never leaves the panel rect (M-9) */
            CHECK(v[i].x >= 360.0f && v[i].x <= 472.0f);
            CHECK(v[i + 1].x >= 360.0f && v[i + 1].x <= 472.0f);
            if (poses[k] == 200.0f && v[i].x > v[i + 1].x)
                swapped_at_200++;
        }
    }
    CHECK(swapped_at_200 >= 1); /* orientation kept on the doubling-back road */
}

static void test_minimap_ribbon_order(void)
{
    car_state_t car = {0};
    minimap_strip_t strip;
    minimap_point_t v[MINIMAP_VERTS];
    int i;

    /* a documented pose (DEFAULT, s = 50) where the 1.5 px floor is inert, so
     * the strip is the raw projection and can be compared with left_x/right_x */
    road_reset(ROAD_SEED);
    car.s = 50.0f;
    minimap_build(&car, 366.0f, 132.0f, 466.0f, 218.0f, 360.0f, 118.0f,
                  472.0f, 224.0f, &strip);
    CHECK(minimap_ribbon(&strip, v) == MINIMAP_VERTS);
    for (i = 0; i < MINIMAP_SAMPLES; i++) {
        CHECK(nearly(v[2 * i].x, strip.left_x[i], 1e-4f));
        CHECK(nearly(v[2 * i].y, strip.left_y[i], 1e-4f));
        CHECK(nearly(v[2 * i + 1].x, strip.right_x[i], 1e-4f));
        CHECK(nearly(v[2 * i + 1].y, strip.right_y[i], 1e-4f));
    }
    CHECK(v[MINIMAP_VERTS - 1].y < v[1].y); /* far end is higher on the panel */
}

static void test_minimap_floor_uses_the_panel_centre(void)
{
    /* M5: the floor must widen toward the PANEL rect's centre, not the map
     * rect's. The two share a centre for today's rects (416), so the bug is
     * invisible until they differ. A hand-built strip with car_x = 416 and
     * panel_cx = 406 makes them differ: the pair 410..411 is 4/5 px from the
     * panel centre, so the RIGHT point is the outer one and stays put while
     * the left moves inward to 409.5. Using the map centre instead would keep
     * 410 and push the right point to 411.5 - the opposite choice. */
    minimap_strip_t map;
    minimap_point_t v[MINIMAP_VERTS];
    int i;

    memset(&map, 0, sizeof map);
    map.count = MINIMAP_SAMPLES;
    map.car_x = 416.0f;
    map.panel_cx = 406.0f;
    for (i = 0; i < MINIMAP_SAMPLES; i++) {
        map.left_x[i] = 410.0f;
        map.left_y[i] = 150.0f;
        map.right_x[i] = 411.0f;
        map.right_y[i] = 150.0f;
    }
    CHECK(minimap_ribbon(&map, v) == MINIMAP_VERTS);
    for (i = 0; i < MINIMAP_VERTS; i += 2) {
        CHECK(nearly(v[i].x, 409.5f, 1e-4f));
        CHECK(nearly(v[i + 1].x, 411.0f, 1e-4f));
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
    minimap_build(&car, 366.0f, 132.0f, 466.0f, 218.0f, 360.0f, 118.0f,
                  472.0f, 224.0f, &def);

    c.map = ROAD_MAP_HIGHWAY;
    road_reset_config(ROAD_SEED, &c);
    minimap_build(&car, 366.0f, 132.0f, 466.0f, 218.0f, 360.0f, 118.0f,
                  472.0f, 224.0f, &hw);

    def_w = def.right_x[0] - def.left_x[0];
    hw_w = hw.right_x[0] - hw.left_x[0];
    /* The 0.5 px absolute tolerance is now LOOSE relative to these values:
     * it is +/-8.4 % on the width and +/-27 % on the 1.875 px difference,
     * because the values fell 13x with the 250 m scale. The exact constants
     * (5.9375 / 7.8125) are pinned at 1e-4 by test_minimap_widths_at_the_new_
     * scale; this test only has to prove the GRAMMAR widens the ribbon. */
    CHECK(nearly(def_w, 5.9375f, 0.5f)); /* 2-lane, edge 9.5 m */
    CHECK(nearly(hw_w, 7.8125f, 0.5f));  /* 4-lane, edge 12.5 m */
    CHECK(nearly(hw_w - def_w, 1.875f, 0.5f));
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
    minimap_build(&car, 366.0f, 132.0f, 466.0f, 218.0f, 360.0f, 118.0f,
                  472.0f, 224.0f, &map);
    p0 = road_sample(car.s - MINIMAP_BEHIND);
    width = map.right_x[0] - map.left_x[0];
    CHECK(nearly(width, 2.0f * p0.edge_half_width * 50.0f / MINIMAP_LATERAL,
                 0.5f));
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
    menu_input_t start = {false, false, false, false, false, false, false, true, false, false};
    menu_input_t select = {false, false, false, false, false, false, true, false, false, false};

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
    menu_input_t select = {false, false, false, false, false, false, true, false, false, false};
    menu_input_t down = {false, true, false, false, false, false, false, false, false, false};
    menu_input_t up = {true, false, false, false, false, false, false, false, false, false};

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
    menu_input_t select = {false, false, false, false, false, false, true, false, false, false};
    menu_input_t down = {false, true, false, false, false, false, false, false, false, false};
    menu_input_t right = {false, false, false, true, false, false, false, false, false, false};
    menu_input_t left = {false, false, true, false, false, false, false, false, false, false};
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
    menu_input_t select = {false, false, false, false, false, false, true, false, false, false};
    menu_input_t right = {false, false, false, true, false, false, false, false, false, false};
    menu_input_t left = {false, false, true, false, false, false, false, false, false, false};
    menu_input_t accept = {false, false, false, false, true, false, false, false, false, false};
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
    menu_input_t select = {false, false, false, false, false, false, true, false, false, false};
    menu_input_t right = {false, false, false, true, false, false, false, false, false, false};
    menu_input_t left = {false, false, true, false, false, false, false, false, false, false};
    menu_input_t cancel = {false, false, false, false, false, true, false, false, false, false};

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
    menu_input_t select = {false, false, false, false, false, false, true, false, false, false};
    menu_input_t right = {false, false, false, true, false, false, false, false, false, false};
    menu_input_t left = {false, false, true, false, false, false, false, false, false, false};
    menu_input_t accept = {false, false, false, false, true, false, false, false, false, false};
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
    menu_input_t select = {false, false, false, false, false, false, true, false, false, false};
    menu_input_t accept = {false, false, false, false, true, false, false, false, false, false};

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
    menu_input_t select = {false, false, false, false, false, false, true, false, false, false};
    menu_input_t accept = {false, false, false, false, true, false, false, false, false, false};

    menu_reset(&m);
    settings_defaults(&s);
    menu_press(&m, &s, select);
    m.cursor = MENU_ITEM_RESTART;
    CHECK(menu_press(&m, &s, accept) == MENU_ACTION_RESTART);
    CHECK(!m.open);
    CHECK(m.screen == MENU_SCREEN_MAIN);
    CHECK(MENU_ITEM_CALIBRATE == 5);
}

/* G3: the SIGN SIZE row is appended at 8, BEFORE RESUME, so the existing
 * indices the 030 probe walks to (CALIBRATE 5, RESTART 7) stay valid. */
static void test_menu_sign_size_row(void)
{
    menu_state_t m;
    racing_settings_t s;
    menu_input_t in;

    CHECK(MENU_ITEM_SIGN_SIZE == 8);
    CHECK(MENU_ITEM_RESUME == 9);
    CHECK(MENU_ITEM_CALIBRATE == 5);
    CHECK(MENU_ITEM_RESTART == 7);
    CHECK(MENU_ITEM_COUNT == 10);
    CHECK(MENU_SIGN_STEP == 10);

    /* menu_update() returns NONE while the menu is closed - open it first. */
    settings_defaults(&s);
    menu_reset(&m);
    m.open = true;
    m.cursor = MENU_ITEM_SIGN_SIZE;
    memset(&in, 0, sizeof in);
    in.right = true;
    CHECK(menu_update(&m, &s, &in, 128, 128) == MENU_ACTION_SAVE);
    CHECK(s.sign_scale_pct == 110);

    s.sign_scale_pct = 160;
    memset(&in, 0, sizeof in);
    in.right = true;
    CHECK(menu_update(&m, &s, &in, 128, 128) == MENU_ACTION_SAVE);
    CHECK(s.sign_scale_pct == 160); /* clamped */

    s.sign_scale_pct = 60;
    memset(&in, 0, sizeof in);
    in.left = true;
    CHECK(menu_update(&m, &s, &in, 128, 128) == MENU_ACTION_SAVE);
    CHECK(s.sign_scale_pct == 60);

    /* The cursor still wraps over all ten rows (menu stays open). */
    m.open = true;
    m.cursor = MENU_ITEM_COUNT - 1;
    memset(&in, 0, sizeof in);
    in.down = true;
    menu_update(&m, &s, &in, 128, 128);
    CHECK(m.cursor == 0);
}

static void test_welcome_mode_row_appended(void)
{
    welcome_state_t w;
    road_config_t c;
    menu_input_t up = {true, false, false, false, false, false, false, false,
                       false, false};
    menu_input_t down = {false, true, false, false, false, false, false, false,
                         false, false};
    menu_input_t right = {false, false, false, true, false, false, false,
                          false, false, false};

    CHECK(WELCOME_ITEM_MODE == WELCOME_ITEM_COUNT - 1);
    CHECK(WELCOME_ITEM_COUNT == 7);
    welcome_reset(&w, &c);
    CHECK(w.mode == WELCOME_MODE_TIME_ATTACK);
    CHECK(w.page == WELCOME_PAGE_RACE);
    CHECK(w.cursor == WELCOME_ITEM_START);

    welcome_press(&w, &c, up);   CHECK(w.cursor == WELCOME_ITEM_MAP);
    welcome_press(&w, &c, down); CHECK(w.cursor == WELCOME_ITEM_START);
    welcome_press(&w, &c, down); CHECK(w.cursor == WELCOME_ITEM_ELEVATION);

    w.cursor = WELCOME_ITEM_MODE;
    welcome_press(&w, &c, right);
    CHECK(w.mode == WELCOME_MODE_FREE_DRIVE);
    welcome_press(&w, &c, right);
    CHECK(w.mode == WELCOME_MODE_TIME_ATTACK);
}

static void test_welcome_tabs_switch_pages(void)
{
    welcome_state_t w;
    road_config_t c;
    hiscore_book_t book;
    menu_input_t rtrig = {false, false, false, false, false, false, false, false,
                          false, true};
    menu_input_t ltrig = {false, false, false, false, false, false, false, false,
                          true, false};

    hiscore_clear(&book);
    welcome_reset(&w, &c);
    c.map = ROAD_MAP_HIGHWAY;
    CHECK(welcome_update(&w, &c, &book, &rtrig) == WELCOME_ACTION_NONE);
    CHECK(w.page == WELCOME_PAGE_LEADERBOARD);
    CHECK(w.lb_map == ROAD_MAP_HIGHWAY);
    CHECK(w.lb_cursor == 0 && w.lb_first == 0);
    CHECK(welcome_update(&w, &c, &book, &ltrig) == WELCOME_ACTION_NONE);
    CHECK(w.page == WELCOME_PAGE_RACE);
    CHECK(w.cursor == WELCOME_ITEM_START);
}

static void test_welcome_page_switch_consumes_input(void)
{
    welcome_state_t w;
    road_config_t c;
    hiscore_book_t book;
    menu_input_t both = {false, false, true, false, false, false, false, false,
                         true, false};

    hiscore_clear(&book);
    welcome_reset(&w, &c);
    w.cursor = WELCOME_ITEM_ELEVATION;
    w.page = WELCOME_PAGE_LEADERBOARD;
    CHECK(welcome_update(&w, &c, &book, &both) == WELCOME_ACTION_NONE);
    CHECK(w.page == WELCOME_PAGE_RACE);
    CHECK(w.cursor == WELCOME_ITEM_ELEVATION);
    CHECK(nearly(c.elevation_pct, 100.0f, 1e-4f));
}

static void test_welcome_leaderboard_scroll_and_accept(void)
{
    welcome_state_t w;
    road_config_t c;
    hiscore_book_t book;
    menu_input_t down = {false, true, false, false, false, false, false, false,
                         false, false};
    menu_input_t accept = {false, false, false, false, true, false, false,
                           false, false, false};
    menu_input_t right = {false, false, false, true, false, false, false, false,
                          false, false};
    int i;

    hiscore_clear(&book);
    book.table[ROAD_MAP_DEFAULT].count = 10;
    welcome_reset(&w, &c);
    w.page = WELCOME_PAGE_LEADERBOARD;
    for (i = 0; i < 8; i++)
        welcome_press_book(&w, &c, &book, down);
    CHECK(w.lb_cursor == 8 && w.lb_first == 1);
    for (i = 0; i < 4; i++)
        welcome_press_book(&w, &c, &book, down);
    CHECK(w.lb_cursor == 9 && w.lb_first == 2);
    welcome_press_book(&w, &c, &book, right);
    CHECK(w.lb_map == ROAD_MAP_UPHILL);
    CHECK(w.lb_cursor == 0 && w.lb_first == 0);
    CHECK(welcome_press_book(&w, &c, &book, accept) == WELCOME_ACTION_NONE);
}

static void test_welcome_leaderboard_short_table(void)
{
    welcome_state_t w;
    road_config_t c;
    hiscore_book_t book;
    menu_input_t down = {false, true, false, false, false, false, false, false,
                         false, false};
    int i;

    hiscore_clear(&book);
    book.table[ROAD_MAP_DEFAULT].count = 3;
    welcome_reset(&w, &c);
    w.page = WELCOME_PAGE_LEADERBOARD;
    for (i = 0; i < 5; i++)
        welcome_press_book(&w, &c, &book, down);
    CHECK(w.lb_cursor == 2);
    CHECK(w.lb_first == 0);

    book.table[ROAD_MAP_DEFAULT].count = 0;
    welcome_press_book(&w, &c, &book, down);
    CHECK(w.lb_cursor == 0 && w.lb_first == 0);
}

static void test_input_gate_zeros_analog_steer(void)
{
    input_shaper_t shaper;
    racing_raw_input_t raw;
    racing_settings_t s;
    float steer;

    settings_defaults(&s);
    s.steer_mode = RACING_STEER_ANALOG;
    s.analog_centre = 128;
    s.analog_deadzone = 32;
    input_reset(&shaper);

    raw.left = true;
    raw.right = true;
    raw.brake = true;
    raw.nub_lx = 240; /* full lock to the right */
    steer = input_step(&shaper, &raw, &s, 1.0f / 60.0f);
    CHECK(steer > 0.5f);

    /* Controls dead (clock finished): every steering/brake source is zeroed. */
    input_gate_raw(&raw, false, s.analog_centre);
    CHECK(raw.nub_lx == s.analog_centre);
    CHECK(!raw.left && !raw.right && !raw.brake);
    input_reset(&shaper);
    steer = input_step(&shaper, &raw, &s, 1.0f / 60.0f);
    CHECK(nearly(steer, 0.0f, 1e-6f));

    /* Controls alive: the raw values survive untouched. */
    raw.left = true;
    raw.brake = true;
    raw.nub_lx = 200;
    input_gate_raw(&raw, true, s.analog_centre);
    CHECK(raw.left && !raw.right && raw.brake);
    CHECK(raw.nub_lx == 200);
}

static void test_welcome_page_next_consumes_input(void)
{
    welcome_state_t w;
    road_config_t c;
    hiscore_book_t book;
    menu_input_t both = {false, false, false, false, false, false, false, false,
                         false, true}; /* page_next */

    both.left = true; /* and a slider nudge in the same frame */
    hiscore_clear(&book);
    welcome_reset(&w, &c);
    w.cursor = WELCOME_ITEM_ELEVATION;
    CHECK(welcome_update(&w, &c, &book, &both) == WELCOME_ACTION_NONE);
    CHECK(w.page == WELCOME_PAGE_LEADERBOARD);
    CHECK(nearly(c.elevation_pct, 100.0f, 1e-4f));
    CHECK(w.cursor == WELCOME_ITEM_ELEVATION);
    /* the same frame's Left must not also move the leaderboard map selector */
    CHECK(w.lb_map == ROAD_MAP_DEFAULT);
}

static void test_welcome_mode_left_cycles(void)
{
    welcome_state_t w;
    road_config_t c;
    menu_input_t left = {false, false, true, false, false, false, false, false,
                         false, false};

    welcome_reset(&w, &c);
    CHECK(w.mode == WELCOME_MODE_TIME_ATTACK);
    w.cursor = WELCOME_ITEM_MODE;
    welcome_press(&w, &c, left);
    CHECK(w.mode == WELCOME_MODE_FREE_DRIVE);
    welcome_press(&w, &c, left);
    CHECK(w.mode == WELCOME_MODE_TIME_ATTACK);
}

static void test_welcome_leaderboard_scroll_clamps(void)
{
    welcome_state_t w;
    road_config_t c;
    hiscore_book_t book;
    menu_input_t down = {false, true, false, false, false, false, false, false,
                         false, false};
    menu_input_t up = {true, false, false, false, false, false, false, false,
                       false, false};
    int i;

    hiscore_clear(&book);
    book.table[ROAD_MAP_DEFAULT].count = 10;
    welcome_reset(&w, &c);
    w.page = WELCOME_PAGE_LEADERBOARD;
    for (i = 0; i < 9; i++)
        welcome_press_book(&w, &c, &book, down);
    CHECK(w.lb_cursor == 9 && w.lb_first == 2);
    for (i = 0; i < 9; i++)
        welcome_press_book(&w, &c, &book, up);
    CHECK(w.lb_cursor == 0 && w.lb_first == 0); /* clamps back to the top */
    welcome_press_book(&w, &c, &book, up);      /* no wrap below 0 */
    CHECK(w.lb_cursor == 0 && w.lb_first == 0);

    /* A stale window start (e.g. after the table shrank) is clamped back so
     * the visible window can never start past count - WELCOME_LB_VISIBLE. */
    w.lb_cursor = 9;
    w.lb_first = 5;
    welcome_press_book(&w, &c, &book, down);
    CHECK(w.lb_cursor == 9);
    CHECK(w.lb_first == 2);
}

int main(void)
{
    test_settings_defaults();
    test_settings_defaults_v2();
    test_settings_crc16_vector();
    test_settings_round_trip();
    test_settings_rejects_corruption();
    test_settings_rejects_unknown_flags();
    test_settings_validate_clamps();
    test_settings_file_is_v2_and_16_bytes();
    test_settings_v2_round_trip();
    test_settings_v1_still_loads();
    test_settings_v2_rejects_bad_scale();
    test_settings_serialize_clamps_sign_scale();

    test_welcome_defaults_and_navigation();
    test_welcome_sliders_adjust();
    test_welcome_start_action();
    test_welcome_sliders_keep_values_on_restart();
    test_welcome_map_row();
    test_welcome_map_restart();
    test_welcome_highway_disabled_sliders();
    test_welcome_uphill_width_slider();
    test_welcome_mode_row_appended();
    test_welcome_tabs_switch_pages();
    test_welcome_page_switch_consumes_input();
    test_welcome_leaderboard_scroll_and_accept();
    test_welcome_leaderboard_short_table();
    test_input_gate_zeros_analog_steer();
    test_welcome_page_next_consumes_input();
    test_welcome_mode_left_cycles();
    test_welcome_leaderboard_scroll_clamps();

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
    test_minimap_ribbon_counts();
    test_minimap_widths_at_the_new_scale();
    test_minimap_isotropic();
    test_minimap_min_width_documented_poses();
    test_minimap_min_width_u_turn();
    test_minimap_ribbon_order();
    test_minimap_floor_uses_the_panel_centre();
    test_minimap_grammar_width();

    test_menu_start_opens_and_closes();
    test_menu_navigation_wraps();
    test_menu_sensitivity();
    test_menu_toggles();
    test_menu_mode_enters_calibration();
    test_menu_calibration_capture();
    test_menu_reset_peak_and_resume();
    test_menu_restart();
    test_menu_sign_size_row();

    if (failures) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    printf("all racing UI tests passed\n");
    return 0;
}
