/*
 * net_probe.c - DNS / TCP / gateway / HTTP-204 probes.
 *
 * All blocking calls are bounded and the caller can stop between stages.
 * The resolver is owned here and must be terminated before WiFi shutdown.
 */
#include "net_probe.h"
#include "net_sock.h"

#include <pspkernel.h>
#include <pspnet_resolver.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>

#define WIFI_RESOLVER_ALREADY ((int)0x80411101)

static int s_resolver_ready;
static int s_resolver_owned;
static int s_rid = -1;
static unsigned char s_resolver_buf[1024] __attribute__((aligned(16)));

typedef struct {
    const char *ip;
    unsigned short port;
    const char *label;
} TcpTarget;

static const TcpTarget s_tcp_targets[NET_PROBE_TCP_COUNT] = {
    { "1.1.1.1", 443, "1.1.1.1:443" },
    { "8.8.8.8", 53,  "8.8.8.8:53"  },
    { "9.9.9.9", 53,  "9.9.9.9:53"  }
};

static int stopped(const volatile int *stop)
{
    return stop && *stop;
}

static uint32_t now_ms(void)
{
    return (uint32_t)sceKernelGetSystemTimeLow() / 1000u;
}

int net_probe_init(void)
{
    int ret;

    if (s_resolver_ready)
        return 0;

    ret = sceNetResolverInit();
    if (ret >= 0) {
        s_resolver_owned = 1;
    } else if (ret == WIFI_RESOLVER_ALREADY) {
        s_resolver_owned = 0;
    } else {
        return ret;
    }

    s_resolver_ready = 1;
    ret = sceNetResolverCreate(&s_rid, s_resolver_buf, sizeof(s_resolver_buf));
    if (ret < 0) {
        s_resolver_ready = 0;
        s_rid = -1;
        if (s_resolver_owned) {
            sceNetResolverTerm();
            s_resolver_owned = 0;
        }
        return ret;
    }

    return 0;
}

void net_probe_shutdown(void)
{
    if (s_rid >= 0) {
        sceNetResolverDelete(s_rid);
        s_rid = -1;
    }
    if (s_resolver_owned) {
        sceNetResolverTerm();
        s_resolver_owned = 0;
    }
    s_resolver_ready = 0;
}

int net_probe_ready(void)
{
    return s_resolver_ready && s_rid >= 0;
}

int net_probe_resolve(const char *host, char *out, size_t out_size)
{
    struct in_addr addr;
    int ret;

    if (!host || !host[0] || !out || out_size < 16)
        return -1;
    if (net_is_dotted_ipv4(host)) {
        if (strlen(host) >= out_size)
            return -1;
        memcpy(out, host, strlen(host) + 1u);
        return 0;
    }
    if (!net_probe_ready())
        return -1;
    memset(&addr, 0, sizeof(addr));
    ret = sceNetResolverStartNtoA(s_rid, host, &addr, 5, 1);
    if (ret < 0)
        return -1;
    if (!sceNetInetInetNtop(AF_INET, &addr, out, (socklen_t)out_size))
        return -1;
    return 0;
}

static NetProbeResult map_sock_result(PspSockResult r)
{
    switch (r) {
    case PSP_SOCK_OK:          return NET_PROBE_OK;
    case PSP_SOCK_OK_REFUSED:  return NET_PROBE_OK_REFUSED;
    case PSP_SOCK_ERR_TIMEOUT: return NET_PROBE_ERR_TIMEOUT;
    case PSP_SOCK_ERR_UNREACHABLE: return NET_PROBE_ERR_UNREACHABLE;
    case PSP_SOCK_ERR_BAD_ADDR:    return NET_PROBE_ERR_ARG;
    default:                       return NET_PROBE_ERR_SOCKET;
    }
}

static void run_tcp_probe(const char *ip, unsigned short port,
                          const char *label, NetProbeTarget *out)
{
    PspSockProbe probe;

    memset(out, 0, sizeof(*out));
    strncpy(out->label, label, sizeof(out->label) - 1);
    out->label[sizeof(out->label) - 1] = '\0';

    (void)psp_sock_tcp_probe(ip, port, NET_PROBE_TCP_TIMEOUT_MS, &probe);
    out->result = map_sock_result(probe.result);
    out->elapsed_ms = probe.elapsed_ms;
    out->sys_errno = probe.sys_errno;
}

