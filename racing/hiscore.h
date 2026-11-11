/*
 * hiscore.h - portable Racing 3D per-map leaderboard and RACING.HOF format.
 *
 * No PSP headers here: the ranking, the validation and the 512-byte layout are
 * host-tested. storage.c is the only place that touches the memory stick.
 *
 * Layout (512 bytes, little-endian, CRC-16/CCITT-FALSE over bytes 0..509 at
 * 510..511):
 *   0..3   "R3DH"
 *   4      version = 1
 *   5      flags = 0
 *   6      table_count = 3
 *   7      reserved = 0
 *   8..10  count[0..2] per map, each 0..10
 *   11..15 reserved = 0
 *   16..175  ROAD_MAP_DEFAULT table   (10 x 16-byte entries)
 *   176..335 ROAD_MAP_UPHILL table
 *   336..495 ROAD_MAP_HIGHWAY table
 *   496..509 reserved = 0
 *   510..511 CRC-16/CCITT-FALSE
 *
 * Entry (16 bytes):
 *   0..1   distance_dm (u16 LE, decimetres)
 *   2..5   seed (u32 LE)
 *   6      map
 *   7      elevation_pct
 *   8      hill_pct
 *   9      width_pct
 *   10     one_lane_dm (total one-lane width x10)
 *   11     year_offset (year - 1999: 1 = 2000 .. 201 = 2200, 0 = unknown)
 *   12     month (0 = unknown)
 *   13     day   (0 = unknown)
 *   14..15 reserved = 0
 */

#ifndef RACING_HISCORE_H
#define RACING_HISCORE_H

#include <stdbool.h>
#include <stdint.h>

#include "road.h"

#define HISCORE_TOP_N 10
#define HISCORE_ENTRY_SIZE 16
#define HISCORE_FILE_SIZE 512
#define HISCORE_VERSION 1
#define HISCORE_TABLE_COUNT ROAD_MAP_COUNT

/* 3200.0 m; a 60 s run peaks at 3148.5 m = 31485 dm. */
#define HISCORE_MAX_DISTANCE_DM 32000
#define HISCORE_YEAR_OFFSET_MIN 1  /* 1 => 2000 */
#define HISCORE_YEAR_OFFSET_MAX 201 /* 201 => 2200 */
#define HISCORE_PCT_MIN 50
#define HISCORE_PCT_MAX 150
#define HISCORE_ONE_LANE_DM_MIN 40
#define HISCORE_ONE_LANE_DM_MAX 60

/*
 * 0.9.0: only a run on the DEFAULT TERRAIN is comparable. The four
 * session-only sliders (elevation, hills, width, one-lane) disqualify; every
 * other choice in the game - steering mode, sensitivity, sign size, HUD
 * toggles, analog centre/deadzone - is not even a parameter, which is the
 * point: nothing but terrain can disqualify a run. map, rail_enabled and
 * rail_density are ignored (per-map tables; not player-adjustable).
 *
 * PCT_EPS is 5 % of the 10 pct slider step: slider values are exact multiples
 * of 10, so it only absorbs float noise, never a real step. ONE_LANE_EPS is
 * half a 0.1 m step.
 */
#define HISCORE_TERRAIN_PCT_EPS 0.5f
#define HISCORE_TERRAIN_ONE_LANE_EPS 0.05f

bool hiscore_run_eligible(const road_config_t *terrain);

typedef struct {
    uint16_t distance_dm;
    uint32_t seed;
    uint8_t map;
    uint8_t elevation_pct;
    uint8_t hill_pct;
    uint8_t width_pct;
    uint8_t one_lane_dm;
    uint8_t year_offset;
    uint8_t month;
    uint8_t day;
} hiscore_entry_t;

typedef struct {
    uint8_t count;
    hiscore_entry_t entry[HISCORE_TOP_N];
} hiscore_table_t;

typedef struct {
    hiscore_table_t table[HISCORE_TABLE_COUNT];
} hiscore_book_t;

typedef struct {
    hiscore_entry_t entry;
    int rank;      /* 1..HISCORE_TOP_N, or 0 = not ranked */
    bool eligible; /* false = non-default terrain: never inserted */
} hiscore_result_t;

