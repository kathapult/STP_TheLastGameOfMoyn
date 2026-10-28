#include "CruiseNavigator.h"
#include <algorithm>
#include <cmath>

// Shortest signed distance from b to a, wrapped into (-PI, PI]. This lets the
// camera follow a continuous drag heading across the -PI/PI seam without ever
// choosing the long way around.
static float angleDiff(float a, float b) {
	float d = std::fmod(a - b + PI, TWO_PI);

	if (d < 0.0f) {
		d += TWO_PI;
	}

	return d - PI;
}

CruiseNavigator::CruiseNavigator() {
	// This screen's own tuning for the shared tracker - see each field's
	// comment on ObjectTracker for what it does in general.
	//
	// beta well above ObjectTracker's own default: steering is meant to
	// answer a hand at once (see the class comment on why the heading is set
	// outright from the hand's zone rather than integrated), so a
	// fast-moving hand should pass through close to unfiltered - minCutoff
	// alone is what still calms a hand held still.
	tracker.minCutoff = 1.4f;
	tracker.beta = 1.4f;
	tracker.moveEpsilon = 0.025f;
	// Pi A relabels a motionless contour after five seconds and hides it after
	// ten. Do not impose an earlier second stillness cutoff here: an already
	// engaged map drag remains owned by that track until Pi A actually removes
	// it, while new candidates remain hands-only below.
	tracker.inertSeconds = 12.0f;
	tracker.holdSeconds = 0.8f;
	tracker.trackForgetSeconds = 2.5f;
	tracker.handoverSeconds = 2.0f;

	reset();
}

void CruiseNavigator::reset() {
	position = homePosition;
	yaw = 0.0f;
	bank = 0.0f;
	steerLevel = 0.0f;
	idleSeconds = 0.0f;

	tracker.reset();

	speedBoost.reset();
	speedEnvelope.reset();

	dragSteering.resetSession();
	controlHeight = 0.0f;
}

// Level flight: the heading is a single angle, and there is no pitch at all.
// Deliberately - the whole input is left and right, and a flight that could also
// climb would need a way to level out again that nobody has been given.
glm::vec3 CruiseNavigator::getTarget() const {
	return position + glm::vec3(std::sin(yaw), 0.0f, std::cos(yaw));
}

// Up, rolled around the direction of flight by the current bank. Rotating the up
// vector rather than the heading is what makes the horizon tip while the flight
// still goes where it is pointed.
glm::vec3 CruiseNavigator::getUp() const {
	// Sideways, at right angles to both the heading and world up.
	glm::vec3 right(std::cos(yaw), 0.0f, -std::sin(yaw));
	glm::vec3 up(0.0f, 1.0f, 0.0f);

	return up * std::cos(bank) + right * std::sin(bank);
}

// Where the "you are here" cursor goes: the flying hand's own smoothed
// position, not the zone it currently reads as or anything derived from it -
// the cursor answers "where is my hand", the steering math answers "what
// does that mean", and conflating the two would put the cursor somewhere
// the hand never was.
glm::vec2 CruiseNavigator::getControlPosition() const {
	const ObjectTracker::Track * track = tracker.isDriving() ? tracker.find(tracker.getPrimaryId()) : nullptr;

	if (track == nullptr) {
		return glm::vec2(0.5f, 0.5f);
	}

	return glm::vec2(track->x, track->y);
}

// Reads the two-hand spread gesture from the current mapped input. The wire's
// vx/vy values are already per second, so the result does not spike on the one
// render frame where a new sensor snapshot arrives. Height is deliberately not
// part of this gesture: z uses a different physical normalization and ordinary
// lifting must not become an accidental forward lurch.
void CruiseNavigator::updateSpeedBoost(
	const std::vector<DetectedObject> & detectedObjects, float deltaTime) {
	const DetectedObject * first = nullptr;
	const DetectedObject * second = nullptr;
	bool ambiguous = false;

	for (const auto & object : detectedObjects) {
		if (object.type != ObjectType::Hand) {
			continue;
		}

		if (first == nullptr) {
			first = &object;
		} else if (second == nullptr) {
			second = &object;
		} else {
			ambiguous = true;
			break;
		}
	}

	TwoHandSpeedBoost::HandMotion firstMotion;
	TwoHandSpeedBoost::HandMotion secondMotion;

	if (first != nullptr && second != nullptr && !ambiguous) {
		firstMotion = { first->id, first->x, first->y, first->vx, first->vy };
		secondMotion = { second->id, second->x, second->y, second->vx, second->vy };
	}

	speedBoost.update(first != nullptr && second != nullptr && !ambiguous,
		firstMotion, secondMotion, deltaTime);
}

