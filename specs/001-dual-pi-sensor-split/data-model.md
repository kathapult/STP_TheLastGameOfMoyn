# Phase 1 Data Model: Dual-Pi Sensor/Render Split

## DetectedObject

`shared/DetectedObject.h` is the only sensor-to-visual value type. Pi A creates
it from depth/CV data; Pi B's visuals consume it without knowing about Kinect,
OpenCV or OSC.

| Field | Type | Meaning |
|---|---|---|
| `id` | integer | Tracker label. Unique in a committed snapshot and normally stable while the contour remains tracked. |
| `x`, `y` | float | Position normalized to the configured rounded table ROI's bounding rectangle, each in 0–1. Pixels outside the physical table outline are rejected. For a classified hand this may be its palm centre. |
| `z` | float | Normalized height within the configured depth window, in 0–1. |
| `area` | float | Fraction of the ROI's bounding rectangle covered by the table-clipped contour mask, in 0–1. |
| `vx`, `vy` | float | Motion in normalized ROI units per second. |
| `age` | float | Seconds since Pi A created the current tracker state. |
| `type` | `ObjectType` | `Unknown`, `Object` or `Hand`; unrecognized wire values become `Unknown`. |

All floating-point wire values must be finite. `id` and `age` must be
non-negative. Classification, metric depth interpretation and palm-centre
selection happen entirely on Pi A.

## Versioned object snapshot

A sensor frame produces a complete set of zero to 40 `DetectedObject` values.
The wire representation adds metadata that is not exposed to visuals:

| Field | Purpose |
|---|---|
| `streamId` | Positive process/session id; changes when Pi A's sender restarts. |
| `sequence` | Non-negative int32 frame number with explicit modulo-2³¹ wrap. |
| `senderTimeUs` | Monotonic Pi A process time for diagnostics, not clock synchronization. |
| `totalObjectCount` | Exact number of object slots required before commit. |
| `partIndex`, `partCount` | Position and number of MTU-safe UDP parts. |
| `firstObjectIndex`, `partObjectCount` | Exact object-index interval carried by this part. |

The set is deterministically ordered (hands, then area, then id), partitioned
at 17 objects per UDP datagram and committed atomically on Pi B. An empty
snapshot has an explicit header with count zero; it is not represented by the
absence of UDP data.

### Lifecycle on Pi A

- A new tracked contour creates an id and begins appearing in snapshots.
- A matched contour preserves its id while position, velocity, age, area,
  height and classification update.
- Tracker memory handles short contour gaps. Once a track is dropped it is
  absent from the next complete snapshot.
- If more than 40 contours exist, Pi A retains hands first and then the largest
  remaining contours using the deterministic ordering above.

### Lifecycle on Pi B

- Headers and objects are staged by `(streamId, sequence)`; their UDP arrival
  order does not define application order.
- Only a structurally valid, complete snapshot replaces the current object
  state. No incremental merge or partial publication occurs for version 1.
- An explicit empty snapshot clears all objects immediately.
- Packet loss creates an incomplete/missing-sequence diagnostic, not an empty
  snapshot. The most recent complete values may be extrapolated for at most
  350 ms from their velocities, then expire.
- A changed `streamId` clears old pending and displayed object state before the
  restarted sender's first complete snapshot is accepted. Retired-stream and
  older-sequence packets cannot replace it.

The exact OSC encoding, MTU calculation, compatibility behavior and preview
model are defined in `contracts/osc-object-stream.md`.
