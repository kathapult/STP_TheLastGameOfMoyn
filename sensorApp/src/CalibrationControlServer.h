#pragma once

#include "CalibrationProtocol.h"
#include "ObjectDetector.h"
#include "ofxOsc.h"
#include <cstdint>
#include <deque>
#include <string>

// Calibration-only command endpoint on Pi A. It is never set up by the
// production profile, validates Pi B's source address, and remains independent
// of Kinect frame availability.
class CalibrationControlServer {
public:
	void setup(const std::string & clientHost, int listenPort, int responsePort);
	void update(ObjectDetector & detector);

private:
	struct CachedResponse {
		CalibrationProtocol::Request request;
		CalibrationProtocol::ResultCode code = CalibrationProtocol::ResultCode::Ok;
		std::string message;
		int32_t revision = 0;
		bool dirty = false;
		CalibrationProtocol::SettingsState state;
	};

	bool parseRequest(const ofxOscMessage & message,
		CalibrationProtocol::Request & request) const;
	void processRequest(const CalibrationProtocol::Request & request,
		ObjectDetector & detector);
	void sendResponse(const CachedResponse & response,
		const ObjectDetector & detector);
	void sendResult(int32_t requestId, CalibrationProtocol::ResultCode code,
		const std::string & message, int32_t responseRevision);
	void sendState(int32_t requestId, int32_t responseRevision, bool responseDirty,
		const CalibrationProtocol::SettingsState & state,
		const CalibrationProtocol::TableMaskState & tableMask);
	const CachedResponse * findCached(int32_t requestId) const;
	void cache(const CachedResponse & response);

	ofxOscReceiver receiver;
	ofxOscSender sender;
	std::string expectedClientIp;
	bool enabled = false;
	int32_t sessionId = 0;
	int32_t revision = 0;
	bool dirty = false;
	float lastStateBroadcastTime = -100.0f;
	float lastSourceWarningTime = -100.0f;
	uint64_t rejectedSourceMessages = 0;
	std::deque<CachedResponse> responses;
	static constexpr size_t maximumCachedResponses = 64;
	static constexpr float stateBroadcastIntervalSeconds = 1.0f;
};
