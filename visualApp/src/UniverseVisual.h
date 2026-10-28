#pragma once

#include "CruiseNavigator.h"
#include "DetectedObject.h"
#include "VisualBase.h"
#include "ofMain.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// A flight through nowhere in particular. No solar system, no orbits: an
// undefined space with invented things in it, drifting past. The flight never
// stops and the hands only decide which way it points.
//
// Everything here is drawn out of points and dotted lines rather than surfaces,
// which is the aesthetic and the performance budget agreeing for once - a
// stippled sphere is a few hundred vertices where a lit one is a mesh and a
// shader, and on a Pi that difference is the whole frame.
//
// The scene is rendered into a deliberately small buffer and blown up with no
// filtering, so it arrives as visible pixels. That is what ties this screen to
// the pushing ones: the same chunky grid, moving instead of being shoved.
//
// The field keeps time. A beat clock runs underneath everything and things
// brighten on it - a quarter of the field per beat, and the concentric rings one
// shell per beat so they light outwards - which is the piece pretending to hear
// music that is playing in the room anyway. There is no audio input on Pi B and
// there is not meant to be: a fixed tempo agrees with a DJ often enough to read
// as reactive, and never drops out.
class UniverseVisual : public VisualBase {
public:
	UniverseVisual();

	void setProjectionGeometry(int width, int height,
		float surfaceWidthCm, float surfaceHeightCm,
		float cornerRadiusFraction, BaseEdge baseEdge) override;
	void setup() override;
	void update(const std::vector<DetectedObject> & detectedObjects) override;
	void draw() override;
	void onEnter() override;
	bool isReady(std::string & failureReason) const override;

	bool keyPressed(int key) override;
	std::string getStatusLine() const override;

private:
	// What a drifting thing is. One struct for all of them, with the kind
	// deciding how it is drawn, because they differ in appearance rather than in
	// behaviour - all of them just sit there and get passed.
	enum class ElementKind {
		StippledOrb, // a sphere drawn as a scatter of points on its surface
		DottedRing, // a circle of points, tipped over
		Halo, // concentric dotted rings, lighting one shell per beat
		Mushroom, // a cap with gills under it on a stalk, drawn the same way
		MoynLogo, // the festival's mark, as a body in its own right
		Mote, // a single bright point, the small stuff that fills the gaps
		PokerChip // a flat disc with a brightened rim, scattered through the field rarely
	};

	// Which silhouette a mushroom has. Four rather than one because the first
	// version drew every mushroom as the same flattened dome, which is the shape
	// of a jellyfish; what makes the reference photographs read as fungus is that
	// no two of them have the same outline.
	enum class MushroomShape {
		Bell, // tall and closed, on a thread of a stalk
		Flat, // a wide plate with a waving rim, gills underneath doing the work
		Cup, // opens upwards, so what is seen is the inside
		Cluster // five small ones out of a single point
	};

	struct Element {
		ElementKind kind = ElementKind::Mote;
		MushroomShape shape = MushroomShape::Bell; // only read for mushrooms
		glm::vec3 position;
		float radius = 1.0f;
		float tiltX = 0.0f;
		float tiltY = 0.0f;
		float spin = 0.0f; // radians per second, so nothing is quite still
		ofColor color;
		float twinklePhase = 0.0f;

		// The slow wave this body melts on: its own phase and its own rate, so
		// that a field of them comes apart as many things rather than as one.
		float wobblePhase = 0.0f;
		float wobbleRate = 1.0f;

		// Whether this one is wearing a hoop. Two in the whole field - see
		// hoopCount.
		bool hasHoop = false;

		// Which beat of the bar this one answers to. Spread across the bar so
		// that a quarter of the field lights at a time - everything pulsing
		// together would read as the brightness being turned up and down.
		int beatSlot = 0;

		// Empty for most of them. The few that carry a name are the festival's
		// floors - see namedBodies.
		std::string name;

