#pragma once

#include "DetectedObject.h"
#include "TransformBase.h"
#include "ofMain.h"
#include <algorithm>

class GameOfLife : public TransformBase {
public:
	GameOfLife();

	void setup() override;
	void update(const std::vector<DetectedObject> & detectedObjects); // get detected-object data
	Grid transform(const Grid & grid) override;
	void setRules(std::vector<int> birth, std::vector<int> survive); // flexible rules

	const Grid & getGrid() const { return grid; } // get Grid
	const Grid & getInfluenceField() const { return influenceField; } // colors

private:

	Grid grid; // saves Grid
	int countAliveNeighbours(const Grid & grid, int x, int y); // nightbours check
	Grid influenceField; // hand influence field

	std::vector<int> birthCounts = { 3 }; // goes live again
	std::vector<int> surviveCounts = { 2, 3 }; // survives

	float aliveProbability = 0.2f; // grid probability of alive cells

	// Influence radius range (grid cells). An object's radius is interpolated across this
	// range from its DetectedObject::area, so a spread hand reaches further than a fingertip.
	// The lower bound stays large enough to be visible at all; the upper bound stops a very
	// large object (an arm laid across the table) from flooding the whole grid.
	int minInfluenceRadius = 3;
	int maxInfluenceRadius = 12;

	// Share of the table at which an object already reaches maxInfluenceRadius. A hand covers
	// only a few percent of the table, so this is small on purpose - tuned against what area
	// values actually arrive, visible in Pi B's debug HUD.
	float areaAtMaxInfluence = 0.05f;

	// How much an object's height above the table scales its influence. At z = 0 (resting on
	// the table) an object still acts at baselineHeightInfluence; lifting it towards the sensor
	// raises that towards 1.0. Never reaches zero, so a hand flat on the table is not silently
	// ignored - the piece should react to touching as well as to hovering.
	float baselineHeightInfluence = 0.35f;

	// How strongly something Pi A classified as a static object acts, relative to a hand. A cup
	// left standing on the table should register but not dominate the grid the way a person
	// interacting with the piece does. Unknown deliberately counts as full strength: a
	// misclassification then shows up as something reacting slightly too much, not as a dead
	// patch, which is far easier to spot and far less damaging during a show.
	float staticObjectInfluence = 0.5f;
};