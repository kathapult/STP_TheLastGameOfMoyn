#include "InvaderGame.h"
#include <algorithm>
#include <array>
#include <cmath>

void InvaderGame::setup() {
	// The vectors are given their ceiling up front: the field is capped anyway,
	// and an installation that runs for a whole festival should not be doing
	// allocations in its draw loop.
	fallers.reserve(maxFallers);
	shots.reserve(maxShots);
	powerUps.reserve(maxPowerUps);

	reset();
}

void InvaderGame::reset() {
	fallers.clear();
	shots.clear();
	explosions.clear();
	powerUps.clear();

	state = State::Playing;
	stateSeconds = 0.0f;

	playerX = 0.5f;
	playerVelocity = 0.0f;

	score = 0;
	wave = 1;
	elapsed = 0.0f;

	hitFlash = 0.0f;
	fireFlash = 0.0f;
	fireTimer = 0.0f;

	shieldActive = false;
	shieldSecondsLeft = 0.0f;
	shieldBreakFlash = 0.0f;
	recolorSecondsLeft = 0.0f;
	shotPatternLevel = 0;
	fireRateLevel = 0;
	shipSpeedLevel = 0;

	// The first faller is not spawned immediately: a round that opens with
	// something already halfway down gives whoever just walked up no moment to
	// work out what they are looking at.
	spawnTimer = baseSpawnInterval;
}

float InvaderGame::currentFallSpeed() const {
	// wave - 1, so wave one is the base speed rather than one step above it.
	float speed = baseFallSpeed + fallSpeedPerWave * static_cast<float>(wave - 1);
	return speed * difficultyScale;
}

float InvaderGame::currentSpawnInterval() const {
	float interval = baseSpawnInterval - spawnIntervalPerWave * static_cast<float>(wave - 1);

	// Divided by the difficulty rather than multiplied: turning the difficulty up
	// has to make things arrive more often, not less.
	interval /= std::max(difficultyScale, 0.01f);

	return std::max(interval, minSpawnInterval);
}

float InvaderGame::getCurrentFireInterval() const {
	return InvaderPowerUpRules::fireInterval(fireInterval, fireRateLevel);
}

float InvaderGame::getPlayerSpeed() const {
	return InvaderPowerUpRules::shipSpeed(playerSpeed, shipSpeedLevel);
}

void InvaderGame::update(float deltaTime, float targetX) {
	// A frame that took unusually long must not translate into a faller jumping
	// past the figure without ever having been drawn next to it - the same guard
	// the navigators use, and here it is the difference between a fair loss and
	// an inexplicable one.
	deltaTime = ofClamp(deltaTime, 0.0f, 1.0f / 20.0f);

	stateSeconds += deltaTime;

	// The field is frozen while the loss is being shown: nothing falls, nothing
	// is shot, and the picture the visitor lost on is the picture they are left
	// looking at. The round then restarts itself, which is what keeps the table
	// alive with nobody tending it.
	if (state == State::GameOver) {
		hitFlash = std::max(hitFlash - hitFlashDecay * deltaTime, 0.0f);
		shieldBreakFlash = std::max(shieldBreakFlash - shieldBreakFlashDecay * deltaTime, 0.0f);

		if (stateSeconds >= gameOverSeconds) {
			reset();
		}
		return;
	}

	elapsed += deltaTime;

	// Waves are time-based rather than kill-based, so the difficulty rises at the
	// same rate for a visitor who hits everything and one who hits nothing -
	// missing is already punished by the field filling up.
	wave = std::min(1 + static_cast<int>(elapsed / waveSeconds), maxWave);

	// All three flashes are drawing state, not game state, and are decayed
	// here so the visual has nothing to keep track of.
	hitFlash = std::max(hitFlash - hitFlashDecay * deltaTime, 0.0f);
	fireFlash = std::max(fireFlash - fireFlashDecay * deltaTime, 0.0f);
	shieldBreakFlash = std::max(shieldBreakFlash - shieldBreakFlashDecay * deltaTime, 0.0f);

	updatePlayer(deltaTime, targetX);
	updateSpawning(deltaTime);

	if (!updateFallers(deltaTime)) {
		// One of them reached the near edge: the round is over. Cleared rather
		// than left standing, so the loser feedback is drawn over the field as it
		// was at the moment it was lost, with no shots still in flight.
		state = State::GameOver;
		stateSeconds = 0.0f;
		shots.clear();
		powerUps.clear();
		hitFlash = 1.0f;
		return;
	}

	updateShots(deltaTime);
	updateCollisions();
	updatePowerUps(deltaTime);

	for (auto & explosion : explosions) {
		explosion.age += deltaTime;
	}

	explosions.erase(
		std::remove_if(explosions.begin(), explosions.end(), [this](const Explosion & explosion) {
			return explosion.age >= explosionLifetime;
		}),
		explosions.end());
}

