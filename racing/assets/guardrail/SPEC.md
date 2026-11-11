# Guardrail — specification (Racing 3D)

Status: **implemented; revised for Racing 3D 0.8.0 (Part D)**. The original
task breakdown is [`docs/guardrail-PLAN.md`](../../../docs/guardrail-PLAN.md);
this file is the durable reference for span rules, dimensions, materials,
tunables and the measured `ROAD_SEED` behaviour.

0.8.0 moved the rail from the **shoulder edge** (`edge_half_width`) to the
**asphalt edge** (`half_width`), replaced the narrowing stop with a constant
wheel-half-width stop plus a separate eligibility test, made one predicate
(`road_rail_ok`) the only clamp/draw decision, and made the drawn span the
**visible interval** `[v0, v1]` so a rail end is never an invisible wall.
Sections 2, 3.2, 4.4, 5, 7, 8, 9, 11 and 12 below carry the new numbers; the
0.6.0 figures they replace are quoted where they explain a design decision.

Everything here is deterministic from the road seed. Nothing depends on frame
time, camera state or the car's position.

---

## 1. Purpose

A guardrail that follows the road, is generated with it, survives re-basing,
windowing and backward generation, reads as a real barrier (visible, blocking,
closed at both ends), and keeps the car on the road. It exists in three
situations:

1. **Outside of tight curves** — the primary case.
2. **Inside of blind curves** — entered on a descending grade, so the inside
   cannot be cut.
3. **Both sides of one-lane (narrow) spans.**

## 2. Tunables (`road_config_t`)

| Field | Type | Default | Range | Meaning |
|---|---|---|---|---|
| `rail_enabled` | `bool` | `true` | — | master switch; `false` yields no rail on any side, from either source |
| `rail_density` | `float` | `0.6` | 0…1 (validated, NaN → default) | fraction of tight curves that get the *outside* rail |

Compile-time span grammar (`road.h`):

| Constant | Value | Meaning |
|---|---:|---|
| `ROAD_RAIL_LEAD` | 20.0 m | a span starts this far before the curve start / one-lane plateau |
| `ROAD_RAIL_TRAIL` | 15.0 m | and ends this far after it |
| `ROAD_RAIL_BLIND_GRADE` | 0.06 | a curve is *blind* when its entrance grade is ≤ −6 % |
| `ROAD_RAIL_STOP_FULL` | 1.05 m | wheel half-width 1.04 + 0.01 m; the clamp is `half_width − STOP` at **every** width |
| `ROAD_RAIL_MIN_CENTRE` | 0.30 m | eligibility: a rail exists only when `half_width − 1.05 ≥ 0.30` |
| `ROAD_RAIL_CLAMP_MIN_TOP` | 0.25 m | the clamp exists only where `guardrail_beam_top ≥ 0.25` (the visible interval) |
| `ROAD_RAIL_MAX_SPANS` | 48 | collector capacity |
| `ROAD_TIGHT_CURVATURE` | 1/50 m⁻¹ | tight-curve threshold (existing) |
| `ROAD_LANE_HALF_1` | 2.75 m | one-lane threshold (existing) |

`rail_density` affects the tight-curve draw only. Blind inside rails and narrow
corridors are unconditional (subject to `rail_enabled`).

## 3. Span rules

### 3.1 Curve spans

One decision per segment, taken right after the vertical setter so
`seg->start_grade` is final, in both `append_forward()` and
`prepend_backward()`:

- **Outside (tight):** `|κ| ≥ ROAD_TIGHT_CURVATURE`. Side:
  `κ > 0` (right turn) → **left**; `κ < 0` → **right**.
  Railed only when the dedicated stream draws `unit < rail_density`.
- **Inside (blind):** `|κ| > 1e-5` **and** `start_grade ≤ −0.06`. Side:
  `κ > 0` → **right**; `κ < 0` → **left**. Always railed when enabled.

A curve can be both tight and blind; the two rules then contribute one side
each (a fully railed corner).

The decision is stored as a bit mask on the segment
(`rail_side`: bit 1 left, bit 2 right). Span extent is
`[start_s − 20, start_s + length + 15]`.

### 3.2 Narrow (one-lane) spans

Derived on demand from the existing world-anchored width cycle — not stored.
A NARROW event that is not `suppressed` contributes, on **both** sides,
`[s0 + flat_start − 20, s0 + flat_start + flat_len + 15]`. No RNG is consumed.
The sampler tests cycles `k−1, k, k+1` around the sampled world s; the collector
takes `k0 = floor((world_s0 − phase) / cycle)` and `k1` from the second query
endpoint and scans `k = k0−1 … k1+1`, so a long query spanning several cycle
boundaries is never missed.

