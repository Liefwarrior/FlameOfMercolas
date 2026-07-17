package com.trojia.sim.actor;

import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.type.MilitiaWatch;
import com.trojia.sim.actor.type.Wastrel;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The Watch-side {@code APPREHEND} enforcement loop (law &amp; order pass, Pass 11): sense the
 * lowest-id eligible offender (throttled, same-z, chebyshev), intercept, warn (move-along +
 * grace), then fine (Royal debit into the civic pool; seize-what-exists when broke) + arrest
 * with the FIXED one-day sentence via the shared custody machinery. The score is a HIGH FIXED
 * CONSTANT while a case is live (landmine D: the notional 500-899 RESPONSE band loses to
 * NEED scores that reach ~1305), so a guard never abandons an apprehension to eat or go home.
 */
final class ApprehendPolicyTest {

    private static final int Z = 11;
    private static final long SENSE_TICK = 20; // % SENSE_PERIOD_TICKS(10) == 0

    /** Ctx double: synthetic zones + prison cells + queue + a wired civic pool account. */
    private static class EnforcementContext extends NoOpActorContext {
        private PrisonCellRegistry prisonCells = PrisonCellRegistry.EMPTY;
        private BankQueue bankQueue = BankQueue.EMPTY;
        private int poolAccount = Actor.NONE;

        EnforcementContext(ActorRegistry registry) {
            super(registry);
        }

        @Override
        public PrisonCellRegistry prisonCells() {
            return prisonCells;
        }

        @Override
        public BankQueue bankQueue() {
            return bankQueue;
        }

        @Override
        public int civicPoolAccount() {
            return poolAccount;
        }
    }

    private static int cell(int x, int y) {
        return PackedPos.pack(x, y, Z);
    }

    private static Actor spawnGuard(ActorRegistry registry, ActorContext ctx, int x, int y) {
        Actor guard = registry.spawn(MilitiaWatch.TYPE,
                ActorTestFixtures.stats(MilitiaWatch.TYPE), cell(x, y));
        guard.setJobOrdinal((short) ctx.jobs().ordinalOf(Job.Watch.Patrol.ID));
        return guard;
    }

    private static Actor spawnWastrel(ActorRegistry registry, ActorContext ctx, int x, int y) {
        Actor wastrel = registry.spawn(Wastrel.TYPE, ActorTestFixtures.stats(Wastrel.TYPE),
                cell(x, y));
        wastrel.setJobOrdinal((short) ctx.jobs().ordinalOf(Job.Wastrel.Streetlife.ID));
        return wastrel;
    }


    /**
     * An offender that WANDERED into {@code (x, y)}: spawned (and thus anchored/homed) on
     * neutral ground, then stepped onto the target cell — so the staff exemption (work anchor
     * inside the zone) genuinely does not apply, exactly like a drifting district wastrel.
     */
    private static Actor wanderedInWastrel(ActorRegistry registry, ActorContext ctx, int x, int y) {
        Actor wastrel = spawnWastrel(registry, ctx, 30, 30);
        wastrel.setCell(cell(x, y));
        return wastrel;
    }

    /** One Trader-gated zone over the given cells (the policed-shop shape). */
    private static RestrictedZoneTable traderZone(int... cells) {
        return new RestrictedZoneTable(List.of(
                new RestrictedZone(Job.Trade.Trader.ID, Actor.NONE, cells)));
    }

    // ======================================================================
    // Score pricing (landmine D)
    // ======================================================================

    @Test
    void scoreIsTheHighFixedConstantWhileACaseIsLiveAndZeroOtherwise() {
        ActorRegistry registry = new ActorRegistry();
        EnforcementContext ctx = new EnforcementContext(registry);
        Actor guard = spawnGuard(registry, ctx, 50, 50);
        ctx.setTick(SENSE_TICK + 1); // not a sense boundary, no case

        assertEquals(0, Policies.APPREHEND.score(guard, ctx), "no case, no boundary -> 0");

        guard.setApprehendTargetId(7);
        assertEquals(1500, Policies.APPREHEND.score(guard, ctx),
                "live case -> the fixed constant, on ANY tick");
    }

