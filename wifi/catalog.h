/*
 * catalog.h - portable catalog manifest, URL and config parser.
 *
 * No PSP headers: host-tested and used by wifi/main.c and download_psp.c.
 */
#ifndef PSP_WIFI_CATALOG_H
#define PSP_WIFI_CATALOG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CATALOG_MAX_ENTRIES 16
#define CATALOG_MAX_BYTES 8192
#define CATALOG_TITLE_MAX 48
#define CATALOG_ID_MAX 32
#define CATALOG_FOLDER_MAX 32
#define CATALOG_PATH_MAX 96
#define CATALOG_VERSION_MAX 16
#define CATALOG_URL_MAX 192
#define CATALOG_MAX_FILE_BYTES (64u * 1024u * 1024u)

typedef struct {
    char id[CATALOG_ID_MAX];
    char title[CATALOG_TITLE_MAX];
    char version[CATALOG_VERSION_MAX];
    uint32_t size;
    char sha256[65];
    char folder[CATALOG_FOLDER_MAX];
    char path[CATALOG_PATH_MAX];
} CatalogEntry;

typedef struct {
    char host[64];
    unsigned short port;
    char path[128];
    char base[CATALOG_URL_MAX];
} CatalogUrl;

typedef struct {
    CatalogEntry entries[CATALOG_MAX_ENTRIES];
    int count;
} Catalog;

typedef enum {
    CATALOG_OK = 0,
    CATALOG_ERR_HEADER,
    CATALOG_ERR_ENTRY,
    CATALOG_ERR_TOO_MANY,
    CATALOG_ERR_TOO_LARGE,
    CATALOG_ERR_FIELD,
    CATALOG_ERR_VALUE
} CatalogResult;

CatalogResult catalog_parse(const char *text, int len, Catalog *out);
const char *catalog_result_name(CatalogResult result);

int catalog_version_compare(const char *a, const char *b); /* -1/0/1, 2 invalid */
int catalog_version_valid(const char *v);

int catalog_url_parse(const char *url, CatalogUrl *out);
int catalog_url_join(const CatalogUrl *base, const char *rel,
                     char *out, int size);

/* 0 = ok (including no url), -1 = malformed. Empty out = use default. */
int catalog_config_parse(const char *text, int len, char *url_out, int size);

#ifdef __cplusplus
}
#endif

#endif /* PSP_WIFI_CATALOG_H */