`RACING_DIAG_NARROW` overrides `width_at()` directly for world 0..290 m and
creates **no width event**, but it does not touch the cycle table this rule
reads. The forced 0–290 m section therefore has no rail, while the cycle-based
rails still exist in that build: measured with `-DRACING_DIAG_NARROW`,
`road_rail_collect(400, 1100)` returns 3 spans including 797.4..1064.3 on both
sides (`kind = NARROW`) and `road_sample(900)` reports **1.150** on both sides
(2.20 − 1.05; it was 5.650 under the 0.6.0 rule that subtracted 1.05 m from
the shoulder edge). The
frozen one-lane pins capture at 2 s (world ~12–66 m) with a 700 m draw window,
i.e. more than 700 m before that rail, so they are unaffected.

### 3.3 Dedicated streams

| Stream | Seed |
|---|---|
| forward rail | `seed ^ 0x2C1B3C6D` (1 if it would be 0) |
| backward rail | `seed ^ 0x5C6E9A31` (1 if it would be 0) |

Draw: `unit = (r >> 8) / 2^24`, `hit = unit < clampf(density, 0, 1)`, where `r`
comes from the existing xorshift32 `rng_next()`. The stream is advanced for
every tight curve **even when `rail_enabled` is false**, so toggling the switch
cannot shift later decisions. Forward and backward generation use separate
streams, so a reverse drive cannot move a forward rail.

## 4. Measured behaviour at `ROAD_SEED = 0x5EED2A17` (DEFAULT map)

### 4.1 Tight/blind curve candidates (forward)

| # | start_s | length | κ | entrance grade | tight | blind |
|---|---:|---:|---:|---:|:--:|:--:|
| – | 374.277 | 58.902 | +0.00935 | 0.000 | no | no |
| – | 502.856 | 79.732 | −0.01388 | −0.120 | no | **yes** |
| 1 | 608.589 | 55.297 | +0.03821 | +0.120 | **yes** | no |
| – | 716.312 | 83.988 | −0.00922 | +0.120 | no | no |
| – | 870.190 | 90.404 | −0.01339 | +0.120 | no | no |
| – | 1162.594 | 80.359 | +0.01229 | 0.000 | no | no |
| 2 | 1290.424 | 32.258 | +0.02435 | −0.120 | **yes** | **yes** |
| 3 | 1365.039 | 25.404 | −0.03717 | −0.120 | **yes** | **yes** |
| 4 | 1416.444 | 58.988 | +0.03439 | +0.120 | **yes** | no |
| – | 1595.402 | 82.643 | +0.00848 | +0.120 | no | no |
| – | 1753.442 | 58.163 | −0.00914 | +0.120 | no | no |
| 5 | 1811.605 | 39.701 | −0.02289 | +0.120 | **yes** | no |
| 6 | 1851.306 | 52.944 | +0.03574 | +0.120 | **yes** | no |
| 7 | 1904.250 | 48.755 | +0.03676 | +0.120 | **yes** | no |
| 8 | 2285.020 | 44.267 | −0.02992 | −0.120 | **yes** | **yes** |
| 9 | 2374.629 | 53.730 | −0.02318 | −0.120 | **yes** | **yes** |
| 10 | 2480.6 | 112.8 | −0.00877 | −0.120 | no | **yes** |

### 4.2 Tight-curve draw (17 candidates, density 0.6)

| draw | curve s | unit | rail? |
|---:|---:|---:|:--|
| 1 | 608.589 | 0.7152 | no |
| 2 | 1290.424 | 0.2765 | yes |
| 3 | 1365.039 | 0.5808 | yes |
| 4 | 1416.444 | 0.2976 | yes |
| 5 | 1811.605 | 0.7637 | no |
| 6 | 1851.306 | 0.5911 | yes |
| 7 | 1904.250 | 0.1295 | yes |
| 8 | 2285.020 | 0.4040 | yes |
| 9 | 2374.629 | 0.8789 | no |
| 10 | 2619.4 | 0.7195 | no |
| 11 | 2934.5 | 0.5034 | yes |
| 12 | 2960.5 | 0.6736 | no |
| 13 | 3017.3 | 0.6823 | no |
| 14 | 3075.2 | 0.4901 | yes |
| 15 | 3605.9 | 0.1864 | yes |
| 16 | 3704.3 | 0.9383 | no |
| 17 | 3764.7 | 0.9435 | no |

