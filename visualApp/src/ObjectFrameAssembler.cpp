#include "ObjectFrameAssembler.h"
#include <algorithm>
#include <unordered_set>

using namespace ObjectStreamProtocol;

bool ObjectFrameAssembler::isRetiredStream(int32_t streamId) const {
	return std::find(retiredStreamIds.begin(), retiredStreamIds.end(), streamId)
		!= retiredStreamIds.end();
}

void ObjectFrameAssembler::retireActiveStream() {
	if (activeStreamId <= 0) {
		return;
	}

	retiredStreamIds.push_back(activeStreamId);
	while (retiredStreamIds.size() > maximumRetiredStreams) {
		retiredStreamIds.pop_front();
	}
}

bool ObjectFrameAssembler::selectStream(int32_t streamId) {
	if (streamId <= 0 || isRetiredStream(streamId)) {
		counters.lateFrames++;
		return false;
	}
	if (activeStreamId == streamId) {
		return true;
	}

	retireActiveStream();
	activeStreamId = streamId;
	lastCommittedSequence = -1;
	pendingFrames.clear();
	streamGeneration++;
	return true;
}

void ObjectFrameAssembler::limitPendingFrames() {
	while (pendingFrames.size() >= maximumPendingFrames) {
		auto oldest = std::min_element(pendingFrames.begin(), pendingFrames.end(),
			[](const auto & left, const auto & right) {
				return left.second.firstReceivedAt < right.second.firstReceivedAt;
			});
		if (oldest == pendingFrames.end()) {
			return;
		}
		pendingFrames.erase(oldest);
		counters.incompleteFrames++;
	}
}

std::optional<ObjectFrameAssembler::CompleteFrame> ObjectFrameAssembler::addHeader(
	const FrameHeader & header, double receivedAtSeconds) {
	if (!isValidHeader(header)) {
		counters.malformedMessages++;
		return std::nullopt;
	}
	if (!selectStream(header.identity.streamId)) {
		return std::nullopt;
	}
	if (lastCommittedSequence >= 0
		&& !isSequenceNewer(header.identity.sequence, lastCommittedSequence)) {
		counters.lateFrames++;
		return std::nullopt;
	}

	auto pendingIt = pendingFrames.find(header.identity.sequence);
	if (pendingIt == pendingFrames.end()) {
		limitPendingFrames();
		PendingFrame pending;
		pending.identity = header.identity;
		pending.totalObjectCount = header.totalObjectCount;
		pending.partCount = header.partCount;
		pending.firstReceivedAt = receivedAtSeconds;
		pending.partHeaders.assign(static_cast<size_t>(header.partCount), false);
		pending.objectSlots.assign(static_cast<size_t>(header.totalObjectCount), false);
		pending.objects.resize(static_cast<size_t>(header.totalObjectCount));
		pendingIt = pendingFrames.emplace(header.identity.sequence, std::move(pending)).first;
	}

	PendingFrame & pending = pendingIt->second;
	if (pending.identity.streamId != header.identity.streamId
		|| pending.identity.senderTimeUs != header.identity.senderTimeUs
		|| pending.totalObjectCount != header.totalObjectCount
		|| pending.partCount != header.partCount) {
		pendingFrames.erase(pendingIt);
		counters.malformedMessages++;
		return std::nullopt;
	}

	const size_t part = static_cast<size_t>(header.partIndex);
	if (pending.partHeaders[part]) {
		counters.duplicateMessages++;
	} else {
		pending.partHeaders[part] = true;
	}

	return completeIfReady(header.identity.sequence);
}

std::optional<ObjectFrameAssembler::CompleteFrame> ObjectFrameAssembler::addObject(
	const FramedObject & framed, double receivedAtSeconds) {
	(void)receivedAtSeconds;
	if (!isSaneObject(framed) || framed.streamId != activeStreamId) {
		counters.malformedMessages++;
		return std::nullopt;
	}
	if (lastCommittedSequence >= 0
		&& !isSequenceNewer(framed.sequence, lastCommittedSequence)) {
		counters.lateFrames++;
		return std::nullopt;
	}

	auto pendingIt = pendingFrames.find(framed.sequence);
	if (pendingIt == pendingFrames.end()) {
		counters.malformedMessages++;
		return std::nullopt;
	}

	PendingFrame & pending = pendingIt->second;
	if (framed.streamId != pending.identity.streamId
		|| framed.objectIndex >= pending.totalObjectCount) {
		pendingFrames.erase(pendingIt);
		counters.malformedMessages++;
		return std::nullopt;
	}

	const size_t index = static_cast<size_t>(framed.objectIndex);
	if (pending.objectSlots[index]) {
		if (!sameObject(pending.objects[index], framed.object)) {
			pendingFrames.erase(pendingIt);
			counters.malformedMessages++;
			return std::nullopt;
		}
		counters.duplicateMessages++;
		return completeIfReady(framed.sequence);
	}

	pending.objects[index] = framed.object;
	pending.objectSlots[index] = true;
	pending.receivedObjectCount++;
	return completeIfReady(framed.sequence);
}

std::optional<ObjectFrameAssembler::CompleteFrame> ObjectFrameAssembler::completeIfReady(
	int32_t sequence) {
	auto pendingIt = pendingFrames.find(sequence);
	if (pendingIt == pendingFrames.end()) {
		return std::nullopt;
	}

	PendingFrame & pending = pendingIt->second;
	if (pending.receivedObjectCount != pending.totalObjectCount
		|| !std::all_of(pending.partHeaders.begin(), pending.partHeaders.end(),
			[](bool received) { return received; })) {
		return std::nullopt;
	}

	std::unordered_set<int> ids;
	for (const DetectedObject & object : pending.objects) {
		if (!ids.insert(object.id).second) {
			pendingFrames.erase(pendingIt);
			counters.malformedMessages++;
			return std::nullopt;
		}
	}

	CompleteFrame complete { pending.identity, std::move(pending.objects) };
	if (lastCommittedSequence >= 0) {
		const uint32_t distance = sequenceDistance(sequence, lastCommittedSequence);
		if (distance > 1) {
			counters.missingSequences += distance - 1;
		}
	}
	lastCommittedSequence = sequence;
	counters.completedFrames++;

	for (auto it = pendingFrames.begin(); it != pendingFrames.end();) {
		if (it->first == sequence || !isSequenceNewer(it->first, sequence)) {
			if (it->first != sequence) {
				counters.incompleteFrames++;
			}
			it = pendingFrames.erase(it);
		} else {
			++it;
		}
	}

	return complete;
}

void ObjectFrameAssembler::expire(double nowSeconds) {
	for (auto it = pendingFrames.begin(); it != pendingFrames.end();) {
		if (nowSeconds - it->second.firstReceivedAt > pendingFrameTimeoutSeconds) {
			it = pendingFrames.erase(it);
			counters.incompleteFrames++;
		} else {
			++it;
		}
	}
}
