#pragma once

#include "ofMain.h"
#include "ofxOsc.h"
#include "CalibrationProtocol.h"
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

// Receives Pi A's downscaled detection mask and reassembles it into an image.
// Development aid only: it exists so the sensor's view can be checked while
// Pi A runs headless, and is drawn as a corner overlay by ofApp rather than by
// any visual.
//
// This does not cross the sensor/visual boundary (constitution Principle II).
// That principle keeps *visuals* free of sensor internals; this is a
// diagnostic overlay in ofApp, like the existing object HUD, and no VisualBase
// subclass can see it. What arrives is a picture, not detection data - nothing
// here feeds the render pipeline.
class PreviewReceiver {
public:
	void setup(int listenPort, const std::string & expectedSourceIp, float freshnessSeconds);

	// Drains queued strips and refreshes the image. Non-blocking, like
	// KinectOSCInput's own update.
	void update();

	// A disconnected preview sender must not leave an old mask presented as a
	// live calibration image.
	bool hasImage() const;
	bool hasDepthImage() const;
	bool hasTrackDiagnostics() const;
	float getImageAgeSeconds() const;
	float getDepthAgeSeconds() const;
	float getTrackAgeSeconds() const;
	std::string getStatusLine() const;
	float getWidth() const { return image.getWidth(); }
	float getHeight() const { return image.getHeight(); }

	void draw(float x, float y, float width, float height) const { image.draw(x, y, width, height); }
	void drawDepth(float x, float y, float width, float height) const {
		depthImage.draw(x, y, width, height);
	}
	const std::vector<CalibrationProtocol::TrackDiagnostic> & getTrackDiagnostics() const {
		return publishedTracks;
	}

private:
	void startFrame(int width, int height, int32_t streamId, int32_t sequence,
		int stripCount, float now);
	void discardStagingFrame();
	void publishStagingFrame(float now);
	bool acceptsSequence(int32_t streamId, int32_t sequence) const;
	bool isRetiredStream(int32_t streamId) const;
	void retireStream(int32_t streamId);
	void processDepthStrip(const ofxOscMessage & message, float now);
	void discardDepthStaging();
	void publishDepthStaging(float now);
	void processTrackHeader(const ofxOscMessage & message, float now);
	void processTrack(const ofxOscMessage & message, float now);
	void publishTracksIfComplete(float now);
	void discardTrackStaging(bool countIncomplete);

	ofxOscReceiver receiver;
	std::string expectedSourceIp;
	float freshnessSeconds = 1.0f;
	float lastSourceWarningTime = -100.0f;
	uint64_t rejectedSourceMessages = 0;
	uint64_t malformedMessages = 0;
	uint64_t incompleteFrames = 0;
	uint64_t completedFrames = 0;

	ofImage image;
	ofPixels stagingPixels;
	std::vector<bool> stagingStrips;
	std::vector<int> stagingStripRows;
	std::vector<int> stagingStripHeights;
	std::vector<bool> stagingRows;
	int32_t stagingStreamId = 0;
	int32_t stagingSequence = -1;
	int stagingStripCount = 0;
	int receivedStripCount = 0;
	float lastStagingStripTime = -1.0f;
	int32_t completedStreamId = 0;
	int32_t completedSequence = -1;
	std::deque<int32_t> retiredStreamIds;
	float lastCompleteFrameTime = -1.0f;

	// Depth uses the same atomic strip rules as the mask, but has independent
	// staging so interleaved packets cannot overwrite one another.
	ofImage depthImage;
	ofPixels depthStagingPixels;
	std::vector<bool> depthStagingStrips;
	std::vector<int> depthStagingStripRows;
	std::vector<int> depthStagingStripHeights;
	std::vector<bool> depthStagingRows;
	int32_t depthStagingStreamId = 0;
	int32_t depthStagingSequence = -1;
	int depthStagingStripCount = 0;
	int depthReceivedStripCount = 0;
	float lastDepthStagingTime = -1.0f;
	float lastCompleteDepthTime = -1.0f;
	int32_t completedDepthStreamId = 0;
	int32_t completedDepthSequence = -1;
	std::deque<int32_t> retiredDepthStreamIds;
	uint64_t incompleteDepthFrames = 0;
	uint64_t completedDepthFrames = 0;

	int32_t trackStagingStreamId = 0;
	int32_t trackStagingSequence = -1;
	int trackTotalCount = 0;
	int trackPartCount = 0;
	int trackReceivedCount = 0;
	float trackFirstReceivedAt = -1.0f;
	std::vector<bool> trackPartHeaders;
	std::vector<bool> trackSlots;
	std::vector<CalibrationProtocol::TrackDiagnostic> stagingTracks;
	std::vector<CalibrationProtocol::TrackDiagnostic> publishedTracks;
	int32_t completedTrackStreamId = 0;
	int32_t completedTrackSequence = -1;
	float lastCompleteTrackTime = -1.0f;
	uint64_t incompleteTrackFrames = 0;
	uint64_t completedTrackFrames = 0;
	std::deque<int32_t> retiredTrackStreamIds;
	static constexpr float frameAssemblyTimeoutSeconds = 0.5f;
	static constexpr size_t maximumRetiredStreams = 4;
};
