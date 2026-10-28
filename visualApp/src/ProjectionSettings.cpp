#include "ProjectionSettings.h"
#include "ofMain.h"
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>

namespace {
bool readFloat(const ofJson & object, const std::string & key, float & target) {
	if (!object.contains(key)) return true;
	if (!object[key].is_number()) return false;
	try {
		target = object[key].get<float>();
	} catch (const std::exception &) {
		return false;
	}
	return true;
}

bool readBool(const ofJson & object, const std::string & key, bool & target) {
	if (!object.contains(key)) return true;
	if (!object[key].is_boolean()) return false;
	try {
		target = object[key].get<bool>();
	} catch (const std::exception &) {
		return false;
	}
	return true;
}

bool readTurn(const ofJson & object, const std::string & key, QuarterTurn & target) {
	if (!object.contains(key)) return true;
	if (!object[key].is_string()) return false;
	try {
		return parseQuarterTurn(object[key].get<std::string>(), target);
	} catch (const std::exception &) {
		return false;
	}
}

bool readBaseEdge(const ofJson & object, const std::string & key, BaseEdge & target) {
	if (!object.contains(key)) return true;
	if (!object[key].is_string()) return false;
	try {
		return parseBaseEdge(object[key].get<std::string>(), target);
	} catch (const std::exception &) {
		return false;
	}
}
}

bool ProjectionSettings::setup(const std::string & path) {
	settingsPath = path;
	return loadFromDisk();
}

bool ProjectionSettings::reload() {
	return loadFromDisk();
}

bool ProjectionSettings::loadFromDisk() {
	if (!ofFile::doesFileExist(ofToDataPath(settingsPath))) {
		error = true;
		lastResult = "projection file missing; built-in defaults active";
		ofLogWarning("ProjectionSettings") << lastResult << ": " << settingsPath;
		return false;
	}

	ofJson json;
	try {
		json = ofLoadJson(settingsPath);
	} catch (const std::exception & exception) {
		error = true;
		lastResult = "projection file is invalid JSON; keeping current values";
		ofLogError("ProjectionSettings") << lastResult << ": " << exception.what();
		return false;
	}
	if (!json.is_object()) {
		error = true;
		lastResult = "projection file is not a JSON object";
		ofLogError("ProjectionSettings") << lastResult;
		return false;
	}

	ProjectionCalibration candidate;
	if (json.contains("version")) {
		if (!json["version"].is_number_integer()) {
			error = true;
			lastResult = "projection version is not an integer";
			return false;
		}
		try {
			candidate.version = json["version"].get<int>();
		} catch (const std::exception &) {
			error = true;
			lastResult = "projection version is outside the integer range";
			return false;
		}
	}

	if (!readFloat(json, "surfaceWidthCm", candidate.surfaceWidthCm)
		|| !readFloat(json, "surfaceHeightCm", candidate.surfaceHeightCm)
		|| !readFloat(json, "cornerRadiusFraction", candidate.cornerRadiusFraction)
		|| !readBaseEdge(json, "baseEdge", candidate.baseEdge)
		|| !readTurn(json, "outputRotation", candidate.outputRotation)
		|| !readTurn(json, "sensorToTableRotation", candidate.sensorToTableRotation)
		|| !readBool(json, "sensorMirrorX", candidate.sensorMirrorX)
		|| !readBool(json, "sensorMirrorY", candidate.sensorMirrorY)) {
		error = true;
		lastResult = "projection file contains a value with the wrong type";
		ofLogError("ProjectionSettings") << lastResult;
		return false;
	}

	if (json.contains("viewport")) {
		if (!json["viewport"].is_object()
			|| !readFloat(json["viewport"], "x", candidate.viewportX)
			|| !readFloat(json["viewport"], "y", candidate.viewportY)
			|| !readFloat(json["viewport"], "width", candidate.viewportWidth)
			|| !readFloat(json["viewport"], "height", candidate.viewportHeight)) {
			error = true;
			lastResult = "projection viewport is malformed";
			ofLogError("ProjectionSettings") << lastResult;
			return false;
		}
	}

	if (!isSaneProjectionCalibration(candidate)) {
		error = true;
		lastResult = "projection file is outside safe ranges";
		ofLogError("ProjectionSettings") << lastResult;
		return false;
	}

	current = candidate;
	persisted = candidate;
	error = false;
	lastResult = "projection calibration loaded";
	ofLogNotice("ProjectionSettings") << lastResult << " from " << settingsPath;
	return true;
}

bool ProjectionSettings::apply(const ProjectionCalibration & candidate) {
	if (!isSaneProjectionCalibration(candidate)) {
		error = true;
		lastResult = "rejected unsafe projection value";
		return false;
	}
	current = candidate;
	error = false;
	lastResult = isDirty() ? "projection changed (not saved)" : "projection unchanged";
	return true;
}

bool ProjectionSettings::save() {
	if (!isSaneProjectionCalibration(current)) {
		error = true;
		lastResult = "refusing to save unsafe projection values";
		return false;
	}

	ofJson json;
	json["version"] = current.version;
	json["surfaceWidthCm"] = current.surfaceWidthCm;
	json["surfaceHeightCm"] = current.surfaceHeightCm;
	json["cornerRadiusFraction"] = current.cornerRadiusFraction;
	json["baseEdge"] = baseEdgeName(current.baseEdge);
	json["viewport"] = {
		{ "x", current.viewportX },
		{ "y", current.viewportY },
		{ "width", current.viewportWidth },
		{ "height", current.viewportHeight }
	};
	json["outputRotation"] = quarterTurnName(current.outputRotation);
	json["sensorToTableRotation"] = quarterTurnName(current.sensorToTableRotation);
	json["sensorMirrorX"] = current.sensorMirrorX;
	json["sensorMirrorY"] = current.sensorMirrorY;

	std::filesystem::path target(ofToDataPath(settingsPath, true));
	std::error_code pathError;
	if (std::filesystem::is_symlink(target, pathError)) {
		target = std::filesystem::canonical(target, pathError);
	}
	if (pathError) {
		error = true;
		lastResult = "could not resolve projection file";
		ofLogError("ProjectionSettings") << lastResult << ": " << pathError.message();
		return false;
	}

	const std::filesystem::path temporary = target.string() + ".tmp";
	std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
	if (!output) {
		error = true;
		lastResult = "could not open temporary projection file";
		return false;
	}
	output << json.dump(4) << '\n';
	output.close();
	if (!output) {
		std::filesystem::remove(temporary, pathError);
		error = true;
		lastResult = "could not finish projection file";
		return false;
	}

	if (std::rename(temporary.string().c_str(), target.string().c_str()) != 0) {
		std::filesystem::remove(temporary, pathError);
		error = true;
		lastResult = "could not publish projection file";
		return false;
	}

	persisted = current;
	error = false;
	lastResult = "projection calibration saved";
	ofLogNotice("ProjectionSettings") << lastResult << " to " << target.string();
	return true;
}
