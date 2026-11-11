/*
 * tests/host/psp/psprtc.h - test-only stand-in for the PSP RTC header.
 *
 * racing/storage.c links sceRtcGetCurrentClockLocalTime() through
 * storage_read_date(); the host test never calls it, but the object file
 * references it, so the stub has to exist.
 */

#ifndef PSPRTC_HOST_STUB_H
#define PSPRTC_HOST_STUB_H

typedef struct ScePspDateTimeHost {
    unsigned short year;
    unsigned short month;
    unsigned short day;
    unsigned short hour;
    unsigned short minute;
    unsigned short second;
    unsigned int microsecond;
} ScePspDateTime;

int sceRtcGetCurrentClockLocalTime(ScePspDateTime *t);

#endif /* PSPRTC_HOST_STUB_H */
