/*
 * hiscore.c - portable Racing 3D per-map leaderboard model.
 *
 * Ranking: distance descending, ties keep the entry that was already stored.
 * The table is always kept sorted descending, and every entry is memset before
 * it is filled so the struct padding is deterministic for memcmp tests.
 */

#include "hiscore.h"

#include <stdio.h>
#include <string.h>

#include "settings.h"

static uint8_t clamp_u8(int value, int low, int high)
{
    if (value < low)
        return (uint8_t)low;
    if (value > high)
        return (uint8_t)high;
    return (uint8_t)value;
}

void hiscore_clear(hiscore_book_t *book)
{
    if (!book)
        return;
    memset(book, 0, sizeof(*book));
}

bool hiscore_entry_valid(const hiscore_entry_t *e)
{
    if (!e)
        return false;
    if (e->map >= HISCORE_TABLE_COUNT)
        return false;
    /* A run that never moved forward scores 0 dm: it must not take rank 1
     * of an empty table, so 0 is out of range, not a valid score. */
    if (e->distance_dm == 0 || e->distance_dm > HISCORE_MAX_DISTANCE_DM)
        return false;
    if (e->elevation_pct < HISCORE_PCT_MIN ||
        e->elevation_pct > HISCORE_PCT_MAX)
        return false;
    if (e->hill_pct < HISCORE_PCT_MIN || e->hill_pct > HISCORE_PCT_MAX)
        return false;
    if (e->width_pct < HISCORE_PCT_MIN || e->width_pct > HISCORE_PCT_MAX)
        return false;
    if (e->one_lane_dm < HISCORE_ONE_LANE_DM_MIN ||
        e->one_lane_dm > HISCORE_ONE_LANE_DM_MAX)
        return false;
    if (e->year_offset == 0) {
        /* Unknown date: month and day must be unknown too. */
        if (e->month != 0 || e->day != 0)
            return false;
    } else {
        if (e->year_offset < HISCORE_YEAR_OFFSET_MIN ||
            e->year_offset > HISCORE_YEAR_OFFSET_MAX)
            return false;
        if (e->month < 1 || e->month > 12)
            return false;
        if (e->day < 1 || e->day > 31)
            return false;
    }
    return true;
}

void hiscore_entry_from_run(hiscore_entry_t *e, float distance_m,
                            uint32_t seed, const road_config_t *cfg, int year,
                            int month, int day)
{
    int dm;

    if (!e)
        return;
    memset(e, 0, sizeof(*e));
    if (!cfg)
        return;

    dm = (int)(distance_m * 10.0f + 0.5f);
    if (dm < 0)
        dm = 0;
    if (dm > HISCORE_MAX_DISTANCE_DM)
        dm = HISCORE_MAX_DISTANCE_DM;

    e->distance_dm = (uint16_t)dm;
    e->seed = seed;
    e->map = (uint8_t)cfg->map;
    e->elevation_pct = clamp_u8((int)(cfg->elevation_pct + 0.5f),
                                HISCORE_PCT_MIN, HISCORE_PCT_MAX);
    e->hill_pct = clamp_u8((int)(cfg->hill_pct + 0.5f), HISCORE_PCT_MIN,
                           HISCORE_PCT_MAX);
    e->width_pct = clamp_u8((int)(cfg->width_pct + 0.5f), HISCORE_PCT_MIN,
                            HISCORE_PCT_MAX);
    e->one_lane_dm = clamp_u8((int)(cfg->one_lane_width * 10.0f + 0.5f),
                              HISCORE_ONE_LANE_DM_MIN,
                              HISCORE_ONE_LANE_DM_MAX);

    if (year >= 2000 && year <= 2200 && month >= 1 && month <= 12 &&
        day >= 1 && day <= 31) {
        e->year_offset = (uint8_t)(year - 1999);
        e->month = (uint8_t)month;
        e->day = (uint8_t)day;
    }
}

