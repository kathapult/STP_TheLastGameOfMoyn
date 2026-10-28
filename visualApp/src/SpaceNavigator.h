#pragma once

#include "DetectedObject.h"
#include "ofMain.h"

// Base for the ways a camera can be moved through the space screen. Only one
// survives - CruiseNavigator, where the flight never stops and the hands only
// steer. A second, swimming, where nothing happened until a visitor pulled, was
// removed along with the solar system screen it belonged to.
//
// Kept as a base rather than folded into that one subclass for the same reason
// VisualBase and TransformBase exist (constitution Principle III): another way
// of moving should be another class, not another case in an if.
//
// Everything a subclass may read is a DetectedObject - position, height and
// velocity - because the sensor recognizes no gestures at all (Principle II).
class SpaceNavigator {
public:
	SpaceNavigator() = default;
	virtual ~SpaceNavigator() = default;

	virtual void setup() { reset(); }

	// Reads this frame's hands and moves the camera. deltaTime because
	// DetectedObject's velocities are per second and a frame rate that drifts
	// must not change how far anybody travels.
	virtual void update(const std::vector<DetectedObject> & detectedObjects, float deltaTime) = 0;

	// Back to wherever this navigator considers the beginning.
	virtual void reset() = 0;

	virtual glm::vec3 getPosition() const = 0;

	// Point the camera should look at: one unit ahead along the current heading.
	virtual glm::vec3 getTarget() const = 0;

	// Which way is up for the camera. Level by default; a navigator that banks
	// into its turns overrides it.
	virtual glm::vec3 getUp() const { return glm::vec3(0.0f, 1.0f, 0.0f); }

	// How hard the traveller is currently being driven, 0.0-1.0, for the visual
	// to show that the piece noticed. Without it, an input that did not register
	// is indistinguishable from a piece that is broken.
	virtual float getThrustLevel() const = 0;

	// How much a second, separate input is currently speeding the traveller up,
	// 0.0-1.0. Default zero for a navigator that has no such input at all - not
	// every way of moving through this space needs one.
	virtual float getSpeedBoost() const { return 0.0f; }

	// Where the input currently driving this navigator is, normalized 0-1
	// across the table, and whether there is one at all right now - for the
	// visual's "you are here" cursor. Defaulted rather than pure virtual,
	// since a future navigator with nothing steerable at a point (an idle
	// drift, say) has nothing honest to answer here.
	virtual bool hasControlPosition() const { return false; }
	virtual glm::vec2 getControlPosition() const { return glm::vec2(0.5f, 0.5f); }

	// How high the input currently driving this navigator is being held,
	// 0.0-1.0 (DetectedObject::z) - a continuous third input a visual may
	// choose to read for something that isn't steering or speed. Zero when
	// nothing is driving it or the navigator has no notion of height at all.
	virtual float getControlHeight() const { return 0.0f; }

	// Seconds since a hand was last seen, so the visual can tell whether
	// anybody is at the table at all.
	virtual float getIdleSeconds() const = 0;

	// Whether the world should be rebuilt ahead of the traveller as they pass
	// it. False for a navigator that stays inside a fixed arrangement, true for
	// one that never stops moving and would otherwise fly out of it for good.
	virtual bool wantsEndlessWorld() const { return false; }
};
