#include "ProjectionMapper.h"

bool ProjectionMapper::configure(const ProjectionCalibration & calibration,
	int outputWidth, int outputHeight) {
	if (!isSaneProjectionCalibration(calibration)
		|| outputWidth <= 0 || outputHeight <= 0) {
		return false;
	}

	const bool swapAxes = quarterTurnSwapsAxes(calibration.outputRotation);
	const int requestedWidth = swapAxes ? outputHeight : outputWidth;
	const int requestedHeight = swapAxes ? outputWidth : outputHeight;

	if (canvas.isAllocated() && canvasWidth == requestedWidth
		&& canvasHeight == requestedHeight) {
		return true;
	}

	canvas.allocate(requestedWidth, requestedHeight, GL_RGBA);
	if (!canvas.isAllocated()) {
		canvasWidth = 0;
		canvasHeight = 0;
		return false;
	}

	canvas.getTexture().setTextureMinMagFilter(GL_NEAREST, GL_NEAREST);
	canvasWidth = requestedWidth;
	canvasHeight = requestedHeight;
	return true;
}

bool ProjectionMapper::beginFrame() {
	if (!canvas.isAllocated() || frameOpen) {
		return false;
	}
	canvas.begin();
	ofPushStyle();
	ofClear(0, 0, 0, 255);
	frameOpen = true;
	return true;
}

void ProjectionMapper::endFrame() {
	if (!frameOpen) return;
	ofPopStyle();
	canvas.end();
	frameOpen = false;
}

ofRectangle ProjectionMapper::getOutputViewport(
	const ProjectionCalibration & calibration,
	int outputWidth, int outputHeight) const {
	return ofRectangle(
		calibration.viewportX * static_cast<float>(outputWidth),
		calibration.viewportY * static_cast<float>(outputHeight),
		calibration.viewportWidth * static_cast<float>(outputWidth),
		calibration.viewportHeight * static_cast<float>(outputHeight));
}

void ProjectionMapper::drawToOutput(const ProjectionCalibration & calibration,
	int outputWidth, int outputHeight) {
	if (!canvas.isAllocated() || frameOpen) return;

	const ofRectangle target = getOutputViewport(calibration, outputWidth, outputHeight);
	ofClear(0, 0, 0, 255);
	ofPushStyle();
	ofDisableDepthTest();
	ofEnableBlendMode(OF_BLENDMODE_ALPHA);
	ofSetColor(255);
	ofPushMatrix();

	switch (calibration.outputRotation) {
	case QuarterTurn::Normal:
		ofTranslate(target.x, target.y);
		ofScale(target.width / canvasWidth, target.height / canvasHeight);
		break;
	case QuarterTurn::Clockwise90:
		ofTranslate(target.x + target.width, target.y);
		ofRotateDeg(90.0f);
		ofScale(target.height / canvasWidth, target.width / canvasHeight);
		break;
	case QuarterTurn::HalfTurn:
		ofTranslate(target.x + target.width, target.y + target.height);
		ofRotateDeg(180.0f);
		ofScale(target.width / canvasWidth, target.height / canvasHeight);
		break;
	case QuarterTurn::CounterClockwise90:
		ofTranslate(target.x, target.y + target.height);
		ofRotateDeg(-90.0f);
		ofScale(target.height / canvasWidth, target.width / canvasHeight);
		break;
	}

	canvas.draw(0.0f, 0.0f, static_cast<float>(canvasWidth),
		static_cast<float>(canvasHeight));
	ofPopMatrix();
	ofPopStyle();
}

bool ProjectionMapper::isReady(std::string & failureReason) const {
	if (!canvas.isAllocated()) {
		failureReason = "final projection FBO allocation failed";
		return false;
	}
	if (static_cast<int>(canvas.getWidth()) != canvasWidth
		|| static_cast<int>(canvas.getHeight()) != canvasHeight) {
		failureReason = "final projection FBO has unexpected dimensions";
		return false;
	}
	failureReason.clear();
	return true;
}
