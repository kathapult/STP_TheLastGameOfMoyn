#include "ObjectDetector.h"
#include "HandClassificationPolicy.h"
#include "PaddedComponentRegion.h"
#include "TableShape.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace {
constexpr float inwardAcquireSeconds = 0.12f;
constexpr float inwardGraceSeconds = 0.25f;
constexpr float positionSourceTransitionSeconds = 0.20f;

float smoothStep(float value) {
	const float t = ofClamp(value, 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}
} // namespace

void ObjectDetector::setup(const std::string & path) {
	settingsPath = path;

	// A missing file leaves DetectionSettings' defaults in place, which are
	// the values this class used to hard-code - so a fresh checkout without
	// a settings file behaves exactly as before.
	settings.load(settingsPath);

	// Managed releases expose settings.json as a symlink into /var/lib. Resolve
	// that target before deriving the larger table model, so updates and
	// rollbacks all address the same site calibration without adding a large
	// binary asset to each release.
	std::filesystem::path settingsFile(ofToDataPath(settingsPath, true));
	std::error_code pathError;
	if (std::filesystem::is_symlink(settingsFile, pathError)) {
		settingsFile = std::filesystem::canonical(settingsFile, pathError);
	}
	if (pathError) {
		ofLogError("ObjectDetector") << "could not resolve table mask path beside "
			<< settingsPath << ": " << pathError.message();
		tableSurfaceMask.setup("");
	} else {
		const std::filesystem::path tableMaskPath = settingsFile.parent_path()
			/ "table-surface-mask.bin";
		if (!tableSurfaceMask.setup(tableMaskPath.string())) {
			ofLogError("ObjectDetector") << "table surface mask was not loaded: "
				<< tableSurfaceMask.getLastError();
		} else if (tableSurfaceMask.isActive()) {
			ofLogNotice("ObjectDetector") << "loaded persistent table surface mask from "
				<< tableMaskPath.string();
		}
	}
	applySettings();
}

void ObjectDetector::setSettings(const DetectionSettings & newSettings) {
	settings = newSettings;
	settings.sanitize();
	applySettings();
}

bool ObjectDetector::reloadSettings() {
	if (!settings.load(settingsPath)) {
		return false;
	}
	applySettings();
	return true;
}

bool ObjectDetector::saveSettings() const {
	return settings.save(settingsPath);
}

bool ObjectDetector::requestTableSurfaceCapture() {
	if (!tableSurfaceMask.requestCapture()) {
		return false;
	}
	tracks.clear();
	trackDiagnostics.clear();
	ofLogNotice("ObjectDetector") << "empty-table capture requested; keep the table clear for "
		<< TableSurfaceMask::captureFrameCount << " fresh depth frames";
	return true;
}

bool ObjectDetector::clearTableSurfaceMask() {
	if (!tableSurfaceMask.clear()) {
		ofLogError("ObjectDetector") << tableSurfaceMask.getLastError();
		return false;
	}
	tracks.clear();
	trackDiagnostics.clear();
	ofLogNotice("ObjectDetector") << "persistent table surface mask cleared";
	return true;
}

bool ObjectDetector::reloadCalibration() {
	const bool settingsLoaded = reloadSettings();
	const bool tableMaskLoaded = tableSurfaceMask.reload();
	if (!tableMaskLoaded) {
		ofLogError("ObjectDetector") << "table surface mask reload failed: "
			<< tableSurfaceMask.getLastError();
	}
	tracks.clear();
	trackDiagnostics.clear();
	return settingsLoaded && tableMaskLoaded;
}

void ObjectDetector::applySettings() {
	// Noise specks and oversized blobs are rejected by area, not by
	// hand-tuning the mask.
	contourFinder.setMinAreaRadius(settings.minAreaRadius);
	contourFinder.setMaxAreaRadius(settings.maxAreaRadius);

	// ContourFinder's built-in tracker provides exactly the lifecycle
	// data-model.md describes (nearest-match within a distance threshold,
	// a grace window before a track is dropped) - no tracker of our own to
	// write, test, or tune. See research.md item 2.
	contourFinder.getTracker().setMaximumDistance(settings.trackerMaximumDistance);
	contourFinder.getTracker().setPersistence(settings.trackerPersistence);

	// Elliptical rather than rectangular, so the clean-up does not square off
	// the rounded shapes (hands, arms) this is meant to preserve. A radius of
	// 0 leaves the kernel empty, which update() reads as "skip this step".
	openKernel = buildKernel(settings.morphOpenRadius);
	closeKernel = buildKernel(settings.morphCloseRadius);
}

