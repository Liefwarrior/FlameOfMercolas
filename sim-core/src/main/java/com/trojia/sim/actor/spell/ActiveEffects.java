package com.trojia.sim.actor.spell;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.Need;
import com.trojia.sim.world.io.WorldHasher;

import java.io.DataInput;
import java.io.DataOutput;
import java.io.IOException;

/**
 * EVERY LINGERING EFFECT IN THE WARD, in one fixed-size table — the single piece of persisted
 * state the whole magic system owns.
 *
 * <p>A held warmth, a timed +1 AGI and a harm that trickles are not three mechanisms; they are
 * three rows here. Because readers SUM the live rows instead of banking a value on the actor,
 * an effect can never outlive its own expiry, a save can never disagree with the effects that
 * produced it, and adding a fourth axis adds no new save frame.
 *
 * <p><b>The triad.</b> Behaviour-carrying without question — a live {@link EffectKind#ATTRIBUTE}
 * row shifts every skill check the target makes — so the slot arrays ride the
 * {@code ActorsSystem} chunk's serialize/load/hashInto in one canonical order, the
 * {@code FishingSpots} discipline. Absolute end ticks, never countdowns (the
 * {@code culledUntilTick} determinism rule).
 *
 * <p><b>Determinism.</b> Fixed capacity, primitive parallel arrays, ascending-slot iteration
 * everywhere, no draws of its own, no allocation on the tick path. A cast that arrives with the
 * table full evicts the row that would have expired soonest (lowest slot breaking the tie), so
 * the newest crafting is always the one that lands and the outcome never depends on allocation
 * history.
 */
public final class ActiveEffects {

    /**
     * Rows available district-wide. Sized well past what the ward can hold at once: only the
     * played actor casts today, spells carry a cooldown, and the longest authored effect is
     * shorter than the shortest working day.
     */
    public static final int SLOT_CAPACITY = 96;

    /** A trickle lands one dose every this many ticks ({@link EffectMode#OVER_TIME}). */
    public static final int OVER_TIME_PERIOD_TICKS = 10;

    /**
     * A body is never crafted below this many hit points. The public shelf teaches scalds and
     * welts, not killings — twenty full crafters pooled make one lit torch flare (L567), and a
     * common crafter runs at about a thousandth of a Luxerne's throughput (L516). Keeping the
     * floor at 1 is also what keeps this whole system out of the ecology's way: no cast can
     * put a body on the ground, so the predators' supply, the death feed and the justice
     * pipeline are all untouched by magic.
     */
    public static final int VITALITY_FLOOR = 1;

    /**
     * Warmth pays REST every this many ticks — being warm is restful and being cold is not.
     * A cadence rather than a per-tick drip so the coupling stays small and legible.
     */
    public static final int WARMTH_REST_PERIOD_TICKS = 50;

    /** Deci-Kelvin of held offset that buys one point of REST per {@link #WARMTH_REST_PERIOD_TICKS}. */
    public static final int DECIK_PER_REST_POINT = 1;

    /** {@code slotTarget[s] == NO_TARGET} marks a free row. */
    private static final int NO_TARGET = Actor.NONE;

    private final int[] slotTarget = new int[SLOT_CAPACITY];
    private final byte[] slotKind = new byte[SLOT_CAPACITY];
    private final byte[] slotMode = new byte[SLOT_CAPACITY];
    private final byte[] slotParam = new byte[SLOT_CAPACITY];
    private final int[] slotMagnitude = new int[SLOT_CAPACITY];
    private final long[] slotStartTick = new long[SLOT_CAPACITY];
    private final long[] slotEndTick = new long[SLOT_CAPACITY];

    public ActiveEffects() {
        java.util.Arrays.fill(slotTarget, NO_TARGET);
    }

    public int slotCapacity() {
        return SLOT_CAPACITY;
    }

    /** Whether {@code slot} holds a live effect. */
    public boolean isLive(int slot) {
        return slotTarget[slot] != NO_TARGET;
    }

    /** The actor a live row is working on. */
    public int targetAt(int slot) {
        return slotTarget[slot];
    }

    /** The axis of a live row. */
    public EffectKind kindAt(int slot) {
        return EffectKind.of(slotKind[slot]);
    }

    /** The time-shape of a live row. */
    public EffectMode modeAt(int slot) {
        return EffectMode.of(slotMode[slot]);
    }

    /** The axis selector of a live row (the {@code AttributeId} ordinal, or 0). */
    public int paramAt(int slot) {
        return slotParam[slot];
    }

