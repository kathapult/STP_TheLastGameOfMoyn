#pragma once

#include "DetectedObject.h"
#include "ProjectionCalibration.h"
#include <vector>

// Converts Pi A's normalized sensor/ROI axes into the installation's stable
// table axes. Visuals never need to know how the Kinect is mounted.
class TableCoordinateMapper {
public:
	static DetectedObject map(const DetectedObject & source,
		const ProjectionCalibration & calibration);
	static std::vector<DetectedObject> map(const std::vector<DetectedObject> & source,
		const ProjectionCalibration & calibration);
};
