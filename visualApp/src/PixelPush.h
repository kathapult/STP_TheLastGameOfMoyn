#pragma once

#include "DetectedObject.h"
#include "ObjectTracker.h"
#include "OneEuroFilter.h"
#include "TransformBase.h"
#include "ofMain.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

// Shoves the contents of the grid around instead of deciding what lives and
// dies: every cell's value is read as a quantity of material ("mass") and moved
// along a displacement field built from the hands. Nothing is added or removed
// in the process, so material pushed out of one place has to end up in another -
// that conservation is what makes the result read as a heap of stuff being
// shoved rather than as pixels being switched on and off.
//
// It is a TransformBase like GameOfLife is (constitution Principle III), so the
// two chain: GameOfLife decides birth/death, PixelPush moves what is there. It
// deliberately does not know about GameOfLife, and GameOfLife does not know
// about it - the only thing they share is the Grid they both take and return.
//
// Two things in here exist purely to keep the cost down on the Pi, and both are
// load-bearing rather than micro-optimizations - see the comments on
// massEpsilon and on the bounds fields.
class PixelPush : public TransformBase {
public:
	PixelPush();

	void setup() override;

	// Rebuilds the displacement field from this frame's objects. deltaTime is
	// passed in because DetectedObject::vx/vy are normalized units per *second*,
	// so a push has to be converted into a per-frame distance - otherwise the
	// same hand movement would shove material further on a Pi running at 60 fps
	// than on one running at 30.
	void update(const std::vector<DetectedObject> & detectedObjects, float deltaTime);

	// Moves every cell's mass along the displacement field.
	Grid transform(const Grid & grid) override;

	// How hard each cell is currently being shoved, 0.0-1.0. Only used for
	// colouring, so a viewer can see where the piece thinks the hand is - the
	// same job GameOfLife's influence field does there. Returned by reference:
	// this is 60 kB and the caller only reads it.
	const Grid & getPushField() const { return pushField; }

	// Where the colour ramp should treat a cell as a full heap, exposed so the
	// visual normalizes against the same number rather than keeping its own.
	float getHeapDisplayMass() const { return heapDisplayMass; }

	// Below this a cell counts as empty - see the field's comment. Exposed
	// because anything else that writes into the same grid has to apply it too,
	// or it reintroduces exactly the values this class removes.
	float getMassEpsilon() const { return massEpsilon; }

	// The height gate as it currently stands, in DetectedObject::z. Worth
	// showing while calibrating: it moves on its own, so a number that cannot be
	// read is a number that cannot be trusted.
	float getPushHeightThreshold() const;

	// Read-only access to the per-id position memory, for the visual's "you
	// are here" cursor - drawing straight from this rather than from a raw
	// per-frame DetectedObject list is what lets the cursor keep drifting
	// through a brief tracking gap (ObjectTracker::holdSeconds) instead of
	// disappearing on the exact frame a blob is not reported.
	const ObjectTracker & getPositionTracker() const { return positionTracker; }

	// Largest displacement anywhere in the field this frame, in grid cells -
	// how hard whatever is currently pushing is pushing. Exposed so a caller
	// can notice a push happened without iterating the whole field itself.
	float getMaxDisplacement() const { return maxDisplacement; }

	// Adjusting the two things that decide whether pushing feels generous or
	// fiddly, both of which can only really be judged with a hand over the real
	// table. Each returns the value it settled on, for printing.
	float adjustPushHeightFraction(float delta);
	float adjustReachScale(float delta);

	float getPushHeightFraction() const { return pushHeightFraction; }
	float getReachScale() const { return reachScale; }

private:
	// Widens the region that transform() rebuilds so it covers everything this
	// object touches.
	void growActiveBounds(int centreX, int centreY, int radius);

