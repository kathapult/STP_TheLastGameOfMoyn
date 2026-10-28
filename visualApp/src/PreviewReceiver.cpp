#include "PreviewReceiver.h"
#include "ObjectStreamProtocol.h"
#include <algorithm>
#include <cstring>
#include <exception>
#include <sstream>
#include <unordered_set>

using namespace ObjectStreamProtocol;

namespace {
constexpr int maximumPreviewDimension = 1024;
constexpr float sourceWarningIntervalSeconds = 2.0f;

bool hasType(const ofxOscMessage & message, int index, ofxOscArgType type) {
	return index >= 0 && index < message.getNumArgs()
		&& message.getArgType(index) == type;
}
}

void PreviewReceiver::setup(int listenPort, const std::string & sourceIp, float timeoutSeconds) {
	expectedSourceIp = sourceIp;
	freshnessSeconds = timeoutSeconds;
	receiver.setup(listenPort);
	ofLogNotice("PreviewReceiver") << "listening for preview frames on UDP " << listenPort
		<< " from " << expectedSourceIp << " with " << freshnessSeconds << "s timeout";
}

bool PreviewReceiver::acceptsSequence(int32_t streamId, int32_t sequence) const {
	if (streamId <= 0 || sequence < 0 || isRetiredStream(streamId)) {
		return false;
	}
	if (stagingStreamId == streamId && stagingSequence >= 0) {
		return sequence == stagingSequence || isSequenceNewer(sequence, stagingSequence);
	}
	if (completedStreamId == streamId && completedSequence >= 0) {
		return isSequenceNewer(sequence, completedSequence);
	}
	return true;
}

bool PreviewReceiver::isRetiredStream(int32_t streamId) const {
	return std::find(retiredStreamIds.begin(), retiredStreamIds.end(), streamId)
		!= retiredStreamIds.end();
}

void PreviewReceiver::retireStream(int32_t streamId) {
	if (streamId <= 0 || isRetiredStream(streamId)) {
		return;
	}
	retiredStreamIds.push_back(streamId);
	while (retiredStreamIds.size() > maximumRetiredStreams) {
		retiredStreamIds.pop_front();
	}
}

void PreviewReceiver::discardStagingFrame() {
	stagingPixels.clear();
	stagingStrips.clear();
	stagingStripRows.clear();
	stagingStripHeights.clear();
	stagingRows.clear();
	stagingStreamId = 0;
	stagingSequence = -1;
	stagingStripCount = 0;
	receivedStripCount = 0;
	lastStagingStripTime = -1.0f;
}

void PreviewReceiver::startFrame(int width, int height, int32_t streamId,
	int32_t sequence, int stripCount, float now) {
	if (stagingSequence >= 0) {
		incompleteFrames++;
	}
	if (completedStreamId != streamId) {
		retireStream(completedStreamId);
	}
	if (stagingStreamId != streamId) {
		retireStream(stagingStreamId);
	}

	stagingPixels.allocate(width, height, 1);
	stagingPixels.set(0);
	stagingStrips.assign(static_cast<size_t>(stripCount), false);
	stagingStripRows.assign(static_cast<size_t>(stripCount), -1);
	stagingStripHeights.assign(static_cast<size_t>(stripCount), 0);
	stagingRows.assign(static_cast<size_t>(height), false);
	stagingStreamId = streamId;
	stagingSequence = sequence;
	stagingStripCount = stripCount;
	receivedStripCount = 0;
	lastStagingStripTime = now;
}

void PreviewReceiver::publishStagingFrame(float now) {
	if (!image.isAllocated()
		|| static_cast<int>(image.getWidth()) != static_cast<int>(stagingPixels.getWidth())
		|| static_cast<int>(image.getHeight()) != static_cast<int>(stagingPixels.getHeight())) {
		image.allocate(stagingPixels.getWidth(), stagingPixels.getHeight(), OF_IMAGE_GRAYSCALE);
	}
	image.getPixels() = stagingPixels;
	image.update();
	completedStreamId = stagingStreamId;
	completedSequence = stagingSequence;
	lastCompleteFrameTime = now;
	completedFrames++;
	discardStagingFrame();
}

