#include "CalibrationPanel.h"
#include "DetectedObject.h"
#include "Palette.h"
#include "TableShape.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <vector>

using namespace CalibrationProtocol;

namespace {
const std::array<CalibrationPanel::GroupFields,
	static_cast<size_t>(CalibrationPanel::Group::SurfaceSize)> groups {{
	{ "DEPTH WINDOW", Field::NearMM, Field::FarMM, false, false },
	{ "TABLE SURFACE MASK", Field::TableSurfaceClearanceMM,
		Field::TableSurfaceClearanceMM, false, false },
	// The camera pictures are mirrored left-to-right across their vertical
	// axis. Inverting ROI X keeps the visible box moving with the arrow key.
	{ "ROI POSITION", Field::RoiX, Field::RoiY, true, true },
	{ "ROI SIZE", Field::RoiWidth, Field::RoiHeight, false, false },
	{ "BLOB SIZE", Field::MinAreaRadius, Field::MaxAreaRadius, false, false },
	{ "TRACKER", Field::TrackerMaximumDistance, Field::TrackerPersistence, false, false },
	{ "MORPHOLOGY", Field::MorphOpenRadius, Field::MorphCloseRadius, false, false },
	{ "SMOOTHING", Field::PositionSmoothingMinCutoff, Field::PositionSmoothingBeta, false, false },
	{ "CLASSIFICATION", Field::HandSolidityThreshold, Field::HandMaxBorderWidth, false, false },
	{ "HAND SIZE", Field::HandMinPalmRadiusMM, Field::HandMaxPalmRadiusMM, false, false },
	{ "HAND POSITION", Field::HandPositionMode, Field::InwardTipCapFraction, false, false },
	{ "STATIONARY", Field::StationarySeconds, Field::StationaryTolerance, false, false },
	{ "VISIBILITY", Field::HideSeconds, Field::HideSeconds, false, false }
}};

constexpr float bitmapCharacterWidth = 8.0f;
constexpr float bitmapLineHeight = 14.0f;
constexpr float controlConnectionFreshnessSeconds = 2.0f;

float roundedRectangleInsetAtY(const ofRectangle & rectangle, float radius,
	float y) {
	if (radius <= 0.0f) return 0.0f;
	const float upperCentre = rectangle.y + radius;
	const float lowerCentre = rectangle.getBottom() - radius;
	float distance = 0.0f;
	if (y < upperCentre) distance = upperCentre - y;
	else if (y > lowerCentre) distance = y - lowerCentre;
	else return 0.0f;
	distance = std::min(distance, radius);
	return radius - std::sqrt(std::max(0.0f,
		radius * radius - distance * distance));
}

ofRectangle lowerTextRectangle(const ofRectangle & playRect) {
	const float verticalMargin = ofClamp(playRect.height * 0.04f, 12.0f, 20.0f);
	const float top = playRect.y + playRect.height * 0.30f;
	const float bottom = playRect.getBottom() - verticalMargin;
	return ofRectangle(playRect.x, top, playRect.width,
		std::max(1.0f, bottom - top));
}

ofRectangle tableBandRectangle(const ofRectangle & playRect, float radius,
	float top, float bottom) {
	const float curveInset = std::max(
		roundedRectangleInsetAtY(playRect, radius, top),
		roundedRectangleInsetAtY(playRect, radius, bottom));
	const float horizontalInset = curveInset + 14.0f;
	return ofRectangle(playRect.x + horizontalInset, top,
		std::max(1.0f, playRect.width - horizontalInset * 2.0f),
		std::max(1.0f, bottom - top));
}

void drawRoiCornerCutout(const glm::vec2 & sharpCorner,
	const glm::vec2 & arcCentre, float radius, float startAngle, float endAngle) {
	const int arcSteps = 12;
	ofMesh cutout;
	cutout.setMode(OF_PRIMITIVE_TRIANGLE_FAN);
	cutout.addVertex(glm::vec3(sharpCorner, 0.0f));
	for (int step = 0; step <= arcSteps; ++step) {
		const float angle = ofLerp(startAngle, endAngle,
			static_cast<float>(step) / static_cast<float>(arcSteps));
		cutout.addVertex(glm::vec3(arcCentre
			+ radius * glm::vec2(std::cos(angle), std::sin(angle)), 0.0f));
	}
	cutout.draw();
}

void drawRoiCornerCutouts(const ofRectangle & roi, float radius) {
	drawRoiCornerCutout(glm::vec2(roi.x, roi.y),
		glm::vec2(roi.x + radius, roi.y + radius), radius, -HALF_PI, -PI);
	drawRoiCornerCutout(glm::vec2(roi.getRight(), roi.y),
		glm::vec2(roi.getRight() - radius, roi.y + radius), radius, -HALF_PI, 0.0f);
	drawRoiCornerCutout(glm::vec2(roi.getRight(), roi.getBottom()),
		glm::vec2(roi.getRight() - radius, roi.getBottom() - radius), radius, 0.0f, HALF_PI);
	drawRoiCornerCutout(glm::vec2(roi.x, roi.getBottom()),
		glm::vec2(roi.x + radius, roi.getBottom() - radius), radius, HALF_PI, PI);
}

struct AdjustmentRow {
	std::string label;
	std::string value;
	std::string hint;
};

enum class DiagnosticImage {
	Depth,
	Mask
};

struct DiagnosticPane {
	DiagnosticImage image;
	const char * label;
	bool showRoi;
	bool showTracks;
};

struct ModeStatus {
	std::string text;
	ofColor color;
};

std::string fixedValue(float value, int precision) {
	std::ostringstream result;
	result << std::fixed << std::setprecision(precision) << value;
	return result.str();
}

bool displaysAsPercent(Field field) {
	switch (field) {
	case Field::RoiX:
	case Field::RoiY:
	case Field::RoiWidth:
	case Field::RoiHeight:
	case Field::HandSolidityThreshold:
	case Field::StationaryTolerance:
	case Field::HandMaxBorderWidth:
	case Field::InwardTipCapFraction:
		return true;
	default:
		return false;
	}
}

const char * fieldDisplayName(Field field) {
	switch (field) {
	case Field::NearMM: return "NEAR DISTANCE";
	case Field::FarMM: return "FAR DISTANCE";
	case Field::MinAreaRadius: return "MINIMUM BLOB RADIUS";
	case Field::MaxAreaRadius: return "MAXIMUM BLOB RADIUS";
	case Field::TrackerMaximumDistance: return "TRACK MATCH DISTANCE";
	case Field::TrackerPersistence: return "TRACK PERSISTENCE";
	case Field::RoiX: return "ROI HORIZONTAL POSITION";
	case Field::RoiY: return "ROI VERTICAL POSITION";
	case Field::RoiWidth: return "ROI WIDTH";
	case Field::RoiHeight: return "ROI HEIGHT";
	case Field::MorphOpenRadius: return "OPENING RADIUS";
	case Field::MorphCloseRadius: return "CLOSING RADIUS";
	case Field::PositionSmoothingMinCutoff: return "SMOOTHING CUTOFF";
	case Field::PositionSmoothingBeta: return "SMOOTHING RESPONSE";
	case Field::HandSolidityThreshold: return "HAND SOLIDITY";
	case Field::HandMaxBorderWidth: return "HAND BORDER CONTACT";
	case Field::HandMinPalmRadiusMM: return "MINIMUM PALM RADIUS";
	case Field::HandMaxPalmRadiusMM: return "MAXIMUM PALM RADIUS";
	case Field::StationarySeconds: return "STATIONARY TIME";
	case Field::StationaryTolerance: return "STATIONARY MOVEMENT";
	case Field::HideSeconds: return "HIDE AFTER";
	case Field::UsePalmCentreForHands: return "LEGACY HAND POSITION SOURCE";
	case Field::TableSurfaceClearanceMM: return "MINIMUM HEIGHT ABOVE TABLE";
	case Field::HandPositionMode: return "HAND POSITION SOURCE";
	case Field::InwardTipCapFraction: return "INWARD TIP CAP SIZE";
	default: return "UNKNOWN VALUE";
	}
}

std::string fieldValueText(const SettingsState & state, Field field) {
	const FieldDescriptor & item = descriptor(field);
	const float value = fieldValue(state, field);
	if (field == Field::HandPositionMode) {
		return handPositionModeLabel(state.handPositionMode);
	}
	if (item.kind == ValueKind::Boolean) {
		return value > 0.5f ? "PALM CENTRE" : "BLOB CENTRE";
	}
	if (displaysAsPercent(field)) {
		return fixedValue(value * 100.0f, 1) + "%";
	}
	switch (field) {
	case Field::NearMM:
	case Field::FarMM:
	case Field::HandMinPalmRadiusMM:
	case Field::HandMaxPalmRadiusMM:
	case Field::TableSurfaceClearanceMM:
		return fixedValue(value, 0) + " mm";
	case Field::MinAreaRadius:
	case Field::MaxAreaRadius:
	case Field::TrackerMaximumDistance:
	case Field::MorphOpenRadius:
	case Field::MorphCloseRadius:
		return fixedValue(value, item.kind == ValueKind::Integer ? 0 : 1) + " px";
	case Field::TrackerPersistence:
		return fixedValue(value, 0) + " frames";
	case Field::StationarySeconds:
	case Field::HideSeconds:
		return fixedValue(value, 1) + " s";
	default:
		return fixedValue(value, 2);
	}
}

std::string fieldStepText(Field field) {
	const FieldDescriptor & item = descriptor(field);
	if (field == Field::HandPositionMode) return "mode";
	if (item.kind == ValueKind::Boolean) return "toggle";
	if (displaysAsPercent(field)) return fixedValue(item.step * 100.0f, 1) + "%";
	switch (field) {
	case Field::NearMM:
	case Field::FarMM:
	case Field::HandMinPalmRadiusMM:
	case Field::HandMaxPalmRadiusMM:
	case Field::TableSurfaceClearanceMM:
		return fixedValue(item.step, 0) + " mm";
	case Field::MinAreaRadius:
	case Field::MaxAreaRadius:
	case Field::TrackerMaximumDistance:
	case Field::MorphOpenRadius:
	case Field::MorphCloseRadius:
		return fixedValue(item.step, 0) + " px";
	case Field::TrackerPersistence:
		return fixedValue(item.step, 0) + " frame";
	case Field::StationarySeconds:
	case Field::HideSeconds:
		return fixedValue(item.step, 1) + " s";
	default:
		return fixedValue(item.step, 3);
	}
}

AdjustmentRow sensorRow(const SettingsState & state, Field field,
	bool vertical, bool inverted) {
	const FieldDescriptor & item = descriptor(field);
	AdjustmentRow row;
	row.label = fieldDisplayName(field);
	row.value = fieldValueText(state, field);
	if (field == Field::HandPositionMode) {
		row.hint = "LEFT  PREVIOUS SOURCE     NEXT SOURCE  RIGHT";
		return row;
	}
	if (item.kind == ValueKind::Boolean) {
		row.hint = vertical ? "UP / DOWN  TOGGLE" : "LEFT / RIGHT  TOGGLE";
		return row;
	}

	const std::string step = fieldStepText(field);
	if (!vertical) {
		row.hint = inverted
			? "RIGHT  -" + step + "     +" + step + "  LEFT"
			: "LEFT  -" + step + "     +" + step + "  RIGHT";
	} else {
		row.hint = inverted
			? "UP  -" + step + "     +" + step + "  DOWN"
			: "DOWN  -" + step + "     +" + step + "  UP";
	}
	return row;
}

std::vector<AdjustmentRow> sensorRows(const SettingsState & state,
	const CalibrationPanel::GroupFields & selected) {
	return {
		sensorRow(state, selected.horizontal, false, selected.invertHorizontal),
		sensorRow(state, selected.vertical, true, selected.invertVertical)
	};
}

std::vector<AdjustmentRow> visibilityRows(const SettingsState & state) {
	return { sensorRow(state, Field::HideSeconds, false, false) };
}

std::vector<AdjustmentRow> handPositionRows(const SettingsState & state,
	bool supported) {
	if (!supported) {
		return {{ "HAND POSITION SETTINGS", "PI A UPDATE REQUIRED",
			"CURRENT PI A ONLY PROVIDES THE LEGACY PALM / CENTRE SWITCH" }};
	}
	return sensorRows(state,
		groups[static_cast<size_t>(CalibrationPanel::Group::HandPosition)]);
}

std::vector<AdjustmentRow> depthRows(const SettingsState & settings,
	const TableMaskState & mask) {
	std::vector<AdjustmentRow> rows = sensorRows(settings,
		groups[static_cast<size_t>(CalibrationPanel::Group::Depth)]);
	if (mask.active) {
		rows[1].label = "FAR DISTANCE (FALLBACK)";
		rows[1].hint = "USED ONLY AFTER [X] CLEARS THE TABLE MASK";
	}
	return rows;
}

std::vector<AdjustmentRow> tableMaskRows(const SettingsState & settings,
	const TableMaskState & mask) {
	std::string stateText = "PI A UPDATE REQUIRED";
	std::string stateHint = "TABLE MASK CONTROL IS NOT AVAILABLE";
	if (mask.supported) {
		switch (mask.captureState) {
		case TableMaskCaptureState::Capturing:
			stateText = "CAPTURING " + ofToString(mask.capturedFrames)
				+ " / " + ofToString(mask.requiredFrames);
			stateHint = "KEEP THE TABLE EMPTY UNTIL CAPTURE COMPLETES";
			break;
		case TableMaskCaptureState::Ready:
			stateText = std::string(mask.active ? "ACTIVE | " : "INACTIVE | ")
				+ fixedValue(mask.validCoverage * 100.0f, 1) + "% COVERAGE";
			stateHint = "[C] RECAPTURE EMPTY TABLE     [X] CLEAR";
			break;
		case TableMaskCaptureState::Error:
			stateText = mask.active ? "CAPTURE ERROR | PREVIOUS MASK ACTIVE"
				: "CAPTURE ERROR | NO ACTIVE MASK";
			stateHint = "CHECK PI A JOURNAL, THEN PRESS [C] TO RETRY";
			break;
		case TableMaskCaptureState::Missing:
		default:
			stateText = "NOT CAPTURED";
			stateHint = "EMPTY THE TABLE, THEN PRESS [C] TO CAPTURE";
			break;
		}
	}

	AdjustmentRow clearance = sensorRow(settings,
		Field::TableSurfaceClearanceMM, false, false);
	clearance.hint += "   (LEFT / RIGHT ONLY)";
	return {
		{ "EMPTY-TABLE DEPTH REFERENCE", stateText, stateHint },
		clearance
	};
}

std::vector<AdjustmentRow> projectionRows(CalibrationPanel::Group group,
	const ProjectionCalibration & value) {
	switch (group) {
	case CalibrationPanel::Group::SurfaceSize:
		return {
			{ "SURFACE WIDTH", fixedValue(value.surfaceWidthCm, 1) + " cm",
				"LEFT  -1.0 cm     +1.0 cm  RIGHT   (SHIFT: 0.1 cm)" },
			{ "SURFACE HEIGHT", fixedValue(value.surfaceHeightCm, 1) + " cm",
				"DOWN  -1.0 cm     +1.0 cm  UP   (SHIFT: 0.1 cm)" }
		};
	case CalibrationPanel::Group::SurfaceRounding:
		return {{ "SHORT-EDGE ROUNDING",
			fixedValue(value.cornerRadiusFraction * 100.0f, 1) + "% of maximum",
			"LEFT  LESS ROUND     MORE ROUND  RIGHT   (SHIFT: 0.1%)" }};
	case CalibrationPanel::Group::BaseEdge:
		return {{ "SELECTED BASE EDGE", std::string(baseEdgeDisplayName(value.baseEdge))
			+ " / " + baseEdgeLengthClass(value),
			"LEFT / UP  PREVIOUS     NEXT  RIGHT / DOWN" }};
	case CalibrationPanel::Group::ViewportPosition:
		return {
			{ "DISPLAY X POSITION", fixedValue(value.viewportX, 3),
				"LEFT  -0.005     +0.005  RIGHT   (SHIFT: 0.001)" },
			{ "DISPLAY Y POSITION", fixedValue(value.viewportY, 3),
				"UP  -0.005     +0.005  DOWN   (SHIFT: 0.001)" }
		};
	case CalibrationPanel::Group::ViewportExtent:
		return {
			{ "DISPLAY WIDTH", fixedValue(value.viewportWidth, 3),
				"LEFT  -0.005     +0.005  RIGHT   (SHIFT: 0.001)" },
			{ "DISPLAY HEIGHT", fixedValue(value.viewportHeight, 3),
				"DOWN  -0.005     +0.005  UP   (SHIFT: 0.001)" }
		};
	case CalibrationPanel::Group::OutputRotation:
		return {{ "PROJECTED IMAGE ROTATION", quarterTurnDisplayName(value.outputRotation),
			"LEFT  COUNTER-CLOCKWISE     CLOCKWISE  RIGHT" }};
	case CalibrationPanel::Group::SensorOrientation:
		return {{ "SENSOR TO TABLE ROTATION",
			quarterTurnDisplayName(value.sensorToTableRotation),
			"LEFT  COUNTER-CLOCKWISE     CLOCKWISE  RIGHT" }};
	case CalibrationPanel::Group::SensorMirror:
		return {
			{ "MIRROR TABLE X", value.sensorMirrorX ? "ON" : "OFF",
				"LEFT / RIGHT  TOGGLE" },
			{ "MIRROR TABLE Y", value.sensorMirrorY ? "ON" : "OFF",
				"UP / DOWN  TOGGLE" }
		};
	default:
		return {};
	}
}

CalibrationPanel::Group adjacentGroup(CalibrationPanel::Group group, int direction) {
	const int count = static_cast<int>(CalibrationPanel::Group::Count);
	int value = (static_cast<int>(group) + (direction < 0 ? -1 : 1)) % count;
	if (value < 0) value += count;
	return static_cast<CalibrationPanel::Group>(value);
}

const char * groupDisplayName(CalibrationPanel::Group group) {
	const int sensorGroupCount = static_cast<int>(CalibrationPanel::Group::SurfaceSize);
	if (static_cast<int>(group) < sensorGroupCount) {
		return groups[static_cast<size_t>(group)].label;
	}
	switch (group) {
	case CalibrationPanel::Group::SurfaceSize: return "DISPLAY SIZE CM";
	case CalibrationPanel::Group::SurfaceRounding: return "DISPLAY ROUNDING";
	case CalibrationPanel::Group::BaseEdge: return "BASE EDGE";
	case CalibrationPanel::Group::ViewportPosition: return "DISPLAY POSITION";
	case CalibrationPanel::Group::ViewportExtent: return "DISPLAY EXTENT";
	case CalibrationPanel::Group::OutputRotation: return "OUTPUT ROTATION";
	case CalibrationPanel::Group::SensorOrientation: return "SENSOR -> TABLE";
	case CalibrationPanel::Group::SensorMirror: return "SENSOR MIRROR";
	default: return "UNKNOWN";
	}
}

float textWidth(const std::string & text, float scale) {
	return static_cast<float>(text.size()) * bitmapCharacterWidth * scale;
}

void drawText(const std::string & text, float x, float y, float scale,
	const ofColor & color) {
	ofPushMatrix();
	ofTranslate(x, y);
	ofScale(scale, scale);
	ofSetColor(color);
	ofDrawBitmapString(text, 0.0f, 11.0f);
	ofPopMatrix();
}

void drawCenteredText(const std::string & text, const ofRectangle & rectangle,
	float scale, const ofColor & color) {
	const float x = rectangle.getCenter().x - textWidth(text, scale) * 0.5f;
	const float y = rectangle.getCenter().y - bitmapLineHeight * scale * 0.5f;
	drawText(text, x, y, scale, color);
}

void drawPanel(const ofRectangle & rectangle, const ofColor & fill,
	const ofColor & border) {
	ofFill();
	ofSetColor(fill);
	ofDrawRectangle(rectangle);
	ofNoFill();
	ofSetLineWidth(2.0f);
	ofSetColor(border);
	ofDrawRectangle(rectangle);
	ofFill();
}

void drawBadge(const std::string & text, float x, float y, float scale,
	const ofColor & foreground, const ofColor & background) {
	const ofRectangle backgroundRectangle(x, y,
		textWidth(text, scale) + 12.0f,
		bitmapLineHeight * scale + 8.0f);
	ofFill();
	ofSetColor(background);
	ofDrawRectangle(backgroundRectangle);
	drawText(text, x + 6.0f, y + 4.0f, scale, foreground);
}

std::string clippedText(const std::string & text, size_t maximumCharacters) {
	if (text.size() <= maximumCharacters) return text;
	if (maximumCharacters <= 3) return text.substr(0, maximumCharacters);
	return text.substr(0, maximumCharacters - 3) + "...";
}

std::vector<DiagnosticPane> diagnosticPanes(CalibrationPanel::Group group) {
	switch (group) {
	case CalibrationPanel::Group::Depth:
		return {
			{ DiagnosticImage::Depth, "RAW DEPTH", false, false },
			{ DiagnosticImage::Mask, "CURRENT TRACKING MASK", false, false }
		};
	case CalibrationPanel::Group::TableMask:
		return {
			{ DiagnosticImage::Depth, "EMPTY TABLE FOR CAPTURE", true, false },
			{ DiagnosticImage::Mask, "TRACKING AFTER TABLE SUBTRACTION", true, false }
		};
	case CalibrationPanel::Group::RoiPosition:
	case CalibrationPanel::Group::RoiSize:
		return {
			{ DiagnosticImage::Depth, "RAW DEPTH + ROI", true, false },
			{ DiagnosticImage::Mask, "MASK + ROI", true, false }
		};
	case CalibrationPanel::Group::BlobSize:
		return {{ DiagnosticImage::Mask, "MASK + ACCEPTED BLOBS", false, true }};
	case CalibrationPanel::Group::Tracker:
		return {{ DiagnosticImage::Depth, "TRACK IDS", false, true }};
	case CalibrationPanel::Group::Morphology:
		return {{ DiagnosticImage::Mask, "PROCESSED MASK", false, false }};
	case CalibrationPanel::Group::Smoothing:
		return {{ DiagnosticImage::Depth, "RAW + SMOOTHED CENTRES", false, true }};
	case CalibrationPanel::Group::Classification:
		return {{ DiagnosticImage::Mask, "TYPE + SOLIDITY + BORDER", false, true }};
	case CalibrationPanel::Group::HandSize:
		return {{ DiagnosticImage::Mask, "TYPE + PALM RADIUS", false, true }};
	case CalibrationPanel::Group::HandPosition:
		return {{ DiagnosticImage::Mask, "SELECTED RAW + SMOOTHED POSITION", false, true }};
	case CalibrationPanel::Group::Stationary:
		return {{ DiagnosticImage::Depth, "TYPE + STILL TIME + SPEED", false, true }};
	case CalibrationPanel::Group::Visibility:
		return {{ DiagnosticImage::Depth, "VISIBLE + HIDDEN TRACKS", false, true }};
	default:
		return {};
	}
}

std::vector<ofRectangle> diagnosticRectangles(CalibrationPanel::Group group,
	const ofRectangle & available) {
	const std::vector<DiagnosticPane> panes = diagnosticPanes(group);
	if (panes.empty()) return {};

	const float margin = 10.0f;
	const float gap = 14.0f;
	const float aspectRatio = 128.0f / 106.0f;
	const float maximumHeight = std::max(1.0f, available.height - margin * 2.0f);
	const float maximumWidth = panes.size() == 2
		? (available.width - margin * 2.0f - gap) * 0.5f
		: available.width - margin * 2.0f;
	const float height = std::min(maximumHeight, maximumWidth / aspectRatio);
	const float width = height * aspectRatio;
	const float totalWidth = width * static_cast<float>(panes.size())
		+ gap * static_cast<float>(panes.size() - 1);
	const float left = available.getCenter().x - totalWidth * 0.5f;
	const float top = available.getCenter().y - height * 0.5f;

	std::vector<ofRectangle> rectangles;
	for (size_t index = 0; index < panes.size(); ++index) {
		rectangles.emplace_back(left + static_cast<float>(index) * (width + gap),
			top, width, height);
	}
	return rectangles;
}
}

