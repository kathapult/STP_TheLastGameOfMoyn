#include "InvaderVisual.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

InvaderVisual::InvaderVisual() {
	// Shown in the debug HUD, which is how the current screen is told apart from
	// outside the picture.
	title = "INVADERS";

	// This screen's own tuning for the shared tracker - see each field's
	// comment on ObjectTracker for what it does in general. Matches what this
	// screen's own fixed values used to be (moveEpsilon, inertSeconds,
	// holdSeconds, trackForgetSeconds), plus handoverSeconds, which this
	// screen never had before since it steered by averaging rather than by a
	// single exclusive driver - kept short relative to CruiseNavigator's 2.0s,
	// since this screen already answers a takeover on the demo/hands boundary
	// in well under a second (see takeoverSeconds) and a driver handover
	// should not visibly lag behind that.
	//
	// Pi A already applies a One-Euro filter. The former 2.2 beta on this
	// second filter opened it almost completely during quick or extrapolated
	// movement, passing contour corrections straight into the ship. A modest
	// adaptive term keeps deliberate movement responsive while attenuating the
	// jumps reported at the table.
	tracker.minCutoff = 1.3f;
	tracker.beta = 0.8f;
	tracker.moveEpsilon = 0.02f;
	tracker.inertSeconds = 5.0f;
	tracker.holdSeconds = 0.8f;
	tracker.trackForgetSeconds = 2.0f;
	tracker.handoverSeconds = 1.0f;
}

void InvaderVisual::setup() {
	VisualBase::setup();
	game.setup();

	// visualApp.service owns one foreground process. Starting that process calls
	// setup() once, which is the requested session boundary for the ranking.
	highScores.clear();
	lastCompletedScore = 0;
	lastCompletedRank = -1;
	lastCompletedWave = 0;
	lastWaveScore = 0;
	waveScoreSeconds = waveScoreHoldSeconds + waveScoreFadeSeconds;

	buildSprites();

	if (playerSprite.load("fusion-rocket-player.png")) {
		// Preserve the deliberately chunky generated pixels when the sprite is
		// scaled down for the projector.
		playerSprite.getTexture().setTextureMinMagFilter(GL_NEAREST, GL_NEAREST);
	} else {
		ofLogError("InvaderVisual") << "could not load fusion-rocket-player.png";
	}
}

void InvaderVisual::buildSprites() {
	// A pill: a plain rounded capsule, seven cells across, drawn full length in
	// one of the two border colours (see drawFallers()) rather than needing
	// two sprites for the two colours - the tint carries that, the same way
	// the old chips let one sprite serve every colour a screen asked of it. A
	// highlight down one side and a shadow down the other read as a rounded
	// cylinder rather than a flat bar at the size this is shown on the table.
	pillSprite.build({
		"..###..",
		".#####.",
		"#*###-#",
		"#*###-#",
		"#*###-#",
		"#*###-#",
		"#*###-#",
		"#*###-#",
		"#*###-#",
		"#*###-#",
		"#*###-#",
		"#*###-#",
		"#*###-#",
		".#####.",
		"..###.." });

	// A capsule: the same silhouette split at its middle into two halves, each
	// its own colour (turquoise over lilac, see drawFallers()) and drawn
	// stacked - the two-tone medicine-capsule shape rather than the pill's
	// single colour. Each half is rounded on its own end and cut flat at the
	// seam, where the two meet.
	capsuleTopSprite.build({
		"..###..",
		".#####.",
		"#*###-#",
		"#*###-#",
		"#*###-#",
		"#*###-#",
		"#*###-#",
		"#######" });

	capsuleBottomSprite.build({
		"#######",
		"#*###-#",
		"#*###-#",
		"#*###-#",
		"#*###-#",
		"#*###-#",
		".#####.",
		"..###.." });

	// A tablet: round rather than elongated, so it reads as a different kind
	// of piece from the pill at a glance rather than only by its colour.
	// Same seven-wide shading convention as the pill (highlight left, shadow
	// right) but far shorter, which is what makes it read as flat and round
	// instead of a shortened pill.
	tabletSprite.build({
		"..###..",
		".#####.",
		"#*###-#",
		"#*###-#",
		"#*###-#",
		".#####.",
		"..###.." });

	// Power-up silhouettes are intentionally different at a glance. Colour is
	// a second cue in drawPowerUps(), but the shapes remain distinguishable on
	// an uneven projector or to a colour-blind visitor.
	shieldPowerUpSprite.build({
		".#####.",
		"#######",
		"##***##",
		"#######",
		".#####.",
		"..###..",
		"...#..." });

	recolorPowerUpSprite.build({
		"##...**",
		"##...**",
		".......",
		"..###..",
		"..###..",
		".......",
		"--...##" });

	shotPatternPowerUpSprite.build({
		".*.*.*.",
		".#.#.#.",
		".#.#.#.",
		".#.#.#.",
		".#.#.#.",
		".-.-.-." });

	fireRatePowerUpSprite.build({
		"....##.",
		"...##..",
		"..##...",
		".#####.",
		"...##..",
		"..##...",
		".##...." });

	shipSpeedPowerUpSprite.build({
		"#..#...",
		".#..#..",
		"..#..#.",
		"...#..#",
		"..#..#.",
		".#..#..",
		"#..#..." });
}

bool InvaderVisual::isReady(std::string & failureReason) const {
	if (!playerSprite.isAllocated() || !playerSprite.getTexture().isAllocated()) {
		failureReason = "Fusion rocket player sprite failed to load";
		return false;
	}

	failureReason.clear();
	return true;
}

