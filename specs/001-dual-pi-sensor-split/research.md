# Phase 0 Research: Dual-Pi Sensor/Render Split

## 1. Object stream transport (Pi A → Pi B)

- **Decision**: `ofxOsc` (bundled openFrameworks core addon, built on oscpack) over UDP. Pi A
  sends a complete-snapshot event stream, not add/remove events. The original single-bundle
  assumption was superseded by OSC v1: one sensor snapshot uses one to three MTU-safe bundles,
  each with an explicit `/frame/v1` header and at most 17 `/object` messages.
- **Rationale**: matches the transport already sketched in `processDocumentation.md`'s prior
  "2-Raspys-Split" draft. `ofxOsc` ships with every OF install, so no extra native library needs
  cross-compiling for ARM. UDP fits FR-008's explicit tolerance for occasional dropped updates
  without needing retransmission/ack logic. A full snapshot is committed only when all of its
  parts arrive, so a dropped packet means one diagnosed stale/incomplete frame rather than an
  accumulating diff error or partial object set. The direct point-to-point
  link (FR-009) keeps latency low without needing a broker.
- **Alternatives considered**: raw UDP sockets (more manual encode/decode work for no benefit
  over OSC's already-solved bundling/typing); TCP (head-of-line blocking on drops works against
  FR-008's "tolerate gaps" requirement and adds latency risk, contrary to the plan's latency-
  minimization goal); a message broker/MQTT (unnecessary infrastructure for a fixed 2-device
  point-to-point link).

## 2. Depth-based object detection approach (Pi A)

- **Decision**: `ofxCV` (kylemcdonald) `ContourFinder` on a **depth-thresholded binary mask**
  built from `KinectInput::getDepthPixels()`, using `ContourFinder`'s **built-in tracker** for
  stable IDs rather than writing one.

  ```cpp
  contourFinder.setMinAreaRadius(20);
  contourFinder.setMaxAreaRadius(200);
  contourFinder.getTracker().setPersistence(15);      // frames survived unseen
  contourFinder.getTracker().setMaximumDistance(32);  // match threshold, px
  contourFinder.findContours(mask);

  for (int i = 0; i < contourFinder.size(); i++) {
      unsigned int id = contourFinder.getLabel(i);
      glm::vec2   c  = ofxCv::toOf(contourFinder.getCentroid(i));
  }
  ```

  **Depth thresholding, not background subtraction**: `getDepthPixels()` returns `ofFloatPixels`
  in millimetres, while `ContourFinder` expects an 8-bit image. Rather than normalizing the depth
  frame and learning a background model, threshold the raw millimetre values to a fixed
  depth window (the volume of interest above the table) and feed the resulting binary mask
  straight to `ContourFinder`.

- **Rationale**: `ofxCV` is the CV layer the constitution names (Principle I). Its
  `ContourFinder` already implements exactly the nearest-centroid tracker with distance threshold
  and grace period that this item originally specified by hand — `setMaximumDistance` is the
  distance threshold, `setPersistence` is the grace period, `getLabel()` is the stable id. Using
  it satisfies FR-005 and SC-003 with no tracker code of our own to write, test, or tune.

  Depth thresholding is preferable to background subtraction here because the sensor is fixed
  above a table: the volume of interest is known in advance and constant, so it can be stated
  directly instead of learned. It is also immune to lighting changes (depth is not a brightness
  measurement) and needs no warm-up period before the installation is usable — relevant to SC-005's
  setup-time goal. A conversion step from float millimetres to an 8-bit mask is required either
  way, so thresholding adds no work; it removes the background model instead.

  **Verify against the installed addon at implementation time**: the API names above are from
  `ofxCv`'s long-standing interface, but addon APIs drift between versions and this could not be
  verified from the Mac checkout. Confirm against the copy actually installed under
  `OF_ROOT/addons/` before relying on the exact spellings.

