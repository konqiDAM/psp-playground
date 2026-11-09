/*
 * install_state.c - install registry, target path and planner.
 *
 * Uses CatalogEntry only as a type; it never calls catalog_* functions so the
 * host test links without catalog.c.
 */
#include "install_state.h"

#include <stdio.h>
#include <string.h>

#define INSTALL_LINE_MAX 256

static int ascii_lower(int c)
{
    if (c >= 'A' && c <= 'Z')
        return c + ('a' - 'A');
    return c;
}

static int ci_equal(const char *a, const char *b)
{
    if (!a || !b)
        return 0;
    while (*a && *b) {
        if (ascii_lower((unsigned char)*a) != ascii_lower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int is_hex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

static int version_valid(const char *v)
{
    const char *p = v;
    int i;

    if (!v)
        return 0;
    for (i = 0; i < 3; i++) {
        int digits = 0;
        while (*p >= '0' && *p <= '9' && digits < 4) {
            p++;
            digits++;
        }
        if (digits == 0)
            return 0;
        if (i < 2) {
            if (*p != '.')
                return 0;
            p++;
        }
    }
    return *p == '\0';
}

static int reserved_dos_name(const char *s)
{
    static const char *names[] = {
        "CON", "PRN", "AUX", "NUL",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
    };
    size_t i;

    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (ci_equal(s, names[i]))
            return 1;
    }
    return 0;
}

static int folder_valid(const char *s)
{
    size_t i, n;

    if (!s)
        return 0;
    n = strlen(s);
    if (n < 1 || n > 31)
        return 0;
    if (strcmp(s, ".") == 0 || strcmp(s, "..") == 0)
        return 0;
    if (reserved_dos_name(s))
        return 0;
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return 0;
    }
    return 1;
}

static int installed_path_valid(const char *s)
{
    size_t n;

    if (!s)
        return 0;
    n = strlen(s);
    if (n < strlen("ms0:/PSP/GAME/HOMEBREW/a/EBOOT.PBP") ||
        n >= INSTALLED_PATH_MAX)
        return 0;
    if (strncmp(s, INSTALL_ROOT "/", strlen(INSTALL_ROOT) + 1) != 0)
        return 0;
    if (n < 10 || strcmp(s + n - 10, "/EBOOT.PBP") != 0)
        return 0;
    if (strstr(s, "..") || strchr(s, '\\') || strchr(s, '?') ||
        strchr(s, '#') || strchr(s, '%'))
        return 0;
    return 1;
}

static int size_valid(const char *s, uint32_t *out)
{
    uint64_t v = 0;
    size_t i, n;

    if (!s)
        return 0;
    n = strlen(s);
    if (n < 1 || n > 10)
        return 0;
    for (i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9')
            return 0;
        v = v * 10u + (uint64_t)(s[i] - '0');
        if (v > CATALOG_MAX_FILE_BYTES)
            return 0;
    }
    if (v < 1)
        return 0;
    *out = (uint32_t)v;
    return 1;
}

static int sha_valid(const char *s, char out[65])
{
    size_t i;

    if (!s || strlen(s) != 64)
        return 0;
    for (i = 0; i < 64; i++) {
        if (!is_hex(s[i]))
            return 0;
        out[i] = (char)ascii_lower((unsigned char)s[i]);
    }
    out[64] = '\0';
    return 1;
}

int install_target_path(const CatalogEntry *entry, char *out, int size)
{
    int n;

    if (!entry || !out || size <= 0)
        return -1;
    if (!folder_valid(entry->folder))
        return -1;
    n = snprintf(out, (size_t)size, "%s/%s/EBOOT.PBP",
                 INSTALL_ROOT, entry->folder);
    if (n < 0 || n >= size || n >= INSTALLED_PATH_MAX)
        return -1;
    return 0;
}

const char *install_plan_name(PlanKind plan)
{
    switch (plan) {
    case PLAN_INSTALL:    return "INSTALL";
    case PLAN_UPDATE:     return "UPDATE";
    case PLAN_INSTALLED:  return "INSTALLED";
    case PLAN_UNVERIFIED: return "INSTALLED?";
    case PLAN_SELF:       return "SELF";
    case PLAN_BROKEN:     return "REINSTALL";
    case PLAN_NO_SPACE:   return "NO SPACE";
    default:              return "?";
    }
}

int installed_find(const InstallRegistry *reg, const char *id)
{
    int i;

    if (!reg || !id)
        return -1;
    for (i = 0; i < reg->count; i++) {
        if (ci_equal(reg->records[i].id, id))
            return i;
    }
    return -1;
}

int installed_parse(const char *text, int len, InstallRegistry *out)
{
    InstallRecord cur;
    int mask = 0;
    int header_seen = 0;
    int pos = 0;

    if (!out)
        return -1;
    memset(out, 0, sizeof(*out));
    if (!text || len < 0 || len > INSTALLED_MAX_BYTES)
        return -1;

    memset(&cur, 0, sizeof(cur));

    while (pos < len) {
        int start = pos;
        int end;
        int line_len;
        char line[INSTALL_LINE_MAX + 1];

        while (pos < len && text[pos] != '\n')
            pos++;
        end = pos;
        if (pos < len)
            pos++;
        line_len = end - start;
        if (line_len > 0 && text[end - 1] == '\r')
            line_len--;
        if (line_len > INSTALL_LINE_MAX)
            return -1;
        memcpy(line, text + start, (size_t)line_len);
        line[line_len] = '\0';

        if (line_len == 0 || line[0] == '#')
            continue;
        if (!header_seen) {
            if (strcmp(line, "PSPWIFI-INSTALLED 1") != 0)
                return -1;
            header_seen = 1;
            continue;
        }
        if (strcmp(line, "--") == 0) {
            if (mask != 0x1f)
                return -1;
            if (out->count >= INSTALLED_MAX_RECORDS)
                return -1;
            if (installed_find(out, cur.id) >= 0)
                return -1;
            out->records[out->count++] = cur;
            memset(&cur, 0, sizeof(cur));
            mask = 0;
            continue;
        }
        {
            char *eq = strchr(line, '=');
            char *value;
            int key_len;

            if (!eq)
                return -1;
            key_len = (int)(eq - line);
            value = eq + 1;
            if (key_len == 2 && strncmp(line, "id", 2) == 0) {
                if (mask & 1 || strlen(value) > 31 || !folder_valid(value))
                    return -1;
                snprintf(cur.id, sizeof(cur.id), "%s", value);
                mask |= 1;
            } else if (key_len == 7 && strncmp(line, "version", 7) == 0) {
                if (mask & 2 || strlen(value) > 15 || !version_valid(value))
                    return -1;
                snprintf(cur.version, sizeof(cur.version), "%s", value);
                mask |= 2;
            } else if (key_len == 4 && strncmp(line, "size", 4) == 0) {
                if (mask & 4 || !size_valid(value, &cur.size))
                    return -1;
                mask |= 4;
            } else if (key_len == 6 && strncmp(line, "sha256", 6) == 0) {
                if (mask & 8 || !sha_valid(value, cur.sha256))
                    return -1;
                mask |= 8;
            } else if (key_len == 4 && strncmp(line, "path", 4) == 0) {
                if (mask & 16 || strlen(value) >= INSTALLED_PATH_MAX ||
                    !installed_path_valid(value))
                    return -1;
                snprintf(cur.path, sizeof(cur.path), "%s", value);
                mask |= 16;
            } else {
                return -1;
            }
        }
    }

    if (!header_seen)
        return -1;
    if (mask != 0) {
        if (mask != 0x1f || out->count >= INSTALLED_MAX_RECORDS)
            return -1;
        if (installed_find(out, cur.id) >= 0)
            return -1;
        out->records[out->count++] = cur;
    }
    return 0;
}

int installed_render(const InstallRegistry *reg, char *out, int size)
{
    int off = 0;
    int i;
    int n;

    if (!reg || !out || size <= 0)
        return -1;
    n = snprintf(out, (size_t)size, "PSPWIFI-INSTALLED 1\n");
    if (n < 0 || n >= size)
        return -1;
    off = n;
    for (i = 0; i < reg->count; i++) {
        const InstallRecord *r = &reg->records[i];
        n = snprintf(out + off, (size_t)(size - off),
                     "id=%s\nversion=%s\nsize=%u\nsha256=%s\npath=%s\n--\n",
                     r->id, r->version, (unsigned)r->size, r->sha256, r->path);
        if (n < 0 || n >= size - off)
            return -1;
        off += n;
    }
    return off;
}

uint32_t install_free_or_unknown(int rc, uint32_t free_bytes)
{
    return rc == 0 ? free_bytes : UINT32_MAX;
}

int install_bad_slot(const uint8_t *used, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        if (!used || !used[i])
            return i;
    }
    return -1; /* full: do not drop an archived registry */
}

