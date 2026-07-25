package com.trojia.client.inspect;

import com.trojia.client.scenario.DocksPopulation;
import com.trojia.sim.actor.FactionStandings;
import com.trojia.sim.actor.SkillTrackRegistry;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;

/**
 * {@link CheckLineFormatter} — the Sprint-5 generalized visible-dice contract: the one
 * shared contest-line shape, the pre-existing pickpocket/search lines byte-stable through
 * the refactor, the new barter-quote decomposition and the inputs-only lenience line,
 * every degraded mode pinned. Headless, crafted tracks/standings, zero sim writes.
 */
class CheckLineFormatterTest {

    private final SkillTrackRegistry tracks = DocksPopulation.freshSkillTracks();
    private final FactionStandings standings = DocksPopulation.freshFactionStandings();

    private int watchFaction() {
        return standings.factions().rawId("watch");
    }

    private int merchantsFaction() {
        return standings.factions().rawId("merchants");
    }

    @Test
    void contestLineIsTheSharedShape() {
        assertEquals("[Skyrunning 2 vs Onna's Streetwise 0: 72% -- SUCCESS]",
                CheckLineFormatter.contestLine("Skyrunning", 2, "Onna's Streetwise 0",
                        726, "SUCCESS"));
    }

    @Test
    void pickpocketLineSurvivesTheRefactorByteStable() {
        // Level the thief once so the line carries real numbers (75 cp = Skyrunning...
        // skyrunning is FAVORED like streetwise: 1500 grains = level 1).
        tracks.award(4, tracks.skyrunningRaw(), 75, 1L, 1L);
        String line = CheckLineFormatter.pickpocketLine(tracks, 4, 7, "Onna", true);
        // The exact pre-Sprint-5 shape: skill levels + live contest permille as a percent.
        assertEquals("[Skyrunning 1 vs Onna's Streetwise 0: "
                + (com.trojia.sim.actor.SkillChecks.pickpocketContestPermille(tracks, 4, 7)
                        / 10) + "% -- SUCCESS]", line);
        assertEquals("[pickpocket -- CAUGHT]", CheckLineFormatter.pickpocketLine(
                SkillTrackRegistry.UNWIRED, 4, 7, "Onna", false));
    }

    @Test
    void searchLineKeepsItsFailureShape() {
        int streetwise = tracks.streetwiseRaw();
        assertEquals("[Streetwise 0 vs lock 12 - the drawer holds]",
                CheckLineFormatter.searchLine(tracks, 3, streetwise, 12));
        assertEquals("[search - the drawer holds]",
                CheckLineFormatter.searchLine(tracks, 3, -1, 12));
        assertEquals("[search - the drawer holds]",
                CheckLineFormatter.searchLine(SkillTrackRegistry.UNWIRED, 3, streetwise, 12));
    }

    @Test
    void barterQuoteDecomposesThePersonalQuote() {
        // A clean unskilled face: the flat base price, every component zero.
        assertEquals("[Streetwise 0: -0 haggle, -0 standing, +0 surcharge -- 5R]",
                CheckLineFormatter.barterQuoteLine(tracks, standings, 2, 2));

        // Streetwise 25+ haggles one Royal off (75 + 4425 cp on fresh contexts: FAVORED
        // cumulative grains to level 25 = 750 * 25 * 26 = 487,500 -> just seed it).
        tracks.seedLevel(2, tracks.streetwiseRaw(), 25);
        assertEquals("[Streetwise 25: -1 haggle, -0 standing, +0 surcharge -- 4R]",
                CheckLineFormatter.barterQuoteLine(tracks, standings, 2, 2));

        // Honest coin remembered (-2 standing at merchants +50); a stained watch record
        // surcharges (+1 at watch -20). Both on the PRESENTED face.
        standings.adjust(2, merchantsFaction(), 50);
        standings.adjust(2, watchFaction(), -20);
        assertEquals("[Streetwise 25: -1 haggle, -2 standing, +1 surcharge -- 3R]",
                CheckLineFormatter.barterQuoteLine(tracks, standings, 2, 2));
    }

    @Test
    void barterQuoteDegradesToRefusalAndSilence() {
        standings.adjust(5, watchFaction(), -70);
        assertEquals("[every counter refuses this face]",
                CheckLineFormatter.barterQuoteLine(tracks, standings, 5, 5));
        assertEquals("", CheckLineFormatter.barterQuoteLine(SkillTrackRegistry.UNWIRED,
                standings, 2, 2), "unwired tracks: the caller skips the toast");
        assertEquals("", CheckLineFormatter.barterQuoteLine(tracks,
                FactionStandings.UNWIRED, 2, 2), "unwired standings: same");
    }

    @Test
    void fishingLineReadsTheLiveCatchOdds() {
        // Untrained: Fishing 0 + AGI 10 vs deep-water 40 -> 350 + 10*(10-40) = 50,
        // clamped to the 100 floor.
        assertEquals("[Fishing 0 vs deep water 40: 10% -- GOT AWAY]",
                CheckLineFormatter.fishingLine(tracks, 3, 2, 40, false));
        // A trained fisher against inshore water: level moves the line live.
        tracks.seedLevel(3, tracks.fishingRaw(), 20);
        assertEquals("[Fishing 20 vs inshore water 0: 65% -- CAUGHT]",
                CheckLineFormatter.fishingLine(tracks, 3, 0, 0, true));
        assertEquals("[fishing -- CAUGHT]",
                CheckLineFormatter.fishingLine(SkillTrackRegistry.UNWIRED, 3, 1, 20, true));
    }

    @Test
    void lenienceLineShowsTheInputsTheDrawRead() {
        tracks.seedLevel(6, tracks.streetwiseRaw(), 12);
        standings.adjust(6, watchFaction(), -20);
        assertEquals("[the Watch weighs the face you wear: standing -20, Streetwise 12"
                        + " -- WARNED]",
                CheckLineFormatter.lenienceLine(tracks, standings, 6, 6, "WARNED"));
        assertEquals("[the Watch weighs the face you wear: standing +0, Streetwise 0"
                        + " -- FINED & HELD]",
                CheckLineFormatter.lenienceLine(tracks, standings, 8, 8, "FINED & HELD"));
        assertEquals("[the Watch weighs the face you wear -- WARNED]",
                CheckLineFormatter.lenienceLine(SkillTrackRegistry.UNWIRED, standings,
                        6, 6, "WARNED"));
    }
}
