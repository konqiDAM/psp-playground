/*
 * Host tests for the portable WiFi Connection page/action state.
 */
#include <stdio.h>
#include <string.h>

#include "app.h"

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static void test_init(void)
{
    App a;
    app_init(&a);
    CHECK(a.page == APP_PAGE_STATUS);
    CHECK(a.selected == 0);
    CHECK(!a.exit_requested);
    CHECK(!a.probe_running);
    CHECK(!a.netconf_active);
    CHECK(a.message[0] == '\0');
}

static void test_page_cycle(void)
{
    App a;
    app_init(&a);
    CHECK(app_handle_input(&a, APP_INPUT_RIGHT, 0, 0, 0).type == APP_ACTION_NONE);
    CHECK(a.page == APP_PAGE_PROFILES);
    CHECK(app_handle_input(&a, APP_INPUT_LEFT, 0, 0, 0).type == APP_ACTION_NONE);
    CHECK(a.page == APP_PAGE_STATUS);
    CHECK(app_handle_input(&a, APP_INPUT_LEFT, 0, 0, 0).type == APP_ACTION_NONE);
    CHECK(a.page == APP_PAGE_HELP);
}

static void test_five_page_cycle(void)
{
    App a;
    app_init(&a);
    app_handle_input(&a, APP_INPUT_RIGHT, 0, 0, 0);
    CHECK(a.page == APP_PAGE_PROFILES);
    app_handle_input(&a, APP_INPUT_RIGHT, 0, 0, 0);
    CHECK(a.page == APP_PAGE_PROBES);
    app_handle_input(&a, APP_INPUT_RIGHT, 0, 0, 0);
    CHECK(a.page == APP_PAGE_DOWNLOADS);
    app_handle_input(&a, APP_INPUT_RIGHT, 0, 0, 0);
    CHECK(a.page == APP_PAGE_HELP);
    app_handle_input(&a, APP_INPUT_RIGHT, 0, 0, 0);
    CHECK(a.page == APP_PAGE_STATUS);
}

static void test_selection_clamps(void)
{
    App a;
    app_init(&a);
    a.page = APP_PAGE_PROFILES;
    app_handle_input(&a, APP_INPUT_DOWN, 2, 0, 0);
    CHECK(a.selected == 1);
    app_handle_input(&a, APP_INPUT_DOWN, 2, 0, 0);
    CHECK(a.selected == 1);
    app_handle_input(&a, APP_INPUT_UP, 2, 0, 0);
    CHECK(a.selected == 0);
    app_handle_input(&a, APP_INPUT_UP, 0, 0, 0);
    CHECK(a.selected == 0);
}

static void test_profiles_cross(void)
{
    App a;
    AppAction act;
    app_init(&a);
    a.page = APP_PAGE_PROFILES;
    a.selected = 1;
    act = app_handle_input(&a, APP_INPUT_CROSS, 3, 0, 0);
    CHECK(act.type == APP_ACTION_CONNECT);
    CHECK(act.profile_pos == 1);
    CHECK(app_handle_input(&a, APP_INPUT_CROSS, 0, 0, 0).type == APP_ACTION_NONE);
}

static void test_probes_cross_and_busy(void)
{
    App a;
    app_init(&a);
    a.page = APP_PAGE_PROBES;
    CHECK(app_handle_input(&a, APP_INPUT_CROSS, 0, 0, 0).type ==
          APP_ACTION_RUN_PROBES);
    a.probe_running = 1;
    CHECK(app_handle_input(&a, APP_INPUT_CROSS, 0, 0, 0).type == APP_ACTION_NONE);
}

static void test_start_netconf(void)
{
    App a;
    app_init(&a);
    CHECK(app_handle_input(&a, APP_INPUT_START, 0, 0, 0).type ==
          APP_ACTION_OPEN_NETCONF);
    a.netconf_active = 1;
    CHECK(app_handle_input(&a, APP_INPUT_START, 0, 0, 0).type == APP_ACTION_NONE);
    a.netconf_active = 0;
    a.download_active = 1;
    CHECK(app_handle_input(&a, APP_INPUT_START, 0, 0, 0).type == APP_ACTION_NONE);
}

static void test_square_refresh(void)
{
    App a;
    app_init(&a);
    a.page = APP_PAGE_PROFILES;
    CHECK(app_handle_input(&a, APP_INPUT_SQUARE, 0, 0, 0).type ==
          APP_ACTION_REFRESH_PROFILES);
}

static void test_triangle_toggle(void)
{
    App a;
    app_init(&a);
    CHECK(app_handle_input(&a, APP_INPUT_TRIANGLE, 0, 0, 0).type ==
          APP_ACTION_TOGGLE_SUPERVISOR);
    CHECK(a.supervisor_enabled == 1);
}

static void test_circle_sets_exit(void)
{
    App a;
    app_init(&a);
    CHECK(app_handle_input(&a, APP_INPUT_CIRCLE, 0, 0, 0).type == APP_ACTION_EXIT);
    CHECK(a.exit_requested == 1);
    CHECK(app_handle_input(&a, APP_INPUT_CROSS, 0, 0, 0).type == APP_ACTION_NONE);
}

