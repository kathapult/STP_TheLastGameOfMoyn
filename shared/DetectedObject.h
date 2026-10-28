#pragma once

// What Pi A believes a detected blob is. Classification happens entirely on Pi
// A, from the mask; Pi B only ever reads the answer (Principle II).
//
// The numeric values are part of the wire format - they are what travels as
// the OSC type argument - so they must never be reordered or reused. New kinds
// get appended with the next free number, and an unrecognized value read on Pi
// B is treated as Unknown rather than rejected.
enum class ObjectType {
	Unknown = 0, // not classified, or classification not confident
	Object = 1,  // sits on the table by itself - a cup, a prop, a forgotten phone
	Hand = 2     // reaches in from outside, i.e. attached to a person
};

// Converts a value off the wire. Anything not recognized becomes Unknown
// rather than being rejected or cast blindly, so a future Pi A that introduces
// a new kind cannot make an older Pi B read garbage.
inline ObjectType toObjectType(int wireValue) {
	switch (wireValue) {
	case 1:
		return ObjectType::Object;
	case 2:
		return ObjectType::Hand;
	default:
		return ObjectType::Unknown;
	}
}

// Short label for debug overlays on both devices.
inline const char * objectTypeLabel(ObjectType type) {
	switch (type) {
	case ObjectType::Object:
		return "obj";
	case ObjectType::Hand:
		return "hand";
	default:
		return "?";
	}
}

// Replaces HandData - sensor-agnostic, generalized beyond hands now that
// detection comes from a CV pipeline rather than a hand-tracking sensor.
// Shared between sensorApp (Pi A, produces these) and visualApp (Pi B,
// consumes these) - see research.md item 3.
//
// Every field is normalized and frame-rate independent on purpose: Pi B must
// stay ignorant of the depth resolution, the millimetre range of the depth
// window, the region of interest and Pi A's frame rate (constitution
// Principle II). Pi A owns all of that; what crosses the wire is only ever
// "where within the table, how big, how high, how fast".
struct DetectedObject {
	// Stable across consecutive frames while the same physical object is
	// tracked.
	int id = 0;

	// Interaction position within the rounded table ROI's bounding rectangle,
	// 0.0-1.0. Depending on Pi A's calibrated hand-position mode this is the
	// contour centre, palm centre, or stable inward distal cap; non-hands use
	// their contour centre. TableShape excludes its cut corners before tracking,
	// so no valid point can originate beyond the physical table edge.
	float x = 0.0f;
	float y = 0.0f;

	// Height above the far edge of the depth window, 0.0-1.0: 0.0 sits at
	// farMM (the table surface), 1.0 at nearMM (closest to the sensor). Sent
	// as a fraction rather than millimetres so Pi B does not need to know
	// where the depth window was set - a hand lifted off the table reads the
	// same whether the sensor hangs 1 m or 2 m up.
	float z = 0.0f;

	// Share of the ROI's bounding rectangle the object covers, 0.0-1.0.
	// Measured from the table-clipped mask, so it is the actual filled area
	// rather than the blob's bounding box - a spread hand and a fist of the
	// same width differ here. The rectangular denominator preserves the v1
	// value scale when the table-shaped mask removes its corners.
	float area = 0.0f;

	// Movement in normalized units per second, along the same axes as x/y.
	// Per second rather than per frame so the value does not change meaning
	// if Pi A's frame rate drifts. Zero on the frame an object first appears,
	// since there is nothing to compare against yet.
	float vx = 0.0f;
	float vy = 0.0f;

	// Seconds since this id was first seen. Lets a visual fade an object in
	// instead of having it appear at full strength, and tell a settled object
	// from one that just arrived.
	float age = 0.0f;

	// What Pi A thinks this is. Unknown is a legitimate everyday value, not an
	// error state - a visual should treat it as "probably worth reacting to"
	// rather than skipping it, since silently ignoring anything unclassified
	// would make a misclassification read as broken hardware.
	ObjectType type = ObjectType::Unknown;
};
