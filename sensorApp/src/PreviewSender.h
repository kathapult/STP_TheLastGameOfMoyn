#pragma once

#include "ofxOsc.h"
#include "ofxCv.h"
#include "ObjectStreamProtocol.h"
#include "CalibrationProtocol.h"
#include <string>
#include <vector>

// Streams a small copy of the detection mask to Pi B, so the calibration view
// can be watched during development even though Pi A has no monitor attached.
// Off unless sensorApp was started with --preview or --calibration; in the
// show configuration nothing here runs at all.
//
// Deliberately its own sender on its own port rather than extra messages in
// ObjectStreamSender's bundle: the preview is far larger than the object
// stream, and a dropped or fragmented preview packet must never be able to
// disturb the detection data Pi B actually renders from.
class PreviewSender {
public:
	// Until this is called the sender stays disabled and send() does nothing,
	// so the caller does not need its own flag.
	void setup(const std::string & host, int port);

	// Downscales and sends mask, raw depth and Pi-A-computed track diagnostics.
	// Silently does nothing when disabled or between send intervals.
	void send(const cv::Mat & mask, const ofFloatPixels & depth,
		const std::vector<CalibrationProtocol::TrackDiagnostic> & tracks,
		const ObjectStreamProtocol::FrameIdentity & frame);

private:
	void sendImage(const char * address, const cv::Mat & pixels,
		const ObjectStreamProtocol::FrameIdentity & frame);
	void sendTracks(const std::vector<CalibrationProtocol::TrackDiagnostic> & tracks,
		const ObjectStreamProtocol::FrameIdentity & frame);

	ofxOscSender sender;
	bool enabled = false;

	// Reused between frames so the resize does not reallocate every time.
	cv::Mat scaled;
	cv::Mat depth8;

	int frameCounter = 0;

	// 512x424 becomes 128x106, i.e. 13.5 kB per frame instead of 217 kB. Big
	// enough to judge a threshold or an ROI edge on, small enough that the
	// cost is a resize and a few memcpys.
	static constexpr int downscale = 4;

	// Every third frame is about 10 fps, which is plenty for watching a mask
	// and keeps the link at roughly 1 Mbit/s.
	static constexpr int sendEveryNthFrame = 3;

	// Rows per message. At 128 px wide this keeps each packet near 1 kB -
	// under the ethernet MTU, so nothing is IP-fragmented, and far under any
	// OSC library's output buffer. Pi B publishes the frame only after all of
	// its strips arrived, so a lost strip cannot create a mixed-age image.
	static constexpr int rowsPerStrip = 8;
};