static void test_clamp_after_shrink(void)
{
    App a;
    app_init(&a);
    a.selected = 4;
    app_clamp_selection(&a, 2);
    CHECK(a.selected == 1);
    app_clamp_selection(&a, 0);
    CHECK(a.selected == 0);
}

static void test_message_expiry(void)
{
    App a;
    app_init(&a);
    app_set_message(&a, "hello", 1000);
    CHECK(strcmp(a.message, "hello") == 0);
    app_tick(&a, 3999);
    CHECK(a.message[0] != '\0');
    app_tick(&a, 4000);
    CHECK(a.message[0] == '\0');
}

static void test_netconf_outcomes(void)
{
    CHECK(app_netconf_outcome(0, 4) == APP_NETCONF_FAILED);
    CHECK(app_netconf_outcome(1, 4) == APP_NETCONF_CONNECTED);
    CHECK(app_netconf_outcome(1, 0) == APP_NETCONF_CANCELLED);
    CHECK(app_netconf_outcome(1, 2) == APP_NETCONF_PENDING);
    CHECK(app_netconf_outcome(1, 9) == APP_NETCONF_FAILED);
}

static void test_downloads_selection(void)
{
    App a;
    app_init(&a);
    a.page = APP_PAGE_DOWNLOADS;
    CHECK(app_handle_input(&a, APP_INPUT_DOWN, 0, 3, 0).type ==
          APP_ACTION_NONE);
    CHECK(a.download_selected == 1);
    app_handle_input(&a, APP_INPUT_DOWN, 0, 3, 0);
    CHECK(a.download_selected == 2);
    app_handle_input(&a, APP_INPUT_DOWN, 0, 3, 0);
    CHECK(a.download_selected == 2);
    app_handle_input(&a, APP_INPUT_UP, 0, 3, 0);
    CHECK(a.download_selected == 1);
    app_handle_input(&a, APP_INPUT_UP, 0, 0, 0);
    CHECK(a.download_selected == 0);
}

static void test_downloads_cross(void)
{
    App a;
    app_init(&a);
    a.page = APP_PAGE_DOWNLOADS;
    CHECK(app_handle_input(&a, APP_INPUT_CROSS, 0, 0, 0).type ==
          APP_ACTION_FETCH_CATALOG);

    a.catalog_state = APP_CATALOG_READY;
    CHECK(app_handle_input(&a, APP_INPUT_CROSS, 0, 3, 0).type ==
          APP_ACTION_INSTALL);

    a.download_active = 1;
    CHECK(app_handle_input(&a, APP_INPUT_CROSS, 0, 3, 0).type ==
          APP_ACTION_NONE);
}

static void test_downloads_square(void)
{
    App a;
    app_init(&a);
    a.page = APP_PAGE_DOWNLOADS;
    CHECK(app_handle_input(&a, APP_INPUT_SQUARE, 0, 3, 0).type ==
          APP_ACTION_FETCH_CATALOG);
    a.download_active = 1;
    CHECK(app_handle_input(&a, APP_INPUT_SQUARE, 0, 3, 0).type ==
          APP_ACTION_CANCEL_DOWNLOAD);
    a.download_active = 0;
    a.catalog_state = APP_CATALOG_FETCHING;
    CHECK(app_handle_input(&a, APP_INPUT_SQUARE, 0, 3, 0).type ==
          APP_ACTION_NONE);
}

static void test_catalog_submit_result(void)
{
    App a;
    app_init(&a);
    a.catalog_state = APP_CATALOG_ERROR;
    app_catalog_submit_result(&a, 0, 1000);
    CHECK(a.catalog_state == APP_CATALOG_FETCHING);

    a.catalog_state = APP_CATALOG_ERROR;
    app_catalog_submit_result(&a, -4, 1000);
    CHECK(a.catalog_state == APP_CATALOG_ERROR);
    CHECK(strcmp(a.message, "Catalog fetch unavailable") == 0);
}

static void test_circle_exits_during_download(void)
{
    App a;
    app_init(&a);
    a.download_active = 1;
    CHECK(app_handle_input(&a, APP_INPUT_CIRCLE, 0, 3, 0).type ==
          APP_ACTION_EXIT);
    CHECK(a.exit_requested == 1);
}

int main(void)
{
    test_init();
    test_page_cycle();
    test_five_page_cycle();
    test_selection_clamps();
    test_profiles_cross();
    test_probes_cross_and_busy();
    test_start_netconf();
    test_square_refresh();
    test_triangle_toggle();
    test_circle_sets_exit();
    test_clamp_after_shrink();
    test_message_expiry();
    test_netconf_outcomes();
    test_downloads_selection();
    test_downloads_cross();
    test_downloads_square();
    test_catalog_submit_result();
    test_circle_exits_during_download();

    if (failures) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    printf("all wifi app tests passed\n");
    return 0;
}
