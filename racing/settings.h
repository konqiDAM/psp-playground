/*
 * settings.h - portable Racing 3D settings state and file format.
 *
 * No PSP headers here on purpose: tests/test_racing_ui.c builds this module on
 * the host. The ms0: I/O lives in storage.c (PSP only).
 */

#ifndef RACING_SETTINGS_H
#define RACING_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#define RACING_SETTINGS_FILE_SIZE 16
#define RACING_SETTINGS_VERSION 2

#define RACING_SETTINGS_SENS_MIN 50  /* stored x100: 0.50 */
#define RACING_SETTINGS_SENS_MAX 150 /* 1.50 */
#define RACING_SETTINGS_DEADZONE_MIN 8
#define RACING_SETTINGS_DEADZONE_MAX 56
#define RACING_SETTINGS_CENTRE_DEFAULT 128
#define RACING_SETTINGS_DEADZONE_DEFAULT 32

/* v2: byte 9 is the sign size in percent. v1 kept it reserved at 0. */
#define RACING_SETTINGS_SIGN_SCALE_MIN 60
#define RACING_SETTINGS_SIGN_SCALE_MAX 160
#define RACING_SETTINGS_SIGN_SCALE_DEFAULT 100

typedef enum {
    RACING_STEER_DPAD = 0,
    RACING_STEER_ANALOG = 1
} racing_steer_mode_t;

typedef struct {
    racing_steer_mode_t steer_mode; /* default RACING_STEER_DPAD */
    float sensitivity;              /* 0.50..1.50, default 1.0 */
    bool minimap;                   /* default true */
    bool gmeter;                    /* default true */
    bool steer_bar;                 /* default true */
    int analog_centre;              /* 0..255, default 128 */
    int analog_deadzone;            /* 8..56, default 32 */
    int sign_scale_pct;             /* 60..160, default 100 (RACING.CFG v2) */
} racing_settings_t;

void settings_defaults(racing_settings_t *s);

/* Clamp an in-RAM struct; returns true when anything changed. */
bool settings_validate(racing_settings_t *s);

/* CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection. */
uint16_t settings_crc16(const uint8_t *data, int len);

/* Returns RACING_SETTINGS_FILE_SIZE, or -1 when cap is too small. */
int settings_serialize(const racing_settings_t *s, uint8_t *buf, int cap);

/* On any invalid/corrupt input: applies defaults and returns false. */
bool settings_deserialize(racing_settings_t *s, const uint8_t *buf, int len);

#endif /* RACING_SETTINGS_H */
