# Roadside warning signs — specification

Status: design frozen for implementation (plan: [`docs/roadsigns-PLAN.md`](../../../docs/roadsigns-PLAN.md)).
Portable model: `signs.h` / `signs.c`. GU renderer: `signs_gu.c`. Host tests:
`tests/test_racing_signs.c`. Probe: `scripts/check-racing-signs.py`.

## 1. Scope and frozen surfaces

- Symbol-only signs: every face is built from triangles/quads in C. No text, no
  texture, no PNG, no runtime file loading, no asset pipeline.
- No new `.c`/`.h` files: the feature lives in the existing four roadsign files
  (`signs.h`, `signs.c`, `signs_gu.h`, `signs_gu.c`) plus this document, the
  test file and the new probe script.
- `racing/render.c` is frozen; it already calls `signs_draw(car, settings)`
  after `draw_posts()`. `racing/CMakeLists.txt` and `scripts/test.sh` are
  frozen and already list the sign sources.
- Do not edit `docs/RACING.md`, `docs/HANDOFF.md`, `README.md`, or any existing
  `scripts/check-racing-*.py`. Only `scripts/check-racing-signs.py` is new.
- User mode only: no `pspkernel`, no `*ForKernel` imports.

## 2. Placement

A sign pair is emitted for an event at a fixed world position `event_s`:

| Sign | Position | Purpose |
|---|---|---|
| far | `event_s - 120 m` | advance warning |
| near | `event_s - 60 m` | confirmation |

Only signs with `8 m ≤ sign_s - car_s ≤ 240 m` are emitted, and only if the
road surface does not block the view (see §6). Event search window
`SIGNS_LOOKAHEAD = 260 m`, so the far sign first appears ~140 m ahead and never
pops in at the last moment.

| Event | Detector | Event position |
|---|---|---|
| heavy curve | `road_next_curve(car_s, 260, 1/50)` | the returned segment start (already exact) |
| crest | `road_next_crest(car_s, 260, 0.003)` | first ring segment with `start_s ≥ car_s + 1`, `start_s ≤ car_s + d + 5`, `grade_rate ≤ -0.003` (`road_segment_at`) |
| width ramp | local scan of `w(s+2) - w(s)` on a fixed 2 m lattice | first point with the slope `≤ -0.05` or `≥ +0.05`; only the nearest one |

The scan starts at the **smallest multiple of 2 m ≥ car_s + 1** and steps by 2,
so the sample points are fixed world positions and the detected ramp start does
not depend on `car_s` (a car-relative scan would sample `car_s+1, car_s+3, ...`
and shift the event by up to 2 m per frame). The ≤ 2 m quantisation of the
analytic onset is constant per ramp.

`road_next_narrow()` is **not called**: it reports a `car_s`-relative grid
value that crosses `ROAD_NARROW_WARN_HALF` mid-ramp, so using it to choose the
type would flip a visible panel from MERGE to ONELANE at
`car_s = crossing − 260`. The plateau target from the width scan carries the
same information at a fixed event position and distinguishes 1 from 2 lanes.

Sign type for a width ramp is decided from the event alone — `(direction,
lanes, t)` with `t = w(event_s + ROAD_WIDTH_RAMP)` and
`lanes = road_lane_count(t)`:
`ONELANE` iff narrowing and `lanes == 1` and `t ≤ ROAD_NARROW_WARN_HALF`;
otherwise `MERGE` (narrowing) or `WIDEN`. No `car_s` enters the decision, so a
panel can never change face while it is on screen. The boundary matters: a
one-lane setting of 6.0 m total has `half_width = 3.0`, exactly
`ROAD_NARROW_WARN_HALF`, but `road_lane_count(3.0) == 2`, so it shows a merge
panel, not a one-lane panel.

