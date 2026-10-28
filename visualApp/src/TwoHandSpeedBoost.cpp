#include "TwoHandSpeedBoost.h"
#include <algorithm>
#include <cmath>

namespace {
float clampUnit(float value) {
	return std::max(0.0f, std::min(value, 1.0f));
}

bool isFinite(const TwoHandSpeedBoost::HandMotion & hand) {
	return std::isfinite(hand.x) && std::isfinite(hand.y)
		&& std::isfinite(hand.velocityX) && std::isfinite(hand.velocityY);
}
}

void TwoHandSpeedBoost::setSurfaceSize(float widthCm, float heightCm) {
	if (!std::isfinite(widthCm) || !std::isfinite(heightCm)
		|| widthCm <= 0.0f || heightCm <= 0.0f) {
		return;
	}

	surfaceWidthCm = widthCm;
	surfaceHeightCm = heightCm;
}

float TwoHandSpeedBoost::update(bool hasExactPair,
	const HandMotion & first, const HandMotion & second, float deltaTime) {
	float target = 0.0f;
	bool validPair = hasExactPair && first.id >= 0 && second.id >= 0
		&& first.id != second.id
		&& isFinite(first) && isFinite(second);

	if (validPair) {
		int nextFirstId = std::min(first.id, second.id);
		int nextSecondId = std::max(first.id, second.id);
		bool samePair = nextFirstId == firstId && nextSecondId == secondId;

		firstId = nextFirstId;
		secondId = nextSecondId;

		if (samePair) {
			float shortEdge = std::max(
				std::min(surfaceWidthCm, surfaceHeightCm), 0.001f);
			float dx = (first.x - second.x) * surfaceWidthCm / shortEdge;
			float dy = (first.y - second.y) * surfaceHeightCm / shortEdge;
			float velocityX = (first.velocityX - second.velocityX)
				* surfaceWidthCm / shortEdge;
			float velocityY = (first.velocityY - second.velocityY)
				* surfaceHeightCm / shortEdge;
			float separation = std::hypot(dx, dy);

			if (separation >= minimumPairSeparation) {
				// The derivative of distance is the relative velocity projected
				// onto the line between the hands. Using the wire-format velocities
				// avoids render-rate spikes when a new 30 Hz sensor frame lands in a
				// 60 Hz visual update.
				float separationRate = (dx * velocityX + dy * velocityY) / separation;
				float range = std::max(spreadFullRate - spreadThreshold, 0.001f);
				target = clampUnit((separationRate - spreadThreshold) / range);
			}
		}
	} else {
		firstId = -1;
		secondId = -1;
	}

	if (!std::isfinite(deltaTime) || deltaTime <= 0.0f) {
		return level;
	}

	deltaTime = std::min(deltaTime, 1.0f / 20.0f);
	float follow = target > level ? followUp : followDown;
	float amount = 1.0f - std::exp(-follow * deltaTime);
	level += (target - level) * amount;
	level = clampUnit(level);
	return level;
}

void TwoHandSpeedBoost::reset() {
	firstId = -1;
	secondId = -1;
	level = 0.0f;
}
