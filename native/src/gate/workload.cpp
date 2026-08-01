#include "granadad/gate/workload.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "granadad/content/ascii.hpp"
#include "granadad/content/content_dir.hpp"
#include "granadad/content/coords.hpp"
#include "granadad/content/lanes.hpp"
#include "granadad/content/world.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/sim/engine.hpp"
#include "granadad/sim/engine_error.hpp"
#include "granadad/sim/fixed.hpp"
#include "granadad/sim/rng.hpp"
#include "granadad/sim/world_hash.hpp"

namespace granadad::gate {
namespace {

using content::dec;
using content::hex64;
using content::padLeft;
using content::padRight;

/// The global tile index of a packed world position.
[[nodiscard]] std::size_t tile_of(const content::Coords& coords, std::int32_t pos) noexcept {
    return content::World::tileIndex(static_cast<std::size_t>(coords.chunkIndex(pos)),
                                     static_cast<std::size_t>(content::Coords::localIdx(pos)));
}

/// Whether a tile can be stood on: a real floor or open air in a chunk that is
/// not part of the immutable VOID border ring.
[[nodiscard]] bool walkable(const content::World& world, std::int32_t pos) {
    const std::int32_t chunk = world.coords().chunkIndex(pos);
    if (world.coords().isVoidBorder(chunk)) {
        return false;
    }
    const content::TileForm form = world.form(tile_of(world.coords(), pos));
    return form == content::TileForm::Floor || form == content::TileForm::Open;
}

// The eight compass steps, in a fixed order that is part of the hash.
constexpr std::array<std::int32_t, 8> kStepX = {1, 1, 0, -1, -1, -1, 0, 1};
constexpr std::array<std::int32_t, 8> kStepY = {0, 1, 1, 1, 0, -1, -1, -1};

// ---------------------------------------------------------------------------
// heartbeat -- phase tick-begin
// ---------------------------------------------------------------------------

class HeartbeatSystem final : public sim::SimulationSystem {
public:
    [[nodiscard]] const sim::SystemId& id() const noexcept override { return id_; }
    [[nodiscard]] sim::TickPhase phase() const noexcept override {
        return sim::TickPhase::TickBegin;
    }

    void tick(const sim::TickContext& context) override {
        ++ticks_;
        last_tick_ = context.tick();
        // Deliberately draws nothing. A system that consumes no randomness must
        // still be able to hold state and still be hashed, and the Java's only
        // other production system is exactly this shape.
    }

    void hash_into(sim::HashSink& sink) const override {
        sink.put_long(static_cast<std::uint64_t>(ticks_));
        sink.put_long(static_cast<std::uint64_t>(last_tick_));
    }

    [[nodiscard]] std::int64_t ticks() const noexcept { return ticks_; }

private:
    sim::SystemId id_ = sim::SystemId::of("heartbeat");
    std::int64_t ticks_ = 0;
    std::int64_t last_tick_ = 0;
};

// ---------------------------------------------------------------------------
// drift -- phase actors
// ---------------------------------------------------------------------------

class DriftSystem final : public sim::SimulationSystem {
public:
    struct Walker {
        std::int32_t cell = 0;
        std::int32_t moves = 0;
        std::int32_t blocked = 0;
        std::int32_t chatter = 0;
    };

    DriftSystem(const content::World& world, std::int32_t count) {
        seed_walkers(world, count);
        draw_counters_.assign(walkers_.size(), 0);
    }

    [[nodiscard]] const sim::SystemId& id() const noexcept override { return id_; }
    [[nodiscard]] sim::TickPhase phase() const noexcept override { return sim::TickPhase::Actors; }

    void tick(const sim::TickContext& context) override {
        // THE DRAW SCHEDULE, and the one rule that governs it: draws are
        // APPENDED, never inserted.
        //
        // drawIndex is a per-walker, per-tick counter shared across every
        // stream and reset to zero at the top of every tick. Every value
        // depends on its index, so adding a new draw BEFORE an existing one --
        // or adding an early return that skips one that used to always
        // happen -- shifts every later index by one for that walker and
        // re-phases the whole simulation from that tick forward. A new draw
        // goes after every existing draw on every path that can reach it.
        //
        // The established idiom for "I must not draw here but must keep phase"
        // is a discarded draw: consume the index, ignore the value.
        for (std::size_t i = 0; i < draw_counters_.size(); ++i) {
            draw_counters_[i] = 0;
        }

        content::World& world = context.world();
        for (std::size_t i = 0; i < walkers_.size(); ++i) {
            Walker& walker = walkers_[i];
            const auto key = static_cast<std::uint64_t>(i);

            // Draw 0: which way.
            const std::uint64_t heading = context.draw(key, next_draw_index(i));
            const std::size_t dir = static_cast<std::size_t>(heading % 8u);
            const std::int32_t target =
                content::packed_pos::step(walker.cell, kStepX[dir], kStepY[dir], 0);
            if (walkable(world, target)) {
                walker.cell = target;
                ++walker.moves;
            } else {
                ++walker.blocked;
            }

            // Draw 1: whether this walker says something. Unconditional, so it
            // cannot re-phase depending on whether draw 0 let it move.
            const std::uint64_t chatter = context.draw(key, next_draw_index(i));
            if (sim::passes(chatter, 250)) {
                ++walker.chatter;
            }
        }
        ++ticks_;
    }

