package com.trojia.sim.actor;

import com.trojia.sim.world.PackedPos;

/**
 * The shove verb (density revisit, Eli's push mechanic): when a mover's step is blocked ONLY by
 * the occupancy cap (walkable floor, leash fine, but somebody is standing there), it may — on a
 * {@link #PUSH_COOLDOWN_TICKS}-tick cooldown — displace the occupant to a deterministic adjacent
 * free walkable cell and take the vacated square; when NO free cell exists, pusher and occupant
 * SWAP cells instead (the squeeze-past, below). Stateless static verbs (the
 * {@link PathFinder}/{@code JobBehaviors} precedent), called from {@link ActorsSystem}'s live
 * occupancy view via {@link Actor.OccupancyQuery#tryPush}.
 *
 * <p><b>The squeeze-past swap (density revisit fix pass — the corridor-gridlock liveness fix):</b>
 * under ONE-per-square, two actors meeting head-on in a 1-wide passage are unresolvable by
 * displacement alone — every adjacent cell of each is a wall or the other party, both replan the
 * identical only route every tick, and the pair stands gridlocked forever (soak-measured: a serf
 * and a shopkeeper parked 3,500+ ticks at a Backwall bend, incidentally sealing a starving gull
 * inside the dead-end alcove behind them). The physical resolution real pedestrians use is to
 * squeeze past each other, so when the displacement scan finds no free cell the pusher and the
 * occupant exchange cells atomically: the cap is never violated (both cells stay at one), no
 * third cell is involved, and every 1-wide head-on deadlock dissolves in one cooldown. The swap
 * is a full shove — logged, cooldown-burning, stagger-stamping — so a scrum of squeezers at a
 * genuine crush point still feeds the riot detector exactly like any other repeat shoving.
 *
 * <p><b>No push chains, by construction and by stagger.</b> The displaced occupant is only ever
 * moved into a cell that is already FREE (occupancy 0) or into the pusher's own simultaneously
 * vacated cell (the swap), so one shove can never mechanically cascade into another.
 * Additionally the displacement back-dates the pushee's own {@code lastPushTick} (being shoved
 * staggers you for {@link #PUSHEE_STAGGER_TICKS} ticks), so a just-displaced actor cannot
 * itself shove anyone this tick or the next — closing the chain-reaction loophole where a
 * displaced actor's own later-in-tick move starts a conga line, WITHOUT handing a saturated
 * crowd the power to perpetually disarm one trapped victim (the constant's own doc).
 *
 * <p><b>The push CONTEST (Sprint 1 skill-check core, first consumer).</b> Where a wired
 * {@link SkillTrackRegistry} is in play, the shove is no longer automatic: the pusher's
 * open_hand+AGI is checked against the occupant's grit+VIG on the {@code check.push} named
 * stream ({@link SkillChecks#pushContestPermille} — floor/ceiling-clamped so the contest
 * stays a liveness-safe perturbation). A LOST contest burns no cooldown (the blocked step
 * simply retries next tick, so deadlock dissolution is delayed a tick or two at worst,
 * never defeated). A WON contest awards use-XP at the outcome that just resolved: the
 * pusher's open_hand (&sect;3.1 "strike landed", context = the shovee) and the shovee's
 * grit (&sect;3.1 "blunt", pain teaches — context = the pusher). Unwired contexts (the
 * world-less bootstrap, every pre-existing test) skip the contest and the awards entirely:
 * byte-identical to the pre-progression shove.
 *
 * <p><b>Determinism.</b> Ascending fixed-order direction scan for the displacement target
 * (W, E, N, S, then the four diagonals — {@link PathFinder}'s neighbor order), ascending-id
 * registry scan for the occupant, absolute-tick cooldown arithmetic; the only draw is the
 * wired contest's single named {@code check.push} draw, attributed to the pusher through
 * the shared per-actor per-tick counter.
 */
public final class PushMechanics {

    /**
     * The contest's draw source: invoked AT MOST ONCE per {@link #tryPush} call, only after
     * the cooldown and occupant gates pass and only when tracks are wired — so a
     * cooldown-blocked attempt consumes no draw index. The caller binds it to the
     * {@code check.push} stream with the pusher's own next draw index.
     */
    @FunctionalInterface
    public interface ContestDraw {
        long draw(int pusherId);
    }

