/*
 * host_download_io.h - host-only POSIX transport/storage for the mock server.
 */
#ifndef PSP_HOST_DOWNLOAD_IO_H
#define PSP_HOST_DOWNLOAD_IO_H

#include "download.h"

typedef struct {
    char root[256];
} HostStorage;

typedef struct {
    int fd;
    unsigned char tail[4096];
    int tail_len;
    int tail_pos;
    uint32_t body_left;
    int has_body_left;
    int last_status;
    int opened;
} HostTransport;

void host_storage_init(HostStorage *s, const char *root);
void host_storage_env(DlStorage *st, HostStorage *s);

void host_transport_init(HostTransport *t);
void host_transport_env(DlTransport *tr, HostTransport *t);

#endif /* PSP_HOST_DOWNLOAD_IO_H */
