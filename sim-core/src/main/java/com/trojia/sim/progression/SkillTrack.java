package com.trojia.sim.progression;

import com.trojia.sim.world.io.WorldHasher;

import java.io.DataInput;
import java.io.DataOutput;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

/**
 * The per-entity skill-progression state: level and progress (in grains) for
 * every skill in a {@link SkillRegistry}, plus per-context satiation
 * (PROGRESSION-SPEC.md &sect;1, &sect;3). This is the pluggability seam
 * (this pass's DoD8): a value type keyed by an opaque {@code entityId}, with
 * zero dependency on {@code com.trojia.sim.actor}.
 *
 * <h2>Wiring status (Sprint 1 "the character sheet comes alive")</h2>
 * <p>This class is now LIVE: {@code com.trojia.sim.actor.SkillTrackRegistry}
 * owns one track per actor (dense, index == ActorId — the Home/Inventory
 * side-table shape this javadoc's original wiring note called for), routes
 * XP-worthy outcomes through {@link #awardXp}, and serializes/loads/hashes
 * every track via {@link #writeTo}/{@link #readFrom}/{@link #hashInto} inside
 * the {@code ActorsSystem} TROJSAV chunk. Attribute recomputation stays
 * <em>stateless</em> per &sect;5's "recomputed live" rule: consumers call
 * {@link AttributeCalculator#compute} against the current track at read time,
 * so there is no banked attribute to recompute on a
 * {@link SkillLevelledEvent} — the recompute is the read.</p>
 *
 * <h2>Satiation storage (the flatten, Sprint-1 risk note)</h2>
 * <p>Satiation was originally a {@code TreeMap<SatiationKey, SatiationEntry>};
 * it is now four parallel primitive arrays sorted by {@code (skillRaw,
 * contextKey)} with binary-search lookup/insert — the {@code ShoveLog}
 * primitive-parallel-arrays pattern — so the persisted triad
 * (serialize/load/hash) walks pure primitives in one canonical order and the
 * live state carries no {@code java.util} container. Semantics are identical:
 * lazy decay from {@code lastTick}, tier advance on award, per-context
 * isolation (all pinned by {@code SkillTrackTest}, unchanged).</p>
 *
 * <p>Not thread-safe; one {@link SkillTrack} belongs to exactly one entity's
 * owning system.</p>
 */
public final class SkillTrack {

    private final long entityId;
    private final SkillRegistry registry;
    private final int[] level;
    private final int[] progressGrains;

    // ---- satiation rows, sorted by (skillRaw, contextKey): parallel primitive arrays
    // (the ShoveLog pattern — no java.util container in live state). satCount rows live. ----
    private int[] satSkill = new int[0];
    private long[] satContext = new long[0];
    private int[] satTier = new int[0];
    private long[] satLastTick = new long[0];
    private int satCount;

    /**
     * Creates a fresh track (every skill at level 0, no progress, no
     * satiation history) for one entity against a registry.
     *
     * @param entityId the opaque owning entity id
     * @param registry the boot-built skill registry this track is sized against
     * @throws NullPointerException if {@code registry} is {@code null}
     */
    public SkillTrack(long entityId, SkillRegistry registry) {
        this.entityId = entityId;
        this.registry = Objects.requireNonNull(registry, "registry");
        this.level = new int[registry.size()];
        this.progressGrains = new int[registry.size()];
    }

    /** Returns the opaque entity id this track belongs to. */
    public long entityId() {
        return entityId;
    }

    /** Returns the registry this track is sized against. */
    public SkillRegistry registry() {
        return registry;
    }

    /**
     * Returns a skill's current level, {@code 0..100}.
     *
     * @param skill the skill
     * @return the level
     */
    public int level(SkillId skill) {
        return level[skill.raw()];
    }

    /** Returns a skill's current level by raw registry index, {@code 0..100}. */
    public int levelRaw(int skillRaw) {
        return level[skillRaw];
    }

    /**
     * Returns a skill's currently banked progress in grains (always
     * {@code < thresholdGrains(level)}, except that no such invariant is
     * enforced once level 100 is reached and further awards are simply
     * discarded, per &sect;1).
     *
     * @param skill the skill
     * @return the banked grains, {@code >= 0}
     */
    public int progressGrains(SkillId skill) {
        return progressGrains[skill.raw()];
    }

