#include "InwardInteractionPointSelector.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

namespace {
constexpr float diagonalStep = 1.41421356237f;

struct QueueEntry {
	float distance;
	int index;
};

struct NearestFirst {
	bool operator()(const QueueEntry & left, const QueueEntry & right) const {
		return left.distance > right.distance;
	}
};

int flatIndex(int x, int y, int width) {
	return y * width + x;
}
}

bool InwardInteractionPointSelector::MaskView::isValid() const {
	return pixels != nullptr && width > 0 && height > 0 && stride >= width;
}

bool InwardInteractionPointSelector::MaskView::contains(int x, int y) const {
	return isValid() && x >= 0 && y >= 0 && x < width && y < height
		&& pixels[y * stride + x] != 0;
}

InwardInteractionPointSelector::Result InwardInteractionPointSelector::select(
	const MaskView & component, const MaskView & tableRoi,
	float capFraction, float minimumPenetrationPixels) {
	Result result;
	if (!component.isValid() || !tableRoi.isValid()
		|| component.width != tableRoi.width
		|| component.height != tableRoi.height) {
		return result;
	}

	const int width = component.width;
	const int height = component.height;
	const int pixelCount = width * height;
	std::vector<std::uint8_t> contacts(static_cast<size_t>(pixelCount), 0);

	double entrySumX = 0.0;
	double entrySumY = 0.0;
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			if (!component.contains(x, y)) continue;

			bool atTableBoundary = false;
			for (int dy = -1; dy <= 1 && !atTableBoundary; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					if (dx == 0 && dy == 0) continue;
					if (!tableRoi.contains(x + dx, y + dy)) {
						atTableBoundary = true;
						break;
					}
				}
			}
			if (!atTableBoundary) continue;

			contacts[static_cast<size_t>(flatIndex(x, y, width))] = 1;
			entrySumX += x;
			entrySumY += y;
			result.contactPixelCount++;
		}
	}

	if (result.contactPixelCount == 0) return result;
	result.entryX = static_cast<float>(entrySumX / result.contactPixelCount);
	result.entryY = static_cast<float>(entrySumY / result.contactPixelCount);

	// Multiple disconnected entry patches make "away from the entry" ambiguous
	// (merged hands and bodies are the common cases), so count them explicitly
	// and let the caller fall back to its palm/centroid chain.
	std::vector<std::uint8_t> visited(static_cast<size_t>(pixelCount), 0);
	std::queue<int> contactQueue;
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			const int start = flatIndex(x, y, width);
			if (contacts[static_cast<size_t>(start)] == 0
				|| visited[static_cast<size_t>(start)] != 0) continue;

			result.contactComponentCount++;
			visited[static_cast<size_t>(start)] = 1;
			contactQueue.push(start);
			while (!contactQueue.empty()) {
				const int current = contactQueue.front();
				contactQueue.pop();
				const int currentX = current % width;
				const int currentY = current / width;
				for (int dy = -1; dy <= 1; ++dy) {
					for (int dx = -1; dx <= 1; ++dx) {
						if (dx == 0 && dy == 0) continue;
						const int nextX = currentX + dx;
						const int nextY = currentY + dy;
						if (nextX < 0 || nextY < 0 || nextX >= width || nextY >= height) continue;
						const int next = flatIndex(nextX, nextY, width);
						if (contacts[static_cast<size_t>(next)] == 0
							|| visited[static_cast<size_t>(next)] != 0) continue;
						visited[static_cast<size_t>(next)] = 1;
						contactQueue.push(next);
					}
				}
			}
		}
	}

	if (result.contactComponentCount != 1) return result;

	const float infinity = std::numeric_limits<float>::infinity();
	std::vector<float> distances(static_cast<size_t>(pixelCount), infinity);
	std::priority_queue<QueueEntry, std::vector<QueueEntry>, NearestFirst> frontier;
	for (int index = 0; index < pixelCount; ++index) {
		if (contacts[static_cast<size_t>(index)] == 0) continue;
		distances[static_cast<size_t>(index)] = 0.0f;
		frontier.push({ 0.0f, index });
	}

	while (!frontier.empty()) {
		const QueueEntry current = frontier.top();
		frontier.pop();
		if (current.distance != distances[static_cast<size_t>(current.index)]) continue;

		const int currentX = current.index % width;
		const int currentY = current.index / width;
		for (int dy = -1; dy <= 1; ++dy) {
			for (int dx = -1; dx <= 1; ++dx) {
				if (dx == 0 && dy == 0) continue;
				const int nextX = currentX + dx;
				const int nextY = currentY + dy;
				if (!component.contains(nextX, nextY)) continue;
				const int next = flatIndex(nextX, nextY, width);
				const float step = dx != 0 && dy != 0 ? diagonalStep : 1.0f;
				const float candidate = current.distance + step;
				if (candidate >= distances[static_cast<size_t>(next)]) continue;
				distances[static_cast<size_t>(next)] = candidate;
				frontier.push({ candidate, next });
			}
		}
	}

	for (float distance : distances) {
		if (std::isfinite(distance)) {
			result.penetrationPixels = std::max(result.penetrationPixels, distance);
		}
	}
	minimumPenetrationPixels = std::max(1.0f, minimumPenetrationPixels);
	if (result.penetrationPixels < minimumPenetrationPixels) return result;

	capFraction = std::max(0.01f, std::min(capFraction, 0.5f));
	const float capThreshold = result.penetrationPixels * (1.0f - capFraction);
	double weightedX = 0.0;
	double weightedY = 0.0;
	double weightSum = 0.0;
	const float capSpan = std::max(result.penetrationPixels - capThreshold, 0.001f);
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			const float distance = distances[static_cast<size_t>(flatIndex(x, y, width))];
			if (!std::isfinite(distance) || distance < capThreshold) continue;
			const float weight = 1.0f + (distance - capThreshold) / capSpan;
			weightedX += x * weight;
			weightedY += y * weight;
			weightSum += weight;
			result.capPixelCount++;
		}
	}
	if (result.capPixelCount == 0 || weightSum <= 0.0) return result;

	result.x = static_cast<float>(weightedX / weightSum);
	result.y = static_cast<float>(weightedY / weightSum);
	result.confidence = std::max(0.0f, std::min(
		(result.penetrationPixels - minimumPenetrationPixels)
			/ minimumPenetrationPixels, 1.0f));
	result.valid = true;
	return result;
}

float InwardInteractionPointSelector::minimumPenetrationForElongatedReach(
	float palmRadiusPixels) {
	if (!std::isfinite(palmRadiusPixels)) {
		palmRadiusPixels = 0.0f;
	}
	return std::max(4.0f, std::max(0.0f, palmRadiusPixels) * 3.0f);
}

InwardInteractionPointSelector::TemporalState
InwardInteractionPointSelector::stabilize(const TemporalState & previous,
	bool candidateValid, float now, float acquireSeconds,
	float graceSeconds) {
	TemporalState next;
	acquireSeconds = std::max(0.0f, acquireSeconds);
	graceSeconds = std::max(0.0f, graceSeconds);
	if (candidateValid) {
		const bool continuous = previous.candidateSince >= 0.0f
			&& previous.lastValidTime >= 0.0f
			&& now - previous.lastValidTime <= graceSeconds;
		next.candidateSince = continuous ? previous.candidateSince : now;
		next.lastValidTime = now;
		next.active = previous.active
			|| now - next.candidateSince >= acquireSeconds;
		return next;
	}

	if (previous.active && previous.lastValidTime >= 0.0f
		&& now - previous.lastValidTime <= graceSeconds) {
		next = previous;
		next.holdPrevious = true;
	}
	return next;
}
