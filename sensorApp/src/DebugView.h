#pragma once

#include "ofMain.h"
#include "KinectInput.h"
#include "ObjectDetector.h"

// Pi A calibration/diagnostics window, drawn only when sensorApp is started
// with --debug (see main.cpp). Shows the detection stages side by side: the
// raw depth frame, and the binary mask the contour finder actually runs on
// with its contours, interaction points and tracker ids drawn on top. Without this,
// every threshold and area value in ObjectDetector has to be tuned blind,
// since Pi A is otherwise windowless and only logs on exception paths.
//
// This is not a VisualBase and not part of any render pipeline - Pi B owns
// all visuals (FR-003). Constitution Principle II keeps *visuals* from
// reaching into sensor/CV code; this is the opposite direction and never
// leaves sensorApp.
class DebugView {
public:
	void setup();

	// Reads the current frame straight from the two objects that produced it
	// rather than taking a copied snapshot, so enabling the window cannot
	// change what the detection pipeline sees. The detector is non-const
	// because ofxCv's contour accessors are not const-qualified.
	// calibrationTarget only affects the footer text - it says which values
	// the arrow keys are currently editing.
	void draw(const KinectInput & kinect, ObjectDetector & detector, CalibrationTarget calibrationTarget);

private:
	// Draws contour outlines, interaction points and track ids at the given screen
	// offset, in the mask's own pixel coordinates.
	void drawContours(ObjectDetector & detector, CalibrationTarget calibrationTarget,
		float originX, float originY);

	// Outlines the rounded table region and greys out everything outside it, so
	// the diagnostic matches the exact pixels ObjectDetector may accept.
	void drawRoi(const ofRectangle & roi, float originX, float originY, int frameWidth, int frameHeight);

	// 8-bit grayscale copies of the two panels, rebuilt each drawn frame -
	// the depth source is float millimetres, the mask source is a cv::Mat.
	ofImage depthImage;
	ofImage maskImage;

	// Millimetre distance mapped to white. Kinect v2's usable range ends
	// around 4.5 m; further readings and the 0 that marks an invalid pixel
	// both clamp, so invalid areas read as solid black.
	static constexpr float depthDisplayMaxMM = 4500.0f;

	static constexpr float margin = 10.0f;
	static constexpr float captionHeight = 18.0f;

	// Pixels drawn per one normalized-unit-per-second of velocity. Velocity is
	// a fraction of the table per second, so a hand crossing the table in a
	// second is only 1.0 - it needs scaling up to be visible as a line at all.
	static constexpr float velocityDisplayScale = 120.0f;
};
