/*
 * game.h - portable Snake rules.
 *
 * No PSP headers: this module compiles on the build machine so the rules can
 * be tested without an emulator or hardware.
 */

#ifndef SNAKE_GAME_H
#define SNAKE_GAME_H

#include <stdbool.h>
#include <stdint.h>

#define SNAKE_MAX_CELLS 1024

typedef enum {
    SNAKE_DIR_UP = 0,
    SNAKE_DIR_DOWN,
    SNAKE_DIR_LEFT,
    SNAKE_DIR_RIGHT
} snake_dir_t;

typedef enum {
    SNAKE_STEP_OK = 0, /* moved, nothing special */
    SNAKE_STEP_ATE,    /* moved onto food, grew */
    SNAKE_STEP_DIED,   /* hit a wall or itself */
    SNAKE_STEP_WON     /* filled the board */
} snake_step_result_t;

typedef struct {
    int cols;
    int rows;
    int cell_count;
    int snake[SNAKE_MAX_CELLS]; /* cell indices, head first */
    int length;
    snake_dir_t dir;         /* direction used by the last step */
    snake_dir_t pending_dir; /* direction for the next step */
    int food;                /* cell index, or -1 if none */
    int score;
    bool alive;
    bool won;
    uint32_t rng;
} snake_game_t;

/* Reset the game to a three-segment snake in the middle, moving right. */
void snake_init(snake_game_t *g, int cols, int rows, uint32_t seed);

/* Queue a direction for the next step. Opposite directions are ignored. */
void snake_set_dir(snake_game_t *g, snake_dir_t dir);

/* Advance one cell. */
snake_step_result_t snake_step(snake_game_t *g);

bool snake_is_snake_cell(const snake_game_t *g, int cell);
int snake_head(const snake_game_t *g);
int snake_cell_x(const snake_game_t *g, int cell);
int snake_cell_y(const snake_game_t *g, int cell);

/* Exposed for tests and for deterministic food placement. */
uint32_t snake_rng_next(snake_game_t *g);

#endif /* SNAKE_GAME_H */
