/*
 * guardrail_gu.h - sceGu drawing hook for the Racing 3D guardrail.
 *
 * The hook takes the car and settings directly instead of render.h's frame
 * args, because both the Snake and the Racing renderer are called render.h
 * and only the racing include path is unambiguous for these names.
 */

#ifndef RACING_GUARDRAIL_GU_H
#define RACING_GUARDRAIL_GU_H

#include "physics.h"
#include "settings.h"

/* Draw the guardrail spans visible around the car. Called from render_frame(). */
void guardrail_draw(const car_state_t *car, const racing_settings_t *settings);

#endif /* RACING_GUARDRAIL_GU_H */
