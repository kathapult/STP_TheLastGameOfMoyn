# Tasks: Dual-Pi Sensor/Render Split

**Input**: Design documents from `/specs/001-dual-pi-sensor-split/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/osc-object-stream.md, quickstart.md

**Tests**: Not requested in the spec — this repo has no test framework (see `CLAUDE.md`); verification is manual via `quickstart.md`.

**Organization**: Tasks are grouped by user story (US1/US2/US3, matching spec.md's P1/P2/P3) so each can be implemented and validated independently.

## Path Conventions

Two openFrameworks projects in one repo, per plan.md's Structure Decision:
- Pi B (render-only): existing `src/`, `config.make`, `addons.make` at repo root
- Pi A (sense+detect-only): new `sensorApp/` (own `src/`, `config.make`, `addons.make`, `Makefile`)
- Shared: new `shared/` (the one type both sides agree on)

---

## Phase 1: Setup

**Purpose**: Create the new project skeleton before any config wiring or logic is added.

- [X] T001 Create `sensorApp/` openFrameworks project skeleton: `sensorApp/src/main.cpp` using **`ofAppNoWindow`** (windowless — `#include "ofAppNoWindow.h"`, then `ofAppNoWindow window; ofSetupOpenGL(&window, 1, 1, OF_WINDOW); ofRunApp(new ofApp());`), `sensorApp/src/ofApp.h` / `.cpp` (empty `setup()`/`update()`/`draw()`/`exit()` stubs), `sensorApp/Makefile` (same pattern as the root `Makefile` — includes `compile.project.mk`). Verify the exact windowless-setup call against the local openFrameworks 0.12.1 install before finalizing — the `ofSetupOpenGL`/`ofInit` spelling has shifted across OF releases (research.md item 5). This must be checked on the Pi; window/GL setup cannot be verified from the Mac checkout.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Shared type and build wiring both projects need before any story-specific code can compile.

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [X] T002 [P] Create `shared/DetectedObject.h` per `data-model.md`: struct with `id` (int), `x` (float, 0.0–1.0), `y` (float, 0.0–1.0); one-line comment noting it replaces `HandData` (Principle IV, kept brief per project convention)
- [X] T003 [P] Edit `config.make` (root): add `PROJECT_EXCLUSIONS += $(PROJECT_ROOT)/sensorApp%` so the Pi B build doesn't also compile `sensorApp/`'s sources (research.md item 3 — avoids `multiple definition of main`)
- [X] T004 [P] Edit `addons.make` (root, currently empty): add `ofxOsc`
- [X] T005 [P] Edit `sensorApp/config.make` (after T001): set `OF_ROOT = ../../../..`; add `PROJECT_EXTERNAL_SOURCE_PATHS += ../shared`; add the same `ifeq ($(shell uname -s), Linux)`-guarded block as the root project for `libfreenect2` plus the CV addon's build flags (research.md items 4 and 6)
- [X] T006 [P] Create `sensorApp/addons.make` (after T001): add `ofxOsc` and the CV addon (verify exact folder name/casing under `OF_ROOT/addons/` before finalizing — research.md item 3 flags a possible `ofxCV`/`ofxCv` mismatch)
- [X] T006a [P] Configure the point-to-point network link between the two Pis (FR-009): assign a static IP on each Pi's Ethernet interface (no DHCP/router in between), pick and record the OSC port, verify reachability in both directions, and document the addresses/port in `kinect-v2-raspberry-pi-setup.md` — that file currently contains no network configuration at all. Blocking: without this, nothing sent by Pi A can reach Pi B.
- [X] T006b Record the network-link decision in `processDocumentation.md` (after T006a): the IP scheme, the chosen port and why, and a pointer to `kinect-v2-raspberry-pi-setup.md` for the full setup steps. Principle VI covers hardware/OS setup changes, not just code — this mirrors how the original Kinect v2 setup was recorded in both files.

**Checkpoint**: Both projects have the config/addon wiring needed to build against `DetectedObject` and each other's role-specific dependencies, and the two devices can reach each other over the direct link.

---

## Phase 3: User Story 1 - Visuals stay responsive under full sensing + detection load (Priority: P1) 🎯 MVP

**Goal**: Pi B renders its existing visuals driven entirely by an incoming object stream, with no sensor or detection code of its own.

**Independent Test**: Run Pi B alone with a synthetic OSC sender emitting `/object` messages (per `contracts/osc-object-stream.md`) and confirm the visuals react smoothly — see `quickstart.md` § US1.

