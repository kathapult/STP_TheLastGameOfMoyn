#pragma once

#include "CalibrationProtocol.h"

// One-time settings migration for the interaction point. Releases before
// version 2 wrote "palm" as the normal production value, including into the
// persistent /var/lib file. Versioning this narrow setting lets those files
// adopt InwardTip once while still respecting an operator who explicitly saves
// PalmCentre after the upgrade.
namespace HandPositionModeMigration {

constexpr int currentVersion = 2;

inline CalibrationProtocol::HandPositionMode apply(int storedVersion,
	CalibrationProtocol::HandPositionMode mode) {
	if (storedVersion < currentVersion
		&& mode == CalibrationProtocol::HandPositionMode::PalmCentre) {
		return CalibrationProtocol::HandPositionMode::InwardTip;
	}

	return mode;
}

} // namespace HandPositionModeMigration
