#pragma once

#include "TableShape.h"
#include <cmath>
#include <string>

// Quarter turns are named by what the rendered/sensor image must do to reach
// its destination. Keeping the direction explicit avoids the ambiguous phrase
// "the device is mounted clockwise", which often requires the inverse mapping.
enum class QuarterTurn {
	Normal = 0,
	Clockwise90 = 1,
	HalfTurn = 2,
	CounterClockwise90 = 3
};

// The physical edge treated as the installation's base, expressed in the
// logical table coordinate system. Top/bottom run along surfaceWidthCm;
// left/right run along surfaceHeightCm, so either a long or short side can be
// selected without changing the measured surface dimensions.
enum class BaseEdge {
	Top = 0,
	Right = 1,
	Bottom = 2,
	Left = 3
};

inline const char * quarterTurnName(QuarterTurn turn) {
	switch (turn) {
	case QuarterTurn::Normal: return "normal";
	case QuarterTurn::Clockwise90: return "clockwise90";
	case QuarterTurn::HalfTurn: return "halfTurn";
	case QuarterTurn::CounterClockwise90: return "counterClockwise90";
	default: return "normal";
	}
}

inline const char * quarterTurnDisplayName(QuarterTurn turn) {
	switch (turn) {
	case QuarterTurn::Normal: return "0 deg";
	case QuarterTurn::Clockwise90: return "90 deg CW";
	case QuarterTurn::HalfTurn: return "180 deg";
	case QuarterTurn::CounterClockwise90: return "90 deg CCW";
	default: return "invalid";
	}
}

inline bool parseQuarterTurn(const std::string & text, QuarterTurn & turn) {
	if (text == "normal") {
		turn = QuarterTurn::Normal;
	} else if (text == "clockwise90") {
		turn = QuarterTurn::Clockwise90;
	} else if (text == "halfTurn") {
		turn = QuarterTurn::HalfTurn;
	} else if (text == "counterClockwise90") {
		turn = QuarterTurn::CounterClockwise90;
	} else {
		return false;
	}
	return true;
}

inline QuarterTurn rotateQuarterTurn(QuarterTurn turn, int direction) {
	int value = static_cast<int>(turn);
	value = (value + (direction < 0 ? 3 : 1)) % 4;
	return static_cast<QuarterTurn>(value);
}

inline bool quarterTurnSwapsAxes(QuarterTurn turn) {
	return turn == QuarterTurn::Clockwise90
		|| turn == QuarterTurn::CounterClockwise90;
}

inline const char * baseEdgeName(BaseEdge edge) {
	switch (edge) {
	case BaseEdge::Top: return "top";
	case BaseEdge::Right: return "right";
	case BaseEdge::Bottom: return "bottom";
	case BaseEdge::Left: return "left";
	default: return "bottom";
	}
}

inline const char * baseEdgeDisplayName(BaseEdge edge) {
	switch (edge) {
	case BaseEdge::Top: return "top";
	case BaseEdge::Right: return "right";
	case BaseEdge::Bottom: return "bottom";
	case BaseEdge::Left: return "left";
	default: return "invalid";
	}
}

inline bool parseBaseEdge(const std::string & text, BaseEdge & edge) {
	if (text == "top") {
		edge = BaseEdge::Top;
	} else if (text == "right") {
		edge = BaseEdge::Right;
	} else if (text == "bottom") {
		edge = BaseEdge::Bottom;
	} else if (text == "left") {
		edge = BaseEdge::Left;
	} else {
		return false;
	}
	return true;
}

inline BaseEdge rotateBaseEdge(BaseEdge edge, int direction) {
	int value = static_cast<int>(edge);
	value = (value + (direction < 0 ? 3 : 1)) % 4;
	return static_cast<BaseEdge>(value);
}

inline bool baseEdgeRunsAlongSurfaceWidth(BaseEdge edge) {
	return edge == BaseEdge::Top || edge == BaseEdge::Bottom;
}

