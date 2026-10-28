#include "TableShape.h"

#include <cassert>
#include <cmath>
#include <iostream>

namespace {
bool close(float left, float right) {
	return std::fabs(left - right) < 0.0001f;
}

int countPixels(int width, int height) {
	int count = 0;
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			if (TableShape::containsPixel(x, y, width, height)) count++;
		}
	}
	return count;
}
}

int main() {
	assert(close(TableShape::cornerRadius(188.0f, 79.0f), 34.76f));
	assert(close(TableShape::cornerRadius(79.0f, 188.0f), 34.76f));
	assert(close(TableShape::cornerRadius(188.0f, 79.0f, 1.0f), 39.5f));
	assert(close(TableShape::cornerRadius(188.0f, 79.0f, 0.5f), 19.75f));
	assert(close(TableShape::cornerRadius(188.0f, 79.0f, -1.0f), 0.0f));
	assert(close(TableShape::cornerRadius(188.0f, 79.0f, 2.0f), 39.5f));

	// The centre and the middle of every side belong to the table; its sharp
	// bounding-box corners do not.
	assert(TableShape::contains(94.0f, 39.5f, 188.0f, 79.0f));
	assert(TableShape::contains(0.0f, 39.5f, 188.0f, 79.0f));
	assert(TableShape::contains(187.999f, 39.5f, 188.0f, 79.0f));
	assert(TableShape::contains(94.0f, 0.0f, 188.0f, 79.0f));
	assert(TableShape::contains(94.0f, 78.999f, 188.0f, 79.0f));
	assert(!TableShape::contains(0.0f, 0.0f, 188.0f, 79.0f));
	assert(!TableShape::contains(187.999f, 78.999f, 188.0f, 79.0f));

	// Rotating the sensor ROI only swaps its axes; the shorter side still
	// determines the same physical corner radius.
	assert(TableShape::contains(39.5f, 0.0f, 79.0f, 188.0f));
	assert(!TableShape::contains(0.0f, 0.0f, 79.0f, 188.0f));

	const int horizontalArea = countPixels(100, 40);
	const int verticalArea = countPixels(40, 100);
	assert(horizontalArea == verticalArea);
	assert(horizontalArea > 0);
	assert(horizontalArea < 100 * 40);

	// Pixel membership is symmetric about both axes.
	for (int y = 0; y < 40; ++y) {
		for (int x = 0; x < 100; ++x) {
			const bool value = TableShape::containsPixel(x, y, 100, 40);
			assert(value == TableShape::containsPixel(99 - x, y, 100, 40));
			assert(value == TableShape::containsPixel(x, 39 - y, 100, 40));
		}
	}

	std::cout << "table shape tests passed\n";
	return 0;
}
