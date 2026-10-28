#pragma once

#include "ofxCv.h"
#include "KinectInput.h"
#include "DetectionSettings.h"
#include "DetectedObject.h"
#include "OneEuroFilter.h"
#include "CalibrationProtocol.h"
#include "TableSurfaceMask.h"
#include "InwardInteractionPointSelector.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>

// Pi A: turns KinectInput's raw depth stream into a live DetectedObject
// stream. See research.md item 2 for the approach (depth-thresholded mask +
// ofxCv::ContourFinder's built-in tracker, not a hand-written one) and why
// (no OpenCL/CUDA on the Pi 4 GPU - constitution Principle V).
//
// All tunable values live in DetectionSettings, loaded from a file at setup
// and re-loadable at runtime, because they can only be calibrated once the
// sensor is mounted over the real table.
//
// VERIFY ON THE PI: the ofxCv API spellings below (ContourFinder,
// getTracker(), setPersistence, setMaximumDistance, getLabel, getCentroid)
// are from ofxCv's long-standing interface but drift between addon versions
// - confirm against the copy actually installed under OF_ROOT/addons/
// before relying on the exact spellings (cannot be checked from the Mac).
class ObjectDetector {
public:
	// settingsPath is relative to bin/data/. A missing file is not an error:
	// DetectionSettings' built-in defaults are used and reported.
	void setup(const std::string & settingsPath = "settings.json");

	std::vector<DetectedObject> update(const KinectInput & kinect);

	const DetectionSettings & getSettings() const { return settings; }

	// Replaces the values in force and pushes them into the contour finder
	// immediately - the next update() already detects with them. Sanitized
	// first, so callers may hand over out-of-range values freely.
	void setSettings(const DetectionSettings & newSettings);

	// Re-reads the settings file written since startup, so the file can be
	// edited over SSH while the calibration window stays open.
	bool reloadSettings();

	// Writes the values currently in force back to the same file.
	bool saveSettings() const;

	// The empty-table reference is captured and persisted separately from the
	// scalar settings because it contains one metric depth per sensor pixel.
	// Capture happens over the next TableSurfaceMask::captureFrameCount fresh
	// frames; the old active reference remains in force until the new one has
	// been completed and saved successfully.
	bool requestTableSurfaceCapture();
	bool clearTableSurfaceMask();
	bool reloadCalibration();
	TableSurfaceMask::Status getTableSurfaceMaskStatus() const {
		return tableSurfaceMask.getStatus();
	}
	const std::string & getTableSurfaceMaskError() const {
		return tableSurfaceMask.getLastError();
	}

	// Read-only access to the two intermediate stages, for DebugView to draw
	// (sensorApp/src/DebugView.h) - the mask is the filtered foreground image
	// the contour finder actually runs on, and the contour finder carries the
	// per-blob polylines and tracker labels that update() reduces to a single
	// normalized interaction position per DetectedObject. Detection behaviour is
	// unaffected by whether anyone calls these.
	//
	// The contour finder is handed out non-const because ofxCv's own
	// accessors (getPolyline, getCentroid, getLabel) are not consistently
	// const-qualified across addon versions.
	const cv::Mat & getMask() const { return mask; }
	ofxCv::ContourFinder & getContourFinder() { return contourFinder; }

	// What a single tracked object carries between frames. Everything here
	// needs history, which a single frame's contour cannot provide: the
	// smoothed position, the velocity derived from it, and the moment the id
	// first appeared.
	struct TrackState {
		// One One-Euro filter per axis (see shared/OneEuroFilter.h and
		// DetectionSettings::positionSmoothingMinCutoff/Beta), run on the
		// depth-frame-pixel interaction point - smoothedPosition below is just
		// filterX/filterY's own current value, kept as a plain glm::vec2 too
		// since that is what DebugView already draws and it costs nothing to
		// keep both in step.
		OneEuroFilter filterX;
		OneEuroFilter filterY;

		// Depth-frame pixels. What actually gets normalized into
		// DetectedObject, as opposed to the raw contour centroid the contour
		// finder reports - DebugView draws both so the effect of the
		// smoothing filter is visible while tuning it.
		glm::vec2 smoothedPosition { 0.0f, 0.0f };

		// Raw candidates retained for diagnostics and source hysteresis. A valid
		// inward point may survive a short broken-border gap so Kinect dropout
		// cannot flip the output between palm and tip every other frame.
		glm::vec2 contourCentroid { 0.0f, 0.0f };
		glm::vec2 palmCentre { 0.0f, 0.0f };
		glm::vec2 inwardPoint { 0.0f, 0.0f };
		glm::vec2 entryPoint { 0.0f, 0.0f };
		bool hasPalmCentre = false;
		bool inwardCandidateValid = false;
		bool inwardActive = false;
		float inwardConfidence = 0.0f;
		float inwardCandidateSince = -1.0f;
		float lastInwardCandidateTime = -1.0f;

		// A source change is eased independently of real motion. Resetting the
		// normal filters throughout that transition prevents it from producing a
		// synthetic velocity on the production object stream.
		CalibrationProtocol::HandPositionMode positionSource =
			CalibrationProtocol::HandPositionMode::ContourCentroid;
		glm::vec2 positionTransitionFrom { 0.0f, 0.0f };
		float positionTransitionStartedAt = -1.0f;
		bool positionTransitionActive = false;

		// Previous frame's normalized position, and the velocity read
		// straight off filterX/filterY's own derivative (pixels/second)
		// and converted to ROI units/second - a second, separately-smoothed
		// EMA of its own is no longer needed, since the One-Euro filter
		// already keeps a smoothed derivative for its adaptive cutoff.
		glm::vec2 normalizedPosition { 0.0f, 0.0f };
		glm::vec2 velocity { 0.0f, 0.0f };

