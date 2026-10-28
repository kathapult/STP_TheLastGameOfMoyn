#include "CalibrationControlClient.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <sstream>

using namespace CalibrationProtocol;

namespace {
bool hasType(const ofxOscMessage & message, int index, ofxOscArgType type) {
	return index >= 0 && index < message.getNumArgs()
		&& message.getArgType(index) == type;
}

int32_t createInitialRequestId() {
	const uint32_t clockBits = static_cast<uint32_t>(
		std::chrono::high_resolution_clock::now().time_since_epoch().count());
	std::random_device randomDevice;
	const uint32_t value = (clockBits ^ randomDevice()) & 0x7fffffffu;
	return static_cast<int32_t>(value == 0 ? 1 : value);
}
}

void CalibrationControlClient::setup(const std::string & serverHost,
	int listenPort, int targetPort) {
	expectedServerIp = serverHost;
	receiver.setup(listenPort);
	sender.setup(serverHost, targetPort);
	nextId = createInitialRequestId();
	enabled = true;
	ofLogNotice("CalibrationControlClient") << "calibration control listening on UDP "
		<< listenPort << " from " << expectedServerIp;
	requestGet();
}

bool CalibrationControlClient::begin(Command command, Field field, float value) {
	if (!enabled || pending.has_value()) {
		return false;
	}
	if (command != Command::Get && !stateAvailable) {
		lastResult = "no confirmed sensor settings";
		error = true;
		return false;
	}
	if ((command == Command::CaptureTableMask
		|| command == Command::ClearTableMask)
		&& !tableMaskState.supported) {
		lastResult = "table mask control is not supported by Pi A";
		error = true;
		return false;
	}
	if (command == Command::Set && !isValidSetValue(field, value)) {
		lastResult = "invalid calibration value";
		error = true;
		return false;
	}

	PendingRequest created;
	created.request.command = command;
	created.request.requestId = nextId;
	created.request.expectedRevision = command == Command::Get ? 0 : revision;
	created.request.field = field;
	created.request.value = value;
	nextId = nextRequestId(nextId);
	pending = created;
	error = false;
	lastResult = "request pending";
	sendPending();
	return true;
}

bool CalibrationControlClient::requestGet() {
	return begin(Command::Get);
}

bool CalibrationControlClient::requestSet(Field field, float absoluteValue) {
	return begin(Command::Set, field, absoluteValue);
}

bool CalibrationControlClient::requestSave() {
	return begin(Command::Save);
}

bool CalibrationControlClient::requestReload() {
	return begin(Command::Reload);
}

bool CalibrationControlClient::requestResetRoi() {
	return begin(Command::ResetRoi);
}

bool CalibrationControlClient::requestCaptureTableMask() {
	return begin(Command::CaptureTableMask);
}

bool CalibrationControlClient::requestClearTableMask() {
	return begin(Command::ClearTableMask);
}

void CalibrationControlClient::sendPending() {
	if (!pending.has_value()) {
		return;
	}
	const Request & request = pending->request;
	ofxOscMessage message;
	switch (request.command) {
	case Command::Get: message.setAddress(getAddress); break;
	case Command::Set: message.setAddress(setAddress); break;
	case Command::Save: message.setAddress(saveAddress); break;
	case Command::Reload: message.setAddress(reloadAddress); break;
	case Command::ResetRoi: message.setAddress(resetRoiAddress); break;
	case Command::CaptureTableMask: message.setAddress(captureTableMaskAddress); break;
	case Command::ClearTableMask: message.setAddress(clearTableMaskAddress); break;
	}
	message.addInt32Arg(version);
	message.addInt32Arg(request.requestId);
	if (request.command != Command::Get) {
		message.addInt32Arg(request.expectedRevision);
	}
	if (request.command == Command::Set) {
		message.addInt32Arg(static_cast<int32_t>(request.field));
		message.addFloatArg(request.value);
	}
	sender.sendMessage(message, false);
	pending->lastSentAt = ofGetElapsedTimef();
	pending->sendCount++;
}

