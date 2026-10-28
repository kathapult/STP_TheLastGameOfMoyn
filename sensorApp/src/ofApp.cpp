#include "ofApp.h"
#include <cstdlib>

// Network defaults match the documented direct Ethernet link. The managed
// service can override the destination through its site environment file.
static const std::string DEFAULT_PI_B_HOST = "10.0.0.2";
static const int OSC_PORT = 8000;

// Separate port for the mask preview, so its much larger packets travel on
// their own socket and a lost or fragmented one can never disturb the object
// stream Pi B renders from.
static const int PREVIEW_PORT = 8001;

namespace {
std::string environmentValue(const char * name, const std::string & fallback) {
	const char * value = std::getenv(name);
	return value != nullptr && value[0] != '\0' ? value : fallback;
}
}

//--------------------------------------------------------------
ofApp::ofApp(bool debugEnabled, bool previewEnabled, bool calibrationEnabled)
	: debugEnabled(debugEnabled)
	, previewEnabled(previewEnabled)
	, calibrationEnabled(calibrationEnabled) {
}

//--------------------------------------------------------------
void ofApp::setup() {
	if (kinect.setup()) {
		ofLogNotice("ofApp") << "Kinect connection started; waiting for the first fresh depth frame";
	} else {
		ofLogWarning("ofApp") << "Kinect is not currently available; automatic reconnect is active"
			<< " (check power/USB, see kinect-v2-raspberry-pi-setup.md)";
	}

	detector.setup();
	const std::string piBHost = environmentValue("OSC_TARGET_IP", DEFAULT_PI_B_HOST);
	sender.setup(piBHost, OSC_PORT);

	// Only touched when --debug was passed; without it there is no window to
	// set a title or background on.
	if (debugEnabled) {
		debugView.setup();
		ofLogNotice("ofApp") << "debug window enabled (--debug) - detection stages are drawn, run without the flag for the show";
	}

	// Left un-set-up otherwise, which is what keeps PreviewSender::send() a
	// no-op in the show configuration.
	if (previewEnabled) {
		previewSender.setup(piBHost, PREVIEW_PORT);
	}
	if (calibrationEnabled) {
		calibrationControl.setup(piBHost, CalibrationProtocol::controlPort,
			CalibrationProtocol::controlPort);
	}
}

//--------------------------------------------------------------
void ofApp::update() {
	// Control must remain responsive during Kinect timeouts and reconnects.
	// The production profile never sets the server up, so this is a no-op there.
	if (calibrationEnabled) {
		calibrationControl.update(detector);
	}

	KinectInput::FrameStatus frameStatus = kinect.update();
	if (frameStatus != KinectInput::FrameStatus::FreshFrame) {
		// In particular, never let the detector advance object age, stillness
		// or velocity from the previous pixel buffer during a sensor outage.
		// Pi B already expires an unrefreshed object stream after its hold time.
		return;
	}

	std::vector<DetectedObject> objects = detector.update(kinect);
	ObjectStreamProtocol::FrameIdentity frame = sender.send(objects);

	// After detection, so what is streamed is the mask this frame's objects
	// were actually found in. No-op unless --preview was passed.
	previewSender.send(detector.getMask(), kinect.getDepthPixels(),
		detector.getTrackDiagnostics(), frame);
}

//--------------------------------------------------------------
void ofApp::draw() {
	// Without --debug the app runs on ofAppNoWindow, so this is never called
	// at all and the debug path costs nothing per frame. The guard covers the
	// case of a window existing for some other reason.
	if (!debugEnabled) {
		return;
	}

	// Reads the current frame back out of kinect/detector rather than from a
	// copy, so what is drawn is exactly what was just sent to Pi B.
	debugView.draw(kinect, detector, calibrationTarget);
}

