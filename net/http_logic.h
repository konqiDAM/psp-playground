/*
 * http_logic.h - portable HTTP response head parser and request builder.
 *
 * No PSP headers: host-tested and used by net/http_client.c.
 */
#ifndef PSP_NET_HTTP_LOGIC_H
#define PSP_NET_HTTP_LOGIC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HTTP_HEAD_MAX 4096

typedef enum {
    HTTP_PARSE_NEED_MORE = 0,
    HTTP_PARSE_OK,
    HTTP_PARSE_MALFORMED,
    HTTP_PARSE_TOO_LARGE
} HttpParseResult;

typedef struct {
    int status;
    int has_content_length;
    uint32_t content_length;
    int has_content_range;
    uint32_t range_start;
    uint32_t range_end;
    uint32_t range_total;
    int chunked;
    int encoded;
    int has_etag;
    int etag_strong;
    char etag[80];
    int has_last_modified;
    char last_modified[40];
    int header_end;
} HttpResponseHead;

HttpParseResult http_parse_head(const char *data, int len, HttpResponseHead *out);
const char *http_parse_result_name(HttpParseResult result);

int http_build_get(char *buf, int size, const char *host, unsigned short port,
                   const char *path, int has_range, uint32_t range_start,
                   const char *if_range);

/* Splits "http://host[:port]/path"; 0 = ok, -1 = invalid. */
int http_url_split(const char *url, char *host, int host_size,
                   unsigned short *port, char *path, int path_size);

#ifdef __cplusplus
}
#endif

#endif /* PSP_NET_HTTP_LOGIC_H */
