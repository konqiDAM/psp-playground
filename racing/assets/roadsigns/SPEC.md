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
- `racing/render.c` **is edited** by 0.8.0: the `signs_draw(car, settings)`
  call moves **before** `guardrail_draw()` and `draw_posts()`, so both
  depth-tested passes occlude a panel and its depth-less ink (HIGH-2), and the
  same file carries Parts A and C of the release. `racing/CMakeLists.txt` and
  `scripts/test.sh` still list the sign sources unchanged.
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

Lateral placement (0.8.0): anchored at the verge, not centred —
`lateral = edge_half_width + SIGNS_PANEL_GAP + SIGNS_PANEL_W*s/2` on the
**right** shoulder. At 100 % that is `9.5 + 0.8 + 1.8` = **12.1 m**, so the
panel spans 10.3-13.9 m and its inner edge is `edge + 0.8`. The **inner edge
never moves with the scale**, which makes both clearances scale-invariant:

| Envelope | Position | Clearance from the inner edge |
|---|---:|---:|
| marker post (`edge + 0.6 + 0.08*sqrt(2)`) | edge + 0.7131 | **0.087 m** |
| car (`edge - 0.4 + 1.04` wheel face) | edge + 0.64 | **0.16 m** |

The fixed 0.70 m panel bottom is below the 1.38 m car roof, so the lateral
clearance is what keeps the panel out of the car. A post at the sign's own `s`
can therefore never sit behind the panel; a post at a different `s` can still
project near the panel on a curve, which is why the exact counters are compared
on the base and new EBOOT.

## 3. Panel

0.8.0 geometry: the panel is **1.5x** the 0.6.0 one and its bottom is fixed at
every scale, so the post and car clearance are scale-invariant. `s` is
`panel_scale` (0.60..1.60, clamped by `signs_place()`; non-finite -> 1.00).

| Quantity | Value |
|---|---:|
| Width | `SIGNS_PANEL_W * s` = 3.6 m at 100 % |
| Height | `SIGNS_PANEL_H * s` = 2.7 m at 100 % |
| Bottom above local road height | `SIGNS_PANEL_BOTTOM` = **0.70 m, fixed** |
| Top above local road height | `0.70 + H*s` = 3.40 m at 100 % |
| Centre above local road height | `0.70 + H*s/2` = 2.05 m at 100 % |
| Half-extents | `hu = W*s/2`, `hv = H*s/2` (in `sign_panel_t`) |
| Post | 0.14 m half-width, **top = `road_height + SIGNS_PANEL_BOTTOM`**, 4 side quads = 24 verts |

