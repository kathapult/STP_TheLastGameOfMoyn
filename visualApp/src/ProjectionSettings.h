#pragma once

#include "ProjectionCalibration.h"
#include <string>

// Loads and atomically persists Pi B's installation-specific projection,
// table-outline and sensor-axis calibration. It is deliberately local and not
// part of the Pi A calibration OSC protocol.
class ProjectionSettings {
public:
	bool setup(const std::string & path = "projection-calibration.json");
	bool reload();
	bool save();
	bool apply(const ProjectionCalibration & candidate);

	const ProjectionCalibration & get() const { return current; }
	bool isDirty() const { return !sameProjectionCalibration(current, persisted); }
	bool hasError() const { return error; }
	const std::string & getLastResult() const { return lastResult; }

private:
	bool loadFromDisk();

	std::string settingsPath;
	ProjectionCalibration current;
	ProjectionCalibration persisted;
	bool error = false;
	std::string lastResult = "projection defaults active";
};