cv::Mat ObjectDetector::buildKernel(int radius) const {
	if (radius <= 0) {
		return cv::Mat();
	}

	// Diameter is 2r+1 so the kernel has a well-defined centre pixel.
	int diameter = 2 * radius + 1;
	return cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(diameter, diameter));
}

void ObjectDetector::ensureRoiMask(int frameWidth, int frameHeight,
	int roiX, int roiY, int roiWidth, int roiHeight) {
	if (!roiMask.empty() && roiMask.cols == frameWidth && roiMask.rows == frameHeight
		&& cachedRoiX == roiX && cachedRoiY == roiY
		&& cachedRoiWidth == roiWidth && cachedRoiHeight == roiHeight) return;

	roiMask.create(frameHeight, frameWidth, CV_8UC1);
	roiMask.setTo(cv::Scalar(0));
	for (int y = roiY; y < roiY + roiHeight; ++y) {
		unsigned char * roiRow = roiMask.ptr<unsigned char>(y);
		for (int x = roiX; x < roiX + roiWidth; ++x) {
			if (TableShape::containsPixel(x - roiX, y - roiY, roiWidth, roiHeight)) {
				roiRow[x] = 255;
			}
		}
	}

	cachedRoiX = roiX;
	cachedRoiY = roiY;
	cachedRoiWidth = roiWidth;
	cachedRoiHeight = roiHeight;
}

