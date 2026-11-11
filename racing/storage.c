/*
 * storage.c - PSP ms0: settings persistence for Racing 3D.
 *
 * Path: ms0:/PSP/SAVEDATA/RACING3D/RACING.CFG (16 bytes, version 2).
 * The config is saved through RACING.CFG.NEW and rotated to RACING.CFG.BAK, so
 * once the first save has succeeded a power loss can never leave the device
 * without a valid file. All failures are non-fatal: the game keeps running
 * with in-RAM settings.
 */

#include "storage.h"

#include <pspiofilemgr.h>
#include <psprtc.h>
#include <string.h>

#define SAVE_DIR "ms0:/PSP/SAVEDATA/RACING3D"
#define SETTINGS_PATH SAVE_DIR "/RACING.CFG"
#define SETTINGS_NEW_PATH SAVE_DIR "/RACING.CFG.NEW"
#define SETTINGS_BAK_PATH SAVE_DIR "/RACING.CFG.BAK"
#define HISCORE_PATH SAVE_DIR "/RACING.HOF"
#define HISCORE_NEW_PATH SAVE_DIR "/RACING.HOF.NEW"
#define HISCORE_BAK_PATH SAVE_DIR "/RACING.HOF.BAK"

static void ensure_save_dir(void)
{
    sceIoMkdir("ms0:/PSP", 0777);
    sceIoMkdir("ms0:/PSP/SAVEDATA", 0777);
    sceIoMkdir(SAVE_DIR, 0777);
}

/* Reads and validates a 16-byte config; false on any short read or rejection. */
static bool read_settings_file(const char *path, racing_settings_t *s)
{
    uint8_t buf[RACING_SETTINGS_FILE_SIZE];
    SceIoStat st;
    SceUID fd;
    int n;

    /* Exactly 16 bytes, not "at least 16": a truncating read would silently
     * accept a file with a stray byte past byte 16, while scripts/racing_cfg.py
     * rejects it. The game and the probes must agree on what a config is. */
    if (sceIoGetstat(path, &st) < 0 ||
        st.st_size != RACING_SETTINGS_FILE_SIZE)
        return false;
    fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0)
        return false;
    n = sceIoRead(fd, buf, (SceSize)sizeof(buf));
    sceIoClose(fd);
    if (n != (int)sizeof(buf))
        return false;
    return settings_deserialize(s, buf, (int)sizeof(buf));
}

bool storage_load_settings(racing_settings_t *s)
{
    if (!s)
        return false;
    settings_defaults(s);

    /* A save rotates the previous file to .BAK, so a power loss between the
     * two renames leaves the last valid config there. */
    if (read_settings_file(SETTINGS_PATH, s))
        return true;
    settings_defaults(s);
    return read_settings_file(SETTINGS_BAK_PATH, s);
}

/*
 * Crash-safe save, the same .NEW -> read back -> .BAK -> rename rotation the
 * leaderboard uses (sceIoRename does not replace on FAT, so the old file must
 * move aside first).
 *
 * At every instant AFTER THE FIRST SUCCESSFUL SAVE at least one VALID file
 * exists:
 *   write .NEW (CFG untouched) -> validate .NEW -> remove .BAK (CFG untouched)
 *   -> CFG becomes .BAK -> .NEW becomes CFG -> remove .BAK.
 * A power loss leaves CFG, or .BAK (which storage_load_settings() falls back
 * to), or both - never a torn file. The one exception is the very first save
 * on a device: a power loss between the .NEW write and the rename leaves only
 * .NEW, which the loader ignores, so the next boot starts from defaults.
 *
 * The .BAK remove before the rotation is LOAD-BEARING whenever CFG and .BAK
 * are BOTH present - after a power loss between the final rename and the
 * cleanup, or after a 0.7.0 downgrade that rewrote CFG directly and left the
 * .BAK behind. rename(CFG -> CFG.BAK) cannot replace an existing .BAK, so
 * without the remove it fails, then rename(NEW -> CFG) fails because CFG still
 * exists, and every later save fails the same way. Pinned by
 * check-racing-030.py's "CFG+BAK" case; the ordinary second save does NOT
 * prove it, because the cleanup below has already deleted the .BAK.
 *
 * A CFG that does not parse is NOT rotated aside. It is not a backup, and
 * moving it would overwrite the last valid .BAK with junk; a failed
 * NEW -> CFG rename would then restore the junk, and the next load would see
 * defaults even though a valid .BAK (or the read-back validated .NEW) was on
 * the card. Dropping the junk CFG keeps the .BAK, so a valid file survives
 * every failure path. Pinned by tests/test_racing_storage.c case 14.
 */
