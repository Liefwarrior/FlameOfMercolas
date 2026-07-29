package com.trojia.client.scenario;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.client.input.CullInput;
import com.trojia.client.input.SellInput;
import com.trojia.client.inspect.CullAvailability;
import com.trojia.client.inspect.CullFeedbackTracker;
import com.trojia.client.inspect.PlayModeState;
import com.trojia.client.inspect.SellFeedbackTracker;
import com.trojia.client.inspect.ToastQueue;
import com.trojia.client.time.SimulationDriver;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorGeometry;
import com.trojia.sim.actor.CullVerb;
import com.trojia.sim.actor.ItemKinds;
import com.trojia.sim.actor.ReasonCode;
import com.trojia.sim.actor.SellVerb;
import com.trojia.sim.actor.StatusBit;
import com.trojia.sim.engine.SimulationSystem;
import com.trojia.sim.world.PackedPos;

import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * THE PLAYABLE LOOP, walked end to end (S8's whole point — Eli wants something to PLAY).
 *
 * <p>A NAMED soul, driven only through the seams the keyboard drives — the arrow-key move
 * intent, {@code K} ({@link CullInput}), {@code B} ({@link SellInput}) — walks the entire
 * sprint: find a downed mouse, walk to it, take the scalp, carry it across the ward, sell it
 * at a counter for Royals. No console commands, no test-only backdoor: every state change
 * below goes through the same public verb a player's finger reaches.
 *
 * <p>The one thing this test does that a player would not is CHOOSE its soul and its route by
 * scanning — a player uses their eyes. Everything after the choice is the ordinary game.
 */
class PlayableScalpLoopTest {

    /** How long to let the ward run before a cat has downed a mouse somewhere. */
    private static final int WARMUP_TICKS = 12_000;
    /** Player ticks budgeted for the walk to the carcass. */
    private static final int WALK_TO_QUARRY_TICKS = 1_500;
    /** Player ticks budgeted for the walk to a counter. */
    private static final int WALK_TO_COUNTER_TICKS = 2_500;

    @Test
    void aNamedSoulCullsAScalpCarriesItAndSellsItAtACounter() {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation pop = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        SimulationDriver driver = new SimulationDriver(loaded.world(), loaded.worldSeed(),
                List.<SimulationSystem>of(pop.system()));
        var registry = pop.registry();
        var identity = pop.identity();
        var cursor = loaded.world().cursor();
        Actor.WalkabilityQuery walk =
                c -> com.trojia.sim.world.Walkability.isWalkable(cursor.moveTo(c));

        // ---- 1. Let the ward run until a cat has put a mouse on the ground. -------------
        int quarry = Actor.NONE;
        int hero = Actor.NONE;
        for (int t = 0; t < WARMUP_TICKS && hero == Actor.NONE; t++) {
            driver.requestStep();
            if (t % 50 != 0) {
                continue;
            }
            for (int b = 0; b < registry.size() && hero == Actor.NONE; b++) {
                Actor body = registry.get(b);
                if (!CullVerb.isQuarry(body)) {
                    continue;
                }
                int candidate = nearestNamedCuller(pop, body.cell());
                if (candidate != Actor.NONE) {
                    quarry = b;
                    hero = candidate;
                }
            }
        }
        assertNotEquals(Actor.NONE, hero,
                "no downed mouse with a named soul near it in " + WARMUP_TICKS + " ticks — "
                        + "the loop needs the S6 beast hunt to be putting bodies on the ground");
        String heroName = identity.get(hero).fullName();

        // ---- 2. Take the wheel (the Play-mode seam the P key drives). -------------------
        PlayModeState playMode = new PlayModeState();
        playMode.enable(hero);
        registry.get(hero).setStatus(StatusBit.PLAYER_CONTROLLED, true);
        ToastQueue toasts = new ToastQueue();
        CullFeedbackTracker cullFeedback = new CullFeedbackTracker(registry, toasts,
                playMode::playedActorId, pop.system().skillTracks());
        SellFeedbackTracker sellFeedback = new SellFeedbackTracker(registry, toasts,
                playMode::playedActorId);

        // ---- 3. Walk to the carcass on the ordinary move intent (the arrow keys). -------
        int walked = 0;
        while (walked++ < WALK_TO_QUARRY_TICKS
                && !quarryAdjacent(pop, hero)) {
            Actor body = registry.get(quarry);
            if (!CullVerb.isQuarry(body)) {
                break; // it got up; the next scan below re-targets whatever else is down
            }
            int before = registry.get(hero).cell();
            stepPlayerToward(walk, registry.get(hero), body.cell(), walked);
            driver.requestStep();
            if (registry.get(hero).cell() == before) {
                walked++; // blocked: next frame tries a different key, like a player would
            }
        }

        // ---- 4. Press K until the pelt comes away clean (the latch paces the retries). --
        int scalps = 0;
        for (int attempt = 0; attempt < 6 && scalps == 0; attempt++) {
            if (!quarryAdjacent(pop, hero)) {
                break;
            }
            CullInput.applyCull(playMode, registry, toasts, cullFeedback,
                    driver.currentTick());
            driver.requestStep();
            cullFeedback.afterTick(driver.currentTick());
            ReasonCode stamp = registry.get(hero).lastReasonCode();
            assertTrue(stamp == ReasonCode.TOOK_SCALP || stamp == ReasonCode.SCALP_RUINED,
                    heroName + " pressed K beside a carcass and the verb did not resolve: "
                            + stamp);
            scalps = pop.items().countCarriedOfKind(hero, ItemKinds.RAT_SCALP);
            if (scalps == 0) {
                // The latch: the knife hand waits its turn, and the player waits with it —
                // ticking until the same read the refusal toast quotes says the hand is free.
                while (CullAvailability.cooldownTicksLeft(registry.get(hero),
                        driver.currentTick()) > 0) {
                    driver.requestStep();
                }
            }
        }
        assertTrue(scalps > 0, heroName + " never landed a scalp in 6 attempts at the check");
        assertTrue(toasts.toString() != null); // the toasts queue took the narration

        // ---- 5. CARRY it: the scalp is an ordinary item and it travels with the body. ---
        assertEquals(scalps, pop.items().countCarriedOfKind(hero, ItemKinds.RAT_SCALP));

        // ---- 6. Walk to a counter and press B. -----------------------------------------
        int buyer = nearestCounter(pop, hero);
        assertNotEquals(Actor.NONE, buyer, "the ward baked no counter to sell to");
        long purseBefore = pop.bankAccounts().balanceOf(accountOf(pop, hero));
        int toCounter = 0;
        while (toCounter++ < WALK_TO_COUNTER_TICKS
                && !counterInReach(pop, hero, buyer)) {
            int before = registry.get(hero).cell();
            stepPlayerToward(walk, registry.get(hero), registry.get(buyer).cell(), toCounter);
            driver.requestStep();
            if (registry.get(hero).cell() == before) {
                toCounter++;
            }
        }
        assertTrue(counterInReach(pop, hero, buyer),
                heroName + " could not reach a counter in " + WALK_TO_COUNTER_TICKS + " ticks");

        SellInput.applySell(playMode, registry, toasts, sellFeedback);
        driver.requestStep();
        sellFeedback.afterTick(driver.currentTick());

        assertEquals(ReasonCode.SOLD_GOODS, registry.get(hero).lastReasonCode(),
                heroName + " pressed B at a counter and nothing was bought");
        assertEquals(0, pop.items().countCarriedOfKind(hero, ItemKinds.RAT_SCALP),
                "the scalp changed hands");
        assertEquals(scalps, pop.items().countCarriedOfKind(buyer, ItemKinds.RAT_SCALP),
                "and it is now the counter's stock — a sale MOVES, it never sinks");
        assertTrue(pop.bankAccounts().balanceOf(accountOf(pop, hero)) > purseBefore,
                heroName + " sold a scalp and got no Royals for it");
    }

    /** Whether a downed scalpable body sits within knife reach — the test's own eyes. */
    private static boolean quarryAdjacent(DocksPopulation pop, int heroId) {
        Actor hero = pop.registry().get(heroId);
        for (int i = 0; i < pop.registry().size(); i++) {
            Actor other = pop.registry().get(i);
            if (i != heroId && CullVerb.isQuarry(other)
                    && PackedPos.z(other.cell()) == PackedPos.z(hero.cell())
                    && ActorGeometry.chebyshev(hero.cell(), other.cell())
                            <= CullVerb.CULL_REACH) {
                return true;
            }
        }
        return false;
    }

    /** Whether {@code buyer}'s counter is within the sale's reach of the played soul. */
    private static boolean counterInReach(DocksPopulation pop, int heroId, int buyer) {
        Actor hero = pop.registry().get(heroId);
        Actor shop = pop.registry().get(buyer);
        return PackedPos.z(hero.cell()) == PackedPos.z(shop.cell())
                && ActorGeometry.chebyshev(hero.cell(), shop.cell()) <= SellVerb.SELL_REACH;
    }

    /** The nearest same-z NAMED soul that can cull and carries an ID card, within a short walk. */
    private static int nearestNamedCuller(DocksPopulation pop, int bodyCell) {
        var registry = pop.registry();
        int best = Actor.NONE;
        int bestDist = 121;
        for (int i = 0; i < registry.size(); i++) {
            Actor a = registry.get(i);
            if (!CullVerb.canCull(a) || a.isDead()
                    || a.hasStatus(StatusBit.HELD) || a.hasStatus(StatusBit.HOUSE_ARREST)
                    || i >= pop.identity().size() || !pop.identity().get(i).named()
                    || pop.items().firstCarriedOfKind(i, ItemKinds.ID_CARD) == Actor.NONE) {
                continue;
            }
            if (PackedPos.z(a.cell()) != PackedPos.z(bodyCell)) {
                continue;
            }
            int d = ActorGeometry.chebyshev(a.cell(), bodyCell);
            if (d < bestDist) {
                bestDist = d;
                best = i;
            }
        }
        return best;
    }

    /**
     * The nearest same-z shopkeeper with coin — the sale's destination. Scanned off the
     * registry by type (the baked vendor roster is sim-internal); the SIM's own sell verb
     * validates the counter itself when B is pressed.
     */
    private static int nearestCounter(DocksPopulation pop, int heroId) {
        var registry = pop.registry();
        int heroCell = registry.get(heroId).cell();
        int best = Actor.NONE;
        int bestDist = Integer.MAX_VALUE;
        for (int i = 0; i < registry.size(); i++) {
            Actor shop = registry.get(i);
            if (shop.isDead()
                    || !shop.typeId().equals(com.trojia.sim.actor.type.Shopkeeper.TYPE)
                    || PackedPos.z(shop.cell()) != PackedPos.z(heroCell)
                    || pop.bankAccounts().balanceOf(i) <= 0) {
                continue;
            }
            int d = ActorGeometry.chebyshev(heroCell, shop.cell());
            if (d < bestDist) {
                bestDist = d;
                best = i;
            }
        }
        return best;
    }

    private static int accountOf(DocksPopulation pop, int actorId) {
        int card = pop.items().firstCarriedOfKind(actorId, ItemKinds.ID_CARD);
        return card == Actor.NONE ? Actor.NONE : pop.items().get(card).accountId();
    }

    /**
     * One frame of held movement input: arm the move intent at the adjacent cell in the
     * target's direction, exactly as {@code PlayModeInput} does for a held arrow key. The SIM
     * commits (or refuses) the step — walls, occupancy and the shove escape all apply.
     */
    private static void stepPlayerToward(Actor.WalkabilityQuery walk, Actor hero,
            int targetCell, int frame) {
        if (hero.cell() == targetCell) {
            return;
        }
        // The test plays the part of the player's EYES: look at the map, work out which
        // adjacent cell is on the way, and press that key. The route read is exactly what a
        // human does looking at the screen; the MOVE itself still commits through
        // PlayerControlPolicy's ordinary move intent — walls, occupancy cap, shove escape and
        // the speed gate all apply, nothing is teleported.
        int[] route = com.trojia.sim.actor.PathFinder.findRoute(hero.cell(), targetCell,
                walk, 20_000);
        int next;
        if (route != null && route.length > 1) {
            next = route[1];
        } else {
            int hx = PackedPos.x(hero.cell());
            int hy = PackedPos.y(hero.cell());
            int dx = Integer.signum(PackedPos.x(targetCell) - hx);
            int dy = Integer.signum(PackedPos.y(targetCell) - hy);
            if (frame % 3 == 1) {
                dy = 0;
            } else if (frame % 3 == 2) {
                dx = 0;
            }
            if (dx == 0 && dy == 0) {
                return;
            }
            next = PackedPos.pack(hx + dx, hy + dy, PackedPos.z(hero.cell()));
        }
        hero.setPlayerMoveTarget(next);
    }
}
