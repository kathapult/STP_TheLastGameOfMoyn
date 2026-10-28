#pragma once

#include <algorithm>

// Shared geometry of the physical poker-table surface. The detector uses it
// to reject depth pixels beyond the curved edge, while visualApp uses the
// same radius for its projected border. Keeping this independent of
// openFrameworks lets the shape be checked with a small host-side test.
namespace TableShape {

// Fraction of half the shorter side used as the rounded-rectangle radius.
// The value was matched to the real table after projector inspection: close
// to a stadium, but slightly steeper than an exact semicircle. Pi B may
// override this default with its local projection calibration; Pi A keeps the
// default for its sensor ROI.
constexpr float cornerRadiusFraction = 0.88f;

inline float cornerRadius(float width, float height, float radiusFraction) {
	if (width <= 0.0f || height <= 0.0f) return 0.0f;
	const float boundedFraction = std::max(0.0f,
		std::min(radiusFraction, 1.0f));
	return std::min(width, height) * 0.5f * boundedFraction;
}

inline float cornerRadius(float width, float height) {
	return cornerRadius(width, height, cornerRadiusFraction);
}

// Tests a point in coordinates local to the table ROI's bounding rectangle.
// The cross through the rectangle is accepted directly; points in one of the
// four remaining corner squares are checked against that corner's arc.
inline bool contains(float localX, float localY, float width, float height) {
	if (localX < 0.0f || localY < 0.0f
		|| localX >= width || localY >= height) return false;

	const float radius = cornerRadius(width, height);
	if (radius <= 0.0f) return false;
	if ((localX >= radius && localX < width - radius)
		|| (localY >= radius && localY < height - radius)) return true;

	const float centreX = localX < radius ? radius : width - radius;
	const float centreY = localY < radius ? radius : height - radius;
	const float dx = localX - centreX;
	const float dy = localY - centreY;
	return dx * dx + dy * dy <= radius * radius;
}

// Pixel membership is decided at the pixel centre, keeping opposite edges
// symmetric and avoiding an orientation-dependent extra row or column.
inline bool containsPixel(int localX, int localY, int width, int height) {
	return contains(static_cast<float>(localX) + 0.5f,
		static_cast<float>(localY) + 0.5f,
		static_cast<float>(width), static_cast<float>(height));
}

} // namespace TableShape
