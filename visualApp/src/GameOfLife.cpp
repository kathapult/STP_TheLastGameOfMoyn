#include "GameOfLife.h"
#include "DetectedObject.h"
#include <iostream>

// Constructor
GameOfLife::GameOfLife()
	: TransformBase() {
}

void GameOfLife::setup() {

	// create random grid
	for (int y = 0; y < GRID_HEIGHT; y++) {
		for (int x = 0; x < GRID_WIDTH; x++) {
			int index = x + y * GRID_WIDTH;
			
			grid[index] = (ofRandom(0, 1) < aliveProbability) ? 1.0f : 0.0f; // aliveProbability 
			influenceField[index] = 0.0f;
		}
	}
}

// Object Influence
// The influence an object exerts is no longer uniform: its reach comes from DetectedObject::area
// and its intensity from DetectedObject::z, both of which Pi A now measures (see
// contracts/osc-object-stream.md). This restores the responsiveness the Leap-era version had via
// hand.pinchStrength, but from properties of the object itself rather than from a vendor gesture
// value - a big flat hand reaches wide, a fingertip pokes narrow, and lifting off the table makes
// the effect stronger without ever dropping it to nothing.
void GameOfLife::update(const std::vector<DetectedObject> & detectedObjects) {
	for (int i = 0; i < GRID_SIZE; i++) {
		influenceField[i] = influenceField[i] * 0.9f;
	}

	// map normalized detected-object position (0.0-1.0) to grid coordinates
	for (const auto & obj : detectedObjects) {
		int gx = static_cast<int>(obj.x * GRID_WIDTH);
		int gy = static_cast<int>(obj.y * GRID_HEIGHT);

		gx = ofClamp(gx, 0, GRID_WIDTH - 1);
		gy = ofClamp(gy, 0, GRID_HEIGHT - 1);

		// area is the share of the table an object covers, so it is a small number even for a
		// whole hand - sqrt turns that area into something proportional to a radius, which is
		// what makes the reach grow evenly rather than barely moving and then jumping.
		float reach = ofClamp(sqrt(obj.area / areaAtMaxInfluence), 0.0f, 1.0f);
		int radius = static_cast<int>(ofMap(reach, 0.0f, 1.0f, minInfluenceRadius, maxInfluenceRadius));
		radius = std::max(radius, 1);

		// Height above the table scales the whole contribution, from baselineHeightInfluence
		// when resting on it up to full strength when lifted towards the sensor.
		float heightScale = ofLerp(baselineHeightInfluence, 1.0f, ofClamp(obj.z, 0.0f, 1.0f));

		// A blob Pi A identified as standing on the table by itself counts for less than a hand.
		// Only Object is damped - Unknown stays at full strength, so an unclassified blob errs
		// towards reacting rather than towards being invisible.
		float typeScale = (obj.type == ObjectType::Object) ? staticObjectInfluence : 1.0f;
		float strength = heightScale * typeScale;

		for (int dy = -radius; dy <= radius; dy++) {
			for (int dx = -radius; dx <= radius; dx++) {

				int nx = gx + dx;
				int ny = gy + dy;

				if (nx < 0 || ny < 0 || nx >= GRID_WIDTH || ny >= GRID_HEIGHT) {
					continue;
				}

				float distance = sqrt(dx * dx + dy * dy);

				// radius
				if (distance > radius) {
					continue;
				}
				// calc radius influence

				int index = nx + ny * GRID_WIDTH;
				float t = ofClamp(distance / radius, 0.0f, 1.0f);
				float falloff = 1.0f - (t * t * (3.0f - 2.0f * t)); // smoothstep

				influenceField[index] = std::min(influenceField[index] + falloff * strength, 1.0f);
			}
		}
	}
}

// CheckNeighbours Function
int GameOfLife::countAliveNeighbours(const Grid & grid, int x, int y) {
	int count = 0;

	for (int dy = -1; dy <= 1; dy++) {
		for (int dx = -1; dx <= 1; dx++) {
			// neighbour coordinates
			int nx = x + dx;
			int ny = y + dy;

			// skip first cell to avoid crash
			if (dx == 0 && dy == 0) {
				continue;
			}

			if (nx < 0 || ny < 0 || nx >= GRID_WIDTH || ny >= GRID_HEIGHT) {
				continue;
			}

			int index = nx + ny * GRID_WIDTH;

			if (grid[index] > 0.5f) {
				count = count + 1;
			}
		}
	}
	return count;
}

// Transform Function
GameOfLife::Grid GameOfLife::transform(const Grid & grid) {
	Grid newGrid;

	for (int y = 0; y < GRID_HEIGHT; y++) {
		for (int x = 0; x < GRID_WIDTH; x++) {
			int index = x + y * GRID_WIDTH;
			int aliveNeighbours = countAliveNeighbours(grid, x, y);
			bool isAlive = grid[index] > 0.5f;

			bool isSurviveCount = std::find(surviveCounts.begin(), surviveCounts.end(), aliveNeighbours) != surviveCounts.end();
			bool isBirthCount = std::find(birthCounts.begin(), birthCounts.end(), aliveNeighbours) != birthCounts.end();

			// hand influence
			float influence = influenceField[index];
			float influenceMultiplyer = 0.3;

			if (isAlive && isSurviveCount) {

				newGrid[index] = 1.0f;

			} else if (!isAlive && isBirthCount) {
				newGrid[index] = 1.0f;
			} else if (isAlive && !isSurviveCount) {

				// cell dies - influence can save
				if (ofRandom(0, 1) < influence * influenceMultiplyer) {
					newGrid[index] = 1.0f;
				} else {
					newGrid[index] = 0.0f;
				}
			} else {
				// cell stays dead - influence can reanimate
				if (ofRandom(0, 1) < influence * influenceMultiplyer) {
					newGrid[index] = 1.0f;
				} else {
					newGrid[index] = 0.0f;
				}
			}
		}
	}
	return newGrid;
}

// SetRules Function
void GameOfLife::setRules(std::vector<int> birth, std::vector<int> survive) {
	birthCounts = birth;
	surviveCounts = survive;
}
