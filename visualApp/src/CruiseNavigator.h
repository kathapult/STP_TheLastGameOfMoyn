#pragma once

#include "DetectedObject.h"
#include "CruiseSpeedEnvelope.h"
#include "ObjectTracker.h"
#include "SessionDragSteering.h"
#include "SpaceNavigator.h"
#include "TwoHandSpeedBoost.h"
#include "ofMain.h"
#include <string>
#include <vector>

// How the space screen is flown: the flight never stops, and the hands steer
// it and, together, speed it up.
//
// It is already happening when a visitor walks up, and their hands only decide
// where it goes. So it needs a world with no end to it, which is what
// wantsEndlessWorld() tells the visual.
//
// Steering reads like dragging an online map. The first controlling hand
// position becomes this visitor session's fixed anchor; pulling away draws a
// continuous physical-space vector. Its direction selects any heading around
// the full circle and its length controls how decisively the ship turns towards
// that heading. Holding the pull keeps that command without requiring repeated
// waving while Pi A continues to publish the contour; its eventual stationary
// hide still ends an abandoned gesture. Returning to the anchor restores
// straight flight. A deadzone with radial hysteresis keeps ordinary tracker
// jitter neutral without reducing the vector to a handful of fixed directions.
// A small angular hysteresis stabilizes a held direction, and the last deliberate
// heading is retained while the hand returns through the short-vector neutral
// band. Whatever vector is drawn, the flight only turns towards it at a bounded
// rate (maxYawRate), so a handover or full reversal arrives as a turn rather
// than a camera jump.
//
// One hand flies it, not the average of everything on the table. Averaging is
// what made the view lurch: a second hand appearing, or a contour splitting in
// two for a few frames, moved the average by half the distance between them and
// the camera followed. So this keeps a short memory of the stream, picks the
// hand that has been there longest, and stays with it - through dropped frames,
// and for a couple of seconds after it leaves - before letting anybody else
// have it.
//
// The same hand's height sets the cruising speed, continuously rather than
// in zones: nearer the table is moderately faster, nearer the sensor is slower.
// CruiseSpeedEnvelope smooths and bounds that response, so flying fast into a
// turn remains one motion without ordinary height variation causing a flyby.
//
// A second, separate gesture needs both hands at once: pulling them apart
// raises the cruising speed further, for as long as they are visibly moving
// apart, and lets go the moment they stop - a repeatable push rather than a
// switch, so a visitor speeds the flight up by doing the gesture again rather
// than by holding a pose. It is read from the pair's frame-rate-independent
// planar velocities and only after the same two track ids persist; height is
// excluded because its normalization is physically different. The result
// stacks with height but is capped by the same speed envelope.
class CruiseNavigator : public SpaceNavigator {
public:
	CruiseNavigator();

	void update(const std::vector<DetectedObject> & detectedObjects, float deltaTime) override;
	void reset() override;

	glm::vec3 getPosition() const override { return position; }
	glm::vec3 getTarget() const override;
	glm::vec3 getUp() const override;

	float getThrustLevel() const override { return steerLevel; }
	float getSpeedBoost() const override { return speedBoost.getLevel(); }
	float getIdleSeconds() const override { return idleSeconds; }

	bool hasControlPosition() const override { return tracker.isDriving(); }
	glm::vec2 getControlPosition() const override;
	float getControlHeight() const override { return controlHeight; }

	// Supplies the measured table aspect already owned by Pi B projection
	// settings. The drag gesture converts normalized x/y movement to physical
	// proportions, while Kinect rotation and mirroring remain exclusively in
	// TableCoordinateMapper before this class receives the objects.
	void setSteeringSurfaceSize(float widthCm, float heightCm) {
		dragSteering.setSurfaceSize(widthCm, heightCm);
		speedBoost.setSurfaceSize(widthCm, heightCm);
	}

	// The flight would leave the arrangement behind within seconds and never
	// come back, so the world is rebuilt ahead of it instead.
	bool wantsEndlessWorld() const override { return true; }

	// Whether only blobs Pi A has typed as hands may steer. On by default: a
	// visitor's hand is the only thing that should move the camera. The toggle
	// exists because the gate is strict and its failure mode - Pi A typing
	// everything Unknown, so nothing steers and the screen looks broken - can
	// only be found out at the table (constitution: values that can only be
	// judged on the Pi must be adjustable while it runs there).
	void setStrictHands(bool strict) { strictHands = strict; }
	bool getStrictHands() const { return strictHands; }

