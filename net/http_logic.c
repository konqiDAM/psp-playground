/*
 * http_logic.c - bounded HTTP response head parsing and request building.
 */
#include "http_logic.h"

#include <stdio.h>
#include <string.h>

#define HTTP_LINE_MAX 1024
#define HTTP_VALUE_MAX 255

static int ascii_lower(int c)
{
    if (c >= 'A' && c <= 'Z')
        return c + ('a' - 'A');
    return c;
}

static int ci_prefix(const char *s, const char *prefix)
{
    while (*prefix) {
        if (ascii_lower((unsigned char)*s) !=
            ascii_lower((unsigned char)*prefix))
            return 0;
        s++;
        prefix++;
    }
    return 1;
}

static int all_digits(const char *s, int len)
{
    int i;
    if (len <= 0)
        return 0;
    for (i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9')
            return 0;
    }
    return 1;
}

static int parse_u32(const char *s, int len, uint32_t *out)
{
    uint64_t v = 0;
    int i;

    if (!all_digits(s, len))
        return 0;
    for (i = 0; i < len; i++) {
        v = v * 10u + (uint64_t)(s[i] - '0');
        if (v > 0xFFFFFFFFu)
            return 0;
    }
    *out = (uint32_t)v;
    return 1;
}

static int parse_content_range(const char *v, HttpResponseHead *h)
{
    uint32_t start, end, total;
    const char *p = v;
    const char *dash;
    const char *slash;
    int n;

    if (!ci_prefix(p, "bytes "))
        return 0;
    p += 6;
    n = 0;
    while (p[n] >= '0' && p[n] <= '9')
        n++;
    if (n == 0 || p[n] != '-')
        return 0;
    if (!parse_u32(p, n, &start))
        return 0;
    dash = p + n;
    p = dash + 1;
    n = 0;
    while (p[n] >= '0' && p[n] <= '9')
        n++;
    if (n == 0 || p[n] != '/')
        return 0;
    if (!parse_u32(p, n, &end))
        return 0;
    slash = p + n;
    p = slash + 1;
    n = 0;
    while (p[n] >= '0' && p[n] <= '9')
        n++;
    if (n == 0 || p[n] != '\0')
        return 0;
    if (!parse_u32(p, n, &total))
        return 0;
    if (total == 0 || end < start || end >= total)
        return 0;

    h->has_content_range = 1;
    h->range_start = start;
    h->range_end = end;
    h->range_total = total;
    return 1;
}

const char *http_parse_result_name(HttpParseResult result)
{
    switch (result) {
    case HTTP_PARSE_NEED_MORE: return "need more";
    case HTTP_PARSE_OK:        return "ok";
    case HTTP_PARSE_MALFORMED: return "malformed";
    case HTTP_PARSE_TOO_LARGE: return "too large";
    default:                   return "?";
    }
}

