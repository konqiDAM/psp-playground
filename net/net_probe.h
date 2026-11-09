/*
 * net_probe.h - DNS / TCP / gateway / HTTP-204 probes for the PSP.
 */
#ifndef PSP_NET_PROBE_H
#define PSP_NET_PROBE_H

#include <stddef.h>
#include <stdint.h>

#include "net_probe_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NET_PROBE_TCP_COUNT 3
#define NET_PROBE_TCP_TIMEOUT_MS         2000
#define NET_PROBE_GATEWAY_TIMEOUT_MS     1500
#define NET_PROBE_GATEWAY_PORT             80
#define NET_PROBE_DNS_TIMEOUT_S             2
#define NET_PROBE_DNS_RETRY                 1
#define NET_PROBE_HTTP_CONNECT_TIMEOUT_MS 2000
#define NET_PROBE_HTTP_TOTAL_TIMEOUT_MS   4000
#define NET_PROBE_HTTP_PORT                80
#define NET_PROBE_DNS_HOST "connectivitycheck.gstatic.com"
#define NET_PROBE_HTTP_PATH "/generate_204"
#define NET_PROBE_HTTP_BUF_MAX 1024

typedef enum {
    NET_PROBE_OK = 0,
    NET_PROBE_OK_REFUSED = 1,     /* TCP RST: host reachable, port closed */
    NET_PROBE_ERR_ARG = -1,
    NET_PROBE_ERR_NOT_INIT = -2,
    NET_PROBE_ERR_DNS = -3,
    NET_PROBE_ERR_TIMEOUT = -4,
    NET_PROBE_ERR_REFUSED = -5,
    NET_PROBE_ERR_UNREACHABLE = -6,
    NET_PROBE_ERR_SOCKET = -7,
    NET_PROBE_ERR_HTTP = -8,
    NET_PROBE_ERR_ABORTED = -9
} NetProbeResult;

typedef struct {
    char label[24];
    NetProbeResult result;
    int elapsed_ms;
    int sys_errno;
} NetProbeTarget;

typedef struct {
    uint32_t started_ms;
    uint32_t finished_ms;
    NetProbeResult dns_result;
    int dns_elapsed_ms;
    char dns_ip[16];
    NetProbeTarget tcp[NET_PROBE_TCP_COUNT];
    int gateway_configured;
    NetProbeTarget gateway;
    NetProbeResult http_result;
    int http_elapsed_ms;
    int http_status;
    ProbeHttpClass http_class;
    int aborted;
} NetProbeReport;

int net_probe_init(void);
void net_probe_shutdown(void);
int net_probe_ready(void);
int net_probe_resolve(const char *host, char *out, size_t out_size);
int net_probe_run(NetProbeReport *report, const char *gateway,
                  const volatile int *stop);
const char *net_probe_result_name(NetProbeResult result);
const char *net_probe_http_class_name(ProbeHttpClass c);

#ifdef __cplusplus
}
#endif

#endif /* PSP_NET_PROBE_H */
