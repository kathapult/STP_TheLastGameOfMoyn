# Quickstart: Validating the Dual-Pi Sensor/Render Split

Manual validation guide — this repo has no automated test suite (see `CLAUDE.md`). Each section
below maps to one user story in `spec.md` and can be run independently, per that story's
"Independent Test."

## Prerequisites

> Updated 2026-08-08 — this repo now holds two sibling project subfolders (`sensorApp/` for Pi
> A, `visualApp/` for Pi B, see `processDocumentation.md`), and each's `config.make` resolves
> `OF_ROOT` against the Pi's fixed openFrameworks install path
> (`/opt/of_v0.12.1_linuxaarch64_release`), not a relative `apps/myApps/` placement.

- Two Raspberry Pi 4 boards ("Pi A", "Pi B"), each with this repository pulled anywhere on disk
  (placement no longer matters — `OF_ROOT` is resolved via a fixed absolute path on Linux, see
  `CLAUDE.md`).
- Kinect v2 connected to Pi A only, powered via the breakout adapter (see
  `kinect-v2-raspberry-pi-setup.md`).
- Pi A and Pi B connected via a direct point-to-point Ethernet cable, both on a static IP —
  `10.0.0.1`/`10.0.0.2`, port `8000` — see `kinect-v2-raspberry-pi-setup.md` § 8.
- Both projects built (`make -j` in `sensorApp/` for Pi A, `make -j` in `visualApp/` for Pi B).

## US1 (P1) — Pi B stays smooth, independent of real detection

Validates the plumbing without needing `ObjectDetector` to be finished yet.

1. Run Pi B alone (`make RunRelease` in `visualApp/`), with Pi A off/disconnected.
2. Confirm the visuals run (per FR-007 — this should already work, since "no data" is the
   existing baseline behavior).
3. Stop Pi A's update timer and normal sender, start Pi B's calibration service, and run
   `python3 tests/osc_v1_probe.py --send 10.0.0.2` on Pi A. The
   provided sender exercises explicit empty, multipart, reordered, duplicated and incomplete
   snapshots according to `contracts/osc-object-stream.md` while satisfying Pi B's source-IP
   policy.
4. Confirm the visuals visibly react (grid influence field) to the synthetic positions, and stay
   smooth — no stutter attributable to the (currently absent) detection workload.

**Expected outcome**: Pi B renders correctly driven purely by network input, matching
Acceptance Scenario 2 of User Story 1.

## US2 (P2) — Pi A produces a real object stream

Validates detection in isolation, no Pi B required.

1. Run Pi A alone (`make RunRelease` in `sensorApp/`), Kinect connected.
2. Move a hand in front of the Kinect.
3. Inspect Pi A's outgoing `/frame/v1` and `/object` stream on Pi B (for example with `tcpdump`)
   and confirm:
   - An entry appears representing the hand's approximate position when it enters view.
   - The same `id` persists across consecutive frames while the hand keeps moving continuously.
   - The entry disappears once the hand leaves the sensor's field of view.

**Expected outcome**: matches Acceptance Scenarios 1–3 of User Story 2.

## US3 (P3) — Pi B tolerates Pi A being unavailable

Validates resilience end-to-end, both devices involved.

1. Start Pi B first, with Pi A still off. Confirm visuals run normally (no freeze/crash).
2. Start Pi A. Confirm Pi B's visuals begin reflecting live detected-object data *without*
   restarting Pi B.
3. With both running, physically unplug the Ethernet link for a few seconds, then reconnect.
   Confirm Pi B doesn't crash during the gap and resumes reflecting live data once reconnected.

**Expected outcome**: matches all three Acceptance Scenarios of User Story 3.

## Success criteria checks

- **SC-001 / SC-002** (smoothness / latency): visually judge frame smoothness and
  movement-to-visual latency during US1–US3; no fixed measurement tooling exists in this repo,
  so this is an eyeball check against "no visitor-perceptible stutter/delay."
- **SC-003** (id stability): during US2, watch the logged/inspected `id` values — confirm they
  don't change while a hand is continuously visible and moving.
- **SC-004** (crash-free on disconnect): covered by US3 step 3.
- **SC-005** (setup time): time a full cold setup of both devices (power, cabling, starting both
  apps) once the feature is implemented, to check it against "about today's single-device setup
  time, plus a few minutes."
