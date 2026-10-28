#include "DebugView.h"
#include "TableShape.h"
#include "ofxCv.h"

#include <cmath>

namespace {
void drawRoiCornerCutout(const glm::vec2 & sharpCorner,
	const glm::vec2 & arcCentre, float radius, float startAngle, float endAngle) {
	const int arcSteps = 12;
	ofMesh cutout;
	cutout.setMode(OF_PRIMITIVE_TRIANGLE_FAN);
	cutout.addVertex(glm::vec3(sharpCorner, 0.0f));
	for (int step = 0; step <= arcSteps; ++step) {
		const float angle = ofLerp(startAngle, endAngle,
			static_cast<float>(step) / static_cast<float>(arcSteps));
		cutout.addVertex(glm::vec3(arcCentre
			+ radius * glm::vec2(std::cos(angle), std::sin(angle)), 0.0f));
	}
	cutout.draw();
}

void drawRoiCornerCutouts(const ofRectangle & roi, float radius) {
	drawRoiCornerCutout(glm::vec2(roi.x, roi.y),
		glm::vec2(roi.x + radius, roi.y + radius), radius, -HALF_PI, -PI);
	drawRoiCornerCutout(glm::vec2(roi.getRight(), roi.y),
		glm::vec2(roi.getRight() - radius, roi.y + radius), radius, -HALF_PI, 0.0f);
	drawRoiCornerCutout(glm::vec2(roi.getRight(), roi.getBottom()),
		glm::vec2(roi.getRight() - radius, roi.getBottom() - radius), radius, 0.0f, HALF_PI);
	drawRoiCornerCutout(glm::vec2(roi.x, roi.getBottom()),
		glm::vec2(roi.x + radius, roi.getBottom() - radius), radius, HALF_PI, PI);
}
}

void DebugView::setup() {
	ofSetWindowTitle("sensorApp - debug");
	ofBackground(0);
}

void DebugView::draw(const KinectInput & kinect, ObjectDetector & detector, CalibrationTarget calibrationTarget) {
	const ofFloatPixels & depth = kinect.getDepthPixels();
	int w = depth.getWidth();
	int h = depth.getHeight();

	// Nothing has arrived from the sensor yet (not connected, or still
	// starting up). Say so rather than leaving a black window that reads as
	// a crash.
	if (w == 0 || h == 0) {
		ofDrawBitmapStringHighlight("no depth frame yet - is the Kinect connected? (see kinect-v2-raspberry-pi-setup.md)",
			margin, margin + captionHeight);
		return;
	}

	// Reallocate only when the frame size actually changes, which in practice
	// means once, on the first drawn frame.
	if (!depthImage.isAllocated()
		|| static_cast<int>(depthImage.getWidth()) != w
		|| static_cast<int>(depthImage.getHeight()) != h) {
		depthImage.allocate(w, h, OF_IMAGE_GRAYSCALE);
	}

	// Millimetres to 8-bit grey across the sensor's whole range, so the full
	// scene stays visible - the panel next to it is what shows which part of
	// it survives the near/far window.
	const float * depthSrc = depth.getData();
	unsigned char * depthDst = depthImage.getPixels().getData();
	int pixelCount = w * h;
	for (int i = 0; i < pixelCount; i++) {
		depthDst[i] = static_cast<unsigned char>(
			ofMap(depthSrc[i], 0.0f, depthDisplayMaxMM, 0.0f, 255.0f, true));
	}
	depthImage.update();

	float panelY = margin + captionHeight;
	float maskX = margin + w + margin;

	const DetectionSettings & settings = detector.getSettings();
	ofRectangle roi = settings.roiInPixels(w, h);

	ofSetColor(255);
	depthImage.draw(margin, panelY);
	// Drawn on the depth panel too, since that is the one that still shows
	// what falls outside - on the mask panel the outside is already black.
	drawRoi(roi, margin, panelY, w, h);

	// The mask is CV_8UC1 and allocated contiguously by ObjectDetector, so
	// its buffer maps straight onto a grayscale ofImage - no ofxCv drawing
	// helper needed, whose signature varies between addon versions.
	const cv::Mat & mask = detector.getMask();
	if (!mask.empty()) {
		ofSetColor(255);
		maskImage.setFromPixels(mask.data, mask.cols, mask.rows, OF_IMAGE_GRAYSCALE);
		maskImage.draw(maskX, panelY);
		drawRoi(roi, maskX, panelY, w, h);
		drawContours(detector, calibrationTarget, maskX, panelY);
	}

	// Panel captions, so it is unambiguous which side is which. The active
	// foreground rule is named on the mask panel because that panel is its
	// result.
	const TableSurfaceMask::Status tableMask = detector.getTableSurfaceMaskStatus();
	ofDrawBitmapStringHighlight("DEPTH - raw, 0-" + ofToString(static_cast<int>(depthDisplayMaxMM)) + "mm as black-white",
		margin, margin + 12);
	const std::string maskRule = tableMask.active
		? ofToString(settings.nearMM, 0) + "mm to captured table, gap >"
			+ ofToString(settings.tableSurfaceClearanceMM, 0) + "mm"
		: ofToString(settings.nearMM, 0) + "-" + ofToString(settings.farMM, 0) + "mm";
	const std::string positionLegend = calibrationTarget == CalibrationTarget::HandPosition
		? ", cyan = palm, orange = inward, yellow = entry" : "";
	ofDrawBitmapStringHighlight("MASK - " + maskRule
			+ "  (green = hand, purple = object, red = raw centroid, blue = sent"
			+ positionLegend + ")",
		maskX, margin + 12);

	// Footer: the values actually in force, which group the arrow keys edit,
	// and the remaining keys. Without this, nudging a value blind gives no way
	// to tell what it is now or what would be written to settings.json.
	float footerY = panelY + h + captionHeight;
	ofDrawBitmapStringHighlight(settings.toDisplayString(), margin, footerY);
	ofDrawBitmapStringHighlight("rounded table roi px " + ofToString(static_cast<int>(roi.width)) + "x"
			+ ofToString(static_cast<int>(roi.height))
			+ " at " + ofToString(static_cast<int>(roi.x)) + "," + ofToString(static_cast<int>(roi.y))
			+ "   |   editing: " + calibrationTargetLabel(calibrationTarget),
		margin, footerY + 16);
	std::string tableMaskText = "table mask ";
	if (tableMask.captureState == TableSurfaceMask::CaptureState::Capturing) {
		tableMaskText += "capturing " + ofToString(tableMask.capturedFrames) + "/"
			+ ofToString(tableMask.requiredFrames);
	} else if (tableMask.captureState == TableSurfaceMask::CaptureState::Ready) {
		tableMaskText += "active " + ofToString(tableMask.validCoverage * 100.0f, 1) + "%";
	} else if (tableMask.captureState == TableSurfaceMask::CaptureState::Error) {
		tableMaskText += "ERROR " + detector.getTableSurfaceMaskError();
	} else {
		tableMaskText += "not captured";
	}
	ofDrawBitmapStringHighlight(tableMaskText, margin, footerY + 32);
	ofDrawBitmapStringHighlight("m: switch target  c: capture empty table  x: clear table mask  f: full roi  r: reload  w: save",
		margin, footerY + 48);
}

