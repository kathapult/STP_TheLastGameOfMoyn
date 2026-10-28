#include "InvaderHighScores.h"

#include <cassert>
#include <iostream>
#include <vector>

int main() {
	InvaderHighScores highScores;
	assert(highScores.getEntries().empty());

	assert(highScores.record(120) == 0);
	assert(highScores.record(500) == 0);
	assert(highScores.record(250) == 1);
	assert(highScores.record(500) == 1);
	assert((highScores.getEntries() == std::vector<int> { 500, 500, 250, 120 }));

	for (int score : { 1000, 900, 800, 700, 600, 400, 300 }) {
		highScores.record(score);
	}

	assert(highScores.getEntries().size() == InvaderHighScores::capacity);
	assert((highScores.getEntries() == std::vector<int> {
		1000, 900, 800, 700, 600, 500, 500, 400, 300, 250
	}));

	assert(highScores.record(200) == -1);
	assert(highScores.record(250) == -1);
	assert(highScores.record(550) == 5);
	assert((highScores.getEntries() == std::vector<int> {
		1000, 900, 800, 700, 600, 550, 500, 500, 400, 300
	}));

	highScores.clear();
	assert(highScores.getEntries().empty());
	assert(highScores.record(0) == 0);
	assert((highScores.getEntries() == std::vector<int> { 0 }));

	std::cout << "Invader high-score tests passed\n";
	return 0;
}
