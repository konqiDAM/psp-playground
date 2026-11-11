# Racing 3D — Score Attack reuse spec (Time Attack + leaderboard)

Status: **design only** (no code on this branch). Implements
`docs/score-attack-PLAN.md`. Everything here is portable C99 unless marked
**PSP**.

Authoritative values: nothing in this file is a placeholder. Every constant is
either copied from the current tree (`racing/road.h`, `racing/settings.h`) or
derived in §5/§7 with the arithmetic shown.

**Scope rulings (orchestrator):** the design branch ships only
`docs/score-attack-PLAN.md` + this file. The later implementation may add
`racing/score.c/h`, `racing/hiscore.c/h`, `tests/test_racing_score.c`,
`scripts/check-racing-score.py` and extend `racing/CMakeLists.txt` /
`scripts/test.sh`. `VERSION`, PBP `APP_VER`, `README.md`, `docs/RACING.md`,
`docs/HANDOFF.md` and `docs/HEADLESS-TESTING.md` are updated by the
integrator/publisher **after** integration, never by the implementation
branch.

**Naming:** module = `racing/hiscore.c` (`hiscore_*` / `HISCORE_*`); the
memory-stick file stays `ms0:/PSP/SAVEDATA/RACING3D/RACING.HOF`, so "HOF" in
prose always means the file.

---

## 1. Modes

| Mode | Default | Behaviour |
|---|---|---|
| `WELCOME_MODE_TIME_ATTACK` (0) | **yes** | 60 s clock, score, result panel, HOF entry |
| `WELCOME_MODE_FREE_DRIVE` (1) | no | 0.5.0 behaviour exactly: no clock, no score, no HOF write, `DIST` = odometer |

The mode is session-only and is **not** written to `RACING.CFG` (that file
stays the 16-byte v1 settings file). `RESTART` in the pause menu aborts the
run without recording it and returns to the welcome RACE page with the current
mode/map/sliders.

## 2. Timer state machine (`racing/score.c`)

```
states: IDLE (not started) | RUNNING | COASTING (clock latched, car rolling)
        | RESULT (at rest: the card may show) | FREE (Free Drive, inert)

score_reset(r, free):
    state   = free ? FREE : IDLE
    elapsed_s = 0 ; distance_m = 0 ; rest_s = 0

score_step(r, throttle, ds, dt):
    if state in (FREE, COASTING, RESULT) or dt <= 0:  return  # latched/inert
    if state == IDLE:
        if not throttle:              return      # idling is free
        state = RUNNING                            # throttle start edge
    if state == RUNNING:
        if ds > 0: distance_m += ds                # forward progress only
        elapsed_s += dt
        if elapsed_s >= SCORE_RUN_SECONDS:
            elapsed_s = SCORE_RUN_SECONDS
            state = COASTING                       # 0.9.0: not FINISHED
    # COASTING / RESULT: no tick, no accumulation

score_coast_step(r, speed, airborne, dt):
    if state != COASTING or dt <= 0:  return
    if airborne or |speed| > SCORE_REST_SPEED_MPS: rest_s = 0
    else:                                          rest_s += dt
    if rest_s >= SCORE_REST_HOLD_S:  state = RESULT

score_brake_allowed(r, speed):
    state != COASTING            -> true
    state == COASTING            -> speed > SCORE_REST_SPEED_MPS

score_remaining(r): FREE -> 0 ; IDLE -> SCORE_RUN_SECONDS ;
                    COASTING/RESULT -> 0 ;
                    else max(0, SCORE_RUN_SECONDS - elapsed_s)
score_finished(r):     state in (COASTING, RESULT)
score_result_ready(r): state == RESULT
```

0.9.0 constants: `SCORE_REST_SPEED_MPS` = 0.556 (2 km/h) and
`SCORE_REST_HOLD_S` = 0.3. The brake gate exists because `physics.c` turns a
held SQUARE into reverse once the speed reaches 0; without it a player holding
the brake at rest can never reach the latch (measured: no rest in 6 s at
-8 m/s). Airborne at the latch resets `rest_s`, so a crest launch at the
finish waits for landing *and* rest.

Rules:

- `SCORE_RUN_SECONDS` = `(float)RACING_DIAG_TA_SECONDS`, default **60** (a
  test-only compile override, never in a published EBOOT).
- The start edge frame **counts**: its distance and its `dt` are accumulated.
- Pause: the caller does not call `score_step()` while the menu is open.
- `dt` is the frame delta already clamped to ≤ **0.05 s** by `main.c`; a hitch
  therefore advances at most 50 ms and a run never ends early.
