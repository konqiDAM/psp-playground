/*
 * Host tests for the portable Racing 3D score and leaderboard modules:
 * the Time Attack timer/score (score.c) and the per-map leaderboard
 * including its 512-byte RACING.HOF format (hiscore.c).
 *
 * Runs with a normal C99 compiler; no PSP SDK needed.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hiscore.h"
#include "road.h"
#include "score.h"
#include "settings.h"

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static int nearly(float a, float b, float epsilon)
{
    return fabsf(a - b) <= epsilon;
}

/* -------------------------------------------------------------------- score */

static void test_score_release_clock_is_60(void)
{
    CHECK(nearly(SCORE_RUN_SECONDS, 60.0f, 1e-6f));
}

static void test_score_starts_on_throttle_only(void)
{
    score_run_t r;

    score_reset(&r, false);
    CHECK(r.state == SCORE_STATE_IDLE);
    CHECK(nearly(score_remaining(&r), 60.0f, 1e-4f));
    CHECK(nearly(r.distance_m, 0.0f, 1e-6f));

    score_step(&r, false, 10.0f, 0.05f); /* coast: no clock, no score */
    CHECK(r.state == SCORE_STATE_IDLE);
    CHECK(nearly(r.distance_m, 0.0f, 1e-6f));
    CHECK(nearly(score_remaining(&r), 60.0f, 1e-4f));

    score_step(&r, false, 10.0f, 0.0f); /* zero-length frame is a no-op */
    CHECK(r.state == SCORE_STATE_IDLE);

    score_step(&r, true, 2.5f, 0.05f); /* throttle edge: same frame counts */
    CHECK(r.state == SCORE_STATE_RUNNING);
    CHECK(nearly(r.distance_m, 2.5f, 1e-4f));
    CHECK(nearly(score_remaining(&r), 59.95f, 1e-3f));
}

static void test_score_counts_forward_only(void)
{
    score_run_t r;

    score_reset(&r, false);
    score_step(&r, true, 10.0f, 0.05f);
    score_step(&r, true, -4.0f, 0.05f); /* reverse adds nothing */
    CHECK(nearly(r.distance_m, 10.0f, 1e-4f));
    score_step(&r, true, 0.0f, 0.05f); /* stopped adds nothing */
    CHECK(nearly(r.distance_m, 10.0f, 1e-4f));
    score_step(&r, true, 1.5f, 0.0f); /* dt = 0 never advances */
    CHECK(nearly(r.distance_m, 10.0f, 1e-4f));
    score_step(&r, true, 0.5f, 0.05f);
    CHECK(nearly(r.distance_m, 10.5f, 1e-4f));
}

static void test_score_finishes_once(void)
{
    score_run_t r;
    int i;

    score_reset(&r, false);
    score_step(&r, true, 0.5f, 1.0f / 60.0f);
    for (i = 0; i < 3599; i++) /* 3600 steps total */
        score_step(&r, true, 0.5f, 1.0f / 60.0f);
    CHECK(r.state == SCORE_STATE_RUNNING); /* at most one frame late */
    CHECK(score_remaining(&r) > 0.0f);

    score_step(&r, true, 0.5f, 1.0f / 60.0f); /* frame 3601 */
    CHECK(r.state == SCORE_STATE_FINISHED);
    CHECK(score_finished(&r));
    CHECK(nearly(score_remaining(&r), 0.0f, 1e-6f));
    CHECK(nearly(r.distance_m, 1800.5f, 1e-2f));

    score_step(&r, true, 99.0f, 0.05f); /* latched: no more score */
    CHECK(nearly(r.distance_m, 1800.5f, 1e-2f));
    CHECK(nearly(score_remaining(&r), 0.0f, 1e-6f));
}

