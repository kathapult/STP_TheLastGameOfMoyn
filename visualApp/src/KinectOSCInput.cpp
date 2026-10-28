#include "KinectOSCInput.h"
#include "ObjectStreamProtocol.h"
#include <algorithm>
#include <exception>
#include <sstream>

using namespace ObjectStreamProtocol;

namespace {
bool hasType(const ofxOscMessage & message, int index, ofxOscArgType type) {
	return index >= 0 && index < static_cast<int>(message.getNumArgs())
		&& message.getArgType(index) == type;
}

bool parseDetectedObject(const ofxOscMessage & message, DetectedObject & object) {
	const int argumentCount = static_cast<int>(message.getNumArgs());
	if (argumentCount < 3 || (argumentCount > 3 && argumentCount < 8)
		|| !hasType(message, 0, OFXOSC_TYPE_INT32)
		|| !hasType(message, 1, OFXOSC_TYPE_FLOAT)
		|| !hasType(message, 2, OFXOSC_TYPE_FLOAT)) {
		return false;
	}
	for (int index = 3; index < std::min(argumentCount, 8); ++index) {
		if (!hasType(message, index, OFXOSC_TYPE_FLOAT)) {
			return false;
		}
	}
	if (argumentCount >= legacyObjectArgumentCount
		&& !hasType(message, 8, OFXOSC_TYPE_INT32)) {
		return false;
	}

	object.id = message.getArgAsInt32(0);
	object.x = message.getArgAsFloat(1);
	object.y = message.getArgAsFloat(2);
	if (message.getNumArgs() >= 8) {
		object.z = message.getArgAsFloat(3);
		object.area = message.getArgAsFloat(4);
		object.vx = message.getArgAsFloat(5);
		object.vy = message.getArgAsFloat(6);
		object.age = message.getArgAsFloat(7);
	}
	if (message.getNumArgs() >= legacyObjectArgumentCount) {
		object.type = toObjectType(message.getArgAsInt32(8));
	}

	FramedObject validation;
	validation.object = object;
	validation.streamId = 1;
	validation.sequence = 0;
	validation.objectIndex = 0;
	return isSaneObject(validation);
}

bool parseFrameHeader(const ofxOscMessage & message, FrameHeader & header) {
	if (message.getNumArgs() < frameHeaderArgumentCount) {
		return false;
	}
	for (int index = 0; index < frameHeaderArgumentCount; ++index) {
		const ofxOscArgType expected = index == 2 ? OFXOSC_TYPE_INT64 : OFXOSC_TYPE_INT32;
		if (!hasType(message, index, expected)) {
			return false;
		}
	}

	header.identity.streamId = message.getArgAsInt32(0);
	header.identity.sequence = message.getArgAsInt32(1);
	header.identity.senderTimeUs = message.getArgAsInt64(2);
	header.totalObjectCount = message.getArgAsInt32(3);
	header.partIndex = message.getArgAsInt32(4);
	header.partCount = message.getArgAsInt32(5);
	header.firstObjectIndex = message.getArgAsInt32(6);
	header.partObjectCount = message.getArgAsInt32(7);
	return isValidHeader(header);
}

bool parseFramedObject(const ofxOscMessage & message, FramedObject & framed) {
	if (message.getNumArgs() < framedObjectArgumentCount
		|| !parseDetectedObject(message, framed.object)) {
		return false;
	}
	for (int index = 9; index < framedObjectArgumentCount; ++index) {
		if (!hasType(message, index, OFXOSC_TYPE_INT32)) {
			return false;
		}
	}

	framed.streamId = message.getArgAsInt32(9);
	framed.sequence = message.getArgAsInt32(10);
	framed.objectIndex = message.getArgAsInt32(11);
	return isSaneObject(framed);
}
}

void KinectOSCInput::setup(int listenPort, const std::string & sourceIp) {
	expectedSourceIp = sourceIp;
	receiver.setup(listenPort);
	lastProtocolPacketTime = ofGetElapsedTimef();
	ofLogNotice("KinectOSCInput") << "listening for object frames on UDP " << listenPort
		<< " from " << expectedSourceIp;
}

