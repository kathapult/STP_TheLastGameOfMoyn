#include "UniverseVisual.h"
#include "UniverseFireworkPolicy.h"
#include <algorithm>
#include <iostream>

UniverseVisual::UniverseVisual() {
	title = "UNIVERSE";
	navigator = std::make_unique<CruiseNavigator>();
}

void UniverseVisual::setProjectionGeometry(int width, int height,
	float surfaceWidthCm, float surfaceHeightCm,
	float cornerRadiusFraction, BaseEdge baseEdge) {
	VisualBase::setProjectionGeometry(width, height,
		surfaceWidthCm, surfaceHeightCm, cornerRadiusFraction, baseEdge);
	navigator->setSteeringSurfaceSize(surfaceWidthCm, surfaceHeightCm);

	constexpr int longestSceneEdge = 320;
	int requestedWidth = longestSceneEdge;
	int requestedHeight = longestSceneEdge;
	if (surfaceWidthCm >= surfaceHeightCm) {
		requestedHeight = std::max(1, static_cast<int>(
			longestSceneEdge * surfaceHeightCm / surfaceWidthCm + 0.5f));
	} else {
		requestedWidth = std::max(1, static_cast<int>(
			longestSceneEdge * surfaceWidthCm / surfaceHeightCm + 0.5f));
	}

	if (requestedWidth == sceneWidth && requestedHeight == sceneHeight) return;
	sceneWidth = requestedWidth;
	sceneHeight = requestedHeight;
	if (scene.isAllocated()) {
		scene.allocate(sceneWidth, sceneHeight, GL_RGBA);
		if (scene.isAllocated()) {
			scene.getTexture().setTextureMinMagFilter(GL_NEAREST, GL_NEAREST);
		}
	}
}

