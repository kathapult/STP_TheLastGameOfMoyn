#include "CalibrationControlServer.h"
#include <chrono>
#include <cmath>
#include <random>

using namespace CalibrationProtocol;

namespace {
int32_t createSessionId() {
	const uint32_t clockBits = static_cast<uint32_t>(
		std::chrono::high_resolution_clock::now().time_since_epoch().count());
	std::random_device randomDevice;
	const uint32_t value = (clockBits ^ randomDevice()) & 0x7fffffffu;
	return static_cast<int32_t>(value == 0 ? 1 : value);
}

bool hasType(const ofxOscMessage & message, int index, ofxOscArgType type) {
	return index >= 0 && index < message.getNumArgs()
		&& message.getArgType(index) == type;
}

SettingsState toWire(const DetectionSettings & source) {
	SettingsState state;
	state.nearMM = source.nearMM;
	state.farMM = source.farMM;
	state.minAreaRadius = source.minAreaRadius;
	state.maxAreaRadius = source.maxAreaRadius;
	state.trackerMaximumDistance = source.trackerMaximumDistance;
	state.trackerPersistence = source.trackerPersistence;
	state.roiX = source.roiX;
	state.roiY = source.roiY;
	state.roiWidth = source.roiWidth;
	state.roiHeight = source.roiHeight;
	state.morphOpenRadius = source.morphOpenRadius;
	state.morphCloseRadius = source.morphCloseRadius;
	state.positionSmoothingMinCutoff = source.positionSmoothingMinCutoff;
	state.positionSmoothingBeta = source.positionSmoothingBeta;
	state.handSolidityThreshold = source.handSolidityThreshold;
	state.handMinPalmRadiusMM = source.handMinPalmRadiusMM;
	state.handMaxPalmRadiusMM = source.handMaxPalmRadiusMM;
	state.stationarySeconds = source.stationarySeconds;
	state.stationaryTolerance = source.stationaryTolerance;
	state.hideSeconds = source.hideSeconds;
	state.handMaxBorderWidth = source.handMaxBorderWidth;
	state.handPositionMode = source.handPositionMode;
	state.inwardTipCapFraction = source.inwardTipCapFraction;
	state.usePalmCentreForHands = source.handPositionMode
		!= HandPositionMode::ContourCentroid;
	state.tableSurfaceClearanceMM = source.tableSurfaceClearanceMM;
	return state;
}

TableMaskState toWire(const TableSurfaceMask::Status & source) {
	TableMaskState state;
	state.supported = true;
	state.active = source.active;
	state.captureState = static_cast<TableMaskCaptureState>(
		static_cast<int32_t>(source.captureState));
	state.capturedFrames = source.capturedFrames;
	state.requiredFrames = source.requiredFrames;
	state.validCoverage = source.validCoverage;
	return state;
}

DetectionSettings fromWire(const SettingsState & source) {
	DetectionSettings settings;
	settings.nearMM = source.nearMM;
	settings.farMM = source.farMM;
	settings.minAreaRadius = source.minAreaRadius;
	settings.maxAreaRadius = source.maxAreaRadius;
	settings.trackerMaximumDistance = source.trackerMaximumDistance;
	settings.trackerPersistence = source.trackerPersistence;
	settings.roiX = source.roiX;
	settings.roiY = source.roiY;
	settings.roiWidth = source.roiWidth;
	settings.roiHeight = source.roiHeight;
	settings.morphOpenRadius = source.morphOpenRadius;
	settings.morphCloseRadius = source.morphCloseRadius;
	settings.positionSmoothingMinCutoff = source.positionSmoothingMinCutoff;
	settings.positionSmoothingBeta = source.positionSmoothingBeta;
	settings.handSolidityThreshold = source.handSolidityThreshold;
	settings.handMinPalmRadiusMM = source.handMinPalmRadiusMM;
	settings.handMaxPalmRadiusMM = source.handMaxPalmRadiusMM;
	settings.stationarySeconds = source.stationarySeconds;
	settings.stationaryTolerance = source.stationaryTolerance;
	settings.hideSeconds = source.hideSeconds;
	settings.handMaxBorderWidth = source.handMaxBorderWidth;
	settings.handPositionMode = source.handPositionMode;
	settings.inwardTipCapFraction = source.inwardTipCapFraction;
	settings.tableSurfaceClearanceMM = source.tableSurfaceClearanceMM;
	return settings;
}
}

void CalibrationControlServer::setup(const std::string & clientHost,
	int listenPort, int responsePort) {
	expectedClientIp = clientHost;
	receiver.setup(listenPort);
	sender.setup(clientHost, responsePort);
	sessionId = createSessionId();
	revision = 0;
	dirty = false;
	enabled = true;
	ofLogNotice("CalibrationControlServer") << "calibration control session "
		<< sessionId << " listening on UDP " << listenPort << " from "
		<< expectedClientIp;
}

