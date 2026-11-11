/*
 * menu.c - portable Racing 3D settings menu and analog calibration.
 */

#include "menu.h"

static int clampi(int value, int low, int high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

static float clampf(float value, float low, float high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

void menu_reset(menu_state_t *m)
{
    m->open = false;
    m->screen = MENU_SCREEN_MAIN;
    m->cursor = 0;
}

static menu_action_t update_calibration(menu_state_t *m, racing_settings_t *s,
                                        const menu_input_t *in, int raw_lx)
{
    if (in->cancel) {
        m->screen = MENU_SCREEN_MAIN;
        return MENU_ACTION_NONE;
    }
    if (in->accept) {
        s->analog_centre = clampi(raw_lx, 0, 255);
        return MENU_ACTION_SAVE;
    }
    if (in->left) {
        s->analog_deadzone = clampi(s->analog_deadzone - MENU_DEADZONE_STEP,
                                    RACING_SETTINGS_DEADZONE_MIN,
                                    RACING_SETTINGS_DEADZONE_MAX);
        return MENU_ACTION_SAVE;
    }
    if (in->right) {
        s->analog_deadzone = clampi(s->analog_deadzone + MENU_DEADZONE_STEP,
                                    RACING_SETTINGS_DEADZONE_MIN,
                                    RACING_SETTINGS_DEADZONE_MAX);
        return MENU_ACTION_SAVE;
    }
    return MENU_ACTION_NONE;
}

menu_action_t menu_update(menu_state_t *m, racing_settings_t *s,
                          const menu_input_t *in, int raw_lx, int raw_ly)
{
    (void)raw_ly;

    if (!m->open) {
        if (in->start)
            return MENU_ACTION_EXIT;
        if (in->menu) {
            m->open = true;
            m->screen = MENU_SCREEN_MAIN;
            m->cursor = MENU_ITEM_STEER_MODE;
        }
        return MENU_ACTION_NONE;
    }

    if (in->menu || in->start) {
        m->open = false;
        m->screen = MENU_SCREEN_MAIN;
        return MENU_ACTION_CLOSE;
    }

    if (m->screen == MENU_SCREEN_CALIBRATE)
        return update_calibration(m, s, in, raw_lx);

    if (in->up)
        m->cursor = (m->cursor + MENU_ITEM_COUNT - 1) % MENU_ITEM_COUNT;
    if (in->down)
        m->cursor = (m->cursor + 1) % MENU_ITEM_COUNT;

    if (in->cancel) {
        m->open = false;
        m->screen = MENU_SCREEN_MAIN;
        return MENU_ACTION_CLOSE;
    }

    switch (m->cursor) {
    case MENU_ITEM_STEER_MODE:
        if (in->left) {
            s->steer_mode = RACING_STEER_DPAD;
            return MENU_ACTION_SAVE;
        }
        if (in->right) {
            s->steer_mode = RACING_STEER_ANALOG;
            m->screen = MENU_SCREEN_CALIBRATE;
            return MENU_ACTION_SAVE;
        }
        if (in->accept) {
            if (s->steer_mode == RACING_STEER_DPAD) {
                s->steer_mode = RACING_STEER_ANALOG;
                m->screen = MENU_SCREEN_CALIBRATE;
            } else {
                s->steer_mode = RACING_STEER_DPAD;
            }
            return MENU_ACTION_SAVE;
        }
        break;

    case MENU_ITEM_SENSITIVITY:
        if (in->left) {
            s->sensitivity = clampf(s->sensitivity - MENU_SENS_STEP, 0.5f,
                                    1.5f);
            return MENU_ACTION_SAVE;
        }
        if (in->right) {
            s->sensitivity = clampf(s->sensitivity + MENU_SENS_STEP, 0.5f,
                                    1.5f);
            return MENU_ACTION_SAVE;
        }
        break;

    case MENU_ITEM_MINIMAP:
        if (in->left) {
            s->minimap = false;
            return MENU_ACTION_SAVE;
        }
        if (in->right) {
            s->minimap = true;
            return MENU_ACTION_SAVE;
        }
        if (in->accept) {
            s->minimap = !s->minimap;
            return MENU_ACTION_SAVE;
        }
        break;

    case MENU_ITEM_GMETER:
        if (in->left) {
            s->gmeter = false;
            return MENU_ACTION_SAVE;
        }
        if (in->right) {
            s->gmeter = true;
            return MENU_ACTION_SAVE;
        }
        if (in->accept) {
            s->gmeter = !s->gmeter;
            return MENU_ACTION_SAVE;
        }
        break;

    case MENU_ITEM_STEER_BAR:
        if (in->left) {
            s->steer_bar = false;
            return MENU_ACTION_SAVE;
        }
        if (in->right) {
            s->steer_bar = true;
            return MENU_ACTION_SAVE;
        }
        if (in->accept) {
            s->steer_bar = !s->steer_bar;
            return MENU_ACTION_SAVE;
        }
        break;

    case MENU_ITEM_CALIBRATE:
        if (in->accept) {
            m->screen = MENU_SCREEN_CALIBRATE;
            return MENU_ACTION_NONE;
        }
        break;

    case MENU_ITEM_RESET_PEAK:
        if (in->accept)
            return MENU_ACTION_RESET_PEAK;
        break;

    case MENU_ITEM_RESTART:
        if (in->accept) {
            m->open = false;
            m->screen = MENU_SCREEN_MAIN;
            return MENU_ACTION_RESTART;
        }
        break;

    case MENU_ITEM_SIGN_SIZE:
        /* Sensitivity semantics: step and clamp, never wrap. */
        if (in->left) {
            s->sign_scale_pct = clampi(s->sign_scale_pct - MENU_SIGN_STEP,
                                       RACING_SETTINGS_SIGN_SCALE_MIN,
                                       RACING_SETTINGS_SIGN_SCALE_MAX);
            return MENU_ACTION_SAVE;
        }
        if (in->right) {
            s->sign_scale_pct = clampi(s->sign_scale_pct + MENU_SIGN_STEP,
                                       RACING_SETTINGS_SIGN_SCALE_MIN,
                                       RACING_SETTINGS_SIGN_SCALE_MAX);
            return MENU_ACTION_SAVE;
        }
        break;

    case MENU_ITEM_RESUME:
        if (in->accept) {
            m->open = false;
            m->screen = MENU_SCREEN_MAIN;
            return MENU_ACTION_CLOSE;
        }
        break;

    default:
        break;
    }

    return MENU_ACTION_NONE;
}
