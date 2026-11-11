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
#include <string.h>

#include "hiscore.h"
#include "input.h"
#include "menu.h"
#include "physics.h"
#include "render.h"
#include "road.h"
#include "score.h"
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
    menu_in->page_prev = (pressed & PSP_CTRL_LTRIGGER) != 0;
    menu_in->page_next = (pressed & PSP_CTRL_RTRIGGER) != 0;
    (void)pad;
}

static void reset_run_state(car_state_t *car, telemetry_state_t *telemetry,
                            input_shaper_t *shaper, menu_state_t *menu,
                            road_warning_t *warnings, float *steer_bar,
                            bool *suppress, bool *menu_was_open)
{
    car_reset(car);
    telemetry_reset(telemetry);
    input_reset(shaper);
    menu_reset(menu);
    warnings->narrow = false;
    warnings->narrow_distance = 0.0f;
    warnings->crest = false;
    warnings->crest_distance = 0.0f;
    *steer_bar = 0.0f;
    *suppress = false;
    *menu_was_open = false;
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
    score_run_t score;
    hiscore_book_t hiscore;
    hiscore_result_t result;
    uint32_t run_seed = ROAD_SEED;
    bool result_done = false;
    bool hiscore_save_failed = false; /* sticky until a save succeeds */
    bool hiscore_dirty = false;

    setup_callbacks();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    storage_load_settings(&settings);
    /* A missing or corrupt RACING.HOF/.BAK leaves an empty book, which is
     * exactly "no scores yet": the leaderboard shows NO RUNS YET and the next
     * successful save rewrites the file. Accepted limitation, SPEC §7. */
    (void)storage_load_hiscore(&hiscore);
    road_config_defaults(&terrain);
    welcome_reset(&welcome, &terrain);
    memset(&result, 0, sizeof(result));
    score_reset(&score, false);
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
                welcome_update(&welcome, &terrain, &hiscore, &menu_in);

            if (welcome_action == WELCOME_ACTION_START) {
                run_seed = road_next_start_seed(&seed_state);
                road_reset_config(run_seed, &terrain);
                reset_run_state(&car, &telemetry, &shaper, &menu, &warnings,
                                &steer_bar, &suppress, &menu_was_open);
                score_reset(&score,
                            welcome.mode == WELCOME_MODE_FREE_DRIVE);
                result_done = false;
                mode = 1;
            }

            render_welcome(&welcome, &terrain, &hiscore, hiscore_save_failed);
            continue;
        }

        if (score_finished(&score) && !menu.open) {
            if (pressed & PSP_CTRL_START) {
                mode = 0;
                welcome_restart(&welcome);
                menu_reset(&menu);
                input_reset(&shaper);
                steer_bar = 0.0f;
                suppress = false;
                menu_was_open = false;
                render_welcome(&welcome, &terrain, &hiscore, hiscore_save_failed);
                continue;
            }
            if (pressed & PSP_CTRL_CROSS) {
                /* Retry the SAME seed: no seed_state advance. */
                road_reset_config(run_seed, &terrain);
                reset_run_state(&car, &telemetry, &shaper, &menu, &warnings,
                                &steer_bar, &suppress, &menu_was_open);
                score_reset(&score, false);
                result_done = false;
                continue;
            }
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
            render_welcome(&welcome, &terrain, &hiscore, hiscore_save_failed);
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
            bool controls = !score_finished(&score);

            raw.left = (pad.Buttons & PSP_CTRL_LEFT) != 0;
            raw.right = (pad.Buttons & PSP_CTRL_RIGHT) != 0;
            raw.nub_lx = (int)pad.Lx;
            raw.brake = (pad.Buttons & PSP_CTRL_SQUARE) != 0 && !suppress;
            input_gate_raw(&raw, controls, settings.analog_centre);

            input.throttle = controls &&
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

            score_step(&score, input.throttle, car.ds, dt);

            if (score_finished(&score) && !result_done) {
                int year = 0;
                int month = 0;
                int day = 0;

                storage_read_date(&year, &month, &day);
                hiscore_entry_from_run(&result.entry, score.distance_m,
                                       run_seed, &terrain, year, month, day);
                {
                    bool needs_save = false;
                    bool saved_ok = false;

                    result.rank = hiscore_commit(&hiscore, &result.entry,
                                                 &needs_save);
                    if (needs_save)
                        saved_ok = storage_save_hiscore(&hiscore);
                    hiscore_dirty = hiscore_dirty_after(hiscore_dirty,
                                                        needs_save, saved_ok);
                    hiscore_save_failed = hiscore_dirty;
                }
                result_done = true;
            }
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
        view.score = &score;
        view.hiscore = &hiscore;
        view.result = result_done ? &result : NULL;
        view.hiscore_save_failed = hiscore_save_failed;

        render_frame(&view, menu.open ? 0.0f : dt);
    }

    return 0;
}