// Every shape in the field, once, at unit size. Nothing here is rebuilt while the
// screen runs: bodies differ by scale, tilt and colour rather than by which
// points they are made of, so one copy of each does for all of them.
void UniverseVisual::buildMeshes() {
	// One sphere's worth of points, spread evenly by walking up the sphere in
	// equal steps of height rather than of angle - stepping the angle bunches
	// them at the poles, which reads as two bright spots rather than as a
	// surface.
	unitSpherePoints.clear();
	unitSpherePoints.setMode(OF_PRIMITIVE_POINTS);

	const int spherePoints = 420;
	const float golden = PI * (3.0f - std::sqrt(5.0f));

	for (int i = 0; i < spherePoints; i++) {
		float y = 1.0f - (static_cast<float>(i) / static_cast<float>(spherePoints - 1)) * 2.0f;
		float ringRadius = std::sqrt(std::max(0.0f, 1.0f - y * y));
		float angle = golden * static_cast<float>(i);

		unitSpherePoints.addVertex(glm::vec3(std::cos(angle) * ringRadius, y, std::sin(angle) * ringRadius));
	}

	unitRingPoints.clear();
	unitRingPoints.setMode(OF_PRIMITIVE_POINTS);

	const int ringPoints = 150;

	for (int i = 0; i < ringPoints; i++) {
		float angle = TWO_PI * static_cast<float>(i) / static_cast<float>(ringPoints);
		unitRingPoints.addVertex(glm::vec3(std::cos(angle), 0.0f, std::sin(angle)));
	}

	// The poker chip's face: concentric rings of points filling a flat disc,
	// left short of radius 1 so unitRingPoints - drawn brighter over the top,
	// at full radius, see drawElement() - reads as a separate rim rather than
	// as the outermost ring of the face blurring into it.
	unitChipFacePoints.clear();
	unitChipFacePoints.setMode(OF_PRIMITIVE_POINTS);

	const int chipFaceRings = 5;

	for (int r = 1; r <= chipFaceRings; r++) {
		float ringRadius = static_cast<float>(r) / static_cast<float>(chipFaceRings) * 0.8f;
		int pointsOnRing = std::max(6, static_cast<int>(ringRadius * 90.0f));

		for (int i = 0; i < pointsOnRing; i++) {
			float angle = TWO_PI * static_cast<float>(i) / static_cast<float>(pointsOnRing);
			unitChipFacePoints.addVertex(glm::vec3(std::cos(angle) * ringRadius, 0.0f, std::sin(angle) * ringRadius));
		}
	}

	// Three caps rather than one. The first version had a single flattened dome,
	// which is exactly the shape of a jellyfish and was read as one - what makes
	// the reference photographs look like fungus is that no two of them are the
	// same silhouette: tall bells on thread stems, wide wavy plates lying almost
	// flat, and cups that open upwards instead of hanging down.

	// The bell: tall, narrow, drawn to a point. The exponent is what does it - a
	// hemisphere raised to a power under one keeps its width almost to the top
	// and then closes quickly, which is a bell rather than a dome.
	mushroomCapBell.clear();
	mushroomCapBell.setMode(OF_PRIMITIVE_POINTS);

	const int bellPoints = 200;

	for (int i = 0; i < bellPoints; i++) {
		float up = static_cast<float>(i) / static_cast<float>(bellPoints - 1);
		float ringRadius = std::pow(std::max(0.0f, 1.0f - up), 0.62f);
		float angle = golden * static_cast<float>(i);

		mushroomCapBell.addVertex(glm::vec3(std::cos(angle) * ringRadius, up * 1.25f, std::sin(angle) * ringRadius));
	}

	// The plate: wide, barely raised, with a rim that waves up and down. The
	// waving is the whole difference between a plate and a disc, and it is one
	// term.
	mushroomCapFlat.clear();
	mushroomCapFlat.setMode(OF_PRIMITIVE_POINTS);

	const int flatPoints = 240;

	for (int i = 0; i < flatPoints; i++) {
		float out = static_cast<float>(i) / static_cast<float>(flatPoints - 1);
		float ringRadius = std::sqrt(std::max(0.0f, out)); // spread evenly over the disc rather than bunched at the middle
		float angle = golden * static_cast<float>(i);
		float wave = std::sin(angle * 3.0f) * 0.09f * ringRadius;

		mushroomCapFlat.addVertex(glm::vec3(
			std::cos(angle) * ringRadius,
			0.30f - ringRadius * ringRadius * 0.26f + wave,
			std::sin(angle) * ringRadius));
	}

	// The cup: opens upwards, so what is seen is the inside of it. The red
	// goblets in the reference are the odd ones out in a field of caps and that
	// is exactly why they are worth having.
	mushroomCapCup.clear();
	mushroomCapCup.setMode(OF_PRIMITIVE_POINTS);

	const int cupPoints = 190;

	for (int i = 0; i < cupPoints; i++) {
		float up = static_cast<float>(i) / static_cast<float>(cupPoints - 1);
		float ringRadius = 0.30f + up * 0.70f;
		float angle = golden * static_cast<float>(i);

		mushroomCapCup.addVertex(glm::vec3(std::cos(angle) * ringRadius, up * up * 0.85f, std::sin(angle) * ringRadius));
	}

	// The gills: spokes of points on the underside, running out from the stalk to
	// the rim. This is the whole reason a mushroom is worth drawing in points at
	// all - the radiating lines are what the reference photographs are mostly
	// made of, and they cost nothing here. More spokes than before, and run right
	// out to the rim, since under a flat cap they are the shape rather than a
	// detail of it.
	mushroomGillPoints.clear();
	mushroomGillPoints.setMode(OF_PRIMITIVE_POINTS);

	const int gillSpokes = 30;
	const int gillSteps = 10;

	for (int s = 0; s < gillSpokes; s++) {
		float angle = TWO_PI * static_cast<float>(s) / static_cast<float>(gillSpokes);

		for (int i = 0; i < gillSteps; i++) {
			float along = 0.16f + 0.86f * (static_cast<float>(i) / static_cast<float>(gillSteps - 1));

			// Curved up towards the rim, so the underside is a shallow bowl
			// rather than a flat disc seen edge on.
			float y = -0.18f + 0.14f * along * along;

			mushroomGillPoints.addVertex(glm::vec3(std::cos(angle) * along, y, std::sin(angle) * along));
		}
	}

	// The stalk: thin, and bent. Straight stems were the other half of the
	// jellyfish problem - nothing that grows out of the ground goes up in a
	// perfectly straight line, and the bend is what makes the cap look carried
	// rather than floating above it.
	mushroomStemPoints.clear();
	mushroomStemPoints.setMode(OF_PRIMITIVE_POINTS);

	const int stemColumns = 5;
	const int stemSteps = 28;

	for (int c = 0; c < stemColumns; c++) {
		float angle = TWO_PI * static_cast<float>(c) / static_cast<float>(stemColumns);

		for (int i = 0; i < stemSteps; i++) {
			float down = static_cast<float>(i) / static_cast<float>(stemSteps - 1);
			float y = -0.1f - down * 2.15f;
			float thickness = 0.075f + down * down * 0.085f; // thin, flaring a little towards the ground
			float bend = std::sin(down * 2.1f) * 0.26f; // the lean, baked in; which way it leans is the element's own rotation

			mushroomStemPoints.addVertex(glm::vec3(bend + std::cos(angle) * thickness, y, std::sin(angle) * thickness));
		}
	}

	// A clump: five small bells on splayed thread stems out of one point, the way
	// the pink and yellow ones in the reference grow. Baked into two meshes
	// rather than drawn as five mushrooms, so a clump costs the same two draw
	// calls as a single one.
	mushroomClusterCaps.clear();
	mushroomClusterCaps.setMode(OF_PRIMITIVE_POINTS);
	mushroomClusterStems.clear();
	mushroomClusterStems.setMode(OF_PRIMITIVE_POINTS);

	const int clusterCount = 5;

	for (int k = 0; k < clusterCount; k++) {
		float around = TWO_PI * static_cast<float>(k) / static_cast<float>(clusterCount) + 0.4f;
		float lean = 0.34f + 0.14f * static_cast<float>(k % 3);
		float height = 0.75f + 0.30f * static_cast<float>(k % 2);
		float capSize = 0.30f + 0.06f * static_cast<float>(k % 3);

		glm::vec3 top(std::cos(around) * lean, height, std::sin(around) * lean);

		// One small bell each, built the same way as the big one.
		const int clusterCapPoints = 44;

		for (int i = 0; i < clusterCapPoints; i++) {
			float up = static_cast<float>(i) / static_cast<float>(clusterCapPoints - 1);
			float ringRadius = std::pow(std::max(0.0f, 1.0f - up), 0.62f) * capSize;
			float angle = golden * static_cast<float>(i + k * 13);

			mushroomClusterCaps.addVertex(top + glm::vec3(std::cos(angle) * ringRadius, up * capSize * 1.3f, std::sin(angle) * ringRadius));
		}

		// A thread from the common base up to it, curving out as it climbs.
		const int clusterStemSteps = 16;

		for (int i = 0; i < clusterStemSteps; i++) {
			float up = static_cast<float>(i) / static_cast<float>(clusterStemSteps - 1);
			float curve = up * up;

			mushroomClusterStems.addVertex(glm::vec3(
				std::cos(around) * lean * curve,
				-0.85f + (height + 0.85f) * up,
				std::sin(around) * lean * curve));
		}
	}

	// The Moyn mark: a ring, a stroke up through it, and two strokes crossing
	// above. Flat, in the xy plane, and drawn facing the traveller - see
	// drawElement - because a flat glyph turned edge on is a line, and a mark
	// nobody can read is not the mark.
	//
	// Every point below is stamped down as a small plus-shaped cluster
	// (addThickPoint) instead of a single point, rather than relying on GL's
	// own point size - nothing else in this file touches that state, and the
	// mark is the one element worth being deliberately bolder than the rest
	// of the field. Thick enough that the ring and the three strokes read as
	// solid strokes once blown up, not as a dotted outline.
	logoPoints.clear();
	logoPoints.setMode(OF_PRIMITIVE_POINTS);

	const float logoThickness = 0.05f;
	const glm::vec2 thicknessKernel[5] = {
		glm::vec2(0.0f, 0.0f),
		glm::vec2(logoThickness, 0.0f),
		glm::vec2(-logoThickness, 0.0f),
		glm::vec2(0.0f, logoThickness),
		glm::vec2(0.0f, -logoThickness)
	};

	auto addThickPoint = [&](glm::vec2 p) {
		for (const auto & offset : thicknessKernel) {
			logoPoints.addVertex(glm::vec3(p.x + offset.x, p.y + offset.y, 0.0f));
		}
	};

	const int logoRing = 132;

	for (int i = 0; i < logoRing; i++) {
		float angle = TWO_PI * static_cast<float>(i) / static_cast<float>(logoRing);
		addThickPoint(glm::vec2(std::cos(angle), std::sin(angle)));
	}

	// Where the Y forks - exactly the ring's own top-centre point, since the
	// unit ring above has radius 1. Both the stem and the two branches meet
	// here, so the fork always lands flush with the O's top edge rather than
	// floating above it.
	const glm::vec2 fork(0.0f, 1.0f);

	// A small lambda would be one more thing to read; the three strokes are
	// written out because each has its own endpoints and nothing else in common.
	const int strokeSteps = 34;

	for (int i = 0; i < strokeSteps; i++) {
		float t = static_cast<float>(i) / static_cast<float>(strokeSteps - 1);

		// The upright, from just below the middle of the ring up to the fork.
		addThickPoint(glm::vec2(fork.x, -0.25f + t * (fork.y - -0.25f)));

		// The two branches, diverging from the fork up and outward.
		addThickPoint(glm::vec2(fork.x - t * 0.62f, fork.y + t * 0.85f));
		addThickPoint(glm::vec2(fork.x + t * 0.62f, fork.y + t * 0.85f));
	}
}