//--------------------------------------------------------------
// Applies one arrow-key press to whichever group of values is currently
// targeted. Split out so keyPressed stays readable now that the same four keys
// mean three different things.
void ofApp::nudge(DetectionSettings & settings, int key) const {
	float horizontal = (key == OF_KEY_RIGHT) ? 1.0f : (key == OF_KEY_LEFT ? -1.0f : 0.0f);
	float vertical = (key == OF_KEY_UP) ? 1.0f : (key == OF_KEY_DOWN ? -1.0f : 0.0f);

	switch (calibrationTarget) {
	case CalibrationTarget::DepthWindow:
		settings.nearMM += horizontal * CalibrationProtocol::descriptor(
			CalibrationProtocol::Field::NearMM).step;
		settings.farMM += vertical * CalibrationProtocol::descriptor(
			CalibrationProtocol::Field::FarMM).step;
		break;
	case CalibrationTarget::TableSurface:
		settings.tableSurfaceClearanceMM += horizontal * CalibrationProtocol::descriptor(
			CalibrationProtocol::Field::TableSurfaceClearanceMM).step;
		break;
	case CalibrationTarget::RoiPosition:
		// Screen-natural: down moves the rectangle down the image, and image
		// y grows downwards, so the vertical sign is inverted here.
		settings.roiX += horizontal * CalibrationProtocol::descriptor(
			CalibrationProtocol::Field::RoiX).step;
		settings.roiY -= vertical * CalibrationProtocol::descriptor(
			CalibrationProtocol::Field::RoiY).step;
		break;
	case CalibrationTarget::RoiSize:
		settings.roiWidth += horizontal * CalibrationProtocol::descriptor(
			CalibrationProtocol::Field::RoiWidth).step;
		settings.roiHeight += vertical * CalibrationProtocol::descriptor(
			CalibrationProtocol::Field::RoiHeight).step;
		break;
	case CalibrationTarget::Noise:
		// Whole pixels, since the kernels are built from an integer radius
		// anyway - a fractional step would produce presses that do nothing.
		settings.morphOpenRadius += static_cast<int>(horizontal
			* CalibrationProtocol::descriptor(CalibrationProtocol::Field::MorphOpenRadius).step);
		settings.morphCloseRadius += static_cast<int>(vertical
			* CalibrationProtocol::descriptor(CalibrationProtocol::Field::MorphCloseRadius).step);
		break;
	case CalibrationTarget::Smoothing:
		settings.positionSmoothingMinCutoff += horizontal * CalibrationProtocol::descriptor(
			CalibrationProtocol::Field::PositionSmoothingMinCutoff).step;
		settings.positionSmoothingBeta += vertical * CalibrationProtocol::descriptor(
			CalibrationProtocol::Field::PositionSmoothingBeta).step;
		break;
	case CalibrationTarget::Classification:
		settings.handSolidityThreshold += horizontal * CalibrationProtocol::descriptor(
			CalibrationProtocol::Field::HandSolidityThreshold).step;
		settings.handMaxBorderWidth += vertical * CalibrationProtocol::descriptor(
			CalibrationProtocol::Field::HandMaxBorderWidth).step;
		break;
	case CalibrationTarget::HandSize:
		settings.handMinPalmRadiusMM += horizontal * CalibrationProtocol::descriptor(
			CalibrationProtocol::Field::HandMinPalmRadiusMM).step;
		settings.handMaxPalmRadiusMM += vertical * CalibrationProtocol::descriptor(
			CalibrationProtocol::Field::HandMaxPalmRadiusMM).step;
		break;
	case CalibrationTarget::HandPosition:
		if (horizontal != 0.0f) {
			const int mode = ofClamp(static_cast<int>(settings.handPositionMode)
				+ static_cast<int>(horizontal),
				static_cast<int>(CalibrationProtocol::HandPositionMode::ContourCentroid),
				static_cast<int>(CalibrationProtocol::HandPositionMode::InwardTip));
			settings.handPositionMode = static_cast<CalibrationProtocol::HandPositionMode>(mode);
		}
		settings.inwardTipCapFraction += vertical * CalibrationProtocol::descriptor(
			CalibrationProtocol::Field::InwardTipCapFraction).step;
		break;
	case CalibrationTarget::Stationary:
		settings.stationarySeconds += horizontal * CalibrationProtocol::descriptor(
			CalibrationProtocol::Field::StationarySeconds).step;
		settings.stationaryTolerance += vertical * CalibrationProtocol::descriptor(
			CalibrationProtocol::Field::StationaryTolerance).step;
		break;
	}
}

//--------------------------------------------------------------
// Calibration keys, active only in the debug window. The arrow keys edit
// whichever target 'm' has selected; everything not reachable by key is edited
// in settings.json and picked up with 'r'.
void ofApp::keyPressed(int key) {
	if (!debugEnabled) {
		return;
	}

	switch (key) {
	case OF_KEY_LEFT:
	case OF_KEY_RIGHT:
	case OF_KEY_UP:
	case OF_KEY_DOWN: {
		// Copy-modify-set rather than mutating in place, so ObjectDetector
		// can sanitize the result before it takes effect (setSettings).
		DetectionSettings updated = detector.getSettings();
		nudge(updated, key);
		detector.setSettings(updated);
		break;
	}
	case 'm':
		calibrationTarget = nextCalibrationTarget(calibrationTarget);
		break;
	case 'f': {
		// Escape hatch back to the whole frame - much quicker than nudging a
		// mis-set rectangle back open edge by edge.
		DetectionSettings updated = detector.getSettings();
		updated.roiX = 0.0f;
		updated.roiY = 0.0f;
		updated.roiWidth = 1.0f;
		updated.roiHeight = 1.0f;
		detector.setSettings(updated);
		break;
	}
	case 'c':
	case 'C':
		if (calibrationTarget == CalibrationTarget::TableSurface) {
			detector.requestTableSurfaceCapture();
		}
		break;
	case 'x':
	case 'X':
		if (calibrationTarget == CalibrationTarget::TableSurface) {
			detector.clearTableSurfaceMask();
		}
		break;
	case 'r':
		// Re-reads the file, so it can be edited over SSH with the window
		// left open.
		detector.reloadCalibration();
		break;
	case 'w':
		// Writes the values nudged in with the arrow keys back to disk,
		// where they become the next start's defaults.
		detector.saveSettings();
		break;
	default:
		break;
	}
}

//--------------------------------------------------------------
void ofApp::exit() {
	kinect.exit();
}