    void hash_into(sim::HashSink& sink) const override {
        // Dense index order, which is also ascending walker id. No container
        // here has an iteration order that could be an implementation detail.
        sink.put_int(static_cast<std::uint32_t>(walkers_.size()));
        for (const Walker& walker : walkers_) {
            sink.put_int(static_cast<std::uint32_t>(walker.cell));
            sink.put_int(static_cast<std::uint32_t>(walker.moves));
            sink.put_int(static_cast<std::uint32_t>(walker.blocked));
            sink.put_int(static_cast<std::uint32_t>(walker.chatter));
        }
        sink.put_long(static_cast<std::uint64_t>(ticks_));
    }

    [[nodiscard]] std::size_t count() const noexcept { return walkers_.size(); }

    // Report-only aggregates. They are recomputed rather than maintained, so a
    // bug in the accumulator cannot make the report agree with itself while
    // disagreeing with the state that gets hashed.
    [[nodiscard]] std::int64_t moves() const {
        std::int64_t sum = 0;
        for (const Walker& walker : walkers_) {
            sum += walker.moves;
        }
        return sum;
    }

    [[nodiscard]] std::int64_t blocked() const {
        std::int64_t sum = 0;
        for (const Walker& walker : walkers_) {
            sum += walker.blocked;
        }
        return sum;
    }

    [[nodiscard]] std::int64_t chatter() const {
        std::int64_t sum = 0;
        for (const Walker& walker : walkers_) {
            sum += walker.chatter;
        }
        return sum;
    }

private:

    /// Post-increment, exactly like the Java: returns the index this draw uses
    /// and leaves the counter pointing at the next one.
    [[nodiscard]] std::int32_t next_draw_index(std::size_t walker) noexcept {
        return draw_counters_[walker]++;
    }

    /// Places walkers on the world's own walkable tiles, spread evenly through
    /// them in ascending tile order. Content-derived and content-ordered: a
    /// world that decoded differently puts them somewhere else, which is half
    /// the reason this workload is worth running on two toolchains.
    void seed_walkers(const content::World& world, std::int32_t count) {
        if (count <= 0) {
            throw sim::EngineError("the workload needs at least one walker");
        }
        const content::Coords& coords = world.coords();
        std::vector<std::int32_t> candidates;
        const std::size_t chunk_count = world.chunkCount();
        for (std::size_t chunk = 0; chunk < chunk_count; ++chunk) {
            if (coords.isVoidBorder(static_cast<std::int32_t>(chunk))) {
                continue;
            }
            for (std::int32_t local = 0; local < content::kTilesPerChunk; ++local) {
                const std::size_t tile = content::World::tileIndex(
                    chunk, static_cast<std::size_t>(local));
                const content::TileForm form = world.form(tile);
                if (form == content::TileForm::Floor || form == content::TileForm::Open) {
                    candidates.push_back(
                        coords.packedPos(static_cast<std::int32_t>(chunk), local));
                }
            }
        }
        if (candidates.empty()) {
            throw sim::EngineError("no walkable tile in this world; the workload has "
                                   "nothing to stand on");
        }
        const std::size_t wanted = static_cast<std::size_t>(count);
        const std::size_t stride = candidates.size() >= wanted ? candidates.size() / wanted : 1;
        walkers_.reserve(wanted);
        for (std::size_t k = 0; k < wanted; ++k) {
            walkers_.push_back(Walker{candidates[(k * stride) % candidates.size()], 0, 0, 0});
        }
    }

    sim::SystemId id_ = sim::SystemId::of("drift", "DRFT");
    std::vector<Walker> walkers_;
    std::vector<std::int32_t> draw_counters_;
    std::int64_t ticks_ = 0;
};

// ---------------------------------------------------------------------------
// ledger -- phase tick-end
// ---------------------------------------------------------------------------

class LedgerSystem final : public sim::SimulationSystem {
public:
    LedgerSystem() {
        for (const char* name : kAccounts) {
            balances_[name] = 0;
        }
    }

    [[nodiscard]] const sim::SystemId& id() const noexcept override { return id_; }
    [[nodiscard]] sim::TickPhase phase() const noexcept override { return sim::TickPhase::TickEnd; }

    void tick(const sim::TickContext& context) override {
        std::int32_t index = 0;
        for (auto& entry : balances_) {
            // Keyed by the account's POSITION in sorted order, so the draw a
            // given account gets is a function of the sorted set and not of
            // anybody's hash function.
            const std::uint64_t draw = context.draw(static_cast<std::uint64_t>(index), 0);
            const std::int64_t delta = static_cast<std::int64_t>(draw % 97u) - 48;
            entry.second = sim::wrap_add(entry.second, delta);
            ++index;
        }
    }

