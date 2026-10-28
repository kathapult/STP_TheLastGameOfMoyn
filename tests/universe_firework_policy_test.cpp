#include "UniverseFireworkPolicy.h"
#include <cassert>
#include <cstdint>
#include <iostream>

int main() {
	constexpr std::uint64_t seed = 0x5a17c9e3ULL;
	constexpr std::uint64_t sampleCount = 200000ULL;
	std::uint64_t fireworkCount = 0;
	std::uint64_t differentSeedDecisions = 0;

	for (std::uint64_t serial = 0; serial < sampleCount; serial++) {
		bool first = UniverseFireworkPolicy::shouldExplode(seed, serial);
		bool repeated = UniverseFireworkPolicy::shouldExplode(seed, serial);
		bool otherSession = UniverseFireworkPolicy::shouldExplode(seed + 1ULL, serial);

		// A spawned element keeps one decision; rendering or update frequency cannot
		// alter it, while a new session does receive a genuinely different pattern.
		assert(first == repeated);
		fireworkCount += first ? 1ULL : 0ULL;
		differentSeedDecisions += first != otherSession ? 1ULL : 0ULL;
	}

	double rate = static_cast<double>(fireworkCount) / static_cast<double>(sampleCount);
	assert(rate > 0.018 && rate < 0.022);
	assert(differentSeedDecisions > 1000ULL);

	// A scheduled body bursts on whichever comes first: visible approach or the
	// fallback timer. Unselected bodies never borrow either trigger.
	assert(!UniverseFireworkPolicy::isBurstDue(false, -1.0f, 10.0f, 650.0f));
	assert(!UniverseFireworkPolicy::isBurstDue(true, 2.0f, 651.0f, 650.0f));
	assert(UniverseFireworkPolicy::isBurstDue(true, 2.0f, 650.0f, 650.0f));
	assert(UniverseFireworkPolicy::isBurstDue(true, 0.0f, 2000.0f, 650.0f));

	std::cout << "universe firework policy tests passed at rate " << rate << "\n";
	return 0;
}
