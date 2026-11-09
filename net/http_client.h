/*
 * http_client.h - small PSP HTTP GET transport over net_sock.
 */
#ifndef PSP_NET_HTTP_CLIENT_H
#define PSP_NET_HTTP_CLIENT_H

#include <stdint.h>

#include "http_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HTTP_CLIENT_ERR_NET          (-1)
#define HTTP_CLIENT_ERR_TIMEOUT      (-2)
#define HTTP_CLIENT_ERR_BAD_RESPONSE (-3)
#define HTTP_CLIENT_ERR_CANCELLED    (-4)

/* URL is validated by catalog_url_join/catalog_url_parse; the transport
 * re-checks scheme/host/path with http_url_split before any socket. */
int  http_client_open(const char *url, int has_range, uint32_t range_start,
                      const char *if_range, HttpResponseHead *out,
                      int *net_err, void **handle);
int  http_client_open_ex(const char *url, int has_range, uint32_t range_start,
                         const char *if_range, int connect_timeout_ms,
                         HttpResponseHead *out, int *net_err, void **handle);
/* >0 bytes, 0 EOF, <0 error with *net_err set. */
int  http_client_read(void *handle, void *buf, int max, int *net_err);
void http_client_close(void *handle);

/* Optional stop check for long reads; returns 0 to continue or a DlError
 * value (positive) to abort. Called about once per second. */
typedef int (*HttpClientStopFn)(void *ctx);
void http_client_set_stop_check(HttpClientStopFn fn, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* PSP_NET_HTTP_CLIENT_H */