static void test_score_free_drive_is_inert(void)
{
    score_run_t r;

    score_reset(&r, true);
    CHECK(r.state == SCORE_STATE_FREE);
    score_step(&r, true, 12.0f, 0.05f);
    CHECK(r.state == SCORE_STATE_FREE);
    CHECK(nearly(r.distance_m, 0.0f, 1e-6f));
    CHECK(nearly(score_remaining(&r), 0.0f, 1e-6f));
    CHECK(!score_finished(&r));
}

/* ---------------------------------------------------------------- hiscore */

static void fill_entry(hiscore_entry_t *e, uint16_t dist, uint32_t seed)
{
    memset(e, 0, sizeof(*e));
    e->distance_dm = dist;
    e->seed = seed;
    e->map = ROAD_MAP_DEFAULT;
    e->elevation_pct = 100;
    e->hill_pct = 100;
    e->width_pct = 100;
    e->one_lane_dm = 44;
}

static void test_hiscore_rank_and_insert(void)
{
    hiscore_book_t b;
    hiscore_entry_t e;
    int i;

    hiscore_clear(&b);
    CHECK(hiscore_rank(&b.table[ROAD_MAP_DEFAULT], 1000) == 1);
    CHECK(hiscore_rank(&b.table[ROAD_MAP_DEFAULT], 1000) == 1);

    for (i = 0; i < 10; i++) {
        fill_entry(&e, (uint16_t)(1000 - 10 * i), (uint32_t)(i + 1));
        CHECK(hiscore_insert(&b.table[0], &e) == i + 1);
    }
    CHECK(b.table[0].count == 10);

    fill_entry(&e, 955, 99); /* between 960 and 950 */
    CHECK(hiscore_insert(&b.table[0], &e) == 6);
    CHECK(b.table[0].entry[5].distance_dm == 955);
    CHECK(b.table[0].count == 10);
    CHECK(b.table[0].entry[0].distance_dm == 1000);
    CHECK(b.table[0].entry[9].distance_dm == 920); /* old #10 (910) dropped */
    CHECK(b.table[0].entry[0].distance_dm > b.table[0].entry[9].distance_dm);
}

static void test_hiscore_ties_keep_the_older_entry(void)
{
    hiscore_book_t b;
    hiscore_entry_t e;

    hiscore_clear(&b);
    fill_entry(&e, 1000, 1);
    CHECK(hiscore_insert(&b.table[0], &e) == 1);
    fill_entry(&e, 900, 2);
    CHECK(hiscore_insert(&b.table[0], &e) == 2);
    fill_entry(&e, 900, 3);
    CHECK(hiscore_insert(&b.table[0], &e) == 3);
    CHECK(b.table[0].entry[1].seed == 2); /* older tie first */
    CHECK(b.table[0].entry[2].seed == 3);
}

static void test_hiscore_rejects_when_full(void)
{
    hiscore_book_t b;
    hiscore_entry_t e;
    int i;

    hiscore_clear(&b);
    for (i = 0; i < 10; i++) {
        fill_entry(&e, (uint16_t)(1000 - 10 * i), (uint32_t)(i + 1));
        CHECK(hiscore_insert(&b.table[0], &e) == i + 1);
    }
    fill_entry(&e, 905, 99);
    CHECK(hiscore_insert(&b.table[0], &e) == 0);
    CHECK(b.table[0].count == 10);
    CHECK(b.table[0].entry[9].distance_dm == 910);
}

static void test_hiscore_insert_rejects_invalid_entry(void)
{
    hiscore_book_t b;
    hiscore_entry_t e;

    hiscore_clear(&b);
    fill_entry(&e, 1000, 1);
    CHECK(hiscore_insert(&b.table[0], &e) == 1);

    fill_entry(&e, (uint16_t)(HISCORE_MAX_DISTANCE_DM + 1), 2);
    CHECK(hiscore_insert(&b.table[0], &e) == 0); /* no clamping */
    CHECK(b.table[0].count == 1);
    CHECK(b.table[0].entry[0].distance_dm == 1000);

    fill_entry(&e, 1200, 3);
    e.elevation_pct = 49; /* out of range */
    CHECK(hiscore_insert(&b.table[0], &e) == 0);
    CHECK(b.table[0].count == 1);
}

