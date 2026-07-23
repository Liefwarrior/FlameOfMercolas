package com.trojia.sim.actor;

import com.trojia.sim.actor.job.Job;
import com.trojia.sim.bark.BarkTableRegistry;

/**
 * The deterministic bark selection core (Sprint 2 rank 3): ONE pure function from readable
 * sim state — the speaker's presented job, its mood ({@code statusBits}), its attitude
 * toward the listener (relationship edge, then the LISTENER's standing with the SPEAKER's
 * faction), and the time of day — to a {@link BarkChoice}: a bark TABLE KEY (the schema
 * vocabulary in {@link com.trojia.sim.bark.BarkRawsLoader}'s javadoc) plus a row draw on
 * the existing {@code actor.bark} named stream.
 *
 * <p><b>Presentation-fed, sim-silent.</b> Selection is meant to be called from the
 * OBSERVER side (a speech bubble, a debug log): it reads sim state and computes a
 * {@link NamedDraws} value directly — {@code (worldSeed, tick, ACTOR_BARK,
 * speakerId, }{@link #PRESENTATION_DRAW_INDEX}{@code )} — WITHOUT touching the sim's
 * per-actor draw counter, so asking for a bark can never perturb a running simulation
 * (twin-run identity holds whether or not anyone listens). The pinned presentation lane
 * keeps the attribution reconstructable: same seed, same tick, same speaker — same bark,
 * every run, on every machine.
 *
 * <p><b>The reactivity payoff.</b> Attitude reads the listener's PRESENTED id (the Persona
 * rule): rob the Merchant Row and the counters greet you cold; put on a different face and
 * they greet THAT face — the "NPCs greet you differently after you robbed their guild"
 * loop, priced at one string lookup.
 */
public final class BarkSelector {

    /**
     * The reserved drawIndex lane for presentation-side bark draws — far above any
     * per-tick sim counter, so a bark draw can never collide with (or shift) a sim draw's
     * attribution.
     */
    public static final int PRESENTATION_DRAW_INDEX = 1 << 20;

    // ---- attitude thresholds on the listener's standing with the speaker's faction ----
    /** At/below: the speaker's faction actively hates this identity. */
    public static final int HOSTILE_STANDING = -60;
    /** At/below (and above hostile): a known offender against the speaker's faction. */
    public static final int COLD_STANDING = -20;
    /** At/above: a friend of the speaker's faction. */
    public static final int WARM_STANDING = 25;

    private BarkSelector() {
    }

    /**
     * A selected bark: the table key (the World-authored text tables' vocabulary) and the
     * raw row draw. {@link #rowIn} folds the draw onto a concrete table's row count;
     * {@link #resolve} additionally applies the documented sparse-authoring fallback chain
     * ({@code a.b.c.d} → {@code a.b.c} → {@code a.b}) against a wired registry.
     */
    public record BarkChoice(String tableKey, long rowDraw) {

        /** The selected row index for a table of {@code rowCount} rows (unsigned fold). */
        public int rowIn(int rowCount) {
            return rowCount <= 0 ? 0 : (int) Long.remainderUnsigned(rowDraw, rowCount);
        }

        /**
         * Resolves this choice to authored text, walking the fallback chain from the most
         * specific key to its dot-prefixes; {@code null} when nothing is authored (the
         * consumer stays silent).
         */
        public String resolve(BarkTableRegistry tables) {
            String key = tableKey;
            while (true) {
                int rows = tables.rowCount(key);
                if (rows > 0) {
                    return tables.row(key, rowIn(rows));
                }
                int cut = key.lastIndexOf('.');
                if (cut < 0) {
                    return null;
                }
                key = key.substring(0, cut);
            }
        }
    }

    /**
     * Selects the bark for {@code speaker} addressing {@code listenerPresentedId} at
     * {@code tick}. Pure and allocation-light; every input is observer-readable sim state.
     *
     * @param worldSeed           the one persisted seed
     * @param tick                the tick being rendered
     * @param speaker             the barking actor (mood bits, id)
     * @param speakerPresentedJob the speaker's PRESENTED job (resolve via
     *                            {@code ActorContext#presentedJob} or the observer's
     *                            equivalent) — a disguised villain speaks as its cover
     * @param listenerPresentedId the identity the listener PRESENTS (the Persona rule)
     * @param standings           the live standing ledger ({@code UNWIRED} degrades to
     *                            neutral attitude)
     * @param relationships       the relationship side-table (kin/friend recognition)
     */
    public static BarkChoice select(long worldSeed, long tick, Actor speaker,
            Job speakerPresentedJob, int listenerPresentedId, FactionStandings standings,
            RelationshipRegistry relationships) {
        long rowDraw = NamedDraws.draw(ActorRngStream.ACTOR_BARK, worldSeed, tick,
                speaker.id(), PRESENTATION_DRAW_INDEX);
        String mood = moodOf(speaker.statusBits());
        if (mood != null) {
            return new BarkChoice("mood." + mood, rowDraw);
        }
        String family = familyOf(speakerPresentedJob);
        String attitude = attitudeOf(speaker, speakerPresentedJob, listenerPresentedId,
                standings, relationships);
        String time = timeBucketOf(DailyRhythm.tickOfDay(tick));
        return new BarkChoice("greet." + family + "." + attitude + "." + time, rowDraw);
    }

