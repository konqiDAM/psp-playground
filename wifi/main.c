/*
 * main.c - WiFi Connection 0.2.0 for the PSP.
 *
 * Text UI via pspDebugScreen. One lower-priority worker runs probes, the
 * catalog fetch and downloads; the main thread owns input, APCTL, netconf
 * and all drawing.
 */
#include <pspctrl.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <pspnet_apctl.h>
#include <pspuser.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "catalog.h"
#include "download_psp.h"
#include "install_state.h"
#include "netconf.h"
#include "net_probe.h"
#include "power.h"
#include "ui.h"
#include "wifi.h"
#include "worker.h"

#ifndef WIFI_DEFAULT_CATALOG_URL
#define WIFI_DEFAULT_CATALOG_URL "http://192.168.1.14:8080/psp/catalog.txt"
#endif

#define WIFI_CATALOG_CFG "ms0:/PSP/SAVEDATA/WIFICONN/CATALOG.CFG"
#define WIFI_INSTALLED_FILE "ms0:/PSP/SAVEDATA/WIFICONN/INSTALLED.TXT"

PSP_MODULE_INFO("WifiConnection", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);
PSP_HEAP_SIZE_KB(-1);
PSP_HEAP_THRESHOLD_SIZE_KB(4096);

static volatile int s_exit_requested;
static int s_text_repaint_frames;
static uint32_t s_last_draw_ms;
static NetProbeReport s_probe_report;
static int s_have_probe_report;

static Catalog s_catalog;
static CatalogUrl s_catalog_base;
static UiCatalogRow s_rows[CATALOG_MAX_ENTRIES];
static int s_row_count;
static InstallRegistry s_registry;
static WifiCatalogReport s_catalog_report;
static WifiDownloadProgress s_download_progress;
static char s_catalog_url[CATALOG_URL_MAX];
static int s_last_download_phase = DL_PHASE_IDLE;

static int exit_callback(int arg1, int arg2, void *common)
{
    (void)arg1;
    (void)arg2;
    (void)common;
    s_exit_requested = 1;
    return 0;
}

