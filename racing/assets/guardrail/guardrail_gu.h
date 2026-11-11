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

/*
 * Draw the rail-contact flash. MUST be called at the END of render_frame()'s
 * content block - after draw_posts(), draw_car(), the HUD and the minimap -
 * because it is a 2D overlay drawn last: emitted inside guardrail_draw() the
 * marker posts that draw_posts() paints afterwards covered its column (924 px
 * on four probe frames, 772 on the one where a post sat in front; isolated by
 * moving the call either side of draw_posts()). A source gate in
 * scripts/test.sh pins the order.
 */
void guardrail_draw_flash(const car_state_t *car);

#endif /* RACING_GUARDRAIL_GU_H */
