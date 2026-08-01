// The tick loop and system registration.
//
// Two things get most of the attention here, because they are the two that fail
// silently: the boot-time identity checks (a salt collision aliases two systems'
// RNG streams and nothing downstream ever notices) and phase ordering with
// registration order preserved inside a phase (std::sort would be free to swap
// equal keys and the first symptom would be a moved world hash).
//
// The systems in this file are stubs. They record what happened to them and
// nothing else, so a failure here is about the engine and never about a
// workload.

#include <doctest/doctest.h>

#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "golden_java_vectors.hpp"
#include "granadad/content/content_dir.hpp"
#include "granadad/content/world.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/sim/engine.hpp"
#include "granadad/sim/engine_error.hpp"
#include "granadad/sim/system_id.hpp"

using namespace granadad::sim;
namespace content = granadad::content;

namespace {

/// Records every tick it is given, and nothing else.
class RecordingSystem final : public SimulationSystem {
public:
    RecordingSystem(SystemId id, TickPhase phase, std::vector<std::string>* trace)
        : id_(std::move(id)), phase_(phase), trace_(trace) {}

    [[nodiscard]] const SystemId& id() const noexcept override { return id_; }
    [[nodiscard]] TickPhase phase() const noexcept override { return phase_; }

    void tick(const TickContext& context) override {
        if (trace_ != nullptr) {
            trace_->push_back(id_.name());
        }
        ++ticks_;
        last_tick_ = context.tick();
        last_phase_ = context.phase();
        last_draw_ = context.draw(0, 0);
    }

    void hash_into(HashSink& sink) const override {
        sink.put_long(static_cast<std::uint64_t>(ticks_));
        sink.put_long(static_cast<std::uint64_t>(last_tick_));
    }

    [[nodiscard]] std::int64_t ticks() const noexcept { return ticks_; }
    [[nodiscard]] std::int64_t last_tick() const noexcept { return last_tick_; }
    [[nodiscard]] TickPhase last_phase() const noexcept { return last_phase_; }
    [[nodiscard]] std::uint64_t last_draw() const noexcept { return last_draw_; }

private:
    SystemId id_;
    TickPhase phase_;
    std::vector<std::string>* trace_;
    std::int64_t ticks_ = 0;
    std::int64_t last_tick_ = 0;
    TickPhase last_phase_ = TickPhase::TickBegin;
    std::uint64_t last_draw_ = 0;
};

[[nodiscard]] content::World tavern() {
    return content::loadWorldFile(content::bakedMap("tavern_fixture"));
}

/// Looked up by name rather than by index: the golden table is emitted in
/// directory-sorted order and a fourth baked world would silently renumber it.
[[nodiscard]] const golden::WorldHashVector& golden_world(const char* name) {
    for (const golden::WorldHashVector& v : golden::kWorldHashes) {
        if (std::string(v.world) == name) {
            return v;
        }
    }
    FAIL("no golden world hash for " << name);
    return golden::kWorldHashes[0];
}

}  // namespace

TEST_CASE("section ids derive from names the way the JVM derives them") {
    for (const golden::SystemIdVector& v : golden::kSystemIds) {
        CAPTURE(v.name);
        const SystemId id = SystemId::of(v.name);
        CHECK(id.salt() == v.salt);
        CHECK(id.section_id() == v.sectionId);
        CHECK(id.name() == v.name);
    }
    // The padding and the skipping, stated directly.
    CHECK(derive_section_id("a") == "A___");
    CHECK(derive_section_id("input-gate") == "INPU");
    CHECK(derive_section_id("z9") == "Z9__");
    CHECK(derive_section_id("--!!") == "____");
}

TEST_CASE("an explicit section id overrides the derived one but not the salt") {
    const SystemId derived = SystemId::of("actors");
    const SystemId pinned = SystemId::of("actors", "ACTR");
    CHECK(derived.section_id() == "ACTO");
    CHECK(pinned.section_id() == "ACTR");
    // The salt follows the NAME, never the section id. This is what makes a
    // pinned section id a save-layout decision and not a determinism one.
    CHECK(derived.salt() == pinned.salt());
}

TEST_CASE("a malformed identity is refused at construction") {
    CHECK_THROWS_AS((void)SystemId::of(""), EngineError);
    CHECK_THROWS_AS((void)SystemId::of("actors", "ABC"), EngineError);
    CHECK_THROWS_AS((void)SystemId::of("actors", "ABCDE"), EngineError);
    CHECK_THROWS_AS((void)SystemId::of("actors", std::string("AB\x01") + "D"), EngineError);
}