// Pi A's classification is used as a preference, never as a hard gate: if
// any currently-usable track is typed a hand, only hands may acquire control;
// otherwise anything usable may. The already-present driver is retained across
// a transient Hand/Object relabel so classification flicker cannot steer by a
// different person or contour.
const ObjectTracker::Track * InvaderVisual::chooseSteeringTrack(float deltaTime) {
	bool preferHands = false;

	for (const auto & track : tracker.getTracks()) {
		if (tracker.isUsable(track) && track.type == ObjectType::Hand) {
			preferHands = true;
			break;
		}
	}

	const int currentDriverId = tracker.getPrimaryId();
	auto typeAllowed = [preferHands, currentDriverId](const ObjectTracker::Track & track) {
		// Type is a preference when acquiring control, not a reason to evict the
		// same physical driver for a transient Hand/Object reclassification.
		return (track.id == currentDriverId && track.unseenSeconds <= 0.0f)
			|| !preferHands
			|| track.type == ObjectType::Hand;
	};

	return tracker.chooseExclusive(deltaTime, typeAllowed);
}

void InvaderVisual::updateControl(float deltaTime, bool handSeen) {
	if (handSeen) {
		handSeconds += deltaTime;
		noHandSeconds = 0.0f;
	} else {
		noHandSeconds += deltaTime;
		handSeconds = 0.0f;
	}

	// Whether the figure should be playing itself this frame. The two thresholds
	// are deliberately different: taking over from a visitor waits fifteen
	// seconds, giving the game back takes a third of one. Being slow to take over
	// is politeness; being slow to hand back would feel like the table ignoring
	// somebody who is already reaching for it.
	bool wantDemo = demoActive;

	switch (control) {
	case Control::ForceDemo:
		wantDemo = true;
		break;
	case Control::ForceHands:
		wantDemo = false;
		break;
	case Control::Automatic:
		wantDemo = demoActive ? (handSeconds < takeoverSeconds) : (noHandSeconds >= demoAfterSeconds);
		break;
	}

	if (wantDemo == demoActive) {
		return;
	}

	demoActive = wantDemo;

	if (restartOnControlChange) {
		// A visitor should not inherit the wave the demo has climbed to, and the
		// demo should not inherit a field somebody walked away from mid-round.
		game.reset();
		waveScoreSeconds = waveScoreHoldSeconds + waveScoreFadeSeconds;
	}

	std::cout << "Invaders: " << (demoActive ? "demo playing" : "hands playing")
			  << " (objects " << tracker.getSeenCount() << ", steering by " << tracker.getUsableCount() << ")" << std::endl;
}

void InvaderVisual::update(const std::vector<DetectedObject> & detectedObjects) {
	float deltaTime = static_cast<float>(ofGetLastFrameTime());

	// Clamped for the same reason the game clamps it: a frame that took a second
	// must not age every track by a second's worth of stillness.
	deltaTime = ofClamp(deltaTime, 0.0f, 1.0f / 20.0f);

	tracker.update(detectedObjects, deltaTime);

	// Whether Pi A typed anything a hand this frame at all, independent of
	// whether it ended up driving - see the field's own comment.
	sawTypedHand = false;
	for (const auto & obj : detectedObjects) {
		if (obj.type == ObjectType::Hand) {
			sawTypedHand = true;
			break;
		}
	}

	const ObjectTracker::Track * driver = chooseSteeringTrack(deltaTime);
	bool handSeen = driver != nullptr;

	if (handSeen && driver->unseenSeconds <= 0.0f) {
		// Stretched out from the middle of the region of interest: hands
		// rarely reach the outer tenth of the sensor's view, so without this
		// the figure could never be brought fully to either side of the
		// table. The guard keeps the mapping valid if the margin is turned up
		// to where the two edges would meet.
		float usableWidth = std::max(1.0f - 2.0f * handEdgeMargin, 0.05f);
		float mappedHandX = ofClamp((driver->x - handEdgeMargin) / usableWidth, 0.0f, 1.0f);
		handFollower.update(driver->id, mappedHandX, deltaTime);
	} else if (!handSeen) {
		handFollower.releaseDriver();
	}

	updateControl(deltaTime, handSeen);

	// No "you are here" cursor on this screen (unlike PixelPush and
	// Universe): the figure itself is already that feedback - it moves
	// towards whoever is steering it - and a ring on top of a game already
	// answering a hand with a score and a way to lose was one thing on the
	// table too many.

	// The game is handed one number and is never told where it came from
	// (Principle II, and the reason the autopilot lives in the game rather than
	// here: it is the only thing that has to know where the chips are).
	float target = demoActive ? game.getAutopilotTargetX() : handFollower.getTarget();

	InvaderGame::State stateBeforeUpdate = game.getState();
	int waveBeforeUpdate = game.getWave();
	int scoreBeforeUpdate = game.getScore();
	game.update(deltaTime, target);
	waveScoreSeconds += deltaTime;

	// InvaderGame increments its time-based wave before updating the rest of the
	// field. Capture the score on that edge, not during the following wave, so
	// the short summary belongs to the wave that actually earned it.
	if (stateBeforeUpdate == InvaderGame::State::Playing
		&& game.getState() == InvaderGame::State::Playing
		&& game.getWave() > waveBeforeUpdate) {
		lastCompletedWave = waveBeforeUpdate;
		lastWaveScore = scoreBeforeUpdate;
		waveScoreSeconds = 0.0f;
	}

	// A score becomes a result exactly once: on the Playing -> GameOver edge.
	// Resets caused by a control handover, a mode entry or the calibration key
	// are incomplete rounds and never enter the table. Demo rounds are also kept
	// out, or unattended autoplay would eventually occupy every place.
	if (stateBeforeUpdate == InvaderGame::State::Playing
		&& game.getState() == InvaderGame::State::GameOver) {
		lastCompletedScore = game.getScore();
		lastCompletedRank = demoActive ? -1 : highScores.record(lastCompletedScore);

		std::cout << "Invaders: completed " << (demoActive ? "demo" : "visitor")
				  << " round with " << lastCompletedScore << " points";
		if (lastCompletedRank >= 0) {
			std::cout << ", high-score rank " << (lastCompletedRank + 1);
		}
		std::cout << std::endl;
	}

	// The border's reaction to the game: firing is the visitor's own
	// interaction (well, the figure's - but it is standing in for them), a hit
	// or a loss is the border's "win" feedback, folded into one trigger since
	// InvaderGame does not distinguish scoring a hit from losing the round.
	if (game.getFireFlash() >= flashPeak) {
		pulseBorder(Palette::colorBorderTurquoise, 0.8f);
	}

	if (game.getHitFlash() >= flashPeak) {
		pulseBorder(Palette::colorBorderTurquoise, 1.3f);
	}

	// A shield absorbing a hit is its own kind of event - the round kept
	// going when it otherwise would not have - so it gets the power-up
	// colour rather than the turquoise the ordinary hit/loss trigger above
	// uses.
	if (game.getShieldBreakFlash() >= flashPeak) {
		pulseBorder(Palette::colorPowerUp, 1.1f);
	}

	updateBorderAnimation(deltaTime);
}

