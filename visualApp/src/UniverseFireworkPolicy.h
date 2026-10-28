#pragma once

#include <cstdint>

// A stable one-time decision for whether a spawned Universe element receives a
// scheduled firework. Keeping this independent from rendering makes the
// probability testable and, importantly, prevents a 2% chance from being rolled
// again every frame until it inevitably succeeds.
namespace UniverseFireworkPolicy {
constexpr std::uint64_t chanceScale = 10000ULL;
constexpr std::uint64_t fireworkChance = 200ULL; // 2.00%

// SplitMix64's finalizer turns a session seed and monotonically increasing spawn
// serial into a well-distributed value without storing a random-engine state on
// every element. The result is deterministic for tests and stable for the full
// lifetime of the spawned body.
inline std::uint64_t mix(std::uint64_t value) {
	value += 0x9e3779b97f4a7c15ULL;
	value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
	value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
	return value ^ (value >> 31U);
}

inline bool shouldExplode(std::uint64_t sessionSeed, std::uint64_t spawnSerial) {
	std::uint64_t value = mix(sessionSeed ^ mix(spawnSerial));
	return value % chanceScale < fireworkChance;
}

// A selected body bursts at the first of two deterministic conditions: it has
// entered the visible near-approach range, or its fallback countdown elapsed.
// The approach path prevents a maximum-speed visitor from passing the body
// before the timer; the timer still guarantees an event after a sharp turn.
inline bool isBurstDue(bool scheduled, float secondsRemaining,
	float distanceToViewer, float approachDistance) {
	return scheduled
		&& (secondsRemaining <= 0.0f
			|| (distanceToViewer >= 0.0f
				&& distanceToViewer <= approachDistance));
}
}