    @Test
    void anApprehendingGuardNeverAbandonsTheCaseToEatOrGoHome() {
        // Landmine D: the fixed 1500 must beat every NEED score. The worst observed NEED
        // ceiling is ~1305 (SEEK_FOOD at crit with the militia raws' 1000 critBonus); with
        // this fixture's stats both NEED policies peak at 305+500=805. Assert the ladder.
        ActorRegistry registry = new ActorRegistry();
        EnforcementContext ctx = new EnforcementContext(registry);
        Actor guard = spawnGuard(registry, ctx, 50, 50);
        guard.setHomeId(ctx.homes().addHome(cell(40, 40)));
        guard.applyNeedDelta(Need.HUNGER, -8500); // below CRITICAL
        guard.applyNeedDelta(Need.REST, -8500);   // below CRITICAL
        guard.setApprehendTargetId(3);
        ctx.setTick(SENSE_TICK + 3);

        int apprehend = Policies.APPREHEND.score(guard, ctx);
        assertEquals(1500, apprehend);
        assertTrue(apprehend > Policies.SEEK_FOOD.score(guard, ctx),
                "a starving guard still finishes the apprehension");
        assertTrue(apprehend > Policies.RETURN_HOME.score(guard, ctx),
                "a tired guard still finishes the apprehension");
        assertTrue(apprehend > 1305, "above the district's observed NEED ceiling (~1305)");
        assertTrue(apprehend < 2000, "below PLAYER_CONTROL (2000) / HELD (5000) / EXECUTED (6000)");
    }

    // ======================================================================
    // Sensing: throttle + lowest-id eligibility
    // ======================================================================

    @Test
    void sensingIsThrottledToTheCadenceBoundary() {
        ActorRegistry registry = new ActorRegistry();
        EnforcementContext ctx = new EnforcementContext(registry);
        Actor guard = spawnGuard(registry, ctx, 50, 50);
        wanderedInWastrel(registry, ctx, 52, 50); // wandered into the zone, eligible
        ctx.setRestrictedZones(traderZone(cell(52, 50)));

        ctx.setTick(SENSE_TICK + 1);
        assertEquals(0, Policies.APPREHEND.score(guard, ctx), "off-boundary tick: no acquisition");

        ctx.setTick(SENSE_TICK);
        assertEquals(1500, Policies.APPREHEND.score(guard, ctx), "boundary tick: offender sensed");
    }

    @Test
    void actLocksTheLowestIdEligibleOffender() {
        ActorRegistry registry = new ActorRegistry();
        EnforcementContext ctx = new EnforcementContext(registry);
        Actor guard = spawnGuard(registry, ctx, 50, 50);
        Actor first = wanderedInWastrel(registry, ctx, 55, 50);  // id 1, further away
        Actor second = wanderedInWastrel(registry, ctx, 51, 50); // id 2, nearer
        ctx.setRestrictedZones(traderZone(first.cell(), second.cell()));
        ctx.setTick(SENSE_TICK);

        Policies.APPREHEND.act(guard, ctx);

        assertEquals(first.id(), guard.apprehendTargetId(),
                "ascending-id scan: the LOWEST id wins, not the nearest");
    }

    @Test
    void offendersBeyondTheSenseRadiusOrOffBandAreNeverSensed() {
        ActorRegistry registry = new ActorRegistry();
        EnforcementContext ctx = new EnforcementContext(registry);
        Actor guard = spawnGuard(registry, ctx, 50, 50);
        Actor far = wanderedInWastrel(registry, ctx, 59, 50); // chebyshev 9 > radius 8
        Actor offBand = registry.spawn(Wastrel.TYPE, ActorTestFixtures.stats(Wastrel.TYPE),
                PackedPos.pack(30, 31, Z + 1));
        offBand.setCell(PackedPos.pack(51, 50, Z + 1));
        ctx.setRestrictedZones(traderZone(far.cell(), offBand.cell()));
        ctx.setTick(SENSE_TICK);

        assertEquals(0, Policies.APPREHEND.score(guard, ctx));
    }

    // ======================================================================
    // The correction sequence: warn -> (leave: clear) / (stay: fine + arrest)
    // ======================================================================

    @Test
    void firstContactWarnsWithTheMoveAlongGrace() {
        ActorRegistry registry = new ActorRegistry();
        EnforcementContext ctx = new EnforcementContext(registry);
        Actor guard = spawnGuard(registry, ctx, 51, 50);
        Actor offender = wanderedInWastrel(registry, ctx, 52, 50);
        ctx.setRestrictedZones(traderZone(offender.cell()));
        ctx.setTick(SENSE_TICK);

        Policies.APPREHEND.act(guard, ctx); // adjacent: lock + warn in one contact

        assertTrue(offender.hasStatus(StatusBit.MOVE_ALONG));
        assertEquals(SENSE_TICK + 12, offender.moveAlongUntilTick(), "short grace deadline");
        assertEquals(ReasonCode.WARNED_MOVE_ALONG, offender.lastReasonCode());
        assertFalse(offender.hasStatus(StatusBit.HELD), "a warning is not an arrest");
        assertEquals(offender.id(), guard.apprehendTargetId(), "case stays open through the grace");
    }