void InvaderGame::updatePlayer(float deltaTime, float targetX) {
	// A target off the table, or one that is not a number at all, would otherwise
	// walk the figure off the edge and leave it there.
	if (!std::isfinite(targetX)) {
		targetX = playerX;
	}

	// The figure may reach the edges but not go past them, so it is always fully
	// on the table.
	float wanted = ofClamp(targetX, playerHalfWidth, 1.0f - playerHalfWidth);

	// Eased towards the target and then capped: the easing is what stops a
	// tracking wobble from making it twitch, the cap is what stops a hand
	// appearing at the far side of the table from teleporting it across.
	float currentSpeed = getPlayerSpeed();
	float velocity = ofClamp((wanted - playerX) * playerFollow, -currentSpeed, currentSpeed);

	playerX = ofClamp(playerX + velocity * deltaTime, playerHalfWidth, 1.0f - playerHalfWidth);
	playerVelocity = velocity;

	// Firing is on its own clock rather than tied to the frame rate, so the rate
	// of fire is the same on a Pi holding 60 fps and one dropping to 30.
	fireTimer -= deltaTime;

	if (fireTimer <= 0.0f) {
		fireTimer = getCurrentFireInterval();

		int shotsBefore = static_cast<int>(shots.size());

		// The first upgrade replaces the centre shot with a parallel pair. The
		// second adds the centre back and fans the side shots outward, producing
		// three visibly different patterns rather than only making one streak
		// thicker.
		int patternShots = InvaderPowerUpRules::shotCount(shotPatternLevel);

		if (patternShots == 1) {
			spawnShot(0.0f, 0.0f);
		} else if (patternShots == 2) {
			spawnShot(-shotPatternOffset, 0.0f);
			spawnShot(shotPatternOffset, 0.0f);
		} else {
			spawnShot(-shotPatternOffset, -shotFanSpeed);
			spawnShot(0.0f, 0.0f);
			spawnShot(shotPatternOffset, shotFanSpeed);
		}

		if (static_cast<int>(shots.size()) > shotsBefore) {
			fireFlash = 1.0f;
		}
	}
}

void InvaderGame::spawnShot(float offsetX, float horizontalSpeed) {
	if (static_cast<int>(shots.size()) >= maxShots) {
		return;
	}

	Shot shot;
	shot.x = ofClamp(playerX + offsetX, 0.0f, 1.0f);
	shot.y = playerY - playerHalfHeight; // leaves at the tip, not the centre
	shot.velocityX = horizontalSpeed;
	shot.speed = shotSpeed;
	shots.push_back(shot);
}