void CalibrationPanel::setup() {
	group = Group::SurfaceSize;
	showText = true;
	showImages = true;
}

bool CalibrationPanel::isProjectionGroup() const {
	return static_cast<int>(group) >= static_cast<int>(Group::SurfaceSize);
}

const char * CalibrationPanel::activeGroupLabel() const {
	return groupDisplayName(group);
}

const CalibrationPanel::GroupFields & CalibrationPanel::activeFields() const {
	return groups[static_cast<size_t>(group)];
}

void CalibrationPanel::selectAdjacentGroup(int direction) {
	group = adjacentGroup(group, direction);
}

void CalibrationPanel::nudge(int key, CalibrationControlClient & control) {
	if (!control.hasState() || control.hasPendingRequest()) return;
	if (group == Group::TableMask) {
		if (!control.getTableMaskState().supported
			|| (key != OF_KEY_LEFT && key != OF_KEY_RIGHT)) return;
		const Field field = Field::TableSurfaceClearanceMM;
		const float direction = key == OF_KEY_LEFT ? -1.0f : 1.0f;
		control.requestSet(field, fieldValue(control.getState(), field)
			+ direction * descriptor(field).step);
		return;
	}
	if (group == Group::HandPosition
		&& !control.supportsHandPositionSettings()) return;
	if (group == Group::Visibility
		&& key != OF_KEY_LEFT && key != OF_KEY_RIGHT) return;
	const GroupFields & selected = activeFields();
	Field field = selected.horizontal;
	float direction = 0.0f;
	if (key == OF_KEY_LEFT) {
		direction = selected.invertHorizontal ? 1.0f : -1.0f;
	} else if (key == OF_KEY_RIGHT) {
		direction = selected.invertHorizontal ? -1.0f : 1.0f;
	} else if (key == OF_KEY_UP) {
		field = selected.vertical;
		direction = selected.invertVertical ? -1.0f : 1.0f;
	} else if (key == OF_KEY_DOWN) {
		field = selected.vertical;
		direction = selected.invertVertical ? 1.0f : -1.0f;
	} else {
		return;
	}

	const FieldDescriptor & item = descriptor(field);
	float value = fieldValue(control.getState(), field);
	if (field == Field::HandPositionMode) {
		value = ofClamp(value + direction * item.step,
			static_cast<float>(HandPositionMode::ContourCentroid),
			static_cast<float>(HandPositionMode::InwardTip));
		if (value == fieldValue(control.getState(), field)) return;
	} else if (item.kind == ValueKind::Boolean) {
		value = value > 0.5f ? 0.0f : 1.0f;
	} else {
		value += direction * item.step;
	}
	control.requestSet(field, value);
}

