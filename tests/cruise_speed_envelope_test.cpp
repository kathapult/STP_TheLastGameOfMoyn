#include "CruiseSpeedEnvelope.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
	CruiseSpeedEnvelope speed;

	// Maximum height-plus-boost input accelerates gradually and never exceeds the
	// comfort ceiling, even after it has fully settled.
	float firstFastFrame = speed.update(true, 0.0f, 1.0f, 1.0f / 60.0f);
	assert(firstFastFrame > 1.0f && firstFastFrame < 1.05f);

	CruiseSpeedEnvelope speedAtThirtyFps;
	for (int i = 0; i < 30; i++) {
		speedAtThirtyFps.update(true, 0.0f, 1.0f, 1.0f / 30.0f);
	}
	CruiseSpeedEnvelope speedAtSixtyFps;
	for (int i = 0; i < 60; i++) {
		speedAtSixtyFps.update(true, 0.0f, 1.0f, 1.0f / 60.0f);
	}
	assert(std::fabs(speedAtThirtyFps.getMultiplier()
		- speedAtSixtyFps.getMultiplier()) < 0.001f);

	for (int i = 0; i < 600; i++) {
		speed.update(true, 0.0f, 1.0f, 1.0f / 60.0f);
	}
	assert(speed.getMultiplier() > 1.65f);
	assert(speed.getMultiplier() <= 1.70f);

	// Releasing the gesture decelerates faster than it accelerated and returns to
	// ordinary cruise when no hand is driving.
	float fast = speed.getMultiplier();
	float firstReleaseFrame = speed.update(false, 0.0f, 0.0f, 1.0f / 60.0f);
	assert(firstReleaseFrame < fast);
	for (int i = 0; i < 120; i++) {
		speed.update(false, 0.0f, 0.0f, 1.0f / 60.0f);
	}
	assert(speed.getMultiplier() > 0.999f && speed.getMultiplier() < 1.001f);

	// Height still has a legible but compressed effect: a high hand slows the
	// flight without stopping it, and a low hand alone stays well below the cap.
	speed.reset();
	for (int i = 0; i < 600; i++) {
		speed.update(true, 1.0f, 0.0f, 1.0f / 60.0f);
	}
	assert(speed.getMultiplier() >= 0.80f && speed.getMultiplier() < 0.81f);

	speed.reset();
	for (int i = 0; i < 600; i++) {
		speed.update(true, 0.0f, 0.0f, 1.0f / 60.0f);
	}
	assert(speed.getMultiplier() > 1.24f && speed.getMultiplier() < 1.26f);

	std::cout << "cruise speed envelope tests passed\n";
	return 0;
}
