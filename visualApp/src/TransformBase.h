#pragma once

#include "ofMain.h"
#include <array>

class TransformBase {
public:
	// constructor
	TransformBase();

	// virtaual destructor
	virtual ~TransformBase() = default;

	// GRID size, matched to the installation's initial 188:79 cm calibration,
	// so a cell reads as square at that aspect instead of visibly squashed the
	// way a plain 100x66 grid would be. Projection calibration may correct the
	// measured surface dimensions later; this fixed simulation topology then
	// stretches deliberately with the play area rather than changing state.
	// 126x53 keeps almost exactly the same cell
	// count as the previous 100x66 (6678 against 6600), which is the number to
	// watch if a Pi struggles - see PixelPush's massEpsilon comment on what
	// happens once too many of them are alive at once.
	static const int GRID_WIDTH = 126;
	static const int GRID_HEIGHT = 53;
	static const int GRID_SIZE = GRID_WIDTH * GRID_HEIGHT;

	// GRID array
	using Grid = std::array<float, GRID_SIZE>;

	// virtual functions
	virtual void setup();
	virtual Grid transform(const Grid & grid) = 0;

private:
protected:
};