void InvaderGame::updateSpawning(float deltaTime) {
	spawnTimer -= deltaTime;

	if (spawnTimer > 0.0f) {
		return;
	}

	// Jittered, so the arrivals do not fall on an audible beat once the interval
	// gets short.
	float interval = currentSpawnInterval();
	spawnTimer = interval * ofRandom(1.0f - spawnJitter, 1.0f + spawnJitter);

	// The cap is a silent skip rather than a queue: a field this full is already
	// unreadable, and the timer above will try again shortly.
	if (static_cast<int>(fallers.size()) >= maxFallers) {
		return;
	}

	spawnFaller();
}

void InvaderGame::spawnFaller() {
	Faller faller;

	faller.halfWidth = fallerHalfWidth;
	faller.halfHeight = fallerHalfHeight;

	// Spawned inside a margin, so nothing arrives half off the table, and with
	// enough room left over for its own drift.
	float margin = spawnMargin + faller.halfWidth + maxDriftAmount;
	faller.driftCentre = ofRandom(margin, 1.0f - margin);
	faller.x = faller.driftCentre;

	// Starts just above the top edge, so it slides into view rather than
	// appearing.
	faller.y = -faller.halfHeight;

	// Speeds vary a little around the wave's speed, which is what makes a field
	// of them read as separate things rather than as one descending row.
	faller.speed = currentFallSpeed() * ofRandom(0.85f, 1.15f);

	faller.driftAmount = ofRandom(0.0f, maxDriftAmount);
	faller.driftRate = ofRandom(minDriftRate, maxDriftRate);
	faller.age = 0.0f;

	// Clamped rather than trusted: ofRandom's upper bound is not guaranteed to
	// be exclusive, and one value past the end would index a sprite that is not
	// there.
	faller.variant = std::min(static_cast<int>(ofRandom(0.0f, static_cast<float>(fallerVariants))), fallerVariants - 1);

	fallers.push_back(faller);
}

bool InvaderGame::updateFallers(float deltaTime) {
	bool stillAlive = true;

	// An iterator loop rather than a range-for, so a shield can erase the
	// faller it just absorbed mid-pass instead of the round ending under it.
	for (auto it = fallers.begin(); it != fallers.end();) {
		it->age += deltaTime;
		it->y += it->speed * deltaTime;

		// The drift swings around where it was spawned rather than accumulating,
		// so a long-lived faller cannot wander off the table.
		it->x = it->driftCentre + std::sin(it->age * it->driftRate) * it->driftAmount;

		// The leading edge, not the centre: what ends the round is the thing
		// touching the near edge, which is what a visitor watching it sees.
		if (it->y + it->halfHeight >= 1.0f) {
			if (shieldActive) {
				// Absorbed instead of ending the round: gone as if it had
				// never reached the edge, minus the score and the explosion a
				// shot kill gets - a block reads as a block, not a kill.
				shieldActive = false;
				shieldSecondsLeft = 0.0f;
				shieldBreakFlash = 1.0f;
				it = fallers.erase(it);
				continue;
			}

			stillAlive = false;
		}

		++it;
	}

	return stillAlive;
}

void InvaderGame::updateShots(float deltaTime) {
	for (auto & shot : shots) {
		shot.x += shot.velocityX * deltaTime;
		shot.y -= shot.speed * deltaTime;
	}

	// Anything that left the far or long edges is dropped, or a round nobody
	// hits anything in would keep every shot it ever fired. The long-edge check
	// matters once the maximum pattern fans its two side shots outward.
	shots.erase(
		std::remove_if(shots.begin(), shots.end(), [](const Shot & shot) {
			return shot.y < 0.0f || shot.x < 0.0f || shot.x > 1.0f;
		}),
		shots.end());
}