void DebugView::drawRoi(const ofRectangle & roi, float originX, float originY, int frameWidth, int frameHeight) {
	ofPushMatrix();
	ofTranslate(originX, originY);

	// Grey wash over the four bands outside the ROI's bounding rectangle and
	// the four cut corners. The table-shaped interior stays at its true
	// brightness, which is necessary for judging a depth threshold.
	ofFill();
	ofSetColor(0, 0, 0, 140);
	ofDrawRectangle(0, 0, frameWidth, roi.y);
	ofDrawRectangle(0, roi.y + roi.height, frameWidth, frameHeight - (roi.y + roi.height));
	ofDrawRectangle(0, roi.y, roi.x, roi.height);
	ofDrawRectangle(roi.x + roi.width, roi.y, frameWidth - (roi.x + roi.width), roi.height);
	const float radius = TableShape::cornerRadius(roi.width, roi.height);
	drawRoiCornerCutouts(roi, radius);

	// The same rounded outline ObjectDetector applies to the depth pixels.
	ofNoFill();
	ofSetColor(255, 200, 0);
	ofDrawRectRounded(roi.x, roi.y, roi.width, roi.height, radius);

	ofPopMatrix();

	ofFill();
	ofSetColor(255);
}

void DebugView::drawContours(ObjectDetector & detector,
	CalibrationTarget calibrationTarget, float originX, float originY) {
	ofxCv::ContourFinder & contours = detector.getContourFinder();

	// Drawn in the mask's own pixel coordinates, shifted onto the mask panel.
	ofPushMatrix();
	ofTranslate(originX, originY);

	const std::unordered_map<int, ObjectDetector::TrackState> & tracks = detector.getTracks();
	const DetectionSettings & settings = detector.getSettings();

	for (int i = 0; i < contours.size(); i++) {
		int label = static_cast<int>(contours.getLabel(i));
		auto track = tracks.find(label);
		ObjectType type = (track != tracks.end()) ? track->second.type : ObjectType::Unknown;

		// Outline: the blob shape the area limits are accepting or rejecting,
		// coloured by classification so a misread is obvious at a glance
		// without reading any numbers.
		ofNoFill();
		if (type == ObjectType::Hand) {
			ofSetColor(0, 255, 120);
		} else if (type == ObjectType::Object) {
			ofSetColor(200, 120, 255);
		} else {
			ofSetColor(160, 160, 160);
		}
		contours.getPolyline(i).draw();
		glm::vec2 rawCentroid = ofxCv::toOf(contours.getCentroid(i));

		// Raw contour centroid, hollow: the measurement before smoothing.
		ofNoFill();
		ofSetColor(255, 0, 0);
		ofDrawCircle(rawCentroid.x, rawCentroid.y, 4.0f);

		// Smoothed interaction position, filled: the point that actually becomes
		// DetectedObject::x/y. Drawing both is what makes the One-Euro
		// smoothing (DetectionSettings::positionSmoothingMinCutoff/Beta)
		// tunable by eye - the gap between the two circles is the lag being
		// traded for steadiness.
		bool tracked = (track != tracks.end());
		glm::vec2 sentPosition = tracked ? track->second.smoothedPosition : rawCentroid;
		ofFill();
		ofSetColor(80, 160, 255);
		ofDrawCircle(sentPosition.x, sentPosition.y, 3.0f);

		// Tracker id, classification and the numbers behind it: how long the
		// id has survived (it keeps resetting to 0 when the tracker settings
		// are wrong), the solidity that was measured, and whether the blob
		// reached the ROI border. Both classification cues are shown because
		// either one alone can produce a Hand, so the label on its own does
		// not say which fired.
		std::string labelText = ofToString(label);
		if (tracked) {
			const ObjectDetector::TrackState & state = track->second;
			if (calibrationTarget == CalibrationTarget::HandPosition) {
				if (state.hasPalmCentre) {
					ofNoFill();
					ofSetColor(0, 230, 230);
					ofDrawCircle(state.palmCentre, 5.0f);
				}
				if (state.inwardCandidateValid || state.inwardActive) {
					ofFill();
					ofSetColor(255, 150, 0);
					ofDrawCircle(state.inwardPoint, 5.0f);
					ofSetColor(255, 220, 0);
					ofDrawLine(state.entryPoint.x - 5.0f, state.entryPoint.y,
						state.entryPoint.x + 5.0f, state.entryPoint.y);
					ofDrawLine(state.entryPoint.x, state.entryPoint.y - 5.0f,
						state.entryPoint.x, state.entryPoint.y + 5.0f);
				}
			}
			float stillFor = ofGetElapsedTimef() - state.stillSince;
			// Same test ObjectDetector itself applies before dropping a track
			// from update()'s returned list - HIDDEN here means this blob is
			// still drawn on this debug panel only, and is not actually being
			// sent to Pi B any more.
			bool hidden = settings.hideSeconds > 0.0f && stillFor >= settings.hideSeconds;
			// palm radius in mm is the size veto, solidity and border width are
			// the two shape cues, still is the stationary override - all four
			// inputs to the verdict, so a wrong label says which one was off.
			labelText += " " + std::string(objectTypeLabel(state.type))
				+ " palm" + ofToString(state.palmRadiusMM, 0) + "mm"
				+ " sol" + ofToString(state.solidity, 2)
				+ " bd" + ofToString(state.borderContact, 2)
				+ " still" + ofToString(stillFor, 0) + "s"
				+ (hidden ? " HIDDEN" : "");
			if (calibrationTarget == CalibrationTarget::HandPosition) {
				labelText += " pos=" + std::string(
					CalibrationProtocol::handPositionModeLabel(state.positionSource));
				if (state.inwardCandidateValid || state.inwardActive) {
					labelText += " conf" + ofToString(state.inwardConfidence, 2)
						+ (state.inwardCandidateValid ? "" : " held");
				}
			}
		}
		ofDrawBitmapStringHighlight(labelText, sentPosition.x + 6, sentPosition.y - 6);

		// Velocity as a line from the interaction position, scaled up so a slow drift is
		// still visible. Shows at a glance whether the smoothing has made the
		// derived velocity usable or left it noisy.
		if (tracked) {
			glm::vec2 velocity = track->second.velocity;
			ofSetColor(255, 200, 0);
			ofDrawLine(sentPosition.x, sentPosition.y,
				sentPosition.x + velocity.x * velocityDisplayScale,
				sentPosition.y + velocity.y * velocityDisplayScale);
		}
	}

	ofPopMatrix();

	// Leave the draw state as found, since ofApp may draw after this.
	ofFill();
	ofSetColor(255);
}