void CruiseNavigator::update(const std::vector<DetectedObject> & detectedObjects, float deltaTime) {
	// A frame that took unusually long must not translate into an unusually long
	// jump forwards, nor age every track by a second's worth of stillness.
	deltaTime = ofClamp(deltaTime, 0.0f, 1.0f / 20.0f);

	tracker.update(detectedObjects, deltaTime);
	updateSpeedBoost(detectedObjects, deltaTime);

	// Only hands may begin flying it. Once a hand has drawn an engaged pull,
	// retain that same id through Pi A's five-second Hand->Object stillness
	// relabel: map-style holding must not require periodic waving. This exception
	// cannot let a cup take control because it applies only to the track that
	// already owns an engaged gesture; Pi A's ten-second hide still ends it.
	const int dragOwnerId = dragSteering.getTrackId();
	const bool retainEngagedDrag = dragSteering.isEngaged();
	auto typeAllowed = [this, dragOwnerId, retainEngagedDrag](const ObjectTracker::Track & track) {
		if (retainEngagedDrag && track.id == dragOwnerId) {
			return true;
		}

		if (strictHands) {
			return track.type == ObjectType::Hand;
		}
		return track.type != ObjectType::Object;
	};

	const ObjectTracker::Track * hand = tracker.chooseExclusive(deltaTime, typeAllowed);

	if (hand != nullptr) {
		idleSeconds = 0.0f;

		// On the first controlling frame this captures the visitor's personal
		// anchor and deliberately leaves the vector neutral. Later frames draw a
		// continuous pull relative to that fixed point, after physical-aspect
		// correction and the deadzone guards owned by SessionDragSteering.
		dragSteering.update(hand->id, hand->x, hand->y);

		controlHeight = ofClamp(hand->z, 0.0f, 1.0f);
	} else {
		idleSeconds += deltaTime;

		// Nobody flying it: hold the current drag for a moment rather than
		// straightening the instant a hand blinks out, then straighten,
		// because a curve held for minutes is a circle in empty space.
		if (idleSeconds > idleSecondsBeforeStraightening) {
			dragSteering.resetSession();
		}

		controlHeight += (0.0f - controlHeight) * ofClamp(controlHeightDecay * deltaTime, 0.0f, 1.0f);
	}

	// Map the physical pull vector to the ship's heading: table-up is straight,
	// table-right is +90 degrees, down reverses and left is -90. At the anchor the
	// vector is zero and the wanted heading returns to straight. Pull length does
	// not distort the direction; it scales how quickly the ship commits to it.
	float pullStrength = dragSteering.getStrength();
	float wantedYaw = dragSteering.getHeadingRadians();
	float yawRateScale = dragSteering.isEngaged()
		? ofLerp(minDragYawRateScale, 1.0f, pullStrength)
		: returnYawRateScale;
	float maxStep = maxYawRate * yawRateScale * deltaTime;
	float yawStep = ofClamp(angleDiff(wantedYaw, yaw), -maxStep, maxStep);

	yaw += yawStep;
	yaw = angleDiff(yaw, 0.0f); // kept wrapped into (-PI, PI] so it cannot drift unbounded over a long show

	// This is actual turn effort rather than absolute compass heading. Once the
	// ship reaches a held drag direction it levels out instead of looking as if it
	// is still turning forever.
	float fullRateStep = maxYawRate * deltaTime;
	steerLevel = fullRateStep > 0.0f
		? ofClamp(std::fabs(yawStep) / fullRateStep, 0.0f, 1.0f)
		: 0.0f;

	// The camera leans into the signed turn rate actually flown rather than the
	// absolute heading, so it levels once the requested direction is reached.
	// Negative means a right turn drops the picture's right-hand side.
	float signedTurnLevel = fullRateStep > 0.0f
		? ofClamp(yawStep / fullRateStep, -1.0f, 1.0f)
		: 0.0f;
	float wantedBank = -signedTurnLevel * maxBank;
	bank += (wantedBank - bank) * ofClamp(bankFollow * deltaTime, 0.0f, 1.0f);

	// Always moving, whatever anybody does. This is the whole difference from
	// swimming: there is nothing here that can stop the flight - the height
	// scale and the two-hand pull-apart gesture both only ever change how
	// fast, never whether.
	float speedMultiplier = speedEnvelope.update(hand != nullptr,
		controlHeight, speedBoost.getLevel(), deltaTime);
	float speed = cruiseSpeed * speedMultiplier;
	position += glm::normalize(getTarget() - position) * (speed * deltaTime);
}

std::string CruiseNavigator::getSteeringLine() const {
	int primaryId = tracker.getPrimaryId();
	std::string who = primaryId < 0 ? "nobody" : ("id " + ofToString(primaryId));

	std::string anchor = dragSteering.hasAnchor()
		? ofToString(dragSteering.getAnchorX(), 2) + "," + ofToString(dragSteering.getAnchorY(), 2)
		: "waiting";
	std::string drag = ofToString(dragSteering.getPullX(), 2)
		+ "," + ofToString(dragSteering.getPullY(), 2);

	return "objects " + ofToString(tracker.getSeenCount())
		+ "  hands " + ofToString(tracker.getUsableCount())
		+ "  flown by " + who
		+ "  drag " + drag
		+ "  pull " + ofToString(dragSteering.getStrength(), 2)
		+ "  anchor " + anchor
		+ "  boost " + ofToString(speedBoost.getLevel(), 2)
		+ "  speed x" + ofToString(speedEnvelope.getMultiplier(), 2)
		+ (strictHands ? "  [hands only]" : "  [any blob]");
}