float InvaderVisual::textWidth(const std::string & text, float pixelHeight) const {
	if (myFont.isLoaded() && myFont.getSize() > 0) {
		return myFont.stringWidth(text) * (pixelHeight / static_cast<float>(myFont.getSize()));
	}

	// The bitmap fallback is a fixed-width face, eight pixels per character.
	return static_cast<float>(text.size()) * 8.0f;
}

void InvaderVisual::drawText(const std::string & text, float x, float y, float pixelHeight, bool centred) const {
	if (myFont.isLoaded() && myFont.getSize() > 0) {
		// The font is loaded once at a fixed size and scaled here, rather than
		// being reloaded per size: loading a face allocates an atlas, which is not
		// something to do while drawing.
		float scale = pixelHeight / static_cast<float>(myFont.getSize());
		float width = myFont.stringWidth(text) * scale;

		ofPushMatrix();
		ofTranslate(centred ? x - width / 2.0f : x, y);
		ofScale(scale, scale);
		myFont.drawString(text, 0, 0);
		ofPopMatrix();
		return;
	}

	// Fallback for a font that failed to load. On this screen the score and the
	// loss are the whole point, and a screen that silently stopped showing them
	// would read as a crash rather than as a missing file.
	float width = static_cast<float>(text.size()) * 8.0f;
	ofDrawBitmapString(text, centred ? x - width / 2.0f : x, y);
}

void InvaderVisual::drawBackdrop() {
	ofBackground(0);

	// A few faint lines across the table, fading out towards the far edge. They
	// are not decoration: with a black field and only a handful of moving things
	// on it, there is nothing to read the speed of the fall against.
	const int lineCount = 12;

	ofSetLineWidth(1.0f);

	for (int i = 1; i < lineCount; i++) {
		float y = static_cast<float>(i) / static_cast<float>(lineCount);

		// Brighter towards the near edge, where the round is decided.
		int alpha = static_cast<int>(ofMap(y, 0.0f, 1.0f, 6.0f, 34.0f));
		ofSetColor(Palette::colorAccent, alpha);
		ofDrawLine(playRect.x, toScreenY(y), playRect.x + playRect.width, toScreenY(y));
	}
}

void InvaderVisual::drawFallers() {
	if (!pillSprite.isBuilt()) {
		return; // nothing was built
	}

	// The four resting colours a piece's variant can land on - real medicine
	// comes in more than two colours, which is what replaced the fixed
	// turquoise/lilac pair this used to be. Built locally rather than kept as
	// a member: four ofColor copies once a frame is not worth a static array
	// just to avoid.
	const std::array<ofColor, 4> fallerColors = {
		Palette::colorBorderTurquoise, Palette::colorBorderLilac, Palette::colorBorderPurple, Palette::colorPillGreen
	};

	for (const auto & faller : game.getFallers()) {
		float centreX = toScreenX(faller.x);
		float centreY = toScreenY(faller.y);
		float height = toScreenSizeY(faller.halfHeight * 2.0f);

		// Faded in over the first fraction of a second, so one does not blink into
		// existence at the top edge.
		float fade = ofClamp(faller.age * 3.0f, 0.0f, 1.0f);

		// Tinted towards the warning colour as it comes down, so how close the
		// round is to being lost can be read off the field's colour without
		// anybody having to work out which one is lowest.
		float danger = ofClamp((faller.y - dangerStartY) / std::max(1.0f - dangerStartY, 0.01f), 0.0f, 1.0f);

		// Shape and resting colour are independent questions, both read off
		// the one variant int - see InvaderGame::fallerVariants. The
		// capsule's second half is always the slot two along rather than a
		// second random draw, so its own two-tone pairing stays the same
		// combination for as long as this faller falls.
		int shapeIndex = faller.variant % 3; // 0 pill, 1 tablet, 2 capsule
		int colorSlot = (faller.variant / 3) % static_cast<int>(fallerColors.size());
		bool isCapsule = shapeIndex == 2;

		ofColor topColor = fallerColors[colorSlot].getLerped(Palette::colorGlow, danger);
		ofColor bottomColor = fallerColors[(colorSlot + 2) % fallerColors.size()].getLerped(Palette::colorGlow, danger);
		ofColor body = topColor;
		// Scaled by its height: the height is what the danger line is measured
		// against, so it is the dimension that has to match the rules. The sprite
		// keeps its own proportions, which is why this cannot simply be drawn into
		// the collision box - that box is in play field units, and those are
		// stretched by whatever aspect ratio the projector has. There is no halo
		// behind it: every visible sprite cell is opaque once the short entrance
		// fade completes, so the medicine has no translucent rim.
		int alpha = static_cast<int>(255.0f * fade);

		if (isCapsule) {
			float halfHeight = height * 0.5f;
			ofSetColor(topColor, alpha);
			capsuleTopSprite.drawFitHeight(centreX, centreY - halfHeight * 0.5f, halfHeight);
			ofSetColor(bottomColor, alpha);
			capsuleBottomSprite.drawFitHeight(centreX, centreY + halfHeight * 0.5f, halfHeight);
		} else if (shapeIndex == 1) {
			ofSetColor(body, alpha);
			tabletSprite.drawFitHeight(centreX, centreY, height);
		} else {
			ofSetColor(body, alpha);
			pillSprite.drawFitHeight(centreX, centreY, height);
		}
	}
}