/* Empty every table. Always memset, so entry padding is zero. */
void hiscore_clear(hiscore_book_t *book);

/* Field ranges only (no table context): every caller can use it. */
bool hiscore_entry_valid(const hiscore_entry_t *e);

/*
 * Build an entry from a finished run. Clamps the distance to
 * HISCORE_MAX_DISTANCE_DM; a 0 year/month/day means "date unknown".
 * A run that never moved forward keeps distance_dm == 0, which
 * hiscore_entry_valid() rejects, so it never ranks and is never written.
 */
void hiscore_entry_from_run(hiscore_entry_t *e, float distance_m,
                            uint32_t seed, const road_config_t *cfg,
                            int year, int month, int day);

/* 1 + number of stored entries with distance_dm >= distance_dm, else 0. */
int hiscore_rank(const hiscore_table_t *t, uint16_t distance_dm);

/*
 * Insert if the run ranks in the top HISCORE_TOP_N. Returns 1..N (the rank it
 * took) or 0 when the table is unchanged: invalid entry (no clamping), or the
 * run does not make the table. Ties keep the older entry ahead.
 */
int hiscore_insert(hiscore_table_t *t, const hiscore_entry_t *e);

const char *hiscore_map_name(int map);

/* Fixed leaderboard columns (px, scale 1). Header and rows share these. */
typedef enum {
    HISCORE_COLUMN_RANK = 0,
    HISCORE_COLUMN_DIST,
    HISCORE_COLUMN_SEED,
    HISCORE_COLUMN_DATE,
    HISCORE_COLUMN_COUNT
} hiscore_column_t;

int hiscore_column_x(int column);

/*
 * "26-11-10" from year_offset 27 (2026); "--" when the date is unknown.
 * `buf` must hold at least 12 bytes (the call is snprintf(buf, 12, ...)).
 */
void hiscore_format_date(char *buf, const hiscore_entry_t *e);

/*
 * The result card's status line: "NOT RANKED" when the run did not make the
 * table (nothing was written), else "SAVED"/"NOT SAVED".
 */
const char *hiscore_result_status(const hiscore_result_t *r, bool save_failed);

/*
 * The one commit path: ALWAYS fill out->entry with hiscore_entry_from_run()
 * (the result card prints MAP / SEED / DIST from it, so a refused run must
 * still identify itself), then insert only when the terrain is default. An
 * ineligible run is never inserted and *needs_save stays false, so the file
 * cannot change. Returns the rank (0 when not ranked).
 */
int hiscore_commit_run(hiscore_book_t *book, hiscore_result_t *out,
                       float distance_m, uint32_t seed,
                       const road_config_t *terrain, int year, int month,
                       int day, bool *needs_save);

/* "CUSTOM TERRAIN" when !out->eligible, else NULL (the card's reason line). */
const char *hiscore_ineligible_reason(const hiscore_result_t *out);

/*
 * Insert a finished run and report whether it needs a disk write. `needs_save`
 * is always written (false when the run did not make the table), so a stale
 * flag from an earlier run can never leak into the next result card.
 */
int hiscore_commit(hiscore_book_t *book, const hiscore_entry_t *e,
                   bool *needs_save);

/*
 * The "unsaved runs" warning must survive runs that never attempted a write:
 * a failed save marks the book dirty, a successful save clears it, and a run
 * that did not need a save keeps the previous state.
 */
bool hiscore_dirty_after(bool dirty, bool attempted, bool save_ok);

/* Returns HISCORE_FILE_SIZE, or -1 when the book or buffer is unusable. */
int hiscore_serialize(const hiscore_book_t *book, uint8_t *buf, int cap);

/*
 * Parses and validates the whole file; on any failure (or a NULL book) the
 * book is cleared and false is returned. Out-of-order entries are re-sorted
 * with a stable insertion sort, so equal distances keep their file order.
 */
bool hiscore_deserialize(hiscore_book_t *book, const uint8_t *buf, int len);

#endif /* RACING_HISCORE_H */