void CalibrationPanel::nudgeProjection(int key, ProjectionSettings & projection) {
	ProjectionCalibration candidate = projection.get();
	const bool fineAdjustment = ofGetKeyPressed(OF_KEY_SHIFT);
	const float surfaceStepCm = fineAdjustment ? 0.1f : 1.0f;
	const float viewportStep = fineAdjustment ? 0.001f : 0.005f;
	const float roundingStep = fineAdjustment ? 0.001f : 0.01f;
	constexpr float minimumSurfaceCm = 10.0f;
	constexpr float maximumSurfaceCm = 1000.0f;
	constexpr float minimumViewport = 0.05f;

	switch (group) {
	case Group::SurfaceSize:
		if (key == OF_KEY_LEFT) candidate.surfaceWidthCm -= surfaceStepCm;
		else if (key == OF_KEY_RIGHT) candidate.surfaceWidthCm += surfaceStepCm;
		else if (key == OF_KEY_UP) candidate.surfaceHeightCm += surfaceStepCm;
		else if (key == OF_KEY_DOWN) candidate.surfaceHeightCm -= surfaceStepCm;
		candidate.surfaceWidthCm = ofClamp(candidate.surfaceWidthCm,
			minimumSurfaceCm, maximumSurfaceCm);
		candidate.surfaceHeightCm = ofClamp(candidate.surfaceHeightCm,
			minimumSurfaceCm, maximumSurfaceCm);
		break;
	case Group::SurfaceRounding:
		if (key == OF_KEY_LEFT) candidate.cornerRadiusFraction -= roundingStep;
		else if (key == OF_KEY_RIGHT) candidate.cornerRadiusFraction += roundingStep;
		else return;
		candidate.cornerRadiusFraction = ofClamp(
			candidate.cornerRadiusFraction, 0.0f, 1.0f);
		break;
	case Group::BaseEdge:
		if (key == OF_KEY_LEFT || key == OF_KEY_UP) {
			candidate.baseEdge = rotateBaseEdge(candidate.baseEdge, -1);
		} else if (key == OF_KEY_RIGHT || key == OF_KEY_DOWN) {
			candidate.baseEdge = rotateBaseEdge(candidate.baseEdge, 1);
		} else {
			return;
		}
		break;
	case Group::ViewportPosition:
		if (key == OF_KEY_LEFT) candidate.viewportX -= viewportStep;
		else if (key == OF_KEY_RIGHT) candidate.viewportX += viewportStep;
		else if (key == OF_KEY_UP) candidate.viewportY -= viewportStep;
		else if (key == OF_KEY_DOWN) candidate.viewportY += viewportStep;
		candidate.viewportX = ofClamp(candidate.viewportX, 0.0f,
			1.0f - candidate.viewportWidth);
		candidate.viewportY = ofClamp(candidate.viewportY, 0.0f,
			1.0f - candidate.viewportHeight);
		break;
	case Group::ViewportExtent:
		if (key == OF_KEY_LEFT) candidate.viewportWidth -= viewportStep;
		else if (key == OF_KEY_RIGHT) candidate.viewportWidth += viewportStep;
		else if (key == OF_KEY_UP) candidate.viewportHeight += viewportStep;
		else if (key == OF_KEY_DOWN) candidate.viewportHeight -= viewportStep;
		candidate.viewportWidth = ofClamp(candidate.viewportWidth,
			minimumViewport, 1.0f - candidate.viewportX);
		candidate.viewportHeight = ofClamp(candidate.viewportHeight,
			minimumViewport, 1.0f - candidate.viewportY);
		break;
	case Group::OutputRotation:
		if (key == OF_KEY_LEFT) {
			candidate.outputRotation = rotateQuarterTurn(candidate.outputRotation, -1);
		} else if (key == OF_KEY_RIGHT) {
			candidate.outputRotation = rotateQuarterTurn(candidate.outputRotation, 1);
		} else {
			return;
		}
		break;
	case Group::SensorOrientation:
		if (key == OF_KEY_LEFT) {
			candidate.sensorToTableRotation = rotateQuarterTurn(
				candidate.sensorToTableRotation, -1);
		} else if (key == OF_KEY_RIGHT) {
			candidate.sensorToTableRotation = rotateQuarterTurn(
				candidate.sensorToTableRotation, 1);
		} else {
			return;
		}
		break;
	case Group::SensorMirror:
		if (key == OF_KEY_LEFT || key == OF_KEY_RIGHT) {
			candidate.sensorMirrorX = !candidate.sensorMirrorX;
		} else if (key == OF_KEY_UP || key == OF_KEY_DOWN) {
			candidate.sensorMirrorY = !candidate.sensorMirrorY;
		} else {
			return;
		}
		break;
	default:
		return;
	}

	projection.apply(candidate);
}