### Implementation for User Story 1

- [X] T007 [US1] Edit `src/VisualBase.h`: change the pure virtual `update(const std::vector<HandData> &)` to `update(const std::vector<DetectedObject> &)`; swap the `HandData.h` include for `shared/DetectedObject.h`
- [X] T008 [P] [US1] Edit `src/TransformBase.h`: remove the now-unused `HandData` include/forward-declaration (its own interface never used it directly)
- [X] T009 [P] [US1] Edit `src/Shader.h` / `src/Shader.cpp` (after T007): rename `HandData`→`DetectedObject` throughout; remove the `grabStrength`-driven `noiseSpeed` interaction in `Shader::draw()` (~`src/Shader.cpp` L27–31), since `DetectedObject` carries no engagement value (same rationale as dropping grab-pause, see spec Assumptions) — `noiseSpeed` keeps its existing default constant. Leave a brief comment noting what the removed `grabStrength` mapping did and why it went (Principle IV, kept short per project convention)
- [X] T010 [P] [US1] Edit `src/GameOfLife.h` / `src/GameOfLife.cpp` (after T007): rename `update()`'s parameter to `std::vector<DetectedObject>`; replace the old Leap-space-position + `pinchStrength`-driven influence-radius logic with normalized `x`/`y` (0.0–1.0) mapped into grid coordinates and a fixed default influence radius. Leave a brief comment noting the previous coordinate mapping and the `pinchStrength`-driven radius it replaces (Principle IV, kept short per project convention)
- [X] T011 [P] [US1] Edit `src/GameOfLifeVisual.h` / `src/GameOfLifeVisual.cpp` (after T007): rename `update()` override's parameter type to match
- [X] T012 [US1] Delete `src/HandData.h` (after T008, T009, T010, T011 — nothing references it anymore; superseded by `shared/DetectedObject.h`)
- [X] T013 [P] [US1] Read `/object` arguments by count, not by fixed position — accept three arguments today and ignore any trailing ones, so a later Pi A that appends an object-type argument (planned for the hand-recognition feature) does not break this receiver (`contracts/osc-object-stream.md` § "Designed to grow"). Create `src/KinectOSCInput.h` / `src/KinectOSCInput.cpp`: `setup(int listenPort)` opens an `ofxOscReceiver`; `update()` drains incoming bundles, decodes `/object` messages per `contracts/osc-object-stream.md`, and replaces its internal `detectedObjects` vector wholesale each call (full-snapshot semantics, `data-model.md`); `getDetectedObjects()` accessor
- [X] T014 [US1] Edit `src/ofApp.h` / `src/ofApp.cpp` (after T012, T013): replace the `KinectInput kinect;` member with `KinectOSCInput kinectInput;` (brief comment noting what it replaces, per Principle IV); update `setup()`/`update()`/`exit()` accordingly; populate `detectedObjects` from `kinectInput.getDetectedObjects()` once per frame; remove the grab-triggered pause call (`gestureDetector.isGrabHeld(...)` driving `gol->setRunning(...)`) per FR-006, with a short comment noting it's intentionally dropped (Leap-era, no CV equivalent)
- [X] T015 [US1] Edit `config.make` (root, after T014): remove the now-unused Linux-guarded `libfreenect2` linker/include flags — Pi B no longer links `libfreenect2` directly (FR-002)
- [X] T016 [US1] Update `processDocumentation.md` documenting the `KinectOSCInput` swap, the `HandData`→`DetectedObject` rename, and the dropped grab-pause behavior (Principle VI)

**Checkpoint**: Pi B builds and runs, driven purely by a synthetic OSC stream — `quickstart.md` § US1 passes.

---

## Phase 4: User Story 2 - Pi A turns raw depth data into a usable object stream (Priority: P2)

**Goal**: Pi A turns the Kinect's depth image into a live, trackable `DetectedObject` stream and sends it out continuously.

**Independent Test**: Run Pi A alone with the Kinect connected; inspect the outgoing OSC stream directly as a hand moves in/out of view — see `quickstart.md` § US2.

### Implementation for User Story 2

