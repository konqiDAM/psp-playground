/*
 * test_racing_storage.c - host tests for racing/storage.c's save state machine.
 *
 * racing/storage.c is PSP-only I/O, so this test links a test-only shim
 * (tests/host/psp/) that maps ms0: onto a temporary directory and injects the
 * failures a memory stick produces. The shim exists because the interesting
 * states are failure states: PPSSPP refuses to replace an existing file
 * (which is the .BAK-remove rule) but cannot be told to fail a rename that
 * would otherwise succeed, and the "a corrupt CFG is not a backup" hazard
 * lives exactly there.
 *
 * Every sequence prints its outcome (save result, which files survive, and
 * what the next load sees), so a change in the rotation is visible in the log
 * rather than only in a pass/fail.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "settings.h"
#include "storage.h"

#include "psp/psp_stub.h"

static int failures;
static char work_root[256];
static char cfg_path[512];
static char bak_path[512];
static char new_path[512];

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static int path_exists(const char *path)
{
    struct stat sb;

    return stat(path, &sb) == 0;
}

static void mkdir_p(const char *path)
{
    char tmp[512];
    char *q;

    snprintf(tmp, sizeof(tmp), "%s", path);
    for (q = tmp + 1; *q; q++) {
        if (*q == '/') {
            *q = '\0';
            (void)mkdir(tmp, 0777);
            *q = '/';
        }
    }
    (void)mkdir(tmp, 0777);
}

/* Removes the three save files and the directories, keeping the root. */
static void wipe(void)
{
    char dir[512];

    (void)remove(cfg_path);
    (void)remove(bak_path);
    (void)remove(new_path);
    snprintf(dir, sizeof(dir), "%s", cfg_path);
    if (strrchr(dir, '/')) {
        *strrchr(dir, '/') = '\0';
        (void)rmdir(dir); /* RACING3D */
    }
    if (strrchr(dir, '/')) {
        *strrchr(dir, '/') = '\0';
        (void)rmdir(dir); /* SAVEDATA */
    }
    if (strrchr(dir, '/')) {
        *strrchr(dir, '/') = '\0';
        (void)rmdir(dir); /* PSP */
    }
    psp_stub_reset();
}

/* Writes a config the game itself would accept, at `sign` percent. */
static void put_valid(const char *path, int sign)
{
    racing_settings_t s;
    uint8_t buf[RACING_SETTINGS_FILE_SIZE];
    FILE *f;
    char dir[512];
    char *slash;

    snprintf(dir, sizeof(dir), "%s", path);
    slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        mkdir_p(dir);
    }
    settings_defaults(&s);
    s.sign_scale_pct = sign;
    if (settings_serialize(&s, buf, (int)sizeof(buf)) !=
        (int)sizeof(buf)) {
        fprintf(stderr, "cannot serialize a fixture\n");
        exit(2);
    }
    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        exit(2);
    }
    if (fwrite(buf, 1, sizeof(buf), f) != sizeof(buf)) {
        fprintf(stderr, "short fixture write\n");
        exit(2);
    }
    fclose(f);
}

static long file_size(const char *path)
{
    struct stat sb;

    return stat(path, &sb) == 0 ? (long)sb.st_size : -1;
}

/* Breaks the CRC but leaves every field in range (byte 6 110 -> 111). */
static void corrupt_crc(const char *path)
{
    FILE *f = fopen(path, "r+b");
    int c;

    if (!f)
        exit(2);
    if (fseek(f, 6, SEEK_SET) != 0)
        exit(2);
    c = fgetc(f);
    if (c == EOF)
        exit(2);
    if (fseek(f, 6, SEEK_SET) != 0)
        exit(2);
    fputc(c ^ 0x01, f);
    fclose(f);
}

static void append_byte(const char *path, int byte)
{
    FILE *f = fopen(path, "ab");

    if (!f)
        exit(2);
    fputc(byte, f);
    fclose(f);
}

static void truncate_to(const char *path, long size)
{
    FILE *f = fopen(path, "r+b");

    if (!f)
        exit(2);
    fclose(f);
    if (truncate(path, (off_t)size) != 0)
        exit(2);
}

static int load_sign(void)
{
    racing_settings_t s;

    (void)storage_load_settings(&s); /* false: defaults are already in place */
    return s.sign_scale_pct;
}

static int save(int sign)
{
    racing_settings_t s;

    settings_defaults(&s);
    s.sign_scale_pct = sign;
    return storage_save_settings(&s) ? 1 : 0;
}

