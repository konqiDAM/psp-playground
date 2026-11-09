/*
 * font.c - tiny 3x5 bitmap font.
 *
 * Each glyph is five rows; each row has three bits, bit 2 = leftmost.
 * Only the characters the game needs are defined.
 */

#include "font.h"

#include <stddef.h>

static const uint8_t g_space[5] = {0, 0, 0, 0, 0};
static const uint8_t g_colon[5] = {0, 2, 0, 2, 0};
static const uint8_t g_dash[5] = {0, 0, 7, 0, 0};
static const uint8_t g_bang[5] = {2, 2, 2, 0, 2};
static const uint8_t g_dot[5] = {0, 0, 0, 0, 2};
static const uint8_t g_slash[5] = {1, 1, 2, 4, 4};
static const uint8_t g_percent[5] = {5, 1, 2, 4, 5};
static const uint8_t g_lparen[5] = {1, 2, 2, 2, 1};
static const uint8_t g_rparen[5] = {4, 2, 2, 2, 4};
static const uint8_t g_question[5] = {7, 1, 2, 0, 2};
static const uint8_t g_plus[5] = {0, 2, 7, 2, 0};
static const uint8_t g_equal[5] = {0, 7, 0, 7, 0};

static const uint8_t g_A[5] = {7, 5, 7, 5, 5};
static const uint8_t g_B[5] = {6, 5, 6, 5, 6};
static const uint8_t g_C[5] = {7, 4, 4, 4, 7};
static const uint8_t g_D[5] = {6, 5, 5, 5, 6};
static const uint8_t g_E[5] = {7, 4, 7, 4, 7};
static const uint8_t g_F[5] = {7, 4, 7, 4, 4};
static const uint8_t g_G[5] = {7, 4, 5, 5, 7};
static const uint8_t g_H[5] = {5, 5, 7, 5, 5};
static const uint8_t g_I[5] = {7, 2, 2, 2, 7};
static const uint8_t g_J[5] = {1, 1, 1, 5, 7};
static const uint8_t g_K[5] = {5, 5, 6, 5, 5};
static const uint8_t g_L[5] = {4, 4, 4, 4, 7};
static const uint8_t g_M[5] = {5, 7, 7, 5, 5};
static const uint8_t g_N[5] = {5, 7, 7, 7, 5};
static const uint8_t g_O[5] = {7, 5, 5, 5, 7};
static const uint8_t g_P[5] = {7, 5, 7, 4, 4};
static const uint8_t g_Q[5] = {7, 5, 5, 7, 1};
static const uint8_t g_R[5] = {7, 5, 7, 6, 5};
static const uint8_t g_S[5] = {7, 4, 7, 1, 7};
static const uint8_t g_T[5] = {7, 2, 2, 2, 2};
static const uint8_t g_U[5] = {5, 5, 5, 5, 7};
static const uint8_t g_V[5] = {5, 5, 5, 5, 2};
static const uint8_t g_W[5] = {5, 5, 7, 7, 5};
static const uint8_t g_X[5] = {5, 5, 2, 5, 5};
static const uint8_t g_Y[5] = {5, 5, 2, 2, 2};
static const uint8_t g_Z[5] = {7, 1, 2, 4, 7};

static const uint8_t g_0[5] = {7, 5, 5, 5, 7};
static const uint8_t g_1[5] = {2, 6, 2, 2, 7};
static const uint8_t g_2[5] = {7, 1, 7, 4, 7};
static const uint8_t g_3[5] = {7, 1, 7, 1, 7};
static const uint8_t g_4[5] = {5, 5, 7, 1, 1};
static const uint8_t g_5[5] = {7, 4, 7, 1, 7};
static const uint8_t g_6[5] = {7, 4, 7, 5, 7};
static const uint8_t g_7[5] = {7, 1, 1, 1, 1};
static const uint8_t g_8[5] = {7, 5, 7, 5, 7};
static const uint8_t g_9[5] = {7, 5, 7, 1, 7};

const uint8_t *font_glyph(char c)
{
    if (c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');

    switch (c) {
    case ' ': return g_space;
    case ':': return g_colon;
    case '-': return g_dash;
    case '!': return g_bang;
    case '.': return g_dot;
    case '/': return g_slash;
    case '%': return g_percent;
    case '(': return g_lparen;
    case ')': return g_rparen;
    case '?': return g_question;
    case '+': return g_plus;
    case '=': return g_equal;

    case 'A': return g_A;
    case 'B': return g_B;
    case 'C': return g_C;
    case 'D': return g_D;
    case 'E': return g_E;
    case 'F': return g_F;
    case 'G': return g_G;
    case 'H': return g_H;
    case 'I': return g_I;
    case 'J': return g_J;
    case 'K': return g_K;
    case 'L': return g_L;
    case 'M': return g_M;
    case 'N': return g_N;
    case 'O': return g_O;
    case 'P': return g_P;
    case 'Q': return g_Q;
    case 'R': return g_R;
    case 'S': return g_S;
    case 'T': return g_T;
    case 'U': return g_U;
    case 'V': return g_V;
    case 'W': return g_W;
    case 'X': return g_X;
    case 'Y': return g_Y;
    case 'Z': return g_Z;

    case '0': return g_0;
    case '1': return g_1;
    case '2': return g_2;
    case '3': return g_3;
    case '4': return g_4;
    case '5': return g_5;
    case '6': return g_6;
    case '7': return g_7;
    case '8': return g_8;
    case '9': return g_9;

    default: return NULL;
    }
}

int font_text_width(const char *text, int scale)
{
    int n = 0;

    if (scale < 1)
        scale = 1;
    while (text[n])
        n++;
    if (n == 0)
        return 0;

    /* Three columns plus one column of spacing, minus the trailing gap. */
    return n * 4 * scale - scale;
}
