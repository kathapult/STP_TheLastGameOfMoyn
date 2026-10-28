#pragma once

#include "DetectedObject.h"
#include "GameOfLife.h"
#include "LifePhaseTiming.h"
#include "PixelPush.h"
#include "VisualBase.h"
#include "ofMain.h"

// The Game of Life screen: the grid can be shovelled around by hand - a hand
// moving close to the table drags the material with it and heaps it up - while
// Conway's rules play out in whatever arrangement the pushing leaves behind.
//
// It is a chain of transforms rather than a variant of either (constitution
// Principle III): PixelPush moves material every frame, and GameOfLife judges
// what survives in the new arrangement on its own tick. Both of those classes
// are used exactly as they are - neither knows this screen exists.
//
// Two of these are in the rotation, differing only in whether Game of Life is in
// the chain, which the constructor takes:
//
//   - with it, the material is pushed around while life plays out in it;
//   - without it, the material only ever moves where it is pushed, and a heap
//     stays a heap. That is the screen where the pixels are simply shovelled
//     into piles, which is a different thing to stand at than one where what you
//     piled up is immediately eaten.
//
// They are one class rather than two because the difference really is only that
// one link of the chain, and the transforms are what carry the behavior.
// Measurement on why both exist, rather than one being the obvious winner, is in
// processDocumentation.md - briefly, pushing gathers material into dense clumps
// and Conway's rules kill dense clumps, so the two screens are the same
// interaction answered two ways.
//
// A third Game of Life screen used to sit in front of these - plain Conway with
// clickable rule buttons and no pushing at all. It was folded into these: it drew
// the same grid while asking less of a visitor, and what it did contribute lives
// on as the rule-preset rotation below.
//
// On the push-only screen a hand still does one thing and one thing only: it
// pushes. On the screen with Game of Life, gameOfLife.update() is now called
// every frame alongside pixelPush.update() (see PixelPushVisual::update()),
// which feeds GameOfLife's own influence field from the same detectedObjects
// - so besides pushing material around, a hand also has a chance to revive
// dead cells underneath it on the next generation tick, through
// GameOfLife::transform()'s own already-built "influence can reanimate a
// dead cell" rule. That rule existed and was reachable from the moment this
// class was written; only the call that fed it was missing.
class PixelPushVisual : public VisualBase {
public:
	// withGameOfLife also decides what 'g' toggles away from - each screen keeps
	// its own setting once changed, so the two can be compared at the table
	// without leaving whichever one is showing.
	explicit PixelPushVisual(bool withGameOfLife);

	void setup() override;
	void update(const std::vector<DetectedObject> & detectedObjects) override;
	void draw() override;
	void onEnter() override;
	bool isReady(std::string & failureReason) const override;
	float getScheduledModeDurationSeconds() const override;

	// 'g' turns Game of Life on and off, '+'/'-' change how fast its generations
	// run, '['/']' how high a hand may be and still push, ','/'.' how wide a
	// hand's reach is. All four are questions about how the screen behaves with
	// a hand over the real table, so they are answered there rather than by
	// rebuilding. The rule presets are deliberately not among them - they are the
	// one thing here that is meant to run without anybody deciding.
	bool keyPressed(int key) override;

	// The height gate and the reach, for the debug HUD. Both are what a visitor
	// is being asked to do with their hands, and the height one moves on its own
	// as hands are seen, so calibrating without being able to read them is
	// guesswork.
	std::string getStatusLine() const override;

private:
	// Turns the current mass grid into the texture's RGB bytes. Split out of
	// update() so the frame's logic and its colouring stay readable apart.
	void updateTexture();

	// Scatters the starting material across the table. Done here rather than by
	// taking GameOfLife's own random fill, so the chain's first link stays
	// PixelPush's business and GameOfLife is only ever asked about survival.
	void seedGrid(float probability);

	// Builds the selected third-phase rule's sparse motifs or local clusters.
	// This keeps global density low without relying on a random soup to happen
	// to contain the structures that make the rule spread.
	void seedDynamicGrid();

	// Draws all three phase durations together and starts at Conway. The mode
	// scheduler reads their sum, so its deadline and the end of phase three are
	// the same planned moment.
	void beginRuleSequence();

	// Applies one phase's rule and duration. The dynamic third phase also draws
	// its rule-specific sparse seed and starts from a fresh population.
	void activateRuleSet(int ruleIndex);

	// Advances only within the three-part sequence. The third rule remains in
	// place if interaction grace postpones the actual production mode switch.
	void advanceRuleSet();

	GameOfLife gameOfLife; // birth/death, run on the generation tick below
	PixelPush pixelPush; // displacement, run every frame

	// Cell values here are quantities of material, not the 0.0/1.0 alive flags
	// GameOfLife hands back: pushing can heap several cells' worth into one and
	// can smear a cell's worth thinly across several.
	GameOfLife::Grid grid;

	ofTexture texture;
	std::array<unsigned char, GameOfLife::GRID_SIZE * 3> pixels;

	// Whether Game of Life runs on this screen at all - set by the constructor,
	// and still togglable with 'g' so the two screens can be compared without
	// switching between them.
	bool useGameOfLife;