void PreviewReceiver::discardDepthStaging() {
	depthStagingPixels.clear();
	depthStagingStrips.clear();
	depthStagingStripRows.clear();
	depthStagingStripHeights.clear();
	depthStagingRows.clear();
	depthStagingStreamId = 0;
	depthStagingSequence = -1;
	depthStagingStripCount = 0;
	depthReceivedStripCount = 0;
	lastDepthStagingTime = -1.0f;
}

void PreviewReceiver::publishDepthStaging(float now) {
	if (!depthImage.isAllocated()
		|| static_cast<int>(depthImage.getWidth()) != static_cast<int>(depthStagingPixels.getWidth())
		|| static_cast<int>(depthImage.getHeight()) != static_cast<int>(depthStagingPixels.getHeight())) {
		depthImage.allocate(depthStagingPixels.getWidth(), depthStagingPixels.getHeight(),
			OF_IMAGE_GRAYSCALE);
	}
	depthImage.getPixels() = depthStagingPixels;
	depthImage.update();
	completedDepthStreamId = depthStagingStreamId;
	completedDepthSequence = depthStagingSequence;
	lastCompleteDepthTime = now;
	completedDepthFrames++;
	discardDepthStaging();
}

void PreviewReceiver::processDepthStrip(const ofxOscMessage & message, float now) {
	if (message.getNumArgs() != framedPreviewArgumentCount
		|| !hasType(message, 0, OFXOSC_TYPE_INT32)
		|| !hasType(message, 1, OFXOSC_TYPE_INT32)
		|| !hasType(message, 2, OFXOSC_TYPE_INT32)
		|| !hasType(message, 3, OFXOSC_TYPE_INT32)
		|| !hasType(message, 4, OFXOSC_TYPE_BLOB)) {
		malformedMessages++;
		return;
	}
	for (int index = 5; index < framedPreviewArgumentCount; ++index) {
		if (!hasType(message, index, OFXOSC_TYPE_INT32)) {
			malformedMessages++;
			return;
		}
	}
	const int width = message.getArgAsInt32(0);
	const int height = message.getArgAsInt32(1);
	const int row = message.getArgAsInt32(2);
	const int rows = message.getArgAsInt32(3);
	const ofBuffer strip = message.getArgAsBlob(4);
	const int32_t streamId = message.getArgAsInt32(5);
	const int32_t sequence = message.getArgAsInt32(6);
	const int stripIndex = message.getArgAsInt32(7);
	const int stripCount = message.getArgAsInt32(8);
	const bool retired = std::find(retiredDepthStreamIds.begin(),
		retiredDepthStreamIds.end(), streamId) != retiredDepthStreamIds.end();
	if (width <= 0 || height <= 0 || width > maximumPreviewDimension
		|| height > maximumPreviewDimension || row < 0 || row >= height
		|| rows <= 0 || rows > height - row
		|| strip.size() != static_cast<size_t>(width) * static_cast<size_t>(rows)
		|| streamId <= 0 || sequence < 0 || stripCount <= 0 || stripCount > height
		|| stripIndex < 0 || stripIndex >= stripCount || retired) {
		malformedMessages++;
		return;
	}

	const bool sameFrame = depthStagingStreamId == streamId
		&& depthStagingSequence == sequence;
	if (!sameFrame) {
		if (depthStagingStreamId == streamId && depthStagingSequence >= 0
			&& !isSequenceNewer(sequence, depthStagingSequence)) return;
		if (completedDepthStreamId == streamId && completedDepthSequence >= 0
			&& !isSequenceNewer(sequence, completedDepthSequence)) return;
		if (depthStagingSequence >= 0) incompleteDepthFrames++;
		if (completedDepthStreamId > 0 && completedDepthStreamId != streamId) {
			retiredDepthStreamIds.push_back(completedDepthStreamId);
		}
		if (depthStagingStreamId > 0 && depthStagingStreamId != streamId) {
			retiredDepthStreamIds.push_back(depthStagingStreamId);
		}
		while (retiredDepthStreamIds.size() > maximumRetiredStreams) {
			retiredDepthStreamIds.pop_front();
		}
		discardDepthStaging();
		depthStagingPixels.allocate(width, height, 1);
		depthStagingPixels.set(0);
		depthStagingStrips.assign(static_cast<size_t>(stripCount), false);
		depthStagingStripRows.assign(static_cast<size_t>(stripCount), -1);
		depthStagingStripHeights.assign(static_cast<size_t>(stripCount), 0);
		depthStagingRows.assign(static_cast<size_t>(height), false);
		depthStagingStreamId = streamId;
		depthStagingSequence = sequence;
		depthStagingStripCount = stripCount;
	} else if (static_cast<int>(depthStagingPixels.getWidth()) != width
		|| static_cast<int>(depthStagingPixels.getHeight()) != height
		|| depthStagingStripCount != stripCount) {
		incompleteDepthFrames++;
		discardDepthStaging();
		return;
	}

	const size_t slot = static_cast<size_t>(stripIndex);
	if (depthStagingStrips[slot]) {
		const bool sameRange = depthStagingStripRows[slot] == row
			&& depthStagingStripHeights[slot] == rows;
		const unsigned char * existing = sameRange
			? depthStagingPixels.getData() + static_cast<size_t>(row) * width : nullptr;
		if (!sameRange || std::memcmp(existing, strip.getData(), strip.size()) != 0) {
			incompleteDepthFrames++;
			discardDepthStaging();
		}
		return;
	}
	for (int currentRow = row; currentRow < row + rows; ++currentRow) {
		if (depthStagingRows[static_cast<size_t>(currentRow)]) {
			incompleteDepthFrames++;
			discardDepthStaging();
			return;
		}
	}
	std::memcpy(depthStagingPixels.getData() + static_cast<size_t>(row) * width,
		strip.getData(), strip.size());
	depthStagingStrips[slot] = true;
	depthStagingStripRows[slot] = row;
	depthStagingStripHeights[slot] = rows;
	for (int currentRow = row; currentRow < row + rows; ++currentRow) {
		depthStagingRows[static_cast<size_t>(currentRow)] = true;
	}
	depthReceivedStripCount++;
	lastDepthStagingTime = now;
	if (depthReceivedStripCount == depthStagingStripCount
		&& std::all_of(depthStagingRows.begin(), depthStagingRows.end(),
			[](bool received) { return received; })) {
		publishDepthStaging(now);
	}
}

