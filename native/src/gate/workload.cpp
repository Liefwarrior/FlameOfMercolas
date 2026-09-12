#include "granadad/gate/workload.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
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
#include "granadad/sim/compound.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/engine.hpp"
#include "granadad/sim/engine_error.hpp"
#include "granadad/sim/notables.hpp"
#include "granadad/sim/fixed.hpp"
#include "granadad/sim/player.hpp"
#include "granadad/sim/rng.hpp"
#include "granadad/sim/tavern.hpp"
#include "granadad/sim/ward_actors.hpp"
#include "granadad/sim/world_hash.hpp"

namespace granadad::gate {
namespace {

using content::dec;
using content::hex64;
using content::padLeft;
using content::padRight;

/// A signed integer, formatted without libc for the same reason everything else
/// in this report is: the bytes are compared across two toolchains.
[[nodiscard]] std::string signed_dec(std::int32_t value) {
    if (value < 0) {
        return "-" + dec(static_cast<std::uint64_t>(-static_cast<std::int64_t>(value)));
    }
    return dec(static_cast<std::uint64_t>(value));
}

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
// tavern driver -- phase tick-begin
// ---------------------------------------------------------------------------
//
// The room runs on TWO clocks: it decides once a second and moves sixty times
// a second (see granadad/sim/actor.hpp). The client's loop drives both. The
// gate has only a tick, so the movement clock arrives as a system of its own,
// registered in the phase that runs BEFORE actors -- exactly the order
// Session::step uses, and for the same reason: everybody is where they are
// going to be before anybody decides anything about it.
//
// It hashes only its own tick count. The tavern's state is the tavern's to
// hash, and two systems folding the same numbers would make a divergence look
// like two divergences.

class TavernDriverSystem final : public sim::SimulationSystem {
public:
    explicit TavernDriverSystem(const sim::Tavern* tavern) noexcept
        : tavern_(const_cast<sim::Tavern*>(tavern)) {}

    [[nodiscard]] const sim::SystemId& id() const noexcept override { return id_; }
    [[nodiscard]] sim::TickPhase phase() const noexcept override {
        return sim::TickPhase::TickBegin;
    }

    void tick(const sim::TickContext& context) override {
        (void)context;
        for (std::int32_t step = 0; step < sim::kStepsPerSecond; ++step) {
            tavern_->stepMovement();
        }
        ++ticks_;

        // S5: THE WORKLOAD COMMITS CRIMES, and it does so because the S4 review
        // found that it did not. Faction state has been in the world hash since
        // S4 and the Tavern has been a registered system since S2 -- but
        // nothing in this workload ever moved a faction number, so the gate
        // compared zeros to zeros for every one of those rows and would have
        // gone green over a mirror ledger that had stopped working entirely.
        //
        // A hand in a purse every ninety seconds moves ALL of it through the
        // real path: the tally, the Watch's heat, the roofs' standing, the
        // garrison's by the mirror, the thief's own cracksmanship, the victim's
        // memory and the coin in two purses.
        if (ticks_ % 90 == 0) {
            if (tavern_->talkTo()) {
                const std::vector<sim::Topic>& topics = tavern_->dialogue().topics();
                for (std::size_t i = 0; i < topics.size(); ++i) {
                    if (topics[i].kind == sim::TopicKind::PickPocket) {
                        (void)tavern_->chooseTopic(i);
                        break;
                    }
                }
                tavern_->endConversation();
            }
        }
        // And a roof-run every four minutes. The gate has no body -- it is a
        // headless engine, not a session -- so this is REPORTED rather than
        // walked, which is the one thing in this file that reaches past the
        // player-facing verbs, and it is said out loud here rather than hidden.
        if (ticks_ % 240 == 0) {
            tavern_->dialogue().noteCrime(sim::Crime::RoofRun, false);
        }
        // S6: AND IT TAKES WORK. The contract board is hashed, and a board
        // nobody ever takes anything off is four Offered rows that never
        // change -- which is the same shape of hole S5 found in the faction
        // rows: perfectly deterministic, and proving nothing. Taking one moves
        // the state that a save would have to carry and that two runs have to
        // agree about.
        //
        // REPORTED rather than walked, like the roof-run above and for the same
        // reason: the gate is a headless engine with a player who never moves,
        // so it cannot cross a taproom to a broker. Said out loud here.
        if (ticks_ % 300 == 0) {
            sim::ContractBoard& board = tavern_->dialogue().contracts();
            for (const sim::Contract& row : board.contracts()) {
                if (row.state == sim::ContractState::Offered) {
                    (void)board.take(row.id);
                    break;
                }
            }
        }
        // RADIANT BUILD: AND IT TAKES AN ERRAND, for exactly the sentence the
        // contract block above wrote in S6 -- the radiant board is now in the
        // hash, and a board nobody ever takes anything off is four Offered
        // rows that never change: perfectly deterministic, proving nothing.
        // Populated only in a run with the district's people registered
        // (attachPeople in run_workload below); otherwise this loop walks an
        // empty vector and the tavern-only gate report stays what it was.
        //
        // REPORTED rather than walked, like everything above and for the same
        // reason. A different cadence than the contract take on purpose, so
        // the two boards' state moves out of step and a hash that conflated
        // them could not stay green by accident.
        if (ticks_ % 360 == 0) {
            sim::RadiantBoard& errands = tavern_->dialogue().radiant();
            for (const sim::RadiantObjective& row : errands.objectives()) {
                if (row.state == sim::RadiantState::Offered) {
                    (void)errands.take(row.id);
                    break;
                }
            }
        }
        // S8: AND IT LOSES A FIGHT, for exactly the reason S5 made it commit
        // crimes and S6 made it take work. The nemesis book is in the tavern's
        // hash; a workload nobody ever beats compares an empty book to an empty
        // book on every row of it, which is perfectly deterministic and proves
        // nothing at all.
        //
        // Losing one moves a rung on a real ladder, a faction's weight and its
        // rival's, an actor's weapon and intent, a memory in the social ledger
        // and -- for a gate run with the ward registered -- a charge on the
        // roll. All of it hashed, all of it compared.
        //
        // REPORTED rather than fought, like the roof-run above and for the same
        // reason: the gate is a headless engine with a player who never moves,
        // so it cannot pick a fight across a taproom. Tavern::concedeTo is the
        // same call the combat screen will make, and it is said out loud here.
        if (ticks_ % 420 == 0) {
            for (const sim::Actor& actor : tavern_->actors()) {
                if (actor.present() && actor.role() == sim::ActorRole::Patron) {
                    tavern_->concedeTo(actor.id());
                    tavern_->reviveAfterDefeat();
                    break;
                }
            }
        }
    }

