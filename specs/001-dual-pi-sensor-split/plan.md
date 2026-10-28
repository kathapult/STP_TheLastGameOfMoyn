# Implementation Plan: Dual-Pi Sensor/Render Split

**Branch**: `001-dual-pi-sensor-split` | **Date**: 2026-08-06 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `/specs/001-dual-pi-sensor-split/spec.md`

## Summary

Split the installation's workload across two Raspberry Pi 4s: **Pi A** owns the Kinect v2,
runs depth-based object detection (`ofxCV`), and streams the resulting detected-object
positions out over a direct network link; **Pi B** runs the existing visuals unchanged in
spirit, but sourced from that incoming stream instead of a local sensor. Two new,
independently-buildable openFrameworks projects are added — `sensorApp/` (Pi A) alongside the
existing project (which becomes Pi B's app) — sharing a small `shared/` source folder for the
`DetectedObject` type and OSC wire helpers. This also executes the rename the constitution
already flagged as deferred: `HandData`/`hands` → `DetectedObject`/`detectedObjects`. The
existing grab-triggered pause behavior (`GestureDetector::isGrabHeld`) is dropped per the spec,
not carried over.

## Technical Context

**Language/Version**: C++17, openFrameworks 0.12.1 (unchanged from the existing project)

**Primary Dependencies**:
- Pi A (`sensorApp/`): `libfreenect2` (Jyurineko fork, OpenGL ES 3.1 backend — already used by
  the existing `KinectInput`), `ofxCV` (kylemcdonald) for contour/blob detection on the depth
  frame, `ofxOsc` (bundled openFrameworks core addon) to send the object stream.
- Pi B (existing project): `ofxOsc` to receive the object stream. `libfreenect2`/`ofxCV` are
  **not** linked here — Pi B has no sensor or CV code (FR-002).

**Storage**: N/A — no persistence; only a live, unbuffered per-frame object stream.

**Testing**: None — this repo has no test framework (see `CLAUDE.md`). Verification is manual,
via the scenarios in `quickstart.md`, matching existing project convention.

**Target Platform**: Both Pi A and Pi B are Raspberry Pi 4 (confirmed) — same hardware as
today's single-Pi setup, so no new GPU-driver or OS variant is introduced by the split itself.
Both run Raspberry Pi OS (Bookworm) in an X11 session, not Wayland — OF 0.12.1's bundled GLFW
doesn't support native Wayland (per `processDocumentation.md`). Because both boards are the
same model, they naturally share one OS image/build toolchain; this isn't a new constraint the
split adds, just a consequence of both roles running on identical hardware. macOS (Apple
Silicon) remains edit-only — nothing is built locally on Mac for either project (see
`CLAUDE.md`); only the Pi checkouts build and run.

**Project Type**: Two openFrameworks native applications sharing one git repository — not a
web/mobile project. (See Project Structure below.)

**Performance Goals**: Latency end-to-end (sensor frame → visual response on Pi B) should be as
low as achievable — this is the point of the split, so it's treated as a "minimize" goal rather
than a single fixed target. SC-002 sets the outer bound ("a fraction of a second," visitor-
perceptible immediacy), but the design choices in this plan (UDP/OSC over a direct point-to-
point link, no broker, no per-frame acknowledgement round-trip) are chosen specifically to keep
actual latency well under that bound, not just to clear it.

**Constraints**: No CUDA/OpenCL, no Rosetta/x86 emulation (Principle V) — `ofxCV`'s contour
detection runs CPU-only, no GPU compute required. No dependency may ship as an Intel-only
binary (Additional Constraints) — `ofxOsc` is pure cross-platform source (bundled with every OF
install), `ofxCV` is a source-level OpenCV wrapper.

**Scale/Scope**: Not yet fixed. Development starts with a single tracked object (one hand) to
validate the pipeline end-to-end, with 2+ simultaneous objects as a likely but not committed
next step, depending on remaining project time. The design (a per-frame snapshot of however
many objects are currently detected, see `data-model.md`) doesn't hard-code an object count, so
this is a tuning/testing matter for later, not a structural constraint on this plan.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|---|---|---|
| I. Kinect-First Sensor Pipeline | **PASS** | `libfreenect2` + `ofxCV` on Pi A only, exactly the pipeline the constitution already names. Leap Motion stays untouched/unreintroduced. |
| II. Data Boundary Between Sensor and Visuals | **PASS** | Pi B's visuals keep reading only the per-frame `detectedObjects` vector populated once per frame in `ofApp::update()`; they gain a new *source* (an OSC receiver class) but never call into it, `KinectInput`, `libfreenect2`, or CV code directly — the boundary the principle protects is unchanged. This feature also resolves the principle's own deferred rename TODO (`HandData` → `DetectedObject`). |
| III. Modular Class Hierarchy | **PASS** | No new `VisualBase`/`TransformBase` subclasses are needed — existing visuals keep consuming the same vector shape. New non-visual classes (`ObjectDetector`, OSC sender/receiver) sit outside this hierarchy, same as `KinectInput` does today. |
| IV. Preserve Prior Code as Comments | **Applies at implementation, lightweight** | When `ofApp.h`/`.cpp` on Pi B swap `KinectInput kinect;` for the new OSC-based input, and when `HandData`/`hands` are renamed, keep a short, readable note of what the code used to do rather than pasting the full prior implementation inline — matches the principle's intent (don't lose context on a change) without hurting readability. |
| V. Platform Portability (Pi-primary, Mac dev-only) | **PASS, with a guard to add** | No CUDA/OpenCL/Rosetta dependency is introduced. `sensorApp/config.make` must Linux-guard its `libfreenect2`/`ofxCV` build flags the same way the existing project's `config.make` already guards `libfreenect2` (`ifeq ($(shell uname -s), Linux)`), so the written principle ("Code MUST build cleanly on both targets") stays honored even though, in practice, only the Pi checkouts are ever built. |
| VI. Documentation & Language Convention | **Applies at implementation** | Every behavioral/architectural change from this feature must be logged in `processDocumentation.md` alongside the change, in English. |