void PreviewReceiver::discardTrackStaging(bool countIncomplete) {
	if (countIncomplete && trackStagingSequence >= 0) incompleteTrackFrames++;
	trackStagingStreamId = 0;
	trackStagingSequence = -1;
	trackTotalCount = 0;
	trackPartCount = 0;
	trackReceivedCount = 0;
	trackFirstReceivedAt = -1.0f;
	trackPartHeaders.clear();
	trackSlots.clear();
	stagingTracks.clear();
}

void PreviewReceiver::processTrackHeader(const ofxOscMessage & message, float now) {
	if (message.getNumArgs() != 7) { malformedMessages++; return; }
	for (int index = 0; index < 7; ++index) {
		if (!hasType(message, index, OFXOSC_TYPE_INT32)) { malformedMessages++; return; }
	}
	const int32_t streamId = message.getArgAsInt32(0);
	const int32_t sequence = message.getArgAsInt32(1);
	const int total = message.getArgAsInt32(2);
	const int partIndex = message.getArgAsInt32(3);
	const int partCount = message.getArgAsInt32(4);
	const int first = message.getArgAsInt32(5);
	const int count = message.getArgAsInt32(6);
	const int expectedParts = std::max(1, (total
		+ CalibrationProtocol::maximumTracksPerDatagram - 1)
		/ CalibrationProtocol::maximumTracksPerDatagram);
	const int expectedFirst = partIndex * CalibrationProtocol::maximumTracksPerDatagram;
	const int expectedCount = std::min(CalibrationProtocol::maximumTracksPerDatagram,
		std::max(0, total - expectedFirst));
	const bool retired = std::find(retiredTrackStreamIds.begin(),
		retiredTrackStreamIds.end(), streamId) != retiredTrackStreamIds.end();
	if (streamId <= 0 || sequence < 0 || total < 0
		|| total > CalibrationProtocol::maximumTrackCount || partCount != expectedParts
		|| partIndex < 0 || partIndex >= partCount || first != expectedFirst
		|| count != expectedCount || retired) {
		malformedMessages++;
		return;
	}

	const bool sameFrame = trackStagingStreamId == streamId
		&& trackStagingSequence == sequence;
	if (!sameFrame) {
		if (trackStagingStreamId == streamId && trackStagingSequence >= 0
			&& !isSequenceNewer(sequence, trackStagingSequence)) return;
		if (completedTrackStreamId == streamId && completedTrackSequence >= 0
			&& !isSequenceNewer(sequence, completedTrackSequence)) return;
		if (completedTrackStreamId > 0 && completedTrackStreamId != streamId) {
			retiredTrackStreamIds.push_back(completedTrackStreamId);
		}
		if (trackStagingStreamId > 0 && trackStagingStreamId != streamId) {
			retiredTrackStreamIds.push_back(trackStagingStreamId);
		}
		while (retiredTrackStreamIds.size() > maximumRetiredStreams) {
			retiredTrackStreamIds.pop_front();
		}
		discardTrackStaging(true);
		trackStagingStreamId = streamId;
		trackStagingSequence = sequence;
		trackTotalCount = total;
		trackPartCount = partCount;
		trackFirstReceivedAt = now;
		trackPartHeaders.assign(static_cast<size_t>(partCount), false);
		trackSlots.assign(static_cast<size_t>(total), false);
		stagingTracks.resize(static_cast<size_t>(total));
	} else if (trackTotalCount != total || trackPartCount != partCount) {
		malformedMessages++;
		discardTrackStaging(true);
		return;
	}
	trackPartHeaders[static_cast<size_t>(partIndex)] = true;
	publishTracksIfComplete(now);
}

