# Contract: Calibration Control and Telemetry v1

This contract is active only while both non-installable calibration services
run. Production does not bind UDP 8001 or 8002. The show-critical object
contract on UDP 8000 is unchanged.

## Endpoints and source policy

- Pi B sends control requests to Pi A on OSC/UDP 8002.
- Pi A sends results and complete settings states to Pi B on OSC/UDP 8002.
- Both applications bind 8002 only with `--calibration` and accept only the
  statically configured peer IP (`OSC_TARGET_IP` on Pi A,
  `OSC_EXPECTED_SOURCE_IP` on Pi B).
- Mask, raw-depth and track telemetry travel one way from Pi A to Pi B on
  OSC/UDP 8001. The legacy `/preview` mask remains append-compatible.
- UDP has no acknowledgement layer. Pi B retries one unanswered request at
  350 ms intervals at most four times, using the same request id.

All messages below start with `version:i = 1`. Request ids are positive int32
values and wrap from `2147483647` to `1`. Revisions use the non-negative int32
space and wrap from `2147483647` to `0`.

## Requests

```text
/calibration/v1/get       <version:i> <requestId:i>
/calibration/v1/set       <version:i> <requestId:i> <expectedRevision:i>
                          <field:i> <absoluteValue:f>
/calibration/v1/save      <version:i> <requestId:i> <expectedRevision:i>
/calibration/v1/reload    <version:i> <requestId:i> <expectedRevision:i>
/calibration/v1/reset-roi <version:i> <requestId:i> <expectedRevision:i>
/calibration/v1/capture-table-mask
                          <version:i> <requestId:i> <expectedRevision:i>
/calibration/v1/clear-table-mask
                          <version:i> <requestId:i> <expectedRevision:i>
```

`set` is absolute and therefore idempotent. Integer fields require an integral
float value; the boolean field accepts only `0.0` and `1.0`, and
`handPositionMode` only `0.0`, `1.0`, or `2.0`. Pi A rejects NaN,
infinity, unreasonable magnitudes, unknown ids and stale revisions. Values
inside the wire domain still pass through `DetectionSettings::sanitize()`;
the returned state is the authoritative applied value.

Pi A caches the last 64 responses. An exact duplicate request receives the
same result and state again. Reusing a request id with different content is an
error and never changes settings.

`capture-table-mask` starts a 15-fresh-frame temporal average over the current
ROI. The table must be empty for the complete capture. At least 70% of ROI
pixels must contain valid depth in at least ten samples. Pi A immediately
confirms that capture started; progress and final success/error follow in the
state heartbeat. A completed model is atomically stored as
`table-surface-mask.bin` beside the resolved persistent `settings.json` target.
The previous active model remains in force until replacement succeeds.
`clear-table-mask` cancels capture and removes that exact persistent file.

## Results and confirmed state

```text
/calibration/v1/result <version:i> <sessionId:i> <requestId:i> <revision:i>
                       <resultCode:i> <message:s>

/calibration/v1/state  <version:i> <sessionId:i> <revision:i> <requestId:i>
                       <dirty:i> <original 22 settings fields>
                       <tableSurfaceClearanceMM:f>
                       <tableMaskActive:i> <tableMaskState:i>
                       <capturedFrames:i> <requiredFrames:i>
                       <validCoverage:f>
                       <handPositionMode:i> <inwardTipCapFraction:f>
```

Result codes are `0 ok`, `1 invalid request`, `2 stale revision`, `3 unknown
field`, `4 invalid value`, and `5 persistence error`. Pi A also publishes a
complete state heartbeat once per second with request id `0`.

The settings payload follows these stable ids and types:

| id | field | type | arrow step |
|---:|---|---|---:|
| 0 | `nearMM` | float | 25 mm |
| 1 | `farMM` | float | 25 mm |
| 2 | `minAreaRadius` | float | 1 px |
| 3 | `maxAreaRadius` | float | 5 px |
| 4 | `trackerMaximumDistance` | float | 1 px |
| 5 | `trackerPersistence` | int | 1 frame |
| 6–9 | `roiX`, `roiY`, `roiWidth`, `roiHeight` | float | 0.01 |
| 10–11 | `morphOpenRadius`, `morphCloseRadius` | int | 1 px |
| 12 | `positionSmoothingMinCutoff` | float | 0.2 Hz |
| 13 | `positionSmoothingBeta` | float | 0.1 |
| 14 | `handSolidityThreshold` | float | 0.01 |
| 15–16 | `handMinPalmRadiusMM`, `handMaxPalmRadiusMM` | float | 5 mm |
| 17 | `stationarySeconds` | float | 1 s |
| 18 | `stationaryTolerance` | float | 0.005 |
| 19 | `hideSeconds` | float | 1 s |
| 20 | `handMaxBorderWidth` | float | 0.01 |
| 21 | `usePalmCentreForHands` | int boolean | toggle |
| 22 | `tableSurfaceClearanceMM` | float | 2 mm |
| 23 | `handPositionMode` | int enum | previous/next |
| 24 | `inwardTipCapFraction` | float | 0.01 |

Existing ids are never reused. The first 23 settings and the table-mask block
keep their established positions; ids 23 and 24 are appended after that
complete 33-argument prefix so an older Pi B still reads table metadata at the
same indexes. Pi B publishes a state to the panel only after the complete
payload and all invariants have been validated.