bool CalibrationControlClient::parseState(const ofxOscMessage & message,
	int32_t & incomingSession, int32_t & incomingRevision, int32_t & requestId,
	bool & incomingDirty, SettingsState & incoming,
	TableMaskState & incomingTableMask,
	bool & incomingHandPositionSupport) const {
	// Future protocol revisions may append fields. The v1 prefix remains
	// authoritative and is validated completely; trailing arguments are
	// ignored rather than shifting any existing meaning.
	const int argumentCount = message.getNumArgs();
	if (!isCompleteStateArgumentCount(argumentCount)) {
		return false;
	}
	for (int index = 0; index < stateMetadataArgumentCount; ++index) {
		if (!hasType(message, index, OFXOSC_TYPE_INT32)) return false;
	}
	if (message.getArgAsInt32(0) != version) return false;
	incomingSession = message.getArgAsInt32(1);
	incomingRevision = message.getArgAsInt32(2);
	requestId = message.getArgAsInt32(3);
	const int32_t dirtyValue = message.getArgAsInt32(4);
	if (incomingSession <= 0 || incomingRevision < 0 || requestId < 0
		|| (dirtyValue != 0 && dirtyValue != 1)) return false;
	incomingDirty = dirtyValue == 1;

	const int floatIndexes[] = { 5, 6, 7, 8, 9, 11, 12, 13, 14,
		17, 18, 19, 20, 21, 22, 23, 24, 25 };
	for (int index : floatIndexes) {
		if (!hasType(message, index, OFXOSC_TYPE_FLOAT)) return false;
	}
	for (int index : { 10, 15, 16, 26 }) {
		if (!hasType(message, index, OFXOSC_TYPE_INT32)) return false;
	}

	incoming.nearMM = message.getArgAsFloat(5);
	incoming.farMM = message.getArgAsFloat(6);
	incoming.minAreaRadius = message.getArgAsFloat(7);
	incoming.maxAreaRadius = message.getArgAsFloat(8);
	incoming.trackerMaximumDistance = message.getArgAsFloat(9);
	incoming.trackerPersistence = message.getArgAsInt32(10);
	incoming.roiX = message.getArgAsFloat(11);
	incoming.roiY = message.getArgAsFloat(12);
	incoming.roiWidth = message.getArgAsFloat(13);
	incoming.roiHeight = message.getArgAsFloat(14);
	incoming.morphOpenRadius = message.getArgAsInt32(15);
	incoming.morphCloseRadius = message.getArgAsInt32(16);
	incoming.positionSmoothingMinCutoff = message.getArgAsFloat(17);
	incoming.positionSmoothingBeta = message.getArgAsFloat(18);
	incoming.handSolidityThreshold = message.getArgAsFloat(19);
	incoming.handMinPalmRadiusMM = message.getArgAsFloat(20);
	incoming.handMaxPalmRadiusMM = message.getArgAsFloat(21);
	incoming.stationarySeconds = message.getArgAsFloat(22);
	incoming.stationaryTolerance = message.getArgAsFloat(23);
	incoming.hideSeconds = message.getArgAsFloat(24);
	incoming.handMaxBorderWidth = message.getArgAsFloat(25);
	const int32_t palmValue = message.getArgAsInt32(26);
	if (palmValue != 0 && palmValue != 1) return false;
	incoming.usePalmCentreForHands = palmValue == 1;
	incoming.handPositionMode = palmValue == 1
		? HandPositionMode::PalmCentre : HandPositionMode::ContourCentroid;
	incoming.inwardTipCapFraction = 0.15f;

	// The original v1 state ends after usePalmCentreForHands. A new Pi B must
	// still accept that complete prefix from an older Pi A, but it marks table
	// capture unsupported and never sends the new commands optimistically.
	incomingTableMask = TableMaskState {};
	if (argumentCount >= tableMaskStateArgumentCount) {
		if (!hasType(message, 27, OFXOSC_TYPE_FLOAT)
			|| !hasType(message, 28, OFXOSC_TYPE_INT32)
			|| !hasType(message, 29, OFXOSC_TYPE_INT32)
			|| !hasType(message, 30, OFXOSC_TYPE_INT32)
			|| !hasType(message, 31, OFXOSC_TYPE_INT32)
			|| !hasType(message, 32, OFXOSC_TYPE_FLOAT)) {
			return false;
		}
		incoming.tableSurfaceClearanceMM = message.getArgAsFloat(27);
		const int32_t activeValue = message.getArgAsInt32(28);
		const int32_t captureStateValue = message.getArgAsInt32(29);
		if ((activeValue != 0 && activeValue != 1)
			|| captureStateValue < static_cast<int32_t>(TableMaskCaptureState::Missing)
			|| captureStateValue > static_cast<int32_t>(TableMaskCaptureState::Error)) {
			return false;
		}
		incomingTableMask.supported = true;
		incomingTableMask.active = activeValue == 1;
		incomingTableMask.captureState = static_cast<TableMaskCaptureState>(
			captureStateValue);
		incomingTableMask.capturedFrames = message.getArgAsInt32(30);
		incomingTableMask.requiredFrames = message.getArgAsInt32(31);
		incomingTableMask.validCoverage = message.getArgAsFloat(32);
	}

	incomingHandPositionSupport = false;
	if (argumentCount >= stateArgumentCount) {
		if (!hasType(message, 33, OFXOSC_TYPE_INT32)
			|| !hasType(message, 34, OFXOSC_TYPE_FLOAT)) {
			return false;
		}
		const int32_t mode = message.getArgAsInt32(33);
		if (!isKnownHandPositionMode(mode)) return false;
		incoming.handPositionMode = static_cast<HandPositionMode>(mode);
		incoming.usePalmCentreForHands = incoming.handPositionMode
			!= HandPositionMode::ContourCentroid;
		incoming.inwardTipCapFraction = message.getArgAsFloat(34);
		incomingHandPositionSupport = true;
	}
	return isSaneSettings(incoming) && isSaneTableMaskState(incomingTableMask);
}