Why the detector distance is not used for crest: `road_next_crest` scans a
`car_s + 1 + 2k` grid that moves with the car, so its returned onset moves by
up to 2 m frame to frame and a sign would shimmer. The ring segment start and
the fixed 2 m lattice (multiples of 2 m in local `s`; the re-base origin is a
multiple of 80 m, which preserves the lattice) are fixed world points. The 2 m
quantisation can put the event up to one step before the analytic onset; that
offset is constant for a ramp.

Dedupe: candidates sorted by `s`, then any candidate within
`SIGNS_MIN_GAP = 18 m` of the previous kept sign is dropped, then the list is
truncated to `SIGNS_MAX_DRAWN = 6` (a defensive assertion of the 3-event
ceiling; the ceiling alone can never exceed it).

Lateral placement: `edge_half_width + 2.4 m` on the **right** shoulder
(`SIGNS_LATERAL`), i.e. 11.9 m at the default width; the 2.4 m panel spans
10.7-13.1 m, so its inner edge is 0.6 m outside the marker-post line
(`edge_half_width + 0.6` = 10.1 m) and the guardrail line (9.5 m) is farther
inboard. A post at the sign's own `s` can therefore never sit behind the panel.
A post at a different `s` can still project near the panel on a curve, which is
why Task 5 compares the exact counters on the base and new EBOOT.

## 3. Panel

| Quantity | Value |
|---|---:|
| Width | 2.4 m |
| Height | 1.8 m |
| Top above local road height | 2.4 m |
| Centre above local road height | 1.5 m |
| Bottom above local road height | 0.6 m |
| Post | 0.14 m half-width, 0.6 m tall, 4 side quads = 24 verts |

The panel is vertical in the road frame: width along the local right vector
`R3 = (-cos ψ, 0, -sin ψ)`, height along `U3 = R3 × T3` where `T3` is the 3D
tangent pitched by `atan(grade)`. The face normal is `-T3` (toward the driver).
At ψ = 0 and grade 0.12 the normal is `(0, -0.11912, -0.99288)` — it points
down because the road climbs away from the viewer.

## 4. Colours

| Name | RGB | Packed (GU 8888) |
|---|---|---|
| Face | (230, 180, 0) | `0xFF00B4E6` |
| Ink | (24, 24, 44) | `0xFF2C1818` |
| Post | (96, 96, 120) | `0xFF786060` |

**Hard rule: no sign pixel may qualify `is_bright` or `is_amber` at any
distance the renderer can produce** (`check-racing-050.py` compares exact
counters `default_roi=1543`, `highway_roi=1477`, `one_lane_amber=150`). The
face is outside both at every fog level: its blend is
`(230-95f, 180+26f, 235f)`, so `is_bright` would need f in (0.681, 0.737) —
308-328 m, beyond the 250.1 m maximum eye-to-sign distance
(`SIGNS_MAX_AHEAD` 240 + 8.2 + 0.035·55, so f ≤ 0.515) — and `is_amber` needs
g < 180, which never
happens (g starts at 180 and rises). The face *is* `is_accent` beyond 70 m
(r>200, g>180, b<100); no gameplay 3-D-view assertion counts accents, so that
is documented, not suppressed. The post was light grey `(176,176,176)` in the
first draft — `is_bright`, and it would add a few pixels to `default_roi`; it is
now steel-blue, whose fog blend `(96+39f, 96+110f, 120+115f)` never reaches
r > 160 or r > 200. The ink is `is_asphalt`-free at every fog level
(`b-g = 20+9f`), and the only asphalt checks in the probes are `>= 5000`
floors, never equalities. The probe matches the face exactly, which is valid
because `sceGuFog(70, 420)` leaves anything under 70 m unfogged.

All three colours are written **unshaded**: `signs_gu.c` must not reuse
`render.c`'s `shade_color`/`append_quad` factors (the roadside posts render at
0.72×, so a 208 red reads as 149); a shaded face would be ~(218,171,0) and the
exact match would fail.

## 5. Symbols

