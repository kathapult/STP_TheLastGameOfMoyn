#pragma once

#include "ofxOsc.h"
#include "DetectedObject.h"
#include "ObjectStreamProtocol.h"
#include <cstdint>
#include <vector>
#include <string>

// Pi A: encodes the current detected-object set as one or more MTU-safe OSC
// bundles and sends it to Pi B. See contracts/osc-object-stream.md for the
// wire format.
class ObjectStreamSender {
public:
	void setup(const std::string & host, int port);
	ObjectStreamProtocol::FrameIdentity send(const std::vector<DetectedObject> & objects);

private:
	ofxOscSender sender;
	int32_t streamId = 0;
	int32_t nextFrameSequence = 0;
};
