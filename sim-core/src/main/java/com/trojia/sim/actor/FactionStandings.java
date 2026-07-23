package com.trojia.sim.actor;

import com.trojia.sim.actor.faction.FactionRegistry;
import com.trojia.sim.world.io.WorldHasher;

import java.io.DataInput;
import java.io.DataOutput;
import java.io.IOException;

/**
 * The per-actor per-faction standing ledger (Sprint 1 "the world remembers me"): a dense
 * clamped-int table, {@code row index == ActorId} (the side-table convention), one column
 * per {@link FactionRegistry} faction. Standings move by DETERMINISTIC deltas at events the
 * sim already emits — arrests and fines and house arrests (down with the Watch, up with the
 * Skyrunner brotherhood: the street respects who the law hates) and purchases (up with the
 * Merchants) — never by draws.
 *
 * <p><b>Identity contract.</b> Every read AND every event delta keys on the actor's
 * PRESENTED id (the Persona rule: all social reads use {@code presentedId()}): the Watch
 * fines the person it believes it fined, so a disguised actor's crimes stain the presented
 * identity, not the true one — the disguise gameplay seam, pre-wired for Sprint 2's
 * unmasking. Callers pass the presented id; this class never resolves identities itself.
 *
 * <p><b>The ONE behavior read this sprint</b> (adversarially verifiable, per the plan):
 * {@link ApprehendPolicy} shifts its warn-vs-fine lenience threshold by
 * {@link #watchStanding} — a crime spree measurably hardens subsequent Watch treatment.
 *
 * <p><b>Determinism.</b> Rows materialize on first delta (event-ordered, so twin runs grow
 * identical tables); values clamp to {@code [-100, +100]}; the whole table is the persisted
 * triad (serialize/load/hashInto) inside the {@code ActorsSystem} chunk with a
 * faction-count frame guard. {@link #UNWIRED} no-ops every delta and reads 0 everywhere —
 * pre-faction behavior, byte-identical.
 */
public final class FactionStandings {

    /** The degraded no-op instance (world-less bootstrap, test doubles, old constructors). */
    public static final FactionStandings UNWIRED = new FactionStandings();

    public static final int MIN_STANDING = -100;
    public static final int MAX_STANDING = 100;

    // ---- the deterministic event deltas (Sprint-1 tuning: small, clamp-bounded) ----
    /** An arrest: the Watch remembers an offender... */
    public static final int ARREST_WATCH_DELTA = -20;
    /** ...and the Skyrunner brotherhood warms to whoever the Watch cages. */
    public static final int ARREST_SKYRUNNERS_DELTA = 10;
    /** A loiter fine. */
    public static final int FINE_WATCH_DELTA = -10;
    public static final int FINE_SKYRUNNERS_DELTA = 5;
    /** A shove-riot house arrest. */
    public static final int HOUSE_ARREST_WATCH_DELTA = -15;
    public static final int HOUSE_ARREST_SKYRUNNERS_DELTA = 5;
    /** A market purchase: honest coin, remembered by the counter trade. */
    public static final int PURCHASE_MERCHANTS_DELTA = 1;

    // Well-known faction raws keys the event deltas route to.
    static final String KEY_WATCH = "watch";
    static final String KEY_SKYRUNNERS = "skyrunners";
    static final String KEY_MERCHANTS = "merchants";

    /** The boot-built faction universe, or {@code null} when unwired. */
    private final FactionRegistry factions;
    /** Dense standings, {@code [actorId * factionCount + faction]}; {@code rows} rows live. */
    private int[] standings = new int[0];
    private int rows;

    private final int watch;
    private final int skyrunners;
    private final int merchants;

    private FactionStandings() {
        this.factions = null;
        this.watch = -1;
        this.skyrunners = -1;
        this.merchants = -1;
    }

    /** Wires the live ledger against a boot-built registry (the committed 5-faction raws). */
    public FactionStandings(FactionRegistry factions) {
        this.factions = java.util.Objects.requireNonNull(factions, "factions");
        this.watch = factions.contains(KEY_WATCH) ? factions.rawId(KEY_WATCH) : -1;
        this.skyrunners = factions.contains(KEY_SKYRUNNERS) ? factions.rawId(KEY_SKYRUNNERS) : -1;
        this.merchants = factions.contains(KEY_MERCHANTS) ? factions.rawId(KEY_MERCHANTS) : -1;
    }

