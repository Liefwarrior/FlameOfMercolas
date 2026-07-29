package com.trojia.client.fpv;

/**
 * <b>The decoupling.</b> The actor STEPS — on the grid, one cell at a time, through the sim's
 * own occupancy cap and walls and shove escape and speed gate, entirely unchanged. The EYE
 * slides. This class is the whole of that seam: it is handed the cell the sim has actually
 * committed the actor to, and it produces a continuous position that eases toward it at
 * whatever framerate the window is running.
 *
 * <p>That is what buys a smooth high-framerate view over a 100 ms tick without ever lying
 * about where anybody is. The camera is never authoritative for anything: at every instant the
 * actor is exactly where {@code Actor.cell()} says, and the eye is merely somewhere on the
 * line between the last cell and that one. Pure state and pure math — no libGDX, no world, no
 * clock of its own — so the whole feel of movement is unit-testable.
 *
 * <h2>Getting the stride right</h2>
 *
 * <p>A teleport is a step that finishes instantly; a glide is a step that never finishes. The
 * ease here is <b>ease-out cubic over a measured duration</b>. Ease-out (fast off the mark,
 * settling into the cell) rather than symmetric smoothstep, because the first frames after a
 * keypress are where responsiveness is felt, and because a stride is a push followed by a
 * settle, not a swell.
 *
 * <p>The duration is <em>measured, not assumed</em>. Step cadence is
 * {@code speedTicksPerStep} x the tick length, and the tick length itself changes with the
 * observer's speed setting — a duration hard-coded for 100 ms ticks turns into a glide the
 * moment someone presses the fast-forward key, because the next step commits while the last
 * ease is still running. So the camera times the interval between committed steps and sizes
 * each ease at {@link #STEP_ARRIVAL_FRACTION} of the last one, clamped. It arrives before the
 * next step is due, at any sim speed, without being told what the sim speed is.
 *
 * <p>A vertical band change gets its own, longer ease ({@link #CLIMB_ARRIVAL_FRACTION}): a
 * climb should read as a climb, and an instant band-height translation is the most jarring
 * thing this view can do. And each lateral stride carries a small head dip, phase-locked to
 * the ease itself rather than free-running, so the bob is the walk instead of a wobble laid
 * over it.
 *
 * <h2>Yaw is the client's, facing is the sim's</h2>
 *
 * <p>{@link #yaw()} is a continuous, client-side view direction. {@code Actor.facing()} is
 * four-way sim state, written only on a committed step, and on a diagonal stride it takes the
 * x component — so a north-east step reports EAST. Reconciling a continuous look direction
 * against that every step would snap the camera ninety degrees sideways while you walk.
 *
 * <p>So the flow is one-way and one-time: {@link #snapTo} seeds the yaw from the actor's
 * facing when the view is first entered (or the driven actor changes), and after that the yaw
 * belongs to the player. Nothing here ever calls {@code Actor.setFacing} — facing is
 * serialized sim state that the twin-run gate compares byte-for-byte, and a camera that writes
 * it would show up as a nondeterminism bug rather than as the camera bug it is.
 */
public final class FirstPersonCamera {

    /** Fraction of the measured step interval a lateral ease is allowed to take. */
    public static final float STEP_ARRIVAL_FRACTION = 0.62f;

    /** Fraction of the measured step interval a band change is allowed to take — longer, so
     * a climb reads as climbing, but still short of the next step. */
    public static final float CLIMB_ARRIVAL_FRACTION = 0.92f;

    /** Shortest an ease may be: below this a step is a teleport again. */
    public static final float MIN_EASE_SECONDS = 0.045f;

    /** Longest a lateral ease may be, however slow the sim is running. */
    public static final float MAX_STEP_EASE_SECONDS = 0.20f;

    /** Longest a band-change ease may be. */
    public static final float MAX_CLIMB_EASE_SECONDS = 0.42f;

    /** Ease used for the very first step, before any interval has been measured. */
    private static final float DEFAULT_EASE_SECONDS = 0.16f;

    /** Depth of the head dip at mid-stride, in world units. */
    public static final float BOB_DIP = 0.055f;

    private static final float TWO_PI = (float) (Math.PI * 2);

    private float x;
    private float y;
    /** World height of the eye's FEET — the floor slab it is standing on, eased. */
    private float feetHeight;
    private float yaw;

    private int band;
    private int targetTileX;
    private int targetTileY;

    private float fromX;
    private float fromY;
    private float fromHeight;
    private float toX;
    private float toY;
    private float toHeight;
    private float easeElapsed;
    private float easeDuration;
    private boolean easeIsClimb;

    private float sinceLastStep;
    private float measuredInterval;
    private boolean seeded;

    /**
     * Hard-places the eye on a cell with no easing — entering the view, or handing control to
     * a different actor. {@code yawRadians} normally comes from {@code Actor.facing()} exactly
     * once, here; see the class javadoc.
     */
    public void snapTo(int tileX, int tileY, int bandZ, float yawRadians) {
        this.targetTileX = tileX;
        this.targetTileY = tileY;
        this.band = bandZ;
        this.x = tileX + 0.5f;
        this.y = tileY + 0.5f;
        this.feetHeight = BandGeometry.floorHeight(bandZ);
        this.fromX = x;
        this.fromY = y;
        this.fromHeight = feetHeight;
        this.toX = x;
        this.toY = y;
        this.toHeight = feetHeight;
        this.easeElapsed = 0f;
        this.easeDuration = 0f;
        this.easeIsClimb = false;
        this.sinceLastStep = 0f;
        this.measuredInterval = 0f;
        this.yaw = normalize(yawRadians);
        this.seeded = true;
    }