- [X] T017 [US2] Move `src/KinectInput.h` and `src/KinectInput.cpp` into `sensorApp/src/` (Pi A now owns the sensor, FR-001/FR-003) — no code changes expected beyond include-path fixups if any. **Depends on T014**: until T014 lands, the root project's `ofApp.h` still has `#include "KinectInput.h"` (line 5) and a `KinectInput kinect;` member (line 40), with calls in `ofApp.cpp` (lines 7, 24, 79). Moving the files before T014 removes those references breaks the Pi B build. This is a transitional ordering constraint only — in the finished architecture Pi B has no dependency on `KinectInput` whatsoever.
- [X] T018 [P] [US2] Create `sensorApp/src/ObjectDetector.h` / `sensorApp/src/ObjectDetector.cpp` (after T017, T002) per `research.md` item 2: (a) convert `KinectInput::getDepthPixels()` (`ofFloatPixels`, millimetres) into an 8-bit binary mask by thresholding to a fixed depth window covering the volume of interest above the table — make the near/far bounds tunable constants, they will need calibrating on site; (b) run `ofxCv::ContourFinder` on that mask with `setMinAreaRadius`/`setMaxAreaRadius` to reject noise specks and oversized blobs; (c) use `ContourFinder`'s **built-in** tracker for identity — `getTracker().setMaximumDistance()` is the match threshold, `setPersistence()` the grace window, `getLabel(i)` the stable id. Do **not** hand-write a centroid tracker; `ContourFinder` already implements exactly the lifecycle `data-model.md` describes. Output `std::vector<DetectedObject>` with centroids normalized to 0.0–1.0 against the depth frame's 512x424 bounds. Verify the `ofxCv` API spellings against the installed addon before finalizing
- [X] T019 [P] [US2] Create `sensorApp/src/ObjectStreamSender.h` / `sensorApp/src/ObjectStreamSender.cpp` (after T002, T006): wraps an `ofxOscSender` and sends complete snapshots to Pi B's configured address/port. The later OSC-v1 hardening keeps the 40-object frame cap but deterministically prioritizes hands/area/id and partitions every frame into bundles of at most 17 objects. Each part carries `/frame/v1` metadata and extended `/object` messages; the exact largest payload is 1372 bytes, below the 1472-byte IPv4 UDP budget at MTU 1500. Log when the cap trims a noisy frame. See `contracts/osc-object-stream.md`
- [X] T020 [US2] Edit `sensorApp/src/ofApp.h` / `sensorApp/src/ofApp.cpp` (after T017, T018, T019): `setup()` opens `KinectInput`, `ObjectDetector`, `ObjectStreamSender`; `update()` calls `kinect.update()` → `detector.update(kinect)` → `sender.send(objects)`; `draw()` stays empty — with `ofAppNoWindow` (T001) there is no window to draw into, so surface any status output via console logging instead of on-screen text (research.md item 5 — Pi A never renders visuals, FR-003)
- [X] T021 [US2] Update `processDocumentation.md` documenting the new `sensorApp` project and its `KinectInput`/`ObjectDetector`/`ObjectStreamSender` pipeline (Principle VI)

**Checkpoint**: Pi A builds and runs standalone, producing an inspectable, id-stable object stream from real Kinect input — `quickstart.md` § US2 passes, independent of Pi B.

---

## Phase 5: User Story 3 - Rendering keeps running when the sensor Pi is unavailable (Priority: P3)

**Goal**: Pi B never freezes, crashes, or blocks regardless of whether Pi A is sending data.

**Independent Test**: Start Pi B alone (no data), confirm visuals run; start Pi A and confirm live reflection without restarting Pi B; interrupt/restore the network link mid-show — see `quickstart.md` § US3.

### Implementation for User Story 3

- [X] T022 [US3] ~~Edit `src/KinectOSCInput.cpp`: add an idle-timeout check~~ — superseded: T013's unconditional-replace design already satisfies this (no stale snapshot to clear). Repurposed as a silence-diagnostic log instead (`ofLogWarning` after 2s with no detected objects) — see processDocumentation.md for why
- [X] T023 [US3] Verify/harden `src/KinectOSCInput.cpp`'s receive loop (after T022) is non-blocking — `update()` must never wait for a packet that isn't there, so `ofApp::update()`'s frame timing is unaffected whether or not Pi A is sending (FR-007, FR-008) — confirmed by API contract (documented in code), not executable from this Mac checkout
- [ ] T024 [US3] Manually validate `quickstart.md` § US3 end-to-end (after T020, T023): start Pi B alone, then start Pi A, then unplug/replug the network link — confirm zero crashes/freezes and automatic resumption (SC-004) — **requires physical Pi hardware, not done in this session**
- [X] T024a [US3] Update `processDocumentation.md` documenting the idle-timeout and stale-data-clearing behavior added in T022–T023, including the chosen timeout value and why (Principle VI — behavioral change must be recorded alongside the change, same as T016 and T021 do for US1 and US2)