void UniverseVisual::setup() {
	VisualBase::setup();

	// The set's own colours, so this screen belongs to the build rather than
	// being the one loud thing in front of it: the turquoise of the steps and
	// posts, the two purples of the canopy and the panels, and the salmon the
	// sign is printed in. Three families rather than seven unrelated hues - a
	// field picked from a palette reads as one place, and the old mess of colour
	// read as a test card.
	palette = {
		Palette::universeTurquoise,
		Palette::universeTurquoiseDeep,
		Palette::universeLilac,
		Palette::universePurple,
		Palette::universeSalmon,
		Palette::universeAccent,
		Palette::universeTurquoisePale
	};

	// Pushed past their reference values - this screen reads through a
	// projector from across a room, and the muted set colours photograph
	// truer than they project. Multiplicative rather than a flat add so the
	// near-white paper stays near white instead of turning into an eighth hue.
	for (auto & c : palette) {
		c.setSaturation(std::min(255.0f, c.getSaturation() * 1.45f));
	}

	// The festival's floors, back as bodies in an undefined space rather than as
	// a diagram with orbits round it. MOYN is kept for the logo; the rest go to
	// whatever large thing is passing that has not got a name yet. Eight of them
	// against about thirty bodies big enough to be named, so at any moment most
	// of what goes by is still nameless - which is what keeps a name worth
	// reading.
	namedBodies = {
		{ "MOYN", -1 },
		{ "WILDE HILDE", -1 },
		{ "JEKKE JOLLE", -1 },
		{ "SPEZIALE", -1 },
		{ "EXTRAVAGANZA", -1 },
		{ "BACKSBERG", -1 },
		{ "DORFDISSE", -1 },
		{ "SIEBDRUCK", -1 },

		// WUEMME rather than WÜMME because poxel.ttf has no glyph above ASCII
		// 126: the umlaut would be drawn as a hole in the middle of the word, on
		// a projection, at the one moment a visitor is reading it. UE is what a
		// sign painter short of a punch would have done anyway.
		{ "WUEMME BAR", -1 }
	};

	buildMeshes();

	// Small, and blown up with no filtering - the pixels are the point.
	scene.allocate(sceneWidth, sceneHeight, GL_RGBA);
	scene.getTexture().setTextureMinMagFilter(GL_NEAREST, GL_NEAREST);

	camera.setNearClip(1.0f);
	camera.setFarClip(9000.0f);
	camera.setFov(62.0f);

	navigator->setup();
	buildElements();
}

bool UniverseVisual::isReady(std::string & failureReason) const {
	if (!scene.isAllocated()) {
		failureReason = "Universe scene FBO allocation failed";
		return false;
	}
	if (static_cast<int>(scene.getWidth()) != sceneWidth
		|| static_cast<int>(scene.getHeight()) != sceneHeight) {
		failureReason = "Universe scene FBO has unexpected dimensions";
		return false;
	}

	failureReason.clear();
	return true;
}

void UniverseVisual::claimName(Element & element, int elementIndex) {
	// The mark carries the festival's own name and nothing else does.
	if (element.kind == ElementKind::MoynLogo) {
		if (namedBodies[0].elementIndex < 0) {
			namedBodies[0].elementIndex = elementIndex;
			element.name = namedBodies[0].name;
		}

		return;
	}

	// Only things big enough to be looked at get named. A name floating beside a
	// speck would read as a caption on nothing.
	bool nameable = (element.kind == ElementKind::StippledOrb || element.kind == ElementKind::Mushroom)
		&& element.radius > 55.0f;

	if (!nameable) {
		return;
	}

	for (std::size_t n = 1; n < namedBodies.size(); n++) {
		if (namedBodies[n].elementIndex >= 0) {
			continue;
		}

		namedBodies[n].elementIndex = elementIndex;
		element.name = namedBodies[n].name;
		return;
	}
}

void UniverseVisual::releaseName(int elementIndex) {
	for (auto & named : namedBodies) {
		if (named.elementIndex == elementIndex) {
			named.elementIndex = -1;
		}
	}
}

