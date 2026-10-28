#include "PaddedComponentRegion.h"

#include <cassert>
#include <iostream>
#include <limits>

int main() {
	using PaddedComponentRegion::fromBounds;

	// Bounds include contour pixels through x=14/y=15. Since the crop's end is
	// exclusive, it must reach 16/17 to include one empty maximum-side pixel.
	auto middle = fromBounds(8.0f, 9.0f, 14.0f, 15.0f, 100, 80);
	assert(middle.isValid());
	assert(middle.x == 7 && middle.y == 8);
	assert(middle.rightExclusive() == 16);
	assert(middle.bottomExclusive() == 17);

	// Fractional maxima may rasterize upward and still retain padding.
	auto fractional = fromBounds(8.2f, 9.2f, 14.7f, 15.7f, 100, 80);
	assert(fractional.x == 7 && fractional.y == 8);
	assert(fractional.rightExclusive() == 17);
	assert(fractional.bottomExclusive() == 18);

	// At a real frame edge there is no in-frame padding to add, so clamping to
	// the exclusive frame dimensions is the only valid region.
	auto edge = fromBounds(0.0f, 0.0f, 99.0f, 79.0f, 100, 80);
	assert(edge.x == 0 && edge.y == 0);
	assert(edge.rightExclusive() == 100);
	assert(edge.bottomExclusive() == 80);

	auto invalid = fromBounds(std::numeric_limits<float>::quiet_NaN(),
		0.0f, 1.0f, 1.0f, 100, 80);
	assert(!invalid.isValid());

	std::cout << "padded component region tests passed\n";
	return 0;
}
