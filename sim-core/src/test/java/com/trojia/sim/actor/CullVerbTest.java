package com.trojia.sim.actor;

import com.trojia.sim.actor.type.CatActor;
import com.trojia.sim.actor.type.MouseActor;
import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * THE CULL VERB (S8 scalps). Two of these tests are the ones that matter:
 *
 * <ul>
 *   <li>{@link #cullNeverTouchesTheBodysReviveTimer()} — the ecology guard. A downed mouse's
 *       revive countdown is the predators' food supply, already tuned DOWN from 6000 by a 30k
 *       soak because thin clusters starved their cats. If the cull verb ever writes to that
 *       timer, the ward quietly starves its own predators and no report notices.</li>
 *   <li>{@link #theLatchStopsOneCarcassBeingFarmed()} — a downed body is inert for thousands
 *       of ticks, so without a per-culler latch one soul beside one mouse is an infinite
 *       materials faucet.</li>
 * </ul>
 */
final class CullVerbTest {

    private static final int Z = 11;

    private static int cell(int x, int y) {
        return PackedPos.pack(x, y, Z);
    }

    private static NeedConfig[] plainNeeds() {
        NeedConfig[] needs = new NeedConfig[Need.COUNT];
        needs[Need.HUNGER.ordinal()] = new NeedConfig(9000, 300, 0, 350, 700);
        needs[Need.REST.ordinal()] = new NeedConfig(9000, 800, 0, 150, 300);
        needs[Need.COIN.ordinal()] = new NeedConfig(10000, 0, 0, 0, 0);
        needs[Need.SAFETY.ordinal()] = new NeedConfig(10000, 0, 25, 500, 900);
        needs[Need.DUTY.ordinal()] = new NeedConfig(10000, 0, 0, 0, 0);
        return needs;
    }

    /** A scalpable quarry type, exactly the shape the committed mouse raws bind. */
    private static ActorTypeStats quarryStats(ActorTypeId type, short scalpKind, int resist) {
        return new ActorTypeStats(type, "Test " + type, 'r', 0x9A8468, "feral",
                (short) 2, 1, 8, 0, plainNeeds(), false, 0, 0, 950, 305, 305, 0, 0, 0, 20,
                scalpKind, resist);
    }

    /** A working person: an inventory, no scalp of its own, an ordinary faction. */
    private static ActorTypeStats personStats() {
        return new ActorTypeStats(Serf.TYPE, "Test serf", 's', 0xC8C0B4, "commons",
                (short) 10, 1, 64, 8, plainNeeds(), false, 0, 0, 950, 305, 305, 0, 0, 0, 20);
    }

    private static Actor spawnPerson(ActorRegistry registry, int x, int y) {
        return registry.spawn(Serf.TYPE, personStats(), cell(x, y));
    }

    private static Actor spawnDownedMouse(ActorRegistry registry, int x, int y) {
        Actor mouse = registry.spawn(MouseActor.TYPE,
                quarryStats(MouseActor.TYPE, ItemKinds.RAT_SCALP, 6), cell(x, y));
        mouse.setStatus(StatusBit.DOWNED, true);
        mouse.setDownedTimer((short) 3000);
        return mouse;
    }

    @Test
    void anAdjacentWorkerTakesTheScalpTheRawsNamed() {
        ActorRegistry registry = new ActorRegistry();
        Actor worker = spawnPerson(registry, 50, 50);
        spawnDownedMouse(registry, 51, 50);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        ctx.setTick(100);

        assertTrue(CullVerb.tryCullInReach(worker, ctx), "an attempt was spent");
        // Success is a draw, so assert the SHAPE: either a scalp of exactly the raws-named
        // kind, or a ruined pelt — never some other kind, never more than one.
        int scalps = ctx.items().countCarriedOfKind(worker.id(), ItemKinds.RAT_SCALP);
        assertTrue(scalps == 0 || scalps == 1, "one attempt yields at most one scalp");
        assertEquals(scalps == 1 ? ReasonCode.TOOK_SCALP : ReasonCode.SCALP_RUINED,
                worker.lastReasonCode());
        assertEquals(0, ctx.items().countCarriedOfKind(worker.id(), ItemKinds.GULL_SCALP),
                "the kind comes from the BODY's raws, not from a switch");
    }

    @Test
    void cullNeverTouchesTheBodysReviveTimer() {
        ActorRegistry registry = new ActorRegistry();
        Actor worker = spawnPerson(registry, 50, 50);
        Actor mouse = spawnDownedMouse(registry, 51, 50);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        ctx.setTick(100);
        short timerBefore = mouse.downedTimer();
        short statusBefore = mouse.statusBits();
        int hungerBefore = mouse.need(Need.HUNGER);
        int cellBefore = mouse.cell();

        CullVerb.tryCullInReach(worker, ctx);

        assertEquals(timerBefore, mouse.downedTimer(),
                "the revive countdown is the PREDATORS' food supply — the cull verb does not "
                        + "get to spend it (PREY_REVIVE_TICKS was already tuned down once "
                        + "because thin clusters starved their cats)");
        assertEquals(statusBefore, mouse.statusBits(), "the body is not re-downed or killed");
        assertEquals(hungerBefore, mouse.need(Need.HUNGER), "no need delta lands on the body");
        assertEquals(cellBefore, mouse.cell(), "the body is not moved");
    }

    @Test
    void theLatchStopsOneCarcassBeingFarmed() {
        ActorRegistry registry = new ActorRegistry();
        Actor worker = spawnPerson(registry, 50, 50);
        spawnDownedMouse(registry, 51, 50);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        ctx.setTick(100);

        assertTrue(CullVerb.tryCullInReach(worker, ctx), "the first attempt lands");
        assertEquals(100 + CullVerb.CULL_COOLDOWN_TICKS, worker.culledUntilTick(),
                "the latch is stamped on the ATTEMPT, win or lose — the time is what it costs");

        for (long t = 101; t < 100 + CullVerb.CULL_COOLDOWN_TICKS; t += 97) {
            ctx.setTick(t);
            assertFalse(CullVerb.tryCullInReach(worker, ctx),
                    "latched at tick " + t + ": one carcass is not a faucet");
        }
        ctx.setTick(100 + CullVerb.CULL_COOLDOWN_TICKS);
        assertTrue(CullVerb.tryCullInReach(worker, ctx), "the latch expires and work resumes");
    }

    @Test
    void beastsAndTheQuarryItselfNeverCull() {
        ActorRegistry registry = new ActorRegistry();
        Actor cat = registry.spawn(CatActor.TYPE,
                quarryStats(CatActor.TYPE, ItemKinds.CAT_SCALP, 18), cell(50, 50));
        spawnDownedMouse(registry, 51, 50);
        NoOpActorContext ctx = new NoOpActorContext(registry);
        ctx.setTick(100);

        assertFalse(CullVerb.canCull(cat),
                "a cat that just ate the mouse is not going to skin it — and is itself quarry");
        assertFalse(CullVerb.tryCullInReach(cat, ctx));
        assertEquals(0, ctx.items().countCarriedOfKind(cat.id(), ItemKinds.RAT_SCALP));
    }

    @Test
    void anUprightMouseIsNotQuarryAndDistanceMatters() {
        ActorRegistry registry = new ActorRegistry();
        Actor worker = spawnPerson(registry, 50, 50);
        Actor upright = registry.spawn(MouseActor.TYPE,
                quarryStats(MouseActor.TYPE, ItemKinds.RAT_SCALP, 6), cell(51, 50));
        NoOpActorContext ctx = new NoOpActorContext(registry);
        ctx.setTick(100);

        assertFalse(CullVerb.isQuarry(upright), "a living mouse is not a carcass");
        assertFalse(CullVerb.tryCullInReach(worker, ctx));

        upright.setStatus(StatusBit.DOWNED, true);
        assertNotEquals(Actor.NONE, CullVerb.quarryInReach(worker, ctx), "downed and adjacent");

        // Two cells away is out of knife reach.
        ActorRegistry far = new ActorRegistry();
        Actor worker2 = spawnPerson(far, 50, 50);
        spawnDownedMouse(far, 53, 50);
        NoOpActorContext ctx2 = new NoOpActorContext(far);
        ctx2.setTick(100);
        assertEquals(Actor.NONE, CullVerb.quarryInReach(worker2, ctx2));
        assertFalse(CullVerb.tryCullInReach(worker2, ctx2));
    }

    @Test
    void theCullCeilingStaysUnderCertainty() {
        assertTrue(SkillChecks.CULL_CEIL_PERMILLE < SkillChecks.PERMILLE,
                "mastery never buys certainty — and a ceiling at 1000 would make a body "
                        + "beside a worker a guaranteed materials faucet");
        // A master hand against the softest quarry still tops out at the ceiling.
        assertEquals(SkillChecks.CULL_CEIL_PERMILLE,
                SkillChecks.successPermille(500, 0, SkillChecks.CULL_BASE_PERMILLE,
                        SkillChecks.CULL_FLOOR_PERMILLE, SkillChecks.CULL_CEIL_PERMILLE));
        assertEquals(SkillChecks.CULL_FLOOR_PERMILLE,
                SkillChecks.successPermille(0, 500, SkillChecks.CULL_BASE_PERMILLE,
                        SkillChecks.CULL_FLOOR_PERMILLE, SkillChecks.CULL_CEIL_PERMILLE));
    }
}