	// What the last frame's steering was made of, for the debug HUD. The failure
	// mode here is "there are blobs and none of them are flying it", which a
	// count of objects alone cannot show.
	std::string getSteeringLine() const;

private:
	// The stream's short per-id memory - smoothed position, stillness,
	// hold/forget bookkeeping - and the single-exclusive-driver selection
	// this class's own chooseSteeringHand() used to implement by hand (one
	// hand flies it, not an average of everything on the table, which is what
	// made the view lurch whenever a second hand appeared or a contour split
	// for a few frames - see ObjectTracker::chooseExclusive()). Configured in
	// the constructor with this screen's own tuning.
	ObjectTracker tracker;

	// Finds exactly two hands in the current mapped input and lets
	// TwoHandSpeedBoost measure their planar relative velocity. This bypasses
	// ObjectTracker's secondary hold/dead-reckoning state: a hand missing from
	// the current receiver output cannot continue creating acceleration, and
	// strictHands remains solely the steering gate.
	void updateSpeedBoost(const std::vector<DetectedObject> & detectedObjects,
		float deltaTime);

	glm::vec3 position;
	float yaw = 0.0f; // heading, radians, 0 flies along +z
	float bank = 0.0f; // how far the camera is currently rolled into its turn

	float steerLevel = 0.0f; // how hard it is turning, 0-1, for the visual
	float idleSeconds = 0.0f;

	bool strictHands = true;

	glm::vec3 homePosition = glm::vec3(0.0f, 40.0f, -1200.0f);

	// Speed, in world units per second, before the bounded speed envelope applies.
	// Set against the scale of the scene: the drifting things
	// sit within a few hundred units of each other, so this passes them at a
	// walking-past pace rather than a flyby.
	float cruiseSpeed = 140.0f;

	// Pair identity, physical-aspect correction, separation-rate threshold and
	// fast release for the deliberate two-hand acceleration gesture.
	TwoHandSpeedBoost speedBoost;

	// Bounded combination of height and boost. Keeping the actual multiplier as
	// state also prevents a single noisy z or velocity sample from changing the
	// distance travelled in one render frame abruptly.
	CruiseSpeedEnvelope speedEnvelope;

	// The per-visitor anchor, continuous physical-space pull vector and radial
	// deadzone/hysteresis. Kept as one composable state machine so the singular
	// first reading and the session reset cannot diverge across update paths.
	SessionDragSteering dragSteering;

	// The steering hand's current height (DetectedObject::z, smoothed),
	// decaying back to rest when nobody is flying rather than freezing on
	// whatever it last was. Read by the visual as a third, continuous input
	// alongside steering and the speed-boost gesture - see
	// SpaceNavigator::getControlHeight() - and, since this entry, drives the
	// flight's own bounded cruising speed through CruiseSpeedEnvelope.
	float controlHeight = 0.0f;
	float controlHeightDecay = 2.0f;

	// The hard limit on how fast the view may swing, in radians per second, no
	// matter what the input does. Pull strength scales this from a gentle minimum
	// to full rate; the separate return scale controls how quickly a released drag
	// comes back to straight flight. The cap also protects handovers and contour
	// splits from becoming single-frame camera jumps.
	float maxYawRate = 1.65f;
	float minDragYawRateScale = 0.2f;
	float returnYawRateScale = 0.55f;

	// Per-track smoothing, stillness, hold/forget and handover timing are all
	// tuned on tracker itself now (ObjectTracker::minCutoff/beta/moveEpsilon/
	// inertSeconds/holdSeconds/trackForgetSeconds/handoverSeconds) - see the
	// constructor for the values this screen sets them to and why.

	// How far the camera rolls at the maximum actual turn rate, in radians, and
	// how quickly it follows that rate. Purely how it feels - nothing is steered
	// by it - but a flight that turns without leaning reads as the whole universe
	// sliding sideways rather than as the viewer banking.
	float maxBank = 0.45f;
	float bankFollow = 2.0f;

	// With nobody at the table the flight straightens out rather than holding
	// whatever curve the last visitor left it in, which over minutes would be a
	// circle in empty space. The same gap ends that visitor's drag anchor, so the
	// next controlling hand establishes a fresh session.
	float idleSecondsBeforeStraightening = 6.0f;
};
