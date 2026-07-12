package com.trojia.sim.world.io;

import com.trojia.sim.engine.SystemId;
import com.trojia.sim.world.LaneId;
import com.trojia.sim.world.OverlayId;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

/**
 * Canonical-order invariance and sensitivity tests for {@link WorldHasher}:
 * the combined hash must not depend on the order sinks were created or fed,
 * multi-byte folds must be exactly their little-endian byte streams, and any
 * content difference must surface in both the section and combined hashes.
 */
final class WorldHasherTest {

    private static final SystemId FIRE = SystemId.of("fire");
    private static final SystemId FLUIDS = SystemId.of("fluids");
    private static final List<LaneId> LANES = FakeChunkView.coreLanes();

    @Test
    void combinedHashIsInvariantToSectionFeedOrder() {
        WorldHasher forward = new WorldHasher();
        forward.sectionSink(FIRE).putLong(0x1111L);
        forward.sectionSink(FLUIDS).putLong(0x2222L);

        WorldHasher reversed = new WorldHasher();
        reversed.sectionSink(FLUIDS).putLong(0x2222L);
        reversed.sectionSink(FIRE).putLong(0x1111L);

        assertEquals(forward.combinedHash(), reversed.combinedHash());
        assertEquals(forward.sectionHash(FIRE), reversed.sectionHash(FIRE));
        assertEquals(forward.sectionHash(FLUIDS), reversed.sectionHash(FLUIDS));
    }

    @Test
    void interleavedFeedingMatchesSequentialFeeding() {
        WorldHasher sequential = new WorldHasher();
        WorldHasher.Sink seqA = sequential.sectionSink(FIRE);
        seqA.putInt(1);
        seqA.putInt(2);
        WorldHasher.Sink seqB = sequential.sectionSink(FLUIDS);
        seqB.putInt(3);
        seqB.putInt(4);

        WorldHasher interleaved = new WorldHasher();
        WorldHasher.Sink intA = interleaved.sectionSink(FIRE);
        WorldHasher.Sink intB = interleaved.sectionSink(FLUIDS);
        intA.putInt(1);
        intB.putInt(3);
        intA.putInt(2);
        intB.putInt(4);

        assertEquals(sequential.combinedHash(), interleaved.combinedHash());
    }

    @Test
    void anyContentChangeChangesSectionAndCombinedHash() {
        WorldHasher a = new WorldHasher();
        a.sectionSink(FIRE).putLong(0xABCDL);
        WorldHasher b = new WorldHasher();
        b.sectionSink(FIRE).putLong(0xABCEL);

        assertNotEquals(a.sectionHash(FIRE), b.sectionHash(FIRE));
        assertNotEquals(a.combinedHash(), b.combinedHash());
    }

    @Test
    void identicalContentUnderDifferentSystemIdsHashesDifferently() {
        WorldHasher hasher = new WorldHasher();
        hasher.sectionSink(FIRE).putLong(42L);
        hasher.sectionSink(FLUIDS).putLong(42L);

        assertNotEquals(hasher.sectionHash(FIRE), hasher.sectionHash(FLUIDS));
    }

    @Test
    void multiByteFoldsAreExactlyTheirLittleEndianByteStreams() {
        WorldHasher viaInt = new WorldHasher();
        viaInt.sectionSink(FIRE).putInt(0x04030201);

        WorldHasher viaShorts = new WorldHasher();
        viaShorts.sectionSink(FIRE).putShort(0x0201);
        viaShorts.sectionSink(FIRE).putShort(0x0403);

        WorldHasher viaBytes = new WorldHasher();
        viaBytes.sectionSink(FIRE).putBytes(new byte[]{1, 2, 3, 4}, 0, 4);

        WorldHasher viaLong = new WorldHasher();
        viaLong.sectionSink(FIRE).putLong(0x0807060504030201L);
        WorldHasher viaBytes8 = new WorldHasher();
        viaBytes8.sectionSink(FIRE).putBytes(new byte[]{1, 2, 3, 4, 5, 6, 7, 8}, 0, 8);

        assertEquals(viaInt.sectionHash(FIRE), viaShorts.sectionHash(FIRE));
        assertEquals(viaInt.sectionHash(FIRE), viaBytes.sectionHash(FIRE));
        assertEquals(viaLong.sectionHash(FIRE), viaBytes8.sectionHash(FIRE));
    }

    @Test
    void widthMethodsFoldOnlyTheirDeclaredBits() {
        WorldHasher masked = new WorldHasher();
        masked.sectionSink(FIRE).putByte(0x1FF);
        WorldHasher plain = new WorldHasher();
        plain.sectionSink(FIRE).putByte(0xFF);

        assertEquals(plain.sectionHash(FIRE), masked.sectionHash(FIRE));
    }

