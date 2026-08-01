// Emits the C++ golden-vector header for the M1 determinism spine.
//
// WHY THIS EXISTS. The C++ RNG and world hasher have to agree with the Java
// reference build bit for bit, forever. The weak way to establish that is to
// read the Java, re-derive the algorithm by hand, and assert the C++ matches
// the re-derivation -- which proves only that one person transcribed the same
// thing twice.
//
// So this compiles against the REAL sim-core classes (RandomSource,
// CounterRandomSource, NamedDraws, ActorRngStream, SystemId, WorldHasher,
// WorldLoader, TrojSav -- the production types, not copies) and executes them on
// a JVM. Every number in the generated header is observed Java output.
//
// sim-core is zero-dependency (JDK 21 only, see sim-core/build.gradle.kts), so
// this needs javac and java and nothing else -- no Gradle, no network.
//
//   javac -d <out> $(find sim-core/src/main/java -name '*.java')
//   javac -cp <out> -d <out> tools/golden/GoldenVectors.java
//   java  -cp <out> GoldenVectors <repo>/content <repo>/native/tests/golden_java_vectors.hpp
//
// See tools/golden/generate.ps1, which is that, spelled out.
//
// The output is COMMITTED. Regenerating it must be a deliberate act with a
// visible diff: a golden file that regenerates itself as a build step is not a
// golden file, it is a mirror.

import com.trojia.sim.actor.ActorRngStream;
import com.trojia.sim.actor.NamedDraws;
import com.trojia.sim.engine.SystemId;
import com.trojia.sim.random.CounterRandomSource;
import com.trojia.sim.random.RandomSource;
import com.trojia.sim.world.TickableWorld;
import com.trojia.sim.world.io.TrojSav;
import com.trojia.sim.world.io.WorldHasher;
import com.trojia.sim.world.io.WorldLoader;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

public final class GoldenVectors {

    private final StringBuilder out = new StringBuilder();

    public static void main(String[] args) throws Exception {
        if (args.length != 2) {
            System.err.println("usage: GoldenVectors <content-dir> <output.hpp>");
            System.exit(2);
        }
        Path contentDir = Path.of(args[0]);
        Path output = Path.of(args[1]);
        GoldenVectors gen = new GoldenVectors();
        gen.emit(contentDir);
        Files.createDirectories(output.toAbsolutePath().getParent());
        // '\n' only, binary write: this header is compared and diffed on two
        // platforms and must not pick up CRLF from a Java writer's default.
        Files.write(output, gen.out.toString().getBytes(StandardCharsets.US_ASCII));
        System.out.println("wrote " + output.toAbsolutePath() + " ("
                + gen.out.length() + " bytes)");
    }

    private void emit(Path contentDir) throws IOException {
        header();
        mix64Vectors();
        systemSalts();
        streamSalts();
        counterDraws();
        namedDraws();
        unsignedRemainder();
        sinkVectors();
        combinedVectors();
        worldHashes(contentDir);
        footer();
    }

    // ------------------------------------------------------------------ text

    private void line(String text) {
        out.append(text).append('\n');
    }

    private static String u64(long v) {
        return "0x" + String.format("%016X", v) + "ull";
    }

    private static String i32(int v) {
        return Integer.toString(v);
    }

