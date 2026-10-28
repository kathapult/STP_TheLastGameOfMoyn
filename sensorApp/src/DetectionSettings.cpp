#include "DetectionSettings.h"
#include "HandPositionModeMigration.h"
#include <cstdio>
#include <filesystem>
#include <fstream>

// Smallest depth window the detector is allowed to end up with (mm). Keeps a
// nudge or a bad file from collapsing near/far onto each other, which would
// mask away everything and look like a broken sensor.
static const float MIN_DEPTH_SPAN_MM = 10.0f;

// Smallest ROI edge, as a fraction of the frame. Below this the rectangle is
// too small to hold a blob that passes minAreaRadius, so detection would go
// quiet with no obvious cause.
static const float MIN_ROI_FRACTION = 0.05f;

// Sanity ceiling for the morphology kernels (px radius) - far beyond anything
// useful on a 512x424 frame, present only so a mistyped file cannot build a
// kernel big enough to stall the Pi.
static const int MAX_MORPH_RADIUS = 15;

// Sanity bounds for the One-Euro smoothing parameters (see
// DetectionSettings::positionSmoothingMinCutoff/Beta) - wide enough to cover
// anything worth trying at the table, tight enough that a mistyped file or a
// key held down cannot reach a cutoff so low it reads as frozen or so high
// the filter stops smoothing anything at all.
static const float MIN_SMOOTHING_CUTOFF = 0.05f;
static const float MAX_SMOOTHING_CUTOFF = 15.0f;
static const float MAX_SMOOTHING_BETA = 5.0f;
static const float MIN_INWARD_TIP_CAP_FRACTION = 0.05f;
static const float MAX_INWARD_TIP_CAP_FRACTION = 0.40f;

CalibrationTarget nextCalibrationTarget(CalibrationTarget current) {
	switch (current) {
	case CalibrationTarget::DepthWindow:
		return CalibrationTarget::TableSurface;
	case CalibrationTarget::TableSurface:
		return CalibrationTarget::RoiPosition;
	case CalibrationTarget::RoiPosition:
		return CalibrationTarget::RoiSize;
	case CalibrationTarget::RoiSize:
		return CalibrationTarget::Noise;
	case CalibrationTarget::Noise:
		return CalibrationTarget::Smoothing;
	case CalibrationTarget::Smoothing:
		return CalibrationTarget::Classification;
	case CalibrationTarget::Classification:
		return CalibrationTarget::HandSize;
	case CalibrationTarget::HandSize:
		return CalibrationTarget::HandPosition;
	case CalibrationTarget::HandPosition:
		return CalibrationTarget::Stationary;
	case CalibrationTarget::Stationary:
	default:
		return CalibrationTarget::DepthWindow;
	}
}

std::string calibrationTargetLabel(CalibrationTarget target) {
	switch (target) {
	case CalibrationTarget::DepthWindow:
		return "DEPTH WINDOW (left/right: near, up/down: far)";
	case CalibrationTarget::TableSurface:
		return "TABLE MASK (left/right: clearance mm, c: capture, x: clear)";
	case CalibrationTarget::RoiPosition:
		return "ROI POSITION (arrows move the table outline)";
	case CalibrationTarget::RoiSize:
		return "ROI SIZE (left/right: width, up/down: height)";
	case CalibrationTarget::Noise:
		return "NOISE (left/right: open radius, up/down: close radius)";
	case CalibrationTarget::Smoothing:
		return "SMOOTHING (left/right: min cutoff, up/down: beta / speed response)";
	case CalibrationTarget::Classification:
		return "CLASSIFICATION (left/right: solidity threshold, up/down: max border width)";
	case CalibrationTarget::HandSize:
		return "HAND SIZE (left/right: min palm radius mm, up/down: max palm radius mm)";
	case CalibrationTarget::HandPosition:
		return "HAND POSITION (left/right: source, up/down: inward cap size)";
	case CalibrationTarget::Stationary:
		return "STATIONARY (left/right: seconds until object, up/down: drift tolerance)";
	default:
		return "UNKNOWN";
	}
}

