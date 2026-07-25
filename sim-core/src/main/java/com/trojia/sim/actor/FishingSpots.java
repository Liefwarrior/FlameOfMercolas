package com.trojia.sim.actor;

import com.trojia.sim.world.io.WorldHasher;

import java.io.DataInput;
import java.io.DataOutput;
import java.io.IOException;

/**
 * The live fishing-spot registry (Sprint 6 fishing, Eli's bug 6): which authored
 * {@link FishingZoneTable} zones currently have a surfaced spot. Spots spawn and expire on a
 * fixed cadence with named {@link ActorRngStream#FISHING_SPOT_SPAWN} draws — deterministic,
 * capped per size class ({@value #CAP_SMALL} small / {@value #CAP_MEDIUM} medium /
 * {@value #CAP_LARGE} large — "there shouldn't be a ton of them"), never two spots in one
 * zone. Larger spots live longer and yield more per catch; the catch itself is the actors'
 * {@code check.fishing} draw, not this registry's concern.
 *
 * <p><b>Perception</b> ({@link #visibleTo}): whether a soul can SEE a live spot is a pure
 * function of {@code (worldSeed, spot spawnTick, actorId, zoneIndex)} against a FISHING-skill
 * threshold ({@link SkillChecks#fishSightPermille}) — stable for the spot's whole life (no
 * flicker), zero per-tick draw-counter traffic, and evaluable client-side for the played
 * soul's rendering without touching sim state. An invisible spot is NOT TARGETABLE by that
 * actor: the fisher behaviors and the play-mode cast both gate on this.
 *
 * <p><b>Determinism &amp; persistence.</b> Behavior-carrying state (fishers target spots), so
 * the slot array rides the {@code ActorsSystem} chunk's persisted triad (serialize/load/hash,
 * the {@link ShoveLog} discipline) with the zone count as a framing guard. Primitive parallel
 * arrays only; ascending-slot iteration everywhere.
 */
public final class FishingSpots {

    /** Live-spot caps per size class — a handful district-wide, never a ton. */
    public static final int CAP_SMALL = 3;
    public static final int CAP_MEDIUM = 2;
    public static final int CAP_LARGE = 1;

    /** Total slot capacity (the sum of the per-class caps). */
    public static final int SLOT_CAPACITY = CAP_SMALL + CAP_MEDIUM + CAP_LARGE;

    /** Spawn/expiry cadence: the registry re-rolls each size class once per this many ticks. */
    public static final int SPAWN_PERIOD_TICKS = 1_500;

    /** Per-cadence permille chance a below-cap size class surfaces a fresh spot. */
    public static final int SPAWN_CHANCE_PERMILLE = 300;

    /** Spot lifetime by size class (small/medium/large): richer water holds longer. */
    static final long[] LIFETIME_TICKS = {6_000L, 10_000L, 16_000L};

    /** FISH minted per successful catch by size class (large water runs heavy). */
    static final int[] YIELD = {1, 1, 2};

    /** The catch check's resist by size class ({@link SkillChecks#fishCatchPermille}). */
    static final int[] CATCH_RESIST = {0, 20, 40};

    /** The degraded no-spot instance for zone-less bakes: never spawns, every query misses. */
    public static final FishingSpots EMPTY = new FishingSpots(FishingZoneTable.EMPTY);

    private final FishingZoneTable zones;
    private final int[] slotZone = new int[SLOT_CAPACITY];
    private final long[] slotSpawnTick = new long[SLOT_CAPACITY];
    private final long[] slotExpiryTick = new long[SLOT_CAPACITY];

    public FishingSpots(FishingZoneTable zones) {
        this.zones = zones;
        java.util.Arrays.fill(slotZone, Actor.NONE);
    }

    /** The baked zone table behind this registry (client rendering + cast-cell reads). */
    public FishingZoneTable zones() {
        return zones;
    }

    /** Fixed slot capacity; iterate {@code 0..slotCapacity()-1} and skip non-live slots. */
    public int slotCapacity() {
        return SLOT_CAPACITY;
    }

    /** Whether {@code slot} currently holds a live spot. */
    public boolean isLive(int slot) {
        return slotZone[slot] != Actor.NONE;
    }