// One drifting thing. `ahead` decides whether it is placed anywhere in the
// volume - which is what the opening fill wants - or out in front, which is what
// a replacement for something just passed wants.
UniverseVisual::Element UniverseVisual::makeElement(bool ahead, int elementIndex) {
	Element element;

	// Weighted so most of what goes past is small: the big bodies are events, and
	// a space made entirely of events has none.
	float roll = ofRandom(0.0f, 1.0f);

	if (roll < 0.45f) {
		element.kind = ElementKind::Mote;
		element.radius = ofRandom(0.6f, 2.4f);
	} else if (roll < 0.63f) {
		element.kind = ElementKind::DottedRing;
		element.radius = ofRandom(18.0f, 70.0f);
	} else if (roll < 0.75f) {
		element.kind = ElementKind::StippledOrb;
		element.radius = ofRandom(22.0f, 95.0f);
	} else if (roll < 0.83f) {
		element.kind = ElementKind::Halo;
		element.radius = ofRandom(40.0f, 130.0f);
	} else if (roll < 0.88f) {
		// Rare on purpose - "vereinzelte", scattered individuals rather than a
		// kind the field is made of, the same rarity Halo already has. Taken out
		// of Mote's share rather than any of the larger kinds', so nothing that
		// already reads as an event gets crowded out to make room for this one.
		element.kind = ElementKind::PokerChip;
		element.radius = ofRandom(20.0f, 55.0f);
	} else if (roll < 0.97f || logoCount > 0) {
		element.kind = ElementKind::Mushroom;
		element.radius = ofRandom(30.0f, 110.0f);
	} else {
		// Big, and there is only ever one - see logoCount.
		element.kind = ElementKind::MoynLogo;
		element.radius = ofRandom(150.0f, 230.0f);
		logoCount++;
	}

	element.color = palette[static_cast<int>(ofRandom(0, static_cast<float>(palette.size()) - 0.001f))];
	element.tiltX = ofRandom(0.0f, TWO_PI);
	element.tiltY = ofRandom(0.0f, TWO_PI);
	element.spin = ofRandom(-0.35f, 0.35f);
	element.twinklePhase = ofRandom(0.0f, TWO_PI);
	element.beatSlot = static_cast<int>(ofRandom(0, static_cast<float>(beatsPerBar) - 0.001f));
	element.wobblePhase = ofRandom(0.0f, TWO_PI);
	element.wobbleRate = ofRandom(0.6f, 1.5f);

	// Mushrooms used to stand up on a fixed world "up" - only their facing
	// was random, not their attitude, since a mushroom lying on its side
	// read as a mistake on a screen with one canonical viewing angle. This
	// is a table, not a screen: visitors stand around every side of it, so
	// there is no direction that is more "up" than another to stand up
	// against, and forcing one made every mushroom read as upright for
	// whoever happened to be standing on the one side that agreed with it
	// and wrong for everyone else. tiltX is left at the same full-range
	// random draw every other kind already gets a few lines up, so a
	// mushroom now tumbles the same as everything else in the field.
	if (element.kind == ElementKind::Mushroom) {
		// Which of the four silhouettes this one is. Clumps are the rarest
		// because they are the busiest, and the plate is the commonest because
		// its gills are what the kind is recognized by.
		float shapeRoll = ofRandom(0.0f, 1.0f);

		if (shapeRoll < 0.34f) {
			element.shape = MushroomShape::Flat;
		} else if (shapeRoll < 0.64f) {
			element.shape = MushroomShape::Bell;
		} else if (shapeRoll < 0.85f) {
			element.shape = MushroomShape::Cup;
		} else {
			element.shape = MushroomShape::Cluster;
		}

		// Two of them, anywhere in the field, are wearing a hoop. Two rather than
		// a share of them: it is a joke, and a joke that everything is in on is
		// scenery.
		if (element.shape != MushroomShape::Cluster && hoopCount < maxHoops && ofRandom(0.0f, 1.0f) < 0.25f) {
			element.hasHoop = true;
			hoopCount++;
		}
	}

	// One stable decision for this element's entire lifetime. Including motes
	// keeps the requested percentage relative to all spawned objects; a minimum
	// burst radius below keeps a selected tiny mote's event visible.
	std::uint64_t spawnSerial = nextElementSerial++;
	element.fireworkScheduled = UniverseFireworkPolicy::shouldExplode(
		fireworkSeed, spawnSerial);
	if (element.fireworkScheduled) {
		element.fireworkSeconds = ofRandom(minFireworkSeconds, maxFireworkSeconds);
	}

	float half = fieldSize * 0.5f;
	glm::vec3 position = navigator->getPosition();

	if (ahead || element.fireworkScheduled) {
		glm::vec3 forward = glm::normalize(navigator->getTarget() - position);
		glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
		glm::vec3 up = glm::cross(right, forward);

		// Replacements now start farther ahead, so they are visible for longer
		// before the traveller reaches them. Scheduled fireworks use an even
		// narrower corridor, so their rare event is likely to be on screen instead
		// of happening somewhere at the edge of the surrounding volume.
		float lateralRange = element.fireworkScheduled ? half * 0.24f : half * 0.7f;
		float verticalRange = element.fireworkScheduled ? half * 0.16f : half * 0.38f;
		element.position = position
			+ forward * ofRandom(half * 0.9f, half * 1.3f)
			+ right * ofRandom(-lateralRange, lateralRange)
			+ up * ofRandom(-verticalRange, verticalRange);
	} else {
		element.position = position + glm::vec3(
			ofRandom(-half, half),
			ofRandom(-half * 0.7f, half * 0.7f),
			ofRandom(-half, half));
	}

	claimName(element, elementIndex);

	return element;
}

void UniverseVisual::buildElements() {
	elements.clear();
	elements.reserve(elementCount);

	// All three are state about the field as a whole, so they are put back before
	// a new field is filled rather than after the old one is emptied.
	logoCount = 0;
	hoopCount = 0;
	nextElementSerial = 0;

	// Build a full-width seed from four small draws. Using several draws avoids
	// relying on the precision of one float returned by ofRandom for 64 bits.
	fireworkSeed = 0;

	for (int word = 0; word < 4; word++) {
		std::uint64_t randomWord = static_cast<std::uint64_t>(ofRandom(0.0f, 65536.0f));
		fireworkSeed = (fireworkSeed << 16U) | (randomWord & 0xffffULL);
	}

	for (auto & named : namedBodies) {
		named.elementIndex = -1;
	}

	for (int i = 0; i < elementCount; i++) {
		// Keep a third around the traveller so turning still discovers something,
		// while two thirds already occupy the forward corridor on mode entry.
		bool ahead = i >= elementCount / 3;
		elements.push_back(makeElement(ahead, i));
	}
}

void UniverseVisual::replaceElement(std::size_t elementIndex, bool createBurst) {
	if (createBurst) {
		Burst burst;
		burst.position = elements[elementIndex].position;
		burst.color = elements[elementIndex].color;
		burst.radius = std::max(elements[elementIndex].radius, minFireworkRadius);
		bursts.push_back(burst);
	}

	// Whatever it was carrying goes back into the pool before it is replaced,
	// or the names and limited special bodies would be spent within a minute.
	releaseName(static_cast<int>(elementIndex));

	if (elements[elementIndex].kind == ElementKind::MoynLogo) {
		logoCount--;
	}

	if (elements[elementIndex].hasHoop) {
		hoopCount--;
	}

	elements[elementIndex] = makeElement(true, static_cast<int>(elementIndex));
}

void UniverseVisual::updateScheduledFireworks(float deltaTime) {
	const glm::vec3 viewer = navigator->getPosition();

	for (std::size_t i = 0; i < elements.size(); i++) {
		if (!elements[i].fireworkScheduled) {
			continue;
		}

		elements[i].fireworkSeconds -= deltaTime;
		const float distance = glm::distance(elements[i].position, viewer);
		if (UniverseFireworkPolicy::isBurstDue(true,
			elements[i].fireworkSeconds, distance, fireworkApproachDistance)) {
			replaceElement(i, true);
		}
	}
}

// Anything that has fallen far enough behind is thrown away and a new one made
// out in front. Replacing rather than moving is what keeps the space feeling
// undefined: what comes past next is never what just went by.
void UniverseVisual::recycleElements() {
	glm::vec3 position = navigator->getPosition();
	glm::vec3 forward = glm::normalize(navigator->getTarget() - position);
	float half = fieldSize * 0.5f;

	for (std::size_t i = 0; i < elements.size(); i++) {
		// A selected element lives only until its near-approach or fallback timer
		// and must reach that promised event even if the visitor turns away.
		if (elements[i].fireworkScheduled) {
			continue;
		}

		glm::vec3 offset = elements[i].position - position;
		float distance = glm::length(offset);

		// Behind, or off to the side far enough that it is never coming back
		// into shot - the second case matters because the heading can swing, so
		// things can be left abeam rather than astern.
		if (glm::dot(offset, forward) >= -half * 0.35f && distance <= half * 1.6f) {
			continue;
		}

		replaceElement(i, false);
	}
}