// Reads one optional key. A missing or non-numeric entry leaves the target
// untouched, so a partial settings file is valid and unknown keys are ignored
// - that is what lets later phases add fields without breaking older files.
static void readNumber(const ofJson & json, const std::string & key, float & target) {
	if (json.contains(key) && json[key].is_number()) {
		target = json[key].get<float>();
	}
}

static void readNumber(const ofJson & json, const std::string & key, int & target) {
	if (json.contains(key) && json[key].is_number()) {
		target = json[key].get<int>();
	}
}

bool DetectionSettings::load(const std::string & path) {
	// Checked explicitly because ofLoadJson only logs on a missing file and
	// hands back an empty object, which is indistinguishable from an empty
	// but valid file at the call site.
	if (!ofFile::doesFileExist(ofToDataPath(path))) {
		ofLogWarning("DetectionSettings") << "no settings file at " << path << " - using built-in defaults";
		return false;
	}

	ofJson json = ofLoadJson(path);
	if (!json.is_object()) {
		ofLogError("DetectionSettings") << path << " is not a JSON object - keeping current values";
		return false;
	}

	readNumber(json, "nearMM", nearMM);
	readNumber(json, "farMM", farMM);
	readNumber(json, "tableSurfaceClearanceMM", tableSurfaceClearanceMM);
	readNumber(json, "minAreaRadius", minAreaRadius);
	readNumber(json, "maxAreaRadius", maxAreaRadius);
	readNumber(json, "trackerMaximumDistance", trackerMaximumDistance);
	readNumber(json, "trackerPersistence", trackerPersistence);
	readNumber(json, "roiX", roiX);
	readNumber(json, "roiY", roiY);
	readNumber(json, "roiWidth", roiWidth);
	readNumber(json, "roiHeight", roiHeight);
	readNumber(json, "morphOpenRadius", morphOpenRadius);
	readNumber(json, "morphCloseRadius", morphCloseRadius);
	readNumber(json, "positionSmoothingMinCutoff", positionSmoothingMinCutoff);
	readNumber(json, "positionSmoothingBeta", positionSmoothingBeta);
	readNumber(json, "handSolidityThreshold", handSolidityThreshold);
	readNumber(json, "handMinPalmRadiusMM", handMinPalmRadiusMM);
	readNumber(json, "handMaxPalmRadiusMM", handMaxPalmRadiusMM);
	readNumber(json, "handMaxBorderWidth", handMaxBorderWidth);
	readNumber(json, "stationarySeconds", stationarySeconds);
	readNumber(json, "stationaryTolerance", stationaryTolerance);
	readNumber(json, "hideSeconds", hideSeconds);
	readNumber(json, "inwardTipCapFraction", inwardTipCapFraction);

	// New files name all three modes and version the one deliberate default
	// migration. Existing persistent files were seeded once by an older release
	// with PalmCentre, so an unversioned PalmCentre adopts InwardTip here. A mode
	// saved after this release carries version 2 and is respected exactly.
	int positionModeVersion = 1;
	if (json.contains("handPositionModeVersion")
		&& json["handPositionModeVersion"].is_number_integer()) {
		positionModeVersion = json["handPositionModeVersion"].get<int>();
	}

	bool loadedPositionMode = false;
	if (json.contains("handPositionMode") && json["handPositionMode"].is_string()) {
		const std::string mode = json["handPositionMode"].get<std::string>();
		if (mode == "centroid") {
			handPositionMode = CalibrationProtocol::HandPositionMode::ContourCentroid;
			loadedPositionMode = true;
		} else if (mode == "palm") {
			handPositionMode = CalibrationProtocol::HandPositionMode::PalmCentre;
			loadedPositionMode = true;
		} else if (mode == "inward") {
			handPositionMode = CalibrationProtocol::HandPositionMode::InwardTip;
			loadedPositionMode = true;
		}
	} else if (json.contains("handPositionMode") && json["handPositionMode"].is_number_integer()) {
		const int mode = json["handPositionMode"].get<int>();
		if (CalibrationProtocol::isKnownHandPositionMode(mode)) {
			handPositionMode = static_cast<CalibrationProtocol::HandPositionMode>(mode);
			loadedPositionMode = true;
		}
	}
	if (!loadedPositionMode && json.contains("usePalmCentreForHands")
		&& json["usePalmCentreForHands"].is_boolean()) {
		// The legacy true value enabled the best hand-specific point available
		// at the time. InwardTip is its new equivalent and retains PalmCentre as
		// its fallback; false still explicitly requests the plain centroid.
		handPositionMode = json["usePalmCentreForHands"].get<bool>()
			? CalibrationProtocol::HandPositionMode::InwardTip
			: CalibrationProtocol::HandPositionMode::ContourCentroid;
	}

	const CalibrationProtocol::HandPositionMode loadedMode = handPositionMode;
	handPositionMode = HandPositionModeMigration::apply(
		positionModeVersion, handPositionMode);
	if (handPositionMode != loadedMode) {
		ofLogNotice("DetectionSettings")
			<< "migrated legacy palm interaction point to inward tip";
	}

	sanitize();
	ofLogNotice("DetectionSettings") << "loaded " << path << ": " << toDisplayString();
	return true;
}