HttpParseResult http_parse_head(const char *data, int len, HttpResponseHead *out)
{
    int line_end = -1;
    int i;
    int status = 0;
    int digits = 0;

    if (!out)
        return HTTP_PARSE_MALFORMED;

    memset(out, 0, sizeof(*out));
    out->header_end = -1;

    if (!data || len <= 0)
        return HTTP_PARSE_NEED_MORE;
    if (len > HTTP_HEAD_MAX)
        return HTTP_PARSE_TOO_LARGE;

    for (i = 0; i + 1 < len; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n') {
            line_end = i;
            break;
        }
    }
    if (line_end < 0)
        return len >= HTTP_HEAD_MAX ? HTTP_PARSE_TOO_LARGE
                                    : HTTP_PARSE_NEED_MORE;
    if (line_end < 12 || strncmp(data, "HTTP/", 5) != 0)
        return HTTP_PARSE_MALFORMED;

    i = 5;
    while (i < line_end && data[i] != ' ')
        i++;
    if (i >= line_end || data[i] != ' ')
        return HTTP_PARSE_MALFORMED;
    i++;
    while (i < line_end && data[i] >= '0' && data[i] <= '9' && digits < 3) {
        status = status * 10 + (data[i] - '0');
        digits++;
        i++;
    }
    if (digits != 3 || (i < line_end && data[i] != ' '))
        return HTTP_PARSE_MALFORMED;
    if (status < 100 || status > 599)
        return HTTP_PARSE_MALFORMED;

    out->status = status;

    i = line_end + 2;
    for (;;) {
        int end;
        int colon;
        int name_len;
        int value_start;
        int value_len;
        const char *line;

        if (i + 1 >= len)
            return len >= HTTP_HEAD_MAX ? HTTP_PARSE_TOO_LARGE
                                        : HTTP_PARSE_NEED_MORE;
        if (data[i] == '\r' && data[i + 1] == '\n') {
            out->header_end = i + 2;
            break;
        }
        if (data[i] == ' ' || data[i] == '\t')
            return HTTP_PARSE_MALFORMED;

        end = -1;
        for (colon = i; colon + 1 < len; colon++) {
            if (data[colon] == '\r' && data[colon + 1] == '\n') {
                end = colon;
                break;
            }
        }
        if (end < 0)
            return len >= HTTP_HEAD_MAX ? HTTP_PARSE_TOO_LARGE
                                        : HTTP_PARSE_NEED_MORE;
        if (end - i > HTTP_LINE_MAX)
            return HTTP_PARSE_MALFORMED;

        line = data + i;
        colon = -1;
        for (name_len = 0; name_len < end - i; name_len++) {
            if (line[name_len] == ':') {
                colon = name_len;
                break;
            }
        }
        if (colon <= 0)
            return HTTP_PARSE_MALFORMED;
        value_start = colon + 1;
        while (value_start < end - i &&
               (line[value_start] == ' ' || line[value_start] == '\t'))
            value_start++;
        value_len = end - i - value_start;
        while (value_len > 0 &&
               (line[value_start + value_len - 1] == ' ' ||
                line[value_start + value_len - 1] == '\t'))
            value_len--;
        if (value_len > HTTP_VALUE_MAX)
            return HTTP_PARSE_MALFORMED;
        for (int k = 0; k < value_len; k++) {
            unsigned char c = (unsigned char)line[value_start + k];
            if (c < 0x20 || c == 0x7f)
                return HTTP_PARSE_MALFORMED;
        }
        {
            char value[HTTP_VALUE_MAX + 1];
            memcpy(value, line + value_start, (size_t)value_len);
            value[value_len] = '\0';

            if (colon == 14 && ci_prefix(line, "Content-Length")) {
                uint32_t v;
                if (out->has_content_length)
                    return HTTP_PARSE_MALFORMED;
                if (!parse_u32(value, value_len, &v))
                    return HTTP_PARSE_MALFORMED;
                out->has_content_length = 1;
                out->content_length = v;
            } else if (colon == 13 && ci_prefix(line, "Content-Range")) {
                if (out->has_content_range)
                    return HTTP_PARSE_MALFORMED;
                if (!parse_content_range(value, out))
                    return HTTP_PARSE_MALFORMED;
            } else if (colon == 4 && ci_prefix(line, "ETag")) {
                if (out->has_etag)
                    return HTTP_PARSE_MALFORMED;
                out->has_etag = 1;
                out->etag_strong = !(value_len >= 2 && value[0] == 'W' &&
                                     value[1] == '/');
                if (value_len >= (int)sizeof(out->etag))
                    return HTTP_PARSE_MALFORMED;
                memcpy(out->etag, value, (size_t)value_len);
                out->etag[value_len] = '\0';
            } else if (colon == 13 && ci_prefix(line, "Last-Modified")) {
                if (out->has_last_modified)
                    return HTTP_PARSE_MALFORMED;
                out->has_last_modified = 1;
                if (value_len >= (int)sizeof(out->last_modified))
                    return HTTP_PARSE_MALFORMED;
                memcpy(out->last_modified, value, (size_t)value_len);
                out->last_modified[value_len] = '\0';
            } else if (colon == 17 && ci_prefix(line, "Transfer-Encoding")) {
                if (!(value_len == 8 && ci_prefix(value, "identity")))
                    out->chunked = 1;
            } else if (colon == 16 && ci_prefix(line, "Content-Encoding")) {
                if (!(value_len == 8 && ci_prefix(value, "identity")))
                    out->encoded = 1;
            }
        }

        i = end + 2;
    }

    return HTTP_PARSE_OK;
}

static int path_has_dotdot(const char *path, int len)
{
    int i;
    for (i = 0; i + 2 < len; i++) {
        if (path[i] == '/' && path[i + 1] == '.' && path[i + 2] == '.' &&
            (i + 3 == len || path[i + 3] == '/'))
            return 1;
    }
    return 0;
}

static int path_ok(const char *path)
{
    int len;
    int i;

    if (!path)
        return 0;
    len = (int)strlen(path);
    if (len <= 0 || len > 127 || path[0] != '/')
        return 0;
    if (path_has_dotdot(path, len))
        return 0;
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)path[i];
        if (c < 0x20 || c == 0x7f || c == ' ' || c == '\\' ||
            c == '?' || c == '#')
            return 0;
    }
    return 1;
}