// A sharp arrival and a decay across the beat, and nothing at all for the rest of
// the bar. Anything gentler than this reads as a wave rather than as a rhythm.
float UniverseVisual::pulseFor(int beatSlot) const {
	int delta = ((beatIndex - beatSlot) % beatsPerBar + beatsPerBar) % beatsPerBar;

	if (delta != 0) {
		return 0.0f;
	}

	return std::pow(1.0f - beatFraction, pulseDecay);
}

void UniverseVisual::update(const std::vector<DetectedObject> & detectedObjects) {
	float deltaTime = static_cast<float>(ofGetLastFrameTime());

	systemTime += deltaTime;
	navigator->update(detectedObjects, deltaTime);
	updateScheduledFireworks(deltaTime);
	recycleElements();

	for (auto & burst : bursts) {
		burst.age += deltaTime;
	}

	bursts.erase(
		std::remove_if(bursts.begin(), bursts.end(), [this](const Burst & burst) {
			return burst.age >= burstLifetime;
		}),
		bursts.end());

	viewerPosition = navigator->getPosition();
	camera.setPosition(viewerPosition);
	camera.lookAt(navigator->getTarget(), navigator->getUp());

	// The beat. Counted in seconds and converted, rather than kept as a phase, so
	// that the tempo can be read straight off beatsPerMinute while calibrating.
	beatTime += deltaTime;

	float beats = beatTime * (beatsPerMinute / 60.0f);

	beatIndex = static_cast<int>(std::floor(beats)) % beatsPerBar;
	beatFraction = beats - std::floor(beats);

	// How far everything is currently coming apart. There is always some of it,
	// so the field is never merely still, and it rises with how hard the flight
	// is being turned, or with both hands pulling apart to speed it up - so the
	// trip is something a visitor does rather than something that happens at
	// them.
	float steer = navigator->getThrustLevel();
	float speedBoost = navigator->getSpeedBoost();
	float controlHeight = navigator->getControlHeight();

	warpLevel = ofClamp(warpBase + steer * warpSteer + speedBoost * warpZoom + controlHeight * warpHeight, 0.0f, 1.0f);

	// The "you are here" cursor: the hand currently flying it, at full
	// engagement since being shown at all here means it is in control. Same
	// controlHeight already feeding warpLevel above closes the ring in as
	// that hand nears the table.
	clearHandCursors();

	if (navigator->hasControlPosition()) {
		addHandCursor(navigator->getControlPosition(), controlHeight, 1.0f);
	}

	// The border's reaction to the hand-pull-apart gesture: a rising edge
	// against a threshold, rather than every frame the boost is nonzero, so a
	// sustained pull reads as one flash rather than a flicker.
	bool boosting = speedBoost > boostPulseThreshold;

	if (boosting && !wasBoosting) {
		pulseBorder();
	}

	wasBoosting = boosting;

	updateBorderAnimation(deltaTime);

	// Glitches arrive, last a moment, and go. Timed rather than constant so they
	// are felt as something happening rather than as a filter left switched on.
	// Deliberately not on the beat: they are a fault in the picture, and a fault
	// that keeps time is an effect. Their clock does run faster while the flight
	// is being thrown around or sped up, which is the same bargain as the
	// melting.
	if (glitchRemaining > 0.0f) {
		glitchRemaining -= deltaTime;
	} else {
		glitchTimer += deltaTime * (1.0f + steer * glitchSteerRate + speedBoost * glitchZoomRate);

		if (glitchTimer >= glitchNextIn) {
			glitchTimer = 0.0f;
			glitchNextIn = ofRandom(glitchMinGap, glitchMaxGap);
			glitchRemaining = ofRandom(glitchMinLength, glitchMaxLength);
		}
	}
}

