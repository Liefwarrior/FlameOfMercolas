#pragma once

// The stable identity of a simulation system.
//
// Three things, all derived from one name:
//
//   name        a stable, unique, lower-case string
//   salt        the 64-bit value that seeds this system's RNG derivation AND
//               names its sub-hash section
//   sectionId   the 4-character TROJSAV section its state saves under
//
// The salt is a pure function of the name, which is what makes the engine's
// boot-time salt-collision check also a duplicate-name check, and what makes a
// save written by one build readable by another. Renaming a system therefore
// re-rolls every random decision it has ever made and orphans its saved state.
// A rename is a migration, not a refactor.

#include <cstdint>
#include <string>
#include <string_view>

#include "granadad/sim/rng.hpp"

namespace granadad::sim {

/// Section ids are exactly four printable-ASCII characters.
inline constexpr std::size_t SECTION_ID_LENGTH = 4;

/// The name-derived section id: the first four letters/digits of the name,
/// upper-cased, '_'-padded. "fire" -> "FIRE", "fluids" -> "FLUI", "a" -> "A___".
[[nodiscard]] std::string derive_section_id(std::string_view name);

/// A system's identity. Immutable.
class SystemId {
public:
    /// Derives an identity whose section id comes from the name.
    /// Throws EngineError on an empty name.
    [[nodiscard]] static SystemId of(std::string_view name);

    /// Derives an identity saving under an explicit section id -- the way a
    /// system whose natural name derives differently reaches a pinned id.
    /// Throws EngineError on an empty name or a section id that is not exactly
    /// four printable-ASCII characters.
    [[nodiscard]] static SystemId of(std::string_view name, std::string_view section_id);

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] std::uint64_t salt() const noexcept { return salt_; }
    [[nodiscard]] const std::string& section_id() const noexcept { return section_id_; }

private:
    SystemId(std::string name, std::uint64_t salt, std::string section_id)
        : name_(std::move(name)), salt_(salt), section_id_(std::move(section_id)) {}

    std::string name_;
    std::uint64_t salt_;
    std::string section_id_;
};

/// The reserved identity the world's own sub-hash accumulates under. Its salt
/// is constexpr because the hasher needs it before any SystemId exists.
inline constexpr std::string_view WORLD_SECTION_NAME = "world";
inline constexpr std::uint64_t WORLD_SECTION_SALT = system_salt(WORLD_SECTION_NAME);
static_assert(WORLD_SECTION_SALT == 0xC437BA0F7088FD8Full,
              "the world section salt is pinned by golden_java_vectors.hpp");

}  // namespace granadad::sim
