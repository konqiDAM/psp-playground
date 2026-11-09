/*
 * render.h - sceGu renderer for Racing 3D.
 */

#ifndef RACING_RENDER_H
#define RACING_RENDER_H

#include <stdbool.h>

#include "menu.h"
#include "physics.h"
#include "settings.h"
#include "telemetry.h"
#include "welcome.h"

typedef struct {
    const car_state_t *car;
    const racing_settings_t *settings;
    const telemetry_state_t *telemetry;
    const menu_state_t *menu;
    const road_warning_t *warnings;
    float steer_bar; /* -1..1 shaped input for the steering bar */
    float boost_timer; /* seconds of brake-tap boost left */
    int raw_lx, raw_ly;
    bool settings_save_failed;
} render_frame_args_t;

void render_init(void);
void render_frame(const render_frame_args_t *args, float dt);
void render_welcome(const welcome_state_t *welcome,
                    const road_config_t *terrain);

#endif /* RACING_RENDER_H */