    void hash_into(sim::HashSink& sink) const override {
        sink.put_long(static_cast<std::uint64_t>(ticks_));
    }

private:
    sim::SystemId id_ = sim::SystemId::of("tavern.movement", "TVMV");
    sim::Tavern* tavern_;
    std::int64_t ticks_ = 0;
};

// ---------------------------------------------------------------------------
// street assault -- phase tick-begin (STREET SENSES, the violence leg)
// ---------------------------------------------------------------------------
//
// THE NUMBER HAS TO PROVE THE BEHAVIOUR. The population workload had no player
// and called nothing that frightened anybody, so 9a's street panic -- built,
// hashed and re-blessed -- moved NOTHING in the one run the population baseline
// is taken from: the gate compared a district nobody ever startled to itself.
// The lane brief's own words: "a scripted street assault so the new number
// proves the new behaviour (a blow on the Tarwalk at 16:00 scatters the crowd
// within 8 tiles and LOS)."
//
// So this driver throws one. It runs in the SAME phase and the SAME shape as
// the tavern driver above -- TickBegin, before the Actors phase decides, so the
// crowd reacts to the fright the same tick -- and it does through the REAL
// verbs (setPlayer + alarm), never a back door: a fixed point on the Tarwalk,
// found once by the roster's own order, held for a window so the scatter is
// sustained and legible in the twin run. Draw-free and deterministic: the point
// is the first standing body on the quay by ascending id, and the window is
// absolute ticks, so run A and run B assault the same tile on the same ticks.
//
// It fires only for a run long enough to have settled first (the ward walks to
// its posts over the first minutes), which the population baseline's 7200 ticks
// always is; a short population run never reaches the window and is untouched.

class StreetAssaultDriver final : public sim::SimulationSystem {
public:
    explicit StreetAssaultDriver(const sim::WardPopulation* people) noexcept
        : people_(const_cast<sim::WardPopulation*>(people)) {}

    [[nodiscard]] const sim::SystemId& id() const noexcept override { return id_; }
    [[nodiscard]] sim::TickPhase phase() const noexcept override {
        return sim::TickPhase::TickBegin;
    }