    /** The signed magnitude of a live row, in the axis's own units. */
    public int magnitudeAt(int slot) {
        return slotMagnitude[slot];
    }

    /** The absolute tick a live row stops. */
    public long endTickAt(int slot) {
        return slotEndTick[slot];
    }

    /** Live rows right now (ascending-slot count). */
    public int liveCount() {
        int count = 0;
        for (int s = 0; s < SLOT_CAPACITY; s++) {
            if (isLive(s)) {
                count++;
            }
        }
        return count;
    }

    /** Live rows working on one actor. */
    public int liveCountOn(int actorId) {
        int count = 0;
        for (int s = 0; s < SLOT_CAPACITY; s++) {
            if (slotTarget[s] == actorId) {
                count++;
            }
        }
        return count;
    }

    // ======================================================================
    // Adding
    // ======================================================================

    /**
     * Files one lingering effect and returns the slot it took. {@code startTick} is the tick
     * the crafting resolved; {@code endTick} is absolute. Free rows are taken lowest-first; a
     * full table evicts the soonest-to-expire row (lowest slot on a tie) so the newest crafting
     * always lands — a deterministic, bounded policy that never depends on allocation history.
     */
    public int add(int targetId, EffectKind kind, EffectMode mode, int param, int magnitude,
            long startTick, long endTick) {
        int slot = lowestFreeSlot();
        if (slot == Actor.NONE) {
            slot = soonestExpiringSlot();
        }
        slotTarget[slot] = targetId;
        slotKind[slot] = (byte) kind.ordinal();
        slotMode[slot] = (byte) mode.ordinal();
        slotParam[slot] = (byte) param;
        slotMagnitude[slot] = magnitude;
        slotStartTick[slot] = startTick;
        slotEndTick[slot] = endTick;
        return slot;
    }

    private int lowestFreeSlot() {
        for (int s = 0; s < SLOT_CAPACITY; s++) {
            if (!isLive(s)) {
                return s;
            }
        }
        return Actor.NONE;
    }

    private int soonestExpiringSlot() {
        int best = 0;
        for (int s = 1; s < SLOT_CAPACITY; s++) {
            if (slotEndTick[s] < slotEndTick[best]) {
                best = s;
            }
        }
        return best;
    }

    /** Clears every row working on {@code actorId} (a body that died stops being warm). */
    public void clearFor(int actorId) {
        for (int s = 0; s < SLOT_CAPACITY; s++) {
            if (slotTarget[s] == actorId) {
                slotTarget[s] = NO_TARGET;
            }
        }
    }

    // ======================================================================
    // Reads the rest of the sim makes
    // ======================================================================

    /**
     * This actor's body heat as an offset from ambient, in deci-Kelvin: the live sum of its
     * held {@link EffectKind#TEMPERATURE} rows. Zero for a body nobody has warmed or chilled.
     */
    public int temperatureOffsetDeciK(int actorId) {
        int sum = 0;
        for (int s = 0; s < SLOT_CAPACITY; s++) {
            if (slotTarget[s] == actorId && slotKind[s] == (byte) EffectKind.TEMPERATURE.ordinal()
                    && slotMode[s] == (byte) EffectMode.WHILE_ACTIVE.ordinal()) {
                sum += slotMagnitude[s];
            }
        }
        return sum;
    }

    /**
     * This actor's live modifier to one attribute, in attribute points: the sum of its held
     * {@link EffectKind#ATTRIBUTE} rows for that {@code attributeOrdinal}. Read by
     * {@code SkillTrackRegistry.attribute}, which is how a crafting reaches every check in the
     * game at once.
     */
    public int attributeModifier(int actorId, int attributeOrdinal) {
        int sum = 0;
        for (int s = 0; s < SLOT_CAPACITY; s++) {
            if (slotTarget[s] == actorId && slotKind[s] == (byte) EffectKind.ATTRIBUTE.ordinal()
                    && slotMode[s] == (byte) EffectMode.WHILE_ACTIVE.ordinal()
                    && slotParam[s] == (byte) attributeOrdinal) {
                sum += slotMagnitude[s];
            }
        }
        return sum;
    }

    // ======================================================================
    // The cadence tick
    // ======================================================================

