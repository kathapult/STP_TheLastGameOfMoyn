#pragma once

#include "DetectedObject.h"
#include "InvaderGame.h"
#include "InvaderHandFollower.h"
#include "InvaderHighScores.h"
#include "ObjectTracker.h"
#include "PixelSprite.h"
#include "VisualBase.h"
#include "ofMain.h"
#include <string>
#include <vector>

// The one screen on the table that is a game rather than a picture: things come
// down the length of the table towards the near edge, a figure standing at the
// middle of that edge fires at them without being asked to, and a hand held over
// the table decides where it stands. One of them getting past it ends the round.
//
// It is the last screen in the rotation on purpose. Everything before it answers
// a hand with a picture; this one answers with a score and a way to lose, which
// is a different thing to ask of somebody and only worth asking once they have
// already found out that the table watches them at all.
//
// Two things about an installation, rather than a game, shape most of what is in
// here:
//
//   - Nobody explains it. The figure fires on its own and the only input is left
//     and right, so the whole instruction is "move your hand" and a visitor can
//     work that out by moving it.
//   - It has to be worth walking up to. With nobody at the table the figure
//     plays itself (see demoAfterSeconds), so what a passer-by sees from across
//     the room is a game in progress rather than a still frame - and, because
//     the autopilot is deliberately imperfect, a game that can be lost.
//
// The rules themselves are in InvaderGame; this class only reads hands, decides
// who is steering, and draws.
class InvaderVisual : public VisualBase {
public:
	InvaderVisual();

	void setup() override;
	void update(const std::vector<DetectedObject> & detectedObjects) override;
	void draw() override;
	void onEnter() override;
	bool isReady(std::string & failureReason) const override;

	// 'r' restarts the round, 'v' cycles who is steering (automatic / demo
	// forced / hands forced), '[' / ']' change how hard the game is, ',' / '.'
	// how much of the table a hand has to cross to send the figure from edge to
	// edge. All of them are questions that can only be answered with a hand over
	// the real table.
	bool keyPressed(int key) override;

	std::string getStatusLine() const override;

private:
	// Who is steering. The two forced settings exist because this is the one
	// screen where a detection problem is indistinguishable from the piece
	// working as designed - a demo that will not hand over and a table nobody is
	// standing at look exactly alike from the far end of an SSH session.
	enum class Control {
		Automatic, // the timers below decide
		ForceDemo, // for watching the autopilot without leaving the table
		ForceHands // for proving a suspected detection problem is one
	};

	// The stream's short per-id memory (smoothed position, stillness,
	// hold/forget bookkeeping) and the single-exclusive-driver selection this
	// screen used to lack entirely - readSteeringTarget() used to average
	// every currently-usable track's raw x together instead, which is exactly
	// the lurch CruiseNavigator was built around avoiding: a second visitor's
	// hand appearing nearby, or a contour splitting for a few frames, moved
	// the average and jumped the figure. One exclusive driver, chosen and held
	// the same way the flight chooses who is flying it, replaces that - see
	// ObjectTracker::chooseExclusive() and chooseSteeringTrack() below.
	ObjectTracker tracker;

	// Picks which track is steering the figure this frame, or nullptr if
	// nobody currently qualifies. Pi A's classification is used as an
	// acquisition preference, never as a hard gate: a continuing driver survives
	// transient Hand/Object relabelling, while a new driver prefers typed hands.
	// This keeps a misclassified hand usable without letting classification
	// flicker switch the figure to a different contour.
	const ObjectTracker::Track * chooseSteeringTrack(float deltaTime);

	// Decides whether the figure is being steered or is playing itself this
	// frame, and restarts the round when that changes hands.
	void updateControl(float deltaTime, bool handSeen);

	// Play field coordinates (0-1 across and down the table) to pixels, within
	// the table's play rect rather than the full window - see
	// VisualBase::getPlayRect(). No frame, no title, nothing drawn at the edges
	// for a visitor to have to lean over; only the animated border sits outside
	// the play field itself.
	float toScreenX(float x) const { return playRect.x + x * playRect.width; }
	float toScreenY(float y) const { return playRect.y + y * playRect.height; }

	// The same conversion for a size (a width, a height, a radius) rather than
	// a position - no offset added, since a size has to scale with the play
	// rect without being pushed sideways by wherever its corner happens to
	// sit. Needed now that the play rect can start somewhere other than (0, 0)
	// - before, when it was always the full window, toScreenX/Y happened to be
	// safe for sizes too, which is why every call site below was not already
	// split this way.
	float toScreenSizeX(float w) const { return w * playRect.width; }
	float toScreenSizeY(float h) const { return h * playRect.height; }

	void drawBackdrop();
	void drawFallers();