int hiscore_rank(const hiscore_table_t *t, uint16_t distance_dm)
{
    int count;

    if (!t)
        return 0;
    count = 1;
    for (int i = 0; i < (int)t->count; i++) {
        if (t->entry[i].distance_dm >= distance_dm)
            count++;
    }
    return (count > HISCORE_TOP_N) ? 0 : count;
}

int hiscore_insert(hiscore_table_t *t, const hiscore_entry_t *e)
{
    int rank;
    int index;
    int last;

    if (!t || !e || !hiscore_entry_valid(e))
        return 0;

    rank = hiscore_rank(t, e->distance_dm);
    if (rank == 0)
        return 0;

    index = rank - 1;
    last = (t->count < HISCORE_TOP_N) ? (int)t->count : HISCORE_TOP_N - 1;
    for (int i = last; i > index; i--)
        t->entry[i] = t->entry[i - 1];
    t->entry[index] = *e;
    if (t->count < HISCORE_TOP_N)
        t->count++;
    return rank;
}

const char *hiscore_map_name(int map)
{
    switch (map) {
    case ROAD_MAP_DEFAULT:
        return "DEFAULT";
    case ROAD_MAP_UPHILL:
        return "UPHILL";
    case ROAD_MAP_HIGHWAY:
        return "HIGHWAY";
    default:
        return "?";
    }
}

/* --------------------------------------------------------------- format */

static int table_offset(int map)
{
    return 16 + map * HISCORE_TOP_N * HISCORE_ENTRY_SIZE;
}

static void write_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void write_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint16_t read_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static bool entry_matches_table(const hiscore_entry_t *e, int map)
{
    return hiscore_entry_valid(e) && (int)e->map == map;
}

/* True when all 16 bytes of a slot are zero (unused slot). */
static bool slot_is_zero(const uint8_t *p)
{
    for (int i = 0; i < HISCORE_ENTRY_SIZE; i++) {
        if (p[i] != 0)
            return false;
    }
    return true;
}

static void encode_entry(uint8_t *p, const hiscore_entry_t *e)
{
    write_u16(p, e->distance_dm);
    write_u32(p + 2, e->seed);
    p[6] = e->map;
    p[7] = e->elevation_pct;
    p[8] = e->hill_pct;
    p[9] = e->width_pct;
    p[10] = e->one_lane_dm;
    p[11] = e->year_offset;
    p[12] = e->month;
    p[13] = e->day;
    p[14] = 0;
    p[15] = 0;
}

static void decode_entry(hiscore_entry_t *e, const uint8_t *p)
{
    memset(e, 0, sizeof(*e));
    e->distance_dm = read_u16(p);
    e->seed = read_u32(p + 2);
    e->map = p[6];
    e->elevation_pct = p[7];
    e->hill_pct = p[8];
    e->width_pct = p[9];
    e->one_lane_dm = p[10];
    e->year_offset = p[11];
    e->month = p[12];
    e->day = p[13];
}

int hiscore_serialize(const hiscore_book_t *book, uint8_t *buf, int cap)
{
    uint16_t crc;

    if (!book || !buf || cap < HISCORE_FILE_SIZE)
        return -1;

    for (int map = 0; map < HISCORE_TABLE_COUNT; map++) {
        const hiscore_table_t *t = &book->table[map];

        if (t->count > HISCORE_TOP_N)
            return -1;
        for (int i = 0; i < (int)t->count; i++) {
            if (!entry_matches_table(&t->entry[i], map))
                return -1;
        }
    }

    memset(buf, 0, HISCORE_FILE_SIZE);
    memcpy(buf, "R3DH", 4);
    buf[4] = HISCORE_VERSION;
    buf[5] = 0;
    buf[6] = HISCORE_TABLE_COUNT;
    buf[7] = 0;
    for (int map = 0; map < HISCORE_TABLE_COUNT; map++) {
        const hiscore_table_t *t = &book->table[map];
        int base = table_offset(map);

        buf[8 + map] = t->count;
        for (int i = 0; i < (int)t->count; i++)
            encode_entry(buf + base + i * HISCORE_ENTRY_SIZE, &t->entry[i]);
    }

    crc = settings_crc16(buf, HISCORE_FILE_SIZE - 2);
    write_u16(buf + HISCORE_FILE_SIZE - 2, crc);
    return HISCORE_FILE_SIZE;
}

