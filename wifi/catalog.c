/*
 * catalog.c - strict catalog manifest, URL and config parsing.
 */
#include "catalog.h"

#include <stdio.h>
#include <string.h>

#include "http_logic.h"

#define CATALOG_LINE_MAX 256

static int ascii_lower(int c)
{
    if (c >= 'A' && c <= 'Z')
        return c + ('a' - 'A');
    return c;
}

static int ci_equal(const char *a, const char *b)
{
    if (!a || !b)
        return 0;
    while (*a && *b) {
        if (ascii_lower((unsigned char)*a) != ascii_lower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int ci_ends_with(const char *s, const char *suffix)
{
    size_t slen, tlen;

    if (!s || !suffix)
        return 0;
    slen = strlen(s);
    tlen = strlen(suffix);
    if (tlen > slen)
        return 0;
    return ci_equal(s + slen - tlen, suffix);
}

static int is_hex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

static int valid_id(const char *s)
{
    size_t i, n;

    if (!s)
        return 0;
    n = strlen(s);
    if (n < 1 || n > 31)
        return 0;
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-'))
            return 0;
    }
    return 1;
}

static void copy_trimmed(char *dst, size_t size, const char *src)
{
    size_t n;

    while (*src == ' ')
        src++;
    n = strlen(src);
    while (n > 0 && src[n - 1] == ' ')
        n--;
    if (n >= size)
        n = size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static int valid_title(const char *s)
{    size_t i, n;

    if (!s)
        return 0;
    while (*s == ' ')
        s++;
    n = strlen(s);
    while (n > 0 && s[n - 1] == ' ')
        n--;
    if (n < 1 || n > 47)
        return 0;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 32 || c > 126)
            return 0;
    }
    return 1;
}

static int parse_version(const char *v, unsigned *a, unsigned *b, unsigned *c)
{
    const char *p = v;
    unsigned parts[3];
    int i;

    if (!v)
        return 0;
    for (i = 0; i < 3; i++) {
        int digits = 0;
        unsigned value = 0;
        while (*p >= '0' && *p <= '9' && digits < 4) {
            value = value * 10u + (unsigned)(*p - '0');
            p++;
            digits++;
        }
        if (digits == 0 || digits > 4)
            return 0;
        if (i < 2) {
            if (*p != '.')
                return 0;
            p++;
        }
        parts[i] = value;
    }
    if (*p != '\0')
        return 0;
    *a = parts[0];
    *b = parts[1];
    *c = parts[2];
    return 1;
}

int catalog_version_valid(const char *v)
{
    unsigned a, b, c;
    return parse_version(v, &a, &b, &c);
}

int catalog_version_compare(const char *a, const char *b)
{
    unsigned a1, a2, a3, b1, b2, b3;

    if (!parse_version(a, &a1, &a2, &a3) ||
        !parse_version(b, &b1, &b2, &b3))
        return 2;
    if (a1 != b1)
        return a1 < b1 ? -1 : 1;
    if (a2 != b2)
        return a2 < b2 ? -1 : 1;
    if (a3 != b3)
        return a3 < b3 ? -1 : 1;
    return 0;
}

static int valid_size(const char *s, uint32_t *out)
{
    uint64_t v = 0;
    size_t i, n;

    if (!s)
        return 0;
    n = strlen(s);
    if (n < 1 || n > 10)
        return 0;
    for (i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9')
            return 0;
        v = v * 10u + (uint64_t)(s[i] - '0');
        if (v > CATALOG_MAX_FILE_BYTES)
            return 0;
    }
    if (v < 1)
        return 0;
    *out = (uint32_t)v;
    return 1;
}

static int valid_sha(const char *s, char out[65])
{
    size_t i;

    if (!s || strlen(s) != 64)
        return 0;
    for (i = 0; i < 64; i++) {
        if (!is_hex(s[i]))
            return 0;
        out[i] = (char)ascii_lower((unsigned char)s[i]);
    }
    out[64] = '\0';
    return 1;
}

static int reserved_dos_name(const char *s)
{
    static const char *names[] = {
        "CON", "PRN", "AUX", "NUL",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
    };
    size_t i;

    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (ci_equal(s, names[i]))
            return 1;
    }
    return 0;
}

static int valid_folder(const char *s)
{
    size_t i, n;

    if (!s)
        return 0;
    n = strlen(s);
    if (n < 1 || n > 31)
        return 0;
    if (strcmp(s, ".") == 0 || strcmp(s, "..") == 0)
        return 0;
    if (reserved_dos_name(s))
        return 0;
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return 0;
    }
    return 1;
}

