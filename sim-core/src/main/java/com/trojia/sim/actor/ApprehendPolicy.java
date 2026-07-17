package com.trojia.sim.actor;

import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.job.JobBehaviors;
import com.trojia.sim.world.PackedPos;

/**
 * {@code APPREHEND} (law &amp; order pass, Pass 11) — the Watch-side enforcement loop that
 * replaces the villain-side self-arrest hack's missing half: a guard SENSES an offender,
 * INTERCEPTS it, and CORRECTS it (warn → fine → arrest), reusing the entire existing custody
 * machinery ({@code arrestAndHold} cell assignment, {@link HeldPolicy} escort + timed release).
 *
 * <p><b>Offenses</b> (the eligibility predicate, {@link #inViolation}):
 * <ol>
 *   <li><b>Loitering / unauthorized presence in a restricted zone</b> — standing on a {@link
 *       RestrictedZone} cell without legitimate business: not staff (no work anchor in the
 *       zone), not a paying customer mid-purchase (the SeekFood buy path's reason codes), and
 *       not presenting a job the zone accepts ({@link ActorContext#canAccess} — reads the
 *       PRESENTED job, so a disguised player passes a gate its cover satisfies).</li>
 *   <li><b>Bank-queue violation</b> — standing on a {@link BankQueue} slot cell that is not
 *       the slot the deterministic ascending-id ranking assigns.</li>
 * </ol>
 * The Watch itself, the Wielder, and beasts are never eligible (all read via the PRESENTED
 * job — "act as guard" deliberately grants the Watch's zone immunity, the F3 disguise seam);
 * neither is anyone already {@code HELD}/{@code EXECUTED}.
 *
 * <p><b>Score pricing (landmine D).</b> The notional RESPONSE band (500-899) LOSES to a tired
 * guard's RETURN_HOME (~1305 observed ceiling — see {@link PlayerControlPolicy}'s ladder:
 * ordinary AI bands &lt; PLAYER_CONTROL 2000 &lt; HELD 5000 &lt; EXECUTED 6000). So this policy
 * prices as a HIGH FIXED CONSTANT ({@value #APPREHEND_SCORE}) while a live case is open —
 * above every NEED score so a guard never abandons an apprehension to eat or go home, below
 * PLAYER_CONTROL/HELD/EXECUTED so a played guard stays steerable and a jailed offender stays
 * jailed — and exactly {@code 0} with no case.
 *
 * <p><b>Sensing (landmine H).</b> Mirrors {@code watchIsNearby} inverted: an ascending-id,
 * same-z, chebyshev-{@value #SENSE_RADIUS} registry scan picking the LOWEST-id eligible
 * offender, throttled to every {@value #SENSE_PERIOD_TICKS} ticks (absolute-tick cadence) to
 * bound the O(guards x actors) cost. Acquisition runs the scan read-only in {@code score()}
 * on the cadence tick and (deterministically identically) again in {@code act()} to store the
 * lock — never a spatial hash, never map iteration. Draw-free end to end (no new RNG stream).
 *
 * <p><b>Correction sequence.</b> Chase to {@value #CONTACT_RADIUS} tiles (route-following,
 * leash-ignoring; an A*-unreachable offender closes the case — bounded, mirroring the patrol
 * route-failure skip). First contact WARNS: {@link StatusBit#MOVE_ALONG} + an absolute
 * {@code moveAlongUntilTick} grace deadline — leave the zone before it and the case closes
 * free of charge. Still in violation at the deadline: FINE {@value #LOITER_FINE_ROYALS}
 * Royals ({@code ledger.transfer} offender → civic/market pool; short balances seize what
 * exists — never minted, never burned) then ARREST with the FIXED
 * {@value #LOITER_SENTENCE_TICKS}-tick (1-day) sentence via the shared
 * {@link JobBehaviors#arrestAndHold(Actor, ActorContext, long)}.
 */
public final class ApprehendPolicy implements BehaviorPolicy {

