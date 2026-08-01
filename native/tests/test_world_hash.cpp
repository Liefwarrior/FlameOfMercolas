// The world hasher, asserted against a JVM and against the real baked worlds.
//
// The strongest case in this file is "the C++ hash of a real world equals the
// JVM's". Java loaded content/maps/baked/*.trojsav through its own TROJSAV
// container, its own chunk codec and its own WorldHasher; C++ loads the same
// files through a reader that shares no line of code with it and hashes them
// with a hasher written from the spec. Two languages, two readers, two hashers,
// one 64-bit answer, over ~2.7 million tiles.
//
// The rest of the file exists because that one number could be right by luck in
// ways a single value cannot distinguish: the sink protocol at every width, the
// tail-block tagging, the signed combine order, and -- the negative nobody
// remembers to write -- that the hash actually MOVES when the world does.

#include <doctest/doctest.h>

#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "golden_java_vectors.hpp"
#include "granadad/content/content_dir.hpp"
#include "granadad/content/lanes.hpp"
#include "granadad/content/world.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/sim/engine_error.hpp"
#include "granadad/sim/rng.hpp"
#include "granadad/sim/system_id.hpp"
#include "granadad/sim/world_hash.hpp"

using namespace granadad::sim;
namespace content = granadad::content;

namespace {

/// Interprets the golden vectors' opcode scripts. Same grammar the Java
/// generator used, so all five sink methods are exercised rather than only the
/// one that is easy to tabulate.
///
///   B:v  put_byte    S:v  put_short   I:v  put_int
///   L:v  put_long    Y:hex put_bytes
void run_script(HashSink& sink, const std::string& script) {
    std::size_t at = 0;
    while (at < script.size()) {
        std::size_t stop = script.find(';', at);
        if (stop == std::string::npos) {
            stop = script.size();
        }
        const std::string op = script.substr(at, stop - at);
        at = stop + 1;
        REQUIRE(op.size() >= 2);
        REQUIRE(op[1] == ':');
        const char kind = op[0];
        const std::string arg = op.substr(2);
        if (kind == 'Y') {
            std::vector<std::uint8_t> bytes;
            for (std::size_t i = 0; i + 1 < arg.size(); i += 2) {
                bytes.push_back(static_cast<std::uint8_t>(
                    std::strtoul(arg.substr(i, 2).c_str(), nullptr, 16)));
            }
            sink.put_bytes(bytes);
            continue;
        }
        const long long value = std::strtoll(arg.c_str(), nullptr, 10);
        switch (kind) {
            case 'B':
                sink.put_byte(static_cast<std::uint32_t>(static_cast<std::int32_t>(value)));
                break;
            case 'S':
                sink.put_short(static_cast<std::uint32_t>(static_cast<std::int32_t>(value)));
                break;
            case 'I':
                sink.put_int(static_cast<std::uint32_t>(static_cast<std::int32_t>(value)));
                break;
            case 'L':
                sink.put_long(static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
                break;
            default:
                FAIL("bad opcode: " << op);
        }
    }
}

/// `world=I:7,actors=I:9` -> a fed hasher.
void run_sections(WorldHasher& hasher, const std::string& spec) {
    std::size_t at = 0;
    while (at < spec.size()) {
        std::size_t stop = spec.find(',', at);
        if (stop == std::string::npos) {
            stop = spec.size();
        }
        const std::string entry = spec.substr(at, stop - at);
        at = stop + 1;
        const std::size_t eq = entry.find('=');
        REQUIRE(eq != std::string::npos);
        run_script(hasher.section_sink(SystemId::of(entry.substr(0, eq))),
                   entry.substr(eq + 1));
    }
}

}  // namespace

TEST_CASE("the sink protocol reproduces the JVM at every width") {
    for (const golden::SinkVector& v : golden::kSinkVectors) {
        CAPTURE(v.salt);
        CAPTURE(v.script);
        HashSink sink(v.salt);
        run_script(sink, v.script);
        CHECK(sink.finished() == v.finished);
    }
}

TEST_CASE("the width methods are equivalent to the same bytes") {
    // putInt(0x04030201) must equal two putShorts must equal four raw bytes.
    // Pinned by the JVM as well, but stated here because it is the property the
    // little-endian contract IS.
    HashSink a(0);
    a.put_int(0x04030201u);
    HashSink b(0);
    b.put_short(0x0201u);
    b.put_short(0x0403u);
    HashSink c(0);
    const std::uint8_t bytes[] = {1, 2, 3, 4};
    c.put_bytes(bytes);
    CHECK(a.finished() == b.finished());
    CHECK(a.finished() == c.finished());

    HashSink d(0);
    d.put_long(0x0807060504030201ull);
    HashSink e(0);
    const std::uint8_t eight[] = {1, 2, 3, 4, 5, 6, 7, 8};
    e.put_bytes(eight);
    CHECK(d.finished() == e.finished());
}

TEST_CASE("width methods mask, so an over-wide value cannot smuggle bits in") {
    HashSink a(0);
    a.put_byte(0x1FFu);
    HashSink b(0);
    b.put_byte(0xFFu);
    CHECK(a.finished() == b.finished());
}

TEST_CASE("the same bytes hash the same however they are split across calls") {
    // The bulk eight-at-a-time path inside put_bytes is an optimisation and
    // must be invisible. This is the case that would catch it if it were not:
    // every split point of a 24-byte run, including the ones that leave the
    // buffer partially full when the bulk loop starts.
    std::vector<std::uint8_t> all;
    for (int i = 0; i < 24; ++i) {
        all.push_back(static_cast<std::uint8_t>(i * 7 + 1));
    }
    HashSink whole(0x1234u);
    whole.put_bytes(all);
    const std::uint64_t expected = whole.finished();

    for (std::size_t split = 0; split <= all.size(); ++split) {
        CAPTURE(split);
        HashSink parts(0x1234u);
        parts.put_bytes(std::span<const std::uint8_t>(all.data(), split));
        parts.put_bytes(std::span<const std::uint8_t>(all.data() + split, all.size() - split));
        CHECK(parts.finished() == expected);
    }
}

TEST_CASE("N zero bytes does not hash the same as N+1") {
    // This is what the tail-block byte-count tag exists for. Without it every
    // run of trailing zeros collapses to the same value, and a world that lost
    // a chunk of empty lane would hash identically to one that did not.
    std::uint64_t previous = 0;
    for (int n = 0; n <= 16; ++n) {
        HashSink sink(0);
        for (int i = 0; i < n; ++i) {
            sink.put_byte(0);
        }
        const std::uint64_t now = sink.finished();
        if (n > 0) {
            CHECK(now != previous);
        }
        previous = now;
    }
}

TEST_CASE("finished() is pure: reading it does not disturb the stream") {
    HashSink sink(0x99u);
    sink.put_int(7);
    const std::uint64_t once = sink.finished();
    CHECK(sink.finished() == once);
    CHECK(sink.finished() == once);
    // And feeding may continue afterwards.
    sink.put_int(8);
    CHECK(sink.finished() != once);
    CHECK(sink.total_bytes() == 8);
}

TEST_CASE("the same content under different salts hashes differently") {
    HashSink a(system_salt("actors"));
    HashSink b(system_salt("drift"));
    a.put_int(1);
    b.put_int(1);
    CHECK(a.finished() != b.finished());
}

TEST_CASE("the combined hash reproduces the JVM, signed salt order and all") {
    for (const golden::CombinedVector& v : golden::kCombinedVectors) {
        CAPTURE(v.sections);
        WorldHasher hasher;
        run_sections(hasher, v.sections);
        CHECK(hasher.combined_hash() == v.combined);
    }
}

TEST_CASE("sections combine in SIGNED salt order") {
    // The single easiest way to break this port. `world`'s salt has the top bit
    // set and `actors`' does not, so signed ordering folds world FIRST and
    // unsigned ordering folds it second. Both are self-consistent; only one
    // matches the Java.
    CHECK((WORLD_SECTION_SALT >> 63) == 1);
    CHECK((system_salt("actors") >> 63) == 0);
    CHECK(static_cast<std::int64_t>(WORLD_SECTION_SALT) < 0);
    CHECK(static_cast<std::int64_t>(WORLD_SECTION_SALT)
          < static_cast<std::int64_t>(system_salt("actors")));
    CHECK(WORLD_SECTION_SALT > system_salt("actors"));  // unsigned says the opposite
}

TEST_CASE("the combined hash ignores the order sections were created and fed") {
    WorldHasher forward;
    forward.section_sink(SystemId::of("world")).put_int(7);
    forward.section_sink(SystemId::of("actors")).put_int(9);

    WorldHasher backward;
    backward.section_sink(SystemId::of("actors")).put_int(9);
    backward.section_sink(SystemId::of("world")).put_int(7);

    WorldHasher interleaved;
    HashSink& w = interleaved.section_sink(SystemId::of("world"));
    HashSink& a = interleaved.section_sink(SystemId::of("actors"));
    a.put_short(9);
    w.put_short(7);
    a.put_short(0);
    w.put_short(0);

    CHECK(forward.combined_hash() == backward.combined_hash());
    CHECK(forward.combined_hash() == interleaved.combined_hash());
}

TEST_CASE("an unfed section throws instead of returning a plausible number") {
    WorldHasher hasher;
    hasher.section_sink(SystemId::of("actors")).put_int(1);
    CHECK_THROWS_AS((void)hasher.section_hash(SystemId::of("drift")), EngineError);
    CHECK(hasher.section_count() == 1);
}

TEST_CASE("zero sections combine to mix64(COMBINE_SEED)") {
    const WorldHasher hasher;
    CHECK(hasher.combined_hash() == mix64(COMBINE_SEED));
    CHECK(hasher.section_count() == 0);
}

// ---------------------------------------------------------------------------
// the real worlds
// ---------------------------------------------------------------------------

TEST_CASE("every shipped world hashes to exactly what the JVM said") {
    CHECK(std::size(golden::kWorldHashes) == 3);
    for (const golden::WorldHashVector& v : golden::kWorldHashes) {
        CAPTURE(v.world);
        const content::World world = content::loadWorldFile(content::bakedMap(v.world));
        WorldHasher hasher;
        hasher.hash_world(world);
        CHECK(hasher.section_hash(WORLD_SECTION_SALT) == v.wrldSectionHash);
        CHECK(hasher.combined_hash() == v.combinedHash);
    }
}

TEST_CASE("hashing the same world twice gives the same answer") {
    const content::World world = content::loadWorldFile(content::bakedMap("tavern_fixture"));
    WorldHasher first;
    first.hash_world(world);
    WorldHasher second;
    second.hash_world(world);
    CHECK(first.section_hash(WORLD_SECTION_SALT) == second.section_hash(WORLD_SECTION_SALT));
}

TEST_CASE("the three shipped worlds hash to three different values") {
    // Not a tautology worth skipping: a hasher that dropped its input entirely
    // and returned the seed would pass every equality assertion in this file.
    CHECK(golden::kWorldHashes[0].wrldSectionHash != golden::kWorldHashes[1].wrldSectionHash);
    CHECK(golden::kWorldHashes[1].wrldSectionHash != golden::kWorldHashes[2].wrldSectionHash);
    CHECK(golden::kWorldHashes[0].wrldSectionHash != golden::kWorldHashes[2].wrldSectionHash);
}

TEST_CASE("one changed tile changes the world hash") {
    content::World world = content::loadWorldFile(content::bakedMap("tavern_fixture"));
    WorldHasher before;
    before.hash_world(world);
    const std::uint64_t original = before.section_hash(WORLD_SECTION_SALT);

    SUBCASE("a byte lane") {
        const std::span<std::uint8_t> flags = world.byteLane(content::kFlagsLane);
        flags[12345] = static_cast<std::uint8_t>(flags[12345] ^ 0x01u);
        WorldHasher after;
        after.hash_world(world);
        CHECK(after.section_hash(WORLD_SECTION_SALT) != original);
    }

    SUBCASE("a short lane") {
        const std::span<std::uint16_t> material = world.shortLane(content::kMaterialLane);
        material[9999] = static_cast<std::uint16_t>(material[9999] + 1u);
        WorldHasher after;
        after.hash_world(world);
        CHECK(after.section_hash(WORLD_SECTION_SALT) != original);
    }

    SUBCASE("the very last tile of the very last chunk") {
        // The end of the stream is where an off-by-one in the chunk loop hides.
        const std::span<std::uint8_t> flags = world.byteLane(content::kFlagsLane);
        flags[flags.size() - 1] = static_cast<std::uint8_t>(flags[flags.size() - 1] ^ 0x02u);
        WorldHasher after;
        after.hash_world(world);
        CHECK(after.section_hash(WORLD_SECTION_SALT) != original);
    }

    SUBCASE("an overlay cell") {
        // The shipped worlds carry no CHARGE cells at all, so this path is not
        // exercised by any of the three -- which is exactly why it is asserted
        // here rather than assumed from them.
        CHECK(world.overlay(content::OverlayId::Charge).empty());
        world.putOverlay(content::OverlayId::Charge, 4096, 77);
        WorldHasher after;
        after.hash_world(world);
        CHECK(after.section_hash(WORLD_SECTION_SALT) != original);
    }
}

TEST_CASE("swapping two tiles' values changes the hash") {
    // A hasher that summed or xored its input would be blind to this. The
    // position of every value has to matter, not just the multiset of them.
    content::World world = content::loadWorldFile(content::bakedMap("tavern_fixture"));
    WorldHasher before;
    before.hash_world(world);
    const std::uint64_t original = before.section_hash(WORLD_SECTION_SALT);

    const std::span<std::uint16_t> material = world.shortLane(content::kMaterialLane);
    std::size_t other = 0;
    for (std::size_t i = 1; i < material.size(); ++i) {
        if (material[i] != material[0]) {
            other = i;
            break;
        }
    }
    REQUIRE(other != 0);
    std::swap(material[0], material[other]);

    WorldHasher after;
    after.hash_world(world);
    CHECK(after.section_hash(WORLD_SECTION_SALT) != original);
}
