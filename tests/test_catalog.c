/*
 * Host tests for the catalog manifest, URL and config parser.
 */
#include <stdio.h>
#include <string.h>

#include "catalog.h"

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static const char *k_manifest =
    "PSPWIFI-CATALOG 1\n"
    "# Current PSP builds.\n"
    "id=wifi\n"
    "title=WiFi Connection\n"
    "version=0.2.0\n"
    "size=173456\n"
    "sha256=9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08\n"
    "folder=WiFiConnection\n"
    "path=wifi/EBOOT.PBP\n"
    "--\n"
    "id=racing\n"
    "title=Racing 3D\n"
    "version=0.4.0\n"
    "size=196023\n"
    "sha256=b580e36d83ede39d3d2e7b47e7296118c8d395ff867b0bfa38a317c934d0024a\n"
    "folder=Racing3D\n"
    "path=racing/EBOOT.PBP\n";

static int parse(const char *s, Catalog *c)
{
    return catalog_parse(s, (int)strlen(s), c);
}

static void test_parse_two_entries(void)
{
    Catalog c;

    CHECK(parse(k_manifest, &c) == CATALOG_OK);
    CHECK(c.count == 2);
    CHECK(strcmp(c.entries[0].id, "wifi") == 0);
    CHECK(strcmp(c.entries[0].title, "WiFi Connection") == 0);
    CHECK(strcmp(c.entries[0].version, "0.2.0") == 0);
    CHECK(c.entries[0].size == 173456u);
    CHECK(strcmp(c.entries[0].sha256,
                 "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08") == 0);
    CHECK(strcmp(c.entries[0].folder, "WiFiConnection") == 0);
    CHECK(strcmp(c.entries[0].path, "wifi/EBOOT.PBP") == 0);
    CHECK(strcmp(c.entries[1].id, "racing") == 0);
    CHECK(c.entries[1].size == 196023u);
}

static void test_parse_empty(void)
{
    Catalog c;

    CHECK(parse("PSPWIFI-CATALOG 1\n", &c) == CATALOG_OK);
    CHECK(c.count == 0);
}

static void test_parse_crlf_comments_blank(void)
{
    Catalog c;
    const char *s = "PSPWIFI-CATALOG 1\r\n\r\n# c\r\n"
                    "id=a\r\ntitle=A\r\nversion=1.0.0\r\nsize=1\r\n"
                    "sha256=0000000000000000000000000000000000000000000000000000000000000000\r\n"
                    "folder=A\r\npath=a/EBOOT.PBP\r\n";

    CHECK(parse(s, &c) == CATALOG_OK);
    CHECK(c.count == 1);
}

static void test_parse_rejects(void)
{
    Catalog c;
    char big[8300];
    char line[300];
    int i;

    CHECK(parse("PSPWIFI-CATALOG 2\n", &c) == CATALOG_ERR_HEADER);
    CHECK(parse("id=a\n", &c) == CATALOG_ERR_HEADER);
    CHECK(parse("PSPWIFI-CATALOG 1\nid=a\n", &c) == CATALOG_ERR_FIELD);
    CHECK(parse("PSPWIFI-CATALOG 1\nid=a\nid=b\n", &c) == CATALOG_ERR_FIELD);
    CHECK(parse("PSPWIFI-CATALOG 1\nid=a\nunknown=x\n", &c) == CATALOG_ERR_FIELD);

    /* 17 entries */
    memset(big, 0, sizeof(big));
    strcpy(big, "PSPWIFI-CATALOG 1\n");
    for (i = 0; i < 17; i++) {
        char rec[256];
        snprintf(rec, sizeof(rec),
                 "id=a%d\ntitle=A%d\nversion=1.0.0\nsize=1\n"
                 "sha256=0000000000000000000000000000000000000000000000000000000000000000\n"
                 "folder=F%d\npath=p%d/EBOOT.PBP\n--\n", i, i, i, i);
        strcat(big, rec);
    }
    CHECK(parse(big, &c) == CATALOG_ERR_TOO_MANY);

    /* oversized */
    memset(big, 'A', 8193);
    big[8193] = '\0';
    CHECK(catalog_parse(big, 8193, &c) == CATALOG_ERR_TOO_LARGE);

    /* overlong line */
    memset(line, 'a', 260);
    line[0] = 'P'; /* not a header, but line length must fail first */
    line[259] = '\0';
    CHECK(catalog_parse(line, 259, &c) != CATALOG_OK);
}

static void expect_bad(const char *bad_key, const char *bad_value)
{
    static const char *keys[7] = {
        "id", "title", "version", "size", "sha256", "folder", "path"
    };
    static const char *defaults[7] = {
        "a", "A", "1.0.0", "1",
        "0000000000000000000000000000000000000000000000000000000000000000",
        "A", "a/EBOOT.PBP"
    };
    char manifest[1024];
    int off;
    int i;

    off = snprintf(manifest, sizeof(manifest), "PSPWIFI-CATALOG 1\n");
    for (i = 0; i < 7; i++) {
        const char *v = strcmp(keys[i], bad_key) == 0 ? bad_value : defaults[i];
        off += snprintf(manifest + off, sizeof(manifest) - (size_t)off,
                        "%s=%s\n", keys[i], v);
    }
    {
        Catalog c;
        if (catalog_parse(manifest, off, &c) == CATALOG_OK) {
            fprintf(stderr, "FAIL bad field %s=%s accepted\n",
                    bad_key, bad_value);
            failures++;
        }
    }
}