- Measured float behaviour (`elapsed_s += dt`, C99 float32, verified with a
  local compiler): after 3600 steps at `dt = 1/60` the accumulated
  `elapsed = 59.99926 s` (remaining 0.00074 s), so the clock finishes on frame
  **3601**; after 1200 steps at `dt = 0.05` `elapsed = 59.99937 s`, so it
  finishes on frame **1201**. Guarantee to test: 3601 frames at `dt = 1/60`
  and 1201 at `dt = 0.05` ("at most one frame late by under a millisecond",
  never early).
- On FINISH the caller feeds a zeroed input (throttle off, brake 0, steer 0)
  but keeps stepping physics: the car coasts to a stop, never freezes.
  Known cosmetic edge: the slope term is applied after the coast snap
  (`racing/physics.c:119`), so downhill the finished car may creep slowly.
  Accepted; never feed brake to force a stop, because with throttle off the
  brake input drives the car *backwards* once it is stationary
  (`racing/physics.c:100-104`).

## 3. Score

`distance_m` accumulates `max(0, ds)` where `ds = speed * cos(slip) * dt` is
the increment `car_step()` applies to `car->s` (`car_state_t.ds`).

| Case | Counts? |
|---|---|
| Forward on asphalt | yes |
| Forward off-road (on the shoulder) | yes — capped by the 16.5 m/s off-road limit |
| Reverse (`ds < 0`) | no (adds nothing, subtracts nothing) |
| Stationary / `dt = 0` | no |
| Sliding / airborne | yes, through `ds` |
| Road re-base (`car.s` jumps by −960 or more) | unaffected: per-frame increments |

Oscillating forward/reverse cannot double-count: each positive increment is
added once.

Theoretical maximum for 60 s: `v(t) = 120(1 − e^{−0.1t})` clamped at 55 m/s,
`t₁ = 6.131 s`, `d₁ = 185.73 m`, then 53.87 s at 55 m/s ⇒ **3148.5 m**
⇒ **31485 dm**.

## 4. Leaderboard model (`racing/hiscore.c`)

```c
#define HISCORE_TOP_N 10
#define HISCORE_ENTRY_SIZE 16
#define HISCORE_FILE_SIZE 512
#define HISCORE_VERSION 1
#define HISCORE_MAX_DISTANCE_DM 32000  /* 3200 m; a 60 s run peaks at 31485 */
typedef struct {
    uint16_t distance_dm;   /* decimetres, 0..32000 */
    uint32_t seed;          /* road seed of the run */
    uint8_t  map;           /* ROAD_MAP_* */
    uint8_t  elevation_pct; /* 50..150 */
    uint8_t  hill_pct;      /* 50..150 */
    uint8_t  width_pct;     /* 50..150 */
    uint8_t  one_lane_dm;   /* one-lane total width x10, 40..60 */
    uint8_t  year_offset;   /* year - 1999: 1 = 2000 .. 201 = 2200, 0 = unknown */
    uint8_t  month;         /* 1..12, 0 = unknown */
    uint8_t  day;           /* 1..31, 0 = unknown */
} hiscore_entry_t;              /* 14 meaningful bytes; sizeof == 16 (padding) */
typedef struct { uint8_t count; hiscore_entry_t entry[HISCORE_TOP_N]; } hiscore_table_t;
typedef struct { hiscore_table_t table[ROAD_MAP_COUNT]; } hiscore_book_t;
typedef struct { hiscore_entry_t entry; int rank; } hiscore_result_t;
```

Decimetres: ~31 500 distinguishable values in a run instead of ~3 150, so
near-identical runs do not tie, and `31485 < 65535` so `uint16_t` suffices.

**Year offset is 1-based** (`year - 1999`) so `0` can mean "unknown" without
colliding with the year 2000. `sizeof(hiscore_entry_t) == 16` (host compiler:
`distance_dm@0`, `seed@4`, `map@8` … `day@15`, i.e. 2 padding bytes at 2–3).
The on-disk bytes are written field by field, so padding never reaches the
file; **all constructors and `hiscore_deserialize` must `memset` the entry or
book to zero** so `memcmp` round-trip tests compare identical padding.

Ranking / insertion / eviction:

```
rank = 1 + number of stored entries with distance_dm >= new.distance_dm
if the entry is invalid (hiscore_entry_valid() false, which includes
   distance_dm > HISCORE_MAX_DISTANCE_DM): reject -> return 0, table unchanged
if rank > HISCORE_TOP_N: reject (return 0), table unchanged
else: insert at index rank-1 (shift the tail), and if the table was full drop
      the old last entry; entries stay sorted descending
```