static NetProbeResult run_http(NetProbeReport *report, const char *dns_ip,
                               const volatile int *stop)
{
    char request[256];
    char response[NET_PROBE_HTTP_BUF_MAX];
    PspSockProbe connect_probe;
    uint32_t start;
    int request_len;
    int fd;
    int total = 0;

    report->http_result = NET_PROBE_ERR_HTTP;
    report->http_class = PROBE_HTTP_NEED_MORE;
    report->http_status = 0;

    if (!dns_ip || !dns_ip[0]) {
        report->http_result = NET_PROBE_ERR_DNS;
        return report->http_result;
    }
    if (!probe_request_allowed(NET_PROBE_DNS_HOST, NET_PROBE_HTTP_PATH)) {
        report->http_result = NET_PROBE_ERR_ARG;
        return report->http_result;
    }
    if (stopped(stop)) {
        report->http_result = NET_PROBE_ERR_ABORTED;
        report->aborted = 1;
        return report->http_result;
    }

    start = (uint32_t)sceKernelGetSystemTimeLow();
    fd = psp_sock_connect_ex(dns_ip, NET_PROBE_HTTP_PORT,
                             NET_PROBE_HTTP_CONNECT_TIMEOUT_MS,
                             &connect_probe);
    if (fd < 0) {
        /* For HTTP a refused port is a probe failure, not reachability. */
        if (connect_probe.result == PSP_SOCK_OK_REFUSED)
            report->http_result = NET_PROBE_ERR_REFUSED;
        else
            report->http_result = map_sock_result(connect_probe.result);
        report->http_elapsed_ms = connect_probe.elapsed_ms;
        return report->http_result;
    }

    if (stopped(stop)) {
        psp_sock_close_abortive(fd);
        report->http_result = NET_PROBE_ERR_ABORTED;
        report->aborted = 1;
        return report->http_result;
    }

    request_len = probe_build_http_get(request, (int)sizeof(request),
                                       NET_PROBE_DNS_HOST,
                                       NET_PROBE_HTTP_PATH);
    if (request_len < 0) {
        psp_sock_close_abortive(fd);
        report->http_result = NET_PROBE_ERR_ARG;
        return report->http_result;
    }

    if (psp_sock_send_all(fd, request, request_len, 2000) != request_len) {
        psp_sock_close_abortive(fd);
        report->http_result = NET_PROBE_ERR_SOCKET;
        return report->http_result;
    }

    for (;;) {
        ProbeHttpParse parsed;
        ProbeHttpClass cls;
        int remaining;
        int wait_ms;
        int n;

        if (stopped(stop)) {
            report->http_result = NET_PROBE_ERR_ABORTED;
            report->aborted = 1;
            break;
        }

        remaining = NET_PROBE_HTTP_TOTAL_TIMEOUT_MS -
                    (int)(((uint32_t)sceKernelGetSystemTimeLow() - start) /
                          1000u);
        if (remaining <= 0) {
            report->http_result = NET_PROBE_ERR_TIMEOUT;
            break;
        }

        wait_ms = remaining > 250 ? 250 : remaining;
        n = psp_sock_wait_readable(fd, wait_ms);
        if (n < 0) {
            report->http_result = NET_PROBE_ERR_SOCKET;
            break;
        }
        if (n == 0)
            continue;

        n = psp_sock_recv_nb(fd, response + total,
                             (int)sizeof(response) - total);
        if (n == -2)
            continue;
        if (n <= 0) {
            report->http_result = NET_PROBE_ERR_TIMEOUT;
            break;
        }

        total += n;
        cls = probe_http_parse(response, total, &parsed);
        if (cls != PROBE_HTTP_NEED_MORE) {
            report->http_class = cls;
            report->http_status = parsed.status_code;
            report->http_result = NET_PROBE_OK;
            break;
        }
        if (total >= (int)sizeof(response)) {
            report->http_class = PROBE_HTTP_MALFORMED;
            report->http_result = NET_PROBE_ERR_HTTP;
            break;
        }
    }

    psp_sock_close_abortive(fd);
    report->http_elapsed_ms =
        (int)(((uint32_t)sceKernelGetSystemTimeLow() - start) / 1000u);
    return report->http_result;
}

