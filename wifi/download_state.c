/*
 * download_state.c - strict, checksummed .resume format.
 */
#include "download_state.h"

#include <stdio.h>
#include <string.h>

#include "checksum.h"

static int is_hex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

static int parse_u32(const char *s, int len, uint32_t *out)
{
    uint64_t v = 0;
    int i;

    if (!s || len <= 0)
        return 0;
    for (i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9')
            return 0;
        v = v * 10u + (uint64_t)(s[i] - '0');
        if (v > 0xFFFFFFFFu)
            return 0;
    }
    *out = (uint32_t)v;
    return 1;
}

static int parse_hex32(const char *s, int len, uint32_t *out)
{
    uint32_t v = 0;
    int i;

    if (!s || len != 8)
        return 0;
    for (i = 0; i < len; i++) {
        int digit;
        if (s[i] >= '0' && s[i] <= '9')
            digit = s[i] - '0';
        else if (s[i] >= 'a' && s[i] <= 'f')
            digit = s[i] - 'a' + 10;
        else if (s[i] >= 'A' && s[i] <= 'F')
            digit = s[i] - 'A' + 10;
        else
            return 0;
        v = (v << 4) | (uint32_t)digit;
    }
    *out = v;
    return 1;
}

static int sha_valid(const char *s)
{
    size_t i;
    if (!s || strlen(s) != 64)
        return 0;
    for (i = 0; i < 64; i++) {
        if (!is_hex(s[i]))
            return 0;
    }
    return 1;
}

int dl_resume_render(const DlResume *in, char *out, int size)
{
    char prefix[512];
    int n;
    int off;
    uint32_t check;

    if (!in || !out || size <= 0)
        return -1;
    n = snprintf(prefix, sizeof(prefix),
                 "version=1\n"
                 "size=%u\n"
                 "sha256=%s\n"
                 "etag=%s\n"
                 "last_modified=%s\n"
                 "offset=%u\n",
                 (unsigned)in->size, in->sha256, in->etag,
                 in->last_modified, (unsigned)in->offset);
    if (n < 0 || n >= (int)sizeof(prefix))
        return -1;
    check = crc32_bytes(prefix, (size_t)n);
    off = snprintf(out, (size_t)size, "%scheck=%08x\n", prefix, (unsigned)check);
    if (off < 0 || off >= size || off > DL_RESUME_MAX_BYTES)
        return -1;
    return off;
}

int dl_resume_parse(const char *text, int len, DlResume *out)
{
    int pos = 0;
    int check_start = -1;
    int check_end = -1;
    int mask = 0;
    uint32_t check_value = 0;

    if (!out)
        return -1;
    memset(out, 0, sizeof(*out));
    if (!text || len <= 0 || len > DL_RESUME_MAX_BYTES)
        return -1;

    /* Find the check line and require it to be last. */
    while (pos < len) {
        int start = pos;
        int end;
        while (pos < len && text[pos] != '\n')
            pos++;
        end = pos;
        if (pos < len)
            pos++;
        if (end > start && text[end - 1] == '\r')
            end--;
        if (end - start >= 6 && strncmp(text + start, "check=", 6) == 0) {
            check_start = start;
            check_end = end;
            break;
        }
    }
    if (check_start < 0 || check_end < 0)
        return -1;
    if (!parse_hex32(text + check_start + 6, check_end - check_start - 6,
                     &check_value))
        return -1;
    /* Nothing but an optional newline may follow the check line. */
    {
        int after = check_end;
        if (after < len && text[after] == '\r')
            after++;
        if (after < len && text[after] == '\n')
            after++;
        if (after != len)
            return -1;
    }
    if (crc32_bytes(text, (size_t)check_start) != check_value)
        return -1;

    pos = 0;
    while (pos < check_start) {
        int start = pos;
        int end;
        char line[128];
        char *eq;
        int key_len;

        while (pos < check_start && text[pos] != '\n')
            pos++;
        end = pos;
        if (pos < check_start)
            pos++;
        if (end > start && text[end - 1] == '\r')
            end--;
        if (end - start <= 0 || end - start > 127)
            return -1;
        memcpy(line, text + start, (size_t)(end - start));
        line[end - start] = '\0';
        eq = strchr(line, '=');
        if (!eq)
            return -1;
        key_len = (int)(eq - line);
        eq++;
        if (key_len == 7 && strncmp(line, "version", 7) == 0) {
            if (mask & 1 || strcmp(eq, "1") != 0)
                return -1;
            mask |= 1;
        } else if (key_len == 4 && strncmp(line, "size", 4) == 0) {
            if (mask & 2 || !parse_u32(eq, (int)strlen(eq), &out->size))
                return -1;
            mask |= 2;
        } else if (key_len == 6 && strncmp(line, "sha256", 6) == 0) {
            if (mask & 4 || !sha_valid(eq) || strlen(eq) >= sizeof(out->sha256))
                return -1;
            snprintf(out->sha256, sizeof(out->sha256), "%s", eq);
            mask |= 4;
        } else if (key_len == 4 && strncmp(line, "etag", 4) == 0) {
            if (mask & 8 || strlen(eq) >= sizeof(out->etag))
                return -1;
            snprintf(out->etag, sizeof(out->etag), "%s", eq);
            mask |= 8;
        } else if (key_len == 13 && strncmp(line, "last_modified", 13) == 0) {
            if (mask & 16 || strlen(eq) >= sizeof(out->last_modified))
                return -1;
            snprintf(out->last_modified, sizeof(out->last_modified), "%s", eq);
            mask |= 16;
        } else if (key_len == 6 && strncmp(line, "offset", 6) == 0) {
            if (mask & 32 || !parse_u32(eq, (int)strlen(eq), &out->offset))
                return -1;
            mask |= 32;
        } else if (key_len == 5 && strncmp(line, "check", 5) == 0) {
            return -1; /* must be last, handled above */
        } else {
            return -1;
        }
    }

    if (mask != 0x3f)
        return -1;
    if (out->size == 0 || out->offset > out->size)
        return -1;
    return 0;
}

DlResumeResult dl_resume_usable(const DlResume *r, uint32_t expected_size,
                                const char *expected_sha, uint32_t part_size)
{
    if (!r || !expected_sha)
        return DL_RESUME_MISMATCH;
    if (r->size != expected_size)
        return DL_RESUME_MISMATCH;
    if (strcmp(r->sha256, expected_sha) != 0)
        return DL_RESUME_MISMATCH;
    if (r->offset > r->size || part_size < r->offset || part_size > r->size)
        return DL_RESUME_MISMATCH;
    if (r->offset == r->size)
        return DL_RESUME_COMPLETE;
    return DL_RESUME_OK;
}

int dl_resume_has_validator(const DlResume *r)
{
    if (!r)
        return 0;
    if (r->etag[0] && !(r->etag[0] == 'W' && r->etag[1] == '/'))
        return 1;
    return r->last_modified[0] != '\0';
}