		// Rolled once when this element is created. A selected element is placed
		// in the forward viewing corridor and bursts on near approach or after this
		// fallback countdown; it is never rerolled per frame, so about 2% of spawns
		// actually explode.
		bool fireworkScheduled = false;
		float fireworkSeconds = 0.0f;
	};

	// A name and whichever element is currently carrying it, by index into
	// `elements`, or -1 when nothing is. Held this way round because the field is
	// recycled constantly: a name has to be handed back when the thing wearing it
	// drifts out of the world, or the five names would be used up within a minute
	// and never appear again.
	struct NamedBody {
		std::string name;
		int elementIndex = -1;
	};

	// A firework-style burst left where one of the rare scheduled elements
	// explodes - see updateScheduledFireworks() and UniverseFireworkPolicy.
	// Purely cosmetic, the same "nothing about a spark's path is stored"
	// reasoning InvaderVisual's own explosion sparks use: every spark's
	// position is worked out fresh from age each frame in drawScene(), so
	// this struct only ever needs to remember where the burst started, what
	// colour, how big, and how long ago.
	struct Burst {
		glm::vec3 position;
		ofColor color;
		float radius = 1.0f; // the exploded body's own radius - scales how far and how fast sparks travel
		float age = 0.0f;
	};

	void buildMeshes();
	void buildElements();
	Element makeElement(bool ahead, int elementIndex);
	void replaceElement(std::size_t elementIndex, bool createBurst);
	void updateScheduledFireworks(float deltaTime);
	void recycleElements();

	// Hands a name to a newly made element if it is the sort of thing worth
	// naming and a name is free, and takes it back when one is recycled away.
	void claimName(Element & element, int elementIndex);
	void releaseName(int elementIndex);

	// How brightly something on this beat of the bar is lit right now: a sharp
	// arrival and a decay across the beat, zero for the rest of the bar.
	float pulseFor(int beatSlot) const;

	// A name, already worked out to a place in the small buffer. Where a label
	// goes has to be asked of the camera while the scene is being rendered - the
	// answer depends on the buffer being bound, since drawing into one flips the
	// world upside down and a point projected outside that pass comes back
	// mirrored. Where it is *drawn* is afterwards, at full resolution, so the
	// names stay readable rather than being 320 pixels wide along with everything
	// else.
	struct Label {
		std::string text;
		glm::vec2 point; // in small-buffer pixels
		ofColor color;
		float fade = 0.0f;
		int beatSlot = 0;
	};

	void drawElement(const Element & element, float time);
	void drawScene(float time);
	void collectLabels();
	void drawGlitch();
	void drawNames();

	// The sparks from every current Burst, all gathered into burstMesh and
	// drawn together - see moteMesh's own comment for why one mesh beats one
	// draw call per spark. Called from drawScene(), inside the same additive,
	// no-depth-test pass the motes already use.
	void drawBursts();

	std::unique_ptr<CruiseNavigator> navigator;
	ofCamera camera;
	glm::vec3 viewerPosition; // where the camera is this frame, for billboarding and label fades

	std::vector<Element> elements;
	std::vector<NamedBody> namedBodies;
	std::vector<Label> labels;
	std::vector<Burst> bursts;

	// A fresh seed per rebuilt field plus a serial per spawned element feed the
	// deterministic 2% firework policy. The resulting decision lives on Element,
	// so neither frame rate nor time spent near a body changes its probability.
	std::uint64_t fireworkSeed = 0;
	std::uint64_t nextElementSerial = 0;

	// Points on a unit sphere and a unit circle, generated once and reused by
	// every orb and every ring. The shapes differ by scale, tilt and colour, not
	// by which points they are made of, so there is no reason to hold more than
	// one of each. The mushroom and the logo are built the same way, in three and
	// one part respectively.
	ofMesh unitSpherePoints;
	ofMesh unitRingPoints;
	ofMesh mushroomCapBell;
	ofMesh mushroomCapFlat;
	ofMesh mushroomCapCup;
	ofMesh mushroomGillPoints;
	ofMesh mushroomStemPoints;
	ofMesh mushroomClusterCaps;
	ofMesh mushroomClusterStems;
	ofMesh logoPoints;

