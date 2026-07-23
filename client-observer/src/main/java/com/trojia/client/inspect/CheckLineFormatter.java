package com.trojia.client.inspect;

import com.trojia.sim.actor.SkillChecks;
import com.trojia.sim.actor.SkillTrackRegistry;

/**
 * CRPG-style skill-check result lines (Sprint 2 item 1's "visible dice"): the
 * Disco-Elysium-shaped {@code [Skyrunning 2 vs Onna Tidewatcher's Streetwise 0: 72% -- SUCCESS]}
 * rendered in the talk panel and the played actor's toasts. GL-free, pure formatting.
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
        int permille = SkillChecks.pickpocketContestPermille(tracks, thiefId, victimId);
        return "[Skyrunning " + tracks.level(thiefId, tracks.skyrunningRaw())
                + " vs " + victimName + "'s Streetwise "
                + tracks.level(victimId, tracks.streetwiseRaw())
                + ": " + (permille / 10) + "% -- " + outcome + "]";
    }
}
