#pragma once

// Combines hand height and the deliberate two-hand boost into a bounded,
// smoothed flight-speed multiplier. It is independent from camera and tracking
// code so the response curve can be verified without openFrameworks.
class CruiseSpeedEnvelope {
public:
	// Without a driver the target is ordinary cruise speed. With a driver, a low
	// hand is somewhat faster and a high hand somewhat slower; boost remains
	// noticeable but the combined result has a hard comfort ceiling.
	float update(bool hasDriver, float controlHeight,
		float speedBoost, float deltaTime);

	void reset();
	float getMultiplier() const { return multiplier; }

private:
	float multiplier = 1.0f;

	float highHandMultiplier = 0.80f;
	float lowHandMultiplier = 1.25f;
	float fullBoostAddition = 0.50f;
	float maximumMultiplier = 1.70f;

	// Acceleration builds deliberately; deceleration catches up more quickly so
	// a released gesture does not keep the visitor in an unexpectedly fast flyby.
	float followUp = 1.8f;
	float followDown = 5.0f;
};
