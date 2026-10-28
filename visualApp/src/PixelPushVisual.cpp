#include "PixelPushVisual.h"
#include "DynamicLifeSeeder.h"
#include "DynamicLifeRulePool.h"
#include <cmath>
#include <iostream>

PixelPushVisual::PixelPushVisual(bool withGameOfLife)
	: useGameOfLife(withGameOfLife) {

	// Shown in the debug HUD, which is the only way to tell the two apart from
	// the table once they are both in the rotation.
	title = withGameOfLife ? "PUSH + LIFE" : "PUSH";
}

void PixelPushVisual::seedGrid(float probability) {
	currentFillProbability = ofClamp(probability, 0.0f, 1.0f);
	for (int i = 0; i < GameOfLife::GRID_SIZE; i++) {
		grid[i] = (ofRandom(0.0f, 1.0f) < currentFillProbability) ? 1.0f : 0.0f;
	}
}

void PixelPushVisual::seedDynamicGrid() {
	const auto & dynamicRule = DynamicLifeRulePool::rules()[
		static_cast<std::size_t>(currentDynamicRuleIndex)];
	currentFillProbability = DynamicLifeSeeder::seed(grid,
		GameOfLife::GRID_WIDTH, GameOfLife::GRID_HEIGHT, dynamicRule.seedKind,
		[]() { return ofRandom(0.0f, 1.0f); });
}

// Draws the complete timing plan before phase one starts. Its sum is exposed to
// ofApp as this visual's production deadline, so no independent random mode
// interval can cut the sequence short.
void PixelPushVisual::beginRuleSequence() {
	for (std::size_t i = 0; i < 2; i++) {
		const auto & range = LifePhaseTiming::ranges[i];
		ruleDurations[i] = ofRandom(range.minSeconds, range.maxSeconds);
	}
	const auto & thirdRange = LifePhaseTiming::ranges[2];
	const float thirdMaximum = LifePhaseTiming::maximumThirdPhaseSeconds(
		ruleDurations[0], ruleDurations[1]);
	ruleDurations[2] = thirdMaximum <= thirdRange.minSeconds
		? thirdRange.minSeconds
		: ofRandom(thirdRange.minSeconds, thirdMaximum);

	activateRuleSet(0);
}

// Applies one of the three planned phases. Conway and Maze continue the
// material already on the table; only the dynamic third phase receives a new
// population suited to the selected rule.
void PixelPushVisual::activateRuleSet(int ruleIndex) {
	currentRuleIndex = ruleIndex;
	currentDynamicRuleIndex = -1;

	if (currentRuleIndex == randomRuleSlot) {
		const auto & dynamicRules = DynamicLifeRulePool::rules();
		currentDynamicRuleIndex = std::min(
			static_cast<int>(ofRandom(static_cast<float>(dynamicRules.size()))),
			static_cast<int>(dynamicRules.size()) - 1);
		const auto & rules = dynamicRules[static_cast<std::size_t>(currentDynamicRuleIndex)];
		gameOfLife.setRules(rules.birth, rules.survive);

		// This is a new population for the newly drawn rule, not a mutation of
		// the preceding Maze field. Each rule owns a sparse motif or set of local
		// clusters that exposes its characteristic growth without a noisy soup.
		if (useGameOfLife) {
			seedDynamicGrid();
		}
	} else {
		const auto & rules = fixedRulePresets[currentRuleIndex];
		gameOfLife.setRules(rules.first, rules.second);
	}

	ruleTimer = 0.0f;
	generationTimer = 0.0f;
	ruleInterval = ruleDurations[static_cast<std::size_t>(currentRuleIndex)];

	std::string ruleDetail;
	if (currentDynamicRuleIndex >= 0) {
		const auto & dynamicRule = DynamicLifeRulePool::rules()[
			static_cast<std::size_t>(currentDynamicRuleIndex)];
		ruleDetail = " (" + std::string(dynamicRule.name) + ", fill "
			+ ofToString(currentFillProbability * 100.0f, 1) + "%)";
	}

	std::cout << "Life phase " << (currentRuleIndex + 1)
			  << ruleDetail
			  << " for " << ruleInterval << " s" << std::endl;
}