PlanKind install_plan(const CatalogEntry *entry, const InstallRegistry *reg,
                      int target_exists, uint32_t target_size,
                      uint32_t free_bytes, const char *self_id)
{
    int idx;
    PlanKind plan;

    if (!entry)
        return PLAN_BROKEN;
    if (self_id && ci_equal(entry->id, self_id))
        return PLAN_SELF;

    idx = installed_find(reg, entry->id);
    if (idx >= 0) {
        const InstallRecord *r = &reg->records[idx];
        if (!target_exists || target_size != entry->size) {
            plan = PLAN_BROKEN;
        } else if (strcmp(r->version, entry->version) == 0 &&
                   ci_equal(r->sha256, entry->sha256)) {
            plan = PLAN_INSTALLED;
        } else {
            plan = PLAN_UPDATE;
        }
    } else if (!target_exists) {
        plan = PLAN_INSTALL;
    } else if (target_size == entry->size) {
        plan = PLAN_UNVERIFIED;
    } else {
        plan = PLAN_UPDATE;
    }

    if (plan == PLAN_INSTALL || plan == PLAN_UPDATE ||
        plan == PLAN_UNVERIFIED || plan == PLAN_BROKEN) {
        if (free_bytes != UINT32_MAX &&
            free_bytes < entry->size + INSTALL_MARGIN_BYTES)
            return PLAN_NO_SPACE;
    }
    return plan;
}