- **Alternatives considered**: hand-writing the centroid tracker — this was the original decision
  here, made without checking what `ContourFinder` already provides. Rejected as duplicated work
  with no advantage. Background subtraction (`ofxCv::RunningBackground`) — also the original
  decision; still a reasonable fallback if a fixed depth window proves too rigid in practice, but
  it introduces a learned model and warm-up phase that a fixed-sensor installation does not need.
  A pose/skeleton ML model — disproportionate engineering/CPU cost for a feature that only needs
  position + stable id, not skeletal detail (see plan.md's Scale/Scope). OpenCV's built-in
  multi-object trackers (e.g. CSRT) — heavier per-object CPU cost than nearest-centroid matching
  for no accuracy benefit at this feature's bar.

  **Note for the planned hand-recognition feature**: `ofxCv` finds blobs, not hands — it has no
  hand model, skeleton, or finger detection. Distinguishing hands from other detected objects
  means building on top of the contours it returns (convexity defects for finger gaps, area and
  aspect ratio, perimeter-to-area). That is deliberate follow-on work, not something this feature
  provides.

## 3. Splitting one repository into two independently-buildable OF apps

- **Decision**: add a second openFrameworks project folder, `sensorApp/`, alongside the existing
  project at the repo root. Both are standard OF project folders (own `main.cpp`, `ofApp.h/.cpp`,
  `config.make`, `addons.make`, `Makefile`); each Pi's checkout of this one repo builds only the
  subfolder relevant to its role.

  This repo's own `config.make` documents that its Makefile tooling treats *every* folder under
  a project's own directory as a valid source location unless explicitly excluded — and only
  `bin%`, `obj%`, `%.xcodeproj` are excluded by default. That means placing `sensorApp/` inside
  the existing project's directory tree is not just cosmetic: without an explicit exclusion, the
  root project's build would also compile `sensorApp/`'s sources, including its own `main.cpp`,
  producing a `multiple definition of main` linker error. So the root project's `config.make`
  **must** add:

  ```make
  PROJECT_EXCLUSIONS += $(PROJECT_ROOT)/sensorApp%
  ```

  The `DetectedObject` struct and OSC wire helpers live in a new top-level `shared/` folder.
  Because `shared/` sits inside the *root* project's own directory tree, the root project
  auto-discovers and compiles it with no extra config — it needs no exclusion and no
  `PROJECT_EXTERNAL_SOURCE_PATHS` entry. `sensorApp/`, however, has its own, separate
  `PROJECT_ROOT` (`sensorApp/`), so `shared/` is *outside* its tree; `sensorApp/config.make`
  needs:

  ```make
  PROJECT_EXTERNAL_SOURCE_PATHS += ../shared
  ```

  to reach it. In short: the wiring is asymmetric — root excludes `sensorApp/` and auto-includes
  `shared/`; `sensorApp/` needs no exclusion but must explicitly include `shared/`.

  Both projects' `addons.make` need real content, since openFrameworks addons are opted in per
  project rather than auto-discovered. The root project's `addons.make` is currently empty (0
  bytes) and must gain an `ofxOsc` entry. `sensorApp/addons.make` is new and needs `ofxOsc` plus
  the CV addon — the exact folder name to reference must match whatever is actually installed
  under `OF_ROOT/addons/` (case-sensitive on the Pi's Linux filesystem; the constitution spells
  it `ofxCV`, the upstream kylemcdonald repo is `ofxCv` — verify against the real addon folder
  at implementation time rather than assuming either spelling).

- **Rationale**: keeps the single-repo, single-git-remote deploy flow the constitution's
  Development Workflow section already documents (each Pi pulls the same repo and builds its own
  checkout). Sharing `DetectedObject` keeps one source of truth for the type both sides agree on,
  instead of two hand-kept-in-sync copies.
- **Alternatives considered**: one OF app with a compile-time role flag
  (`PI_ROLE=SENSOR|VISUAL`) — rejected because it would force both Pi A's dependencies
  (`libfreenect2`, `ofxCV`) and Pi B's full visuals stack into one binary/build even though
  FR-002/FR-003 require the roles to be strictly separate, and it works against FR-010's
  "verify each role independently" requirement. Two fully separate git repositories — rejected
  as unnecessary process overhead that loses `DetectedObject` as a single shared source of truth.

## 4. `OF_ROOT` path depth for `sensorApp/`

- **Decision**: `sensorApp/config.make` sets `OF_ROOT = ../../../..` (four `..` segments)
  instead of the existing project's `OF_ROOT = ../../..` (three), since `sensorApp/` sits one
  directory level deeper.
- **Rationale**: an OF project's `OF_ROOT` is a relative path from the project folder up to the
  OF install root. The existing project's location (`apps/myApps/<project>/` on each Pi, per
  today's convention) is unchanged, so its three-`..` path stays correct; `sensorApp/`, being a
  child of that same project folder, needs one more `..` to reach the same OF root.
- **Alternatives considered**: placing `sensorApp/` as a sibling of the existing project (i.e.
  directly under `apps/myApps/`) — rejected because that would put it outside this git repo's
  own directory tree, breaking the "one repo, one pull, per Pi" deploy flow.