    /** Pusher's open_hand base award for a won push contest (§3.1 "strike landed" row). */
    public static final int OPEN_HAND_SHOVE_CP = 90;
    /** Shovee's grit base award for being displaced (§3.1 "blunt damage" row — pain teaches). */
    public static final int GRIT_SHOVED_CP = 150;

    /** Cooldown between self-initiated shoves by one actor: 10 ticks = 10 seconds. */
    public static final long PUSH_COOLDOWN_TICKS = 10;
    /**
     * Stagger on the PUSHEE: being shoved winds you for this many ticks before you can shove
     * anyone yourself. Deliberately much shorter than the pusher's {@link #PUSH_COOLDOWN_TICKS}
     * (density revisit fix pass — the crowd-lock fix): the stagger's job is only to break
     * same-tick/next-tick chain reactions, and at a full 10 ticks it handed a SATURATED room
     * collective supremacy over any single trapped actor — six parked residents each shoving
     * once per cooldown reset the victim's whole clock on every bounce, so it never
     * accumulated the quiet ticks to shove its own way out (soak-measured: gull#410
     * crowd-locked for 8,000 ticks between a bunkroom's dead-end alcove and its 1-wide neck,
     * starving while its prey sat four cells outside the wall). Two ticks still kills the
     * conga line (a displaced actor cannot push this tick or next) while letting the bounced
     * fight back between incoming shoves. Encoded by BACK-DATING {@code lastPushTick} on the
     * pushee ({@code tick - (PUSH_COOLDOWN_TICKS - PUSHEE_STAGGER_TICKS)}), so the single
     * persisted clock serves both roles and the serialized shape is unchanged.
     */
    public static final long PUSHEE_STAGGER_TICKS = 2;

    /** Fixed displacement-scan order — W, E, N, S, then diagonals (PathFinder's neighbor order). */
    private static final int[] DX = {-1, 1, 0, 0, -1, 1, -1, 1};
    private static final int[] DY = {0, 0, -1, 1, -1, -1, 1, 1};

    private PushMechanics() {
    }

    /**
     * Attempts the shove: {@code pusher} wants {@code contestedCell} (already checked walkable,
     * leash-legal, and at the occupancy cap by {@link Actor}'s step gate). Returns {@code true}
     * iff the occupant was displaced — the contested cell is then free and the caller commits
     * the pusher's step onto it.
     *
     * <ol>
     *   <li><b>Cooldown</b>: {@code tick - pusher.lastPushTick() >= PUSH_COOLDOWN_TICKS}.</li>
     *   <li><b>Occupant</b>: the lowest-id actor standing on {@code contestedCell}.</li>
     *   <li><b>Displacement cell</b>: the first adjacent same-z cell in the fixed scan order
     *       that is walkable, not a solid-corner diagonal cut (PathFinder parity — a shove must
     *       never seal an actor into an A*-unreachable pocket), and currently FREE (occupancy 0
     *       — the cap is never violated, and no chain push can occur). If none exists, the
     *       displacement target is the pusher's OWN cell — the squeeze-past swap (class doc):
     *       the pusher's step onto the vacated contested cell completes the exchange.</li>
     *   <li><b>Commit</b>: pushee moves ({@code occ.onEnter} keeps the shared index live), its
     *       cached route is invalidated (force replan around wherever it now stands), both
     *       parties' {@code lastPushTick} stamp to {@code tick} (cooldown + stagger), and the
     *       shove is recorded in {@code log} for riot detection.</li>
     * </ol>
     */
    public static boolean tryPush(Actor pusher, int contestedCell, ActorRegistry registry,
            long tick, Actor.WalkabilityQuery walk, Actor.OccupancyQuery occ, ShoveLog log) {
        // Contest-free overload (world-less bootstrap + pre-progression tests): unwired
        // tracks skip the check and the awards — byte-identical to the original shove.
        return tryPush(pusher, contestedCell, registry, tick, walk, occ, log,
                SkillTrackRegistry.UNWIRED, id -> 0L);
    }