    /** High fixed constant while a case is open: above every NEED (~1305 ceiling), below 2000. */
    static final int APPREHEND_SCORE = 1500;
    /** Same-z chebyshev sense radius — mirrors {@code JobBehaviors.ARREST_DETECT_RADIUS}. */
    static final int SENSE_RADIUS = 8;
    /** Sense-scan cadence (absolute tick % PERIOD == 0) bounding the O(N^2) cost (landmine H). */
    static final int SENSE_PERIOD_TICKS = 10;
    /** Contact ("shout") distance at which the guard warns/escalates instead of stepping closer. */
    static final int CONTACT_RADIUS = 2;
    /** Move-along grace: short by design — dawdle past it in the zone and the fine lands. */
    static final long WARN_GRACE_TICKS = 12;
    /** The loiter fine, in Royals, paid into the civic/market pool (seize-what-exists if short). */
    static final long LOITER_FINE_ROYALS = 10;
    /** The fixed loiter sentence: exactly one day (DailyRhythm.DAY = 24,000 ticks). */
    static final long LOITER_SENTENCE_TICKS = 24_000L;

    @Override
    public PolicyId id() {
        return PolicyId.APPREHEND;
    }

    @Override
    public int score(Actor self, ActorContext ctx) {
        if (self.apprehendTargetId() != Actor.NONE) {
            return APPREHEND_SCORE; // live case: never abandon an apprehension mid-chase
        }
        if (ctx.tick() % SENSE_PERIOD_TICKS != 0) {
            return 0; // between sense boundaries: no acquisition (the throttle)
        }
        // Read-only acquisition probe; act() re-runs the identical deterministic scan to lock.
        return senseLowestEligible(self, ctx) != Actor.NONE ? APPREHEND_SCORE : 0;
    }

    @Override
    public void act(Actor self, ActorContext ctx) {
        int targetId = self.apprehendTargetId();
        if (targetId == Actor.NONE) {
            targetId = senseLowestEligible(self, ctx); // byte-identical to score()'s probe
            self.setApprehendTargetId(targetId);
        }
        self.setLastReasonCode(ReasonCode.APPREHENDING);
        if (targetId == Actor.NONE) {
            return; // defensive: only reachable if score() and act() ever diverged
        }
        Actor offender = ctx.registry().get(targetId);
        if (offender.hasStatus(StatusBit.HELD) || offender.hasStatus(StatusBit.EXECUTED)) {
            closeCase(self); // another guard (or the exposure path) already took them in
            return;
        }
        if (!inViolation(offender, ctx)) {
            // Complied (left the zone / queue): the warning clears free — no fine (Pass-12 DoD).
            offender.setStatus(StatusBit.MOVE_ALONG, false);
            closeCase(self);
            return;
        }
        if (ActorGeometry.chebyshev(self.cell(), offender.cell()) > CONTACT_RADIUS) {
            self.stepAlongRoute(offender.cell(), true, ctx::isWalkable, ctx.occupancy());
            if (self.routeFailedTo(offender.cell())) {
                // Bounded abandon: the guard moves on, and the abandoned case withdraws any
                // live warning — otherwise a stale MOVE_ALONG deadline would let a later
                // guard escalate straight to fine+arrest with no fresh warn.
                offender.setStatus(StatusBit.MOVE_ALONG, false);
                closeCase(self);
            }
            return;
        }
        if (!offender.hasStatus(StatusBit.MOVE_ALONG)) {
            // First contact: WARN — set the short move-along status + absolute grace deadline.
            offender.setStatus(StatusBit.MOVE_ALONG, true);
            offender.setMoveAlongUntilTick(ctx.tick() + WARN_GRACE_TICKS);
            offender.setLastReasonCode(ReasonCode.WARNED_MOVE_ALONG);
            return;
        }
        if (ctx.tick() < offender.moveAlongUntilTick()) {
            return; // grace running: hold at contact, give them the chance to leave
        }
        // Grace expired, still in violation: FINE (seize what exists) then ARREST (fixed 1 day).
        fine(offender, ctx);
        JobBehaviors.arrestAndHold(offender, ctx, LOITER_SENTENCE_TICKS);
        offender.setStatus(StatusBit.MOVE_ALONG, false);
        closeCase(self);
    }

    private static void closeCase(Actor self) {
        self.setApprehendTargetId(Actor.NONE);
    }

    /**
     * Debits the loiter fine from the offender's account into the civic/market pool — a pure
     * {@link BankLedger#transfer}, so Royals move and are never minted/burned. A balance short
     * of the fine is seized in full (decision D-fine's "seize what exists"); no wired pool or
     * no account skips the fine entirely.
     */
    private static void fine(Actor offender, ActorContext ctx) {
        int pool = ctx.civicPoolAccount();
        BankLedger bank = ctx.bankAccounts();
        if (pool == Actor.NONE || offender.id() >= bank.accountCount()) {
            return;
        }
        long seized = Math.min(LOITER_FINE_ROYALS, bank.balanceOf(offender.id()));
        if (seized > 0) {
            bank.transfer(offender.id(), pool, seized); // accountId == actorId (bake convention)
        }
    }

