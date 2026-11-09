/*
 * storage.c - PSP ms0: settings persistence for Racing 3D.
 *
 * Path: ms0:/PSP/SAVEDATA/RACING3D/RACING.CFG (16 bytes).
 * All failures are non-fatal: the game keeps running with in-RAM settings.
 */

#include "storage.h"

#include <pspiofilemgr.h>

#define SETTINGS_DIR "ms0:/PSP/SAVEDATA/RACING3D"
#define SETTINGS_PATH SETTINGS_DIR "/RACING.CFG"

bool storage_load_settings(racing_settings_t *s)
{
    uint8_t buf[RACING_SETTINGS_FILE_SIZE];
    SceUID fd;
    int n;

    if (!s)
        return false;
    settings_defaults(s);

    fd = sceIoOpen(SETTINGS_PATH, PSP_O_RDONLY, 0);
    if (fd < 0)
        return false;
    n = sceIoRead(fd, buf, (SceSize)sizeof(buf));
    sceIoClose(fd);
    if (n != (int)sizeof(buf))
        return false;
    return settings_deserialize(s, buf, (int)sizeof(buf));
}

bool storage_save_settings(const racing_settings_t *s)
{
    uint8_t buf[RACING_SETTINGS_FILE_SIZE];
    racing_settings_t validated;
    SceUID fd;
    int n;

    if (!s)
        return false;

    validated = *s;
    settings_validate(&validated);
    if (settings_serialize(&validated, buf, (int)sizeof(buf)) !=
        (int)sizeof(buf))
        return false;

    sceIoMkdir("ms0:/PSP", 0777);
    sceIoMkdir("ms0:/PSP/SAVEDATA", 0777);
    sceIoMkdir(SETTINGS_DIR, 0777);

    fd = sceIoOpen(SETTINGS_PATH,
                   PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0666);
    if (fd < 0)
        return false;
    n = sceIoWrite(fd, buf, (SceSize)sizeof(buf));
    sceIoClose(fd);
    return n == (int)sizeof(buf);
}
