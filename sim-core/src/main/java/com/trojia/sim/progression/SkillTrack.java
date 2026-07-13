package com.trojia.sim.progression;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.TreeMap;

/**
 * The per-entity skill-progression state: level and progress (in grains) for
 * every skill in a {@link SkillRegistry}, plus per-context satiation
 * (PROGRESSION-SPEC.md &sect;1, &sect;3). This is the pluggability seam
 * (this pass's DoD8): a value type keyed by an opaque {@code entityId}, with
 * zero dependency on {@code com.trojia.sim.actor}.
 *
 * <h2>Wiring note for a later "connect to Actor" pass</h2>
 * <p>This class never references Actor and never will &mdash; that dependency
 * must be added from the other side. The seam is exactly the same shape as
 * this repo's other per-actor side-tables (Home/Inventory/Relationship, per
 * the actor-system task running in parallel): a future pass should (1) add a
 * side-table {@code Map<ActorId, SkillTrack>} in (or alongside) ActorRegistry,
 * keyed the same way those other side-tables are keyed; (2) construct each
 * actor's {@code SkillTrack} against the shared, boot-built
 * {@link SkillRegistry} instance; (3) route XP-worthy outcomes (weapon hits,
 * blocks, sneak checks, brews, etc. &mdash; the qualifying uses enumerated in
 * PROGRESSION-SPEC.md &sect;3.1/&sect;3.2) through
 * {@link #awardXp(SkillId, int, long, long)} at the point those outcomes are
 * already being resolved (e.g. inside the combat-hit system, per
 * COMBAT-SPEC.md &sect;2.4); (4) feed the resulting {@link SkillLevelledEvent}s
 * either onto the real {@code SimEvent} bus (which requires adding this record
 * to {@code SimEvent}'s {@code permits} list &mdash; a change outside this
 * pass's scope) or into whatever observer-log channel Actor-side systems use
 * for non-bus facts; (5) feed attribute recomputation
 * ({@link AttributeCalculator#compute}) off the same events, per &sect;5's
 * "recomputed on every SkillLevelledEvent" rule.</p>
 *
 * <p>Not thread-safe; one {@link SkillTrack} belongs to exactly one entity's
 * owning system.</p>
 */
public final class SkillTrack {

    private final long entityId;
    private final SkillRegistry registry;
    private final int[] level;
    private final int[] progressGrains;
    private final TreeMap<SatiationKey, SatiationEntry> satiation;

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
        this.satiation = new TreeMap<>();
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
        SatiationEntry entry = satiation.get(new SatiationKey(skill.raw(), contextKey));
        if (entry == null) {
            return 0;
        }
        long elapsed = Math.max(0, currentTick - entry.lastTick());
        long decaySteps = elapsed / ProgressionMath.SATIATION_DECAY_TICKS;
        long decayed = entry.tier() - decaySteps;
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
        satiation.put(new SatiationKey(idx, contextKey),
                new SatiationEntry(Math.min(tier + 1, ProgressionMath.MAX_SATIATION_TIER), currentTick));

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
}