void InvaderGame::updateCollisions() {
	// Both lists are short - a couple of dozen at most, by the caps - so this is
	// a plain pair of loops rather than anything spatial. A grid or a sweep would
	// be more code to be wrong in for a saving the Pi cannot measure.
	for (auto shotIt = shots.begin(); shotIt != shots.end();) {
		bool hit = false;

		for (auto fallerIt = fallers.begin(); fallerIt != fallers.end(); ++fallerIt) {
			bool insideX = std::fabs(shotIt->x - fallerIt->x) <= fallerIt->halfWidth;
			bool insideY = std::fabs(shotIt->y - fallerIt->y) <= fallerIt->halfHeight;

			if (insideX && insideY) {
				Explosion explosion;
				explosion.x = fallerIt->x;
				explosion.y = fallerIt->y;
				explosion.variant = fallerIt->variant;
				explosions.push_back(explosion);

				// Rolled before the faller is erased, since spawnPowerUp()
				// needs the position it was hit at.
				if (ofRandom(1.0f) < powerUpDropChance) {
					spawnPowerUp(fallerIt->x, fallerIt->y);
				}

				fallers.erase(fallerIt);

				// Worth more in the later waves, so a long round is scored as the
				// harder thing it was rather than only as the longer one.
				score += pointsPerHit * wave;
				hitFlash = 1.0f;
				hit = true;
				break;
			}
		}

		// A shot is spent on whatever it hits: one shot cannot clear a column.
		shotIt = hit ? shots.erase(shotIt) : shotIt + 1;
	}
}

void InvaderGame::updatePowerUps(float deltaTime) {
	for (auto it = powerUps.begin(); it != powerUps.end();) {
		it->y += it->speed * deltaTime;

		bool insideX = std::fabs(it->x - playerX) <= playerHalfWidth + powerUpHalfSize;
		bool insideY = std::fabs(it->y - playerY) <= playerHalfHeight + powerUpHalfSize;

		if (insideX && insideY) {
			applyPowerUp(*it);
			it = powerUps.erase(it);
			continue;
		}

		// Unlike a faller, missing one costs nothing but the chance at it -
		// the round does not end because a pickup was not caught.
		if (it->y - powerUpHalfSize >= 1.0f) {
			it = powerUps.erase(it);
			continue;
		}

		++it;
	}

	// Counts down whether or not the shield is ever used, so it is a "use it
	// or lose it" carry rather than a permanent upgrade - see
	// shieldTimeoutSeconds.
	if (shieldActive) {
		shieldSecondsLeft = std::max(shieldSecondsLeft - deltaTime, 0.0f);

		if (shieldSecondsLeft <= 0.0f) {
			shieldActive = false;
		}
	}

	recolorSecondsLeft = std::max(recolorSecondsLeft - deltaTime, 0.0f);
}

void InvaderGame::spawnPowerUp(float x, float y) {
	if (static_cast<int>(powerUps.size()) >= maxPowerUps) {
		return;
	}

	PowerUp powerUp;
	powerUp.x = x;
	powerUp.y = y;
	powerUp.speed = powerUpSpeed;

	// Shield and recolour stay eligible throughout a round. A tiered upgrade is
	// removed from the draw once it reaches its cap, so a visitor never detours
	// for a pickup that the rules already know cannot improve anything.
	std::array<PowerUpKind, 5> availableKinds {};
	int availableCount = 0;
	availableKinds[availableCount++] = PowerUpKind::Shield;
	availableKinds[availableCount++] = PowerUpKind::Recolor;

	// Pending pickups count against the remaining tiers as well. Without this,
	// three copies could already be falling when the first one reaches its cap,
	// leaving a visually valuable but mechanically empty pickup behind it.
	auto hasUpgradeRoom = [this](PowerUpKind kind, int currentLevel, int maximumLevel) {
		int pending = static_cast<int>(std::count_if(
			powerUps.begin(),
			powerUps.end(),
			[kind](const PowerUp & pendingPowerUp) {
				return pendingPowerUp.kind == kind;
			}));
		return currentLevel + pending < maximumLevel;
	};

	if (hasUpgradeRoom(PowerUpKind::ShotPattern, shotPatternLevel, InvaderPowerUpRules::maxShotPatternLevel)) {
		availableKinds[availableCount++] = PowerUpKind::ShotPattern;
	}

	if (hasUpgradeRoom(PowerUpKind::FireRate, fireRateLevel, InvaderPowerUpRules::maxFireRateLevel)) {
		availableKinds[availableCount++] = PowerUpKind::FireRate;
	}

	if (hasUpgradeRoom(PowerUpKind::ShipSpeed, shipSpeedLevel, InvaderPowerUpRules::maxShipSpeedLevel)) {
		availableKinds[availableCount++] = PowerUpKind::ShipSpeed;
	}

	int selected = std::min(
		static_cast<int>(ofRandom(0.0f, static_cast<float>(availableCount))),
		availableCount - 1);
	powerUp.kind = availableKinds[selected];

	// Clamped rather than trusted, the same as Faller::variant - ofRandom's
	// upper bound is not guaranteed exclusive.
	powerUp.colorVariant = std::min(
		static_cast<int>(ofRandom(0.0f, static_cast<float>(powerUpColorVariants))),
		powerUpColorVariants - 1);

	powerUps.push_back(powerUp);
}

