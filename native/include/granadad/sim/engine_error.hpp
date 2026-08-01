#pragma once

// The one exception type the simulation spine throws.
//
// granadad::content has FormatError for "the bytes on disk violate the format".
// This is its sibling for "the engine was assembled wrong": a duplicate system
// name, a salt collision, an overlay that is not ascending, a section fed twice.
//
// Same doctrine as FormatError, and it is worth restating rather than assuming:
// there is no tolerant mode. A simulation that boots with two systems sharing a
// salt has silently aliased their RNG streams, and every draw either of them
// makes from then on is wrong in a way no test will name. Refusing to start is
// the cheap outcome.
//
// Deliberately NOT reusing content::FormatError. "The save is corrupt" and "you
// registered two systems called actors" are different findings, and a catch
// site that cannot tell them apart will eventually treat one as the other.

#include <stdexcept>
#include <string>

namespace granadad::sim {

/// Thrown when the engine's own contracts are violated.
class EngineError : public std::runtime_error {
public:
    explicit EngineError(const std::string& message) : std::runtime_error(message) {}
};

}  // namespace granadad::sim