int net_probe_run(NetProbeReport *report, const char *gateway,
                  const volatile int *stop)
{
    struct in_addr addr;
    int i;
    int ret;

    if (!report)
        return NET_PROBE_ERR_ARG;

    memset(report, 0, sizeof(*report));
    report->started_ms = now_ms();
    report->dns_result = NET_PROBE_ERR_NOT_INIT;

    if (stopped(stop)) {
        report->aborted = 1;
        report->finished_ms = now_ms();
        return NET_PROBE_ERR_ABORTED;
    }

    if (net_probe_ready()) {
        uint32_t dns_start = (uint32_t)sceKernelGetSystemTimeLow();
        ret = sceNetResolverStartNtoA(s_rid, NET_PROBE_DNS_HOST, &addr,
                                      NET_PROBE_DNS_TIMEOUT_S,
                                      NET_PROBE_DNS_RETRY);
        report->dns_elapsed_ms =
            (int)(((uint32_t)sceKernelGetSystemTimeLow() - dns_start) / 1000u);
        if (ret >= 0) {
            if (sceNetInetInetNtop(AF_INET, &addr, report->dns_ip,
                                   (socklen_t)sizeof(report->dns_ip)))
                report->dns_result = NET_PROBE_OK;
            else
                report->dns_result = NET_PROBE_ERR_DNS;
        } else {
            report->dns_result = NET_PROBE_ERR_DNS;
        }
    }

    for (i = 0; i < NET_PROBE_TCP_COUNT; i++) {
        if (stopped(stop)) {
            report->aborted = 1;
            report->finished_ms = now_ms();
            return NET_PROBE_ERR_ABORTED;
        }
        run_tcp_probe(s_tcp_targets[i].ip, s_tcp_targets[i].port,
                      s_tcp_targets[i].label, &report->tcp[i]);
    }

    if (stopped(stop)) {
        report->aborted = 1;
        report->finished_ms = now_ms();
        return NET_PROBE_ERR_ABORTED;
    }

    if (gateway && gateway[0]) {
        report->gateway_configured = 1;
        run_tcp_probe(gateway, NET_PROBE_GATEWAY_PORT, "gateway:80",
                      &report->gateway);
    }

    if (stopped(stop)) {
        report->aborted = 1;
        report->finished_ms = now_ms();
        return NET_PROBE_ERR_ABORTED;
    }

    if (report->dns_result != NET_PROBE_OK) {
        report->http_result = NET_PROBE_ERR_DNS;
    } else {
        run_http(report, report->dns_ip, stop);
    }

    report->finished_ms = now_ms();
    if (report->aborted)
        return NET_PROBE_ERR_ABORTED;
    return 0;
}

const char *net_probe_result_name(NetProbeResult result)
{
    switch (result) {
    case NET_PROBE_OK:             return "ok";
    case NET_PROBE_OK_REFUSED:     return "refused";
    case NET_PROBE_ERR_ARG:        return "bad arg";
    case NET_PROBE_ERR_NOT_INIT:   return "not available";
    case NET_PROBE_ERR_DNS:        return "dns failed";
    case NET_PROBE_ERR_TIMEOUT:    return "timeout";
    case NET_PROBE_ERR_REFUSED:    return "refused";
    case NET_PROBE_ERR_UNREACHABLE:return "unreachable";
    case NET_PROBE_ERR_SOCKET:     return "socket error";
    case NET_PROBE_ERR_HTTP:       return "http error";
    case NET_PROBE_ERR_ABORTED:    return "aborted";
    default:                       return "?";
    }
}

const char *net_probe_http_class_name(ProbeHttpClass c)
{
    return probe_http_class_name(c);
}
