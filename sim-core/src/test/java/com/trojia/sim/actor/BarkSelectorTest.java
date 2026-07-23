package com.trojia.sim.actor;

import com.trojia.sim.actor.faction.FactionRawsLoader;
import com.trojia.sim.actor.faction.FactionRegistry;
import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.job.JobRegistry;
import com.trojia.sim.actor.type.MilitiaWatch;
import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.actor.type.Wastrel;
import com.trojia.sim.bark.BarkTableRegistry;
import com.trojia.sim.world.PackedPos;

import org.junit.jupiter.api.Test;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The Sprint-2 bark selection core: a pure, deterministic function from readable sim state
 * to {@code (tableKey, rowDraw)} — the table-driven matrix over (faction standing x time x
 * status) the plan's DoD names, plus the disguise flip (the listener's PRESENTED id drives
 * the attitude) and the sparse-authoring fallback resolution.
 */
final class BarkSelectorTest {

    private static final long SEED = 42L;
    private static final FactionRegistry FACTIONS = FactionRawsLoader.load(committedRawsRoot());

    private static Path committedRawsRoot() {
        Path dir = Path.of("").toAbsolutePath();
        while (dir != null) {
            Path candidate = dir.resolve("content").resolve("raws");
            if (Files.isDirectory(candidate)) {
                return candidate;
            }
            dir = dir.getParent();
        }
        throw new IllegalStateException("content/raws not found above "
                + Path.of("").toAbsolutePath());
    }

    private static final class Fixture {
        final ActorRegistry registry = new ActorRegistry();
        final NoOpActorContext ctx = new NoOpActorContext(registry);
        final FactionStandings standings = new FactionStandings(FACTIONS);
        final RelationshipRegistry relationships = new RelationshipRegistry();
        final Actor guard;
        final Actor listener;

