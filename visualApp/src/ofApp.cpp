#include "ofApp.h"
#include <cerrno>
#include <cmath>
#include <cstdlib>

namespace {
const std::string defaultSensorSourceIp = "10.0.0.1";
constexpr float defaultPreviewTimeoutSeconds = 1.0f;

std::string environmentValue(const char * name, const std::string & fallback) {
	const char * value = std::getenv(name);
	return value != nullptr && value[0] != '\0' ? value : fallback;
}

float positiveEnvironmentFloat(const char * name, float fallback) {
	const char * text = std::getenv(name);
	if (text == nullptr || text[0] == '\0') {
		return fallback;
	}

	errno = 0;
	char * end = nullptr;
	const float value = std::strtof(text, &end);
	if (errno != 0 || end == text || *end != '\0' || !std::isfinite(value)
		|| value <= 0.0f || value > 60.0f) {
		ofLogWarning("ofApp") << "invalid " << name << "='" << text
			<< "'; using " << fallback;
		return fallback;
	}
	return value;
}
}

//--------------------------------------------------------------
ofApp::ofApp(bool diagnosticsEnabled, bool calibrationEnabled)
	: diagnosticsEnabled(diagnosticsEnabled)
	, calibrationEnabled(calibrationEnabled)
	, showDebugHud(diagnosticsEnabled)
	, showSensorPreview(diagnosticsEnabled) {
}

//--------------------------------------------------------------
void ofApp::setup() {
	// In calibration Esc means "reload persisted values". Disable OF's normal
	// Escape-to-quit behavior only for that explicitly started maintenance mode.
	if (calibrationEnabled) {
		ofSetEscapeQuitsApp(false);
	}

	// The production window must never expose the desktop pointer on the
	// projection. Calibration keeps it visible as an additional mode cue.
	if (diagnosticsEnabled) {
		ofShowCursor();
		ofLogNotice("ofApp") << (calibrationEnabled
			? "visual calibration enabled (--calibration)"
			: "visual diagnostics enabled (--diagnostics)");
	} else {
		ofHideCursor();
	}

	// This file is local to Pi B and survives releases through a /var/lib
	// symlink. A malformed/missing file leaves the compiled defaults active and
	// visible in calibration rather than preventing access to the repair UI.
	projectionSettings.setup("projection-calibration.json");
	if (!projectionMapper.configure(projectionSettings.get(), ofGetWidth(), ofGetHeight())) {
		ofLogFatalError("ofApp") << "final projection FBO could not be configured";
		std::exit(EXIT_FAILURE);
	}

	// Pi B doesn't own the sensor (FR-002) - it listens for Pi A's object
	// stream instead. Port 8000, static IPs - see
	// kinect-v2-raspberry-pi-setup.md section 8.
	const std::string sensorSourceIp = environmentValue(
		"OSC_EXPECTED_SOURCE_IP", defaultSensorSourceIp);
	kinectInput.setup(8000, sensorSourceIp);

	// Production neither binds nor drains the diagnostic preview port. Only
	// the explicit calibration profile accepts Pi A's mask stream.
	if (diagnosticsEnabled) {
		const float previewTimeout = positiveEnvironmentFloat(
			"PREVIEW_TIMEOUT_SECONDS", defaultPreviewTimeoutSeconds);
		previewReceiver.setup(8001, sensorSourceIp, previewTimeout);
	}
	if (calibrationEnabled) {
		calibrationControl.setup(sensorSourceIp, CalibrationProtocol::controlPort,
			CalibrationProtocol::controlPort);
		calibrationPanel.setup();
	}

	// MODI ORDER
	//
	// The Game of Life is two screens, not three. The plain Game of Life that
	// used to open the rotation is gone - it drew the same grid as these two and
	// asked less of a visitor, and its rule buttons needed a mouse the
	// installation does not have. Its rule presets live on inside PixelPushVisual,
	// changing on their own so the table does not settle into one look.
	//
	// The two that remain differ only in whether Game of Life is in the transform
	// chain: pushing material around while life plays out in it, and pushing
	// material that only ever moves where it is pushed. They are separate modes
	// rather than one setting because they are different pieces to stand in front
	// of - on the second one a heap that has been shovelled together stays a heap
	// - see processDocumentation.md.
	visuals.push_back(std::make_unique<PixelPushVisual>(true));
	visuals.push_back(std::make_unique<PixelPushVisual>(false));

	// A flight through nowhere in particular, which never stops and asks only
	// which way to point. No system, no orbits, no names - invented things
	// drifting past, drawn as pixels. It shares only the table and the hands
	// with the Game of Life screen.
	visuals.push_back(std::make_unique<UniverseVisual>());

	// Last in the rotation, and the only screen that can be lost. Everything
	// before it answers a hand with a picture; this one answers with a score,
	// which is a different thing to ask of somebody and worth asking only once
	// they have already found out that the table watches them at all. It plays
	// itself while nobody is there, so the end of the rotation is never a still
	// frame - see InvaderVisual.
	visuals.push_back(std::make_unique<InvaderVisual>());
	synchronizeProjectionGeometry();

	for (auto & v : visuals) {
		v->setup();

		std::string failureReason;
		if (!v->isReady(failureReason)) {
			ofLogFatalError("ofApp") << "visual startup health check failed for "
				<< v->getTitle() << ": " << failureReason;
			std::exit(EXIT_FAILURE);
		}
	}
	visualsInitialized = true;

	// onEnter() plans any visual-owned finite sequence before its requested mode
	// duration is read. PUSH + LIFE uses this to align the production deadline
	// with the sum of its three freshly drawn phase durations.
	visuals[currentIndex]->onEnter();
	previousIndex = currentIndex;
	resetModeRotationTimer();

	std::string projectionFailure;
	if (!projectionMapper.isReady(projectionFailure)) {
		ofLogFatalError("ofApp") << "visual startup health check failed: "
			<< projectionFailure;
		std::exit(EXIT_FAILURE);
	}
}

