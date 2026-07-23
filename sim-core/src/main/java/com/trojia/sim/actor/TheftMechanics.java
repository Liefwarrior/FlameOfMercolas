package com.trojia.sim.actor;

import com.trojia.sim.actor.job.Job;
import com.trojia.sim.world.PackedPos;

/**
 * The pickpocket verb (Sprint 2 rank 1, "reactive streets"): an ADJACENCY lift of a mark's
 * pocket COIN, resolved as a {@link SkillChecks#pickpocketContestPermille skyrunning-vs-
 * streetwise} check on the {@code check.pickpocket} named stream. Stateless static verbs
 * (the {@link PushMechanics} precedent), shared by BOTH callers:
 * <ul>
 *   <li><b>the played actor</b> — Play mode's pickpocket intent
 *       ({@link Actor#setPlayerPickpocketTarget}), consumed by
 *       {@code PlayerControlPolicy.act} exactly like the move intent;</li>
 *   <li><b>ambient AI theft</b> — {@link #ambientTheft}: every {@link Job.Villain} leaf at
 *       its wander-dwell boundary (working the crowd is their job), and a
 *       {@code wastrel.streetlife} ONLY when it can no longer afford a meal
 *       ({@link #isDesperate} — a fed beggar begs; a starving one steals).</li>
 * </ul>
 *
 * <p><b>Outcomes.</b> SUCCESS moves the mark's whole loose-COIN pocket thief-ward via
 * {@link ItemsLiteRegistry#moveCarried} — an item MOVE, never a mint, so the closed COIN
 * supply ({@code minted == vault + loose + sunk}) is untouched by construction — and awards
 * the thief skyrunning use-XP (satiation context = the mark, so farming one mark decays to
 * the &sect;3.3 floor). FAILURE means the mark caught the hand: a WITNESSED row lands in the
 * {@link CrimeLog}, which is the stim the EXISTING justice pipeline consumes —
 * {@link ApprehendPolicy}'s theft branch senses the row, chases the body, and corrects it
 * (fine + custody; a Skyrunner caught in the act rides the maim/hang escalation).
 *
 * <p><b>Determinism.</b> Two named draws only (impulse, check), both through the shared
 * per-actor per-tick counter with the thief as spatialKey; the mark scan is an ascending-id
 * registry walk; no float, no map state.
 */
public final class TheftMechanics {

    /** Thief's skyrunning base award for a clean lift (§3.1's "strike landed" scale). */
    public static final int SKYRUNNING_PICKPOCKET_CP = 150;

    /**
     * The ambient-theft sense cadence: a working thief checks the adjacent crowd every
     * {@value} ticks (offset by its own id so the district's thieves don't all scan the
     * same tick — the prey-vigilance throttle shape). Dwell boundaries proved the WRONG
     * trigger in the 15k soak (a wanderer dwells at random, mostly EMPTY cells — zero
     * attempts fired): opportunity is being brushed in a crowd, and that happens while
     * MOVING, so the hook rides every pursue tick behind this throttle instead.
     */
    public static final int AMBIENT_SENSE_PERIOD_TICKS = 10;

    /**
     * Ambient impulse permille for a {@link Job.Villain} per sense check — the primary
     * theft-volume tuning knob (the lead's bars ruling): at 250 permille every 10 ticks, a
     * villain brushing through a crowd works it roughly every 40 ticks.
     */
    public static final int VILLAIN_THEFT_IMPULSE_PERMILLE = 250;
    /** Ambient impulse permille for a DESPERATE wastrel — rarer: theft is their last resort. */
    public static final int WASTREL_THEFT_IMPULSE_PERMILLE = 100;

    /** Chebyshev reach of a lift: adjacency only (same z). */
    public static final int PICKPOCKET_REACH = 1;

