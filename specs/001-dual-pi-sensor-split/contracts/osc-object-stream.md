# Contract: Object Stream (Pi A → Pi B)

This contract is the live, one-way interface between `sensorApp` on Pi A and
`visualApp` on Pi B. Version 1 adds explicit snapshot boundaries while keeping
the original `/object` prefix compatible with older releases.

## Transport and endpoint policy

- OSC over UDP via `ofxOsc`; there are no acknowledgements or retries.
- Pi A sends object data to UDP 8000. `OSC_TARGET_IP` selects the destination
  and defaults to `10.0.0.2`.
- The optional calibration preview uses UDP 8001.
- Pi B accepts packets only when the source returned by `ofxOsc` equals
  `OSC_EXPECTED_SOURCE_IP`, which defaults to `10.0.0.1`. Rejected packets are
  counted and shown in the diagnostic HUD.
- The Pis are connected by a point-to-point Ethernet link with static IPv4
  addresses. Neither transport path depends on a router, DHCP or the internet.

## Version 1 object snapshots

Every processed sensor frame has one identity:

```text
streamId:int32 sequence:int32 senderTimeUs:int64
```

`streamId` is a random positive value generated when `ObjectStreamSender`
starts. A changed id means a sender restart and allows its sequence to begin at
zero without being confused with delayed packets from the previous process.
`sequence` uses the non-negative int32 range and wraps explicitly from
`2147483647` to `0`. `senderTimeUs` is Pi A's monotonic process time in
microseconds; it is diagnostic metadata and is never compared with Pi B's
clock.

A snapshot is split into one to three independently delivered OSC bundles.
Every bundle begins with this header:

```text
/frame/v1 <streamId:i> <sequence:i> <senderTimeUs:h>
          <totalObjectCount:i> <partIndex:i> <partCount:i>
          <firstObjectIndex:i> <partObjectCount:i>
```

The header is followed by exactly `partObjectCount` messages:

```text
/object <id:i> <x:f> <y:f> <z:f> <area:f>
        <vx:f> <vy:f> <age:f> <type:i>
        <streamId:i> <sequence:i> <objectIndex:i>
```

The first nine `/object` arguments retain the legacy contract. The final three
bind the message to one slot in one versioned snapshot.

### Field meanings

- `id`: tracker id, unique within the complete snapshot and normally stable
  while the same contour remains tracked.
- `x`, `y`: interaction position normalized to the configured rounded table
  ROI's bounding rectangle, each in 0–1. Pi A rejects pixels outside the
  physical table outline. Non-hands use the contour centroid. For hands the
  calibrated source is contour centroid, palm centre, or a stable distal cap
  furthest along the component from its single table-edge entry. Invalid or
  ambiguous inward geometry falls back to palm, then centroid. A source change
  is eased with zero reported velocity so it cannot look like real motion.
- `z`: normalized height in Pi A's configured depth window, 0 at its far/table
  edge and 1 at its near edge.
- `area`: share of the ROI's bounding rectangle covered by the table-clipped
  object mask, in 0–1. The rectangular denominator preserves the v1 scale.
- `vx`, `vy`: normalized ROI units per second.
- `age`: seconds since the tracker state was created.
- `type`: `0` unknown, `1` object and `2` hand. Unknown numeric values are
  mapped to `Unknown` by Pi B.
- `objectIndex`: zero-based position in the deterministic transmitted snapshot.

All numeric values must be finite. Normalized fields, ids, counts and indexes
are range-checked before a snapshot can be published.

### Deterministic limit and packet partition

Pi A sends at most 40 objects. It orders them before applying that cap:

1. hands before non-hands;
2. larger mask area first;
3. lower tracker id first.

Each bundle carries at most 17 objects. The partition is fixed rather than
negotiated: `firstObjectIndex = partIndex * 17`, and the final part carries the
remainder. An empty snapshot is one header with `totalObjectCount = 0`,
`partIndex = 0`, `partCount = 1` and no `/object` messages. It is both an
explicit removal signal and a heartbeat; network silence is a different state.

### Receiver commit rules