bool CalibrationControlClient::isRetiredSession(int32_t candidate) const {
	return std::find(retiredSessions.begin(), retiredSessions.end(), candidate)
		!= retiredSessions.end();
}

void CalibrationControlClient::selectSession(int32_t incomingSession) {
	if (sessionId == incomingSession) {
		return;
	}
	const bool replacingSession = sessionId > 0;
	if (replacingSession) {
		retiredSessions.push_back(sessionId);
		while (retiredSessions.size() > maximumRetiredSessions) {
			retiredSessions.pop_front();
		}
	}
	sessionId = incomingSession;
	revision = -1;
	stateAvailable = false;
	tableMaskState = TableMaskState {};
	handPositionSettingsSupported = false;
	if (replacingSession) {
		pending.reset();
	}
	lastResult = "sensor calibration session changed";
}

void CalibrationControlClient::handleState(const ofxOscMessage & message, float now) {
	int32_t incomingSession = 0;
	int32_t incomingRevision = -1;
	int32_t requestId = 0;
	bool incomingDirty = false;
	SettingsState incoming;
	TableMaskState incomingTableMask;
	bool incomingHandPositionSupport = false;
	if (!parseState(message, incomingSession, incomingRevision, requestId,
		incomingDirty, incoming, incomingTableMask,
		incomingHandPositionSupport)
		|| isRetiredSession(incomingSession)) {
		malformedMessages++;
		return;
	}

	selectSession(incomingSession);
	if (revision >= 0 && incomingRevision != revision
		&& !isRevisionNewer(incomingRevision, revision)) {
		return;
	}
	state = incoming;
	tableMaskState = incomingTableMask;
	handPositionSettingsSupported = incomingHandPositionSupport;
	revision = incomingRevision;
	dirty = incomingDirty;
	stateAvailable = true;
	lastStateTime = now;

	if (pending.has_value() && requestId == pending->request.requestId) {
		pending->stateReceived = true;
		finishPendingIfComplete();
	}
}

