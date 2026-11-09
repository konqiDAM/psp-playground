/*
 * Host tests for the install registry, target paths and planner.
 */
#include <stdio.h>
#include <string.h>

#include "catalog.h"
#include "install_state.h"

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static CatalogEntry entry(const char *id, const char *version, uint32_t size,
                          const char *folder)
{
    CatalogEntry e;
    memset(&e, 0, sizeof(e));
    snprintf(e.id, sizeof(e.id), "%s", id);
    snprintf(e.title, sizeof(e.title), "%s", id);
    snprintf(e.version, sizeof(e.version), "%s", version);
    e.size = size;
    memset(e.sha256, 'a', 64);
    e.sha256[64] = '\0';
    snprintf(e.folder, sizeof(e.folder), "%s", folder);
    snprintf(e.path, sizeof(e.path), "%s/EBOOT.PBP", id);
    return e;
}

static void test_target_path(void)
{
    CatalogEntry e = entry("wifi", "0.2.0", 100, "WiFiConnection");
    char out[176];

    CHECK(install_target_path(&e, out, sizeof(out)) == 0);
    CHECK(strcmp(out, "ms0:/PSP/GAME/HOMEBREW/WiFiConnection/EBOOT.PBP") == 0);

    snprintf(e.folder, sizeof(e.folder), "%s", "");
    CHECK(install_target_path(&e, out, sizeof(out)) < 0);
    snprintf(e.folder, sizeof(e.folder), "%s", ".");
    CHECK(install_target_path(&e, out, sizeof(out)) < 0);
    snprintf(e.folder, sizeof(e.folder), "%s", "..");
    CHECK(install_target_path(&e, out, sizeof(out)) < 0);
    snprintf(e.folder, sizeof(e.folder), "%s", "a/b");
    CHECK(install_target_path(&e, out, sizeof(out)) < 0);
    snprintf(e.folder, sizeof(e.folder), "%s", "a\\b");
    CHECK(install_target_path(&e, out, sizeof(out)) < 0);
    snprintf(e.folder, sizeof(e.folder), "%s", "CON");
    CHECK(install_target_path(&e, out, sizeof(out)) < 0);
    snprintf(e.folder, sizeof(e.folder), "%s", "NUL");
    CHECK(install_target_path(&e, out, sizeof(out)) < 0);
    snprintf(e.folder, sizeof(e.folder), "%s", "COM1");
    CHECK(install_target_path(&e, out, sizeof(out)) < 0);
    snprintf(e.folder, sizeof(e.folder), "%s", " A");
    CHECK(install_target_path(&e, out, sizeof(out)) < 0);
}

static void test_registry_roundtrip(void)
{
    InstallRegistry r;
    InstallRegistry back;
    char text[4096];
    int n;

    memset(&r, 0, sizeof(r));
    r.count = 2;
    snprintf(r.records[0].id, sizeof(r.records[0].id), "wifi");
    snprintf(r.records[0].version, sizeof(r.records[0].version), "0.2.0");
    r.records[0].size = 173456;
    memset(r.records[0].sha256, 'a', 64);
    r.records[0].sha256[64] = '\0';
    snprintf(r.records[0].path, sizeof(r.records[0].path),
             "ms0:/PSP/GAME/HOMEBREW/WiFiConnection/EBOOT.PBP");
    snprintf(r.records[1].id, sizeof(r.records[1].id), "racing");
    snprintf(r.records[1].version, sizeof(r.records[1].version), "0.4.0");
    r.records[1].size = 196023;
    memset(r.records[1].sha256, 'b', 64);
    r.records[1].sha256[64] = '\0';
    snprintf(r.records[1].path, sizeof(r.records[1].path),
             "ms0:/PSP/GAME/HOMEBREW/Racing3D/EBOOT.PBP");

    n = installed_render(&r, text, sizeof(text));
    CHECK(n > 0);
    CHECK(installed_parse(text, n, &back) == 0);
    CHECK(back.count == 2);
    CHECK(strcmp(back.records[0].id, "wifi") == 0);
    CHECK(back.records[0].size == 173456u);
    CHECK(strcmp(back.records[1].path,
                 "ms0:/PSP/GAME/HOMEBREW/Racing3D/EBOOT.PBP") == 0);
}