    void tick(const sim::TickContext& context) override {
        ++ticks_;
        // STREET SENSES leg (b): the mailbox of blows thrown at the phantom
        // player is drained every tick -- the gate has no sheet to land them
        // on, and a mailbox nobody reads is the one thing this file must not
        // leave growing. Drained BEFORE the window check so a fight that
        // outlives the window is drained too. Leg (c): the Watch's mailbox the
        // same, and its ARRESTS COUNTED -- the gate has nobody to take, so the
        // arrest at reach is reported rather than served (the tavern driver's
        // own rule for the roof-run and the concession), said out loud here.
        (void)people_->takeStreetBlows();
        for (const sim::WatchEvent& event : people_->takeWatchEvents()) {
            if (event.kind == sim::WatchEventKind::Arrest) {
                arrests_ = sim::wrap_add(arrests_, 1);
                // TAKEN. Once the docker has been put down, the first arrest at
                // reach ends the assault: a man in custody throws no more
                // blows and frightens nobody, so the alarms stop and the street
                // is left to recover on the compared report.
                if (downedOnce_) {
                    arrested_ = true;
                }
            }
        }
        if (ticks_ < kFromTick || ticks_ > kToTick || arrested_) {
            return;
        }
        // The point, found ONCE and held: a person standing on the Tarwalk on
        // walking ground -- a docker where the day trades are -- and, leg (c),
        // one a WATCHMAN CAN SEE (kWatchSightTiles, same band, line of sight)
        // so the blow has a beat to answer it; failing that, the first docker
        // at all. Ascending id, deterministic and draw-free. Latched, so the
        // fright's origin stays where the blow landed while the crowd breaks
        // away from it (actFlee reads exactly that vector).
        if (!located_) {
            std::int32_t fallback = -1;
            for (const sim::WardActor& actor : people_->actors()) {
                if (!actor.visible() || !sim::isPerson(actor.type) ||
                    actor.type == sim::WardType::MilitiaWatch) {
                    continue;
                }
                if (actor.x < sim::wardplaces::kTarwalkX0 ||
                    actor.x > sim::wardplaces::kTarwalkX1 ||
                    actor.y < sim::wardplaces::kTarwalkY0 ||
                    actor.y > sim::wardplaces::kTarwalkY1) {
                    continue;
                }
                if (!people_->onWalkingGround(actor.x, actor.y, actor.band)) {
                    continue;
                }
                if (fallback < 0) {
                    fallback = actor.id;
                }
                if (people_->witnessesInSight(actor.x, actor.y, actor.band, sim::kWatchSightTiles,
                                              actor.id) > 0 &&
                    watchmanInSight(actor)) {
                    fallback = actor.id;
                    break;
                }
            }
            if (fallback >= 0) {
                const sim::WardActor& victim = *people_->byId(fallback);
                x_ = victim.x;
                y_ = victim.y;
                band_ = victim.band;
                victimId_ = victim.id;
                located_ = true;
            }
        }
        if (!located_) {
            return;
        }
        // THE BLOW ON THE TARWALK, re-asserted every tick of the window the way
        // the client re-asserts a raised blade once a second: the player stands
        // where the blow landed, and everyone who can SEE it (same band, in
        // range, line of sight -- the ward's own rule) is driven under the FLEE
        // gate. A BLOW is the widest of the three tiers a bar-fight punch would
        // never reach past the door; here it is a punch on the open quay.
        people_->setPlayer(x_, y_, band_);
        people_->alarm(x_, y_, band_, sim::alarmRadius(sim::AlarmSeverity::Blow),
                       sim::AlarmSeverity::Blow);

        // STREET SENSES leg (b): AND THE BLOW LANDS ON HIM. The docker the point
        // was found on takes a fist a tick -- the Gull's own strike() on his
        // sheet, the brawl class (Fists, Subdue: nobody dies), one draw per
        // blow on this driver's own salt (the gate's stand-in for the swing's
        // drawForPlayerAction, said out loud) -- until he goes down. Then he is
        // left alone, lies his kStreetFloorSeconds and STANDS UP at a quarter,
        // and the report shows downed= rise and fall. That is the number
        // proving "a docker struck goes down and gets up", not asserting it.
        if (!downedOnce_) {
            const sim::WardActor* victim = people_->byId(victimId_);
            if (victim != nullptr && victim->downedUntil >= 0) {
                downedOnce_ = true;
            } else if (victim != nullptr && victim->visible()) {
                sim::Fighter sheet;
                sheet.actorId = victimId_;
                sheet.weapon = sim::Weapon::Fists;
                sheet.intent = sim::Intent::Subdue;
                sheet.hp = victim->hp;
                sheet.hpMax = sim::kActorHealth;
                const std::uint64_t roll =
                    context.draw(static_cast<std::uint64_t>(victimId_), blows_);
                blows_ = sim::wrap_add(blows_, 1);
                const sim::Blow blow = sim::strike(sim::Weapon::Fists, sheet, roll);
                (void)people_->applyStreetBlow(victimId_, sheet.hp, blow, /*lethal=*/false);
            }
        }
    }