	// Per-id memory of where objects are (see ObjectTracker) - used here only
	// for its smoothed x/y, not for stillness or exclusive-driver selection,
	// which is why every object counts rather than only hands (a cup being
	// dragged should push too). Reading obj.x/obj.y straight off each frame's
	// DetectedObject, the way this class used to, meant the point a push
	// originated from jittered by exactly whatever tracking noise the sensor
	// had that frame, even though the displacement itself was already smoothed
	// by pushDecay - the origin and the effect were on two different footings.
	ObjectTracker positionTracker;

	// Per-id smoothing of the push *direction*, kept separately from
	// positionTracker because it filters a different pair of quantities
	// (obj.vx/vy, not obj.x/y) and wants its own response - direction needs to
	// follow a genuine change of heading quickly, where position mostly wants
	// jitter calmed. A raw per-frame direction (this class's previous design)
	// passed the sensor's own velocity noise straight into which way material
	// gets shoved, most visible on a slow diagonal push, where frame-to-frame
	// angle noise fanned the shove out rather than drawing one clean stroke.
	//
	// A plain std::unordered_map rather than ObjectTracker: only two scalars
	// need smoothing here, not a whole track's worth of position/stillness/
	// hold-forget bookkeeping ObjectTracker already does for positionTracker -
	// pulled in twice over would be paying for machinery only one of the two
	// copies is using.
	struct DirectionFilter {
		OneEuroFilter vx;
		OneEuroFilter vy;
	};
	std::unordered_map<int, DirectionFilter> directionFilters;

	// One-Euro parameters for directionFilters, re-applied every lookup the
	// same way positionTracker's are - see ObjectTracker::minCutoff/beta.
	// Beta well above positionTracker's own: a genuine change of heading
	// should come through almost at once (a push that lagged behind the hand
	// turning would draw a curve where the hand drew a corner), while
	// minCutoff still calms the jitter a barely-moving hand's velocity
	// reading has.
	float directionMinCutoff = 0.8f;
	float directionBeta = 2.5f;

	// Drops directionFilters entries for ids positionTracker itself has
	// forgotten (see ObjectTracker::trackForgetSeconds), so this map cannot
	// grow without bound over a long show either - piggy-backed on
	// positionTracker's own forgetting rather than keeping a second timer.
	void forgetStaleDirectionFilters();

	// Per-cell displacement in grid cells, applied by transform(). Kept as two
	// scalar grids rather than a grid of vectors so the whole class stays on the
	// Grid type both base-class methods already speak.
	Grid pushX;
	Grid pushY;
	Grid pushField; // magnitude of the above, normalized for display

	// The rectangle of cells that currently holds a non-zero displacement, and
	// therefore the only part of the grid transform() has to rebuild. Everything
	// outside it would be moved by exactly zero cells, i.e. copied to itself, so
	// touching it at all is wasted work - and it is nearly the whole grid, since
	// a hand reaches at most maxPushRadius cells. It grows as hands move and is
	// dropped entirely once the field has decayed away.
	//
	// While the field is empty these hold an inverted (empty) rectangle, which
	// is what boundsActive being false means.
	bool boundsActive = false;
	int boundsMinX = 0;
	int boundsMinY = 0;
	int boundsMaxX = 0;
	int boundsMaxY = 0;

	// Largest displacement currently anywhere in the field, in cells. The
	// scatter can throw mass this far outside the rectangle, so it is how far
	// past the rectangle transform() has to tidy up afterwards.
	float maxDisplacement = 0.0f;

	// Longest step update() will integrate over, in seconds. Without it a frame
	// that took unusually long - the first one after switching to this screen,
	// say - would displace material by a correspondingly huge distance and the
	// material would appear to teleport. Capping means a slow frame pushes less
	// far rather than further, which is the right way round.
	float maxDeltaTime = 1.0f / 20.0f;

