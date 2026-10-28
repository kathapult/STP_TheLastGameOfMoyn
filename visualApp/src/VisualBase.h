#pragma once

#include "DetectedObject.h"
#include "Palette.h"
#include "ProjectionCalibration.h"
#include "ofMain.h"
#include <string>
#include <vector>

class VisualBase {
public:
	// Constructor
	VisualBase();

	// virtual destructor
	virtual ~VisualBase() = default;

	// virtual functions
	virtual void setup();
	virtual void update(const std::vector<DetectedObject> & detectedObjects) = 0;
	virtual void draw() = 0;

	// Called once after setup() for every visual. GPU resources are part of
	// startup health: a process with an unallocated render target must fail
	// visibly so systemd can roll back the release instead of supervising a
	// permanently black window. Visuals without dedicated GPU allocations are
	// ready by default.
	virtual bool isReady(std::string & failureReason) const {
		failureReason.clear();
		return true;
	}

	// Called by ofApp on the frame this visual becomes the current one. Empty by
	// default, since most screens have nothing to reset - it is declared here so
	// ofApp can announce the switch to whichever visual is now showing without
	// having to know which concrete class that is (constitution Principle III).
	virtual void onEnter() {}

	// A visual with an internal, finite sequence can align the production mode
	// deadline with that sequence. Zero leaves ofApp's normal random interval in
	// charge; a positive value is read after onEnter() has planned the sequence.
	virtual float getScheduledModeDurationSeconds() const { return 0.0f; }

	// Updated by ofApp whenever projection calibration or the native output
	// size changes. Every visual consequently reads the same logical canvas,
	// physical surface aspect and rounded outline without depending on the
	// projector's mounting.
	virtual void setProjectionGeometry(int width, int height,
		float surfaceWidthCm, float surfaceHeightCm,
		float cornerRadiusFraction, BaseEdge baseEdge);
	int getCanvasWidth() const { return canvasWidth; }
	int getCanvasHeight() const { return canvasHeight; }
	BaseEdge getBaseEdge() const { return calibratedBaseEdge; }

	// The rectangle, in logical-canvas pixels, that lands on the calibrated
	// projectable surface. It is centered and letterboxed to the measured
	// physical aspect; the final ProjectionMapper handles output rotation and
	// placement after every visual has drawn.
	ofRectangle getPlayRect() const;

	// Offered every key press before ofApp uses it to advance to the next
	// visual. Returning true keeps the key, which is how a screen claims one for
	// its own settings; returning false - the default - lets it cycle as before.
	//
	// This exists because the installation is calibrated on the Pi and edited on
	// the Mac, with a git push and a rebuild between the two. Anything that can
	// only be found out by watching it on the table has to be adjustable while
	// it is running there, or it costs a full deploy cycle per guess.
	virtual bool keyPressed(int key) { return false; }

	// Name of this screen, for the debug HUD. Two of the visuals differ only in
	// what is in their transform chain, so an index alone no longer says which
	// one is showing. Empty for screens that never set it.
	const std::string & getTitle() const { return title; }

	// One line of whatever this screen's settings are, appended to the debug
	// HUD. Empty by default. It exists because several of those settings can
	// only be judged with a hand over the real table and some of them move on
	// their own - a value that cannot be read while calibrating cannot be
	// calibrated.
	virtual std::string getStatusLine() const { return ""; }

private:
protected:
	// frame
	ofTrueTypeFont myFont; // font
	std::string title;

	// Every named colour used anywhere in visualApp lives in Palette.h now,
	// not here - see that file for the one place to change any of them, and
	// for what each one means (referenced below as Palette::colorX).
	// Palette::colorBorderBase (= Palette::colorBorderPurple) is the border's
	// resting colour; Palette::colorBorderTurquoise is the reaction hue - a
	// hit landed, a push registered, a hand pulled the flight faster - so a
	// visitor comes to read "the frame flashed turquoise" as "the piece just
	// noticed me"; Palette::colorBorderLilac is reserved for the frame around
	// the table changing what it means: only the mode-entrance sweep uses
	// it, so none of the three ever means the same thing as another.

	ofRectangle getFrameRect(float widthPercent = 0.9f, float heightPercent = 0.8f, float topOffset = 40.0f) const;
	void drawFrameBorder(const ofRectangle & frame, ofColor color = Palette::colorAccent, float lineWidth = 3.0f) const;