    void hash_into(sim::HashSink& sink) const override {
        // Its own tick, blow and arrest counts only: the fright, the floor and
        // the closing are the population's state to hash, and two systems
        // folding the same numbers would make one divergence look like two.
        // The tavern driver's own rule.
        sink.put_long(static_cast<std::uint64_t>(ticks_));
        sink.put_int(static_cast<std::uint32_t>(blows_));
        sink.put_int(static_cast<std::uint32_t>(arrests_));
    }

private:
    /// Whether a watchman can see this body: the street Watch's own three
    /// clauses (same band, kWatchSightTiles, a line), asked of the roll.
    [[nodiscard]] bool watchmanInSight(const sim::WardActor& body) const {
        for (const sim::WardActor& actor : people_->actors()) {
            if (actor.type != sim::WardType::MilitiaWatch || !actor.visible() ||
                actor.band != body.band) {
                continue;
            }
            if (std::max(std::abs(actor.x - body.x), std::abs(actor.y - body.y)) >
                sim::kWatchSightTiles) {
                continue;
            }
            return true;
        }
        return false;
    }

    /// 16:30 on a 16:00-start run: past the minutes the ward spends walking to
    /// its posts, so the crowd is on the Tarwalk to be scattered.
    static constexpr std::int64_t kFromTick = 1800;
    /// Six minutes of it -- longer than a BLOW's ~80 s panic, so the scatter is
    /// sustained across the window and the recovery is visible after it.
    static constexpr std::int64_t kToTick = 2160;

    sim::SystemId id_ = sim::SystemId::of("street.assault", "STAS");
    sim::WardPopulation* people_;
    std::int64_t ticks_ = 0;
    std::int32_t x_ = 0;
    std::int32_t y_ = 0;
    std::int32_t band_ = 0;
    bool located_ = false;
    /// STREET SENSES leg (b): the docker the point was found on, the fists
    /// thrown at him (the draw index), and whether he has been put down once.
    std::int32_t victimId_ = -1;
    std::int32_t blows_ = 0;
    bool downedOnce_ = false;
    /// STREET SENSES leg (c): the Watch's arrests at reach, counted since the
    /// gate has no sheet to land one on (hash_into's own note), and the
    /// assault's give-up once the docker taken has already gone down once
    /// (the tick() comment above this field's use).
    std::int32_t arrests_ = 0;
    bool arrested_ = false;
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

    const std::string world_name =
        (config.with_tavern || config.with_ward || config.with_population)
            ? std::string(sim::docks::kWorldName)
            : config.world;
    content::World world = content::loadWorldFile(content::bakedMap(world_name));

    // Declared before the engine so it outlives it: the Tavern borrows this
    // and the engine owns the Tavern. Destruction runs in reverse declaration
    // order, so the engine goes first and the query is still alive while it
    // does.
    std::unique_ptr<sim::TileQuery> tiles;
    if (config.with_tavern || config.with_population) {
        tiles = std::make_unique<sim::TileQuery>(world);
    }

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

    if (config.with_ward) {
        // THE COMPOUNDS. Registered before the tavern so registration order
        // inside the Actors phase stays a stated fact rather than an accident
        // of which flag was typed first.
        engine.register_system(std::make_unique<sim::Ward>(
            config.seed, content::contentDir(),
            sim::NotableRegistry::load(content::contentDir())));
    }

    // #78. THE PEOPLE, registered after the roll and before the taproom, so
    // registration order inside the Actors phase stays a stated fact rather
    // than an accident of which flag was typed first.
    const sim::WardPopulation* people_view = nullptr;
    if (config.with_population) {
        auto people = std::make_unique<sim::WardPopulation>(
            *tiles, config.population_start_second, config.seed, content::contentDir());
        people_view = people.get();
        engine.register_system(std::move(people));
        // STREET SENSES, the violence leg: the scripted assault, registered
        // right after the people it frightens. TickBegin, so it lands the fright
        // before the Actors phase decides that tick -- and it moves the number
        // the population baseline is taken from, which 9a's panic never did in a
        // player-less run. See StreetAssaultDriver.
        engine.register_system(std::make_unique<StreetAssaultDriver>(people_view));
    }

