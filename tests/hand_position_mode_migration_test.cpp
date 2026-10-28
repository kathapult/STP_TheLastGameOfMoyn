#include "HandPositionModeMigration.h"

#include <cassert>
#include <iostream>

int main() {
	using CalibrationProtocol::HandPositionMode;
	using HandPositionModeMigration::apply;

	// The former production default migrates exactly once.
	assert(apply(1, HandPositionMode::PalmCentre)
		== HandPositionMode::InwardTip);
	assert(apply(HandPositionModeMigration::currentVersion,
		HandPositionMode::PalmCentre) == HandPositionMode::PalmCentre);

	// Existing explicit centroid/inward choices and future versions are stable.
	assert(apply(1, HandPositionMode::ContourCentroid)
		== HandPositionMode::ContourCentroid);
	assert(apply(1, HandPositionMode::InwardTip)
		== HandPositionMode::InwardTip);
	assert(apply(HandPositionModeMigration::currentVersion + 1,
		HandPositionMode::PalmCentre) == HandPositionMode::PalmCentre);

	std::cout << "hand position mode migration tests passed\n";
	return 0;
}
