#include "ProjectionCalibration.h"
#include "TableCoordinateMapper.h"
#include <cassert>
#include <cmath>
#include <iostream>

namespace {
bool close(float left, float right) {
	return std::fabs(left - right) < 0.00001f;
}

DetectedObject sample() {
	DetectedObject object;
	object.id = 17;
	object.x = 0.2f;
	object.y = 0.7f;
	object.z = 0.4f;
	object.area = 0.05f;
	object.vx = 0.3f;
	object.vy = -0.6f;
	object.age = 2.0f;
	object.type = ObjectType::Hand;
	return object;
}

void assertMetadataPreserved(const DetectedObject & value) {
	assert(value.id == 17);
	assert(close(value.z, 0.4f));
	assert(close(value.area, 0.05f));
	assert(close(value.age, 2.0f));
	assert(value.type == ObjectType::Hand);
}
}

int main() {
	ProjectionCalibration calibration;
	assert(isSaneProjectionCalibration(calibration));
	assert(close(calibration.cornerRadiusFraction,
		TableShape::cornerRadiusFraction));
	assert(quarterTurnSwapsAxes(QuarterTurn::Clockwise90));
	assert(quarterTurnSwapsAxes(QuarterTurn::CounterClockwise90));
	assert(!quarterTurnSwapsAxes(QuarterTurn::Normal));
	assert(rotateQuarterTurn(QuarterTurn::Normal, 1) == QuarterTurn::Clockwise90);
	assert(rotateQuarterTurn(QuarterTurn::Normal, -1) == QuarterTurn::CounterClockwise90);
	assert(rotateBaseEdge(BaseEdge::Bottom, 1) == BaseEdge::Left);
	assert(rotateBaseEdge(BaseEdge::Bottom, -1) == BaseEdge::Right);
	BaseEdge parsedEdge = BaseEdge::Top;
	assert(parseBaseEdge("left", parsedEdge) && parsedEdge == BaseEdge::Left);
	assert(!parseBaseEdge("long", parsedEdge));
	assert(std::string(baseEdgeLengthClass(calibration)) == "long side");
	calibration.baseEdge = BaseEdge::Right;
	assert(std::string(baseEdgeLengthClass(calibration)) == "short side");
	calibration.baseEdge = BaseEdge::Bottom;
	ProjectionCalibration nearlyEqual = calibration;
	nearlyEqual.surfaceWidthCm += 0.000001f;
	assert(sameProjectionCalibration(calibration, nearlyEqual));
	nearlyEqual.surfaceWidthCm += 0.1f;
	assert(!sameProjectionCalibration(calibration, nearlyEqual));
	nearlyEqual = calibration;
	nearlyEqual.cornerRadiusFraction += 0.01f;
	assert(!sameProjectionCalibration(calibration, nearlyEqual));

	const DetectedObject source = sample();

	calibration.sensorToTableRotation = QuarterTurn::Normal;
	DetectedObject mapped = TableCoordinateMapper::map(source, calibration);
	assert(close(mapped.x, 0.2f) && close(mapped.y, 0.7f));
	assert(close(mapped.vx, 0.3f) && close(mapped.vy, -0.6f));

	calibration.sensorToTableRotation = QuarterTurn::Clockwise90;
	mapped = TableCoordinateMapper::map(source, calibration);
	assert(close(mapped.x, 0.3f) && close(mapped.y, 0.2f));
	assert(close(mapped.vx, 0.6f) && close(mapped.vy, 0.3f));
	assertMetadataPreserved(mapped);

	calibration.sensorToTableRotation = QuarterTurn::HalfTurn;
	mapped = TableCoordinateMapper::map(source, calibration);
	assert(close(mapped.x, 0.8f) && close(mapped.y, 0.3f));
	assert(close(mapped.vx, -0.3f) && close(mapped.vy, 0.6f));

	calibration.sensorToTableRotation = QuarterTurn::CounterClockwise90;
	mapped = TableCoordinateMapper::map(source, calibration);
	assert(close(mapped.x, 0.7f) && close(mapped.y, 0.8f));
	assert(close(mapped.vx, -0.6f) && close(mapped.vy, -0.3f));

	calibration.sensorMirrorX = true;
	calibration.sensorMirrorY = true;
	mapped = TableCoordinateMapper::map(source, calibration);
	assert(close(mapped.x, 0.3f) && close(mapped.y, 0.2f));
	assert(close(mapped.vx, 0.6f) && close(mapped.vy, 0.3f));

	ProjectionCalibration invalid = calibration;
	invalid.viewportX = 0.8f;
	invalid.viewportWidth = 0.4f;
	assert(!isSaneProjectionCalibration(invalid));
	invalid = calibration;
	invalid.cornerRadiusFraction = -0.01f;
	assert(!isSaneProjectionCalibration(invalid));
	invalid.cornerRadiusFraction = 1.01f;
	assert(!isSaneProjectionCalibration(invalid));

	std::vector<DetectedObject> objects { source, source };
	assert(TableCoordinateMapper::map(objects, calibration).size() == 2);

	std::cout << "projection mapping tests passed\n";
	return 0;
}
