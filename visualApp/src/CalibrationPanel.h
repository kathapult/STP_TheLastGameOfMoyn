#pragma once

#include "CalibrationControlClient.h"
#include "DetectedObject.h"
#include "ProjectionSettings.h"
#include "PreviewReceiver.h"
#include "ofMain.h"
#include <vector>

// Full-screen maintenance overlay owned by ofApp, never by a VisualBase
// subclass. It consumes every key while calibration is active. Sensor values
// remain round-tripped through Pi A; Pi B projection values apply immediately
// and are persisted independently.
class CalibrationPanel {
public:
	void setup();
	void draw(const CalibrationControlClient & control,
		const PreviewReceiver & telemetry,
		const ProjectionSettings & projection,
		const std::vector<DetectedObject> & mappedObjects,
		const ofRectangle & playRect, int canvasWidth, int canvasHeight) const;
	void keyPressed(int key, CalibrationControlClient & control,
		ProjectionSettings & projection, bool & cycleVisual);

public:
	enum class Group {
		Depth,
		TableMask,
		RoiPosition,
		RoiSize,
		BlobSize,
		Tracker,
		Morphology,
		Smoothing,
		Classification,
		HandSize,
		HandPosition,
		Stationary,
		Visibility,
		SurfaceSize,
		SurfaceRounding,
		BaseEdge,
		ViewportPosition,
		ViewportExtent,
		OutputRotation,
		SensorOrientation,
		SensorMirror,
		Count
	};

	struct GroupFields {
		const char * label;
		CalibrationProtocol::Field horizontal;
		CalibrationProtocol::Field vertical;
		bool invertHorizontal = false;
		bool invertVertical = false;
	};

private:
	bool isProjectionGroup() const;
	const char * activeGroupLabel() const;
	const GroupFields & activeFields() const;
	void selectAdjacentGroup(int direction);
	void nudge(int key, CalibrationControlClient & control);
	void nudgeProjection(int key, ProjectionSettings & projection);
	void drawImageDiagnostics(const CalibrationControlClient & control,
		const PreviewReceiver & telemetry,
		const ofRectangle & rectangle) const;
	void drawControlCard(const ofRectangle & rectangle,
		const CalibrationControlClient & control,
		const ProjectionSettings & projection) const;
	void drawShortcutBar(const ofRectangle & rectangle) const;
	void drawProjectionGuide(const ProjectionSettings & projection,
		const std::vector<DetectedObject> & mappedObjects,
		const ofRectangle & playRect) const;
	// Mirrors and outlines the same rounded table ROI Pi A applies to depth
	// pixels, rather than presenting its bounding rectangle as active area.
	void drawRoiOverlay(const ofRectangle & rectangle,
		const CalibrationProtocol::SettingsState & state) const;
	void drawTracks(const ofRectangle & rectangle,
		const PreviewReceiver & telemetry) const;

	Group group = Group::SurfaceSize;
	bool showText = true;
	bool showImages = true;
};