    void hash_into(sim::HashSink& sink) const override {
        // std::map, so iteration is sorted by key and the hash cannot depend on
        // insertion order or on a hash function.
        //
        // Worth being precise about what the twin-run gate can and cannot see
        // here: swapping this for std::unordered_map would NOT make the gate go
        // red. Two runs in one process insert the same keys in the same order
        // and get the same bucket layout, so the iteration order agrees with
        // itself. The ban on unordered containers is enforced by the docker
        // build's grep, not by this gate. See docker/build.Dockerfile.
        sink.put_int(static_cast<std::uint32_t>(balances_.size()));
        for (const auto& entry : balances_) {
            sink.put_int(static_cast<std::uint32_t>(entry.first.size()));
            for (const char c : entry.first) {
                sink.put_byte(static_cast<std::uint32_t>(static_cast<unsigned char>(c)));
            }
            sink.put_long(static_cast<std::uint64_t>(entry.second));
        }
    }

    [[nodiscard]] std::string render() const {
        std::string out;
        for (const auto& entry : balances_) {
            if (!out.empty()) {
                out += ",";
            }
            out += entry.first + "=" + dec(entry.second);
        }
        return out;
    }

private:
    static constexpr const char* kAccounts[] = {"docks", "granary", "quay", "watch", "wharf"};

    sim::SystemId id_ = sim::SystemId::of("ledger", "LEDG");
    std::map<std::string, std::int64_t> balances_;
};

}  // namespace

RunResult run_workload(const WorkloadConfig& config) {
    if (config.sample_every < 1) {
        throw sim::EngineError("sample_every must be at least 1");
    }

    content::World world = content::loadWorldFile(content::bakedMap(config.world));

    auto heartbeat = std::make_unique<HeartbeatSystem>();
    auto drift = std::make_unique<DriftSystem>(world, config.walkers);
    auto ledger = std::make_unique<LedgerSystem>();
    const HeartbeatSystem* heartbeat_view = heartbeat.get();
    const DriftSystem* drift_view = drift.get();
    const LedgerSystem* ledger_view = ledger.get();

    sim::PhasedEngine engine(config.seed, world);
    // Registered out of phase order on purpose: the engine sorts by phase and
    // must preserve registration order inside a phase, so registering in the
    // order they run would make a sorting bug invisible.
    engine.register_system(std::move(ledger));
    engine.register_system(std::move(drift));
    engine.register_system(std::move(heartbeat));
    engine.boot();

    std::string out;
    out += "granadad twin-run workload v1\n";
    out += "  world        " + config.world + "\n";
    out += "  seed         " + hex64(config.seed) + "\n";
    out += "  ticks        " + dec(config.ticks) + "\n";
    out += "  walkers      " + dec(static_cast<std::uint64_t>(drift_view->count())) + "\n";
    out += "  chunks       " + dec(static_cast<std::uint64_t>(world.chunkCount())) + "\n";
    out += "  tiles        " + dec(static_cast<std::uint64_t>(world.tileCount())) + "\n";
    out += "  lanes        " + dec(static_cast<std::uint64_t>(world.lanes().count())) + "\n";
    out += "  systems      " + dec(static_cast<std::uint64_t>(engine.system_count())) + "\n";
    for (std::size_t i = 0; i < engine.system_count(); ++i) {
        const sim::SimulationSystem& system = engine.system_at(i);
        out += "  system " + dec(static_cast<std::uint64_t>(i)) + "  "
               + padRight(std::string(sim::tick_phase_name(system.phase())), 11) + " "
               + padRight(system.id().name(), 10) + " " + system.id().section_id() + " "
               + hex64(system.id().salt()) + "\n";
    }

    out += "samples\n";
    for (std::int64_t t = 0; t < config.ticks; ++t) {
        engine.tick();
        const std::int64_t tick = engine.current_tick();
        if (tick % config.sample_every == 0 || t + 1 == config.ticks) {
            out += "  t=" + padLeft(dec(tick), 7) + "  moved=" + padLeft(dec(drift_view->moves()), 8)
                   + "  blocked=" + padLeft(dec(drift_view->blocked()), 8) + "  chatter="
                   + padLeft(dec(drift_view->chatter()), 8) + "  ledger[" + ledger_view->render()
                   + "]\n";
        }
    }

    sim::WorldHasher hasher;
    engine.hash_into(hasher);

    RunResult result;
    result.world_hash = hasher.section_hash(sim::WORLD_SECTION_SALT);
    result.combined_hash = hasher.combined_hash();

    out += "sections\n";
    out += "  WRLD  " + hex64(result.world_hash) + "\n";
    for (std::size_t i = 0; i < engine.system_count(); ++i) {
        const sim::SimulationSystem& system = engine.system_at(i);
        out += "  " + system.id().section_id() + "  " + hex64(hasher.section_hash(system.id()))
               + "\n";
    }
    out += "  heartbeat ticks " + dec(heartbeat_view->ticks()) + "\n";
    out += std::string(kCombinedHashTag) + content::hexDigits(result.combined_hash, 16) + "\n";

    result.report = std::move(out);
    return result;
}

}  // namespace granadad::gate
