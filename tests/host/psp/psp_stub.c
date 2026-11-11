/*
 * tests/host/psp/psp_stub.c - test-only implementation of the PSP I/O calls
 * racing/storage.c makes, backed by a host directory.
 *
 * ms0:/PSP/SAVEDATA/RACING3D/RACING.CFG maps onto
 * <root>/PSP/SAVEDATA/RACING3D/RACING.CFG. See psp_stub.h for the injection
 * flags; sceIoRename() also refuses to replace an existing destination, which
 * is the FAT rule the production code is built around (and the reason the
 * .BAK must be removed before the rotation).
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "pspiofilemgr.h"
#include "psprtc.h"
#include "psp_stub.h"

#define STUB_MAX_PATH 1024
#define STUB_MAX_FDS 16

static char stub_root[512];

int psp_stub_fail_write_short;
int psp_stub_fail_readback_corrupt;
int psp_stub_fail_remove_bak;
int psp_stub_fail_rename_cfg_to_bak;
int psp_stub_fail_rename_new_to_cfg;
int psp_stub_fail_rename_bak_restore;

static FILE *stub_fds[STUB_MAX_FDS];

void psp_stub_set_root(const char *root)
{
    snprintf(stub_root, sizeof(stub_root), "%s", root ? root : "");
}

void psp_stub_reset(void)
{
    int i;

    psp_stub_fail_write_short = 0;
    psp_stub_fail_readback_corrupt = 0;
    psp_stub_fail_remove_bak = 0;
    psp_stub_fail_rename_cfg_to_bak = 0;
    psp_stub_fail_rename_new_to_cfg = 0;
    psp_stub_fail_rename_bak_restore = 0;
    for (i = 0; i < STUB_MAX_FDS; i++) {
        if (stub_fds[i]) {
            fclose(stub_fds[i]);
            stub_fds[i] = NULL;
        }
    }
}

static void stub_map(char *out, size_t n, const char *path)
{
    const char *rel = path;

    if (rel && strncmp(rel, "ms0:", 4) == 0)
        rel += 4;
    snprintf(out, n, "%s%s", stub_root, rel ? rel : "");
}

static int ends_with(const char *s, const char *suffix)
{
    size_t n, m;

    if (!s || !suffix)
        return 0;
    n = strlen(s);
    m = strlen(suffix);
    return n >= m && strcmp(s + n - m, suffix) == 0;
}

/* Creates every directory component of a mapped path; ignores EEXIST. */
static void stub_mkdirs(const char *path)
{
    char tmp[STUB_MAX_PATH];
    char *q;

    snprintf(tmp, sizeof(tmp), "%s", path);
    for (q = tmp + 1; *q; q++) {
        if (*q == '/') {
            *q = '\0';
            (void)mkdir(tmp, 0777);
            *q = '/';
        }
    }
}

SceUID sceIoOpen(const char *path, int flags, int mode)
{
    char real[STUB_MAX_PATH];
    const char *how;
    int i;

    (void)mode;
    stub_map(real, sizeof(real), path);
    if (flags & PSP_O_CREAT)
        stub_mkdirs(real);
    if ((flags & (PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC)) == 0)
        how = "rb";
    else if (flags & PSP_O_TRUNC)
        how = "wb";
    else
        how = "ab";
    for (i = 0; i < STUB_MAX_FDS; i++) {
        if (!stub_fds[i]) {
            stub_fds[i] = fopen(real, how);
            return stub_fds[i] ? i + 1 : -1;
        }
    }
    return -1;
}

int sceIoRead(SceUID fd, void *buf, SceSize n)
{
    FILE *f;

    if (fd < 1 || fd > STUB_MAX_FDS)
        return -1;
    f = stub_fds[fd - 1];
    if (!f)
        return -1;
    return (int)fread(buf, 1, n, f);
}

int sceIoWrite(SceUID fd, const void *buf, SceSize n)
{
    unsigned char copy[64];
    const void *src = buf;
    FILE *f;

    if (fd < 1 || fd > STUB_MAX_FDS)
        return -1;
    f = stub_fds[fd - 1];
    if (!f)
        return -1;
    if (psp_stub_fail_write_short)
        n = 4;
    if (psp_stub_fail_readback_corrupt && n > 0) {
        SceSize k = n < sizeof(copy) ? n : (SceSize)sizeof(copy);
        memcpy(copy, buf, k);
        copy[k - 1] ^= 0x01; /* break the CRC (or the last payload byte) */
        src = copy;
    }
    return (int)fwrite(src, 1, n, f);
}

int sceIoClose(SceUID fd)
{
    if (fd < 1 || fd > STUB_MAX_FDS || !stub_fds[fd - 1])
        return -1;
    fclose(stub_fds[fd - 1]);
    stub_fds[fd - 1] = NULL;
    return 0;
}

int sceIoRemove(const char *path)
{
    char real[STUB_MAX_PATH];

    stub_map(real, sizeof(real), path);
    if (psp_stub_fail_remove_bak && ends_with(path, ".BAK"))
        return -1;
    return remove(real) == 0 ? 0 : -1;
}

int sceIoRename(const char *oldpath, const char *newpath)
{
    char from[STUB_MAX_PATH];
    char to[STUB_MAX_PATH];

    stub_map(from, sizeof(from), oldpath);
    stub_map(to, sizeof(to), newpath);
    if (psp_stub_fail_rename_cfg_to_bak &&
        ends_with(oldpath, "RACING.CFG") && ends_with(newpath, ".BAK"))
        return -1;
    if (psp_stub_fail_rename_new_to_cfg &&
        ends_with(oldpath, ".NEW") && ends_with(newpath, "RACING.CFG"))
        return -1;
    if (psp_stub_fail_rename_bak_restore &&
        ends_with(oldpath, ".BAK") && ends_with(newpath, "RACING.CFG"))
        return -1;
    /* FAT: a rename does not replace an existing destination. */
    if (access(to, F_OK) == 0)
        return -1;
    return rename(from, to) == 0 ? 0 : -1;
}

int sceIoGetstat(const char *path, SceIoStat *st)
{
    char real[STUB_MAX_PATH];
    struct stat sb;

    stub_map(real, sizeof(real), path);
    if (stat(real, &sb) != 0)
        return -1;
    if (st)
        st->st_size = (SceOff)sb.st_size;
    return 0;
}

int sceIoMkdir(const char *path, int mode)
{
    char real[STUB_MAX_PATH];

    (void)mode;
    stub_map(real, sizeof(real), path);
    return mkdir(real, 0777) == 0 ? 0 : -1;
}

int sceIoSync(const char *dev, int flag)
{
    (void)dev;
    (void)flag;
    return 0;
}

int sceRtcGetCurrentClockLocalTime(ScePspDateTime *t)
{
    if (!t)
        return -1;
    memset(t, 0, sizeof(*t));
    t->year = 2026;
    t->month = 11;
    t->day = 10;
    return 0;
}
