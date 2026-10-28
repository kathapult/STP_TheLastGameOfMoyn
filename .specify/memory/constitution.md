<!--
Sync Impact Report
Version change: 2.0.0 → 2.1.0 (MINOR - materially changed guidance on Principle IV)
Modified principles:
  - IV. Explain, Don't Preserve, Old Code: broadened the comment requirement. Comments MUST
    now briefly explain code/logic/behavior in general (what a reader would otherwise have to
    puzzle out), not only "important/non-obvious" cases as the 2.0.0 wording said - that
    qualifier read as narrower than intended.
Added sections: none
Removed sections: none
Resolved deferred TODOs:
  - The commented-out old-code leftovers flagged in the 2.0.0 report below (config.make files,
    GameOfLifeVisual.h, ofApp.cpp/.h, Shader.cpp, GameOfLife.cpp, main.cpp) were cleaned up in
    the same pass as this amendment.
-->

<!--
Sync Impact Report
Version change: 1.0.1 → 2.0.0 (MAJOR - backward-incompatible redefinition of Principle IV)
Modified principles:
  - IV. Preserve Prior Code as Comments → Explain, Don't Preserve, Old Code: reversed. Old
    behavior/logic/defaults being replaced MUST now be deleted outright, not kept as a
    commented-out block next to the replacement - the accumulation of dead commented code was
    making the codebase messy. Comments are still required, but only to explain non-obvious
    WHY (important logic, behavior, tricky snippets), never to preserve old code for later
    reference. Git history is the record of what used to be there.
Added sections: none
Removed sections: none
Resolved deferred TODOs: none
Note: this repo still contained commented-out old-code blocks written under the old version of
this principle (e.g. old `OF_ROOT`/`PROJECT_EXCLUSIONS` values in config.make files, the old
`running` default in GameOfLifeVisual.h, old `keyPressed` logic in ofApp.cpp) - resolved in the
2.1.0 amendment above.
-->

<!--
Sync Impact Report
Version change: 1.0.0 → 1.0.1 (PATCH - wording/clarification only)
Modified principles:
  - I. Kinect-First Sensor Pipeline: updated the historical note - `HandData` has now been
    renamed to `DetectedObject` (see below), so it's no longer listed among the still-present
    Leap-era names. `GestureDetector` remains, unwired.
  - II. Data Boundary Between Sensor and Visuals: resolved the deferred rename TODO from the
    1.0.0 report below - the intermediate per-frame struct is now named `DetectedObject`
    (`shared/DetectedObject.h`), replacing `HandData`.
Added sections: none
Removed sections: none
Resolved deferred TODOs:
  - The `HandData` rename flagged as deferred in the 1.0.0 report below is done:
    `DetectedObject`, implemented as part of specs/001-dual-pi-sensor-split (the Pi A/Pi B
    sensor-detection/render split). See that feature's processDocumentation.md entry and
    research.md item 7 for the naming rationale.
-->

<!--
Sync Impact Report
Version change: [TEMPLATE] → 1.0.0 (initial ratification)
Modified principles: n/a (first fill of template placeholders)
Added sections:
  - I. Kinect-First Sensor Pipeline
  - II. Data Boundary Between Sensor and Visuals
  - III. Modular Class Hierarchy
  - IV. Preserve Prior Code as Comments
  - V. Platform Portability (Pi-primary, Mac dev-only)
  - VI. Documentation & Language Convention
  - Additional Constraints (tech stack, dependency rules, reference documents)
  - Development Workflow (deploy flow, compliance checks)
  - Governance
Removed sections: none (template had no prior content)
Deferred / follow-up TODOs:
  - The rename of `HandData` to something less Leap-specific is planned but not yet
    decided/executed (see processDocumentation.md's "2-Raspys-Split" draft, which proposes
    `DetectedObject` as one option). Principle II intentionally does not mandate a final name.
-->

# The Last Game of Moyn Constitution

## Core Principles

### I. Kinect-First Sensor Pipeline
This project's sensor pipeline is Kinect v2 via `libfreenect2`, with `ofxCV` (kylemcdonald)
as the computer-vision layer for extracting objects/hands from the depth and IR streams.
Leap Motion is no longer used and MUST NOT be reintroduced as a live code path - it has been replaced by the KinectV2 sensor.

Historical note: this codebase originated from an earlier, related project built around a
Leap Motion sensor (see "Related Documents" under Additional Constraints). That is why
Leap-era names and remnants (`GestureDetector`, the general shape of `LeapMotionInput`) still
appear in the code — they are inherited structure, not evidence of an active multi-sensor
abstraction. `LeapMotionInput` itself has been deleted; do not restore it. `HandData` has been
renamed to `DetectedObject` (see Principle II) as part of the Pi A/Pi B sensor-detection/render
split (specs/001-dual-pi-sensor-split).