void UniverseVisual::drawElement(const Element & element, float time) {
	// Everything breathes a little rather than sitting at a fixed brightness. A
	// field of things all at the same value reads as a printed pattern; a field
	// where each is on its own cycle reads as alive.
	float twinkle = 0.75f + 0.25f * std::sin(time * 1.7f + element.twinklePhase);

	// And on top of the breathing, the beat: a full lift on this body's own beat,
	// a small one on all four so nothing is ever completely inert.
	float beat = pulseFor(element.beatSlot);
	float common = std::pow(1.0f - beatFraction, pulseDecay) * pulseCommon;
	float lit = ofClamp(twinkle * (1.0f + beat * pulseBrightness + common), 0.0f, 1.0f);

	// The beat is a *contraction*, not a swell: on its beat a body is pulled in
	// at the top and pushed out at the sides, and relaxes back across the beat.
	// A thing that only grows reads as a balloon being inflated; a thing that
	// squashes and recovers reads as something with a pulse in it.
	float squeeze = beat * pulseSquash;

	// Two slow waves per body, on its own phase and its own rate, which are what
	// everything melting is made of. Nothing here is on the beat - the beat is
	// the sharp thing and this is the slow thing, and they have to be different
	// or the whole field moves as one object.
	float wobble = std::sin(time * element.wobbleRate + element.wobblePhase);
	float wobbleB = std::sin(time * element.wobbleRate * 1.37f + element.wobblePhase * 1.9f);

	float swell = element.radius * (1.0f - squeeze * 0.35f);
	float scaleX = element.radius * (1.0f + squeeze * 0.55f + wobble * warpLevel * warpStretch);
	float scaleY = element.radius * (1.0f - squeeze + wobbleB * warpLevel * warpStretch);
	float scaleZ = element.radius * (1.0f + squeeze * 0.55f - wobble * warpLevel * warpStretch * 0.8f);

	ofPushMatrix();

	if (element.kind == ElementKind::MoynLogo) {
		// Turned to face the traveller, since a flat mark seen edge on is a line.
		// Built as a basis rather than as two rotations: the angles would have to
		// be recovered from the direction anyway, and this is the direction.
		glm::vec3 toViewer = viewerPosition - element.position;
		float distance = glm::length(toViewer);

		if (distance < 0.001f) {
			ofPopMatrix();
			return;
		}

		glm::vec3 forward = toViewer / distance;
		glm::vec3 right = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), forward);

		// Straight above or below, where world up and the view direction are the
		// same line, there is no sideways to be had and the cross product
		// collapses.
		if (glm::length(right) < 0.001f) {
			right = glm::vec3(1.0f, 0.0f, 0.0f);
		} else {
			right = glm::normalize(right);
		}

		glm::vec3 up = glm::cross(forward, right);

		glm::mat4 facing(
			glm::vec4(right * swell, 0.0f),
			glm::vec4(up * swell, 0.0f),
			glm::vec4(forward * swell, 0.0f),
			glm::vec4(element.position, 1.0f));

		ofMultMatrix(facing);
	} else if (element.kind == ElementKind::Mushroom) {
		// Mushrooms sway instead of melting. They are the one kind here with a
		// stalk holding a weight up, and shearing one turns it straight back into
		// the jellyfish this was meant to stop being - a lean is the movement that
		// belongs to the shape.
		ofTranslate(element.position);
		ofRotateXDeg(ofRadToDeg(element.tiltX) + wobbleB * (3.0f + warpLevel * 9.0f));
		ofRotateZDeg(wobble * (4.0f + warpLevel * 12.0f));
		ofRotateYDeg(ofRadToDeg(element.tiltY + element.spin * time * 0.4f));
		ofScale(scaleX, scaleY, scaleZ);
	} else {
		ofTranslate(element.position);
		ofRotateXDeg(ofRadToDeg(element.tiltX));
		ofRotateYDeg(ofRadToDeg(element.tiltY + element.spin * time));
		ofScale(scaleX, scaleY, scaleZ);

		// And then the melt: the axes leaned into each other, by an amount that
		// rises with how hard the flight is being steered. Done as a matrix rather
		// than by moving vertices, because the meshes are shared between every
		// body of a kind and there are only so many milliseconds in a Pi's frame.
		glm::mat4 melt(
			glm::vec4(1.0f, wobbleB * warpLevel * warpShear, 0.0f, 0.0f),
			glm::vec4(wobble * warpLevel * warpShear, 1.0f, wobbleB * warpLevel * warpShear * 0.6f, 0.0f),
			glm::vec4(0.0f, wobble * warpLevel * warpShear * 0.8f, 1.0f, 0.0f),
			glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

		ofMultMatrix(melt);
	}

	ofSetColor(element.color, static_cast<int>(255 * lit));

	switch (element.kind) {
	case ElementKind::Mote:
		// Drawn elsewhere, all of them together in one mesh - see drawScene.
		break;

	case ElementKind::DottedRing: {
		// Squeezed from a circle into an ellipse and back, hard on the beat and
		// slowly in between. A ring is one line and has nothing else to do with
		// itself; this is where its whole performance is.
		float ellipse = 1.0f + beat * 0.30f + wobble * 0.16f;

		ofScale(ellipse, 1.0f, 2.0f - ellipse);
		unitRingPoints.draw();
		break;
	}

	case ElementKind::StippledOrb:
		unitSpherePoints.draw();
		break;

	case ElementKind::PokerChip:
		// The rim first, brightened towards Turkis the same way the mushroom
		// gills are - the edge-stripe a real chip has is what reads as "chip"
		// rather than "coin" at the size and distance this is seen from, so it
		// gets the lighter accent every other body's brightest part already
		// gets.
		ofSetColor(element.color.getLerped(Palette::colorAccentTurquoise, 0.5f), static_cast<int>(255 * lit));
		unitRingPoints.draw();

		ofSetColor(element.color, static_cast<int>(220 * lit));
		unitChipFacePoints.draw();
		break;

	case ElementKind::MoynLogo:
		logoPoints.draw();
		break;

	case ElementKind::Mushroom: {
		// The cap takes the body's colour, the gills are the same colour pushed
		// towards Turkis and the stalk is it held back - which is the lit-from-
		// underneath look of the reference photographs without a single light in
		// the scene.
		switch (element.shape) {
		case MushroomShape::Bell:
			mushroomCapBell.draw();
			break;
		case MushroomShape::Flat:
			mushroomCapFlat.draw();
			break;
		case MushroomShape::Cup:
			mushroomCapCup.draw();
			break;
		case MushroomShape::Cluster:
			mushroomClusterCaps.draw();
			break;
		}

		// A cup is open upwards and has no underside to show, and the bells in a
		// clump are too small for anything to be read under them.
		if (element.shape == MushroomShape::Bell || element.shape == MushroomShape::Flat) {
			ofSetColor(element.color.getLerped(Palette::colorAccentTurquoise, 0.45f), static_cast<int>(255 * lit));
			mushroomGillPoints.draw();
		}

		ofSetColor(element.color, static_cast<int>(160 * lit));

		if (element.shape == MushroomShape::Cluster) {
			mushroomClusterStems.draw();
		} else {
			mushroomStemPoints.draw();
		}

		if (element.hasHoop) {
			// A hoop, kept up the only way a hoop is kept up: tipped over and
			// going round. The tilt is what does the work - a level ring around a
			// stalk is a napkin holder - and it rides up and down the stalk on a
			// slower cycle than it spins, which is what a hula hoop does.
			float hoopTime = time * hoopSpin + element.twinklePhase;
			float slide = -1.15f + 0.55f * std::sin(hoopTime * 0.55f);

			// Follows the stalk's baked-in bend, so it stays around the stalk
			// rather than hanging off it - same expression as the stem's, solved
			// for the height the hoop is at.
			float down = ofClamp((-0.1f - slide) / 2.15f, 0.0f, 1.0f);
			float bend = std::sin(down * 2.1f) * 0.26f;

			ofSetColor(element.color.getLerped(Palette::colorAccentTurquoise, 0.6f), static_cast<int>(255 * lit));

			ofPushMatrix();
			ofTranslate(glm::vec3(bend, slide, 0.0f));
			ofRotateYDeg(ofRadToDeg(hoopTime));
			ofRotateZDeg(17.0f + 7.0f * std::sin(hoopTime * 1.3f));
			ofScale(0.60f, 0.60f, 0.60f);
			unitRingPoints.draw();
			ofPopMatrix();
		}
		break;
	}

	case ElementKind::Halo:
		// Concentric rings, and the beat runs outwards through them one shell at
		// a time: the innermost lights on this body's own beat, the next on the
		// beat after, and so on round the bar. Each shell is also pushed outwards
		// as it lights, so the bar reads as one ripple leaving the middle rather
		// than as four rings taking turns. This is the part that reads as
		// something in the room being answered.
		for (int r = 0; r < beatsPerBar; r++) {
			float shellBeat = pulseFor((element.beatSlot + r) % beatsPerBar);
			float shell = (1.0f - static_cast<float>(r) * 0.22f) * (1.0f + shellBeat * 0.20f);
			float shellLit = ofClamp(twinkle * (0.45f + shellBeat * 1.1f + common), 0.0f, 1.0f);

			ofSetColor(element.color, static_cast<int>(255 * shellLit));
			ofPushMatrix();
			ofScale(shell, shell, shell);
			unitRingPoints.draw();
			ofPopMatrix();
		}
		break;
	}

	ofPopMatrix();
}