void PixelPushVisual::advanceRuleSet() {
	if (currentRuleIndex + 1 < ruleSlotCount) {
		activateRuleSet(currentRuleIndex + 1);
	}
}

void PixelPushVisual::setup() {
	VisualBase::setup();

	// Called even on the screen that runs without Game of Life: it is what zeroes
	// the influence field GameOfLife::transform() reads, so leaving it out would
	// hand that method uninitialized memory the moment 'g' turned life on.
	gameOfLife.setup();
	pixelPush.setup();

	seedGrid(fillProbability);

	// One texel per grid cell, drawn without filtering: the cells are meant to
	// be visible as chunky pixels rather than smoothed into a blur.
	texture.allocate(GameOfLife::GRID_WIDTH, GameOfLife::GRID_HEIGHT, GL_RGB);
	texture.setTextureMinMagFilter(GL_NEAREST, GL_NEAREST);
}

bool PixelPushVisual::isReady(std::string & failureReason) const {
	if (!texture.isAllocated()) {
		failureReason = "PixelPush grid texture allocation failed";
		return false;
	}

	failureReason.clear();
	return true;
}

float PixelPushVisual::getScheduledModeDurationSeconds() const {
	return useGameOfLife ? LifePhaseTiming::totalSeconds(ruleDurations) : 0.0f;
}

void PixelPushVisual::update(const std::vector<DetectedObject> & detectedObjects) {
	float deltaTime = static_cast<float>(ofGetLastFrameTime());

	// Pushing runs every frame, so a hand's movement is followed as closely as
	// the display can show it - a shove that only updated on the generation tick
	// would feel like the material was lagging behind the hand.
	pixelPush.update(detectedObjects, deltaTime);
	grid = pixelPush.transform(grid);

	// The "you are here" cursor, one per object still held by positionTracker
	// (not one per object in this frame's raw detectedObjects) - see
	// PixelPush::getPositionTracker(). Drawing from the tracker rather than
	// the raw list is what keeps the cursor drifting smoothly through a
	// dropped frame or two instead of blinking out and snapping back. Full
	// engagement inside the push-height gate, dimmed above it - the gate is
	// otherwise invisible, so a hand held slightly too high to push had no
	// way to explain why nothing was happening under it. Passing the
	// track's height as the cursor's height on top of that means the ring
	// itself closes in as a hand is lowered towards the gate, which is the
	// same fact told twice, once as brightness and once as size. Read after
	// pixelPush.update() so the threshold reflects whatever it just
	// adjusted itself to.
	clearHandCursors();

	float heightThreshold = pixelPush.getPushHeightThreshold();
	const ObjectTracker & positionTracker = pixelPush.getPositionTracker();

	for (const auto & track : positionTracker.getTracks()) {
		if (track.unseenSeconds > positionTracker.holdSeconds) {
			continue; // gone longer than the hold grace - no longer "seen"
		}

		float engagement = track.z <= heightThreshold ? 1.0f : 0.15f;
		addHandCursor(glm::vec2(ofClamp(track.x, 0.0f, 1.0f), ofClamp(track.y, 0.0f, 1.0f)), track.z, engagement);
	}

	// Game of Life runs on its own clock, and only if it is switched on at all
	// (see useGameOfLife). With it off nothing here is timed, so both clocks stop
	// rather than running up numbers nobody reads - and in particular the rules
	// do not quietly rotate behind a screen that is not applying them.
	if (useGameOfLife) {
		generationTimer += deltaTime;
		ruleTimer += deltaTime;

		// Feeds GameOfLife's own influence field from this frame's hands -
		// see the class comment on why this screen alone calls it. Off on
		// the push-only screen for the same reason the two clocks above are:
		// nothing here should be quietly running while it is not what a
		// visitor is looking at.
		gameOfLife.update(detectedObjects);

		// How long the table has been empty, and how much of the idle
		// shimmer that is worth showing right now - see updateTexture() and
		// idleShimmerStrength's own comment.
		if (detectedObjects.empty()) {
			idleSeconds += deltaTime;
		} else {
			idleSeconds = 0.0f;
		}

		float shimmerTarget = idleSeconds > idleSecondsBeforeShimmer ? 1.0f : 0.0f;
		float shimmerFollow = shimmerTarget > idleShimmerStrength ? shimmerFollowUp : shimmerFollowDown;
		idleShimmerStrength += (shimmerTarget - idleShimmerStrength) * ofClamp(shimmerFollow * deltaTime, 0.0f, 1.0f);
	} else {
		idleSeconds = 0.0f;
		idleShimmerStrength = 0.0f;
	}

	// Conway advances to Maze, then Maze to the newly seeded dynamic phase. Once
	// phase three ends it stays visible while ModeRotationScheduler waits out a
	// possible interaction grace period; the sequence never wraps back to Conway.
	if (useGameOfLife && ruleTimer >= ruleInterval
		&& currentRuleIndex + 1 < ruleSlotCount) {
		advanceRuleSet();
	}

	const float activeGenerationInterval = currentRuleIndex == randomRuleSlot
		? generationInterval * dynamicGenerationIntervalScale
		: generationInterval;
	if (useGameOfLife && generationTimer >= activeGenerationInterval) {
		generationTimer = 0.0f;

		// GameOfLife answers in alive/dead flags, which is the one thing it
		// knows; the material grid is then updated from that answer rather than
		// replaced by it. A cell that survives keeps whatever has been heaped
		// onto it - taking the larger of the two also gives a newly born cell a
		// full cell's worth of material to start from. A cell that dies is faded
		// instead of emptied, so a heap that gets eaten away still looks like a
		// heap while it goes.
		GameOfLife::Grid alive = gameOfLife.transform(grid);
		float massEpsilon = pixelPush.getMassEpsilon();

		for (int i = 0; i < GameOfLife::GRID_SIZE; i++) {
			if (alive[i] > 0.5f) {
				grid[i] = std::max(grid[i], 1.0f);
			} else {
				float faded = grid[i] * deathDecay;

				// Fading alone only ever approaches zero, so the remnant has to
				// be snapped away or it stays in the grid forever as a value too
				// small to see and expensive to keep processing - the same trap
				// massEpsilon exists for in PixelPush, reached from the other
				// direction.
				grid[i] = (faded < massEpsilon) ? 0.0f : faded;
			}
		}
	}

	updateTexture();

	// The border's reaction to a push: a cooldown rather than a plain rising
	// edge, since a hand held down mid-push sits above the threshold for many
	// frames in a row and would otherwise flash the border every one of them.
	pushPulseCooldownRemaining = std::max(pushPulseCooldownRemaining - deltaTime, 0.0f);

	if (pixelPush.getMaxDisplacement() >= pushPulseThreshold && pushPulseCooldownRemaining <= 0.0f) {
		pulseBorder();
		pushPulseCooldownRemaining = pushPulseCooldown;
	}

	updateBorderAnimation(deltaTime);
}