void CalibrationPanel::keyPressed(int key, CalibrationControlClient & control,
	ProjectionSettings & projection, bool & cycleVisual) {
	cycleVisual = false;
	switch (key) {
	case OF_KEY_LEFT:
	case OF_KEY_RIGHT:
	case OF_KEY_UP:
	case OF_KEY_DOWN:
		if (isProjectionGroup()) nudgeProjection(key, projection);
		else nudge(key, control);
		break;
	case '\t':
	case 'm':
	case 'M':
		selectAdjacentGroup((key == 'M' || ofGetKeyPressed(OF_KEY_SHIFT)) ? -1 : 1);
		break;
	case 'f':
	case 'F':
		control.requestResetRoi();
		break;
	case 'c':
	case 'C':
		if (group == Group::TableMask) control.requestCaptureTableMask();
		break;
	case 'x':
	case 'X':
		if (group == Group::TableMask) control.requestClearTableMask();
		break;
	case 'r':
	case 'R':
	case OF_KEY_ESC:
		projection.reload();
		control.requestReload();
		break;
	case 'w':
	case 'W':
		projection.save();
		control.requestSave();
		break;
	case 'd':
	case 'D':
		showText = !showText;
		break;
	case 'p':
	case 'P':
		showImages = !showImages;
		break;
	case 'v':
	case 'V':
		cycleVisual = true;
		break;
	default:
		// Focus is intentional: unassigned keys never reach the visual or the
		// normal any-key mode switch.
		break;
	}
}