void UniverseVisual::drawScene(float time) {
	camera.begin();

	// Additive, so where things overlap they brighten instead of hiding each
	// other. Depth testing is off for the same reason - with points there is
	// nothing to occlude, and sorting them would cost more than the effect.
	ofEnableBlendMode(OF_BLENDMODE_ADD);
	ofDisableDepthTest();

	// Every mote in one mesh with a colour per vertex. They are the majority of
	// what is out there, and one draw call for all of them is the difference
	// between this screen costing a hundred draw calls and costing forty.
	moteMesh.clear();
	moteMesh.setMode(OF_PRIMITIVE_POINTS);

	float common = std::pow(1.0f - beatFraction, pulseDecay) * pulseCommon;

	for (const auto & element : elements) {
		if (element.kind != ElementKind::Mote) {
			drawElement(element, time);
			continue;
		}

		float twinkle = 0.75f + 0.25f * std::sin(time * 1.7f + element.twinklePhase);
		float lit = ofClamp(twinkle * (1.0f + pulseFor(element.beatSlot) * pulseBrightness + common), 0.0f, 1.0f);

		// Motes are single points and cannot be deformed - there is nothing to
		// deform - so what they do while everything else melts is wander off
		// their own position. Across a field of them that is the smear the rest
		// of the picture is doing, at the scale a point can do it.
		glm::vec3 drift(
			std::sin(time * 1.3f + element.twinklePhase) * warpLevel * moteDrift,
			std::sin(time * 1.7f + element.twinklePhase * 1.6f) * warpLevel * moteDrift,
			std::sin(time * 1.1f + element.twinklePhase * 2.3f) * warpLevel * moteDrift);

		moteMesh.addVertex(element.position + drift);
		moteMesh.addColor(ofColor(element.color, static_cast<int>(255 * lit)));
	}

	ofSetColor(255);
	moteMesh.draw();

	drawBursts();

	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
	camera.end();
}

// A small rocket-firework burst for each rare scheduled body (see
// updateScheduledFireworks()/Burst), in the same additive, no-depth-test pass and the
// same one-mesh-for-every-spark batching drawScene() already draws motes
// with. Every spark's position is worked out fresh from burst.age each
// frame - nothing about its path is stored anywhere, including on Burst
// itself - from a stable pseudo-random direction and speed seeded by its
// own index and the burst's position, the same sine-hash InvaderVisual's
// own firework sparks use, so the same spark answers the same way every
// frame of its short life without needing a seed of its own to be carried.
void UniverseVisual::drawBursts() {
	burstMesh.clear();
	burstMesh.setMode(OF_PRIMITIVE_POINTS);

	for (const auto & burst : bursts) {
		float progress = ofClamp(burst.age / std::max(burstLifetime, 0.01f), 0.0f, 1.0f);
		float fade = 1.0f - progress;

		if (fade <= 0.0f) {
			continue;
		}

		float speed = burstSpeedScale * std::max(burst.radius, 1.0f);

		for (int i = 0; i < burstSparkCount; i++) {
			float seedA = std::sin(static_cast<float>(i) * 91.7f + burst.position.x * 0.01f) * 43758.5453f;
			seedA -= std::floor(seedA);
			float seedB = std::sin(static_cast<float>(i) * 47.3f + burst.position.y * 0.01f + burst.position.z * 0.02f) * 12543.6f;
			seedB -= std::floor(seedB);

			float azimuth = seedA * TWO_PI;
			float elevation = (seedB - 0.5f) * PI; // -HALF_PI..HALF_PI, so sparks fly out in every direction, not just sideways
			float sparkSpeed = speed * (0.6f + seedB * 0.7f);
			float distance = sparkSpeed * burst.age;

			glm::vec3 direction(
				std::cos(elevation) * std::cos(azimuth),
				std::sin(elevation),
				std::cos(elevation) * std::sin(azimuth));

			// Staggered by the same seed rather than a second random value,
			// so the sparks do not all wink out on the same frame.
			float sparkFade = ofClamp(fade - seedA * 0.35f, 0.0f, 1.0f);

			if (sparkFade <= 0.0f) {
				continue;
			}

			burstMesh.addVertex(burst.position + direction * distance);
			burstMesh.addColor(ofColor(burst.color.getLerped(Palette::colorAccentTurquoise, 0.4f), static_cast<int>(255 * sparkFade)));
		}
	}

	ofSetColor(255);
	burstMesh.draw();
}

// Colour channels pulled apart and a few bands slid sideways. Done by drawing
// the small buffer several times rather than in a shader, which keeps it working
// whatever the Pi's driver decides to support.
void UniverseVisual::drawGlitch() {
	float width = playRect.width;
	float height = playRect.height;
	float scaleX = width / static_cast<float>(sceneWidth);
	float scaleY = height / static_cast<float>(sceneHeight);
	float split = glitchChannelSplit * scaleX;

	ofEnableBlendMode(OF_BLENDMODE_ADD);

	// The same picture twice more, each tinted from this screen's own
	// palette and offset - warm against cool is what reads as a signal
	// coming apart, the way true red/cyan channel fringing would, without
	// stepping outside the four hues everything else here is built from.
	ofSetColor(Palette::universeSalmon);
	scene.draw(playRect.x - split, playRect.y, width, height);

	ofSetColor(Palette::universeTurquoise);
	scene.draw(playRect.x + split, playRect.y, width, height);

	ofEnableBlendMode(OF_BLENDMODE_ALPHA);

	// A handful of horizontal bands taken from one place and put down in
	// another, which is the part that reads as pixels tearing rather than as a
	// colour effect.
	for (int b = 0; b < glitchBands; b++) {
		float bandTop = ofRandom(0.0f, static_cast<float>(sceneHeight));
		float bandHeight = ofRandom(2.0f, 14.0f);

		if (bandTop + bandHeight > sceneHeight) {
			bandHeight = sceneHeight - bandTop;
		}

		float shift = ofRandom(-glitchBandShift, glitchBandShift) * scaleX;

		ofSetColor(255);
		scene.getTexture().drawSubsection(
			playRect.x + shift, playRect.y + bandTop * scaleY, width, bandHeight * scaleY,
			0.0f, bandTop, static_cast<float>(sceneWidth), bandHeight);
	}
}

