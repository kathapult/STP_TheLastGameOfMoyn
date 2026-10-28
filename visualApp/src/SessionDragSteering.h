#pragma once

// Converts one tracked hand into a map-style drag vector. The first valid hand
// position of a visitor session becomes a fixed anchor; pulling away from it
// produces a direction and a proportional strength. This class deliberately
// owns only the dependency-free gesture state. Track selection, camera motion
// and the session timeout stay in CruiseNavigator.
class SessionDragSteering {
public:
	// Uses the already-calibrated physical table dimensions so equal hand travel
	// along the long and short axes produces equal vector lengths. Invalid values
	// are ignored so a transient bad projection setting cannot poison the gesture.
	void setSurfaceSize(float widthCm, float heightCm);

	// Captures the anchor on the first valid call, then updates the drag vector
	// relative to that stable point. A different controlling track starts a new
	// gesture immediately, so a handover cannot inherit the previous hand's
	// anchor. Input coordinates are normalized table space.
	void update(int trackId, float normalizedX, float normalizedY);

	// Ends only the visitor gesture. The physical surface size remains valid for
	// the next visitor and does not need to be supplied again.
	void resetSession();

	bool hasAnchor() const { return anchored; }
	bool isEngaged() const { return engaged; }
	int getTrackId() const { return ownerTrackId; }
	float getAnchorX() const { return anchorX; }
	float getAnchorY() const { return anchorY; }

	// Stabilized physical-space direction scaled by pull strength, with x
	// positive towards table-right and y positive towards the table's lower edge.
	// Its length is the same 0..1 value returned by getStrength().
	float getPullX() const { return pullX; }
	float getPullY() const { return pullY; }
	float getStrength() const { return strength; }

	// Heading consumed by CruiseNavigator: table-up is 0 radians, right is
	// +PI/2, down is PI and left is -PI/2. Neutral returns straight (0).
	float getHeadingRadians() const;

private:
	float surfaceWidthCm = 1.0f;
	float surfaceHeightCm = 1.0f;

	float anchorX = 0.5f;
	float anchorY = 0.5f;
	int ownerTrackId = -1;
	bool anchored = false;
	bool engaged = false;
	float pullX = 0.0f;
	float pullY = 0.0f;
	float strength = 0.0f;
	float stableHeadingRadians = 0.0f;

	// A pull must cross the larger radius to engage, but only returning through
	// the smaller radius releases it. This radial hysteresis rejects tracker
	// jitter without quantizing the continuous direction into angular zones.
	// Fractions use the short physical edge, preserving the gesture's real size
	// when the calibrated surface dimensions change.
	float releaseRadiusOfShortEdge = 0.04f;
	float activationRadiusOfShortEdge = 0.065f;

	// Once engaged, small angular changes remain inside this continuous
	// hysteresis window. The heading is also held while the hand returns through
	// the radial hysteresis band, where a short vector makes ordinary position
	// noise look like a large and unintended direction change.
	float directionHysteresisRadians = 0.10f;

	// The length at which the drag reaches full strength. Direction remains fully
	// continuous beyond it; only the magnitude is capped.
	float fullPullRadiusOfShortEdge = 0.28f;
};