static void test_registry_rejects(void)
{
    InstallRegistry r;
    char text[4200];
    int i;

    CHECK(installed_parse("bad\n", (int)strlen("bad\n"), &r) < 0);
    CHECK(installed_parse("PSPWIFI-INSTALLED 1\nunknown=x\n", (int)strlen("PSPWIFI-INSTALLED 1\nunknown=x\n"), &r) < 0);
    CHECK(installed_parse("PSPWIFI-INSTALLED 1\nid=a\nid=b\n", (int)strlen("PSPWIFI-INSTALLED 1\nid=a\nid=b\n"), &r) < 0);
    CHECK(installed_parse("PSPWIFI-INSTALLED 1\n", (int)strlen("PSPWIFI-INSTALLED 1\n"), &r) == 0);

    memset(text, 0, sizeof(text));
    strcpy(text, "PSPWIFI-INSTALLED 1\n");
    for (i = 0; i < 17; i++) {
        char rec[256];
        snprintf(rec, sizeof(rec),
                 "id=a%d\nversion=1.0.0\nsize=1\n"
                 "sha256=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
                 "path=ms0:/PSP/GAME/HOMEBREW/F%d/EBOOT.PBP\n--\n", i, i);
        strcat(text, rec);
    }
    CHECK(installed_parse(text, (int)strlen(text), &r) < 0);

    CHECK(installed_parse("PSPWIFI-INSTALLED 1\nid=a\nversion=1.0.0\nsize=1\n"
                          "sha256=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
                          "path=ms0:/PSP/GAME/OTHER/EBOOT.PBP\n", (int)strlen("PSPWIFI-INSTALLED 1\nid=a\nversion=1.0.0\nsize=1\nsha256=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\npath=ms0:/PSP/GAME/OTHER/EBOOT.PBP\n"), &r) < 0);
}

static void test_plan_self(void)
{
    CatalogEntry e = entry("wifi", "0.2.0", 100, "WiFiConnection");
    InstallRegistry r;

    memset(&r, 0, sizeof(r));
    CHECK(install_plan(&e, &r, 0, 0, UINT32_MAX, "wifi") == PLAN_SELF);
}

static void test_plan_install(void)
{
    CatalogEntry e = entry("snake", "0.1.3", 1000, "Snake");
    InstallRegistry r;

    memset(&r, 0, sizeof(r));
    CHECK(install_plan(&e, &r, 0, 0, 100u * 1024u * 1024u, "wifi") ==
          PLAN_INSTALL);
}

static void test_plan_unverified(void)
{
    CatalogEntry e = entry("snake", "0.1.3", 1000, "Snake");
    InstallRegistry r;

    memset(&r, 0, sizeof(r));
    CHECK(install_plan(&e, &r, 1, 1000, 100u * 1024u * 1024u, "wifi") ==
          PLAN_UNVERIFIED);
    CHECK(install_plan(&e, &r, 1, 999, 100u * 1024u * 1024u, "wifi") ==
          PLAN_UPDATE);
}

