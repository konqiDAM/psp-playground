/*
 * host_download_io.c - host-only POSIX storage and HTTP transport.
 *
 * Not part of the PSP build; used by test-download-http.sh only.
 */
#define _POSIX_C_SOURCE 200809L

#include "host_download_io.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct {
    FILE *fp;
} HostFile;

static void host_map(HostStorage *s, const char *path, char *out, size_t size)
{
    const char *p = path;
    if (strncmp(p, "ms0:", 4) == 0)
        p += 4;
    snprintf(out, size, "%s%s", s->root, p);
}

static int host_open(void *ctx, const char *path, int mode, void **file)
{
    HostStorage *s = ctx;
    char real[512];
    HostFile *f;
    const char *m;

    host_map(s, path, real, sizeof(real));
    f = calloc(1, sizeof(*f));
    if (!f)
        return -1;
    if (mode == DL_OPEN_READ)
        m = "rb";
    else if (mode == DL_OPEN_WRITE)
        m = "wb";
    else
        m = "r+b";
    f->fp = fopen(real, m);
    if (!f->fp && mode == DL_OPEN_APPEND)
        f->fp = fopen(real, "w+b");
    if (!f->fp) {
        free(f);
        return -1;
    }
    *file = f;
    return 0;
}

static int host_read(void *file, void *buf, int len)
{
    HostFile *f = file;
    size_t n = fread(buf, 1, (size_t)len, f->fp);
    if (n == 0 && ferror(f->fp))
        return -1;
    return (int)n;
}

static int host_write(void *file, const void *buf, int len)
{
    HostFile *f = file;
    if (fwrite(buf, 1, (size_t)len, f->fp) != (size_t)len)
        return -1;
    return len;
}

static int host_seek(void *file, uint32_t offset)
{
    HostFile *f = file;
    return fseek(f->fp, (long)offset, SEEK_SET) == 0 ? 0 : -1;
}

static int host_sync(void *file)
{
    HostFile *f = file;
    if (fflush(f->fp) != 0)
        return -1;
    return fsync(fileno(f->fp)) == 0 ? 0 : -1;
}

static int host_close(void *file)
{
    HostFile *f = file;
    int r = fclose(f->fp);
    free(f);
    return r == 0 ? 0 : -1;
}

static int host_size(void *ctx, const char *path, uint32_t *out)
{
    HostStorage *s = ctx;
    char real[512];
    struct stat st;

    host_map(s, path, real, sizeof(real));
    if (stat(real, &st) != 0)
        return errno == ENOENT ? 1 : -1;
    *out = (uint32_t)st.st_size;
    return 0;
}

static int host_remove(void *ctx, const char *path)
{
    HostStorage *s = ctx;
    char real[512];

    host_map(s, path, real, sizeof(real));
    if (remove(real) != 0)
        return errno == ENOENT ? 1 : -1;
    return 0;
}

static int host_rename(void *ctx, const char *from, const char *to)
{
    HostStorage *s = ctx;
    char a[512], b[512];
    struct stat st;

    host_map(s, from, a, sizeof(a));
    host_map(s, to, b, sizeof(b));
    if (stat(b, &st) == 0)
        return -1; /* FAT: no replace */
    return rename(a, b) == 0 ? 0 : -1;
}

static int host_mkdir(void *ctx, const char *path)
{
    HostStorage *s = ctx;
    char real[512];

    host_map(s, path, real, sizeof(real));
    if (mkdir(real, 0777) == 0)
        return 0;
    return errno == EEXIST ? 1 : -1;
}

static int host_free(void *ctx, uint32_t *out)
{
    (void)ctx;
    *out = 100u * 1024u * 1024u;
    return 0;
}

void host_storage_init(HostStorage *s, const char *root)
{
    memset(s, 0, sizeof(*s));
    snprintf(s->root, sizeof(s->root), "%s", root);
}

void host_storage_env(DlStorage *st, HostStorage *s)
{
    memset(st, 0, sizeof(*st));
    st->ctx = s;
    st->open = host_open;
    st->read = host_read;
    st->write = host_write;
    st->seek = host_seek;
    st->sync = host_sync;
    st->close = host_close;
    st->size = host_size;
    st->remove = host_remove;
    st->rename = host_rename;
    st->mkdir = host_mkdir;
    st->free_bytes = host_free;
}

static int send_all(int fd, const char *buf, int len)
{
    int sent = 0;
    while (sent < len) {
        int n = (int)send(fd, buf + sent, (size_t)(len - sent), 0);
        if (n <= 0)
            return -1;
        sent += n;
    }
    return 0;
}