9/17 = 53 %, within one binomial standard deviation of the 60 % target.

### 4.3 Narrow span

One-lane plateau **817.413 … 1049.258** (231.845 m) → rail span
**797.413 … 1064.258** (266.845 m), both sides. This is the first forward rail
of any kind except the blind span at 482.856.

### 4.4 Collected intervals at `car_s = 1000`, request `[400, 2100]`

Sorted by `(s0, side)`, merged per side, kinds OR'd, then clipped to the visible
interval — **8 entries** (`s0/s1` is the grammar span, `v0/v1` what is clamped
and drawn):

| s0 | s1 | v0 | v1 | side | kind |
|---:|---:|---:|---:|:--:|---|
| 482.856 | 597.589 | 489.224 | 591.216 | left | INSIDE |
| 797.414 | 1064.257 | 803.781 | 1057.868 | left | NARROW |
| 797.414 | 1064.257 | 803.781 | 1057.868 | right | NARROW |
| 1270.424 | 1337.682 | 1276.792 | 1331.303 | left | OUTSIDE |
| 1270.424 | 1337.682 | 1276.792 | 1331.303 | right | INSIDE |
| 1345.039 | 1490.431 | 1351.406 | 1484.063 | left | OUTSIDE\|INSIDE |
| 1345.039 | 1405.444 | 1351.406 | 1399.063 | right | OUTSIDE |
| 1831.306 | 1968.005 | 1837.673 | 1961.620 | left | OUTSIDE |

The narrow pair is **whole at both one-lane widths** (4.4 and 4.0): eligibility
(`half − 1.05 ≥ 0.30`) never fails on the release grammar, whose narrowest road
is 4.0 m total (`half 2.00`). The 0.6.0 rule (`half − 1.35`) dropped or split
this pair into 10 spans; that count and the 12.15 / 7.16 m ramp pieces are void.
The visible-interval clip trims 6.4 m at each end (the beam reaches 0.25 m
above the road there), so the narrow pair is 254.087 m of drawn rail inside a
266.844 m grammar span. The splitter scans in `ROAD_RAIL_SCAN_STEP` 10 m
brackets and bisects `ROAD_RAIL_BISECT_STEPS` 8 times, so each boundary is
exact to 10/2^8 = **0.039 m** and the whole per-frame pass stays inside
`ROAD_RAIL_SAMPLE_BUDGET` = **320** `road_sample()` calls. The budget is a
**sampled bound and a cost target, not a proof over every start seed**: 192 is
the reference frame (`car_s` 1000, `ROAD_SEED`), 228 the worst of the
repository's host-tested 64-seed × 3-map sweep (seeds i·7919+1, `car_s` 0..3000
step 50; 201 at seed 21, `car_s` 2410 in a separate probe), and 279 / 281 over
12 000 seeds along the game's own `road_next_start_seed` orbit / an LCG
sequence — 288 and 290 in the reviewer's 4 000- and 12 000-seed random sweeps.
The game starts on arbitrary xorshift32 orbits, which no finite sweep
enumerates, so 320 is set above the measured worst with headroom; the host
sweep test fails if its measured worst frame exceeds it. The test's
`CHECK(worst == 228)` is labelled "worst of the sweep", not "the bound".

Curve #1 (608.589) draws no rail; its span 588.589…678.886 must be absent.

### 4.5 Re-base

At `car_s = 3400` the road re-bases by origin 2400 (`car_s` becomes 1000,
`world_offset` 2400). The span of curve #8 is `2265.020 … 2344.287` before and
`−135.0 … −55.7` after — same sides, same kinds, same length.

## 5. Geometry

| Constant | Value |
|---|---:|
| `GUARDRAIL_HEIGHT` (beam top above road, mid-span) | 0.75 m |
| `GUARDRAIL_BEAM_DEPTH` (beam face height) | 0.30 m |
| `GUARDRAIL_END_DEPTH` (beam top below the road at the ends) | 0.35 m |
| `GUARDRAIL_RISE` / `GUARDRAIL_SINK` | 12 m / 12 m |
| `GUARDRAIL_ROW_STEP` | 5 m (equals `ROAD_MESH_STEP`) |
| `GUARDRAIL_BEAM_THICKNESS` (top face) | 0.12 m |
| `GUARDRAIL_POST_SPACING` | 8 m |
| `GUARDRAIL_POST_MIN_TOP` | 0.25 m (= `ROAD_RAIL_CLAMP_MIN_TOP`, host-tested) |
| post width | 0.10 m |
| beam inner face | `±guardrail_rail_lateral(p.half_width)` = `±half_width` (the white line) |
| rail post centre | `±guardrail_post_lateral(p.half_width)` = `±(half_width + 0.02)`, 0.05 m half-width |

