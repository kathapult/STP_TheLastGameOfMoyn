#pragma once

#include <array>
#include <cstddef>

// Pure timing contract for the three visible parts of PUSH + LIFE. Keeping the
// ranges and total calculation independent of openFrameworks lets the mode
// deadline be checked on a development host.
namespace LifePhaseTiming {

struct DurationRange {
	float minSeconds;
	float maxSeconds;
};

inline constexpr std::array<DurationRange, 3> ranges {{
	{ 30.0f, 45.0f }, // Conway
	{ 10.0f, 15.0f }, // Maze
	{ 30.0f, 45.0f } // curated dynamic rule
}};

using Durations = std::array<float, ranges.size()>;

constexpr float totalSeconds(const Durations & durations) {
	float total = 0.0f;
	for (float duration : durations) {
		total += duration;
	}
	return total;
}

constexpr float minimumTotalSeconds =
	ranges[0].minSeconds + ranges[1].minSeconds + ranges[2].minSeconds;
inline constexpr float maximumTotalSeconds = 90.0f;

// Parts one and two are drawn first. Part three keeps its requested lower bound
// but gives up only the portion of its upper range that would exceed the normal
// production mode maximum.
constexpr float maximumThirdPhaseSeconds(float firstSeconds,
	float secondSeconds) {
	const float remaining = maximumTotalSeconds - firstSeconds - secondSeconds;
	return remaining < ranges[2].maxSeconds ? remaining : ranges[2].maxSeconds;
}

static_assert(maximumThirdPhaseSeconds(ranges[0].maxSeconds,
	ranges[1].maxSeconds) >= ranges[2].minSeconds,
	"the mode budget must leave room for the minimum third phase");

} // namespace LifePhaseTiming
