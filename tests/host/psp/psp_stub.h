/*
 * tests/host/psp/psp_stub.h - control surface for the test-only ms0: shim.
 *
 * The shim answers the sceIo* calls racing/storage.c makes and can fail any
 * one of them on demand. That is the point: the save state machine only
 * differs from the happy path on failure, and PPSSPP cannot be told to fail a
 * rename that would otherwise succeed (it already refuses to replace an
 * existing target - that is the .BAK-remove case - but nothing makes it fail
 * with an empty target).
 */

#ifndef PSP_STUB_H
#define PSP_STUB_H

/* Root directory that ms0:/ maps onto. Created by the caller. */
void psp_stub_set_root(const char *root);

/* Clears every failure flag below. */
void psp_stub_reset(void);

/* Injection flags, all 0 (off) after psp_stub_reset(). */
extern int psp_stub_fail_write_short;       /* sceIoWrite writes 4 bytes      */
extern int psp_stub_fail_readback_corrupt;  /* sceIoWrite flips the last byte */
extern int psp_stub_fail_remove_bak;        /* sceIoRemove("*.BAK") fails     */
extern int psp_stub_fail_rename_cfg_to_bak; /* rename(RACING.CFG -> .BAK)     */
extern int psp_stub_fail_rename_new_to_cfg; /* rename(*.NEW -> base)          */
extern int psp_stub_fail_rename_bak_restore;/* rename(*.BAK -> base)          */

#endif /* PSP_STUB_H */