/* Stable insertion sort on distance only: equal runs keep their file order. */
static void sort_table(hiscore_table_t *t)
{
    for (int i = 1; i < (int)t->count; i++) {
        hiscore_entry_t key = t->entry[i];
        int j = i - 1;

        while (j >= 0 && t->entry[j].distance_dm < key.distance_dm) {
            t->entry[j + 1] = t->entry[j];
            j--;
        }
        t->entry[j + 1] = key;
    }
}

bool hiscore_deserialize(hiscore_book_t *book, const uint8_t *buf, int len)
{
    uint16_t stored;
    int map;

    if (!book)
        return false;
    hiscore_clear(book);
    if (!buf || len != HISCORE_FILE_SIZE)
        return false;
    if (memcmp(buf, "R3DH", 4) != 0)
        return false;
    if (buf[4] != HISCORE_VERSION)
        return false;
    if (buf[5] != 0 || buf[6] != HISCORE_TABLE_COUNT || buf[7] != 0)
        return false;
    for (int i = 11; i < 16; i++) {
        if (buf[i] != 0)
            return false;
    }
    for (int i = 496; i < HISCORE_FILE_SIZE - 2; i++) {
        if (buf[i] != 0)
            return false;
    }

    stored = read_u16(buf + HISCORE_FILE_SIZE - 2);
    if (stored != settings_crc16(buf, HISCORE_FILE_SIZE - 2))
        return false;

    for (map = 0; map < HISCORE_TABLE_COUNT; map++) {
        hiscore_table_t *t = &book->table[map];
        int base = table_offset(map);

        if (buf[8 + map] > HISCORE_TOP_N)
            return false;
        for (int i = 0; i < HISCORE_TOP_N; i++) {
            const uint8_t *slot = buf + base + i * HISCORE_ENTRY_SIZE;

            if (i >= (int)buf[8 + map]) {
                if (!slot_is_zero(slot))
                    return false;
                continue;
            }
            if (slot[14] != 0 || slot[15] != 0)
                return false; /* reserved bytes inside a used entry */
            decode_entry(&t->entry[i], slot);
            if (!entry_matches_table(&t->entry[i], map))
                return false;
        }
        t->count = buf[8 + map];
        sort_table(t);
    }
    return true;
}

int hiscore_column_x(int column)
{
    switch (column) {
    case HISCORE_COLUMN_RANK:
        return 150;
    case HISCORE_COLUMN_DIST:
        return 166;
    case HISCORE_COLUMN_SEED:
        return 230;
    case HISCORE_COLUMN_DATE:
        return 300;
    default:
        return 150;
    }
}

void hiscore_format_date(char *buf, const hiscore_entry_t *e)
{
    if (!buf)
        return;
    if (!e || e->year_offset == 0) {
        strcpy(buf, "--");
        return;
    }
    /* year_offset is 1-based (1 = 2000), so show offset - 1: 27 -> "26". */
    snprintf(buf, 12, "%02d-%02d-%02d", (int)e->year_offset - 1,
             (int)e->month, (int)e->day);
}

const char *hiscore_result_status(const hiscore_result_t *r, bool save_failed)
{
    if (!r || r->rank <= 0)
        return "NOT RANKED";
    return save_failed ? "NOT SAVED" : "SAVED";
}

int hiscore_commit(hiscore_book_t *book, const hiscore_entry_t *e,
                   bool *needs_save)
{
    int rank;

    if (needs_save)
        *needs_save = false;
    if (!book || !e || !hiscore_entry_valid(e))
        return 0;
    if (e->map >= HISCORE_TABLE_COUNT)
        return 0;
    rank = hiscore_insert(&book->table[e->map], e);
    if (needs_save)
        *needs_save = rank > 0;
    return rank;
}

bool hiscore_dirty_after(bool dirty, bool attempted, bool save_ok)
{
    if (!attempted)
        return dirty; /* nothing written: keep the earlier warning */
    return !save_ok;
}