bool DetectionSettings::save(const std::string & path) const {
	ofJson json;
	json["nearMM"] = nearMM;
	json["farMM"] = farMM;
	json["tableSurfaceClearanceMM"] = tableSurfaceClearanceMM;
	json["minAreaRadius"] = minAreaRadius;
	json["maxAreaRadius"] = maxAreaRadius;
	json["trackerMaximumDistance"] = trackerMaximumDistance;
	json["trackerPersistence"] = trackerPersistence;
	json["roiX"] = roiX;
	json["roiY"] = roiY;
	json["roiWidth"] = roiWidth;
	json["roiHeight"] = roiHeight;
	json["morphOpenRadius"] = morphOpenRadius;
	json["morphCloseRadius"] = morphCloseRadius;
	json["positionSmoothingMinCutoff"] = positionSmoothingMinCutoff;
	json["positionSmoothingBeta"] = positionSmoothingBeta;
	json["handSolidityThreshold"] = handSolidityThreshold;
	json["handMinPalmRadiusMM"] = handMinPalmRadiusMM;
	json["handMaxPalmRadiusMM"] = handMaxPalmRadiusMM;
	json["handMaxBorderWidth"] = handMaxBorderWidth;
	json["stationarySeconds"] = stationarySeconds;
	json["stationaryTolerance"] = stationaryTolerance;
	json["hideSeconds"] = hideSeconds;
	json["handPositionModeVersion"] = HandPositionModeMigration::currentVersion;
	switch (handPositionMode) {
	case CalibrationProtocol::HandPositionMode::ContourCentroid:
		json["handPositionMode"] = "centroid";
		break;
	case CalibrationProtocol::HandPositionMode::InwardTip:
		json["handPositionMode"] = "inward";
		break;
	case CalibrationProtocol::HandPositionMode::PalmCentre:
	default:
		json["handPositionMode"] = "palm";
		break;
	}
	json["inwardTipCapFraction"] = inwardTipCapFraction;
	// An older release ignores handPositionMode. Keeping its boolean mirror in
	// the persisted file gives rollback a safe centroid/palm approximation;
	// InwardTip deliberately rolls back to the established palm behavior.
	json["usePalmCentreForHands"] = handPositionMode
		!= CalibrationProtocol::HandPositionMode::ContourCentroid;

	// Resolve the managed-release symlink first. Renaming onto the symlink
	// itself would replace it inside the release instead of atomically
	// replacing the persistent file under /var/lib.
	std::filesystem::path target(ofToDataPath(path, true));
	std::error_code pathError;
	if (std::filesystem::is_symlink(target, pathError)) {
		target = std::filesystem::canonical(target, pathError);
	}
	if (pathError) {
		ofLogError("DetectionSettings") << "could not resolve " << path
			<< " for atomic save: " << pathError.message();
		return false;
	}

	const std::filesystem::path temporary = target.string() + ".tmp";
	std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
	if (!output) {
		ofLogError("DetectionSettings") << "could not open temporary settings file "
			<< temporary.string();
		return false;
	}

	// Pretty-printed because this file is meant to remain hand-editable.
	output << json.dump(4) << '\n';
	output.close();
	if (!output) {
		std::filesystem::remove(temporary, pathError);
		ofLogError("DetectionSettings") << "could not finish temporary settings file "
			<< temporary.string();
		return false;
	}

	// POSIX rename is atomic when source and target share a directory. Both Pi
	// and macOS satisfy that contract; the temporary file is never published
	// partially.
	bool saved = std::rename(temporary.string().c_str(), target.string().c_str()) == 0;
	if (!saved) {
		std::filesystem::remove(temporary, pathError);
	}
	if (saved) {
		ofLogNotice("DetectionSettings") << "saved " << path << ": " << toDisplayString();
	} else {
		ofLogError("DetectionSettings") << "could not write " << path;
	}
	return saved;
}

