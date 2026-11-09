/*
 * Racing 3D - a small third-person driving game for the PSP.
 *
 * Runtime-generated curved road, chase camera, D-pad steering by default with
 * an optional calibrated analog mode, grip-limited handling, an off-road
 * shoulder, a computed HUD and a pausing Select settings menu. Direct PSPSDK +
 * sceGu + pspgum; no external assets. START exits when the menu is closed and
 * resumes when it is open; HOME is handled by the system callback.
 */

#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspuser.h>

#include <stdbool.h>
#include <stdint.h>

#include "input.h"
#include "menu.h"
#include "physics.h"
#include "render.h"
#include "road.h"
#include "settings.h"
#include "storage.h"
#include "telemetry.h"
#include "welcome.h"

PSP_MODULE_INFO("Racing3D", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

/* All user RAM minus a 4 MiB system reserve (same policy as Snake/Cube 3D). */
PSP_HEAP_SIZE_KB(-1);
PSP_HEAP_THRESHOLD_SIZE_KB(4096);

static int exit_callback(int arg1, int arg2, void *common)
{
    (void)arg1;
    (void)arg2;
    (void)common;
    sceKernelExitGame();
    return 0;
}

static int callback_thread(SceSize args, void *argp)
{
    (void)args;
    (void)argp;

    int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

static void setup_callbacks(void)
{
    int thid = sceKernelCreateThread("update_thread", callback_thread, 0x11,
                                     0xFA0, 0, NULL);
    if (thid >= 0)
        sceKernelStartThread(thid, 0, NULL);
}

static void read_menu_edges(const SceCtrlData *pad, uint32_t pressed,
                            menu_input_t *menu_in)
{
    menu_in->up = (pressed & PSP_CTRL_UP) != 0;
    menu_in->down = (pressed & PSP_CTRL_DOWN) != 0;
    menu_in->left = (pressed & PSP_CTRL_LEFT) != 0;
    menu_in->right = (pressed & PSP_CTRL_RIGHT) != 0;
    menu_in->accept = (pressed & PSP_CTRL_CROSS) != 0;
    menu_in->cancel = (pressed & PSP_CTRL_SQUARE) != 0;
    menu_in->menu = (pressed & PSP_CTRL_SELECT) != 0;
    menu_in->start = (pressed & PSP_CTRL_START) != 0;
    (void)pad;
}

int main(void)
{
    car_state_t car;
    racing_settings_t settings;
    telemetry_state_t telemetry;
    menu_state_t menu;
    road_warning_t warnings = {0};
    input_shaper_t shaper;
    road_config_t terrain;
    welcome_state_t welcome;
    uint32_t seed_state = ROAD_SEED;
    uint32_t previous_buttons = 0;
    uint64_t last_us;
    bool save_failed = false;
    bool suppress = false;
    bool menu_was_open;
    float steer_bar = 0.0f;
    int mode = 0; /* 0 = welcome, 1 = game */

    setup_callbacks();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    storage_load_settings(&settings);
    road_config_defaults(&terrain);
    welcome_reset(&welcome, &terrain);
    car_reset(&car);
    telemetry_reset(&telemetry);
    input_reset(&shaper);
    menu_reset(&menu);
    render_init();
    menu_was_open = menu.open;

    last_us = (uint64_t)sceKernelGetSystemTimeWide();

    for (;;) {
        SceCtrlData pad;
        car_input_t input = {false, false, 0.0f, 0.0f};
        racing_raw_input_t raw;
        menu_input_t menu_in;
        menu_action_t action;
        uint64_t now_us;
        float dt;
        uint32_t pressed;
        render_frame_args_t view;

        sceCtrlPeekBufferPositive(&pad, 1);

        pressed = pad.Buttons & ~previous_buttons;
        previous_buttons = pad.Buttons;

        read_menu_edges(&pad, pressed, &menu_in);

        now_us = (uint64_t)sceKernelGetSystemTimeWide();
        dt = (float)(now_us - last_us) / 1000000.0f;
        last_us = now_us;
        if (dt < 0.0f)
            dt = 0.0f;
        if (dt > 0.05f)
            dt = 0.05f;

        if (mode == 0) {
            welcome_action_t welcome_action =
                welcome_update(&welcome, &terrain, &menu_in);

            if (welcome_action == WELCOME_ACTION_START) {
                uint32_t seed = road_next_start_seed(&seed_state);

                road_reset_config(seed, &terrain);
                car_reset(&car);
                telemetry_reset(&telemetry);
                input_reset(&shaper);
                menu_reset(&menu);
                warnings.narrow = false;
                warnings.crest = false;
                steer_bar = 0.0f;
                suppress = false;
                menu_was_open = false;
                mode = 1;
            }

            render_welcome(&welcome, &terrain);
            continue;
        }

        action = menu_update(&menu, &settings, &menu_in, (int)pad.Lx,
                             (int)pad.Ly);
        if (action == MENU_ACTION_EXIT)
            sceKernelExitGame();
        if (action == MENU_ACTION_RESTART) {
            mode = 0;
            welcome_restart(&welcome);
            menu_reset(&menu);
            input_reset(&shaper);
            warnings.narrow = false;
            warnings.crest = false;
            steer_bar = 0.0f;
            suppress = false;
            menu_was_open = false;
            render_welcome(&welcome, &terrain);
            continue;
        }
        if (action == MENU_ACTION_SAVE)
            save_failed = !storage_save_settings(&settings);
        if (action == MENU_ACTION_RESET_PEAK)
            telemetry_reset_peak(&telemetry);

        if (menu.open != menu_was_open) {
            input_reset(&shaper);
            steer_bar = 0.0f;
            suppress = true;
            menu_was_open = menu.open;
        }

        if (!menu.open) {
            raw.left = (pad.Buttons & PSP_CTRL_LEFT) != 0;
            raw.right = (pad.Buttons & PSP_CTRL_RIGHT) != 0;
            raw.nub_lx = (int)pad.Lx;
            raw.brake =
                (pad.Buttons & PSP_CTRL_SQUARE) != 0 && !suppress;

            input.throttle =
                (pad.Buttons & PSP_CTRL_CROSS) != 0 && !suppress;
            input.steer = input_step(&shaper, &raw, &settings, dt);
            input.brake = shaper.brake_level;
            input.boost = shaper.boost_timer > 0.0f ? 1.0f : 0.0f;
            steer_bar = input.steer;

            car.s = road_update(car.s);
            {
                road_sample_t road = road_sample(car.s);

                car_step(&car, &input, dt, &road);
            }
            road_warnings(car.s, &warnings);
            telemetry_step(&telemetry, &car, dt);
        }

        if (suppress &&
            (pad.Buttons & (PSP_CTRL_CROSS | PSP_CTRL_SQUARE)) == 0)
            suppress = false;

        view.car = &car;
        view.settings = &settings;
        view.telemetry = &telemetry;
        view.menu = &menu;
        view.warnings = &warnings;
        view.steer_bar = steer_bar;
        view.boost_timer = shaper.boost_timer;
        view.raw_lx = (int)pad.Lx;
        view.raw_ly = (int)pad.Ly;
        view.settings_save_failed = save_failed;

        render_frame(&view, menu.open ? 0.0f : dt);
    }

    return 0;
}
