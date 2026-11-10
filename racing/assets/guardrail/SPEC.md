# Guardrail — specification (Racing 3D)

Status: **specification for implementation** (plan cycle 1). The authoritative
task breakdown is [`docs/guardrail-PLAN.md`](../../../docs/guardrail-PLAN.md);
this file is the durable reference for span rules, dimensions, materials,
tunables and the measured `ROAD_SEED` behaviour.

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
| `ROAD_RAIL_STOP` | 1.05 m | wheel half-width; physics clamp = `edge_half_width − STOP` |
| `ROAD_RAIL_MAX_SPANS` | 24 | collector capacity |
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
sides (`kind = NARROW`) and `road_sample(900)` reports 5.650 on both sides. The
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

Sorted by `(s0, side)`, merged per side, kinds OR'd — 8 entries:

| s0 | s1 | side | kind |
|---:|---:|:--:|---|
| 482.856 | 597.589 | left | INSIDE |
| 797.413 | 1064.258 | left | NARROW |
| 797.413 | 1064.258 | right | NARROW |
| 1270.424 | 1337.682 | left | OUTSIDE |
| 1270.424 | 1337.682 | right | INSIDE |
| 1345.039 | 1490.431 | left | OUTSIDE\|INSIDE |
| 1345.039 | 1405.444 | right | OUTSIDE |
| 1831.306 | 1968.005 | left | OUTSIDE |

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
| `GUARDRAIL_POST_MIN_TOP` | 0.05 m |
| post width | 0.10 m |
| beam inner face | `±road_sample(s).edge_half_width` |
| rail post centre | `±(edge_half_width + 0.02)`, 0.05 m half-width |

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
`top(s) < 0.05 m`, so no post floats at a sunken end.

The post is a 0.10 m wide quad centred 0.02 m outboard of the beam plane, so
its inner face sits **3 cm inboard** of the plane (`edge − 0.03`) while the
wheels stop 1 cm short of it (`edge − 0.01`, clamp = edge − 1.05 with a 1.04 m
wheel reach). A car pinned exactly at the clamp can therefore overlap the
post's inner 2 cm visually; the beam itself is never penetrated. This is a
deliberate small tolerance, not a collision volume.

The 12 m rise/sink is drawn on 5 m rows, so the curve is a chord over each row
and the rendered envelope deviates from the smooth one by up to **8.4 cm** on
the first row (maximum of `1.1·smoothstep` at the steepest part). The "≤ 3 cm"
figure elsewhere in this file is a different error: the interpolation of the
*road height* between the rail's span-anchored rows and the road's own car-
anchored rows.

