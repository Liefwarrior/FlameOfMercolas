package com.trojia.sim.actor;

import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.type.MouseActor;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Caught-prey inertness + revive-at-den (living-docks beast pass): a {@code DOWNED} mouse
 * holds perfectly still for the whole {@code downedTimer} countdown (DOWNED_INERT wins the
 * stack, act no-op), the existing {@code auditStatus} machinery clears DOWNED at exactly zero
 * (no new scalar, no removal), the job resumes next tick, and a mouse revived/stranded outside
 * its leash walks itself back in via the relative-improvement leash rule — an ordinary walk,
 * never a teleport.
 */
final class DownedInertPolicyTest {

    private static final int Z = 11;

    private static int cell(int x, int y) {
        return PackedPos.pack(x, y, Z);
    }

    private static ActorTypeStats mouseStats() {
        NeedConfig[] needs = new NeedConfig[Need.COUNT];
        needs[Need.HUNGER.ordinal()] = new NeedConfig(9000, 300, 0, 350, 700);
        needs[Need.REST.ordinal()] = new NeedConfig(9000, 800, 0, 150, 300);
        needs[Need.COIN.ordinal()] = new NeedConfig(10000, 0, 0, 0, 0);
        needs[Need.SAFETY.ordinal()] = new NeedConfig(10000, 0, 25, 500, 900);
        needs[Need.DUTY.ordinal()] = new NeedConfig(10000, 0, 0, 0, 0);
        return new ActorTypeStats(MouseActor.TYPE, "Test mouse", 'r', 0x9A8468, "feral",
                (short) 2, 1, 8, 0, needs, false, 0, 0, 950, 305, 305, 0, 0, 0, 20);
    }

    private static Actor spawnMouse(ActorRegistry registry, int x, int y) {
        return registry.spawn(MouseActor.TYPE, mouseStats(), cell(x, y));
    }

    @Test
    void downedMouseHoldsStillForTheWholeCountdownThenTheJobResumes() {
        ActorRegistry registry = new ActorRegistry();
        Actor mouse = spawnMouse(registry, 50, 50);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        mouse.setHomeId(ctx.homes().addHome(mouse.cell()));
        mouse.setJobOrdinal((short) ctx.jobs().ordinalOf(Job.Beast.Prey.ID));
        int den = mouse.cell();

        mouse.setStatus(StatusBit.DOWNED, true);
        mouse.setDownedTimer(BeastHuntPolicy.PREY_REVIVE_TICKS);

        for (int t = 1; t <= BeastHuntPolicy.PREY_REVIVE_TICKS; t++) {
            ctx.setTick(t);
            mouse.tick(ctx);
            assertEquals(den, mouse.cell(), "a downed mouse never moves (tick " + t + ")");
        }
        assertFalse(mouse.hasStatus(StatusBit.DOWNED),
                "DOWNED clears after exactly PREY_REVIVE_TICKS auditStatus ticks");
        assertEquals(0, mouse.downedTimer());

        ctx.setTick(BeastHuntPolicy.PREY_REVIVE_TICKS + 1);
        mouse.tick(ctx);
        assertEquals(ReasonCode.JOB_GOAL, mouse.lastReasonCode(),
                "the scurry job wins the very next selection after revival");
    }

    @Test
    void downedScoreSitsInTheCustodyLadderAboveHeldBelowExecuted() {
        ActorRegistry registry = new ActorRegistry();
        Actor mouse = spawnMouse(registry, 50, 50);
        NoOpActorContext ctx = new NoOpActorContext(registry);

        assertEquals(0, Policies.DOWNED_INERT.score(mouse, ctx));
        mouse.setStatus(StatusBit.DOWNED, true);
        int score = Policies.DOWNED_INERT.score(mouse, ctx);
        assertTrue(score > 5000 && score < 6000, "above HELD (5000), below EXECUTED (6000)");
        mouse.setStatus(StatusBit.EXECUTED, true);
        assertEquals(0, Policies.DOWNED_INERT.score(mouse, ctx),
                "EXECUTED stays the authoritative permanent override");
    }

    @Test
    void aMouseStrandedOutsideItsLeashWalksBackTowardItsDen() {
        ActorRegistry registry = new ActorRegistry();
        Actor mouse = spawnMouse(registry, 50, 50); // anchor + den at (50,50), leash 8
        NoOpActorContext ctx = new NoOpActorContext(registry);
        mouse.setHomeId(ctx.homes().addHome(mouse.cell()));
        mouse.setJobOrdinal((short) ctx.jobs().ordinalOf(Job.Beast.Prey.ID));
        int den = mouse.cell();
        mouse.setCell(cell(70, 50)); // 12 beyond the leash — a long flee's worst case

        for (int t = 1; t <= 200; t++) {
            ctx.setTick(t);
            mouse.tick(ctx);
        }
        assertTrue(ActorGeometry.chebyshev(mouse.cell(), den) <= mouse.stats().leashRadius(),
                "the relative-improvement leash rule walked it back in, one cell at a time");
    }
}