std::vector<DetectedObject> ObjectDetector::update(const KinectInput & kinect) {
	// KinectInput keeps its last pixel buffer for drawing between normal
	// frames, so validity must include explicit freshness rather than merely a
	// non-null data pointer. The sequence check is defense in depth against a
	// second detector call before the next KinectInput::update().
	std::uint64_t frameSequence = kinect.getDepthFrameSequence();
	if (!kinect.hasFreshDepthFrame()
		|| frameSequence == 0
		|| frameSequence == lastProcessedDepthFrameSequence) {
		return {};
	}

	const ofFloatPixels & depth = kinect.getDepthPixels();

	int w = depth.getWidth();
	int h = depth.getHeight();
	if (w <= 0 || h <= 0 || depth.getNumChannels() != 1 || depth.getData() == nullptr) {
		ofLogError("ObjectDetector") << "fresh Kinect status carried no valid depth pixels; frame skipped";
		mask.release();
		return {};
	}

	// From here on this sequence has been consumed, even if a malformed ROI
	// below prevents CV processing. That keeps every sensor frame strictly
	// single-use across all early-return paths.
	lastProcessedDepthFrameSequence = frameSequence;

	std::uint64_t connectionGeneration = kinect.getConnectionGeneration();
	if (lastConnectionGeneration != 0 && connectionGeneration != lastConnectionGeneration) {
		tracks.clear();
		ofLogNotice("ObjectDetector") << "Kinect reconnected; filter, age and stillness state reset";
	}
	lastConnectionGeneration = connectionGeneration;

	// Bounding rectangle of the table in the depth frame. TableShape cuts its
	// four corners to the real rounded outline below; the rectangle still owns
	// calibration, coordinate normalization and persistence.
	ofRectangle roi = settings.roiInPixels(w, h);
	int roiX = static_cast<int>(roi.x);
	int roiY = static_cast<int>(roi.y);
	int roiW = static_cast<int>(roi.width);
	int roiH = static_cast<int>(roi.height);
	if (roiX < 0 || roiY < 0 || roiW <= 0 || roiH <= 0
		|| roiX + roiW > w || roiY + roiH > h) {
		ofLogError("ObjectDetector") << "invalid ROI " << roiX << "," << roiY
			<< " " << roiW << "x" << roiH << " for depth frame " << w << "x" << h
			<< "; frame skipped";
		mask.release();
		return {};
	}

	// Build the 8-bit foreground mask from the fixed near plane and either the
	// legacy far plane or the captured per-pixel table reference. Values are
	// read from settings every frame so a reload or key nudge takes effect on
	// the very next frame without any re-setup.
	//
	// The mask stays full frame size and is cleared first, then only the ROI
	// rows are written. That keeps mask coordinates identical to depth
	// coordinates - which is what lets DebugView overlay the two panels
	// without a second mapping - while doing the per-pixel work only where it
	// can produce a detection.
	mask.create(h, w, CV_8UC1);
	mask.setTo(cv::Scalar(0));
	ensureRoiMask(w, h, roiX, roiY, roiW, roiH);

	const float * src = depth.getData();
	const bool captureFrame = tableSurfaceMask.isCapturing();
	if (captureFrame) {
		tableSurfaceMask.addFrame(src, w, h, roiX, roiY, roiW, roiH);
		const TableSurfaceMask::Status captureStatus = tableSurfaceMask.getStatus();
		if (captureStatus.captureState == TableSurfaceMask::CaptureState::Ready) {
			ofLogNotice("ObjectDetector") << "table surface mask captured with "
				<< ofToString(captureStatus.validCoverage * 100.0f, 1)
				<< "% valid ROI coverage";
		} else if (captureStatus.captureState == TableSurfaceMask::CaptureState::Error) {
			ofLogError("ObjectDetector") << "table surface capture failed: "
				<< tableSurfaceMask.getLastError();
		}
	}

	if (tableSurfaceMask.isActive() && !tableSurfaceMask.matchesDimensions(w, h)) {
		ofLogError("ObjectDetector") << "table surface mask dimensions do not match the depth frame; frame skipped";
		mask.release();
		return {};
	}

	for (int y = roiY; y < roiY + roiH; y++) {
		const float * srcRow = src + static_cast<size_t>(y) * w;
		const unsigned char * roiRow = roiMask.ptr<unsigned char>(y);
		unsigned char * dstRow = mask.ptr<unsigned char>(y);
		for (int x = roiX; x < roiX + roiW; x++) {
			if (roiRow[x] == 0) continue;
			float d = srcRow[x];

			// libfreenect2 reports 0 where it could not measure at all
			// (reflective surface, shadow, too close). That is "no reading",
			// not "zero millimetres away", so it is excluded explicitly
			// rather than left to depend on where nearMM happens to sit.
			bool valid = d > 0.0f;

			// Once a table reference exists it becomes the spatially varying far
			// plane. Keeping the old global farMM ceiling as well would still cut
			// off a hand close to the farther side of a sloped table, defeating
			// the purpose of capturing that surface in the first place.
			const bool insideNearPlane = valid && d >= settings.nearMM;
			const bool insideFarPlane = tableSurfaceMask.isActive()
				? tableSurfaceMask.isForeground(d, x, y,
					settings.tableSurfaceClearanceMM)
				: d <= settings.farMM;
			dstRow[x] = (insideNearPlane && insideFarPlane) ? 255 : 0;
		}
	}

	// A capture is a maintenance action, not an object frame. Publish an empty
	// detector result for all sampled frames so old tracks and a hand left on
	// the table cannot leak into the show stream while the reference is built.
	if (captureFrame) {
		mask.setTo(cv::Scalar(0));
		contourFinder.findContours(mask);
		tracks.clear();
		trackDiagnostics.clear();
		return {};
	}

	// Clean up before the contour finder ever sees the mask. Opening first
	// (drop speckle), then closing (fill dropout holes and rejoin fragments):
	// the other order would inflate the speckle before removing it, so some
	// of it would survive as a legitimate-looking small blob.
	if (!openKernel.empty()) {
		cv::morphologyEx(mask, mask, cv::MORPH_OPEN, openKernel);
	}
	if (!closeKernel.empty()) {
		cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, closeKernel);
	}

	// Morphology operates on the complete image and can otherwise grow a
	// cleaned contour back into a cut-off corner. The physical table outline
	// is a hard boundary, so apply it again immediately before contouring.
	cv::bitwise_and(mask, roiMask, mask);

	contourFinder.findContours(mask);

	// Built fresh rather than updated in place, so ids that no longer appear
	// simply do not get carried over.
	std::unordered_map<int, TrackState> nextTracks;

	// One timestamp for the whole frame, so every object's age and velocity
	// are measured against the same instant.
	float now = ofGetElapsedTimef();
	float roiArea = static_cast<float>(roiW) * static_cast<float>(roiH);
	float depthSpan = std::max(1.0f, settings.farMM - settings.nearMM);

	// Read from the sensor's own factory calibration, so pixel measurements
	// can be turned into millimetres. Guarded because a disconnected device
	// leaves it at a fallback rather than zero, but a zero here would divide.
	float focalLengthX = std::max(1.0f, kinect.getDepthFocalLengthX());

	std::vector<DetectedObject> objects;
	std::vector<CalibrationProtocol::TrackDiagnostic> nextDiagnostics;
	nextDiagnostics.reserve(static_cast<size_t>(contourFinder.size()));
	for (int i = 0; i < contourFinder.size(); i++) {
		DetectedObject obj;
		obj.id = static_cast<int>(contourFinder.getLabel(i));

		const ofPolyline & outline = contourFinder.getPolyline(i);
		ofRectangle bounds = outline.getBoundingBox();
		cv::Mat component;
		cv::Rect componentRegion;
		if (!buildComponentMask(outline, bounds, component, componentRegion)) {
			continue;
		}

		// Height and size come from the mask itself rather than from the
		// contour, so they describe the filled shape - a spread hand and a
		// fist of the same width differ in area. Measured before the centroid
		// work because the classification below depends on the area.
		float meanDepthMM = 0.0f;
		float meanSurfaceDepthMM = 0.0f;
		int filledPixels = 0;
		if (measureBlob(depth, component, componentRegion,
			meanDepthMM, meanSurfaceDepthMM,
			filledPixels)) {
			// A captured surface supplies the local zero-height plane. Without
			// it, retain the established farMM mapping for rolling compatibility.
			const float zeroHeightDepthMM = meanSurfaceDepthMM > 0.0f
				? meanSurfaceDepthMM : settings.farMM;
			const float localDepthSpan = meanSurfaceDepthMM > 0.0f
				? std::max(1.0f, zeroHeightDepthMM - settings.nearMM)
				: depthSpan;
			obj.z = ofClamp((zeroHeightDepthMM - meanDepthMM)
				/ localDepthSpan, 0.0f, 1.0f);
			obj.area = ofClamp(filledPixels / roiArea, 0.0f, 1.0f);
		}

		// The largest circle that fits inside the blob. Its centre is the palm
		// of a hand (the arm is part of the same blob, so the contour centroid
		// sits somewhere up the forearm instead); its radius, once converted
		// to millimetres, is the size measurement the classification runs on.
		glm::vec2 palmCentre { 0.0f, 0.0f };
		float palmRadiusPixels = 0.0f;
		bool hasPalm = findPalmCentre(component, componentRegion,
			palmCentre, palmRadiusPixels);

		// Pixels to millimetres, using this blob's own distance: an object d
		// millimetres away covers focalLength/d pixels per millimetre, so the
		// same hand shrinks in pixels as it is lifted towards the sensor.
		// Doing this conversion is what makes the thresholds below physical
		// quantities that can be checked with a ruler, instead of numbers that
		// silently depend on mounting height.
		float palmRadiusMM = (hasPalm && meanDepthMM > 0.0f)
			? palmRadiusPixels * meanDepthMM / focalLengthX
			: 0.0f;

		// Classification. The normal hand path keeps the physical palm-size
		// veto. A second, deliberately narrow exception recognizes a long blob
		// entering through one table-edge contact: that topology describes a
		// reaching arm even when dropout or a sleeve distorts the measured palm.
		// The stillness override below means the exception applies only while the
		// reach is moving or has moved recently.
		//
		// The normal size veto is not a formality: a body silhouette scores low
		// solidity too (head and arms cut notches just like fingers do) and
		// reaches the border just like an arm does. Only the stricter single-entry
		// elongated-reach cue below may bypass it; mere border contact cannot.
		float solidity = measureSolidity(outline);
		float borderContact = measureBorderContact(component, componentRegion, roi);

		bool sizeFitsHand = palmRadiusMM >= settings.handMinPalmRadiusMM
			&& palmRadiusMM <= settings.handMaxPalmRadiusMM;

		// An arm crosses the table border narrowly. Merely reaching the
		// border is not evidence of anything - everything entering the scene
		// does that.
		bool armLike = borderContact > 0.0f && borderContact <= settings.handMaxBorderWidth;

		// Compute this before classification: a valid result proves both a
		// single unambiguous entry and enough geodesic penetration to be longer
		// than a round blob. It is also reused as the interaction position when
		// InwardTip is active, so the component is traversed only once per frame.
		InwardInteractionPointSelector::Result inward;
		if (armLike) {
			inward = findInwardPoint(component, componentRegion, palmRadiusPixels);
		}
		// Spread fingers leave notches a compact object does not have.
		bool fingerLike = solidity <= settings.handSolidityThreshold;

		auto previous = tracks.find(obj.id);
		bool isKnown = (previous != tracks.end());

		TrackState state;
		state.firstSeenTime = isKnown ? previous->second.firstSeenTime : now;
		state.lastSeenTime = now;
		state.contourCentroid = ofxCv::toOf(contourFinder.getCentroid(i));
		state.palmCentre = palmCentre;
		state.hasPalmCentre = hasPalm;

		// The inward point is accepted only after a short continuous observation.
		// Once active, a brief missing boundary contact keeps the last good point;
		// this prevents depth dropout from flipping palm/tip every other frame.
		InwardInteractionPointSelector::TemporalState previousTemporal;
		if (isKnown) {
			previousTemporal.active = previous->second.inwardActive;
			previousTemporal.candidateSince = previous->second.inwardCandidateSince;
			previousTemporal.lastValidTime = previous->second.lastInwardCandidateTime;
		}
		const InwardInteractionPointSelector::TemporalState temporal =
			InwardInteractionPointSelector::stabilize(previousTemporal,
				inward.valid, now, inwardAcquireSeconds, inwardGraceSeconds);
		state.inwardCandidateSince = temporal.candidateSince;
		state.lastInwardCandidateTime = temporal.lastValidTime;
		state.inwardCandidateValid = inward.valid;
		state.inwardActive = temporal.active;
		if (inward.valid) {
			state.inwardPoint = glm::vec2(inward.x, inward.y);
			state.entryPoint = glm::vec2(inward.entryX, inward.entryY);
			state.inwardConfidence = inward.confidence;
		} else if (temporal.holdPrevious && isKnown) {
			state.inwardPoint = previous->second.inwardPoint;
			state.entryPoint = previous->second.entryPoint;
			state.inwardConfidence = previous->second.inwardConfidence;
		}

		// The same temporal state drives the size-veto exception and the output
		// point. During its grace window neither type nor point source changes, so
		// a short missing edge pixel cannot reset stillness or drop the Universe
		// steering hand for one frame.
		bool looksLikeHand = HandClassificationPolicy::looksLikeHand(
			sizeFitsHand, armLike, fingerLike, state.inwardActive);

		CalibrationProtocol::HandPositionMode desiredSource =
			CalibrationProtocol::HandPositionMode::ContourCentroid;
		glm::vec2 desiredPosition = state.contourCentroid;
		if (looksLikeHand
			&& settings.handPositionMode != CalibrationProtocol::HandPositionMode::ContourCentroid
			&& hasPalm) {
			desiredSource = CalibrationProtocol::HandPositionMode::PalmCentre;
			desiredPosition = palmCentre;
		}
		if (looksLikeHand
			&& settings.handPositionMode == CalibrationProtocol::HandPositionMode::InwardTip
			&& state.inwardActive) {
			desiredSource = CalibrationProtocol::HandPositionMode::InwardTip;
			desiredPosition = state.inwardPoint;
		}

		// The filters themselves carry a track's whole history, so they are
		// copied forward from last frame's state rather than recreated -
		// recreating them here would throw the history away and start over
		// from raw every single frame. An id seen for the first time has
		// nothing to carry forward and starts fresh, which OneEuroFilter
		// itself treats as "report the first sample exactly" (see
		// shared/OneEuroFilter.h).
		if (isKnown) {
			state.filterX = previous->second.filterX;
			state.filterY = previous->second.filterY;
		}

		// Re-applied every frame rather than only at construction, so a
		// smoothing value nudged at the table takes hold of a track already
		// being followed immediately, the same way the fixed rate this
		// replaced did.
		state.filterX.setParams(settings.positionSmoothingMinCutoff, settings.positionSmoothingBeta);
		state.filterY.setParams(settings.positionSmoothingMinCutoff, settings.positionSmoothingBeta);

		float smoothingDt = isKnown ? std::max(now - previous->second.lastSeenTime, 0.0f) : 0.0f;
		const bool sourceChanged = isKnown
			&& desiredSource != previous->second.positionSource;
		state.positionSource = desiredSource;
		if (sourceChanged) {
			state.positionTransitionFrom = previous->second.smoothedPosition;
			state.positionTransitionStartedAt = now;
			state.positionTransitionActive = true;
		} else if (isKnown && previous->second.positionTransitionActive) {
			state.positionTransitionFrom = previous->second.positionTransitionFrom;
			state.positionTransitionStartedAt = previous->second.positionTransitionStartedAt;
			state.positionTransitionActive = true;
		}

		if (state.positionTransitionActive) {
			const float progress = (now - state.positionTransitionStartedAt)
				/ positionSourceTransitionSeconds;
			const float blend = smoothStep(progress);
			state.smoothedPosition = state.positionTransitionFrom
				+ (desiredPosition - state.positionTransitionFrom) * blend;
			state.filterX.reset(state.smoothedPosition.x);
			state.filterY.reset(state.smoothedPosition.y);
			state.velocity = glm::vec2(0.0f);
			if (progress >= 1.0f) {
				state.positionTransitionActive = false;
			}
		} else {
			state.smoothedPosition.x = state.filterX.filter(desiredPosition.x, smoothingDt);
			state.smoothedPosition.y = state.filterY.filter(desiredPosition.y, smoothingDt);
			state.velocity = glm::vec2(
				state.filterX.getDerivative() / static_cast<float>(roiW),
				state.filterY.getDerivative() / static_cast<float>(roiH));
		}

		// Normalized against the ROI, not the whole frame: the table's edges
		// become 0.0 and 1.0, so Pi B's grid is filled by the table rather
		// than by whatever the sensor sees around it. Still resolution- and
		// sensor-agnostic on Pi B's side (data-model.md, Principle II) - only
		// the rectangle the numbers refer to has changed.
		obj.x = ofClamp((state.smoothedPosition.x - roiX) / static_cast<float>(roiW), 0.0f, 1.0f);
		obj.y = ofClamp((state.smoothedPosition.y - roiY) / static_cast<float>(roiH), 0.0f, 1.0f);
		state.normalizedPosition = glm::vec2(obj.x, obj.y);

		// Outside a source transition, velocity is the filters' own smoothed
		// derivative - pixels per
		// second, since that is the space they filter in - converted into
		// the same ROI-normalized units per second as x/y. No separate
		// smoothing pass is needed here the way the fixed-rate version
		// required: the One-Euro filter already keeps a smoothed derivative
		// of its own to compute its adaptive cutoff from, and this is simply
		// reading it.
		obj.vx = state.velocity.x;
		obj.vy = state.velocity.y;
		obj.age = now - state.firstSeenTime;

		// How long this blob has been holding still, measured against the last
		// position it was seen to move away from rather than frame to frame -
		// a slow drift would otherwise never accumulate into "it moved".
		if (isKnown && !sourceChanged && !state.positionTransitionActive) {
			state.stillReference = previous->second.stillReference;
			state.stillSince = previous->second.stillSince;

			if (glm::distance(state.normalizedPosition, state.stillReference) > settings.stationaryTolerance) {
				state.stillReference = state.normalizedPosition;
				state.stillSince = now;
			}
		} else {
			state.stillReference = state.normalizedPosition;
			state.stillSince = now;
		}

		// Something that has not moved for this long is lying on the table,
		// whatever its shape says. This overrides the shape cues rather than
		// voting with them,
		// because it is the more reliable signal when it fires at all.
		float stillFor = now - state.stillSince;
		bool stationary = settings.stationarySeconds > 0.0f && stillFor >= settings.stationarySeconds;

		obj.type = (looksLikeHand && !stationary) ? ObjectType::Hand : ObjectType::Object;

		// Carried on the track purely so DebugView can show what drove the
		// classification - the label alone gives nothing to calibrate against.
		state.type = obj.type;
		state.solidity = solidity;
		state.borderContact = borderContact;
		state.palmRadiusMM = palmRadiusMM;

		// Always tracked internally, whatever hideSeconds says - so DebugView
		// keeps showing it and so it is recognized as the same id, not a new
		// one, the moment it moves again.
		nextTracks[obj.id] = state;

		// Beyond hideSeconds this stops being pushed to the output list at
		// all: not sent, not drawn as a cursor, not pushing or influencing
		// anything on Pi B. stationarySeconds only relabels a still object;
		// this is what actually makes it disappear from the piece.
		bool hidden = settings.hideSeconds > 0.0f && stillFor >= settings.hideSeconds;

		CalibrationProtocol::TrackDiagnostic diagnostic;
		diagnostic.id = obj.id;
		diagnostic.type = static_cast<int32_t>(obj.type);
		diagnostic.rawX = ofClamp(desiredPosition.x / static_cast<float>(w), 0.0f, 1.0f);
		diagnostic.rawY = ofClamp(desiredPosition.y / static_cast<float>(h), 0.0f, 1.0f);
		diagnostic.smoothedX = ofClamp(state.smoothedPosition.x / static_cast<float>(w), 0.0f, 1.0f);
		diagnostic.smoothedY = ofClamp(state.smoothedPosition.y / static_cast<float>(h), 0.0f, 1.0f);
		diagnostic.vx = state.velocity.x;
		diagnostic.vy = state.velocity.y;
		diagnostic.palmRadiusMM = palmRadiusMM;
		diagnostic.solidity = solidity;
		diagnostic.borderContact = borderContact;
		diagnostic.stillForSeconds = stillFor;
		diagnostic.hidden = hidden;
		nextDiagnostics.push_back(diagnostic);

		if (!hidden) {
			objects.push_back(obj);
		}
	}

	tracks = nextTracks;
	trackDiagnostics = std::move(nextDiagnostics);

	return objects;
}

