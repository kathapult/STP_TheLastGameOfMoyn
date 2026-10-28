#include "PixelPush.h"

PixelPush::PixelPush()
	: TransformBase() {
	// positionTracker's own tuning - see ObjectTracker for what each of these
	// does in general. Fairly responsive (a high beta): a push is meant to
	// follow the hand closely, and minCutoff alone is what still calms a
	// resting hand's jitter. The exclusive-driver machinery (moveEpsilon,
	// inertSeconds, handoverSeconds) is left at ObjectTracker's own defaults
	// since nothing here uses chooseExclusive() - every object gets to push,
	// not just one "driving" hand.
	positionTracker.minCutoff = 1.2f;
	positionTracker.beta = 1.2f;
}

void PixelPush::setup() {
	// Nothing is being pushed before the first hand arrives, so all three fields
	// start empty and transform() is an identity until update() says otherwise.
	pushX.fill(0.0f);
	pushY.fill(0.0f);
	pushField.fill(0.0f);

	boundsActive = false;
	boundsMinX = 0;
	boundsMinY = 0;
	boundsMaxX = 0;
	boundsMaxY = 0;
	maxDisplacement = 0.0f;

	// A fresh screen should not inherit smoothed positions or directions from
	// whatever was over the table on a previous visit to it.
	positionTracker.reset();
	directionFilters.clear();
}

// Drops directionFilters entries for ids positionTracker itself has already
// forgotten (see ObjectTracker::trackForgetSeconds) - piggy-backed on
// positionTracker's own forgetting rather than keeping a second timer, so
// this map cannot grow without bound over a long show either.
void PixelPush::forgetStaleDirectionFilters() {
	for (auto it = directionFilters.begin(); it != directionFilters.end();) {
		if (positionTracker.find(it->first) == nullptr) {
			it = directionFilters.erase(it);
		} else {
			++it;
		}
	}
}

// The height gate, worked out from how high hands actually get over this table
// rather than fixed in advance - see pushHeightFraction for why.
float PixelPush::getPushHeightThreshold() const {
	return ofClamp(observedHeightCeiling * pushHeightFraction, minPushHeight, maxPushHeight);
}

float PixelPush::adjustPushHeightFraction(float delta) {
	pushHeightFraction = ofClamp(pushHeightFraction + delta, 0.1f, 1.0f);
	return pushHeightFraction;
}

float PixelPush::adjustReachScale(float delta) {
	reachScale = ofClamp(reachScale + delta, 0.3f, 3.0f);
	return reachScale;
}

// Widens the active rectangle to cover a circle of the given radius, clipped to
// the grid. On the first call after the field was empty the rectangle is set
// rather than grown, since there is nothing yet to grow from.
void PixelPush::growActiveBounds(int centreX, int centreY, int radius) {
	int minX = ofClamp(centreX - radius, 0, GRID_WIDTH - 1);
	int maxX = ofClamp(centreX + radius, 0, GRID_WIDTH - 1);
	int minY = ofClamp(centreY - radius, 0, GRID_HEIGHT - 1);
	int maxY = ofClamp(centreY + radius, 0, GRID_HEIGHT - 1);

	if (!boundsActive) {
		boundsMinX = minX;
		boundsMaxX = maxX;
		boundsMinY = minY;
		boundsMaxY = maxY;
		boundsActive = true;
		return;
	}

	boundsMinX = std::min(boundsMinX, minX);
	boundsMaxX = std::max(boundsMaxX, maxX);
	boundsMinY = std::min(boundsMinY, minY);
	boundsMaxY = std::max(boundsMaxY, maxY);
}