bool storage_save_settings(const racing_settings_t *s)
{
    uint8_t buf[RACING_SETTINGS_FILE_SIZE];
    racing_settings_t validated;
    racing_settings_t parsed;
    racing_settings_t current;
    SceIoStat st;
    SceUID fd;
    bool rotated = false;
    int n;

    if (!s)
        return false;

    validated = *s;
    settings_validate(&validated);
    if (settings_serialize(&validated, buf, (int)sizeof(buf)) !=
        (int)sizeof(buf))
        return false;

    ensure_save_dir();

    fd = sceIoOpen(SETTINGS_NEW_PATH,
                   PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0666);
    if (fd < 0)
        return false;
    n = sceIoWrite(fd, buf, (SceSize)sizeof(buf));
    sceIoClose(fd);
    if (n != (int)sizeof(buf))
        return false;
    (void)sceIoSync("ms0:", 0);

    /* Never trust the write: read it back and validate the same way. */
    if (!read_settings_file(SETTINGS_NEW_PATH, &parsed))
        return false;

    if (sceIoGetstat(SETTINGS_PATH, &st) >= 0) {
        if (read_settings_file(SETTINGS_PATH, &current)) {
            /* Load-bearing whenever CFG and .BAK are BOTH present: see the
             * block comment above. The ordinary second save finds no .BAK, so
             * this is a no-op there. */
            (void)sceIoRemove(SETTINGS_BAK_PATH);
            if (sceIoRename(SETTINGS_PATH, SETTINGS_BAK_PATH) < 0)
                return false;
            rotated = true;
        } else if (sceIoRemove(SETTINGS_PATH) < 0) {
            /* Junk we cannot clear: the rename below would fail anyway. */
            return false;
        }
    }
    if (sceIoRename(SETTINGS_NEW_PATH, SETTINGS_PATH) < 0) {
        /* Put back only what this save moved aside. After a rotation the .BAK
         * is the previous config. Without one it is an older file the loader
         * was already falling back to, and moving it to CFG would leave the
         * card with no backup copy. */
        if (rotated)
            (void)sceIoRename(SETTINGS_BAK_PATH, SETTINGS_PATH);
        return false;
    }
    (void)sceIoSync("ms0:", 0);
    (void)sceIoRemove(SETTINGS_BAK_PATH);
    (void)sceIoSync("ms0:", 0);
    return true;
}

/* ------------------------------------------------------- leaderboard file */

static bool read_file_exact(const char *path, uint8_t *buf, int size)
{
    SceUID fd;
    int n;

    fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0)
        return false;
    n = sceIoRead(fd, buf, (SceSize)size);
    sceIoClose(fd);
    return n == size;
}

static bool write_file_exact(const char *path, const uint8_t *buf, int size)
{
    SceUID fd;
    int n;

    fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0666);
    if (fd < 0)
        return false;
    n = sceIoWrite(fd, buf, (SceSize)size);
    sceIoClose(fd);
    return n == size;
}

bool storage_load_hiscore(hiscore_book_t *book)
{
    uint8_t buf[HISCORE_FILE_SIZE];
    hiscore_book_t parsed;

    if (!book)
        return false;
    hiscore_clear(book);

    if (read_file_exact(HISCORE_PATH, buf, (int)sizeof(buf)) &&
        hiscore_deserialize(&parsed, buf, (int)sizeof(buf))) {
        *book = parsed;
        return true;
    }
    if (read_file_exact(HISCORE_BAK_PATH, buf, (int)sizeof(buf)) &&
        hiscore_deserialize(&parsed, buf, (int)sizeof(buf))) {
        *book = parsed;
        return true;
    }
    return false;
}

bool storage_save_hiscore(const hiscore_book_t *book)
{
    uint8_t buf[HISCORE_FILE_SIZE];
    uint8_t check[HISCORE_FILE_SIZE];
    hiscore_book_t parsed;
    SceIoStat st;
    bool had_prev;

    if (!book)
        return false;
    if (hiscore_serialize(book, buf, (int)sizeof(buf)) !=
        (int)sizeof(buf))
        return false;

    ensure_save_dir();

    if (!write_file_exact(HISCORE_NEW_PATH, buf, (int)sizeof(buf)))
        return false;
    (void)sceIoSync("ms0:", 0);

    /* Never trust the write: read it back and validate the same way. */
    if (!read_file_exact(HISCORE_NEW_PATH, check, (int)sizeof(check)) ||
        !hiscore_deserialize(&parsed, check, (int)sizeof(check)))
        return false;

    had_prev = sceIoGetstat(HISCORE_PATH, &st) >= 0;
    if (had_prev) {
        (void)sceIoRemove(HISCORE_BAK_PATH);
        if (sceIoRename(HISCORE_PATH, HISCORE_BAK_PATH) < 0)
            return false;
    }
    if (sceIoRename(HISCORE_NEW_PATH, HISCORE_PATH) < 0) {
        /* Restore only what this save moved aside; with no table at entry the
         * .BAK is an older file the loader was ignoring (same rule as the
         * settings save above). */
        if (had_prev)
            (void)sceIoRename(HISCORE_BAK_PATH, HISCORE_PATH);
        return false;
    }
    (void)sceIoSync("ms0:", 0);
    (void)sceIoRemove(HISCORE_BAK_PATH);
    (void)sceIoSync("ms0:", 0);
    return true;
}

bool storage_read_date(int *year, int *month, int *day)
{
    ScePspDateTime t;

    if (!year || !month || !day)
        return false;
    *year = 0;
    *month = 0;
    *day = 0;
    if (sceRtcGetCurrentClockLocalTime(&t) < 0)
        return false;
    if (t.year < 2000 || t.year > 2200 || t.month < 1 || t.month > 12 ||
        t.day < 1 || t.day > 31)
        return false;
    *year = (int)t.year;
    *month = (int)t.month;
    *day = (int)t.day;
    return true;
}
