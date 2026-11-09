/*
 * welcome.c - portable welcome-screen rows and terrain sliders.
 */

#include "welcome.h"

static float clampf(float value, float low, float high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

void welcome_reset(welcome_state_t *w, road_config_t *cfg)
{
    w->cursor = WELCOME_ITEM_START;
    road_config_defaults(cfg);
}

void welcome_restart(welcome_state_t *w)
{
    w->cursor = WELCOME_ITEM_START;
}

static void adjust_percent(float *value, float delta)
{
    *value = clampf(*value + delta, ROAD_TERRAIN_MIN_PCT,
                    ROAD_TERRAIN_MAX_PCT);
}

welcome_action_t welcome_update(welcome_state_t *w, road_config_t *cfg,
                                const menu_input_t *in)
{
    if (in->up)
        w->cursor = (w->cursor + WELCOME_ITEM_COUNT - 1) % WELCOME_ITEM_COUNT;
    if (in->down)
        w->cursor = (w->cursor + 1) % WELCOME_ITEM_COUNT;

    switch (w->cursor) {
    case WELCOME_ITEM_MAP:
        if (in->left)
            cfg->map = (road_map_t)((cfg->map + ROAD_MAP_COUNT - 1) %
                                    ROAD_MAP_COUNT);
        if (in->right)
            cfg->map = (road_map_t)((cfg->map + 1) % ROAD_MAP_COUNT);
        break;

    case WELCOME_ITEM_START:
        if (in->accept)
            return WELCOME_ACTION_START;
        break;

    case WELCOME_ITEM_ELEVATION:
        if (in->left)
            adjust_percent(&cfg->elevation_pct, -WELCOME_PCT_STEP);
        if (in->right)
            adjust_percent(&cfg->elevation_pct, WELCOME_PCT_STEP);
        break;

    case WELCOME_ITEM_HILLS:
        if (in->left)
            adjust_percent(&cfg->hill_pct, -WELCOME_PCT_STEP);
        if (in->right)
            adjust_percent(&cfg->hill_pct, WELCOME_PCT_STEP);
        break;

    case WELCOME_ITEM_WIDTH:
        if (cfg->map == ROAD_MAP_HIGHWAY)
            break;
        if (in->left)
            adjust_percent(&cfg->width_pct, -WELCOME_PCT_STEP);
        if (in->right)
            adjust_percent(&cfg->width_pct, WELCOME_PCT_STEP);
        break;

    case WELCOME_ITEM_ONE_LANE:
        if (cfg->map == ROAD_MAP_HIGHWAY)
            break;
        if (in->left)
            cfg->one_lane_width = clampf(cfg->one_lane_width -
                                             WELCOME_ONE_LANE_STEP,
                                         ROAD_ONE_LANE_TOTAL_MIN,
                                         ROAD_ONE_LANE_TOTAL_MAX);
        if (in->right)
            cfg->one_lane_width = clampf(cfg->one_lane_width +
                                             WELCOME_ONE_LANE_STEP,
                                         ROAD_ONE_LANE_TOTAL_MIN,
                                         ROAD_ONE_LANE_TOTAL_MAX);
        break;

    default:
        break;
    }

    return WELCOME_ACTION_NONE;
}