static void test_hiscore_entry_from_run(void)
{
    road_config_t cfg;
    hiscore_entry_t e;

    road_config_defaults(&cfg);
    cfg.map = ROAD_MAP_UPHILL;
    cfg.elevation_pct = 120.0f;
    cfg.hill_pct = 90.0f;
    cfg.width_pct = 150.0f;
    cfg.one_lane_width = 5.6f;

    hiscore_entry_from_run(&e, 1234.56f, 0x5EED2A17u, &cfg, 2026, 11, 10);
    CHECK(e.distance_dm == 12346);
    CHECK(e.seed == 0x5EED2A17u);
    CHECK(e.map == ROAD_MAP_UPHILL);
    CHECK(e.elevation_pct == 120 && e.hill_pct == 90 && e.width_pct == 150);
    CHECK(e.one_lane_dm == 56);
    CHECK(e.year_offset == 27 && e.month == 11 && e.day == 10);

    hiscore_entry_from_run(&e, 3148.5f, 1u, &cfg, 0, 0, 0);
    CHECK(e.distance_dm == 31485);
    CHECK(e.year_offset == 0 && e.month == 0 && e.day == 0); /* unknown */
    CHECK(HISCORE_MAX_DISTANCE_DM >= 31486);

    hiscore_entry_from_run(&e, 9999.9f, 1u, &cfg, 2026, 11, 10);
    CHECK(e.distance_dm == HISCORE_MAX_DISTANCE_DM); /* clamped here only */
}

/* --------------------------------------------------------- file format */

static void put_entry(uint8_t *p, uint16_t dist, uint32_t seed, uint8_t map,
                      uint8_t el, uint8_t hi, uint8_t wi, uint8_t ol,
                      uint8_t yo, uint8_t mo, uint8_t dy)
{
    p[0] = (uint8_t)(dist & 0xFF);
    p[1] = (uint8_t)(dist >> 8);
    p[2] = (uint8_t)(seed & 0xFF);
    p[3] = (uint8_t)((seed >> 8) & 0xFF);
    p[4] = (uint8_t)((seed >> 16) & 0xFF);
    p[5] = (uint8_t)((seed >> 24) & 0xFF);
    p[6] = map;
    p[7] = el;
    p[8] = hi;
    p[9] = wi;
    p[10] = ol;
    p[11] = yo;
    p[12] = mo;
    p[13] = dy;
    p[14] = 0;
    p[15] = 0;
}

static void put_crc(uint8_t *buf, int covered)
{
    uint16_t crc = settings_crc16(buf, covered);

    buf[510] = (uint8_t)(crc & 0xFF);
    buf[511] = (uint8_t)(crc >> 8);
}

static void build_reference_buffer(uint8_t *buf)
{
    memset(buf, 0, HISCORE_FILE_SIZE);
    memcpy(buf, "R3DH", 4);
    buf[4] = HISCORE_VERSION;
    buf[5] = 0;
    buf[6] = HISCORE_TABLE_COUNT;
    buf[7] = 0;
    buf[8] = 2; /* DEFAULT */
    buf[9] = 0; /* UPHILL */
    buf[10] = 1; /* HIGHWAY */
    put_entry(buf + 16, 31485, 0x5EED2A17u, ROAD_MAP_DEFAULT, 100, 100, 100,
              44, 27, 11, 10);
    put_entry(buf + 32, 12346, 0x00000001u, ROAD_MAP_DEFAULT, 150, 50, 110, 60,
              27, 11, 9);
    put_entry(buf + 336, 900, 0xDEADBEEFu, ROAD_MAP_HIGHWAY, 100, 120, 100, 40,
              0, 0, 0);
    put_crc(buf, 510);
}