void DetectionSettings::sanitize() {
	nearMM = std::max(0.0f, nearMM);

	// A depth window read the wrong way round masks away every pixel, so it
	// is corrected rather than accepted.
	if (farMM < nearMM + MIN_DEPTH_SPAN_MM) {
		farMM = nearMM + MIN_DEPTH_SPAN_MM;
	}
	tableSurfaceClearanceMM = ofClamp(tableSurfaceClearanceMM, 0.0f, 200.0f);

	minAreaRadius = std::max(1.0f, minAreaRadius);
	maxAreaRadius = std::max(minAreaRadius + 1.0f, maxAreaRadius);

	trackerMaximumDistance = std::max(1.0f, trackerMaximumDistance);
	trackerPersistence = std::max(1, trackerPersistence);

	// Keep the ROI's bounding rectangle inside the frame and large enough to
	// hold a blob. The curved table outline is derived from this rectangle. The
	// origin is pulled in first, then the size is trimmed to what is left, so
	// dragging the outline towards an edge shrinks it rather than pushing
	// part of it outside where it would be silently ignored.
	roiX = ofClamp(roiX, 0.0f, 1.0f - MIN_ROI_FRACTION);
	roiY = ofClamp(roiY, 0.0f, 1.0f - MIN_ROI_FRACTION);
	roiWidth = ofClamp(roiWidth, MIN_ROI_FRACTION, 1.0f - roiX);
	roiHeight = ofClamp(roiHeight, MIN_ROI_FRACTION, 1.0f - roiY);

	// 0 means "skip this step" and is legitimate. The upper bound is well
	// past anything useful on a 512x424 frame and only exists so a typo in
	// the file cannot produce a kernel large enough to stall the Pi.
	morphOpenRadius = ofClamp(morphOpenRadius, 0, MAX_MORPH_RADIUS);
	morphCloseRadius = ofClamp(morphCloseRadius, 0, MAX_MORPH_RADIUS);

	positionSmoothingMinCutoff = ofClamp(positionSmoothingMinCutoff, MIN_SMOOTHING_CUTOFF, MAX_SMOOTHING_CUTOFF);
	positionSmoothingBeta = ofClamp(positionSmoothingBeta, 0.0f, MAX_SMOOTHING_BETA);

	// A solidity threshold of 1.0 would call everything a hand (no shape can
	// exceed its own convex hull), 0.0 would call nothing one - both are
	// legal to dial towards while calibrating, so only the range is enforced.
	handSolidityThreshold = ofClamp(handSolidityThreshold, 0.0f, 1.0f);
	handMinPalmRadiusMM = std::max(0.0f, handMinPalmRadiusMM);

	// Kept above the lower bound, so nudging the two towards each other
	// closes the window rather than inverting it - an inverted window would
	// silently classify nothing as a hand at all.
	handMaxPalmRadiusMM = std::max(handMinPalmRadiusMM, handMaxPalmRadiusMM);

	handMaxBorderWidth = ofClamp(handMaxBorderWidth, 0.0f, 1.0f);
	if (!CalibrationProtocol::isKnownHandPositionMode(
		static_cast<int>(handPositionMode))) {
		handPositionMode = CalibrationProtocol::HandPositionMode::PalmCentre;
	}
	inwardTipCapFraction = ofClamp(inwardTipCapFraction,
		MIN_INWARD_TIP_CAP_FRACTION, MAX_INWARD_TIP_CAP_FRACTION);

	// 0 seconds is meaningful - it switches the stationary rule off - so only
	// negatives are corrected.
	stationarySeconds = std::max(0.0f, stationarySeconds);
	stationaryTolerance = ofClamp(stationaryTolerance, 0.0f, 1.0f);
	hideSeconds = std::max(0.0f, hideSeconds);
}

