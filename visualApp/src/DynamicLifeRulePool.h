#pragma once

#include <array>
#include <vector>

// Curated, moderately active alternatives for PixelPushVisual's third Life
// phase. Kept free of openFrameworks so the rules and their sparse seed kinds
// can be verified on a development host.
namespace DynamicLifeRulePool {

enum class SeedKind {
	HighLifeReplicators,
	DotLifeBHeptominoes,
	DryLifePuffers,
	WalledCitiesClusters
};

struct RuleSet {
	const char * name;
	std::vector<int> birth;
	std::vector<int> survive;
	SeedKind seedKind;
};

inline const std::array<RuleSet, 4> & rules() {
	static const std::array<RuleSet, 4> catalog {{
		// Three tiny diagonal replicators form legible copying fronts.
		{ "HighLife", { 3, 6 }, { 2, 3 }, SeedKind::HighLifeReplicators },

		// B-heptominoes become localized, expanding natural growth instead of a
		// field-wide random soup.
		{ "DotLife", { 3 }, { 0, 2, 3 }, SeedKind::DotLifeBHeptominoes },

		// Paired B-heptominoes leave orthogonal puffer trails through otherwise
		// empty space.
		{ "DryLife", { 3, 7 }, { 2, 3 }, SeedKind::DryLifePuffers },

		// Three small local soups form separated persistent activity centres;
		// the rest of the table starts empty.
		{ "Walled Cities", { 4, 5, 6, 7, 8 }, { 2, 3, 4, 5 }, SeedKind::WalledCitiesClusters }
	}};
	return catalog;
}

} // namespace DynamicLifeRulePool
