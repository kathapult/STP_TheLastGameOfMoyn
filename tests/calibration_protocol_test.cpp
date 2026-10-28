#include "CalibrationProtocol.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <set>

using namespace CalibrationProtocol;

int main() {
	static_assert(static_cast<int32_t>(Field::Count) == 25);
	static_assert(legacyStateArgumentCount == 27);
	static_assert(tableMaskStateArgumentCount == 33);
	static_assert(stateArgumentCount == 35);
	static_assert(maximumTrackDatagramBytes == 1204);
	assert(isCompleteStateArgumentCount(legacyStateArgumentCount));
	assert(isCompleteStateArgumentCount(tableMaskStateArgumentCount));
	assert(isCompleteStateArgumentCount(stateArgumentCount));
	assert(isCompleteStateArgumentCount(stateArgumentCount + 1));
	assert(!isCompleteStateArgumentCount(legacyStateArgumentCount + 1));
	assert(!isCompleteStateArgumentCount(tableMaskStateArgumentCount + 1));

	std::set<int32_t> wireIds;
	for (const FieldDescriptor & item : fields) {
		assert(wireIds.insert(static_cast<int32_t>(item.field)).second);
		assert(item.name != nullptr && item.name[0] != '\0');
		assert(item.step > 0.0f);
	}

	SettingsState state;
	assert(isSaneSettings(state));
	assert(setFieldValue(state, Field::NearMM, 995.0f));
	assert(fieldValue(state, Field::NearMM) == 995.0f);
	assert(!isSaneSettings(state)); // farMM is now less than the required span.
	state.farMM = 1100.0f;
	assert(isSaneSettings(state));

	assert(!setFieldValue(state, Field::NearMM,
		std::numeric_limits<float>::quiet_NaN()));
	assert(!setFieldValue(state, Field::MorphOpenRadius, 1.5f));
	assert(!setFieldValue(state, Field::UsePalmCentreForHands, 2.0f));
	assert(setFieldValue(state, Field::UsePalmCentreForHands, 0.0f));
	assert(!state.usePalmCentreForHands);
	assert(state.handPositionMode == HandPositionMode::ContourCentroid);
	assert(!setFieldValue(state, Field::HandPositionMode, 3.0f));
	assert(setFieldValue(state, Field::HandPositionMode, 2.0f));
	assert(state.handPositionMode == HandPositionMode::InwardTip);
	assert(state.usePalmCentreForHands);
	assert(setFieldValue(state, Field::InwardTipCapFraction, 0.2f));
	assert(state.inwardTipCapFraction == 0.2f);
	assert(setFieldValue(state, Field::TableSurfaceClearanceMM, 7.0f));
	assert(state.tableSurfaceClearanceMM == 7.0f);
	state.tableSurfaceClearanceMM = 201.0f;
	assert(!isSaneSettings(state));
	state.tableSurfaceClearanceMM = 15.0f;
	assert(isSaneSettings(state));

	TableMaskState tableMask;
	assert(isSaneTableMaskState(tableMask));
	tableMask.supported = true;
	tableMask.requiredFrames = 15;
	tableMask.captureState = TableMaskCaptureState::Capturing;
	tableMask.capturedFrames = 7;
	assert(isSaneTableMaskState(tableMask));
	tableMask.capturedFrames = 16;
	assert(!isSaneTableMaskState(tableMask));
	tableMask.capturedFrames = 0;
	tableMask.captureState = TableMaskCaptureState::Ready;
	assert(!isSaneTableMaskState(tableMask));
	tableMask.active = true;
	assert(isSaneTableMaskState(tableMask));

	assert(nextRequestId(0) == 1);
	assert(nextRequestId(1) == 2);
	assert(nextRequestId(maximumId) == 1);
	assert(nextRevision(-1) == 0);
	assert(nextRevision(maximumId) == 0);
	assert(isRevisionNewer(0, maximumId));
	assert(!isRevisionNewer(maximumId, 0));

	Request first { Command::Set, 7, 3, Field::RoiX, 0.2f };
	Request duplicate = first;
	Request conflicting = first;
	conflicting.value = 0.3f;
	Request stale = first;
	stale.requestId = 8;
	stale.expectedRevision = 2;
	assert(isValidRequestIdentity(first));
	assert(hasExpectedRevision(first, 3));
	assert(!hasExpectedRevision(stale, 3));
	assert(sameRequest(first, duplicate));
	assert(!sameRequest(first, conflicting));
	Request capture { Command::CaptureTableMask, 9, 3, Field::NearMM, 0.0f };
	assert(sameRequest(capture, capture));
	assert(stale.expectedRevision != 3);
	assert(maximumRequestSendCount == 4);
	assert(requestRetryIntervalSeconds == 0.35f);

	std::cout << "calibration protocol tests passed\n";
	return 0;
}