static void report(const char *name, int saved)
{
    printf("%-52s save=%-5s cfg=%-3s bak=%-3s new=%-3s load=%d\n", name,
           saved ? "true" : "false", path_exists(cfg_path) ? "yes" : "no",
           path_exists(bak_path) ? "yes" : "no",
           path_exists(new_path) ? "yes" : "no", load_sign());
}

/* The 13 sequences the 0.8.0-G reviews simulated, now asserted. */

static void test_happy_path(void)
{
    wipe();
    put_valid(cfg_path, 120);
    CHECK(save(130));
    report("0 happy path", 1);
    CHECK(load_sign() == 130);
    CHECK(!path_exists(bak_path));
    CHECK(!path_exists(new_path));
}

static void test_write_failures(void)
{
    wipe();
    put_valid(cfg_path, 120);
    psp_stub_fail_write_short = 1;
    CHECK(!save(130));
    psp_stub_fail_write_short = 0;
    report("1 write short", 0);
    CHECK(load_sign() == 120);

    wipe();
    put_valid(cfg_path, 120);
    psp_stub_fail_readback_corrupt = 1;
    CHECK(!save(130));
    psp_stub_fail_readback_corrupt = 0;
    report("2 read-back invalid", 0);
    CHECK(load_sign() == 120);
}

static void test_rotation_failures(void)
{
    /* The .BAK remove fails while a .BAK exists: the rotation cannot run. */
    wipe();
    put_valid(cfg_path, 120);
    put_valid(bak_path, 110);
    psp_stub_fail_remove_bak = 1;
    CHECK(!save(130));
    psp_stub_fail_remove_bak = 0;
    report("3 remove BAK fails (BAK present)", 0);
    CHECK(load_sign() == 120);

    /* First rename fails with no .BAK: nothing was moved yet. */
    wipe();
    put_valid(cfg_path, 120);
    psp_stub_fail_rename_cfg_to_bak = 1;
    CHECK(!save(130));
    psp_stub_fail_rename_cfg_to_bak = 0;
    report("4 rename CFG->BAK fails", 0);
    CHECK(load_sign() == 120);

    /* Second rename fails, the restore puts the old config back. */
    wipe();
    put_valid(cfg_path, 120);
    psp_stub_fail_rename_new_to_cfg = 1;
    CHECK(!save(130));
    psp_stub_fail_rename_new_to_cfg = 0;
    report("5 rename NEW->CFG fails, restore ok", 0);
    CHECK(load_sign() == 120);

    /* Both renames fail: the .BAK still holds the previous config. */
    wipe();
    put_valid(cfg_path, 120);
    psp_stub_fail_rename_new_to_cfg = 1;
    psp_stub_fail_rename_bak_restore = 1;
    CHECK(!save(130));
    psp_stub_fail_rename_new_to_cfg = 0;
    psp_stub_fail_rename_bak_restore = 0;
    report("6 both renames fail", 0);
    CHECK(load_sign() == 120);

    /* Success, but the post-success .BAK cleanup fails: still saved. */
    wipe();
    put_valid(cfg_path, 120);
    psp_stub_fail_remove_bak = 1;
    CHECK(save(130));
    psp_stub_fail_remove_bak = 0;
    report("7 remove BAK after success fails", 1);
    CHECK(load_sign() == 130);
}

static void test_first_save_failure(void)
{
    /* A device's very first save has no previous file to keep: a failed
     * rename leaves only .NEW, which the loader ignores. Documented. */
    wipe();
    psp_stub_fail_rename_new_to_cfg = 1;
    CHECK(!save(130));
    psp_stub_fail_rename_new_to_cfg = 0;
    report("8 first save, NEW->CFG fails", 0);
    CHECK(load_sign() == 100);
    CHECK(path_exists(new_path));
}