Panel-local coordinates `u,v ∈ [-1,1]` (`u` = driver's right, `v` = up). One
primitive: `bar(P0, P1, half_thickness)` = quad offset by ±`half_thickness`
along the normalised perpendicular of `P1-P0`. Local → world:
`C + u·1.2·R3 + v·0.9·U3 − 0.02·T3`.

| Type | Quads | Reading |
|---|---:|---|
| `CURVE_RIGHT` | 2 | chevron `>` (mirror for LEFT) |
| `MERGE` | lanes+2 | N lane bars + two inward wedges |
| `WIDEN` | lanes+2 | N lane bars + two outward wedges |
| `ONELANE` | 3 | one lane bar + exclamation mark |
| `CREST` | 3 | baseline + two bars forming a hump |

`lanes` is clamped to 1..4. Every symbol vertex satisfies `|u| ≤ 1`, `|v| ≤ 1`.

## 6. Visibility (never draw a sign through a rise)

Depth writes are enabled for the road, shoulder, post, sign and car passes
(`sceGuDepthMask(GU_FALSE)`; only the ground layer uses `GU_TRUE`), so the
depth buffer normally hides a sign behind the road. The 5 m road mesh can
under-sample a crest lip between rows, so `signs.c` also culls a sign
geometrically unless **both** sight lines are clear:

- to the panel centre (`road_height + 1.5`);
- to the bottom-inner corner (`road_height + 0.6`), the edge closest to the
  asphalt.

This is a terrain test, not a general screen-overlap guarantee: a panel that
covers road *beyond* it is correct (the panel is nearer) and is allowed.
What it prevents is a panel being drawn through a crest, which is what the
Review Focus and the Task 1 test pin.

Each line is sampled every 10 m from `eye_s + 10` to `sign_s - 4`, where
`eye_s = car_s - (8.2 + 0.035|speed|)` and
`eye_y = road_height(eye_s) + 3.1 + 0.006|speed| + 0.6·suspension` — the three
terms are copied from `setup_3d()` (`racing/render.c:1158-1159`) so the model
sees exactly the camera the frame uses. A sample fails when the line is below
`road_height(s) + 0.05`. The bottom-edge test is what prevents a panel from
being drawn over the crest road when its lower half is behind the rise; a sign
therefore emerges over a crest as the geometry allows, which is physically
correct and is not a pop-in bug.

Guard band: `sign_s - car_s ≥ 8` and
`eye_s = car_s - (8.2 + 0.035|v|)` keep every panel at least 16.2 m in front of
the eye (at v = 0; speed only increases it). The outer corner is 13.1 m
lateral; the car's lateral clamp is `edge_half_width - 0.4` = 9.1 m
(`physics.c:204-206`), so `cam_lat ≈ 0.55·9.1 ≤ 5.005 m` and the worst case is
18.105 m: `x = 240 + 261.3·18.105/16.2 = 532 px` (451 px with no shift). In GU
viewport coordinates that is x ≈ 2340, far below the 4095 drop threshold, so
the primitive is never dropped; in the extreme the panel is partly scissored
off the right edge.

## 7. Legibility (honest arithmetic)

Focal length `f = 136 / tan(27.5°) = 261.3 px`; screen size 480 × 272.

| Feature | World size | 30 m | 40 m | 100 m |
|---|---:|---:|---:|---:|
| Panel width | 2.4 m | 20.9 px | 15.7 px | 6.3 px |
| Panel height | 1.8 m | 15.7 px | 11.8 px | 4.7 px |
| Stroke across a horizontal bar (v, `0.15` half-thickness) | 0.270 m | 2.4 px | 1.8 px | 0.7 px |
| Stroke across a vertical bar (u, `0.15` half-thickness) | 0.360 m | 3.1 px | 2.4 px | 0.9 px |
| Crest baseline (`0.07`) | 0.126 m (v) / 0.168 m (u) | 1.1–1.5 px | 0.8–1.1 px | 0.3–0.4 px |

The normalised half-thickness scales anisotropically (1.2 m in `u`, 0.9 m in
`v`), so a diagonal bar is 0.27–0.36 m thick, not a uniform 0.36 m. Result: the
**panel** is recognisable from ~100 m (6 px wide, full yellow), while the
**symbol** resolves from ~40 m (1.7–2.4 px strokes) and is clear at 30 m (2.4–3.1
px). The stroke is sub-pixel beyond ~100 m, so the symbol can rasterise as a
broken outline there; that is expected and no probe asserts ink beyond ~50 m. This is the documented limit of a 480 × 272 screen; the brief's 30–100 m
range is met by "sign visible at 100 m, symbol legible from ~40 m".

## 8. Draw flow and budget

```text
signs_draw(car, settings):
    n = signs_place(car->s, car->speed, car->suspension, list, SIGNS_MAX)
    for each sign:
        v = sceGuGetMemory(96 * sizeof(SignVtx)); if (!v) continue
        write post (24 verts) + panel (6) + symbols (<= 36)
        sceGuDrawArray(GU_TRIANGLES, 8888|32BITF|3D, count, NULL, v)
```

| Item | Limit |
|---|---:|
| event kinds | 3 (curve, crest, width) → ≤ 6 signs |
| signs per frame | ≤ 6 |
| vertices per sign | ≤ 66 (cap 96) |
| vertices per frame | ≤ 576 |
| `sceGuGetMemory` calls | ≤ 6 |
| display-list memory | ≤ 14 KiB of 256 KiB |
| `road_sample` calls | ≈ 390 placement + ≤ 288 sight lines (6 × 2 × 24 at the 240 m clamp) ≈ 680 |
| worst-case placement cost | ≈ 1.0–1.5 ms at 333 MHz (≤ 9 % of a 16.7 ms frame); typical 1 event ≈ 0.35 ms |
| static RAM | 0 B |
| malloc in the frame loop | none |

`SignVtx` is checked at compile time in C99 with
`typedef char signs_vtx_size_check[sizeof(SignVtx) == 16 ? 1 : -1];`
(`_Static_assert` is C11 and the build is `-std=c99`).

## 9. Probe windows

`scripts/check-racing-signs.py` runs one PPSSPPHeadless session with Cross held
from 0.1 s and captures 0.5 / 2.0 / 2.5 / 4.0 s. Region x 250..356, y 88..151
(right shoulder, above the road, left of the minimap).

The car position at each capture is calibrated from roadside posts visible in
the base build (measured x vs predicted x): s=40 post at x=327.5 (2.0 s,
`car_s = 18.7`), s=80 post at x=285.0 (2.5 s, `car_s = 29.8`), s=120 post at
x=291.0 (4.0 s, `car_s = 77.7`).

| Capture | Sign | Measured panel | yellow | ink |
|---|---|---|---:|---:|
| 0.5 s | 52.28 | bbox (287,101)-(297,108), 11 × 8 px, centre (292, 104) | 70 | 15 |
| 2.0 s | 52.28 | bbox (307,105)-(321,115), 15 × 11 px, centre (314, 110) | 96 | 43 |
| 2.5 s | 52.28 | bbox (330,108)-(350,123), 21 × 16 px, centre (340, 115) | 219 | 68 |
| 4.0 s | 112.28 | bbox (303,105)-(317,114), 15 × 10 px, centre (310, 109) | 108 | 33 |

These are the Task 4 calibration measurements on the first green build. The
probe's `EXPECT` is tightened to ±3 px on the centre, ±2 px on the size, and
yellow/ink floors at 60 % of the measured values (42/58/131/65 yellow,
25/40 ink), so a panel can lose ~40 % of its pixels before the probe fails;
a range may only ever be narrowed. A yellow *island* enclosed by the symbol is
part of the same panel and is ignored by the second-cluster rule. The
emission-window cull is not probed (a behind-camera sign is near-plane clipped
regardless); it is covered by the host test `test_window_and_reverse()`.

Measured RED state of the base EBOOT (`89b765e3…`, `67ba94f`, 203,431 B):
**0 exact-yellow pixels in the region at all four times**, probe exit 1.