bool ofApp::automaticModeRotationEnabled() const {
	return !diagnosticsEnabled && !calibrationEnabled;
}

void ofApp::resetModeRotationTimer() {
	float interval = ofRandom(minModeSeconds, maxModeSeconds);
	float visualInterval = visuals[currentIndex]->getScheduledModeDurationSeconds();
	if (std::isfinite(visualInterval) && visualInterval > 0.0f) {
		interval = visualInterval;
	}
	modeRotation.reset(interval);
}

void ofApp::advanceVisual(const char * reason) {
	currentIndex = (currentIndex + 1) % static_cast<int>(visuals.size());
	ofLogNotice("ofApp") << "switched to visual index " << currentIndex
		<< " (" << reason << ")";
}

void ofApp::synchronizeProjectionGeometry() {
	if (!projectionMapper.configure(projectionSettings.get(),
		ofGetWidth(), ofGetHeight())) {
		ofLogFatalError("ofApp") << "final projection FBO reconfiguration failed";
		std::exit(EXIT_FAILURE);
	}

	for (auto & visual : visuals) {
		visual->setProjectionGeometry(projectionMapper.getCanvasWidth(),
			projectionMapper.getCanvasHeight(),
			projectionSettings.get().surfaceWidthCm,
			projectionSettings.get().surfaceHeightCm,
			projectionSettings.get().cornerRadiusFraction,
			projectionSettings.get().baseEdge);
		if (visualsInitialized) {
			std::string failureReason;
			if (!visual->isReady(failureReason)) {
				ofLogFatalError("ofApp") << "visual projection reconfiguration failed: "
					<< failureReason;
				std::exit(EXIT_FAILURE);
			}
		}
	}
}

