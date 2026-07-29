package com.trojia.sim.actor;

import com.trojia.sim.world.PackedPos;

/**
 * THE CULL VERB (S8, "The Ward Prices Itself"). Eli's ruling: kills are counted as SCALPS, not
 * counters — a scalpable type drops a NAMED ITEM, so kill-tracking is an item problem, and
 * items already persist, already move through counters, already appear in a conservation
 * proof. This class is the whole of that: stand beside a downed body, spend a work event,
 * make the check, and there is a scalp in your hand.
 *
 * <p><b>VERMIN ONLY.</b> Combat is out of this arc (Decision 2b amendment, commit 57a444f), so
 * the only bodies on the ground are the ones the S6 beast hunt already puts there — a mouse a
 * cat ran down. Human scalps defer past S12; there is deliberately no wastrel scalp and no
 * melee here. WHICH type drops WHICH scalp is raws data ({@code scalpItem}/{@code scalpResist}
 * on the actor JSON), so this file names no species.
 *
 * <p><b>The body's revive timer is NOT touched, and that is load-bearing.</b> A downed mouse's
 * {@code downedTimer} is the predators' food supply — {@code PREY_REVIVE_TICKS} was already
 * tuned down from 6000 by a 30k soak because thin clusters starved their cats. Culling reads
 * the body and writes nothing to it. The scalp is a by-product of a death the ecology already
 * decided on; the cull verb does not get to spend the ecology's budget.
 *
 * <p><b>What stops the faucet</b> is a per-CULLER latch ({@link Actor#culledUntilTick}, the
 * {@code houseArrestUntilTick} shape) rather than any state on the body: one soul beside one
 * carcass takes ONE scalp per {@link #CULL_COOLDOWN_TICKS}, win or lose, because the attempt is
 * what costs the time. Two different souls may each work the same carcass — that is a crowd
 * around a windfall, not an exploit.
 *
 * <p>Draw-free except for the single named {@code check.cull} draw, resolved through the ONE
 * {@link SkillChecks#successPermille} function like every other check family, with a ceiling
 * below 1000 so no hand is ever certain. Ascending-index scans only; no maps, no allocation.
 */
public final class CullVerb {

    /**
     * Chebyshev reach of the knife: adjacency. You do not skin something across the lane, and
     * the 2-per-cell occupancy cap means a culler can rarely stand ON the body anyway.
     */
    public static final int CULL_REACH = 1;

    /**
     * Ticks before the same soul may take another scalp. Sized against the work-event cadence
     * (a craft yard finishes a unit every ~30 ticks): without a latch, one worker beside one
     * downed mouse would harvest a scalp every work event for the body's whole revive
     * countdown — hundreds of scalps off one carcass. At {@value} a soul working beside a
     * steady supply of quarry takes a handful a day, which is what a bounty looks like.
     * Mirrors {@code BeastHuntPolicy.HUNT_BACKOFF_TICKS} (500), the same bounded-latch
     * precedent.
     */
    public static final int CULL_COOLDOWN_TICKS = 500;

    /**
     * FIELDCRAFT cp per cull ATTEMPT — pelt or no pelt, exactly like the fishing cast's
     * attempt-XP (Eli's S6 spec: XP on attempts, not only on wins). Priced at the same scale
     * as the FISHING cast so neither verb is the obviously better grind.
     */
    public static final int CULL_ATTEMPT_CP = 60;

    /**
     * Factions whose members do not take scalps. A cat that has just eaten a mouse is not
     * going to skin it, and a hungry gull is the quarry in the next line of the same table.
     * The gate is stated as data-shaped constants rather than a type list so a new beast type
     * inherits it for free; combined with an inventory (you need somewhere to put a pelt) and
     * with not being scalpable yourself, it means exactly the ward's PEOPLE cull.
     */
    private static final String[] NON_CULLING_FACTIONS = {"animals", "feral"};

    private CullVerb() {
    }

    /**
     * Whether {@code self} is the sort of soul that takes scalps at all: it has somewhere to
     * put one, it is not itself somebody's quarry, and it is not a beast. Pure raws read.
     */
    public static boolean canCull(Actor self) {
        ActorTypeStats stats = self.stats();
        if (stats.inventoryCap() <= 0 || stats.isScalpable()) {
            return false;
        }
        for (String faction : NON_CULLING_FACTIONS) {
            if (faction.equals(stats.factionId())) {
                return false;
            }
        }
        return true;
    }

