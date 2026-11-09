/*
 * net_probe_logic.c - portable HTTP probe parsing and allowlist.
 */
#include "net_probe_logic.h"

#include <stdio.h>
#include <string.h>

static int ascii_lower(int c)
{
    if (c >= 'A' && c <= 'Z')
        return c + ('a' - 'A');
    return c;
}

static int host_equal(const char *a, const char *b)
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

const char *probe_http_class_name(ProbeHttpClass c)
{
    switch (c) {
    case PROBE_HTTP_NEED_MORE:  return "need more";
    case PROBE_HTTP_NO_CONTENT: return "clean 204";
    case PROBE_HTTP_PORTAL:     return "portal suspected";
    case PROBE_HTTP_ERROR:      return "http error";
    case PROBE_HTTP_MALFORMED:  return "malformed";
    default:                    return "?";
    }
}

ProbeHttpClass probe_http_classify(int status_code)
{
    if (status_code < 100 || status_code > 599)
        return PROBE_HTTP_MALFORMED;
    if (status_code == 204)
        return PROBE_HTTP_NO_CONTENT;
    if (status_code == 511)
        return PROBE_HTTP_PORTAL;
    if ((status_code >= 200 && status_code <= 299) ||
        (status_code >= 300 && status_code <= 399))
        return PROBE_HTTP_PORTAL;
    return PROBE_HTTP_ERROR;
}

ProbeHttpClass probe_http_parse(const char *data, int len, ProbeHttpParse *out)
{
    int line_end = -1;
    int header_end = -1;
    int i;
    int status = 0;
    int digits = 0;

    if (!out)
        return PROBE_HTTP_MALFORMED;

    out->status_code = 0;
    out->header_end = -1;
    out->class = PROBE_HTTP_NEED_MORE;

    if (!data || len <= 0)
        return PROBE_HTTP_NEED_MORE;

    for (i = 0; i + 1 < len; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n') {
            line_end = i;
            break;
        }
    }
    if (line_end < 0)
        return PROBE_HTTP_NEED_MORE;

    if (line_end < 12 || strncmp(data, "HTTP/", 5) != 0) {
        out->class = PROBE_HTTP_MALFORMED;
        return out->class;
    }

    i = 5;
    while (i < line_end && data[i] != ' ')
        i++;
    if (i >= line_end || data[i] != ' ' || i + 3 >= line_end) {
        out->class = PROBE_HTTP_MALFORMED;
        return out->class;
    }

    i++;
    while (i < line_end && data[i] >= '0' && data[i] <= '9' && digits < 3) {
        status = status * 10 + (data[i] - '0');
        digits++;
        i++;
    }
    if (digits != 3 || (i < line_end && data[i] != ' ' && data[i] != '\r')) {
        out->class = PROBE_HTTP_MALFORMED;
        return out->class;
    }
    if (status < 100 || status > 599) {
        out->class = PROBE_HTTP_MALFORMED;
        return out->class;
    }

    for (i = line_end; i + 3 < len; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n' &&
            data[i + 2] == '\r' && data[i + 3] == '\n') {
            header_end = i + 4;
            break;
        }
    }
    if (header_end < 0)
        return PROBE_HTTP_NEED_MORE;

    out->status_code = status;
    out->header_end = header_end;
    out->class = probe_http_classify(status);
    return out->class;
}

int probe_request_allowed(const char *host, const char *path)
{
    if (!host || !path)
        return 0;

    if (!host_equal(host, "connectivitycheck.gstatic.com"))
        return 0;

    return strcmp(path, "/generate_204") == 0;
}

int probe_build_http_get(char *buf, int size, const char *host, const char *path)
{
    int n;

    if (!buf || size <= 0) {
        if (buf && size > 0)
            buf[0] = '\0';
        return -1;
    }

    buf[0] = '\0';
    if (!host || !path)
        return -1;

    n = snprintf(buf, (size_t)size,
                 "GET %s HTTP/1.1\r\n"
                 "Host: %s\r\n"
                 "User-Agent: psp-wifi/0.1.0\r\n"
                 "Accept: */*\r\n"
                 "Connection: close\r\n"
                 "\r\n",
                 path, host);
    if (n < 0 || n >= size) {
        buf[0] = '\0';
        return -1;
    }
    return n;
}

int net_is_dotted_ipv4(const char *host)
{
    int part;
    int digits;
    int value;

    if (!host || !host[0])
        return 0;
    part = 0;
    digits = 0;
    value = 0;
    while (*host) {
        if (*host >= '0' && *host <= '9') {
            if (digits == 0 && *host == '0' && host[1] >= '0' && host[1] <= '9')
                return 0; /* leading zero */
            value = value * 10 + (*host - '0');
            if (value > 255)
                return 0;
            digits++;
        } else if (*host == '.') {
            if (digits == 0 || part >= 3)
                return 0;
            part++;
            digits = 0;
            value = 0;
        } else {
            return 0;
        }
        host++;
    }
    return part == 3 && digits > 0;
}
