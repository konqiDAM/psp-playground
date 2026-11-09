/*
 * Host tests for the checksummed .resume state.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "checksum.h"
#include "download_state.h"

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                        \
        }                                                                      \
    } while (0)

#define SHA "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"

static void make(DlResume *r, uint32_t offset)
{
    memset(r, 0, sizeof(*r));
    r->size = 2097152;
    memcpy(r->sha256, SHA, 65);
    snprintf(r->etag, sizeof(r->etag), "\"v1\"");
    snprintf(r->last_modified, sizeof(r->last_modified),
             "Wed, 21 Oct 2015 07:28:00 GMT");
    r->offset = offset;
}

static void test_roundtrip(void)
{
    DlResume in, out;
    char text[512];
    int n;

    make(&in, 1048576);
    n = dl_resume_render(&in, text, sizeof(text));
    CHECK(n > 0);
    CHECK(n < 512);
    CHECK(dl_resume_parse(text, n, &out) == 0);
    CHECK(out.size == in.size);
    CHECK(strcmp(out.sha256, in.sha256) == 0);
    CHECK(strcmp(out.etag, in.etag) == 0);
    CHECK(strcmp(out.last_modified, in.last_modified) == 0);
    CHECK(out.offset == in.offset);
}

static void test_crc_detects_bit_flip(void)
{
    DlResume in, out;
    char text[512];
    int n;

    make(&in, 1000);
    n = dl_resume_render(&in, text, sizeof(text));
    text[5] ^= 0x01;
    CHECK(dl_resume_parse(text, n, &out) < 0);
}

static void test_truncated(void)
{
    DlResume in, out;
    char text[512];
    int n;

    make(&in, 1000);
    n = dl_resume_render(&in, text, sizeof(text));
    CHECK(dl_resume_parse(text, n - 3, &out) < 0);
}

static void test_bad_version_unknown_key(void)
{
    DlResume in, out;
    char text[512];
    char bad[512];
    int n;

    make(&in, 1000);
    n = dl_resume_render(&in, text, sizeof(text));
    snprintf(bad, sizeof(bad), "%s", text);
    memcpy(bad, "version=2", 9);
    CHECK(dl_resume_parse(bad, n, &out) < 0);

    make(&in, 1000);
    n = dl_resume_render(&in, text, sizeof(text));
    {
        const char *chk = strstr(text, "check=");
        int prefix = (int)(chk - text);
        uint32_t crc;
        memcpy(bad, text, (size_t)prefix);
        memcpy(bad + prefix, "extra=1\n", 8);
        crc = crc32_bytes(bad, (size_t)(prefix + 8));
        snprintf(bad + prefix + 8, sizeof(bad) - (size_t)(prefix + 8),
                 "check=%08x\n", (unsigned)crc);
        CHECK(dl_resume_parse(bad, (int)strlen(bad), &out) < 0);
    }
}

static void test_usable(void)
{
    DlResume r;

    make(&r, 1000);
    CHECK(dl_resume_usable(&r, r.size, r.sha256, 1000) == DL_RESUME_OK);
    CHECK(dl_resume_usable(&r, r.size, r.sha256, 999) == DL_RESUME_MISMATCH);
    CHECK(dl_resume_usable(&r, r.size + 1, r.sha256, 1000) ==
          DL_RESUME_MISMATCH);
    CHECK(dl_resume_usable(&r, r.size, SHA "b", 1000) == DL_RESUME_MISMATCH);
    CHECK(dl_resume_usable(&r, r.size, r.sha256, r.size + 1) ==
          DL_RESUME_MISMATCH);

    r.offset = r.size;
    CHECK(dl_resume_usable(&r, r.size, r.sha256, r.size) == DL_RESUME_COMPLETE);
    CHECK(dl_resume_usable(&r, r.size, r.sha256, r.size - 1) ==
          DL_RESUME_MISMATCH);
}

static void test_validator(void)
{
    DlResume r;

    make(&r, 0);
    CHECK(dl_resume_has_validator(&r) == 1);
    snprintf(r.etag, sizeof(r.etag), "%s", "");
    CHECK(dl_resume_has_validator(&r) == 1); /* Last-Modified fallback */
    snprintf(r.last_modified, sizeof(r.last_modified), "%s", "");
    CHECK(dl_resume_has_validator(&r) == 0);
    snprintf(r.etag, sizeof(r.etag), "W/\"weak\"");
    CHECK(dl_resume_has_validator(&r) == 0);
}

int main(void)
{
    test_roundtrip();
    test_crc_detects_bit_flip();
    test_truncated();
    test_bad_version_unknown_key();
    test_usable();
    test_validator();

    if (failures) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    printf("all download state tests passed\n");
    return 0;
}
