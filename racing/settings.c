/*
 * settings.c - portable Racing 3D settings validation and 16-byte format.
 *
 * Layout (little-endian), version 2:
 *   0..3   "R3DS"
 *   4      version = 2 (version 1 is still accepted on read)
 *   5      flags: bit0 minimap, bit1 gmeter, bit2 steer bar, bit3 analog
 *   6      sensitivity x100 (50..150)
 *   7      analog deadzone (8..56)
 *   8      analog centre (0..255)
 *   9      sign scale percent (60..160); reserved = 0 in a version 1 file
 *   10..11 CRC-16/CCITT-FALSE over bytes 0..9
 *   12..15 reserved = 0
 *
 * The file stays 16 bytes. Version 2 reuses version 1's reserved byte 9, so a
 * v1 file still loads (with the default sign size) while byte 9 stays inside
 * the CRC and every single-bit flip is still rejected. Downgrading to a v1
 * build is data loss: it rejects v2, resets to defaults and rewrites v1.
 */

#include "settings.h"

#include <string.h>

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

void settings_defaults(racing_settings_t *s)
{
    s->steer_mode = RACING_STEER_DPAD;
    s->sensitivity = 1.0f;
    s->minimap = true;
    s->gmeter = true;
    s->steer_bar = true;
    s->analog_centre = RACING_SETTINGS_CENTRE_DEFAULT;
    s->analog_deadzone = RACING_SETTINGS_DEADZONE_DEFAULT;
    s->sign_scale_pct = RACING_SETTINGS_SIGN_SCALE_DEFAULT;
}

bool settings_validate(racing_settings_t *s)
{
    bool changed = false;
    float sensitivity = clampf(s->sensitivity, 0.5f, 1.5f);
    int deadzone = clampi(s->analog_deadzone,
                          RACING_SETTINGS_DEADZONE_MIN,
                          RACING_SETTINGS_DEADZONE_MAX);
    int centre = clampi(s->analog_centre, 0, 255);
    int sign_scale = clampi(s->sign_scale_pct,
                            RACING_SETTINGS_SIGN_SCALE_MIN,
                            RACING_SETTINGS_SIGN_SCALE_MAX);

    if (s->steer_mode != RACING_STEER_DPAD &&
        s->steer_mode != RACING_STEER_ANALOG) {
        s->steer_mode = RACING_STEER_DPAD;
        changed = true;
    }
    if (sensitivity != s->sensitivity) {
        s->sensitivity = sensitivity;
        changed = true;
    }
    if (deadzone != s->analog_deadzone) {
        s->analog_deadzone = deadzone;
        changed = true;
    }
    if (centre != s->analog_centre) {
        s->analog_centre = centre;
        changed = true;
    }
    if (sign_scale != s->sign_scale_pct) {
        s->sign_scale_pct = sign_scale;
        changed = true;
    }
    return changed;
}

uint16_t settings_crc16(const uint8_t *data, int len)
{
    uint16_t crc = 0xFFFFu;
    int i;

    for (i = 0; i < len; i++) {
        int bit;

        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (bit = 0; bit < 8; bit++) {
            if (crc & 0x8000u)
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            else
                crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

int settings_serialize(const racing_settings_t *s, uint8_t *buf, int cap)
{
    uint16_t crc;
    int sensitivity;
    int deadzone;
    int centre;
    int sign_scale;
    uint8_t flags = 0;

    if (!buf || cap < RACING_SETTINGS_FILE_SIZE)
        return -1;

    sensitivity = clampi((int)(s->sensitivity * 100.0f + 0.5f),
                         RACING_SETTINGS_SENS_MIN,
                         RACING_SETTINGS_SENS_MAX);
    deadzone = clampi(s->analog_deadzone, RACING_SETTINGS_DEADZONE_MIN,
                      RACING_SETTINGS_DEADZONE_MAX);
    centre = clampi(s->analog_centre, 0, 255);
    /* Clamp here, not only in settings_validate(): this function must never
     * write a file its own settings_deserialize() rejects. */
    sign_scale = clampi(s->sign_scale_pct, RACING_SETTINGS_SIGN_SCALE_MIN,
                        RACING_SETTINGS_SIGN_SCALE_MAX);

    if (s->minimap)
        flags |= 0x01u;
    if (s->gmeter)
        flags |= 0x02u;
    if (s->steer_bar)
        flags |= 0x04u;
    if (s->steer_mode == RACING_STEER_ANALOG)
        flags |= 0x08u;

    memset(buf, 0, RACING_SETTINGS_FILE_SIZE);
    memcpy(buf, "R3DS", 4);
    buf[4] = RACING_SETTINGS_VERSION;
    buf[5] = flags;
    buf[6] = (uint8_t)sensitivity;
    buf[7] = (uint8_t)deadzone;
    buf[8] = (uint8_t)centre;
    buf[9] = (uint8_t)sign_scale;

    crc = settings_crc16(buf, 10);
    buf[10] = (uint8_t)(crc & 0xFFu);
    buf[11] = (uint8_t)((crc >> 8) & 0xFFu);
    return RACING_SETTINGS_FILE_SIZE;
}

bool settings_deserialize(racing_settings_t *s, const uint8_t *buf, int len)
{
    uint16_t crc;
    uint16_t stored;
    uint8_t flags;
    int sign_scale;
    int i;

    if (!s || !buf || len != RACING_SETTINGS_FILE_SIZE)
        goto fail;
    if (memcmp(buf, "R3DS", 4) != 0)
        goto fail;
    /* Accept version 1 (byte 9 reserved) and version 2 (sign scale). */
    if (buf[4] == 1) {
        if (buf[9] != 0)
            goto fail;
        sign_scale = RACING_SETTINGS_SIGN_SCALE_DEFAULT;
    } else if (buf[4] == RACING_SETTINGS_VERSION) {
        if (buf[9] < RACING_SETTINGS_SIGN_SCALE_MIN ||
            buf[9] > RACING_SETTINGS_SIGN_SCALE_MAX)
            goto fail;
        sign_scale = (int)buf[9];
    } else {
        goto fail;
    }

    flags = buf[5];
    if ((flags & 0xF0u) != 0)
        goto fail;
    if (buf[6] < RACING_SETTINGS_SENS_MIN ||
        buf[6] > RACING_SETTINGS_SENS_MAX)
        goto fail;
    if (buf[7] < RACING_SETTINGS_DEADZONE_MIN ||
        buf[7] > RACING_SETTINGS_DEADZONE_MAX)
        goto fail;
    for (i = 12; i < RACING_SETTINGS_FILE_SIZE; i++) {
        if (buf[i] != 0)
            goto fail;
    }

    stored = (uint16_t)((uint16_t)buf[10] | ((uint16_t)buf[11] << 8));
    crc = settings_crc16(buf, 10);
    if (crc != stored)
        goto fail;

    s->steer_mode = (flags & 0x08u) ? RACING_STEER_ANALOG
                                    : RACING_STEER_DPAD;
    s->sensitivity = (float)buf[6] / 100.0f;
    s->minimap = (flags & 0x01u) != 0;
    s->gmeter = (flags & 0x02u) != 0;
    s->steer_bar = (flags & 0x04u) != 0;
    s->analog_centre = (int)buf[8];
    s->analog_deadzone = (int)buf[7];
    s->sign_scale_pct = sign_scale;
    return true;

fail:
    settings_defaults(s);
    return false;
}