    @Test
    void warnedOffenderWhoLeavesTheZoneIsClearedWithoutAFine() {
        ActorRegistry registry = new ActorRegistry();
        EnforcementContext ctx = new EnforcementContext(registry);
        Actor guard = spawnGuard(registry, ctx, 51, 50);
        Actor offender = wanderedInWastrel(registry, ctx, 52, 50);
        BankLedger bank = ctx.bankAccounts();
        bank.openAccount(); // 0: guard
        bank.openAccount(); // 1: offender
        ctx.poolAccount = bank.openAccount(); // 2: civic pool
        bank.credit(offender.id(), 20);
        ctx.setRestrictedZones(traderZone(cell(52, 50)));
        ctx.setTick(SENSE_TICK);
        Policies.APPREHEND.act(guard, ctx); // warn

        offender.setCell(cell(60, 60)); // complies: leaves the zone
        ctx.setTick(SENSE_TICK + 5);
        Policies.APPREHEND.act(guard, ctx);

        assertFalse(offender.hasStatus(StatusBit.MOVE_ALONG), "compliance clears the warning");
        assertFalse(offender.hasStatus(StatusBit.HELD));
        assertEquals(20, bank.balanceOf(offender.id()), "leaving in time costs nothing");
        assertEquals(Actor.NONE, guard.apprehendTargetId(), "case closed");
    }

    @Test
    void stayingPastTheGraceFinesIntoTheCivicPoolAndArrestsForExactlyOneDay() {
        ActorRegistry registry = new ActorRegistry();
        EnforcementContext ctx = new EnforcementContext(registry);
        Actor guard = spawnGuard(registry, ctx, 51, 50);
        Actor offender = wanderedInWastrel(registry, ctx, 52, 50);
        BankLedger bank = ctx.bankAccounts();
        bank.openAccount();
        bank.openAccount();
        ctx.poolAccount = bank.openAccount();
        bank.credit(offender.id(), 20);
        int prisonCell = cell(90, 90);
        ctx.prisonCells = new PrisonCellRegistry(new int[] {prisonCell});
        ctx.setRestrictedZones(traderZone(cell(52, 50)));
        long totalBefore = bank.totalRoyals();
        ctx.setTick(SENSE_TICK);
        Policies.APPREHEND.act(guard, ctx); // warn (grace ends at SENSE_TICK + 12)

        ctx.setTick(SENSE_TICK + 12); // deadline reached, still standing in the zone
        Policies.APPREHEND.act(guard, ctx);

        assertEquals(10, bank.balanceOf(offender.id()), "fined LOITER_FINE (10 Royals)");
        assertEquals(10, bank.balanceOf(ctx.poolAccount), "the fine lands in the civic pool");
        assertEquals(totalBefore, bank.totalRoyals(), "fines move Royals, never mint/burn");
        assertTrue(offender.hasStatus(StatusBit.HELD));
        assertEquals(SENSE_TICK + 12 + 24_000, offender.heldUntilTick(),
                "the FIXED one-day loiter sentence, not a drawn 1-3 day one");
        assertEquals(prisonCell, offender.assignedHoldCell(), "a real cell was assigned");
        assertEquals(1, offender.offenseCount());
        assertFalse(offender.hasStatus(StatusBit.MOVE_ALONG), "consumed by the arrest");
        assertEquals(Actor.NONE, guard.apprehendTargetId(), "case closed");

        // The existing HeldPolicy machinery serves + releases the fixed sentence exactly.
        ctx.setTick(offender.heldUntilTick() - 1);
        Policies.HELD.act(offender, ctx);
        assertTrue(offender.hasStatus(StatusBit.HELD), "one tick early: still in custody");
        ctx.setTick(offender.heldUntilTick());
        Policies.HELD.act(offender, ctx);
        assertFalse(offender.hasStatus(StatusBit.HELD), "released at exactly heldUntilTick");
        assertEquals(Actor.NONE, offender.assignedHoldCell(), "the cell slot is freed");
    }