static void fill_reference_book(hiscore_book_t *b)
{
    uint8_t buf[HISCORE_FILE_SIZE];

    build_reference_buffer(buf);
    CHECK(hiscore_deserialize(b, buf, HISCORE_FILE_SIZE));
}

static void test_hiscore_file_is_byte_exact(void)
{
    hiscore_book_t b;
    uint8_t buf[HISCORE_FILE_SIZE];
    const uint8_t expect_header[16] = {0x52, 0x33, 0x44, 0x48, 1, 0, 3, 0,
                                       2,    0,    1,    0,    0, 0, 0, 0};
    const uint8_t expect_e0[16] = {0xfd, 0x7a, 0x17, 0x2a, 0xed, 0x5e, 0x00,
                                   0x64, 0x64, 0x64, 0x2c, 0x1b, 0x0b, 0x0a,
                                   0x00, 0x00};
    const uint8_t expect_e1[16] = {0x3a, 0x30, 0x01, 0x00, 0x00, 0x00, 0x00,
                                   0x96, 0x32, 0x6e, 0x3c, 0x1b, 0x0b, 0x09,
                                   0x00, 0x00};
    const uint8_t expect_e2[16] = {0x84, 0x03, 0xef, 0xbe, 0xad, 0xde, 0x02,
                                   0x64, 0x78, 0x64, 0x28, 0x00, 0x00, 0x00,
                                   0x00, 0x00};

    CHECK(HISCORE_FILE_SIZE == 512);
    CHECK(HISCORE_ENTRY_SIZE == 16);
    CHECK(HISCORE_TOP_N == 10);
    CHECK(sizeof(hiscore_entry_t) == HISCORE_ENTRY_SIZE);

    fill_reference_book(&b);
    CHECK(hiscore_serialize(&b, buf, (int)sizeof(buf)) == HISCORE_FILE_SIZE);
    CHECK(memcmp(buf, expect_header, sizeof(expect_header)) == 0);
    CHECK(memcmp(buf + 16, expect_e0, sizeof(expect_e0)) == 0);
    CHECK(memcmp(buf + 32, expect_e1, sizeof(expect_e1)) == 0);
    CHECK(memcmp(buf + 336, expect_e2, sizeof(expect_e2)) == 0);
    for (int i = 496; i < 510; i++)
        CHECK(buf[i] == 0);
    CHECK(buf[510] == 0xAD && buf[511] == 0xC4); /* CRC 0xC4AD */
}

static void test_hiscore_round_trip(void)
{
    hiscore_book_t a;
    hiscore_book_t b;
    uint8_t buf[HISCORE_FILE_SIZE];

    memset(&a, 0, sizeof(a));
    fill_reference_book(&a);
    memset(&b, 0, sizeof(b));
    CHECK(hiscore_serialize(&a, buf, (int)sizeof(buf)) == HISCORE_FILE_SIZE);
    CHECK(hiscore_deserialize(&b, buf, HISCORE_FILE_SIZE));
    CHECK(b.table[0].count == 2);
    CHECK(b.table[1].count == 0);
    CHECK(b.table[2].count == 1);
    CHECK(memcmp(&a, &b, sizeof(a)) == 0);
    CHECK(hiscore_serialize(&a, buf, 8) == -1); /* too small */
}

