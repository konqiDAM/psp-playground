/*
 * welcome.c - portable welcome-screen pages, rows and terrain sliders.
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

static int clampi(int value, int low, int high)
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
    w->page = WELCOME_PAGE_RACE;
    w->mode = WELCOME_MODE_TIME_ATTACK;
    w->lb_map = ROAD_MAP_DEFAULT;
    w->lb_cursor = 0;
    w->lb_first = 0;
    road_config_defaults(cfg);
}

void welcome_restart(welcome_state_t *w)
{
    w->cursor = WELCOME_ITEM_START;
    w->page = WELCOME_PAGE_RACE;
}

static void adjust_percent(float *value, float delta)
{
    *value = clampf(*value + delta, ROAD_TERRAIN_MIN_PCT,
                    ROAD_TERRAIN_MAX_PCT);
}

static void leaderboard_scroll(welcome_state_t *w, int count)
{
    int visible = (count > WELCOME_LB_VISIBLE) ? WELCOME_LB_VISIBLE : count;
    int max_first = count - visible;
    int max_cursor = (count > 0) ? count - 1 : 0;

    w->lb_cursor = clampi(w->lb_cursor, 0, max_cursor);
    if (max_first < 0)
        max_first = 0;
    if (w->lb_cursor < w->lb_first)
        w->lb_first = w->lb_cursor;
    else if (w->lb_cursor >= w->lb_first + WELCOME_LB_VISIBLE)
        w->lb_first = w->lb_cursor - (WELCOME_LB_VISIBLE - 1);
    w->lb_first = clampi(w->lb_first, 0, max_first);
}

static welcome_action_t update_race(welcome_state_t *w, road_config_t *cfg,
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

    case WELCOME_ITEM_MODE:
        if (in->left)
            w->mode = (welcome_mode_t)((w->mode + WELCOME_MODE_COUNT - 1) %
                                       WELCOME_MODE_COUNT);
        if (in->right)
            w->mode = (welcome_mode_t)((w->mode + 1) % WELCOME_MODE_COUNT);
        break;

    default:
        break;
    }

    return WELCOME_ACTION_NONE;
}

static welcome_action_t update_leaderboard(welcome_state_t *w,
                                           const hiscore_book_t *book,
                                           const menu_input_t *in)
{
    int count = 0;

    if (book && w->lb_map >= 0 && w->lb_map < ROAD_MAP_COUNT)
        count = (int)book->table[w->lb_map].count;

    if (in->left) {
        w->lb_map = (w->lb_map + ROAD_MAP_COUNT - 1) % ROAD_MAP_COUNT;
        w->lb_cursor = 0;
        w->lb_first = 0;
        count = 0;
        if (book)
            count = (int)book->table[w->lb_map].count;
    }
    if (in->right) {
        w->lb_map = (w->lb_map + 1) % ROAD_MAP_COUNT;
        w->lb_cursor = 0;
        w->lb_first = 0;
        count = 0;
        if (book)
            count = (int)book->table[w->lb_map].count;
    }
    if (in->up)
        w->lb_cursor--;
    if (in->down)
        w->lb_cursor++;
    leaderboard_scroll(w, count);
    return WELCOME_ACTION_NONE;
}

welcome_action_t welcome_update(welcome_state_t *w, road_config_t *cfg,
                                const hiscore_book_t *book,
                                const menu_input_t *in)
{
    if (!w || !cfg || !in)
        return WELCOME_ACTION_NONE;

    /* A page-changing frame consumes the rest of the input. */
    if (in->page_next && w->page != WELCOME_PAGE_LEADERBOARD) {
        w->page = WELCOME_PAGE_LEADERBOARD;
        w->lb_map = clampi((int)cfg->map, 0, ROAD_MAP_COUNT - 1);
        w->lb_cursor = 0;
        w->lb_first = 0;
        return WELCOME_ACTION_NONE;
    }
    if (in->page_prev && w->page != WELCOME_PAGE_RACE) {
        w->page = WELCOME_PAGE_RACE;
        return WELCOME_ACTION_NONE;
    }

    if (w->page == WELCOME_PAGE_LEADERBOARD)
        return update_leaderboard(w, book, in);
    return update_race(w, cfg, in);
}
