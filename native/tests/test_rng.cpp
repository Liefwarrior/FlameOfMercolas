// The RNG, asserted against a JVM.
//
// Every expectation in this file comes from golden_java_vectors.hpp, which was
// written by a Java program that imported com.trojia.sim.random.RandomSource,
// CounterRandomSource, actor.NamedDraws and actor.ActorRngStream and ran them.
// So these are not "the C++ agrees with my reading of the Java" -- they are
// "the C++ agrees with the Java".
//
// The cases that are NOT table-driven are here for the properties a table
// cannot state: call-order independence, tick rebinding, and the fact that a
// fresh source reproduces an old one (which is the save/load property, since
// the world seed is the only persisted RNG state).

#include <doctest/doctest.h>

#include <cstdint>
#include <iterator>
#include <set>
#include <vector>

#include "golden_java_vectors.hpp"
#include "granadad/sim/rng.hpp"

using namespace granadad::sim;

TEST_CASE("mix64 reproduces the JVM for every pinned input") {
    for (const golden::Mix64Vector& v : golden::kMix64) {
        CAPTURE(v.input);
        CHECK(mix64(v.input) == v.output);
    }
    // mix64(0) == 0 is a real property of SplitMix64's finalizer, not a bug and
    // not a placeholder row somebody forgot to fill in. Said out loud here so
    // the next reader does not "fix" it.
    CHECK(mix64(0) == 0);
}

TEST_CASE("mix64 is constexpr, so a salt costs nothing at run time") {
    static_assert(mix64(1) == 0x5692161D100B05E5ull);
    static_assert(system_salt("actors") == 0x10AC023B151D819Full);
    static_assert(stream_salt("actor.wander") == 0x20214C5ABA354735ull);
    CHECK(true);
}

TEST_CASE("system salts reproduce the JVM, name by name") {
    for (const golden::SystemIdVector& v : golden::kSystemIds) {
        CAPTURE(v.name);
        CHECK(system_salt(v.name) == v.salt);
    }
}

TEST_CASE("all 23 actor stream salts reproduce the JVM") {
    // The count is asserted so a stream vanishing from the golden header is a
    // failure rather than a quietly smaller loop.
    CHECK(std::size(golden::kStreamSalts) == 23);
    for (const golden::StreamSaltVector& v : golden::kStreamSalts) {
        CAPTURE(v.name);
        CHECK(stream_salt(v.name) == v.salt);
    }
}

TEST_CASE("stream salts are distinct, and distinct from the system salts") {
    // A collision would silently alias two streams into one sequence. Cheap to
    // check, and the engine's boot-time check only covers system salts.
    std::set<std::uint64_t> seen;
    for (const golden::StreamSaltVector& v : golden::kStreamSalts) {
        CHECK(seen.insert(v.salt).second);
    }
    for (const golden::SystemIdVector& v : golden::kSystemIds) {
        CHECK(seen.count(v.salt) == 0);
    }
}

TEST_CASE("the two fold seeds are different, so a name cannot mean both things") {
    // "actors" as a system and "actors" as a stream must not be the same salt.
    CHECK(system_salt("actors") != stream_salt("actors"));
    CHECK(SYSTEM_SALT_SEED != STREAM_SALT_SEED);
}

TEST_CASE("the draw chain reproduces the JVM, sign extension included") {
    for (const golden::DrawVector& v : golden::kCounterDraws) {
        CAPTURE(v.worldSeed);
        CAPTURE(v.tick);
        CAPTURE(v.drawIndex);
        CHECK(derive_draw(v.worldSeed, v.tick, v.systemSalt, v.spatialKey, v.drawIndex)
              == v.value);

        // The cached-prefix implementation must agree with the flat one. These
        // are two different code paths in Java too, and the whole point of the
        // cache is that it changes nothing.
        CounterRandomSource rng(v.worldSeed, v.systemSalt);
        rng.begin_tick(v.tick);
        CHECK(rng.draw(v.spatialKey, v.drawIndex) == v.value);
    }
}

TEST_CASE("named draws reproduce the JVM") {
    for (const golden::NamedDrawVector& v : golden::kNamedDraws) {
        CAPTURE(v.stream);
        CAPTURE(v.actorId);
        CAPTURE(v.drawIndex);
        // NamedDraws keys on the actorId as the spatial key, and the actorId is
        // a Java int sign-extended into the add -- so the cast has to go
        // through int64, not straight to uint64.
        const std::uint64_t key = static_cast<std::uint64_t>(static_cast<std::int64_t>(v.actorId));
        CHECK(derive_draw(v.worldSeed, v.tick, stream_salt(v.stream), key, v.drawIndex)
              == v.value);
    }
}

TEST_CASE("a draw is a pure function of its tuple, not of call order") {
    // The property the whole design exists for: systems may draw in any order,
    // interleaved, and every value is the one it would have been alone.
    CounterRandomSource a(0xDEADBEEFull, system_salt("actors"));
    CounterRandomSource b(0xDEADBEEFull, system_salt("actors"));
    a.begin_tick(97);
    b.begin_tick(97);

    std::vector<std::uint64_t> forward;
    for (std::int32_t i = 0; i < 16; ++i) {
        forward.push_back(a.draw(static_cast<std::uint64_t>(i), i));
    }
    for (std::int32_t i = 15; i >= 0; --i) {
        CHECK(b.draw(static_cast<std::uint64_t>(i), i)
              == forward[static_cast<std::size_t>(i)]);
    }
}