        Fixture() {
            guard = registry.spawn(MilitiaWatch.TYPE,
                    ActorTestFixtures.stats(MilitiaWatch.TYPE), PackedPos.pack(10, 10, 11));
            guard.setJobOrdinal((short) ctx.jobs().ordinalOf(Job.Watch.Patrol.ID));
            listener = registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE),
                    PackedPos.pack(11, 10, 11));
            listener.setJobOrdinal((short) ctx.jobs().ordinalOf(Job.Serf.Laborer.ID));
        }

        JobRegistry jobs() {
            return ctx.jobs();
        }

        Job guardJob() {
            return jobs().get(guard.jobOrdinal());
        }

        BarkSelector.BarkChoice greet(long tick) {
            return BarkSelector.select(SEED, tick, guard, guardJob(),
                    listener.identity().presentedId(), standings, relationships);
        }
    }

    @Test
    void theStandingByTimeMatrixSelectsTheDocumentedKeys() {
        Fixture f = new Fixture();
        int listener = f.listener.identity().presentedId();
        int watch = FACTIONS.rawId("watch");
        long morning = 3_000;
        long day = 8_000;
        long evening = 14_000;
        long night = 20_000;

        // Neutral standing across the four time buckets.
        assertEquals("greet.watch.neutral.morning", f.greet(morning).tableKey());
        assertEquals("greet.watch.neutral.day", f.greet(day).tableKey());
        assertEquals("greet.watch.neutral.evening", f.greet(evening).tableKey());
        assertEquals("greet.watch.neutral.night", f.greet(night).tableKey());

        // The standing ladder at one time of day.
        f.standings.adjust(listener, watch, 30);
        assertEquals("greet.watch.warm.day", f.greet(day).tableKey(),
                "+30 with the Watch: a friend of the garrison");
        f.standings.adjust(listener, watch, -60); // net -30
        assertEquals("greet.watch.cold.day", f.greet(day).tableKey(),
                "-30: a known offender is greeted cold");
        f.standings.adjust(listener, watch, -40); // net -70
        assertEquals("greet.watch.hostile.day", f.greet(day).tableKey(),
                "-70: the guild you robbed does not greet, it warns");
    }

    @Test
    void moodBitsOverrideTheGreetingInPriorityOrder() {
        Fixture f = new Fixture();
        long day = 8_000;
        f.guard.setStatus(StatusBit.MOVE_ALONG, true);
        assertEquals("mood.harried", f.greet(day).tableKey());
        f.guard.setStatus(StatusBit.PANICKED, true);
        assertEquals("mood.panicked", f.greet(day).tableKey());
        f.guard.setStatus(StatusBit.HOUSE_ARREST, true);
        assertEquals("mood.confined", f.greet(day).tableKey());
        f.guard.setStatus(StatusBit.HELD, true);
        assertEquals("mood.held", f.greet(day).tableKey());
        f.guard.setStatus(StatusBit.DOWNED, true);
        assertEquals("mood.downed", f.greet(day).tableKey());
        f.guard.setStatus(StatusBit.EXECUTED, true);
        assertEquals("mood.dead", f.greet(day).tableKey());
    }

    @Test
    void kinAndFriendTiesOutrankStandingAndReadThePresentedFace() {
        Fixture f = new Fixture();
        long day = 8_000;
        f.relationships.addSymmetric(f.guard.id(), f.listener.id(), RelationshipKind.FRIEND);
        assertEquals("greet.watch.friend.day", f.greet(day).tableKey());

        // The disguise flip: a THIRD actor presenting as the guard's friend is greeted as
        // the friend; dropping the mask restores the stranger's greeting.
        Actor stranger = f.registry.spawn(Wastrel.TYPE, ActorTestFixtures.stats(Wastrel.TYPE),
                PackedPos.pack(12, 10, 11));
        stranger.setJobOrdinal((short) f.jobs().ordinalOf(Job.Wastrel.Streetlife.ID));
        BarkSelector.BarkChoice asSelf = BarkSelector.select(SEED, day, f.guard, f.guardJob(),
                stranger.identity().presentedId(), f.standings, f.relationships);
        assertEquals("greet.watch.neutral.day", asSelf.tableKey());
        stranger.setActAs(f.listener.id());
        BarkSelector.BarkChoice disguised = BarkSelector.select(SEED, day, f.guard, f.guardJob(),
                stranger.identity().presentedId(), f.standings, f.relationships);
        assertEquals("greet.watch.friend.day", disguised.tableKey(),
                "the greeting follows the FACE presented (the Persona rule)");
    }

    @Test
    void aDisguisedVillainSpeaksAsItsCoverFamily() {
        Fixture f = new Fixture();
        Actor cutpurse = f.registry.spawn(Wastrel.TYPE, ActorTestFixtures.stats(Wastrel.TYPE),
                PackedPos.pack(13, 10, 11));
        cutpurse.setJobOrdinal((short) f.jobs().ordinalOf(Job.Villain.Cutpurse.ID));
        Job trueJob = f.jobs().get(cutpurse.jobOrdinal());
        BarkSelector.BarkChoice choice = BarkSelector.select(SEED, 8_000, cutpurse, trueJob,
                f.listener.identity().presentedId(), f.standings, f.relationships);
        assertEquals("greet.wastrel.neutral.day", choice.tableKey(),
                "the villain's voice is its COVER's voice — the secret does not leak");
    }

    @Test
    void selectionIsDeterministicAndTheDrawNeverTouchesSimCounters() {
        Fixture f = new Fixture();
        BarkSelector.BarkChoice first = f.greet(8_000);
        BarkSelector.BarkChoice again = f.greet(8_000);
        assertEquals(first, again, "same (seed, tick, speaker, state) -> same choice");
        // The draw is addressed (seed, tick, ACTOR_BARK, speaker, PRESENTATION_DRAW_INDEX)
        // and never touches a ctx counter (BarkSelector never sees the context), so asking
        // for a bark cannot perturb a running sim — reconstructable attribution:
        assertEquals(NamedDraws.draw(ActorRngStream.ACTOR_BARK, SEED, 8_000, f.guard.id(),
                BarkSelector.PRESENTATION_DRAW_INDEX), first.rowDraw());
        BarkSelector.BarkChoice nextTick = f.greet(8_001);
        assertEquals(first.tableKey(), nextTick.tableKey(),
                "the key is state-driven, not draw-driven");
    }

    @Test
    void resolveWalksTheSparseAuthoringFallbackChain() {
        BarkTableRegistry tables = BarkTableRegistry.of(List.of(
                new BarkTableRegistry.BarkTable("greet.watch",
                        List.of("The Watch sees you.")),
                new BarkTableRegistry.BarkTable("greet.watch.hostile.day",
                        List.of("You. Stay where you are.", "Hands where I can see them."))));
        BarkSelector.BarkChoice specific =
                new BarkSelector.BarkChoice("greet.watch.hostile.day", 7);
        assertEquals("Hands where I can see them.", specific.resolve(tables),
                "an authored exact key resolves its own rows (draw 7 % 2 = 1)");
        BarkSelector.BarkChoice fallback =
                new BarkSelector.BarkChoice("greet.watch.cold.night", 7);
        assertEquals("The Watch sees you.", fallback.resolve(tables),
                "an unauthored specific key walks up to greet.watch");
        BarkSelector.BarkChoice miss = new BarkSelector.BarkChoice("greet.serf.warm.day", 7);
        assertNull(miss.resolve(tables), "nothing authored anywhere on the chain -> silent");
    }
}