    /** Whether {@code body} is a downed, scalpable carcass — the thing a cull needs. */
    public static boolean isQuarry(Actor body) {
        return body.hasStatus(StatusBit.DOWNED) && body.stats().isScalpable();
    }

    /**
     * The nearest DOWNED scalpable body within {@link #CULL_REACH} of {@code self} on the same
     * z, ascending registry index breaking ties (the {@code senseNearestPrey} discipline), or
     * {@link Actor#NONE}. Read-only and draw-free, so a caller may probe before committing.
     */
    public static int quarryInReach(Actor self, ActorContext ctx) {
        ActorRegistry registry = ctx.registry();
        int selfCell = self.cell();
        int selfZ = PackedPos.z(selfCell);
        int best = Actor.NONE;
        int bestDist = CULL_REACH + 1;
        for (int i = 0; i < registry.size(); i++) {
            Actor other = registry.get(i);
            if (other.id() == self.id() || !isQuarry(other)) {
                continue;
            }
            int cell = other.cell();
            if (PackedPos.z(cell) != selfZ) {
                continue;
            }
            int d = ActorGeometry.chebyshev(selfCell, cell);
            if (d <= CULL_REACH && d < bestDist) {
                bestDist = d;
                best = i;
            }
        }
        return best;
    }

    /**
     * The opportunistic cull, called at the job system's EXISTING work-event seam: if this
     * soul may cull, its latch has expired and a downed scalpable body is within knife reach,
     * spend the attempt. Returns whether an attempt was made (not whether it succeeded).
     *
     * <p>This rides a work event rather than a policy on purpose. Adding a CULL policy would
     * have people walking across the ward to stand over carcasses, which changes where the
     * population is, which is precisely the sort of quiet perturbation that could eat the
     * predators' supply margin. Riding the seam means the ward's routes do not move at all:
     * the hands that cull are the hands that were already working there.
     */
    public static boolean tryCullInReach(Actor self, ActorContext ctx) {
        if (!canCull(self) || ctx.tick() < self.culledUntilTick()) {
            return false;
        }
        int body = quarryInReach(self, ctx);
        if (body == Actor.NONE) {
            return false;
        }
        resolveCull(self, ctx, body);
        return true;
    }

    /**
     * Resolves ONE cull attempt on {@code bodyId} (shared by the work-event seam and the
     * play-mode verb, the {@code resolveCastAttempt} precedent): FIELDCRAFT cp on the attempt,
     * the {@code check.cull} named draw against the quarry type's authored resist, the scalp
     * minted into the culler's carry on success. Stamps the latch either way — the attempt is
     * what costs the time. Reason-coded TOOK_SCALP / SCALP_RUINED.
     *
     * <p>Writes NOTHING to the body: no revive-timer reset, no status change, no need delta.
     *
     * @return {@code true} if the scalp was taken
     */
    public static boolean resolveCull(Actor self, ActorContext ctx, int bodyId) {
        Actor body = ctx.registry().get(bodyId);
        self.setCulledUntilTick(ctx.tick() + CULL_COOLDOWN_TICKS);
        SkillTrackRegistry tracks = ctx.skillTracks();
        tracks.award(self.id(), tracks.fieldcraftRaw(), CULL_ATTEMPT_CP, body.cell(), ctx.tick());
        long draw = ctx.draw(ActorRngStream.CHECK_CULL, self.id(), ctx.nextDrawIndex(self.id()));
        int permille = SkillChecks.cullPermille(tracks, self.id(), body.stats().scalpResist());
        if (!SkillChecks.passes(draw, permille)) {
            self.setLastReasonCode(ReasonCode.SCALP_RUINED);
            return false;
        }
        short kind = body.stats().scalpItemKind();
        int minted = ctx.items().addCarried(self.id(), kind, 1);
        ctx.recordGoodsMinted(kind, minted);
        ctx.recordScalpTaken(self.id(), kind);
        self.setLastReasonCode(ReasonCode.TOOK_SCALP);
        return true;
    }
}
