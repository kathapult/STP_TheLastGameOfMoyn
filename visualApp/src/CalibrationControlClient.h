#pragma once

#include "CalibrationProtocol.h"
#include "ofxOsc.h"
#include <cstdint>
#include <deque>
#include <optional>
#include <string>

// Calibration-only Pi B client. Values are published to the UI only after a
// complete state from Pi A passes source, session, revision, type and range
// validation.
class CalibrationControlClient {
public:
	void setup(const std::string & serverHost, int listenPort, int targetPort);
	void update();

	bool requestGet();
	bool requestSet(CalibrationProtocol::Field field, float absoluteValue);
	bool requestSave();
	bool requestReload();
	bool requestResetRoi();
	bool requestCaptureTableMask();
	bool requestClearTableMask();

	bool hasState() const { return stateAvailable; }
	bool hasPendingRequest() const { return pending.has_value(); }
	const CalibrationProtocol::SettingsState & getState() const { return state; }
	const CalibrationProtocol::TableMaskState & getTableMaskState() const {
		return tableMaskState;
	}
	bool supportsHandPositionSettings() const {
		return handPositionSettingsSupported;
	}
	int32_t getSessionId() const { return sessionId; }
	int32_t getRevision() const { return revision; }
	bool isDirty() const { return dirty; }
	float getStateAgeSeconds() const;
	std::string getStatusLine() const;
	const std::string & getLastResult() const { return lastResult; }
	bool hasError() const { return error; }

private:
	struct PendingRequest {
		CalibrationProtocol::Request request;
		float lastSentAt = 0.0f;
		int sendCount = 0;
		bool resultReceived = false;
		bool stateReceived = false;
		CalibrationProtocol::ResultCode resultCode = CalibrationProtocol::ResultCode::Ok;
		std::string resultMessage;
	};

	bool begin(CalibrationProtocol::Command command,
		CalibrationProtocol::Field field = CalibrationProtocol::Field::NearMM,
		float value = 0.0f);
	void sendPending();
	void handleState(const ofxOscMessage & message, float now);
	void handleResult(const ofxOscMessage & message);
	bool parseState(const ofxOscMessage & message, int32_t & incomingSession,
		int32_t & incomingRevision, int32_t & requestId, bool & incomingDirty,
		CalibrationProtocol::SettingsState & incoming,
		CalibrationProtocol::TableMaskState & incomingTableMask,
		bool & incomingHandPositionSupport) const;
	void selectSession(int32_t incomingSession);
	void finishPendingIfComplete();
	bool isRetiredSession(int32_t candidate) const;

	ofxOscReceiver receiver;
	ofxOscSender sender;
	std::string expectedServerIp;
	bool enabled = false;
	bool stateAvailable = false;
	bool dirty = false;
	bool error = false;
	bool handPositionSettingsSupported = false;
	int32_t sessionId = 0;
	int32_t revision = -1;
	int32_t nextId = 1;
	float lastStateTime = -1.0f;
	float lastSourceWarningTime = -100.0f;
	uint64_t rejectedSourceMessages = 0;
	uint64_t malformedMessages = 0;
	CalibrationProtocol::SettingsState state;
	CalibrationProtocol::TableMaskState tableMaskState;
	std::optional<PendingRequest> pending;
	std::deque<int32_t> retiredSessions;
	std::string lastResult = "waiting for calibration state";
	static constexpr size_t maximumRetiredSessions = 4;
	static constexpr float disconnectedAfterSeconds = 2.0f;
};
