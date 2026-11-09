/*
 * net_sock.c - PSP socket helpers.
 *
 * The firmware reads SceNetInetTimeval (8 bytes), not newlib's 16-byte
 * struct timeval. Every timeout here uses the firmware type.
 */
#include "net_sock.h"

#include <pspkernel.h>
#include <pspnet_inet.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>

#ifndef INADDR_NONE
#define INADDR_NONE 0xFFFFFFFFu
#endif

typedef char psp_sock_timeval_must_be_8[(sizeof(SceNetInetTimeval) == 8) ? 1 : -1];
typedef char psp_sock_timeval_sec_must_be_4[
    (sizeof(((SceNetInetTimeval *)0)->tv_sec) == 4) ? 1 : -1];

static int elapsed_since(uint32_t start)
{
    return (int)((uint32_t)sceKernelGetSystemTimeLow() - start) / 1000;
}

int psp_sock_set_nonblock(int fd, int on)
{
    return sceNetInetSetsockopt(fd, SOL_SOCKET, SO_NONBLOCK, &on, sizeof(on));
}

static int wait_writable(int fd, int timeout_ms)
{
    fd_set wfds;
    SceNetInetTimeval tv;
    int ret;

    if (timeout_ms < 0)
        timeout_ms = 0;

    FD_ZERO(&wfds);
    FD_SET(fd, &wfds);
    tv.tv_sec = (uint32_t)(timeout_ms / 1000);
    tv.tv_usec = (uint32_t)(timeout_ms % 1000) * 1000u;

    ret = sceNetInetSelect(fd + 1, NULL, &wfds, NULL, &tv);
    if (ret <= 0)
        return ret;
    return FD_ISSET(fd, &wfds) ? 1 : 0;
}

static PspSockResult classify_errno(int err)
{
    switch (err) {
    case ECONNREFUSED:
        return PSP_SOCK_OK_REFUSED;
    case ETIMEDOUT:
        return PSP_SOCK_ERR_TIMEOUT;
    case EHOSTUNREACH:
    case ENETUNREACH:
        return PSP_SOCK_ERR_UNREACHABLE;
    default:
        return PSP_SOCK_ERR_IO;
    }
}

