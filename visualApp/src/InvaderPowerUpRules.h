#pragma once

// Tier limits and their gameplay effects live in this openFrameworks-free
// header so the upgrade curve can be checked without a Raspberry Pi build.
// The upgrades last for one round; InvaderGame resets their levels together
// with the score and wave.
namespace InvaderPowerUpRules {

inline constexpr int maxShotPatternLevel = 2;
inline constexpr int maxFireRateLevel = 3;
inline constexpr int maxShipSpeedLevel = 3;

// Saturates at the kind's maximum, so a repeated pickup or a bad future caller
// can never push a gameplay value beyond its tuned range.
constexpr int upgradedLevel(int currentLevel, int maximumLevel) {
	if (currentLevel < 0) {
		return 1;
	}

	return currentLevel < maximumLevel ? currentLevel + 1 : maximumLevel;
}

constexpr int clampedLevel(int level, int maximumLevel) {
	if (level < 0) {
		return 0;
	}

	return level > maximumLevel ? maximumLevel : level;
}

// Pattern levels are deliberately readable: one centre shot, then a pair,
// then a three-shot fan. There is no fourth tier that could fill the screen
// faster than a visitor can understand what their pickup changed.
constexpr int shotCount(int level) {
	return 1 + clampedLevel(level, maxShotPatternLevel);
}

// Each fire-rate tier removes 15% of the base interval. At the cap the stock
// 0.20 second interval becomes 0.11 seconds, still below InvaderGame's shot
// count ceiling even together with the three-shot pattern.
constexpr float fireInterval(float baseInterval, int level) {
	int tier = clampedLevel(level, maxFireRateLevel);
	return baseInterval * (1.0f - static_cast<float>(tier) * 0.15f);
}

// Speed grows by 12% of the calibrated base per tier. The maximum is a useful
// 36% lift without bypassing the eased movement or its hard play-area bounds.
constexpr float shipSpeed(float baseSpeed, int level) {
	int tier = clampedLevel(level, maxShipSpeedLevel);
	return baseSpeed * (1.0f + static_cast<float>(tier) * 0.12f);
}

}