void KinectOSCInput::update() {
	const float now = ofGetElapsedTimef();
	std::vector<DetectedObject> legacyIncoming;

	while (receiver.hasWaitingMessages()) {
		ofxOscMessage message;
		receiver.getNextMessage(message);

		if (!expectedSourceIp.empty() && message.getRemoteHost() != expectedSourceIp) {
			rejectedSourceMessages++;
			if (now - lastSourceWarningTime >= silenceLogThresholdSeconds) {
				ofLogWarning("KinectOSCInput") << "dropping OSC message from unexpected source "
					<< message.getRemoteHost() << "; expected " << expectedSourceIp;
				lastSourceWarningTime = now;
			}
			continue;
		}

		try {
			if (message.getAddress() == frameAddress) {
				FrameHeader header;
				if (!parseFrameHeader(message, header)) {
					malformedMessages++;
					continue;
				}

				lastProtocolPacketTime = now;
				lastFramedPacketTime = now;
				framedMode = true;
				const uint64_t generationBefore = frameAssembler.getStreamGeneration();
				auto complete = frameAssembler.addHeader(header, now);
				if (frameAssembler.getStreamGeneration() != generationBefore) {
					remembered.clear();
					detectedObjects.clear();
					ofLogNotice("KinectOSCInput") << "accepted object stream id "
						<< frameAssembler.getActiveStreamId();
				}
				if (complete.has_value()) {
					remembered.clear();
					for (const DetectedObject & object : complete->objects) {
						remembered[object.id] = RememberedObject { object, now };
					}
				}
				continue;
			}

			if (message.getAddress() != objectAddress) {
				continue;
			}

			if (message.getNumArgs() >= framedObjectArgumentCount) {
				FramedObject framed;
				if (!parseFramedObject(message, framed)) {
					malformedMessages++;
					continue;
				}
				lastProtocolPacketTime = now;
				auto complete = frameAssembler.addObject(framed, now);
				if (complete.has_value()) {
					remembered.clear();
					for (const DetectedObject & object : complete->objects) {
						remembered[object.id] = RememberedObject { object, now };
					}
				}
				continue;
			}

			DetectedObject legacyObject;
			if (parseDetectedObject(message, legacyObject)) {
				legacyIncoming.push_back(legacyObject);
			} else {
				malformedMessages++;
			}
		} catch (const std::exception & error) {
			malformedMessages++;
			ofLogWarning("KinectOSCInput") << "dropping malformed OSC message: " << error.what();
		} catch (...) {
			malformedMessages++;
			ofLogWarning("KinectOSCInput") << "dropping malformed OSC message";
		}
	}

	frameAssembler.expire(now);

	// A new receiver can run against an old sender. After framed traffic has
	// been seen, legacy packets are ignored briefly so they cannot overwrite a
	// newer snapshot. Continuous legacy traffic after that grace period means
	// Pi A was rolled back and is accepted again without restarting Pi B.
	if (!legacyIncoming.empty()
		&& (!framedMode || now - lastFramedPacketTime > legacyFallbackSeconds)) {
		if (framedMode) {
			framedMode = false;
			remembered.clear();
			ofLogNotice("KinectOSCInput") << "falling back to the legacy /object stream";
		}
		lastProtocolPacketTime = now;
		for (const DetectedObject & object : legacyIncoming) {
			remembered[object.id] = RememberedObject { object, now };
		}
	}

	// Between complete sensor snapshots the last authoritative positions are
	// extrapolated briefly. An incomplete or lost multipart frame never leaks a
	// partial set into the visuals.
	for (auto it = remembered.begin(); it != remembered.end();) {
		if (now - it->second.receivedAt > maxExtrapolationSeconds) {
			it = remembered.erase(it);
		} else {
			++it;
		}
	}

	detectedObjects.clear();
	detectedObjects.reserve(remembered.size());
	for (const auto & entry : remembered) {
		const RememberedObject & rememberedObject = entry.second;
		const float age = now - rememberedObject.receivedAt;
		DetectedObject object = rememberedObject.object;
		if (age > 0.0f) {
			object.x = ofClamp(object.x + object.vx * age, 0.0f, 1.0f);
			object.y = ofClamp(object.y + object.vy * age, 0.0f, 1.0f);
			object.age += age;
		}
		detectedObjects.push_back(object);
	}

	if (now - lastProtocolPacketTime <= silenceLogThresholdSeconds) {
		loggedSilence = false;
	} else if (!loggedSilence) {
		ofLogWarning("KinectOSCInput") << "no object-stream packet received in over "
			<< silenceLogThresholdSeconds << "s - check Pi A / the network link";
		loggedSilence = true;
	}
}

std::string KinectOSCInput::getProtocolStatusLine() const {
	const ObjectFrameAssembler::Counters & counters = frameAssembler.getCounters();
	std::ostringstream status;
	status << "osc " << (framedMode ? "v1" : "legacy")
		<< " stream=" << frameAssembler.getActiveStreamId()
		<< " seq=" << frameAssembler.getLastCommittedSequence()
		<< " gaps=" << counters.missingSequences
		<< " partial=" << counters.incompleteFrames
		<< " late=" << counters.lateFrames
		<< " malformed=" << counters.malformedMessages + malformedMessages
		<< " sourceDrop=" << rejectedSourceMessages;
	return status.str();
}
