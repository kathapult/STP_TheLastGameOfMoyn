#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <vector>

// Keeps the best completed Invader rounds in descending score order. The
// owner decides which rounds qualify; this class only maintains the fixed-size
// ranking and reports where a newly recorded score landed.
class InvaderHighScores {
public:
	static constexpr std::size_t capacity = 10;

	// Returns the new zero-based rank, or -1 when a full table already contains
	// ten strictly better or equal scores. Equal scores stay in play order.
	int record(int score) {
		auto position = std::upper_bound(
			entries.begin(), entries.end(), score, std::greater<int>());

		if (position == entries.end() && entries.size() >= capacity) {
			return -1;
		}

		int rank = static_cast<int>(std::distance(entries.begin(), position));
		entries.insert(position, score);

		if (entries.size() > capacity) {
			entries.resize(capacity);
		}

		return rank;
	}

	void clear() {
		entries.clear();
	}

	const std::vector<int> & getEntries() const {
		return entries;
	}

private:
	std::vector<int> entries;
};