bool CalibrationControlServer::parseRequest(const ofxOscMessage & message,
	Request & request) const {
	const std::string & address = message.getAddress();
	if (address == getAddress) {
		request.command = Command::Get;
		if (message.getNumArgs() != getArgumentCount) return false;
	} else if (address == setAddress) {
		request.command = Command::Set;
		if (message.getNumArgs() != setArgumentCount) return false;
	} else if (address == saveAddress) {
		request.command = Command::Save;
		if (message.getNumArgs() != mutationArgumentCount) return false;
	} else if (address == reloadAddress) {
		request.command = Command::Reload;
		if (message.getNumArgs() != mutationArgumentCount) return false;
	} else if (address == resetRoiAddress) {
		request.command = Command::ResetRoi;
		if (message.getNumArgs() != mutationArgumentCount) return false;
	} else if (address == captureTableMaskAddress) {
		request.command = Command::CaptureTableMask;
		if (message.getNumArgs() != mutationArgumentCount) return false;
	} else if (address == clearTableMaskAddress) {
		request.command = Command::ClearTableMask;
		if (message.getNumArgs() != mutationArgumentCount) return false;
	} else {
		return false;
	}

	if (!hasType(message, 0, OFXOSC_TYPE_INT32)
		|| !hasType(message, 1, OFXOSC_TYPE_INT32)
		|| message.getArgAsInt32(0) != version) {
		return false;
	}
	request.requestId = message.getArgAsInt32(1);
	request.expectedRevision = 0;
	if (request.command == Command::Get) {
		return request.requestId > 0;
	}

	if (!hasType(message, 2, OFXOSC_TYPE_INT32)) {
		return false;
	}
	request.expectedRevision = message.getArgAsInt32(2);
	if (request.command == Command::Set) {
		if (!hasType(message, 3, OFXOSC_TYPE_INT32)
			|| !hasType(message, 4, OFXOSC_TYPE_FLOAT)) {
			return false;
		}
		const int32_t wireField = message.getArgAsInt32(3);
		if (!isKnownField(wireField)) {
			request.field = Field::Count;
		} else {
			request.field = static_cast<Field>(wireField);
		}
		request.value = message.getArgAsFloat(4);
	}
	return isValidRequestIdentity(request);
}

const CalibrationControlServer::CachedResponse * CalibrationControlServer::findCached(
	int32_t requestId) const {
	for (const CachedResponse & response : responses) {
		if (response.request.requestId == requestId) {
			return &response;
		}
	}
	return nullptr;
}

void CalibrationControlServer::cache(const CachedResponse & response) {
	responses.push_back(response);
	while (responses.size() > maximumCachedResponses) {
		responses.pop_front();
	}
}

void CalibrationControlServer::processRequest(const Request & request,
	ObjectDetector & detector) {
	if (const CachedResponse * cached = findCached(request.requestId)) {
		if (sameRequest(cached->request, request)) {
			sendResponse(*cached, detector);
		} else {
			sendResult(request.requestId, ResultCode::InvalidRequest,
				"request id reused with different content", revision);
			sendState(request.requestId, revision, dirty,
				toWire(detector.getSettings()),
				toWire(detector.getTableSurfaceMaskStatus()));
		}
		return;
	}

	CachedResponse response;
	response.request = request;
	response.code = ResultCode::Ok;
	response.message = "ok";

	if (!hasExpectedRevision(request, revision)) {
		response.code = ResultCode::StaleRevision;
		response.message = "stale revision";
	} else if (request.command == Command::Set) {
		if (request.field == Field::Count) {
			response.code = ResultCode::UnknownField;
			response.message = "unknown field";
		} else if (!isValidSetValue(request.field, request.value)) {
			response.code = ResultCode::InvalidValue;
			response.message = "invalid value";
		} else {
			SettingsState state = toWire(detector.getSettings());
			setFieldValue(state, request.field, request.value);
			detector.setSettings(fromWire(state));
			dirty = true;
			revision = nextRevision(revision);
		}
	} else if (request.command == Command::Save) {
		if (detector.saveSettings()) {
			dirty = false;
			revision = nextRevision(revision);
		} else {
			response.code = ResultCode::PersistenceError;
			response.message = "settings save failed";
		}
	} else if (request.command == Command::Reload) {
		if (detector.reloadCalibration()) {
			dirty = false;
			revision = nextRevision(revision);
		} else {
			response.code = ResultCode::PersistenceError;
			response.message = "settings reload failed";
		}
	} else if (request.command == Command::ResetRoi) {
		DetectionSettings settings = detector.getSettings();
		settings.roiX = 0.0f;
		settings.roiY = 0.0f;
		settings.roiWidth = 1.0f;
		settings.roiHeight = 1.0f;
		detector.setSettings(settings);
		dirty = true;
		revision = nextRevision(revision);
	} else if (request.command == Command::CaptureTableMask) {
		if (detector.requestTableSurfaceCapture()) {
			response.message = "table mask capture started";
			revision = nextRevision(revision);
		} else {
			response.code = ResultCode::InvalidRequest;
			response.message = "table mask capture already active or unavailable";
		}
	} else if (request.command == Command::ClearTableMask) {
		if (detector.clearTableSurfaceMask()) {
			response.message = "table mask cleared";
			revision = nextRevision(revision);
		} else {
			response.code = ResultCode::PersistenceError;
			response.message = "table mask deletion failed";
		}
	}

	response.revision = revision;
	response.dirty = dirty;
	response.state = toWire(detector.getSettings());
	cache(response);
	sendResponse(response, detector);
}

