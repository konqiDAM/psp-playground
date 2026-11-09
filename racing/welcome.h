/*
 * welcome.h - portable Racing 3D welcome-screen rows and terrain sliders.
 *
 * No PSP headers here: main.c maps pad edges into menu_input_t and the state
 * machine is host-tested. The terrain config is session-only.
 */

#ifndef RACING_WELCOME_H
#define RACING_WELCOME_H

#include <stdbool.h>

#include "menu.h"
#include "road.h"

#define WELCOME_PCT_STEP 10.0f
#define WELCOME_ONE_LANE_STEP 0.1f

typedef enum {
    WELCOME_ITEM_MAP = 0,
    WELCOME_ITEM_START,
    WELCOME_ITEM_ELEVATION,
    WELCOME_ITEM_HILLS,
    WELCOME_ITEM_WIDTH,
    WELCOME_ITEM_ONE_LANE,
    WELCOME_ITEM_COUNT
} welcome_item_t;

typedef enum {
    WELCOME_ACTION_NONE = 0,
    WELCOME_ACTION_START
} welcome_action_t;

typedef struct {
    int cursor;
} welcome_state_t;

/* Reset the cursor and restore the dramatic-alpine defaults. */
void welcome_reset(welcome_state_t *w, road_config_t *cfg);

/* Reset only the cursor; keep the current slider values for a restart. */
void welcome_restart(welcome_state_t *w);

welcome_action_t welcome_update(welcome_state_t *w, road_config_t *cfg,
                                const menu_input_t *in);

#endif /* RACING_WELCOME_H */
