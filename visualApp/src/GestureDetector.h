#pragma once

#include "ofMain.h"

// sensor-agnostic gesture detection (glm::vec3 in, no Leap API dependency)
class GestureDetector {
public:
	GestureDetector();

	// true while fist held (hysteresis, continuous)
	bool isGrabHeld(float grabStrength, float deltaTime);

	// true once both hands fist together
	bool checkBothHandsGrabHoldToggle(float grabStrengthA, float grabStrengthB, float deltaTime);

	// fist-hold thresholds
	float grabThreshold = 0.8f;
	float releaseThreshold = 0.4f; // hysteresis
	float holdTime = 0.3f; // seconds

private:
	// two-hand gesture state
	float bothGrabTimer = 0.0f;
	bool bothGrabReady = true;

	// isGrabHeld() state
	float grabHoldTimer = 0.0f;
	bool grabHeld = false;
};