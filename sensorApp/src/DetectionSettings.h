#pragma once

#include "ofMain.h"
#include "CalibrationProtocol.h"
#include <string>

// Which group of values the calibration window's arrow keys act on. There are
// more tunables than there are sensible key bindings, so the arrow keys are
// re-targeted instead of inventing modifier combinations - the depth window
// and the ROI are both two-value pairs that map naturally onto four arrows.
enum class CalibrationTarget {
	DepthWindow,
	TableSurface,
	RoiPosition,
	RoiSize,
	Noise,
	Smoothing,
	Classification,
	HandSize,
	HandPosition,
	Stationary
};

// Cycles round-robin, so one key can reach every target.
CalibrationTarget nextCalibrationTarget(CalibrationTarget current);

// Label plus the meaning of the arrow keys for that target, for the
// calibration window's footer.
std::string calibrationTargetLabel(CalibrationTarget target);

// Every value ObjectDetector needs calibrating on site, in one place and
// loadable from bin/data/settings.json. These were compile-time constants
// before, which meant a full rebuild-and-restart on the Pi for every
// adjustment - unworkable for values that only have a correct answer once the
// sensor is mounted over the actual table.
//
// The defaults below are exactly the values that used to be hard-coded, so a
// missing or unreadable settings file behaves identically to before rather
// than falling back to zeroes.
struct DetectionSettings {
	// Depth limits (mm): nearMM is always the camera-side plane. farMM is the
	// table-side plane until a captured table surface replaces it with a local
	// per-pixel reference. Both depend on how high the sensor is mounted.
	float nearMM = 500.0f;
	float farMM = 1000.0f;

	// Minimum distance in front of the captured empty-table surface before a
	// pixel may enter the contour mask. This absorbs Kinect depth noise while
	// remaining small enough to retain a hand resting close to the table.
	// It has no effect until a table-surface mask has been captured or loaded.
	float tableSurfaceClearanceMM = 15.0f;

	// Blob size limits (px radius): reject noise specks below, and
	// oversized regions (a person leaning in, a lighting artefact) above.
	float minAreaRadius = 20.0f;
	float maxAreaRadius = 200.0f;

	// Tracker: how far a blob may move between frames and still count as the
	// same object (px), and how many frames an unmatched track survives
	// before its id is retired.
	float trackerMaximumDistance = 32.0f;
	int trackerPersistence = 15;

	// Region of interest: the bounding rectangle of the physical table in the
	// depth frame, as a fraction of the frame (0.0-1.0). Shared/TableShape.h
	// cuts it to the table's rounded outline. Everything outside that shape is
	// masked away, while detected positions remain normalized against the
	// bounding rectangle - so Pi B keeps the established 0-1 coordinate space
	// and simply receives no points from beyond the curved table edge.
	//
	// Stored normalized rather than in pixels so the value stays meaningful
	// if the depth resolution ever changes, matching how DetectedObject is
	// already defined. Defaults to the full frame, which reproduces the
	// previous behaviour exactly.
	float roiX = 0.0f;
	float roiY = 0.0f;
	float roiWidth = 1.0f;
	float roiHeight = 1.0f;

	// Mask clean-up, in pixels of radius. Opening (erode then dilate) removes
	// speckle smaller than its radius; closing (dilate then erode) fills
	// holes and joins fragments smaller than its radius, which is what turns
	// a hand that broke into several patches back into one blob with one
	// stable tracker id. Either at 0 skips that step entirely, which is also
	// how to A/B compare against the un-cleaned mask.
	//
	// Closing is the larger of the two by default: depth dropout inside a
	// blob is the more common problem here than isolated speckle, and an
	// oversized opening radius eats fingers.
	int morphOpenRadius = 1;
	int morphCloseRadius = 2;

	// Per-blob interaction-position smoothing, run through a One-Euro filter (see
	// shared/OneEuroFilter.h) rather than a fixed-rate average: blob edges
	// flicker frame to frame even when nothing moves, and that jitter
	// otherwise lands directly in Pi B's influence field, but a fixed rate
	// smooths a resting hand and a moving one by the same amount, which
	// means it is always wrong about one of the two.
	//
	// positionSmoothingMinCutoff (Hz) is how hard a motionless blob is
	// smoothed - lower is steadier but adds lag once it starts moving.
	// positionSmoothingBeta is how far a fast-moving blob's cutoff is
	// allowed to rise above that floor - higher follows a fast hand more
	// tightly, at the cost of more visible jitter while it is slow.
	float positionSmoothingMinCutoff = 1.2f;
	float positionSmoothingBeta = 0.4f;