static int valid_path(const char *s)
{
    size_t i, n;
    const char *seg;

    if (!s)
        return 0;
    n = strlen(s);
    if (n < 10 || n > 95 || s[0] == '/')
        return 0;
    if (!ci_ends_with(s, "/EBOOT.PBP"))
        return 0;

    seg = s;
    for (i = 0; i <= n; i++) {
        char c = i < n ? s[i] : '\0';
        if (c == '\0' || c == '/') {
            size_t len = (size_t)((s + i) - seg);
            if (len == 0)
                return 0;
            if (len == 1 && seg[0] == '.')
                return 0;
            if (len == 2 && seg[0] == '.' && seg[1] == '.')
                return 0;
            if (c == '\0')
                break;
            seg = s + i + 1;
        }
        if (c != '\0' && c != '/') {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
                return 0;
        }
    }
    return 1;
}

const char *catalog_result_name(CatalogResult result)
{
    switch (result) {
    case CATALOG_OK:            return "ok";
    case CATALOG_ERR_HEADER:    return "bad header";
    case CATALOG_ERR_ENTRY:     return "bad entry";
    case CATALOG_ERR_TOO_MANY:  return "too many entries";
    case CATALOG_ERR_TOO_LARGE: return "too large";
    case CATALOG_ERR_FIELD:     return "bad field";
    case CATALOG_ERR_VALUE:     return "bad value";
    default:                    return "?";
    }
}

static int field_index(const char *key, int len)
{
    static const char *keys[7] = {
        "id", "title", "version", "size", "sha256", "folder", "path"
    };
    int i;

    for (i = 0; i < 7; i++) {
        if ((int)strlen(keys[i]) == len && strncmp(keys[i], key, (size_t)len) == 0)
            return i;
    }
    return -1;
}

static CatalogResult finish_entry(CatalogEntry *e, int mask,
                                  Catalog *out, int *count)
{
    int i;

    if (mask != 0x7f)
        return CATALOG_ERR_FIELD;
    if (!valid_id(e->id) || !valid_title(e->title) ||
        !catalog_version_valid(e->version))
        return CATALOG_ERR_VALUE;

    /* Re-run the string validators for the stored values. */
    if (!valid_folder(e->folder) || !valid_path(e->path))
        return CATALOG_ERR_VALUE;

    for (i = 0; i < *count; i++) {
        if (ci_equal(out->entries[i].id, e->id) ||
            ci_equal(out->entries[i].folder, e->folder) ||
            ci_equal(out->entries[i].path, e->path))
            return CATALOG_ERR_FIELD;
    }
    if (*count >= CATALOG_MAX_ENTRIES)
        return CATALOG_ERR_TOO_MANY;
    out->entries[*count] = *e;
    (*count)++;
    return CATALOG_OK;
}

