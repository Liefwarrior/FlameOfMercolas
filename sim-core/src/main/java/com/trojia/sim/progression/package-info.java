/**
 * The skill/attribute progression engine (PROGRESSION-SPEC.md &sect;2, &sect;3,
 * &sect;5, &sect;8): raws-loaded skill definitions ({@link
 * com.trojia.sim.progression.SkillRegistry}), the use-XP/grains/satiation
 * model ({@link com.trojia.sim.progression.SkillTrack}), attribute
 * derivation ({@link com.trojia.sim.progression.AttributeCalculator}), and
 * the evasion hook ({@link com.trojia.sim.progression.Evasion}) that
 * COMBAT-SPEC.md &sect;8 consumes.
 *
 * <p><strong>Self-contained by design:</strong> this package has zero
 * dependency on {@code com.trojia.sim.actor}. The pluggability seam is
 * {@link com.trojia.sim.progression.SkillTrack}, a value type keyed by an
 * opaque entity id (a {@code long}) rather than any Actor reference &mdash;
 * see that class's javadoc for the exact wiring steps a later pass needs to
 * connect this engine to the actor system.</p>
 *
 * <p><strong>Explicitly out of scope this pass</strong> (PROGRESSION-SPEC.md
 * &sect;7 ability effects, &sect;6 trainer/book acceleration mechanics): The
 * Flame exists in the skill table (aptitude tier {@link
 * com.trojia.sim.progression.AptitudeTier#FLAME}, governing attribute
 * {@link com.trojia.sim.progression.GoverningAttribute#NONE}) with the same
 * grains/threshold math as every other skill, but no unlock rite, calm gate,
 * or ability-grant logic is implemented here. Trainers and books are not
 * modeled at all (no raws field, no acceleration path) &mdash; both need
 * combat/economy design this pass does not own.</p>
 */
package com.trojia.sim.progression;