    /**
     * Chebyshev radius a working thief SENSES marks in (same z): the 15k soak proved
     * chance adjacency alone yields ~1 lift a day district-wide — a thief does not wait
     * for the crowd to brush him, he DRIFTS AT it. An impulse that passes with a mark in
     * this radius but out of reach retargets the thief's own wander leg onto the mark's
     * cell (the ordinary wander walker does the closing; the lift lands at a later
     * cadence once adjacent).
     */
    public static final int MARK_SENSE_RADIUS = 6;

    private TheftMechanics() {
    }

    /**
     * One pickpocket attempt by {@code thief} against {@code victim}. Validates reach
     * (same z, chebyshev &le; {@value #PICKPOCKET_REACH}) and victim state defensively so
     * the play-mode intent path can hand it any picked id. Returns whether the lift
     * succeeded.
     */
    public static boolean pickpocket(Actor thief, Actor victim, ActorContext ctx) {
        if (victim.id() == thief.id()
                || victim.hasStatus(StatusBit.EXECUTED)
                || PackedPos.z(victim.cell()) != PackedPos.z(thief.cell())
                || ActorGeometry.chebyshev(thief.cell(), victim.cell()) > PICKPOCKET_REACH) {
            return false;
        }
        int permille = SkillChecks.pickpocketContestPermille(ctx.skillTracks(), thief.id(),
                victim.id());
        long draw = ctx.draw(ActorRngStream.CHECK_PICKPOCKET, thief.id(),
                ctx.nextDrawIndex(thief.id()));
        int cell = victim.cell();
        int presentedId = thief.identity().presentedId();
        if (SkillChecks.passes(draw, permille)) {
            // The lift: the mark's whole loose pocket moves (an item MOVE — conservation
            // holds by construction). Use-XP lands on the TRUE doer (the body that lifted).
            int pocket = ctx.items().countCarriedOfKind(victim.id(), ItemKinds.COIN);
            int moved = pocket > 0
                    ? ctx.items().moveCarried(victim.id(), thief.id(), ItemKinds.COIN, pocket)
                    : 0;
            ctx.skillTracks().award(thief.id(), ctx.skillTracks().skyrunningRaw(),
                    SKYRUNNING_PICKPOCKET_CP, victim.id(), ctx.tick());
            ctx.crimeLog().record(ctx.tick(), cell, thief.id(), presentedId, victim.id(), false);
            thief.setLastReasonCode(ReasonCode.PICKPOCKETED);
            ctx.recordTheft(true, moved);
            return true;
        }
        // Caught in the act: the WITNESSED crime row is the stim the justice pipeline reads.
        ctx.crimeLog().record(ctx.tick(), cell, thief.id(), presentedId, victim.id(), true);
        thief.setLastReasonCode(ReasonCode.CAUGHT_STEALING);
        ctx.recordTheft(false, 0);
        return false;
    }

    /**
     * The ambient AI theft hook (called by every {@link Job.Villain} leaf's pursue, every
     * tick): behind the id-offset sense cadence, draw the {@code theft.impulse} gate
     * (cheap; THE volume knob), then work the adjacent crowd for the lowest-id markable
     * neighbor carrying COIN.
     */
    public static void ambientTheft(Actor self, ActorContext ctx, int impulsePermille) {
        if ((ctx.tick() + self.id()) % AMBIENT_SENSE_PERIOD_TICKS != 0) {
            return; // between sense boundaries (the throttle; id-offset spreads the load)
        }
        ambientTheftAtBoundary(self, ctx, impulsePermille);
    }

    /**
     * The DESPERATION theft hook (called by {@code wastrel.streetlife}'s pursue, every
     * tick): the same cadence throttle first (so the {@link #isDesperate} ledger read runs
     * once per period, not per tick), then the desperation gate, then the rarer wastrel
     * impulse — a fed beggar begs; a starving one steals.
     */
    public static void desperateAmbientTheft(Actor self, ActorContext ctx) {
        if ((ctx.tick() + self.id()) % AMBIENT_SENSE_PERIOD_TICKS != 0) {
            return;
        }
        if (!isDesperate(self, ctx)) {
            return;
        }
        ambientTheftAtBoundary(self, ctx, WASTREL_THEFT_IMPULSE_PERMILLE);
    }