    /** The zone index of {@code slot}'s spot, or {@link Actor#NONE} when empty. */
    public int zoneAt(int slot) {
        return slotZone[slot];
    }

    /** The tick {@code slot}'s spot surfaced (meaningful only while live). */
    public long spawnTickAt(int slot) {
        return slotSpawnTick[slot];
    }

    /** The absolute tick {@code slot}'s spot sinks again (meaningful only while live). */
    public long expiryTickAt(int slot) {
        return slotExpiryTick[slot];
    }

    /** The size-class ordinal of {@code slot}'s spot (live slots only). */
    public int sizeClassAt(int slot) {
        return zones.sizeClassOf(slotZone[slot]);
    }

    /** The cast cell of {@code slot}'s spot (live slots only). */
    public int castCellAt(int slot) {
        return zones.castCellOf(slotZone[slot]);
    }

    /** FISH minted per successful catch at {@code slot} (live slots only). */
    public int yieldAt(int slot) {
        return YIELD[sizeClassAt(slot)];
    }

    /** The catch check's resist at {@code slot} (live slots only). */
    public int catchResistAt(int slot) {
        return CATCH_RESIST[sizeClassAt(slot)];
    }

    /** Live spots right now (ascending-slot count). */
    public int liveCount() {
        int count = 0;
        for (int s = 0; s < SLOT_CAPACITY; s++) {
            if (isLive(s)) {
                count++;
            }
        }
        return count;
    }

    /** The lowest live slot whose CAST cell is {@code cell}, or {@link Actor#NONE}. */
    public int slotAtCastCell(int cell) {
        for (int s = 0; s < SLOT_CAPACITY; s++) {
            if (isLive(s) && castCellAt(s) == cell) {
                return s;
            }
        }
        return Actor.NONE;
    }

    /**
     * The lowest live slot with any WATER cell within chebyshev {@code reach} of
     * {@code fromCell} on the same or an adjacent z (a pier deck casts one band above the
     * water), or {@link Actor#NONE} — the play-mode cast's "a spot is beside me" probe.
     */
    public int slotNear(int fromCell, int reach) {
        for (int s = 0; s < SLOT_CAPACITY; s++) {
            if (!isLive(s)) {
                continue;
            }
            int zone = slotZone[s];
            for (int j = 0; j < zones.waterCellCount(zone); j++) {
                int water = zones.waterCellAt(zone, j);
                int dz = Math.abs(com.trojia.sim.world.PackedPos.z(water)
                        - com.trojia.sim.world.PackedPos.z(fromCell));
                if (dz <= 1 && ActorGeometry.chebyshev(fromCell, water) <= reach) {
                    return s;
                }
            }
        }
        return Actor.NONE;
    }

    /**
     * Whether {@code actorId} can SEE the live spot in {@code slot} (class doc): the stable
     * per-(spot, soul) {@link ActorRngStream#FISHING_PERCEIVE} draw against the actor's
     * FISHING-deepened sight threshold. An invisible spot is not targetable by this actor.
     */
    public boolean visibleTo(int slot, long worldSeed, int actorId, SkillTrackRegistry tracks) {
        long draw = NamedDraws.draw(ActorRngStream.FISHING_PERCEIVE, worldSeed,
                slotSpawnTick[slot], actorId, slotZone[slot]);
        return SkillChecks.passes(draw, SkillChecks.fishSightPermille(tracks, actorId));
    }

