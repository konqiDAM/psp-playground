/*
 * storage.h - PSP ms0: settings persistence for Racing 3D.
 *
 * The portable format/validation lives in settings.c; this module is the
 * only place that touches the memory stick.
 */

#ifndef RACING_STORAGE_H
#define RACING_STORAGE_H

#include <stdbool.h>

#include "hiscore.h"
#include "settings.h"

/*
 * Loads the 16-byte config, falling back to RACING.CFG.BAK. A file that is not
 * exactly 16 bytes is ignored, not truncated to 16. Applies defaults and
 * returns false on failure.
 */
bool storage_load_settings(racing_settings_t *s);

/*
 * Validates and writes the 16-byte config through RACING.CFG.NEW (read back and
 * verified) and a RACING.CFG.BAK rotation, because FAT sceIoRename does not
 * replace: at every instant after the first successful save at least one valid
 * file exists. (A device's very first save has no previous file to keep; a
 * power loss there leaves only .NEW, which the loader ignores.) A current
 * config that does not parse is dropped, never rotated: junk must not
 * overwrite the last valid .BAK. False on any I/O or verification failure.
 */
bool storage_save_settings(const racing_settings_t *s);

/*
 * Loads RACING.HOF, falling back to RACING.HOF.BAK. Returns whether a valid
 * table was loaded; on any failure the book is cleared and false returned.
 */
bool storage_load_hiscore(hiscore_book_t *book);

/*
 * Writes RACING.HOF through RACING.HOF.NEW (read back and verified) and a
 * RACING.HOF.BAK rotation, because FAT sceIoRename does not replace. False on
 * any I/O or verification failure; the in-RAM book is never modified.
 */
bool storage_save_hiscore(const hiscore_book_t *book);

/* Local PSP clock, stamped at finish. False (and 0/0/0) when unavailable. */
bool storage_read_date(int *year, int *month, int *day);

#endif /* RACING_STORAGE_H */