//--------------------------------------------------------------
void ofApp::update() {
	synchronizeProjectionGeometry();
	kinectInput.update(); // drains queued OSC messages, doesn't poll a local sensor
	detectedObjects = TableCoordinateMapper::map(
		kinectInput.getDetectedObjects(), projectionSettings.get());

	// The receiver exists only in the explicit calibration profile. It is still
	// drained while the overlay is toggled off so UDP packets cannot queue up.
	if (diagnosticsEnabled) {
		previewReceiver.update();
	}
	if (calibrationEnabled) {
		calibrationControl.update();
	}

	// A published object is treated conservatively as interaction. Pi A already
	// suppresses blobs that have remained inert; using the complete atomic frame
	// here protects hands and movable objects equally and cannot switch halfway
	// through a short classification wobble between Hand and Unknown.
	if (automaticModeRotationEnabled()
		&& modeRotation.update(static_cast<float>(ofGetLastFrameTime()), !detectedObjects.empty())) {
		advanceVisual("scheduled after interaction grace");
	}

	// Tell the visual that it is now the one showing, so it can start its sound
	// or reset whatever a previous visitor left behind. Declared on VisualBase,
	// so no cast to a specific screen is needed to find out who cares.
	if (currentIndex != previousIndex) {
		visuals[currentIndex]->onEnter();
		previousIndex = currentIndex;
		resetModeRotationTimer();
	}

	// Grab-triggered pause (fist paused GameOfLife) was dropped per FR-006:
	// DetectedObject carries no grabStrength-style engagement value, so
	// gestureDetector stays available but unwired.

	visuals[currentIndex]->update(detectedObjects);
}
//--------------------------------------------------------------
void ofApp::draw() {
	synchronizeProjectionGeometry();
	if (!projectionMapper.beginFrame()) {
		ofLogFatalError("ofApp") << "could not begin final projection frame";
		std::exit(EXIT_FAILURE);
	}

	visuals[currentIndex]->draw();
	ofDisableDepthTest();

	if (showDebugHud && !calibrationEnabled) {
		// makes the OSC-received DetectedObject stream visible on screen for
		// testing the Pi A <-> Pi B link (kinect-v2-raspberry-pi-setup.md § 8)
		// Frame rate first, because it is per-visual: the visuals differ by more
		// than an order of magnitude in what they cost, so "is this screen too
		// slow" is a question that can only be answered while it is showing.
		std::string hud = "fps: " + ofToString(ofGetFrameRate(), 1)
			+ "  visual: " + ofToString(currentIndex) + " " + visuals[currentIndex]->getTitle()
			+ "  objects: " + ofToString(detectedObjects.size()) + "  ('d' to hide)";

		// Whatever the current screen wants shown while it is being calibrated.
		std::string status = visuals[currentIndex]->getStatusLine();

		if (!status.empty()) {
			hud += "\n  " + status;
		}
		hud += "\n  " + kinectInput.getProtocolStatusLine();
		hud += "\n  output "
			+ std::string(quarterTurnDisplayName(projectionSettings.get().outputRotation))
			+ "  base " + baseEdgeDisplayName(projectionSettings.get().baseEdge)
			+ "  sensor->table "
			+ quarterTurnDisplayName(projectionSettings.get().sensorToTableRotation);
		if (diagnosticsEnabled) {
			hud += "\n  " + previewReceiver.getStatusLine();
		}
		for (const auto & obj : detectedObjects) {
			// area and z are shown because GameOfLife's influence radius and strength are
			// derived from them - the values arriving here are what areaAtMaxInfluence and
			// baselineHeightInfluence have to be tuned against.
			hud += "\n  id=" + ofToString(obj.id)
				+ " " + objectTypeLabel(obj.type)
				+ " x=" + ofToString(obj.x, 2) + " y=" + ofToString(obj.y, 2)
				+ " z=" + ofToString(obj.z, 2) + " area=" + ofToString(obj.area, 3)
				+ " v=" + ofToString(obj.vx, 2) + "," + ofToString(obj.vy, 2)
				+ " age=" + ofToString(obj.age, 1) + "s";
		}
		ofDrawBitmapStringHighlight(hud, 20, 20);
	}

	if (calibrationEnabled) {
		calibrationPanel.draw(calibrationControl, previewReceiver,
			projectionSettings, detectedObjects,
			visuals[currentIndex]->getPlayRect(),
			projectionMapper.getCanvasWidth(), projectionMapper.getCanvasHeight());
	} else {
		drawSensorPreview();
	}

	projectionMapper.endFrame();
	projectionMapper.drawToOutput(projectionSettings.get(),
		ofGetWidth(), ofGetHeight());
}

