#include "ObjectTracker.h"
#include <algorithm>
#include <cmath>

void ObjectTracker::reset() {
	tracks.clear();
	primaryId = -1;
	primaryLostSeconds = 0.0f;
	primaryPresent = false;
	seenCount = 0;
	usableCount = 0;
}

const ObjectTracker::Track * ObjectTracker::find(int id) const {
	auto found = std::find_if(tracks.begin(), tracks.end(), [id](const Track & track) {
		return track.id == id;
	});

	return found == tracks.end() ? nullptr : &(*found);
}

bool ObjectTracker::isUsable(const Track & track) const {
	return track.unseenSeconds <= holdSeconds && track.stillSeconds < inertSeconds;
}

void ObjectTracker::update(const std::vector<DetectedObject> & detectedObjects, float deltaTime) {
	// Everything ages first; anything that arrives below has its clock put
	// back to zero. Done this way round so a track that is simply missing
	// needs no bookkeeping of its own.
	for (auto & track : tracks) {
		track.unseenSeconds += deltaTime;

		// Dead-reckoned forward from the last known velocity while still
		// held, fading to a stop by holdSeconds rather than running at full
		// speed for the whole window - a missing track is still moving the
		// way it was moving right up until it is not, but a gap that turns
		// out to be a genuine departure should coast to a halt rather than
		// keep sliding in a straight line. KinectOSCInput does the same
		// dead-reckoning one layer down, for the same reason: a hand that
		// drops out for a frame or two should look like it kept moving, not
		// like it froze and then jumped on the frame it reappears.
		if (track.unseenSeconds <= holdSeconds) {
			float carryThrough = 1.0f - ofClamp(track.unseenSeconds / std::max(holdSeconds, 0.001f), 0.0f, 1.0f);
			track.x = ofClamp(track.x + track.filterX.getDerivative() * deltaTime * carryThrough, 0.0f, 1.0f);
			track.y = ofClamp(track.y + track.filterY.getDerivative() * deltaTime * carryThrough, 0.0f, 1.0f);
		}
	}

	seenCount = 0;

	for (const auto & obj : detectedObjects) {
		// Anything not finite would pass straight through into whatever a
		// caller steers or pushes with and leave it nowhere it can recover
		// from.
		if (!std::isfinite(obj.x) || !std::isfinite(obj.y) || !std::isfinite(obj.z)) {
			continue;
		}

		seenCount++;

		float x = ofClamp(obj.x, 0.0f, 1.0f);
		float y = ofClamp(obj.y, 0.0f, 1.0f);
		float z = ofClamp(obj.z, 0.0f, 1.0f);

		auto found = std::find_if(tracks.begin(), tracks.end(), [&obj](const Track & track) {
			return track.id == obj.id;
		});

		if (found == tracks.end()) {
			Track track;
			track.id = obj.id;
			track.rawX = x;
			track.rawY = y;
			track.anchorX = x;
			track.anchorY = y;
			track.type = obj.type;

			track.filterX.setParams(minCutoff, beta);
			track.filterY.setParams(minCutoff, beta);
			track.filterZ.setParams(minCutoff, beta);

			// First sample: the filters have no history yet, so this is
			// exactly x/y/z reported straight through - see
			// OneEuroFilter::reset().
			track.x = track.filterX.filter(x, deltaTime);
			track.y = track.filterY.filter(y, deltaTime);
			track.z = track.filterZ.filter(z, deltaTime);

			tracks.push_back(track);
			continue;
		}

		found->rawX = x;
		found->rawY = y;
		found->type = obj.type;
		found->unseenSeconds = 0.0f;
		found->seenSeconds += deltaTime;

		// Re-applied every frame rather than only when the track was first
		// seen, so a value tuned at the table takes hold immediately on
		// every track already in flight, not just the next new one.
		found->filterX.setParams(minCutoff, beta);
		found->filterY.setParams(minCutoff, beta);
		found->filterZ.setParams(minCutoff, beta);

		found->x = found->filterX.filter(x, deltaTime);
		found->y = found->filterY.filter(y, deltaTime);
		found->z = found->filterZ.filter(z, deltaTime);

		// Measured against the raw position and the anchor, both unfiltered
		// - see the comment on Track::rawX for why the filtered x/y would be
		// the wrong signal to use here.
		float moved = std::hypot(found->rawX - found->anchorX, found->rawY - found->anchorY);

		if (moved > moveEpsilon) {
			found->anchorX = found->rawX;
			found->anchorY = found->rawY;
			found->stillSeconds = 0.0f;
		} else {
			found->stillSeconds += deltaTime;
		}
	}

	// Ids that are never coming back would otherwise pile up for as long as
	// the screen is showing.
	tracks.erase(
		std::remove_if(tracks.begin(), tracks.end(), [this](const Track & track) {
			return track.unseenSeconds > trackForgetSeconds;
		}),
		tracks.end());
}

const ObjectTracker::Track * ObjectTracker::chooseExclusive(float deltaTime, const std::function<bool(const Track &)> & typeAllowed) {
	usableCount = 0;

	for (const auto & track : tracks) {
		if (isUsable(track) && typeAllowed(track)) {
			usableCount++;
		}
	}

	// The track that is already driving keeps driving, as long as it still
	// qualifies. Everything else is ignored while it does.
	const Track * primary = primaryId < 0 ? nullptr : find(primaryId);

	if (primary != nullptr && isUsable(*primary) && typeAllowed(*primary)) {
		primaryLostSeconds = 0.0f;
		primaryPresent = true;
		return primary;
	}

	primaryPresent = false;

	// It has gone. Whether that is a departure or a couple of dropped frames
	// is not knowable yet, so nothing is handed over until it has been gone
	// a while - long enough that a second person leaning in cannot take over
	// from whoever is already driving.
	if (primaryId >= 0) {
		primaryLostSeconds += deltaTime;

		if (primaryLostSeconds < handoverSeconds) {
			return nullptr;
		}
	}

	// Free to choose: whichever qualifying track has been seen longest.
	// First come, first driven - both the fairest rule and the steadiest
	// one, since it does not change its mind when somebody reaches further.
	const Track * best = nullptr;

	for (const auto & track : tracks) {
		if (!isUsable(track) || !typeAllowed(track)) {
			continue;
		}

		if (best == nullptr || track.seenSeconds > best->seenSeconds) {
			best = &track;
		}
	}

	primaryId = best == nullptr ? -1 : best->id;
	primaryLostSeconds = 0.0f;
	primaryPresent = best != nullptr;

	return best;
}