`handPositionMode` is `0 contour centroid`, `1 palm centre`, or `2 inward tip`.
Mode 2 derives a distal cap by walking geodesically through one isolated,
border-connected hand component away from its single table entry. Ambiguous or
missing entries fall back to palm, then centroid. `inwardTipCapFraction`
(`0.05`–`0.40`) controls how much of the most distant band is averaged instead
of selecting a jitter-prone single pixel. `usePalmCentreForHands` remains as a
rollback-compatible mirror of modes 0 versus 1/2 and is not shown as a separate
current control.

Table-mask state values are `0 missing`, `1 capturing`, `2 ready`, and
`3 error`. `tableMaskActive=1` means a validated model is currently filtering
the detector; it may remain active while a replacement capture reports an
error. Coverage is the share of the captured ROI with a stable reference depth.
During capture, the detector publishes empty object frames rather than
advancing old tracks from calibration input.

For rolling compatibility, both the original 27-argument state (22 settings)
and the 33-argument table-mask state remain valid complete prefixes. A new Pi B
accepts them and marks only the unavailable newer controls unsupported. An old
Pi B ignores the appended hand-position fields from a new Pi A. The production
object packet is unchanged.

`sessionId` is a new random positive value for every Pi-A process. Pi B retires
old session ids and never applies their delayed states or results. A session
change clears pending UI state and waits for a complete new state.

`dirty=1` means the scalar settings differ from the last successful save or
reload. `save` writes a temporary file beside the persistent target, closes it
and atomically renames it. The table model is persisted by its capture command;
`reload` reloads both scalar settings and that model. A failure returns code 5
and remains visible as an error state.

When a table model is active, it replaces `farMM` as the spatially varying far
plane: a pixel must be at least `tableSurfaceClearanceMM` closer to the Kinect
than its captured reference. `nearMM` remains the global near plane. Pixels
without a valid reference are excluded. Blob height `z` uses the local captured
surface as zero; without a model, the legacy `nearMM`/`farMM` mapping remains.

## Image telemetry on UDP 8001

The existing mask remains:

```text
/preview <width:i> <height:i> <row:i> <rows:i> <pixels:b>
         <streamId:i> <sequence:i> <stripIndex:i> <stripCount:i>
```

Raw depth uses the same atomic strip shape at a new address:

```text
/calibration/depth/v1 <width:i> <height:i> <row:i> <rows:i> <pixels:b>
                      <streamId:i> <sequence:i> <stripIndex:i> <stripCount:i>
```

Both are 128 × 106, at most eight rows per packet, and published only after
every non-overlapping strip arrived. A full depth strip is 1096 bytes. Staging
expires after 500 ms; the displayed image expires after
`PREVIEW_TIMEOUT_SECONDS`.

## Track diagnostics on UDP 8001

Up to 40 tracks are partitioned into bundles of at most ten. Every bundle
begins with:

```text
/calibration/tracks/v1 <streamId:i> <sequence:i> <total:i>
                       <partIndex:i> <partCount:i> <firstIndex:i> <count:i>
```

It is followed by `count` messages:

```text
/calibration/track/v1 <streamId:i> <sequence:i> <index:i> <id:i> <type:i>
                      <rawX:f> <rawY:f> <smoothedX:f> <smoothedY:f>
                      <vx:f> <vy:f> <palmRadiusMM:f> <solidity:f>
                      <borderContact:f> <stillForSeconds:f> <hidden:i>
```

Coordinates are normalized against the full depth image so Pi B can overlay
them without knowing the Kinect resolution. `rawX/rawY` are the currently
selected unsmoothed interaction source; `smoothedX/smoothedY` are the point sent
on the production object stream. Pi B performs no contour or class calculation.
It validates and atomically publishes the full diagnostic frame.
Conflicting duplicates, duplicate track ids, impossible geometry and
non-finite measurements invalidate staging. A ten-track datagram is exactly
1204 bytes, below the 1472-byte IPv4 UDP payload at MTU 1500.

## Rolling compatibility and verification

- A new Pi B shows control as unavailable when paired with an old Pi A and
  continues draining the legacy mask without blocking; with the immediately
  preceding v1 Pi A, existing setting control remains available and only the
  table-mask mode reports that a Pi A update is required.
- A new Pi A retains `/preview`; an old Pi B ignores the new addresses.
- Calibration packets never change UDP 8000 behavior.

Offline checks:

```bash
g++ -std=c++17 -Wall -Wextra -Werror -pedantic \
  tests/calibration_protocol_test.cpp -Ishared \
  -o /tmp/calibration_protocol_test
/tmp/calibration_protocol_test
g++ -std=c++17 -Wall -Wextra -Werror -pedantic \
  tests/table_surface_mask_test.cpp sensorApp/src/TableSurfaceMask.cpp \
  -IsensorApp/src -o /tmp/table_surface_mask_test
/tmp/table_surface_mask_test
g++ -std=c++17 -Wall -Wextra -Werror -pedantic \
  tests/inward_interaction_point_test.cpp \
  sensorApp/src/InwardInteractionPointSelector.cpp \
  -IsensorApp/src -o /tmp/inward_interaction_point_test
/tmp/inward_interaction_point_test
python3 tests/calibration_v1_probe.py
```

For a live control rejection probe, stop `visualApp` on Pi B so UDP 8002 is
free, run `python3 tests/calibration_v1_probe.py --send 10.0.0.1`, then restore
the managed services. The probe sends `get` plus one unknown-field request and
does not alter accepted settings.
