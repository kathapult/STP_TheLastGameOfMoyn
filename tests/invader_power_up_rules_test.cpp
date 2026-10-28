#include "InvaderPowerUpRules.h"
#include <cassert>
#include <cmath>
#include <iostream>

namespace {

bool almostEqual(float a, float b) {
	return std::fabs(a - b) < 0.0001f;
}

}

int main() {
	using namespace InvaderPowerUpRules;

	// Every tier saturates, including bad negative state from a future caller.
	assert(upgradedLevel(0, maxShotPatternLevel) == 1);
	assert(upgradedLevel(1, maxShotPatternLevel) == 2);
	assert(upgradedLevel(2, maxShotPatternLevel) == 2);
	assert(upgradedLevel(-4, maxShotPatternLevel) == 1);
	assert(upgradedLevel(maxFireRateLevel, maxFireRateLevel) == maxFireRateLevel);
	assert(upgradedLevel(maxShipSpeedLevel, maxShipSpeedLevel) == maxShipSpeedLevel);

	assert(shotCount(0) == 1);
	assert(shotCount(1) == 2);
	assert(shotCount(2) == 3);
	assert(shotCount(99) == 3);

	assert(almostEqual(fireInterval(0.20f, 0), 0.20f));
	assert(almostEqual(fireInterval(0.20f, -1), 0.20f));
	assert(almostEqual(fireInterval(0.20f, 3), 0.11f));
	assert(almostEqual(fireInterval(0.20f, 99), 0.11f));

	assert(almostEqual(shipSpeed(1.7f, 0), 1.7f));
	assert(almostEqual(shipSpeed(1.7f, -1), 1.7f));
	assert(almostEqual(shipSpeed(1.7f, 3), 2.312f));
	assert(almostEqual(shipSpeed(1.7f, 99), 2.312f));

	std::cout << "Invader power-up rules test passed" << std::endl;
	return 0;
}