    /** Whether {@link #snapTo} has ever run — the eye has nowhere to be until it has. */
    public boolean isPlaced() {
        return seeded;
    }

    /**
     * Tells the camera where the sim has actually put the actor. Call every frame with
     * {@code Actor.cell()}; a repeat of the current cell is free. A change starts a fresh
     * ease from wherever the eye currently is, so chained steps read as continuous walking
     * rather than as a queue of hops.
     */
    public void followCell(int tileX, int tileY, int bandZ) {
        if (!seeded) {
            snapTo(tileX, tileY, bandZ, yaw);
            return;
        }
        if (tileX == targetTileX && tileY == targetTileY && bandZ == band) {
            return;
        }
        boolean climbed = bandZ != band;
        if (sinceLastStep > 0f) {
            measuredInterval = sinceLastStep;
        }
        sinceLastStep = 0f;
        targetTileX = tileX;
        targetTileY = tileY;
        band = bandZ;
        fromX = x;
        fromY = y;
        fromHeight = feetHeight;
        toX = tileX + 0.5f;
        toY = tileY + 0.5f;
        toHeight = BandGeometry.floorHeight(bandZ);
        easeElapsed = 0f;
        easeIsClimb = climbed;
        easeDuration = easeSeconds(climbed);
    }

    /**
     * How long this step's ease should take: a fraction of the last measured gap between
     * committed steps, so it lands before the next one is due whatever speed the sim is
     * running at. Falls back to a sane default until a gap has been seen.
     */
    private float easeSeconds(boolean climbed) {
        float fraction = climbed ? CLIMB_ARRIVAL_FRACTION : STEP_ARRIVAL_FRACTION;
        float cap = climbed ? MAX_CLIMB_EASE_SECONDS : MAX_STEP_EASE_SECONDS;
        if (measuredInterval <= 0f) {
            return Math.min(cap, DEFAULT_EASE_SECONDS);
        }
        return Math.max(MIN_EASE_SECONDS, Math.min(cap, fraction * measuredInterval));
    }

    /** Advances the ease and the head bob by a frame. */
    public void advance(float deltaSeconds) {
        if (!seeded || deltaSeconds <= 0f) {
            return;
        }
        sinceLastStep += deltaSeconds;
        if (easeDuration <= 0f) {
            x = toX;
            y = toY;
            feetHeight = toHeight;
            return;
        }
        easeElapsed = Math.min(easeDuration, easeElapsed + deltaSeconds);
        float t = easeElapsed / easeDuration;
        float e = easeOut(t);
        x = fromX + (toX - fromX) * e;
        y = fromY + (toY - fromY) * e;
        feetHeight = fromHeight + (toHeight - fromHeight) * e;
        if (easeElapsed >= easeDuration) {
            easeDuration = 0f;
            x = toX;
            y = toY;
            feetHeight = toHeight;
        }
    }

    /** Ease-out cubic: leaves immediately, settles into the cell. */
    static float easeOut(float t) {
        float inv = 1f - t;
        return 1f - inv * inv * inv;
    }

    /** Turns the view. Purely a client-side look direction — never written back to the sim. */
    public void turn(float radians) {
        yaw = normalize(yaw + radians);
    }

    /** Sets the view direction outright (the mode switch seeding it from a facing). */
    public void setYaw(float radians) {
        yaw = normalize(radians);
    }

    // ------------------------------------------------------------------ readout

    /** Continuous eye x in tile units. */
    public float eyeX() {
        return x;
    }

    /** Continuous eye y in tile units. */
    public float eyeY() {
        return y;
    }

    /**
     * Continuous eye height in world units: the eased floor height plus standing height, plus
     * the stride's head dip. The dip is a half-sine over the lateral ease — deepest at
     * mid-stride, gone on arrival — so it is the walk rather than an idle wobble, and standing
     * still is perfectly still.
     */
    public float eyeHeight() {
        return feetHeight + BandGeometry.EYE_HEIGHT - bobDip();
    }

    private float bobDip() {
        if (easeDuration <= 0f || easeIsClimb) {
            return 0f;
        }
        float t = easeElapsed / easeDuration;
        return BOB_DIP * (float) Math.sin(Math.PI * t);
    }

    /** View yaw in radians: 0 looks EAST, increasing toward SOUTH. */
    public float yaw() {
        return yaw;
    }

    /** The band the driven actor is standing on — integer, straight from the sim. */
    public int band() {
        return band;
    }

    /** The cell the driven actor is standing on (x). */
    public int tileX() {
        return targetTileX;
    }

    /** The cell the driven actor is standing on (y). */
    public int tileY() {
        return targetTileY;
    }

    /** Whether an ease is still running — the stride has not landed yet. */
    public boolean isStriding() {
        return easeDuration > 0f;
    }

    /** The last measured gap between committed steps, seconds (0 before any is seen). */
    public float measuredStepIntervalSeconds() {
        return measuredInterval;
    }

    /** Wraps an angle into {@code [0, 2pi)}. */
    public static float normalize(float radians) {
        float r = radians % TWO_PI;
        return r < 0f ? r + TWO_PI : r;
    }
}
