#pragma once

#include "InvaderPowerUpRules.h"
#include "ofMain.h"
#include <vector>

// The simulation behind the last screen: things fall down the length of the
// table, a figure at the near edge shoots at them without ever being asked to,
// and the round ends the moment one of them gets past it.
//
// This is a plain class rather than a TransformBase subclass (constitution
// Principle III): TransformBase is for grid transformations - a Grid in, a Grid
// out - and nothing here is a grid. It is the same arrangement the space screen
// uses, where SpaceNavigator holds the movement and the visual only draws
// what it is told. The split matters for the same reason it does there: this
// class holds the rules of the game and no drawing at all, so what the game does
// can be reasoned about without reading a single ofDraw call, and the screen can
// be re-styled without touching the rules.
//
// It also knows nothing about the sensor (Principle II) - not even indirectly.
// It is handed one number per frame, "where the figure should be", and it is
// InvaderVisual's job to decide whether that number came from a visitor's hand
// or from the demo autopilot.
class InvaderGame {
public:
	// Everything in this class lives in a normalized play field, never in pixels:
	// x is 0 at one long edge of the table and 1 at the other, y is 0 at the far
	// short edge the things fall from and 1 at the near edge the figure stands
	// on. The projection can then be any resolution or aspect ratio without a
	// single value in here changing meaning.
	struct Faller {
		float x = 0.5f; // current centre, across the table
		float y = 0.0f; // current centre, down the table
		float halfWidth = 0.02f;
		float halfHeight = 0.05f;
		float speed = 0.1f; // play field units per second, downwards
		float driftCentre = 0.5f; // the x its sideways drift swings around
		float driftAmount = 0.0f; // how far it swings, 0 for one that falls straight
		float driftRate = 0.0f; // radians per second of that swing
		float age = 0.0f; // seconds since it was spawned, drives the drift and the fade-in

		// Which of the interchangeable piece shape+colour combinations this one
		// is (see fallerVariants below). Decided once at spawn and never read
		// by the rules - it exists so a piece keeps the same look for its
		// whole fall, which the screen could not arrange on its own without a
		// per-faller identity the game has no other use for.
		int variant = 0;
	};

	// A piece's last position, kept for a moment after it is shot so the screen
	// can draw a burst there even though the Faller itself is already gone
	// from getFallers() - erased the instant it is hit, see updateCollisions().
	// Purely cosmetic: nothing here is read by the rules, the same as
	// hitFlash/fireFlash below.
	struct Explosion {
		float x = 0.5f;
		float y = 0.0f;
		float age = 0.0f;
		int variant = 0; // which piece this was, so the burst can be tinted to match
	};

	// One member of a shot pattern. The default travels straight up; the final
	// pattern gives its two side members a horizontal velocity. Modelled as a
	// point rather than a rectangle: a drawn streak that clipped with its tail
	// would score hits the visitor cannot see happening.
	struct Shot {
		float x = 0.5f;
		float y = 1.0f;
		float velocityX = 0.0f; // play field units per second, for the upgraded fan
		float speed = 1.0f; // play field units per second, upwards
	};

	// What a power-up does once caught. Unlike Faller::variant or
	// Explosion::variant, every value is read by the rules (applyPowerUp()), so
	// it cannot be left as an opaque int the way a piece's shape can.
	enum class PowerUpKind {
		Shield,
		Recolor,
		ShotPattern,
		FireRate,
		ShipSpeed
	};

	// A pickup left behind by some of the pieces that are shot, falling
	// straight down at its own pace rather than the faller's. Missing one
	// costs nothing but the chance at it - only a Faller reaching the near
	// edge ends the round.
	struct PowerUp {
		float x = 0.5f;
		float y = 0.0f;
		float speed = 0.09f;
		PowerUpKind kind = PowerUpKind::Shield;

		// Which of the screen's recolour tints this is, meaningless for every
		// kind except Recolor - decided at spawn like Faller::variant, for the same
		// reason: the screen needs a stable per-pickup identity the rules
		// have no other use for.
		int colorVariant = 0;
	};

	enum class State {
		Playing,
		GameOver // frozen field, waiting out the loser feedback before the reset
	};

	void setup();

	// Puts a fresh round on the table: empty field, score zero, wave one. Used
	// both by the automatic restart after a loss and whenever control changes
	// hands between a visitor and the demo.
	void reset();