// A small rocket-firework burst rather than the generic radiating impact
// this used to be: a bright flash at the moment of ignition, then
// explosionSparkCount square sparks that fly outward on their own fixed
// angle and arc down under explosionSparkGravity, the way a real shell's
// sparks do after it goes off - in the same chunky, filled-square pixel
// style as everything else here rather than smooth lines or circles.
//
// A spark's whole path is worked out fresh from explosion.age every frame;
// nothing about it is stored anywhere, including in InvaderGame::Explosion
// itself, which stays the plain {x, y, age, variant} it already was. What
// keeps a spark from jumping to a new position every frame is a stable
// pseudo-random value seeded by its own index and which piece this was
// (explosion.variant) rather than re-rolled per frame - the same angle and
// speed come out of it every time, so the path it draws is continuous
// across the burst's short life even though nothing about it persists
// between frames.
void InvaderVisual::drawExplosions() {
	float lifetime = std::max(game.getExplosionLifetime(), 0.01f);

	// One hue apart from the falling pieces' own resting colours
	// (drawFallers()) and from what Palette::colorGlow already means elsewhere on
	// this screen (the danger line, the muzzle flash), so a burst reads as
	// its own small event rather than borrowing another one's colour.
	const std::array<ofColor, 4> sparkColors = {
		Palette::colorGlow, Palette::colorBorderTurquoise, Palette::colorSalmon, Palette::colorBorderPurple
	};

	for (const auto & explosion : game.getExplosions()) {
		float progress = ofClamp(explosion.age / lifetime, 0.0f, 1.0f);
		float fade = 1.0f - progress;

		if (fade <= 0.0f) {
			continue;
		}

		float pixelSize = toScreenSizeY(0.014f);

		// The ignition flash: gone within the burst's first quarter, before
		// the sparks have travelled far enough to be the thing worth
		// looking at.
		float flash = ofClamp(1.0f - progress * 4.0f, 0.0f, 1.0f);

		if (flash > 0.0f) {
			float fx = toScreenX(explosion.x);
			float fy = toScreenY(explosion.y);
			ofSetColor(Palette::colorAccentTurquoise, static_cast<int>(210.0f * flash));
			ofDrawRectangle(fx - pixelSize, fy - pixelSize, pixelSize * 2.0f, pixelSize * 2.0f);
		}

		for (int i = 0; i < explosionSparkCount; i++) {
			// A stable value in 0-1 from (i, explosion.variant) alone via a
			// sine-hash, the cheapest source of per-index pseudo-randomness
			// that needs no state of its own to stay consistent call to
			// call.
			float seed = std::sin(static_cast<float>(i) * 91.7f + explosion.variant * 13.3f) * 43758.5453f;
			seed -= std::floor(seed);

			float angle = TWO_PI * (static_cast<float>(i) / explosionSparkCount) + (seed - 0.5f) * 0.7f;
			float speed = 0.30f + seed * 0.35f;
			float distance = speed * explosion.age;

			float sx = explosion.x + std::cos(angle) * distance;
			float sy = explosion.y + std::sin(angle) * distance
				+ 0.5f * explosionSparkGravity * explosion.age * explosion.age;

			// Staggered by the same seed rather than a second random value,
			// so the sparks do not all wink out on the same frame.
			float sparkFade = ofClamp(fade - seed * 0.35f, 0.0f, 1.0f);

			if (sparkFade <= 0.0f) {
				continue;
			}

			ofSetColor(sparkColors[i % sparkColors.size()], static_cast<int>(230.0f * sparkFade));

			float size = pixelSize * ofLerp(1.3f, 0.5f, progress); // sparks shrink as they cool
			ofDrawRectangle(toScreenX(sx) - size * 0.5f, toScreenY(sy) - size * 0.5f, size, size);
		}
	}
}

void InvaderVisual::drawShots() {
	// Drawn as short streaks rather than dots: a dot travelling this fast across
	// a projector reads as a flicker, a streak reads as a shot. The cursor's
	// own pastel Lachs rather than the screen's hot glow tone, the same accent
	// the "you are here" cursor uses on every other screen - what the figure is
	// firing reads as unmistakably a visitor's own doing.
	float length = toScreenSizeY(0.030f);

	ofSetLineWidth(std::max(toScreenSizeX(0.004f), 2.0f));

	for (const auto & shot : game.getShots()) {
		float x = toScreenX(shot.x);
		float y = toScreenY(shot.y);
		float tailOffsetX = toScreenSizeX(shot.velocityX / std::max(shot.speed, 0.01f) * 0.030f);

		ofSetColor(Palette::colorSalmon, 90);
		ofDrawLine(x, y, x - tailOffsetX * 1.6f, y + length * 1.6f);

		ofSetColor(Palette::colorSalmon);
		ofDrawLine(x, y, x - tailOffsetX, y + length);
	}
}