`hiscore_insert` never clamps; only `hiscore_entry_from_run()` clamps to
`HISCORE_MAX_DISTANCE_DM`, so bad input can never silently rewrite a score.

- Ties favour the **older** stored entry ⇒ `NEW BEST` means `rank == 1`.
- Insertion happens in RAM before the save attempt, so a failed write still
  shows the rank and the entry for this session (like settings).
- The book is validated on load and re-sorted descending if the file order is
  wrong (forgiving, not data-destroying) using a **stable insertion sort keyed
  on `distance_dm` only** — never `qsort`, which may permute equal keys and
  break "older first".

`hiscore_entry_from_run(entry, distance_m, seed, cfg, year, month, day)`:
`distance_dm = min(32000, (int)(distance_m * 10 + 0.5))`, percentages rounded
from the config floats, `one_lane_dm = (int)(one_lane_width * 10 + 0.5)`,
`year_offset = year - 1999` (1 = 2000 … 201 = 2200; 0 when the date is
unknown — see §5).

## 5. On-disk format — `ms0:/PSP/SAVEDATA/RACING3D/RACING.HOF`

**512 bytes**, little-endian, CRC-16/CCITT-FALSE (poly `0x1021`, init
`0xFFFF`, no reflection — the same routine as `settings_crc16`, reused).

Header (16 B):

| Off | Bytes | Field |
|---:|---:|---|
| 0 | 4 | magic `52 33 44 48` = `"R3DH"` |
| 4 | 1 | version = 1 |
| 5 | 1 | flags = 0 (must be 0) |
| 6 | 1 | table_count = 3 |
| 7 | 1 | reserved = 0 |
| 8 | 3 | `count[0..2]` per table, each 0..10 |
| 11 | 5 | reserved = 0 |

Tables — ten fixed 16-byte slots each, unused slots all zero:

| Off | Bytes | Table |
|---:|---:|---|
| 16 | 160 | `ROAD_MAP_DEFAULT` |
| 176 | 160 | `ROAD_MAP_UPHILL` |
| 336 | 160 | `ROAD_MAP_HIGHWAY` |

Entry at `16 + 160*map + 16*index`:

| Off | Bytes | Field |
|---:|---:|---|
| 0 | 2 | `distance_dm` u16 LE |
| 2 | 4 | `seed` u32 LE |
| 6 | 1 | `map` 0..2 |
| 7 | 1 | `elevation_pct` 50..150 |
| 8 | 1 | `hill_pct` 50..150 |
| 9 | 1 | `width_pct` 50..150 |
| 10 | 1 | `one_lane_dm` 40..60 |
| 11 | 1 | `year_offset` 0 or 1..201 (1 = 2000, 0 = unknown) |
| 12 | 1 | `month` 1..12, or 0 when the date is unknown |
| 13 | 1 | `day` 1..31, or 0 when the date is unknown |
| 14 | 2 | reserved = 0 |

Tail:

| Off | Bytes | Field |
|---:|---:|---|
| 496 | 14 | reserved = 0 |
| 510 | 2 | CRC-16/CCITT-FALSE over bytes 0…509, u16 LE |

Load validation (any failure ⇒ clear the book, return false): length exactly
512; magic; version 1; flags 0; table_count 3; each count ≤ 10; every
reserved byte 0; per-entry ranges above; date is either `0/0/0` or
`year_offset 1..201, month 1..12, day 1..31`; `entry.map == table index`;
unused slots fully zero. Out-of-order entries are sorted stably, not rejected.

Reference vector (pinned by the host test):

```
table0 count 2:  {31485, 0x5EED2A17, 0, 100,100,100, 44, 27,11,10}
                 {12346, 0x00000001, 0, 150, 50,110, 60, 27,11, 9}
table1 count 0
table2 count 1:  {  900, 0xDEADBEEF, 2, 100,120,100, 40,  0, 0, 0}

header   52 33 44 48 01 00 03 00 02 00 01 00 00 00 00 00
entry0@16 fd 7a 17 2a ed 5e 00 64 64 64 2c 1b 0b 0a 00 00
entry1@32 3a 30 01 00 00 00 00 96 32 6e 3c 1b 0b 09 00 00
entry0@336 84 03 ef be ad de 02 64 78 64 28 00 00 00 00 00
496..509   all 00
510..511   ad c4            # CRC 0xC4AD, little-endian
```

