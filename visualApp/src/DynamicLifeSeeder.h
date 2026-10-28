#pragma once

#include "DynamicLifeRulePool.h"

#include <algorithm>
#include <array>
#include <cstddef>

// Sparse, rule-specific starting populations for the dynamic Life phase. The
// patterns are distributed across three horizontal zones so each activation
// begins at well under four percent live cells but still contains the local
// structure its selected rule needs in order to spread.
namespace DynamicLifeSeeder {

namespace detail {

template <typename Random>
int randomIndex(int exclusiveMaximum, Random & random) {
	if (exclusiveMaximum <= 1) {
		return 0;
	}
	return std::min(static_cast<int>(random() * exclusiveMaximum),
		exclusiveMaximum - 1);
}

template <typename Grid, std::size_t RowCount, typename Random>
void stampDistributed(Grid & grid, int width, int height,
	const std::array<const char *, RowCount> & pattern, int patternWidth,
	int copyCount, Random & random) {
	for (int copy = 0; copy < copyCount; copy++) {
		const int quarterTurns = randomIndex(4, random);
		const int outputWidth = quarterTurns % 2 == 0
			? patternWidth : static_cast<int>(RowCount);
		const int outputHeight = quarterTurns % 2 == 0
			? static_cast<int>(RowCount) : patternWidth;
		const int zoneStart = copy * width / copyCount;
		const int zoneEnd = (copy + 1) * width / copyCount;
		const int zoneSpace = std::max(zoneEnd - zoneStart - outputWidth + 1, 1);
		const int originX = std::min(zoneStart + randomIndex(zoneSpace, random),
			width - outputWidth);
		const int originY = randomIndex(height - outputHeight + 1, random);

		for (int sourceY = 0; sourceY < static_cast<int>(RowCount); sourceY++) {
			for (int sourceX = 0; sourceX < patternWidth; sourceX++) {
				if (pattern[static_cast<std::size_t>(sourceY)][sourceX] != '#') {
					continue;
				}

				int targetX = sourceX;
				int targetY = sourceY;
				if (quarterTurns == 1) {
					targetX = static_cast<int>(RowCount) - 1 - sourceY;
					targetY = sourceX;
				} else if (quarterTurns == 2) {
					targetX = patternWidth - 1 - sourceX;
					targetY = static_cast<int>(RowCount) - 1 - sourceY;
				} else if (quarterTurns == 3) {
					targetX = sourceY;
					targetY = patternWidth - 1 - sourceX;
				}

				grid[(originX + targetX) + (originY + targetY) * width] = 1.0f;
			}
		}
	}
}

template <typename Grid, typename Random>
void seedWalledCitiesClusters(Grid & grid, int width, int height,
	Random & random) {
	constexpr int clusterSize = 12;
	constexpr int clusterCount = 3;
	constexpr int clusterCellCount = clusterSize * clusterSize;
	constexpr int liveCellsPerCluster = 58; // approximately 40% local fill

	for (int cluster = 0; cluster < clusterCount; cluster++) {
		const int zoneStart = cluster * width / clusterCount;
		const int zoneEnd = (cluster + 1) * width / clusterCount;
		const int zoneSpace = std::max(zoneEnd - zoneStart - clusterSize + 1, 1);
		const int originX = std::min(zoneStart + randomIndex(zoneSpace, random),
			width - clusterSize);
		const int originY = randomIndex(height - clusterSize + 1, random);

		// Shuffle the cells and fill an exact number. This keeps each activation
		// visually different while making the promised low global density a hard
		// bound rather than a probability with rare outliers.
		std::array<int, clusterCellCount> cells {};
		for (int cell = 0; cell < clusterCellCount; cell++) {
			cells[static_cast<std::size_t>(cell)] = cell;
		}
		for (int cell = clusterCellCount - 1; cell > 0; cell--) {
			const int swapWith = randomIndex(cell + 1, random);
			std::swap(cells[static_cast<std::size_t>(cell)],
				cells[static_cast<std::size_t>(swapWith)]);
		}

		for (int live = 0; live < liveCellsPerCluster; live++) {
			const int cell = cells[static_cast<std::size_t>(live)];
			const int x = cell % clusterSize;
			const int y = cell / clusterSize;
			grid[(originX + x) + (originY + y) * width] = 1.0f;
		}
	}
}

} // namespace detail

template <typename Grid, typename Random>
float seed(Grid & grid, int width, int height,
	DynamicLifeRulePool::SeedKind seedKind, Random random) {
	std::fill(grid.begin(), grid.end(), 0.0f);

	switch (seedKind) {
	case DynamicLifeRulePool::SeedKind::HighLifeReplicators: {
		const std::array<const char *, 5> replicator {{
			"..###",
			".#..#",
			"#...#",
			"#..#.",
			"###.."
		}};
		detail::stampDistributed(grid, width, height,
			replicator, 5, 3, random);
		break;
	}
	case DynamicLifeRulePool::SeedKind::DotLifeBHeptominoes: {
		const std::array<const char *, 4> bHeptomino {{
			"##.",
			".##",
			"##.",
			"#.."
		}};
		detail::stampDistributed(grid, width, height,
			bHeptomino, 3, 3, random);
		break;
	}
	case DynamicLifeRulePool::SeedKind::DryLifePuffers: {
		const std::array<const char *, 9> puffer {{
			"###",
			"..#",
			"#.#",
			"##.",
			"...",
			"##.",
			"#.#",
			"..#",
			"###"
		}};
		detail::stampDistributed(grid, width, height,
			puffer, 3, 3, random);
		break;
	}
	case DynamicLifeRulePool::SeedKind::WalledCitiesClusters:
		detail::seedWalledCitiesClusters(grid, width, height, random);
		break;
	}

	const std::size_t liveCells = static_cast<std::size_t>(std::count_if(
		grid.begin(), grid.end(), [](float value) { return value > 0.5f; }));
	return static_cast<float>(liveCells) /
		static_cast<float>(std::max(width * height, 1));
}

} // namespace DynamicLifeSeeder
