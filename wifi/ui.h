/*
 * ui.h - pspDebugScreen text pages for WiFi Connection.
 */
#ifndef PSP_WIFI_UI_H
#define PSP_WIFI_UI_H

#include "app.h"
#include "catalog.h"
#include "download_psp.h"
#include "install_state.h"
#include "net_probe.h"
#include "wifi.h"

typedef struct {
    const CatalogEntry *entry;
    PlanKind plan;
    char installed_version[16];
} UiCatalogRow;

typedef struct {
    const WifiLinkInfo *link;
    const WifiProfile *profiles;
    int profile_count;
    const NetProbeReport *probes;
    int probe_running;
    int probe_ready;
    int netconf_active;
    int switch_on;
    WifiStatus status;
    int raw_state;
    int apctl_event;
    int apctl_event_error;
    int apctl_event_state;
    int apctl_event_seq;
    int last_error;
    int supervisor_enabled;
    int supervisor_attempts;
    int supervisor_gave_up;
    uint32_t supervisor_next_ms;
    int system_power_save;
    int power_save_pending;
    const UiCatalogRow *rows;
    int row_count;
    int catalog_state;
    const WifiDownloadProgress *progress;
    int download_active;
    const char *catalog_url;
    int cpu_mhz;
    int cpu_applied;
    int worker_stack_free;
    uint32_t now_ms;
} UiModel;

void ui_clear(void);
void ui_draw(const App *app, const UiModel *model);

#endif /* PSP_WIFI_UI_H */