void PixelPushVisual::updateTexture() {
	// PixelPush hands this back by reference - it is 60 kB and only read here.
	const GameOfLife::Grid & push = pixelPush.getPushField();

	// Anything above a single cell's worth is a heap, and the ramp from
	// heapDisplayMass down is what makes one visible as such.
	float heapRange = std::max(pixelPush.getHeapDisplayMass() - 1.0f, 0.001f);

	// Hoisted out of the loop: these are the same four colours for every one of
	// the 15,000 cells, and building them per cell was 60,000 constructions a
	// frame for nothing.
	const ofColor empty(0, 0, 0);
	const ofColor material = Palette::colorActive; // Milka-Violett: ordinary material
	const ofColor piled = Palette::colorAccent; // Flieder: heaped up
	const ofColor shove = Palette::colorGlow; // deep Lachs: currently being pushed

	// One read of the clock rather than one per cell - see idleShimmerStrength's
	// own comment for what this drives and why it costs nothing on an
	// ordinary interactive frame.
	float shimmerTime = ofGetElapsedTimef() * shimmerNoiseSpeed;

	for (int i = 0; i < GameOfLife::GRID_SIZE; i++) {
		float mass = grid[i];
		float pushHere = push[i];

		// A moving noise field rather than a uniform flicker: only the
		// brightest share of it (shimmerThreshold) actually lights a cell at
		// any given moment, which is what makes this read as a scatter of
		// sparks travelling across the table rather than the whole grid
		// dimming and brightening together. ofNoise() is skipped entirely
		// while idleShimmerStrength is exactly 0 (not idle long enough, or
		// this is the push-only screen - see update()), so this costs
		// nothing on an ordinary interactive frame.
		float shimmer = 0.0f;

		if (idleShimmerStrength > 0.0f) {
			int gx = i % GameOfLife::GRID_WIDTH;
			int gy = i / GameOfLife::GRID_WIDTH;
			float n = ofNoise(static_cast<float>(gx) * shimmerNoiseScale, static_cast<float>(gy) * shimmerNoiseScale, shimmerTime);
			shimmer = ofClamp(ofMap(n, shimmerThreshold, 1.0f, 0.0f, 1.0f, true), 0.0f, 1.0f) * idleShimmerStrength;
		}

		// The shimmer's own colour: Flieder, drifting towards Turkis as this
		// cell's own shimmer value approaches its brightest share, so the
		// noise's densest points carry a second accent rather than reading as
		// one flat tint throughout.
		ofColor shimmerTint = piled.getLerped(Palette::colorAccentTurquoise, shimmer * shimmerTurquoiseMix);

		// Most of the grid is empty most of the time. Nobody pushing it and
		// no shimmer on it right now is still black without any
		// interpolation being needed; a shimmer alone is what lets a cell
		// that has never been touched still light up on its own.
		if (mass <= 0.0f && pushHere <= 0.0f) {
			if (shimmer <= 0.0f) {
				pixels[i * 3 + 0] = 0;
				pixels[i * 3 + 1] = 0;
				pixels[i * 3 + 2] = 0;
			} else {
				ofColor c = empty.getLerped(shimmerTint, shimmer * shimmerEmptyAlpha);
				pixels[i * 3 + 0] = c.r;
				pixels[i * 3 + 1] = c.g;
				pixels[i * 3 + 2] = c.b;
			}
			continue;
		}

		float density = ofClamp(mass, 0.0f, 1.0f); // empty up to one cell's worth
		float heap = ofClamp((mass - 1.0f) / heapRange, 0.0f, 1.0f); // piled beyond that

		ofColor c = empty.getLerped(material, density);
		c = c.getLerped(piled, heap);

		// Warms the area the hand is actually shoving, so the cause of the
		// movement is visible on the table and not just its effect.
		c = c.getLerped(shove, pushHere * 0.35f);

		// The same shimmer, weaker here than on an empty cell (shimmerFilledAlpha
		// vs shimmerEmptyAlpha) so it reads as a highlight on material that is
		// already there rather than overriding it.
		if (shimmer > 0.0f) {
			c = c.getLerped(shimmerTint, shimmer * shimmerFilledAlpha);
		}

		pixels[i * 3 + 0] = c.r;
		pixels[i * 3 + 1] = c.g;
		pixels[i * 3 + 2] = c.b;
	}

	texture.loadData(pixels.data(), GameOfLife::GRID_WIDTH, GameOfLife::GRID_HEIGHT, GL_RGB);
}