    @Test
    void aBrokeOffenderIsSeizedForWhatExists() {
        ActorRegistry registry = new ActorRegistry();
        EnforcementContext ctx = new EnforcementContext(registry);
        Actor guard = spawnGuard(registry, ctx, 51, 50);
        Actor offender = wanderedInWastrel(registry, ctx, 52, 50);
        BankLedger bank = ctx.bankAccounts();
        bank.openAccount();
        bank.openAccount();
        ctx.poolAccount = bank.openAccount();
        bank.credit(offender.id(), 3); // cannot cover the 10-Royal fine
        ctx.setRestrictedZones(traderZone(cell(52, 50)));
        ctx.setTick(SENSE_TICK);
        Policies.APPREHEND.act(guard, ctx); // warn

        ctx.setTick(SENSE_TICK + 12);
        Policies.APPREHEND.act(guard, ctx); // fine + arrest

        assertEquals(0, bank.balanceOf(offender.id()), "everything he had was seized");
        assertEquals(3, bank.balanceOf(ctx.poolAccount), "…and only what he had");
        assertTrue(offender.hasStatus(StatusBit.HELD), "still jailed on top of the seizure");
    }

    @Test
    void guardChasesADistantOffenderAndDropsAnUnreachableOne() {
        ActorRegistry registry = new ActorRegistry();
        EnforcementContext ctx = new EnforcementContext(registry);
        Actor guard = spawnGuard(registry, ctx, 44, 50);
        Actor offender = wanderedInWastrel(registry, ctx, 50, 50); // chebyshev 6 > CONTACT_RADIUS
        ctx.setRestrictedZones(traderZone(offender.cell()));
        ctx.setTick(SENSE_TICK);

        int before = guard.cell();
        Policies.APPREHEND.act(guard, ctx);
        assertNotEquals(before, guard.cell(), "the guard steps toward the offender");
        assertEquals(ReasonCode.APPREHENDING, guard.lastReasonCode());
        assertEquals(offender.id(), guard.apprehendTargetId());

        // Now make the offender A*-unreachable: the case must close (bounded), not freeze.
        EnforcementContext walled = new EnforcementContext(registry) {
            @Override
            public boolean isWalkable(int c) {
                return false; // nothing is walkable: every route search fails
            }
        };
        walled.setRestrictedZones(traderZone(offender.cell()));
        // Two acts: the first invalidates the stale cached route (the blocked step), the
        // second's fresh A* search fails outright and the case closes — bounded, not frozen.
        walled.setTick(SENSE_TICK + 1);
        Policies.APPREHEND.act(guard, walled);
        walled.setTick(SENSE_TICK + 2);
        Policies.APPREHEND.act(guard, walled);
        assertEquals(Actor.NONE, guard.apprehendTargetId(),
                "an unreachable offender closes the case instead of pinning the guard forever");
    }

    // ======================================================================
    // Eligibility: staff, customers, presenters, disguises, the law itself
    // ======================================================================

    @Test
    void anchoredStaffInsideTheZoneIsNotEligible() {
        ActorRegistry registry = new ActorRegistry();
        EnforcementContext ctx = new EnforcementContext(registry);
        spawnGuard(registry, ctx, 51, 50);
        Actor staff = wanderedInWastrel(registry, ctx, 52, 50);
        staff.setAnchorCell(cell(53, 50)); // works another cell of the same zone
        ctx.setRestrictedZones(traderZone(cell(52, 50), cell(53, 50)));

        assertFalse(ApprehendPolicy.inViolation(staff, ctx),
                "its work anchor is in the zone: that is staff, not a loiterer");
    }

    @Test
    void aPayingCustomerMidPurchaseIsNotEligible() {
        ActorRegistry registry = new ActorRegistry();
        EnforcementContext ctx = new EnforcementContext(registry);
        spawnGuard(registry, ctx, 51, 50);
        Actor customer = wanderedInWastrel(registry, ctx, 52, 50);
        customer.setLastReasonCode(ReasonCode.BOUGHT_FOOD); // the SeekFood buy path
        ctx.setRestrictedZones(traderZone(cell(52, 50)));

        assertFalse(ApprehendPolicy.inViolation(customer, ctx));
    }

    @Test
    void aCorrectlyPresentingActorPassesTheGate() {
        ActorRegistry registry = new ActorRegistry();
        EnforcementContext ctx = new EnforcementContext(registry);
        spawnGuard(registry, ctx, 51, 50);
        Actor trader = wanderedInWastrel(registry, ctx, 52, 50);
        trader.setJobOrdinal((short) ctx.jobs().ordinalOf(Job.Trade.Trader.ID));
        ctx.setRestrictedZones(traderZone(cell(52, 50)));

        assertFalse(ApprehendPolicy.inViolation(trader, ctx));
    }

