#include "PixelSprite.h"
#include <algorithm>

void PixelSprite::build(const std::vector<std::string> & rows) {
	cellsHigh = static_cast<int>(rows.size());
	cellsWide = 0;

	// The widest row wins, so a row written a character short pads out with
	// transparent cells instead of shifting the art or dropping the sprite.
	for (const auto & row : rows) {
		cellsWide = std::max(cellsWide, static_cast<int>(row.size()));
	}

	if (cellsWide <= 0 || cellsHigh <= 0) {
		return;
	}

	ofPixels pixels;
	pixels.allocate(cellsWide, cellsHigh, OF_PIXELS_RGBA);
	pixels.setColor(ofColor(0, 0, 0, 0));

	for (int y = 0; y < cellsHigh; y++) {
		const std::string & row = rows[y];

		for (int x = 0; x < cellsWide; x++) {
			char cell = (x < static_cast<int>(row.size())) ? row[x] : '.';

			// Grey levels rather than colours: the draw tints the sprite, and a
			// tint multiplies, so this is what carries the shading through
			// whatever colour the screen asks for.
			int level = 0;

			switch (cell) {
			case '*':
				level = 255; // highlight
				break;
			case '#':
				level = 170; // body
				break;
			case '-':
				level = 95; // shadow
				break;
			default:
				level = 0; // transparent
				break;
			}

			pixels.setColor(x, y, ofColor(level, level, level, level == 0 ? 0 : 255));
		}
	}

	texture.allocate(pixels);

	// The cells are meant to be visible as cells. Filtering them would turn a
	// sprite this small into a smudge as soon as it is blown up to the size it
	// is drawn at on the table.
	texture.setTextureMinMagFilter(GL_NEAREST, GL_NEAREST);
	texture.loadData(pixels);
}

float PixelSprite::getAspect() const {
	if (cellsHigh <= 0) {
		return 1.0f;
	}

	return static_cast<float>(cellsWide) / static_cast<float>(cellsHigh);
}

void PixelSprite::drawInBox(float centreX, float centreY, float width, float height) const {
	if (!texture.isAllocated()) {
		return;
	}

	// The sprites are mostly transparent, so without this they would be drawn as
	// their bounding boxes. Enabled per draw rather than once at setup: the
	// screens run one after another and none of them owns the GL state.
	ofEnableAlphaBlending();

	texture.draw(centreX - width / 2.0f, centreY - height / 2.0f, width, height);
}

void PixelSprite::drawFitHeight(float centreX, float centreY, float height) const {
	drawInBox(centreX, centreY, height * getAspect(), height);
}

void PixelSprite::drawFitWidth(float centreX, float centreY, float width) const {
	float aspect = std::max(getAspect(), 0.01f);
	drawInBox(centreX, centreY, width, width / aspect);
}