void PreviewReceiver::processTrack(const ofxOscMessage & message, float now) {
	if (message.getNumArgs() != 16) { malformedMessages++; return; }
	for (int index = 0; index < 5; ++index) {
		if (!hasType(message, index, OFXOSC_TYPE_INT32)) { malformedMessages++; return; }
	}
	for (int index = 5; index < 15; ++index) {
		if (!hasType(message, index, OFXOSC_TYPE_FLOAT)) { malformedMessages++; return; }
	}
	if (!hasType(message, 15, OFXOSC_TYPE_INT32)) { malformedMessages++; return; }
	const int32_t streamId = message.getArgAsInt32(0);
	const int32_t sequence = message.getArgAsInt32(1);
	const int index = message.getArgAsInt32(2);
	if (streamId != trackStagingStreamId || sequence != trackStagingSequence
		|| index < 0 || index >= trackTotalCount) {
		malformedMessages++;
		return;
	}
	CalibrationProtocol::TrackDiagnostic track;
	track.id = message.getArgAsInt32(3);
	track.type = message.getArgAsInt32(4);
	track.rawX = message.getArgAsFloat(5);
	track.rawY = message.getArgAsFloat(6);
	track.smoothedX = message.getArgAsFloat(7);
	track.smoothedY = message.getArgAsFloat(8);
	track.vx = message.getArgAsFloat(9);
	track.vy = message.getArgAsFloat(10);
	track.palmRadiusMM = message.getArgAsFloat(11);
	track.solidity = message.getArgAsFloat(12);
	track.borderContact = message.getArgAsFloat(13);
	track.stillForSeconds = message.getArgAsFloat(14);
	const int hidden = message.getArgAsInt32(15);
	track.hidden = hidden == 1;
	const bool finite = std::isfinite(track.rawX) && std::isfinite(track.rawY)
		&& std::isfinite(track.smoothedX) && std::isfinite(track.smoothedY)
		&& std::isfinite(track.vx) && std::isfinite(track.vy)
		&& std::isfinite(track.palmRadiusMM) && std::isfinite(track.solidity)
		&& std::isfinite(track.borderContact) && std::isfinite(track.stillForSeconds);
	if (!finite || track.id < 0 || track.type < 0 || track.rawX < 0.0f
		|| track.rawX > 1.0f || track.rawY < 0.0f || track.rawY > 1.0f
		|| track.smoothedX < 0.0f || track.smoothedX > 1.0f
		|| track.smoothedY < 0.0f || track.smoothedY > 1.0f
		|| track.palmRadiusMM < 0.0f || track.solidity < 0.0f || track.solidity > 1.0f
		|| track.borderContact < 0.0f || track.borderContact > 1.0f
		|| track.stillForSeconds < 0.0f || (hidden != 0 && hidden != 1)) {
		malformedMessages++;
		discardTrackStaging(true);
		return;
	}
	const size_t slot = static_cast<size_t>(index);
	if (trackSlots[slot]) {
		if (!CalibrationProtocol::sameTrackDiagnostic(stagingTracks[slot], track)) {
			malformedMessages++;
			discardTrackStaging(true);
		}
		return;
	}
	stagingTracks[slot] = track;
	trackSlots[slot] = true;
	trackReceivedCount++;
	publishTracksIfComplete(now);
}

