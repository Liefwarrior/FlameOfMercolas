package com.trojia.sim.actor;

import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.type.CatActor;
import com.trojia.sim.actor.type.MouseActor;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The mouse's scurry job hooks ({@code JobBehaviors.pursuePreyScurry}, living-docks beast
 * pass): a predator LINGERING inside the vigilance radius drives SAFETY under CRITICAL within
 * three sense cadences and the shared {@code FleePolicy} takes over (then recovery hands the
 * job back); and the den nibble at each wander-dwell boundary keeps a lone mouse's HUNGER
 * closed-loop healthy without touching a single FOOD item.
 */
final class PreyScurryTest {

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

    private static ActorTypeStats catStats() {
        NeedConfig[] needs = new NeedConfig[Need.COUNT];
        needs[Need.HUNGER.ordinal()] = new NeedConfig(8000, 1000, 0, 350, 700);
        needs[Need.REST.ordinal()] = new NeedConfig(9000, 800, 0, 150, 300);
        needs[Need.COIN.ordinal()] = new NeedConfig(10000, 0, 0, 0, 0);
        needs[Need.SAFETY.ordinal()] = new NeedConfig(10000, 0, 6, 500, 900);
        needs[Need.DUTY.ordinal()] = new NeedConfig(10000, 0, 0, 0, 0);
        return new ActorTypeStats(CatActor.TYPE, "Test cat", 'c', 0xC8C0B4, "feral",
                (short) 6, 1, 20, 0, needs, false, 0, 0, 950, 305, 305, 0, 0, 0, 20);
    }

    @Test
    void aLingeringPredatorPanicsTheMouseIntoFleeThenRecoveryHandsTheJobBack() {
        ActorRegistry registry = new ActorRegistry();
        Actor mouse = registry.spawn(MouseActor.TYPE, mouseStats(), cell(50, 50));
        Actor cat = registry.spawn(CatActor.TYPE, catStats(), cell(54, 50)); // inside radius 6
        NoOpActorContext ctx = new NoOpActorContext(registry);
        mouse.setHomeId(ctx.homes().addHome(mouse.cell()));
        mouse.setJobOrdinal((short) ctx.jobs().ordinalOf(Job.Beast.Prey.ID));

        long fledAt = -1;
        for (long t = 1; t <= 40 && fledAt < 0; t++) {
            ctx.setTick(t);
            mouse.tick(ctx);
            if (mouse.lastReasonCode() == ReasonCode.SAFETY_CRITICAL) {
                fledAt = t;
            }
        }
        assertTrue(fledAt > 0, "three vigilance hits from a lingering cat must trigger FLEE");
        assertTrue(mouse.need(Need.SAFETY) < NeedThresholds.CRITICAL);

        cat.setCell(cell(120, 120)); // the predator moves off — the panic must end
        boolean jobResumed = false;
        for (long t = fledAt + 1; t <= fledAt + 80 && !jobResumed; t++) {
            ctx.setTick(t);
            mouse.tick(ctx);
            jobResumed = mouse.lastReasonCode() == ReasonCode.JOB_GOAL
                    || mouse.lastReasonCode() == ReasonCode.NIBBLED_DEN;
        }
        assertTrue(jobResumed, "safety.recoverPerTick(25) ends the panic and the scurry resumes");
    }

    @Test
    void theDenNibbleKeepsALoneMouseFedWithoutAnyFoodItem() {
        ActorRegistry registry = new ActorRegistry();
        Actor mouse = registry.spawn(MouseActor.TYPE, mouseStats(), cell(50, 50));
        NoOpActorContext ctx = new NoOpActorContext(registry);
        mouse.setHomeId(ctx.homes().addHome(mouse.cell()));
        mouse.setJobOrdinal((short) ctx.jobs().ordinalOf(Job.Beast.Prey.ID));
        mouse.applyNeedDelta(Need.HUNGER, -4000); // start well down at 5000

        boolean nibbled = false;
        for (long t = 1; t <= 3000; t++) {
            ctx.setTick(t);
            mouse.tick(ctx);
            nibbled |= mouse.lastReasonCode() == ReasonCode.NIBBLED_DEN;
            assertTrue(mouse.need(Need.HUNGER) >= NeedThresholds.LOW,
                    "a scurrying mouse never goes hungry (tick " + t + ")");
        }
        assertTrue(nibbled, "the dwell-boundary nibble fired");
        assertTrue(mouse.need(Need.HUNGER) >= 9000,
                "+1500 per dwell cycle vs 0.3/tick decay pins HUNGER high, ended at "
                        + mouse.need(Need.HUNGER));
        assertTrue(ctx.items().size() == 0, "the nibble touches no item");
    }
}
