#include "TableCoordinateMapper.h"
#include <algorithm>

namespace {
float clampUnit(float value) {
	return std::max(0.0f, std::min(value, 1.0f));
}
}

DetectedObject TableCoordinateMapper::map(const DetectedObject & source,
	const ProjectionCalibration & calibration) {
	DetectedObject mapped = source;

	switch (calibration.sensorToTableRotation) {
	case QuarterTurn::Normal:
		break;
	case QuarterTurn::Clockwise90:
		mapped.x = 1.0f - source.y;
		mapped.y = source.x;
		mapped.vx = -source.vy;
		mapped.vy = source.vx;
		break;
	case QuarterTurn::HalfTurn:
		mapped.x = 1.0f - source.x;
		mapped.y = 1.0f - source.y;
		mapped.vx = -source.vx;
		mapped.vy = -source.vy;
		break;
	case QuarterTurn::CounterClockwise90:
		mapped.x = source.y;
		mapped.y = 1.0f - source.x;
		mapped.vx = source.vy;
		mapped.vy = -source.vx;
		break;
	}

	// Mirrors are applied in table space after the quarter turn, matching the
	// order shown in calibration and keeping their velocity signs consistent.
	if (calibration.sensorMirrorX) {
		mapped.x = 1.0f - mapped.x;
		mapped.vx = -mapped.vx;
	}
	if (calibration.sensorMirrorY) {
		mapped.y = 1.0f - mapped.y;
		mapped.vy = -mapped.vy;
	}

	mapped.x = clampUnit(mapped.x);
	mapped.y = clampUnit(mapped.y);
	return mapped;
}

std::vector<DetectedObject> TableCoordinateMapper::map(
	const std::vector<DetectedObject> & source,
	const ProjectionCalibration & calibration) {
	std::vector<DetectedObject> mapped;
	mapped.reserve(source.size());
	for (const DetectedObject & object : source) {
		mapped.push_back(map(object, calibration));
	}
	return mapped;
}