	// A flat disc of points filling the chip's face - drawn together with
	// unitRingPoints (already the right shape for the chip's own rim, just
	// brightened) rather than needing a mesh of its own for the edge.
	ofMesh unitChipFacePoints;

	// Every mote in the field, gathered into one mesh each frame. They are the
	// majority of what is out there, and drawing them one at a time would be a
	// hundred draw calls for a hundred single points.
	ofMesh moteMesh;

	// Every spark from every current Burst, gathered the same way moteMesh
	// gathers motes - see drawBursts().
	ofMesh burstMesh;

	// The whole scene is drawn in here and then blown up. Small on purpose - see
	// the class comment - and it is also why the Pi can afford this screen at
	// all: at this size the fragment count is a fraction of the display's.
	// Its dimensions follow the calibrated play-surface aspect. The longer FBO
	// edge stays at 320 pixels, retaining the performance budget without
	// stretching spheres or rings when the measured dimensions change live.
	ofFbo scene;
	int sceneWidth = 320;
	int sceneHeight = 134;

	// The table's play rect, worked out once at the top of draw() and read by
	// drawGlitch()/drawNames() afterwards, since the scene is drawn into it
	// rather than into the full window - see VisualBase::getPlayRect().
	// collectLabels() does not read it: it works in the small buffer's own
	// coordinate space, before the blow-up happens.
	ofRectangle playRect;

	float systemTime = 0.0f;

	// The beat everything lights on. Fixed tempo, four beats to the bar - see the
	// class comment for why it is not listening to anything.
	float beatsPerMinute = 100.0f;
	int beatsPerBar = 4;
	float beatTime = 0.0f; // seconds since the screen was entered, counted for the beat
	int beatIndex = 0; // which beat of the bar is sounding
	float beatFraction = 0.0f; // how far through that beat, 0-1

	// How sharply a pulse decays across its beat. Above 1 it arrives hard and
	// fades, which is what makes it a beat rather than a wave.
	float pulseDecay = 2.2f;

	// How far a pulse lifts a thing's brightness, and how hard it squashes it.
	// The squash is what a beat looks like on a body: pulled in at the top and
	// pushed out at the sides, then let go. A quarter is a lot - it is meant to
	// be seen from across a room and through a projector.
	float pulseBrightness = 0.65f;
	float pulseSquash = 0.25f;

	// A much smaller lift that every element gets on every beat, whatever its
	// slot. Without it three quarters of the field is inert at any moment and the
	// rhythm reads as four separate things blinking.
	float pulseCommon = 0.16f;

	// How many things are drifting around at once, and the cube they are kept
	// in around the traveller. Enough to feel crowded from the inside, since an
	// undefined space with three things in it is just an empty one.
	//
	// Only the larger kinds cost a draw call each - the motes, which are most of
	// them, go in one mesh together - so this is around 120 calls a frame.
	// That is the number to watch if the Pi struggles: it is draw calls rather
	// than vertices that will be the limit here.
	//
	// More bodies than the previous field, while motes remain batched into one
	// draw call. Two thirds of the opening fill and every replacement are placed
	// in the forward corridor; replacements start farther away than before, so
	// they enter the view earlier and several can be approached at once.
	int elementCount = 190;
	float fieldSize = 3000.0f;

	// A selected spawn is guaranteed time to enter the view before it bursts.
	// Randomized per element so several rare fireworks never line up on a beat.
	float minFireworkSeconds = 3.0f;
	float maxFireworkSeconds = 7.0f;
	float minFireworkRadius = 12.0f;

	// Close enough that the body and its burst are clearly visible in the 62°
	// camera, yet far enough that even maximum boost cannot step past it between
	// frames. The timer above remains the fallback after a sharp course change.
	float fireworkApproachDistance = 650.0f;

