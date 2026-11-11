/*
 * menu.h - portable Racing 3D settings menu and analog calibration state.
 *
 * No PSP headers here: main.c maps pressed buttons into menu_input_t, and the
 * state machine is host-tested.
 */

#ifndef RACING_MENU_H
#define RACING_MENU_H

#include <stdbool.h>

#include "settings.h"

#define MENU_SENS_STEP 0.1f
#define MENU_DEADZONE_STEP 8
#define MENU_SIGN_STEP 10 /* percent per Left/Right press */

typedef enum {
    MENU_SCREEN_MAIN = 0,
    MENU_SCREEN_CALIBRATE
} menu_screen_t;

typedef enum {
    MENU_ITEM_STEER_MODE = 0,
    MENU_ITEM_SENSITIVITY,
    MENU_ITEM_MINIMAP,
    MENU_ITEM_GMETER,
    MENU_ITEM_STEER_BAR,
    MENU_ITEM_CALIBRATE,
    MENU_ITEM_RESET_PEAK,
    MENU_ITEM_RESTART,
    /* Appended at 8, before RESUME: MENU_ITEM_CALIBRATE stays 5 and
     * MENU_ITEM_RESTART stays 7, which check-racing-030.py walks to. */
    MENU_ITEM_SIGN_SIZE,
    MENU_ITEM_RESUME,
    MENU_ITEM_COUNT
} menu_item_t;

typedef enum {
    MENU_ACTION_NONE = 0,
    MENU_ACTION_SAVE,
    MENU_ACTION_RESET_PEAK,
    MENU_ACTION_CLOSE,
    MENU_ACTION_EXIT,
    MENU_ACTION_RESTART
} menu_action_t;

typedef struct {
    bool up, down, left, right, accept, cancel, menu, start; /* edges */
    bool page_prev, page_next; /* shoulder edges: welcome page tabs */
} menu_input_t;

typedef struct {
    bool open;
    menu_screen_t screen;
    int cursor;
} menu_state_t;

void menu_reset(menu_state_t *m);

menu_action_t menu_update(menu_state_t *m, racing_settings_t *s,
                          const menu_input_t *in, int raw_lx, int raw_ly);

#endif /* RACING_MENU_H */
