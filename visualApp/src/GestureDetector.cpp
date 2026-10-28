#include "GestureDetector.h"

GestureDetector::GestureDetector() {}

// continuous state (hysteresis)
bool GestureDetector::isGrabHeld(float grabStrength, float deltaTime) {
    if (grabStrength > grabThreshold) {
        grabHoldTimer += deltaTime;
    } else if (grabStrength < releaseThreshold) {
        grabHoldTimer = 0.0f;
        grabHeld = false; // "open" again immediately, no second trigger needed
    }

    if (grabHoldTimer >= holdTime) {
        grabHeld = true;
    }

    return grabHeld;
}

// both hands need to hold a fist at the same time
bool GestureDetector::checkBothHandsGrabHoldToggle(float grabStrengthA, float grabStrengthB, float deltaTime) {
    bool bothGrabbing = grabStrengthA > grabThreshold && grabStrengthB > grabThreshold;
    bool bothOpen = grabStrengthA < releaseThreshold && grabStrengthB < releaseThreshold;

    if (bothGrabbing) {
        bothGrabTimer += deltaTime;
    } else if (bothOpen) {
        // both hands clearly open again -> next double-fist counts again
        bothGrabTimer = 0.0f;
        bothGrabReady = true;
    }

    if (bothGrabReady && bothGrabTimer >= holdTime) {
        bothGrabReady = false; // blocks re-trigger until both hands open again
        return true;
    }
    return false;
}