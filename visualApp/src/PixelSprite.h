#pragma once

#include "ofMain.h"
#include <string>
#include <vector>

// A small piece of pixel art, written out as rows of characters in the source
// and baked once into a texture.
//
// Drawn rather than loaded from a file on purpose. `bin/` is gitignored except
// for `bin/data/`, and the Pi builds from its own pulled checkout - an image
// that failed to arrive there would leave the screen drawing nothing, with no
// way to tell that apart from a bug from the other end of an SSH session. Art
// that lives in the source cannot go missing, and at this size - a dozen or so
// cells across - writing it out is no harder than drawing it.
//
// It also keeps the look consistent with the rest of the piece: the Game of
// Life screens are chunky nearest-neighbour cells, and so is this.
//
// The characters are brightness levels rather than colours:
//
//   '.'  transparent
//   '-'  shadow
//   '#'  body
//   '*'  highlight
//
// They are stored as shades of grey, so that ofSetColor() before the draw tints
// the whole sprite while keeping its shading - which is what lets one stone
// sprite be drawn in the ordinary colour high up the table and in the warning
// colour near the bottom.
class PixelSprite {
public:
	// rows must all be the same length; a shorter one is padded with
	// transparent cells rather than rejected, since a row a character short is a
	// typo in the art and not worth losing the screen over.
	void build(const std::vector<std::string> & rows);

	bool isBuilt() const { return texture.isAllocated(); }

	// Cells across divided by cells down. Used to scale the sprite without
	// distorting it: the play field is normalized against a window whose aspect
	// ratio nobody here knows, so a sprite drawn into a box in play field units
	// would be stretched by whatever the projector happens to be.
	float getAspect() const;

	// Both draw centred on the given point. Which one to use depends on which
	// dimension carries the meaning: a falling stone is scaled by its height,
	// because the height is what the danger line is measured against, and the
	// figure by its width, because its width is what a visitor is aiming with.
	void drawFitHeight(float centreX, float centreY, float height) const;
	void drawFitWidth(float centreX, float centreY, float width) const;

private:
	void drawInBox(float centreX, float centreY, float width, float height) const;

	ofTexture texture;
	int cellsWide = 0;
	int cellsHigh = 0;
};
