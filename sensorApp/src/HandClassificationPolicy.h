#pragma once

// Pure policy for the shape part of hand classification. The ordinary path
// requires a plausible physical palm size. Only a temporally stabilized,
// elongated border reach may bypass that veto; consuming the stabilized state
// here keeps a one-frame depth dropout from changing both type and cursor source.
namespace HandClassificationPolicy {

inline bool looksLikeHand(bool sizeFitsHand, bool armLike, bool fingerLike,
	bool stableElongatedReach) {
	return (sizeFitsHand && (armLike || fingerLike)) || stableElongatedReach;
}

} // namespace HandClassificationPolicy
