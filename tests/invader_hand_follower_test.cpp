#include "InvaderHandFollower.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

namespace {

bool near(float actual, float expected, float tolerance = 0.0001f) {
	return std::fabs(actual - expected) <= tolerance;
}

void testAcquisitionAndHandoverAreContinuous() {
	InvaderHandFollower follower;
	follower.reset(0.5f);

	assert(near(follower.update(10, 0.85f, 1.0f / 60.0f), 0.5f));
	assert(follower.getDriverId() == 10);

	for (int i = 0; i < 60; ++i) {
		follower.update(10, 0.85f, 1.0f / 60.0f);
	}
	assert(follower.getTarget() > 0.82f);

	const float beforeHandover = follower.getTarget();
	assert(near(follower.update(24, 0.15f, 1.0f / 60.0f), beforeHandover));
}

void testContinuingHandGuidesTargetByItsMovement() {
	InvaderHandFollower follower;
	follower.reset(0.5f);
	follower.update(3, 0.5f, 1.0f / 60.0f);

	assert(near(follower.update(3, 0.62f, 1.0f / 60.0f), 0.62f));
	assert(near(follower.update(3, 0.41f, 1.0f / 60.0f), 0.41f));
}

void testGapAndInvalidSamplesHoldTarget() {
	InvaderHandFollower follower;
	follower.reset(0.4f);
	follower.update(7, 0.4f, 1.0f / 60.0f);
	follower.update(7, 0.6f, 1.0f / 60.0f);
	const float held = follower.getTarget();

	follower.releaseDriver();
	assert(near(follower.getTarget(), held));
	assert(near(follower.update(-1, 0.9f, 1.0f / 60.0f), held));
	assert(near(follower.update(8, std::numeric_limits<float>::quiet_NaN(),
		1.0f / 60.0f), held));
	assert(near(follower.update(8, 0.1f, 1.0f / 60.0f), held));
	assert(std::isfinite(follower.update(8, 0.1f,
		std::numeric_limits<float>::quiet_NaN())));
}

} // namespace

int main() {
	testAcquisitionAndHandoverAreContinuous();
	testContinuingHandGuidesTargetByItsMovement();
	testGapAndInvalidSamplesHoldTarget();

	std::cout << "invader hand follower tests passed\n";
	return 0;
}