void PreviewReceiver::publishTracksIfComplete(float now) {
	if (trackStagingSequence < 0 || trackReceivedCount != trackTotalCount
		|| !std::all_of(trackPartHeaders.begin(), trackPartHeaders.end(),
			[](bool received) { return received; })) return;
	std::unordered_set<int32_t> ids;
	for (const CalibrationProtocol::TrackDiagnostic & track : stagingTracks) {
		if (!ids.insert(track.id).second) {
			malformedMessages++;
			discardTrackStaging(true);
			return;
		}
	}
	publishedTracks = stagingTracks;
	completedTrackStreamId = trackStagingStreamId;
	completedTrackSequence = trackStagingSequence;
	lastCompleteTrackTime = now;
	completedTrackFrames++;
	discardTrackStaging(false);
}

void PreviewReceiver::update() {
	const float now = ofGetElapsedTimef();

	while (receiver.hasWaitingMessages()) {
		ofxOscMessage message;
		receiver.getNextMessage(message);

		if (!expectedSourceIp.empty() && message.getRemoteHost() != expectedSourceIp) {
			rejectedSourceMessages++;
			if (now - lastSourceWarningTime >= sourceWarningIntervalSeconds) {
				ofLogWarning("PreviewReceiver") << "dropping preview from unexpected source "
					<< message.getRemoteHost() << "; expected " << expectedSourceIp;
				lastSourceWarningTime = now;
			}
			continue;
		}
		if (message.getAddress() == CalibrationProtocol::depthPreviewAddress
			|| message.getAddress() == CalibrationProtocol::trackFrameAddress
			|| message.getAddress() == CalibrationProtocol::trackAddress) {
			try {
				if (message.getAddress() == CalibrationProtocol::depthPreviewAddress) {
					processDepthStrip(message, now);
				} else if (message.getAddress() == CalibrationProtocol::trackFrameAddress) {
					processTrackHeader(message, now);
				} else {
					processTrack(message, now);
				}
			} catch (const std::exception & error) {
				malformedMessages++;
				ofLogWarning("PreviewReceiver") << "dropping malformed calibration telemetry: "
					<< error.what();
			} catch (...) {
				malformedMessages++;
			}
			continue;
		}
		if (message.getAddress() != previewAddress
			|| message.getNumArgs() < legacyPreviewArgumentCount) {
			if (message.getAddress() == previewAddress) {
				malformedMessages++;
			}
			continue;
		}

		try {
			const bool legacyTypes = message.getArgType(0) == OFXOSC_TYPE_INT32
				&& message.getArgType(1) == OFXOSC_TYPE_INT32
				&& message.getArgType(2) == OFXOSC_TYPE_INT32
				&& message.getArgType(3) == OFXOSC_TYPE_INT32
				&& message.getArgType(4) == OFXOSC_TYPE_BLOB;
			if (!legacyTypes) {
				malformedMessages++;
				continue;
			}
			const int width = message.getArgAsInt32(0);
			const int height = message.getArgAsInt32(1);
			const int row = message.getArgAsInt32(2);
			const int rows = message.getArgAsInt32(3);
			const ofBuffer strip = message.getArgAsBlob(4);
			const bool saneGeometry = width > 0 && height > 0
				&& width <= maximumPreviewDimension && height <= maximumPreviewDimension
				&& row >= 0 && row < height && rows > 0 && rows <= height - row
				&& strip.size() == static_cast<size_t>(width) * static_cast<size_t>(rows);
			if (!saneGeometry) {
				malformedMessages++;
				ofLogWarning("PreviewReceiver") << "dropping malformed preview strip";
				continue;
			}

			// Legacy preview packets have no frame identity. Keep their historical
			// direct-display behavior only for compatibility with an older Pi A.
			if (message.getNumArgs() < framedPreviewArgumentCount) {
				if (!image.isAllocated() || static_cast<int>(image.getWidth()) != width
					|| static_cast<int>(image.getHeight()) != height) {
					image.allocate(width, height, OF_IMAGE_GRAYSCALE);
					image.getPixels().set(0);
				}
				std::memcpy(image.getPixels().getData() + static_cast<size_t>(row) * width,
					strip.getData(), strip.size());
				image.update();
				completedStreamId = 0;
				completedSequence = -1;
				lastCompleteFrameTime = now;
				continue;
			}
			bool framedTypes = true;
			for (int index = 5; index < framedPreviewArgumentCount; ++index) {
				framedTypes = framedTypes && message.getArgType(index) == OFXOSC_TYPE_INT32;
			}
			if (!framedTypes) {
				malformedMessages++;
				continue;
			}

			const int32_t streamId = message.getArgAsInt32(5);
			const int32_t sequence = message.getArgAsInt32(6);
			const int stripIndex = message.getArgAsInt32(7);
			const int stripCount = message.getArgAsInt32(8);
			if (stripCount <= 0 || stripCount > height || stripIndex < 0
				|| stripIndex >= stripCount || !acceptsSequence(streamId, sequence)) {
				malformedMessages++;
				continue;
			}

			const bool sameFrame = stagingStreamId == streamId
				&& stagingSequence == sequence;
			if (!sameFrame) {
				startFrame(width, height, streamId, sequence, stripCount, now);
			} else if (static_cast<int>(stagingPixels.getWidth()) != width
				|| static_cast<int>(stagingPixels.getHeight()) != height
				|| stagingStripCount != stripCount) {
				incompleteFrames++;
				discardStagingFrame();
				continue;
			}

			const size_t index = static_cast<size_t>(stripIndex);
			if (stagingStrips[index]) {
				const bool sameRange = stagingStripRows[index] == row
					&& stagingStripHeights[index] == rows;
				const unsigned char * existing = sameRange
					? stagingPixels.getData() + static_cast<size_t>(row) * width
					: nullptr;
				if (!sameRange || std::memcmp(existing, strip.getData(), strip.size()) != 0) {
					incompleteFrames++;
					discardStagingFrame();
				}
				continue;
			}

			bool overlapsAnotherStrip = false;
			for (int currentRow = row; currentRow < row + rows; ++currentRow) {
				if (stagingRows[static_cast<size_t>(currentRow)]) {
					overlapsAnotherStrip = true;
					break;
				}
			}
			if (overlapsAnotherStrip) {
				incompleteFrames++;
				discardStagingFrame();
				continue;
			}

			std::memcpy(stagingPixels.getData() + static_cast<size_t>(row) * width,
				strip.getData(), strip.size());
			stagingStrips[index] = true;
			stagingStripRows[index] = row;
			stagingStripHeights[index] = rows;
			for (int currentRow = row; currentRow < row + rows; ++currentRow) {
				stagingRows[static_cast<size_t>(currentRow)] = true;
			}
			receivedStripCount++;
			lastStagingStripTime = now;

			if (receivedStripCount == stagingStripCount
				&& std::all_of(stagingRows.begin(), stagingRows.end(),
					[](bool received) { return received; })) {
				publishStagingFrame(now);
			}
		} catch (const std::exception & error) {
			malformedMessages++;
			ofLogWarning("PreviewReceiver") << "dropping malformed preview strip: "
				<< error.what();
		} catch (...) {
			malformedMessages++;
			ofLogWarning("PreviewReceiver") << "dropping malformed preview strip";
		}
	}

	if (lastStagingStripTime >= 0.0f
		&& now - lastStagingStripTime > frameAssemblyTimeoutSeconds) {
		incompleteFrames++;
		discardStagingFrame();
	}
	if (lastDepthStagingTime >= 0.0f
		&& now - lastDepthStagingTime > frameAssemblyTimeoutSeconds) {
		incompleteDepthFrames++;
		discardDepthStaging();
	}
	if (trackFirstReceivedAt >= 0.0f
		&& now - trackFirstReceivedAt > frameAssemblyTimeoutSeconds) {
		discardTrackStaging(true);
	}
}