### II. Data Boundary Between Sensor and Visuals
Visuals MUST read only from the intermediate per-frame data struct (`DetectedObject`, in
`shared/DetectedObject.h` — renamed from `HandData`) populated once per frame in
`ofApp::update()`. Visuals MUST NOT call into `KinectInput`, `KinectOSCInput`, `libfreenect2`,
or any CV/detection/network code directly. The pipeline is no longer required to be swappable
across sensor vendors, but this internal boundary is kept because it keeps visuals free of
sensor/CV-pipeline/network internals and change churn.

### III. Modular Class Hierarchy
New visual screens MUST inherit from `VisualBase`; new grid/data transformations MUST
inherit from `TransformBase`. The render/data pipeline is a chain of composable
transformations, not a monolithic implementation — a new transform is added as a new class
in the chain, not as a branch inside an existing one.

### IV. Explain, Don't Preserve, Old Code
When replacing existing logic, defaults, or behavior, delete the old code outright - do not
leave it commented out next to the replacement. Git history is the record of what it used to
be; commented-out old code just accumulates and makes the codebase messy. Comments MUST briefly
explain logic, behavior, and code snippets in general - not only important/non-obvious cases -
but never as a way to keep dead code around.

### V. Platform Portability (Pi-primary, Mac dev-only)
Raspberry Pi (Linux ARM64) is the deployed runtime target; macOS (Apple Silicon) is used for
development only. No dependency may require Rosetta/x86 emulation, CUDA, or OpenCL, since the Pi 4's GPU supports neither (this is why the Jyurineko `libfreenect2` fork with an OpenGL ES 3.1 backend is used instead of stock `libfreenect2`). Code MUST build cleanly on both targets; where a platform difference is unavoidable (e.g. the `Linux`-guarded linker flags in`config.make`), it MUST be guarded so the other target is unaffected.

### VI. Documentation & Language Convention
Every change that affects behavior, architecture, or hardware/OS setup MUST be recorded in
`processDocumentation.md` alongside the change, not deferred to "later." Code, identifiers,and comments MUST be written in English. 


## Additional Constraints

- **Tech stack**: openFrameworks 0.12.1, C++17, `libfreenect2` (Jyurineko fork, OpenGL ES
  3.1 backend), `ofxCV` (kylemcdonald) for the CV layer.
- **No platform-locked dependencies**: reject any addon or library shipped only as an
  Intel-only/x86-only compiled binary, or one that hard-requires CUDA/OpenCL. This is a
  direct lesson from the old `ofxLeapMotion` addon's Intel-only `libLeap.dylib` (incompatible
  with Apple Silicon) and stock `libfreenect2`'s CUDA/OpenCL requirement (incompatible with
  the Pi 4 GPU) — both had to be replaced or forked for exactly this reason.
- **No Kinect OF addons**: sensor integration links `libfreenect2` directly inside
  `KinectInput` (Principle II) rather than depending on `ofxKinect` (Kinect v1-only) or
  `ofxKinectV2`/`ofxLibfreenect2` (require OpenCL, unavailable on the Pi).
- **Related documents — do not conflate**: `projectDescription.md` documents a *prior,
  related* project built around Leap Motion, which this project's code was originally based
  on. It is not a live spec of this project and is not fully identical to it (this project
  has since moved fully to Kinect v2). `processDocumentation.md` is the authoritative,
  current-project change log. When in doubt about current architecture, `processDocumentation.md`
  and the code win over `projectDescription.md`. We kept this here mainly because of the Class structure and for the logic og the game of Life idea.

## Development Workflow

- **Deploy flow**: changes are committed and pushed to the git repository
  (`git@bitbucket.org:WorstConn/thelastgameofmoyn.git`); the Raspberry Pi pulls via SSH. There
  is no direct file-copy or build-on-Mac-then-transfer step — the Pi always builds from its
  own pulled checkout.
- **Compliance check before merging/pulling on the Pi**: any change touching sensor or CV
  pipeline behavior must preserve the visuals/sensor data boundary (Principle II), and any
  change touching build config must be verified not to break the macOS dev build
  (Principle V).
- add, commit and push every step directly to the repository

## Governance

This constitution supersedes ad-hoc practice for this project. Amendments are made by editing
this file directly and MUST update the version per semantic versioning: MAJOR for a backward-
incompatible principle removal/redefinition, MINOR for a new principle or materially expanded
guidance, PATCH for wording/clarification only. Every amendment updates `Last Amended` and
prepends a Sync Impact Report summarizing the change. Spec Kit commands (`/speckit-plan`,
`/speckit-implement`, etc.) and code review MUST verify compliance with these principles;
deviations require an explicit justification recorded in the relevant feature's plan.

**Version**: 2.1.0 | **Ratified**: 2026-08-04 | **Last Amended**: 2026-08-08
