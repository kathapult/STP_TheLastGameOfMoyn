#include "VisualBase.h"
#include "TableShape.h"
#include "TransformBase.h"
#include <algorithm>
#include <cmath>

//constructor
VisualBase::VisualBase() {
}

void VisualBase::setup() {
	// poxel.ttf carries ASCII 32-126 and nothing else - no accented glyphs at
	// all - which is why anything drawn through this font has to be spelled
	// within that range. See the Universe screen's body names.
	myFont.load("poxel.ttf", 60);
}

void VisualBase::update(const std::vector<DetectedObject> & detectedObjects) {
}

void VisualBase::draw() {
}

void VisualBase::setProjectionGeometry(int width, int height,
	float surfaceWidthCm, float surfaceHeightCm,
	float cornerRadiusFraction, BaseEdge baseEdge) {
	canvasWidth = std::max(width, 1);
	canvasHeight = std::max(height, 1);
	calibratedSurfaceWidthCm = std::max(surfaceWidthCm, 1.0f);
	calibratedSurfaceHeightCm = std::max(surfaceHeightCm, 1.0f);
	calibratedCornerRadiusFraction = ofClamp(cornerRadiusFraction, 0.0f, 1.0f);
	calibratedBaseEdge = baseEdge;
}

// Frame / Rectangle
ofRectangle VisualBase::getFrameRect(float widthPercent, float heightPercent, float topOffset) const {
	float frameW = canvasWidth * widthPercent;
	float frameH = canvasHeight * heightPercent;
	float frameX = (canvasWidth - frameW) / 2.0f;
	float frameY = (canvasHeight - frameH) / 2.0f + topOffset;
	return ofRectangle(frameX, frameY, frameW, frameH);
}

void VisualBase::drawFrameBorder(const ofRectangle & frame, ofColor color, float lineWidth) const {
	ofNoFill();
	ofSetColor(color);
	ofSetLineWidth(lineWidth);
	ofDrawRectangle(frame.x, frame.y, frame.width, frame.height);
	ofFill();

	float textWidth = myFont.stringWidth(title);
	float textX = frame.x + (frame.width - textWidth) / 2.0f;
	myFont.drawString(title, textX, frame.y - 20);
}

// Letterboxes the calibrated physical surface into the logical canvas.
// Output rotation and native-frame placement happen later in ProjectionMapper,
// so this remains one stable table-oriented coordinate system for every visual.
ofRectangle VisualBase::getPlayRect() const {
	float targetAspect = calibratedSurfaceWidthCm / calibratedSurfaceHeightCm;
	float windowW = static_cast<float>(canvasWidth);
	float windowH = static_cast<float>(canvasHeight);
	float windowAspect = windowW / windowH;

	float w = windowW;
	float h = windowH;

	if (windowAspect > targetAspect) {
		w = windowH * targetAspect;
	} else {
		h = windowW / targetAspect;
	}

	// The physical table's edges sit a hair inside this letterboxed
	// rectangle on both axes - trimming width and height here keeps the
	// border's own stroke (drawPlayAreaBorder()) from poking past the real
	// edge. TableShape supplies the separately shared sensor/render curve.
	w *= playRectWidthTrim;
	h *= playRectHeightTrim;

	return ofRectangle((windowW - w) * 0.5f, (windowH - h) * 0.5f, w, h);
}

float VisualBase::playAreaCornerRadius(const ofRectangle & rect) const {
	return TableShape::cornerRadius(rect.width, rect.height,
		calibratedCornerRadiusFraction);
}

