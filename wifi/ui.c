/*
 * ui.c - fixed-row text pages for WiFi Connection.
 *
 * pspDebugScreen is 60x34 (8x8 font). Every line is printed at a fixed row
 * with a 59-character bound so page changes never leave stale characters.
 */
#include "ui.h"

#include <pspdebug.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define UI_LAST_ROW 33
#define UI_FOOTER_ROW 32

static void line(int row, const char *fmt, ...)
{
    char buf[64];
    va_list ap;

    if (row < 0 || row > UI_LAST_ROW)
        return;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    pspDebugScreenSetXY(0, row);
    pspDebugScreenPrintf("%-59.59s", buf);
}

static void separator(int row)
{
    line(row, "------------------------------------------------------------");
}

static void format_bssid(char *out, size_t size, const WifiLinkInfo *link)
{
    if (!link || !link->bssid_valid) {
        snprintf(out, size, "-");
        return;
    }
    snprintf(out, size, "%02X:%02X:%02X:%02X:%02X:%02X",
             link->bssid[0], link->bssid[1], link->bssid[2],
             link->bssid[3], link->bssid[4], link->bssid[5]);
}

static unsigned age_seconds(uint32_t now, uint32_t then)
{
    if (then == 0)
        return 0;
    return (uint32_t)((int32_t)(now - then) / 1000);
}

static void draw_header(const App *app)
{
    line(0, "WiFi Connection %s", WIFI_APP_VERSION);

    pspDebugScreenSetXY(0, 1);
    switch (app->page) {
    case APP_PAGE_STATUS:
        pspDebugScreenPrintf("%-59.59s", "[STATUS]  PROFILES  PROBES  DOWNLOADS  HELP");
        break;
    case APP_PAGE_PROFILES:
        pspDebugScreenPrintf("%-59.59s", " STATUS  [PROFILES] PROBES  DOWNLOADS  HELP");
        break;
    case APP_PAGE_PROBES:
        pspDebugScreenPrintf("%-59.59s", " STATUS   PROFILES [PROBES] DOWNLOADS  HELP");
        break;
    case APP_PAGE_DOWNLOADS:
        pspDebugScreenPrintf("%-59.59s", " STATUS   PROFILES  PROBES [DOWNLOADS] HELP");
        break;
    default:
        pspDebugScreenPrintf("%-59.59s", " STATUS   PROFILES  PROBES  DOWNLOADS [HELP]");
        break;
    }
    separator(2);
}

static void draw_status(const App *app, const UiModel *m)
{
    const WifiLinkInfo *l = m->link;
    char bssid[24];
    char supervisor[48];
    char power[48];
    char proxy[48];
    char strength[24];

    format_bssid(bssid, sizeof(bssid), l);

    if (!m->supervisor_enabled) {
        snprintf(supervisor, sizeof(supervisor), "off");
    } else if (m->supervisor_gave_up) {
        snprintf(supervisor, sizeof(supervisor), "gave up (press X)");
    } else {
        int remain = 0;
        if (m->supervisor_next_ms != 0)
            remain = (int32_t)(m->supervisor_next_ms - m->now_ms);
        if (remain < 0)
            remain = 0;
        snprintf(supervisor, sizeof(supervisor), "on %d/%d next %ds",
                 m->supervisor_attempts, 8, (remain + 999) / 1000);
    }

    if (!l || l->strength_percent < 0)
        snprintf(strength, sizeof(strength), "unknown");
    else
        snprintf(strength, sizeof(strength), "%d%%", l->strength_percent);

    if (!l || l->use_proxy < 0)
        snprintf(proxy, sizeof(proxy), "?");
    else if (!l->use_proxy)
        snprintf(proxy, sizeof(proxy), "off");
    else
        snprintf(proxy, sizeof(proxy), "on port %d", l->proxy_port);

    snprintf(power, sizeof(power), "sys %s / apctl %s",
             m->system_power_save == 0 ? "off" :
             m->system_power_save == 1 ? "on" : "?",
             !l || l->power_save < 0 ? "?" :
             l->power_save ? "on" : "off");

    line(3,  "WLAN switch : %s", m->switch_on ? "ON" : "OFF");
    line(4,  "APCTL status: %s (%d)", wifi_status_name(m->status),
             (int)m->status);
    line(5,  "APCTL state : %s (%d)", wifi_state_name(m->raw_state),
             m->raw_state);
    line(6,  "APCTL event : %s (err 0x%08X)",
             wifi_event_name(m->apctl_event),
             (unsigned)m->apctl_event_error);
    line(7,  "last error  : %s (0x%08X)",
             wifi_error_name(m->last_error), (unsigned)m->last_error);
    line(8,  "profile     : %d %s", wifi_selected_profile(),
             l && l->profile_name[0] ? l->profile_name : "");
    line(9,  "SSID        : %s", l && l->ssid_valid ? l->ssid : "?");
    line(10, "BSSID       : %s", bssid);
    line(11, "channel     : %d   strength: %s", l ? l->channel : 0, strength);
    line(12, "security    : %s", wifi_security_name(l ? l->security_type : -1));
    line(13, "IP          : %s", l && l->ip_valid ? l->ip : "?");
    line(14, "netmask     : %s", l && l->ip_valid ? l->netmask : "?");
    line(15, "gateway     : %s", l && l->gateway_valid ? l->gateway : "?");
    line(16, "DNS 1       : %s",
             l && l->dns_primary_valid ? l->primary_dns : "?");
    line(17, "DNS 2       : %s",
             l && l->dns_secondary_valid ? l->secondary_dns : "?");
    line(18, "proxy       : %s", proxy);
    line(19, "power save  : %s", power);
    line(20, "supervisor  : %s", supervisor);
    line(21, "link age    : %us", (unsigned)age_seconds(m->now_ms,
             l ? l->updated_ms : 0));
    line(22, "event state : %s (%d) seq %d",
             wifi_state_name(m->apctl_event_state), m->apctl_event_state,
             m->apctl_event_seq);
    line(23, "restore pend: %s", m->power_save_pending ? "yes" : "no");
    line(24, "CPU         : %d MHz%s", m->cpu_mhz,
             m->cpu_applied ? " (download clock)" : "");
    if (m->worker_stack_free >= 0)
        line(25, "worker stack: %d B free", m->worker_stack_free);
    else
        line(25, "worker stack: unavailable");
    line(26, "page        : %s", app_page_name(app->page));
}