bool ObjectDetector::buildComponentMask(const ofPolyline & outline,
	const ofRectangle & bounds, cv::Mat & component, cv::Rect & region) const {
	if (mask.empty() || outline.getVertices().size() < 3) {
		return false;
	}

	const PaddedComponentRegion::Region padded = PaddedComponentRegion::fromBounds(
		bounds.getLeft(), bounds.getTop(), bounds.getRight(), bounds.getBottom(),
		mask.cols, mask.rows);
	if (!padded.isValid()) {
		return false;
	}
	region = cv::Rect(padded.x, padded.y, padded.width, padded.height);

	std::vector<cv::Point> polygon;
	polygon.reserve(outline.getVertices().size());
	for (const glm::vec3 & vertex : outline.getVertices()) {
		polygon.emplace_back(
			static_cast<int>(std::lround(vertex.x)) - region.x,
			static_cast<int>(std::lround(vertex.y)) - region.y);
	}

	component = cv::Mat::zeros(region.height, region.width, CV_8UC1);
	const std::vector<std::vector<cv::Point>> polygons { polygon };
	cv::fillPoly(component, polygons, cv::Scalar(255));
	cv::bitwise_and(component, mask(region), component);
	return cv::countNonZero(component) > 0;
}

bool ObjectDetector::measureBlob(const ofFloatPixels & depth,
	const cv::Mat & component, const cv::Rect & region,
	float & meanDepthMM, float & meanSurfaceDepthMM, int & filledPixels) const {

	const int w = depth.getWidth();

	const float * src = depth.getData();
	double depthSum = 0.0;
	double surfaceDepthSum = 0.0;
	int surfaceDepthCount = 0;
	int count = 0;

	for (int localY = 0; localY < component.rows; ++localY) {
		const unsigned char * componentRow = component.ptr<unsigned char>(localY);
		const int y = region.y + localY;
		const float * depthRow = src + static_cast<size_t>(y) * w;
		for (int localX = 0; localX < component.cols; ++localX) {
			if (componentRow[localX] == 0) {
				continue;
			}
			const int x = region.x + localX;
			depthSum += depthRow[x];
			const float surfaceDepth = tableSurfaceMask.referenceDepthAt(x, y);
			if (surfaceDepth > 0.0f) {
				surfaceDepthSum += surfaceDepth;
				surfaceDepthCount++;
			}
			count++;
		}
	}

	if (count == 0) {
		return false;
	}

	meanDepthMM = static_cast<float>(depthSum / count);
	meanSurfaceDepthMM = surfaceDepthCount > 0
		? static_cast<float>(surfaceDepthSum / surfaceDepthCount) : 0.0f;
	filledPixels = count;
	return true;
}