// Site-specific values owned by Pi B. The output viewport is normalized
// against the native projector signal, so a saved calibration survives a
// window recreation and is independent of the renderer's logical canvas.
struct ProjectionCalibration {
	int version = 1;
	float surfaceWidthCm = 188.0f;
	float surfaceHeightCm = 79.0f;
	// Fraction of the maximum possible radius (half the short axis) used by
	// every projected play-area border and corner mask.
	float cornerRadiusFraction = TableShape::cornerRadiusFraction;
	BaseEdge baseEdge = BaseEdge::Bottom;
	float viewportX = 0.0f;
	float viewportY = 0.0f;
	float viewportWidth = 1.0f;
	float viewportHeight = 1.0f;
	QuarterTurn outputRotation = QuarterTurn::Clockwise90;
	QuarterTurn sensorToTableRotation = QuarterTurn::CounterClockwise90;
	bool sensorMirrorX = false;
	bool sensorMirrorY = false;
};

inline bool isSaneProjectionCalibration(const ProjectionCalibration & value) {
	const auto finite = [](float number) { return std::isfinite(number); };
	constexpr float minimumSurfaceCm = 10.0f;
	constexpr float maximumSurfaceCm = 1000.0f;
	constexpr float minimumViewportFraction = 0.05f;
	constexpr float epsilon = 0.0001f;

	return value.version == 1
		&& finite(value.surfaceWidthCm)
		&& value.surfaceWidthCm >= minimumSurfaceCm
		&& value.surfaceWidthCm <= maximumSurfaceCm
		&& finite(value.surfaceHeightCm)
		&& value.surfaceHeightCm >= minimumSurfaceCm
		&& value.surfaceHeightCm <= maximumSurfaceCm
		&& finite(value.cornerRadiusFraction)
		&& value.cornerRadiusFraction >= 0.0f
		&& value.cornerRadiusFraction <= 1.0f
		&& static_cast<int>(value.baseEdge) >= 0
		&& static_cast<int>(value.baseEdge) <= 3
		&& finite(value.viewportX) && value.viewportX >= 0.0f && value.viewportX <= 1.0f
		&& finite(value.viewportY) && value.viewportY >= 0.0f && value.viewportY <= 1.0f
		&& finite(value.viewportWidth)
		&& value.viewportWidth >= minimumViewportFraction && value.viewportWidth <= 1.0f
		&& finite(value.viewportHeight)
		&& value.viewportHeight >= minimumViewportFraction && value.viewportHeight <= 1.0f
		&& value.viewportX + value.viewportWidth <= 1.0f + epsilon
		&& value.viewportY + value.viewportHeight <= 1.0f + epsilon
		&& static_cast<int>(value.outputRotation) >= 0
		&& static_cast<int>(value.outputRotation) <= 3
		&& static_cast<int>(value.sensorToTableRotation) >= 0
		&& static_cast<int>(value.sensorToTableRotation) <= 3;
}

inline bool sameProjectionCalibration(const ProjectionCalibration & left,
	const ProjectionCalibration & right) {
	const auto sameFloat = [](float first, float second) {
		return std::fabs(first - second) <= 0.00001f;
	};
	return left.version == right.version
		&& sameFloat(left.surfaceWidthCm, right.surfaceWidthCm)
		&& sameFloat(left.surfaceHeightCm, right.surfaceHeightCm)
		&& sameFloat(left.cornerRadiusFraction, right.cornerRadiusFraction)
		&& left.baseEdge == right.baseEdge
		&& sameFloat(left.viewportX, right.viewportX)
		&& sameFloat(left.viewportY, right.viewportY)
		&& sameFloat(left.viewportWidth, right.viewportWidth)
		&& sameFloat(left.viewportHeight, right.viewportHeight)
		&& left.outputRotation == right.outputRotation
		&& left.sensorToTableRotation == right.sensorToTableRotation
		&& left.sensorMirrorX == right.sensorMirrorX
		&& left.sensorMirrorY == right.sensorMirrorY;
}

inline const char * baseEdgeLengthClass(const ProjectionCalibration & value) {
	const float alongBase = baseEdgeRunsAlongSurfaceWidth(value.baseEdge)
		? value.surfaceWidthCm : value.surfaceHeightCm;
	const float perpendicular = baseEdgeRunsAlongSurfaceWidth(value.baseEdge)
		? value.surfaceHeightCm : value.surfaceWidthCm;
	if (std::fabs(alongBase - perpendicular) < 0.01f) return "equal side";
	return alongBase > perpendicular ? "long side" : "short side";
}
