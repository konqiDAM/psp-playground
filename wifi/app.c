/*
 * app.c - portable WiFi Connection page/selection/action state.
 */
#include "app.h"

#include <stdio.h>
#include <string.h>

static AppAction make_action(AppActionType type, int profile_pos)
{
    AppAction action;

    action.type = type;
    action.profile_pos = profile_pos;
    return action;
}

void app_init(App *app)
{
    if (!app)
        return;

    memset(app, 0, sizeof(*app));
    app->page = APP_PAGE_STATUS;
    app->selected = 0;
    app->download_selected = 0;
}

const char *app_page_name(AppPage page)
{
    switch (page) {
    case APP_PAGE_STATUS:    return "STATUS";
    case APP_PAGE_PROFILES:  return "PROFILES";
    case APP_PAGE_PROBES:    return "PROBES";
    case APP_PAGE_DOWNLOADS: return "DOWNLOADS";
    case APP_PAGE_HELP:      return "HELP";
    default:                 return "?";
    }
}

void app_clamp_selection(App *app, int profile_count)
{
    if (!app)
        return;

    if (profile_count <= 0) {
        app->selected = 0;
        return;
    }
    if (app->selected < 0)
        app->selected = 0;
    if (app->selected >= profile_count)
        app->selected = profile_count - 1;
}

static void clamp_download_selection(App *app, int catalog_count)
{
    if (!app)
        return;
    if (catalog_count <= 0) {
        app->download_selected = 0;
        return;
    }
    if (app->download_selected < 0)
        app->download_selected = 0;
    if (app->download_selected >= catalog_count)
        app->download_selected = catalog_count - 1;
}

void app_request_exit(App *app)
{
    if (app)
        app->exit_requested = 1;
}

void app_set_message(App *app, const char *text, uint32_t now_ms)
{
    if (!app)
        return;

    if (!text || !text[0]) {
        app->message[0] = '\0';
        app->message_until_ms = 0;
        return;
    }

    snprintf(app->message, sizeof(app->message), "%s", text);
    app->message_until_ms = now_ms + APP_MESSAGE_MS;
}

void app_catalog_submit_result(App *app, int rc, uint32_t now_ms)
{
    if (!app)
        return;
    if (rc == 0) {
        app->catalog_state = APP_CATALOG_FETCHING;
        return;
    }
    app_set_message(app, "Catalog fetch unavailable", now_ms);
}

void app_tick(App *app, uint32_t now_ms)
{
    if (!app || app->message_until_ms == 0)
        return;

    if ((int32_t)(now_ms - app->message_until_ms) >= 0) {
        app->message[0] = '\0';
        app->message_until_ms = 0;
    }
}

AppAction app_handle_input(App *app, uint32_t pressed, int profile_count,
                           int catalog_count, uint32_t now_ms)
{
    (void)now_ms;

    if (!app || app->exit_requested)
        return make_action(APP_ACTION_NONE, 0);

    if (pressed & APP_INPUT_CIRCLE) {
        app->exit_requested = 1;
        return make_action(APP_ACTION_EXIT, 0);
    }

    if (pressed & APP_INPUT_LEFT) {
        app->page = (AppPage)((app->page + APP_PAGE_COUNT - 1) % APP_PAGE_COUNT);
        return make_action(APP_ACTION_NONE, 0);
    }
    if (pressed & APP_INPUT_RIGHT) {
        app->page = (AppPage)((app->page + 1) % APP_PAGE_COUNT);
        return make_action(APP_ACTION_NONE, 0);
    }

    if (pressed & APP_INPUT_UP) {
        if (app->page == APP_PAGE_PROFILES && app->selected > 0)
            app->selected--;
        else if (app->page == APP_PAGE_DOWNLOADS && app->download_selected > 0)
            app->download_selected--;
        return make_action(APP_ACTION_NONE, 0);
    }
    if (pressed & APP_INPUT_DOWN) {
        if (app->page == APP_PAGE_PROFILES && profile_count > 0 &&
            app->selected < profile_count - 1)
            app->selected++;
        else if (app->page == APP_PAGE_DOWNLOADS && catalog_count > 0 &&
                 app->download_selected < catalog_count - 1)
            app->download_selected++;
        return make_action(APP_ACTION_NONE, 0);
    }

    if (pressed & APP_INPUT_SQUARE) {
        if (app->page == APP_PAGE_PROFILES)
            return make_action(APP_ACTION_REFRESH_PROFILES, 0);
        if (app->page == APP_PAGE_DOWNLOADS) {
            if (app->download_active)
                return make_action(APP_ACTION_CANCEL_DOWNLOAD, 0);
            if (app->catalog_state != APP_CATALOG_FETCHING)
                return make_action(APP_ACTION_FETCH_CATALOG, 0);
        }
        return make_action(APP_ACTION_NONE, 0);
    }

    if (pressed & APP_INPUT_TRIANGLE) {
        app->supervisor_enabled = !app->supervisor_enabled;
        return make_action(APP_ACTION_TOGGLE_SUPERVISOR, 0);
    }

    if (pressed & APP_INPUT_START) {
        if (!app->netconf_active && !app->probe_running && !app->download_active)
            return make_action(APP_ACTION_OPEN_NETCONF, 0);
        return make_action(APP_ACTION_NONE, 0);
    }

    if (pressed & APP_INPUT_CROSS) {
        switch (app->page) {
        case APP_PAGE_STATUS:
            return make_action(APP_ACTION_CONNECT, app->selected);
        case APP_PAGE_PROFILES:
            if (profile_count > 0)
                return make_action(APP_ACTION_CONNECT, app->selected);
            return make_action(APP_ACTION_NONE, 0);
        case APP_PAGE_PROBES:
            if (!app->probe_running)
                return make_action(APP_ACTION_RUN_PROBES, 0);
            return make_action(APP_ACTION_NONE, 0);
        case APP_PAGE_DOWNLOADS:
            if (app->download_active)
                return make_action(APP_ACTION_NONE, 0);
            if (app->catalog_state == APP_CATALOG_FETCHING)
                return make_action(APP_ACTION_NONE, 0);
            if (app->catalog_state == APP_CATALOG_READY && catalog_count > 0)
                return make_action(APP_ACTION_INSTALL, 0);
            return make_action(APP_ACTION_FETCH_CATALOG, 0);
        default:
            return make_action(APP_ACTION_NONE, 0);
        }
    }

    clamp_download_selection(app, catalog_count);
    return make_action(APP_ACTION_NONE, 0);
}

AppNetconfOutcome app_netconf_outcome(int netconf_ok, int apctl_state)
{
    if (!netconf_ok)
        return APP_NETCONF_FAILED;

    if (apctl_state == 4)
        return APP_NETCONF_CONNECTED;
    if (apctl_state == 0)
        return APP_NETCONF_CANCELLED;
    if (apctl_state >= 1 && apctl_state <= 3)
        return APP_NETCONF_PENDING;

    return APP_NETCONF_FAILED;
}
