/*
 * download_state.h - checksummed .resume state for a resumable transfer.
 */
#ifndef PSP_WIFI_DOWNLOAD_STATE_H
#define PSP_WIFI_DOWNLOAD_STATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DL_RESUME_MAX_BYTES 512

typedef struct {
    uint32_t size;
    char sha256[65];
    char etag[80];
    char last_modified[40];
    uint32_t offset;
} DlResume;

typedef enum {
    DL_RESUME_OK = 0,
    DL_RESUME_MISSING,
    DL_RESUME_BAD,
    DL_RESUME_MISMATCH,
    DL_RESUME_COMPLETE
} DlResumeResult;

int dl_resume_parse(const char *text, int len, DlResume *out);
int dl_resume_render(const DlResume *in, char *out, int size);
DlResumeResult dl_resume_usable(const DlResume *r, uint32_t expected_size,
                                const char *expected_sha, uint32_t part_size);
int dl_resume_has_validator(const DlResume *r);

#ifdef __cplusplus
}
#endif

#endif /* PSP_WIFI_DOWNLOAD_STATE_H */