    /**
     * The wired overload (Sprint 1): same shove, but resolved as an open_hand-vs-grit
     * contest and awarding use-XP on success (class doc). {@code contestDraw} is consumed
     * at most once, only when the contest actually runs.
     */
    public static boolean tryPush(Actor pusher, int contestedCell, ActorRegistry registry,
            long tick, Actor.WalkabilityQuery walk, Actor.OccupancyQuery occ, ShoveLog log,
            SkillTrackRegistry tracks, ContestDraw contestDraw) {
        if (tick - pusher.lastPushTick() < PUSH_COOLDOWN_TICKS) {
            return false; // still winded from the last shove (or from being shoved)
        }
        Actor pushee = occupantOf(contestedCell, pusher, registry);
        if (pushee == null) {
            return false; // defensive: the index said full but no actor stands there
        }
        if (tracks.isWired()) {
            int permille = SkillChecks.pushContestPermille(tracks, pusher.id(), pushee.id());
            if (!SkillChecks.passes(contestDraw.draw(pusher.id()), permille)) {
                // Contest lost: the occupant holds its ground. No cooldown burn (the
                // liveness guard — the blocked step retries next tick), no XP, no log row.
                return false;
            }
        }
        int dest = displacementCell(contestedCell, walk, occ);
        if (dest == Actor.NONE) {
            // No free cell anywhere adjacent: squeeze past — the pusher and the occupant swap.
            // The pusher's own cell is walkable by construction (it stands there) and is
            // vacated by the very step this push unblocks, so the cap holds end to end.
            dest = pusher.cell();
        }
        pushee.setCell(dest);
        occ.onEnter(contestedCell, dest);
        pushee.invalidateRoute();
        // Stagger: a just-shoved actor cannot shove this tick or the next (back-dated stamp,
        // see PUSHEE_STAGGER_TICKS — much shorter than the pusher's own full cooldown). The
        // max() keeps the stamp monotone: being shoved must never SHORTEN a wait already on
        // the clock (a fresh pusher who is immediately shoved back would otherwise have its
        // full cooldown replaced by the 2-tick stagger, and a mutually-swapping pair could
        // thrash at stagger rate instead of cooldown rate — measured 4x total-push inflation).
        pushee.setLastPushTick(Math.max(pushee.lastPushTick(),
                tick - (PUSH_COOLDOWN_TICKS - PUSHEE_STAGGER_TICKS)));
        pusher.setLastPushTick(tick);
        log.record(tick, contestedCell, pusher.id());
        // The contest resolved (Sprint 1): use-XP lands at the outcome, both parties, TRUE
        // ids (a physical contest, not a social read). Satiation context = the other party,
        // so the same pair jostling at one doorway decays to the 25% floor (§3.3) while
        // fresh opponents pay full rate. No-ops when unwired.
        tracks.award(pusher.id(), tracks.openHandRaw(), OPEN_HAND_SHOVE_CP, pushee.id(), tick);
        tracks.award(pushee.id(), tracks.gritRaw(), GRIT_SHOVED_CP, pusher.id(), tick);
        return true;
    }

    /** The lowest-id actor (never {@code pusher}) standing on {@code cell}, or {@code null}. */
    private static Actor occupantOf(int cell, Actor pusher, ActorRegistry registry) {
        for (int i = 0; i < registry.size(); i++) {
            Actor actor = registry.get(i);
            if (actor.id() != pusher.id() && actor.cell() == cell) {
                return actor;
            }
        }
        return null;
    }

    /**
     * First adjacent free walkable same-z cell of {@code contestedCell} in the fixed scan order,
     * or {@link Actor#NONE}. Diagonal displacements are flank-checked against walls (never cut a
     * solid diagonal corner — movement/pathfinding parity), and only a cell with occupancy 0
     * qualifies, so the cap holds and the displaced actor can always route back out.
     */
    private static int displacementCell(int contestedCell, Actor.WalkabilityQuery walk,
            Actor.OccupancyQuery occ) {
        int x = PackedPos.x(contestedCell);
        int y = PackedPos.y(contestedCell);
        int z = PackedPos.z(contestedCell);
        for (int d = 0; d < DX.length; d++) {
            int nx = x + DX[d];
            int ny = y + DY[d];
            if (nx < 0 || ny < 0 || nx > PackedPos.X_MASK || ny > PackedPos.Y_MASK) {
                continue;
            }
            int cell = PackedPos.pack(nx, ny, z);
            if (!walk.isWalkable(cell)) {
                continue;
            }
            if (DX[d] != 0 && DY[d] != 0
                    && (!walk.isWalkable(PackedPos.pack(nx, y, z))
                            || !walk.isWalkable(PackedPos.pack(x, ny, z)))) {
                continue; // never shove someone through a solid diagonal wall corner
            }
            if (occ.occupantsAt(cell) > 0) {
                continue; // only a FREE cell: the cap holds and no push chain can start
            }
            return cell;
        }
        return Actor.NONE;
    }
}
