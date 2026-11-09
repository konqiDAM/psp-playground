/*
 * Host tests for the portable HTTP probe parser and allowlist.
 */
#include <stdio.h>
#include <string.h>

#include "net_probe_logic.h"

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static void test_parse_204(void)
{
    const char *s = "HTTP/1.1 204 No Content\r\n\r\n";
    ProbeHttpParse p;
    CHECK(probe_http_parse(s, (int)strlen(s), &p) == PROBE_HTTP_NO_CONTENT);
    CHECK(p.status_code == 204);
    CHECK(p.header_end == (int)strlen(s));
}

static void test_parse_200_is_portal(void)
{
    const char *s = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
    ProbeHttpParse p;
    CHECK(probe_http_parse(s, (int)strlen(s), &p) == PROBE_HTTP_PORTAL);
    CHECK(p.status_code == 200);
}

static void test_parse_302_is_portal(void)
{
    const char *s = "HTTP/1.1 302 Found\r\nLocation: http://portal/\r\n\r\n";
    ProbeHttpParse p;
    CHECK(probe_http_parse(s, (int)strlen(s), &p) == PROBE_HTTP_PORTAL);
    CHECK(p.status_code == 302);
}

static void test_parse_511_is_portal(void)
{
    const char *s = "HTTP/1.1 511 Network Authentication Required\r\n\r\n";
    ProbeHttpParse p;
    CHECK(probe_http_parse(s, (int)strlen(s), &p) == PROBE_HTTP_PORTAL);
    CHECK(p.status_code == 511);
}

static void test_parse_500_is_error(void)
{
    const char *s = "HTTP/1.1 500 Oops\r\n\r\n";
    ProbeHttpParse p;
    CHECK(probe_http_parse(s, (int)strlen(s), &p) == PROBE_HTTP_ERROR);
    CHECK(p.status_code == 500);
}

static void test_parse_needs_more(void)
{
    const char *s = "HTTP/1.1 204 No";
    ProbeHttpParse p;
    CHECK(probe_http_parse(s, (int)strlen(s), &p) == PROBE_HTTP_NEED_MORE);
    CHECK(p.header_end == -1);
}

static void test_parse_malformed(void)
{
    ProbeHttpParse p;
    const char *bad = "nope\r\n\r\n";
    const char *bad_status = "HTTP/1.1 abc\r\n\r\n";
    CHECK(probe_http_parse(bad, (int)strlen(bad), &p) == PROBE_HTTP_MALFORMED);
    CHECK(probe_http_parse(bad_status, (int)strlen(bad_status), &p) ==
          PROBE_HTTP_MALFORMED);
}

static void test_classify(void)
{
    CHECK(probe_http_classify(204) == PROBE_HTTP_NO_CONTENT);
    CHECK(probe_http_classify(200) == PROBE_HTTP_PORTAL);
    CHECK(probe_http_classify(302) == PROBE_HTTP_PORTAL);
    CHECK(probe_http_classify(511) == PROBE_HTTP_PORTAL);
    CHECK(probe_http_classify(404) == PROBE_HTTP_ERROR);
    CHECK(probe_http_classify(999) == PROBE_HTTP_MALFORMED);
}

static void test_allowlist(void)
{
    CHECK(probe_request_allowed("connectivitycheck.gstatic.com",
                                "/generate_204"));
    CHECK(probe_request_allowed("CONNECTIVITYCHECK.GSTATIC.COM",
                                "/generate_204"));
    CHECK(!probe_request_allowed("evil.example", "/generate_204"));
    CHECK(!probe_request_allowed("connectivitycheck.gstatic.com", "/"));
    CHECK(!probe_request_allowed("", ""));
}

static void test_build_get(void)
{
    char buf[256];
    int n = probe_build_http_get(buf, (int)sizeof(buf),
                                 "connectivitycheck.gstatic.com",
                                 "/generate_204");
    CHECK(n == (int)strlen(buf));
    CHECK(strstr(buf, "GET /generate_204 HTTP/1.1\r\n") != NULL);
    CHECK(strstr(buf, "Host: connectivitycheck.gstatic.com\r\n") != NULL);
    CHECK(strstr(buf, "Connection: close\r\n") != NULL);
    CHECK(probe_build_http_get(buf, 8, "connectivitycheck.gstatic.com",
                               "/generate_204") < 0);
}

static void test_dotted_ipv4(void)
{
    CHECK(net_is_dotted_ipv4("1.2.3.4") == 1);
    CHECK(net_is_dotted_ipv4("0.0.0.0") == 1);
    CHECK(net_is_dotted_ipv4("255.255.255.255") == 1);
    CHECK(net_is_dotted_ipv4("256.1.1.1") == 0);
    CHECK(net_is_dotted_ipv4("1.2.3") == 0);
    CHECK(net_is_dotted_ipv4("1.2.3.4.5") == 0);
    CHECK(net_is_dotted_ipv4("01.2.3.4") == 0);
    CHECK(net_is_dotted_ipv4("") == 0);
}

int main(void)
{
    test_parse_204();
    test_parse_200_is_portal();
    test_parse_302_is_portal();
    test_parse_511_is_portal();
    test_parse_500_is_error();
    test_parse_needs_more();
    test_parse_malformed();
    test_classify();
    test_allowlist();
    test_build_get();
    test_dotted_ipv4();

    if (failures) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    printf("all net probe logic tests passed\n");
    return 0;
}
