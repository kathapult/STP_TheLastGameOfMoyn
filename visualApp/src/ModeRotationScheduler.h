#pragma once

#include <algorithm>
#include <cmath>

// Pure timing policy for production visual rotation. ofApp owns the random
// interval draw and the actual index change; this class only decides when a
// scheduled change is allowed to happen. Keeping that decision independent of
// openFrameworks makes the no-switch-during-interaction contract testable on a
// development machine without a Pi or projector.
class ModeRotationScheduler {
public:
	void reset(float intervalSeconds) {
		interval = std::max(intervalSeconds, 0.001f);
		elapsed = 0.0f;
		idle = 0.0f;
		overdue = false;
	}

	// Returns true once the scheduled interval has elapsed and the table has
	// also been free of interaction for the full grace period. A caller that
	// performs the switch immediately resets this instance with the next random
	// interval.
	bool update(float deltaTime, bool interactionActive) {
		if (!std::isfinite(deltaTime) || deltaTime <= 0.0f) {
			return false;
		}

		elapsed += deltaTime;
		idle = interactionActive ? 0.0f : idle + deltaTime;
		overdue = elapsed >= interval;

		return overdue && idle >= idleGraceSeconds;
	}

	float getSecondsUntilDue() const {
		return std::max(interval - elapsed, 0.0f);
	}

	float getIdleSeconds() const { return idle; }
	bool isOverdue() const { return overdue; }

	static constexpr float idleGraceSeconds = 25.0f;

private:
	float interval = 30.0f;
	float elapsed = 0.0f;
	float idle = 0.0f;
	bool overdue = false;
};
