/*
 * welcome.h - portable Racing 3D welcome-screen pages, rows and sliders.
 *
 * No PSP headers here: main.c maps pad edges into menu_input_t and the state
 * machine is host-tested. The terrain config and the mode are session-only.
 */

#ifndef RACING_WELCOME_H
#define RACING_WELCOME_H

#include <stdbool.h>

#include "hiscore.h"
#include "menu.h"
#include "road.h"

#define WELCOME_PCT_STEP 10.0f
#define WELCOME_ONE_LANE_STEP 0.1f
#define WELCOME_LB_VISIBLE 8

/*
 * RACE page rows. MODE is appended last on purpose: the 0.5.0/0.6.0 probe
 * navigation (cursor on START GAME, UP to MAP, DOWN to ELEVATION) is part of
 * the regression contract.
 */
typedef enum {
    WELCOME_ITEM_MAP = 0,
    WELCOME_ITEM_START,
    WELCOME_ITEM_ELEVATION,
    WELCOME_ITEM_HILLS,
    WELCOME_ITEM_WIDTH,
    WELCOME_ITEM_ONE_LANE,
    WELCOME_ITEM_MODE,
    WELCOME_ITEM_COUNT
} welcome_item_t;

typedef enum {
    WELCOME_ACTION_NONE = 0,
    WELCOME_ACTION_START
} welcome_action_t;

typedef enum {
    WELCOME_PAGE_RACE = 0,
    WELCOME_PAGE_LEADERBOARD
} welcome_page_t;

typedef enum {
    WELCOME_MODE_TIME_ATTACK = 0, /* default */
    WELCOME_MODE_FREE_DRIVE,
    WELCOME_MODE_COUNT
} welcome_mode_t;

typedef struct {
    int cursor;             /* RACE page row */
    welcome_page_t page;
    welcome_mode_t mode;
    int lb_map;             /* ROAD_MAP_* shown on the leaderboard page */
    int lb_cursor;          /* selected leaderboard entry */
    int lb_first;           /* first visible row of the 8-row window */
} welcome_state_t;

/* Reset the cursor, the page and the terrain to the defaults. */
void welcome_reset(welcome_state_t *w, road_config_t *cfg);

/* Reset the cursor and the page; keep mode and slider values for a restart. */
void welcome_restart(welcome_state_t *w);

/*
 * One unpaused welcome frame. A frame that changes page consumes the whole
 * input and returns WELCOME_ACTION_NONE, so L/R can never also move a slider.
 * The leaderboard page is read-only: Up/Down scroll, Left/Right pick the map.
 */
welcome_action_t welcome_update(welcome_state_t *w, road_config_t *cfg,
                                const hiscore_book_t *book,
                                const menu_input_t *in);

#endif /* RACING_WELCOME_H */
