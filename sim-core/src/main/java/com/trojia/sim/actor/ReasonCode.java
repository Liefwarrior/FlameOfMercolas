package com.trojia.sim.actor;

/**
 * The legibility reason code carried by a policy change (ACTORS-SPEC.md
 * §1.2/§11.6): "if the observer can't reconstruct WHY, the emergence is
 * wasted." Append-only.
 */
public enum ReasonCode {
    NEED_HUNGER_LOW,
    NEED_REST_LOW,
    RHYTHM_NIGHT_HOME,
    STIM_FIRE_SEEN,
    STIM_CRIME_SEEN,
    STIM_ALARM_HEARD,
    TARGET_LOST,
    TIMER_EXPIRED,
    DEFERENCE,
    JOB_GOAL,
    IDLE_DEFAULT,
    SAFETY_CRITICAL,
    ARRESTED,
    HELD_IN_CUSTODY,
    RELEASED_FROM_CUSTODY,
    MAIMED_FIRST_OFFENSE,
    EXECUTED_SECOND_OFFENSE,
    /** Play mode: this tick's movement/idle came from direct human input (PLAY-MODE-SPEC.md §5.2). */
    PLAYER_CONTROLLED,
    /** Ate a FOOD from a home larder or a free commons cell (economy-loop pass). */
    ATE_FOOD,
    /** Bought a FOOD at a shop counter (ID-authorized Royal transfer) and ate it (economy-loop pass). */
    BOUGHT_FOOD,
    /** A guard is actively intercepting/holding a locked offender (law &amp; order pass, APPREHEND). */
    APPREHENDING,
    /** An offender was told to move along (first-contact warning; leave the zone and it clears). */
    WARNED_MOVE_ALONG,
    /** Ate a FOOD scrap off a garbage-bin cell — the broke's last resort (law &amp; order pass). */
    SCAVENGED_FOOD,
    /** A hungry beast is chasing a locked live mouse (beast food channel, BEAST_HUNT). */
    HUNTING,
    /** A predator caught its mouse at adjacency and restored HUNGER (no FOOD item involved). */
    ATE_PREY,
    /** This mouse was just caught: DOWNED with a revive countdown on {@code downedTimer}. */
    PREY_CAUGHT,
    /** A mouse nibbled crumbs/spilled grain around its den at a wander-dwell boundary. */
    NIBBLED_DEN,
    /** The Watch sensed a shove riot and sent this shover home under a 1-day house arrest. */
    HOUSE_ARRESTED,
    /** Serving house arrest: walking home / held at home sleeping until the deadline. */
    UNDER_HOUSE_ARREST,
    /** The house-arrest deadline passed — released back to ordinary life. */
    RELEASED_FROM_HOUSE_ARREST,
    /** Lifted a mark's pocket coin clean (Sprint 2 theft: a won pickpocket contest). */
    PICKPOCKETED,
    /** A pickpocket attempt failed — the mark caught the hand (a witnessed crime row). */
    CAUGHT_STEALING,
    /** A guard corrected a witnessed theft: fine seized + custody (Sprint 2 justice). */
    ARRESTED_FOR_THEFT,
    /** Play mode: this tick the played actor TALKED to an adjacent actor (Sprint 3 quests). */
    TALKED,
    /** A quest stage advanced for this actor this tick ({@code QuestEngine}, Sprint 3). */
    QUEST_ADVANCED,
    /**
     * Play mode: the eat intent resolved to NOTHING — no carried ration, no affordable
     * willing counter, larder, commons or (if broke) stocked bin within reach (Sprint 4's
     * played-actor eat verb; the client's "nothing to eat here" toast reads this).
     */
    NO_MEAL_IN_REACH,
    /** A completed cast landed a fish (Sprint 6 fishing — FISH minted into the carry). */
    CAUGHT_FISH,
    /** A completed cast came up empty (the catch check failed; XP still earned). */
    FISH_GOT_AWAY,
    /** A fisher sold its catch at a shop's buy-side counter (the coin faucet firing). */
    SOLD_CATCH,
    /** Play mode: the fish intent found no live, perceived spot within casting reach. */
    NO_SPOT_IN_REACH,
    /**
     * Terminal starvation (Sprint 6 death): HUNGER sat at 0 for the whole long grace
     * window and this soul died of it — {@code DEAD} set, inert forever, by name in the feed.
     */
    STARVED_TO_DEATH,
    /**
     * S8 cull: a clean harvest — the named scalp is minted into this soul's carry
     * ({@code CullVerb}). Vermin only this arc; the body's revive timer is untouched.
     */
    TOOK_SCALP,
    /** S8 cull: the knife slipped and the pelt was spoiled — the check failed, no scalp. */
    SCALP_RUINED,
    /** Play mode: the cull intent found no downed scalpable body within knife reach. */
    NO_QUARRY_IN_REACH,
    /** S8 counter sale: carried MATERIALS changed hands for Royals ({@code SellVerb}). */
    SOLD_GOODS,
    /** Play mode: the sell intent found no willing, coin-carrying counter within reach. */
    NO_BUYER_IN_REACH,
    /**
     * Simple Magic: a crafting took — the link opened and every one of the spell's components
     * landed on its target ({@code SpellVerb}). What actually changed is the spell's own data.
     */
    SPELL_WORKED,
    /** Simple Magic: the link would not open. The check failed; the use-XP was earned anyway. */
    SPELL_FIZZLED,
    /** Play mode: the cast intent found nothing this crafting could reach (the bridge rule). */
    NO_LINK_TO_TARGET,
    /**
     * Simple Magic: the ward's lingering-effect table had no free rows for what this crafting
     * would have filed, so the cast was refused BEFORE it cost anything. The alternative was to
     * evict somebody else's live effect silently, which is the same lie as a cast that lands
     * nothing ({@code ActiveEffects.freeSlots}).
     */
    NO_ROOM_FOR_CRAFTING,
    /**
     * Simple Magic: this body does not read at all, so no crafting is available to it — the
     * literacy gate ({@code SpellVerb.isLiterate}). A gull cannot use a library.
     */
    CANNOT_READ_A_CRAFTING,
    /**
     * Simple Magic: the reader has not got deep enough into the shelf for this row — its live
     * level in the crafting's OWN declared skill is under {@code SpellDefinition.minLevel}. The
     * public-issue edition against the restricted one on the top shelf (L2472).
     */
    CRAFTING_UNREAD,
    /**
     * Simple Magic: the hand is still on the last link — this caster's cast latch has not run
     * out ({@code Actor.castUntilTick}). Stamped by the shared verb rather than by whichever
     * caller happened to remember the rule.
     */
    CRAFTING_HAND_LATCHED
}