// Walks a rounded rectangle: four straight edges, inset by radius at each
// end, joined by four quarter-circle arcs. With radius 0 every arc segment
// below has zero length and is skipped, so this degenerates to the plain
// rectangle walk it replaced.
glm::vec2 VisualBase::pointOnPerimeter(const ofRectangle & rect, float radius, float t) const {
	t -= std::floor(t); // wrap into 0-1

	float straightW = rect.width - 2.0f * radius;
	float straightH = rect.height - 2.0f * radius;
	float arcLen = HALF_PI * radius;
	float total = 2.0f * straightW + 2.0f * straightH + 4.0f * arcLen;
	float d = t * total;

	// Top edge, left to right.
	if (d < straightW) {
		return glm::vec2(rect.x + radius + d, rect.y);
	}
	d -= straightW;

	// Top-right corner.
	if (d < arcLen) {
		float a = d / radius;
		glm::vec2 center(rect.x + rect.width - radius, rect.y + radius);
		return center + radius * glm::vec2(std::sin(a), -std::cos(a));
	}
	d -= arcLen;

	// Right edge, top to bottom.
	if (d < straightH) {
		return glm::vec2(rect.x + rect.width, rect.y + radius + d);
	}
	d -= straightH;

	// Bottom-right corner.
	if (d < arcLen) {
		float a = d / radius;
		glm::vec2 center(rect.x + rect.width - radius, rect.y + rect.height - radius);
		return center + radius * glm::vec2(std::cos(a), std::sin(a));
	}
	d -= arcLen;

	// Bottom edge, right to left.
	if (d < straightW) {
		return glm::vec2(rect.x + rect.width - radius - d, rect.y + rect.height);
	}
	d -= straightW;

	// Bottom-left corner.
	if (d < arcLen) {
		float a = d / radius;
		glm::vec2 center(rect.x + radius, rect.y + rect.height - radius);
		return center + radius * glm::vec2(-std::sin(a), std::cos(a));
	}
	d -= arcLen;

	// Left edge, bottom to top.
	if (d < straightH) {
		return glm::vec2(rect.x, rect.y + rect.height - radius - d);
	}
	d -= straightH;

	// Top-left corner - whatever remains.
	float a = d / radius;
	glm::vec2 center(rect.x + radius, rect.y + radius);
	return center + radius * glm::vec2(-std::cos(a), -std::sin(a));
}

void VisualBase::drawCornerCutout(glm::vec2 sharpCorner, glm::vec2 arcCenter, float radius, float startAngle, float endAngle) const {
	const int arcSteps = 10;

	ofMesh cutout;
	cutout.setMode(OF_PRIMITIVE_TRIANGLE_FAN);
	cutout.addVertex(glm::vec3(sharpCorner, 0.0f));

	for (int i = 0; i <= arcSteps; i++) {
		float a = ofLerp(startAngle, endAngle, static_cast<float>(i) / static_cast<float>(arcSteps));
		cutout.addVertex(glm::vec3(arcCenter + radius * glm::vec2(std::cos(a), std::sin(a)), 0.0f));
	}

	cutout.draw();
}

float VisualBase::cometBrightness(float t, float position, float width) const {
	float dist = std::fabs(t - position);
	dist = std::min(dist, 1.0f - dist); // the perimeter wraps, so the short way round may cross 0/1

	return ofClamp(1.0f - dist / width, 0.0f, 1.0f);
}

void VisualBase::updateBorderAnimation(float deltaTime) {
	if (borderIdleActive) {
		borderIdleElapsed += deltaTime;
		float duration = borderIdleKind == BorderIdleKind::Blink ? borderBlinkDuration : borderSweepDuration;

		if (borderIdleElapsed >= duration) {
			borderIdleActive = false;
			borderIdleTimer = 0.0f;
			borderIdleNextIn = ofRandom(borderIdleMinGap, borderIdleMaxGap);
		}
	} else {
		borderIdleTimer += deltaTime;

		if (borderIdleTimer >= borderIdleNextIn) {
			borderIdleActive = true;
			borderIdleElapsed = 0.0f;
			borderIdleKind = ofRandom(1.0f) < 0.5f ? BorderIdleKind::Blink : BorderIdleKind::Sweep;
		}
	}

	if (borderEntranceActive) {
		borderEntranceElapsed += deltaTime;

		if (borderEntranceElapsed >= borderEntranceDuration) {
			borderEntranceActive = false;
		}
	}

	for (auto & pulse : borderPulses) {
		pulse.age += deltaTime;
	}

	borderPulses.erase(
		std::remove_if(borderPulses.begin(), borderPulses.end(), [this](const BorderPulse & pulse) {
			return pulse.age >= borderPulseLifetime;
		}),
		borderPulses.end());
}