ofRectangle DetectionSettings::roiInPixels(int frameWidth, int frameHeight) const {
	// Rounded to whole pixels because the mask is filled per pixel; clamped
	// again here so a frame size of 0 (no depth frame yet) cannot produce a
	// negative or out-of-range rectangle.
	int x = ofClamp(static_cast<int>(roiX * frameWidth), 0, std::max(0, frameWidth - 1));
	int y = ofClamp(static_cast<int>(roiY * frameHeight), 0, std::max(0, frameHeight - 1));
	int width = ofClamp(static_cast<int>(roiWidth * frameWidth), 1, std::max(1, frameWidth - x));
	int height = ofClamp(static_cast<int>(roiHeight * frameHeight), 1, std::max(1, frameHeight - y));

	return ofRectangle(x, y, width, height);
}

std::string DetectionSettings::toDisplayString() const {
	return "depth " + ofToString(nearMM, 0) + "-" + ofToString(farMM, 0) + "mm"
		+ " table-gap>" + ofToString(tableSurfaceClearanceMM, 0) + "mm"
		+ " | table roi " + ofToString(roiX, 2) + "," + ofToString(roiY, 2)
		+ " " + ofToString(roiWidth, 2) + "x" + ofToString(roiHeight, 2)
		+ " | area r " + ofToString(minAreaRadius, 0) + "-" + ofToString(maxAreaRadius, 0) + "px"
		+ " | track dist " + ofToString(trackerMaximumDistance, 0) + "px"
		+ " persist " + ofToString(trackerPersistence) + "f"
		+ " | open " + ofToString(morphOpenRadius) + " close " + ofToString(morphCloseRadius)
		+ " smooth cutoff " + ofToString(positionSmoothingMinCutoff, 2) + " beta " + ofToString(positionSmoothingBeta, 2)
		+ " | hand sol<" + ofToString(handSolidityThreshold, 2)
		+ " palm " + ofToString(handMinPalmRadiusMM, 0) + "-" + ofToString(handMaxPalmRadiusMM, 0) + "mm"
		+ " border<" + ofToString(handMaxBorderWidth, 2)
		+ " still>" + ofToString(stationarySeconds, 0) + "s"
		+ " hide>" + ofToString(hideSeconds, 0) + "s"
		+ " position " + CalibrationProtocol::handPositionModeLabel(handPositionMode)
		+ " cap " + ofToString(inwardTipCapFraction, 2);
}