	// Advances the border's own animation clock: the idle blink/sweep
	// schedule, any reaction pulses still fading, and a mode-entrance sweep if
	// one is running. Call once a frame, from update().
	void updateBorderAnimation(float deltaTime);

	// Draws the filigree line around getPlayRect(), rounded the same way
	// maskPlayAreaCorners() cuts the corners off. Call last from draw(), so it
	// sits on top of everything else on the table.
	void drawPlayAreaBorder() const;

	// Paints solid black over the four corners outside the rounded play-area
	// shape drawn by drawPlayAreaBorder(), so content that fills the whole
	// rectangle does not poke out past the round frame at the corners - the
	// real poker table's own top is rounded the same way. Call after a visual
	// has drawn its content, before drawPlayAreaBorder() (so the border's own
	// line is drawn on top of the masked corners rather than under them).
	void maskPlayAreaCorners() const;

	// The radius maskPlayAreaCorners()/drawPlayAreaBorder() round getPlayRect()'s
	// corners to, in pixels. Exposed so a visual can keep its own UI elements
	// clear of the masked corners - text put too close to a corner would have
	// its own edge painted over rather than being cut cleanly.
	float getPlayAreaCornerRadius() const { return playAreaCornerRadius(getPlayRect()); }

	// Flashes the whole border once, turquoise by default, for something a
	// visitor just caused - a hit landed, a push registered, the flight
	// lurching forward. Layers on top of whatever the idle animation is doing
	// rather than interrupting it, so two things happening close together read
	// as one brighter flash instead of one cutting the other off.
	void pulseBorder(ofColor color = Palette::colorBorderTurquoise, float strength = 1.0f);

	// Runs one bright lap of the border in lilac - call from a visual's own
	// onEnter(), so a mode change reads as something happening to the frame
	// itself, not just to whatever is inside it. laps and duration are
	// exposed so each of the four screens can give its own entrance a
	// different pace rather than all four looking the same.
	void sweepBorderOnEnter(ofColor color = Palette::colorBorderLilac, int laps = 1, float duration = 1.8f);

	// The "you are here" cursor: a soft highlight brightening the table at
	// wherever the piece currently has its attention, so a visitor who isn't
	// getting a reaction can tell "wrong position" from "not seen at all"
	// from "seen but not acted on" instead of only ever seeing nothing
	// happen. Call clearHandCursors() once at the top of update(),
	// addHandCursor() for every point worth showing that frame, and
	// drawHandCursors() from draw() - before drawPlayAreaBorder(), so the
	// border still reads as the outermost thing.
	//
	// Drawn additively as a radial gradient in the cursor's own colour
	// (Lachs/salmon by default), full brightness at the centre fading to
	// nothing at its own radius - it brightens the pixels already there
	// rather than laying an opaque shape over them. Replaces an earlier ring
	// outline, which against a projector read as a thin, half-transparent
	// line rather than a highlight. The highlight closes in as the hand nears
	// the table (height, DetectedObject::z convention: 0 at the surface) and
	// opens out as it lifts away, the way a target reticle tightens on
	// approach - a visitor watches it happen to their own hand. engagement
	// (0-1) marks "acted on" via the centre's own brightness rather than a
	// separate dot. What counts as engaged is each visual's own business -
	// PixelPushVisual reads it off the push-height gate, UniverseVisual off
	// whether this is the hand currently flying it.
	void clearHandCursors();
	void addHandCursor(glm::vec2 position, float height, float engagement, ofColor color = Palette::colorSalmon);
	void drawHandCursors() const;

private:
	int canvasWidth = 1024;
	int canvasHeight = 768;
	float calibratedSurfaceWidthCm = 188.0f;
	float calibratedSurfaceHeightCm = 79.0f;
	float calibratedCornerRadiusFraction = TableShape::cornerRadiusFraction;
	BaseEdge calibratedBaseEdge = BaseEdge::Bottom;

	// How much width getPlayRect() trims off its letterboxed rectangle, so the
	// border's own stroke clears the physical table's rounded short sides
	// instead of poking past them. Kept as one factor so the border, mask and
	// animated perimeter (all built from getPlayRect()) trim together.
	float playRectWidthTrim = 0.97f;

	// Same trim, along the other axis - the table's own long, straight sides
	// sat a hair outside the border's stroke there too once the short sides
	// above were fixed.
	float playRectHeightTrim = 0.97f;

