#pragma once

// GENERATED FILE -- do not edit by hand. Regenerate with:
//     pwsh tools/golden/generate.ps1
//
// Every number below was OBSERVED coming out of a JVM running the real
// sim-core classes -- com.trojia.sim.random.RandomSource,
// CounterRandomSource, actor.NamedDraws, actor.ActorRngStream,
// engine.SystemId and world.io.WorldHasher -- not re-derived by hand from
// reading them. tools/golden/GoldenVectors.java is the generator and it
// imports those production types directly.
//
// The world-hash vectors at the bottom are the strongest of the lot: Java
// loaded content/maps/baked/*.trojsav through its own WorldLoader and
// hashed the result with its own WorldHasher. The C++ side loads the same
// files through an independently written reader and must land on the same
// 64 bits. Two languages, two readers, two hashers, one answer.
//
// JDK that produced this file: OpenJDK 64-Bit Server VM 21.0.11

#include <cstdint>

namespace granadad::sim::golden {

/// The SplitMix64 finalizer every derivation step folds through.
struct Mix64Vector {
    std::uint64_t input;
    std::uint64_t output;
};
inline constexpr Mix64Vector kMix64[] = {
    {0x0000000000000000ull, 0x0000000000000000ull},
    {0x0000000000000001ull, 0x5692161D100B05E5ull},
    {0x0000000000000002ull, 0xDBD238973A2B148Aull},
    {0xFFFFFFFFFFFFFFFFull, 0xB4D055FCF2CBBD7Bull},
    {0x8000000000000000ull, 0x25C26EA579CEA98Aull},
    {0x7FFFFFFFFFFFFFFFull, 0x5A682AFE7965DEBDull},
    {0x9E3779B97F4A7C15ull, 0xE220A8397B1DCDAFull},
    {0x54524F4A53415631ull, 0x287B55F7C8588FA7ull},
    {0x464C414D45563031ull, 0x88DED864E39B715Full},
    {0x05CA1AB1E0DDBA11ull, 0x64E99C38F45E0C13ull},
    {0xACABCAFE12345678ull, 0x8DA4977B8F752CEAull},
    {0x0123456789ABCDEFull, 0xB2C058E4EBB5112Cull},
    {0xFEDCBA9876543210ull, 0xEE128D82CE22FE61ull},
    {0x8000000000000001ull, 0x22CB06B07578BBFEull},
    {0x000000000000002Aull, 0xA759EA27D4727622ull},
    {0x00000000000F4240ull, 0x62FC1862C57C2356ull},
    {0xFFFFFFFFF8A432EBull, 0xA28BED01541369B4ull},
};

/// SystemId: name -> 64-bit salt and 4-char TROJSAV section id.
/// The salt fold seed is 0x05CA1AB1E0DDBA11 and the input is UTF-16 code
/// units zero-extended; every name here is ASCII, so bytes work.
struct SystemIdVector {
    const char* name;
    std::uint64_t salt;
    const char* sectionId;
};
inline constexpr SystemIdVector kSystemIds[] = {
    {"world", 0xC437BA0F7088FD8Full, "WORL"},
    {"actors", 0x10AC023B151D819Full, "ACTO"},
    {"input-gate", 0xA71857A8D3121A89ull, "INPU"},
    {"heartbeat", 0xF7B6B2BB071669F5ull, "HEAR"},
    {"fluids", 0xC376FD76B2127B51ull, "FLUI"},
    {"thermal", 0xFBA08328B2ABAD4Dull, "THER"},
    {"light", 0xE658DC5B5C15FFD5ull, "LIGH"},
    {"economy", 0x52FA7852D319274Bull, "ECON"},
    {"a", 0x439222C88FC082D0ull, "A___"},
    {"z9", 0xEB8EAC8C779CEDFFull, "Z9__"},
    {"the-quick-brown-fox", 0x12F9AFCAEEDAD758ull, "THEQ"},
};

/// ActorRngStream: the 23 registered draw streams. The salt is a pure
/// function of the NAME (fold seed 0xACABCAFE12345678) and the enum
/// ordinal is used nowhere -- which is why inserting a stream mid-enum is
/// harmless and RENAMING one is catastrophic.
struct StreamSaltVector {
    const char* name;
    std::uint64_t salt;
};
inline constexpr StreamSaltVector kStreamSalts[] = {
    {"actor.wander", 0x20214C5ABA354735ull},
    {"actor.fleeJitter", 0xE46C83F4DF829CDEull},
    {"actor.bark", 0x86DFCEE620C9E326ull},
    {"job.assign", 0x12209C1C3D733DE0ull},
    {"job.targetPick", 0xCBD356A38BCEFA3Aull},
    {"job.renew", 0x33D50649656DF34Cull},
    {"household.size", 0x4AC2ADD312C87E83ull},
    {"household.staffCount", 0xF3AB9B2570177271ull},
    {"household.neighborPick", 0x678950C6CBFA98C3ull},
    {"household.friendPick", 0x87FA65EE25AFFE96ull},
    {"watch.arrestCheck", 0x9A83F57A8F1D0FDCull},
    {"watch.sentenceLength", 0xCB409D9DA857D10Aull},
    {"identity.names", 0x0F460870416BAE6Bull},
    {"check.push", 0x474FFD74DFDEE06Dull},
    {"watch.lenience", 0x95003B4A45338FD0ull},
    {"check.pickpocket", 0x47D9216907540A94ull},
    {"theft.impulse", 0xF8317B5C0EA778C2ull},
    {"check.search", 0xEFFFFB28ABDE29BEull},
    {"fishing.spotSpawn", 0xC0C3E9265F4EC354ull},
    {"fishing.perceive", 0x52E5C27DA1C52A8Aull},
    {"check.fishing", 0x4AD2660926DC58D7ull},
    {"check.cull", 0x1D21E85B302B8B7Full},
    {"check.linkcraft", 0x78C2C6D802B6D937ull},
};

/// CounterRandomSource: the pinned four-step chain
///   h = mix64(worldSeed + TICK_STRIDE*tick)
///   h = mix64(h ^ systemSalt)
///   h = mix64(h + spatialKey)
///   draw = mix64(h + drawIndex)
/// drawIndex is a Java int SIGN-EXTENDED into the add, which is why the
/// INT32_MIN row is here and why the C++ must not zero-extend it.
struct DrawVector {
    std::uint64_t worldSeed;
    std::uint64_t systemSalt;
    std::uint64_t tick;
    std::uint64_t spatialKey;
    std::int32_t drawIndex;
    std::uint64_t value;
};
inline constexpr DrawVector kCounterDraws[] = {
    {0x0000000000000000ull, 0x0000000000000000ull, 0x0000000000000000ull, 0x0000000000000000ull, 0, 0x0000000000000000ull},
    {0x000000000000002Aull, 0x0123456789ABCDEFull, 0x0000000000000001ull, 0x000000003FFFFFFFull, 0, 0x0D7266CF2620BC13ull},
    {0xFFFFFFFFFFFFFFF9ull, 0xFFFFFFFFFFFFFFFFull, 0x00000000000F4240ull, 0xFFFFFFFFF8A432EBull, 17, 0x6967AB74FA2409F3ull},
    {0x7FFFFFFFFFFFFFFFull, 0x8000000000000000ull, 0x0000000000000003ull, 0x000000002AAAAAAAull, 255, 0x1D6EF11D8F76072Eull},
    {0x0000000000000001ull, 0x10AC023B151D819Full, 0x0000000000000001ull, 0x0000000000000000ull, 0, 0xCAB26FFF4B33FC56ull},
    {0x0000000000000001ull, 0x10AC023B151D819Full, 0x0000000000000001ull, 0x0000000000000000ull, 1, 0x83E4F8047F305A16ull},
    {0x0000000000000001ull, 0x10AC023B151D819Full, 0x0000000000000002ull, 0x0000000000000000ull, 0, 0x750990597DCA5704ull},
    {0xDEADBEEFCAFEBABEull, 0xC437BA0F7088FD8Full, 0x0000000000003A98ull, 0x0000000000001FFFull, 3, 0x7D7C60841D06318Eull},
    {0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull, 2147483647, 0xB03977D213D89C62ull},
    {0x0000000000000000ull, 0x0000000000000000ull, 0x8000000000000000ull, 0x8000000000000000ull, -2147483648, 0xDBD96CC44840A94Cull},
};

/// NamedDraws: the same chain keyed by a named stream salt, with the
/// spatialKey being an actorId (a Java int, sign-extended into the add).
struct NamedDrawVector {
    const char* stream;
    std::uint64_t worldSeed;
    std::uint64_t tick;
    std::int32_t actorId;
    std::int32_t drawIndex;
    std::uint64_t value;
};
inline constexpr NamedDrawVector kNamedDraws[] = {
    {"actor.wander", 0x0000000000000001ull, 0x0000000000000001ull, 0, 0, 0xB4CEE8713DDD2D15ull},
    {"actor.wander", 0x0000000000000001ull, 0x0000000000000001ull, 0, 1, 0xA41A6E05538AAB65ull},
    {"actor.wander", 0x0000000000000001ull, 0x0000000000000001ull, 1, 0, 0x8864553C74C98DE4ull},
    {"actor.bark", 0x0000000000000001ull, 0x0000000000000001ull, 0, 0, 0x349387D6DC628D44ull},
    {"check.push", 0x0000000000C0FFEEull, 0x0000000000000190ull, 691, 7, 0xB201E409B6A2C9FAull},
    {"check.linkcraft", 0xFFFFFFFFFFFFFFFFull, 0x0000000000003A98ull, 12345, 63, 0xB09BDAB984C45899ull},
    {"fishing.spotSpawn", 0x0000000000000007ull, 0x00000000000000F0ull, 2, 0, 0xF31633C6508603E3ull},
    {"identity.names", 0x0000000000000063ull, 0x0000000000000000ull, 44, 3, 0xA0149E1152198119ull},
    {"watch.arrestCheck", 0x8000000000000000ull, 0x7FFFFFFFFFFFFFFFull, 0, 0, 0xAE369D69056CD66Full},
};

/// Long.remainderUnsigned -- what SkillChecks.passes and
/// NamedDraws.weightedPick use to turn a draw into an outcome. A SIGNED %
/// here would go negative for half of all draws and silently invert every
/// skill check in the game, which is why these rows exist.
struct RemainderVector {
    std::uint64_t dividend;
    std::uint64_t divisor;
    std::uint64_t remainder;
};
inline constexpr RemainderVector kUnsignedRemainders[] = {
    {0x0000000000000000ull, 0x00000000000003E8ull, 0x0000000000000000ull},
    {0x0000000000000001ull, 0x00000000000003E8ull, 0x0000000000000001ull},
    {0x00000000000003E7ull, 0x00000000000003E8ull, 0x00000000000003E7ull},
    {0x00000000000003E8ull, 0x00000000000003E8ull, 0x0000000000000000ull},
    {0xFFFFFFFFFFFFFFFFull, 0x00000000000003E8ull, 0x0000000000000267ull},
    {0x8000000000000000ull, 0x00000000000003E8ull, 0x0000000000000328ull},
    {0x7FFFFFFFFFFFFFFFull, 0x00000000000003E8ull, 0x0000000000000327ull},
    {0xFFFFFFFFFFFFFFFFull, 0x0000000000000007ull, 0x0000000000000001ull},
    {0x8000000000000000ull, 0x0000000000000003ull, 0x0000000000000002ull},
    {0xDEADBEEFCAFEBABEull, 0x00000000000003E8ull, 0x000000000000002Eull},
};

/// WorldHasher.HashSink: little-endian byte stream folded 8 bytes at a
/// time through h = mix64(h ^ block), seeded mix64(SECTION_SEED ^ salt),
/// finalized by folding the tail block tagged with its byte count and
/// then the total byte count.
///
/// `script` is a ';'-separated opcode list the C++ test interprets:
///   B:v  putByte(v)      S:v  putShort(v)   I:v  putInt(v)
///   L:v  putLong(v)      Y:hex putBytes(hex)
/// so all five Sink methods are exercised, not just the tabulatable one.
struct SinkVector {
    std::uint64_t salt;
    const char* script;
    std::uint64_t finished;
};
inline constexpr SinkVector kSinkVectors[] = {
    {0x0000000000000000ull, "", 0xB163D178B29BC43Eull},
    {0x0000000000000000ull, "B:0", 0xE2FAF062564D1000ull},
    {0x0000000000000000ull, "B:1;B:2;B:3", 0x3A9B254740C80DA6ull},
    {0x0000000000000000ull, "B:511", 0x21771F145E4EC080ull},
    {0x0000000000000000ull, "S:513", 0x78CA916ED76AF130ull},
    {0x0000000000000000ull, "I:1", 0x728AFF8D507FEBC4ull},
    {0x0000000000000000ull, "I:67305985", 0x992C725A87274942ull},
    {0x0000000000000000ull, "S:513;S:1027", 0x992C725A87274942ull},
    {0x0000000000000000ull, "Y:01020304", 0x992C725A87274942ull},
    {0x0000000000000000ull, "L:578437695752307201", 0x2B3D6C5B4C987691ull},
    {0x0000000000000000ull, "Y:0102030405060708", 0x2B3D6C5B4C987691ull},
    {0x0000000000000000ull, "B:0;B:0", 0xFF47726D4E62DBBFull},
    {0x0000000000000000ull, "B:0;B:0;B:0", 0x83336D6307BA7DAAull},
    {0x0000000000000000ull, "I:-1;L:-1;S:-1;B:-1", 0xA9FC1CC4DEDD4F98ull},
    {0x0000000000000000ull, "Y:00112233445566778899AABBCCDDEEFF;I:-559038737", 0x25C833C5974B9446ull},
    {0x0000000000000000ull, "L:-9223372036854775808;L:9223372036854775807", 0xF08ED021F21D855Cull},
    {0x10AC023B151D819Full, "", 0x0D4A6FB81CD12D86ull},
    {0x10AC023B151D819Full, "B:0", 0x4BBD6A7EC98EC988ull},
    {0x10AC023B151D819Full, "B:1;B:2;B:3", 0xF924A531399B1152ull},
    {0x10AC023B151D819Full, "B:511", 0xD2CB6F16EF2AFCFEull},
    {0x10AC023B151D819Full, "S:513", 0xD8FC9C1B595539AEull},
    {0x10AC023B151D819Full, "I:1", 0x1F9BE55440CF4CCBull},
    {0x10AC023B151D819Full, "I:67305985", 0x1A9CCE7E006D766Bull},
    {0x10AC023B151D819Full, "S:513;S:1027", 0x1A9CCE7E006D766Bull},
    {0x10AC023B151D819Full, "Y:01020304", 0x1A9CCE7E006D766Bull},
    {0x10AC023B151D819Full, "L:578437695752307201", 0x925AC44A90CDA0CFull},
    {0x10AC023B151D819Full, "Y:0102030405060708", 0x925AC44A90CDA0CFull},
    {0x10AC023B151D819Full, "B:0;B:0", 0x7834E8702B11BFF6ull},
    {0x10AC023B151D819Full, "B:0;B:0;B:0", 0xC294D9F645D9048Dull},
    {0x10AC023B151D819Full, "I:-1;L:-1;S:-1;B:-1", 0x8B465D10D35D4FC0ull},
    {0x10AC023B151D819Full, "Y:00112233445566778899AABBCCDDEEFF;I:-559038737", 0x7BB4D332DE058B87ull},
    {0x10AC023B151D819Full, "L:-9223372036854775808;L:9223372036854775807", 0x98AF9295EAC420C6ull},
    {0xC437BA0F7088FD8Full, "", 0x59D773F3089B307Dull},
    {0xC437BA0F7088FD8Full, "B:0", 0x7969CC93B095F6EDull},
    {0xC437BA0F7088FD8Full, "B:1;B:2;B:3", 0xAE03EB27B0267B23ull},
    {0xC437BA0F7088FD8Full, "B:511", 0x8637B8B27A335DA4ull},
    {0xC437BA0F7088FD8Full, "S:513", 0x5F57AE8EA23D3F9Full},
    {0xC437BA0F7088FD8Full, "I:1", 0xE0E77F557B115437ull},
    {0xC437BA0F7088FD8Full, "I:67305985", 0xDF97D63AF61855D2ull},
    {0xC437BA0F7088FD8Full, "S:513;S:1027", 0xDF97D63AF61855D2ull},
    {0xC437BA0F7088FD8Full, "Y:01020304", 0xDF97D63AF61855D2ull},
    {0xC437BA0F7088FD8Full, "L:578437695752307201", 0xDA49112B01C8119Cull},
    {0xC437BA0F7088FD8Full, "Y:0102030405060708", 0xDA49112B01C8119Cull},
    {0xC437BA0F7088FD8Full, "B:0;B:0", 0xC3BB7D2AAB61C5E0ull},
    {0xC437BA0F7088FD8Full, "B:0;B:0;B:0", 0x69A9AC3899CE9BABull},
    {0xC437BA0F7088FD8Full, "I:-1;L:-1;S:-1;B:-1", 0xD34B67D4B32B8942ull},
    {0xC437BA0F7088FD8Full, "Y:00112233445566778899AABBCCDDEEFF;I:-559038737", 0xC717001060CCF284ull},
    {0xC437BA0F7088FD8Full, "L:-9223372036854775808;L:9223372036854775807", 0xE52D9075248FA851ull},
    {0xFFFFFFFFFFFFFFFFull, "", 0x5A6CF2A50F5770BCull},
    {0xFFFFFFFFFFFFFFFFull, "B:0", 0xEFC69C392218D8A2ull},
    {0xFFFFFFFFFFFFFFFFull, "B:1;B:2;B:3", 0x8EC5F99CF26C9588ull},
    {0xFFFFFFFFFFFFFFFFull, "B:511", 0xCEEF96233045B231ull},
    {0xFFFFFFFFFFFFFFFFull, "S:513", 0x7F582B8F61AACB8Aull},
    {0xFFFFFFFFFFFFFFFFull, "I:1", 0x535007511CC83E0Eull},
    {0xFFFFFFFFFFFFFFFFull, "I:67305985", 0x7C995F1DA17A6DFCull},
    {0xFFFFFFFFFFFFFFFFull, "S:513;S:1027", 0x7C995F1DA17A6DFCull},
    {0xFFFFFFFFFFFFFFFFull, "Y:01020304", 0x7C995F1DA17A6DFCull},
    {0xFFFFFFFFFFFFFFFFull, "L:578437695752307201", 0x2B091900D17F217Aull},
    {0xFFFFFFFFFFFFFFFFull, "Y:0102030405060708", 0x2B091900D17F217Aull},
    {0xFFFFFFFFFFFFFFFFull, "B:0;B:0", 0x405601B0BD4B27C3ull},
    {0xFFFFFFFFFFFFFFFFull, "B:0;B:0;B:0", 0x7E121B81FE44A341ull},
    {0xFFFFFFFFFFFFFFFFull, "I:-1;L:-1;S:-1;B:-1", 0xA083C59E9B4D77D0ull},
    {0xFFFFFFFFFFFFFFFFull, "Y:00112233445566778899AABBCCDDEEFF;I:-559038737", 0xC00E7C5D04596CFBull},
    {0xFFFFFFFFFFFFFFFFull, "L:-9223372036854775808;L:9223372036854775807", 0x439EF33E7B5C807Eull},
};

/// WorldHasher.combinedHash: mix64(COMBINE_SEED) then, per section in
/// SIGNED ascending salt order, h = mix64(h ^ salt); h = mix64(h + sub).
///
/// `sections` is a ','-separated list of `systemName=script`, evaluated
/// left to right -- so the pair that differs only in feed order pins the
/// order-invariance, and the world/actors pair pins the SIGNED ordering.
struct CombinedVector {
    const char* sections;
    std::uint64_t combined;
};
inline constexpr CombinedVector kCombinedVectors[] = {
    {"", 0x88DED864E39B715Full},
    {"world=I:7", 0x25A857C4E3BB82E7ull},
    {"world=I:7,actors=I:9", 0xC00D4BDCBA273189ull},
    {"actors=I:9,world=I:7", 0xC00D4BDCBA273189ull},
    {"world=Y:0102,actors=L:-1,heartbeat=B:3", 0xF18CCF8A3E9A6CACull},
};

/// THE CROSS-LANGUAGE PROOF.
///
/// Java loaded content/maps/baked/<world>.trojsav through its own
/// WorldLoader + ChunkCodec and hashed the decoded result with its own
/// WorldHasher.hashWorld: chunks ascending, lanes in registry order,
/// every one of the 8192 cells per lane per chunk, then the CHARGE
/// overlay. The C++ side reads the same files through a reader that
/// shares no code with the Java and must land on the same 64 bits.
///
/// `combined` is that one section folded through combinedHash(), which
/// additionally pins the section-salt fold for the reserved `world` id.
struct WorldHashVector {
    const char* world;
    std::uint64_t wrldSectionHash;
    std::uint64_t combinedHash;
};
inline constexpr WorldHashVector kWorldHashes[] = {
    {"compound_block", 0x8431F8DDB4A77BD9ull, 0x96726576CE2A1E58ull},
    {"docks_surface", 0x20275463576E74C3ull, 0xBC7D32021411B541ull},
    {"tavern_fixture", 0x62063C420DAF54FAull, 0x1276F7CAB633724Full},
};

}  // namespace granadad::sim::golden