`ObjectFrameAssembler` buffers headers and objects by stream and sequence. It
publishes a snapshot only after all declared part headers and every unique
object slot have arrived. Part order and exact duplicate packets do not matter.
Conflicting duplicates, duplicate object ids, invalid metadata and impossible
values invalidate the affected snapshot.

Incomplete snapshots never leak into a visual. They expire after 150 ms, are
also discarded when a newer complete snapshot supersedes them, and increment a
diagnostic counter. A missing sequence increments the gap counter but is not
interpreted as an empty snapshot. Late or duplicate sequences cannot overwrite
the last committed sequence.

After a complete snapshot, Pi B may dead-reckon its objects from `vx`/`vy` for
at most 350 ms while waiting for the next complete snapshot. An explicit empty
snapshot clears this state immediately. Prolonged packet silence is logged
separately after two seconds.

## Datagram size budget

The byte counts below include OSC bundle/message framing but exclude the UDP/IP
headers. They are compile-time constants in `shared/ObjectStreamProtocol.h` and
are independently checked by `tests/osc_v1_probe.py`.

| Element | Bytes |
|---|---:|
| OSC bundle header | 16 |
| `/frame/v1` bundle element | 64 |
| extended `/object` bundle element | 76 |
| largest object bundle, 17 objects | **1372** |
| IPv4 UDP payload at MTU 1500 | **1472** |

The 100-byte margin prevents IPv4 fragmentation on the installation link. A
40-object snapshot uses three datagrams of 17, 17 and 6 objects.

## Calibration preview

When `sensorApp` is started with `--preview`, every third fresh sensor frame is
downscaled to 128 × 106 and sent in strips of at most eight rows:

```text
/preview <width:i> <height:i> <row:i> <rows:i> <pixels:b>
         <streamId:i> <sequence:i> <stripIndex:i> <stripCount:i>
```

The first five fields remain compatible with the legacy preview. Version 1
uses the same stream id and sequence as the object snapshot from which the mask
was produced. A full eight-row packet is 1084 bytes and remains below the same
1472-byte limit.

Pi B stages versioned strips separately from the displayed image. It publishes
only after every strip and every image row arrived exactly once; inconsistent,
overlapping or incomplete frames are discarded. Staging expires after 500 ms.
The last complete image is shown only for `PREVIEW_TIMEOUT_SECONDS` (default
1.0 seconds). Preview reception exists only in the explicit read-only
`--diagnostics` or writable `--calibration` maintenance profile.

## Rolling compatibility

- **New Pi A → old Pi B:** the original nine `/object` fields have unchanged
  positions and meaning. The old receiver ignores `/frame/v1` and trailing
  fields; multipart delivery may retain its historical short hold behavior.
- **Old Pi A → new Pi B:** three-, eight- and nine-argument `/object` messages
  remain accepted. If framed traffic has been silent for 750 ms, the receiver
  falls back to legacy processing without a restart.
- **Preview:** the first five fields remain readable by an old receiver. A new
  receiver retains direct strip display only while talking to an old sender;
  complete-only publication applies to versioned preview frames.

This compatibility is for rolling deployment and rollback. Version 1 is the
authoritative production behavior once both Pis run the same release.

## Verification

The platform-independent assembler and sequence tests are built with:

```bash
g++ -std=c++17 -Wall -Wextra -Werror \
  -Ishared -IvisualApp/src \
  tests/object_frame_protocol_test.cpp \
  visualApp/src/ObjectFrameAssembler.cpp \
  -o object_frame_protocol_test
./object_frame_protocol_test
```

The dependency-free wire probe checks real OSC padding and all packet-size
boundaries:

```bash
python3 tests/osc_v1_probe.py
```

For an end-to-end Pi test, stop `sensorApp-update.timer` and `sensorApp`, start Pi B's
`visualApp-calibration.service`, and run the probe on Pi A so its source address
matches Pi B's policy:

```bash
python3 tests/osc_v1_probe.py --send 10.0.0.2
```

It sends empty, normal, multipart, reordered, duplicated and deliberately
incomplete snapshots plus a reordered complete preview. Pi B's diagnostics
must end on the explicit empty snapshot and expose the induced gap/partial
counters without ever showing a partial object or preview frame.