    const sim::Tavern* tavern_view = nullptr;
    if (config.with_tavern) {
        auto tavern = std::make_unique<sim::Tavern>(*tiles, sim::hourOfDay(19), config.seed,
                                                    content::contentDir());
        // A player standing at the bar, and never moving. Everything that
        // happens from here -- fourteen actors keeping their hours, walking
        // routes out of a breadth-first search, ordering drinks, a bouncer
        // deciding who to look at -- is state the twin-run gate now compares.
        tavern->setPlayer(sim::q8_tile_centre(sim::gull::kBartenderX),
                          sim::q8_tile_centre(sim::gull::kBarY - 1), sim::gull::kGroundBand);
        // RADIANT BUILD. A gate run carrying BOTH the taproom and the
        // district's people wires the two together exactly as the client
        // does, so the radiant board it hashes is a POPULATED one whose rows
        // the driver above moves. attachPeople(nullptr) is a stated no-op,
        // so the tavern-only gate keeps its empty board and its frozen shape.
        tavern->attachPeople(people_view);
        tavern_view = tavern.get();
        engine.register_system(std::move(tavern));
        engine.register_system(std::make_unique<TavernDriverSystem>(tavern_view));
    }
    engine.boot();

    std::string out;
    out += "granadad twin-run workload v1\n";
    out += "  world        " + world_name + "\n";
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
                   + "]";
            if (people_view != nullptr) {
                // PRINTED AS WELL AS HASHED. A report that SHOWS the ward
                // moving -- somebody going off shift, somebody going home,
                // somebody eating -- is worth more than an assertion that it is
                // compared, because a population that silently froze on its
                // first tick would hash identically twice and pass.
                out += "  " + people_view->reportLine();
            }
            if (tavern_view != nullptr) {
                const sim::DialogueDirector& talk = tavern_view->dialogue();
                const std::int32_t roofs = talk.factions().indexOf("skyrunners");
                const std::int32_t watch = talk.factions().indexOf("watch");
                out += "  gull[in=" +
                       dec(static_cast<std::uint64_t>(tavern_view->presentCount())) + " noise=" +
                       dec(static_cast<std::uint64_t>(tavern_view->noise())) + " stock=" +
                       dec(static_cast<std::uint64_t>(tavern_view->drinkStock())) + " clock=" +
                       dec(static_cast<std::uint64_t>(tavern_view->timeOfDay())) + "]";
                // S5: printed as well as hashed, so the report SHOWS the
                // faction and crime rows moving instead of asserting that they
                // are compared.
                out += "  work[day=" +
                       dec(static_cast<std::uint64_t>(talk.contracts().day())) + " open=" +
                       dec(static_cast<std::uint64_t>(talk.contracts().contracts().size())) +
                       " taken=" +
                       dec(static_cast<std::uint64_t>(talk.contracts().takenCount())) + "]";
                // RADIANT BUILD: printed as well as hashed, the standing rule
                // -- a report that SHOWS errands being posted and taken is
                // worth more than an assertion that they are compared. Only
                // in a run that actually wired the district in, so the
                // tavern-only report keeps its exact historical shape.
                if (people_view != nullptr) {
                    out += "  errands[day=" +
                           dec(static_cast<std::uint64_t>(talk.radiant().day())) + " open=" +
                           dec(static_cast<std::uint64_t>(talk.radiant().objectives().size())) +
                           " taken=" +
                           dec(static_cast<std::uint64_t>(talk.radiant().takenCount())) + "]";
                }
                out += "  roofs[lifts=" +
                       dec(static_cast<std::uint64_t>(talk.crimes().tally(sim::Crime::Lift))) +
                       " heat=" + dec(static_cast<std::uint64_t>(talk.crimes().heat())) +
                       " sky=" + signed_dec(talk.standings().standing(roofs)) +
                       " watch=" + signed_dec(talk.standings().standing(watch)) + "]";
                // S8: and who has been beating the player, printed for the same
                // reason -- a report that SHOWS the book moving is worth more
                // than an assertion that it is compared.
                const sim::Nemesis* worst = tavern_view->nemesis().worst();
                out += "  rival[";
                if (worst == nullptr) {
                    out += "none";
                } else {
                    out += worst->who + " x" +
                           dec(static_cast<std::uint64_t>(worst->wins)) + " rank" +
                           dec(static_cast<std::uint64_t>(worst->rank)) + " house" +
                           signed_dec(worst->chapter) + " plot" + signed_dec(worst->plot);
                }
                out += "]";
            }
            out += "\n";
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
