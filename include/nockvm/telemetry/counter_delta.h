#pragma once
#include <cstdint>

namespace nockvm::telemetry {

// A counter value tagged with the identity (generation) of the object
// instance it was read from -- lets a value that resets to 0 across
// instance recreation still be compared correctly against a value read
// from a different instance.
struct GenerationCounter {
  uint64_t generation = 0;
  uint64_t value = 0;
};

// How much `current` has grown since `previous`. A generation change means
// `previous`'s value belongs to a different instance and doesn't apply --
// `current`'s own value is the delta in that case, not a comparison
// against `previous`.
inline uint64_t generation_delta(const GenerationCounter& previous, const GenerationCounter& current) {
  if (current.generation != previous.generation) return current.value;
  return current.value >= previous.value ? current.value - previous.value : 0;
}

}  // namespace nockvm::telemetry