## 5. Pi A rendering/window footprint

- **Decision**: `sensorApp` stays an openFrameworks app, but runs windowless via OF's built-in
  `ofAppNoWindow` instead of creating a real window. Its `draw()` stays empty; no GL context is
  set up for rendering visuals, satisfying FR-003 both at the application-logic level and at the
  window/GL level.

  ```cpp
  // sensorApp/src/main.cpp
  #include "ofAppNoWindow.h"

  int main() {
      ofAppNoWindow window;
      ofSetupOpenGL(&window, 1, 1, OF_WINDOW);
      ofRunApp(new ofApp());
  }
  ```

- **Rationale**: `ofAppNoWindow` ships with openFrameworks core — no OF-core patching required.
  This removes the per-frame window/buffer-swap work entirely on the device whose CPU headroom is
  the entire reason for this feature, at essentially zero implementation cost. `KinectInput`,
  `ofPixels`/`ofFloatPixels` and the CV addon all remain directly reusable, since none of them
  depend on a window existing.

  **Verify on the Pi at implementation time**: `ofAppNoWindow` is long-standing OF API, but the
  exact `main()` setup call has shifted across OF releases (`ofSetupOpenGL` vs. `ofInit`). Confirm
  against the local openFrameworks 0.12.1 install before finalizing `sensorApp/src/main.cpp`.
  Per this repo's convention, anything touching GL/window setup cannot be verified from the Mac
  checkout.

- **Alternatives considered**: a standard windowed OF skeleton with an empty `draw()` — this was
  the original decision here, based on the incorrect premise that true headless required patching
  OF core. It does not; `ofAppNoWindow` covers exactly this case. Rejected because it leaves the
  Pi doing a buffer swap every frame for a window nobody looks at. If windowless mode turns out to
  complicate on-device debugging, falling back to a real window is a one-line change in `main.cpp`.

  A non-OF plain C++ process for Pi A (libfreenect2 + OpenCV + oscpack directly) — a legitimate
  architecture, and leaner still: `KinectInput`'s only OF dependency is its `ofPixels`/
  `ofFloatPixels` return types, and a `libfreenect2::Frame` can be wrapped as a `cv::Mat` without
  copying. Rejected for *this* feature because it would require rewriting `KinectInput`,
  maintaining a second build system in the repo, and dropping the `ofxCV` layer the constitution
  names in Principle I — a deviation needing its own documented justification. Worth revisiting as
  a separate feature if on-device measurement shows Pi A is CPU-bound with OF overhead as a
  measurable contributor.

## 6. Constitution compliance for Pi-A-only dependencies

- **Context**: this project is never built on Mac — Mac is edit-only, and only the Pi checkouts
  ever run `make` (see `CLAUDE.md`). So in practice, `sensorApp`'s new dependencies
  (`libfreenect2`, `ofxCV`) never get compiled on Mac at all, and nothing here changes that.
- **Decision**: even so, guard the new Mac-incompatible build config in `sensorApp/config.make`
  the same way the existing project's `config.make` already guards `libfreenect2` — behind
  `ifeq ($(shell uname -s), Linux)`. This is a **compile-time** check `make` evaluates on
  whichever machine happens to run it; it isn't something the running app does, and it isn't
  needed for the app to work correctly — it only matters if `make` is ever invoked on Mac.
- **Rationale**: the constitution's Principle V is still written as "Code MUST build cleanly on
  both targets," and the existing project already set this exact precedent for `libfreenect2`
  (guarding it even though Mac builds don't happen in practice) — matching that keeps the
  written principle honored without contradicting how the project is actually used.
- **Alternatives considered**: skip the guard since Mac builds don't happen in practice —
  rejected only because it would leave a written constitution principle unaddressed without the
  explicit justification the constitution's Governance section requires for any deviation; this
  is a paperwork/consistency reason, not a functional one.
## 7. Naming

- **Decision**: adopt the names already proposed in `processDocumentation.md`'s prior planning
  draft — `DetectedObject` (struct), `detectedObjects` (the vector `ofApp` holds, replacing
  `hands`), `ObjectDetector` (Pi A's detection class), `KinectOSCInput` (Pi B's OSC receiver
  class). This resolves the constitution's own deferred rename TODO under Principle II.
- **Rationale**: these names were already worked out and recorded in this repo's own docs before
  this feature was specified — reusing them avoids re-litigating naming and keeps
  `processDocumentation.md` and the implementation in sync.
