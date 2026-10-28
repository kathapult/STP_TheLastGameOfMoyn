#pragma once

#include "ofxOsc.h"
#include "DetectedObject.h"
#include "ObjectFrameAssembler.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Pi B's OSC receiver - replaces KinectInput on this side of the split.
// Populates detectedObjects from Pi A's object stream instead of a local
// sensor read. See contracts/osc-object-stream.md for the wire format.
class KinectOSCInput {
public:
	void setup(int listenPort, const std::string & expectedSourceIp);
	void update();

	const std::vector<DetectedObject> & getDetectedObjects() const { return detectedObjects; }
	std::string getProtocolStatusLine() const;

private:
	// One object as it last actually arrived off the wire, and when - see
	// update()'s comment on why this is kept at all and only for a bounded
	// time.
	struct RememberedObject {
		DetectedObject object;
		float receivedAt = 0.0f;
	};

	ofxOscReceiver receiver;
	ObjectFrameAssembler frameAssembler;
	std::unordered_map<int, RememberedObject> remembered;
	std::vector<DetectedObject> detectedObjects;
	std::string expectedSourceIp;
	uint64_t rejectedSourceMessages = 0;
	uint64_t malformedMessages = 0;
	float lastSourceWarningTime = -100.0f;
	float lastProtocolPacketTime = 0.0f;
	float lastFramedPacketTime = -100.0f;
	bool framedMode = false;

	// How long a remembered object may be dead-reckoned forward from its own
	// last reported velocity before it is dropped rather than extrapolated
	// further - see update(). Well under a second, and well under every
	// visual's own holdSeconds-style "still counts as present" window (all
	// upwards of 0.8s), so this only ever smooths the gap between two real
	// packets and never becomes a second, competing "is it still there" rule
	// of its own.
	static constexpr float maxExtrapolationSeconds = 0.35f;

	// Silence diagnostics only (T022) - logs once when no packet has actually
	// arrived for a while, so a disconnected/misconfigured link is observable
	// on Pi B's console too (Pi A already logs its own status - see
	// sensorApp/src/ofApp.cpp). Tracked against whether a packet was actually
	// received this call, not against getDetectedObjects() - which briefly
	// stays non-empty under extrapolation even mid-silence - so this still
	// fires on genuine silence at the same threshold as before.
	bool loggedSilence = false;
	static constexpr float silenceLogThresholdSeconds = 2.0f;
	static constexpr float legacyFallbackSeconds = 0.75f;
};