// Builds this frame's displacement field.
//
// The direction a hand pushes in is simply the direction it is moving: material
// gets shoved the way the hand travels, which is the one mapping a visitor
// doesn't have to be told about. That also means a motionless hand pushes
// nothing, which is why the height gate alone isn't enough and speed scales the
// strength on top of it.
void PixelPush::update(const std::vector<DetectedObject> & detectedObjects, float deltaTime) {
	// A frame that took unusually long must not translate into a correspondingly
	// long shove - see maxDeltaTime.
	deltaTime = ofClamp(deltaTime, 0.0f, maxDeltaTime);

	maxDisplacement = 0.0f;

	// Smoothed per-id position, and directionFilters' own forgetting - see
	// positionTracker's and forgetStaleDirectionFilters()'s comments. Run
	// before anything below reads a smoothed position, and before the object
	// loop so every object already has one by the time it needs it.
	positionTracker.update(detectedObjects, deltaTime);
	forgetStaleDirectionFilters();

	// How high hands are reaching over this table, which is what the height gate
	// is measured against. It jumps up to anything higher the moment it is seen,
	// so somebody reaching in high is taken into account immediately, and sinks
	// back towards the default over ceilingDecaySeconds so that one person
	// waving does not leave the piece over-generous for the rest of the evening.
	for (const auto & obj : detectedObjects) {
		if (std::isfinite(obj.z)) {
			observedHeightCeiling = std::max(observedHeightCeiling, ofClamp(obj.z, 0.0f, 1.0f));
		}
	}

	observedHeightCeiling += (restingHeightCeiling - observedHeightCeiling)
		* ofClamp(deltaTime / ceilingDecaySeconds, 0.0f, 1.0f);

	float pushHeightThreshold = getPushHeightThreshold();

	// The previous frame's displacement is faded rather than cleared, so
	// material carries on drifting for a moment after the hand stops. Only the
	// active rectangle is walked - outside it every value is already exactly
	// zero, and multiplying zero by the decay would keep 15,000 cells busy
	// producing zeros.
	if (boundsActive) {
		bool anyLeft = false;

		for (int y = boundsMinY; y <= boundsMaxY; y++) {
			for (int x = boundsMinX; x <= boundsMaxX; x++) {
				int index = x + y * GRID_WIDTH;

				float decayedX = pushX[index] * pushDecay;
				float decayedY = pushY[index] * pushDecay;

				// Decay only ever approaches zero. Snapping the last sliver away
				// is what lets the rectangle below ever be released again.
				if (std::fabs(decayedX) < pushEpsilon && std::fabs(decayedY) < pushEpsilon) {
					pushX[index] = 0.0f;
					pushY[index] = 0.0f;
					pushField[index] = 0.0f;
				} else {
					pushX[index] = decayedX;
					pushY[index] = decayedY;
					pushField[index] = pushField[index] * pushDecay;
					anyLeft = true;

					maxDisplacement = std::max(maxDisplacement,
						std::max(std::fabs(decayedX), std::fabs(decayedY)));
				}
			}
		}

		// Everything faded out: drop the rectangle so transform() can hand the
		// grid straight back until somebody touches the table again.
		if (!anyLeft) {
			boundsActive = false;
		}
	}

	for (const auto & obj : detectedObjects) {
		// Anything not finite would survive ofClamp() untouched - a comparison
		// against NaN is false either way - and end up as an out-of-range array
		// index in transform(). Cheaper to reject the object than to defend
		// every step downstream of it.
		if (!std::isfinite(obj.x) || !std::isfinite(obj.y) || !std::isfinite(obj.z)
			|| !std::isfinite(obj.area) || !std::isfinite(obj.vx) || !std::isfinite(obj.vy)) {
			continue;
		}

		// Fades from full strength at the threshold down to nothing
		// pushHeightFadeBand above it, rather than the hard gate this used to
		// be - see pushHeightFadeBand's own comment.
		float heightScale = ofClamp(1.0f - (obj.z - pushHeightThreshold) / pushHeightFadeBand, 0.0f, 1.0f);

		if (heightScale <= 0.0f) {
			continue; // held clearly above the fade band - passes over untouched
		}

		// Direction, smoothed per id rather than read raw off obj.vx/obj.vy
		// this frame - see directionFilters' own comment. Re-applied every
		// lookup so a value tuned at the table takes hold immediately.
		DirectionFilter & direction = directionFilters[obj.id];
		direction.vx.setParams(directionMinCutoff, directionBeta);
		direction.vy.setParams(directionMinCutoff, directionBeta);
		float smoothedVX = direction.vx.filter(obj.vx, deltaTime);
		float smoothedVY = direction.vy.filter(obj.vy, deltaTime);

		float speed = std::sqrt(smoothedVX * smoothedVX + smoothedVY * smoothedVY);

		// Ramps from 0 at minPushSpeedFadeFloor up to full strength at
		// minPushSpeed, rather than the hard gate this used to be at
		// minPushSpeed alone - see minPushSpeedFadeFloor's own comment. Below
		// the floor this is 0 and the object is skipped, same as the old gate;
		// the difference is only in the frames in between the two.
		float speedScale = ofClamp(
			ofMap(speed, minPushSpeedFadeFloor, speedAtFullPush, 0.0f, 1.0f, true), 0.0f, 1.0f);

		if (speedScale <= 0.0f) {
			continue; // at or below the floor - direction is tracker noise, nothing to push with
		}

		// Unit vector along the smoothed movement. Safe to divide: speedScale
		// being positive already means speed is above minPushSpeedFadeFloor.
		float dirX = smoothedVX / speed;
		float dirY = smoothedVY / speed;

		float typeScale = (obj.type == ObjectType::Object) ? staticObjectPush : 1.0f;

		// Distance this object displaces material at the centre of its reach,
		// converted from cells-per-second into cells-this-frame.
		float distance = maxPushDistance * speedScale * typeScale * heightScale * deltaTime;

		// Smoothed position, not the raw obj.x/obj.y this frame - see
		// positionTracker's own comment. Falls back to the raw value only if
		// the track is somehow missing, which should not happen since
		// positionTracker was just updated from this same detectedObjects
		// list moments ago.
		const ObjectTracker::Track * track = positionTracker.find(obj.id);
		float px = track != nullptr ? track->x : obj.x;
		float py = track != nullptr ? track->y : obj.y;

		// map normalized, smoothed position (0.0-1.0) to grid coordinates
		int gx = static_cast<int>(px * GRID_WIDTH);
		int gy = static_cast<int>(py * GRID_HEIGHT);

		gx = ofClamp(gx, 0, GRID_WIDTH - 1);
		gy = ofClamp(gy, 0, GRID_HEIGHT - 1);

		// area is a share of the table, so it stays a small number even for a
		// whole hand - sqrt turns it into something proportional to a radius,
		// which makes the reach grow evenly instead of barely moving and then
		// jumping. Same conversion GameOfLife uses, for the same reason.
		// max(area, 0) because a negative would make sqrt return NaN.
		float reach = ofClamp(std::sqrt(std::max(obj.area, 0.0f) / areaAtMaxPush), 0.0f, 1.0f);
		int radius = static_cast<int>(ofMap(reach, 0.0f, 1.0f, minPushRadius, maxPushRadius) * reachScale);

		// At least one cell, and never so wide that a single object reaches
		// across the whole table - beyond that the mask's own shape stops
		// mattering and everything shoves everything.
		radius = ofClamp(radius, 1, GRID_HEIGHT / 2);

		growActiveBounds(gx, gy, radius);

		for (int dy = -radius; dy <= radius; dy++) {
			for (int dx = -radius; dx <= radius; dx++) {

				int nx = gx + dx;
				int ny = gy + dy;

				if (nx < 0 || ny < 0 || nx >= GRID_WIDTH || ny >= GRID_HEIGHT) {
					continue;
				}

				float cellDistance = std::sqrt(static_cast<float>(dx * dx + dy * dy));

				if (cellDistance > radius) {
					continue;
				}

				int index = nx + ny * GRID_WIDTH;
				float t = ofClamp(cellDistance / radius, 0.0f, 1.0f);
				float falloff = 1.0f - (t * t * (3.0f - 2.0f * t)); // smoothstep

				// The falloff is what makes material heap up: cells near the
				// centre are displaced further than cells at the edge of the
				// reach, so the fast ones catch up with the slow ones and their
				// mass lands in the same place. A uniform push would just slide
				// the whole patch across without ever forming a pile.
				pushX[index] += dirX * distance * falloff;
				pushY[index] += dirY * distance * falloff;
				pushField[index] = std::min(pushField[index] + falloff * speedScale * heightScale, 1.0f);

				maxDisplacement = std::max(maxDisplacement,
					std::max(std::fabs(pushX[index]), std::fabs(pushY[index])));
			}
		}
	}
}