bool PreviewReceiver::hasImage() const {
	const float age = getImageAgeSeconds();
	return image.isAllocated() && age >= 0.0f && age <= freshnessSeconds;
}

bool PreviewReceiver::hasDepthImage() const {
	const float age = getDepthAgeSeconds();
	return depthImage.isAllocated() && age >= 0.0f && age <= freshnessSeconds;
}

bool PreviewReceiver::hasTrackDiagnostics() const {
	const float age = getTrackAgeSeconds();
	return age >= 0.0f && age <= freshnessSeconds;
}

float PreviewReceiver::getImageAgeSeconds() const {
	return lastCompleteFrameTime < 0.0f ? -1.0f
		: ofGetElapsedTimef() - lastCompleteFrameTime;
}

float PreviewReceiver::getDepthAgeSeconds() const {
	return lastCompleteDepthTime < 0.0f ? -1.0f
		: ofGetElapsedTimef() - lastCompleteDepthTime;
}

float PreviewReceiver::getTrackAgeSeconds() const {
	return lastCompleteTrackTime < 0.0f ? -1.0f
		: ofGetElapsedTimef() - lastCompleteTrackTime;
}

std::string PreviewReceiver::getStatusLine() const {
	std::ostringstream status;
	status.setf(std::ios::fixed);
	status.precision(2);
	status << "telemetry " << (completedStreamId > 0 ? "v1" : "legacy")
		<< " seq=" << completedSequence << "/" << completedDepthSequence
		<< "/" << completedTrackSequence
		<< " age=" << getImageAgeSeconds() << "/" << getDepthAgeSeconds()
		<< "/" << getTrackAgeSeconds() << "s"
		<< " complete=" << completedFrames << "/" << completedDepthFrames
		<< "/" << completedTrackFrames
		<< " partial=" << incompleteFrames << "/" << incompleteDepthFrames
		<< "/" << incompleteTrackFrames
		<< " malformed=" << malformedMessages
		<< " sourceDrop=" << rejectedSourceMessages;
	return status.str();
}
