/*
 * http_client.c - PSP HTTP GET over the existing nonblocking socket helpers.
 */
#include "http_client.h"

#include <pspnet_inet.h>
#include <pspkernel.h>

#include <stdlib.h>
#include <string.h>

#include "net_probe.h"
#include "net_sock.h"

typedef struct {
    int fd;
    unsigned char tail[4096];
    int tail_len;
    int tail_pos;
    uint32_t body_left;
    int has_body_left;
} HttpClient;

static HttpClientStopFn s_stop_fn;
static void *s_stop_ctx;
static int s_stop_result;

static int read_head(int fd, unsigned char *buf, int size,
                     HttpResponseHead *out)
{
    int total = 0;
    uint32_t start_ms = (uint32_t)(sceKernelGetSystemTimeLow() / 1000u);

    while (total < size) {
        int n;
        HttpParseResult pr;
        uint32_t now_ms;

        if (s_stop_fn) {
            now_ms = (uint32_t)(sceKernelGetSystemTimeLow() / 1000u);
            if (now_ms - start_ms >= 500u) {
                int stop = s_stop_fn(s_stop_ctx);
                if (stop) {
                    s_stop_result = stop;
                    return HTTP_CLIENT_ERR_CANCELLED;
                }
            }
        }
        n = psp_sock_recv_nb(fd, buf + total, size - total);
        if (n == -2) {
            if ((uint32_t)(sceKernelGetSystemTimeLow() / 1000u) - start_ms >=
                3000u)
                return HTTP_CLIENT_ERR_TIMEOUT;
            sceKernelDelayThread(10 * 1000);
            continue;
        }
        if (n <= 0)
            return total == 0 ? HTTP_CLIENT_ERR_TIMEOUT
                              : HTTP_CLIENT_ERR_BAD_RESPONSE;
        total += n;
        pr = http_parse_head((const char *)buf, total, out);
        if (pr == HTTP_PARSE_OK)
            return total;
        if (pr == HTTP_PARSE_MALFORMED || pr == HTTP_PARSE_TOO_LARGE)
            return HTTP_CLIENT_ERR_BAD_RESPONSE;
    }
    return HTTP_CLIENT_ERR_BAD_RESPONSE;
}

int http_client_open_ex(const char *url, int has_range, uint32_t range_start,
                        const char *if_range, int connect_timeout_ms,
                        HttpResponseHead *out, int *net_err, void **handle)
{
    HttpClient *c;
    char host[64];
    char path[128];
    char ip[16];
    char request[512];
    unsigned char head[HTTP_HEAD_MAX];
    unsigned short port;
    PspSockProbe probe;
    int fd;
    int n;
    int head_len;

    if (net_err)
        *net_err = 0;
    s_stop_result = 0;
    if (!url || !out || !handle)
        return -1;
    if (http_url_split(url, host, sizeof(host), &port, path, sizeof(path)) != 0) {
        if (net_err)
            *net_err = HTTP_CLIENT_ERR_BAD_RESPONSE;
        return -1;
    }
    if (s_stop_fn) {
        int stop = s_stop_fn(s_stop_ctx);
        if (stop) {
            if (net_err)
                *net_err = stop;
            return -1;
        }
    }
    if (net_probe_resolve(host, ip, sizeof(ip)) != 0) {
        if (net_err)
            *net_err = HTTP_CLIENT_ERR_NET;
        return -1;
    }
    fd = psp_sock_connect_ex(ip, port, connect_timeout_ms, &probe);
    if (fd < 0) {
        if (net_err)
            *net_err = probe.result == PSP_SOCK_ERR_TIMEOUT
                           ? HTTP_CLIENT_ERR_TIMEOUT
                           : HTTP_CLIENT_ERR_NET;
        return -1;
    }
    if (psp_sock_set_nonblock(fd, 1) < 0) {
        psp_sock_close_abortive(fd);
        if (net_err)
            *net_err = HTTP_CLIENT_ERR_NET;
        return -1;
    }
    if (s_stop_fn) {
        int stop = s_stop_fn(s_stop_ctx);
        if (stop) {
            psp_sock_close_abortive(fd);
            if (net_err)
                *net_err = stop;
            return -1;
        }
    }
    n = http_build_get(request, sizeof(request), host, port, path,
                       has_range, range_start, if_range);
    if (n <= 0 ||
        psp_sock_send_all(fd, request, n, 5000) != n) {
        psp_sock_close_abortive(fd);
        if (net_err)
            *net_err = HTTP_CLIENT_ERR_NET;
        return -1;
    }

    head_len = read_head(fd, head, sizeof(head), out);
    if (head_len < 0) {
        psp_sock_close_abortive(fd);
        if (net_err)
            *net_err = head_len == HTTP_CLIENT_ERR_CANCELLED
                           ? s_stop_result : head_len;
        return -1;
    }

    c = calloc(1, sizeof(*c));
    if (!c) {
        psp_sock_close_abortive(fd);
        if (net_err)
            *net_err = HTTP_CLIENT_ERR_NET;
        return -1;
    }
    c->fd = fd;
    if (head_len > out->header_end) {
        c->tail_len = head_len - out->header_end;
        if (c->tail_len > (int)sizeof(c->tail))
            c->tail_len = (int)sizeof(c->tail);
        memcpy(c->tail, head + out->header_end, (size_t)c->tail_len);
    }
    if (out->has_content_length) {
        c->body_left = out->content_length;
        c->has_body_left = 1;
    } else if (out->status == 206 && out->has_content_range) {
        c->body_left = out->range_end - out->range_start + 1;
        c->has_body_left = 1;
    }
    *handle = c;
    return 0;
}