Every byte of `entry1` decodes as: `3a 30` = 12346 dm, `01 00 00 00` = seed 1,
`00` = map DEFAULT, `96` = 150 % elevation, `32` = 50 % hills, `6e` = 110 %
width, `3c` = 60 dm one-lane, `1b` = year 2026 (1-based), `0b` = November,
`09` = day 9, `00 00` = reserved. (An earlier draft printed
`01 00 00 00 10 96 …` here, which put `0x10` in the `map` byte and gave CRC
`0x57CA`; that was a transcription slip in this table only — the plan and the
test already had the correct bytes.)

## 6. Persistence strategy (**PSP**)

Load: clear book → read `RACING.HOF` (exactly 512 B) → `hiscore_deserialize`;
on failure read `RACING.HOF.BAK` the same way and adopt it if valid.

Save (FAT `sceIoRename` does not replace — see `docs/PSP-DEV-GUIDE.md` §1.5):

```
1. hiscore_serialize() into a 512-byte buffer (validate first)
2. sceIoMkdir ms0:/PSP, ms0:/PSP/SAVEDATA, ms0:/PSP/SAVEDATA/RACING3D (ignore errors)
3. write RACING.HOF.NEW (PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC, 0666), 512 B
4. close, sceIoSync("ms0:", 0)
5. read RACING.HOF.NEW back and hiscore_deserialize it (never trust the write)
6. if RACING.HOF exists: remove RACING.HOF.BAK (ignore), rename HOF -> BAK
7. rename HOF.NEW -> HOF, sceIoSync; on failure rename BAK back, return false
8. remove RACING.HOF.BAK (ignore), sceIoSync, return true
```

Date: `sceRtcGetCurrentClockLocalTime` (`<psprtc.h>`, module `sceRtc`),
stamped at finish. Any failure or an out-of-range clock gives `0/0/0`
("unknown"); the run is still ranked and saved. `psprtc` is added to
`target_link_libraries`; the readelf gate must still show no `*ForKernel`.

Every failure is non-fatal: the in-RAM book keeps the run and the UI shows
`NOT SAVED` in warn colour while any run is unsaved (the dirty flag is cleared
only by a successful write, so a later non-ranking run cannot hide it).

**Accepted limitation (load side):** `storage_load_hiscore()`'s result is
discarded. A missing or corrupt `RACING.HOF` *and* `.BAK` yields an empty
book, which is indistinguishable from "no scores yet": the leaderboard shows
`NO RUNS YET`, nothing is warned, and the next successful save rewrites the
file. Surfacing it would need a second empty-state string with no way to
distinguish the two causes, so it is documented instead of guessed at.

## 7. Welcome screen (tabs, rows, leaderboard layout)

Pages (`L` = `PSP_CTRL_LTRIGGER 0x100`, `R` = `PSP_CTRL_RTRIGGER 0x200`,
edges):

- R: `RACE` → `LEADERBOARD`; `lb_map` starts at the RACE page's current map,
  `lb_cursor = lb_first = 0`.
- L: back to `RACE`; the RACE cursor is preserved.
- `accept` on the LEADERBOARD page does nothing.
- **A page-changing frame consumes the whole input**: when `page_next` or
  `page_prev` fires, the page changes and the update returns immediately
  (`WELCOME_ACTION_NONE`) without touching rows — otherwise `L` on the
  LEADERBOARD page would both return to RACE and move the focused slider in
  the same frame.

RACE page rows (append-only, indices are part of the probe contract):

```
0 MAP         1 START GAME   2 ELEVATION   3 HILL FREQ
4 WIDTH FREQ  5 ONE LANE     6 MODE
WELCOME_ITEM_COUNT = 7 ; layout y = 76 + i*24 (MODE at y 220)
```

`MODE` is appended **last** so the 0.5.0 probe navigation is unchanged: the
cursor starts on `START GAME` (1), `UP` reaches `MAP` (0), `DOWN` reaches
`ELEVATION` (2). The existing rows keep their old indices and screen
positions; this ordering is the single source of truth for both documents and
for `WELCOME_ITEM_MODE == WELCOME_ITEM_COUNT - 1`.

LEADERBOARD page (read-only, one map at a time):

