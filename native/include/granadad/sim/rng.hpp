#pragma once

// The one random source of the simulation.
//
// Every random decision derives from a pure four-step chain and nothing else:
//
//     h = mix64(worldSeed + TICK_STRIDE * tick)
//     h = mix64(h ^ systemSalt)
//     h = mix64(h + spatialKey)
//     draw = mix64(h + drawIndex)
//
// There are no stream positions and no hidden state. The ONLY persisted RNG
// state is the world seed, so save/load round-trips are deterministic by
// construction rather than by remembering to serialise a generator. Call order
// does not matter either: the same tuple always yields the same 64 bits.
//
// Everything in here is done in uint64_t. Java's >>> is a logical shift and its
// multiplies wrap by specification; C++ leaves signed overflow undefined and
// signed >> implementation-defined. Doing the mixing in unsigned makes the
// answer identical to Java's by the standard rather than by -fwrapv, so this
// file would still be correct on a toolchain that had never heard of that flag.
//
// The constants are PINNED. They were pinned in Java before the first golden
// master and every number in native/tests/golden_java_vectors.hpp -- which came
// out of a JVM running the real sim-core classes -- depends on them. Changing
// one is not a refactor, it is a new game.

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace granadad::sim {

// ---------------------------------------------------------------------------
// the finalizer
// ---------------------------------------------------------------------------

/// K1: the golden-ratio tick stride folded into the seed each tick.
inline constexpr std::uint64_t TICK_STRIDE = 0x9E3779B97F4A7C15ull;

/// THE 64-bit finalizer (SplitMix64) every derivation step folds through --
/// draws, system salts and stream salts alike.
[[nodiscard]] constexpr std::uint64_t mix64(std::uint64_t z) noexcept {
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// ---------------------------------------------------------------------------
// salt derivation
// ---------------------------------------------------------------------------

/// Fold seed for system identities (Java: SystemId.of).
inline constexpr std::uint64_t SYSTEM_SALT_SEED = 0x05CA1AB1E0DDBA11ull;

/// Fold seed for named actor draw streams (Java: ActorRngStream).
inline constexpr std::uint64_t STREAM_SALT_SEED = 0xACABCAFE12345678ull;

/// Folds a name into a 64-bit salt: `h = mix64(h ^ codeUnit)` per character.
///
/// Java folds `name.charAt(i)`, a UTF-16 code unit zero-extended to long. Every
/// name in the registry is ASCII, where a UTF-16 code unit and a byte are the
/// same number, so folding bytes is exact for the names that exist. It would
/// NOT be exact for a name outside ASCII: such a name must be folded as UTF-16
/// code units, not as UTF-8 bytes. There is no such name today and adding one
/// would be a determinism change, not a translation.
[[nodiscard]] constexpr std::uint64_t fold_name(std::uint64_t seed,
                                                std::string_view name) noexcept {
    std::uint64_t h = seed;
    for (const char c : name) {
        h = mix64(h ^ static_cast<std::uint64_t>(static_cast<unsigned char>(c)));
    }
    return h;
}

/// The 64-bit salt of a system name. Equal names always produce equal salts,
/// which is what makes the engine's boot-time collision check also reject
/// duplicate names.
[[nodiscard]] constexpr std::uint64_t system_salt(std::string_view name) noexcept {
    return fold_name(SYSTEM_SALT_SEED, name);
}

/// The 64-bit salt of a named draw stream.
///
/// A stream's salt is a pure function of its NAME. Its position in any registry
/// is used nowhere -- so inserting a stream in the middle of the list shifts no
/// existing draw, and RENAMING one silently re-rolls every decision that stream
/// has ever made. Never rename a stream, never reuse a name, never change the
/// seed above.
[[nodiscard]] constexpr std::uint64_t stream_salt(std::string_view name) noexcept {
    return fold_name(STREAM_SALT_SEED, name);
}

// ---------------------------------------------------------------------------
// the draw chain
// ---------------------------------------------------------------------------

/// The whole chain, flat. Pure, allocation-free, branch-free.
///
/// `spatialKey` is the canonical key of the deciding entity -- a packed
/// position, a chunk index, an actor id -- and callers must document their key
/// scheme. `drawIndex` is the 0-based index of this draw among the draws made
/// against that key this tick.
///
/// Note the drawIndex parameter is SIGNED. Java's is an `int` sign-extended
/// into the add, so a negative one must sign-extend here too; passing a
/// zero-extended value would silently disagree for exactly the inputs nobody
/// tests. See kCounterDraws' INT32_MIN row.
[[nodiscard]] constexpr std::uint64_t derive_draw(std::uint64_t world_seed, std::uint64_t tick,
                                                  std::uint64_t system_salt_value,
                                                  std::uint64_t spatial_key,
                                                  std::int32_t draw_index) noexcept {
    std::uint64_t h = mix64(world_seed + TICK_STRIDE * tick);
    h = mix64(h ^ system_salt_value);
    h = mix64(h + spatial_key);
    return mix64(h + static_cast<std::uint64_t>(static_cast<std::int64_t>(draw_index)));
}

/// A random source bound to one system's salt and one tick.
///
/// The only state is the per-tick prefix `mix64(mix64(seed + K1*tick) ^ salt)`,
/// cached so the hot path is two mixes instead of four. Nothing here is ever
/// serialised: rebinding a fresh source to the same (seed, salt, tick)
/// reproduces every draw it ever made, which IS the save/load property.
class CounterRandomSource {
public:
    /// Creates a source bound to one system's salt, initially at tick 0.
    constexpr CounterRandomSource(std::uint64_t world_seed, std::uint64_t salt) noexcept
        : world_seed_(world_seed), salt_(salt) {
        begin_tick(0);
    }

    /// Rebinds to `tick`. Engine-only: called once per tick before the owning
    /// system runs, so a system never sees a partially advanced source.
    constexpr void begin_tick(std::uint64_t tick) noexcept {
        tick_prefix_ = mix64(mix64(world_seed_ + TICK_STRIDE * tick) ^ salt_);
    }

    /// The `draw_index`-th draw for `spatial_key` at the bound (tick, system).
    [[nodiscard]] constexpr std::uint64_t draw(std::uint64_t spatial_key,
                                               std::int32_t draw_index) const noexcept {
        const std::uint64_t h = mix64(tick_prefix_ + spatial_key);
        return mix64(h + static_cast<std::uint64_t>(static_cast<std::int64_t>(draw_index)));
    }

    /// The common single-draw case.
    [[nodiscard]] constexpr std::uint64_t draw(std::uint64_t spatial_key) const noexcept {
        return draw(spatial_key, 0);
    }

    [[nodiscard]] constexpr std::uint64_t world_seed() const noexcept { return world_seed_; }
    [[nodiscard]] constexpr std::uint64_t salt() const noexcept { return salt_; }

private:
    std::uint64_t world_seed_;
    std::uint64_t salt_;
    std::uint64_t tick_prefix_ = 0;
};

// ---------------------------------------------------------------------------
// draw -> outcome
// ---------------------------------------------------------------------------

/// Permille is the resolution every skill check is expressed in.
inline constexpr std::uint64_t PERMILLE = 1000;

/// Whether a draw passes a `permille`-in-1000 check.
///
/// UNSIGNED remainder, and that is the whole content of this function. Java
/// uses Long.remainderUnsigned; a signed `%` on a raw 64-bit hash goes negative
/// for half of all draws, `< permille` is then true for every one of them, and
/// every check in the game silently inverts for half its inputs.
[[nodiscard]] constexpr bool passes(std::uint64_t draw_value, std::int32_t permille) noexcept {
    return draw_value % PERMILLE < static_cast<std::uint64_t>(static_cast<std::int64_t>(permille));
}

/// Picks an index into `weights` from a draw, weighted. `weights` must sum to a
/// positive total; the caller owns that (a zero total is a content bug, and
/// returning 0 here would hide it in the one place it is cheapest to see).
[[nodiscard]] constexpr std::size_t weighted_pick(std::uint64_t draw_value,
                                                  const std::int32_t* weights,
                                                  std::size_t count) noexcept {
    std::int64_t total = 0;
    for (std::size_t i = 0; i < count; ++i) {
        total += weights[i];
    }
    if (total <= 0) {
        return 0;
    }
    const std::uint64_t slot = draw_value % static_cast<std::uint64_t>(total);
    std::int64_t cumulative = 0;
    for (std::size_t i = 0; i < count; ++i) {
        cumulative += weights[i];
        if (slot < static_cast<std::uint64_t>(cumulative)) {
            return i;
        }
    }
    return count - 1;
}

}  // namespace granadad::sim
