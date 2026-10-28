#pragma once

// Turns the planar motion of exactly two persistent hands into a short-lived
// speed-boost level. Positions and velocities are expressed in normalized table
// space by the caller; this class applies the calibrated physical aspect before
// measuring whether the hands are genuinely moving apart.
class TwoHandSpeedBoost {
public:
	struct HandMotion {
		int id = -1;
		float x = 0.0f;
		float y = 0.0f;
		float velocityX = 0.0f;
		float velocityY = 0.0f;
	};

	// Invalid dimensions are ignored so a transient projection value cannot
	// replace the last usable calibration.
	void setSurfaceSize(float widthCm, float heightCm);

	// hasExactPair is false for zero, one, or more than two current hands. A new
	// pair only establishes identity on its first update; this prevents a track
	// split or visitor handover from being mistaken for sudden separation.
	float update(bool hasExactPair, const HandMotion & first,
		const HandMotion & second, float deltaTime);

	void reset();
	float getLevel() const { return level; }

private:
	float surfaceWidthCm = 1.0f;
	float surfaceHeightCm = 1.0f;
	int firstId = -1;
	int secondId = -1;
	float level = 0.0f;

	// Rates are measured in short-table-edges per second. The lower value rejects
	// incidental drift; the upper value is a clearly deliberate pull-apart.
	float spreadThreshold = 0.45f;
	float spreadFullRate = 1.20f;
	float minimumPairSeparation = 0.04f;

	// The boost takes several coherent frames to rise but drops promptly when
	// separation stops, avoiding both one-frame spikes and a long fast coast.
	float followUp = 3.0f;
	float followDown = 6.0f;
};