void CalibrationControlClient::finishPendingIfComplete() {
	if (!pending.has_value() || !pending->resultReceived || !pending->stateReceived) {
		return;
	}
	error = pending->resultCode != ResultCode::Ok;
	lastResult = pending->resultMessage;
	pending.reset();
}

void CalibrationControlClient::handleResult(const ofxOscMessage & message) {
	if (message.getNumArgs() != resultArgumentCount
		|| !hasType(message, 0, OFXOSC_TYPE_INT32)
		|| !hasType(message, 1, OFXOSC_TYPE_INT32)
		|| !hasType(message, 2, OFXOSC_TYPE_INT32)
		|| !hasType(message, 3, OFXOSC_TYPE_INT32)
		|| !hasType(message, 4, OFXOSC_TYPE_INT32)
		|| !hasType(message, 5, OFXOSC_TYPE_STRING)
		|| message.getArgAsInt32(0) != version) {
		malformedMessages++;
		return;
	}
	const int32_t incomingSession = message.getArgAsInt32(1);
	const int32_t requestId = message.getArgAsInt32(2);
	const int32_t incomingRevision = message.getArgAsInt32(3);
	const int32_t codeValue = message.getArgAsInt32(4);
	if (incomingSession <= 0 || requestId <= 0 || incomingRevision < 0 || codeValue < 0
		|| codeValue > static_cast<int32_t>(ResultCode::PersistenceError)
		|| isRetiredSession(incomingSession)) {
		malformedMessages++;
		return;
	}
	selectSession(incomingSession);
	if (!pending.has_value() || pending->request.requestId != requestId) {
		return;
	}
	pending->resultReceived = true;
	pending->resultCode = static_cast<ResultCode>(codeValue);
	pending->resultMessage = message.getArgAsString(5);
	finishPendingIfComplete();
}

void CalibrationControlClient::update() {
	if (!enabled) return;
	const float now = ofGetElapsedTimef();
	while (receiver.hasWaitingMessages()) {
		ofxOscMessage message;
		receiver.getNextMessage(message);
		if (!expectedServerIp.empty() && message.getRemoteHost() != expectedServerIp) {
			rejectedSourceMessages++;
			if (now - lastSourceWarningTime >= 2.0f) {
				ofLogWarning("CalibrationControlClient") << "dropping calibration response from "
					<< message.getRemoteHost() << "; expected " << expectedServerIp;
				lastSourceWarningTime = now;
			}
			continue;
		}
		try {
			if (message.getAddress() == stateAddress) {
				handleState(message, now);
			} else if (message.getAddress() == resultAddress) {
				handleResult(message);
			}
		} catch (...) {
			malformedMessages++;
		}
	}

	if (pending.has_value()
		&& now - pending->lastSentAt >= requestRetryIntervalSeconds) {
		if (pending->sendCount < maximumRequestSendCount) {
			sendPending();
		} else {
			error = true;
			lastResult = "calibration request timed out";
			pending.reset();
		}
	}
}

float CalibrationControlClient::getStateAgeSeconds() const {
	return lastStateTime < 0.0f ? -1.0f : ofGetElapsedTimef() - lastStateTime;
}

std::string CalibrationControlClient::getStatusLine() const {
	std::ostringstream status;
	status.setf(std::ios::fixed);
	status.precision(2);
	const float age = getStateAgeSeconds();
	status << "control " << ((age >= 0.0f && age <= disconnectedAfterSeconds)
		? "connected" : "unavailable")
		<< " session=" << sessionId << " revision=" << revision
		<< " age=" << age << "s"
		<< " " << (error ? "error"
			: (pending.has_value() ? "pending" : (dirty ? "changed" : "saved")))
		<< " malformed=" << malformedMessages << " sourceDrop=" << rejectedSourceMessages;
	return status.str();
}