    /**
     * The registry's cadence tick ({@code ActorsSystem}, before any actor acts): on each
     * {@link #SPAWN_PERIOD_TICKS} boundary, expire every spot past its lifetime, then give
     * each below-cap size class ONE {@link ActorRngStream#FISHING_SPOT_SPAWN} draw — a
     * {@link #SPAWN_CHANCE_PERMILLE} surface chance, landing in a drawn zone of that class
     * with no live spot. Deterministic: fixed cadence, ascending scans, named draws
     * addressed {@code (worldSeed, tick, sizeClass, 0)}. A zone-less table no-ops.
     */
    public void tick(long worldSeed, long tick) {
        if (zones.zoneCount() == 0 || tick <= 0 || tick % SPAWN_PERIOD_TICKS != 0) {
            return;
        }
        for (int s = 0; s < SLOT_CAPACITY; s++) {
            if (isLive(s) && slotExpiryTick[s] <= tick) {
                slotZone[s] = Actor.NONE; // the water went quiet: the spot sinks
            }
        }
        for (int sizeClass = 0; sizeClass < FishingZoneTable.SIZE_CLASSES; sizeClass++) {
            if (liveOfClass(sizeClass) >= capOf(sizeClass)) {
                continue;
            }
            long draw = NamedDraws.draw(ActorRngStream.FISHING_SPOT_SPAWN, worldSeed, tick,
                    sizeClass, 0);
            if (Long.remainderUnsigned(draw, 1000) >= SPAWN_CHANCE_PERMILLE) {
                continue; // the water stays quiet this period
            }
            int candidates = 0;
            for (int z = 0; z < zones.zoneCount(); z++) {
                if (zones.sizeClassOf(z) == sizeClass && !zoneIsLive(z)) {
                    candidates++;
                }
            }
            if (candidates == 0) {
                continue;
            }
            int pick = (int) Long.remainderUnsigned(draw >>> 20, candidates);
            for (int z = 0; z < zones.zoneCount(); z++) {
                if (zones.sizeClassOf(z) == sizeClass && !zoneIsLive(z) && pick-- == 0) {
                    surface(z, tick);
                    break;
                }
            }
        }
    }

    /** The per-class live cap. */
    public static int capOf(int sizeClass) {
        return switch (sizeClass) {
            case FishingZoneTable.SMALL -> CAP_SMALL;
            case FishingZoneTable.MEDIUM -> CAP_MEDIUM;
            default -> CAP_LARGE;
        };
    }

    private int liveOfClass(int sizeClass) {
        int count = 0;
        for (int s = 0; s < SLOT_CAPACITY; s++) {
            if (isLive(s) && sizeClassAt(s) == sizeClass) {
                count++;
            }
        }
        return count;
    }

    private boolean zoneIsLive(int zone) {
        for (int s = 0; s < SLOT_CAPACITY; s++) {
            if (slotZone[s] == zone) {
                return true;
            }
        }
        return false;
    }

    private void surface(int zone, long tick) {
        for (int s = 0; s < SLOT_CAPACITY; s++) {
            if (!isLive(s)) {
                slotZone[s] = zone;
                slotSpawnTick[s] = tick;
                slotExpiryTick[s] = tick + LIFETIME_TICKS[zones.sizeClassOf(zone)];
                return;
            }
        }
    }

    // ======================================================================
    // The persisted triad (rides the ActorsSystem chunk after the quest log)
    // ======================================================================

    /** Serializes the zone-count frame guard plus every slot in canonical order. */
    public void serialize(DataOutput out) throws IOException {
        out.writeInt(zones.zoneCount());
        for (int s = 0; s < SLOT_CAPACITY; s++) {
            out.writeInt(slotZone[s]);
            out.writeLong(slotSpawnTick[s]);
            out.writeLong(slotExpiryTick[s]);
        }
    }

    /** Loads what {@link #serialize} wrote; a zone-count mismatch fails loudly (frame guard). */
    public void load(DataInput in) throws IOException {
        int zoneCount = in.readInt();
        if (zoneCount != zones.zoneCount()) {
            throw new IOException("fishing-spot frame mismatch: serialized zoneCount="
                    + zoneCount + " but the loading system bakes " + zones.zoneCount()
                    + " (same zone table must be wired before load)");
        }
        for (int s = 0; s < SLOT_CAPACITY; s++) {
            slotZone[s] = in.readInt();
            slotSpawnTick[s] = in.readLong();
            slotExpiryTick[s] = in.readLong();
        }
    }

    /** Hashes exactly what {@link #serialize} writes (landmine F: spot-only desyncs fail). */
    public void hashInto(WorldHasher.Sink sink) {
        sink.putInt(zones.zoneCount());
        for (int s = 0; s < SLOT_CAPACITY; s++) {
            sink.putInt(slotZone[s]);
            sink.putLong(slotSpawnTick[s]);
            sink.putLong(slotExpiryTick[s]);
        }
    }
}
