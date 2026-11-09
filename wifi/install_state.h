/*
 * install_state.h - install registry, safe target path and planner.
 */
#ifndef PSP_WIFI_INSTALL_STATE_H
#define PSP_WIFI_INSTALL_STATE_H

#include <stdint.h>

#include "catalog.h"

#ifdef __cplusplus
extern "C" {
#endif

#define INSTALLED_MAX_RECORDS 16
#define INSTALLED_MAX_BYTES 4096
#define INSTALLED_PATH_MAX 176
#define INSTALL_ROOT "ms0:/PSP/GAME/HOMEBREW"
#define INSTALL_MARGIN_BYTES (8u * 1024u * 1024u)
#define WIFI_SELF_ID "wifi"

typedef struct {
    char id[CATALOG_ID_MAX];
    char version[CATALOG_VERSION_MAX];
    uint32_t size;
    char sha256[65];
    char path[INSTALLED_PATH_MAX];
} InstallRecord;

typedef struct {
    InstallRecord records[INSTALLED_MAX_RECORDS];
    int count;
} InstallRegistry;

typedef enum {
    PLAN_INSTALL = 0,
    PLAN_UPDATE,
    PLAN_INSTALLED,
    PLAN_UNVERIFIED,
    PLAN_SELF,
    PLAN_BROKEN,
    PLAN_NO_SPACE
} PlanKind;

int install_target_path(const CatalogEntry *entry, char *out, int size);
const char *install_plan_name(PlanKind plan);
uint32_t install_free_or_unknown(int rc, uint32_t free_bytes);
/* Pick the first free INSTALLED.BAD.<stamp>-NN slot; -1 when all are used
 * (the caller aborts rather than dropping an archived registry). */
int install_bad_slot(const uint8_t *used, int count);

int installed_parse(const char *text, int len, InstallRegistry *out);
int installed_render(const InstallRegistry *reg, char *out, int size);
int installed_find(const InstallRegistry *reg, const char *id);

PlanKind install_plan(const CatalogEntry *entry, const InstallRegistry *reg,
                      int target_exists, uint32_t target_size,
                      uint32_t free_bytes, const char *self_id);

#ifdef __cplusplus
}
#endif

#endif /* PSP_WIFI_INSTALL_STATE_H */