// Moves each cell's mass to where the displacement field points, splitting it
// across the four cells around that destination. Summed over the whole grid the
// mass that goes in is the mass that comes out (bar the cap below), which is
// what keeps the material from quietly evaporating as it gets pushed about.
//
// Only the cells inside the active rectangle are rebuilt. Everywhere else the
// displacement is exactly zero, so a cell would be moved onto itself - copying
// the grid and reworking just the rectangle does the same thing while touching
// a few hundred cells instead of fifteen thousand.
PixelPush::Grid PixelPush::transform(const Grid & grid) {
	// Nobody is pushing and nothing is still drifting.
	if (!boundsActive) {
		return grid;
	}

	Grid newGrid = grid;

	// Emptied first, because these cells' mass is about to be re-placed by the
	// scatter below. Mass that lands outside the rectangle is added on top of
	// what is already there, which is a heap forming against undisturbed
	// material - correct, and the reason the rectangle needs no safety margin.
	for (int y = boundsMinY; y <= boundsMaxY; y++) {
		for (int x = boundsMinX; x <= boundsMaxX; x++) {
			newGrid[x + y * GRID_WIDTH] = 0.0f;
		}
	}

	for (int y = boundsMinY; y <= boundsMaxY; y++) {
		for (int x = boundsMinX; x <= boundsMaxX; x++) {
			int index = x + y * GRID_WIDTH;
			float mass = grid[index];

			// Empty cell - nothing to move. Only worth testing because
			// massEpsilon keeps genuinely empty cells at exactly zero.
			if (mass <= 0.0f) {
				continue;
			}

			// The whole of a cell's mass lands on one cell - the nearest one to
			// where it was pushed - rather than being shared out across the four
			// cells around a fractional destination.
			//
			// Splitting it looked like a blur and made heaps hard work: a cell's
			// mass was spread thinner every single frame it moved, so shoving
			// material together fought a smear that was undoing it. Moving whole
			// cells does the opposite - several cells rounding onto the same
			// destination stack their mass there, so a heap builds up by itself
			// as soon as material converges. The cost is that movement happens
			// in whole cells, which is what the pixels are supposed to look
			// like anyway.
			//
			// Clamped rather than dropped, so material shoved at the table's
			// edge banks up against it instead of disappearing over the side.
			int destX = ofClamp(static_cast<int>(std::lround(x + pushX[index])), 0, GRID_WIDTH - 1);
			int destY = ofClamp(static_cast<int>(std::lround(y + pushY[index])), 0, GRID_HEIGHT - 1);

			newGrid[destX + destY * GRID_WIDTH] += mass;
		}
	}

	// The rectangle, plus however far the scatter could have thrown mass past it,
	// which is what maxDisplacement tracks. Every cell that could have changed
	// this frame is inside it.
	int margin = static_cast<int>(std::ceil(maxDisplacement)) + 1;
	int sweepMinX = std::max(boundsMinX - margin, 0);
	int sweepMaxX = std::min(boundsMaxX + margin, GRID_WIDTH - 1);
	int sweepMinY = std::max(boundsMinY - margin, 0);
	int sweepMaxY = std::min(boundsMaxY + margin, GRID_HEIGHT - 1);

	// Material runs downhill: wherever a cell stands more than slumpSlope higher
	// than one of its four neighbours, part of that difference moves across.
	//
	// This replaced a version that only spilled what a cell held above a fixed
	// cap, and the way that failed is worth recording. Its neighbours ended up
	// sitting at exactly the cap, which left them with no surplus of their own,
	// so they passed nothing further on - and the cell behind them was stuck
	// holding everything. A cross-section through a heap read 3.0, 22.0, 3.0
	// with empty cells two steps away: a spike wedged in place by a full ring,
	// not a heap. Comparing against the neighbour's actual height rather than
	// against a cap is what lets material keep moving outwards until the whole
	// mound sits at the slope.
	//
	// Repeated, because one pass only moves material one cell - see
	// slumpIterations. A pass that moves nothing ends it, which is the normal
	// case whenever nothing has been heaped up.
	for (int pass = 0; pass < slumpIterations; pass++) {
		bool movedAny = false;

		for (int y = sweepMinY; y <= sweepMaxY; y++) {
			for (int x = sweepMinX; x <= sweepMaxX; x++) {
				int index = x + y * GRID_WIDTH;

				// Only bites when something upstream adds mass faster than a
				// heap can spread - see absoluteCellCeiling.
				newGrid[index] = std::min(newGrid[index], absoluteCellCeiling);

				float here = newGrid[index];

				// Nothing here can be more than slumpSlope above anything.
				if (here <= slumpSlope) {
					continue;
				}

				int neighbours[4];
				int neighbourCount = 0;

				if (x > 0) {
					neighbours[neighbourCount++] = index - 1;
				}
				if (x < GRID_WIDTH - 1) {
					neighbours[neighbourCount++] = index + 1;
				}
				if (y > 0) {
					neighbours[neighbourCount++] = index - GRID_WIDTH;
				}
				if (y < GRID_HEIGHT - 1) {
					neighbours[neighbourCount++] = index + GRID_WIDTH;
				}

				// Taken one neighbour at a time, with `here` kept up to date, so
				// a cell can never give away more than it has even when all four
				// of them lie far below it.
				for (int n = 0; n < neighbourCount; n++) {
					float difference = here - newGrid[neighbours[n]];

					if (difference <= slumpSlope) {
						continue;
					}

					float moved = (difference - slumpSlope) * slumpRate;

					newGrid[neighbours[n]] += moved;
					here -= moved;
					movedAny = true;
				}

				newGrid[index] = here;
			}
		}

		if (!movedAny) {
			break;
		}
	}

	// Slumping shares out fractions, so it leaves values that are too small to
	// see but not small enough to skip. Snapped away once at the end rather than
	// inside every pass - see massEpsilon for what they cost if left.
	for (int y = sweepMinY; y <= sweepMaxY; y++) {
		for (int x = sweepMinX; x <= sweepMaxX; x++) {
			int index = x + y * GRID_WIDTH;

			if (newGrid[index] > 0.0f && newGrid[index] < massEpsilon) {
				newGrid[index] = 0.0f;
			}
		}
	}

	return newGrid;
}
