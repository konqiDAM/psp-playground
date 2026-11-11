/*
 * tests/host/psp/pspiofilemgr.h - test-only stand-in for the PSP SDK header.
 *
 * tests/test_racing_storage.c links tests/host/psp/psp_stub.c, which maps
 * ms0: onto a temporary directory and injects I/O failures. Only the surface
 * racing/storage.c uses is declared here; `st_size` matches the real SceIoStat
 * field (pspiofilemgr_stat.h:106), which the exact-size rule reads.
 */

#ifndef PSPIOFILEMGR_HOST_STUB_H
#define PSPIOFILEMGR_HOST_STUB_H

#include <stddef.h>

typedef int SceUID;
typedef unsigned int SceSize;
typedef long long SceOff;

typedef struct SceIoStatHost {
    SceOff st_size;
} SceIoStat;

#define PSP_O_RDONLY 0x0001
#define PSP_O_WRONLY 0x0002
#define PSP_O_CREAT 0x0200
#define PSP_O_TRUNC 0x0400

SceUID sceIoOpen(const char *path, int flags, int mode);
int sceIoRead(SceUID fd, void *buf, SceSize n);
int sceIoWrite(SceUID fd, const void *buf, SceSize n);
int sceIoClose(SceUID fd);
int sceIoRemove(const char *path);
int sceIoRename(const char *oldpath, const char *newpath);
int sceIoGetstat(const char *path, SceIoStat *st);
int sceIoMkdir(const char *path, int mode);
int sceIoSync(const char *dev, int flag);

#endif /* PSPIOFILEMGR_HOST_STUB_H */
