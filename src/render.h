/*
 * render.h - sceGu renderer for the Snake game.
 */

#ifndef SNAKE_RENDER_H
#define SNAKE_RENDER_H

#include "game.h"

void render_init(void);
void render_draw(const snake_game_t *g);

#endif /* SNAKE_RENDER_H */
