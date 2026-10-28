#pragma once

#include "ofMain.h"
#include "DetectedObject.h"
#include "KinectOSCInput.h"
#include "PreviewReceiver.h"
#include "CalibrationControlClient.h"
#include "CalibrationPanel.h"
#include "ProjectionMapper.h"
#include "ProjectionSettings.h"
#include "TableCoordinateMapper.h"
#include "ModeRotationScheduler.h"
#include "InvaderVisual.h"
#include "PixelPushVisual.h"
#include "UniverseVisual.h"
#include <vector>
#include <memory>
#include "GestureDetector.h"

class ofApp : public ofBaseApp {
public:
	explicit ofApp(bool diagnosticsEnabled, bool calibrationEnabled);

    void setup();
    void update();
    void draw();
    void exit();

	void keyPressed(int key) override;
	void keyReleased(int key) override;
	void mouseMoved(int x, int y ) override;
	void mouseDragged(int x, int y, int button) override;
	void mousePressed(int x, int y, int button) override;
	void mouseReleased(int x, int y, int button) override;
	void mouseScrolled(int x, int y, float scrollX, float scrollY) override;
	void mouseEntered(int x, int y) override;
	void mouseExited(int x, int y) override;
	void windowResized(int w, int h) override;
	void dragEvent(ofDragInfo dragInfo) override;
	void gotMessage(ofMessage msg) override;


	std::vector<std::unique_ptr<VisualBase>> visuals;
	int currentIndex = 0;

	
private:
	// Keeps the final FBO and every VisualBase on the same logical dimensions
	// after a live output-rotation or window-size change.
	void synchronizeProjectionGeometry();

	// Draws Pi A's mask in the corner. Split out of draw() so the show path
	// reads as one line and the overlay's geometry stays in one place.
	void drawSensorPreview();

	// Production rotates automatically. Maintenance profiles stay manual so a
	// calibration or diagnosis cannot disappear while somebody is reading it.
	bool automaticModeRotationEnabled() const;
	void resetModeRotationTimer();
	void advanceVisual(const char * reason);

	int previousIndex = -1; // tracks onEnter() and its freshly planned mode deadline
	GestureDetector gestureDetector; // kept available, not wired to a pause behavior (see .cpp)
	KinectOSCInput kinectInput; // OSC receiver for Pi A's object stream - Pi B owns no sensor itself (FR-002)
    std::vector<DetectedObject> detectedObjects;

	// Diagnostics are a startup profile, not a persistent toggle. Production
	// construction leaves the receiver closed and both overlays disabled.
	bool diagnosticsEnabled = false;
	bool calibrationEnabled = false;
	bool showDebugHud = false;
	bool showSensorPreview = false;

	// Pi A's downscaled detection mask, drawn small in the top right corner.
	// The receiver is opened only by a diagnostic/calibration startup profile
	// and gets data only while sensorApp runs with --preview or --calibration.
	// Separate toggle from the text HUD ('p'), since watching the mask and reading the numbers are
	// different jobs.
	PreviewReceiver previewReceiver;
	CalibrationControlClient calibrationControl;
	CalibrationPanel calibrationPanel;
	ProjectionSettings projectionSettings;
	ProjectionMapper projectionMapper;
	bool visualsInitialized = false;

	ModeRotationScheduler modeRotation;
	float minModeSeconds = 30.0f;
	float maxModeSeconds = 90.0f;
};
