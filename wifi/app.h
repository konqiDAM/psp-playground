/*
 * app.h - portable WiFi Connection page/selection/action state.
 *
 * No PSP headers: host-tested and used by wifi/main.c and wifi/ui.c.
 */
#ifndef PSP_WIFI_APP_H
#define PSP_WIFI_APP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_APP_VERSION "0.2.0"

#define APP_INPUT_UP       0x0001u
#define APP_INPUT_DOWN     0x0002u
#define APP_INPUT_LEFT     0x0004u
#define APP_INPUT_RIGHT    0x0008u
#define APP_INPUT_CROSS    0x0010u
#define APP_INPUT_CIRCLE   0x0020u
#define APP_INPUT_SQUARE   0x0040u
#define APP_INPUT_TRIANGLE 0x0080u
#define APP_INPUT_START    0x0100u
#define APP_MESSAGE_MS      3000u

typedef enum {
    APP_PAGE_STATUS = 0,
    APP_PAGE_PROFILES,
    APP_PAGE_PROBES,
    APP_PAGE_DOWNLOADS,
    APP_PAGE_HELP,
    APP_PAGE_COUNT
} AppPage;

typedef enum {
    APP_ACTION_NONE = 0,
    APP_ACTION_CONNECT,
    APP_ACTION_REFRESH_PROFILES,
    APP_ACTION_RUN_PROBES,
    APP_ACTION_OPEN_NETCONF,
    APP_ACTION_TOGGLE_SUPERVISOR,
    APP_ACTION_FETCH_CATALOG,
    APP_ACTION_INSTALL,
    APP_ACTION_CANCEL_DOWNLOAD,
    APP_ACTION_EXIT
} AppActionType;

typedef struct {
    AppActionType type;
    int profile_pos;          /* only for APP_ACTION_CONNECT */
} AppAction;

typedef enum {
    APP_NETCONF_CONNECTED = 0,
    APP_NETCONF_CANCELLED,
    APP_NETCONF_PENDING,
    APP_NETCONF_FAILED
} AppNetconfOutcome;

typedef enum {
    APP_CATALOG_IDLE = 0,
    APP_CATALOG_FETCHING,
    APP_CATALOG_READY,
    APP_CATALOG_ERROR
} AppCatalogState;

typedef struct {
    AppPage page;
    int selected;
    int supervisor_enabled;
    int probe_running;
    int netconf_active;
    int exit_requested;
    int catalog_state;
    int catalog_count;
    int download_selected;
    int download_active;
    int download_phase;
    uint32_t download_done;
    uint32_t download_total;
    uint32_t download_speed_bps;
    uint32_t download_eta_s;
    int download_error;
    char message[80];
    uint32_t message_until_ms;
} App;

void app_init(App *app);
AppAction app_handle_input(App *app, uint32_t pressed, int profile_count,
                           int catalog_count, uint32_t now_ms);
void app_set_message(App *app, const char *text, uint32_t now_ms);
void app_tick(App *app, uint32_t now_ms);
void app_request_exit(App *app);
void app_clamp_selection(App *app, int profile_count);
void app_catalog_submit_result(App *app, int rc, uint32_t now_ms);
AppNetconfOutcome app_netconf_outcome(int netconf_ok, int apctl_state);
const char *app_page_name(AppPage page);

#ifdef __cplusplus
}
#endif

#endif /* PSP_WIFI_APP_H */