void InvaderVisual::drawPlayer() {
	float centreX = toScreenX(game.getPlayerX());
	float centreY = toScreenY(game.getPlayerY());
	float halfWidth = toScreenSizeX(game.getPlayerHalfWidth());
	float halfHeight = toScreenSizeY(game.getPlayerHalfHeight());

	// Leans into its own movement. Nothing is steered by it - the figure goes
	// where it was told either way - but a shape that slides sideways without
	// tipping reads as the table moving under it rather than as it moving.
	float lean = ofClamp(game.getPlayerVelocity() / std::max(game.getPlayerSpeed(), 0.01f), -1.0f, 1.0f) * 0.22f;

	// The figure's ordinary colour, unless a caught Recolor pickup currently
	// says otherwise - see InvaderGame::isRecolorActive()/getRecolorVariant().
	// Milka-Violett, the same punchy Palette::colorActive Game of Life's own
	// "living" pixel uses, rather than the paler Flieder the rest of this
	// screen's chrome is - the player figure is the one thing on the table a
	// visitor is always meant to find first. None of the four recolor options
	// is Palette::colorActive itself, since the whole point of catching one is
	// that it visibly changed. Eased back to Palette::colorActive over the
	// pickup's last 1.2 seconds rather than snapping off, the same
	// fade-not-switch language every timed effect on this screen already
	// speaks.
	ofColor playerColor = Palette::colorActive;

	if (game.isRecolorActive()) {
		const std::array<ofColor, 4> recolorColors = {
			Palette::colorSalmon, Palette::colorBorderTurquoise, Palette::colorPowerUp, Palette::colorPillGreen
		};
		int slot = game.getRecolorVariant() % static_cast<int>(recolorColors.size());
		float revertBlend = ofClamp(1.0f - game.getRecolorSecondsLeft() / 1.2f, 0.0f, 1.0f);
		playerColor = recolorColors[slot].getLerped(Palette::colorActive, revertBlend);
	}

	ofPushMatrix();
	ofTranslate(centreX, centreY);
	ofRotateRad(lean);

	// Glow underneath, so the figure sits in a pool of its own light rather than
	// on the black.
	ofFill();
	ofSetColor(playerColor, 45);
	ofDrawCircle(0.0f, 0.0f, halfWidth * 1.8f);

	// The generated top-down Fusion craft deliberately has the same broad 15:9
	// silhouette as the old code-built turret. Width therefore keeps the proven
	// gameplay footprint while the PNG's own aspect supplies height without any
	// stretching or an invisible mismatch around the collision box.
	float spriteWidth = halfWidth * 2.4f;
	float spriteHeight = spriteWidth
		* static_cast<float>(playerSprite.getHeight())
		/ std::max(static_cast<float>(playerSprite.getWidth()), 1.0f);

	// Preserve the authored violet/lilac sprite at rest. Recolor pickups add a
	// restrained multiplicative tint while the stronger matching glow remains
	// the primary feedback, avoiding a flat single-colour silhouette.
	ofColor spriteTint = ofColor::white;
	if (game.isRecolorActive()) {
		spriteTint = ofColor::white.getLerped(playerColor, 0.32f);
	}
	ofSetColor(spriteTint);
	playerSprite.draw(-spriteWidth * 0.5f, -spriteHeight * 0.5f,
		spriteWidth, spriteHeight);

	// Muzzle flash, fading over the fraction of a second after each shot leaves.
	// The firing is continuous and nobody asked for it, so this is what makes it
	// read as the figure's doing.
	float fire = game.getFireFlash();

	if (fire > 0.0f) {
		ofSetColor(Palette::colorGlow, static_cast<int>(220.0f * fire));
		ofDrawCircle(0.0f, -spriteHeight * 0.48f, halfWidth * 0.4f * fire);
	}

	ofPopMatrix();
}

void InvaderVisual::drawShield() {
	float centreX = toScreenX(game.getPlayerX());
	float centreY = toScreenY(game.getPlayerY());
	float halfWidth = toScreenSizeX(game.getPlayerHalfWidth());

	if (game.isShieldActive()) {
		float remain = game.getShieldSecondsLeft();

		// Flickers faster as it runs low, so an unused shield's own timeout
		// (InvaderGame::getShieldTimeoutSeconds()) is something a visitor
		// can see coming rather than a rule that only shows itself by the
		// shield suddenly being gone.
		float warnWindow = 2.5f;
		float flicker = 1.0f;

		if (remain < warnWindow) {
			float rate = ofMap(remain, 0.0f, warnWindow, 9.0f, 2.5f, true);
			flicker = 0.5f + 0.5f * std::sin(ofGetElapsedTimef() * rate * TWO_PI);
		}

		ofNoFill();
		ofSetLineWidth(std::max(toScreenSizeX(0.0035f), 1.5f));
		ofSetColor(Palette::colorPowerUp, static_cast<int>(ofMap(flicker, 0.0f, 1.0f, 90.0f, 220.0f)));
		ofDrawCircle(centreX, centreY, halfWidth * 2.2f);
	}

	float breakFlash = game.getShieldBreakFlash();

	if (breakFlash > 0.0f) {
		// An expanding, fading ring where the shield stood the instant it
		// absorbs a hit - the same "sharp arrival, quick decay" shape used
		// everywhere else here a moment needs to read as an event.
		ofNoFill();
		ofSetLineWidth(std::max(toScreenSizeX(0.005f), 2.0f));
		ofSetColor(Palette::colorPowerUp, static_cast<int>(220.0f * breakFlash));
		ofDrawCircle(centreX, centreY, halfWidth * ofLerp(2.2f, 4.5f, 1.0f - breakFlash));
	}
}