    private static String quoted(String s) {
        StringBuilder b = new StringBuilder("\"");
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (c == '"' || c == '\\') {
                b.append('\\');
            }
            if (c < 0x20 || c > 0x7E) {
                throw new IllegalArgumentException("non-ASCII in a golden name: " + s);
            }
            b.append(c);
        }
        return b.append('"').toString();
    }

    private void header() {
        line("#pragma once");
        line("");
        line("// GENERATED FILE -- do not edit by hand. Regenerate with:");
        line("//     pwsh tools/golden/generate.ps1");
        line("//");
        line("// Every number below was OBSERVED coming out of a JVM running the real");
        line("// sim-core classes -- com.trojia.sim.random.RandomSource,");
        line("// CounterRandomSource, actor.NamedDraws, actor.ActorRngStream,");
        line("// engine.SystemId and world.io.WorldHasher -- not re-derived by hand from");
        line("// reading them. tools/golden/GoldenVectors.java is the generator and it");
        line("// imports those production types directly.");
        line("//");
        line("// The world-hash vectors at the bottom are the strongest of the lot: Java");
        line("// loaded content/maps/baked/*.trojsav through its own WorldLoader and");
        line("// hashed the result with its own WorldHasher. The C++ side loads the same");
        line("// files through an independently written reader and must land on the same");
        line("// 64 bits. Two languages, two readers, two hashers, one answer.");
        line("//");
        line("// JDK that produced this file: " + System.getProperty("java.vm.name") + " "
                + System.getProperty("java.version"));
        line("");
        line("#include <cstdint>");
        line("");
        line("namespace granadad::sim::golden {");
        line("");
    }

    private void footer() {
        line("}  // namespace granadad::sim::golden");
    }

    private void table(String name, List<String> rows) {
        line("inline constexpr " + name);
        for (String row : rows) {
            line("    " + row);
        }
        line("};");
        line("");
    }

    // ----------------------------------------------------------------- mix64

    private void mix64Vectors() {
        long[] inputs = {
            0L, 1L, 2L, -1L, Long.MIN_VALUE, Long.MAX_VALUE,
            RandomSource.TICK_STRIDE,
            0x54524F4A53415631L,   // WorldHasher SECTION_SEED, "TROJSAV1"
            0x464C414D45563031L,   // WorldHasher COMBINE_SEED, "FLAMEV01"
            0x5CA1AB1E0DDBA11L,    // SystemId salt fold seed
            0xACABCAFE12345678L,   // ActorRngStream salt fold seed
            0x0123456789ABCDEFL, 0xFEDCBA9876543210L,
            0x8000000000000001L,
            42L, 1000000L, -123456789L,
        };
        List<String> rows = new ArrayList<>();
        for (long in : inputs) {
            rows.add("{" + u64(in) + ", " + u64(RandomSource.mix64(in)) + "},");
        }
        line("/// The SplitMix64 finalizer every derivation step folds through.");
        line("struct Mix64Vector {");
        line("    std::uint64_t input;");
        line("    std::uint64_t output;");
        line("};");
        table("Mix64Vector kMix64[] = {", rows);
    }

    // ----------------------------------------------------------------- salts

    private void systemSalts() {
        String[] names = {
            "world", "actors", "input-gate", "heartbeat", "fluids", "thermal",
            "light", "economy", "a", "z9", "the-quick-brown-fox",
        };
        List<String> rows = new ArrayList<>();
        for (String name : names) {
            SystemId id = SystemId.of(name);
            rows.add("{" + quoted(name) + ", " + u64(id.salt()) + ", "
                    + quoted(id.sectionId()) + "},");
        }
        line("/// SystemId: name -> 64-bit salt and 4-char TROJSAV section id.");
        line("/// The salt fold seed is 0x05CA1AB1E0DDBA11 and the input is UTF-16 code");
        line("/// units zero-extended; every name here is ASCII, so bytes work.");
        line("struct SystemIdVector {");
        line("    const char* name;");
        line("    std::uint64_t salt;");
        line("    const char* sectionId;");
        line("};");
        table("SystemIdVector kSystemIds[] = {", rows);
    }

    private void streamSalts() {
        List<String> rows = new ArrayList<>();
        for (ActorRngStream stream : ActorRngStream.values()) {
            rows.add("{" + quoted(stream.streamName()) + ", " + u64(stream.salt()) + "},");
        }
        line("/// ActorRngStream: the 23 registered draw streams. The salt is a pure");
        line("/// function of the NAME (fold seed 0xACABCAFE12345678) and the enum");
        line("/// ordinal is used nowhere -- which is why inserting a stream mid-enum is");
        line("/// harmless and RENAMING one is catastrophic.");
        line("struct StreamSaltVector {");
        line("    const char* name;");
        line("    std::uint64_t salt;");
        line("};");
        table("StreamSaltVector kStreamSalts[] = {", rows);
    }

    // ----------------------------------------------------------------- draws

    private void counterDraws() {
        long[][] tuples = {
            // seed, salt, tick, spatialKey, drawIndex -- the four the Java's own
            // CounterRandomSourceTest pins, then a wider spread.
            {0L, 0L, 0L, 0L, 0L},
            {42L, 0x123456789ABCDEFL, 1L, 0x3FFFFFFFL, 0L},
            {-7L, -1L, 1000000L, -123456789L, 17L},
            {Long.MAX_VALUE, Long.MIN_VALUE, 3L, 0x2AAAAAAAL, 255L},
            {1L, SystemId.of("actors").salt(), 1L, 0L, 0L},
            {1L, SystemId.of("actors").salt(), 1L, 0L, 1L},
            {1L, SystemId.of("actors").salt(), 2L, 0L, 0L},
            {0xDEADBEEFCAFEBABEL, SystemId.of("world").salt(), 15000L, 8191L, 3L},
            {-1L, -1L, -1L, -1L, Integer.MAX_VALUE},
            {0L, 0L, Long.MIN_VALUE, Long.MIN_VALUE, Integer.MIN_VALUE},
        };
        List<String> rows = new ArrayList<>();
        for (long[] t : tuples) {
            CounterRandomSource rng = CounterRandomSource.of(t[0], t[1]);
            rng.beginTick(t[2]);
            int drawIndex = (int) t[4];
            long value = rng.draw(t[3], drawIndex);
            rows.add("{" + u64(t[0]) + ", " + u64(t[1]) + ", " + u64(t[2]) + ", "
                    + u64(t[3]) + ", " + i32(drawIndex) + ", " + u64(value) + "},");
        }
        line("/// CounterRandomSource: the pinned four-step chain");
        line("///   h = mix64(worldSeed + TICK_STRIDE*tick)");
        line("///   h = mix64(h ^ systemSalt)");
        line("///   h = mix64(h + spatialKey)");
        line("///   draw = mix64(h + drawIndex)");
        line("/// drawIndex is a Java int SIGN-EXTENDED into the add, which is why the");
        line("/// INT32_MIN row is here and why the C++ must not zero-extend it.");
        line("struct DrawVector {");
        line("    std::uint64_t worldSeed;");
        line("    std::uint64_t systemSalt;");
        line("    std::uint64_t tick;");
        line("    std::uint64_t spatialKey;");
        line("    std::int32_t drawIndex;");
        line("    std::uint64_t value;");
        line("};");
        table("DrawVector kCounterDraws[] = {", rows);
    }

    private void namedDraws() {
        Object[][] tuples = {
            {ActorRngStream.ACTOR_WANDER, 1L, 1L, 0, 0},
            {ActorRngStream.ACTOR_WANDER, 1L, 1L, 0, 1},
            {ActorRngStream.ACTOR_WANDER, 1L, 1L, 1, 0},
            {ActorRngStream.ACTOR_BARK, 1L, 1L, 0, 0},
            {ActorRngStream.CHECK_PUSH, 0xC0FFEEL, 400L, 691, 7},
            {ActorRngStream.CHECK_LINKCRAFT, -1L, 15000L, 12345, 63},
            {ActorRngStream.FISHING_SPOT_SPAWN, 7L, 240L, 2, 0},
            {ActorRngStream.IDENTITY_NAMES, 99L, 0L, 44, 3},
            {ActorRngStream.WATCH_ARREST_CHECK, Long.MIN_VALUE, Long.MAX_VALUE, 0, 0},
        };
        List<String> rows = new ArrayList<>();
        for (Object[] t : tuples) {
            ActorRngStream stream = (ActorRngStream) t[0];
            long seed = (Long) t[1];
            long tick = (Long) t[2];
            int actorId = (Integer) t[3];
            int drawIndex = (Integer) t[4];
            long value = NamedDraws.draw(stream, seed, tick, actorId, drawIndex);
            rows.add("{" + quoted(stream.streamName()) + ", " + u64(seed) + ", " + u64(tick)
                    + ", " + i32(actorId) + ", " + i32(drawIndex) + ", " + u64(value) + "},");
        }
        line("/// NamedDraws: the same chain keyed by a named stream salt, with the");
        line("/// spatialKey being an actorId (a Java int, sign-extended into the add).");
        line("struct NamedDrawVector {");
        line("    const char* stream;");
        line("    std::uint64_t worldSeed;");
        line("    std::uint64_t tick;");
        line("    std::int32_t actorId;");
        line("    std::int32_t drawIndex;");
        line("    std::uint64_t value;");
        line("};");
        table("NamedDrawVector kNamedDraws[] = {", rows);
    }

    private void unsignedRemainder() {
        long[][] pairs = {
            {0L, 1000L}, {1L, 1000L}, {999L, 1000L}, {1000L, 1000L},
            {-1L, 1000L}, {Long.MIN_VALUE, 1000L}, {Long.MAX_VALUE, 1000L},
            {-1L, 7L}, {0x8000000000000000L, 3L}, {0xDEADBEEFCAFEBABEL, 1000L},
        };
        List<String> rows = new ArrayList<>();
        for (long[] p : pairs) {
            rows.add("{" + u64(p[0]) + ", " + u64(p[1]) + ", "
                    + u64(Long.remainderUnsigned(p[0], p[1])) + "},");
        }
        line("/// Long.remainderUnsigned -- what SkillChecks.passes and");
        line("/// NamedDraws.weightedPick use to turn a draw into an outcome. A SIGNED %");
        line("/// here would go negative for half of all draws and silently invert every");
        line("/// skill check in the game, which is why these rows exist.");
        line("struct RemainderVector {");
        line("    std::uint64_t dividend;");
        line("    std::uint64_t divisor;");
        line("    std::uint64_t remainder;");
        line("};");
        table("RemainderVector kUnsignedRemainders[] = {", rows);
    }

    // ------------------------------------------------------------------ sink

    /// A tiny opcode script both sides interpret, so the vectors exercise all
    /// five Sink methods rather than just the one that is easy to tabulate.
    /// B=putByte S=putShort I=putInt L=putLong Y=putBytes(hex), ';'-separated.
    private static final String[] SINK_SCRIPTS = {
        "",
        "B:0",
        "B:1;B:2;B:3",
        "B:511",                       // width mask: 0x1FF folds as 0xFF
        "S:513",
        "I:1",
        "I:67305985",                  // 0x04030201
        "S:513;S:1027",                // must equal I:0x04030201
        "Y:01020304",                  // must equal I:0x04030201
        "L:578437695752307201",        // 0x0807060504030201
        "Y:0102030405060708",          // must equal the putLong above
        "B:0;B:0",
        "B:0;B:0;B:0",                 // must NOT equal two zero bytes
        "I:-1;L:-1;S:-1;B:-1",
        "Y:00112233445566778899AABBCCDDEEFF;I:-559038737",
        "L:-9223372036854775808;L:9223372036854775807",
    };

    private void sinkVectors() {
        long[] salts = {0L, SystemId.of("actors").salt(), SystemId.of("world").salt(), -1L};
        List<String> rows = new ArrayList<>();
        for (long salt : salts) {
            for (String script : SINK_SCRIPTS) {
                WorldHasher hasher = new WorldHasher();
                // sectionSink is keyed by salt, so a synthetic SystemId with the
                // salt we want is the only way in. The name is irrelevant to the
                // fold; only the salt seeds it.
                WorldHasher.Sink sink = hasher.sectionSink(new SystemId("probe", salt, "PROB"));
                runScript(sink, script);
                long finished = hasher.sectionHash(new SystemId("probe", salt, "PROB"));
                rows.add("{" + u64(salt) + ", " + quoted(script) + ", " + u64(finished) + "},");
            }
        }
        line("/// WorldHasher.HashSink: little-endian byte stream folded 8 bytes at a");
        line("/// time through h = mix64(h ^ block), seeded mix64(SECTION_SEED ^ salt),");
        line("/// finalized by folding the tail block tagged with its byte count and");
        line("/// then the total byte count.");
        line("///");
        line("/// `script` is a ';'-separated opcode list the C++ test interprets:");
        line("///   B:v  putByte(v)      S:v  putShort(v)   I:v  putInt(v)");
        line("///   L:v  putLong(v)      Y:hex putBytes(hex)");
        line("/// so all five Sink methods are exercised, not just the tabulatable one.");
        line("struct SinkVector {");
        line("    std::uint64_t salt;");
        line("    const char* script;");
        line("    std::uint64_t finished;");
        line("};");
        table("SinkVector kSinkVectors[] = {", rows);
    }

    private static void runScript(WorldHasher.Sink sink, String script) {
        if (script.isEmpty()) {
            return;
        }
        for (String op : script.split(";")) {
            int colon = op.indexOf(':');
            char kind = op.charAt(0);
            String arg = op.substring(colon + 1);
            switch (kind) {
                case 'B' -> sink.putByte(Integer.parseInt(arg));
                case 'S' -> sink.putShort(Integer.parseInt(arg));
                case 'I' -> sink.putInt(Integer.parseInt(arg));
                case 'L' -> sink.putLong(Long.parseLong(arg));
                case 'Y' -> {
                    byte[] bytes = new byte[arg.length() / 2];
                    for (int i = 0; i < bytes.length; i++) {
                        bytes[i] = (byte) Integer.parseInt(arg.substring(i * 2, i * 2 + 2), 16);
                    }
                    sink.putBytes(bytes, 0, bytes.length);
                }
                default -> throw new IllegalArgumentException("bad opcode: " + op);
            }
        }
    }

    // -------------------------------------------------------------- combined

    private void combinedVectors() {
        List<String> rows = new ArrayList<>();

        // Zero sections: mix64(COMBINE_SEED).
        rows.add("{" + quoted("") + ", " + u64(new WorldHasher().combinedHash()) + "},");

        // One section.
        rows.add("{" + quoted("world=I:7") + ", "
                + u64(combined(new String[]{"world"}, new String[]{"I:7"})) + "},");

        // Two sections whose salts STRADDLE the sign boundary: "world" is
        // 0xC437... (negative as a Java long) and "actors" is 0x10AC...
        // (positive). Java's TreeMap<Long,...> iterates SIGNED, so world folds
        // FIRST. A C++ std::map<uint64_t,...> would fold them the other way and
        // land on a different number -- the single easiest way to break the port.
        rows.add("{" + quoted("world=I:7,actors=I:9") + ", "
                + u64(combined(new String[]{"world", "actors"}, new String[]{"I:7", "I:9"}))
                + "},");
        // Fed in the OPPOSITE creation order; the answer must not move.
        rows.add("{" + quoted("actors=I:9,world=I:7") + ", "
                + u64(combined(new String[]{"actors", "world"}, new String[]{"I:9", "I:7"}))
                + "},");
        rows.add("{" + quoted("world=Y:0102,actors=L:-1,heartbeat=B:3") + ", "
                + u64(combined(new String[]{"world", "actors", "heartbeat"},
                        new String[]{"Y:0102", "L:-1", "B:3"})) + "},");

        line("/// WorldHasher.combinedHash: mix64(COMBINE_SEED) then, per section in");
        line("/// SIGNED ascending salt order, h = mix64(h ^ salt); h = mix64(h + sub).");
        line("///");
        line("/// `sections` is a ','-separated list of `systemName=script`, evaluated");
        line("/// left to right -- so the pair that differs only in feed order pins the");
        line("/// order-invariance, and the world/actors pair pins the SIGNED ordering.");
        line("struct CombinedVector {");
        line("    const char* sections;");
        line("    std::uint64_t combined;");
        line("};");
        table("CombinedVector kCombinedVectors[] = {", rows);
    }

    private static long combined(String[] names, String[] scripts) {
        WorldHasher hasher = new WorldHasher();
        for (int i = 0; i < names.length; i++) {
            runScript(hasher.sectionSink(SystemId.of(names[i])), scripts[i]);
        }
        return hasher.combinedHash();
    }

    // ------------------------------------------------------------ real worlds

    private void worldHashes(Path contentDir) throws IOException {
        String[] worlds = {"compound_block", "docks_surface", "tavern_fixture"};
        List<String> rows = new ArrayList<>();
        for (String world : worlds) {
            Path file = contentDir.resolve("maps").resolve("baked")
                    .resolve(world + ".trojsav");
            TrojSav save = TrojSav.read(file);
            TickableWorld loaded = new WorldLoader().load(save);
            WorldHasher hasher = new WorldHasher();
            hasher.hashWorld(loaded);
            long section = hasher.sectionHash(WorldHasher.WORLD_SECTION);
            long combined = hasher.combinedHash();
            rows.add("{" + quoted(world) + ", " + u64(section) + ", " + u64(combined) + "},");
            System.out.println("  " + world + "  WRLD=" + String.format("%016x", section)
                    + "  COMBINED=" + String.format("%016x", combined));
        }
        line("/// THE CROSS-LANGUAGE PROOF.");
        line("///");
        line("/// Java loaded content/maps/baked/<world>.trojsav through its own");
        line("/// WorldLoader + ChunkCodec and hashed the decoded result with its own");
        line("/// WorldHasher.hashWorld: chunks ascending, lanes in registry order,");
        line("/// every one of the 8192 cells per lane per chunk, then the CHARGE");
        line("/// overlay. The C++ side reads the same files through a reader that");
        line("/// shares no code with the Java and must land on the same 64 bits.");
        line("///");
        line("/// `combined` is that one section folded through combinedHash(), which");
        line("/// additionally pins the section-salt fold for the reserved `world` id.");
        line("struct WorldHashVector {");
        line("    const char* world;");
        line("    std::uint64_t wrldSectionHash;");
        line("    std::uint64_t combinedHash;");
        line("};");
        table("WorldHashVector kWorldHashes[] = {", rows);
    }
}
