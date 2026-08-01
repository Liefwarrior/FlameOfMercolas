#pragma once

// The tick loop: phases, system registration, and the context a system runs in.
//
// WHAT IS MODELLED, AND WHAT IS NOT
//
// The Java engine declares twelve tick phases. Nine of them have never had a
// registered system in any built configuration, because the subsystems they
// were declared for -- fluids, thermal, reactions, light, boundary flux, bubble
// promote/demote, macro economy -- do not exist. The whole repository contains
// two SimulationSystem implementations: the actor simulation and a headless
// heartbeat. ARCHITECTURE.md section 0 marks the rest NOT BUILT and says
// plainly: port the mechanism, do not port the empty phases as if they need
// occupants.
//
// So this enum has three phases and they are the three that run. Declaring
// THERMAL here would be a promise this project has already learned not to make:
// a phase with a five-line doc comment and no occupant reads as "partly done"
// for years.
//
// Collapsing the ordinals is safe, and it is worth saying why rather than
// hoping. A phase ordinal in the Java feeds exactly two things: the event bus's
// (tick, phase, regIndex) ordering key, and a diagnostics array of per-phase
// nanosecond counts. It is folded into no hash and no RNG derivation. Only the
// ORDER is load-bearing, and the order is preserved.
//
// The event bus is not ported either, for the same evidence-based reason: a
// grep across the whole actor package for emit() finds nothing. The bus carries
// zero traffic. When a system needs to publish an event, that is the moment to
// build it -- with a consumer in the same commit.

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "granadad/content/world.hpp"
#include "granadad/sim/rng.hpp"
#include "granadad/sim/system_id.hpp"
#include "granadad/sim/world_hash.hpp"

namespace granadad::sim {

// ---------------------------------------------------------------------------
// phases
// ---------------------------------------------------------------------------

/// Execution order IS ordinal order. Append only.
enum class TickPhase : std::uint8_t {
    /// Before anything else: external input lands, heartbeats fire.
    TickBegin = 0,
    /// The simulation. Actors decide and act.
    Actors = 1,
    /// After everything: end-of-tick bookkeeping.
    TickEnd = 2,
};

inline constexpr std::size_t TICK_PHASE_COUNT = 3;

[[nodiscard]] std::string_view tick_phase_name(TickPhase phase) noexcept;

// ---------------------------------------------------------------------------
// the clock
// ---------------------------------------------------------------------------

/// The simulation clock. Ticks are the only unit of time the simulation knows;
/// wall-clock never enters state.
class TickClock {
public:
    /// One tick is one second of simulated time.
    ///
    /// ARCHITECTURE.md's package map says 100 ms. The Java code says 1000
    /// (TickClock.java:15) and the code is what the goldens were taken from.
    static constexpr std::int64_t MILLIS_PER_TICK = 1000;

    /// The current tick. Zero before the first advance; the first ticked tick
    /// is 1, not 0 -- so "tick 0" means "nothing has happened yet".
    [[nodiscard]] constexpr std::int64_t current_tick() const noexcept { return tick_; }

    constexpr void advance() noexcept { ++tick_; }

    /// Rewinds or fast-forwards, for load. Throws EngineError on a negative.
    void reset_to(std::int64_t tick);

    [[nodiscard]] constexpr std::int64_t simulated_millis() const noexcept {
        return tick_ * MILLIS_PER_TICK;
    }

private:
    std::int64_t tick_ = 0;
};

// ---------------------------------------------------------------------------
// what a system sees
// ---------------------------------------------------------------------------

/// Everything a system is handed for one tick. Borrows; must not outlive the
/// engine's tick call.
class TickContext {
public:
    TickContext(std::int64_t tick, TickPhase phase, const CounterRandomSource& rng,
                content::World& world) noexcept
        : tick_(tick), phase_(phase), rng_(&rng), world_(&world) {}

    [[nodiscard]] std::int64_t tick() const noexcept { return tick_; }
    [[nodiscard]] TickPhase phase() const noexcept { return phase_; }