void InvaderVisual::drawPowerUps() {
	float height = toScreenSizeY(powerUpDisplayHeight);
	const std::array<ofColor, 4> recolorColors = {
		Palette::colorSalmon, Palette::colorBorderTurquoise, Palette::colorPowerUp, Palette::colorPillGreen
	};

	for (const auto & powerUp : game.getPowerUps()) {
		float centreX = toScreenX(powerUp.x);
		float centreY = toScreenY(powerUp.y);
		const PixelSprite * sprite = &shieldPowerUpSprite;
		ofColor color = Palette::colorPowerUp;

		// Shape and tint both identify the kind. Recolor uses the tint it will
		// actually apply, so even two recolour pickups can advertise a different
		// result before they are caught.
		switch (powerUp.kind) {
		case InvaderGame::PowerUpKind::Shield:
			break;
		case InvaderGame::PowerUpKind::Recolor:
			sprite = &recolorPowerUpSprite;
			color = recolorColors[powerUp.colorVariant % static_cast<int>(recolorColors.size())];
			break;
		case InvaderGame::PowerUpKind::ShotPattern:
			sprite = &shotPatternPowerUpSprite;
			color = Palette::colorSalmon;
			break;
		case InvaderGame::PowerUpKind::FireRate:
			sprite = &fireRatePowerUpSprite;
			color = Palette::colorGlow;
			break;
		case InvaderGame::PowerUpKind::ShipSpeed:
			sprite = &shipSpeedPowerUpSprite;
			color = Palette::colorActive;
			break;
		}

		// A slow pulse rather than a static icon, so a pickup reads as
		// something worth reaching for rather than another faller. Phased
		// by its own x so several on the table at once do not pulse in
		// lockstep, without needing to store anything per pickup.
		float pulse = 0.5f + 0.5f * std::sin(ofGetElapsedTimef() * 4.0f + powerUp.x * 10.0f);

		ofFill();
		ofSetColor(color, static_cast<int>(ofMap(pulse, 0.0f, 1.0f, 45.0f, 70.0f)));
		ofDrawCircle(centreX, centreY, height * 0.9f);

		ofSetColor(color);
		sprite->drawFitHeight(centreX, centreY, height * ofMap(pulse, 0.0f, 1.0f, 0.92f, 1.05f));
	}
}

void InvaderVisual::drawDangerLine() {
	// The line the round is lost on. It is drawn because losing has to be
	// something a visitor could see coming - without it, the game simply stops
	// at an arbitrary moment.
	float y = toScreenY(1.0f) - 2.0f;
	float pulse = 0.5f + 0.5f * std::sin(ofGetElapsedTimef() * 2.5f);

	bool lost = game.getState() == InvaderGame::State::GameOver;

	ofSetLineWidth(lost ? 6.0f : 3.0f);
	ofSetColor(Palette::colorGlow, static_cast<int>(lost ? 255.0f : ofMap(pulse, 0.0f, 1.0f, 70.0f, 150.0f)));
	ofDrawLine(playRect.x, y, playRect.x + playRect.width, y);
}

void InvaderVisual::drawScore() {
	float height = playRect.height * scoreTextHeight;

	// At least the play area's own corner radius, so the score and wave
	// numbers - drawn right into the top corners - clear the rounded corner
	// mask instead of having their own corner painted over by it.
	float margin = std::max(height * 0.8f, getPlayAreaCornerRadius() * 1.15f);

	// Along the far short edge - the end of the table nobody stands at - so the
	// numbers are not under the visitor's own arms while they play.
	ofSetColor(Palette::colorAccent);
	drawText(ofToString(game.getScore()), playRect.x + margin, playRect.y + margin + height, height, false);

	// Right-aligned off the measured width rather than a guessed one: the wave
	// number gains a digit partway through a round.
	std::string waveLabel = "WAVE " + ofToString(game.getWave());
	float waveHeight = height * 0.7f;

	ofSetColor(Palette::colorAccent, 160);
	drawText(waveLabel, playRect.x + playRect.width - margin - textWidth(waveLabel, waveHeight), playRect.y + margin + height, waveHeight, false);

	// Says what a passer-by is looking at, and, more usefully, says it is not yet
	// them doing it. Blinks, because a steady label on a table gets read as part
	// of the furniture.
	if (demoActive && game.getState() == InvaderGame::State::Playing) {
		float blink = 0.5f + 0.5f * std::sin(ofGetElapsedTimef() * 2.0f);

		ofSetColor(Palette::colorGlow, static_cast<int>(ofMap(blink, 0.0f, 1.0f, 70.0f, 200.0f)));
		drawText("MOVE YOUR HANDS", playRect.x + playRect.width / 2.0f, playRect.y + margin + height, height * 0.6f, true);
	}
}

void InvaderVisual::drawWaveScore() {
	if (game.getState() != InvaderGame::State::Playing) {
		return;
	}

	float afterHold = waveScoreSeconds - waveScoreHoldSeconds;
	float alpha = afterHold <= 0.0f
		? 1.0f
		: 1.0f - ofClamp(afterHold / waveScoreFadeSeconds, 0.0f, 1.0f);

	if (alpha <= 0.0f) {
		return;
	}

	float centreX = playRect.x + playRect.width / 2.0f;
	float centreY = playRect.y + playRect.height / 2.0f;
	float height = playRect.height * 0.065f;

	// A restrained black field keeps both lines readable over moving fallers
	// without turning the wave boundary into another flashing celebration.
	ofSetColor(0, 0, 0, static_cast<int>(120.0f * alpha));
	ofDrawRectangle(
		playRect.x + playRect.width * 0.33f,
		playRect.y + playRect.height * 0.39f,
		playRect.width * 0.34f,
		playRect.height * 0.22f);

	ofSetColor(Palette::colorAccent, static_cast<int>(190.0f * alpha));
	drawText("WAVE " + ofToString(lastCompletedWave) + " COMPLETE",
		centreX, centreY - height * 0.35f, height * 0.55f, true);

	ofSetColor(Palette::colorGlow, static_cast<int>(255.0f * alpha));
	drawText(ofToString(lastWaveScore) + " PUNKTE",
		centreX, centreY + height * 0.65f, height, true);
}

