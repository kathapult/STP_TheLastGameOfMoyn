#include "HandClassificationPolicy.h"
#include "InwardInteractionPointSelector.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
struct Mask {
	int width;
	int height;
	std::vector<std::uint8_t> pixels;

	Mask(int width, int height)
		: width(width), height(height), pixels(static_cast<size_t>(width * height), 0) {
	}

	void set(int x, int y) {
		pixels[static_cast<size_t>(y * width + x)] = 255;
	}

	void rectangle(int left, int top, int right, int bottom) {
		for (int y = top; y <= bottom; ++y) {
			for (int x = left; x <= right; ++x) set(x, y);
		}
	}

	InwardInteractionPointSelector::MaskView view() const {
		return { pixels.data(), width, height, width };
	}
};

bool close(float actual, float expected, float tolerance = 0.75f) {
	return std::fabs(actual - expected) <= tolerance;
}

Mask fullTable(int width = 24, int height = 24) {
	Mask roi(width, height);
	roi.rectangle(1, 1, width - 2, height - 2);
	return roi;
}
}

int main() {
	const Mask roi = fullTable();

	Mask fromTop(24, 24);
	fromTop.rectangle(10, 1, 12, 13);
	fromTop.rectangle(8, 10, 14, 15);
	auto top = InwardInteractionPointSelector::select(
		fromTop.view(), roi.view(), 0.15f, 5.0f);
	assert(top.valid);
	assert(top.contactComponentCount == 1);
	assert(close(top.entryY, 1.0f));
	assert(top.y > 13.5f);
	assert(close(top.x, 11.0f));

	// The selector's minimum is also the detector's scale-independent
	// elongated-reach gate. A round/shallow edge blob cannot become the hand
	// exception, while the same-width reach extending into the table can.
	Mask shallowFromTop(24, 24);
	shallowFromTop.rectangle(8, 1, 14, 8);
	const float elongatedMinimum =
		InwardInteractionPointSelector::minimumPenetrationForElongatedReach(3.5f);
	assert(close(elongatedMinimum, 10.5f, 0.001f));
	auto shallow = InwardInteractionPointSelector::select(
		shallowFromTop.view(), roi.view(), 0.15f, elongatedMinimum);
	assert(!shallow.valid);
	assert(shallow.penetrationPixels < elongatedMinimum);
	auto elongated = InwardInteractionPointSelector::select(
		fromTop.view(), roi.view(), 0.15f, elongatedMinimum);
	assert(elongated.valid);
	assert(elongated.y > 13.5f);

	// ObjectDetector evaluates a cropped local view. The maximum x/y sides need
	// one empty padding pixel: without it, leaving the MaskView at the crop edge
	// looks like leaving the table ROI and manufactures a second entry contact.
	Mask croppedTopWithoutMaxPadding(8, 17);
	Mask croppedRoiWithoutMaxPadding(8, 17);
	croppedRoiWithoutMaxPadding.rectangle(0, 1, 7, 16);
	croppedTopWithoutMaxPadding.rectangle(3, 1, 5, 13);
	croppedTopWithoutMaxPadding.rectangle(1, 10, 7, 15);
	auto falseCropEntry = InwardInteractionPointSelector::select(
		croppedTopWithoutMaxPadding.view(), croppedRoiWithoutMaxPadding.view(),
		0.15f, 5.0f);
	assert(!falseCropEntry.valid);
	assert(falseCropEntry.contactComponentCount == 2);

	Mask croppedTopPadded(9, 18);
	Mask croppedRoiPadded(9, 18);
	croppedRoiPadded.rectangle(0, 1, 8, 17);
	croppedTopPadded.rectangle(3, 1, 5, 13);
	croppedTopPadded.rectangle(1, 10, 7, 15);
	auto paddedCrop = InwardInteractionPointSelector::select(
		croppedTopPadded.view(), croppedRoiPadded.view(), 0.15f, 5.0f);
	assert(paddedCrop.valid);
	assert(paddedCrop.contactComponentCount == 1);
	assert(close(paddedCrop.entryY, 1.0f));

	Mask fromLeft(24, 24);
	fromLeft.rectangle(1, 10, 13, 12);
	fromLeft.rectangle(10, 8, 15, 14);
	auto left = InwardInteractionPointSelector::select(
		fromLeft.view(), roi.view(), 0.15f, 5.0f);
	assert(left.valid);
	assert(close(left.entryX, 1.0f));
	assert(left.x > 13.5f);
	assert(close(left.y, 11.0f));

	// A bent component is measured through its own pixels. Straight Euclidean
	// distance from the entry would underestimate the end after the turn.
	Mask bent(24, 24);
	bent.rectangle(10, 1, 12, 10);
	bent.rectangle(10, 8, 20, 10);
	auto bend = InwardInteractionPointSelector::select(
		bent.view(), roi.view(), 0.10f, 5.0f);
	assert(bend.valid);
	assert(bend.x > 18.0f);
	assert(bend.y > 7.5f && bend.y < 10.5f);

	// Equal fingertips form one stable distal cap. A one-pixel extension of
	// one finger must move the aggregate point only modestly, not jump to it.
	Mask fingers(24, 24);
	fingers.rectangle(10, 1, 12, 10);
	fingers.rectangle(7, 8, 15, 13);
	fingers.rectangle(7, 13, 9, 18);
	fingers.rectangle(13, 13, 15, 18);
	auto even = InwardInteractionPointSelector::select(
		fingers.view(), roi.view(), 0.20f, 5.0f);
	assert(even.valid);
	assert(close(even.x, 11.0f, 1.0f));
	fingers.set(15, 19);
	auto noisy = InwardInteractionPointSelector::select(
		fingers.view(), roi.view(), 0.20f, 5.0f);
	assert(noisy.valid);
	assert(std::fabs(noisy.x - even.x) < 2.5f);

	Mask interior(24, 24);
	interior.rectangle(8, 8, 15, 15);
	auto noEntry = InwardInteractionPointSelector::select(
		interior.view(), roi.view(), 0.15f, 5.0f);
	assert(!noEntry.valid);
	assert(noEntry.contactPixelCount == 0);

	// Two disconnected border contacts are deliberately ambiguous and must
	// make the caller use the palm/centroid fallback.
	Mask twoEntries(24, 24);
	twoEntries.rectangle(4, 1, 6, 12);
	twoEntries.rectangle(17, 1, 19, 12);
	twoEntries.rectangle(4, 10, 19, 13);
	auto ambiguous = InwardInteractionPointSelector::select(
		twoEntries.view(), roi.view(), 0.15f, 5.0f);
	assert(!ambiguous.valid);
	assert(ambiguous.contactComponentCount == 2);

	// A curved corner in the ROI still supplies the correct local entry; the
	// selector never assumes that the table boundary is axis-aligned.
	Mask roundedRoi(24, 24);
	for (int y = 1; y < 23; ++y) {
		for (int x = 1; x < 23; ++x) {
			const float dx = x - 6.0f;
			const float dy = y - 6.0f;
			if (x >= 6 || y >= 6 || dx * dx + dy * dy <= 25.0f) roundedRoi.set(x, y);
		}
	}
	Mask cornerHand(24, 24);
	for (int step = 0; step < 10; ++step) {
		cornerHand.rectangle(3 + step, 3 + step, 5 + step, 5 + step);
	}
	auto corner = InwardInteractionPointSelector::select(
		cornerHand.view(), roundedRoi.view(), 0.15f, 4.0f);
	assert(corner.valid);
	assert(corner.x > 10.0f && corner.y > 10.0f);

	// Activation is time-based rather than frame-count based. A brief candidate
	// never switches sources; after activation one short dropout is held, while
	// a longer one releases back to the caller's palm/centroid fallback.
	using TemporalState = InwardInteractionPointSelector::TemporalState;
	TemporalState temporal;
	temporal = InwardInteractionPointSelector::stabilize(
		temporal, true, 1.00f, 0.12f, 0.25f);
	assert(!temporal.active);
	temporal = InwardInteractionPointSelector::stabilize(
		temporal, true, 1.08f, 0.12f, 0.25f);
	assert(!temporal.active);
	temporal = InwardInteractionPointSelector::stabilize(
		temporal, true, 1.13f, 0.12f, 0.25f);
	assert(temporal.active && !temporal.holdPrevious);
	assert(HandClassificationPolicy::looksLikeHand(
		false, true, false, temporal.active));
	temporal = InwardInteractionPointSelector::stabilize(
		temporal, false, 1.30f, 0.12f, 0.25f);
	assert(temporal.active && temporal.holdPrevious);
	assert(HandClassificationPolicy::looksLikeHand(
		false, false, false, temporal.active));
	temporal = InwardInteractionPointSelector::stabilize(
		temporal, false, 1.39f, 0.12f, 0.25f);
	assert(!temporal.active && !temporal.holdPrevious);
	assert(!HandClassificationPolicy::looksLikeHand(
		false, false, false, temporal.active));

	std::cout << "inward interaction point tests passed\n";
	return 0;
}