Rows use the span's own grid `s0, s0+5, … , s1` with the last row clamped to
`s1`, so the envelope is exact at both ends. Both ends of the beam and the
road's own 5 m strip grid are linear interpolations of the same road samples;
the residual height mismatch is ≤ 3 cm (curvature of the 26 m crest profile),
invisible at a 0.75 m beam.

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
installed. The flash saves and restores depth test and fog and leaves texture
off (the caller's state).

Roadside marker posts stay at `edge_half_width + 0.6` (= 10.1 m), 0.6 m outside
the beam plane. Depth writes are **off** at the rail hook (`render.c` sets
`sceGuDepthMask(GU_FALSE)`; only the ground and the shadow write), so the beam
and those posts are depth-tested but neither occludes the other; overlaps follow
draw order, and the rail's own posts come after the beams in the same batch.

## 7. Physics contact

- Clamp: per side, `min(edge_half_width − 0.4, edge_half_width − 1.05)`, where
  the 1.05 is `CAR_RAIL_MARGIN`, defined as `ROAD_RAIL_STOP` (one source of
  truth; a host test asserts the two constants are equal).
  At the nominal 9.5 m edge that is **8.45 m** versus today's 9.1 m; the car
  body half-width is 0.90 m and the wheels reach 1.04 m, so the wheels stop
  exactly at the beam.
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
| Allocation | one `sceGuGetMemory` per frame for `rail + flash`, `NULL` checked |
| Draw calls | one 3D batch (rail) + at most one 2D batch (flash) |
| Dynamic memory | none in the frame loop |

`guardrail_vertex_cost(-110, 425) == 3360` is pinned by a host test, together
with `+ GUARDRAIL_FLASH_VERTS <= GUARDRAIL_MAX_VERTS`.

The 3 360 is **nominal**, not the absolute bound: spans are emitted whole (a
clipped span would cut the rise/sink envelope), so a span that only overlaps a
query edge still costs its full length. The absolute bound is the hard cap:
`guardrail_select_spans()` ranks spans by `s0` ascending (nearest the car
first) and keeps whole spans until `GUARDRAIL_MAX_VERTS − flash` is reached, so
an overflow drops the **far ahead** end and keeps the spans around the car.
Physics clamps on the near span whether it is drawn or not, so the opposite
order would put an invisible wall at the car; a dropped far span is fogged and
distant. Whole spans are dropped, never a half-drawn band.

### Probe pin tolerance

`scripts/check-racing-rail.py` asserts two kinds of value differently, and the
split is deliberate:

- **Exact, discrete properties.** Frames that show the rail (5/5), frames that
  flash (5/5), flash pixels (924 — a solid 6×154 rectangle, scene-independent),
  left-edge flash pixels (0), base red-proof frames (0/0). A missing rail or a
  changed beam colour moves these by their whole value, so exactness costs
  nothing.
- **Banded rasterisation counts.** Beam pixel count (766 ± 25), per-side counts
  (238 / 528 ± 10) and cluster centroids (135 / 311 ± 4 px).

The reason: the game derives `dt` from wall time, so a change in per-frame cost
— a new feature's draw calls — perturbs the simulation and shifts the rendered
scene by about a pixel, and the beam's thin diagonal edges re-rasterise.
Measured: `main 6175325` gives 766 / 238 / 528 (centroids 135.3 / 311.3);
`main + feat/roadsigns 6b20e9b` gives 773 / 242 / 531 (137.7 / 311.7). Re-pinning
the new numbers would break again on the next feature, so the counts are banded;
a rail-less build reports 0–13 stray fog pixels, two orders of magnitude below
the band.

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
- `road_rail_collect()` sorts by `(s0, side)` and merges touching/overlapping
  same-side intervals; emitted spans are never clipped to the query, so the
  rise/sink envelope is never cut.

## 10. Orchestrator rulings (binding)

- **No new `.c` or `.h` files.** The rail lives in `racing/road.c/h`,
  `racing/physics.c/h` and the five existing `racing/assets/guardrail/` files.
- **`racing/render.c` is frozen.** The only entry point is
  `guardrail_draw(car, settings)`; the flash is drawn from `guardrail_gu.c`.
- **`docs/RACING.md`, `docs/HANDOFF.md` and `README.md` are frozen** for this
  branch; this file is the durable rail documentation.
- **`scripts/check-racing-050.py` is not editable here.** If one of its pinned
  constants moves, record the old/new numbers here and escalate — never re-pin
  silently and never weaken a behavioural assertion.

## 11. Verification anchors

* `scripts/check-racing-rail.py` (one 54 s session per EBOOT):
  - absent at 2/3/4 s: max 11 classifier pixels (≤ 20);
  - present at 46–50 s inside the corridor: 5/5 frames ≥ 200 px, best 766,
    238 px left of x=240 (centroid 135), 528 px right (centroid 311);
  - flash at 51–53 s with Right held from 50.5 s: 924 px on the right edge in
    all 5 frames, 0 on the left;
  - `--base-eboot` red proof: 0 qualifying frames on the pre-rail build.
* Measured trajectory of the un-steered car (host simulation of the shipped
  `road.c` + `physics.c`): it leaves the road at the first curve and runs the
  16.5 m/s off-road cap — 380 m at 20 s, 734 m at 40 s, 835 m at 46 s, 899 m at
  50 s, corridor exit ~60 s. The probe's times come from this, not from a
  free-speed estimate.
* Host tests: `tests/test_racing.c` (config, spans at `ROAD_SEED`, determinism,
  density, per-map eligibility, re-base, backward generation, clamp/scrub/
  latch/reverse, plain-edge silence) and `tests/test_racing_rail.c` (envelope,
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
- The 12 m rise/sink taper is **visual only**: the physics clamp is active over
  the whole span, including the ~12 m at each end where the beam is below the
  road. A car entering there stops against an invisible wall that becomes
  visible a few metres later.