int http_build_get(char *buf, int size, const char *host, unsigned short port,
                   const char *path, int has_range, uint32_t range_start,
                   const char *if_range)
{
    int n;
    int off;
    int i;

    if (!buf || size <= 0)
        return -1;
    buf[0] = '\0';
    if (!host || !host[0] || strlen(host) > 63 || !path_ok(path))
        return -1;
    if (if_range) {
        if (strlen(if_range) > 79)
            return -1;
        for (i = 0; if_range[i]; i++) {
            if (if_range[i] == '\r' || if_range[i] == '\n')
                return -1;
        }
    }

    if (port == 80) {
        n = snprintf(buf, (size_t)size,
                     "GET %s HTTP/1.1\r\n"
                     "Host: %s\r\n"
                     "User-Agent: psp-wifi/0.2.0\r\n"
                     "Accept: */*\r\n"
                     "Connection: close\r\n",
                     path, host);
    } else {
        n = snprintf(buf, (size_t)size,
                     "GET %s HTTP/1.1\r\n"
                     "Host: %s:%u\r\n"
                     "User-Agent: psp-wifi/0.2.0\r\n"
                     "Accept: */*\r\n"
                     "Connection: close\r\n",
                     path, host, (unsigned)port);
    }
    if (n < 0 || n >= size)
        return -1;
    off = n;

    if (has_range) {
        n = snprintf(buf + off, (size_t)(size - off),
                     "Range: bytes=%u-\r\n", (unsigned)range_start);
        if (n < 0 || n >= size - off)
            return -1;
        off += n;
        if (if_range && if_range[0]) {
            n = snprintf(buf + off, (size_t)(size - off),
                         "If-Range: %s\r\n", if_range);
            if (n < 0 || n >= size - off)
                return -1;
            off += n;
        }
    }

    if (off + 2 >= size)
        return -1;
    buf[off++] = '\r';
    buf[off++] = '\n';
    buf[off] = '\0';
    return off;
}

int http_url_split(const char *url, char *host, int host_size,
                   unsigned short *port, char *path, int path_size)
{
    const char *p;
    const char *auth_end;
    const char *colon;
    int host_len;
    int path_len;
    unsigned long port_value = 80;
    int i;

    if (!url || !host || host_size <= 0 || !port || !path || path_size <= 0)
        return -1;
    if (!ci_prefix(url, "http://"))
        return -1;
    p = url + 7;

    auth_end = p;
    while (*auth_end && *auth_end != '/')
        auth_end++;
    if (*auth_end != '/' || auth_end == p)
        return -1;
    for (i = 0; p + i < auth_end; i++) {
        unsigned char c = (unsigned char)p[i];
        if (c == '@' || c == '?' || c == '#' || c == ' ' || c < 0x20 ||
            c == 0x7f || c == '\\')
            return -1;
    }

    colon = NULL;
    for (i = (int)(auth_end - p) - 1; i >= 0; i--) {
        if (p[i] == ':') {
            colon = p + i;
            break;
        }
    }
    host_len = colon ? (int)(colon - p) : (int)(auth_end - p);
    if (host_len <= 0 || host_len > 63)
        return -1;
    if (p[0] == '.' || p[host_len - 1] == '.')
        return -1;
    for (i = 0; i < host_len; i++) {
        unsigned char c = (unsigned char)p[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '.'))
            return -1;
        if (c == '.' && i + 1 < host_len && p[i + 1] == '.')
            return -1;
    }

    if (colon) {
        const char *q = colon + 1;
        int digits = (int)(auth_end - q);
        if (digits <= 0 || digits > 5)
            return -1;
        if (!all_digits(q, digits))
            return -1;
        port_value = 0;
        for (i = 0; i < digits; i++)
            port_value = port_value * 10u + (unsigned long)(q[i] - '0');
        if (port_value == 0 || port_value > 65535)
            return -1;
    }

    path_len = (int)strlen(auth_end);
    if (path_len <= 0 || path_len > 127)
        return -1;
    if (!path_ok(auth_end))
        return -1;
    if (strlen(url) > 191)
        return -1;

    if (host_len + 1 > host_size || path_len + 1 > path_size)
        return -1;
    memcpy(host, p, (size_t)host_len);
    host[host_len] = '\0';
    memcpy(path, auth_end, (size_t)path_len + 1u);
    *port = (unsigned short)port_value;
    return 0;
}