void CalibrationPanel::drawRoiOverlay(const ofRectangle & rectangle,
	const SettingsState & state) const {
	// The camera image is mirrored left-to-right for direct, mirror-like
	// feedback. Transform every image-space diagnostic by the same rule.
	const ofRectangle roi(rectangle.x
		+ (1.0f - state.roiX - state.roiWidth) * rectangle.width,
		rectangle.y + state.roiY * rectangle.height,
		state.roiWidth * rectangle.width, state.roiHeight * rectangle.height);
	ofFill();
	ofSetColor(0, 170);
	ofDrawRectangle(rectangle.x, rectangle.y, rectangle.width,
		std::max(0.0f, roi.y - rectangle.y));
	ofDrawRectangle(rectangle.x, roi.getBottom(), rectangle.width,
		std::max(0.0f, rectangle.getBottom() - roi.getBottom()));
	ofDrawRectangle(rectangle.x, roi.y,
		std::max(0.0f, roi.x - rectangle.x), roi.height);
	ofDrawRectangle(roi.getRight(), roi.y,
		std::max(0.0f, rectangle.getRight() - roi.getRight()), roi.height);
	const float radius = TableShape::cornerRadius(roi.width, roi.height);
	drawRoiCornerCutouts(roi, radius);
	ofNoFill();
	ofSetColor(255, 210, 0);
	ofSetLineWidth(2.0f);
	ofDrawRectRounded(roi.x, roi.y, roi.width, roi.height, radius);
	ofFill();
}

void CalibrationPanel::drawTracks(const ofRectangle & rectangle,
	const PreviewReceiver & telemetry) const {
	if (!telemetry.hasTrackDiagnostics()) return;
	for (const TrackDiagnostic & track : telemetry.getTrackDiagnostics()) {
		const glm::vec2 raw(rectangle.x + (1.0f - track.rawX) * rectangle.width,
			rectangle.y + track.rawY * rectangle.height);
		const glm::vec2 smooth(rectangle.x + (1.0f - track.smoothedX) * rectangle.width,
			rectangle.y + track.smoothedY * rectangle.height);
		// Smoothing and hand-position calibration both need the relationship
		// between the selected raw source and the filtered production point.
		if (group == Group::Smoothing || group == Group::HandPosition) {
			ofSetColor(Palette::colorCalibrationWarning);
			ofDrawLine(raw.x - 6.0f, raw.y, raw.x + 6.0f, raw.y);
			ofDrawLine(raw.x, raw.y - 6.0f, raw.x, raw.y + 6.0f);
		}
		ofNoFill();
		ofSetColor(track.hidden ? Palette::colorCalibrationMuted
			: Palette::colorCalibrationLive);
		ofDrawCircle(smooth, 8.0f);
		ofFill();

		// Each parameter group gets only the telemetry needed to judge its two
		// active values. The full diagnostic record remains on the wire, but it
		// no longer turns every on-screen label into an unrelated data dump.
		std::ostringstream label;
		label << "ID " << track.id;
		switch (group) {
		case Group::BlobSize:
			label << " ACCEPTED";
			break;
		case Group::Tracker:
			label << (track.hidden ? " HIDDEN" : " ACTIVE");
			break;
		case Group::Smoothing:
			label << " RAW + SMOOTH";
			break;
		case Group::Classification:
			label << " " << objectTypeLabel(toObjectType(track.type))
				<< " SOL " << std::fixed << std::setprecision(0)
				<< track.solidity * 100.0f << "% EDGE "
				<< track.borderContact * 100.0f << "%";
			break;
		case Group::HandSize:
			label << " " << objectTypeLabel(toObjectType(track.type))
				<< " PALM " << std::fixed << std::setprecision(0)
				<< track.palmRadiusMM << " mm";
			break;
		case Group::HandPosition:
			label << " RAW + SMOOTH";
			break;
		case Group::Stationary:
			label << " " << objectTypeLabel(toObjectType(track.type))
				<< " STILL " << std::fixed << std::setprecision(1)
				<< track.stillForSeconds << " s SPEED "
				<< std::setprecision(2) << std::hypot(track.vx, track.vy);
			break;
		case Group::Visibility:
			label << (track.hidden ? " HIDDEN" : " VISIBLE");
			break;
		default:
			break;
		}
		const size_t maximumCharacters = static_cast<size_t>(std::max(12.0f,
			rectangle.width / (bitmapCharacterWidth * 0.9f) - 3.0f));
		drawBadge(clippedText(label.str(), maximumCharacters),
			smooth.x + 10.0f, smooth.y - 13.0f, 0.9f,
			track.hidden ? Palette::colorCalibrationMuted
				: Palette::colorCalibrationText,
			Palette::colorCalibrationPanel);
	}
}