		float firstSeenTime = 0.0f;
		float lastSeenTime = 0.0f;

		// Classification result and the two measurements behind it, kept so
		// DebugView can show *why* a blob was classified the way it was -
		// a bare Hand/Object label gives nothing to calibrate against.
		ObjectType type = ObjectType::Unknown;
		float solidity = 0.0f;
		float borderContact = 0.0f;
		float palmRadiusMM = 0.0f;

		// Where the object was when it last counted as having moved, and
		// since when. Together these are how long it has been holding still,
		// which is what separates something lying on the table from someone
		// interacting with it - no shape analysis can do that.
		glm::vec2 stillReference { 0.0f, 0.0f };
		float stillSince = 0.0f;
	};

	const std::unordered_map<int, TrackState> & getTracks() const { return tracks; }
	const std::vector<CalibrationProtocol::TrackDiagnostic> & getTrackDiagnostics() const {
		return trackDiagnostics;
	}

private:
	// Pushes settings into the contour finder and its tracker, and rebuilds
	// the morphology kernels. Everything else (the depth window, the ROI, the
	// smoothing factor) is read straight from settings each frame, so this
	// only exists for values that are cached rather than re-derived.
	void applySettings();

	// Elliptical structuring element of the given pixel radius, or an empty
	// Mat for radius 0, which update() treats as "skip this step".
	cv::Mat buildKernel(int radius) const;

	// Rebuilds the binary rounded table outline only when the depth dimensions
	// or calibrated ROI bounds change. The shape is static during normal show
	// frames, so there is no reason to rasterize it at Kinect frame rate.
	void ensureRoiMask(int frameWidth, int frameHeight,
		int roiX, int roiY, int roiWidth, int roiHeight);

	// Rasterizes exactly one contour and intersects it with the detector mask.
	// All measurements below share this component-local image so overlapping
	// bounding boxes cannot contaminate each other's position or size.
	bool buildComponentMask(const ofPolyline & outline, const ofRectangle & bounds,
		cv::Mat & component, cv::Rect & region) const;

	// Mean depth (mm) and filled pixel count of one exact component. Returns
	// false when it holds no valid pixels at all, in which case the caller
	// leaves z/area at zero.
	bool measureBlob(const ofFloatPixels & depth, const cv::Mat & component,
		const cv::Rect & region,
		float & meanDepthMM, float & meanSurfaceDepthMM,
		int & filledPixels) const;

	// Blob outline area divided by the area of the convex hull around it -
	// 1.0 for a shape with no notches, lower the more the outline caves in.
	// Returns 0.0 for a degenerate outline that has no hull area.
	float measureSolidity(const ofPolyline & outline) const;

	// Widest normalized axis span where this blob meets the straight or curved
	// ROI boundary, 0.0 if it does not reach the edge. An arm crosses the table
	// border narrowly, a person leaning in crosses it wide - the plain "does
	// it touch" test cannot tell those apart.
	float measureBorderContact(const cv::Mat & component, const cv::Rect & region,
		const ofRectangle & roi) const;

	// Point inside the blob furthest from any of its edges - the palm, for a
	// hand-shaped blob - together with that distance, which is the radius of
	// the largest circle fitting inside the blob. Returns false if the
	// component contains no filled pixels, in which case the caller keeps
	// the contour centroid and treats the radius as unknown.
	bool findPalmCentre(const cv::Mat & component, const cv::Rect & region,
		glm::vec2 & palmCentre, float & palmRadiusPixels) const;

	// Finds the stable distal cap reached by walking inside the component away
	// from its single table-edge entry. The reach must also be substantially
	// longer than its palm diameter, which both rejects round edge objects and
	// supplies the elongated-edge classification cue. Invalid results fall
	// back to palm or centroid rather than guessing between ambiguous entries.
	InwardInteractionPointSelector::Result findInwardPoint(const cv::Mat & component,
		const cv::Rect & region, float palmRadiusPixels) const;

	DetectionSettings settings;
	std::string settingsPath;
	TableSurfaceMask tableSurfaceMask;

	ofxCv::ContourFinder contourFinder;
	cv::Mat mask;

	// Binary physical table outline in depth-frame coordinates. It is applied
	// before and after morphology: the first pass avoids threshold work beyond
	// the table, the second prevents clean-up from growing into a cut corner.
	cv::Mat roiMask;
	int cachedRoiX = -1;
	int cachedRoiY = -1;
	int cachedRoiWidth = -1;
	int cachedRoiHeight = -1;

	// Structuring elements for the mask clean-up. Built once per settings
	// change rather than per frame, since they only depend on the radii.
	// Empty means the corresponding step is switched off (radius 0).
	cv::Mat openKernel;
	cv::Mat closeKernel;

	// Carried between frames, keyed by tracker id. Rebuilt each frame from
	// the ids actually present, so an object leaving the frame drops out on
	// its own and the map cannot grow without bound over a long show.
	std::unordered_map<int, TrackState> tracks;

	// Complete diagnostics for the last fresh detector frame, including tracks
	// hidden from the production DetectedObject stream. Pi B only draws these
	// in the explicit calibration profile and never feeds them into a visual.
	std::vector<CalibrationProtocol::TrackDiagnostic> trackDiagnostics;

	// Protects the temporal detector state from being advanced twice with the
	// same depth image, even if a future caller forgets to inspect
	// KinectInput::FrameStatus. A new connection generation also resets the
	// application's filter/age/stillness state before its first frame.
	std::uint64_t lastProcessedDepthFrameSequence = 0;
	std::uint64_t lastConnectionGeneration = 0;
};