void CalibrationControlServer::sendResult(int32_t requestId, ResultCode code,
	const std::string & message, int32_t responseRevision) {
	ofxOscMessage result;
	result.setAddress(resultAddress);
	result.addInt32Arg(version);
	result.addInt32Arg(sessionId);
	result.addInt32Arg(requestId);
	result.addInt32Arg(responseRevision);
	result.addInt32Arg(static_cast<int32_t>(code));
	result.addStringArg(message);
	sender.sendMessage(result, false);
}

void CalibrationControlServer::sendState(int32_t requestId, int32_t responseRevision,
	bool responseDirty, const SettingsState & state,
	const TableMaskState & tableMask) {
	ofxOscMessage message;
	message.setAddress(stateAddress);
	message.addInt32Arg(version);
	message.addInt32Arg(sessionId);
	message.addInt32Arg(responseRevision);
	message.addInt32Arg(requestId);
	message.addInt32Arg(responseDirty ? 1 : 0);
	message.addFloatArg(state.nearMM);
	message.addFloatArg(state.farMM);
	message.addFloatArg(state.minAreaRadius);
	message.addFloatArg(state.maxAreaRadius);
	message.addFloatArg(state.trackerMaximumDistance);
	message.addInt32Arg(state.trackerPersistence);
	message.addFloatArg(state.roiX);
	message.addFloatArg(state.roiY);
	message.addFloatArg(state.roiWidth);
	message.addFloatArg(state.roiHeight);
	message.addInt32Arg(state.morphOpenRadius);
	message.addInt32Arg(state.morphCloseRadius);
	message.addFloatArg(state.positionSmoothingMinCutoff);
	message.addFloatArg(state.positionSmoothingBeta);
	message.addFloatArg(state.handSolidityThreshold);
	message.addFloatArg(state.handMinPalmRadiusMM);
	message.addFloatArg(state.handMaxPalmRadiusMM);
	message.addFloatArg(state.stationarySeconds);
	message.addFloatArg(state.stationaryTolerance);
	message.addFloatArg(state.hideSeconds);
	message.addFloatArg(state.handMaxBorderWidth);
	message.addInt32Arg(state.usePalmCentreForHands ? 1 : 0);
	message.addFloatArg(state.tableSurfaceClearanceMM);
	message.addInt32Arg(tableMask.active ? 1 : 0);
	message.addInt32Arg(static_cast<int32_t>(tableMask.captureState));
	message.addInt32Arg(tableMask.capturedFrames);
	message.addInt32Arg(tableMask.requiredFrames);
	message.addFloatArg(tableMask.validCoverage);
	// Appended after the complete table-mask-aware v1 prefix. Old Pi B builds
	// ignore these without shifting the metadata indexes they already know.
	message.addInt32Arg(static_cast<int32_t>(state.handPositionMode));
	message.addFloatArg(state.inwardTipCapFraction);
	sender.sendMessage(message, false);
}

void CalibrationControlServer::sendResponse(const CachedResponse & response,
	const ObjectDetector & detector) {
	sendResult(response.request.requestId, response.code, response.message,
		response.revision);
	sendState(response.request.requestId, response.revision, response.dirty,
		response.state, toWire(detector.getTableSurfaceMaskStatus()));
}

void CalibrationControlServer::update(ObjectDetector & detector) {
	if (!enabled) {
		return;
	}
	const float now = ofGetElapsedTimef();
	while (receiver.hasWaitingMessages()) {
		ofxOscMessage message;
		receiver.getNextMessage(message);
		if (!expectedClientIp.empty() && message.getRemoteHost() != expectedClientIp) {
			rejectedSourceMessages++;
			if (now - lastSourceWarningTime >= 2.0f) {
				ofLogWarning("CalibrationControlServer")
					<< "dropping calibration request from " << message.getRemoteHost()
					<< "; expected " << expectedClientIp;
				lastSourceWarningTime = now;
			}
			continue;
		}

		Request request;
		try {
			if (!parseRequest(message, request)) {
				sendResult(request.requestId, ResultCode::InvalidRequest,
					"malformed request", revision);
				continue;
			}
			processRequest(request, detector);
		} catch (const std::exception & error) {
			sendResult(request.requestId, ResultCode::InvalidRequest,
				error.what(), revision);
		} catch (...) {
			sendResult(request.requestId, ResultCode::InvalidRequest,
				"malformed request", revision);
		}
	}

	// Unsolicited complete state is a heartbeat and lets Pi B recover even if
	// the response to its initial get request was lost.
	if (now - lastStateBroadcastTime >= stateBroadcastIntervalSeconds) {
		sendState(0, revision, dirty, toWire(detector.getSettings()),
			toWire(detector.getTableSurfaceMaskStatus()));
		lastStateBroadcastTime = now;
	}
}
