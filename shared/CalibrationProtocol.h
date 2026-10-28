#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

// The bidirectional calibration contract is deliberately separate from the
// show-critical object stream. Both applications include this dependency-free
// description, while OSC parsing and transport stay in their role-specific
// classes.
namespace CalibrationProtocol {

constexpr int32_t version = 1;
constexpr int controlPort = 8002;

constexpr const char * getAddress = "/calibration/v1/get";
constexpr const char * setAddress = "/calibration/v1/set";
constexpr const char * saveAddress = "/calibration/v1/save";
constexpr const char * reloadAddress = "/calibration/v1/reload";
constexpr const char * resetRoiAddress = "/calibration/v1/reset-roi";
constexpr const char * captureTableMaskAddress = "/calibration/v1/capture-table-mask";
constexpr const char * clearTableMaskAddress = "/calibration/v1/clear-table-mask";
constexpr const char * stateAddress = "/calibration/v1/state";
constexpr const char * resultAddress = "/calibration/v1/result";

constexpr const char * depthPreviewAddress = "/calibration/depth/v1";
constexpr const char * trackFrameAddress = "/calibration/tracks/v1";
constexpr const char * trackAddress = "/calibration/track/v1";

constexpr int getArgumentCount = 2;
constexpr int setArgumentCount = 5;
constexpr int mutationArgumentCount = 3;
constexpr int stateMetadataArgumentCount = 5;
constexpr int legacySettingsFieldCount = 22;
constexpr int tableMaskStateArgumentCount = 33;
constexpr int interactionPointSettingsFieldCount = 2;
constexpr int tableMaskMetadataArgumentCount = 5;
constexpr int resultArgumentCount = 6;
constexpr float requestRetryIntervalSeconds = 0.35f;
constexpr int maximumRequestSendCount = 4;

constexpr int32_t maximumId = std::numeric_limits<int32_t>::max();
constexpr uint32_t revisionModulus = static_cast<uint32_t>(maximumId) + 1u;
constexpr int maximumTrackCount = 40;
constexpr int maximumTracksPerDatagram = 10;
constexpr int maximumTrackDatagramBytes = 1204;
constexpr int ipv4UdpPayloadLimit = 1472;

static_assert(maximumTrackDatagramBytes < ipv4UdpPayloadLimit,
	"calibration track packet would require IP fragmentation");

enum class Command {
	Get,
	Set,
	Save,
	Reload,
	ResetRoi,
	CaptureTableMask,
	ClearTableMask
};

enum class ResultCode : int32_t {
	Ok = 0,
	InvalidRequest = 1,
	StaleRevision = 2,
	UnknownField = 3,
	InvalidValue = 4,
	PersistenceError = 5
};

// Every DetectionSettings member has a stable wire id. New fields are
// appended; existing ids are never reused.
enum class Field : int32_t {
	NearMM = 0,
	FarMM = 1,
	MinAreaRadius = 2,
	MaxAreaRadius = 3,
	TrackerMaximumDistance = 4,
	TrackerPersistence = 5,
	RoiX = 6,
	RoiY = 7,
	RoiWidth = 8,
	RoiHeight = 9,
	MorphOpenRadius = 10,
	MorphCloseRadius = 11,
	PositionSmoothingMinCutoff = 12,
	PositionSmoothingBeta = 13,
	HandSolidityThreshold = 14,
	HandMinPalmRadiusMM = 15,
	HandMaxPalmRadiusMM = 16,
	StationarySeconds = 17,
	StationaryTolerance = 18,
	HideSeconds = 19,
	HandMaxBorderWidth = 20,
	UsePalmCentreForHands = 21,
	TableSurfaceClearanceMM = 22,
	HandPositionMode = 23,
	InwardTipCapFraction = 24,
	Count = 25
};

constexpr int legacyStateArgumentCount = stateMetadataArgumentCount
	+ legacySettingsFieldCount;
// The table-mask block already followed the first 23 settings in v1. New
// settings are appended after that complete 33-argument prefix so an older Pi
// B keeps reading the table-mask metadata at its established indexes.
constexpr int stateArgumentCount = tableMaskStateArgumentCount
	+ interactionPointSettingsFieldCount;

inline bool isCompleteStateArgumentCount(int argumentCount) {
	return argumentCount == legacyStateArgumentCount
		|| argumentCount == tableMaskStateArgumentCount
		|| argumentCount >= stateArgumentCount;
}

enum class ValueKind {
	FloatingPoint,
	Integer,
	Boolean
};

struct FieldDescriptor {
	Field field;
	const char * name;
	ValueKind kind;
	float step;
};

constexpr std::array<FieldDescriptor, static_cast<size_t>(Field::Count)> fields {{
	{ Field::NearMM, "nearMM", ValueKind::FloatingPoint, 25.0f },
	{ Field::FarMM, "farMM", ValueKind::FloatingPoint, 25.0f },
	{ Field::MinAreaRadius, "minAreaRadius", ValueKind::FloatingPoint, 1.0f },
	{ Field::MaxAreaRadius, "maxAreaRadius", ValueKind::FloatingPoint, 5.0f },
	{ Field::TrackerMaximumDistance, "trackerMaximumDistance", ValueKind::FloatingPoint, 1.0f },
	{ Field::TrackerPersistence, "trackerPersistence", ValueKind::Integer, 1.0f },
	{ Field::RoiX, "roiX", ValueKind::FloatingPoint, 0.01f },
	{ Field::RoiY, "roiY", ValueKind::FloatingPoint, 0.01f },
	{ Field::RoiWidth, "roiWidth", ValueKind::FloatingPoint, 0.01f },
	{ Field::RoiHeight, "roiHeight", ValueKind::FloatingPoint, 0.01f },
	{ Field::MorphOpenRadius, "morphOpenRadius", ValueKind::Integer, 1.0f },
	{ Field::MorphCloseRadius, "morphCloseRadius", ValueKind::Integer, 1.0f },
	{ Field::PositionSmoothingMinCutoff, "positionSmoothingMinCutoff", ValueKind::FloatingPoint, 0.2f },
	{ Field::PositionSmoothingBeta, "positionSmoothingBeta", ValueKind::FloatingPoint, 0.1f },
	{ Field::HandSolidityThreshold, "handSolidityThreshold", ValueKind::FloatingPoint, 0.01f },
	{ Field::HandMinPalmRadiusMM, "handMinPalmRadiusMM", ValueKind::FloatingPoint, 5.0f },
	{ Field::HandMaxPalmRadiusMM, "handMaxPalmRadiusMM", ValueKind::FloatingPoint, 5.0f },
	{ Field::StationarySeconds, "stationarySeconds", ValueKind::FloatingPoint, 1.0f },
	{ Field::StationaryTolerance, "stationaryTolerance", ValueKind::FloatingPoint, 0.005f },
	{ Field::HideSeconds, "hideSeconds", ValueKind::FloatingPoint, 1.0f },
	{ Field::HandMaxBorderWidth, "handMaxBorderWidth", ValueKind::FloatingPoint, 0.01f },
	{ Field::UsePalmCentreForHands, "usePalmCentreForHands", ValueKind::Boolean, 1.0f },
	{ Field::TableSurfaceClearanceMM, "tableSurfaceClearanceMM", ValueKind::FloatingPoint, 2.0f },
	{ Field::HandPositionMode, "handPositionMode", ValueKind::Integer, 1.0f },
	{ Field::InwardTipCapFraction, "inwardTipCapFraction", ValueKind::FloatingPoint, 0.01f }
}};

// The selected hand position has a stable numeric form because it is edited
// over the calibration wire. It does not alter the production object packet:
// whichever source wins still becomes DetectedObject::x/y.
enum class HandPositionMode : int32_t {
	ContourCentroid = 0,
	PalmCentre = 1,
	InwardTip = 2
};

inline bool isKnownHandPositionMode(int32_t value) {
	return value >= static_cast<int32_t>(HandPositionMode::ContourCentroid)
		&& value <= static_cast<int32_t>(HandPositionMode::InwardTip);
}

inline const char * handPositionModeLabel(HandPositionMode mode) {
	switch (mode) {
	case HandPositionMode::ContourCentroid: return "BLOB CENTRE";
	case HandPositionMode::PalmCentre: return "PALM CENTRE";
	case HandPositionMode::InwardTip: return "INWARD TIP";
	default: return "UNKNOWN";
	}
}

// Transport-only mirror of DetectionSettings. It keeps the shared protocol
// independent of openFrameworks and gives Pi B a complete confirmed snapshot.
struct SettingsState {
	float nearMM = 500.0f;
	float farMM = 1000.0f;
	float minAreaRadius = 20.0f;
	float maxAreaRadius = 200.0f;
	float trackerMaximumDistance = 32.0f;
	int32_t trackerPersistence = 15;
	float roiX = 0.0f;
	float roiY = 0.0f;
	float roiWidth = 1.0f;
	float roiHeight = 1.0f;
	int32_t morphOpenRadius = 1;
	int32_t morphCloseRadius = 2;
	float positionSmoothingMinCutoff = 1.2f;
	float positionSmoothingBeta = 0.4f;
	float handSolidityThreshold = 0.8f;
	float handMinPalmRadiusMM = 25.0f;
	float handMaxPalmRadiusMM = 90.0f;
	float stationarySeconds = 5.0f;
	float stationaryTolerance = 0.02f;
	float hideSeconds = 10.0f;
	float handMaxBorderWidth = 0.25f;
	bool usePalmCentreForHands = true;
	float tableSurfaceClearanceMM = 15.0f;
	HandPositionMode handPositionMode = HandPositionMode::PalmCentre;
	float inwardTipCapFraction = 0.15f;
};

enum class TableMaskCaptureState : int32_t {
	Missing = 0,
	Capturing = 1,
	Ready = 2,
	Error = 3
};

// Appended to a state message after all scalar settings. New Pi B releases
// infer supported=false when paired with an older Pi A that sends only the
// original 22-field prefix.
struct TableMaskState {
	bool supported = false;
	bool active = false;
	TableMaskCaptureState captureState = TableMaskCaptureState::Missing;
	int32_t capturedFrames = 0;
	int32_t requiredFrames = 0;
	float validCoverage = 0.0f;
};

struct TrackDiagnostic {
	int32_t id = 0;
	int32_t type = 0;
	float rawX = 0.0f;
	float rawY = 0.0f;
	float smoothedX = 0.0f;
	float smoothedY = 0.0f;
	float vx = 0.0f;
	float vy = 0.0f;
	float palmRadiusMM = 0.0f;
	float solidity = 0.0f;
	float borderContact = 0.0f;
	float stillForSeconds = 0.0f;
	bool hidden = false;
};

struct Request {
	Command command = Command::Get;
	int32_t requestId = 0;
	int32_t expectedRevision = 0;
	Field field = Field::NearMM;
	float value = 0.0f;
};

inline int32_t nextRequestId(int32_t current) {
	return current <= 0 || current >= maximumId ? 1 : current + 1;
}

inline int32_t nextRevision(int32_t current) {
	return current < 0 || current >= maximumId ? 0 : current + 1;
}

inline uint32_t revisionDistance(int32_t candidate, int32_t reference) {
	if (candidate < 0 || reference < 0) {
		return 0;
	}
	return (static_cast<uint32_t>(candidate) + revisionModulus
		- static_cast<uint32_t>(reference)) % revisionModulus;
}

inline bool isRevisionNewer(int32_t candidate, int32_t reference) {
	const uint32_t distance = revisionDistance(candidate, reference);
	return distance > 0 && distance < revisionModulus / 2u;
}

inline bool isKnownField(int32_t wireValue) {
	return wireValue >= 0 && wireValue < static_cast<int32_t>(Field::Count);
}

inline bool isValidRequestIdentity(const Request & request) {
	return request.requestId > 0 && request.expectedRevision >= 0;
}

inline bool hasExpectedRevision(const Request & request, int32_t currentRevision) {
	return request.command == Command::Get
		|| request.expectedRevision == currentRevision;
}

inline bool sameRequest(const Request & left, const Request & right) {
	return left.command == right.command && left.requestId == right.requestId
		&& left.expectedRevision == right.expectedRevision
		&& (left.command != Command::Set
			|| (left.field == right.field && left.value == right.value));
}

inline const FieldDescriptor & descriptor(Field field) {
	return fields[static_cast<size_t>(field)];
}

inline float fieldValue(const SettingsState & state, Field field) {
	switch (field) {
	case Field::NearMM: return state.nearMM;
	case Field::FarMM: return state.farMM;
	case Field::MinAreaRadius: return state.minAreaRadius;
	case Field::MaxAreaRadius: return state.maxAreaRadius;
	case Field::TrackerMaximumDistance: return state.trackerMaximumDistance;
	case Field::TrackerPersistence: return static_cast<float>(state.trackerPersistence);
	case Field::RoiX: return state.roiX;
	case Field::RoiY: return state.roiY;
	case Field::RoiWidth: return state.roiWidth;
	case Field::RoiHeight: return state.roiHeight;
	case Field::MorphOpenRadius: return static_cast<float>(state.morphOpenRadius);
	case Field::MorphCloseRadius: return static_cast<float>(state.morphCloseRadius);
	case Field::PositionSmoothingMinCutoff: return state.positionSmoothingMinCutoff;
	case Field::PositionSmoothingBeta: return state.positionSmoothingBeta;
	case Field::HandSolidityThreshold: return state.handSolidityThreshold;
	case Field::HandMinPalmRadiusMM: return state.handMinPalmRadiusMM;
	case Field::HandMaxPalmRadiusMM: return state.handMaxPalmRadiusMM;
	case Field::StationarySeconds: return state.stationarySeconds;
	case Field::StationaryTolerance: return state.stationaryTolerance;
	case Field::HideSeconds: return state.hideSeconds;
	case Field::HandMaxBorderWidth: return state.handMaxBorderWidth;
	case Field::UsePalmCentreForHands: return state.usePalmCentreForHands ? 1.0f : 0.0f;
	case Field::TableSurfaceClearanceMM: return state.tableSurfaceClearanceMM;
	case Field::HandPositionMode: return static_cast<float>(state.handPositionMode);
	case Field::InwardTipCapFraction: return state.inwardTipCapFraction;
	default: return 0.0f;
	}
}

inline bool isValidSetValue(Field field, float value) {
	if (!std::isfinite(value) || std::fabs(value) > 100000.0f) {
		return false;
	}
	const ValueKind kind = descriptor(field).kind;
	if (kind == ValueKind::Boolean) {
		return value == 0.0f || value == 1.0f;
	}
	if (field == Field::HandPositionMode) {
		return std::floor(value) == value
			&& isKnownHandPositionMode(static_cast<int32_t>(value));
	}
	return kind != ValueKind::Integer || std::floor(value) == value;
}

inline bool setFieldValue(SettingsState & state, Field field, float value) {
	if (!isValidSetValue(field, value)) {
		return false;
	}
	switch (field) {
	case Field::NearMM: state.nearMM = value; break;
	case Field::FarMM: state.farMM = value; break;
	case Field::MinAreaRadius: state.minAreaRadius = value; break;
	case Field::MaxAreaRadius: state.maxAreaRadius = value; break;
	case Field::TrackerMaximumDistance: state.trackerMaximumDistance = value; break;
	case Field::TrackerPersistence: state.trackerPersistence = static_cast<int32_t>(value); break;
	case Field::RoiX: state.roiX = value; break;
	case Field::RoiY: state.roiY = value; break;
	case Field::RoiWidth: state.roiWidth = value; break;
	case Field::RoiHeight: state.roiHeight = value; break;
	case Field::MorphOpenRadius: state.morphOpenRadius = static_cast<int32_t>(value); break;
	case Field::MorphCloseRadius: state.morphCloseRadius = static_cast<int32_t>(value); break;
	case Field::PositionSmoothingMinCutoff: state.positionSmoothingMinCutoff = value; break;
	case Field::PositionSmoothingBeta: state.positionSmoothingBeta = value; break;
	case Field::HandSolidityThreshold: state.handSolidityThreshold = value; break;
	case Field::HandMinPalmRadiusMM: state.handMinPalmRadiusMM = value; break;
	case Field::HandMaxPalmRadiusMM: state.handMaxPalmRadiusMM = value; break;
	case Field::StationarySeconds: state.stationarySeconds = value; break;
	case Field::StationaryTolerance: state.stationaryTolerance = value; break;
	case Field::HideSeconds: state.hideSeconds = value; break;
	case Field::HandMaxBorderWidth: state.handMaxBorderWidth = value; break;
	case Field::UsePalmCentreForHands:
		state.usePalmCentreForHands = value == 1.0f;
		if (!state.usePalmCentreForHands) {
			state.handPositionMode = HandPositionMode::ContourCentroid;
		} else if (state.handPositionMode == HandPositionMode::ContourCentroid) {
			state.handPositionMode = HandPositionMode::PalmCentre;
		}
		break;
	case Field::TableSurfaceClearanceMM: state.tableSurfaceClearanceMM = value; break;
	case Field::HandPositionMode:
		state.handPositionMode = static_cast<HandPositionMode>(static_cast<int32_t>(value));
		state.usePalmCentreForHands = state.handPositionMode != HandPositionMode::ContourCentroid;
		break;
	case Field::InwardTipCapFraction: state.inwardTipCapFraction = value; break;
	default: return false;
	}
	return true;
}

inline bool isSaneSettings(const SettingsState & state) {
	const auto finiteReasonable = [](float value) {
		return std::isfinite(value) && std::fabs(value) <= 100000.0f;
	};
	return finiteReasonable(state.nearMM) && state.nearMM >= 0.0f
		&& finiteReasonable(state.farMM) && state.farMM >= state.nearMM + 10.0f
		&& finiteReasonable(state.minAreaRadius) && state.minAreaRadius >= 1.0f
		&& finiteReasonable(state.maxAreaRadius) && state.maxAreaRadius >= state.minAreaRadius + 1.0f
		&& finiteReasonable(state.trackerMaximumDistance) && state.trackerMaximumDistance >= 1.0f
		&& state.trackerPersistence >= 1
		&& state.roiX >= 0.0f && state.roiX <= 0.95f
		&& state.roiY >= 0.0f && state.roiY <= 0.95f
		&& state.roiWidth >= 0.05f && state.roiWidth <= 1.0f - state.roiX
		&& state.roiHeight >= 0.05f && state.roiHeight <= 1.0f - state.roiY
		&& state.morphOpenRadius >= 0 && state.morphOpenRadius <= 15
		&& state.morphCloseRadius >= 0 && state.morphCloseRadius <= 15
		&& state.positionSmoothingMinCutoff >= 0.05f
		&& state.positionSmoothingMinCutoff <= 15.0f
		&& state.positionSmoothingBeta >= 0.0f && state.positionSmoothingBeta <= 5.0f
		&& state.handSolidityThreshold >= 0.0f && state.handSolidityThreshold <= 1.0f
		&& state.handMinPalmRadiusMM >= 0.0f
		&& state.handMaxPalmRadiusMM >= state.handMinPalmRadiusMM
		&& state.stationarySeconds >= 0.0f
		&& state.stationaryTolerance >= 0.0f && state.stationaryTolerance <= 1.0f
		&& state.hideSeconds >= 0.0f
		&& state.handMaxBorderWidth >= 0.0f && state.handMaxBorderWidth <= 1.0f
		&& state.tableSurfaceClearanceMM >= 0.0f
		&& state.tableSurfaceClearanceMM <= 200.0f
		&& isKnownHandPositionMode(static_cast<int32_t>(state.handPositionMode))
		&& state.inwardTipCapFraction >= 0.05f
		&& state.inwardTipCapFraction <= 0.40f;
}

inline bool isSaneTableMaskState(const TableMaskState & state) {
	if (!state.supported) return true;
	const int32_t captureState = static_cast<int32_t>(state.captureState);
	const bool capturing = state.captureState == TableMaskCaptureState::Capturing;
	return captureState >= static_cast<int32_t>(TableMaskCaptureState::Missing)
		&& captureState <= static_cast<int32_t>(TableMaskCaptureState::Error)
		&& state.capturedFrames >= 0 && state.requiredFrames > 0
		&& (capturing ? state.capturedFrames < state.requiredFrames
			: state.capturedFrames == 0)
		&& std::isfinite(state.validCoverage)
		&& state.validCoverage >= 0.0f && state.validCoverage <= 1.0f
		&& (!state.active || state.captureState != TableMaskCaptureState::Missing)
		&& (state.captureState != TableMaskCaptureState::Ready || state.active);
}

inline bool sameSettings(const SettingsState & left, const SettingsState & right) {
	for (const FieldDescriptor & item : fields) {
		if (fieldValue(left, item.field) != fieldValue(right, item.field)) {
			return false;
		}
	}
	return true;
}

inline bool sameTrackDiagnostic(const TrackDiagnostic & left,
	const TrackDiagnostic & right) {
	return left.id == right.id && left.type == right.type
		&& left.rawX == right.rawX && left.rawY == right.rawY
		&& left.smoothedX == right.smoothedX && left.smoothedY == right.smoothedY
		&& left.vx == right.vx && left.vy == right.vy
		&& left.palmRadiusMM == right.palmRadiusMM
		&& left.solidity == right.solidity
		&& left.borderContact == right.borderContact
		&& left.stillForSeconds == right.stillForSeconds
		&& left.hidden == right.hidden;
}

} // namespace CalibrationProtocol