Envelope (smoothstep, clamped):

```text
env(s) = smoothstep01((s - s0) / 12) * smoothstep01((s1 - s) / 12)
top(s) = -0.35 + 1.10 * env(s)      /* -0.35 at both ends, +0.75 mid-span */
bottom(s) = top(s) - 0.30
```

Both ends therefore sit **below the road surface** (top −0.35 m, bottom
−0.65 m): the barrier rises out of the ground, closes the span and leaves
nothing to drive around. Maximum slope is `1.10 × 1.5 / 12 = 0.1375 m/m`
(7.8°). Posts are placed at `s0 + i·8` and skipped while
`top(s) < GUARDRAIL_POST_MIN_TOP`, which is `ROAD_RAIL_CLAMP_MIN_TOP` (0.25 m,
asserted equal by `test_guardrail_post_floor_is_the_clamp_floor`): a post
exists only where the beam is visible, so no post can stand alone ahead
of the first drawn band when a split starts the beam at `v0`.

The post is a 0.10 m wide quad centred 0.02 m outboard of the beam plane, so
its inner face sits **3 cm inboard** of the plane (`half_width − 0.03`) while
the wheels stop 1 cm short of it (`half_width − 0.01`: clamp = `half_width −
1.05` with a 1.04 m wheel reach). A car pinned exactly at the clamp can
therefore overlap the post's inner 2 cm visually; the beam itself is never
penetrated. This is a deliberate small tolerance, not a collision volume.

The plane is the **asphalt** edge (`half_width`), not the shoulder edge
(`edge_half_width = half_width + 4.5`): the rail now sits on top of the white
line, between the off-road shoulder and the road, which is Part D's brief.
`guardrail_gu.c` is required by a source gate in `scripts/test.sh` to call both
helpers with `p.half_width` and to contain no `edge_half_width` at all.

The 12 m rise/sink is drawn on 5 m rows, so the curve is a chord over each row
and the rendered envelope deviates from the smooth one by up to **8.4 cm** on
the first row (maximum of `1.1·smoothstep` at the steepest part). The "≤ 3 cm"
figure elsewhere in this file is a different error: the interpolation of the
*road height* between the rail's span-anchored rows and the road's own car-
anchored rows.

Rows use the VISIBLE interval's grid `v0, v0+5, … , v1` with the last row
clamped to `v1` (`guardrail_band_range()`), so the beam starts and ends exactly
at the visible interval the clamp uses — iterating the grammar span `s0…s1`
would draw bands where the beam is below the road. The envelope is still
evaluated on the grammar span `[s0, s1]`, so a run never re-buries its own
ends. Both ends of the beam and the road's own 5 m strip grid are linear
interpolations of the same road samples; the residual height mismatch is
≤ 3 cm (curvature of the 26 m crest profile), invisible at a 0.75 m beam.

## 6. Materials and colours

Every colour is built with a local `GUARDRAIL_RGB(r, g, b)` macro that packs
`0xFF | b<<16 | g<<8 | r`, the same layout as `render.c`'s `RGB()` (the GU
vertex word is **ABGR**). Alpha is **always 0xFF**: a bare 24-bit literal
carries alpha 0 and would render the rail and the flash fully transparent under
the frame-wide `GU_BLEND`.

