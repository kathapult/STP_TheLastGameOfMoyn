#pragma once

#include <algorithm>
#include <cmath>

// Turns an exclusive hand track into the horizontal target used by Invaders.
// A continuing driver remains an absolute position control: once acquired, a
// hand moving left or right moves the target by the same amount. Only a real
// driver-id change is treated specially. The replacement starts from the
// current target and its offset is then removed smoothly, so an id split or a
// handover cannot command a one-frame jump across the table.
class InvaderHandFollower {
public:
	void reset(float initialTarget = 0.5f) {
		target = std::clamp(initialTarget, 0.0f, 1.0f);
		driverId = -1;
		driverOffset = 0.0f;
	}

	// Releases identity without moving the ship. If the same sensor id returns
	// after a real gap it is acquired like a new driver instead of inheriting a
	// stale offset from an earlier visit.
	void releaseDriver() {
		driverId = -1;
		driverOffset = 0.0f;
	}

	float update(int newDriverId, float mappedHandX, float deltaTime) {
		if (newDriverId < 0 || !std::isfinite(mappedHandX)) {
			return target;
		}

		mappedHandX = std::clamp(mappedHandX, 0.0f, 1.0f);
		deltaTime = std::isfinite(deltaTime)
			? std::clamp(deltaTime, 0.0f, 0.1f)
			: 0.0f;

		if (newDriverId != driverId) {
			driverId = newDriverId;
			driverOffset = target - mappedHandX;
			return target;
		}

		// Exponential removal is frame-rate independent. A large correction is
		// initially quicker, while its last few pixels settle gently instead of
		// ending at a visible fixed-duration edge.
		driverOffset *= std::exp(-rebaseFollow * deltaTime);
		target = std::clamp(mappedHandX + driverOffset, 0.0f, 1.0f);
		return target;
	}

	float getTarget() const { return target; }
	int getDriverId() const { return driverId; }

	// Per-second decay of the continuity offset after a driver change. At 3.0,
	// about 95 percent is gone after one second without compromising the direct
	// one-to-one movement of a continuing hand.
	float rebaseFollow = 3.0f;

private:
	float target = 0.5f;
	int driverId = -1;
	float driverOffset = 0.0f;
};
