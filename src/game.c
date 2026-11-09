/*
 * game.c - portable Snake rules.
 */

#include "game.h"

#include <stddef.h>

static bool is_opposite(snake_dir_t a, snake_dir_t b)
{
    return (a == SNAKE_DIR_UP && b == SNAKE_DIR_DOWN) ||
           (a == SNAKE_DIR_DOWN && b == SNAKE_DIR_UP) ||
           (a == SNAKE_DIR_LEFT && b == SNAKE_DIR_RIGHT) ||
           (a == SNAKE_DIR_RIGHT && b == SNAKE_DIR_LEFT);
}

uint32_t snake_rng_next(snake_game_t *g)
{
    /* Numerical Recipes LCG; plenty for food placement. */
    g->rng = g->rng * 1664525u + 1013904223u;
    return g->rng;
}

int snake_cell_x(const snake_game_t *g, int cell)
{
    return cell % g->cols;
}

int snake_cell_y(const snake_game_t *g, int cell)
{
    return cell / g->cols;
}

int snake_head(const snake_game_t *g)
{
    return g->snake[0];
}

bool snake_is_snake_cell(const snake_game_t *g, int cell)
{
    for (int i = 0; i < g->length; i++) {
        if (g->snake[i] == cell)
            return true;
    }
    return false;
}

static void spawn_food(snake_game_t *g)
{
    if (g->length >= g->cell_count) {
        g->food = -1;
        return;
    }

    for (int attempt = 0; attempt < 1000; attempt++) {
        int cell = (int)(snake_rng_next(g) % (uint32_t)g->cell_count);
        if (!snake_is_snake_cell(g, cell)) {
            g->food = cell;
            return;
        }
    }

    /* Pathological fallback: find the first free cell. */
    for (int cell = 0; cell < g->cell_count; cell++) {
        if (!snake_is_snake_cell(g, cell)) {
            g->food = cell;
            return;
        }
    }
    g->food = -1;
}

void snake_init(snake_game_t *g, int cols, int rows, uint32_t seed)
{
    if (cols < 1)
        cols = 1;
    if (rows < 1)
        rows = 1;
    if ((long)cols * rows > SNAKE_MAX_CELLS) {
        rows = SNAKE_MAX_CELLS / cols;
        if (rows < 1) {
            cols = SNAKE_MAX_CELLS;
            rows = 1;
        }
    }

    g->cols = cols;
    g->rows = rows;
    g->cell_count = cols * rows;
    g->length = g->cell_count < 3 ? g->cell_count : 3;
    g->score = 0;
    g->alive = true;
    g->won = false;
    g->dir = SNAKE_DIR_RIGHT;
    g->pending_dir = SNAKE_DIR_RIGHT;
    g->rng = seed ? seed : 1u;
    g->food = -1;

    int cx = cols / 2;
    if (cx < g->length - 1)
        cx = g->length - 1;
    if (cx >= cols)
        cx = cols - 1;

    int head = (rows / 2) * cols + cx;
    g->snake[0] = head;
    for (int i = 1; i < g->length; i++)
        g->snake[i] = head - i;

    spawn_food(g);
}

void snake_set_dir(snake_game_t *g, snake_dir_t dir)
{
    if (!g->alive || g->won)
        return;
    if (dir < SNAKE_DIR_UP || dir > SNAKE_DIR_RIGHT)
        return;
    if (!is_opposite(g->dir, dir))
        g->pending_dir = dir;
}

snake_step_result_t snake_step(snake_game_t *g)
{
    if (g->won)
        return SNAKE_STEP_WON;
    if (!g->alive)
        return SNAKE_STEP_DIED;

    g->dir = g->pending_dir;

    int head = g->snake[0];
    int x = snake_cell_x(g, head);
    int y = snake_cell_y(g, head);

    switch (g->dir) {
    case SNAKE_DIR_UP:
        y--;
        break;
    case SNAKE_DIR_DOWN:
        y++;
        break;
    case SNAKE_DIR_LEFT:
        x--;
        break;
    case SNAKE_DIR_RIGHT:
        x++;
        break;
    }

    if (x < 0 || x >= g->cols || y < 0 || y >= g->rows) {
        g->alive = false;
        return SNAKE_STEP_DIED;
    }

    int new_head = y * g->cols + x;
    bool grow = (new_head == g->food);

    /* When not growing, the tail cell is about to become free. */
    int limit = grow ? g->length : g->length - 1;
    for (int i = 0; i < limit; i++) {
        if (g->snake[i] == new_head) {
            g->alive = false;
            return SNAKE_STEP_DIED;
        }
    }

    if (grow) {
        for (int i = g->length; i > 0; i--)
            g->snake[i] = g->snake[i - 1];
        g->length++;
        g->snake[0] = new_head;
        g->score++;

        if (g->length >= g->cell_count) {
            g->won = true;
            g->alive = false;
            g->food = -1;
            return SNAKE_STEP_WON;
        }

        spawn_food(g);
        return SNAKE_STEP_ATE;
    }

    for (int i = g->length - 1; i > 0; i--)
        g->snake[i] = g->snake[i - 1];
    g->snake[0] = new_head;
    return SNAKE_STEP_OK;
}
