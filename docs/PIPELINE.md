# PSP build pipeline (multi-agent)

The `psp-build-pipeline` skill captures the workflow used to build and release
Memory Test and Racing 3D. It turns one prompt into a gated chain of background
agents: plan → code → independent review → Beam Free audit → integration →
optional publish.

The main agent is the **orchestrator only**. It creates the worktree, asks the
gates, dispatches agents, verifies their model/session, and reports. It does
not write app code, review code, merge, or publish itself.

Skill files:

- [`.opencode/skills/psp-build-pipeline/SKILL.md`](../.opencode/skills/psp-build-pipeline/SKILL.md)
- [`references/agent-prompts.md`](../.opencode/skills/psp-build-pipeline/references/agent-prompts.md)

## How to invoke it

Say any of:

- “Run the `psp-build-pipeline` on <task>”
- “Use the PSP pipeline to build <task>”
- “Plan/code/review/publish <task>”

The agent loads the skill with `@psp-build-pipeline` or the `skill` tool.

## Flow

```text
preflight questions (auto-publish, publish scope, reviewers, Beam Free)
        |
   discovery: brainstorming with you
        |
   plan-only builder  (writing-plans → docs/<slug>-PLAN.md, stops)
        |
   planlink → plan gate: Beam Free / you / both / auto-approve
        |
   builder resumes: executing-plans + test-driven-development (RED→GREEN)
        |
   Reviewer 1 (requesting-code-review; red proof + fixes)
        |
   blocked? → builder (receiving-code-review; one-at-a-time RED→GREEN)
        |
   Reviewer 2 (Beam Free, read-only; plan + TDD audit)
        |
   [pipeline lock] integrator (merge to main, full suite)
        |
   PUBLISH GATE: pre-authorized or ask now
        |
   publisher (publish scope, per-app notes, docs status guard)
        |
   [pipeline lock released]
```

## Preflight questions

Before any agent is launched:

1. **Auto-publish?** `yes` = publish after the required reviews and
   integration pass; `no` = stop after integration and ask again.
2. **Publish scope?** `keep-all` = batch every app currently in `PSP/` plus
   the changed/new app (recommended; nothing is retired); `this-app-only` =
   publish only this app and accept that every other current build moves to
   `PSP_OLD/`; `ask` = decide at the publish gate.
3. **Reviewers?** `both` (Reviewer 1 + Beam Free), `only Reviewer 1`, or
   `only Beam Free`.
4. **Beam Free variant:** `opencode-go/beam-free#max` (recommended),
   `#high`, or the provider default.

The preflight also confirms that Reviewer 2 is Beam Free and read-only.

## Discovery — brainstorming

Before any worktree or builder is created, the orchestrator loads the
**`brainstorming`** skill and interviews you about the task: what exactly is
being built, why, target app/version, goals and non-goals, constraints,
success criteria, and anything ambiguous or risky. It plays the answers back
and gets your confirmation of a short brief.

The confirmed brief is recorded in the pipeline state and passed to the
builder. The plan is not started until the brief is confirmed.

## Plan gate

The plan-only builder uses the **`writing-plans`** skill and stops after
writing `docs/<slug>-PLAN.md`. After the plan commit, run
`planlink docs/<slug>-PLAN.md` and include the MD Viewer URL in the gate
message (and again in the final report if the plan is revised). The gate asks:

1. **Beam Free reviews the plan**
2. **I review the plan**
3. **Both** — Beam Free reviews first, then it still waits for you
4. **Auto-approve**

After approval, the same builder session loads the **`executing-plans`**
skill and implements the approved plan. A rejected plan goes back to the same
builder session. Maximum 2 plan cycles, then the pipeline stops and reports.

The plan builder must validate every numeric test scenario before writing it
down. A test that asserts impossible monotonicity or the wrong decay behaviour
is a plan defect, not an implementation detail; Beam Free will block it.

## Plan links

Plans are stored as `docs/<slug>-PLAN.md` and can be read directly in any
markdown viewer.
## Agent roles

