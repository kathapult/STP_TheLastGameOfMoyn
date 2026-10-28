#pragma once

#include <algorithm>
#include <cmath>

// A low-pass filter whose own cutoff frequency rises with how fast the
// signal is currently changing (Casiez, Roussel, Vogel, "1€ Filter", CHI
// 2012). Replaces the fixed-rate exponential moving average used everywhere
// a tracked position was smoothed in this project before - sensorApp's
// per-blob centroid, and each visualApp screen's own per-hand tracking.
//
// A fixed EMA rate is always a compromise between the two states a
// hand-tracked position is in almost the whole time: held still, where the
// input is pure sensor jitter and heavy smoothing is exactly right, and
// moving quickly, where the same heavy smoothing reads as the piece lagging
// behind the hand. Picking a rate picks one of those two to be wrong about.
// This filter is the standard fix - smooth hard near zero speed, hardly at
// all once the signal is clearly moving - implemented in the handful of
// lines the paper's own reference code comes to, so it is reproduced here
// rather than pulled in as a dependency the Pi would have to build.
//
// Not thread-safe and holds no more state than the two lines below - one
// instance per scalar being tracked (an x, a y, a z), reset()'s worth of
// history per id.
class OneEuroFilter {
public:
	// minCutoff (Hz): the cutoff used while the signal is not moving - lower
	// is smoother but adds more lag once movement starts.
	// beta: how much a change in speed raises the cutoff above minCutoff -
	// higher lets fast movement through with less lag, at the cost of more
	// visible jitter while slow. 0 disables the adaptive part entirely,
	// which makes this a plain fixed-cutoff low-pass filter - a valid and
	// sometimes useful special case, not a misuse.
	// dCutoff (Hz): cutoff for the low-pass filter applied to the derivative
	// itself, before beta reads it. 1.0 is the value the paper's own
	// reference implementation uses and rarely needs changing - the
	// derivative is already a rough signal, and filtering it more only
	// delays how fast the adaptive cutoff can react.
	explicit OneEuroFilter(float minCutoff = 1.0f, float beta = 0.0f, float dCutoff = 1.0f)
		: minCutoff(minCutoff)
		, beta(beta)
		, dCutoff(dCutoff) {
	}

	// Drops all history and starts again from value exactly, with zero
	// derivative - for an id just seen for the first time, or a screen just
	// entered, so the first sample is reported exactly rather than eased in
	// from whatever a default-constructed filter happened to hold.
	void reset(float value) {
		filtered = value;
		derivative = 0.0f;
		initialized = true;
	}

	// Filters one new sample, deltaTime seconds after the last one (or the
	// first one ever, if this is the first call - see initialized below).
	// Both filtered() and getDerivative() reflect this call afterwards.
	float filter(float value, float deltaTime) {
		if (!initialized) {
			reset(value);
			return filtered;
		}

		// Floored rather than guarded-and-skipped: two samples a fraction of
		// a millisecond apart are rare but not impossible (a duplicate
		// frame, a clock rounding to the same tick), and dividing by
		// something that small would turn ordinary jitter into a derivative
		// spike large enough to fully open the adaptive cutoff for a frame.
		// A floor bounds that spike instead of special-casing it away.
		float dt = std::max(deltaTime, 1.0f / 1000.0f);

		float rawDerivative = (value - filtered) / dt;
		derivative += (rawDerivative - derivative) * alpha(dCutoff, dt);

		float cutoff = minCutoff + beta * std::fabs(derivative);
		filtered += (value - filtered) * alpha(cutoff, dt);

		return filtered;
	}

	float getValue() const { return filtered; }

	// Re-reads minCutoff/beta from wherever a caller keeps them live-tunable
	// (DetectionSettings, calibrated at the table) - cheap enough to call
	// every frame, and doing so is what lets a nudge made while a track is
	// already being followed take effect on it immediately rather than only
	// on the next new id.
	void setParams(float newMinCutoff, float newBeta) {
		minCutoff = newMinCutoff;
		beta = newBeta;
	}

	// The filtered derivative, in the same units as the filtered value, per
	// second - already exactly what a caller wanting a smoothed velocity is
	// after, since computing the adaptive cutoff needs the same number.
	// Exposed so a track does not need a second, separately-smoothed
	// velocity alongside this filter, the way the plain-EMA code it replaces
	// did.
	float getDerivative() const { return derivative; }

private:
	// tau = 1/(2*pi*cutoff) is the time constant of a first-order low-pass
	// at that cutoff frequency; alpha is what turns one Euler step of that
	// filter into a single lerp towards the new sample, dt seconds later.
	static float alpha(float cutoff, float dt) {
		float tau = 1.0f / (2.0f * static_cast<float>(M_PI) * std::max(cutoff, 0.0001f));
		return 1.0f / (1.0f + tau / dt);
	}

	float minCutoff;
	float beta;
	float dCutoff;

	bool initialized = false;
	float filtered = 0.0f;
	float derivative = 0.0f;
};