static void test_hiscore_deserialize_rejects_corruption(void)
{
    hiscore_book_t b;
    uint8_t buf[HISCORE_FILE_SIZE];

    /* every rejection must leave an empty book */
    build_reference_buffer(buf);
    buf[16] ^= 0x01; /* CRC covered bit flip */
    hiscore_clear(&b);
    CHECK(!hiscore_deserialize(&b, buf, HISCORE_FILE_SIZE));
    CHECK(b.table[0].count == 0 && b.table[2].count == 0);

    build_reference_buffer(buf);
    CHECK(!hiscore_deserialize(&b, buf, 511)); /* short file */

    build_reference_buffer(buf);
    buf[0] = 'X';
    put_crc(buf, 510);
    CHECK(!hiscore_deserialize(&b, buf, HISCORE_FILE_SIZE)); /* magic */

    build_reference_buffer(buf);
    buf[4] = 2;
    put_crc(buf, 510);
    CHECK(!hiscore_deserialize(&b, buf, HISCORE_FILE_SIZE)); /* version */

    build_reference_buffer(buf);
    buf[5] = 0x01;
    put_crc(buf, 510);
    CHECK(!hiscore_deserialize(&b, buf, HISCORE_FILE_SIZE)); /* flags */

    build_reference_buffer(buf);
    buf[6] = 2;
    put_crc(buf, 510);
    CHECK(!hiscore_deserialize(&b, buf, HISCORE_FILE_SIZE)); /* table count */

    build_reference_buffer(buf);
    buf[8] = 11;
    put_crc(buf, 510);
    CHECK(!hiscore_deserialize(&b, buf, HISCORE_FILE_SIZE)); /* count > 10 */

    build_reference_buffer(buf);
    buf[12] = 1; /* reserved inside the header */
    put_crc(buf, 510);
    CHECK(!hiscore_deserialize(&b, buf, HISCORE_FILE_SIZE));

    build_reference_buffer(buf);
    buf[16 + 6] = ROAD_MAP_UPHILL; /* map does not match its table */
    put_crc(buf, 510);
    CHECK(!hiscore_deserialize(&b, buf, HISCORE_FILE_SIZE));

    build_reference_buffer(buf);
    buf[16 + 14] = 1; /* reserved inside an entry */
    put_crc(buf, 510);
    CHECK(!hiscore_deserialize(&b, buf, HISCORE_FILE_SIZE));

    build_reference_buffer(buf);
    buf[16 + 7] = 49; /* elevation out of range */
    put_crc(buf, 510);
    CHECK(!hiscore_deserialize(&b, buf, HISCORE_FILE_SIZE));

    build_reference_buffer(buf);
    buf[16 + 12] = 13; /* month out of range */
    put_crc(buf, 510);
    CHECK(!hiscore_deserialize(&b, buf, HISCORE_FILE_SIZE));

    build_reference_buffer(buf);
    buf[16 + 11] = 0; /* unknown year with a known day */
    buf[16 + 13] = 3;
    put_crc(buf, 510);
    CHECK(!hiscore_deserialize(&b, buf, HISCORE_FILE_SIZE));

    build_reference_buffer(buf);
    buf[16 + 11] = 202; /* year offset out of range */
    put_crc(buf, 510);
    CHECK(!hiscore_deserialize(&b, buf, HISCORE_FILE_SIZE));

    build_reference_buffer(buf);
    buf[16 + 10] = 61; /* one-lane out of range */
    put_crc(buf, 510);
    CHECK(!hiscore_deserialize(&b, buf, HISCORE_FILE_SIZE));

    build_reference_buffer(buf);
    buf[17] = 0xC8; /* distance 51200+ > ceiling */
    put_crc(buf, 510);
    CHECK(!hiscore_deserialize(&b, buf, HISCORE_FILE_SIZE));
}

static void test_hiscore_deserialize_sorts_unordered_entries(void)
{
    hiscore_book_t b;
    uint8_t buf[HISCORE_FILE_SIZE];

    memset(buf, 0, sizeof(buf));
    memcpy(buf, "R3DH", 4);
    buf[4] = HISCORE_VERSION;
    buf[6] = HISCORE_TABLE_COUNT;
    buf[8] = 3;
    /* file order: 1000/seed1, 900/seed3, 900/seed2 */
    put_entry(buf + 16, 1000, 1u, 0, 100, 100, 100, 44, 27, 11, 10);
    put_entry(buf + 32, 900, 3u, 0, 100, 100, 100, 44, 27, 11, 10);
    put_entry(buf + 48, 900, 2u, 0, 100, 100, 100, 44, 27, 11, 10);
    put_crc(buf, 510);

    hiscore_clear(&b);
    CHECK(hiscore_deserialize(&b, buf, HISCORE_FILE_SIZE));
    CHECK(b.table[0].entry[0].distance_dm == 1000);
    CHECK(b.table[0].entry[1].seed == 3); /* stable: file order kept */
    CHECK(b.table[0].entry[2].seed == 2);
}