    /**
     * Returns the satiation tier currently in effect for a
     * {@code (skill, contextKey)} pair, after applying any decay owed since
     * the last award in that context (&sect;3.3) &mdash; without recording an
     * award. Useful for inspection/tests; {@link #awardXp} applies the same
     * decay internally before pricing the award.
     *
     * @param skill      the skill
     * @param contextKey the context discriminator (&sect;3.3's per-skill table)
     * @param currentTick the tick to evaluate decay against
     * @return the effective tier, {@code 0..4}
     */
    public int effectiveSatiationTier(SkillId skill, long contextKey, long currentTick) {
        int slot = satSlotOf(skill.raw(), contextKey);
        if (slot < 0) {
            return 0;
        }
        long elapsed = Math.max(0, currentTick - satLastTick[slot]);
        long decaySteps = elapsed / ProgressionMath.SATIATION_DECAY_TICKS;
        long decayed = satTier[slot] - decaySteps;
        return (int) Math.max(0, decayed);
    }

    /**
     * Awards XP for one qualifying use (PROGRESSION-SPEC.md &sect;3): prices
     * the award by the context's current satiation tier, adds the resulting
     * grains (saturating), advances the satiation tier for next time, and
     * levels up the skill through as many thresholds as the award covers
     * (looped, not recursive; excess carries).
     *
     * <p>At level 100, per &sect;1 ("no banking"), the award is discarded
     * entirely: no grains are added, no satiation is recorded, no event is
     * emitted. This keeps a capped skill's satiation history inert rather than
     * silently accumulating.</p>
     *
     * @param skill      the skill being used
     * @param baseCp     the qualifying-use base award in cp (&sect;3.1 table); {@code >= 0}
     * @param contextKey the satiation context discriminator (&sect;3.3)
     * @param currentTick the current simulation tick (for satiation decay)
     * @return the level-up events emitted, in level order; empty if no threshold was crossed
     * @throws IllegalArgumentException if {@code baseCp} is negative
     */
    public List<SkillLevelledEvent> awardXp(SkillId skill, int baseCp, long contextKey, long currentTick) {
        int idx = skill.raw();
        if (level[idx] >= ProgressionMath.MAX_LEVEL) {
            return List.of();
        }
        AptitudeTier apt = registry.get(idx).aptitudeTier();
        int tier = effectiveSatiationTier(skill, contextKey, currentTick);
        int grains = ProgressionMath.awardGrains(baseCp, tier);
        progressGrains[idx] = ProgressionMath.saturatingAdd(progressGrains[idx], grains);
        putSatiation(idx, contextKey,
                Math.min(tier + 1, ProgressionMath.MAX_SATIATION_TIER), currentTick);

        List<SkillLevelledEvent> events = new ArrayList<>();
        int lvl = level[idx];
        while (lvl < ProgressionMath.MAX_LEVEL) {
            int threshold = ProgressionMath.thresholdGrains(lvl, apt);
            if (progressGrains[idx] < threshold) {
                break;
            }
            progressGrains[idx] -= threshold;
            lvl++;
            events.add(new SkillLevelledEvent(entityId, idx, lvl));
        }
        level[idx] = lvl;
        return events;
    }

    // ======================================================================
    // Satiation row storage (sorted parallel primitive arrays)
    // ======================================================================

    /** Live satiation row count (inspection/serialization). */
    public int satiationSize() {
        return satCount;
    }

    /** The skill raw index of satiation row {@code i} (rows sorted by (skill, context)). */
    public int satiationSkillAt(int i) {
        return satSkill[i];
    }

    /** The context key of satiation row {@code i}. */
    public long satiationContextAt(int i) {
        return satContext[i];
    }

    /** The stored (undecayed) tier of satiation row {@code i}. */
    public int satiationTierAt(int i) {
        return satTier[i];
    }

    /** The last-award tick of satiation row {@code i}. */
    public long satiationLastTickAt(int i) {
        return satLastTick[i];
    }