void CalibrationPanel::drawImageDiagnostics(const CalibrationControlClient & control,
	const PreviewReceiver & telemetry, const ofRectangle & available) const {
	const std::vector<DiagnosticPane> panes = diagnosticPanes(group);
	const std::vector<ofRectangle> rectangles = diagnosticRectangles(
		group, available);
	for (size_t index = 0; index < panes.size(); ++index) {
		const DiagnosticPane & pane = panes[index];
		const ofRectangle & rectangle = rectangles[index];
		const bool live = pane.image == DiagnosticImage::Depth
			? telemetry.hasDepthImage() : telemetry.hasImage();
		drawPanel(rectangle, Palette::colorCalibrationPanelAlt,
			live ? Palette::colorCalibrationLive
				: Palette::colorCalibrationMuted);

		// Negative X scale produces a mirror-like maintenance view while the
		// transmitted pixels and detector coordinates remain untouched.
		ofSetColor(255);
		if (live) {
			ofPushMatrix();
			ofTranslate(rectangle.getRight(), rectangle.y);
			ofScale(-1.0f, 1.0f);
			if (pane.image == DiagnosticImage::Depth) {
				telemetry.drawDepth(0.0f, 0.0f,
					rectangle.width, rectangle.height);
			} else {
				telemetry.draw(0.0f, 0.0f,
					rectangle.width, rectangle.height);
			}
			ofPopMatrix();
		} else {
			drawCenteredText(pane.image == DiagnosticImage::Depth
				? "WAITING FOR DEPTH" : "WAITING FOR MASK",
				rectangle, 1.25f, Palette::colorCalibrationMuted);
		}

		ofNoFill();
		ofSetLineWidth(2.0f);
		ofSetColor(live ? Palette::colorCalibrationLive
			: Palette::colorCalibrationMuted);
		ofDrawRectangle(rectangle);
		ofFill();
		drawBadge(std::string(pane.label) + (live ? " | LIVE" : " | NO SIGNAL"),
			rectangle.x + 8.0f, rectangle.y + 8.0f, 1.08f,
			live ? Palette::colorCalibrationLive
				: Palette::colorCalibrationMuted,
			Palette::colorCalibrationPanel);

		if (pane.showRoi && control.hasState()) {
			drawRoiOverlay(rectangle, control.getState());
		}
		if (pane.showTracks) {
			drawTracks(rectangle, telemetry);
		}
	}
}

void CalibrationPanel::drawProjectionGuide(const ProjectionSettings & projection,
	const std::vector<DetectedObject> & mappedObjects,
	const ofRectangle & playRect) const {
	const ProjectionCalibration & value = projection.get();
	const bool showRoundedOutline = group == Group::SurfaceRounding;
	ofNoFill();
	ofSetLineWidth(3.0f);
	ofSetColor(Palette::colorCalibrationWarning);
	if (showRoundedOutline) {
		const float radius = TableShape::cornerRadius(playRect.width,
			playRect.height, value.cornerRadiusFraction);
		ofDrawRectRounded(playRect.x, playRect.y, playRect.width,
			playRect.height, radius);
	} else {
		ofDrawRectangle(playRect);
	}

	const float marker = std::max(8.0f, std::min(playRect.width, playRect.height) * 0.025f);
	const std::array<glm::vec2, 4> corners {{
		glm::vec2(playRect.x, playRect.y),
		glm::vec2(playRect.getRight(), playRect.y),
		glm::vec2(playRect.getRight(), playRect.getBottom()),
		glm::vec2(playRect.x, playRect.getBottom())
	}};
	const std::array<const char *, 4> labels {{ "A 0,0", "B +X", "C", "D +Y" }};
	const std::array<ofColor, 4> colors {{
		Palette::colorCalibrationError, Palette::colorCalibrationWarning,
		Palette::colorCalibrationLive, Palette::colorAccent
	}};
	const bool showCoordinateLabels = group == Group::OutputRotation
		|| group == Group::SensorOrientation || group == Group::SensorMirror;

	ofFill();
	if (!showRoundedOutline) {
		for (size_t i = 0; i < corners.size(); ++i) {
			ofSetColor(colors[i]);
			ofDrawCircle(corners[i], marker);
			if (showCoordinateLabels) {
				const float labelX = i == 1 || i == 2
					? corners[i].x - 58.0f : corners[i].x + 8.0f;
				const float labelY = i >= 2
					? corners[i].y - 8.0f : corners[i].y + 18.0f;
				drawBadge(labels[i], labelX, labelY, 1.1f,
					Palette::colorCalibrationText, Palette::colorCalibrationPanel);
			}
		}
	}

	if (group == Group::BaseEdge) {
		glm::vec2 baseStart = corners[3];
		glm::vec2 baseEnd = corners[2];
		glm::vec2 baseLabel(playRect.getCenter().x - 70.0f,
			playRect.getBottom() - 10.0f);
		switch (value.baseEdge) {
		case BaseEdge::Top:
			baseStart = corners[0];
			baseEnd = corners[1];
			baseLabel = glm::vec2(playRect.getCenter().x - 70.0f,
				playRect.y + 22.0f);
			break;
		case BaseEdge::Right:
			baseStart = corners[1];
			baseEnd = corners[2];
			baseLabel = glm::vec2(playRect.getRight() - 155.0f,
				playRect.getCenter().y);
			break;
		case BaseEdge::Bottom:
			break;
		case BaseEdge::Left:
			baseStart = corners[0];
			baseEnd = corners[3];
			baseLabel = glm::vec2(playRect.x + 10.0f,
				playRect.getCenter().y);
			break;
		}
		ofSetColor(Palette::colorCalibrationError);
		ofSetLineWidth(std::max(5.0f, marker * 0.45f));
		ofDrawLine(baseStart, baseEnd);
		drawBadge(std::string("BASE ")
			+ baseEdgeDisplayName(value.baseEdge) + " / "
			+ baseEdgeLengthClass(value), baseLabel.x, baseLabel.y, 1.1f,
			Palette::colorCalibrationError, Palette::colorCalibrationPanel);
	}

	if (group == Group::SensorOrientation || group == Group::SensorMirror) {
		for (const DetectedObject & object : mappedObjects) {
			const glm::vec2 point(
				playRect.x + ofClamp(object.x, 0.0f, 1.0f) * playRect.width,
				playRect.y + ofClamp(object.y, 0.0f, 1.0f) * playRect.height);
			ofSetColor(object.type == ObjectType::Hand
				? Palette::colorCalibrationLive : Palette::colorAccent);
			ofDrawCircle(point, marker * 0.75f);
			drawBadge(ofToString(object.id), point.x + marker, point.y - marker,
				1.0f, Palette::colorCalibrationText,
				Palette::colorCalibrationPanel);
		}
	}

	if (group == Group::SurfaceSize) {
		const std::string dimensions = ofToString(value.surfaceWidthCm, 0) + " x "
			+ ofToString(value.surfaceHeightCm, 0) + " cm";
		drawBadge(dimensions,
			playRect.getCenter().x - textWidth(dimensions, 1.4f) * 0.5f,
			playRect.getCenter().y - bitmapLineHeight, 1.4f,
			Palette::colorCalibrationText, Palette::colorCalibrationPanel);
	}
	if (group == Group::SurfaceRounding) {
		const std::string rounding = "ROUNDING "
			+ ofToString(value.cornerRadiusFraction * 100.0f, 1) + "%";
		drawBadge(rounding,
			playRect.getCenter().x - textWidth(rounding, 1.4f) * 0.5f,
			playRect.getCenter().y - bitmapLineHeight, 1.4f,
			Palette::colorCalibrationText, Palette::colorCalibrationPanel);
	}
}