TEST_CASE("rebinding to an earlier tick reproduces that tick exactly") {
    // This is the save/load property stated as a test. The world seed is the
    // only persisted RNG state precisely because this holds.
    CounterRandomSource rng(12345, system_salt("world"));
    rng.begin_tick(400);
    const std::uint64_t at400 = rng.draw(8191, 3);
    rng.begin_tick(15000);
    const std::uint64_t at15000 = rng.draw(8191, 3);
    rng.begin_tick(400);
    CHECK(rng.draw(8191, 3) == at400);
    CHECK(at400 != at15000);

    CounterRandomSource fresh(12345, system_salt("world"));
    fresh.begin_tick(400);
    CHECK(fresh.draw(8191, 3) == at400);
}

TEST_CASE("a source constructed and never rebound is at tick 0") {
    CounterRandomSource fresh(7, 9);
    CounterRandomSource bound(7, 9);
    bound.begin_tick(0);
    CHECK(fresh.draw(1, 0) == bound.draw(1, 0));
    CHECK(fresh.draw(1) == fresh.draw(1, 0));
}

TEST_CASE("neighbouring tuples do not produce neighbouring draws") {
    // Not a proof of quality, but it catches the class of port bug where a
    // whole mixing step got dropped and adjacent keys come back adjacent.
    CounterRandomSource rng(1, system_salt("actors"));
    rng.begin_tick(1);
    std::set<std::uint64_t> values;
    for (std::int32_t i = 0; i < 256; ++i) {
        values.insert(rng.draw(static_cast<std::uint64_t>(i), 0));
    }
    CHECK(values.size() == 256);
    CHECK(rng.draw(0, 0) != rng.draw(0, 1));
    CHECK(rng.draw(0, 0) != rng.draw(1, 0));
}

TEST_CASE("unsigned remainder reproduces Long.remainderUnsigned") {
    for (const golden::RemainderVector& v : golden::kUnsignedRemainders) {
        CAPTURE(v.dividend);
        CAPTURE(v.divisor);
        CHECK(v.dividend % v.divisor == v.remainder);
    }
}

TEST_CASE("passes uses the unsigned remainder, so a high-bit draw is not a free pass") {
    // A signed % here returns a NEGATIVE remainder for every draw with the top
    // bit set -- which is half of them -- and `< permille` is then true for all
    // of those regardless of the threshold. This is the assertion that catches
    // it: a 0-permille check must never pass, not even on 0xFFFF...
    CHECK_FALSE(passes(0xFFFFFFFFFFFFFFFFull, 0));
    CHECK_FALSE(passes(0x8000000000000000ull, 0));
    CHECK(passes(0xFFFFFFFFFFFFFFFFull, 1000));

    // 0xFFFFFFFFFFFFFFFF mod 1000 == 615 (pinned in kUnsignedRemainders).
    CHECK_FALSE(passes(0xFFFFFFFFFFFFFFFFull, 615));
    CHECK(passes(0xFFFFFFFFFFFFFFFFull, 616));

    // The boundaries of the scale.
    CHECK_FALSE(passes(0, 0));
    CHECK(passes(0, 1));
    CHECK(passes(999, 1000));
    CHECK_FALSE(passes(1000, 0));

    // Over the whole permille range, the pass rate of a fixed draw is monotone.
    bool passed = false;
    for (std::int32_t p = 0; p <= 1000; ++p) {
        const bool now = passes(0x0123456789ABCDEFull, p);
        // Once it passes it must keep passing: a higher threshold can never
        // reject a draw a lower one accepted. Written as one bool because
        // doctest refuses to decompose && inside a CHECK.
        const bool regressed = passed && !now;
        CHECK_FALSE(regressed);
        passed = now;
    }
    CHECK(passed);
}

TEST_CASE("weighted_pick lands in every bucket in proportion") {
    static constexpr std::int32_t kWeights[] = {1, 2, 3, 4};
    std::vector<int> hits(4, 0);
    for (std::uint64_t i = 0; i < 10000; ++i) {
        const std::size_t pick = weighted_pick(i, kWeights, 4);
        REQUIRE(pick < 4);
        hits[pick]++;
    }
    // Weights 1:2:3:4 out of 10 slots, and the draws are 0..9999 so the slots
    // are hit exactly evenly: 1000/2000/3000/4000.
    CHECK(hits[0] == 1000);
    CHECK(hits[1] == 2000);
    CHECK(hits[2] == 3000);
    CHECK(hits[3] == 4000);

    // A high-bit draw must not fall out of range either.
    CHECK(weighted_pick(0xFFFFFFFFFFFFFFFFull, kWeights, 4) < 4);
    CHECK(weighted_pick(0x8000000000000000ull, kWeights, 4) < 4);
}