void InvaderVisual::drawHighScores(float alpha) {
	if (alpha <= 0.0f) {
		return;
	}

	float centreX = playRect.x + playRect.width / 2.0f;
	float headerHeight = playRect.height * 0.065f;
	float rowHeight = playRect.height * 0.045f;
	int baseAlpha = static_cast<int>(255.0f * alpha);

	ofSetColor(Palette::colorGlow, baseAlpha);
	drawText("HIGHSCORES", centreX, playRect.y + playRect.height * 0.24f, headerHeight, true);

	const auto & entries = highScores.getEntries();
	if (entries.empty()) {
		ofSetColor(Palette::colorAccent, baseAlpha);
		drawText("NO SCORES YET", centreX, playRect.y + playRect.height * 0.55f, rowHeight, true);
		return;
	}

	// Five rows per column keep all ten places legible on the table's very wide
	// play area without reducing the pixel font to a thin ten-row stack.
	for (std::size_t index = 0; index < entries.size(); index++) {
		int column = static_cast<int>(index / 5);
		int row = static_cast<int>(index % 5);
		float x = playRect.x + playRect.width * (column == 0 ? 0.30f : 0.70f);
		float y = playRect.y + playRect.height * (0.40f + static_cast<float>(row) * 0.10f);
		bool currentRound = static_cast<int>(index) == lastCompletedRank;

		ofSetColor(currentRound ? Palette::colorGlow : Palette::colorAccent,
			currentRound ? baseAlpha : static_cast<int>(210.0f * alpha));
		drawText(ofToString(index + 1) + ".  " + ofToString(entries[index]),
			x, y, rowHeight, true);
	}
}

void InvaderVisual::drawGameOver() {
	if (game.getState() != InvaderGame::State::GameOver) {
		return;
	}

	float seconds = game.getStateSeconds();
	float total = std::max(game.getGameOverSeconds(), 0.01f);

	// Both presentation phases stay fully readable until the shared final fade.
	// The former full-duration linear fade made a ten-place table too dim before
	// a visitor had time to scan it.
	float fadeDuration = std::min(gameOverFadeOutSeconds, total);
	float fadeStart = total - fadeDuration;
	float fade = 1.0f - ofClamp((seconds - fadeStart) / std::max(fadeDuration, 0.01f), 0.0f, 1.0f);

	// The deepest, most muted shade of Lachs used anywhere - noticeably darker
	// than colorGlow's own hot tint and colorSalmon's pastel one, which is
	// exactly why the loss uses it: it is the one moment that should not look
	// like the rest of the piece, even though it is still built from the same
	// four hues everything else here is.
	const ofColor loseColor = Palette::colorAlarm;

	ofFill();
	ofSetColor(0, 0, 0, static_cast<int>(150.0f * fade));
	ofDrawRectangle(playRect.x, playRect.y, playRect.width, playRect.height);

	// The whole table flashes on the first moments of the loss - the field is
	// frozen by then, so this is the only thing still moving.
	float flash = ofClamp(1.0f - seconds * 2.5f, 0.0f, 1.0f);

	if (flash > 0.0f) {
		ofSetColor(loseColor, static_cast<int>(120.0f * flash));
		ofDrawRectangle(playRect.x, playRect.y, playRect.width, playRect.height);
	}

	float transitionStart = std::max(gameOverScoreSeconds - gameOverTransitionSeconds, 0.0f);
	float highScoreMix = ofClamp(
		(seconds - transitionStart) / std::max(gameOverTransitionSeconds, 0.01f),
		0.0f, 1.0f);
	float scoreAlpha = (1.0f - highScoreMix) * fade;
	float highScoreAlpha = highScoreMix * fade;

	float height = playRect.height * gameOverTextHeight;
	float centreX = playRect.x + playRect.width / 2.0f;
	float centreY = playRect.y + playRect.height / 2.0f;

	if (scoreAlpha > 0.0f) {
		ofSetColor(loseColor, static_cast<int>(255.0f * scoreAlpha));
		drawText("GAME OVER", centreX, centreY, height, true);

		// The completed round's own score gets a short, separate moment before
		// the ranking appears. ASCII is mandatory for visualApp's pixel font.
		ofSetColor(Palette::colorAccent, static_cast<int>(255.0f * scoreAlpha));
		drawText(ofToString(lastCompletedScore) + " PUNKTE", centreX,
			centreY + height * 1.1f, height * 0.5f, true);
	}

	drawHighScores(highScoreAlpha);
}

void InvaderVisual::draw() {
	// Worked out once here rather than in every helper above: the whole field
	// is drawn within the table's play rect rather than the full window, so
	// anything outside it can be left black - see VisualBase::getPlayRect().
	playRect = getPlayRect();

	drawBackdrop();
	drawFallers();
	drawPowerUps();
	drawExplosions();
	drawShots();
	drawPlayer();
	drawShield();
	drawDangerLine();
	drawScore();
	drawWaveScore();
	drawGameOver();
	maskPlayAreaCorners();
	drawPlayAreaBorder();

	// Left as the default, so a screen drawn after this one does not inherit
	// whatever colour the last thing here was tinted with.
	ofSetColor(255);
}