namespace {
float fittedTextScale(const std::string & text, float maximumWidth,
	float preferredScale, float minimumScale) {
	if (text.empty()) return preferredScale;
	return std::max(minimumScale, std::min(preferredScale,
		maximumWidth / (static_cast<float>(text.size()) * bitmapCharacterWidth)));
}

void drawAdjustmentRow(const AdjustmentRow & row,
	const ofRectangle & rectangle) {
	drawPanel(rectangle, Palette::colorCalibrationPanelAlt,
		Palette::colorActive);
	const float contentWidth = rectangle.width - 24.0f;
	const float labelScale = fittedTextScale(row.label, contentWidth,
		ofClamp(1.12f + rectangle.height / 260.0f, 1.2f, 1.5f), 1.0f);
	const float preferredValueScale = ofClamp(1.65f + rectangle.height / 90.0f,
		2.1f, 3.6f);
	const float valueScale = fittedTextScale(row.value, contentWidth,
		preferredValueScale, 1.5f);
	const float hintScale = fittedTextScale(row.hint, contentWidth,
		ofClamp(0.94f + rectangle.height / 440.0f, 1.05f, 1.35f), 0.88f);
	drawText(row.label, rectangle.x + 12.0f, rectangle.y + 9.0f,
		labelScale,
		Palette::colorCalibrationMuted);
	drawText(row.value, rectangle.x + 12.0f,
		rectangle.getCenter().y - bitmapLineHeight * valueScale * 0.5f,
		valueScale,
		Palette::colorCalibrationText);
	drawText(row.hint, rectangle.x + 12.0f,
		rectangle.getBottom() - bitmapLineHeight * hintScale - 8.0f,
		hintScale,
		Palette::colorCalibrationWarning);
}

ModeStatus modeStatus(CalibrationPanel::Group group,
	const CalibrationControlClient & control,
	const ProjectionSettings & projection) {
	const bool projectionGroup = static_cast<int>(group)
		>= static_cast<int>(CalibrationPanel::Group::SurfaceSize);
	if (projectionGroup) {
		if (projection.hasError()) {
			return { "DISPLAY ERROR | "
				+ clippedText(projection.getLastResult(), 24),
				Palette::colorCalibrationError };
		}
		return projection.isDirty()
			? ModeStatus { "DISPLAY UNSAVED", Palette::colorCalibrationWarning }
			: ModeStatus { "DISPLAY SAVED", Palette::colorCalibrationLive };
	}

	const float stateAge = control.getStateAgeSeconds();
	const bool sensorLive = stateAge >= 0.0f
		&& stateAge <= controlConnectionFreshnessSeconds;
	if (!sensorLive || !control.hasState()) {
		return { "PI A NO SIGNAL", Palette::colorCalibrationError };
	}
	if (group == CalibrationPanel::Group::TableMask) {
		const TableMaskState & tableMask = control.getTableMaskState();
		if (!tableMask.supported) {
			return { "PI A UPDATE REQUIRED", Palette::colorCalibrationError };
		}
		if (tableMask.captureState == TableMaskCaptureState::Capturing) {
			return { "CAPTURING EMPTY TABLE", Palette::colorCalibrationWarning };
		}
		if (tableMask.captureState == TableMaskCaptureState::Error) {
			return { "TABLE MASK ERROR", Palette::colorCalibrationError };
		}
		if (!tableMask.active) {
			return { "TABLE MASK MISSING", Palette::colorCalibrationWarning };
		}
	}
	if (group == CalibrationPanel::Group::HandPosition
		&& !control.supportsHandPositionSettings()) {
		return { "PI A UPDATE REQUIRED", Palette::colorCalibrationError };
	}
	if (control.hasError()) {
		return { "SENSOR ERROR | "
			+ clippedText(control.getLastResult(), 24),
			Palette::colorCalibrationError };
	}
	if (control.hasPendingRequest()) {
		return { "PI A CONFIRMING", Palette::colorCalibrationWarning };
	}
	return control.isDirty()
		? ModeStatus { "SENSOR UNSAVED", Palette::colorCalibrationWarning }
		: ModeStatus { "SENSOR SAVED", Palette::colorCalibrationLive };
}
}

void CalibrationPanel::drawControlCard(const ofRectangle & rectangle,
	const CalibrationControlClient & control,
	const ProjectionSettings & projection) const {
	drawPanel(rectangle, Palette::colorCalibrationPanel,
		Palette::colorActive);

	if (!isProjectionGroup() && !control.hasState()) {
		const ofRectangle messageRectangle(rectangle.x + 12.0f,
			rectangle.y + 12.0f, rectangle.width - 24.0f,
			rectangle.height - 24.0f);
		drawPanel(messageRectangle, Palette::colorCalibrationPanelAlt,
			Palette::colorCalibrationWarning);
		drawCenteredText("WAITING FOR CONFIRMED SENSOR VALUES",
			messageRectangle,
			fittedTextScale("WAITING FOR CONFIRMED SENSOR VALUES",
				messageRectangle.width - 24.0f, 1.25f, 0.8f),
			Palette::colorCalibrationWarning);
		return;
	}

	std::vector<AdjustmentRow> rows;
	if (isProjectionGroup()) {
		rows = projectionRows(group, projection.get());
	} else if (group == Group::TableMask) {
		rows = tableMaskRows(control.getState(), control.getTableMaskState());
	} else if (group == Group::Depth) {
		rows = depthRows(control.getState(), control.getTableMaskState());
	} else if (group == Group::HandPosition) {
		rows = handPositionRows(control.getState(),
			control.supportsHandPositionSettings());
	} else if (group == Group::Visibility) {
		rows = visibilityRows(control.getState());
	} else {
		rows = sensorRows(control.getState(), activeFields());
	}
	if (rows.empty()) return;

	const float rowGap = 10.0f;
	const bool placeSideBySide = rows.size() == 2 && rectangle.width >= 480.0f;
	const float availableWidth = rectangle.width - 24.0f;
	const float availableHeight = rectangle.height - 24.0f;
	const float rowWidth = placeSideBySide
		? (availableWidth - rowGap) * 0.5f : availableWidth;
	const float rowHeight = placeSideBySide
		? availableHeight
		: (availableHeight - rowGap * static_cast<float>(rows.size() - 1))
			/ static_cast<float>(rows.size());
	for (size_t index = 0; index < rows.size(); ++index) {
		const float rowX = rectangle.x + 12.0f
			+ (placeSideBySide
				? static_cast<float>(index) * (rowWidth + rowGap) : 0.0f);
		const float rowY = rectangle.y + 12.0f
			+ (placeSideBySide
				? 0.0f : static_cast<float>(index) * (rowHeight + rowGap));
		const ofRectangle rowRectangle(rowX, rowY, rowWidth, rowHeight);
		drawAdjustmentRow(rows[index], rowRectangle);
	}
}