	// The burst a piece leaves behind when it is shot - InvaderGame keeps a
	// short list of recent hit positions (see Explosion in InvaderGame.h)
	// purely so this can be drawn after the piece itself is already gone from
	// getFallers().
	void drawExplosions();

	void drawShots();
	void drawPlayer();

	// The shield ring around the figure while game.isShieldActive(), and the
	// brief breaking flash where it stood the frame it absorbs a hit - see
	// InvaderGame::getShieldBreakFlash(). Drawn as part of the player group
	// but split out of drawPlayer() since it is its own visual idea, not part
	// of the figure itself.
	void drawShield();

	// The falling pickups - see InvaderGame::PowerUp. Each kind has its own icon
	// and palette colour so a visitor can decide which one is worth crossing the
	// table for before it reaches the figure.
	void drawPowerUps();

	void drawDangerLine();
	void drawScore();
	void drawWaveScore();
	void drawGameOver();

	// Draws the ten best completed visitor rounds in two columns. The current
	// round is highlighted when it entered the table; demo rounds have no rank.
	void drawHighScores(float alpha);

	// Draws text at a chosen height in pixels, centred if asked. Wraps myFont so
	// that a font which failed to load still leaves the score and the loss
	// readable - on this screen those two are the whole point, and a screen that
	// silently stops showing them looks like a crash rather than a missing file.
	void drawText(const std::string & text, float x, float y, float pixelHeight, bool centred) const;

	// How wide that text would be drawn. Only needed to hang something off the
	// right-hand edge, which cannot be done by guessing a width - the wave number
	// gains a digit as the round goes on.
	float textWidth(const std::string & text, float pixelHeight) const;

	// Builds the falling pieces and pickup icons. The player itself is the
	// generated Fusion-rocket PNG loaded in setup(), not a second code-art copy.
	void buildSprites();

	InvaderGame game;

	// Transparent pixel-art reinterpretation of the supplied Fusion rocket.
	// Kept as an external asset because the porthole, swept fins and engine are
	// what make the reference recognizable at a glance.
	ofImage playerSprite;

	// The falling pieces: pills, capsules and tablets - see buildSprites().
	// pillSprite and tabletSprite are each one shared silhouette, drawn in
	// whichever of fallerColors a piece's variant lands on (see
	// drawFallers()); the capsule is two halves, each its own colour from
	// that same set, stacked to read as one two-tone piece.
	PixelSprite pillSprite;
	PixelSprite capsuleTopSprite;
	PixelSprite capsuleBottomSprite;
	PixelSprite tabletSprite;

	// Five small silhouettes make pickup kinds identifiable before they are
	// caught: shield, colour swatches, three projectiles, lightning, and double
	// speed chevrons. Tint supplies a second redundant cue in drawPowerUps().
	PixelSprite shieldPowerUpSprite;
	PixelSprite recolorPowerUpSprite;
	PixelSprite shotPatternPowerUpSprite;
	PixelSprite fireRatePowerUpSprite;
	PixelSprite shipSpeedPowerUpSprite;

	// The table's play rect, worked out once at the top of draw() and read by
	// toScreenX/toScreenY and every draw* method afterwards - see
	// VisualBase::getPlayRect().
	ofRectangle playRect;

	// The border's reaction to firing and to hitFlash (a hit scored or the
	// round lost - InvaderGame does not distinguish the two, see
	// processDocumentation.md): both flashes are set to exactly 1.0 on the
	// frame they trigger and only ever decay afterwards, so a value still at
	// its peak is that triggering frame - no separate rising-edge state needs
	// to be kept.
	static constexpr float flashPeak = 0.999f;

	Control control = Control::Automatic;

	// Whether the figure is currently playing itself. Not the same as "nothing
	// over the table this frame": the switch either way is deliberately slow,
	// see the two timers below.
	bool demoActive = true;

	float noHandSeconds = 0.0f; // how long there has been nothing to steer by
	float handSeconds = 0.0f; // how long there has been something, allowing for gaps

	// Keeps the target continuous when Pi A replaces a contour id. A continuing
	// hand remains an absolute one-to-one control, while a real handover is
	// rebased from the ship's current target instead of jumping to the new
	// track's unrelated absolute x position.
	InvaderHandFollower handFollower;

	// Whether Pi A typed any object at all a hand this frame, whether or not
	// it ended up steering - shown in the status line, since this screen's
	// whole failure mode is "the sensor is reporting things and none of them
	// are being steered by", which a count of objects alone cannot diagnose.
	// The other two counts that used to live here (seen/usable) now come
	// straight off tracker.getSeenCount()/getUsableCount().
	bool sawTypedHand = false;

	// How long the table has to be empty before the figure starts playing itself.
	// Long enough that somebody thinking about their next move is not taken over
	// mid-game, short enough that the piece is not left standing dead in front of
	// a room.
	float demoAfterSeconds = 15.0f;