//--------------------------------------------------------------
// Pi A's detection mask, small in the top right corner. This is how the
// calibration picture is watched at all, since Pi A runs headless - see
// sensorApp's --preview flag. Nothing is drawn until a complete fresh frame
// arrives; after the configured timeout the old mask disappears.
void ofApp::drawSensorPreview() {
	if (!showSensorPreview || !previewReceiver.hasImage()) {
		return;
	}

	// Drawn at twice its transmitted size: 128x106 is unreadably small on an
	// installation display, 256x212 sits in the corner without covering the
	// piece.
	const float scale = 2.0f;
	const float margin = 20.0f;

	float width = previewReceiver.getWidth() * scale;
	float height = previewReceiver.getHeight() * scale;
	float x = projectionMapper.getCanvasWidth() - width - margin;
	float y = margin;

	// Match the full calibration panel's mirror-like sensor view. This changes
	// presentation only; the received mask and object coordinates stay in the
	// protocol's original sensor space.
	ofSetColor(255);
	ofPushMatrix();
	ofTranslate(x + width, y);
	ofScale(-1.0f, 1.0f);
	previewReceiver.draw(0.0f, 0.0f, width, height);
	ofPopMatrix();

	// Framed, because a mostly-black mask against a dark visual is otherwise
	// hard to tell apart from the piece itself.
	ofNoFill();
	ofSetColor(255, 200, 0);
	ofDrawRectangle(x, y, width, height);
	ofFill();
	ofSetColor(255);

	ofDrawBitmapStringHighlight("sensor mask - mirrored L/R ('p')",
		x, y + height + 16);
}

//--------------------------------------------------------------
void ofApp::exit() {
	// no-op: KinectOSCInput has no device/socket teardown of its own beyond
	// ofxOscReceiver's destructor.
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key) {
	if (calibrationEnabled) {
		bool cycleVisual = false;
		calibrationPanel.keyPressed(key, calibrationControl,
			projectionSettings, cycleVisual);
		if (cycleVisual) {
			advanceVisual("calibration key");
		}
		return;
	}

	if (key == 'd') {
		if (diagnosticsEnabled) {
			showDebugHud = !showDebugHud;
		}
		return;
	}

	if (key == 'p') {
		if (diagnosticsEnabled) {
			showSensorPreview = !showSensorPreview;
		}
		return;
	}

	// The visual showing gets first refusal on the key, so a screen can claim
	// keys for its own settings and have them adjustable on the table.
	if (visuals[currentIndex]->keyPressed(key)) {
		return;
	}

	// any other key press cycles to the next visual, round-robin
	advanceVisual("manual key");
}

//--------------------------------------------------------------
void ofApp::keyReleased(int key) {
}

//--------------------------------------------------------------
void ofApp::mouseMoved(int x, int y) {
}

//--------------------------------------------------------------
void ofApp::mouseDragged(int x, int y, int button) {
}

//--------------------------------------------------------------
void ofApp::mousePressed(int x, int y, int button) {
	// No visual takes the mouse any more. The rule-preset buttons that used to
	// live on the Game of Life screen are gone: the presets now change on a
	// timer, and the installation has a table rather than a mouse in front of it.
}

//--------------------------------------------------------------
void ofApp::mouseReleased(int x, int y, int button) {
}

//--------------------------------------------------------------
void ofApp::mouseScrolled(int x, int y, float scrollX, float scrollY) {
}

//--------------------------------------------------------------
void ofApp::mouseEntered(int x, int y) {
}

//--------------------------------------------------------------
void ofApp::mouseExited(int x, int y) {
}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h) {
	synchronizeProjectionGeometry();
}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg) {
}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo) {
}