`signs_panel_frame()` is the **only** writer of the frame (`centre`, `right`,
`up`, `normal`, `hu`, `hv`). `signs_panel_corners()` and `signs_symbol_quads()`
both build through it, so the panel and its symbols can never scale apart;
`signs_panel_axes()` is a thin wrapper for the pre-0.8.0 call shape. The post
top is derived from the fixed bottom, **not** from `H*s` (which would put the
post below the panel at 60 %).

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
`C + u·hu·R3 + v·hv·U3 − 0.02·T3`, where `hu`/`hv` come from the panel frame.
The 0.6.0 code froze these at `1.2`/`0.9` (the old panel's half-extents), which
would have shipped a 1.5x panel with 1x symbols.

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
(`sceGuDepthMask(GU_FALSE)`), so the depth buffer normally hides a sign behind
the road. Three passes deliberately use `GU_TRUE` (no depth writes): the ground
layer, the lane-dash + car-shadow decal block in `render.c`, and the sign-ink
range in `signs_gu.c`. The 5 m road mesh can under-sample a crest lip between
rows, so `signs.c` also culls a sign geometrically unless **one** sight line is
clear:

- to the panel **bottom** (`road_height + SIGNS_PANEL_BOTTOM` = 0.70 m), which
  is fixed at every sign scale.

One target is enough because the bottom line is the **lowest** ray to the
panel: at every sample `line_clear()` evaluates, the ray to the bottom lies
below the ray to the centre or the top, so a blocked bottom line culls the
panel and a clear bottom line implies no higher line can be blocked. A centre
or top target would be dead code; the earlier three-target wording is
withdrawn.

This is a terrain test, not a general screen-overlap guarantee: a panel that
covers road *beyond* it is correct (the panel is nearer) and is allowed.
What it prevents is a panel being drawn through a crest, which is what the
Review Focus and the Task 1 test pin.

Each line is sampled every 10 m from `eye_s + 10` to `sign_s - 4`, where
`eye_s = car_s - (8.2 + 0.035|speed|)` and
`eye_y = road_height(eye_s) + 3.1 + 0.006|speed| + 0.6·suspension` — the three
terms are copied from `setup_3d()` (`racing/render.c:1268-1270`, the `eye.y`
assignment — the older `1158-1159` reference pointed into `draw_menu()`) so the
model sees exactly the camera the frame uses. A sample fails when the line is below
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

The shipped 0.8.0 panel is 3.6 × 2.7 m at 100 %, so every figure below is
**1.5×** the 0.6.0 table (2.4 × 1.8 m, half-extents 1.2 / 0.9). **Scale rule:**
all world sizes — and therefore all px figures — multiply by the sign-scale
setting `s` (0.60…1.60); the distances at which a feature resolves scale
inversely with `s`.

| Feature | World size | 30 m | 40 m | 100 m |
|---|---:|---:|---:|---:|
| Panel width | 3.6 m | 31.4 px | 23.5 px | 9.4 px |
| Panel height | 2.7 m | 23.5 px | 17.6 px | 7.1 px |
| Stroke across a horizontal bar (v, `0.15` half-thickness) | 0.405 m | 3.5 px | 2.6 px | 1.1 px |
| Stroke across a vertical bar (u, `0.15` half-thickness) | 0.540 m | 4.7 px | 3.5 px | 1.4 px |
| Crest baseline (`0.07`) | 0.189 m (v) / 0.252 m (u) | 1.6–2.2 px | 1.2–1.7 px | 0.5–0.7 px |

The normalised half-thickness scales anisotropically (`hu` = 1.8 m in `u`,
`hv` = 1.35 m in `v` at 100 %), so a diagonal bar is 0.405–0.54 m thick, not a
uniform 0.54 m. Result: the **panel** is recognisable from ~100 m (9.4 px wide,
full yellow), while the **symbol** resolves from ~60 m (1.8–2.4 px strokes) and
is clear at 45 m (2.4–3.1 px). The stroke is sub-pixel beyond ~105 m, so the
symbol can rasterise as a
broken outline there; that is expected and no probe asserts ink beyond ~50 m. This is the documented limit of a 480 × 272 screen; the brief's 30–100 m
range is met by "sign visible at 100 m, symbol legible from ~60 m" (the 0.6.0
wording said ~40 m; the 1.5x panel pushes that distance out by 1.5x).

## 8. Draw flow and budget

```text
signs_draw(car, settings):
    scale = (float)settings->sign_scale_pct / 100.0f   /* SIGN SIZE, 60..160 % */
    n = signs_place(car->s, car->speed, car->suspension, scale, list, SIGNS_MAX)
    for each sign:
        v = sceGuGetMemory(96 * sizeof(SignVtx)); if (!v) continue
        write post (24) + panel (6)                  -> n_base = 30
        write symbols (<= 36)                        -> n_all   <= 66
        sceGuDrawArray(GU_TRIANGLES, fmt, n_base, NULL, v)          /* base */
        sceGuDisable(GU_DEPTH_TEST); sceGuDepthMask(GU_TRUE)        /* decal */
        sceGuDrawArray(GU_TRIANGLES, fmt, n_all - n_base, NULL, v + n_base)
        sceGuDepthMask(GU_FALSE); sceGuEnable(GU_DEPTH_TEST)
```

`scale` is the persisted **SIGN SIZE** setting: `settings->sign_scale_pct`
(`RACING.CFG` version 2, byte 9; 60…160 %, step 10, default 100 %), read once
per frame by `signs_draw()` and clamped by `signs_place()` to
`SIGNS_SCALE_MIN`..`SIGNS_SCALE_MAX` (non-finite -> `SIGNS_SCALE_DEFAULT`), so
an out-of-range value can never reach the geometry. The file stays 16 bytes:
version 2 reuses version 1's reserved byte 9, which stays inside the CRC.

**Downgrading to 0.7.0 is data loss, not a compatibility nicety.** A 0.7.0
build rejects version 2, resets to defaults, and rewrites the file as version 1
on the next menu change: the sign size is gone, and a later 0.8.0 build
re-defaults to 100 %. Version 1 files still load in 0.8.0 (byte 9 must be 0) at
a 100 % sign scale, so the *upgrade* is lossless; only the downgrade is not.

The ink is a depth-less decal (0.8.0 Part C): the 0.02 m lift is co-planar
with the panel face at a 16-bit depth buffer, so the depth interaction is
removed rather than tuned. `render_frame()` draws the signs **before** the
guardrail and the marker posts, so both depth-tested passes occlude the panel
*and* its depth-less ink; `scripts/check-racing-decal-order.py` pins that
order and the state brackets.

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
from 0.1 s and captures 0.5 / 2.0 / 2.5 / 4.0 s. Region x 250..359, y 88..151
(the ROI's right edge 360 is exclusive; right shoulder, above the road, left of
the minimap). The right edge moved 356 -> 359 in 0.8.0: the 1.5x panel at 2.5 s
reaches x = 358, and a clipped
bbox silently understates the width (measured 30 px clipped vs 32 px whole).
Each capture runs in its own wiped `--memstick=` root into which the probe
writes **exactly one** `RACING.CFG` — a version 2 file at the requested scale,
or a deliberately rejected one (bad CRC, byte 9 = 200, version 1) for the
fallback cases — and never reads one back, so no config from an earlier case
and no probe-side version gate can change a capture. The root is therefore
never "empty": the config it holds is the *input* under test, and the
`--sign-scale` cases depend on it. The no-config path (a device's very first
run, pure defaults) is deliberately **not** exercised here;
`check-racing-030.py`'s save cases cover it.

The car position at each capture is calibrated from roadside posts visible in
the base build (measured x vs predicted x): s=40 post at x=327.5 (2.0 s,
`car_s = 18.7`), s=80 post at x=285.0 (2.5 s, `car_s = 29.8`), s=120 post at
x=291.0 (4.0 s, `car_s = 77.7`).

| Capture | Sign | Measured panel | yellow | ink |
|---|---|---|---:|---:|
| 0.5 s | 52.28 | bbox (285,97)-(300,108), 16 × 12 px, centre (292, 102) | 120 | 48 |
| 2.0 s | 52.28 | bbox (304,99)-(326,115), 23 × 17 px, centre (315, 107) | 242 | 88 |
| 2.5 s | 52.28 | bbox (327,100)-(358,122), 32 × 23 px, centre (342, 111) | 451 | 151 |
| 4.0 s | 112.28 | bbox (301,99)-(322,114), 22 × 16 px, centre (311, 106) | 223 | 79 |

These are the 0.8.0 measurements (1.5x panel at the verge) on the release
EBOOT. The boxes are 1.45-1.53x the 0.6.0 ones (11x8 / 15x11 / 21x16 / 15x10),
the centre moves +1..+2 px in x and 2-4 px **higher on screen** (the panel
centre rises 1.5 -> 2.05 m). The probe's `EXPECT` is tightened to ±3 px on the
centre, ±2 px on the size, and yellow floors at ~60 % of the measured values
(72/145/270/134); the ink floors are the 0.6.0 ones x1.5 (25 -> 37, 40 -> 60),
so a panel can lose ~40 % of its pixels before the probe fails; a range may
only ever be narrowed. A yellow *island* enclosed by the symbol is
part of the same panel and is ignored by the second-cluster rule. The
emission-window cull is not probed (a behind-camera sign is near-plane clipped
regardless); it is covered by the host test `test_window_and_reverse()`.

Measured RED state of the base EBOOT (`89b765e3…`, `67ba94f`, 203,431 B):
**0 exact-yellow pixels in the region at all four times**, probe exit 1.