float ObjectDetector::measureSolidity(const ofPolyline & outline) const {
	const std::vector<glm::vec3> & vertices = outline.getVertices();

	// A hull needs a real polygon; fewer than three points has no area and no
	// meaningful shape to describe.
	if (vertices.size() < 3) {
		return 0.0f;
	}

	// Converted rather than taken from ofxCv's own contour accessor, because
	// the polyline is OF core API and stable across addon versions, while the
	// contour accessor's spelling is not.
	std::vector<cv::Point> points;
	points.reserve(vertices.size());
	for (const auto & vertex : vertices) {
		points.emplace_back(static_cast<int>(vertex.x), static_cast<int>(vertex.y));
	}

	std::vector<cv::Point> hull;
	cv::convexHull(points, hull);
	if (hull.size() < 3) {
		return 0.0f;
	}

	double hullArea = cv::contourArea(hull);
	if (hullArea <= 0.0) {
		return 0.0f;
	}

	// The outline can never exceed its own hull, so this is naturally 0..1;
	// clamped anyway against rounding in the integer conversion above.
	return ofClamp(static_cast<float>(cv::contourArea(points) / hullArea), 0.0f, 1.0f);
}

float ObjectDetector::measureBorderContact(const cv::Mat & component,
	const cv::Rect & region, const ofRectangle & roi) const {
	if (component.empty() || roiMask.empty()) {
		return 0.0f;
	}

	int minimumX = roiMask.cols;
	int maximumX = -1;
	int minimumY = roiMask.rows;
	int maximumY = -1;
	for (int localY = 0; localY < component.rows; ++localY) {
		const unsigned char * componentRow = component.ptr<unsigned char>(localY);
		const int y = region.y + localY;
		const unsigned char * roiRow = roiMask.ptr<unsigned char>(y);
		for (int localX = 0; localX < component.cols; ++localX) {
			if (componentRow[localX] == 0) continue;
			const int x = region.x + localX;
			if (roiRow[x] == 0) continue;

			// Eight-neighbour membership follows straight and curved portions of
			// the same binary ROI. A filled pixel with any excluded neighbour is
			// where the blob has been clipped by the physical table boundary.
			bool atBoundary = false;
			for (int dy = -1; dy <= 1 && !atBoundary; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					if (dx == 0 && dy == 0) continue;
					const int neighbourX = x + dx;
					const int neighbourY = y + dy;
					if (neighbourX < 0 || neighbourY < 0
						|| neighbourX >= roiMask.cols || neighbourY >= roiMask.rows
						|| roiMask.ptr<unsigned char>(neighbourY)[neighbourX] == 0) {
						atBoundary = true;
						break;
					}
				}
			}
			if (!atBoundary) continue;

			minimumX = std::min(minimumX, x);
			maximumX = std::max(maximumX, x);
			minimumY = std::min(minimumY, y);
			maximumY = std::max(maximumY, y);
		}
	}

	if (maximumX < minimumX || maximumY < minimumY) return 0.0f;
	const float horizontalSpan = static_cast<float>(maximumX - minimumX + 1)
		/ std::max(1.0f, roi.width);
	const float verticalSpan = static_cast<float>(maximumY - minimumY + 1)
		/ std::max(1.0f, roi.height);
	return ofClamp(std::max(horizontalSpan, verticalSpan), 0.0f, 1.0f);
}