    /** Whether a real faction universe is wired (deltas live, standings meaningful). */
    public boolean isWired() {
        return factions != null;
    }

    /** The wired faction universe. {@code null} when {@link #UNWIRED}. */
    public FactionRegistry factions() {
        return factions;
    }

    /** Faction column count (0 when unwired). */
    public int factionCount() {
        return factions == null ? 0 : factions.size();
    }

    /**
     * The standing of a (PRESENTED) actor id with a faction, {@code [-100, +100]}; 0 when
     * unwired, out of range, or never moved (the neutral default — no row materialized).
     */
    public int standingOf(int presentedId, int faction) {
        if (factions == null || faction < 0 || faction >= factions.size()
                || presentedId < 0 || presentedId >= rows) {
            return 0;
        }
        return standings[presentedId * factions.size() + faction];
    }

    /** {@link #standingOf} with the Watch column — the ApprehendPolicy lenience read. */
    public int watchStanding(int presentedId) {
        return standingOf(presentedId, watch);
    }

    /**
     * Applies one clamped delta. No-op when unwired or the faction is absent from the raws
     * — event sites never need to guard.
     */
    public void adjust(int presentedId, int faction, int delta) {
        if (factions == null || faction < 0 || faction >= factions.size() || presentedId < 0) {
            return;
        }
        ensureRows(presentedId + 1);
        int at = presentedId * factions.size() + faction;
        int next = standings[at] + delta;
        standings[at] = Math.max(MIN_STANDING, Math.min(MAX_STANDING, next));
    }

    private void ensureRows(int wantRows) {
        if (wantRows <= rows) {
            return;
        }
        int width = factions.size();
        if (wantRows * width > standings.length) {
            int newCap = Math.max(wantRows * width, Math.max(16 * width, standings.length * 2));
            standings = java.util.Arrays.copyOf(standings, newCap);
        }
        rows = wantRows;
    }

    // ---- the event verbs (one-liners at the sim's existing resolution points) ----

    /** An arrest landed on this presented identity ({@code JobBehaviors.arrestAndHold}). */
    public void onArrest(int presentedId) {
        adjust(presentedId, watch, ARREST_WATCH_DELTA);
        adjust(presentedId, skyrunners, ARREST_SKYRUNNERS_DELTA);
    }

    /** A loiter fine landed on this presented identity ({@code ApprehendPolicy}). */
    public void onFine(int presentedId) {
        adjust(presentedId, watch, FINE_WATCH_DELTA);
        adjust(presentedId, skyrunners, FINE_SKYRUNNERS_DELTA);
    }

    /** A shove-riot house arrest landed on this presented identity ({@code ApprehendPolicy}). */
    public void onHouseArrest(int presentedId) {
        adjust(presentedId, watch, HOUSE_ARREST_WATCH_DELTA);
        adjust(presentedId, skyrunners, HOUSE_ARREST_SKYRUNNERS_DELTA);
    }

    /** A FOOD purchase by this presented identity (counter buys + household provisioning). */
    public void onPurchase(int presentedId) {
        adjust(presentedId, merchants, PURCHASE_MERCHANTS_DELTA);
    }

    // ======================================================================
    // The persisted triad (rides the ActorsSystem TROJSAV chunk)
    // ======================================================================

    /** Serializes the frame guard (faction count), the row count, then the dense values. */
    public void serialize(DataOutput out) throws IOException {
        int width = factionCount();
        out.writeInt(width);
        out.writeInt(rows);
        for (int i = 0; i < rows * width; i++) {
            out.writeInt(standings[i]);
        }
    }

    /** Loads what {@link #serialize} wrote; the wired faction count must match the frame. */
    public void load(DataInput in) throws IOException {
        int width = in.readInt();
        if (width != factionCount()) {
            throw new IOException("faction-standing frame mismatch: serialized factionCount="
                    + width + " but the loading system wires " + factionCount()
                    + " (same raws must be booted before load)");
        }
        int loadedRows = in.readInt();
        if (loadedRows > 0) {
            ensureRows(loadedRows);
            for (int i = 0; i < loadedRows * width; i++) {
                standings[i] = in.readInt();
            }
        }
    }

    /** Hashes the exact state {@link #serialize} writes, in the same canonical order. */
    public void hashInto(WorldHasher.Sink sink) {
        int width = factionCount();
        sink.putInt(width);
        sink.putInt(rows);
        for (int i = 0; i < rows * width; i++) {
            sink.putInt(standings[i]);
        }
    }
}
