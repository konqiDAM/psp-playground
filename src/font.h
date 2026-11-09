/*
 * font.h - tiny 3x5 bitmap font for solid-colour GU rectangles.
 */

#ifndef SNAKE_FONT_H
#define SNAKE_FONT_H

#include <stdint.h>

/* Returns five rows of three bits (bit 2 = leftmost), or NULL. */
const uint8_t *font_glyph(char c);

/* Width in pixels of a string drawn at the given integer scale. */
int font_text_width(const char *text, int scale);

#endif /* SNAKE_FONT_H */