```
y 48  tab strip, scale 2: "RACE" centred x120, "LEADERBOARD" centred x340
      active = accent + 2 px underline, inactive = label colour
y 72  map selector, scale 1: DEFAULT x196, UPHILL x236, HIGHWAY x272
      (active map accent)
y 88  header, scale 1 @x150: "RK  DIST      SEED      DATE"
y100  rows, scale 1, pitch 14, 8 visible (WELCOME_LB_VISIBLE):
      rank @150, "1234.5M" @166, "5EED2A17" @230, "26-11-10" @300
      empty: "NO RUNS YET" scale 2 at y100
y210  detail, scale 1 @x150: "EL 100 HI 100 WI 100 OL 4.4M"
y222  "NOT SAVED" scale 1 @x150, warn colour while any run is unsaved
y236  footer scale 1: "UP/DOWN ENTRY  LEFT/RIGHT MAP  L/R PAGE"

Columns (shared by the header and every row, `hiscore_column_x()`):
RK @150, DIST @166, SEED @230, DATE @300. Dates are `YY-MM-DD` derived from the
1-based year offset minus one: `year_offset 27` (2026) prints `26-11-10`.
```

`UP/DOWN` move the entry cursor and scroll the 8-row window. The window is
**lazy**: it only moves when the cursor leaves it, so scrolling up inside the
visible rows keeps the window still (the eager
`clamp(cursor - 7, 0, max_first)` form would jump it on every Up):

```c
max_first = (count > WELCOME_LB_VISIBLE) ? count - WELCOME_LB_VISIBLE : 0;
lb_cursor = clamp(lb_cursor, 0, (count > 0) ? count - 1 : 0);
if (lb_cursor < lb_first)                  lb_first = lb_cursor;
else if (lb_cursor >= lb_first + WELCOME_LB_VISIBLE)
                                           lb_first = lb_cursor - (WELCOME_LB_VISIBLE - 1);
lb_first  = clamp(lb_first, 0, max_first);  /* stale/shrunk tables */
```

The headless probe only scrolls **down** (cursor 0 -> 9), so it cannot
distinguish the lazy window from the eager one; the two forms differ only when
scrolling up inside the window, which the host test covers.

A table with fewer than 8 entries (`count < WELCOME_LB_VISIBLE`) therefore
keeps `lb_first == 0` — a naive `clamp(cursor - 7, 0, count - 8)` would use a
negative upper bound. Dates are `YY-MM-DD`, shown from a 1-based year offset
(`year_offset 27` = 2026; see §5).

Why one table plus a map selector instead of three side-by-side tables: at
4 px per character (3×5 font, scale 1) three columns leave ~155 px each, which
cannot show distance + seed + date + four terrain values; one table shows all
of them and makes the per-map tables explicit.

## 8. In-game HUD and result panel

Timer panel (Time Attack only, i.e. `state != SCORE_STATE_FREE`):

```
panel  (176, 40, 128, 28)  COL_HUD_PANEL
"TIME" (184, 44) scale 1   COL_HUD_LABEL
value  (184, 53) scale 2   COL_HUD_TEXT   (COL_HUD_WARN when remaining <= 10)
bar    (184, 66, 112, 4)   0x80000000 bg + accent width 112*remaining/60
```

`DIST` (right-aligned at 468, y 86): Time Attack shows the score in metres,
Free Drive the 0.5.0 odometer.

Result panel (Time Attack, on FINISH, menu closed; opaque card so the scene and
the G-meter cannot show through, and narrow enough to leave the minimap panel
(x ≥ 358) uncovered):

```
panel (76, 48, 264, 176) opaque RGB(0x10,0x14,0x18); text centred at x 208
"TIME UP"                      y 56  scale 3  accent
"MAP DEFAULT   SEED 5EED2A17"  y 78  scale 1  label
"DIST 1234.5 M"                y 92  scale 2  text
"RANK 3 OF 10"                 y108  scale 2  text   (rank 0 -> "RANK --")
"NEW BEST"                     y124  scale 2  accent (rank 1 only)
"X RETRY   START MENU"         y150  scale 2  text
status line                    y176  scale 1  SAVED / NOT SAVED / NOT RANKED
```

`X` retries with the **same seed** (no `road_next_start_seed()`; the road is
rebuilt from the identical seed); `START` returns to the welcome RACE page.
Both edges are consumed before `menu_update()`, so Start does not exit the
game.

Vertex budget (real font, one lit cell = 2 vertices, `hud_rect`):

| Element | Vertices |
|---|---:|
| current in-game HUD worst case (3498 glyph + ~40 rect) | 3538 |
| + timer panel (running frames only) | 158 |
| **running peak** | **3696** |
| + result panel (finish frames; the timer is skipped) | 1616 |
| **in-game peak** | **5312 of 8192** |
| welcome LEADERBOARD page, 8 full rows | 5918 of 8192 |