int http_client_read(void *handle, void *buf, int max, int *net_err)
{
    HttpClient *c = handle;
    int n;
    uint32_t idle;
    uint32_t last_check;

    if (net_err)
        *net_err = 0;
    if (!c || !buf || max <= 0)
        return -1;
    if (c->tail_pos < c->tail_len) {
        n = c->tail_len - c->tail_pos;
        if (n > max)
            n = max;
        memcpy(buf, c->tail + c->tail_pos, (size_t)n);
        c->tail_pos += n;
        if (c->has_body_left)
            c->body_left -= (uint32_t)n;
        return n;
    }
    if (c->has_body_left && c->body_left == 0)
        return 0;

    idle = (uint32_t)(sceKernelGetSystemTimeLow() / 1000u);
    last_check = idle;
    for (;;) {
        if (s_stop_fn &&
            (uint32_t)(sceKernelGetSystemTimeLow() / 1000u) - last_check >=
                1000u) {
            int stop;
            last_check = (uint32_t)(sceKernelGetSystemTimeLow() / 1000u);
            stop = s_stop_fn(s_stop_ctx);
            if (stop) {
                if (net_err)
                    *net_err = stop;
                return -1;
            }
        }
        n = psp_sock_recv_nb(c->fd, buf, max);
        if (n == -2) {
            if ((uint32_t)(sceKernelGetSystemTimeLow() / 1000u) - idle >=
                30000u) {
                if (net_err)
                    *net_err = HTTP_CLIENT_ERR_TIMEOUT;
                return -1;
            }
            sceKernelDelayThread(10 * 1000);
            continue;
        }
        if (n < 0) {
            if (net_err)
                *net_err = HTTP_CLIENT_ERR_NET;
            return -1;
        }
        if (n == 0)
            return 0;
        if (c->has_body_left)
            c->body_left -= (uint32_t)n;
        return n;
    }
}

int http_client_open(const char *url, int has_range, uint32_t range_start,
                     const char *if_range, HttpResponseHead *out,
                     int *net_err, void **handle)
{
    return http_client_open_ex(url, has_range, range_start, if_range, 10000,
                               out, net_err, handle);
}

void http_client_set_stop_check(HttpClientStopFn fn, void *ctx)
{
    s_stop_fn = fn;
    s_stop_ctx = ctx;
}

void http_client_close(void *handle)
{
    HttpClient *c = handle;

    if (!c)
        return;
    psp_sock_close_abortive(c->fd);
    free(c);
}