static void test_plan_installed(void)
{
    CatalogEntry e = entry("snake", "0.1.3", 1000, "Snake");
    InstallRegistry r;

    memset(&r, 0, sizeof(r));
    r.count = 1;
    snprintf(r.records[0].id, sizeof(r.records[0].id), "snake");
    snprintf(r.records[0].version, sizeof(r.records[0].version), "0.1.3");
    r.records[0].size = 1000;
    memcpy(r.records[0].sha256, e.sha256, 65);
    snprintf(r.records[0].path, sizeof(r.records[0].path),
             "ms0:/PSP/GAME/HOMEBREW/Snake/EBOOT.PBP");
    CHECK(install_plan(&e, &r, 1, 1000, 100u * 1024u * 1024u, "wifi") ==
          PLAN_INSTALLED);

    snprintf(r.records[0].version, sizeof(r.records[0].version), "0.1.2");
    CHECK(install_plan(&e, &r, 1, 1000, 100u * 1024u * 1024u, "wifi") ==
          PLAN_UPDATE);
}

static void test_plan_broken(void)
{
    CatalogEntry e = entry("snake", "0.1.3", 1000, "Snake");
    InstallRegistry r;

    memset(&r, 0, sizeof(r));
    r.count = 1;
    snprintf(r.records[0].id, sizeof(r.records[0].id), "snake");
    snprintf(r.records[0].version, sizeof(r.records[0].version), "0.1.3");
    r.records[0].size = 1000;
    memcpy(r.records[0].sha256, e.sha256, 65);
    CHECK(install_plan(&e, &r, 0, 0, 100u * 1024u * 1024u, "wifi") ==
          PLAN_BROKEN);
}

static void test_plan_no_space(void)
{
    CatalogEntry e = entry("snake", "0.1.3", 1000, "Snake");
    InstallRegistry r;

    memset(&r, 0, sizeof(r));
    CHECK(install_plan(&e, &r, 0, 0, 1000 + INSTALL_MARGIN_BYTES - 1, "wifi") ==
          PLAN_NO_SPACE);
    CHECK(install_plan(&e, &r, 0, 0, 1000 + INSTALL_MARGIN_BYTES, "wifi") ==
          PLAN_INSTALL);
    CHECK(install_plan(&e, &r, 0, 0, UINT32_MAX, "wifi") == PLAN_INSTALL);
}

static void test_free_mapping(void)
{
    CHECK(install_free_or_unknown(0, 1234) == 1234);
    CHECK(install_free_or_unknown(1, 0) == UINT32_MAX);
    CHECK(install_free_or_unknown(-1, 0) == UINT32_MAX);
}

static void test_bad_slot(void)
{
    uint8_t used[100];

    memset(used, 0, sizeof(used));
    CHECK(install_bad_slot(used, 100) == 0);
    used[0] = 1;
    used[1] = 1;
    CHECK(install_bad_slot(used, 100) == 2);
    memset(used, 1, 98);
    CHECK(install_bad_slot(used, 100) == 98);
    used[98] = 1;
    CHECK(install_bad_slot(used, 100) == 99);
    used[99] = 1;
    CHECK(install_bad_slot(used, 100) == -1); /* full -> abort, keep archives */
    CHECK(install_bad_slot(NULL, 100) == 0);
}

static void test_plan_names(void)
{
    CHECK(strcmp(install_plan_name(PLAN_INSTALL), "?") != 0);
    CHECK(strcmp(install_plan_name(PLAN_UPDATE), "?") != 0);
    CHECK(strcmp(install_plan_name(PLAN_INSTALLED), "?") != 0);
    CHECK(strcmp(install_plan_name(PLAN_UNVERIFIED), "?") != 0);
    CHECK(strcmp(install_plan_name(PLAN_SELF), "?") != 0);
    CHECK(strcmp(install_plan_name(PLAN_BROKEN), "?") != 0);
    CHECK(strcmp(install_plan_name(PLAN_NO_SPACE), "?") != 0);
}

int main(void)
{
    test_target_path();
    test_registry_roundtrip();
    test_registry_rejects();
    test_plan_self();
    test_plan_install();
    test_plan_unverified();
    test_plan_installed();
    test_plan_broken();
    test_plan_no_space();
    test_free_mapping();
    test_bad_slot();
    test_plan_names();

    if (failures) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    printf("all install state tests passed\n");
    return 0;
}