    /**
     * The table's own tick, run by {@code ActorsSystem} BEFORE any actor acts so every read
     * this tick sees one coherent set: expire finished rows, drop rows on the dead, then land
     * the doses due — a {@link EffectMode#OVER_TIME} dose on its period, and warmth's REST
     * payment on its own. Ascending-slot, draw-free, allocation-free.
     */
    public void tick(ActorRegistry registry, long tick) {
        for (int s = 0; s < SLOT_CAPACITY; s++) {
            if (!isLive(s)) {
                continue;
            }
            int targetId = slotTarget[s];
            if (targetId < 0 || targetId >= registry.size()) {
                slotTarget[s] = NO_TARGET; // a row whose body never existed on this load
                continue;
            }
            if (registry.get(targetId).isDead()) {
                slotTarget[s] = NO_TARGET;
                continue;
            }
            // The dose lands BEFORE the row is retired, so a duration of exactly one period
            // delivers exactly one dose (the alternative silently makes "harm 3 for 10 ticks"
            // a no-op, which is a very quiet way for a spellcrafting screen to lie).
            long elapsed = tick - slotStartTick[s];
            if (elapsed > 0 && tick <= slotEndTick[s]) {
                EffectMode mode = EffectMode.of(slotMode[s]);
                EffectKind kind = EffectKind.of(slotKind[s]);
                if (mode == EffectMode.OVER_TIME && elapsed % OVER_TIME_PERIOD_TICKS == 0) {
                    applyOnce(registry.get(targetId), kind, slotParam[s], slotMagnitude[s]);
                } else if (mode == EffectMode.WHILE_ACTIVE && kind == EffectKind.TEMPERATURE
                        && elapsed % WARMTH_REST_PERIOD_TICKS == 0) {
                    registry.get(targetId).applyNeedDelta(Need.REST,
                            slotMagnitude[s] / DECIK_PER_REST_POINT);
                }
            }
            if (slotEndTick[s] <= tick) {
                slotTarget[s] = NO_TARGET;
            }
        }
    }

    /**
     * Lands one dose of one axis on one body — the ONE place an effect becomes a state change,
     * shared by the trickle above and by {@link SpellVerb}'s instant parts. Held axes
     * ({@link EffectKind#TEMPERATURE}, {@link EffectKind#ATTRIBUTE}) write nothing here: they
     * ARE their live rows, and are read by summing them.
     */
    static void applyOnce(Actor target, EffectKind kind, int param, int magnitude) {
        if (kind != EffectKind.VITALITY) {
            return;
        }
        int max = target.stats().hp();
        int next = Math.max(VITALITY_FLOOR, Math.min(max, target.hp() + magnitude));
        target.setHp((short) next);
    }

    // ======================================================================
    // The persisted triad (rides the ActorsSystem chunk after the death log)
    // ======================================================================

    /** Serializes every row in canonical slot order, with the capacity as a framing guard. */
    public void serialize(DataOutput out) throws IOException {
        out.writeInt(SLOT_CAPACITY);
        for (int s = 0; s < SLOT_CAPACITY; s++) {
            out.writeInt(slotTarget[s]);
            out.writeByte(slotKind[s]);
            out.writeByte(slotMode[s]);
            out.writeByte(slotParam[s]);
            out.writeInt(slotMagnitude[s]);
            out.writeLong(slotStartTick[s]);
            out.writeLong(slotEndTick[s]);
        }
    }

    /** Loads what {@link #serialize} wrote; a capacity mismatch fails loudly (frame guard). */
    public void load(DataInput in) throws IOException {
        int capacity = in.readInt();
        if (capacity != SLOT_CAPACITY) {
            throw new IOException("active-effect frame mismatch: serialized capacity=" + capacity
                    + " but this build carries " + SLOT_CAPACITY);
        }
        for (int s = 0; s < SLOT_CAPACITY; s++) {
            slotTarget[s] = in.readInt();
            slotKind[s] = in.readByte();
            slotMode[s] = in.readByte();
            slotParam[s] = in.readByte();
            slotMagnitude[s] = in.readInt();
            slotStartTick[s] = in.readLong();
            slotEndTick[s] = in.readLong();
        }
    }

    /** Hashes exactly what {@link #serialize} writes, in the same order (landmine F). */
    public void hashInto(WorldHasher.Sink sink) {
        sink.putInt(SLOT_CAPACITY);
        for (int s = 0; s < SLOT_CAPACITY; s++) {
            sink.putInt(slotTarget[s]);
            sink.putByte(slotKind[s]);
            sink.putByte(slotMode[s]);
            sink.putByte(slotParam[s]);
            sink.putInt(slotMagnitude[s]);
            sink.putLong(slotStartTick[s]);
            sink.putLong(slotEndTick[s]);
        }
    }
}