**Checkpoint**: All three user stories independently functional; the full dual-Pi installation is resilient to Pi A's availability changing at runtime.

---

## Phase 6: Polish & Cross-Cutting Concerns

- [ ] T025 [P] Run the complete `quickstart.md` validation (all three stories plus the Success Criteria checks) as a final end-to-end sanity pass — **requires physical Pi hardware, not done in this session**
- [X] T026 [P] Amend `.specify/memory/constitution.md`: mark the `HandData`→`DetectedObject` rename TODO (recorded in the constitution's own Sync Impact Report under Principle II) as resolved, per the Governance section's amendment process — bumped to v1.0.1

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies — can start immediately
- **Foundational (Phase 2)**: T002–T004 can start immediately in parallel with Phase 1; T005–T006 depend on T001 (need `sensorApp/` to exist) — BLOCKS all user stories
- **User Story 1 (Phase 3)**: Depends on Phase 2 completion
- **User Story 2 (Phase 4)**: Depends on Phase 2 completion — independent of Phase 3 (Pi A never touches Pi B's code)
- **User Story 3 (Phase 5)**: Depends on Phase 3 (extends `KinectOSCInput` built there) and, for its full end-to-end validation task (T024), also on Phase 4 being done
- **Polish (Phase 6)**: Depends on all desired user stories being complete

### User Story Dependencies

- **User Story 1 (P1)**: No dependencies on other stories — the MVP
- **User Story 2 (P2)**: Independent of User Story 1 in *content* (all new code lives in `sensorApp/`), but **T017 must not run before T014**. T017 moves `KinectInput.h/.cpp` out of the root project while `ofApp` still references them until T014 replaces that member with `KinectOSCInput`. This is a one-off transitional constraint, not an architectural coupling — once T014 has landed, Pi B never touches `KinectInput` again. All other US2 tasks (T018–T021) are genuinely independent of US1.
- **User Story 3 (P3)**: Builds on User Story 1's `KinectOSCInput`; its manual validation (T024) needs a working Pi A from User Story 2 too, but the resilience *logic* itself (T022–T023) only touches US1's code

### Parallel Opportunities

- T002, T003, T004 can all run in parallel (different files, no dependencies)
- T005, T006 can run in parallel with each other once T001 is done
- T008, T009, T010, T011, T013 can all run in parallel once T007 (and Phase 2) is done
- Once Phase 2 is done, User Story 1 (Phase 3) and User Story 2 (Phase 4) can proceed in parallel — with one exception: T017 waits for T014 (see User Story Dependencies). T018–T021 need no coordination with US1. Everything else is separate projects, no shared files
- T025 and T026 can run in parallel

---

## Parallel Example: Phase 2 (Foundational)

```bash
Task: "Create shared/DetectedObject.h per data-model.md"
Task: "Edit config.make (root): add PROJECT_EXCLUSIONS += $(PROJECT_ROOT)/sensorApp%"
Task: "Edit addons.make (root): add ofxOsc"
```

## Parallel Example: User Story 1 (after T007)

```bash
Task: "Edit src/TransformBase.h: remove unused HandData reference"
Task: "Edit src/Shader.h/.cpp: rename to DetectedObject, drop grabStrength interaction"
Task: "Edit src/GameOfLife.h/.cpp: rename to DetectedObject, fixed influence radius"
Task: "Edit src/GameOfLifeVisual.h/.cpp: rename update() signature"
Task: "Create src/KinectOSCInput.h/.cpp: OSC receiver"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational
3. Complete Phase 3: User Story 1
4. **STOP and VALIDATE**: run `quickstart.md` § US1 with a synthetic OSC sender — Pi B alone should render correctly
5. This is the MVP: Pi B's side of the split is done, even before any real detection exists on Pi A

### Incremental Delivery

1. Setup + Foundational → both projects buildable
2. Add User Story 1 → validate independently → Pi B ready (MVP)
3. Add User Story 2 → validate independently → Pi A ready, real data now flowing
4. Add User Story 3 → validate independently → full installation resilient to Pi A availability
5. Polish → full quickstart pass, constitution TODO resolved

---

## Notes

- No test tasks — this repo has no test framework; `quickstart.md` is the validation mechanism throughout.
- `src/GestureDetector.h`/`.cpp` is intentionally left untouched by this feature (per spec Assumptions) — it stays available but unwired, not deleted.
- Constitution Principle IV (preserve prior code) is applied as brief, readable comments per this feature's plan-phase clarification — not full old-code blocks.