	// One frame. targetX is where the figure should be heading, 0-1 across the
	// table; the figure is never teleported there, it is moved towards it at a
	// speed a person can follow with their eyes.
	void update(float deltaTime, float targetX);

	// Where the demo would steer if it were playing: the x of whichever faller is
	// closest to the bottom, plus a slowly wandering aiming error. The error is
	// the point - a demo that never misses is a screensaver, and one that
	// occasionally loses shows a passer-by that the round can actually be lost.
	// Returns the middle of the table when there is nothing to shoot at.
	float getAutopilotTargetX() const;

	State getState() const { return state; }
	float getStateSeconds() const { return stateSeconds; }

	float getPlayerX() const { return playerX; }
	float getPlayerY() const { return playerY; }
	float getPlayerHalfWidth() const { return playerHalfWidth; }
	float getPlayerHalfHeight() const { return playerHalfHeight; }

	// Signed, so the figure can be drawn leaning into its own movement.
	float getPlayerVelocity() const { return playerVelocity; }

	// Fades from 1 to 0 over a fraction of a second after each hit, so the screen
	// can flash without the game having to hand out events.
	float getHitFlash() const { return hitFlash; }

	// Fades from 1 to 0 after each shot leaves the figure, for the muzzle flash.
	float getFireFlash() const { return fireFlash; }

	const std::vector<Faller> & getFallers() const { return fallers; }
	const std::vector<Shot> & getShots() const { return shots; }
	const std::vector<Explosion> & getExplosions() const { return explosions; }

	// How long an Explosion is kept before it is dropped, so the visual's own
	// fade cannot drift out of step with when the burst actually disappears.
	float getExplosionLifetime() const { return explosionLifetime; }

	const std::vector<PowerUp> & getPowerUps() const { return powerUps; }
	float getPowerUpHalfSize() const { return powerUpHalfSize; }

	// Whether a shield is currently carried, and how long before an unused
	// one expires - shown by the visual as a ring around the figure that
	// starts flickering as this runs out, so a shield's own timeout is
	// something a visitor can see rather than a rule they find out about by
	// having it vanish on them.
	bool isShieldActive() const { return shieldActive; }
	float getShieldSecondsLeft() const { return shieldSecondsLeft; }
	float getShieldTimeoutSeconds() const { return shieldTimeoutSeconds; }

	// Fades from 1 to 0 the moment a shield absorbs a faller that would
	// otherwise have ended the round - the same "set to 1, only ever decays"
	// pattern as hitFlash/fireFlash, so the visual has nothing of its own to
	// track for it either.
	float getShieldBreakFlash() const { return shieldBreakFlash; }

	// Whether the figure is currently recoloured, which tint it should use
	// (the screen's own business, this is only ever an index), and how long
	// is left - shown fading rather than switching off outright, the same
	// reasoning as every other timed effect in this class.
	bool isRecolorActive() const { return recolorSecondsLeft > 0.0f; }
	float getRecolorSecondsLeft() const { return recolorSecondsLeft; }
	float getRecolorDurationSeconds() const { return recolorDurationSeconds; }
	int getRecolorVariant() const { return recolorVariant; }
	int getPowerUpColorVariants() const { return powerUpColorVariants; }

	// The three gameplay upgrades are round-long tiers. The visual reads both
	// current and maximum levels for its status line; their effects are exposed
	// through the ordinary shots/player getters rather than duplicated there.
	int getShotPatternLevel() const { return shotPatternLevel; }
	int getFireRateLevel() const { return fireRateLevel; }
	int getShipSpeedLevel() const { return shipSpeedLevel; }
	int getMaxShotPatternLevel() const { return InvaderPowerUpRules::maxShotPatternLevel; }
	int getMaxFireRateLevel() const { return InvaderPowerUpRules::maxFireRateLevel; }
	int getMaxShipSpeedLevel() const { return InvaderPowerUpRules::maxShipSpeedLevel; }
	float getCurrentFireInterval() const;

	int getScore() const { return score; }
	int getWave() const { return wave; }

	// How long the loser feedback runs before the round restarts by itself. Read
	// by the visual so the fade it draws and the reset it waits for cannot drift
	// apart.
	float getGameOverSeconds() const { return gameOverSeconds; }

	// Everything below is calibration, adjustable from the keyboard while the
	// piece is running on the table (see VisualBase::keyPressed for why that is
	// worth the methods). Both return the value they ended up at, for printing.
	float adjustDifficultyScale(float delta);
	float adjustPlayerSpeed(float delta);