static void test_recovery_states(void)
{
    /* CFG gone, .BAK only (power loss after the rotation). */
    wipe();
    put_valid(bak_path, 120);
    report("9 CFG gone, BAK only (load)", 0);
    CHECK(load_sign() == 120);
    CHECK(save(130));
    report("9b save from BAK-only", 1);
    CHECK(load_sign() == 130);

    /* CFG corrupt, .BAK valid: the loader falls back. */
    wipe();
    put_valid(cfg_path, 120);
    corrupt_crc(cfg_path);
    put_valid(bak_path, 110);
    report("10 CFG corrupt, BAK 110 (load)", 0);
    CHECK(load_sign() == 110);

    /* v2 file with byte 9 = 200 and a valid CRC: rejected as a whole. */
    wipe();
    put_valid(cfg_path, 160);
    {
        FILE *f = fopen(cfg_path, "r+b");
        uint8_t buf[RACING_SETTINGS_FILE_SIZE];
        uint16_t crc;

        if (!f || fread(buf, 1, sizeof(buf), f) != sizeof(buf))
            exit(2);
        buf[9] = 200;
        crc = settings_crc16(buf, 10);
        buf[10] = (uint8_t)(crc & 0xFFu);
        buf[11] = (uint8_t)((crc >> 8) & 0xFFu);
        if (fseek(f, 0, SEEK_SET) != 0 ||
            fwrite(buf, 1, sizeof(buf), f) != sizeof(buf))
            exit(2);
        fclose(f);
    }
    report("13 v2 byte9=200 (load)", 0);
    CHECK(load_sign() == 100);
}

/* Reviewer 3 finding 2: the game read 16 of 17 bytes and accepted the file. */

static void test_exact_size(void)
{
    wipe();
    put_valid(cfg_path, 120);
    CHECK(load_sign() == 120); /* control */

    append_byte(cfg_path, 0xAA);
    CHECK(file_size(cfg_path) == RACING_SETTINGS_FILE_SIZE + 1);
    report("11 17-byte CFG (load)", 0);
    CHECK(load_sign() == 100);

    wipe();
    put_valid(cfg_path, 120);
    truncate_to(cfg_path, RACING_SETTINGS_FILE_SIZE - 1);
    report("12 15-byte CFG (load)", 0);
    CHECK(load_sign() == 100);
}

/*
 * Reviewer 3 finding 1. The state that only exists on the failure path: the
 * CFG does not parse, the .BAK is the last valid file, and the final rename
 * fails. Before the fix the save rotated the junk CFG over the good .BAK and
 * then restored the junk, so the next load returned defaults even though a
 * validated .NEW was still on the card.
 */
static void test_corrupt_cfg_is_not_a_backup(void)
{
    racing_settings_t s;

    wipe();
    put_valid(cfg_path, 120);
    corrupt_crc(cfg_path);
    put_valid(bak_path, 110); /* the only valid file */

    psp_stub_fail_rename_new_to_cfg = 1;
    CHECK(!save(130));
    psp_stub_fail_rename_new_to_cfg = 0;
    report("14 corrupt CFG + valid BAK, NEW->CFG fails", 0);
    CHECK(load_sign() == 110);      /* the .BAK's value, not defaults */
    CHECK(path_exists(bak_path));   /* the good file was not overwritten */
    CHECK(!path_exists(cfg_path));  /* the junk CFG is gone */
    CHECK(path_exists(new_path));   /* the validated write survives */

    /* The same seed with a rename that works: the save lands, and the next
     * write still rotates (the .BAK is a real backup, not junk). */
    wipe();
    put_valid(cfg_path, 120);
    corrupt_crc(cfg_path);
    put_valid(bak_path, 110);
    CHECK(save(130));
    report("14b corrupt CFG + valid BAK, save ok", 1);
    CHECK(load_sign() == 130);
    CHECK(!path_exists(bak_path));

    settings_defaults(&s);
    s.sign_scale_pct = 140;
    CHECK(storage_save_settings(&s));
    report("14c second save after 14b", 1);
    CHECK(load_sign() == 140);
}

int main(void)
{
    char tmpl[] = "/tmp/racing-storage-XXXXXX";

    if (!mkdtemp(tmpl)) {
        fprintf(stderr, "cannot create a temp directory\n");
        return 2;
    }
    snprintf(work_root, sizeof(work_root), "%s", tmpl);
    snprintf(cfg_path, sizeof(cfg_path),
             "%s/PSP/SAVEDATA/RACING3D/RACING.CFG", work_root);
    snprintf(bak_path, sizeof(bak_path),
             "%s/PSP/SAVEDATA/RACING3D/RACING.CFG.BAK", work_root);
    snprintf(new_path, sizeof(new_path),
             "%s/PSP/SAVEDATA/RACING3D/RACING.CFG.NEW", work_root);
    psp_stub_set_root(work_root);

    test_happy_path();
    test_write_failures();
    test_rotation_failures();
    test_first_save_failure();
    test_recovery_states();
    test_exact_size();
    test_corrupt_cfg_is_not_a_backup();

    wipe();
    (void)rmdir(work_root);

    if (failures) {
        fprintf(stderr, "%d storage test(s) failed\n", failures);
        return 1;
    }
    printf("all storage tests passed\n");
    return 0;
}
