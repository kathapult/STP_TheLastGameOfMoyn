#include "SessionDragSteering.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

namespace {
bool close(float left, float right, float tolerance = 0.00001f) {
	return std::fabs(left - right) < tolerance;
}
}

int main() {
	SessionDragSteering drag;
	drag.setSurfaceSize(188.0f, 79.0f);
	drag.update(7, std::numeric_limits<float>::quiet_NaN(), 0.5f);
	assert(!drag.hasAnchor());

	// The first contact is a stable, visitor-specific anchor and cannot create a
	// direction by itself, even when it starts away from the geometric centre.
	drag.update(7, 0.72f, 0.82f);
	assert(drag.hasAnchor());
	assert(!drag.isEngaged());
	assert(close(drag.getAnchorX(), 0.72f));
	assert(close(drag.getAnchorY(), 0.82f));
	assert(close(drag.getStrength(), 0.0f));

	// Tracker-sized jitter stays neutral. A deliberate rightward pull crosses the
	// outer radius and produces a continuous, proportional vector.
	drag.update(7, 0.73f, 0.83f);
	assert(!drag.isEngaged());
	drag.update(7, 0.82f, 0.82f);
	assert(drag.isEngaged());
	assert(drag.getPullX() > 0.0f);
	assert(close(drag.getPullY(), 0.0f));
	assert(close(drag.getHeadingRadians(), 3.14159265358979323846f * 0.5f));
	float mediumStrength = drag.getStrength();
	assert(mediumStrength > 0.0f && mediumStrength < 1.0f);

	// Pull length saturates without losing its direction.
	drag.update(7, 1.0f, 0.82f);
	assert(close(drag.getStrength(), 1.0f));
	assert(close(drag.getPullX(), 1.0f));

	// Returning through the activation boundary retains the gesture and fades its
	// magnitude. Even a perpendicular noisy sample in this short-vector band must
	// keep the last deliberate heading instead of briefly steering somewhere else.
	drag.update(7, 0.72f, 0.76f);
	assert(drag.isEngaged());
	assert(drag.getStrength() < mediumStrength);
	assert(close(drag.getHeadingRadians(), 3.14159265358979323846f * 0.5f));
	assert(drag.getPullX() > 0.0f);
	assert(close(drag.getPullY(), 0.0f));
	drag.update(7, 0.72f, 0.82f);
	assert(!drag.isEngaged());
	assert(close(drag.getStrength(), 0.0f));
	drag.update(7, 0.745f, 0.82f);
	assert(!drag.isEngaged());

	// Outside the radial return band, a small angle change remains inside the
	// continuous direction hysteresis. A larger diagonal move is accepted, but
	// retains the small angular margin that prevents boundary flicker.
	drag.resetSession();
	drag.update(7, 0.5f, 0.5f);
	drag.update(7, 0.56f, 0.5f);
	float rightHeading = drag.getHeadingRadians();
	drag.update(7, 0.5532f, 0.4937f);
	assert(close(drag.getHeadingRadians(), rightHeading));
	drag.update(7, 0.5532f, 0.3734f);
	assert(drag.getHeadingRadians() < rightHeading);
	assert(drag.getHeadingRadians() > 3.14159265358979323846f * 0.25f);

	// Physical dimensions, not normalized coordinates, determine direction and
	// strength. This is 9.4 cm right and 7.9 cm up, so x must dominate even though
	// the normalized y delta is twice as large.
	drag.resetSession();
	drag.update(7, 0.5f, 0.5f);
	drag.update(7, 0.55f, 0.4f);
	assert(drag.isEngaged());
	assert(drag.getPullX() > 0.0f);
	assert(drag.getPullY() < 0.0f);
	assert(std::fabs(drag.getPullX()) > std::fabs(drag.getPullY()));
	assert(drag.getHeadingRadians() > 0.0f);
	assert(drag.getHeadingRadians() < 3.14159265358979323846f * 0.5f);

	// Invalid surface settings leave the last valid calibration intact.
	drag.setSurfaceSize(-1.0f, 0.0f);
	drag.resetSession();
	drag.update(7, 0.5f, 0.5f);
	drag.update(7, 0.55f, 0.4f);
	assert(std::fabs(drag.getPullX()) > std::fabs(drag.getPullY()));

	// A completed visitor session does not leak its anchor or vector to the next.
	drag.resetSession();
	assert(!drag.hasAnchor());
	assert(!drag.isEngaged());
	drag.update(7, 0.21f, 0.35f);
	assert(close(drag.getAnchorX(), 0.21f));
	assert(close(drag.getAnchorY(), 0.35f));
	assert(close(drag.getStrength(), 0.0f));

	// ObjectTracker can hand control directly to another id before the longer
	// idle timeout. The successor's first point is its own neutral anchor rather
	// than a maximum pull measured from the previous visitor's position.
	drag.update(8, 0.91f, 0.12f);
	assert(drag.getTrackId() == 8);
	assert(close(drag.getAnchorX(), 0.91f));
	assert(close(drag.getAnchorY(), 0.12f));
	assert(!drag.isEngaged());
	assert(close(drag.getStrength(), 0.0f));

	std::cout << "session drag steering tests passed\n";
	return 0;
}