bool ObjectDetector::findPalmCentre(const cv::Mat & component,
	const cv::Rect & region, glm::vec2 & palmCentre,
	float & palmRadiusPixels) const {
	if (component.empty()) {
		return false;
	}

	// The distance transform gives every filled pixel its distance to the
	// nearest empty one, so the maximum sits at the widest point of the blob -
	// the palm, for a hand with fingers and a forearm attached. Run on a copy
	// of just this blob's box, both to keep the cost proportional to the blob
	// and because distanceTransform writes a full-size output.
	cv::Mat distance;
	cv::distanceTransform(component, distance, cv::DIST_L2, 3);

	double maxDistance = 0.0;
	cv::Point maxLocation;
	cv::minMaxLoc(distance, nullptr, &maxDistance, nullptr, &maxLocation);

	// Zero means the box contained no filled pixels at all.
	if (maxDistance <= 0.0) {
		return false;
	}

	// Back from box-local to depth-frame coordinates.
	palmCentre = glm::vec2(region.x + maxLocation.x, region.y + maxLocation.y);

	// The distance itself is the radius of the largest circle that fits
	// inside the blob - for a hand that is the palm, for a torso it is half
	// the chest. Reported in pixels here; the caller converts it to
	// millimetres using the blob's own distance.
	palmRadiusPixels = static_cast<float>(maxDistance);
	return true;
}

InwardInteractionPointSelector::Result ObjectDetector::findInwardPoint(
	const cv::Mat & component, const cv::Rect & region,
	float palmRadiusPixels) const {
	InwardInteractionPointSelector::Result result;
	if (component.empty() || roiMask.empty()) {
		return result;
	}

	const cv::Mat localRoi = roiMask(region);
	const InwardInteractionPointSelector::MaskView componentView {
		component.ptr<unsigned char>(), component.cols, component.rows,
		static_cast<int>(component.step)
	};
	const InwardInteractionPointSelector::MaskView roiView {
		localRoi.ptr<unsigned char>(), localRoi.cols, localRoi.rows,
		static_cast<int>(localRoi.step)
	};
	result = InwardInteractionPointSelector::select(componentView, roiView,
		settings.inwardTipCapFraction,
		InwardInteractionPointSelector::minimumPenetrationForElongatedReach(
			palmRadiusPixels));
	if (result.valid) {
		result.x += static_cast<float>(region.x);
		result.y += static_cast<float>(region.y);
		result.entryX += static_cast<float>(region.x);
		result.entryY += static_cast<float>(region.y);
	}
	return result;
}