	float getDifficultyScale() const { return difficultyScale; }
	float getPlayerSpeed() const;

private:
	// Moves the figure towards targetX: eased, so tracking wobble does not make
	// it twitch, and speed-capped, so it cannot cross the table instantly when
	// a hand appears somewhere new.
	void updatePlayer(float deltaTime, float targetX);

	void updateSpawning(float deltaTime);
	void spawnFaller();

	// Returns false when one of them got past the figure, which is the only way
	// the round ends.
	bool updateFallers(float deltaTime);

	void updateShots(float deltaTime);

	// Adds one member of the current pattern if the global shot ceiling still
	// leaves room. Offsets begin at the figure; horizontalSpeed fans upgraded
	// side shots out as they travel.
	void spawnShot(float offsetX, float horizontalSpeed);

	void updateCollisions();

	// Falls each pickup, catches it against the figure, drops any that reach
	// the near edge unclaimed, and counts down the shield's own carry timer.
	void updatePowerUps(float deltaTime);

	// Rolled once per hit in updateCollisions() - see powerUpDropChance.
	void spawnPowerUp(float x, float y);

	void applyPowerUp(const PowerUp & powerUp);

	// Both derived from the wave rather than stored, so difficulty lives in one
	// place and cannot be left in an inconsistent state by a reset.
	float currentFallSpeed() const;
	float currentSpawnInterval() const;

	State state = State::Playing;
	float stateSeconds = 0.0f; // seconds in the current state, drives the loser feedback

	std::vector<Faller> fallers;
	std::vector<Shot> shots;
	std::vector<Explosion> explosions;

	// How long a burst is kept alive before it is dropped. Short - it is a
	// flourish on the hit itself, not a thing to watch for its own sake.
	float explosionLifetime = 0.5f;

	float playerX = 0.5f;
	float playerVelocity = 0.0f; // units per second, signed, only used for the lean

	int score = 0;
	int wave = 1;
	float elapsed = 0.0f; // seconds this round has been running, drives the waves

	float spawnTimer = 0.0f;
	float fireTimer = 0.0f;
	float hitFlash = 0.0f;
	float fireFlash = 0.0f;

	// Where the figure sits and how big it is. Not quite at the very edge: the
	// bottom of the play field is the line that ends the round, and a figure
	// drawn across that line would make it unreadable.
	float playerY = 0.93f;
	float playerHalfWidth = 0.035f;
	float playerHalfHeight = 0.030f;

	// How fast the unupgraded figure can cross the table, in play field widths
	// per second, and how hard it pulls towards where the hand is. The effective
	// cap returned by getPlayerSpeed() also includes its collected speed tier;
	// the follow rate is what keeps either one from arriving with a jolt.
	//
	// Raised from 1.1/9.0 - both still eased rather than a straight
	// teleport-to-target, but a visitor moving their hand read the old pair as
	// the figure lagging behind and chasing rather than following. At 15 the
	// follow rate's own time constant is about a fifteenth of a second; at 1.7
	// the figure crosses the whole table in a bit over half a second at full
	// tilt, which is fast enough to feel driven directly without being able to
	// out-run what a person's eye can track.
	float playerSpeed = 1.7f;
	float playerFollow = 15.0f;

	// The range the difficulty keys stay inside. Anything under half is not a
	// game any more and anything over double is unplayable within one wave.
	float difficultyScale = 1.0f;
	float minDifficultyScale = 0.5f;
	float maxDifficultyScale = 2.0f;

	// Seconds per wave, and the wave the ramp stops at. Capped because the demo
	// can run for hours: without a ceiling the falling speed would climb until
	// the things crossed the table within a frame, which is not a harder game,
	// it is a broken one.
	float waveSeconds = 30.0f;
	int maxWave = 10;

	// Falling speed in play field lengths per second: at 0.10 a faller takes ten
	// seconds to cross the table, which is slow enough to be read as an approach
	// rather than a drop. Each wave adds a little, up to the cap above.
	float baseFallSpeed = 0.10f;
	float fallSpeedPerWave = 0.022f;

	// Seconds between spawns, shrinking with each wave. The floor is what stops
	// the field from silting up into a wall nothing can be shot through.
	float baseSpawnInterval = 1.7f;
	float spawnIntervalPerWave = 0.11f;
	float minSpawnInterval = 0.45f;