void InvaderVisual::onEnter() {
	// A fresh round every time the screen comes up: whatever the last visitor
	// left on it - a lost field, a demo halfway up the waves - is not what the
	// next one should walk into.
	game.reset();

	// The tracker's memory goes too. It is a memory of blobs that were over
	// the table while a different screen was showing, and the ids they carry
	// may well have been reused by then.
	tracker.reset();

	demoActive = true;
	noHandSeconds = demoAfterSeconds;
	handSeconds = 0.0f;
	handFollower.reset(0.5f);
	sawTypedHand = false;
	lastCompletedWave = 0;
	lastWaveScore = 0;
	waveScoreSeconds = waveScoreHoldSeconds + waveScoreFadeSeconds;

	// Three quick laps rather than one slow one - the one screen that is
	// itself urgent gets the most urgent-looking entrance of the four.
	sweepBorderOnEnter(Palette::colorBorderLilac, 3, 1.0f);
}

bool InvaderVisual::keyPressed(int key) {
	if (key == 'r') {
		game.reset();
		waveScoreSeconds = waveScoreHoldSeconds + waveScoreFadeSeconds;
		std::cout << "Invaders: round restarted" << std::endl;
		return true;
	}

	// Cycles automatic - demo - hands. The forced settings are what makes a
	// detection problem provable at the table: if the figure is steerable under
	// forced hands, the hands are arriving and it is the takeover rule that is
	// wrong; if it is not, nothing is arriving and the problem is Pi A's.
	if (key == 'v') {
		switch (control) {
		case Control::Automatic:
			control = Control::ForceDemo;
			std::cout << "Invaders: demo forced on" << std::endl;
			break;
		case Control::ForceDemo:
			control = Control::ForceHands;
			std::cout << "Invaders: hands forced on" << std::endl;
			break;
		case Control::ForceHands:
			control = Control::Automatic;
			std::cout << "Invaders: control back on the timers" << std::endl;
			break;
		}
		return true;
	}

	// How hard the game is: falling speed and how often things arrive, together,
	// since tuning them apart at the table means tuning neither.
	if (key == '[' || key == ']') {
		float scale = game.adjustDifficultyScale(key == ']' ? 0.1f : -0.1f);
		std::cout << "Invaders difficulty: " << scale << "x" << std::endl;
		return true;
	}

	// How much of the table a hand has to cross to send the figure edge to edge.
	// The one value that cannot be guessed off the table: it depends on how the
	// region of interest was set and how far people actually reach.
	if (key == ',' || key == '.') {
		handEdgeMargin = ofClamp(handEdgeMargin + (key == '.' ? 0.02f : -0.02f), 0.0f, 0.35f);
		std::cout << "Invaders hand margin: " << handEdgeMargin << std::endl;
		return true;
	}

	return false; // anything else still cycles to the next visual
}

std::string InvaderVisual::getStatusLine() const {
	std::string steering = demoActive ? "demo" : "hands";

	switch (control) {
	case Control::ForceDemo:
		steering += " (forced, 'v')";
		break;
	case Control::ForceHands:
		steering += " (forced, 'v')";
		break;
	case Control::Automatic:
		break;
	}

	// The two counts side by side are the whole diagnosis for the failure this
	// screen is prone to: objects arriving while none of them is being steered by
	// means the tracks are all inert or all held out, not that the sensor is
	// quiet. Whether Pi A typed any of them a hand is shown for the same reason -
	// it is a preference here, not a gate, and it is worth being able to see that
	// it is not what is deciding.
	std::string line = steering
		+ "  objects " + ofToString(tracker.getSeenCount()) + " (steering by " + ofToString(tracker.getUsableCount())
		+ (sawTypedHand ? ", typed hand" : ", none typed hand") + ")"
		+ "  score " + ofToString(game.getScore())
		+ "  wave " + ofToString(game.getWave())
		+ "  x " + ofToString(game.getPlayerX(), 2)
		+ " target " + ofToString(handFollower.getTarget(), 2)
		+ " driver " + ofToString(handFollower.getDriverId());

	// Both timers, always: which one is running says whether the screen thinks
	// somebody is there, and that is the question being asked when this line is
	// being read at all.
	line += "  hand " + ofToString(handSeconds, 1) + "/" + ofToString(takeoverSeconds, 2) + "s"
		+ "  idle " + ofToString(noHandSeconds, 1) + "/" + ofToString(demoAfterSeconds, 0) + "s";

	line += "  difficulty " + ofToString(game.getDifficultyScale(), 2) + "x ('[' ']')"
		+ "  hand margin " + ofToString(handEdgeMargin, 2) + " (',' '.')";

	// Power-up state is included because "why did that faller not end the
	// round" and "why are there now three shots" are exactly the kind of
	// remote-diagnosis questions this line exists to answer.
	line += "  powerups " + ofToString(game.getPowerUps().size());
	line += "  highscores " + ofToString(highScores.getEntries().size())
		+ "/" + ofToString(InvaderHighScores::capacity);

	if (game.isShieldActive()) {
		line += " shield " + ofToString(game.getShieldSecondsLeft(), 1) + "s";
	}

	if (game.isRecolorActive()) {
		line += " recolor " + ofToString(game.getRecolorSecondsLeft(), 1) + "s";
	}

	line += " upgrades pattern " + ofToString(game.getShotPatternLevel()) + "/" + ofToString(game.getMaxShotPatternLevel())
		+ " rate " + ofToString(game.getFireRateLevel()) + "/" + ofToString(game.getMaxFireRateLevel())
		+ " speed " + ofToString(game.getShipSpeedLevel()) + "/" + ofToString(game.getMaxShipSpeedLevel());

	if (game.getState() == InvaderGame::State::GameOver) {
		line += "  GAME OVER, restart in " + ofToString(std::max(game.getGameOverSeconds() - game.getStateSeconds(), 0.0f), 1) + "s";
	}

	return line;
}