The 3538 baseline already counts the ~40 panel/bar rectangle vertices, which
still draw under the result panel. The timer and the result card are **never
drawn together** (`draw_hud()` skips the countdown when a result is shown), so
the real finish-frame peak is 3538 + 1616 = **5154**, while the running peak is
3538 + 158 = **3696**. The budget keeps the conservative **5312** (the sum) so
a future change that does draw both cannot overflow the staging array; an
earlier 5272 figure used a glyph-only 3498 baseline and was wrong.

`MAX_HUD_VERTS` = **8192** is the *staging capacity* (128 KiB `.bss`); only the
vertices actually emitted enter the display list, so gameplay peaks at 5312
and the leaderboard page at 5918 either way. The list itself is
`uint32_t[98304]` (**384 KiB**), re-derived by measurement on the final tree:

| Scenario (release clock) | Total list size | of 256 KiB |
|---|---:|---:|
| DEFAULT, 60 s, finish outside a corridor | 235 596 B | 89.9 % |
| HIGHWAY, 60 s | 188 208 B | 71.7 % |
| UPHILL, 60 s | 201 660 B | 76.9 % |
| Free Drive, 9 s | 167 824 B | 64.0 % |
| DEFAULT + rail corridor at finish (+ ≤ 3456 verts) | 290 892 B | **110.9 %** |

256 KiB cannot hold the last row, so 384 KiB it is (peak 74 % of the list).
`MAX_MAP_VERTS` 512 and `MAX_CAR_VERTS` 2048 are unchanged.

## 9. Host test contract

`tests/test_racing_score.c` (compiled by `scripts/test.sh` with
`racing/score.c racing/hiscore.c racing/settings.c racing/road.c`) pins:

- `SCORE_RUN_SECONDS == 60.0f` in the host build.
- IDLE ignores non-throttle input and `dt = 0`; the throttle edge counts.
- forward-only accumulation; FINISH latches; FREE is inert.
- 3601 frames at `dt = 1/60` and 1201 at `dt = 0.05` (measured float
  behaviour, "at most one frame late").
- rank/insert/evict/tie rules; invalid entries (including
  `distance_dm > HISCORE_MAX_DISTANCE_DM`) rejected with return 0 and an
  unchanged table; `HISCORE_MAX_DISTANCE_DM >= 31486`.
- stable re-sort on load: a file whose equal-distance entries are in order
  `1000, 900(seed3), 900(seed2)` loads as `1000, 900(seed3), 900(seed2)`.
- the byte-exact vector and CRC of §5, a `memcmp` round-trip with zeroed
  padding on both sides, `sizeof(hiscore_entry_t) == 16`, and rejection of
  every corruption class listed in §5.

`tests/test_racing_ui.c` pins the welcome tabs, the MODE row (last), the
leaderboard cursor/scroll clamp including a 3-entry and an empty table, that a
page-changing frame consumes the input (L on LEADERBOARD must not also move a
slider), and that existing RACE-page navigation is unchanged.

## 10. Headless probe contract

`scripts/check-racing-score.py` sections: `tabs`, `timer`, `idle`, `pause`,
`run`, `result`, `retry`, `persist`, `leaderboard`, `hiscore_recover` (see the
plan's Task 10 table for the exact pixel/count assertions, including the
per-capture arithmetic for the timer bar). Mechanics that need a finished run
use the test-only `RACING_DIAG_SHORT_TA` build (6 s clock); the release EBOOT
is always used for `tabs`/`timer`/`idle`/`pause`. The probe parses
`RACING.HOF` from the PPSSPP `--memstick` root exactly like
`check-racing-030.py` parses `RACING.CFG`.

## 11. Rebase contract

Guardrail (`racing/physics.c/h`, `guardrail_draw` at `render.c:1445`) and
minimap-outline (`draw_minimap`) land before this feature. After rebasing:
keep the `guardrail_draw`/`signs_draw` calls in `render_frame()` and add
`draw_result()` after `draw_minimap()` only; re-apply `car_state_t.ds`; re-derive
the display-list total (384 KiB, measured peak 290 892 B); re-check the timer
panel (176,40)-(304,68) and the result card (76,48)-(340,224) against the
minimap rectangle and update `HUD_EXCLUDE` in `check-racing-road.py` /
`check-racing-050.py` if a new opaque panel falls inside their road ROI.
