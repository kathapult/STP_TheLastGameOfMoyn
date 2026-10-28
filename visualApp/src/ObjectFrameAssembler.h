#pragma once

#include "ObjectStreamProtocol.h"
#include <cstdint>
#include <deque>
#include <optional>
#include <unordered_map>
#include <vector>

// Reassembles independently delivered OSC messages into authoritative object
// snapshots. It deliberately knows nothing about sockets or openFrameworks.
class ObjectFrameAssembler {
public:
	struct CompleteFrame {
		ObjectStreamProtocol::FrameIdentity identity;
		std::vector<DetectedObject> objects;
	};

	struct Counters {
		uint64_t completedFrames = 0;
		uint64_t missingSequences = 0;
		uint64_t incompleteFrames = 0;
		uint64_t lateFrames = 0;
		uint64_t duplicateMessages = 0;
		uint64_t malformedMessages = 0;
	};

	std::optional<CompleteFrame> addHeader(
		const ObjectStreamProtocol::FrameHeader & header, double receivedAtSeconds);
	std::optional<CompleteFrame> addObject(
		const ObjectStreamProtocol::FramedObject & object, double receivedAtSeconds);
	void expire(double nowSeconds);

	int32_t getActiveStreamId() const { return activeStreamId; }
	uint64_t getStreamGeneration() const { return streamGeneration; }
	int32_t getLastCommittedSequence() const { return lastCommittedSequence; }
	const Counters & getCounters() const { return counters; }

private:
	struct PendingFrame {
		ObjectStreamProtocol::FrameIdentity identity;
		int32_t totalObjectCount = 0;
		int32_t partCount = 0;
		double firstReceivedAt = 0.0;
		std::vector<bool> partHeaders;
		std::vector<bool> objectSlots;
		std::vector<DetectedObject> objects;
		int32_t receivedObjectCount = 0;
	};

	bool selectStream(int32_t streamId);
	bool isRetiredStream(int32_t streamId) const;
	void retireActiveStream();
	void limitPendingFrames();
	std::optional<CompleteFrame> completeIfReady(int32_t sequence);

	std::unordered_map<int32_t, PendingFrame> pendingFrames;
	std::deque<int32_t> retiredStreamIds;
	int32_t activeStreamId = 0;
	int32_t lastCommittedSequence = -1;
	uint64_t streamGeneration = 0;
	Counters counters;

	static constexpr double pendingFrameTimeoutSeconds = 0.15;
	static constexpr size_t maximumPendingFrames = 4;
	static constexpr size_t maximumRetiredStreams = 4;
};