void PixelPushVisual::draw() {
	ofBackground(0);

	// Drawn into the table's play rect rather than the full window - what is
	// outside it stays black, which is what keeps it indistinguishable from
	// the table's own unprojected margin. See VisualBase::getPlayRect().
	ofRectangle playRect = getPlayRect();
	ofSetColor(255);
	texture.draw(playRect.x, playRect.y, playRect.width, playRect.height);

	maskPlayAreaCorners();
	drawHandCursors();
	drawPlayAreaBorder();
}

bool PixelPushVisual::keyPressed(int key) {
	if (key == 'g') {
		useGameOfLife = !useGameOfLife;

		// Reset rather than resumed: with Game of Life off nothing replenishes
		// the material, so it is worth being able to get a full table back
		// without leaving the screen.
		if (!useGameOfLife) {
			seedGrid(fillProbability);
		} else if (currentRuleIndex == randomRuleSlot) {
			// Turning life back on while its dynamic slot is selected should have
			// the same sparse starting structures as entering that slot by timer.
			seedDynamicGrid();
		}

		generationTimer = 0.0f;
		ruleTimer = 0.0f; // the preset gets its full time from when life comes back on
		std::cout << "Game of Life: " << (useGameOfLife ? "on" : "off") << std::endl;
		return true;
	}

	// '=' as well as '+', because '+' needs shift on most layouts and the
	// installation is calibrated over a remote shell.
	if (key == '+' || key == '=') {
		generationInterval = std::max(generationInterval / generationIntervalStep, minGenerationInterval);
		std::cout << "Generation interval: " << generationInterval << " s" << std::endl;
		return true;
	}

	if (key == '-') {
		generationInterval = std::min(generationInterval * generationIntervalStep, maxGenerationInterval);
		std::cout << "Generation interval: " << generationInterval << " s" << std::endl;
		return true;
	}

	// How high a hand may be and still push. Printed together with the height
	// the gate currently sits at, because that one moves on its own as hands are
	// seen - the fraction alone would not say what is actually being asked of a
	// visitor.
	if (key == '[' || key == ']') {
		float fraction = pixelPush.adjustPushHeightFraction(key == ']' ? 0.05f : -0.05f);

		std::cout << "Push height: " << fraction << " of observed hand height"
				  << " (currently z < " << pixelPush.getPushHeightThreshold() << ")" << std::endl;
		return true;
	}

	// How wide a hand's reach is.
	if (key == ',' || key == '.') {
		float scale = pixelPush.adjustReachScale(key == '.' ? 0.15f : -0.15f);

		std::cout << "Hand reach: " << scale << "x" << std::endl;
		return true;
	}

	return false; // anything else still cycles to the next visual
}

