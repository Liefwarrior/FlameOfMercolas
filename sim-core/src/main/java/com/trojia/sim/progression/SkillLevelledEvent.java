package com.trojia.sim.progression;

/**
 * Emitted when a skill crosses a level threshold (PROGRESSION-SPEC.md
 * &sect;1). A single {@link SkillTrack#awardXp} call may emit several of
 * these in one loop (multi-level carries, &sect;1) &mdash; never recursive.
 *
 * <p>This is a self-contained progression-package record, <strong>not</strong>
 * a member of {@code com.trojia.sim.event.SimEvent}'s sealed hierarchy: this
 * pass is scoped to {@code com.trojia.sim.progression} only and must not
 * modify files outside it (including the sealed event interface's
 * {@code permits} list). See {@link SkillTrack}'s class javadoc for the wiring
 * note a later pass would need to route this onto the real event bus.</p>
 *
 * @param entityId the opaque entity id owning the {@link SkillTrack}
 * @param skillRaw the skill's raw registry index
 * @param newLevel the level just reached, {@code 1..100}
 */
public record SkillLevelledEvent(long entityId, int skillRaw, int newLevel) {
}
