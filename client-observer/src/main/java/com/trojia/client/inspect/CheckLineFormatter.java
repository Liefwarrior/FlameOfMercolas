package com.trojia.client.inspect;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.Barter;
import com.trojia.sim.actor.FactionStandings;
import com.trojia.sim.actor.FoodEconomy;
import com.trojia.sim.actor.SkillChecks;
import com.trojia.sim.actor.SkillTrackRegistry;

/**
 * CRPG-style skill-check result lines (Sprint 2 item 1's "visible dice", generalized in
 * Sprint 5 to every skill-read the client can narrate): the Disco-Elysium-shaped
 * {@code [Skyrunning 2 vs Onna Tidewatcher's Streetwise 0: 72% -- SUCCESS]} rendered in
 * the talk panel and the played actor's toasts. GL-free, pure formatting.
 *
 * <p><b>The contest-line contract.</b> {@link #contestLine} is THE published shape for
 * skill-decided outcomes — {@code [<Skill> <level> vs <opposition>: <odds>% -- <OUTCOME>]}
 * — and every new Sim read site that wants narration lands through it (or a sibling
 * below) as it ships. Sites whose odds the sim does not publish narrate their INPUTS
 * instead ({@link #lenienceLine}) — the formatter never re-derives a sim-private formula.
 *
 * <p><b>Honesty note.</b> Levels and odds are read from the LIVE skill table at narration
 * time — one tick after the roll, by which point a successful lift's own use-XP award may
 * already have nudged the thief's level. The line describes the contest as the sheet now
 * shows it; the authoritative roll stays reconstructable from the named
 * {@code check.pickpocket} stream (presentation never pretends to be the ledger).
 */
public final class CheckLineFormatter {

    private CheckLineFormatter() {
    }

    /**
     * The one shared contest-line shape:
     * {@code [Skyrunning 2 vs Onna's Streetwise 0: 72% -- SUCCESS]}.
     *
     * @param skillName  the acting skill's display name
     * @param level      the actor's live level in it
     * @param opposition the defending clause ({@code "Onna's Streetwise 0"}, {@code "lock 12"})
     * @param permille   the live odds in permille (rendered as a percentage)
     * @param outcome    the resolved outcome word(s)
     */
    public static String contestLine(String skillName, int level, String opposition,
            int permille, String outcome) {
        return "[" + skillName + " " + level + " vs " + opposition + ": "
                + (permille / 10) + "% -- " + outcome + "]";
    }

    /**
     * The pickpocket contest line for {@code thiefId} vs {@code victimId} ({@code victimName}
     * is the ward-facing name the caller already resolved). Degrades to a numberless tag when
     * the skill table is unwired.
     */
    public static String pickpocketLine(SkillTrackRegistry tracks, int thiefId, int victimId,
            String victimName, boolean success) {
        String outcome = success ? "SUCCESS" : "CAUGHT";
        if (!tracks.isWired()) {
            return "[pickpocket -- " + outcome + "]";
        }
        return contestLine("Skyrunning", tracks.level(thiefId, tracks.skyrunningRaw()),
                victimName + "'s Streetwise " + tracks.level(victimId, tracks.streetwiseRaw()),
                SkillChecks.pickpocketContestPermille(tracks, thiefId, victimId), outcome);
    }

    /**
     * The quest search-check FAILURE line (S3, the drawer pry): the searcher's live skill
     * level against the authored lock resist — {@code [Streetwise 3 vs lock 12 - the
     * drawer holds]}. Failure-only by design: a successful search advances the stage and
     * gets the quest feed's own narration. Degrades to a numberless tag when the skill
     * table is unwired or the stage declares no search ({@code skillRaw < 0}).
     */
    public static String searchLine(SkillTrackRegistry tracks, int searcherId, int skillRaw,
            int resist) {
        if (!tracks.isWired() || skillRaw < 0) {
            return "[search - the drawer holds]";
        }
        return "[" + tracks.skills().get(skillRaw).displayName() + " "
                + tracks.level(searcherId, skillRaw) + " vs lock " + resist
                + " - the drawer holds]";
    }

