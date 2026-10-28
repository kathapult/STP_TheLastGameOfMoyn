#include "CruiseSpeedEnvelope.h"
#include <algorithm>
#include <cmath>

namespace {
float clampUnit(float value) {
	return std::max(0.0f, std::min(value, 1.0f));
}
}

float CruiseSpeedEnvelope::update(bool hasDriver, float controlHeight,
	float speedBoost, float deltaTime) {
	float target = 1.0f;

	if (hasDriver) {
		float height = std::isfinite(controlHeight)
			? clampUnit(controlHeight)
			: 0.5f;
		float boost = std::isfinite(speedBoost)
			? clampUnit(speedBoost)
			: 0.0f;
		float heightMultiplier = lowHandMultiplier
			+ (highHandMultiplier - lowHandMultiplier) * height;

		target = heightMultiplier * (1.0f + boost * fullBoostAddition);
		target = std::max(highHandMultiplier,
			std::min(target, maximumMultiplier));
	}

	if (!std::isfinite(deltaTime) || deltaTime <= 0.0f) {
		return multiplier;
	}

	deltaTime = std::min(deltaTime, 1.0f / 20.0f);
	float follow = target > multiplier ? followUp : followDown;
	float amount = 1.0f - std::exp(-follow * deltaTime);
	multiplier += (target - multiplier) * amount;
	multiplier = std::max(highHandMultiplier,
		std::min(multiplier, maximumMultiplier));
	return multiplier;
}

void CruiseSpeedEnvelope::reset() {
	multiplier = 1.0f;
}
