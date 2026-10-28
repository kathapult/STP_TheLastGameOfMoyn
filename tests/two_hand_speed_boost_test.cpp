#include "TwoHandSpeedBoost.h"
#include <cassert>
#include <cmath>
#include <iostream>

namespace {
TwoHandSpeedBoost::HandMotion hand(int id, float x, float y,
	float velocityX, float velocityY) {
	return { id, x, y, velocityX, velocityY };
}
}

int main() {
	TwoHandSpeedBoost boost;
	boost.setSurfaceSize(188.0f, 79.0f);

	// The first sample from a pair only establishes identity. A large reported
	// velocity on a new or split track cannot create a one-frame acceleration.
	auto left = hand(4, 0.40f, 0.50f, -0.30f, 0.0f);
	auto right = hand(9, 0.60f, 0.50f, 0.30f, 0.0f);
	assert(boost.update(true, left, right, 1.0f / 60.0f) == 0.0f);

	// Persistent hands moving apart along the physically long table axis raise
	// the boost. Reversing input order is still the same pair.
	float rising = boost.update(true, right, left, 1.0f / 60.0f);
	assert(rising > 0.0f && rising < 1.0f);
	for (int i = 0; i < 60; i++) {
		boost.update(true, left, right, 1.0f / 60.0f);
	}
	assert(boost.getLevel() > 0.75f);
	assert(boost.getLevel() <= 1.0f);
	float sixtyFpsLevel = boost.getLevel();

	// The boost reads velocities already expressed per second and uses a
	// time-constant follower, so 30 and 60 render updates produce the same
	// one-second response rather than doubling the gesture.
	TwoHandSpeedBoost boostAtThirtyFps;
	boostAtThirtyFps.setSurfaceSize(188.0f, 79.0f);
	boostAtThirtyFps.update(true, left, right, 1.0f / 30.0f);
	for (int i = 0; i < 30; i++) {
		boostAtThirtyFps.update(true, left, right, 1.0f / 30.0f);
	}
	assert(std::fabs(boostAtThirtyFps.getLevel() - sixtyFpsLevel) < 0.01f);

	// Hand height is absent from HandMotion: opposite vertical movement cannot
	// alter this planar, physically calibrated gesture.
	left.velocityX = 0.0f;
	right.velocityX = 0.0f;
	float beforeRelease = boost.getLevel();
	float afterRelease = boost.update(true, left, right, 1.0f / 60.0f);
	assert(afterRelease < beforeRelease);

	// A different pair with an extreme first reading must establish a fresh
	// identity while the old boost decays, then a missing/ambiguous pair releases
	// it promptly and cannot leave stale pair state behind.
	auto replacement = hand(12, 0.95f, 0.50f, 2.0f, 0.0f);
	float beforeHandover = boost.getLevel();
	float afterHandover = boost.update(true, left, replacement, 1.0f / 60.0f);
	assert(afterHandover < beforeHandover);

	TwoHandSpeedBoost::HandMotion none;
	for (int i = 0; i < 60; i++) {
		boost.update(false, none, none, 1.0f / 60.0f);
	}
	assert(boost.getLevel() < 0.01f);
	assert(boost.update(true, left, right, 1.0f / 60.0f) < 0.01f);

	std::cout << "two-hand speed boost tests passed\n";
	return 0;
}
