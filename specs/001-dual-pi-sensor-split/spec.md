# Feature Specification: Dual-Pi Sensor/Render Split

**Feature Branch**: `001-dual-pi-sensor-split`

**Created**: 2026-08-06

**Status**: Draft

**Input**: User description: "Aufteilung der Arbeit auf zwei Raspberry Pis: Pi A besitzt die Kinect v2, führt die Objekterkennung auf dem Tiefenbild aus und sendet die erkannten Objekte an Pi B. Pi B rendert nur noch die Visuals und konsumiert den Objektstrom, ohne selbst Sensor- oder CV-Code auszuführen. Hintergrund: ein einzelner Pi 4 schafft Sensorerfassung, Detection und Rendering nicht gleichzeitig."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Visuals stay responsive under full sensing + detection load (Priority: P1)

Today, a single Raspberry Pi 4 is expected to read the Kinect sensor, run object detection on the depth image, and render the interactive visuals all at once — more work than the device can sustain smoothly. This story splits that load across two Pis: one dedicated to sensing and detection ("Pi A"), one dedicated purely to rendering ("Pi B"). A visitor interacting with the installation should see smooth, real-time visuals regardless of how much sensing/detection work is happening, because that work no longer competes with rendering on the same device.

**Why this priority**: This is the core problem statement — without it, the installation cannot reliably run detection and rendering together at all, which is what makes this feature necessary in the first place.

**Independent Test**: Can be fully tested by running Pi B alone, feeding it a stream of test object data (independent of whether real detection is implemented yet) over the network link, and confirming the visuals render smoothly and react to that stream — without Pi B ever touching sensor or detection code.

**Acceptance Scenarios**:

1. **Given** Pi A is capturing sensor data and running detection, and Pi B is rendering visuals, **When** a visitor moves in front of the sensor, **Then** the visuals on Pi B respond in real time with no visible stutter or lag attributable to the detection workload.
2. **Given** Pi B is running on its own with no rendering-side sensor or detection code present, **When** it receives a stream of detected-object data over the network, **Then** it drives the existing hand/object-influenced visual behavior purely from that received data.

---

### User Story 2 - Pi A turns raw depth data into a usable object stream (Priority: P2)

Pi A owns the Kinect v2 and must turn its raw depth image into a live stream of detected objects (e.g., a visitor's hand or body positioned in front of the sensor), each identifiable and trackable frame-to-frame, and continuously send that stream to Pi B.

**Why this priority**: Without this, Pi B has nothing meaningful to render beyond static/idle visuals — this is what makes the split actually useful rather than just an empty architecture change. It's second priority because the plumbing in Story 1 can be built and verified first using placeholder data.

**Independent Test**: Can be fully tested on Pi A alone (no Pi B required) by confirming that moving an object in front of the Kinect produces a corresponding, updating detected-object stream that a developer can inspect directly on Pi A.

**Acceptance Scenarios**:

1. **Given** Pi A is running with the Kinect connected, **When** a visitor's hand enters the sensor's field of view, **Then** Pi A's outgoing object stream includes an entry representing that hand's approximate position.
2. **Given** a visitor's hand is continuously visible and moving, **When** consecutive frames are captured, **Then** the corresponding detected object keeps the same identifier across those frames rather than being reported as a new object each time.
3. **Given** a visitor's hand leaves the sensor's field of view, **When** the next frames are captured, **Then** that object stops appearing in the outgoing stream.

---

### User Story 3 - Rendering keeps running when the sensor Pi is unavailable (Priority: P3)

Pi B must stay usable and stable even when Pi A isn't sending data — for example during setup, when Pi A hasn't started yet, when Pi A is restarted mid-show, or if the network link between them is briefly interrupted.

**Why this priority**: This is a robustness/operational concern rather than the core value of the split, but matters for a live installation where a visitor should never see a frozen or crashed screen because of a problem on the other device.

**Independent Test**: Can be fully tested by starting Pi B with Pi A powered off or disconnected, confirming the visuals still run, then connecting/starting Pi A and confirming the visuals pick up the incoming object stream without needing a restart.

**Acceptance Scenarios**:

1. **Given** Pi B is running and Pi A is not sending any data, **When** the installation is observed, **Then** Pi B's visuals continue running (matching today's behavior when no objects are detected) instead of freezing or crashing.
2. **Given** Pi B has been running without data from Pi A, **When** Pi A starts sending its object stream, **Then** Pi B begins reflecting that data in the visuals without requiring Pi B to be restarted.
3. **Given** Pi A and Pi B are both running normally, **When** the network link between them is briefly interrupted and then restored, **Then** Pi B resumes reflecting live object data once the link is back, without crashing during the interruption.

---

### Edge Cases