	// Highest an object may be and still count as pushing, as a share of how
	// high hands actually get over this table. Above it a hand passes over the
	// piece without disturbing it.
	//
	// A share rather than a fixed height, because DetectedObject::z is
	// normalized against Pi A's depth window and therefore means a different
	// real height for every calibration: with the window at 500-1000 mm, z of
	// 0.25 is 12.5 cm above the table, but widen the window to 500-1500 mm and
	// the same number becomes 25 cm. A value that has to be re-found every time
	// the sensor is re-hung is a value that will be wrong during a show.
	//
	// Measuring it against observed hand heights instead keeps it at the same
	// real height whatever the window, and does it without Pi B learning
	// anything about millimetres - which it must not (Principle II).
	float pushHeightFraction = 0.75f;

	// The height hands are currently assumed to reach, in DetectedObject::z. It
	// rises the moment anything higher is seen and sinks back slowly, so one
	// person waving high does not leave the piece over-generous for the rest of
	// the evening, and a person reaching in is taken account of on the first
	// frame they appear.
	//
	// It sinks towards restingHeightCeiling rather than towards its own starting
	// value, and that distinction is the whole mechanism. Decaying towards a
	// middling default makes the default a *floor*: with a wide depth window,
	// where a hand half a metre up only reads z of 0.3, the ceiling could never
	// drop to where the hands actually were and the gate stayed far too
	// generous - measured, 638 mm of headroom against 374 mm everywhere else.
	float observedHeightCeiling = 0.5f;
	float restingHeightCeiling = 0.15f;
	float ceilingDecaySeconds = 20.0f;

	// The band the resulting threshold is held inside. Without a floor, a table
	// nobody has reached over yet would take almost nothing as a push; without a
	// ceiling, a single bad frame reading z near 1.0 would make everything in
	// the room a push.
	float minPushHeight = 0.12f;
	float maxPushHeight = 0.85f;

	// How far above the threshold strength fades from full to nothing, in the
	// same DetectedObject::z units as the threshold itself. This replaced a
	// hard gate (obj.z > threshold skipped the object outright): a hand held
	// right at the boundary - exactly where a visitor lifting their hand away
	// spends the most time - used to have the push switch on and off every
	// frame as height-measurement noise crossed back and forth over a single
	// value. A short fade band means that same noise only nudges how hard the
	// push lands, the same way the hand cursor's engagement (VisualBase) is
	// already read as a spectrum rather than the on/off gate it also used to
	// be.
	float pushHeightFadeBand = 0.06f;

	// Speed at which a hand pushes at full strength, in normalized units per
	// second. 0.4 is roughly a hand crossing the whole table in two and a half
	// seconds. Below it the push scales down linearly, so resting a hand on the
	// table disturbs nothing and only actual movement moves material.
	float speedAtFullPush = 0.4f;

	// Speed at which a push reaches its full share of speedAtFullPush's ramp,
	// and simultaneously the top of the fade band below - see
	// minPushSpeedFadeFloor. Below the floor the velocity is mostly tracker
	// noise, and its direction is meaningless - without a floor a still hand
	// would jitter the material underneath it.
	float minPushSpeed = 0.03f;

	// Bottom of the fade band: strength ramps linearly from 0 here up to
	// minPushSpeed's own share of the speedAtFullPush ramp, rather than a
	// hand crossing minPushSpeed switching the push on in a single frame.
	// This replaced a hard cutoff at minPushSpeed - with tracking noise
	// riding on top of a hand's actual speed, a hand slowing to a stop or
	// hovering right at that speed used to flicker the push on and off frame
	// to frame; a short ramp instead of a step means noise that size only
	// nudges the strength, not whether there is any push at all.
	float minPushSpeedFadeFloor = 0.012f;

	// Furthest a cell can be displaced in one second, in grid cells. This is the
	// piece's sense of weight: low values feel like wet sand, high values like
	// the material is being blown away.
	float maxPushDistance = 55.0f;

	// Reach of a push, in grid cells, interpolated from DetectedObject::area the
	// same way GameOfLife does it - a flat hand shoves a wide swathe, a
	// fingertip draws a narrow line. Both ends scaled by reachScale below.
	int minPushRadius = 4;
	int maxPushRadius = 20;

