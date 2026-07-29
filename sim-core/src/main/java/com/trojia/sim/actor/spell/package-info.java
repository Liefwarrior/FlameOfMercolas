/**
 * SIMPLE MAGIC — public-shelf craftings, built as a vocabulary rather than as spells.
 *
 * <p><b>The premise.</b> The great libraries were opened by imperial order to any who can read,
 * so the ward's ordinary hands have a real but shallow grasp of how energy moves. Canon calls
 * the practice <em>crafting</em> and its unit a <em>link</em>: "a crafting requires a link
 * between the two entities, normally this would be an arm or a blade" (L459). The physics is
 * Gerik's lecture — heat is the currency (L445), too much of it into one spot simply hurts
 * (L448), distance and magnitude both bleed (L452), many small transfers beat one big one
 * (L457) — and the ceiling is the Tora tower: twenty full crafters pooled make one lit torch
 * flare (L567). A docker warming their hands sits comfortably under that line.
 *
 * <p><b>The design.</b> Daggerfall's five axes, all as data:
 * <ul>
 *   <li><b>effect</b> — {@link com.trojia.sim.actor.spell.EffectKind}: temperature, vitality,
 *       attribute. Three axes, not three spells.</li>
 *   <li><b>magnitude</b> — a signed integer in the axis's own units; the minus sign IS the
 *       opposite spell.</li>
 *   <li><b>duration</b> — {@link com.trojia.sim.actor.spell.EffectMode} plus a tick count.</li>
 *   <li><b>chance</b> — the {@code check.linkcraft} draw through the one
 *       {@code SkillChecks.successPermille} function.</li>
 *   <li><b>area</b> — {@link com.trojia.sim.actor.spell.TargetShape}, range and radius.</li>
 * </ul>
 * A {@link com.trojia.sim.actor.spell.SpellDefinition} is nothing but those numbers, loaded
 * from {@code content/raws/spells/spells.json}, and
 * {@link com.trojia.sim.actor.spell.SpellVerb} resolves any of them with no per-spell branch.
 * The fourth spell costs no Java; so does the fortieth.
 *
 * <p><b>The balance.</b> Nothing clamps magnitude. {@link com.trojia.sim.actor.spell.SpellCost}
 * prices what a crafting moves and how far it moves it, and the skill check does the rest, so a
 * spell nobody has reviewed cannot be accidentally strong. The single hard rule is structural:
 * {@link com.trojia.sim.actor.spell.ActiveEffects#VITALITY_FLOOR} means no crafting can put a
 * body on the ground, so magic never touches the ecology, the death feed or the justice
 * pipeline.
 *
 * <p><b>The state.</b> One table — {@link com.trojia.sim.actor.spell.ActiveEffects} — rides the
 * {@code ActorsSystem} persisted triad. Held effects are READ by summing live rows rather than
 * banked anywhere, so an effect cannot outlive its own expiry and a new axis adds no save frame.
 */
package com.trojia.sim.actor.spell;
