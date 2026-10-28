#include "DynamicLifeRulePool.h"
#include "DynamicLifeSeeder.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {
constexpr int gridWidth = 126;
constexpr int gridHeight = 53;
constexpr int gridSize = gridWidth * gridHeight;
using Grid = std::array<unsigned char, gridSize>;

struct Neighbourhood {
	std::array<int, 8> indices {};
	int count = 0;
};

using Neighbourhoods = std::array<Neighbourhood, gridSize>;

bool same(const std::vector<int> & left, const std::vector<int> & right) {
	return left == right;
}

float nextRandom(std::uint32_t & state) {
	state = state * 1664525U + 1013904223U;
	return static_cast<float>(state >> 8U) / 16777216.0f;
}

Neighbourhoods buildNeighbourhoods() {
	Neighbourhoods result {};
	for (int y = 0; y < gridHeight; y++) {
		for (int x = 0; x < gridWidth; x++) {
			Neighbourhood & neighbourhood = result[x + y * gridWidth];
			for (int dy = -1; dy <= 1; dy++) {
				for (int dx = -1; dx <= 1; dx++) {
					if ((dx == 0 && dy == 0)
						|| x + dx < 0 || x + dx >= gridWidth
						|| y + dy < 0 || y + dy >= gridHeight) {
						continue;
					}
					neighbourhood.indices[neighbourhood.count++] =
						(x + dx) + (y + dy) * gridWidth;
				}
			}
		}
	}
	return result;
}

struct Dynamics {
	double meanLiveFraction = 0.0;
	double meanChangedFraction = 0.0;
	std::uint64_t peakLiveCells = 0;
};

// Runs the same finite, non-wrapping neighbourhood as GameOfLife for the full
// maximum 45 seconds of phase three at its 15-generation-per-second pace. This
// is not an artistic projector acceptance test; it guards against another
// Seeds- or 34-Life-like catalog entry changing almost everywhere at once.
Dynamics measureDynamics(const DynamicLifeRulePool::RuleSet & rule,
	Grid grid) {
	Grid next {};
	const Neighbourhoods neighbourhoods = buildNeighbourhoods();
	std::array<bool, 9> birth {};
	std::array<bool, 9> survive {};
	for (int count : rule.birth) {
		birth[static_cast<std::size_t>(count)] = true;
	}
	for (int count : rule.survive) {
		survive[static_cast<std::size_t>(count)] = true;
	}

	constexpr int generationCount = 675;
	constexpr int measuredGenerations = 225;
	std::uint64_t liveTotal = 0;
	std::uint64_t changedTotal = 0;
	std::uint64_t peakLive = 0;

	for (int generation = 0; generation < generationCount; generation++) {
		std::uint64_t live = 0;
		std::uint64_t changed = 0;

		for (int index = 0; index < gridSize; index++) {
			int neighbourCount = 0;
			const Neighbourhood & neighbourhood = neighbourhoods[index];
			for (int neighbour = 0; neighbour < neighbourhood.count; neighbour++) {
				neighbourCount += grid[neighbourhood.indices[neighbour]] != 0U;
			}

			const bool alive = grid[index] != 0U;
			const bool nextAlive = alive
				? survive[static_cast<std::size_t>(neighbourCount)]
				: birth[static_cast<std::size_t>(neighbourCount)];
			next[index] = nextAlive ? 1U : 0U;
			live += nextAlive;
			changed += nextAlive != alive;
		}

		grid.swap(next);
		peakLive = std::max(peakLive, live);
		if (generation >= generationCount - measuredGenerations) {
			liveTotal += live;
			changedTotal += changed;
		}
	}

	const double measuredCells = static_cast<double>(measuredGenerations * gridSize);
	return {
		static_cast<double>(liveTotal) / measuredCells,
		static_cast<double>(changedTotal) / measuredCells,
		peakLive
	};
}
}

int main() {
	const auto & rules = DynamicLifeRulePool::rules();
	assert(rules.size() == 4);
	assert(std::string(rules[0].name) == "HighLife");
	assert(std::string(rules[1].name) == "DotLife");
	assert(std::string(rules[2].name) == "DryLife");
	assert(std::string(rules[3].name) == "Walled Cities");

	const std::vector<int> formerBirth { 3, 6, 7, 8 };
	const std::vector<int> formerSurvive { 3, 4, 6, 7, 8 };
	const std::vector<int> seedsBirth { 2 };
	const std::vector<int> life34Birth { 3, 4 };
	const std::vector<int> life34Survive { 3, 4 };

	for (std::size_t ruleIndex = 0; ruleIndex < rules.size(); ruleIndex++) {
		const auto & rule = rules[ruleIndex];
		assert(rule.name != nullptr && rule.name[0] != '\0');
		assert(!same(rule.birth, formerBirth) || !same(rule.survive, formerSurvive));
		assert(!same(rule.birth, seedsBirth) || !rule.survive.empty());
		assert(!same(rule.birth, life34Birth) || !same(rule.survive, life34Survive));

		for (const auto * counts : { &rule.birth, &rule.survive }) {
			assert(std::is_sorted(counts->begin(), counts->end()));
			assert(std::adjacent_find(counts->begin(), counts->end()) == counts->end());
			assert(std::all_of(counts->begin(), counts->end(), [](int count) {
				return count >= 0 && count <= 8;
			}));
		}

		Grid grid {};
		std::uint32_t randomState =
			0x9e3779b9U + static_cast<std::uint32_t>(ruleIndex);
		auto random = [&randomState]() { return nextRandom(randomState); };
		const float initialFill = DynamicLifeSeeder::seed(grid,
			gridWidth, gridHeight, rule.seedKind, random);
		const std::uint64_t initialLive = static_cast<std::uint64_t>(std::count_if(
			grid.begin(), grid.end(), [](unsigned char value) { return value != 0U; }));

		// Every dynamic phase begins sparse, but with enough local structure to
		// expand rather than relying on a lucky full-field random soup.
		assert(initialFill >= 0.002f);
		assert(initialFill < 0.04f);

		const Dynamics dynamics = measureDynamics(rule, grid);
		assert(dynamics.peakLiveCells > initialLive * 6U / 5U);
		assert(dynamics.meanLiveFraction > 0.002);
		assert(dynamics.meanLiveFraction < 0.30);
		assert(dynamics.meanChangedFraction > 0.001);
		assert(dynamics.meanChangedFraction < 0.15);
	}

	std::cout << "sparse balanced dynamic Life rule pool tests passed\n";
	return 0;
}