static void test_hiscore_serialize_rejects_invalid_book(void)
{
    hiscore_book_t b;
    uint8_t buf[HISCORE_FILE_SIZE];

    hiscore_clear(&b);
    b.table[0].count = 11; /* impossible count */
    CHECK(hiscore_serialize(&b, buf, (int)sizeof(buf)) == -1);

    hiscore_clear(&b);
    b.table[1].count = 1;
    fill_entry(&b.table[1].entry[0], 1000, 7);
    b.table[1].entry[0].map = ROAD_MAP_DEFAULT; /* wrong table */
    CHECK(hiscore_serialize(&b, buf, (int)sizeof(buf)) == -1);

    hiscore_clear(&b);
    CHECK(hiscore_serialize(NULL, buf, (int)sizeof(buf)) == -1);
}

static void test_hiscore_leaderboard_columns(void)
{
    CHECK(hiscore_column_x(HISCORE_COLUMN_RANK) == 150);
    CHECK(hiscore_column_x(HISCORE_COLUMN_DIST) == 166);
    CHECK(hiscore_column_x(HISCORE_COLUMN_SEED) == 230);
    CHECK(hiscore_column_x(HISCORE_COLUMN_DATE) == 300);
    CHECK(hiscore_column_x(HISCORE_COLUMN_DIST) >
          hiscore_column_x(HISCORE_COLUMN_RANK));
    CHECK(hiscore_column_x(HISCORE_COLUMN_SEED) >=
          hiscore_column_x(HISCORE_COLUMN_DIST) + 27); /* "1234.5M" */
    CHECK(hiscore_column_x(HISCORE_COLUMN_DATE) >=
          hiscore_column_x(HISCORE_COLUMN_SEED) + 31); /* seed is 8 chars */
}

static void test_hiscore_format_date_uses_two_digit_year(void)
{
    hiscore_entry_t e;
    char buf[16];

    memset(&e, 0, sizeof(e));
    e.year_offset = 27; /* 2026 */
    e.month = 11;
    e.day = 10;
    hiscore_format_date(buf, &e);
    CHECK(strcmp(buf, "26-11-10") == 0);

    e.month = 1;
    e.day = 2;
    hiscore_format_date(buf, &e);
    CHECK(strcmp(buf, "26-01-02") == 0);

    e.year_offset = 1; /* 2000 */
    e.month = 12;
    e.day = 31;
    hiscore_format_date(buf, &e);
    CHECK(strcmp(buf, "00-12-31") == 0);

    e.year_offset = 0; /* unknown */
    e.month = 0;
    e.day = 0;
    hiscore_format_date(buf, &e);
    CHECK(strcmp(buf, "--") == 0);
}

static void test_hiscore_result_status(void)
{
    hiscore_result_t r;

    memset(&r, 0, sizeof(r));
    r.rank = 1;
    CHECK(strcmp(hiscore_result_status(&r, false), "SAVED") == 0);
    CHECK(strcmp(hiscore_result_status(&r, true), "NOT SAVED") == 0);

    r.rank = 0; /* did not make the table: nothing was written */
    CHECK(strcmp(hiscore_result_status(&r, true), "NOT RANKED") == 0);
    CHECK(strcmp(hiscore_result_status(&r, false), "NOT RANKED") == 0);
}

