#pragma once

#include "DetectedObject.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

// Wire-level constants and validation shared by Pi A and Pi B. This header has
// no openFrameworks dependency, so frame assembly can be tested on its own.
namespace ObjectStreamProtocol {

constexpr const char * frameAddress = "/frame/v1";
constexpr const char * objectAddress = "/object";
constexpr const char * previewAddress = "/preview";

constexpr int legacyObjectArgumentCount = 9;
constexpr int framedObjectArgumentCount = 12;
constexpr int frameHeaderArgumentCount = 8;
constexpr int legacyPreviewArgumentCount = 5;
constexpr int framedPreviewArgumentCount = 9;

// The existing installation already caps noisy frames at forty objects. A
// datagram carries at most seventeen of them, so even the largest part stays
// safely below the IPv4 UDP payload limit for an MTU of 1500.
constexpr int maxObjectsPerFrame = 40;
constexpr int maxObjectsPerDatagram = 17;
constexpr int ipv4UdpPayloadLimit = 1472;

// OSC byte sizes for the exact v1 encoding used by ObjectStreamSender:
// bundle header, /frame/v1 element and one extended /object element.
constexpr int oscBundleHeaderBytes = 16;
constexpr int frameElementBytes = 64;
constexpr int objectElementBytes = 76;
constexpr int maximumDatagramBytes = oscBundleHeaderBytes + frameElementBytes
	+ maxObjectsPerDatagram * objectElementBytes;

static_assert(maximumDatagramBytes == 1372, "update the OSC size calculation when v1 changes");
static_assert(maximumDatagramBytes < ipv4UdpPayloadLimit, "object packet would require IP fragmentation");

constexpr int32_t maximumSequence = std::numeric_limits<int32_t>::max();
constexpr uint32_t sequenceModulus = static_cast<uint32_t>(maximumSequence) + 1u;

struct FrameIdentity {
	int32_t streamId = 0;
	int32_t sequence = 0;
	int64_t senderTimeUs = 0;
};

struct FrameHeader {
	FrameIdentity identity;
	int32_t totalObjectCount = 0;
	int32_t partIndex = 0;
	int32_t partCount = 1;
	int32_t firstObjectIndex = 0;
	int32_t partObjectCount = 0;
};

struct FramedObject {
	DetectedObject object;
	int32_t streamId = 0;
	int32_t sequence = 0;
	int32_t objectIndex = 0;
};

inline int32_t nextSequence(int32_t sequence) {
	return sequence >= maximumSequence ? 0 : sequence + 1;
}

inline uint32_t sequenceDistance(int32_t candidate, int32_t reference) {
	if (candidate < 0 || reference < 0) {
		return 0;
	}

	return (static_cast<uint32_t>(candidate) + sequenceModulus
		- static_cast<uint32_t>(reference)) % sequenceModulus;
}

inline bool isSequenceNewer(int32_t candidate, int32_t reference) {
	const uint32_t distance = sequenceDistance(candidate, reference);
	return distance > 0 && distance < sequenceModulus / 2u;
}

inline int32_t partCountForObjects(int32_t objectCount) {
	return objectCount <= 0
		? 1
		: (objectCount + maxObjectsPerDatagram - 1) / maxObjectsPerDatagram;
}

inline int32_t expectedObjectsInPart(int32_t totalObjectCount, int32_t partIndex) {
	const int32_t first = partIndex * maxObjectsPerDatagram;
	if (first >= totalObjectCount) {
		return 0;
	}

	return std::min<int32_t>(maxObjectsPerDatagram, totalObjectCount - first);
}

inline bool isValidHeader(const FrameHeader & header) {
	if (header.identity.streamId <= 0 || header.identity.sequence < 0
		|| header.identity.senderTimeUs < 0 || header.totalObjectCount < 0
		|| header.totalObjectCount > maxObjectsPerFrame) {
		return false;
	}

	const int32_t expectedPartCount = partCountForObjects(header.totalObjectCount);
	if (header.partCount != expectedPartCount || header.partIndex < 0
		|| header.partIndex >= header.partCount) {
		return false;
	}

	return header.firstObjectIndex == header.partIndex * maxObjectsPerDatagram
		&& header.partObjectCount == expectedObjectsInPart(
			header.totalObjectCount, header.partIndex);
}

inline bool isSaneObject(const FramedObject & framed) {
	const DetectedObject & object = framed.object;
	return framed.streamId > 0 && framed.sequence >= 0 && framed.objectIndex >= 0
		&& framed.objectIndex < maxObjectsPerFrame && object.id >= 0
		&& std::isfinite(object.x) && object.x >= 0.0f && object.x <= 1.0f
		&& std::isfinite(object.y) && object.y >= 0.0f && object.y <= 1.0f
		&& std::isfinite(object.z) && object.z >= 0.0f && object.z <= 1.0f
		&& std::isfinite(object.area) && object.area >= 0.0f && object.area <= 1.0f
		&& std::isfinite(object.vx) && std::isfinite(object.vy)
		&& std::isfinite(object.age) && object.age >= 0.0f;
}

inline bool sameObject(const DetectedObject & left, const DetectedObject & right) {
	return left.id == right.id && left.x == right.x && left.y == right.y
		&& left.z == right.z && left.area == right.area && left.vx == right.vx
		&& left.vy == right.vy && left.age == right.age && left.type == right.type;
}

} // namespace ObjectStreamProtocol