TEST_CASE("the clock starts at zero and the first ticked tick is one") {
    content::World world = tavern();
    PhasedEngine engine(1, world);
    CHECK(engine.current_tick() == 0);
    engine.boot();
    engine.tick();
    CHECK(engine.current_tick() == 1);
    engine.tick();
    CHECK(engine.current_tick() == 2);
    CHECK(TickClock::MILLIS_PER_TICK == 1000);
}

TEST_CASE("phases run in ordinal order and registration order survives inside one") {
    content::World world = tavern();
    std::vector<std::string> trace;
    PhasedEngine engine(1, world);

    // Registered in an order that is neither phase order nor alphabetical, with
    // THREE systems sharing the actors phase so a non-stable sort has room to
    // reorder them.
    engine.register_system(
        std::make_unique<RecordingSystem>(SystemId::of("end-a"), TickPhase::TickEnd, &trace));
    engine.register_system(
        std::make_unique<RecordingSystem>(SystemId::of("mid-a"), TickPhase::Actors, &trace));
    engine.register_system(
        std::make_unique<RecordingSystem>(SystemId::of("begin-a"), TickPhase::TickBegin, &trace));
    engine.register_system(
        std::make_unique<RecordingSystem>(SystemId::of("mid-b"), TickPhase::Actors, &trace));
    engine.register_system(
        std::make_unique<RecordingSystem>(SystemId::of("mid-c"), TickPhase::Actors, &trace));
    engine.boot();
    engine.tick();

    const std::vector<std::string> expected = {"begin-a", "mid-a", "mid-b", "mid-c", "end-a"};
    CHECK(trace == expected);

    // And the same order is what system_at reports.
    REQUIRE(engine.system_count() == 5);
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CHECK(engine.system_at(i).id().name() == expected[i]);
    }
}

TEST_CASE("every system sees the same tick, its own phase, and its own stream") {
    content::World world = tavern();
    PhasedEngine engine(0xABCDEF, world);
    auto* begin = new RecordingSystem(SystemId::of("begin"), TickPhase::TickBegin, nullptr);
    auto* mid = new RecordingSystem(SystemId::of("mid"), TickPhase::Actors, nullptr);
    engine.register_system(std::unique_ptr<SimulationSystem>(begin));
    engine.register_system(std::unique_ptr<SimulationSystem>(mid));
    engine.boot();
    engine.tick();
    engine.tick();

    CHECK(begin->ticks() == 2);
    CHECK(mid->ticks() == 2);
    CHECK(begin->last_tick() == 2);
    CHECK(mid->last_tick() == 2);
    CHECK(begin->last_phase() == TickPhase::TickBegin);
    CHECK(mid->last_phase() == TickPhase::Actors);

    // Two systems drawing the same (key, index) at the same tick must get
    // different numbers, because their salts differ. If they matched, the two
    // systems would be sharing one stream and every decision either made would
    // be correlated with the other's forever.
    CHECK(begin->last_draw() != mid->last_draw());
    CHECK(begin->last_draw()
          == derive_draw(0xABCDEF, 2, system_salt("begin"), 0, 0));
    CHECK(mid->last_draw() == derive_draw(0xABCDEF, 2, system_salt("mid"), 0, 0));
}

TEST_CASE("boot refuses a colliding identity") {
    content::World world = tavern();

    SUBCASE("a duplicate name") {
        PhasedEngine engine(1, world);
        engine.register_system(
            std::make_unique<RecordingSystem>(SystemId::of("drift"), TickPhase::Actors, nullptr));
        CHECK_THROWS_AS(engine.register_system(std::make_unique<RecordingSystem>(
                            SystemId::of("drift"), TickPhase::Actors, nullptr)),
                        EngineError);
    }

    SUBCASE("a section id claimed twice") {
        // Different names, same four leading characters -- which is how a
        // section-id collision actually happens in practice.
        PhasedEngine engine(1, world);
        engine.register_system(std::make_unique<RecordingSystem>(SystemId::of("actors"),
                                                                 TickPhase::Actors, nullptr));
        CHECK_THROWS_AS(engine.register_system(std::make_unique<RecordingSystem>(
                            SystemId::of("actorsports"), TickPhase::Actors, nullptr)),
                        EngineError);
    }

    SUBCASE("a reserved name") {
        PhasedEngine engine(1, world);
        CHECK_THROWS_AS(engine.register_system(std::make_unique<RecordingSystem>(
                            SystemId::of("world"), TickPhase::Actors, nullptr)),
                        EngineError);
        CHECK_THROWS_AS(engine.register_system(std::make_unique<RecordingSystem>(
                            SystemId::of("input-gate"), TickPhase::TickBegin, nullptr)),
                        EngineError);
    }

    SUBCASE("a reserved section id") {
        PhasedEngine engine(1, world);
        CHECK_THROWS_AS(engine.register_system(std::make_unique<RecordingSystem>(
                            SystemId::of("terrain", "WRLD"), TickPhase::Actors, nullptr)),
                        EngineError);
        CHECK_THROWS_AS(engine.register_system(std::make_unique<RecordingSystem>(
                            SystemId::of("metadata", "META"), TickPhase::Actors, nullptr)),
                        EngineError);
    }

    SUBCASE("a null system") {
        PhasedEngine engine(1, world);
        CHECK_THROWS_AS(engine.register_system(nullptr), EngineError);
    }
}