// Where each name lands in the small buffer. Asked of the camera while that
// buffer is still bound, for the reason given on the Label struct, and asked
// against the buffer's own rectangle rather than the screen's - the picture is
// stretched on the way out, so a label placed against the screen would drift
// further from its body the nearer it got to the edge.
void UniverseVisual::collectLabels() {
	labels.clear();

	ofRectangle sceneViewport(0.0f, 0.0f, static_cast<float>(sceneWidth), static_cast<float>(sceneHeight));

	for (const auto & element : elements) {
		if (element.name.empty()) {
			continue;
		}

		float distance = glm::length(element.position - viewerPosition);

		// Named or not, nothing is labelled from across the whole field: names
		// appear as a body is approached.
		if (distance > labelDistance) {
			continue;
		}

		glm::vec3 point = camera.worldToScreen(element.position, sceneViewport);

		// worldToScreen still returns a point for things behind the camera, so
		// the depth has to be checked or names appear mirrored across the screen
		// for bodies that are out of shot.
		if (point.z < 0.0f || point.z > 1.0f) {
			continue;
		}

		if (point.x < 0.0f || point.x > sceneWidth || point.y < 0.0f || point.y > sceneHeight) {
			continue;
		}

		Label label;
		label.text = element.name;
		label.point = glm::vec2(point.x, point.y);
		label.color = element.color;
		label.fade = ofClamp(1.0f - (distance / labelDistance), 0.0f, 1.0f);
		label.beatSlot = element.beatSlot;

		labels.push_back(label);
	}
}

// The names, drawn flat on the screen after the scene has been blown up rather
// than as geometry in it: a label in 3D would be readable from one side and
// mirrored from the other.
void UniverseVisual::drawNames() {
	if (!myFont.isLoaded() || myFont.getSize() <= 0) {
		return;
	}

	float scaleX = playRect.width / static_cast<float>(sceneWidth);
	float scaleY = playRect.height / static_cast<float>(sceneHeight);

	// The font is loaded once at 60pt for every screen and scaled here rather
	// than loaded again at another size, which would allocate a second atlas.
	float targetHeight = playRect.height * 0.022f;
	float textScale = targetHeight / static_cast<float>(myFont.getSize());

	for (const auto & label : labels) {
		// Fading in as a body is approached rather than switching on, with the
		// beat under it so the names keep time with everything else.
		float beat = pulseFor(label.beatSlot);

		// Never drawn in the body's own colour: a name is meant to stand out
		// from whatever is carrying it, and same on same reads as nothing at
		// this size through a projector. Which way to push it depends on how
		// light the body already is - always lifting towards a fixed bright
		// colour left a name unreadable on this screen's own lightest bodies,
		// so a light body gets a dark, saturated label and a dark body gets
		// a bright one instead of both getting the same treatment.
		float bodyLuminance = (0.299f * label.color.r + 0.587f * label.color.g + 0.114f * label.color.b) / 255.0f;
		ofColor bright = bodyLuminance > 0.6f
			? Palette::colorPrimaryViolet.getLerped(ofColor(0, 0, 0), 0.2f)
			: Palette::colorAccentTurquoise;
		ofSetColor(bright, static_cast<int>(ofClamp(50.0f + label.fade * 180.0f + beat * 60.0f, 0.0f, 255.0f)));

		// Snapped to the small buffer's grid, so a name sits on the same pixels
		// everything else is drawn on instead of sliding between them - the font
		// is a pixel face and this is what keeps it looking like one.
		float x = playRect.x + std::floor(label.point.x + 4.0f) * scaleX;
		float y = playRect.y + std::floor(label.point.y - 3.0f) * scaleY;

		ofPushMatrix();
		ofTranslate(x, y);
		ofScale(textScale, textScale);

		// Faux-bold: the pixel font's strokes are one pixel wide at this size,
		// which all but vanishes against a same-coloured body through a
		// projector. Drawn four times at a small screen-pixel offset instead of
		// loading a second, heavier atlas.
		float boldOffset = 1.3f / textScale;
		myFont.drawString(label.text, 0.0f, 0.0f);
		myFont.drawString(label.text, boldOffset, 0.0f);
		myFont.drawString(label.text, 0.0f, boldOffset);
		myFont.drawString(label.text, boldOffset, boldOffset);
		ofPopMatrix();
	}

	ofSetColor(255);
}

void UniverseVisual::draw() {
	// Drawn small, then blown up with no filtering. Everything the screen shows
	// goes through this, which is what makes it pixels rather than a smooth
	// picture with pixels drawn on it.
	scene.begin();

	// Even the emptiness keeps time: the space between the bodies lifts a shade
	// on the downbeat. Small enough that it is never seen as a flash, large
	// enough that the whole picture moves rather than only the things in it.
	float downbeat = pulseFor(0);

	ofClear(2 + 7 * downbeat, 1 + 4 * downbeat, 8 + 16 * downbeat, 255);
	drawScene(systemTime);

	// Still inside the buffer: the labels' places are worked out here and drawn
	// after it, see the Label struct.
	collectLabels();

	scene.end();

	// Worked out once here rather than in every helper below: the scene is
	// blown up into the table's play rect rather than the full window, so
	// anything outside it can be left black - see VisualBase::getPlayRect().
	playRect = getPlayRect();

	ofBackground(0);
	ofSetColor(255);
	scene.draw(playRect.x, playRect.y, playRect.width, playRect.height);

	if (glitchRemaining > 0.0f) {
		drawGlitch();
	}

	drawNames();
	maskPlayAreaCorners();
	drawHandCursors();
	drawPlayAreaBorder();

	ofSetColor(255);
}

void UniverseVisual::onEnter() {
	navigator->reset();
	systemTime = 0.0f;
	beatTime = 0.0f;
	beatIndex = 0;
	beatFraction = 0.0f;
	glitchRemaining = 0.0f;
	glitchTimer = 0.0f;
	wasBoosting = false;
	bursts.clear();
	buildElements();

	// The slowest, longest entrance of the four - the flight is the one screen
	// that is itself already a journey, so its own arrival takes its time
	// rather than snapping in.
	sweepBorderOnEnter(Palette::colorBorderLilac, 1, 2.4f);
}

bool UniverseVisual::keyPressed(int key) {
	if (key == 'r') {
		onEnter();
		std::cout << "Universe: field rebuilt" << std::endl;
		return true;
	}

	if (key == 'h') {
		// Only hands fly it, unless this is turned off at the table. See
		// CruiseNavigator::setStrictHands for why the escape hatch exists.
		navigator->setStrictHands(!navigator->getStrictHands());
		std::cout << "Universe: steering by "
				  << (navigator->getStrictHands() ? "hands only" : "any blob") << std::endl;
		return true;
	}

	return false; // anything else still cycles to the next visual
}

std::string UniverseVisual::getStatusLine() const {
	return "flight  heading " + ofToString(navigator->getThrustLevel(), 2)
		+ "  " + navigator->getSteeringLine()
		+ "  " + ofToString(static_cast<int>(beatsPerMinute)) + " bpm"
		+ "  ('r' rebuild, 'h' hand gate)";
}