| Part | Colour | Why |
|---|---|---|
| Beam (inner face + top face) | RGB(70, 96, 120), packed `0x786046` | painted steel blue-grey; every channel below `is_bright()`'s >160 threshold and `abs(r-g) = 26` keeps it out of `is_asphalt()`, so the frozen 0.5.0 pixel pins cannot see it. Chromatic on purpose: a neutral grey collides with the bilinear-filtered asphalt/edge-line gradient. `GU_COLOR_8888` is **ABGR** (`b<<16 | g<<8 | r`) |
| Rail post | RGB(56, 78, 98), packed `0xFF624E38` | dark steel with a fog-safe chroma: `b−g = 20` (grows toward the sky's 29, never below) and `g−r = 22`, so `is_asphalt` rule 2 (`b−g ≤ 6`, `|r−g| < 25`) cannot match at any fog level; rule 1 also needs `b−g ≤ 18`. Not bright, not amber, not ground |
| Contact flash | RGB(216, 40, 24), packed `0x1828D8` | crimson; `is_amber()` needs g > 80, `is_bright()` needs all channels > 160 — both false |

Fog (70…420 m, colour `COL_SKY` 135, 206, 235) pulls distant rail pixels toward
a colour whose red channel is 135, so no fogged rail can ever classify as
`is_bright()` either, and the beam is not `is_asphalt()` either (`abs(r−g)=26`
> 25). The rail's own probe keys on `|RGB − (70,96,120)| ≤ 8` per channel.

The 2D flash is 6 vertices at `x 0…6` (left) or `x 474…480` (right),
`y 76…230`, drawn with `GU_TRANSFORM_2D`. No orthographic matrix is needed:
`GU_TRANSFORM_2D` bypasses the MVP matrices and the screen mapping comes from
`sceGuOffset(2048−240, 2048−136)` / `sceGuViewport(2048, 2048, 480, 272)` in
`gfx_init()`; `draw_hud()` already draws this way with a perspective projection
installed.

**The flash is the LAST draw of the content block**, in its own
`guardrail_draw_flash(car)` call after `draw_posts()`, `draw_car()`,
`draw_hud()` and `draw_minimap()`, and before the result/menu overlays. Until
the 0.8.0 paint order fix it was emitted inside `guardrail_draw()`, i.e. mid 3D
block, so the passes drawn after it painted over the 6 px column. **The
occluder is `draw_posts()`** — the roadside marker posts at `edge + 0.6 m`,
drawn immediately after the rail. Isolated on the shipped build by moving the
call either side of `draw_posts()`: **772 px before it, 924 after it**. Signs
are not the cause (`signs_draw()` runs before `guardrail_draw()`), and the
HUD/minimap never reach x 474…480. Measured: 924 px on four frames and
**765 px at 49.3 s** (the Part D audit saw 787 px at 48.5 s on its build) while
a post crossed the column. `scripts/check-racing-decal-order.py` rule 4 pins
the order (flash after posts/car/HUD/minimap and with no draw before the
result/menu overlays) and `scripts/test.sh` runs it, with self-test mutations
(g), (h) and (i) that move the call back, delete it, and insert a draw after it.
Drawn last, the count is exact per frame and scene-independent, and the probe
asserts **every** flash frame at 924 px.

Roadside marker posts stay at `edge_half_width + 0.6` (= 10.1 m), 0.6 m outside
the beam plane. Depth writes are **off** at the rail hook (`render.c` sets
`sceGuDepthMask(GU_FALSE)`; only the ground and the shadow write), so the beam
and those posts are depth-tested but neither occludes the other; overlaps follow
draw order, and the rail's own posts come after the beams in the same batch.

## 7. Physics contact

- Clamp: per side, `min(plain_edge, rail)` where `plain_edge` is the drivable
  edge (`edge_half_width`) less `CAR_EDGE_MARGIN` (0.4 m), with
  `rail = road_rail_allowed(half_width) ? half_width − road_rail_stop(half_width)
  : 0`. `road_rail_stop()` returns `ROAD_RAIL_STOP_FULL` (1.05 m, the wheel
  half-width 1.04 + 1 cm) at **every** width, and eligibility is the separate
  `half_width − 1.05 ≥ ROAD_RAIL_MIN_CENTRE` (0.30 m) test; `CAR_RAIL_MARGIN`
  is an alias of the same constant (a host test asserts the equality).

  | half_width | allowed? | rail = half − 1.05 | outer wheel face = rail + 1.04 | plane − face |
  |---:|---|---:|---:|---:|
  | 5.00 (2-lane) | yes (3.95 ≥ 0.30) | **3.95** | 4.99 | **0.01 m** |
  | 2.20 (one-lane 4.4) | yes (1.15 ≥ 0.30) | **1.15** | 2.19 | **0.01 m** |
  | 2.00 (one-lane 4.0) | yes (0.95 ≥ 0.30) | **0.95** | 1.99 | **0.01 m** |
  | 1.35 | yes (0.30 = 0.30) | 0.30 | 1.34 | 0.01 m |
  | 1.30 | **no** (0.25 < 0.30) | 0 | — | no rail: the plain edge (5.40 m) |

  The 0.6.0 rule made the *stop itself* shrink (`half − 1.35`), which at half
  2.20 put the clamp at 1.35 and the wheel face at 2.39 — 0.19 m **past** the
  plane, i.e. the car clipped the rail. `ROAD_RAIL_STOP_MIN`,
  `ROAD_RAIL_CLEARANCE` and the 2.40 m knee are deleted.

- **One predicate, three consumers.** `road_rail_ok(p, s, s0, s1)` is the only
  clamp/draw decision: the sampler (`road_sample`), the collector
  (`road_rail_collect`) and the renderer (`guardrail_gu.c`) all call it. It
  takes the already-computed sample and **never** calls `road_sample()`, so the
  `road_sample → predicate` path is not a cycle. The `s0, s1` it is given is
  the **merged union** of every overlapping railed window touching `s`, never
  one window's own interval: a per-window envelope has already sunk where two
  railed windows overlap, while the merged beam is drawn at full height, which
  is a drive-through hole up to 12.7 m long. `road_sample()` builds its own
  candidate list (at most six windows per side) and unions it with
  `road_rail_union()`; it never calls the collector.
- **Rail-only effects are gated on `road_sample_t.rail_limit_left/right > 0`.**
  At the plain road edge the clamp is unchanged and nothing else happens: no
  scrub, no latch, no `yaw_rate = 0`, `slip` is not zeroed.
- On rail contact: `slip` toward the wall zeroed, `yaw_rate = 0`, one
  `×0.85` speed scrub per contact episode (`CAR_RAIL_SCRUB`), and a
  `CAR_RAIL_HOLD` = 0.35 s latch exposed as `car_is_rail_hit()` /
  `car_rail_side()`.
- `long_accel` is recomputed after the scrub so telemetry sees it in the same
  frame.

## 8. Budgets

| Item | Value |
|---|---:|
| Rail vertices, nominal worst case (`car−110 … car+425`) | 3 360 |
| Flash vertices | 6 |
| Hard cap `GUARDRAIL_MAX_VERTS` | 3 456 (55 296 B) |
| Allocation | two `sceGuGetMemory` blocks per frame: `rail_len` for the 3D batch and 6 for the flash, both `NULL` checked |
| Draw calls | one 3D batch (rail) + at most one 2D batch (flash) |
| Dynamic memory | none in the frame loop |

`guardrail_vertex_cost(-110, 425) == 3360` is pinned by a host test, together
with `+ GUARDRAIL_FLASH_VERTS <= GUARDRAIL_MAX_VERTS`. The flash has its own
6-vertex block now (it is drawn after the HUD), so the 3D selection no longer
reserves room for it: `guardrail_select_spans()` gets `reserve = 0` and may use
the whole cap. The hard cap still bounds the display list; the extra 96 bytes
of flash block are not part of it.

The 3 360 is **nominal**, not the absolute bound: a span that only overlaps a
query edge still costs its full grammar length, so the cost is an upper bound
on what is emitted. The absolute bound is the hard cap:
`guardrail_select_spans()` ranks spans by `v0` ascending (nearest the car
first), hides a span whose visible interval ends at or behind the eye, and
keeps whole spans until `GUARDRAIL_MAX_VERTS` is reached, so an
overflow drops the **far ahead** end and keeps the spans around the car.
Physics clamps on the near span whether it is drawn or not, so the opposite
order would put an invisible wall at the car; a dropped far span is fogged and
distant. Whole spans are dropped, never a half-drawn band.

The renderer iterates and emits on `[v0, v1]` but evaluates `beam_top` on
`[s0, s1]` — a sub-span would re-bury its own ends — and `emit_band()` skips a
band unless **both** endpoints satisfy `road_rail_ok()`, so no band straddles
the eligibility or visibility boundary.

### Probe pin tolerance

`scripts/check-racing-rail.py` asserts two kinds of value differently, and the
split is deliberate (F1 re-recorded every number below on the shipped 0.8.0
tree with two `--report` passes, reproduced bit-identically on a quiet host;
under load the side counts drift by ≤ 6 px, inside `BAND_SIDE` 120):

- **Exact, discrete properties.** Frames that show the rail (5/5), frames that
  flash (6/6 — five 0.5 s samples plus 49.3 s, the frame a marker post used to
  cover), flash pixels (**924 on every frame**, a solid 6×154 rectangle drawn
  last, so scene-independent), left-edge flash pixels (0), base red-proof
  frames (0/0).
  A missing rail or a changed beam colour moves these by their whole value, so
  exactness costs nothing.
- **Banded rasterisation counts.** Per-side counts (left **1209 ± 120**, right
  **815 ± 120**) and cluster centroids (154.8 / 286.1 ± 4 px) on the fixed 47 s
  frame. `present_best` and `present_worst` are **reported only, never pinned**:
  they are the argmax/min over five chaotically phase-sensitive frames and drift
  with host load (2032/1842 measured on a quiet host, 2031/1840 seen under
  load). The probe asserts only `best / worst ≤ PRESENT_RATIO_MAX` (2.0), which
  a missing span or a wrong side moves far outside.

The reason: the game derives `dt` from wall time, so a change in per-frame cost
— a new feature's draw calls — perturbs the simulation and shifts the rendered
scene by about a pixel, and the beam's thin diagonal edges re-rasterise.
Measured across builds: `main 6175325` 766 / 238 / 528 (centroids 135.3 /
311.3), `main + feat/roadsigns 6b20e9b` 773 / 242 / 531 (137.7 / 311.7).

The `BAND_SIDE` 120 is wider than ±10 % on the right cluster (14.7 % of 815),
so the plan's band policy requires the mechanism DEMONSTRATED, not named. The
boot Cross tap moved by one frame (±16.7 ms) on the shipped build gives:

| tap | best | left | right | centroids |
|---|---:|---:|---:|---|
| 0.5000 s (shipped) | 2032 | 1209 | 815 | 154.8 / 286.1 |
| 0.4833 s (−1 frame) | 2039 | 1196 | 832 | 154.7 / 286.6 |
| 0.5167 s (+1 frame) | 2046 | 1186 | 836 | 155.1 / 285.9 |

At most 23 px of side-count swing and 0.6 px of centroid swing, so 120 is 5.2×
the demonstrated mechanism and 4 px is 6.7×. Both are still an order of
magnitude below the failure they catch: a rail-less build reports 0–7 stray
fog pixels, never 800+.

## 9. Determinism and invariants

- Curve rails depend only on the seed, the validated config and the generation
  order. Narrow rails additionally read the live ring through
  `horizontal_max_curvature()`, which only `suppress`es a one-lane event that
  would contain a tight curve; they are stable for a given ring and shift with
  it at the window edge, and physics and rendering always agree because both
  use the same cached `width_cycle_get()`.
- `road_update()` never regenerates a retained segment: driving back and forth
  inside the ±1200 m window cannot move a rail.
- Re-base translates `start_s` uniformly; `rail_side` rides on the segment and
  the narrow windows are world-anchored, so both are invariant.
- Backward generation has its own stream and is reproducible at a fixed seed.
- `road_rail_collect()` sorts by `(s0, side)`, merges touching/overlapping
  same-side intervals **first** and only then clips each merged span to its
  visible interval `[v0, v1]` by bisection to the predicate crossing
  (`ROAD_RAIL_SCAN_STEP` 10 m brackets, `ROAD_RAIL_BISECT_STEPS` 8 steps, i.e.
  0.039 m). Clipping first would leave the two pieces of a merged
  pair carrying their own `s0/s1` and a later merge would re-collapse or
  duplicate them. Emitted spans are still never clipped to the query, so the
  rise/sink envelope is never cut.
- The clip is **split, not bridged**: `road_rail_visible_runs()` returns every
  disjoint run of the predicate, so a false gap inside one grammar span emits
  one span per run and the gap is drawn nowhere. Bridging the two runs into a
  single `[v0, v1]` would draw bands where `rail_limit_*` is 0 — the same
  drive-through class as the merged-union hole. No release span has a gap
  (eligibility is `half ≥ 1.35` and the narrowest road is `half 2.00`, and the
  envelope is unimodal), so the 8-span counts are unchanged; a synthetic stub
  predicate with a gap is what pins the splitter.

## 10. Orchestrator rulings (binding)

These were the 0.6.0 cycle's rulings. **The 0.8.0 plan
([`docs/racing-080-PLAN.md`](../../../docs/racing-080-PLAN.md), Part D)
supersedes the first three**; the fourth still holds.

- **No new `.c` or `.h` files.** *Superseded in 0.8.0:* `guardrail_env.h` was
  added so that `road.c` (the clamp) and the guardrail model (the draw) inline
  **one** rise/sink formula. It is header-only, has no PSP headers and no
  `road_*`/`car_*` symbols, so `tests/test_racing_rail.c` still links
  `guardrail.c` alone.
- **`racing/render.c` is no longer frozen in 0.8.0.** It has two entry points
  now: `guardrail_draw(car, settings)` draws the beam and posts, and
  `guardrail_draw_flash(car)` draws the contact flash at the END of the content
  block (after `draw_posts()`, the car, the HUD and the minimap), so no 3D pass
  - in particular not the marker posts, the isolated occluder - can paint over
  it. Both live in `guardrail_gu.c`; the order is pinned by
  `scripts/check-racing-decal-order.py` rule 4.
- **`docs/RACING.md`, `docs/HANDOFF.md` and `README.md` are frozen** for this
  branch; this file is the durable rail documentation. *Superseded in 0.8.0 for
  `docs/RACING.md` §2.18 only*, which D4 updates in the same commit as this
  file; `HANDOFF.md` and `README.md` stay with the integration pass.
- **`scripts/check-racing-050.py` is not editable here.** If one of its pinned
  constants moves, record the old/new numbers here and escalate — never re-pin
  silently and never weaken a behavioural assertion.

## 11. Verification anchors

* `scripts/check-racing-rail.py` (one 54 s session per EBOOT; F1 re-measured
  every number on the shipped 0.8.0 tree with two `--report` passes,
  reproduced bit-identically on a quiet host, ≤ 6 px side drift under load):
  - absent at 2/3/4 s: max 7 classifier pixels (≤ 20);
  - present at 45–49 s with RIGHT held from 44 s: 5/5 frames ≥ 200 px, side
    counts (quiet host) 1917 / 1962 / 2024 / 2032 / 1842 — the per-frame counts
    are reported, not pinned, and drift with host load; 1209 px left of x=240
    (centroid 154.8), 815 px right (centroid 286.1) on the fixed 47 s frame;
  - flash from ~45.2 s to ~51.5 s while the clamp holds: **924 px on the right
    edge in all six frames** (47.0, 47.5, 48.0, 48.5, 49.0, 49.3 s), 0 on the
    left. 49.3 s is the frame a roadside marker post used to paint over the
    flash (765 px before the paint-order fix; 772 with the call placed just
    before draw_posts());
  - `--base-eboot` red proof: 0 qualifying frames on the pre-rail build
    67ba94f (203 431 B, SHA-256 `89b765e3…`), which also measures the same
    ≤ 7 stray fog pixels.
* With the rail, the un-steered car is caught at the first curve and reaches
  the one-lane corridor (rail span 797.4..1064.3 m) at ~44 s; the clamp holds
  it there for ~7 s (the flash window) and the rail leaves the frame at
  ~51.5 s. The probe's times come from that trajectory, not from a free-speed
  estimate.
* Host tests: `tests/test_racing.c` (config, spans at `ROAD_SEED`, determinism,
  density, per-map eligibility, re-base, backward generation, clamp/scrub/
  latch/reverse, plain-edge silence, the seed-swept visibility budget, and the
  post floor == clamp floor equality) and `tests/test_racing_rail.c` (envelope,
  monotonicity, posts, 3360-vertex budget).

## 12. Known limitations

- A stretch regenerated after being dropped out of the ±1200 m window is built
  by the *backward* grammar, so its rails follow the regenerated road and need
  not match what was seen before. Inside the window nothing moves.
- The clamp uses the axis-aligned body/wheel half-width; a car at full slip
  (0.45 rad) can visually graze the beam before the clamp bites.
- The narrow corridor's rail follows the 30 m width ramp, so the wall converges
  and can push the car sideways at speed (one scrub per episode).
- No vertical collision: the clamp is lateral regardless of `air_height`.
- The 12 m rise/sink taper used to be visual only: the clamp was active over
  the whole grammar span, including the ends where the beam is below the road,
  so a car entering there hit an invisible wall. **0.8.0 removed that class**:
  the clamp and the draw both live on the visible interval `[v0, v1]`, where
  the beam is at least 0.25 m above the road. The residual is a *cue* rather
  than a wall: at `v0` the beam is only 0.25 m high (below the ~0.6 m wheel
  top), so the first contact is against a curb-height sliver and the full
  0.75 m arrives 6.4 m later (0.12 s at 55 m/s); the screen-edge flash carries
  the feedback. Raising `ROAD_RAIL_CLAMP_MIN_TOP` above 0.5 m in tuning would
  trade reach for a taller first contact.
- A sub-eligibility road (narrower than 2.70 m total) has **no rail at all**
  and falls back to the plain edge; the release grammar's minimum is 4.0 m, so
  this never happens in game.
- The one-lane clamps (1.15 / 0.95) are an exact fit for the wheel face. A
  car at full slip or on a tight radius can still graze the beam with its
  rotated footprint (0.067 m at 0.95 m on a 25 m radius, 0.185 m at 0.1 rad of
  slip): a visual clip, not a physics one. Marked for the hardware check.