void VisualBase::pulseBorder(ofColor color, float strength) {
	BorderPulse pulse;
	pulse.color = color;
	pulse.strength = strength;
	borderPulses.push_back(pulse);
}

void VisualBase::sweepBorderOnEnter(ofColor color, int laps, float duration) {
	borderEntranceActive = true;
	borderEntranceElapsed = 0.0f;
	borderEntranceColor = color;
	borderEntranceLaps = laps;
	borderEntranceDuration = duration;
}

void VisualBase::maskPlayAreaCorners() const {
	ofRectangle rect = getPlayRect();
	float radius = playAreaCornerRadius(rect);

	if (radius <= 0.0f) {
		return;
	}

	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
	ofSetColor(0, 0, 0, 255);

	drawCornerCutout(glm::vec2(rect.x, rect.y),
		glm::vec2(rect.x + radius, rect.y + radius), radius, -HALF_PI, -PI);

	drawCornerCutout(glm::vec2(rect.x + rect.width, rect.y),
		glm::vec2(rect.x + rect.width - radius, rect.y + radius), radius, -HALF_PI, 0.0f);

	drawCornerCutout(glm::vec2(rect.x + rect.width, rect.y + rect.height),
		glm::vec2(rect.x + rect.width - radius, rect.y + rect.height - radius), radius, 0.0f, HALF_PI);

	drawCornerCutout(glm::vec2(rect.x, rect.y + rect.height),
		glm::vec2(rect.x + radius, rect.y + rect.height - radius), radius, HALF_PI, PI);
}

void VisualBase::drawPlayAreaBorder() const {
	ofRectangle rect = getPlayRect();
	float cornerRadius = playAreaCornerRadius(rect);

	float idleFraction = borderIdleActive
		? ofClamp(borderIdleElapsed / (borderIdleKind == BorderIdleKind::Blink ? borderBlinkDuration : borderSweepDuration), 0.0f, 1.0f)
		: 0.0f;

	float entranceFraction = borderEntranceActive
		? ofClamp(borderEntranceElapsed / borderEntranceDuration, 0.0f, 1.0f)
		: 0.0f;
	float entrancePosition = entranceFraction * static_cast<float>(borderEntranceLaps);

	// Reaction pulses are summed rather than drawn one at a time: two events
	// close together should read as one brighter flash, not each getting its
	// own turn on the line.
	ofFloatColor pulseColor(0.0f, 0.0f, 0.0f);
	float pulseBrightness = 0.0f;

	for (const auto & pulse : borderPulses) {
		float fade = ofClamp(1.0f - (pulse.age / borderPulseLifetime), 0.0f, 1.0f);
		fade = fade * fade; // sharp arrival, quick decay - the same shape as UniverseVisual's beat pulses
		float weight = fade * pulse.strength;

		pulseColor.r += pulse.color.r / 255.0f * weight;
		pulseColor.g += pulse.color.g / 255.0f * weight;
		pulseColor.b += pulse.color.b / 255.0f * weight;
		pulseBrightness += weight;
	}

	ofMesh border;
	border.setMode(OF_PRIMITIVE_LINE_LOOP);

	for (int i = 0; i < borderPointCount; i++) {
		float t = static_cast<float>(i) / static_cast<float>(borderPointCount);
		glm::vec2 point = pointOnPerimeter(rect, cornerRadius, t);

		float brightness = borderBaseBrightness;
		ofFloatColor color(Palette::colorBorderBase.r / 255.0f, Palette::colorBorderBase.g / 255.0f, Palette::colorBorderBase.b / 255.0f);

		if (borderIdleActive) {
			if (borderIdleKind == BorderIdleKind::Blink) {
				brightness += std::sin(idleFraction * PI) * 0.5f;
			} else {
				brightness += cometBrightness(t, idleFraction, 0.06f) * 0.9f;
			}
		}

		if (borderEntranceActive) {
			float comet = cometBrightness(t, std::fmod(entrancePosition, 1.0f), 0.10f);
			float fade = 1.0f - entranceFraction;
			color = color.getLerped(
				ofFloatColor(borderEntranceColor.r / 255.0f, borderEntranceColor.g / 255.0f, borderEntranceColor.b / 255.0f),
				comet);
			brightness += comet * fade * 1.3f;
		}

		if (pulseBrightness > 0.0f) {
			float mix = ofClamp(pulseBrightness, 0.0f, 1.0f);
			ofFloatColor avgPulseColor(
				pulseColor.r / std::max(pulseBrightness, 0.001f),
				pulseColor.g / std::max(pulseBrightness, 0.001f),
				pulseColor.b / std::max(pulseBrightness, 0.001f));
			color = color.getLerped(avgPulseColor, mix);
			brightness += pulseBrightness * 0.8f;
		}

		brightness = ofClamp(brightness, 0.0f, 1.4f);

		border.addVertex(glm::vec3(point, 0.0f));
		border.addColor(ofFloatColor(color.r * brightness, color.g * brightness, color.b * brightness, 1.0f));
	}

	// A multiple of one grid cell's width rather than a fixed pixel count, so
	// the border scales with the same chunky-pixel grid every screen is
	// already built from - a thin fixed-width line would read as a separate,
	// finer thing laid over the table rather than as its edge.
	float cellWidth = rect.width / static_cast<float>(TransformBase::GRID_WIDTH);

	// Alpha rather than additive: additive blending is what made the line read
	// as a translucent glow laid over the table instead of a solid edge to it.
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
	ofSetLineWidth(cellWidth * borderLineWidthCells);
	border.draw();
}

