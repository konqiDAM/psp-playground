/*
 * storage.h - PSP ms0: settings persistence for Racing 3D.
 *
 * The portable format/validation lives in settings.c; this module is the
 * only place that touches the memory stick.
 */

#ifndef RACING_STORAGE_H
#define RACING_STORAGE_H

#include <stdbool.h>

#include "settings.h"

/* Loads the 16-byte config; applies defaults and returns false on failure. */
bool storage_load_settings(racing_settings_t *s);

/* Validates and writes the 16-byte config; false on any I/O failure. */
bool storage_save_settings(const racing_settings_t *s);

#endif /* RACING_STORAGE_H */
