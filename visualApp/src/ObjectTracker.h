#pragma once

#include "DetectedObject.h"
#include "OneEuroFilter.h"
#include "ofMain.h"
#include <functional>
#include <string>
#include <vector>

// A screen's short per-id memory of the DetectedObject stream - what lets a
// hand this frame be recognized as the same hand as last frame, rather than
// every screen reading each frame on its own. Three screens each wrote this
// twice over (CruiseNavigator::Track, InvaderVisual::Track, and PixelPush,
// which had no memory of the stream at all and read raw per-frame positions
// straight into its displacement field) with the same handful of ideas -
// per-id smoothing, an anchor-based stillness test, ids aged out after a
// grace window - reimplemented three slightly different ways. One class now,
// used by all three, so a fix to any of that logic only has to happen once.
//
// Two things it does NOT decide, on purpose: what "usable" means beyond
// recent-and-not-inert (a strict hands-only gate, a hand-preferred-but-
// anything-moving-will-do fallback, or no gate at all - PixelPush answers
// every object with a push, not just hands), and what a screen does with the
// positions once it has them (steer, push, average). Those stay each
// screen's own business, the way they were before this was pulled out.
class ObjectTracker {
public:
	// One tracked blob, remembered across frames.
	struct Track {
		int id = 0;

		// Position, One-Euro filtered (see minCutoff/beta below) - what a
		// screen actually steers or pushes by.
		float x = 0.5f;
		float y = 0.5f;

		// Height above the table (DetectedObject::z convention), filtered the
		// same way as x/y.
		float z = 0.0f;

		// The same two axes, unfiltered. Stillness is measured against these,
		// not against the filtered x/y: a filter's whole job is to lag behind
		// a sudden change, so measuring stillness against its output would
		// make a hand that just started moving still read as motionless for
		// a few frames - exactly backwards from what the stillness test is
		// for.
		float rawX = 0.5f;
		float rawY = 0.5f;

		ObjectType type = ObjectType::Unknown;

		// Where this track was last seen to genuinely move, and how long ago
		// that was. Compared against an anchor rather than against the
		// previous frame, so a hand creeping slowly across the table still
		// counts as moving instead of reading as stillness one small step at
		// a time.
		float anchorX = 0.5f;
		float anchorY = 0.5f;
		float stillSeconds = 0.0f;

		float seenSeconds = 0.0f; // how long this id has been tracked
		float unseenSeconds = 0.0f; // how long it has been missing from the stream

		// The filters behind x/y/z. Left accessible rather than hidden behind
		// ObjectTracker, matching every other Track-like struct in this
		// project (CruiseNavigator, InvaderVisual before this) - a caller has
		// no reason to reach into these, but nothing is protected by stopping
		// it from being able to.
		OneEuroFilter filterX;
		OneEuroFilter filterY;
		OneEuroFilter filterZ;
	};

	void reset();

	// Folds this frame's objects into the tracks, ages the ones that did not
	// arrive, and forgets the ones gone longer than trackForgetSeconds.
	void update(const std::vector<DetectedObject> & detectedObjects, float deltaTime);

	const std::vector<Track> & getTracks() const { return tracks; }
	const Track * find(int id) const;

	// Present recently enough (unseenSeconds <= holdSeconds) and not sitting
	// still past inertSeconds. Says nothing about type - a caller wanting
	// hands only, or hands preferred, applies that on top.
	bool isUsable(const Track & track) const;

	// Keeps or hands over a single exclusive "driving" track and returns it,
	// or nullptr when nobody currently qualifies - the logic behind
	// CruiseNavigator's flight (one hand flies it, not an average of
	// everything on the table, which is what made the view lurch whenever a
	// second hand appeared or a contour split for a few frames) generalized
	// so InvaderVisual can be steered the same way instead of averaging.
	//
	// typeAllowed is the caller's extra gate on top of isUsable(), evaluated
	// fresh against every candidate each call - a hard hands-only rule, a
	// soft hands-preferred-this-frame rule computed once by the caller and
	// captured in the lambda, or "true" for no gate at all.
	//
	// The track already driving keeps driving for as long as it stays usable
	// and allowed; once it stops qualifying, nothing is handed over until it
	// has been gone for handoverSeconds (so two people at a table cannot
	// steal control back and forth every time one of them reaches further
	// than the other), and the replacement is whichever qualifying track has
	// been seen longest (first come, first driven).
	const Track * chooseExclusive(float deltaTime, const std::function<bool(const Track &)> & typeAllowed);

	// The currently-driving track's id, or -1. Stays put through a
	// hold/handover gap - see isDriving() for whether it is actually driving
	// this frame or only still remembered as the last one who was.
	int getPrimaryId() const { return primaryId; }

	// Whether chooseExclusive() actually returned a track on its last call -
	// not the same as getPrimaryId() >= 0, which stays set through a
	// hold/handover gap and through the track having simply gone still. A
	// "you are here" cursor, or anything else asking "is somebody driving
	// this right now", needs this rather than the id alone.
	bool isDriving() const { return primaryPresent; }

	int getSeenCount() const { return seenCount; }
	int getUsableCount() const { return usableCount; }

	// Tuning. Public rather than constructor arguments because each screen
	// that owns a tracker sets a different subset of these to its own
	// values, the same way the private constants they replace used to be
	// listed one per screen - see each field's own comment for what used to
	// set it and why.

	// One-Euro filter parameters shared by every axis of every track (see
	// shared/OneEuroFilter.h) - minCutoff how hard a motionless track is
	// smoothed, beta how far a fast-moving one's cutoff is allowed to rise
	// above that floor. Re-applied every update() rather than only at
	// construction, so they stay adjustable without rebuilding tracks.
	float minCutoff = 1.0f;
	float beta = 0.6f;

	// How far a track has to move from its anchor to count as having moved,
	// in the same 0-1 units as x/y. Above the jitter a still object's
	// centroid shows, well below what a hand does even while its owner
	// stands still.
	float moveEpsilon = 0.02f;

	// How long something may sit still before it stops counting as usable,
	// whether it moved before going still or not - only the last
	// inertSeconds of a track's history count.
	float inertSeconds = 5.0f;

	// How long a track that stops being reported still counts as present -
	// bridges the gap the detector or the network drops a blob for a frame
	// or two all the time (a hand crossing the ROI edge, a contour briefly
	// under the area threshold). While held, x/y are also dead-reckoned
	// forward from the track's last velocity (see update()), so a cursor
	// drawn straight from getTracks() keeps drifting through the gap instead
	// of freezing until the next real sample - this is what holdSeconds is
	// for, not only what counts as "still present".
	float holdSeconds = 0.8f;

	// How long after that a track is forgotten entirely, so ids that will
	// never come back do not accumulate.
	float trackForgetSeconds = 2.5f;

	// chooseExclusive() only: how long the driving track has to be
	// unqualified before another may take over.
	float handoverSeconds = 2.0f;

private:
	std::vector<Track> tracks;

	int primaryId = -1;
	float primaryLostSeconds = 0.0f;
	bool primaryPresent = false;

	int seenCount = 0;
	int usableCount = 0;
};