    // ======================================================================
    // Sensing + the eligibility predicate
    // ======================================================================

    /**
     * The throttled sense scan: ascending-id over the registry (the {@code watchIsNearby}
     * shape inverted — landmine H), same z, chebyshev &le; {@link #SENSE_RADIUS}, first
     * (lowest-id) eligible offender wins. {@link Actor#NONE} if none.
     */
    private static int senseLowestEligible(Actor self, ActorContext ctx) {
        ActorRegistry registry = ctx.registry();
        int selfCell = self.cell();
        int selfZ = PackedPos.z(selfCell);
        for (int i = 0; i < registry.size(); i++) {
            Actor other = registry.get(i);
            if (other.id() == self.id() || PackedPos.z(other.cell()) != selfZ) {
                continue;
            }
            if (ActorGeometry.chebyshev(selfCell, other.cell()) > SENSE_RADIUS) {
                continue;
            }
            if (other.hasStatus(StatusBit.HELD) || other.hasStatus(StatusBit.EXECUTED)) {
                continue;
            }
            if (inViolation(other, ctx)) {
                return i;
            }
        }
        return Actor.NONE;
    }

    /**
     * The offense predicate — {@code true} iff {@code actor}, where it stands RIGHT NOW, is
     * (a) loitering/unauthorized inside a restricted zone with no legitimate business, or
     * (b) standing on a bank-queue slot that is not its assigned slot. Reads only presented
     * identity, live cells and reason codes — deterministic, draw-free. Package-visible for
     * the enforcement tests.
     */
    static boolean inViolation(Actor actor, ActorContext ctx) {
        Job presented = ctx.presentedJob(actor);
        if (presented instanceof Job.Watch || presented instanceof Job.FlameOfMerc
                || presented instanceof Job.Beast) {
            // The law doesn't police itself; the Wielder walks where he pleases (social power
            // maxed); a stray goat is shooed, not booked. All read PRESENTED (the F3 seam).
            return false;
        }
        RestrictedZone zone = ctx.restrictedZones().zoneAt(actor.cell());
        if (zone != null) {
            if (zone.contains(actor.anchorCell())) {
                return false; // staff: its own work anchor is in this zone (shopkeeper/trader/victualler)
            }
            if (isBuyingCustomer(actor.lastReasonCode())) {
                return false; // paying customer mid-purchase (the SeekFood buy path)
            }
            if (ctx.canAccess(actor, zone)) {
                return false; // presents a job the zone accepts (disguised player passes here)
            }
            return true;
        }
        return isQueueViolation(actor, ctx);
    }

    /** The SeekFood acquire/eat reason codes that mark someone as a customer, not a loiterer. */
    private static boolean isBuyingCustomer(ReasonCode reason) {
        return reason == ReasonCode.NEED_HUNGER_LOW || reason == ReasonCode.BOUGHT_FOOD
                || reason == ReasonCode.ATE_FOOD || reason == ReasonCode.SCAVENGED_FOOD;
    }

    /**
     * Bank-queue violation: standing on a queue slot cell that is not the one the
     * deterministic ranking (i-th lowest waiting actor id → slot i) assigns. The O(N) waiting
     * scan only runs when the candidate is actually ON a queue cell — effectively never, so
     * the sense scan stays cheap.
     */
    private static boolean isQueueViolation(Actor actor, ActorContext ctx) {
        BankQueue queue = ctx.bankQueue();
        if (queue.slotCount() == 0 || !isOnQueueSlot(queue, actor.cell())) {
            return false;
        }
        ActorRegistry registry = ctx.registry();
        int waitingCount = 0;
        for (int i = 0; i < registry.size(); i++) {
            if (isOnQueueSlot(queue, registry.get(i).cell())) {
                waitingCount++;
            }
        }
        int[] waitingAscending = new int[waitingCount];
        int w = 0;
        for (int i = 0; i < registry.size(); i++) {
            if (isOnQueueSlot(queue, registry.get(i).cell())) {
                waitingAscending[w++] = i;
            }
        }
        return queue.assignSlotCell(waitingAscending, actor.id()) != actor.cell();
    }

    private static boolean isOnQueueSlot(BankQueue queue, int cell) {
        for (int i = 0; i < queue.slotCount(); i++) {
            if (queue.slotCell(i) == cell) {
                return true;
            }
        }
        return false;
    }
}