static void test_parse_bad_values(void)
{
    expect_bad("id", "WiFi");
    expect_bad("id", "12345678901234567890123456789012");
    expect_bad("title", "");
    expect_bad("title", "a\x01b");
    expect_bad("title", "123456789012345678901234567890123456789012345678");
    expect_bad("version", "1.2");
    expect_bad("version", "1.2.3.4");
    expect_bad("size", "0");
    expect_bad("size", "67108865");
    expect_bad("size", "-1");
    expect_bad("sha256", "abc");
    expect_bad("sha256",
               "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz");
    expect_bad("folder", "../x");
    expect_bad("folder", "CON");
    expect_bad("folder", "a/b");
    expect_bad("folder", "12345678901234567890123456789012");
    expect_bad("path", "/abs");
    expect_bad("path", "a/../b");
    expect_bad("path", "a\\b");
    expect_bad("path", "a/EBOOT.PBP?x");
    expect_bad("path", "a/OTHER.BIN");
}

static void test_version_compare(void)
{
    CHECK(catalog_version_compare("0.2.0", "0.1.0") > 0);
    CHECK(catalog_version_compare("0.10.0", "0.9.9") > 0);
    CHECK(catalog_version_compare("1.2.3", "1.2.3") == 0);
    CHECK(catalog_version_compare("1.2", "1.2.3") == 2);
    CHECK(catalog_version_compare("1.2.3.4", "1.2.3") == 2);
}

static void test_url_parse(void)
{
    CatalogUrl u;

    CHECK(catalog_url_parse("http://192.168.1.14:8080/psp/catalog.txt", &u) == 0);
    CHECK(strcmp(u.host, "192.168.1.14") == 0);
    CHECK(u.port == 8080);
    CHECK(strcmp(u.path, "/psp/catalog.txt") == 0);
    CHECK(strcmp(u.base, "http://192.168.1.14:8080/psp/") == 0);

    CHECK(catalog_url_parse("HTTP://host/x", &u) == 0);
    CHECK(catalog_url_parse("https://host/x", &u) < 0);
    CHECK(catalog_url_parse("ftp://host/x", &u) < 0);
    CHECK(catalog_url_parse("http://user@host/x", &u) < 0);
    CHECK(catalog_url_parse("http://host:0/x", &u) < 0);
    CHECK(catalog_url_parse("http://host:65536/x", &u) < 0);
    CHECK(catalog_url_parse("http:///x", &u) < 0);
    CHECK(catalog_url_parse("http://host", &u) < 0);
    CHECK(catalog_url_parse("http://host/a?b", &u) < 0);
    CHECK(catalog_url_parse("http://host/a#b", &u) < 0);
    CHECK(catalog_url_parse("http://host/a/../b", &u) < 0);
}

static void test_url_join(void)
{
    CatalogUrl u;
    char out[192];

    CHECK(catalog_url_parse("http://host:8080/psp/catalog.txt", &u) == 0);
    CHECK(catalog_url_join(&u, "wifi/EBOOT.PBP", out, sizeof(out)) == 0);
    CHECK(strcmp(out, "http://host:8080/psp/wifi/EBOOT.PBP") == 0);

    CHECK(catalog_url_join(&u, "http://evil/x", out, sizeof(out)) < 0);
    CHECK(catalog_url_join(&u, "/abs/EBOOT.PBP", out, sizeof(out)) < 0);
    CHECK(catalog_url_join(&u, "a/../EBOOT.PBP", out, sizeof(out)) < 0);
    CHECK(catalog_url_join(&u, "a\\EBOOT.PBP", out, sizeof(out)) < 0);
    CHECK(catalog_url_join(&u, "a/EBOOT.PBP?x", out, sizeof(out)) < 0);
    CHECK(catalog_url_join(&u, "a/EBOOT.PBP#x", out, sizeof(out)) < 0);
}

static void test_config_parse(void)
{
    char url[192];

    memset(url, 0, sizeof(url));
    CHECK(catalog_config_parse("url=http://host:8080/psp/catalog.txt\n",
                               (int)strlen("url=http://host:8080/psp/catalog.txt\n"),
                               url, sizeof(url)) == 0);
    CHECK(strcmp(url, "http://host:8080/psp/catalog.txt") == 0);

    url[0] = '\0';
    CHECK(catalog_config_parse("# c\n\nurl=http://host/x\r\n",
                               (int)strlen("# c\n\nurl=http://host/x\r\n"),
                               url, sizeof(url)) == 0);
    CHECK(strcmp(url, "http://host/x") == 0);

    url[0] = '\0';
    CHECK(catalog_config_parse("other=1\n", 8, url, sizeof(url)) == 0);
    CHECK(url[0] == '\0');

    CHECK(catalog_config_parse("url=http://a/x\nurl=http://b/x\n",
                               (int)strlen("url=http://a/x\nurl=http://b/x\n"),
                               url, sizeof(url)) < 0);
    CHECK(catalog_config_parse("url=https://a/x\n",
                               (int)strlen("url=https://a/x\n"),
                               url, sizeof(url)) < 0);
    {
        char big[2049];
        memset(big, 'a', sizeof(big));
        CHECK(catalog_config_parse(big, (int)sizeof(big), url, sizeof(url)) < 0);
    }
}

int main(void)
{
    test_parse_two_entries();
    test_parse_empty();
    test_parse_crlf_comments_blank();
    test_parse_rejects();
    test_parse_bad_values();
    test_version_compare();
    test_url_parse();
    test_url_join();
    test_config_parse();

    if (failures) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    printf("all catalog tests passed\n");
    return 0;
}