- What happens when Pi A detects an unusually large number of objects at once (e.g., sensor noise, multiple visitors)? The stream should not overwhelm Pi B or degrade its rendering frame rate.
- What happens if Pi B starts up before Pi A has ever sent any data? It must behave the same as "no objects currently detected," not as an error state.
- What happens if the two devices are connected but misconfigured (e.g., not actually reachable over the network link)? This should be observable/debuggable by an operator setting up the installation, not a silent failure.
- What happens to an object's identifier if it briefly leaves and re-enters the sensor's field of view (e.g., a visitor's hand dips out of range for a moment)? Reasonable to treat as a new object if the gap is more than a brief moment; exact threshold is a tuning detail, not a scope-defining one.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The installation MUST split its work across two devices: one device ("Pi A") that owns the depth sensor and performs object detection, and a second device ("Pi B") that renders the interactive visuals.
- **FR-002**: Pi B MUST render and update all visuals without executing any sensor-reading or object-detection logic itself.
- **FR-003**: Pi A MUST NOT render any visuals; its role is limited to capturing sensor data, detecting objects, and transmitting the results.
- **FR-004**: Pi A MUST continuously send its current set of detected objects to Pi B over a direct connection between the two devices, at a rate sufficient to drive real-time visual response with no visitor-perceptible lag.
- **FR-005**: Each detected object MUST carry at least a position and a tracking identifier that stays stable across consecutive frames for as long as the same physical object continues to be tracked.
- **FR-006**: Pi B MUST feed the incoming detected-object stream into the existing position-based visual behavior (the grid influence field), keeping visuals free of any direct dependency on sensor or detection internals. The existing grab-triggered pause behavior is explicitly out of scope for this feature (see Assumptions) and MUST NOT be treated as a requirement to preserve.
- **FR-007**: If Pi B is not currently receiving object data for any reason (Pi A not yet started, network link down, etc.), Pi B's visuals MUST continue running and rendering rather than freezing, crashing, or blocking while waiting for data.
- **FR-008**: The installation MUST tolerate occasional gaps in the object-data updates between Pi A and Pi B without requiring either device to reconnect or restart.
- **FR-009**: The two devices MUST be operable as physically separate machines connected only via a direct link between them, with no dependency on internet access or an intermediary network device.
- **FR-010**: An operator MUST be able to verify Pi A's sensing/detection role and Pi B's rendering role independently of one another (e.g., confirm Pi A is producing a detected-object stream without Pi B attached, and confirm Pi B renders correctly given a test stream without Pi A attached).

### Key Entities

- **Detected Object**: One physical object or hand tracked by Pi A's detection step at a point in time — a position and a tracking identifier stable across consecutive frames. Generalizes today's hand-only, always-empty data into something populated by real detection and not limited to hands.
- **Object Stream**: The ongoing, real-time sequence of detected-object sets sent from Pi A to Pi B, one set per processed sensor frame.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: The rendering device sustains smooth, real-time visual playback (no visitor-perceptible stutter) during normal operation, whether or not the sensor device is currently connected.
- **SC-002**: A visitor's movement in front of the sensor is reflected in the visuals within a fraction of a second, so the interaction feels immediate rather than delayed.
- **SC-003**: A tracked object keeps its identifier across at least 95% of consecutive frames while continuously visible to the sensor, so movement-driven visual effects feel continuous rather than flickering between "new" objects.
- **SC-004**: If the sensor device is disconnected, restarted, or loses its network link mid-show, the rendering device shows zero crashes or freezes, and automatically resumes reflecting live data once the link is restored.
- **SC-005**: An operator can set up both devices (power, network link, start both applications) and have the full two-device installation running within about the same setup time the single-device installation takes today, plus a few minutes for the second device.

## Assumptions

- The two Raspberry Pis are connected via a dedicated, direct network link between them (point-to-point, no router/DHCP in between), consistent with the split already planned in `processDocumentation.md`.
- A detected object's required attributes are limited to position and a stable tracking identifier (see FR-005). Richer attributes (per-finger detail, orientation, palm normal, a grab/pinch-style engagement value, etc.) are not required by this feature.
- Pi B's existing "no objects currently tracked" behavior (visuals keep running unaffected) is reused as the baseline behavior whenever no data is arriving from Pi A, rather than introducing a new idle/error state.
- Only the position-based grid influence effect is reused from the existing visual logic on top of the new object stream. The existing grab-triggered pause behavior (`GestureDetector::isGrabHeld`, pausing the simulation on a held fist) is explicitly dropped, not carried over — it is Leap-Motion-era gesture logic with no established equivalent in depth-based object detection. A future grab/pinch-equivalent gesture, if wanted, would be a separate later feature.
- Renaming the current hand-only data/vector naming to something object-generic is treated as in scope for this feature, since generalizing beyond hand-only data is inherent to feeding it from a general object-detection step rather than a hand-tracking sensor. Beyond that, the generic naming is deliberate groundwork: hand recognition is planned as the next feature, with further object types likely after it. The detection step will then distinguish *kinds* of detected object rather than being replaced — which is why the type is named for what it always is (a detected object) rather than for what one variety of it happens to be.