    /** Binary search for {@code (skillRaw, contextKey)}: the row slot, or {@code -(ins+1)}. */
    private int satSlotOf(int skillRaw, long contextKey) {
        int lo = 0;
        int hi = satCount - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >>> 1;
            int cmp = Integer.compare(satSkill[mid], skillRaw);
            if (cmp == 0) {
                cmp = Long.compare(satContext[mid], contextKey);
            }
            if (cmp == 0) {
                return mid;
            }
            if (cmp < 0) {
                lo = mid + 1;
            } else {
                hi = mid - 1;
            }
        }
        return -(lo + 1);
    }

    /** Upserts one satiation row, keeping the arrays sorted by (skillRaw, contextKey). */
    private void putSatiation(int skillRaw, long contextKey, int tier, long lastTick) {
        int slot = satSlotOf(skillRaw, contextKey);
        if (slot >= 0) {
            satTier[slot] = tier;
            satLastTick[slot] = lastTick;
            return;
        }
        int insertAt = -(slot + 1);
        if (satCount == satSkill.length) {
            int newCap = Math.max(4, satSkill.length * 2);
            satSkill = java.util.Arrays.copyOf(satSkill, newCap);
            satContext = java.util.Arrays.copyOf(satContext, newCap);
            satTier = java.util.Arrays.copyOf(satTier, newCap);
            satLastTick = java.util.Arrays.copyOf(satLastTick, newCap);
        }
        System.arraycopy(satSkill, insertAt, satSkill, insertAt + 1, satCount - insertAt);
        System.arraycopy(satContext, insertAt, satContext, insertAt + 1, satCount - insertAt);
        System.arraycopy(satTier, insertAt, satTier, insertAt + 1, satCount - insertAt);
        System.arraycopy(satLastTick, insertAt, satLastTick, insertAt + 1, satCount - insertAt);
        satSkill[insertAt] = skillRaw;
        satContext[insertAt] = contextKey;
        satTier[insertAt] = tier;
        satLastTick[insertAt] = lastTick;
        satCount++;
    }

    // ======================================================================
    // The persisted triad (Sprint 1: tracks ride the ActorsSystem TROJSAV chunk)
    // ======================================================================

    /**
     * Serializes this track's full state in canonical order: per-skill levels
     * and banked grains (registry id order), then the satiation rows in their
     * sorted {@code (skillRaw, contextKey)} order. The skill count itself is
     * the owning registry's framing concern (one count for all dense tracks),
     * so it is deliberately not repeated per track.
     */
    public void writeTo(DataOutput out) throws IOException {
        for (int s = 0; s < level.length; s++) {
            out.writeInt(level[s]);
            out.writeInt(progressGrains[s]);
        }
        out.writeInt(satCount);
        for (int i = 0; i < satCount; i++) {
            out.writeInt(satSkill[i]);
            out.writeLong(satContext[i]);
            out.writeInt(satTier[i]);
            out.writeLong(satLastTick[i]);
        }
    }

    /**
     * Loads what {@link #writeTo} wrote into this (fresh, same-registry)
     * track. Satiation rows arrive already sorted (canonical write order), so
     * the load is a straight fill — no re-sort, byte-faithful round trip.
     */
    public void readFrom(DataInput in) throws IOException {
        for (int s = 0; s < level.length; s++) {
            level[s] = in.readInt();
            progressGrains[s] = in.readInt();
        }
        int count = in.readInt();
        satSkill = new int[count];
        satContext = new long[count];
        satTier = new int[count];
        satLastTick = new long[count];
        for (int i = 0; i < count; i++) {
            satSkill[i] = in.readInt();
            satContext[i] = in.readLong();
            satTier[i] = in.readInt();
            satLastTick[i] = in.readLong();
        }
        satCount = count;
    }

    /** Hashes the exact state {@link #writeTo} serializes, in the same canonical order. */
    public void hashInto(WorldHasher.Sink sink) {
        for (int s = 0; s < level.length; s++) {
            sink.putInt(level[s]);
            sink.putInt(progressGrains[s]);
        }
        sink.putInt(satCount);
        for (int i = 0; i < satCount; i++) {
            sink.putInt(satSkill[i]);
            sink.putLong(satContext[i]);
            sink.putInt(satTier[i]);
            sink.putLong(satLastTick[i]);
        }
    }
}
