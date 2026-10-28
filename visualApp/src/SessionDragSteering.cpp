#include "SessionDragSteering.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float pi = 3.14159265358979323846f;
constexpr float twoPi = pi * 2.0f;

float clampUnit(float value) {
	return std::max(0.0f, std::min(value, 1.0f));
}

float angleDifference(float target, float current) {
	float difference = std::fmod(target - current + pi, twoPi);

	if (difference < 0.0f) {
		difference += twoPi;
	}

	return difference - pi;
}

float wrapAngle(float angle) {
	return angleDifference(angle, 0.0f);
}
}

void SessionDragSteering::setSurfaceSize(float widthCm, float heightCm) {
	if (!std::isfinite(widthCm) || !std::isfinite(heightCm)
		|| widthCm <= 0.0f || heightCm <= 0.0f) {
		return;
	}

	surfaceWidthCm = widthCm;
	surfaceHeightCm = heightCm;
}

void SessionDragSteering::update(int trackId, float normalizedX, float normalizedY) {
	if (trackId != ownerTrackId) {
		// Reset the gesture but retain the calibrated physical surface. Assigning
		// the new owner before validating its point also removes the old command if
		// a handover happens to arrive with a malformed first sample.
		resetSession();
		ownerTrackId = trackId;
	}

	if (!std::isfinite(normalizedX) || !std::isfinite(normalizedY)) {
		return;
	}

	float x = clampUnit(normalizedX);
	float y = clampUnit(normalizedY);

	if (!anchored) {
		anchorX = x;
		anchorY = y;
		anchored = true;
		engaged = false;
		pullX = 0.0f;
		pullY = 0.0f;
		strength = 0.0f;
		return;
	}

	// Convert normalized table deltas to centimetres before measuring the pull.
	// On the 188x79 cm table this prevents a normalized y movement from feeling
	// more than twice as strong as the same real movement along x.
	float dx = (x - anchorX) * surfaceWidthCm;
	float dy = (y - anchorY) * surfaceHeightCm;
	float distance = std::hypot(dx, dy);
	float shortEdge = std::min(surfaceWidthCm, surfaceHeightCm);
	float releaseRadius = shortEdge * releaseRadiusOfShortEdge;
	float activationRadius = shortEdge * activationRadiusOfShortEdge;
	float fullPullRadius = shortEdge * fullPullRadiusOfShortEdge;

	if (engaged && distance <= releaseRadius) {
		engaged = false;
		pullX = 0.0f;
		pullY = 0.0f;
		strength = 0.0f;
		stableHeadingRadians = 0.0f;
		return;
	}

	bool justEngaged = false;

	if (!engaged) {
		if (distance <= activationRadius) {
			pullX = 0.0f;
			pullY = 0.0f;
			strength = 0.0f;
			stableHeadingRadians = 0.0f;
			return;
		}

		engaged = true;
		justEngaged = true;
	}

	// While returning through the hysteresis band the vector fades smoothly to
	// zero, but the gesture remains engaged until it reaches the release radius.
	float pullRange = std::max(fullPullRadius - releaseRadius, 0.001f);
	strength = clampUnit((distance - releaseRadius) / pullRange);

	if (distance <= 0.0001f || strength <= 0.0f) {
		pullX = 0.0f;
		pullY = 0.0f;
		return;
	}

	float measuredHeading = std::atan2(dx, -dy);

	if (justEngaged) {
		stableHeadingRadians = measuredHeading;
	} else if (distance >= activationRadius) {
		// Keep a small angular margin behind the measured vector. This is
		// hysteresis rather than quantization: every heading remains reachable,
		// while a held hand cannot alternate the requested course on sensor noise.
		float difference = angleDifference(measuredHeading, stableHeadingRadians);

		if (std::fabs(difference) > directionHysteresisRadians) {
			float acceptedChange = std::copysign(
				std::fabs(difference) - directionHysteresisRadians, difference);
			stableHeadingRadians = wrapAngle(stableHeadingRadians + acceptedChange);
		}
	}

	// Inside the radial return band stableHeadingRadians is deliberately held:
	// the magnitude may fade, but a nearly zero-length vector cannot invent a
	// new direction immediately before the gesture releases.
	pullX = std::sin(stableHeadingRadians) * strength;
	pullY = -std::cos(stableHeadingRadians) * strength;
}

float SessionDragSteering::getHeadingRadians() const {
	if (strength <= 0.0f) {
		return 0.0f;
	}

	return stableHeadingRadians;
}

void SessionDragSteering::resetSession() {
	anchorX = 0.5f;
	anchorY = 0.5f;
	ownerTrackId = -1;
	anchored = false;
	engaged = false;
	pullX = 0.0f;
	pullY = 0.0f;
	strength = 0.0f;
	stableHeadingRadians = 0.0f;
}