No unjustified violations — Complexity Tracking table below is empty.

## Project Structure

### Documentation (this feature)

```text
specs/001-dual-pi-sensor-split/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── contracts/           # Phase 1 output
│   └── osc-object-stream.md
├── quickstart.md        # Phase 1 output
└── tasks.md             # Phase 2 output (/speckit-tasks — not created here)
```

### Source Code (repository root)

```text
# Pi B — existing project, becomes the render-only app.
src/
├── main.cpp
├── ofApp.h / .cpp                 # gains a KinectOSCInput member, loses KinectInput
├── VisualBase.h / .cpp
├── TransformBase.h / .cpp
├── Shader.h / .cpp
├── GameOfLife.h / .cpp
├── GameOfLifeVisual.h / .cpp
├── GestureDetector.h / .cpp       # grab-hold logic kept as inherited structure but no
│                                  # longer wired to a pause behavior (spec drops that use)
├── KinectOSCInput.h / .cpp        # NEW — OSC receiver, populates detectedObjects
config.make                        # gains ofxOsc entry, and MUST add
                                    #   PROJECT_EXCLUSIONS += $(PROJECT_ROOT)/sensorApp%
                                    # otherwise this project's build auto-discovers every
                                    # subfolder under the repo root (config.make's own
                                    # documented default — only bin%/obj%/%.xcodeproj are
                                    # excluded automatically) and would compile sensorApp/'s
                                    # sources too, including its own main() -> linker error
                                    # "multiple definition of main". shared/ needs NO such
                                    # exclusion here — it's meant to be auto-included.
addons.make                        # currently 0 bytes; gains one line: ofxOsc

# Pi A — NEW sibling openFrameworks project, sensing + detection only, no rendering.
sensorApp/
├── src/
│   ├── main.cpp
│   ├── ofApp.h / .cpp             # setup()/update(): kinect.update() -> detector.update()
│   │                              # -> sender.send(); draw() is a no-op (see research.md)
│   ├── KinectInput.h / .cpp       # moved here from the existing project (Pi A owns the sensor)
│   ├── ObjectDetector.h / .cpp    # NEW — ofxCV contour/blob detection + centroid ID tracking
│   └── ObjectStreamSender.h / .cpp # NEW — encodes DetectedObjects as an OSC bundle, sends it
├── config.make                    # OF_ROOT = ../../../.. (one level deeper than the existing
│                                  # project — see research.md); Linux-guarded libfreenect2/ofxCV;
│                                  # PROJECT_EXTERNAL_SOURCE_PATHS += ../shared (see below)
├── addons.make                    # NEW file: ofxOsc + the CV addon (verify exact folder name/
│                                  # casing under OF_ROOT/addons/ — case-sensitive on Linux)
└── Makefile                       # standard OF project makefile, same pattern as repo root

# Shared between both projects. Lives inside the repo root, so Pi B's own project already
# auto-discovers and compiles it (no PROJECT_EXTERNAL_SOURCE_PATHS needed on that side). Only
# sensorApp/, whose own PROJECT_ROOT is sensorApp/, needs PROJECT_EXTERNAL_SOURCE_PATHS to
# reach outside its folder and pull shared/ in.
shared/
└── DetectedObject.h                # NEW — the wire/runtime shape both sides agree on
```

