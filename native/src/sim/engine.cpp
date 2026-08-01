#include "granadad/sim/engine.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

#include "granadad/sim/engine_error.hpp"

namespace granadad::sim {
namespace {

/// Identities the container reserves. A system claiming one of these would
/// write over the world's own sub-hash or a container section on save.
constexpr std::string_view kReservedNames[] = {"world", "input-gate"};
constexpr std::string_view kReservedSectionIds[] = {"META", "INPT", "EVNT",
                                                    "WRLD", "CHNG", "AETH"};

std::string hex64(std::uint64_t value) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) {
        out[static_cast<std::size_t>(i)] = kHex[static_cast<std::size_t>(value & 0xFu)];
        value >>= 4;
    }
    return "0x" + out;
}

}  // namespace

std::string_view tick_phase_name(TickPhase phase) noexcept {
    switch (phase) {
        case TickPhase::TickBegin:
            return "tick-begin";
        case TickPhase::Actors:
            return "actors";
        case TickPhase::TickEnd:
            return "tick-end";
    }
    return "?";
}

void TickClock::reset_to(std::int64_t tick) {
    if (tick < 0) {
        throw EngineError("cannot reset the clock to a negative tick: "
                          + std::to_string(tick));
    }
    tick_ = tick;
}

void PhasedEngine::require_not_booted(const char* what) const {
    if (booted_) {
        throw EngineError(std::string(what) + " after boot()");
    }
}

void PhasedEngine::register_system(std::unique_ptr<SimulationSystem> system) {
    require_not_booted("cannot register a system");
    if (system == nullptr) {
        throw EngineError("cannot register a null system");
    }
    const SystemId& id = system->id();
    // Captured before the move below: `id` is a reference into the object the
    // unique_ptr owns, and reading through it after the move is legal but
    // exactly the kind of subtlety that survives review and then does not.
    const std::uint64_t salt = id.salt();

    for (const std::string_view reserved : kReservedNames) {
        if (id.name() == reserved) {
            throw EngineError("system name '" + id.name() + "' is reserved by the engine");
        }
    }
    for (const std::string_view reserved : kReservedSectionIds) {
        if (id.section_id() == reserved) {
            throw EngineError("system '" + id.name() + "' claims reserved section id '"
                              + id.section_id() + "'");
        }
    }
    // The world's sub-hash lives under the reserved `world` salt; a system that
    // landed on it would fold its state into the world's section and the two
    // would be indistinguishable afterwards.
    if (id.salt() == WORLD_SECTION_SALT) {
        throw EngineError("system '" + id.name() + "' collides with the reserved world salt "
                          + hex64(id.salt()));
    }

    for (const Registration& existing : systems_) {
        const SystemId& other = existing.system->id();
        if (other.name() == id.name()) {
            throw EngineError("duplicate system name '" + id.name() + "'");
        }
        // The salt is derived from the name, so distinct names normally imply
        // distinct salts. Checked anyway: mix64 is not injective by proof, and
        // a collision here would alias two systems' RNG streams silently.
        if (other.salt() == id.salt()) {
            throw EngineError("salt collision " + hex64(id.salt()) + " between systems '"
                              + other.name() + "' and '" + id.name() + "'");
        }
        if (other.section_id() == id.section_id()) {
            throw EngineError("section id '" + id.section_id() + "' claimed by both '"
                              + other.name() + "' and '" + id.name() + "'");
        }
    }

    systems_.push_back(
        Registration{std::move(system), CounterRandomSource(world_seed_, salt)});
}

void PhasedEngine::boot() {
    require_not_booted("cannot boot twice");
    order_.resize(systems_.size());
    for (std::size_t i = 0; i < order_.size(); ++i) {
        order_[i] = i;
    }
    // STABLE, and this is not a preference. Registration order must survive
    // inside a phase: two systems in the same phase run in the order they were
    // added, on both sides of the port, forever. Java's engine hand-writes an
    // insertion sort for exactly this reason. std::sort would be free to swap
    // equal keys and the first divergence would be a world hash, not a
    // compile error.
    std::stable_sort(order_.begin(), order_.end(), [this](std::size_t a, std::size_t b) {
        return systems_[a].system->phase() < systems_[b].system->phase();
    });
    booted_ = true;
}

const SimulationSystem& PhasedEngine::system_at(std::size_t executionIndex) const {
    if (!booted_) {
        throw EngineError("execution order is not fixed until boot()");
    }
    if (executionIndex >= order_.size()) {
        throw EngineError("no system at execution index " + std::to_string(executionIndex));
    }
    return *systems_[order_[executionIndex]].system;
}

void PhasedEngine::tick() {
    if (!booted_) {
        throw EngineError("boot() the engine before ticking it");
    }

    clock_.advance();
    const std::int64_t tick = clock_.current_tick();

    // Every source is rebound BEFORE any system runs, so no system can observe
    // another mid-advance. The rebind is also what makes a draw a pure function
    // of (seed, salt, tick, key, index) rather than of how far the tick got.
    for (Registration& registration : systems_) {
        registration.rng.begin_tick(static_cast<std::uint64_t>(tick));
    }

    // No world.beginTick()/commitTick() pair here. The Java's exists to advance
    // change-log epochs and publish per-chunk revisions for the renderer's
    // dirty tracking; content::World is pure storage with no revision counters,
    // and inventing them now -- with no renderer reading them -- would be
    // building the bookkeeping before the thing it books.

    std::size_t next = 0;
    for (std::size_t p = 0; p < TICK_PHASE_COUNT; ++p) {
        const auto phase = static_cast<TickPhase>(p);
        while (next < order_.size() && systems_[order_[next]].system->phase() == phase) {
            Registration& registration = systems_[order_[next]];
            const TickContext context(tick, phase, registration.rng, *world_);
            registration.system->tick(context);
            ++next;
        }
    }
}

void PhasedEngine::hash_into(WorldHasher& hasher) const {
    hasher.hash_world(*world_);
    // Registration order, not execution order. Either would give the same
    // combined hash -- sections fold in salt order regardless -- but a fixed
    // choice means a report that walks sections in feed order is stable too.
    for (const Registration& registration : systems_) {
        registration.system->hash_into(hasher.section_sink(registration.system->id()));
    }
}

std::uint64_t PhasedEngine::combined_hash() const {
    WorldHasher hasher;
    hash_into(hasher);
    return hasher.combined_hash();
}

}  // namespace granadad::sim
