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

    // ---- Shove-riot detection (density revisit): "excessive shoving in an area should bring
    // guards who send everyone shoving home to house arrest". At the same sense cadence a
    // guard scans the shared ShoveLog for a BRAWL — >= RIOT_AGGRESSORS DISTINCT pushers, each
    // with >= RIOT_REPEAT_SHOVES shoves, all within RIOT_RADIUS cells of one anchor shove and
    // within RIOT_WINDOW ticks — with at least one arrestable aggressor left. Under the
    // 1-per-square cap pushing is NORMAL TRAFFIC (every doorway crossing shoves once), so a
    // raw shove count reads a busy door as a riot (measured: 274/691 actors house-arrested in
    // 0.83 days, starving the ration-less poor). The brawl signal instead requires PEOPLE, not
    // shoves — several distinct actors — and AGGRESSION, not passage: the same pusher shoving
    // repeatedly in one small patch within minutes marks a fight, where a squeeze-past is one
    // shove and gone. Detection is district-wide ("brings guards": the Watch hears of a brawl
    // anywhere in the ward), so a Band-B/C riot is corrected even though the Watch garrisons
    // Band A; the CORRECTION is issued in place (status + deadline) and the offender's own
    // HouseArrestPolicy marches it home. Draw-free, ascending scans only. ----
    /** Distinct repeat-shovers ("aggressors") within radius+window that constitute a riot. */
    static final int RIOT_AGGRESSORS = 8;
    /** In-cluster in-window shoves by ONE pusher that mark it an aggressor, not a passer-by. */
    static final int RIOT_REPEAT_SHOVES = 3;
    /** Chebyshev radius of the riot cluster around the anchor shove's cell. */
    static final int RIOT_RADIUS = 6;
    /** Only shoves this recent count toward a riot (150 ticks — a brawl is FAST). */
    static final long RIOT_WINDOW_TICKS = 150;

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
        // Read-only acquisition probes; act() re-runs the identical deterministic scans. The
        // riot check comes first in BOTH (a brawl outranks one loiterer, and score/act must
        // agree on which branch fires).
        if (senseRiotAnchor(ctx) != Actor.NONE) {
            return APPREHEND_SCORE;
        }
        return senseLowestEligible(self, ctx) != Actor.NONE ? APPREHEND_SCORE : 0;
    }

    @Override
    public void act(Actor self, ActorContext ctx) {
        int targetId = self.apprehendTargetId();
        if (targetId == Actor.NONE) {
            // Riot first (mirrors score()'s branch order): issue the house arrests and be done
            // this tick — no chase, the correction is stamped and each offender's own
            // HouseArrestPolicy marches it home.
            int riotAnchor = senseRiotAnchor(ctx);
            if (riotAnchor != Actor.NONE) {
                int issued = issueHouseArrests(ctx, riotAnchor);
                ctx.recordRiotResponse(issued);
                self.setLastReasonCode(ReasonCode.APPREHENDING);
                return;
            }
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
            // First contact (Sprint 1 — the faction ledger's ONE behavior read): whether the
            // guard extends the customary move-along warning is now a LENIENCE DRAW on the
            // watch.lenience named stream, thresholded by the offender's PRESENTED Watch
            // standing. A clean citizen (standing >= 0) is ALWAYS warned — permille 1000,
            // the pre-faction baseline byte-for-byte in outcome — while a known offender's
            // negative standing erodes the courtesy toward the floor: a staged crime spree
            // measurably hardens subsequent Watch treatment (the DoD probe). The draw is
            // attributed to the GUARD (it is the guard deciding), on the shared counter.
            int lenience = warnLeniencePermille(
                    ctx.factionStandings().watchStanding(offender.identity().presentedId()));
            long draw = ctx.draw(ActorRngStream.WATCH_LENIENCE, self.id(),
                    ctx.nextDrawIndex(self.id()));
            if (SkillChecks.passes(draw, lenience)) {
                offender.setStatus(StatusBit.MOVE_ALONG, true);
                offender.setMoveAlongUntilTick(ctx.tick() + WARN_GRACE_TICKS);
                offender.setLastReasonCode(ReasonCode.WARNED_MOVE_ALONG);
                return;
            }
            // Lenience denied: no warning for this one — straight to the correction below.
        } else if (ctx.tick() < offender.moveAlongUntilTick()) {
            return; // grace running: hold at contact, give them the chance to leave
        }
        // Grace expired (or lenience denied), still in violation: FINE (seize what exists)
        // then ARREST (fixed 1 day). The fine's own standing delta lands here; the arrest's
        // lands inside the shared arrestAndHold transition.
        fine(offender, ctx);
        ctx.factionStandings().onFine(offender.identity().presentedId());
        JobBehaviors.arrestAndHold(offender, ctx, LOITER_SENTENCE_TICKS);
        offender.setStatus(StatusBit.MOVE_ALONG, false);
        closeCase(self);
    }

    /** Base lenience: a clean/positive standing ALWAYS earns the warning (the old baseline). */
    static final int LENIENCE_BASE_PERMILLE = 1000;
    /** Lenience floor: even the Watch's most-hated still draws a 1-in-4 warning chance. */
    static final int LENIENCE_FLOOR_PERMILLE = 250;
    /** Permille of lenience lost per point of NEGATIVE Watch standing. */
    static final int LENIENCE_STANDING_TO_PERMILLE = 10;

    /**
     * The warn-vs-fine lenience threshold (permille) for an offender's Watch standing:
     * {@code clamp(1000 + 10 * standing, 250, 1000)}. Pure, integer-only; package-visible
     * for the crime-spree treatment test.
     */
    static int warnLeniencePermille(int watchStanding) {
        int raw = LENIENCE_BASE_PERMILLE + LENIENCE_STANDING_TO_PERMILLE * watchStanding;
        return Math.max(LENIENCE_FLOOR_PERMILLE, Math.min(LENIENCE_BASE_PERMILLE, raw));
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
    // Shove-riot sensing + the house-arrest correction (density revisit)
    // ======================================================================

    /**
     * The riot probe: scans the shove log oldest-first for an ANCHOR shove {@code E} in the
     * window such that at least {@link #RIOT_AGGRESSORS} DISTINCT pushers each have {@link
     * #RIOT_REPEAT_SHOVES}+ in-window shoves within {@link #RIOT_RADIUS} of {@code E}'s cell
     * AND at least one such aggressor is still arrestable — returns that anchor's cell, or
     * {@link Actor#NONE}. Pure, draw-free, deterministic (the log's oldest-first order is
     * insertion order; the per-pusher tally is insertion-ordered too). Package-visible for
     * the riot unit test.
     */
    static int senseRiotAnchor(ActorContext ctx) {
        ShoveLog log = ctx.shoveLog();
        long now = ctx.tick();
        int[] pushers = new int[log.size()];
        int[] counts = new int[log.size()];
        for (int i = 0; i < log.size(); i++) {
            if (now - log.tickAt(i) > RIOT_WINDOW_TICKS) {
                continue; // too old to anchor
            }
            int anchorCell = log.cellAt(i);
            if (duplicateAnchor(log, now, i, anchorCell)) {
                continue; // an earlier in-window row already probed this identical cluster
            }
            int distinct = tallyClusterPushers(log, now, anchorCell, pushers, counts);
            if (countAggressors(counts, distinct) >= RIOT_AGGRESSORS
                    && anyArrestableAggressor(ctx, pushers, counts, distinct)) {
                return anchorCell;
            }
        }
        return Actor.NONE;
    }

    /** Whether an earlier in-window row shares {@code anchorCell} (identical cluster: skip). */
    private static boolean duplicateAnchor(ShoveLog log, long now, int index, int anchorCell) {
        for (int i = 0; i < index; i++) {
            if (now - log.tickAt(i) <= RIOT_WINDOW_TICKS && log.cellAt(i) == anchorCell) {
                return true;
            }
        }
        return false;
    }

    /**
     * Tallies the in-window shoves within {@link #RIOT_RADIUS} of {@code anchorCell} PER
     * DISTINCT PUSHER into the caller's scratch arrays ({@code pushers[k]} = pusher id,
     * {@code counts[k]} = its clustered shove count; k in first-appearance order — insertion
     * order, so deterministic). Returns the number of distinct pushers tallied.
     */
    private static int tallyClusterPushers(ShoveLog log, long now, int anchorCell,
            int[] pushers, int[] counts) {
        int anchorZ = PackedPos.z(anchorCell);
        int distinct = 0;
        for (int i = 0; i < log.size(); i++) {
            if (now - log.tickAt(i) > RIOT_WINDOW_TICKS
                    || PackedPos.z(log.cellAt(i)) != anchorZ
                    || ActorGeometry.chebyshev(log.cellAt(i), anchorCell) > RIOT_RADIUS) {
                continue;
            }
            int pusher = log.pusherIdAt(i);
            int slot = -1;
            for (int k = 0; k < distinct; k++) {
                if (pushers[k] == pusher) {
                    slot = k;
                    break;
                }
            }
            if (slot < 0) {
                pushers[distinct] = pusher;
                counts[distinct] = 1;
                distinct++;
            } else {
                counts[slot]++;
            }
        }
        return distinct;
    }

    /** Pushers whose clustered shove count marks aggression ({@link #RIOT_REPEAT_SHOVES}+). */
    private static int countAggressors(int[] counts, int distinct) {
        int aggressors = 0;
        for (int k = 0; k < distinct; k++) {
            if (counts[k] >= RIOT_REPEAT_SHOVES) {
                aggressors++;
            }
        }
        return aggressors;
    }

    /** Whether at least one clustered aggressor is still arrestable (gates re-detection). */
    private static boolean anyArrestableAggressor(ActorContext ctx, int[] pushers, int[] counts,
            int distinct) {
        for (int k = 0; k < distinct; k++) {
            if (counts[k] >= RIOT_REPEAT_SHOVES
                    && isHouseArrestable(ctx.registry().get(pushers[k]), ctx)) {
                return true;
            }
        }
        return false;
    }

    /**
     * The correction: every arrestable AGGRESSOR ({@link #RIOT_REPEAT_SHOVES}+ in-window
     * shoves inside the riot cluster) is sent home under a fixed {@link
     * HouseArrestPolicy#HOUSE_ARREST_TICKS} (1-day) house arrest — status bit + absolute
     * deadline; the offender's own {@code HouseArrestPolicy} does the marching. A one-shove
     * passer-by caught in the cluster is NOT arrested (a squeeze-past is not brawling — the
     * busy-door lesson). Returns the number of arrests issued (0 on a re-scan where every
     * aggressor is already corrected — which is exactly what makes the detection idempotent
     * across guards within one cadence tick). Package-visible for the riot unit test.
     */
    static int issueHouseArrests(ActorContext ctx, int anchorCell) {
        ShoveLog log = ctx.shoveLog();
        long now = ctx.tick();
        int[] pushers = new int[log.size()];
        int[] counts = new int[log.size()];
        int distinct = tallyClusterPushers(log, now, anchorCell, pushers, counts);
        int issued = 0;
        for (int k = 0; k < distinct; k++) {
            if (counts[k] < RIOT_REPEAT_SHOVES) {
                continue; // one squeeze-past: in the crowd, not of the brawl
            }
            Actor shover = ctx.registry().get(pushers[k]);
            if (!isHouseArrestable(shover, ctx)) {
                continue; // the Watch/Wielder/beasts are exempt; custody supersedes; no doubles
            }
            shover.setStatus(StatusBit.HOUSE_ARREST, true);
            shover.setHouseArrestUntilTick(now + HouseArrestPolicy.HOUSE_ARREST_TICKS);
            shover.setLastReasonCode(ReasonCode.HOUSE_ARRESTED);
            // Faction ledger (Sprint 1): a riot correction on the PRESENTED identity.
            ctx.factionStandings().onHouseArrest(shover.identity().presentedId());
            issued++;
        }
        return issued;
    }

    /** Guards themselves are never house-arrested; neither are the held/hanged or the already-sent. */
    private static boolean isHouseArrestable(Actor actor, ActorContext ctx) {
        if (actor.hasStatus(StatusBit.HOUSE_ARREST) || actor.hasStatus(StatusBit.HELD)
                || actor.hasStatus(StatusBit.EXECUTED)) {
            return false;
        }
        Job presented = ctx.presentedJob(actor);
        return !(presented instanceof Job.Watch || presented instanceof Job.FlameOfMerc
                || presented instanceof Job.Beast);
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