	// The rule sets the screen moves through, and how long each one is kept.
	// Only ever used while life is on, so on the push-only screen they simply sit
	// there unread.
	//
	// They change on a timer rather than by hand because these screens absorbed
	// one whose rules were picked from on-screen buttons, and there is nobody at
	// the installation to press them. Every entry starts with unchanged Conway,
	// briefly contrasts it with Maze, then gives a newly seeded curated rule the
	// remaining long part of the mode. LifePhaseTiming owns the three requested
	// random ranges and therefore the complete mode budget.
	std::vector<std::pair<std::vector<int>, std::vector<int>>> fixedRulePresets = {
		{ { 3 }, { 2, 3 } }, // classic Conway
		{ { 3 }, { 2, 3, 4 } } // maze: survives more crowding, grows corridors
	};

	// The third phase draws one balanced outer-totalistic rule. Rule-specific
	// sparse motifs make its growth legible from the beginning instead of asking
	// one field-wide random density to suit four very different rules.
	static constexpr int randomRuleSlot = 2;
	static constexpr int ruleSlotCount = static_cast<int>(LifePhaseTiming::ranges.size());

	int currentRuleIndex = 0;
	int currentDynamicRuleIndex = -1;
	float ruleTimer = 0.0f;
	float ruleInterval = 0.0f;
	LifePhaseTiming::Durations ruleDurations {};

	// Conway's unchanged random starting density, followed by the actual density
	// most recently seeded (including sparse third-phase motifs) for diagnostics.
	float fillProbability = 0.2f;
	float currentFillProbability = 0.2f;

	// Seconds between Game of Life generations, adjustable with '+' and '-'.
	// Conway and Maze keep the existing one-generation-per-frame pace at 60 fps.
	float generationInterval = 1.0f / 60.0f;
	float generationTimer = 0.0f;

	// Sparse expanding motifs need time to be read as fronts rather than flashing
	// through hundreds of generations. Only phase three applies this multiplier,
	// giving it 15 generations per second at the unchanged default base rate.
	float dynamicGenerationIntervalScale = 4.0f;

	// Range the '+'/'-' keys stay inside: from every frame on a fast display up
	// to a generation every two seconds.
	float minGenerationInterval = 1.0f / 120.0f;
	float maxGenerationInterval = 2.0f;

	// Factor each press of '+' or '-' multiplies the interval by. A ratio rather
	// than a fixed step, so the keys stay useful across the whole range.
	float generationIntervalStep = 1.3f;

	// What is left of a cell's material after a generation in which Game of Life
	// declared it dead. Below 1.0 so dead matter fades out over a few
	// generations instead of blinking away, which is what lets a pushed heap
	// still read as a heap while it is being consumed.
	//
	// It MUST stay below the 0.5 that GameOfLife counts as alive. Above it, a
	// cell killed in one generation is still alive in the next, death takes two
	// generations everywhere at once, and the grid silts up into a solid mass -
	// measured at 46% of cells alive against the ~6% plain Game of Life settles
	// at. At 0.45 the remnant is immediately dead, still visible, and gone over
	// about three generations.
	float deathDecay = 0.45f;

	// The border's reaction to a push: PixelPush::getMaxDisplacement() crossing
	// this many grid cells counts as a deliberate push rather than a hand
	// resting or drifting. Not yet verified at the table - see
	// processDocumentation.md.
	float pushPulseThreshold = 1.5f;

	// How long after one push pulse before another can fire, so a hand held
	// down mid-push does not strobe the border every frame it stays above
	// pushPulseThreshold.
	float pushPulseCooldown = 0.6f;
	float pushPulseCooldownRemaining = 0.0f;

	// How long nothing has been over the table - only tracked, and only
	// acted on, while useGameOfLife is on (see updateTexture()): the
	// push-only screen stays exactly what a hand leaves it as, the same way
	// its grid never revives on its own either. Presence-based rather than
	// keyed to an actual push, the same test CruiseNavigator's and
	// InvaderVisual's own idle timers use - a hand resting without pushing
	// still counts as somebody being there.
	float idleSeconds = 0.0f;
	float idleSecondsBeforeShimmer = 8.0f;

	// How much of the idle shimmer (see updateTexture()) is currently
	// showing, eased towards 1 once idleSecondsBeforeShimmer is passed and
	// towards 0 the instant a hand reappears. Slow up, fast down on purpose
	// - the shimmer is meant to read as the table quietly starting to
	// breathe on its own, but it must never be seen to delay or fight an
	// actual visitor's own push.
	float idleShimmerStrength = 0.0f;
	float shimmerFollowUp = 0.25f;
	float shimmerFollowDown = 6.0f;

	// The moving noise field the shimmer is sampled from: how large its
	// features are relative to one grid cell, and how fast it drifts
	// through time. Low and slow on purpose - a field of chunky pixels
	// flickering fast would read as static, not as something breathing.
	float shimmerNoiseScale = 0.12f;
	float shimmerNoiseSpeed = 0.15f;

	// Only the brightest share of the noise field lights up at any given
	// moment - a sparse, travelling scatter of cells rather than the whole
	// grid dimming and brightening together, which is what makes it read as
	// "the pixels flicker" and not "the screen breathes".
	float shimmerThreshold = 0.65f;

	// How strongly the shimmer shows on an otherwise-empty cell versus one
	// that already has its own colour - weaker on filled cells so the
	// shimmer reads as a highlight on the material that is there rather
	// than overriding it.
	float shimmerEmptyAlpha = 0.75f;
	float shimmerFilledAlpha = 0.35f;

	// How far the shimmer's own colour drifts from Palette::colorAccent
	// (Flieder) towards Palette::colorAccentTurquoise as a cell's shimmer
	// value approaches its own brightest share - a light second accent in the
	// noise's densest points, rather than one flat tint throughout.
	float shimmerTurquoiseMix = 0.3f;
};
