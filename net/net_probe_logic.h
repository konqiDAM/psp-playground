/*
 * net_probe_logic.h - portable HTTP probe parsing and allowlist.
 *
 * No PSP headers and no I/O: host-tested and shared with net/net_probe.c.
 */
#ifndef PSP_NET_PROBE_LOGIC_H
#define PSP_NET_PROBE_LOGIC_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PROBE_HTTP_NEED_MORE = 0,
    PROBE_HTTP_NO_CONTENT,     /* 204 */
    PROBE_HTTP_PORTAL,         /* 2xx other than 204, 3xx, 511 */
    PROBE_HTTP_ERROR,          /* 1xx, 4xx, 5xx other than 511 */
    PROBE_HTTP_MALFORMED
} ProbeHttpClass;

typedef struct {
    int status_code;
    int header_end;            /* index just past CRLFCRLF, -1 if unknown */
    ProbeHttpClass class;
} ProbeHttpParse;

ProbeHttpClass probe_http_parse(const char *data, int len, ProbeHttpParse *out);
ProbeHttpClass probe_http_classify(int status_code);
int probe_request_allowed(const char *host, const char *path);
int probe_build_http_get(char *buf, int size, const char *host, const char *path);
const char *probe_http_class_name(ProbeHttpClass c);
int net_is_dotted_ipv4(const char *host);

#ifdef __cplusplus
}
#endif

#endif /* PSP_NET_PROBE_LOGIC_H */