    /// This system's random source, already bound to this tick and this
    /// system's salt. The system supplies only the spatial key and draw index.
    [[nodiscard]] const CounterRandomSource& rng() const noexcept { return *rng_; }

    /// Shorthand for the hot path.
    [[nodiscard]] std::uint64_t draw(std::uint64_t spatial_key,
                                     std::int32_t draw_index) const noexcept {
        return rng_->draw(spatial_key, draw_index);
    }

    [[nodiscard]] content::World& world() const noexcept { return *world_; }

private:
    std::int64_t tick_;
    TickPhase phase_;
    const CounterRandomSource* rng_;
    content::World* world_;
};

/// One registered subsystem.
class SimulationSystem {
public:
    SimulationSystem() = default;
    virtual ~SimulationSystem() = default;
    SimulationSystem(const SimulationSystem&) = delete;
    SimulationSystem& operator=(const SimulationSystem&) = delete;

    [[nodiscard]] virtual const SystemId& id() const noexcept = 0;
    [[nodiscard]] virtual TickPhase phase() const noexcept = 0;

    /// One tick of this system's work.
    virtual void tick(const TickContext& context) = 0;

    /// Feeds this system's canonical state into its sub-hash.
    ///
    /// PURE, and iteration must be in canonical sorted or dense-index order.
    /// This is the method where an unordered container stops being a style
    /// preference and starts being a divergent world hash.
    virtual void hash_into(HashSink& sink) const = 0;
};

// ---------------------------------------------------------------------------
// the engine
// ---------------------------------------------------------------------------

/// The phased tick loop.
///
/// Borrows the world; the world must outlive the engine.
class PhasedEngine {
public:
    PhasedEngine(std::uint64_t world_seed, content::World& world) noexcept
        : world_seed_(world_seed), world_(&world) {}

    PhasedEngine(const PhasedEngine&) = delete;
    PhasedEngine& operator=(const PhasedEngine&) = delete;

    /// Registers a system. Legal only before boot().
    ///
    /// Throws EngineError on a duplicate name, a salt collision, a section-id
    /// collision, or a collision with a reserved identity. A collision is a
    /// boot failure and never a silent reseed: two systems sharing a salt share
    /// an RNG stream and a hash section, and every draw either of them makes
    /// from then on is wrong in a way no assertion will name.
    void register_system(std::unique_ptr<SimulationSystem> system);

    /// Freezes the registration list and fixes execution order. Idempotent
    /// only in the sense that calling it twice throws.
    void boot();

    [[nodiscard]] bool booted() const noexcept { return booted_; }

    /// Advances one tick and runs every system, phases in order.
    void tick();

    [[nodiscard]] std::int64_t current_tick() const noexcept { return clock_.current_tick(); }
    [[nodiscard]] std::uint64_t world_seed() const noexcept { return world_seed_; }
    [[nodiscard]] std::size_t system_count() const noexcept { return systems_.size(); }

    /// The systems in EXECUTION order, which is phase order then registration
    /// order. Exposed so a report can list them without guessing.
    [[nodiscard]] const SimulationSystem& system_at(std::size_t executionIndex) const;

    /// Feeds the world and every system into `hasher`.
    void hash_into(WorldHasher& hasher) const;

    /// The combined hash of the world plus every system, right now.
    [[nodiscard]] std::uint64_t combined_hash() const;

private:
    struct Registration {
        std::unique_ptr<SimulationSystem> system;
        CounterRandomSource rng;
    };

    void require_not_booted(const char* what) const;

    std::uint64_t world_seed_;
    content::World* world_;
    TickClock clock_;
    bool booted_ = false;

    /// Registration order. Never reordered -- order_ carries execution order,
    /// so a system's index here stays stable for the life of the engine.
    std::vector<Registration> systems_;

    /// Indices into systems_, sorted by phase with registration order preserved
    /// inside a phase. Built by boot().
    std::vector<std::size_t> order_;
};

}  // namespace granadad::sim