| Phase | Agent | Model | Writes? |
|---|---|---|---|
| Discovery | Orchestrator + user (brainstorming) | Orchestrator's model | Brief only |
| Plan + code | Builder (one session, resumed; `writing-plans` → `executing-plans` + `test-driven-development`) | Orchestrator's model | Yes |
| Plan review | Beam Free reviewer | `opencode-go/beam-free#max` | No |
| Code review | Reviewer 1 (`requesting-code-review`) | Orchestrator's model | Yes, fixes |
| Fix loop | Builder (`receiving-code-review`) | Orchestrator's model | Yes |
| Final audit | Reviewer 2 (`requesting-code-review` + plan/TDD audit) | `opencode-go/beam-free#max` | No |
| Integration | Integrator | Orchestrator's model | Merge only |
| Release | Publisher | Orchestrator's model | Publish + docs |

Reviewer 2 is forced to Beam Free. The orchestrator verifies the child
session’s `providerID`, model `id`, and variant with:

```sh
opencode api get /api/session/<sessionID>
```

If the model is not `opencode-go/beam-free` (and the chosen variant), the
review is rejected — there is no silent fallback.

## Test-first implementation

The builder loads **`executing-plans`** and **`test-driven-development`**.
Every plan task is a TDD cycle:

1. **RED** — write the failing test/probe, run it, and record the expected
   failure (missing behavior/API, not a typo or environment error).
2. **GREEN** — write the minimal production code that makes it pass.
3. **REFACTOR** — clean up without adding behavior.
4. Run the full `./scripts/test.sh`, then commit.

No production code before RED. Code written before its test is deleted and
restarted. A test that passes immediately proves nothing. The builder reports
`RED command → observed failure`, `GREEN command → pass` and the full-suite
result for every task.

Test type by code:

- **Portable C logic** → host test in `tests/` or `host_test.c`.
- **GU/render/UI** → headless probe with pixel/behavior assertions; “non-blank”
  alone is not enough.
- **Hardware-only** → documented manual probe with the wrong behavior recorded
  before the change.
- **Packaging/integration** → the new assertion/gate fails before the fix,
  then `build.sh`, `psp-readelf` and `check-eboot-memsize.py` pass after.