int psp_sock_connect_ex(const char *ip, unsigned short port, int timeout_ms,
                        PspSockProbe *out)
{
    struct sockaddr_in sa;
    SceNetInetTimeval tv;
    fd_set wfds;
    uint32_t start;
    int fd;
    int ret;
    int so_error;
    socklen_t so_len;
    int err;

    if (out) {
        out->result = PSP_SOCK_ERR_SOCKET;
        out->elapsed_ms = 0;
        out->sys_errno = 0;
    }

    if (!ip || timeout_ms <= 0)
        return -1;

    start = (uint32_t)sceKernelGetSystemTimeLow();

    fd = sceNetInetSocket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        if (out) {
            out->result = PSP_SOCK_ERR_SOCKET;
            out->sys_errno = sceNetInetGetErrno();
            out->elapsed_ms = elapsed_since(start);
        }
        return -1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sin_len = (unsigned char)sizeof(sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = inet_addr(ip);

    if (sa.sin_addr.s_addr == INADDR_NONE) {
        sceNetInetClose(fd);
        if (out) {
            out->result = PSP_SOCK_ERR_BAD_ADDR;
            out->elapsed_ms = elapsed_since(start);
        }
        return -1;
    }

    if (psp_sock_set_nonblock(fd, 1) < 0) {
        if (out) {
            out->result = PSP_SOCK_ERR_SOCKET;
            out->sys_errno = sceNetInetGetErrno();
            out->elapsed_ms = elapsed_since(start);
        }
        sceNetInetClose(fd);
        return -1;
    }

    ret = sceNetInetConnect(fd, (struct sockaddr *)&sa, sizeof(sa));
    if (ret < 0) {
        err = sceNetInetGetErrno();
        if (err != EINPROGRESS && err != EALREADY &&
            err != EAGAIN && err != EWOULDBLOCK) {
            if (out) {
                out->result = classify_errno(err);
                out->sys_errno = err;
                out->elapsed_ms = elapsed_since(start);
            }
            sceNetInetClose(fd);
            return -1;
        }

        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        tv.tv_sec = (uint32_t)(timeout_ms / 1000);
        tv.tv_usec = (uint32_t)(timeout_ms % 1000) * 1000u;

        ret = sceNetInetSelect(fd + 1, NULL, &wfds, NULL, &tv);
        if (ret <= 0 || !FD_ISSET(fd, &wfds)) {
            if (out) {
                out->result = ret == 0 ? PSP_SOCK_ERR_TIMEOUT
                                       : PSP_SOCK_ERR_IO;
                out->sys_errno = sceNetInetGetErrno();
                out->elapsed_ms = elapsed_since(start);
            }
            sceNetInetClose(fd);
            return -1;
        }

        so_error = -1;
        so_len = sizeof(so_error);
        if (sceNetInetGetsockopt(fd, SOL_SOCKET, SO_ERROR,
                                 &so_error, &so_len) < 0) {
            if (out) {
                out->result = PSP_SOCK_ERR_IO;
                out->sys_errno = sceNetInetGetErrno();
                out->elapsed_ms = elapsed_since(start);
            }
            sceNetInetClose(fd);
            return -1;
        }
        if (so_error != 0) {
            if (out) {
                out->result = classify_errno(so_error);
                out->sys_errno = so_error;
                out->elapsed_ms = elapsed_since(start);
            }
            sceNetInetClose(fd);
            return -1;
        }
    }

    psp_sock_set_nonblock(fd, 0);
    {
        int one = 1;
        sceNetInetSetsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    }

    if (out) {
        out->result = PSP_SOCK_OK;
        out->elapsed_ms = elapsed_since(start);
        out->sys_errno = 0;
    }
    return fd;
}

int psp_sock_connect_timeout(const char *ip, unsigned short port,
                             int timeout_ms, int *elapsed_ms, int *sys_errno)
{
    PspSockProbe probe;
    int fd = psp_sock_connect_ex(ip, port, timeout_ms, &probe);

    if (elapsed_ms)
        *elapsed_ms = probe.elapsed_ms;
    if (sys_errno)
        *sys_errno = probe.sys_errno;

    if (fd >= 0)
        return fd;
    return -1;
}

int psp_sock_tcp_probe(const char *ip, unsigned short port, int timeout_ms,
                       PspSockProbe *out)
{
    PspSockProbe probe;
    int fd;

    if (!out)
        return PSP_SOCK_ERR_SOCKET;

    fd = psp_sock_connect_ex(ip, port, timeout_ms, &probe);
    if (fd >= 0)
        psp_sock_close_abortive(fd);

    *out = probe;
    if (probe.result == PSP_SOCK_OK_REFUSED)
        return PSP_SOCK_OK_REFUSED;
    if (probe.result == PSP_SOCK_OK)
        return PSP_SOCK_OK;
    return (int)probe.result;
}

int psp_sock_send_all(int fd, const void *buf, int len, int timeout_ms)
{
    const unsigned char *p = (const unsigned char *)buf;
    uint32_t start;
    int sent = 0;

    if (fd < 0 || (!buf && len > 0) || len < 0)
        return PSP_SOCK_ERR_IO;

    start = (uint32_t)sceKernelGetSystemTimeLow();

    while (sent < len) {
        int n = (int)sceNetInetSend(fd, p + sent, (size_t)(len - sent),
                                    MSG_DONTWAIT);
        if (n > 0) {
            sent += n;
            continue;
        }
        if (n == 0)
            return PSP_SOCK_ERR_IO;

        if (sceNetInetGetErrno() != EAGAIN &&
            sceNetInetGetErrno() != EWOULDBLOCK)
            return PSP_SOCK_ERR_IO;

        if (elapsed_since(start) >= timeout_ms)
            return PSP_SOCK_ERR_TIMEOUT;

        if (wait_writable(fd, 20) < 0)
            return PSP_SOCK_ERR_IO;
    }

    return sent;
}

int psp_sock_recv_nb(int fd, void *buf, int len)
{
    int n;

    if (fd < 0 || !buf || len <= 0)
        return -1;

    n = (int)sceNetInetRecv(fd, buf, (size_t)len, MSG_DONTWAIT);
    if (n >= 0)
        return n;

    if (sceNetInetGetErrno() == EAGAIN ||
        sceNetInetGetErrno() == EWOULDBLOCK)
        return -2;
    return -1;
}

int psp_sock_wait_readable(int fd, int timeout_ms)
{
    fd_set rfds;
    SceNetInetTimeval tv;
    int ret;

    if (fd < 0)
        return -1;
    if (timeout_ms < 0)
        timeout_ms = 0;

    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    tv.tv_sec = (uint32_t)(timeout_ms / 1000);
    tv.tv_usec = (uint32_t)(timeout_ms % 1000) * 1000u;

    ret = sceNetInetSelect(fd + 1, &rfds, NULL, NULL, &tv);
    if (ret <= 0)
        return ret;
    return FD_ISSET(fd, &rfds) ? 1 : 0;
}

void psp_sock_close_abortive(int fd)
{
    struct linger lg;

    if (fd < 0)
        return;

    lg.l_onoff = 1;
    lg.l_linger = 0;
    sceNetInetSetsockopt(fd, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
    psp_sock_set_nonblock(fd, 1);
    sceNetInetClose(fd);
}
