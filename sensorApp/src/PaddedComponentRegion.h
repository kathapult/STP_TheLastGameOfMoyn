#pragma once

#include <algorithm>
#include <cmath>

// Dependency-free construction of the local component crop used by
// ObjectDetector. Right and bottom are exclusive. Keeping one guaranteed empty
// pixel beyond the rasterized contour on every in-frame side matters because
// the inward selector treats pixels beyond its local ROI view as table exterior.
namespace PaddedComponentRegion {

struct Region {
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;

	bool isValid() const { return width > 0 && height > 0; }
	int rightExclusive() const { return x + width; }
	int bottomExclusive() const { return y + height; }
};

inline Region fromBounds(float left, float top, float right, float bottom,
	int frameWidth, int frameHeight) {
	Region result;
	if (!std::isfinite(left) || !std::isfinite(top)
		|| !std::isfinite(right) || !std::isfinite(bottom)
		|| frameWidth <= 0 || frameHeight <= 0) {
		return result;
	}

	result.x = std::max(0, std::min(
		static_cast<int>(std::floor(left)) - 1, frameWidth - 1));
	result.y = std::max(0, std::min(
		static_cast<int>(std::floor(top)) - 1, frameHeight - 1));

	// ceil(max)+1 would make max itself the final included pixel because the
	// rectangle end is exclusive. The second increment is the actual padding.
	const int rightExclusive = std::max(result.x + 1, std::min(
		static_cast<int>(std::ceil(right)) + 2, frameWidth));
	const int bottomExclusive = std::max(result.y + 1, std::min(
		static_cast<int>(std::ceil(bottom)) + 2, frameHeight));

	result.width = rightExclusive - result.x;
	result.height = bottomExclusive - result.y;
	return result;
}

} // namespace PaddedComponentRegion