	// How long something has to be over the table before it takes the game back.
	// Short: being slow here reads as the table ignoring somebody who is already
	// reaching for it. It is not smoothing - it guards the restart below against
	// a single frame of tracking noise wiping a round in progress.
	float takeoverSeconds = 0.35f;

	// Per-track smoothing, stillness, hold/forget and driver-handover timing
	// are all tuned on tracker itself now (ObjectTracker::minCutoff/beta/
	// moveEpsilon/inertSeconds/holdSeconds/trackForgetSeconds/handoverSeconds)
	// - see the constructor for the values this screen sets them to and why.
	// tracker.holdSeconds in particular is still what takeoverSeconds above
	// depends on staying short for: the detection drops a blob for a frame or
	// two all the time - a hand passing over the edge of the region of
	// interest, a contour that briefly falls under the area threshold - and a
	// takeover timer that reset on every one of those gaps would never get
	// anywhere near a third of a second.
	//
	// tracker.inertSeconds is the same five-second stillness rule as before:
	// a blob that has not moved that long is treated as furniture regardless
	// of Pi A's own classification, which is what lets a misclassified hand
	// still play and a cup still not - see ObjectTracker::isUsable().

	// The round is restarted whenever control changes hands. Without it, a
	// visitor would inherit whatever wave the demo had climbed to - after a few
	// minutes unplayable - and the demo would inherit a half-lost field. Both
	// directions read as "a new game starts when someone arrives, and again when
	// they leave", which is what an installation wants.
	//
	// It costs a demo round its score when somebody walks up, which is the right
	// way round: the score on the table should be the score of whoever is
	// standing at it.
	bool restartOnControlChange = true;

	// How far in from the edges of the region of interest counts as the edge of
	// the table for steering. Hands rarely reach the last tenth of the sensor's
	// view, and without this the figure could not be brought all the way to
	// either side. Adjustable with ',' and '.'.
	float handEdgeMargin = 0.10f;

	// Fallers are tinted from their own resting colour towards the glow colour
	// as they come down, so how close the round is to being lost is legible
	// from the colour of the field alone whatever colour a given piece is.
	// This is the height at which the tint starts.
	float dangerStartY = 0.45f;

	// Palette::colorPillGreen and Palette::colorPowerUp (both declared in
	// Palette.h, alongside every other named colour in visualApp) are this
	// screen's own additions. Power-ups now reuse distinct colours from that
	// shared palette as a redundant cue alongside their silhouettes.

	// The firework-style burst a piece leaves when shot: a bright flash at
	// the moment of impact, then this many small square sparks that fly
	// outward on their own fixed angle and fall under explosionSparkGravity
	// - the look of a small rocket firework rather than a generic radiating
	// burst. Both are drawn entirely from InvaderGame::Explosion::age each
	// frame - see drawExplosions() for how a spark's angle and speed stay
	// stable across its short life without anything about its path being
	// stored anywhere.
	int explosionSparkCount = 10;
	float explosionSparkGravity = 1.0f; // play field units per second^2, downward

	// How large a power-up pickup's icon is drawn, as a share of the screen
	// height. Purely cosmetic - InvaderGame::getPowerUpHalfSize() is the
	// separately-tuned collision size in play field units.
	float powerUpDisplayHeight = 0.05f;

	// Text sizes, as a share of the screen height. The score sits at the far
	// short edge - the end of the table nobody is standing at - so it does not
	// land under the visitor's own arms.
	float scoreTextHeight = 0.045f;
	float gameOverTextHeight = 0.10f;

	// A timed wave ends whenever InvaderGame advances its wave number. The score
	// captured on that exact frame is shown briefly in the centre; this replaces
	// the old record popup that could restart after every scoring hit.
	int lastCompletedWave = 0;
	int lastWaveScore = 0;
	float waveScoreSeconds = 999.0f;
	static constexpr float waveScoreHoldSeconds = 1.2f;
	static constexpr float waveScoreFadeSeconds = 0.6f;

	// The ranking lives only for this visualApp process. setup() clears it once
	// when the service starts; onEnter() deliberately does not, so scores survive
	// the normal mode rotation until visualApp.service is started again.
	InvaderHighScores highScores;
	int lastCompletedScore = 0;
	int lastCompletedRank = -1;

	// The four-second GameOver state first gives the completed round's own score
	// a quiet moment, then cross-fades to the top ten and fades everything before
	// InvaderGame starts the next round.
	static constexpr float gameOverScoreSeconds = 1.2f;
	static constexpr float gameOverTransitionSeconds = 0.35f;
	static constexpr float gameOverFadeOutSeconds = 0.6f;
};