	// Share of the table at which an object already reaches maxPushRadius. Small
	// on purpose: a hand covers only a few percent of the table. Lowered from
	// the value GameOfLife uses, so an ordinary hand arrives at close to its
	// full reach rather than sitting in the middle of the range - the whole
	// point of a wider reach is that an ordinary hand gets it.
	float areaAtMaxPush = 0.035f;

	// Multiplies both radii, so the reach can be tried out at the table without
	// a rebuild. 1.0 is the pair above.
	float reachScale = 1.0f;

	// How much of the previous frame's displacement survives into this one.
	// Material therefore keeps drifting briefly after the hand stops, instead of
	// halting dead the instant the hand does - the follow-through that makes the
	// stuff feel like it has any weight at all.
	float pushDecay = 0.82f;

	// Displacement below this is snapped to exactly zero. Decay alone only ever
	// approaches zero, so without this the field would stay marginally alive
	// forever and the bounds above would never be released. A hundredth of a
	// cell is far below anything visible.
	float pushEpsilon = 0.01f;

	// Mass below this is snapped to exactly zero, and this one is not cosmetic.
	// Splitting a cell's mass across four destinations makes ever smaller
	// fractions, none of which ever reach zero on their own, so the grid
	// gradually fills up with values far too small to see. Two things go wrong
	// then: the "skip empty cells" test in transform() stops skipping anything
	// (measured: about 11,000 of 15,000 cells still being processed instead of
	// ~3,000), and the values eventually fall into the denormal range, where
	// arithmetic on the Pi's ARM cores is dramatically slower than on normal
	// floats. 1/512 is below one step of the 8-bit colour ramp, so nothing that
	// could have been seen is lost.
	float massEpsilon = 1.0f / 512.0f;

	// How much material counts as a full heap for the purpose of drawing it.
	// Purely a reference for the colour ramp - nothing is clamped to it - so the
	// visual can tell an ordinary cell from a heaped one without keeping its own
	// copy of the number.
	float heapDisplayMass = 3.0f;

	// How much taller than a neighbour a cell may stand before material runs
	// downhill into it: the angle of repose, in cells' worth of material.
	//
	// One is deliberately the value: the starting scatter is cells of exactly
	// 1.0 next to empty ones, and a difference of exactly one is left alone, so
	// an untouched table does not slowly melt. Only what has actually been
	// heaped up flows.
	float slumpSlope = 1.0f;

	// Share of the surplus difference that moves in one pass. Well below the
	// quarter that four neighbours could each take, so levelling approaches the
	// slope instead of overshooting it and sloshing back and forth.
	float slumpRate = 0.15f;

	// Hard ceiling, and the one place mass may still be thrown away. Set high
	// enough that ordinary pushing never reaches it, because everything this
	// class is for depends on material not disappearing.
	//
	// It is needed at all because levelling moves material sideways rather than
	// removing it, so it is not a sink: if something upstream keeps *adding*
	// mass faster than a heap can spread - Game of Life with a rule that lets
	// dense clumps survive does exactly that - the grid would climb without
	// limit. Nothing above heapDisplayMass is distinguishable on screen anyway,
	// so what this discards in that case was already invisible.
	float absoluteCellCeiling = 64.0f;

	// How many times the levelling is applied per frame. One pass only ever
	// moves material one cell outwards, and now that a whole reach's worth
	// arrives on a couple of cells at a time that is far too slow to keep up -
	// what built up was a spike rather than a mound. Repeating it lets material
	// travel several cells within the same frame.
	int slumpIterations = 4;

	// Objects Pi A classified as standing on the table by themselves push this
	// much compared to a hand. A cup dragged across the table should disturb the
	// material, just not like a person does. Unknown stays at full strength for
	// the same reason as in GameOfLife: a misclassification should over-react
	// visibly rather than go silently dead.
	float staticObjectPush = 0.5f;
};