If a task has no testable behavior, stop and report; do not code test-after.
Details and examples for each test type:
[`PSP-DEV-GUIDE.md` §1.7](PSP-DEV-GUIDE.md#17-test-first-implementation-tdd).

## Review request and reception

Reviewer 1 and Beam Free load **`requesting-code-review`** and follow its
`code-reviewer.md` template: description, plan/requirements, exact `BASE_SHA`/`HEAD_SHA`,
TDD evidence, the spec-as-vision rule and a **declined to judge** list.
Output is Strengths / Issues (Critical, Important, Minor) / Recommendations /
Assessment.

Reviewer 1 must verify **red proof**: each new test/probe fails on the base
commit or a targeted revert for the intended reason, then passes on final
HEAD. Missing red proof is a BLOCK; test-after is a BLOCK.

When a review blocks, the builder loads **`receiving-code-review`**:

1. read all findings without reacting;
2. restate each technical requirement;
3. verify against the codebase; push back with code/test evidence if wrong;
4. clarify unclear items before implementing anything;
5. fix one item at a time, each with a RED→GREEN test, then re-run the full
   suite.

No performative agreement or thanks. Maximum 2 review/fix cycles, then stop
and report. The orchestrator rules on every “declined to judge” line and
records the ruling.

Reviewer 2 gets the plan and the final diff, checks every plan item and the
TDD red/green evidence, and audits safety, real-PSP rules and docs. It is
read-only.

## Integration

After the required reviewers approve, the orchestrator acquires the shared
integration/publish lock:

```sh
./scripts/psp-pipeline-lock.sh acquire
```

Then the integrator merges the feature branch into an integration branch from
`main`, resolves shared-file conflicts by combining features, and runs the
full suite. It fast-forwards `main` only if everything passes. App-logic
changes are not allowed at this stage; they go back through review.

The lock serializes integration/publishing across parallel pipelines. Builds
and reviews may run in parallel; merges and publishes may not. If the lock is
held, wait for the other pipeline or stop and report. Release it with
`./scripts/psp-pipeline-lock.sh release` after the publisher finishes.

## Publishing

Publishing is allowed only after the required reviews and integration pass.

- **Auto-publish = yes:** the publisher runs after integration.
- **Auto-publish = no:** the orchestrator asks “Publish now?” and dispatches
  the publisher only on an explicit yes.

The publisher follows the preflight **publish scope**:

- `keep-all` (default): batch every app being published together, so
  publishing one app does not retire the others.
- `this-app-only`: publish only this task's app.
- `ask`: stop at the gate and confirm the shape with the user.

Use per-app versions (`PSP_VERSION_<app>`) and per-app notes
(`PSP_NOTES_<app>`). `PSP_NOTES` is only the fallback, so one app's release
note does not leak into another app's `CURRENT.md`.

Example batch:

```sh
PSP_APPS="racing wifi" \
PSP_VERSION_racing=0.3.0 \
PSP_VERSION_wifi=0.1.0 \
PSP_NOTES_racing="Racing 3D 0.3.0: D-pad, minimap, G-meter, settings" \
PSP_NOTES_wifi="WiFi Connection 0.1.0: unchanged" \
./scripts/publish-eboot.sh
```

**Docs status guard:** the publisher reads the current status text before
editing, never downgrades a hardware-validated build to “pending”, and sweeps
`README.md`, `docs/HANDOFF.md`, `docs/RACING.md`, `docs/WIFI.md`,
`docs/ICONS.md`, `docs/MEMTEST.md` and `docs/DESIGN.md` for stale versions,
hashes, publish/backup states and “last validated” claims. Historical plan
files stay historical.

The publisher verifies the remote EBOOTs, `CURRENT.md` and `PSP_OLD` history,
then updates the docs and commits the doc update on `main`. It never pushes and
never deletes history.

## Gates the builder must run

```sh
./scripts/test.sh
./scripts/build.sh                    # 0 warnings
psp-readelf -x .rodata.sceResident ... # no *ForKernel
./scripts/test-ppsspp.sh
./scripts/check-eboot-memsize.py
python3 scripts/check-pipeline-policy.py  # TDD/review policy anchors
```

## Scope and limits

- One build per invocation. Run the skill again for a second task; each run
  gets its own worktree, so builds/reviews can run in parallel.
- Only one pipeline may integrate/publish at a time. The shared lock
  (`scripts/psp-pipeline-lock.sh`) is held from the integrator through the
  publisher; parallel merges/publishes are not allowed.
- Isolated worktree per task. App work never happens in `main` directly; only
  the integrator fast-forwards `main` and the publisher builds/releases there.
- Implementation is test-first (`executing-plans` + `test-driven-development`);
  Reviewer 1/2 use `requesting-code-review`; blocked reviews go through
  `receiving-code-review` before a fix.
- Every plan created/revised gets its `planlink` MD Viewer URL in the
  user-facing reply, the plan gate and the final report.
- `git push` is never part of the pipeline.
- Real-PSP hardware validation is always left to the user; once reported, the
  docs must record it and no later publisher may downgrade it.
- If a gate, review, model check, or publish fails, the pipeline stops and
  reports instead of guessing.

## History

- Added 2026-11-08 in commit `a863080` (“Add PSP build pipeline skill”).
- Updated 2026-11-08: discovery now uses `brainstorming` before the plan,
  the plan uses `writing-plans`, and implementation uses `executing-plans`
  (later extended with `test-driven-development`).
- Updated 2026-11-08: mandatory `planlink` plan URLs, publish-scope preflight,
  per-app notes, integration/publish lock, docs status guard, and numeric test
  validation in the plan phase.
- Updated 2026-11-08: builders use `executing-plans` + `test-driven-development`
  (RED/GREEN/REFACTOR, red proof); reviewers use `requesting-code-review`;
  blocked reviews use `receiving-code-review`. The policy checker
  `scripts/check-pipeline-policy.py` runs from `./scripts/test.sh` and the
  AGENTS/SKILL/PIPELINE gates.
- The pipeline was used for Memory Test 0.1.0, Racing 3D 0.1.x/0.2.x/0.3.0 and
  WiFi Connection 0.1.0; Racing 3D 0.3.0 and WiFi 0.1.0 are the current
  published builds.
