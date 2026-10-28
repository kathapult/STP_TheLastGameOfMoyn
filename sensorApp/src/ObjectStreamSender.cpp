#include "ObjectStreamSender.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>

using namespace ObjectStreamProtocol;

namespace {
int32_t createStreamId() {
	// The id is not a secret. It only prevents a restarted sender whose frame
	// sequence begins at zero from being confused with delayed old packets.
	const uint32_t clockBits = static_cast<uint32_t>(
		std::chrono::high_resolution_clock::now().time_since_epoch().count());
	std::random_device randomDevice;
	uint32_t value = (clockBits ^ randomDevice()) & 0x7fffffffu;
	return static_cast<int32_t>(value == 0 ? 1 : value);
}

bool objectPriority(const DetectedObject * left, const DetectedObject * right) {
	const bool leftIsHand = left->type == ObjectType::Hand;
	const bool rightIsHand = right->type == ObjectType::Hand;
	if (leftIsHand != rightIsHand) {
		return leftIsHand;
	}
	// Invalid detector output is rejected by Pi B, but it must not make the
	// sort comparator violate strict weak ordering before it reaches the wire.
	const float leftArea = std::isfinite(left->area) ? left->area : -1.0f;
	const float rightArea = std::isfinite(right->area) ? right->area : -1.0f;
	if (leftArea != rightArea) {
		return leftArea > rightArea;
	}
	return left->id < right->id;
}
}

void ObjectStreamSender::setup(const std::string & host, int port) {
	sender.setup(host, port);
	streamId = createStreamId();
	nextFrameSequence = 0;
	ofLogNotice("ObjectStreamSender") << "object stream v1 started with stream id " << streamId;
}

FrameIdentity ObjectStreamSender::send(const std::vector<DetectedObject> & objects) {
	FrameIdentity identity;
	identity.streamId = streamId;
	identity.sequence = nextFrameSequence;
	identity.senderTimeUs = static_cast<int64_t>(ofGetElapsedTimeMicros());
	nextFrameSequence = ObjectStreamProtocol::nextSequence(nextFrameSequence);

	// A stable priority makes overload behavior reproducible: hands remain
	// interactive first, then larger objects, with id as the final tie-breaker.
	std::vector<const DetectedObject *> selected;
	selected.reserve(objects.size());
	for (const DetectedObject & object : objects) {
		selected.push_back(&object);
	}
	std::sort(selected.begin(), selected.end(), objectPriority);

	if (selected.size() > static_cast<size_t>(maxObjectsPerFrame)) {
		ofLogWarning("ObjectStreamSender") << "capping " << selected.size()
			<< " detected objects to " << maxObjectsPerFrame << " for frame "
			<< identity.sequence;
		selected.resize(maxObjectsPerFrame);
	}

	const int32_t totalCount = static_cast<int32_t>(selected.size());
	const int32_t partCount = partCountForObjects(totalCount);

	for (int32_t partIndex = 0; partIndex < partCount; ++partIndex) {
		const int32_t firstObjectIndex = partIndex * maxObjectsPerDatagram;
		const int32_t partObjectCount = expectedObjectsInPart(totalCount, partIndex);
		ofxOscBundle bundle;

		ofxOscMessage frame;
		frame.setAddress(frameAddress);
		frame.addInt32Arg(identity.streamId);
		frame.addInt32Arg(identity.sequence);
		frame.addInt64Arg(identity.senderTimeUs);
		frame.addInt32Arg(totalCount);
		frame.addInt32Arg(partIndex);
		frame.addInt32Arg(partCount);
		frame.addInt32Arg(firstObjectIndex);
		frame.addInt32Arg(partObjectCount);
		bundle.addMessage(frame);

		for (int32_t offset = 0; offset < partObjectCount; ++offset) {
			const int32_t objectIndex = firstObjectIndex + offset;
			const DetectedObject & object = *selected[static_cast<size_t>(objectIndex)];
			ofxOscMessage message;
			message.setAddress(objectAddress);

			// The first nine arguments remain the legacy append-only contract.
			// Old Pi B releases ignore the three v1 fields appended afterwards.
			message.addInt32Arg(object.id);
			message.addFloatArg(object.x);
			message.addFloatArg(object.y);
			message.addFloatArg(object.z);
			message.addFloatArg(object.area);
			message.addFloatArg(object.vx);
			message.addFloatArg(object.vy);
			message.addFloatArg(object.age);
			message.addInt32Arg(static_cast<int32_t>(object.type));
			message.addInt32Arg(identity.streamId);
			message.addInt32Arg(identity.sequence);
			message.addInt32Arg(objectIndex);
			bundle.addMessage(message);
		}

		// maximumDatagramBytes is a compile-time calculation for this exact
		// bundle shape. Seventeen objects produce 1372 bytes, below the 1472
		// byte IPv4 UDP payload limit at MTU 1500.
		sender.sendBundle(bundle);
	}

	return identity;
}