	// Hand/object classification. A hand is attached to an arm, so its blob
	// runs off the edge of the region of interest; a cup standing on the table
	// does not. That single cue does most of the work, and needs no threshold.
	//
	// Solidity is the backup cue: the blob's own area divided by the area of
	// the convex hull wrapped around it. A compact object fills its hull
	// almost completely (near 1.0); spread fingers leave large notches, which
	// pushes the ratio down. Blobs at or below this value count as a hand even
	// without touching the border - so a hand held clear of the table edge is
	// still recognized.
	float handSolidityThreshold = 0.80f;

	// Size window a blob must fall inside to be called a hand, measured as the
	// radius of the largest circle that fits inside it - the palm, for a hand
	// with fingers and a forearm attached. In real millimetres, converted from
	// pixels using the object's own distance and the camera's focal length,
	// which is what makes these numbers something to measure with a ruler
	// rather than guess: an adult palm is roughly 40-50 mm in radius, a torso
	// well over 120 mm.
	//
	// This replaced an earlier pair of thresholds expressed as a share of the
	// region of interest. Those silently depended on how high the object was
	// held - the same hand covers fewer pixels the closer it comes to the
	// sensor - so a threshold tuned with a hand on the table was wrong for the
	// same hand lifted.
	//
	// Size is a veto, not a hint: a body silhouette also has low solidity
	// (head and arms cut notches just like fingers do) and also reaches the
	// border, so both shape cues say "hand" for a whole person. Absolute size
	// is the only thing that separates them.
	float handMinPalmRadiusMM = 25.0f;
	float handMaxPalmRadiusMM = 90.0f;

	// How long a blob must hold still before it is treated as something lying
	// on the table rather than someone interacting, and how far it may drift
	// (as a share of the region of interest) while still counting as still.
	// Independent of shape entirely, which is what makes it a useful second
	// opinion. 0 seconds disables the rule.
	//
	// Short on purpose: an object set on the table should stop influencing the
	// piece quickly rather than after a visitor could plausibly have wandered
	// off and come back. The trade is that a hand held motionless for the same
	// five seconds - thinking mid-gesture, say - is relabelled Object too.
	// CruiseNavigator may retain only an already-engaged driver's id through
	// that relabel, but this sensor-side rule remains authoritative for new
	// interactions and the longer hideSeconds rule below.
	float stationarySeconds = 5.0f;
	float stationaryTolerance = 0.02f;

	// A second, longer stillness threshold, measured against the same
	// still-since clock as stationarySeconds above rather than a separate
	// one. Below it, a still object is only relabelled (stationarySeconds);
	// at or beyond it, the object is dropped from update()'s returned list
	// entirely - not sent, not drawn as a cursor, not pushing or influencing
	// anything on Pi B - which is what actually makes something lying on the
	// table disappear from the piece rather than merely stop steering it.
	// ObjectDetector keeps tracking it internally regardless (so DebugView
	// can still show it, and so it resumes being sent the moment it moves
	// again without being misread as a brand-new object). 0 disables the
	// rule, same convention as stationarySeconds.
	float hideSeconds = 10.0f;

	// How much of either ROI axis a blob may span where it crosses the straight
	// or curved table boundary. An arm reaching over the table border crosses
	// it narrowly; a person leaning in crosses it wide. Without this, "touches
	// the border" fires for anything entering the frame at all, which is why
	// the border cue on its own is not enough.
	float handMaxBorderWidth = 0.25f;

	// Which raw point becomes a hand's interaction position. InwardTip uses the
	// stable distal cap of a narrow border-connected hand, placing interaction
	// at the end actually reaching into the table. It falls back to the palm,
	// then centroid, whenever that geometry is absent or ambiguous.
	CalibrationProtocol::HandPositionMode handPositionMode =
		CalibrationProtocol::HandPositionMode::InwardTip;

	// Share of the largest entry-to-tip geodesic distance included in the
	// distal cap. A band rather than one maximum pixel keeps an open hand from
	// jumping between fingertips as their contour changes by a pixel.
	float inwardTipCapFraction = 0.15f;

	// The ROI's bounding rectangle in pixels for a given frame size,
	// integer-aligned and clamped into the frame. Kept here so the detector and
	// calibration window cannot drift apart on the rounding.
	ofRectangle roiInPixels(int frameWidth, int frameHeight) const;

	// Reads path (relative to bin/data/). Returns false if the file is
	// missing or unparseable; in that case this object is left untouched, so
	// the caller keeps whatever values it already had.
	bool load(const std::string & path);

	// Writes the current values back out, so values dialled in with the
	// calibration window's keys survive a restart.
	bool save(const std::string & path) const;

	// Forces the invariants the detector relies on: a non-inverted depth
	// window, positive radii, min below max, at least one frame of tracker
	// persistence. Applied after loading and after any runtime change, so a
	// hand-edited file cannot put the detector into a state that silently
	// detects nothing.
	void sanitize();

	// One-line summary for the calibration window's footer.
	std::string toDisplayString() const;
};
