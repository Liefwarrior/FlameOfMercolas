package com.trojia.sim.actor;

import com.trojia.sim.actor.job.JobBehaviors;
import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.actor.type.Wastrel;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Sprint 6 slice 6 (Eli's bug 7 — "it should be rare but we need people to die"): DEATH.
 * Terminal starvation kills only after the LONG grace (days at HUNGER 0, any meal resets the
 * whole clock); a hanging now actually kills; the dead are inert forever — skipped by
 * {@code tickAll} (no needs decay, no policies), resident in the roster forever, their tile
 * vacated for the living, their death announced by row in the {@link DeathLog}.
 */
final class DeathTest {

    private static final int Z = 1;

    private static Actor serfAt(ActorRegistry registry, int x, int y) {
        return registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE),
                PackedPos.pack(x, y, Z));
    }

    /** Ctx double with a live death log so recordDeath rows are observable. */
    private static final class DeathContext extends NoOpActorContext {
        final DeathLog deaths = new DeathLog(16);

        DeathContext(ActorRegistry registry) {
            super(registry);
        }

        @Override
        public void recordDeath(int actorId, ReasonCode cause) {
            deaths.record(tick(), actorId, cause);
        }
    }

    @Test
    void terminalStarvationKillsOnlyAfterTheWholeGrace() {
        ActorRegistry registry = new ActorRegistry();
        Actor soul = serfAt(registry, 5, 5);
        DeathContext ctx = new DeathContext(registry);
        soul.setHomeId(ctx.homes().addHome(soul.cell()));
        soul.applyNeedDelta(Need.HUNGER, -10_000); // the spell begins

        ctx.setTick(1_000);
        soul.tick(ctx); // stamps starvingSinceTick
        assertEquals(1_000, soul.starvingSinceTick());
        assertFalse(soul.isDead());

        ctx.setTick(1_000 + Actor.STARVATION_GRACE_TICKS - 1);
        soul.tick(ctx);
        assertFalse(soul.isDead(), "one tick short of the grace: still alive");

        ctx.setTick(1_000 + Actor.STARVATION_GRACE_TICKS);
        soul.tick(ctx);
        assertTrue(soul.isDead(), "the whole grace ran out at HUNGER 0: dead");
        assertTrue(soul.hasStatus(StatusBit.DOWNED), "corpses read DOWNED for rendering");
        assertEquals(ReasonCode.STARVED_TO_DEATH, soul.lastReasonCode());
        assertEquals(1, ctx.deaths.size(), "the death was announced");
        assertEquals(soul.id(), ctx.deaths.actorIdAt(0));
        assertEquals(ReasonCode.STARVED_TO_DEATH, ctx.deaths.causeAt(0));
    }

    @Test
    void anyMealResetsTheWholeStarvationClock() {
        ActorRegistry registry = new ActorRegistry();
        Actor soul = serfAt(registry, 5, 5);
        DeathContext ctx = new DeathContext(registry);
        soul.setHomeId(ctx.homes().addHome(soul.cell()));
        soul.applyNeedDelta(Need.HUNGER, -10_000);

        ctx.setTick(1_000);
        soul.tick(ctx);
        assertEquals(1_000, soul.starvingSinceTick());

        // A scrap off a bin, days into the spell: the clock resets completely.
        ctx.setTick(1_000 + Actor.STARVATION_GRACE_TICKS - 5);
        soul.applyNeedDelta(Need.HUNGER, 500);
        soul.tick(ctx);
        assertEquals(Actor.NEVER_STARVING, soul.starvingSinceTick(),
                "one meal — even one scrap — resets the whole grace");
        assertFalse(soul.isDead());
    }

    @Test
    void aHangingNowActuallyKills() {
        ActorRegistry registry = new ActorRegistry();
        Actor skyrunner = registry.spawn(Wastrel.TYPE,
                ActorTestFixtures.stats(Wastrel.TYPE), PackedPos.pack(8, 8, Z));
        DeathContext ctx = new DeathContext(registry);
        ctx.setTick(500);

        assertFalse(JobBehaviors.escalateSkyrunner(skyrunner, ctx),
                "1st offense: maimed only, still alive");
        assertFalse(skyrunner.isDead());

        assertTrue(JobBehaviors.escalateSkyrunner(skyrunner, ctx), "2nd offense: hanged");
        assertTrue(skyrunner.isDead(), "the noose is real since S6");
        assertTrue(skyrunner.hasStatus(StatusBit.EXECUTED));
        assertEquals(1, ctx.deaths.size(), "the hanging was announced");
        assertEquals(ReasonCode.EXECUTED_SECOND_OFFENSE, ctx.deaths.causeAt(0));
    }

    @Test
    void theDeadAreInertForeverButStayResident() {
        ActorRegistry registry = new ActorRegistry();
        Actor dead = serfAt(registry, 5, 5);
        Actor alive = serfAt(registry, 9, 9);
        DeathContext ctx = new DeathContext(registry);
        dead.setHomeId(ctx.homes().addHome(dead.cell()));
        alive.setHomeId(ctx.homes().addHome(alive.cell()));
        dead.setStatus(StatusBit.DEAD, true);
        short[] needsBefore = dead.needsSnapshot();
        int cellBefore = dead.cell();

        ctx.setTick(2_000);
        registry.tickAll(ctx);

        assertArrayEquals(needsBefore, dead.needsSnapshot(),
                "no needs decay for the dead — no ticking policies, ever");
        assertEquals(cellBefore, dead.cell(), "the corpse lies where it fell");
        assertEquals(2, registry.size(), "the roster keeps its dead — never removed");
        assertTrue(alive.needsSnapshot()[Need.HUNGER.ordinal()]
                        <= 9_000, "the living ticked normally");
    }

    private static void assertArrayEquals(short[] expected, short[] actual, String message) {
        org.junit.jupiter.api.Assertions.assertArrayEquals(expected, actual, message);
    }
}
