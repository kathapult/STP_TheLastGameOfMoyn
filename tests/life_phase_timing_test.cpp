#include "LifePhaseTiming.h"

#include <cassert>
#include <iostream>

int main() {
	using namespace LifePhaseTiming;

	static_assert(ranges.size() == 3, "PUSH + LIFE has exactly three phases");
	static_assert(minimumTotalSeconds == 70.0f, "minimum mode duration changed");
	static_assert(maximumTotalSeconds == 90.0f, "maximum mode duration changed");

	assert(ranges[0].minSeconds == 30.0f && ranges[0].maxSeconds == 45.0f);
	assert(ranges[1].minSeconds == 10.0f && ranges[1].maxSeconds == 15.0f);
	assert(ranges[2].minSeconds == 30.0f && ranges[2].maxSeconds == 45.0f);

	const Durations shortest {
		ranges[0].minSeconds,
		ranges[1].minSeconds,
		ranges[2].minSeconds
	};
	const Durations longest {
		ranges[0].maxSeconds,
		ranges[1].maxSeconds,
		maximumThirdPhaseSeconds(ranges[0].maxSeconds, ranges[1].maxSeconds)
	};
	const Durations sample { 37.5f, 12.5f, 37.5f };

	assert(totalSeconds(shortest) == minimumTotalSeconds);
	assert(totalSeconds(longest) == maximumTotalSeconds);
	assert(totalSeconds(sample) == 87.5f);
	assert(totalSeconds(sample) <= maximumTotalSeconds);
	assert(maximumThirdPhaseSeconds(30.0f, 10.0f) == 45.0f);
	assert(maximumThirdPhaseSeconds(45.0f, 15.0f) == 30.0f);

	std::cout << "Life phase timing tests passed\n";
	return 0;
}