void VisualBase::clearHandCursors() {
	handCursors.clear();
}

void VisualBase::addHandCursor(glm::vec2 position, float height, float engagement, ofColor color) {
	HandCursor cursor;
	cursor.position = position;
	cursor.height = ofClamp(height, 0.0f, 1.0f);
	cursor.engagement = ofClamp(engagement, 0.0f, 1.0f);
	cursor.color = color;
	handCursors.push_back(cursor);
}

void VisualBase::drawHandCursors() const {
	if (handCursors.empty()) {
		return;
	}

	ofRectangle rect = getPlayRect();
	float cellWidth = rect.width / static_cast<float>(TransformBase::GRID_WIDTH);

	// Additive rather than alpha: the point is to brighten the pixels already
	// there in the cursor's own colour, not to lay a separate shape over
	// them the way the ring this replaced did.
	ofEnableBlendMode(OF_BLENDMODE_ADD);

	const int steps = 24;

	for (const auto & cursor : handCursors) {
		float px = rect.x + ofClamp(cursor.position.x, 0.0f, 1.0f) * rect.width;
		float py = rect.y + ofClamp(cursor.position.y, 0.0f, 1.0f) * rect.height;

		// Closes in as the hand nears the table (height towards 0) and opens
		// out as it lifts away - see the class comment for why.
		float radius = cellWidth * ofLerp(cursorRadiusMinCells, cursorRadiusMaxCells, cursor.height);

		// "Acted on" reads as a stronger brightening at the centre rather
		// than a separate solid dot.
		float centerAlpha = ofLerp(0.25f, 0.7f, cursor.engagement);
		ofFloatColor centerColor(cursor.color.r / 255.0f, cursor.color.g / 255.0f, cursor.color.b / 255.0f, centerAlpha);
		ofFloatColor edgeColor(cursor.color.r / 255.0f, cursor.color.g / 255.0f, cursor.color.b / 255.0f, 0.0f);

		// A triangle fan from a full-alpha centre to a zero-alpha outer ring
		// interpolates linearly along every radial line, which sums to a
		// radial gradient over the whole disc.
		ofMesh glow;
		glow.setMode(OF_PRIMITIVE_TRIANGLE_FAN);
		glow.addVertex(glm::vec3(px, py, 0.0f));
		glow.addColor(centerColor);

		for (int i = 0; i <= steps; i++) {
			float a = TWO_PI * static_cast<float>(i) / static_cast<float>(steps);
			glow.addVertex(glm::vec3(px + std::cos(a) * radius, py + std::sin(a) * radius, 0.0f));
			glow.addColor(edgeColor);
		}

		glow.draw();
	}
}