	// How long a burst's sparks are visible, how many fly out per burst, and
	// how fast they travel per unit of the exploded body's own radius - a
	// Moyn-logo-sized burst reads as a bigger event than a mote's the same
	// way InvaderVisual's own firework sparks scale with what they came
	// from.
	float burstLifetime = 0.7f;
	int burstSparkCount = 16;
	float burstSpeedScale = 2.6f;

	// At most one Moyn logo exists at a time. It is the one body in here that is
	// a specific thing rather than an invented one, and two of them in shot at
	// once would make it decoration.
	int logoCount = 0;

	// And at most two mushrooms are wearing a hoop. Same reasoning: it is a joke,
	// and a joke everything is in on is scenery.
	int hoopCount = 0;
	int maxHoops = 2;

	// How fast a hoop goes round, in radians per second. It rides up and down the
	// stalk on a slower cycle than it spins - see where it is drawn.
	float hoopSpin = 2.6f;

	// How far everything is currently coming apart, 0-1, worked out each frame in
	// update(). There is always a little (warpBase) so the field is never merely
	// still, and it rises with how hard the flight is being steered (warpSteer)
	// or sped up by both hands pulling apart (warpZoom) - so the trip is
	// something a visitor does rather than something that happens at them.
	float warpLevel = 0.0f;
	float warpBase = 0.22f;
	float warpSteer = 0.78f;
	float warpZoom = 0.55f;

	// A third, continuous contribution: the steering hand's own height
	// (CruiseNavigator::getControlHeight(), DetectedObject::z) adds to the
	// melt too, on top of how hard the flight is being turned or sped up.
	// Unlike those two this needs no gesture and no threshold - an ordinary
	// hand lifted while gesturing already sits above the table surface, so
	// this arrives as an ambient response to how a visitor is holding their
	// hand rather than as a deliberate control of its own. Not yet verified
	// at the table - see processDocumentation.md.
	float warpHeight = 0.4f;

	// The border's reaction to the hand-pull-apart gesture: how much boost
	// counts as "the gesture is on" for a rising-edge pulse, and whether it was
	// on last frame.
	float boostPulseThreshold = 0.6f;
	bool wasBoosting = false;

	// What that comes to on a body: how far its axes are stretched against each
	// other, and how far they are leaned into each other. The shear is the one
	// that reads as melting; the stretch on its own only reads as breathing.
	float warpStretch = 0.20f;
	float warpShear = 0.30f;

	// How far a mote wanders off its own position at full warp, in world units.
	// A point cannot be deformed, so this is what a single point does while the
	// rest of the picture melts.
	float moteDrift = 16.0f;

	// How much faster glitches arrive at full steering lock, or at full speed
	// boost. The glitches themselves are unchanged - this only turns their clock
	// up, so that throwing the flight around, or pulling it into a burst of
	// speed, both cost picture stability.
	float glitchSteerRate = 0.9f;
	float glitchZoomRate = 0.6f;

	// Colours things are picked from. Deliberately not the piece's violet
	// palette - this screen is meant to be the loud one.
	std::vector<ofColor> palette;

	// How far a body can be and still be named on screen. Names appear as
	// something is approached rather than switching on, so this is where the fade
	// starts from.
	float labelDistance = 1500.0f;

	// A glitch arrives every so often, lasts a moment and goes. Two numbers
	// rather than a constant rate so it is felt as an event rather than as a
	// texture.
	float glitchTimer = 0.0f;
	float glitchNextIn = 3.0f;
	float glitchRemaining = 0.0f;
	float glitchMinGap = 1.6f;
	float glitchMaxGap = 6.5f;
	float glitchMinLength = 0.06f;
	float glitchMaxLength = 0.30f;

	// How far the colour channels come apart and how far a band slides, both in
	// pixels of the small buffer - so they scale up with everything else and
	// stay chunky rather than becoming a fine fringe.
	float glitchChannelSplit = 3.0f;
	float glitchBandShift = 14.0f;
	int glitchBands = 7;
};