    /**
     * The barter-quote decomposition (Sprint 5 — the haggle read made visible): the played
     * actor's PERSONAL meal quote rebuilt from the same published components
     * {@link Barter#quoteFor} folds — {@code [Streetwise 12: -0 haggle, -1 standing, +1
     * surcharge -- 6R]} — so a Morrowind player can see exactly what their streetwise and
     * reputation buy at the counter. Live reads at narration time (the honesty note); the
     * line is the personal quote — a kin/friend counter may charge
     * {@value Barter#RELATIONSHIP_DISCOUNT} less. Degrades to {@code ""} (caller skips)
     * when either table is unwired; a refused face gets the refusal tag.
     *
     * <p>Standings key on the PRESENTED id, haggling on the TRUE body — exactly the
     * components the sim's own quote read.
     */
    public static String barterQuoteLine(SkillTrackRegistry tracks, FactionStandings standings,
            int actorId, int presentedId) {
        if (!tracks.isWired() || !standings.isWired()) {
            return "";
        }
        int watch = standings.watchStanding(presentedId);
        if (watch <= Barter.REFUSAL_WATCH_STANDING) {
            return "[every counter refuses this face]";
        }
        int level = tracks.level(actorId, tracks.streetwiseRaw());
        int haggle = level / Barter.STREETWISE_PER_DISCOUNT;
        int standing = Math.max(0, standings.merchantsStanding(presentedId))
                / Barter.MERCHANTS_PER_DISCOUNT;
        int surcharge = Math.max(0, -watch) / Barter.WATCH_PER_SURCHARGE;
        long price = Math.max(Barter.MIN_PRICE, Math.min(Barter.MAX_PRICE,
                FoodEconomy.FOOD_PRICE - standing - haggle + surcharge));
        return "[Streetwise " + level + ": -" + haggle + " haggle, -" + standing
                + " standing, +" + surcharge + " surcharge -- " + price + "R]";
    }

    /** Human words for the three {@code FishingZoneTable} size classes, by ordinal. */
    static final String[] WATER_WORDS = {"inshore water", "pier-side water", "deep water"};

    /**
     * The fishing catch-check line (Sprint 6 — the cast's visible dice):
     * {@code [Fishing 3 vs deep water 40: 34% -- CAUGHT]} through the shared
     * {@link #contestLine} shape, odds from the sim's own
     * {@link SkillChecks#fishCatchPermille} at narration time (the honesty note — the
     * attempt's own XP may already have nudged the level the line shows). Degrades to a
     * numberless tag when the skill table is unwired.
     *
     * @param sizeClass the spot's {@code FishingZoneTable} size-class ordinal
     * @param resist    the spot's catch resist ({@code FishingSpots.catchResistAt})
     */
    public static String fishingLine(SkillTrackRegistry tracks, int fisherId, int sizeClass,
            int resist, boolean success) {
        String outcome = success ? "CAUGHT" : "GOT AWAY";
        if (!tracks.isWired() || tracks.fishingRaw() == Actor.NONE) {
            return "[fishing -- " + outcome + "]";
        }
        return contestLine(tracks.skills().get(tracks.fishingRaw()).displayName(),
                tracks.level(fisherId, tracks.fishingRaw()),
                WATER_WORDS[sizeClass] + " " + resist,
                SkillChecks.fishCatchPermille(tracks, fisherId, resist), outcome);
    }

    /**
     * The cull check's visible dice (S8 scalps): {@code [Fieldcraft 3 vs Wharf Cat 18: 45% --
     * SCALP TAKEN]} through the shared {@link #contestLine} shape, odds from the sim's own
     * {@link SkillChecks#cullPermille} at narration time (the honesty note — the attempt's own
     * XP may already have nudged the level the line shows). The opposition clause names the
     * quarry the way the ward would: the beast's display name and its authored resist.
     * Degrades to a numberless tag when the skill table is unwired.
     *
     * @param quarryName the quarry type's display name ({@code "Quay Mouse"})
     * @param resist     the quarry type's authored {@code scalpResist}
     */
    public static String cullLine(SkillTrackRegistry tracks, int cullerId, String quarryName,
            int resist, boolean success) {
        String outcome = success ? "SCALP TAKEN" : "PELT RUINED";
        if (!tracks.isWired() || tracks.fieldcraftRaw() == Actor.NONE) {
            return "[cull -- " + outcome + "]";
        }
        return contestLine(tracks.skills().get(tracks.fieldcraftRaw()).displayName(),
                tracks.level(cullerId, tracks.fieldcraftRaw()),
                quarryName + " " + resist,
                SkillChecks.cullPermille(tracks, cullerId, resist), outcome);
    }

    /**
     * The Watch-lenience line (Sprint 5 — the justice pipeline's first skill read, on the
     * played soul): {@code [the Watch weighs the face you wear: standing -20, Streetwise
     * 12 -- WARNED]}. INPUTS-ONLY by design: the warn-vs-fine threshold formula is
     * sim-package-private, so the line shows exactly what the draw read — the presented
     * face's Watch standing and the true body's streetwise — never a re-derived
     * percentage (the formatter's no-formula-duplication rule above). Degrades numberless
     * when either table is unwired.
     */
    public static String lenienceLine(SkillTrackRegistry tracks, FactionStandings standings,
            int actorId, int presentedId, String outcome) {
        if (!tracks.isWired() || !standings.isWired()) {
            return "[the Watch weighs the face you wear -- " + outcome + "]";
        }
        int standing = standings.watchStanding(presentedId);
        int streetwise = tracks.level(actorId, tracks.streetwiseRaw());
        return "[the Watch weighs the face you wear: standing "
                + (standing >= 0 ? "+" + standing : Integer.toString(standing))
                + ", Streetwise " + streetwise + " -- " + outcome + "]";
    }
}
