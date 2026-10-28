#pragma once

#include <cstdint>

// Finds the stable distal end of one already-isolated foreground component.
// It is deliberately independent of openFrameworks and OpenCV so the geometry
// and all fallback cases can be checked on a development host.
class InwardInteractionPointSelector {
public:
	struct MaskView {
		const std::uint8_t * pixels = nullptr;
		int width = 0;
		int height = 0;
		int stride = 0;

		bool isValid() const;
		bool contains(int x, int y) const;
	};

	struct Result {
		bool valid = false;
		float x = 0.0f;
		float y = 0.0f;
		float entryX = 0.0f;
		float entryY = 0.0f;
		float penetrationPixels = 0.0f;
		float confidence = 0.0f;
		int contactPixelCount = 0;
		int contactComponentCount = 0;
		int capPixelCount = 0;
	};

	struct TemporalState {
		bool active = false;
		bool holdPrevious = false;
		float candidateSince = -1.0f;
		float lastValidTime = -1.0f;
	};

	// component and tableRoi use the same local pixel coordinates. component
	// contains exactly one tracked blob; tableRoi contains the physical rounded
	// table. A foreground component pixel touching a tableRoi edge is an entry
	// seed. The result is the weighted centre of the most distant cap, measured
	// geodesically through the component rather than as a fragile single tip.
	static Result select(const MaskView & component, const MaskView & tableRoi,
		float capFraction, float minimumPenetrationPixels);

	// Scale-independent gate used when the selected point is also evidence for
	// an elongated reaching hand. A round blob spans about two inscribed radii;
	// three rejects it while accepting a clearly longer arm/hand component.
	static float minimumPenetrationForElongatedReach(float palmRadiusPixels);

	// Requires a candidate to remain valid before activation, then holds the
	// last accepted point across a short dropout. Kept independent of frame
	// rate and CV types so timing edge cases are unit-testable.
	static TemporalState stabilize(const TemporalState & previous,
		bool candidateValid, float now, float acquireSeconds,
		float graceSeconds);
};