void InvaderGame::applyPowerUp(const PowerUp & powerUp) {
	switch (powerUp.kind) {
	case PowerUpKind::Shield:
		shieldActive = true;
		shieldSecondsLeft = shieldTimeoutSeconds;
		break;
	case PowerUpKind::Recolor:
		recolorSecondsLeft = recolorDurationSeconds;
		recolorVariant = powerUp.colorVariant;
		break;
	case PowerUpKind::ShotPattern:
		shotPatternLevel = InvaderPowerUpRules::upgradedLevel(
			shotPatternLevel, InvaderPowerUpRules::maxShotPatternLevel);
		break;
	case PowerUpKind::FireRate:
		fireRateLevel = InvaderPowerUpRules::upgradedLevel(
			fireRateLevel, InvaderPowerUpRules::maxFireRateLevel);

		// If a slower cadence was already counting down, shorten that remaining
		// wait too; the pickup should be felt on the next shot, not one later.
		fireTimer = std::min(fireTimer, getCurrentFireInterval());
		break;
	case PowerUpKind::ShipSpeed:
		shipSpeedLevel = InvaderPowerUpRules::upgradedLevel(
			shipSpeedLevel, InvaderPowerUpRules::maxShipSpeedLevel);
		break;
	}
}

float InvaderGame::getAutopilotTargetX() const {
	const Faller * threat = nullptr;

	for (const auto & faller : fallers) {
		// The one closest to the near edge is the one about to end the round, so
		// that is the one worth being under. Anything already past the figure is
		// beyond saving and would only pull the aim away from what still matters.
		if (faller.y > playerY) {
			continue;
		}

		if (threat == nullptr || faller.y > threat->y) {
			threat = &faller;
		}
	}

	// Nothing to shoot at: back to the middle, which is both where the next
	// arrival is most likely to be reachable from and where the figure reads as
	// waiting rather than as stuck.
	if (threat == nullptr) {
		return 0.5f;
	}

	// The aim wanders slowly rather than tracking exactly. A demo that never
	// misses looks like an animation; one that occasionally lets something
	// through shows a passer-by that this can be lost, which is the only thing
	// that makes it worth walking up to.
	float wander = std::sin(elapsed * autopilotWanderRate) * autopilotWander;

	return threat->x + wander;
}

float InvaderGame::adjustDifficultyScale(float delta) {
	difficultyScale = ofClamp(difficultyScale + delta, minDifficultyScale, maxDifficultyScale);
	return difficultyScale;
}

float InvaderGame::adjustPlayerSpeed(float delta) {
	// The floor is a figure slower than the things it is shooting at, which is
	// unplayable; the ceiling is one that crosses the table in a fifth of a
	// second, at which point the hand is no longer really steering it.
	playerSpeed = ofClamp(playerSpeed + delta, 0.3f, 3.0f);
	return getPlayerSpeed();
}