**Structure Decision**: Two openFrameworks project folders in one git repository — the
existing repo root (Pi B, render-only) and a new `sensorApp/` (Pi A, sense+detect-only) —
matching the constitution's existing single-repo deploy flow (each Pi pulls the same repo,
builds its own subfolder). `KinectInput` moves from the repo root into `sensorApp/`, since only
Pi A touches the sensor now. A new top-level `shared/` folder holds the one type both projects
need to agree on (`DetectedObject`). Because of how this repo's OF Makefile tooling scans for
source (everything under a project's own folder is fair game unless excluded — see
`config.make`'s documented `PROJECT_EXCLUSIONS` default), the two folders need *asymmetric*
wiring: the root project must explicitly **exclude** `sensorApp/` (or it would compile that
project's `main.cpp` too and fail to link), while `shared/` needs no such exclusion there since
it's meant to be picked up automatically; `sensorApp/`, conversely, must explicitly **include**
`shared/` via `PROJECT_EXTERNAL_SOURCE_PATHS`, since `shared/` sits outside its own project
root. Both `addons.make` files also need real content — the root one is currently empty (0
bytes) and gains `ofxOsc`; `sensorApp/addons.make` is new and needs `ofxOsc` plus the CV addon.

## Constitution Check (post-Phase 1 re-check)

Re-checked after `research.md`, `data-model.md`, `contracts/`, and `quickstart.md` were written.
No new violations surfaced during design:

- `DetectedObject` (data-model.md) carries only position + id, nothing that reintroduces
  Leap-era gesture shape — consistent with Principle I/II and the spec's corrected scope.
- The OSC contract (`contracts/osc-object-stream.md`) is the *only* thing Pi B depends on from
  the network side; visuals still never see `KinectOSCInput`, `ObjectDetector`, or any CV/OSC
  type directly — Principle II holds.
- No new `VisualBase`/`TransformBase` subclasses were introduced — Principle III unaffected.
- The build-config corrections found during design (`PROJECT_EXCLUSIONS` for `sensorApp/`, the
  `ofxOsc`/CV addon entries, the `../shared` external source path) are all Linux/Pi-side
  `config.make`/`addons.make` changes, consistent with Principle V's guard pattern.

Status: **PASS**, unchanged from the pre-Phase-0 check.

## Complexity Tracking

*No entries — no Constitution Check gate required a justified violation.*