std::string PixelPushVisual::getStatusLine() const {
	// The threshold rather than only the fraction it came from: the fraction is
	// what the keys change, but the threshold is what a hand is actually
	// measured against, and it drifts with what the sensor has been seeing.
	std::string line = "push if z < " + ofToString(pixelPush.getPushHeightThreshold(), 2)
		+ " ('[' ']')  reach " + ofToString(pixelPush.getReachScale(), 2) + "x (',' '.')";

	if (useGameOfLife) {
		// The preset and how long it still has, because the rules now change
		// without anybody asking: a grid behaving oddly is only readable if it is
		// clear which rule set is running and that it is about to be a different
		// one.
		const float activeGenerationInterval = currentRuleIndex == randomRuleSlot
			? generationInterval * dynamicGenerationIntervalScale
			: generationInterval;
		line += "  life on, gen " + ofToString(activeGenerationInterval, 3) + "s ('g' '+' '-')"
			+ "  rules " + ofToString(currentRuleIndex);
		if (currentDynamicRuleIndex >= 0) {
			const auto & dynamicRule = DynamicLifeRulePool::rules()[
				static_cast<std::size_t>(currentDynamicRuleIndex)];
			line += "/" + std::string(dynamicRule.name)
				+ " fill " + ofToString(currentFillProbability * 100.0f, 1) + "%";
		}
		line += ", next in "
			+ ofToString(std::max(ruleInterval - ruleTimer, 0.0f), 0) + "s";
	} else {
		line += "  life off ('g')";
	}

	return line;
}

void PixelPushVisual::onEnter() {
	// Reseeded on every entry, because this screen's grid is the one thing a
	// visitor can permanently ruin - material shoved into a corner and left
	// there stays there, and on the screen without Game of Life nothing ever
	// puts it back. Starting over means the next person meets a full table.
	pixelPush.setup();
	seedGrid(fillProbability);

	// gameOfLife.update() only runs while this screen is showing (see
	// update()), so a visitor's influence field would otherwise sit frozen
	// while some other screen is up and resume from stale hand positions on
	// return - this clears it the same way pixelPush.setup() clears the
	// displacement field above. Also re-randomizes GameOfLife's own internal
	// grid member, which is harmless: this class never reads it, only ever
	// handing gameOfLife.transform() its own separate grid above.
	gameOfLife.setup();

	generationTimer = 0.0f;
	pushPulseCooldownRemaining = 0.0f;

	// The complete Conway -> Maze -> dynamic sequence, including all three
	// durations, is drawn afresh instead of resuming the previous visitor's phase.
	beginRuleSequence();

	// One lap with life on, two quicker ones without it - the plainer of the
	// two screens gets the livelier entrance so the pair still reads as
	// different screens from the border alone.
	if (useGameOfLife) {
		sweepBorderOnEnter(Palette::colorBorderLilac, 1, 1.6f);
	} else {
		sweepBorderOnEnter(Palette::colorBorderLilac, 2, 1.2f);
	}
}