static int callback_thread(SceSize args, void *argp)
{
    int cbid;

    (void)args;
    (void)argp;

    cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

static void setup_callbacks(void)
{
    int thid = sceKernelCreateThread("update_thread", callback_thread,
                                     0x11, 0xFA0, 0, NULL);
    if (thid >= 0)
        sceKernelStartThread(thid, 0, NULL);
}

static uint32_t map_buttons(uint32_t buttons)
{
    uint32_t out = 0;

    if (buttons & PSP_CTRL_UP)       out |= APP_INPUT_UP;
    if (buttons & PSP_CTRL_DOWN)     out |= APP_INPUT_DOWN;
    if (buttons & PSP_CTRL_LEFT)     out |= APP_INPUT_LEFT;
    if (buttons & PSP_CTRL_RIGHT)    out |= APP_INPUT_RIGHT;
    if (buttons & PSP_CTRL_LTRIGGER) out |= APP_INPUT_LEFT;
    if (buttons & PSP_CTRL_RTRIGGER) out |= APP_INPUT_RIGHT;
    if (buttons & PSP_CTRL_CROSS)    out |= APP_INPUT_CROSS;
    if (buttons & PSP_CTRL_CIRCLE)   out |= APP_INPUT_CIRCLE;
    if (buttons & PSP_CTRL_SQUARE)   out |= APP_INPUT_SQUARE;
    if (buttons & PSP_CTRL_TRIANGLE) out |= APP_INPUT_TRIANGLE;
    if (buttons & PSP_CTRL_START)    out |= APP_INPUT_START;
    return out;
}

static void sync_profile(App *app)
{
    int count = wifi_profile_count();

    app_clamp_selection(app, count);
    if (count <= 0)
        return;

    wifi_set_profile(wifi_profile_at(app->selected)->slot);
}

static void messagef(App *app, uint32_t now, const char *fmt, ...)
{
    char buf[80];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    app_set_message(app, buf, now);
}

static int read_text_file(const char *path, char *buf, int size)
{
    SceUID fd;
    int n;

    fd = sceIoOpen(path, PSP_O_RDONLY, 0777);
    if (fd < 0)
        return -1;
    n = sceIoRead(fd, buf, (SceSize)size);
    sceIoClose(fd);
    return n;
}

static void load_catalog_url(App *app, uint32_t now)
{
    char text[2048];
    char cfg[CATALOG_URL_MAX];
    int n;

    snprintf(s_catalog_url, sizeof(s_catalog_url), "%s",
             WIFI_DEFAULT_CATALOG_URL);
    n = read_text_file(WIFI_CATALOG_CFG, text, (int)sizeof(text));
    if (n > 0) {
        cfg[0] = '\0';
        if (catalog_config_parse(text, n, cfg, sizeof(cfg)) != 0) {
            messagef(app, now, "catalog config invalid - using default");
        } else if (cfg[0]) {
            snprintf(s_catalog_url, sizeof(s_catalog_url), "%s", cfg);
        }
    }
    if (catalog_url_parse(s_catalog_url, &s_catalog_base) != 0)
        messagef(app, now, "catalog URL invalid");
}

static int load_registry(void)
{
    char text[INSTALLED_MAX_BYTES + 1];
    int n;

    memset(&s_registry, 0, sizeof(s_registry));
    n = read_text_file(WIFI_INSTALLED_FILE, text, INSTALLED_MAX_BYTES);
    if (n > 0) {
        text[n] = '\0';
        if (installed_parse(text, n, &s_registry) != 0) {
            if (download_psp_quarantine_registry(WIFI_INSTALLED_FILE) != 0)
                return -1; /* damaged file kept; write_registry will abort */
            memset(&s_registry, 0, sizeof(s_registry));
            return 1; /* quarantined successfully */
        }
    }
    return 0;
}

static void target_facts(const CatalogEntry *entry, int *exists,
                         uint32_t *size)
{
    char target[INSTALLED_PATH_MAX];
    SceIoStat st;

    *exists = 0;
    *size = 0;
    if (install_target_path(entry, target, sizeof(target)) != 0)
        return;
    if (sceIoGetstat(target, &st) >= 0) {
        *exists = 1;
        *size = (uint32_t)st.st_size;
    }
}

static void build_rows(uint32_t free_bytes)
{
    int i;

    s_row_count = 0;
    for (i = 0; i < s_catalog.count; i++) {
        int exists;
        uint32_t size;
        int idx;
        PlanKind plan;

        target_facts(&s_catalog.entries[i], &exists, &size);
        plan = install_plan(&s_catalog.entries[i], &s_registry, exists, size,
                            free_bytes, WIFI_SELF_ID);
        s_rows[i].entry = &s_catalog.entries[i];
        s_rows[i].plan = plan;
        s_rows[i].installed_version[0] = '\0';
        idx = installed_find(&s_registry, s_catalog.entries[i].id);
        if (idx >= 0)
            snprintf(s_rows[i].installed_version,
                     sizeof(s_rows[i].installed_version), "%s",
                     s_registry.records[idx].version);
        s_row_count++;
    }
}

static void build_model(const App *app, uint32_t now, UiModel *m)
{
    memset(m, 0, sizeof(*m));
    m->link = wifi_link_info();
    m->profiles = wifi_profile_at(0);
    m->profile_count = wifi_profile_count();
    m->probes = s_have_probe_report ? &s_probe_report : NULL;
    m->probe_running = app->probe_running;
    m->probe_ready = net_probe_ready();
    m->netconf_active = app->netconf_active;
    m->switch_on = wifi_wlan_switch();
    m->status = wifi_status();
    m->raw_state = wifi_raw_state();
    m->apctl_event = wifi_apctl_event();
    m->apctl_event_error = wifi_apctl_event_error();
    m->apctl_event_state = wifi_apctl_event_state();
    m->apctl_event_seq = wifi_apctl_event_seq();
    m->last_error = wifi_last_error();
    m->supervisor_enabled = app->supervisor_enabled;
    m->supervisor_attempts = wifi_supervisor_attempts();
    m->supervisor_gave_up = wifi_supervisor_gave_up();
    m->supervisor_next_ms = wifi_supervisor_next_ms();
    m->system_power_save = wifi_power_save_state();
    m->power_save_pending = wifi_power_save_pending();
    m->rows = s_rows;
    m->row_count = s_row_count;
    m->catalog_state = app->catalog_state;
    m->progress = &s_download_progress;
    m->download_active = app->download_active;
    m->worker_stack_free = wifi_worker_stack_free();
    m->catalog_url = s_catalog_url;
    m->cpu_mhz = wifi_power_cpu_mhz();
    m->cpu_applied = wifi_power_cpu_applied();
    m->now_ms = now;
}

static int setup_wifi(App *app, uint32_t now)
{
    int rc;

    rc = wifi_init();
    if (rc != WIFI_OK) {
        messagef(app, now, "Wi-Fi init failed: %s (0x%08X)",
                 wifi_error_name(wifi_last_error()),
                 (unsigned)wifi_last_error());
        app->supervisor_enabled = 0;
        return rc;
    }

    wifi_profile_refresh();
    sync_profile(app);
    wifi_power_init();
    load_catalog_url(app, now);
    {
        int reg = load_registry();
        if (reg > 0)
            messagef(app, now,
                     "Installed-app registry damaged; moved to INSTALLED.BAD");
        else if (reg < 0)
            messagef(app, now, "Registry damaged - INSTALLED.TXT kept");
    }
    wifi_power_save_off();
    wifi_set_auto_reconnect(true);
    app->supervisor_enabled = 1;
    wifi_supervisor_reset();

    if (wifi_selected_profile() != 0)
        wifi_begin_connect();
    else
        messagef(app, now, "No saved profiles - press START to configure");

    if (wifi_worker_start() != WIFI_OK)
        messagef(app, now, "Probe worker unavailable");
    return WIFI_OK;
}

static void handle_connect(App *app, uint32_t now)
{
    int rc;

    rc = wifi_init();
    if (rc != WIFI_OK) {
        messagef(app, now, "Wi-Fi init failed: %s (0x%08X)",
                 wifi_error_name(wifi_last_error()),
                 (unsigned)wifi_last_error());
        return;
    }

    if (wifi_profile_count() == 0)
        wifi_profile_refresh();
    sync_profile(app);

    if (wifi_selected_profile() == 0) {
        messagef(app, now, "No saved profiles - press START to configure");
        return;
    }

    wifi_power_save_off();
    wifi_worker_start();
    wifi_set_auto_reconnect(true);
    app->supervisor_enabled = 1;
    wifi_supervisor_reset();
    wifi_set_profile(wifi_profile_at(app->selected)->slot);

    if (wifi_is_connected())
        rc = wifi_begin_reconnect();
    else
        rc = wifi_begin_connect();

    if (rc != WIFI_OK) {
        int err = wifi_last_error() != 0 ? wifi_last_error() : rc;
        messagef(app, now, "Connect failed: %s (0x%08X)",
                 wifi_error_name(err), (unsigned)err);
    } else {
        messagef(app, now, "Connecting to %s...",
                 wifi_profile_at(app->selected)->ssid);
    }
}

static void handle_netconf(App *app, uint32_t now)
{
    NetconfResult result;
    AppNetconfOutcome outcome;
    int raw = 0;
    int before_state;

    if (wifi_worker_busy()) {
        messagef(app, now, "Worker busy - wait before opening Wi-Fi setup");
        return;
    }

    before_state = wifi_raw_state();
    app->netconf_active = 1;
    result = netconf_run(&s_exit_requested, &raw);
    app->netconf_active = 0;
    s_text_repaint_frames = NETCONF_TEXT_REPAINT_FRAMES;

    if (result == NETCONF_RESULT_EXITED) {
        app->exit_requested = 1;
        return;
    }

    wifi_resync();
    outcome = app_netconf_outcome(result == NETCONF_RESULT_OK,
                                  wifi_raw_state());

    if ((outcome == APP_NETCONF_CONNECTED || outcome == APP_NETCONF_PENDING) &&
        before_state != PSP_NET_APCTL_STATE_GOT_IP)
        wifi_take_link_ownership();

    wifi_profile_refresh();
    sync_profile(app);
    wifi_power_save_off();

    if (outcome == APP_NETCONF_CONNECTED || outcome == APP_NETCONF_PENDING) {
        wifi_set_auto_reconnect(true);
        app->supervisor_enabled = 1;
        wifi_supervisor_reset();
        messagef(app, now, "Wi-Fi setup: %s",
                 outcome == APP_NETCONF_CONNECTED ? "connected" : "connecting");
    } else {
        wifi_set_auto_reconnect(false);
        app->supervisor_enabled = 0;
        if (result != NETCONF_RESULT_OK)
            messagef(app, now, "Wi-Fi setup failed (0x%08X)", (unsigned)raw);
        else
            messagef(app, now, "Wi-Fi setup cancelled");
    }
}

static void handle_fetch_catalog(App *app, uint32_t now)
{
    int rc;

    if (!wifi_is_connected()) {
        messagef(app, now, "Connect to a network first (PROFILES)");
        return;
    }
    rc = wifi_worker_submit_catalog(s_catalog_url);
    app_catalog_submit_result(app, rc, now);
    if (rc == WIFI_OK)
        messagef(app, now, "Fetching catalog...");
}

static void handle_install(App *app, uint32_t now)
{
    const CatalogEntry *entry;
    UiCatalogRow *row;
    WifiDownloadJob job;
    char target[INSTALLED_PATH_MAX];
    char url[CATALOG_URL_MAX];
    int rc;

    if (!wifi_is_connected()) {
        messagef(app, now, "Connect to a network first (PROFILES)");
        return;
    }
    if (wifi_worker_busy()) {
        messagef(app, now, "Worker busy - wait");
        return;
    }
    if (s_row_count <= 0) {
        messagef(app, now, "Catalog empty - press Square");
        return;
    }
    if (app->download_selected < 0 || app->download_selected >= s_row_count)
        app->download_selected = 0;
    row = &s_rows[app->download_selected];
    entry = row->entry;

    if (row->plan == PLAN_SELF) {
        messagef(app, now, "Running app cannot update itself - copy from PC");
        return;
    }
    if (row->plan == PLAN_NO_SPACE) {
        messagef(app, now, "Not enough free space on the Memory Stick");
        return;
    }
    if (row->plan == PLAN_INSTALLED) {
        messagef(app, now, "%s %s is already installed", entry->title,
                 entry->version);
        return;
    }
    if (install_target_path(entry, target, sizeof(target)) != 0) {
        messagef(app, now, "Bad install path");
        return;
    }
    if (catalog_url_join(&s_catalog_base, entry->path, url, sizeof(url)) != 0) {
        messagef(app, now, "Bad catalog path");
        return;
    }

    memset(&job, 0, sizeof(job));
    snprintf(job.url, sizeof(job.url), "%s", url);
    snprintf(job.target, sizeof(job.target), "%s", target);
    job.size = entry->size;
    snprintf(job.sha256, sizeof(job.sha256), "%s", entry->sha256);
    snprintf(job.id, sizeof(job.id), "%s", entry->id);
    snprintf(job.version, sizeof(job.version), "%s", entry->version);

    rc = wifi_worker_submit_download(&job);
    if (rc != WIFI_OK) {
        messagef(app, now, "Download unavailable");
        return;
    }
    app->download_active = 1;
    app->download_phase = DL_PHASE_CONNECTING;
    app->download_error = 0;
    app->download_done = 0;
    app->download_total = entry->size;
    s_last_download_phase = DL_PHASE_CONNECTING;
    messagef(app, now, "Downloading %s %s", entry->title, entry->version);
}

static void poll_worker(App *app, uint32_t now)
{
    WifiCatalogReport report;
    WifiDownloadProgress progress;

    if (wifi_worker_take_catalog(&report)) {
        s_catalog_report = report;
        if (report.error != DL_OK) {
            app->catalog_state = APP_CATALOG_ERROR;
            messagef(app, now, "Catalog refresh failed: %s",
                     dl_error_name((DlError)report.error));
        } else {
            Catalog parsed;
            CatalogResult cr = catalog_parse(report.text, report.len, &parsed);
            if (cr != CATALOG_OK) {
                app->catalog_state = APP_CATALOG_ERROR;
                messagef(app, now, "Bad manifest: %s",
                         catalog_result_name(cr));
            } else {
                s_catalog = parsed;
                app->catalog_count = s_catalog.count;
                app->catalog_state = APP_CATALOG_READY;
                build_rows(report.free_bytes);
                if (app->download_selected >= s_row_count)
                    app->download_selected = s_row_count > 0 ? s_row_count - 1 : 0;
                messagef(app, now, "%d catalog app(s)", s_row_count);
            }
        }
    }

    if (wifi_worker_take_progress(&progress)) {
        s_download_progress = progress;
        app->download_active = progress.active;
        app->download_phase = progress.phase;
        app->download_done = progress.done;
        app->download_total = progress.total;
        app->download_speed_bps = progress.speed_bps;
        app->download_eta_s = progress.eta_s;
        app->download_error = progress.error;

        if (progress.phase != s_last_download_phase) {
            s_last_download_phase = progress.phase;
            if (progress.phase == DL_PHASE_DONE) {
                int reg = load_registry();
                build_rows(s_catalog_report.free_bytes);
                if (reg == 0)
                    messagef(app, now, "Installed - restart to use it");
                else if (reg > 0)
                    messagef(app, now,
                             "Installed - restart; old registry at INSTALLED.BAD");
                else
                    messagef(app, now,
                             "Installed - registry damaged, INSTALLED.TXT kept");
            } else if (progress.phase == DL_PHASE_FAILED) {
                messagef(app, now, "Download failed: %s",
                         dl_error_name((DlError)progress.error));
            } else if (progress.phase == DL_PHASE_CANCELLED) {
                messagef(app, now, "cancelled - X resumes");
            } else if (progress.phase == DL_PHASE_WAIT_WIFI) {
                messagef(app, now, "Waiting for Wi-Fi...");
            }
        }
    }
}

int main(void)
{
    App app;
    SceCtrlData pad;
    uint32_t previous = 0;

    pspDebugScreenInit();
    pspDebugScreenClear();
    setup_callbacks();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    app_init(&app);
    setup_wifi(&app, (uint32_t)sceKernelGetSystemTimeLow() / 1000u);
    ui_clear();
    s_last_download_phase = DL_PHASE_IDLE;

    for (;;) {
        uint32_t now = (uint32_t)sceKernelGetSystemTimeLow() / 1000u;
        uint32_t pressed;
        uint32_t current_buttons;
        AppAction action;
        NetProbeReport report;

        if (s_exit_requested || app.exit_requested)
            break;

        poll_worker(&app, now);

        if (wifi_resume_pending() ||
            (app.download_active && !wifi_link_trusted())) {
            if (wifi_resume_revalidate() == WIFI_OK) {
                s_text_repaint_frames = NETCONF_TEXT_REPAINT_FRAMES;
                messagef(&app, now, "Wi-Fi resumed");
            }
        }

        if (wifi_worker_take_report(&report)) {
            s_probe_report = report;
            s_have_probe_report = 1;
            app.probe_running = 0;
            app_set_message(&app, "Probes complete", now);
        }

        if (s_text_repaint_frames > 0) {
            UiModel model;
            s_text_repaint_frames--;
            if (sceCtrlPeekBufferPositive(&pad, 1) > 0)
                previous = pad.Buttons;
            else
                previous = 0;
            ui_clear();
            build_model(&app, now, &model);
            ui_draw(&app, &model);
            sceDisplayWaitVblankStart();
            continue;
        }

        if (sceCtrlPeekBufferPositive(&pad, 1) <= 0)
            memset(&pad, 0, sizeof(pad));

        current_buttons = pad.Buttons;
        pressed = current_buttons & ~previous;
        previous = current_buttons;

        action = app_handle_input(&app, map_buttons(pressed),
                                  wifi_profile_count(), s_catalog.count, now);

        switch (action.type) {
        case APP_ACTION_CONNECT:
            handle_connect(&app, now);
            break;
        case APP_ACTION_REFRESH_PROFILES: {
            int n = wifi_profile_refresh();
            sync_profile(&app);
            messagef(&app, now, "%d saved profile(s)", n < 0 ? 0 : n);
            break;
        }
        case APP_ACTION_RUN_PROBES:
            if (!wifi_is_connected()) {
                messagef(&app, now, "Connect to a network first");
            } else if (wifi_worker_busy()) {
                messagef(&app, now, "Worker already busy");
            } else {
                int rc = wifi_worker_submit(WIFI_JOB_ALL,
                                            wifi_link_info()->gateway);
                if (rc == WIFI_OK)
                    app.probe_running = 1;
                else
                    messagef(&app, now, "Probe submit failed: %d", rc);
            }
            break;
        case APP_ACTION_OPEN_NETCONF:
            handle_netconf(&app, now);
            break;
        case APP_ACTION_TOGGLE_SUPERVISOR:
            wifi_set_auto_reconnect(app.supervisor_enabled != 0);
            if (app.supervisor_enabled)
                wifi_supervisor_reset();
            messagef(&app, now, "Supervisor %s",
                     app.supervisor_enabled ? "on" : "off");
            break;
        case APP_ACTION_FETCH_CATALOG:
            handle_fetch_catalog(&app, now);
            break;
        case APP_ACTION_INSTALL:
            handle_install(&app, now);
            break;
        case APP_ACTION_CANCEL_DOWNLOAD:
            wifi_worker_cancel_download();
            messagef(&app, now, "Cancelling...");
            break;
        case APP_ACTION_EXIT:
            app.exit_requested = 1;
            break;
        default:
            break;
        }

        if (s_exit_requested || app.exit_requested)
            break;

        wifi_poll();
        wifi_supervise();
        if (app.download_active)
            wifi_power_tick();
        app_tick(&app, now);

        if (pressed || (uint32_t)(now - s_last_draw_ms) >= 250u) {
            UiModel model;
            build_model(&app, now, &model);
            ui_draw(&app, &model);
            s_last_draw_ms = now;
        }

        sceDisplayWaitVblankStart();
    }

    {
        int wf;
        wifi_worker_cancel_download();
        wf = wifi_worker_stop();
        wifi_power_shutdown();
        if (wf == WIFI_OK)
            wifi_shutdown();
        else
            wifi_power_save_restore();
    }
    sceKernelExitGame();
    return 0;
}