CatalogResult catalog_parse(const char *text, int len, Catalog *out)
{
    CatalogEntry cur;
    int mask = 0;
    int count = 0;
    int pos = 0;
    int header_seen = 0;

    if (!out)
        return CATALOG_ERR_ENTRY;
    memset(out, 0, sizeof(*out));
    if (!text || len <= 0)
        return CATALOG_ERR_HEADER;
    if (len > CATALOG_MAX_BYTES)
        return CATALOG_ERR_TOO_LARGE;

    memset(&cur, 0, sizeof(cur));

    while (pos < len) {
        int start = pos;
        int end;
        int line_len;
        char line[CATALOG_LINE_MAX + 1];

        while (pos < len && text[pos] != '\n')
            pos++;
        end = pos;
        if (pos < len)
            pos++;

        line_len = end - start;
        if (line_len > 0 && text[end - 1] == '\r')
            line_len--;
        if (line_len > CATALOG_LINE_MAX)
            return CATALOG_ERR_FIELD;
        memcpy(line, text + start, (size_t)line_len);
        line[line_len] = '\0';

        if (line_len == 0 || line[0] == '#')
            continue;

        if (!header_seen) {
            if (strcmp(line, "PSPWIFI-CATALOG 1") != 0)
                return CATALOG_ERR_HEADER;
            header_seen = 1;
            continue;
        }

        if (strcmp(line, "--") == 0) {
            CatalogResult r = finish_entry(&cur, mask, out, &count);
            if (r != CATALOG_OK)
                return r;
            memset(&cur, 0, sizeof(cur));
            mask = 0;
            continue;
        }

        {
            char *eq = strchr(line, '=');
            int key_len;
            int idx;
            char *value;

            if (!eq)
                return CATALOG_ERR_FIELD;
            key_len = (int)(eq - line);
            idx = field_index(line, key_len);
            if (idx < 0)
                return CATALOG_ERR_FIELD;
            if (mask & (1 << idx))
                return CATALOG_ERR_FIELD;
            value = eq + 1;

            switch (idx) {
            case 0:
                if (strlen(value) > 31 || !valid_id(value))
                    return CATALOG_ERR_VALUE;
                snprintf(cur.id, sizeof(cur.id), "%s", value);
                break;
            case 1: {
                const char *t = value;
                size_t n;
                while (*t == ' ')
                    t++;
                n = strlen(t);
                while (n > 0 && t[n - 1] == ' ')
                    n--;
                if (n < 1 || n > 47)
                    return CATALOG_ERR_VALUE;
                copy_trimmed(cur.title, sizeof(cur.title), value);
                break;
            }
            case 2:
                if (strlen(value) > 15 || !catalog_version_valid(value))
                    return CATALOG_ERR_VALUE;
                snprintf(cur.version, sizeof(cur.version), "%s", value);
                break;
            case 3:
                if (!valid_size(value, &cur.size))
                    return CATALOG_ERR_VALUE;
                break;
            case 4:
                if (!valid_sha(value, cur.sha256))
                    return CATALOG_ERR_VALUE;
                break;
            case 5:
                if (strlen(value) > 31 || !valid_folder(value))
                    return CATALOG_ERR_VALUE;
                snprintf(cur.folder, sizeof(cur.folder), "%s", value);
                break;
            case 6:
                if (strlen(value) > 95 || !valid_path(value))
                    return CATALOG_ERR_VALUE;
                snprintf(cur.path, sizeof(cur.path), "%s", value);
                break;
            default:
                return CATALOG_ERR_FIELD;
            }
            mask |= 1 << idx;
        }
    }

    if (!header_seen)
        return CATALOG_ERR_HEADER;
    if (mask != 0) {
        CatalogResult r = finish_entry(&cur, mask, out, &count);
        if (r != CATALOG_OK)
            return r;
    }
    out->count = count;
    return CATALOG_OK;
}

int catalog_url_parse(const char *url, CatalogUrl *out)
{
    const char *last;
    int base_len;

    if (!out || !url)
        return -1;
    memset(out, 0, sizeof(*out));
    if (http_url_split(url, out->host, sizeof(out->host), &out->port,
                       out->path, sizeof(out->path)) != 0)
        return -1;

    last = strrchr(url, '/');
    if (!last)
        return -1;
    base_len = (int)(last - url) + 1;
    if (base_len <= 0 || base_len >= (int)sizeof(out->base))
        return -1;
    memcpy(out->base, url, (size_t)base_len);
    out->base[base_len] = '\0';
    return 0;
}

int catalog_url_join(const CatalogUrl *base, const char *rel,
                     char *out, int size)
{
    int n;

    if (!base || !rel || !out || size <= 0)
        return -1;
    if (strstr(rel, "://") || rel[0] == '/' || strchr(rel, '\\') ||
        strchr(rel, '?') || strchr(rel, '#'))
        return -1;
    if (!valid_path(rel))
        return -1;
    n = snprintf(out, (size_t)size, "%s%s", base->base, rel);
    if (n < 0 || n >= size || n > 191)
        return -1;
    return 0;
}

int catalog_config_parse(const char *text, int len, char *url_out, int size)
{
    int pos = 0;
    int have_url = 0;

    if (!url_out || size <= 0)
        return -1;
    url_out[0] = '\0';
    if (!text || len < 0)
        return -1;
    if (len > 2048)
        return -1;

    while (pos < len) {
        int start = pos;
        int end;
        int line_len;
        char line[CATALOG_LINE_MAX + 1];
        char *eq;
        char *value;

        while (pos < len && text[pos] != '\n')
            pos++;
        end = pos;
        if (pos < len)
            pos++;
        line_len = end - start;
        if (line_len > 0 && text[end - 1] == '\r')
            line_len--;
        if (line_len > CATALOG_LINE_MAX)
            return -1;
        memcpy(line, text + start, (size_t)line_len);
        line[line_len] = '\0';

        if (line_len == 0 || line[0] == '#' || line[0] == ';')
            continue;
        eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        value = eq + 1;
        if (strcmp(line, "url") != 0)
            continue;
        if (value[0] == '\0')
            continue;
        if (have_url)
            return -1;
        {
            CatalogUrl u;
            if (catalog_url_parse(value, &u) != 0)
                return -1;
        }
        if ((int)strlen(value) >= size)
            return -1;
        strcpy(url_out, value);
        have_url = 1;
    }
    return 0;
}