void CalibrationPanel::drawShortcutBar(const ofRectangle & rectangle) const {
	drawPanel(rectangle, Palette::colorCalibrationPanel,
		Palette::colorCalibrationWarning);
	const std::string first = "[TAB/M] NEXT  [SHIFT+TAB/M] PREVIOUS  [ARROWS] ADJUST";
	std::string second;
	if (!isProjectionGroup()) {
		second = "[W] SAVE  [R/ESC] RELOAD  [P] DIAGNOSTIC  [D] PANEL";
		if (group == Group::TableMask) {
			second = "[C] CAPTURE TABLE  [X] CLEAR  [W] SAVE  [R/ESC] RELOAD";
		}
		if (group == Group::RoiPosition || group == Group::RoiSize) {
			second += "  [F] FULL ROI";
		}
	} else if (group == Group::SurfaceSize || group == Group::SurfaceRounding
		|| group == Group::ViewportPosition || group == Group::ViewportExtent) {
		second = "[SHIFT+ARROWS] FINE  [W] SAVE  [R/ESC] RELOAD  [D] PANEL";
	} else {
		second = "[W] SAVE  [R/ESC] RELOAD  [D] PANEL  [V] VISUAL";
	}
	const float firstScale = fittedTextScale(first, rectangle.width - 20.0f,
		1.22f, 0.9f);
	const float secondScale = fittedTextScale(second, rectangle.width - 20.0f,
		1.22f, 0.9f);
	drawText(first, rectangle.x + 10.0f, rectangle.y + 6.0f,
		firstScale, Palette::colorCalibrationText);
	drawText(second, rectangle.x + 10.0f,
		rectangle.getBottom() - bitmapLineHeight * secondScale - 6.0f,
		secondScale, Palette::colorCalibrationMuted);
}

void CalibrationPanel::draw(const CalibrationControlClient & control,
	const PreviewReceiver & telemetry, const ProjectionSettings & projection,
	const std::vector<DetectedObject> & mappedObjects,
	const ofRectangle & playRect, int canvasWidth, int canvasHeight) const {
	ofPushStyle();
	ofSetColor(Palette::colorCalibrationBackdrop);
	ofDrawRectangle(0, 0, canvasWidth, canvasHeight);
	const float tableRadius = TableShape::cornerRadius(playRect.width,
		playRect.height, projection.get().cornerRadiusFraction);
	const ofRectangle textRectangle = lowerTextRectangle(playRect);
	if (isProjectionGroup()) {
		drawProjectionGuide(projection, mappedObjects, playRect);
	}
	const bool diagnosticsVisible = showImages && !isProjectionGroup();
	if (diagnosticsVisible) {
		const float diagnosticBottom = showText
			? textRectangle.y - 8.0f : playRect.getBottom() - 8.0f;
		const ofRectangle diagnosticRectangle(playRect.x,
			playRect.y + 8.0f, playRect.width,
			std::max(1.0f, diagnosticBottom - playRect.y - 8.0f));
		drawImageDiagnostics(control, telemetry, diagnosticRectangle);
	}

	if (showText) {
		const ModeStatus status = modeStatus(group,
			control, projection);
		const float gap = 8.0f;
		const float headerHeight = ofClamp(textRectangle.height * 0.28f,
			58.0f, 64.0f);
		const float shortcutHeight = ofClamp(textRectangle.height * 0.18f,
			38.0f, 48.0f);
		const ofRectangle header = tableBandRectangle(playRect, tableRadius,
			textRectangle.y, textRectangle.y + headerHeight);
		drawPanel(header, Palette::colorCalibrationPanel,
			status.color);
		const std::string context = std::string(isProjectionGroup()
			? "LOCAL DISPLAY" : "PI A SENSOR") + "  |  MODE "
			+ ofToString(static_cast<int>(group) + 1) + " OF "
			+ ofToString(static_cast<int>(Group::Count));
		drawText(context, header.x + 14.0f, header.y + 6.0f,
			1.12f, isProjectionGroup() ? Palette::colorAccent
				: Palette::colorCalibrationLive);
		drawText(activeGroupLabel(), header.x + 14.0f, header.y + 25.0f,
			fittedTextScale(activeGroupLabel(), header.width - 28.0f,
				2.25f, 1.5f), Palette::colorCalibrationText);
		drawBadge(status.text,
			header.getRight() - textWidth(status.text, 1.08f) - 20.0f,
			header.y + 5.0f, 1.08f, status.color,
			Palette::colorCalibrationPanelAlt);

		const ofRectangle shortcutRectangle = tableBandRectangle(playRect,
			tableRadius, textRectangle.getBottom() - shortcutHeight,
			textRectangle.getBottom());
		const float controlTop = header.getBottom() + gap;
		const ofRectangle controlRectangle = tableBandRectangle(playRect,
			tableRadius, controlTop, shortcutRectangle.y - gap);
		drawControlCard(controlRectangle, control, projection);
		drawShortcutBar(shortcutRectangle);
	} else {
		const ofRectangle hiddenTextRectangle = tableBandRectangle(playRect,
			tableRadius, textRectangle.y, textRectangle.y + 32.0f);
		drawBadge(std::string("[D] SHOW ") + activeGroupLabel()
			+ " SETTINGS", hiddenTextRectangle.x, hiddenTextRectangle.y, 1.3f,
			Palette::colorCalibrationText, Palette::colorCalibrationPanel);
	}

	// Repaint everything outside the calibrated table after all labels have
	// been drawn, so maintenance text cannot spill onto the surroundings.
	ofSetColor(Palette::colorCalibrationBackdrop);
	ofDrawRectangle(0.0f, 0.0f, canvasWidth, playRect.y);
	ofDrawRectangle(0.0f, playRect.getBottom(), canvasWidth,
		canvasHeight - playRect.getBottom());
	ofDrawRectangle(0.0f, playRect.y, playRect.x, playRect.height);
	ofDrawRectangle(playRect.getRight(), playRect.y,
		canvasWidth - playRect.getRight(), playRect.height);
	if (tableRadius > 0.0f) {
		drawRoiCornerCutouts(playRect, tableRadius);
	}
	ofPopStyle();
}
