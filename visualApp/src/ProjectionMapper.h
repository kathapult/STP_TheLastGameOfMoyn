#pragma once

#include "ProjectionCalibration.h"
#include "ofMain.h"
#include <string>

// Owns the one final render surface shared by every visual and maps it into
// the projector's native framebuffer. A quarter turn swaps the logical canvas
// dimensions but leaves XRandR/KMS at the native mode.
class ProjectionMapper {
public:
	bool configure(const ProjectionCalibration & calibration,
		int outputWidth, int outputHeight);
	bool beginFrame();
	void endFrame();
	void drawToOutput(const ProjectionCalibration & calibration,
		int outputWidth, int outputHeight);

	bool isReady(std::string & failureReason) const;
	int getCanvasWidth() const { return canvasWidth; }
	int getCanvasHeight() const { return canvasHeight; }
	ofRectangle getOutputViewport(const ProjectionCalibration & calibration,
		int outputWidth, int outputHeight) const;

private:
	ofFbo canvas;
	int canvasWidth = 0;
	int canvasHeight = 0;
	bool frameOpen = false;
};