    @Test
    void aDisguisedActorPassesAZoneGateItsCoverSatisfies() {
        // The F3 disguise seam: the gate reads the PRESENTED job, never the true one — so a
        // (played) wastrel presenting as a live Trader walks the Trader zone unmolested.
        ActorRegistry registry = new ActorRegistry();
        EnforcementContext ctx = new EnforcementContext(registry);
        spawnGuard(registry, ctx, 51, 50);
        Actor liveTrader = spawnWastrel(registry, ctx, 60, 60);
        liveTrader.setJobOrdinal((short) ctx.jobs().ordinalOf(Job.Trade.Trader.ID));
        Actor disguised = wanderedInWastrel(registry, ctx, 52, 50); // true job: streetlife
        disguised.setActAs(liveTrader.id());
        ctx.setRestrictedZones(traderZone(cell(52, 50)));

        assertFalse(ApprehendPolicy.inViolation(disguised, ctx),
                "presented-job gate: the disguise passes");

        disguised.setActAs(disguised.id()); // drop the disguise
        assertTrue(ApprehendPolicy.inViolation(disguised, ctx),
                "undisguised, the same wastrel is an offender again");
    }

    @Test
    void theWatchAndBeastsAreNeverEligible() {
        ActorRegistry registry = new ActorRegistry();
        EnforcementContext ctx = new EnforcementContext(registry);
        Actor guard = spawnGuard(registry, ctx, 51, 50);
        Actor otherGuard = spawnGuard(registry, ctx, 52, 50); // in the zone, wrong job for it
        Actor beast = registry.spawn(com.trojia.sim.actor.type.AnimalActor.TYPE,
                ActorTestFixtures.stats(com.trojia.sim.actor.type.AnimalActor.TYPE), cell(53, 50));
        beast.setJobOrdinal((short) ctx.jobs().ordinalOf(Job.Beast.Chattel.ID));
        ctx.setRestrictedZones(traderZone(cell(52, 50), cell(53, 50)));

        assertFalse(ApprehendPolicy.inViolation(otherGuard, ctx), "the law does not police itself");
        assertFalse(ApprehendPolicy.inViolation(beast, ctx), "a stray goat is shooed, not booked");
        assertEquals(0, Policies.APPREHEND.score(guard, ctx));
    }

    @Test
    void heldOffendersAreNotReSensed() {
        ActorRegistry registry = new ActorRegistry();
        EnforcementContext ctx = new EnforcementContext(registry);
        Actor guard = spawnGuard(registry, ctx, 51, 50);
        Actor prisoner = wanderedInWastrel(registry, ctx, 52, 50);
        prisoner.setStatus(StatusBit.HELD, true);
        ctx.setRestrictedZones(traderZone(cell(52, 50)));
        ctx.setTick(SENSE_TICK);

        assertEquals(0, Policies.APPREHEND.score(guard, ctx),
                "someone already in custody (e.g. mid-escort through the zone) is not re-arrested");
    }

    // ======================================================================
    // Bank-queue violations
    // ======================================================================

    @Test
    void standingOnTheWrongQueueSlotIsAViolationAndTheRightOneIsNot() {
        ActorRegistry registry = new ActorRegistry();
        EnforcementContext ctx = new EnforcementContext(registry);
        spawnGuard(registry, ctx, 40, 40);
        int front = cell(70, 50);
        int back = cell(70, 51);
        ctx.bankQueue = new BankQueue(new int[] {front, back});

        Actor lawful = wanderedInWastrel(registry, ctx, 70, 50); // alone at the front: rank 0 -> front
        assertFalse(ApprehendPolicy.inViolation(lawful, ctx), "front of an empty line is correct");

        // A second, HIGHER-id actor barges to the front: the ascending-id ranking assigns
        // the lower id the front, so the queue-jumper on the wrong slot is flagged.
        Actor jumper = wanderedInWastrel(registry, ctx, 70, 51);
        lawful.setCell(back);   // id-1 (lower) pushed to the back slot
        jumper.setCell(front);  // id-2 (higher) standing at the front
        assertTrue(ApprehendPolicy.inViolation(jumper, ctx),
                "rank says the higher id belongs at the back: queue violation");
        assertTrue(ApprehendPolicy.inViolation(lawful, ctx),
                "and the displaced lower id is off ITS assigned slot too");
    }
}