static int host_transport_open(void *ctx, const char *url, int has_range,
                               uint32_t range_start, const char *if_range,
                               DlResponse *out, int *net_err)
{
    HostTransport *t = ctx;
    struct sockaddr_in sa;
    char host[64], path[128], request[512];
    unsigned short port;
    unsigned char head[4096];
    int total = 0;
    int fd;
    int n;

    t->opened++;
    if (http_url_split(url, host, sizeof(host), &port, path, sizeof(path)) != 0) {
        if (net_err)
            *net_err = DL_ERR_BAD_RESPONSE;
        return -1;
    }
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        if (net_err)
            *net_err = DL_ERR_NET;
        return -1;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = inet_addr(host);
    if (sa.sin_addr.s_addr == INADDR_NONE ||
        connect(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
        close(fd);
        if (net_err)
            *net_err = DL_ERR_NET;
        return -1;
    }
    n = http_build_get(request, sizeof(request), host, port, path,
                       has_range, range_start, if_range);
    if (n <= 0 || send_all(fd, request, n) != 0) {
        close(fd);
        if (net_err)
            *net_err = DL_ERR_NET;
        return -1;
    }

    for (;;) {
        if (total >= (int)sizeof(head)) {
            close(fd);
            if (net_err)
                *net_err = DL_ERR_BAD_RESPONSE;
            return -1;
        }
        {
            fd_set rfds;
            struct timeval tv;
            int r;
            FD_ZERO(&rfds);
            FD_SET(fd, &rfds);
            tv.tv_sec = 5;
            tv.tv_usec = 0;
            r = select(fd + 1, &rfds, NULL, NULL, &tv);
            if (r <= 0) {
                close(fd);
                if (net_err)
                    *net_err = (r == 0) ? DL_ERR_TIMEOUT : DL_ERR_NET;
                return -1;
            }
        }
        n = (int)recv(fd, head + total, sizeof(head) - (size_t)total, 0);
        if (n <= 0) {
            close(fd);
            if (net_err)
                *net_err = DL_ERR_NET;
            return -1;
        }
        total += n;
        {
            HttpParseResult pr = http_parse_head((const char *)head, total, out);
            if (pr == HTTP_PARSE_MALFORMED || pr == HTTP_PARSE_TOO_LARGE) {
                close(fd);
                if (net_err)
                    *net_err = DL_ERR_BAD_RESPONSE;
                return -1;
            }
            if (pr == HTTP_PARSE_OK)
                break;
        }
    }

    t->fd = fd;
    t->last_status = out->status;
    t->tail_len = 0;
    t->tail_pos = 0;
    if (out->header_end < total) {
        t->tail_len = total - out->header_end;
        if (t->tail_len > (int)sizeof(t->tail))
            t->tail_len = (int)sizeof(t->tail);
        memcpy(t->tail, head + out->header_end, (size_t)t->tail_len);
    }
    if (out->has_content_length) {
        t->body_left = out->content_length;
        t->has_body_left = 1;
    } else if (out->status == 206 && out->has_content_range) {
        t->body_left = out->range_end - out->range_start + 1;
        t->has_body_left = 1;
    } else {
        t->has_body_left = 0;
    }
    return 0;
}

static int host_transport_read(void *ctx, void *buf, int max, int *net_err)
{
    HostTransport *t = ctx;
    int n;

    if (t->tail_pos < t->tail_len) {
        n = t->tail_len - t->tail_pos;
        if (n > max)
            n = max;
        memcpy(buf, t->tail + t->tail_pos, (size_t)n);
        t->tail_pos += n;
        if (t->has_body_left)
            t->body_left -= (uint32_t)n;
        return n;
    }
    if (t->has_body_left && t->body_left == 0)
        return 0;
    for (;;) {
        fd_set rfds;
        struct timeval tv;
        int r;
        FD_ZERO(&rfds);
        FD_SET(t->fd, &rfds);
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        r = select(t->fd + 1, &rfds, NULL, NULL, &tv);
        if (r < 0) {
            if (net_err)
                *net_err = DL_ERR_NET;
            return -1;
        }
        if (r == 0) {
            if (net_err)
                *net_err = DL_ERR_STALL;
            return -1;
        }
        n = (int)recv(t->fd, buf, (size_t)max, 0);
        if (n < 0) {
            if (net_err)
                *net_err = DL_ERR_NET;
            return -1;
        }
        if (n == 0)
            return 0;
        if (t->has_body_left)
            t->body_left -= (uint32_t)n;
        return n;
    }
}

static void host_transport_close(void *ctx)
{
    HostTransport *t = ctx;
    if (t->fd >= 0) {
        close(t->fd);
        t->fd = -1;
    }
}

void host_transport_init(HostTransport *t)
{
    memset(t, 0, sizeof(*t));
    t->fd = -1;
}

void host_transport_env(DlTransport *tr, HostTransport *t)
{
    memset(tr, 0, sizeof(*tr));
    tr->ctx = t;
    tr->open = host_transport_open;
    tr->read = host_transport_read;
    tr->close = host_transport_close;
}
