/*
 * Host tests for the portable Snake game logic.
 *
 * These run on the build machine with a normal C compiler; no PSP SDK needed.
 */

#include <stdint.h>
#include <stdio.h>

#include "game.h"

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static snake_game_t make_game(void)
{
    snake_game_t g;
    snake_init(&g, 30, 15, 12345u);
    g.food = -1; /* isolate movement from random food placement */
    return g;
}

static void test_initial_state(void)
{
    snake_game_t g = make_game();
    int head = 7 * 30 + 15;

    CHECK(g.cols == 30);
    CHECK(g.rows == 15);
    CHECK(g.length == 3);
    CHECK(g.alive);
    CHECK(!g.won);
    CHECK(g.dir == SNAKE_DIR_RIGHT);
    CHECK(g.pending_dir == SNAKE_DIR_RIGHT);
    CHECK(g.score == 0);
    CHECK(snake_head(&g) == head);
    CHECK(g.snake[1] == head - 1);
    CHECK(g.snake[2] == head - 2);
}

static void test_step_moves_head_right(void)
{
    snake_game_t g = make_game();
    int head = snake_head(&g);

    CHECK(snake_step(&g) == SNAKE_STEP_OK);
    CHECK(snake_head(&g) == head + 1);
    CHECK(g.length == 3);
    CHECK(g.alive);
}

static void test_reverse_is_ignored(void)
{
    snake_game_t g = make_game();
    int head = snake_head(&g);

    snake_set_dir(&g, SNAKE_DIR_LEFT); /* opposite of current right */
    CHECK(g.pending_dir == SNAKE_DIR_RIGHT);

    CHECK(snake_step(&g) == SNAKE_STEP_OK);
    CHECK(snake_head(&g) == head + 1);
}

static void test_wall_kills(void)
{
    snake_game_t g;
    snake_init(&g, 5, 5, 1u);
    g.food = -1;

    CHECK(snake_step(&g) == SNAKE_STEP_OK);
    CHECK(snake_step(&g) == SNAKE_STEP_OK);
    CHECK(snake_step(&g) == SNAKE_STEP_DIED);
    CHECK(!g.alive);
}

static void test_eating_grows_and_scores(void)
{
    snake_game_t g = make_game();
    int head = snake_head(&g);

    g.food = head + 1;
    CHECK(snake_step(&g) == SNAKE_STEP_ATE);
    CHECK(g.length == 4);
    CHECK(g.score == 1);
    CHECK(snake_head(&g) == head + 1);
    CHECK(g.food != head + 1);
    CHECK(g.food >= 0);
    CHECK(!snake_is_snake_cell(&g, g.food));
}

static void test_food_never_on_snake(void)
{
    for (uint32_t seed = 1; seed <= 200; seed++) {
        snake_game_t g;
        snake_init(&g, 12, 8, seed);
        CHECK(g.food >= 0);
        CHECK(!snake_is_snake_cell(&g, g.food));
    }
}

static void test_self_collision(void)
{
    snake_game_t g;
    snake_init(&g, 10, 10, 1u);
    g.length = 4;
    g.alive = true;
    g.food = -1;
    g.dir = SNAKE_DIR_UP;
    g.pending_dir = SNAKE_DIR_UP;

    /* 2x2 loop: head (2,2), body (2,1), (1,1), tail (1,2). */
    g.snake[0] = 2 * 10 + 2;
    g.snake[1] = 1 * 10 + 2;
    g.snake[2] = 1 * 10 + 1;
    g.snake[3] = 2 * 10 + 1;

    CHECK(snake_step(&g) == SNAKE_STEP_DIED);
    CHECK(!g.alive);
}

static void test_tail_cell_is_allowed(void)
{
    snake_game_t g;
    snake_init(&g, 10, 10, 1u);
    g.length = 4;
    g.alive = true;
    g.food = -1;
    g.dir = SNAKE_DIR_UP;
    g.pending_dir = SNAKE_DIR_UP;

    /* Head (1,1), body (0,1), (0,0), tail (1,0): moving up enters tail. */
    g.snake[0] = 1 * 10 + 1;
    g.snake[1] = 1 * 10 + 0;
    g.snake[2] = 0 * 10 + 0;
    g.snake[3] = 0 * 10 + 1;

    CHECK(snake_step(&g) == SNAKE_STEP_OK);
    CHECK(g.alive);
    CHECK(snake_head(&g) == 0 * 10 + 1);
}

static void test_win_when_board_full(void)
{
    snake_game_t g;
    snake_init(&g, 4, 1, 1u);

    CHECK(g.length == 3);
    g.food = 3;
    CHECK(snake_step(&g) == SNAKE_STEP_WON);
    CHECK(g.won);
    CHECK(g.length == 4);
}

int main(void)
{
    test_initial_state();
    test_step_moves_head_right();
    test_reverse_is_ignored();
    test_wall_kills();
    test_eating_grows_and_scores();
    test_food_never_on_snake();
    test_self_collision();
    test_tail_cell_is_allowed();
    test_win_when_board_full();

    if (failures) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    printf("all game tests passed\n");
    return 0;
}