    @Test
    void trailingPartialBlockAndLengthAreDisambiguated() {
        // Same bytes, split differently across calls: identical hash.
        WorldHasher oneCall = new WorldHasher();
        oneCall.sectionSink(FIRE).putBytes(new byte[]{9, 8, 7}, 0, 3);
        WorldHasher threeCalls = new WorldHasher();
        threeCalls.sectionSink(FIRE).putByte(9);
        threeCalls.sectionSink(FIRE).putByte(8);
        threeCalls.sectionSink(FIRE).putByte(7);
        assertEquals(oneCall.sectionHash(FIRE), threeCalls.sectionHash(FIRE));

        // A stream of N zero bytes must not collide with N+1 zero bytes.
        WorldHasher two = new WorldHasher();
        two.sectionSink(FIRE).putBytes(new byte[]{0, 0}, 0, 2);
        WorldHasher three = new WorldHasher();
        three.sectionSink(FIRE).putBytes(new byte[]{0, 0, 0}, 0, 3);
        assertNotEquals(two.sectionHash(FIRE), three.sectionHash(FIRE));
    }

    @Test
    void sectionHashIsIdempotentAndUnknownSystemsThrow() {
        WorldHasher hasher = new WorldHasher();
        hasher.sectionSink(FIRE).putInt(5);

        assertEquals(hasher.sectionHash(FIRE), hasher.sectionHash(FIRE));
        assertThrows(IllegalStateException.class, () -> hasher.sectionHash(FLUIDS));
    }

    @Test
    void identicalChunksHashIdenticallyAcrossHashers() {
        FakeChunkView chunkA = new FakeChunkView(5, LANES);
        chunkA.fillRandom(77);
        FakeChunkView chunkB = new FakeChunkView(5, LANES);
        chunkB.fillRandom(77);

        WorldHasher a = new WorldHasher();
        a.hashChunk(chunkA, LANES, ChunkOverlays.EMPTY);
        WorldHasher b = new WorldHasher();
        b.hashChunk(chunkB, LANES, ChunkOverlays.EMPTY);

        assertEquals(a.sectionHash(WorldHasher.WORLD_SECTION),
                b.sectionHash(WorldHasher.WORLD_SECTION));
        assertEquals(a.combinedHash(), b.combinedHash());
    }

    @Test
    void chunkContentAndChunkIndexAreBothHashSensitive() {
        FakeChunkView base = new FakeChunkView(5, LANES);
        base.fillRandom(77);

        FakeChunkView differentContent = new FakeChunkView(5, LANES);
        differentContent.fillRandom(77);
        differentContent.shortLane(LANES.get(0))[100] ^= 1;

        FakeChunkView differentIndex = new FakeChunkView(6, LANES);
        differentIndex.fillRandom(77);

        WorldHasher h1 = new WorldHasher();
        h1.hashChunk(base, LANES, ChunkOverlays.EMPTY);
        WorldHasher h2 = new WorldHasher();
        h2.hashChunk(differentContent, LANES, ChunkOverlays.EMPTY);
        WorldHasher h3 = new WorldHasher();
        h3.hashChunk(differentIndex, LANES, ChunkOverlays.EMPTY);

        assertNotEquals(h1.combinedHash(), h2.combinedHash());
        assertNotEquals(h1.combinedHash(), h3.combinedHash());
    }

    @Test
    void overlayCellsAreHashedInCanonicalSortedOrder() {
        FakeChunkView chunk = new FakeChunkView(2, LANES);

        FakeChunkOverlays insertedForward = new FakeChunkOverlays();
        insertedForward.put(OverlayId.CHARGE, 10, 1);
        insertedForward.put(OverlayId.CHARGE, 20, 2);

        FakeChunkOverlays insertedBackward = new FakeChunkOverlays();
        insertedBackward.put(OverlayId.CHARGE, 20, 2);
        insertedBackward.put(OverlayId.CHARGE, 10, 1);

        WorldHasher a = new WorldHasher();
        a.hashChunk(chunk, LANES, insertedForward);
        WorldHasher b = new WorldHasher();
        b.hashChunk(chunk, LANES, insertedBackward);
        WorldHasher without = new WorldHasher();
        without.hashChunk(chunk, LANES, ChunkOverlays.EMPTY);

        assertEquals(a.combinedHash(), b.combinedHash());
        assertNotEquals(a.combinedHash(), without.combinedHash());
    }
}
