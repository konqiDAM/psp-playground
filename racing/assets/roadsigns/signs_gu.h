/*
 * signs_gu.h - sceGu drawing hook for the Racing 3D roadside signs.
 *
 * The hook takes the car and settings directly instead of render.h's frame
 * args, because both the Snake and the Racing renderer are called render.h
 * and only the racing include path is unambiguous for these names.
 */

#ifndef RACING_SIGNS_GU_H
#define RACING_SIGNS_GU_H

#include "physics.h"
#include "settings.h"

/* Draw the signs visible around the car. Called from render_frame(). */
void signs_draw(const car_state_t *car, const racing_settings_t *settings);

#endif /* RACING_SIGNS_GU_H */
