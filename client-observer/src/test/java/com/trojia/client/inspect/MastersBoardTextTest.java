package com.trojia.client.inspect;

import com.trojia.client.scenario.CompoundBlockPopulation;
import com.trojia.client.scenario.DocksPopulation;
import com.trojia.client.scenario.IdentityRegistry;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.DailyRhythm;
import com.trojia.sim.actor.SkillTrackRegistry;

import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * {@link MastersBoardText}/{@link MastersBoardSnapshot} contract (Sprint 5 item 3): per
 * craft the district's best by ward-facing name with the adept-band census, deterministic
 * ordering (raw asc; ties to the lowest id), the presented-identity rule, and the
 * climbers-since-dawn read off the daily snapshot. Headless, crafted tracks over the
 * compound registry (un-forged names — the degraded "Type #id" style), zero sim writes.
 */
class MastersBoardTextTest {

    private final CompoundBlockPopulation p = CompoundBlockPopulation.build(1234L);
    private final SkillTrackRegistry tracks = DocksPopulation.freshSkillTracks();

    private List<String> lines(MastersBoardSnapshot snapshot) {
        return MastersBoardText.lines(tracks, p.registry(), IdentityRegistry.EMPTY, snapshot);
    }

    @Test
    void unschooledWardListsEveryCraftEmpty() {
        MastersBoardSnapshot snapshot = new MastersBoardSnapshot(tracks, p.registry().size());
        List<String> lines = lines(snapshot);
        assertEquals("The Ward's Masters", lines.get(0));
        // Title + blank + one row per skill + blank + climbers marker + placeholder.
        assertEquals(2 + tracks.skills().size() + 2 + 1, lines.size());
        assertTrue(lines.get(2).endsWith("(unschooled)"), lines.get(2));
        assertEquals("(no growth yet today)", lines.get(lines.size() - 1));
    }

    @Test
    void bestSoulIsNamedWithLevelAndAdeptCensus() {
        int streetwise = tracks.streetwiseRaw();
        // Three souls: #5 best (seeded 40), #3 adept (12), #7 novice (1).
        tracks.seedLevel(5, streetwise, 40);
        tracks.seedLevel(3, streetwise, 12);
        tracks.seedLevel(7, streetwise, 1);
        MastersBoardSnapshot snapshot = new MastersBoardSnapshot(tracks, p.registry().size());

        List<String> lines = lines(snapshot);
        String row = lines.stream().filter(l -> l.startsWith("Streetwise")).findFirst()
                .orElseThrow();
        assertTrue(row.contains("#5") && row.contains(" 40"),
                "the best soul by name and level: " + row);
        assertTrue(row.endsWith("(2 adept+)"),
                "the adept census counts levels >= " + MastersBoardText.ADEPT_LEVEL + ": " + row);
    }

    @Test
    void bestOfTieBreaksToTheLowestActorId() {
        int grit = tracks.gritRaw();
        tracks.seedLevel(9, grit, 20);
        tracks.seedLevel(4, grit, 20);
        MastersBoardSnapshot snapshot = new MastersBoardSnapshot(tracks, p.registry().size());
        String row = lines(snapshot).stream().filter(l -> l.startsWith("Grit")).findFirst()
                .orElseThrow();
        assertTrue(row.contains("#4"), "deterministic tie-break to the lowest id: " + row);
    }

    @Test
    void theBoardNamesThePresentedFace() {
        int seacraft = tracks.skills().id("seacraft").raw();
        ActorRegistry registry = p.registry();
        tracks.seedLevel(2, seacraft, 30);
        try {
            registry.get(2).setActAs(6); // #2 works the decks wearing #6's face
            MastersBoardSnapshot snapshot =
                    new MastersBoardSnapshot(tracks, registry.size());
            String row = lines(snapshot).stream().filter(l -> l.startsWith("Seacraft"))
                    .findFirst().orElseThrow();
            assertTrue(row.contains("#6") && row.contains(" 30"),
                    "the ward names the face it sees; the level is the true body's: " + row);
        } finally {
            registry.get(2).setActAs(2);
        }
    }

    @Test
    void climbersReadGrowthSinceTheDawnBaselineAndRebaselineDaily() {
        int streetwise = tracks.streetwiseRaw();
        tracks.seedLevel(5, streetwise, 40); // a seeded master, standing still
        MastersBoardSnapshot snapshot = new MastersBoardSnapshot(tracks, p.registry().size());
        assertEquals(0, snapshot.climbSinceDawn(5),
                "a seeded master is not a climber for standing still");

        // #3 earns two levels today (75 cp = level 1; +150 cp on fresh contexts -> level 2:
        // cumulative FAVORED grains to level 2 = 4500 = 75+150 cp at tier 0).
        tracks.award(3, streetwise, 75, 1L, 10L);
        tracks.award(3, streetwise, 150, 2L, 11L);
        assertEquals(2, snapshot.climbSinceDawn(3));
        List<String> lines = lines(snapshot);
        assertTrue(lines.get(lines.size() - 1).contains("#3")
                        && lines.get(lines.size() - 1).endsWith("+2 levels"),
                "the top climber row: " + lines.get(lines.size() - 1));

        // The next dawn re-baselines: today's climb becomes yesterday's news.
        snapshot.afterTick(DailyRhythm.DAY + 1);
        assertEquals(0, snapshot.climbSinceDawn(3), "dawn re-baselines the climb");
        assertEquals("(no growth yet today)", lines(snapshot).get(lines(snapshot).size() - 1));
    }
}
