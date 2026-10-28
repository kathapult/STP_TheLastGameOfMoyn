#pragma once

#include "ofMain.h"
#include "KinectInput.h"
#include "ObjectDetector.h"
#include "ObjectStreamSender.h"
#include "PreviewSender.h"
#include "DebugView.h"
#include "CalibrationControlServer.h"

// Pi A: owns the Kinect, runs detection, sends the object stream. Renders
// nothing in normal operation (FR-003) - main.cpp runs it windowless unless
// started with --debug, in which case the calibration window is drawn.
class ofApp : public ofBaseApp {
public:
	// Both flags come from main()'s command line and are fixed for the process
	// lifetime. debugEnabled decides which window type main() already created
	// before constructing this; previewEnabled turns on the mask stream to Pi
	// B, which is how the detection stages are watched when Pi A has no
	// monitor attached.
	ofApp(bool debugEnabled, bool previewEnabled, bool calibrationEnabled);

	void setup();
	void update();
	void draw();
	void exit();

	// Calibration keys. Only ever delivered in debug mode, since without
	// --debug there is no window to receive input.
	void keyPressed(int key);

private:
	// Applies one arrow-key press to the currently targeted group of values.
	void nudge(DetectionSettings & settings, int key) const;

	KinectInput kinect;
	ObjectDetector detector;
	ObjectStreamSender sender;
	CalibrationControlServer calibrationControl;

	// Stays disabled unless setup() sets it up, so no per-frame guard is
	// needed at the call site.
	PreviewSender previewSender;

	const bool debugEnabled;
	const bool previewEnabled;
	const bool calibrationEnabled;
	DebugView debugView;

	// Which values the arrow keys currently edit, cycled with 'm'. Debug-only
	// state; without a window nothing ever changes it.
	CalibrationTarget calibrationTarget = CalibrationTarget::DepthWindow;
};