static void draw_profiles(const App *app, const UiModel *m)
{
    int i;
    int row = 3;

    if (m->profile_count <= 0) {
        line(row++, "No saved profiles.");
        line(row++, "Press START to open the PSP Wi-Fi setup dialog.");
    } else {
        for (i = 0; i < m->profile_count && row < 30; i++) {
            const WifiProfile *p = &m->profiles[i];
            line(row++, "%c %2d. %-20.20s [%s]",
                 i == app->selected ? '>' : ' ',
                 p->slot, p->ssid, wifi_security_name(p->security_type));
        }
    }

    line(30, "X connect   [] refresh   START scan/configure");
}

static void draw_probes(const App *app, const UiModel *m)
{
    const NetProbeReport *p = m->probes;
    int i;
    int row = 3;

    (void)app;

    if (!m->probe_ready) {
        line(row++, "DNS resolver unavailable (TCP probes still run).");
    }

    if (!p) {
        line(row++, "No probe report yet.");
    } else {
        if (p->dns_result == NET_PROBE_OK) {
            line(row++, "DNS  %s -> %s (%d ms)",
                 NET_PROBE_DNS_HOST, p->dns_ip, p->dns_elapsed_ms);
        } else {
            line(row++, "DNS  %s (%d ms)",
                 net_probe_result_name(p->dns_result), p->dns_elapsed_ms);
        }

        for (i = 0; i < NET_PROBE_TCP_COUNT; i++) {
            const NetProbeTarget *t = &p->tcp[i];
            if (t->result == NET_PROBE_OK_REFUSED)
                line(row++, "TCP  %-16.16s refused (reachable) %d ms",
                     t->label, t->elapsed_ms);
            else
                line(row++, "TCP  %-16.16s %-10.10s %d ms",
                     t->label, net_probe_result_name(t->result),
                     t->elapsed_ms);
        }

        if (!p->gateway_configured) {
            line(row++, "GW   no gateway");
        } else if (p->gateway.result == NET_PROBE_OK_REFUSED) {
            line(row++, "GW   %-16.16s refused (reachable) %d ms",
                 p->gateway.label, p->gateway.elapsed_ms);
        } else {
            line(row++, "GW   %-16.16s %-10.10s %d ms",
                 p->gateway.label, net_probe_result_name(p->gateway.result),
                 p->gateway.elapsed_ms);
        }

        if (p->http_result == NET_PROBE_OK)
            line(row++, "HTTP %s status %d (%d ms)",
                 net_probe_http_class_name(p->http_class), p->http_status,
                 p->http_elapsed_ms);
        else
            line(row++, "HTTP %s (%d ms)",
                 net_probe_result_name(p->http_result), p->http_elapsed_ms);

        line(row++, "last run: %us ago", (unsigned)age_seconds(
                 m->now_ms, p->finished_ms));
    }

    if (app->probe_running)
        line(row++, "probe running...");
    line(30, "X run probes");
}