    /** The post-throttle core shared by both hooks (the cadence check already passed). */
    private static void ambientTheftAtBoundary(Actor self, ActorContext ctx,
            int impulsePermille) {
        if (self.hasStatus(StatusBit.HELD) || self.hasStatus(StatusBit.EXECUTED)
                || self.hasStatus(StatusBit.HOUSE_ARREST)) {
            return;
        }
        long draw = ctx.draw(ActorRngStream.THEFT_IMPULSE, self.id(),
                ctx.nextDrawIndex(self.id()));
        if (!SkillChecks.passes(draw, impulsePermille)) {
            return;
        }
        Actor mark = nearestMark(self, ctx);
        if (mark == null) {
            return;
        }
        if (ActorGeometry.chebyshev(self.cell(), mark.cell()) <= PICKPOCKET_REACH) {
            pickpocket(self, mark, ctx);
            return;
        }
        // In sense range but out of reach: drift AT the mark — retarget the thief's own
        // wander leg onto the mark's cell (exactly retargetWander's shape, so the ordinary
        // wander walker, with all its stall tooling, does the closing). Deterministic; the
        // lift itself lands at a later cadence once adjacent.
        self.setGoalTarget(TargetKind.CELL, mark.cell());
        self.setGoalWorkTicks(0);
    }

    /**
     * Whether {@code self} can no longer afford a meal anywhere (no ID, refused at every
     * counter, or a balance under its own {@link Barter#floorPriceFor cheapest possible
     * price}) — the wastrel desperation gate: exactly the moment the money gate shuts is
     * the moment street theft starts.
     */
    public static boolean isDesperate(Actor self, ActorContext ctx) {
        int itemId = ctx.items().firstCarriedOfKind(self.id(), ItemKinds.ID_CARD);
        ItemsLiteEntry card = itemId == Actor.NONE ? null : ctx.items().get(itemId);
        int account = BankLedger.purchaseAuth(card);
        if (account == Actor.NONE) {
            return true;
        }
        long floor = Barter.floorPriceFor(Barter.quoteFor(self, ctx));
        return floor == Long.MAX_VALUE || ctx.bankAccounts().balanceOf(account) < floor;
    }

    /**
     * The NEAREST eligible mark within {@link #MARK_SENSE_RADIUS} (ascending id breaks
     * distance ties): same z, carrying loose COIN, not in custody/downed, and never the
     * law, the Wielder or a beast (thieves do not dip the Watch — all read PRESENTED, the
     * Persona seam). {@code null} if none.
     */
    private static Actor nearestMark(Actor self, ActorContext ctx) {
        ActorRegistry registry = ctx.registry();
        int selfCell = self.cell();
        int selfZ = PackedPos.z(selfCell);
        Actor best = null;
        int bestDist = MARK_SENSE_RADIUS + 1;
        for (int i = 0; i < registry.size(); i++) {
            Actor other = registry.get(i);
            if (other.id() == self.id() || PackedPos.z(other.cell()) != selfZ) {
                continue;
            }
            int d = ActorGeometry.chebyshev(selfCell, other.cell());
            if (d >= bestDist) {
                continue; // strict-<: the lowest id wins a distance tie
            }
            if (other.hasStatus(StatusBit.HELD) || other.hasStatus(StatusBit.EXECUTED)
                    || other.hasStatus(StatusBit.DOWNED)) {
                continue; // robbing the caged/hanged/downed is a different crime, not modeled
            }
            Job presented = ctx.presentedJob(other);
            if (presented instanceof Job.Watch || presented instanceof Job.FlameOfMerc
                    || presented instanceof Job.Beast) {
                continue;
            }
            if (ctx.items().countCarriedOfKind(other.id(), ItemKinds.COIN) > 0) {
                best = other;
                bestDist = d;
            }
        }
        return best;
    }
}