static void test_hiscore_commit_reports_the_save_need(void)
{
    hiscore_book_t b;
    hiscore_entry_t e;
    bool needs_save = true;
    int i;

    hiscore_clear(&b);
    fill_entry(&e, 1000, 1);
    CHECK(hiscore_commit(&b, &e, &needs_save) == 1);
    CHECK(needs_save);

    /* fill the table, then commit a run that cannot make it */
    for (i = 1; i < 10; i++) {
        fill_entry(&e, (uint16_t)(1000 - 10 * i), (uint32_t)(i + 1));
        CHECK(hiscore_commit(&b, &e, &needs_save) == i + 1);
    }
    needs_save = true; /* stale flag from an earlier run */
    fill_entry(&e, 100, 99);
    CHECK(hiscore_commit(&b, &e, &needs_save) == 0);
    CHECK(!needs_save); /* cleared: this run wrote nothing */
    CHECK(b.table[0].count == 10);
}

static void test_hiscore_dirty_survives_a_non_ranking_run(void)
{
    bool dirty = false;

    /* Run A ranks but the card is full/read-only: the write fails. */
    dirty = hiscore_dirty_after(dirty, true, false);
    CHECK(dirty);
    /* Run B does not make the table: no save is attempted, so the warning
     * must stay because the in-RAM book still holds unsaved run A. */
    dirty = hiscore_dirty_after(dirty, false, false);
    CHECK(dirty);
    /* A later successful save clears it. */
    dirty = hiscore_dirty_after(dirty, true, true);
    CHECK(!dirty);
    /* And a failed save after a clean state marks it again. */
    dirty = hiscore_dirty_after(dirty, true, false);
    CHECK(dirty);
}

static void test_hiscore_rejects_zero_distance_run(void)
{
    hiscore_book_t b;
    hiscore_entry_t e;
    bool needs_save = true;
    uint8_t buf[HISCORE_FILE_SIZE];

    /* A run that never moved forward (reverse-only, or pinned on a rail)
     * must not rank: 0 dm would otherwise take rank 1 of an empty table. */
    hiscore_clear(&b);
    fill_entry(&e, 0, 7);
    CHECK(!hiscore_entry_valid(&e));
    CHECK(hiscore_commit(&b, &e, &needs_save) == 0);
    CHECK(!needs_save);              /* nothing to write */
    CHECK(b.table[0].count == 0);    /* empty table stays empty */

    /* Defence in depth: a forced 0 dm entry cannot be serialized either. */
    b.table[0].count = 1;
    b.table[0].entry[0] = e;
    CHECK(hiscore_serialize(&b, buf, (int)sizeof(buf)) == -1);
    hiscore_clear(&b);

    /* One decimetre (0.1 m) still ranks: the bound is > 0, not "meaningful". */
    fill_entry(&e, 1, 8);
    CHECK(hiscore_entry_valid(&e));
    CHECK(hiscore_commit(&b, &e, &needs_save) == 1);
    CHECK(needs_save);
    CHECK(b.table[0].count == 1);
}

int main(void)
{
    test_score_release_clock_is_60();
    test_score_starts_on_throttle_only();
    test_score_counts_forward_only();
    test_score_finishes_once();
    test_score_free_drive_is_inert();
    test_hiscore_rank_and_insert();
    test_hiscore_ties_keep_the_older_entry();
    test_hiscore_rejects_when_full();
    test_hiscore_insert_rejects_invalid_entry();
    test_hiscore_entry_from_run();
    test_hiscore_file_is_byte_exact();
    test_hiscore_round_trip();
    test_hiscore_deserialize_rejects_corruption();
    test_hiscore_deserialize_sorts_unordered_entries();
    test_hiscore_serialize_rejects_invalid_book();
    test_hiscore_leaderboard_columns();
    test_hiscore_format_date_uses_two_digit_year();
    test_hiscore_result_status();
    test_hiscore_commit_reports_the_save_need();
    test_hiscore_dirty_survives_a_non_ranking_run();
    test_hiscore_rejects_zero_distance_run();

    if (failures) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    printf("all racing score tests passed\n");
    return 0;
}