static void draw_downloads(const App *app, const UiModel *m)
{
    int row = 3;
    int i;
    const WifiDownloadProgress *p = m->progress;

    line(row++, "Catalog: %s", m->catalog_url ? m->catalog_url : "?");
    line(row++, "link: %s %s",
         m->link && m->link->ssid_valid ? m->link->ssid : "?",
         m->link && m->link->ip_valid ? m->link->ip : "not connected");
    if (m->catalog_state == APP_CATALOG_FETCHING)
        line(row++, "Fetching catalog...");
    else if (m->catalog_state == APP_CATALOG_ERROR)
        line(row++, "Catalog refresh failed - Square retries");
    else if (m->row_count == 0)
        line(row++, "Catalog empty - Square loads it");

    for (i = 0; i < m->row_count && row < 21; i++) {
        const UiCatalogRow *r = &m->rows[i];
        const CatalogEntry *e = r->entry;
        line(row++, "%c %-17.17s %-6.6s %-12.12s %uK",
             i == app->download_selected ? '>' : ' ',
             e->title, e->version, install_plan_name(r->plan),
             (unsigned)((e->size + 1023u) / 1024u));
    }

    if (p && (m->download_active || p->active)) {
        line(21, "phase: %s  attempt %d",
             dl_phase_name((DlPhase)p->phase), p->attempt);
        line(22, "%u/%u KB  %d%%  %u KB/s  ETA %us",
             (unsigned)((p->done + 1023u) / 1024u),
             (unsigned)((p->total + 1023u) / 1024u),
             dl_percent(p->done, p->total),
             (unsigned)(p->speed_bps / 1024u), (unsigned)p->eta_s);
    } else if (p && p->phase == DL_PHASE_DONE) {
        line(21, "installed - restart to use it");
    } else if (p && p->phase == DL_PHASE_CANCELLED) {
        line(21, "cancelled - X resumes");
    } else if (p && p->phase == DL_PHASE_FAILED) {
        if ((DlError)p->error == DL_ERR_HTTP_STATUS && p->http_status)
            line(21, "failed: http %d", p->http_status);
        else
            line(21, "failed: %s", dl_error_name((DlError)p->error));
    }

    line(30, "X install/resume  [] fetch/cancel  O exit");
}


static void draw_help(const App *app, const UiModel *m)
{
    (void)app;
    (void)m;

    line(3,  "Controls");
    line(4,  "  Left/Right or L/R : previous/next page");
    line(5,  "  Up/Down           : select profile/catalog row");
    line(6,  "  X                 : connect / probe / install");
    line(7,  "  Square            : refresh / fetch / cancel");
    line(8,  "  Triangle          : toggle reconnect supervisor");
    line(9,  "  START             : scan/configure Wi-Fi (netconf)");
    line(10, "  Circle            : exit");
    separator(12);
    line(13, "Probes are fixed: connectivitycheck.gstatic.com/generate_204,");
    line(14, "1.1.1.1:443, 8.8.8.8:53, 9.9.9.9:53 and the gateway on port 80.");
    line(16, "DOWNLOADS: plain HTTP LAN catalog, SHA-256 verified.");
    line(17, "No HTTPS/TLS, no PSP_OLD catalog, no self-update.");
    line(18, "Catalog URL: CATALOG.CFG or the compiled default.");
    separator(20);
    line(21, "0.2.0 hardware validation is PENDING.");
    line(22, "PPSSPP fakes APCTL and proxies host sockets.");
    line(24, "Power save is off while running; a crash can leave it off.");
    line(25, "After a crash, check Settings > Power Save.");
}

void ui_clear(void)
{
    pspDebugScreenClear();
}

void ui_draw(const App *app, const UiModel *model)
{
    static AppPage drawn_page = (AppPage)-1;

    if (!app || !model)
        return;

    if (drawn_page != app->page) {
        pspDebugScreenClear();
        drawn_page = app->page;
    }

    draw_header(app);

    switch (app->page) {
    case APP_PAGE_STATUS:
        draw_status(app, model);
        break;
    case APP_PAGE_PROFILES:
        draw_profiles(app, model);
        break;
    case APP_PAGE_PROBES:
        draw_probes(app, model);
        break;
    case APP_PAGE_DOWNLOADS:
        draw_downloads(app, model);
        break;
    case APP_PAGE_HELP:
        draw_help(app, model);
        break;
    default:
        break;
    }

    if (app->message[0])
        line(31, "%s", app->message);
    else
        line(31, "%s", "");
    line(UI_FOOTER_ROW,
         "L/R pg U/D sel X go [] fetch/cancel ^ sup START cfg O exit");
}