TEST_CASE("registration and boot are one-shot") {
    content::World world = tavern();
    PhasedEngine engine(1, world);
    CHECK_FALSE(engine.booted());
    CHECK_THROWS_AS((void)engine.system_at(0), EngineError);
    CHECK_THROWS_AS(engine.tick(), EngineError);
    engine.boot();
    CHECK(engine.booted());
    CHECK_THROWS_AS(engine.boot(), EngineError);
    CHECK_THROWS_AS(engine.register_system(std::make_unique<RecordingSystem>(
                        SystemId::of("late"), TickPhase::Actors, nullptr)),
                    EngineError);
}

TEST_CASE("an engine with no systems still ticks and still hashes the world") {
    content::World world = tavern();
    PhasedEngine engine(1, world);
    engine.boot();
    engine.tick();
    WorldHasher hasher;
    engine.hash_into(hasher);
    CHECK(hasher.section_count() == 1);
    CHECK(hasher.section_hash(WORLD_SECTION_SALT) == golden_world("tavern_fixture").wrldSectionHash);
    CHECK(engine.combined_hash() == golden_world("tavern_fixture").combinedHash);
}

TEST_CASE("the combined hash moves when a system's state moves") {
    content::World world = tavern();
    PhasedEngine engine(1, world);
    engine.register_system(
        std::make_unique<RecordingSystem>(SystemId::of("counter"), TickPhase::Actors, nullptr));
    engine.boot();

    engine.tick();
    const std::uint64_t after_one = engine.combined_hash();
    engine.tick();
    const std::uint64_t after_two = engine.combined_hash();
    CHECK(after_one != after_two);

    // And the world's own section did not move, because nothing wrote a lane.
    WorldHasher hasher;
    engine.hash_into(hasher);
    CHECK(hasher.section_hash(WORLD_SECTION_SALT) == golden_world("tavern_fixture").wrldSectionHash);
}

TEST_CASE("two engines on the same seed reach the same hash") {
    content::World world_a = tavern();
    content::World world_b = tavern();
    PhasedEngine a(0x1234, world_a);
    PhasedEngine b(0x1234, world_b);
    a.register_system(
        std::make_unique<RecordingSystem>(SystemId::of("counter"), TickPhase::Actors, nullptr));
    b.register_system(
        std::make_unique<RecordingSystem>(SystemId::of("counter"), TickPhase::Actors, nullptr));
    a.boot();
    b.boot();
    for (int i = 0; i < 10; ++i) {
        a.tick();
        b.tick();
    }
    CHECK(a.combined_hash() == b.combined_hash());

    // A different seed must not.
    content::World world_c = tavern();
    PhasedEngine c(0x1235, world_c);
    c.register_system(
        std::make_unique<RecordingSystem>(SystemId::of("counter"), TickPhase::Actors, nullptr));
    c.boot();
    for (int i = 0; i < 10; ++i) {
        c.tick();
    }
    // The RecordingSystem hashes only tick counts, which do not depend on the
    // seed -- so this must be EQUAL, and saying so is the honest version of the
    // assertion. The seed's effect is proven where draws are actually kept:
    // test_rng.cpp and the twin-gate workload.
    CHECK(a.combined_hash() == c.combined_hash());
}

TEST_CASE("the clock refuses to go negative") {
    TickClock clock;
    CHECK(clock.current_tick() == 0);
    clock.advance();
    clock.advance();
    CHECK(clock.current_tick() == 2);
    CHECK(clock.simulated_millis() == 2000);
    clock.reset_to(400);
    CHECK(clock.current_tick() == 400);
    CHECK_THROWS_AS(clock.reset_to(-1), EngineError);
    CHECK(clock.current_tick() == 400);
}

TEST_CASE("phase names cover every phase") {
    CHECK(tick_phase_name(TickPhase::TickBegin) == "tick-begin");
    CHECK(tick_phase_name(TickPhase::Actors) == "actors");
    CHECK(tick_phase_name(TickPhase::TickEnd) == "tick-end");
    CHECK(TICK_PHASE_COUNT == 3);
    for (std::size_t p = 0; p < TICK_PHASE_COUNT; ++p) {
        CHECK(tick_phase_name(static_cast<TickPhase>(p)) != "?");
    }
}