	// Spawns are jittered by up to this share of the interval, so the things do
	// not arrive on an audible beat.
	float spawnJitter = 0.25f;

	// Kept small on purpose. The Pi draws these one by one, and a field with more
	// than this many things in it is unreadable at the table anyway.
	int maxFallers = 32;
	int maxShots = 40;

	// How long and thin a faller is. Elongated along the direction it travels, so
	// it reads as coming down the table rather than sitting on it.
	float fallerHalfWidth = 0.018f;
	float fallerHalfHeight = 0.055f;

	// How far in from the long edges a faller may be spawned, so none of them
	// arrives half off the table.
	float spawnMargin = 0.06f;

	// How many interchangeable piece identities the screen picks from - three
	// shapes (pill, capsule, tablet) times four resting colours, decoded back
	// out of the one int by the screen (variant % 3 for shape, variant / 3 for
	// colour) rather than carried as two fields, the same reasoning as variant
	// itself: the rules have no use for either half, so there is no reason to
	// give them each their own name here. Kept here because it is what the
	// spawn picks from; what a given number actually looks like is the
	// screen's business, not the rules'.
	int fallerVariants = 12;

	// The sideways drift: a slow sine, different per faller, that also stops the
	// autopilot from being able to sit still and win.
	float maxDriftAmount = 0.05f;
	float minDriftRate = 0.4f;
	float maxDriftRate = 1.2f;

	// Shots leave continuously - the figure is never asked to fire, since the one
	// thing a visitor has been given is left and right. fireInterval is the
	// unupgraded cadence; getCurrentFireInterval() applies the collected tier.
	float fireInterval = 0.20f;
	float shotSpeed = 1.15f;
	float shotPatternOffset = 0.022f;
	float shotFanSpeed = 0.12f;

	int pointsPerHit = 10;

	// How long the loser feedback holds before the round restarts by itself.
	// Long enough to be read as a loss from across the room, short enough that
	// somebody walking up to a lost game is not left waiting at a dead table.
	float gameOverSeconds = 4.0f;

	// How far off the autopilot's aim wanders, and how quickly. Small enough that
	// the demo mostly hits, large enough that it eventually does not.
	float autopilotWander = 0.05f;
	float autopilotWanderRate = 1.7f;

	// Flash decay rates, per second, for the hit and muzzle flashes.
	float hitFlashDecay = 4.0f;
	float fireFlashDecay = 12.0f;

	std::vector<PowerUp> powerUps;

	// Share of hits that leave a pickup behind. High enough that a round
	// sees several, low enough that catching one stays worth noticing.
	float powerUpDropChance = 0.18f;

	// Falls slower than even the gentlest faller, so a pickup is something a
	// visitor has time to react to rather than another thing to dodge.
	float powerUpSpeed = 0.09f;
	float powerUpHalfSize = 0.022f;

	// Kept small for the same reason maxFallers/maxShots are: a field this
	// full of pickups would be unreadable, and the drop chance above already
	// keeps the ordinary case well under it.
	int maxPowerUps = 6;

	// How many distinct recolour tints the screen can choose between -
	// purely a range for spawnPowerUp() to draw colorVariant from, the actual
	// colours are the screen's business.
	int powerUpColorVariants = 4;

	// Whether a shield is currently held, and how long an unused one is
	// carried before it expires. Short enough relative to a round
	// (waveSeconds is 30s) that carrying one is a choice about when to use
	// it, not a permanent upgrade; long enough to be worth detouring for.
	bool shieldActive = false;
	float shieldSecondsLeft = 0.0f;
	float shieldTimeoutSeconds = 12.0f;

	float shieldBreakFlash = 0.0f;
	float shieldBreakFlashDecay = 3.0f;

	// How long a caught recolour pickup lasts, and how far through that this
	// one still has to go. Roughly the same order of magnitude as the shield
	// timeout, on purpose - two power-ups that felt very differently
	// generous would make one of them feel like the "real" one.
	float recolorSecondsLeft = 0.0f;
	float recolorDurationSeconds = 10.0f;
	int recolorVariant = 0;

	// These upgrades last until the round is reset and saturate at the limits in
	// InvaderPowerUpRules. Permanent-within-a-round tiers make repeat pickups
	// accumulate into a progression without letting a long-running demo grow
	// unbounded.
	int shotPatternLevel = 0;
	int fireRateLevel = 0;
	int shipSpeedLevel = 0;
};
