/*
 * Host tests for the portable HTTP response parser and request builder.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "http_logic.h"

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static int parse_str(const char *s, HttpResponseHead *h)
{
    return http_parse_head(s, (int)strlen(s), h);
}

static void test_parse_200(void)
{
    const char *s = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nETag: \"v1\"\r\n\r\n";
    HttpResponseHead h;

    CHECK(http_parse_head(s, (int)strlen(s), &h) == HTTP_PARSE_OK);
    CHECK(h.status == 200);
    CHECK(h.has_content_length && h.content_length == 5);
    CHECK(h.has_etag && h.etag_strong);
    CHECK(strcmp(h.etag, "\"v1\"") == 0);
    CHECK(h.header_end == (int)strlen(s));
}

static void test_parse_206(void)
{
    const char *s = "HTTP/1.1 206 Partial Content\r\n"
                    "Content-Range: bytes 100-199/1000\r\n"
                    "Content-Length: 100\r\n"
                    "Last-Modified: Wed, 21 Oct 2015 07:28:00 GMT\r\n\r\n";
    HttpResponseHead h;

    CHECK(parse_str(s, &h) == HTTP_PARSE_OK);
    CHECK(h.status == 206);
    CHECK(h.has_content_range);
    CHECK(h.range_start == 100 && h.range_end == 199 && h.range_total == 1000);
    CHECK(h.has_content_length && h.content_length == 100);
    CHECK(h.has_last_modified);
    CHECK(strcmp(h.last_modified, "Wed, 21 Oct 2015 07:28:00 GMT") == 0);
}

static void test_parse_weak_etag(void)
{
    const char *s = "HTTP/1.1 200 OK\r\nETag: W/\"v1\"\r\nContent-Length: 0\r\n\r\n";
    HttpResponseHead h;

    CHECK(parse_str(s, &h) == HTTP_PARSE_OK);
    CHECK(h.has_etag && !h.etag_strong);
}

static void test_parse_chunked_gzip(void)
{
    const char *chunked = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
    const char *gzip = "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\n\r\n";
    HttpResponseHead h;

    CHECK(parse_str(chunked, &h) == HTTP_PARSE_OK && h.chunked);
    CHECK(parse_str(gzip, &h) == HTTP_PARSE_OK && h.encoded);
}

static void test_parse_case_insensitive(void)
{
    const char *s = "HTTP/1.1 200 OK\r\ncontent-length: 42\r\n\r\n";
    HttpResponseHead h;

    CHECK(parse_str(s, &h) == HTTP_PARSE_OK);
    CHECK(h.content_length == 42);
}

static void test_parse_duplicate_length(void)
{
    const char *s = "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\n";
    HttpResponseHead h;

    CHECK(parse_str(s, &h) == HTTP_PARSE_MALFORMED);
}

static void test_parse_bad_ranges(void)
{
    const char *bad[] = {
        "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 100-99/1000\r\n\r\n",
        "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 100-199/*\r\n\r\n",
        "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 0-1000/1000\r\n\r\n",
        "HTTP/1.1 206 Partial Content\r\nContent-Range: items 0-1/2\r\n\r\n"
    };
    HttpResponseHead h;
    size_t i;

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
        CHECK(parse_str(bad[i], &h) == HTTP_PARSE_MALFORMED);
}

static void test_parse_bad_status(void)
{
    const char *bad[] = {
        "HTTP/1.1 20 OK\r\n\r\n",
        "ICY 200 OK\r\n\r\n"
    };
    HttpResponseHead h;

    CHECK(parse_str(bad[0], &h) == HTTP_PARSE_MALFORMED);
    CHECK(parse_str(bad[1], &h) == HTTP_PARSE_MALFORMED);
}

static void test_parse_folded(void)
{
    const char *s = "HTTP/1.1 200 OK\r\nX: a\r\n b\r\n\r\n";
    HttpResponseHead h;

    CHECK(parse_str(s, &h) == HTTP_PARSE_MALFORMED);
}

static void test_parse_too_large(void)
{
    char buf[4100];
    HttpResponseHead h;

    memset(buf, 'A', sizeof(buf));
    CHECK(http_parse_head(buf, (int)sizeof(buf), &h) == HTTP_PARSE_TOO_LARGE);
}

static void test_build_get_exact(void)
{
    char buf[512];
    int n;
    const char *want =
        "GET /psp/catalog.txt HTTP/1.1\r\n"
        "Host: 192.168.1.14:8080\r\n"
        "User-Agent: psp-wifi/0.2.0\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n\r\n";

    n = http_build_get(buf, (int)sizeof(buf), "192.168.1.14", 8080,
                       "/psp/catalog.txt", 0, 0, NULL);
    CHECK(n == (int)strlen(want));
    CHECK(strcmp(buf, want) == 0);

    n = http_build_get(buf, (int)sizeof(buf), "192.168.1.14", 80,
                       "/x", 0, 0, NULL);
    CHECK(strstr(buf, "Host: 192.168.1.14\r\n") != NULL);
}

static void test_build_range(void)
{
    char buf[512];
    int n = http_build_get(buf, (int)sizeof(buf), "host", 80, "/f",
                           1, 100, "\"v1\"");

    CHECK(n > 0);
    CHECK(strstr(buf, "Range: bytes=100-\r\n") != NULL);
    CHECK(strstr(buf, "If-Range: \"v1\"\r\n") != NULL);
}

static void test_build_rejects_injection(void)
{
    char buf[512];

    CHECK(http_build_get(buf, (int)sizeof(buf), "host", 80,
                         "/x\r\nX: y", 0, 0, NULL) < 0);
    CHECK(http_build_get(buf, (int)sizeof(buf), "host", 80, "/x",
                         0, 0, "a\r\nb") < 0);
    CHECK(http_build_get(buf, (int)sizeof(buf), "host", 80,
                         "/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                         "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                         "aaaaaaaaaaaaaaaaaaaa", 0, 0, NULL) < 0);
}

static void test_url_split(void)
{
    char host[64];
    char path[128];
    unsigned short port;

    CHECK(http_url_split("http://192.168.1.14:8080/psp/catalog.txt",
                         host, sizeof(host), &port, path, sizeof(path)) == 0);
    CHECK(strcmp(host, "192.168.1.14") == 0);
    CHECK(port == 8080);
    CHECK(strcmp(path, "/psp/catalog.txt") == 0);

    CHECK(http_url_split("http://host/", host, sizeof(host), &port,
                         path, sizeof(path)) == 0);
    CHECK(port == 80 && strcmp(path, "/") == 0);

    CHECK(http_url_split("HTTP://Host/x", host, sizeof(host), &port,
                         path, sizeof(path)) == 0);

    CHECK(http_url_split("https://host/x", host, sizeof(host), &port,
                         path, sizeof(path)) < 0);
    CHECK(http_url_split("http://host", host, sizeof(host), &port,
                         path, sizeof(path)) < 0);
    CHECK(http_url_split("http://host:0/x", host, sizeof(host), &port,
                         path, sizeof(path)) < 0);
    CHECK(http_url_split("http://host:65536/x", host, sizeof(host), &port,
                         path, sizeof(path)) < 0);
    CHECK(http_url_split("http://user@host/x", host, sizeof(host), &port,
                         path, sizeof(path)) < 0);
    CHECK(http_url_split("http:///x", host, sizeof(host), &port,
                         path, sizeof(path)) < 0);
    CHECK(http_url_split("http://host/a?b", host, sizeof(host), &port,
                         path, sizeof(path)) < 0);
    CHECK(http_url_split("http://host/a#b", host, sizeof(host), &port,
                         path, sizeof(path)) < 0);
    CHECK(http_url_split("http://host/a b", host, sizeof(host), &port,
                         path, sizeof(path)) < 0);
    CHECK(http_url_split("http://host/a/../b", host, sizeof(host), &port,
                         path, sizeof(path)) < 0);
    CHECK(http_url_split("http://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                         "aaaaaaaaaaaaaaaa/x", host, sizeof(host), &port,
                         path, sizeof(path)) < 0);
    CHECK(http_url_split("http://host/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                         "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                         "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                         host, sizeof(host), &port, path, sizeof(path)) < 0);
}

int main(void)
{
    test_parse_200();
    test_parse_206();
    test_parse_weak_etag();
    test_parse_chunked_gzip();
    test_parse_case_insensitive();
    test_parse_duplicate_length();
    test_parse_bad_ranges();
    test_parse_bad_status();
    test_parse_folded();
    test_parse_too_large();
    test_build_get_exact();
    test_build_range();
    test_build_rejects_injection();
    test_url_split();

    if (failures) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    printf("all http logic tests passed\n");
    return 0;
}
