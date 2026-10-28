#include "ModeRotationScheduler.h"

#include <cassert>
#include <cmath>
#include <iostream>

namespace {
void advance(ModeRotationScheduler & scheduler, float seconds,
	bool interactionActive, bool expectedSwitch = false) {
	const float step = 0.1f;
	bool switched = false;
	for (float elapsed = 0.0f; elapsed + step <= seconds + 0.0001f; elapsed += step) {
		switched = scheduler.update(step, interactionActive) || switched;
	}
	assert(switched == expectedSwitch);
}
}

int main() {
	ModeRotationScheduler scheduler;

	// With an empty table the regular deadline is enough: its minimum interval
	// is already longer than the 25-second quiet grace.
	scheduler.reset(30.0f);
	advance(scheduler, 29.9f, false);
	assert(!scheduler.isOverdue());
	assert(scheduler.update(0.1f, false));
	assert(scheduler.isOverdue());

	// Interaction spanning the deadline defers the switch for as long as it
	// lasts, then for 25 complete quiet seconds.
	scheduler.reset(30.0f);
	advance(scheduler, 35.0f, true);
	assert(scheduler.isOverdue());
	assert(std::abs(scheduler.getIdleSeconds()) < 0.001f);
	advance(scheduler, 24.9f, false);
	assert(scheduler.update(0.1f, false));

	// A new interaction during the grace period restarts it instead of letting
	// an old partial idle interval trigger a mid-session change.
	scheduler.reset(30.0f);
	advance(scheduler, 31.0f, true);
	advance(scheduler, 20.0f, false);
	assert(!scheduler.update(0.1f, true));
	advance(scheduler, 24.9f, false);
	assert(scheduler.update(0.1f, false));

	// Bad frame times do not move either clock.
	scheduler.reset(45.0f);
	assert(!scheduler.update(0.0f, false));
	assert(!scheduler.update(-1.0f, false));
	assert(!scheduler.update(NAN, false));
	assert(std::abs(scheduler.getSecondsUntilDue() - 45.0f) < 0.001f);

	std::cout << "mode rotation scheduler tests passed" << std::endl;
	return 0;
}
