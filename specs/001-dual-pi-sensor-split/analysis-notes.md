# Analysis Notes: Dual-Pi Sensor/Render Split

Consolidated output of `/speckit-analyze`, run three times against `spec.md`, `plan.md`, and
`tasks.md` as they were refined. Read-only analysis — no files were changed by the command
itself; all fixes below were applied by hand between passes. Kept here so the review history
isn't lost once `tasks.md` moves on.

## Current status (as of the 3rd pass)

- **Critical issues: 0** — nothing blocks `/speckit-implement`.
- **Coverage: 100%** (15/15 FR/SC keys have at least one task).
- 5 open low/medium-severity items remain, all non-blocking (see below).

## Open findings

| ID | Category | Severity | Location(s) | Summary | Recommendation |
|----|----------|----------|-------------|---------|----------------|
| B1 | Ambiguity | HIGH | spec.md SC-003 (L93); quickstart.md "Success criteria checks" | SC-003's 95% id-stability figure has no defined measurement mechanism — quickstart's check is qualitative only ("watch the logged values don't change"). | Relax SC-003 to a qualitative check (matches this repo's no-test-framework convention), or add a lightweight counter Pi A logs periodically. |
| B2 | Ambiguity | MEDIUM | spec.md FR-004, SC-002; plan.md "Performance Goals" | The core latency requirement ("no visitor-perceptible lag" / "a fraction of a second") has no concrete number anywhere in spec/plan/tasks. | Record an internal target in research.md for implementers (e.g. "≤100ms end-to-end"), without changing the spec's tech-agnostic wording. |
| F1 | Inconsistency | LOW | data-model.md L10 | `DetectedObject.id`'s description still says "Assigned by Pi A's centroid tracker" — accurate in spirit but slightly imprecise now that research.md/T018 specifically use `ofxCv::ContourFinder`'s built-in tracker rather than a custom one. | Reword to "Pi A's contour tracker (research.md item 2 — `ofxCv::ContourFinder`'s built-in tracker)". |
| E3 | Coverage Gap | LOW | spec.md Edge Cases (L64); tasks.md T006a, T020 | Setup-time reachability check (T006a) and Pi A's console logging (T020) substantially cover the "misconfigured link should be observable/debuggable" edge case; Pi B still doesn't log when it transitions to "no data" via T022's idle timeout. | Optional: have T022 log once when the idle timeout first fires. |
| B3 | Ambiguity | LOW | research.md items 2–3; tasks.md T006, T018 | Exact addon folder names/API spellings (`ofxCV`/`ofxCv`, `ContourFinder` API) are flagged as "verify before finalizing" against the real install — already self-mitigated, listed for completeness. | No action beyond what's already planned. |

## Resolved across earlier passes

Kept as a changelog — each of these was a real finding in an earlier pass, fixed before the next one:

- **D1** (CRITICAL, Principle VI) — User Story 3 had no `processDocumentation.md` task for its idle-timeout/resilience behavior. Fixed: **T024a** added.
- **F1 (1st occurrence)** (HIGH) — `tasks.md`'s Dependencies section claimed User Story 2 had "no dependencies on other stories," but T017 (moving `KinectInput` out of the root project) would break Pi B's build if run before T014 removed the reference. Fixed: explicit "Depends on T014" note added to T017 and the Dependencies section.
- **E1** (HIGH) — FR-009 (direct point-to-point link) had no task that actually configured it; the contract doc explicitly deferred this to `tasks.md` but nothing picked it up. Fixed: **T006a** added (static IP, port, reachability check).
- **E2** (MEDIUM) — The "unusually large number of objects shouldn't overwhelm Pi B" edge case had no corresponding task. Fixed: **T019** now caps objects per OSC bundle (~40, MTU-based rationale) and logs when trimmed.
- **D3** (CRITICAL, Principle VI) — T006a (new network-link config) was a hardware/OS-setup change with no `processDocumentation.md` companion task. Fixed: **T006b** added.
- **D2** (MEDIUM) — T014 called for a "brief comment noting what it replaces" (Principle IV) but the equivalent instruction was missing from T009 (Shader) and T010 (GameOfLife), which replace equivalent old behavior. Fixed: same instruction added to both.
- *(Caught mid-edit, never separately reported)* — Contract doc's new "Designed to grow" forward-compatibility rule (`/object` receivers must read args by count, ignore trailing ones) wasn't yet reflected in T013. Fixed before it needed flagging.
- *(Caught mid-edit, never separately reported)* — research.md item 2 originally specified a hand-written centroid tracker; T018 was rewritten to use `ofxCv::ContourFinder`'s built-in tracker instead (simpler, less to build/test/tune), and research.md item 2 was rewritten to match, including the depth-thresholding-vs-background-subtraction rationale.

## Metrics (3rd pass)

- Total Requirements (FR + SC): 15
- Total Tasks: 29 (T001–T026 plus T006a, T006b, T024a)
- Coverage: 100%
- Ambiguity Count: 3 (B1, B2, B3)
- Duplication Count: 0
- Critical Issues Count: 0
