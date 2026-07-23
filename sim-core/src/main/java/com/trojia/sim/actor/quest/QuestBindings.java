package com.trojia.sim.actor.quest;

/**
 * The scenario bake's symbol resolver (Sprint 3 quests): {@link QuestRegistry#bind} walks
 * the authored {@link QuestRaws} and asks this interface to turn every symbolic reference
 * into its baked binding — party symbols to TRUE actor ids (the notable bind map the bake
 * already owns), item symbols to {@code ItemKinds} shorts, zone symbols to indices into the
 * injected {@code RestrictedZoneTable}, cell symbols to packed world cells, and skill/
 * faction raws keys to their registry raw ids. Returning a negative value for any lookup
 * fails the bind loudly (the fail-fast bake contract) — a quest must never boot half-bound.
 *
 * <p>An interface rather than a data class so the bake can implement it as a thin adapter
 * over the maps it already builds (client-side, outside the actor purity gate) while
 * sim-core stays map-free.
 */
public interface QuestBindings {

    /** The TRUE actor id bound to {@code partySymbol} of quest {@code questId}. */
    int partyActorId(String questId, String partySymbol);

    /** The {@code ItemKinds} id bound to {@code itemSymbol} of quest {@code questId}. */
    short itemKind(String questId, String itemSymbol);

    /** The restricted-zone table INDEX bound to {@code zoneSymbol} of quest {@code questId}. */
    int zoneId(String questId, String zoneSymbol);

    /** The packed world cell bound to {@code cellSymbol} of quest {@code questId}. */
    int cell(String questId, String cellSymbol);

    /** The skill raw registry index for {@code skillKey} ({@code skills.json} id). */
    int skillRaw(String skillKey);

    /** The faction column for {@code factionKey} ({@code factions.json} id). */
    int factionId(String factionKey);
}