	// Shared/TableShape.h owns the common rounded-rectangle formula. Pi B feeds
	// it the locally calibrated display fraction here so every projected border,
	// corner mask and dependent visual keeps one identical table outline.
	float playAreaCornerRadius(const ofRectangle & rect) const;

	// Fills the corner mask fan for one corner: a triangle fan from the
	// rectangle's own sharp corner out to the rounded arc, which is exactly
	// the area outside the rounded shape and inside the sharp rectangle.
	// Shared by all four corners in maskPlayAreaCorners() - they differ only in
	// which corner and which quarter-turn of the arc.
	void drawCornerCutout(glm::vec2 sharpCorner, glm::vec2 arcCenter, float radius, float startAngle, float endAngle) const;

	// A point on getPlayRect()'s rounded perimeter, t going 0-1 clockwise from
	// just past the top-left corner's arc. What both the idle sweep and the
	// entrance sweep walk a "comet" position along.
	glm::vec2 pointOnPerimeter(const ofRectangle & rect, float radius, float t) const;

	// How bright a point at perimeter position t is from a comet currently at
	// position, given how wide the comet's falloff should read - the shared
	// shape both the idle sweep and the entrance sweep are built from.
	float cometBrightness(float t, float position, float width) const;

	// Idle animation: waits a random interval, then plays either a blink (the
	// whole line breathes brighter and back) or a sweep (a bright point runs
	// one lap and fades), so the filigree line is never simply switched on and
	// left - it is always faintly there, and only occasionally does either of
	// the two things asked for it. Timed the same way UniverseVisual's
	// glitches are: two random bounds rather than a constant rate, so it reads
	// as something happening rather than a metronome.
	enum class BorderIdleKind { Blink, Sweep };
	bool borderIdleActive = false;
	BorderIdleKind borderIdleKind = BorderIdleKind::Blink;
	float borderIdleElapsed = 0.0f;
	float borderIdleTimer = 0.0f;
	float borderIdleNextIn = 4.0f;
	float borderIdleMinGap = 5.0f;
	float borderIdleMaxGap = 14.0f;
	float borderBlinkDuration = 0.9f;
	float borderSweepDuration = 1.6f;

	// Reaction flashes still fading - one entry per pulseBorder() call, each
	// ageing out on its own so two close together overlap rather than one
	// cutting the other off.
	struct BorderPulse {
		ofColor color;
		float age = 0.0f;
		float strength = 1.0f;
	};
	std::vector<BorderPulse> borderPulses;
	float borderPulseLifetime = 0.45f;

	// The mode-entrance sweep - kept separate from the idle one so entering a
	// screen always gets its own lap even if the idle scheduler had just used
	// one up.
	bool borderEntranceActive = false;
	float borderEntranceElapsed = 0.0f;
	float borderEntranceDuration = 1.8f;
	int borderEntranceLaps = 1;
	ofColor borderEntranceColor = Palette::colorBorderLilac;

	float borderBaseBrightness = 0.45f; // the filigree line's resting level, always faintly there

	// How thick the border is drawn: a multiple of one grid cell's width once
	// the play rect is stretched over TransformBase::GRID_WIDTH - one cell, so
	// the border reads as one row of the same pixel grid every screen already
	// is, rather than as a separate, finer overlay on top of it. Was briefly
	// two cells; asked back down to one as too heavy against the table.
	float borderLineWidthCells = 1.0f;

	int borderPointCount = 240;

	// One entry per addHandCursor() call this frame, drawn by
	// drawHandCursors() and cleared again by the next clearHandCursors().
	struct HandCursor {
		glm::vec2 position; // normalized 0-1 across the table
		float height = 0.0f; // DetectedObject::z convention: 0 at the table, 1 held high above it
		float engagement = 0.0f;
		ofColor color;
	};
	std::vector<HandCursor> handCursors;

	// The highlight's radius at the table surface and held all the way up, in
	// grid cells - sized off the same pixel grid the border already is, for
	// the same reason: it reads as belonging to the table rather than as a UI
	// element laid over it. The spread between the two is the whole point:
	// far enough apart that closing in is unmistakable rather than a subtle
	// shift.
	float cursorRadiusMinCells = 1.4f;
	float cursorRadiusMaxCells = 9.0f;
};