    /** The mood override, in fixed priority order; {@code null} = no override (greet). */
    static String moodOf(short statusBits) {
        if (StatusBit.isSet(statusBits, StatusBit.EXECUTED)) {
            return "dead"; // the gibbet does not greet
        }
        if (StatusBit.isSet(statusBits, StatusBit.DOWNED)) {
            return "downed";
        }
        if (StatusBit.isSet(statusBits, StatusBit.HELD)) {
            return "held";
        }
        if (StatusBit.isSet(statusBits, StatusBit.HOUSE_ARREST)) {
            return "confined";
        }
        if (StatusBit.isSet(statusBits, StatusBit.PANICKED)) {
            return "panicked";
        }
        if (StatusBit.isSet(statusBits, StatusBit.MOVE_ALONG)) {
            return "harried";
        }
        return null;
    }

    /**
     * The speaker's presented job FAMILY key: the dotted job id's first segment
     * ({@code serf.laborer} → {@code serf}); a {@link Job.Villain} resolves through its
     * COVER (a disguised cutpurse speaks as a wastrel); a jobless actor (never true
     * post-bake; defensive) speaks as {@code commons}.
     */
    static String familyOf(Job presentedJob) {
        if (presentedJob == null) {
            return "commons";
        }
        String key = presentedJob instanceof Job.Villain villain
                ? villain.cover().presentedJob().value()
                : presentedJob.id().value();
        int cut = key.indexOf('.');
        return cut < 0 ? key : key.substring(0, cut);
    }

    /**
     * Attitude toward the listener: personal ties first (the speaker KNOWS its own kin,
     * grudges and friends — checked between the speaker's TRUE id and the face the
     * listener presents), then the listener's standing with the speaker's faction,
     * bucketed. Tie priority (Sprint 3): HOUSEHOLD &gt; GRUDGE &gt; FRIEND — kin forgive,
     * but a quest-minted grudge outweighs old friendship; the grudge is DIRECTED, so only
     * the holder's own greeting turns hostile (speaker TRUE id → listener PRESENTED id),
     * never the reverse. Unaffiliated speakers (wastrels, covers, beasts) read standing 0
     * — neutral.
     */
    static String attitudeOf(Actor speaker, Job presentedJob, int listenerPresentedId,
            FactionStandings standings, RelationshipRegistry relationships) {
        RelationshipKind tie = tieBetween(relationships, speaker.id(), listenerPresentedId);
        if (tie == RelationshipKind.HOUSEHOLD) {
            return "kin";
        }
        if (holdsGrudge(relationships, speaker.id(), listenerPresentedId)) {
            return "hostile"; // the quest ending stays audible forever, at zero content cost
        }
        if (tie == RelationshipKind.FRIEND) {
            return "friend";
        }
        int standing = 0;
        if (standings.isWired() && presentedJob != null) {
            String jobKey = presentedJob instanceof Job.Villain villain
                    ? villain.cover().presentedJob().value()
                    : presentedJob.id().value();
            int faction = standings.factions().factionOfJob(jobKey);
            if (faction >= 0) {
                standing = standings.standingOf(listenerPresentedId, faction);
            }
        }
        if (standing <= HOSTILE_STANDING) {
            return "hostile";
        }
        if (standing <= COLD_STANDING) {
            return "cold";
        }
        if (standing >= WARM_STANDING) {
            return "warm";
        }
        return "neutral";
    }

    /**
     * Whether {@code holderTrueId} holds a DIRECTED {@link RelationshipKind#GRUDGE} against
     * {@code objectPresentedId} (Sprint 3 quest outcomes). Direction matters: only
     * {@code fromId == holder && toId == object} edges bite — a grudge is personal, not
     * mutual, so the object of it greets normally.
     */
    private static boolean holdsGrudge(RelationshipRegistry relationships, int holderTrueId,
            int objectPresentedId) {
        for (int i = 0; i < relationships.size(); i++) {
            RelationshipEdge edge = relationships.get(i);
            if (edge.kind() == RelationshipKind.GRUDGE
                    && edge.fromId() == holderTrueId && edge.toId() == objectPresentedId) {
                return true;
            }
        }
        return false;
    }

    /** The strongest HOUSEHOLD/FRIEND edge between two ids, or {@code null}. */
    private static RelationshipKind tieBetween(RelationshipRegistry relationships, int a, int b) {
        RelationshipKind found = null;
        for (int i = 0; i < relationships.size(); i++) {
            RelationshipEdge edge = relationships.get(i);
            if (!((edge.fromId() == a && edge.toId() == b)
                    || (edge.fromId() == b && edge.toId() == a))) {
                continue;
            }
            if (edge.kind() == RelationshipKind.HOUSEHOLD) {
                return RelationshipKind.HOUSEHOLD; // the strongest tie wins outright
            }
            if (edge.kind() == RelationshipKind.FRIEND) {
                found = RelationshipKind.FRIEND;
            }
        }
        return found;
    }

    /** The DailyRhythm bucket: dawn 0 / noon 6000 / dusk 12000 / midnight 18000. */
    static String timeBucketOf(long tickOfDay) {
        if (tickOfDay < 6_000) {
            return "morning";
        }
        if (tickOfDay < 12_000) {
            return "day";
        }
        if (tickOfDay < 18_000) {
            return "evening";
        }
        return "night";
    }
}
