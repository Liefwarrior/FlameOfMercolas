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
 * <p>A teleport is a step that finishes instantly; a glide is a step that never finishes. A
 * <b>lurch</b> is neither, and it is what the first cut of this class shipped: ease-out cubic
 * squeezed into 62% of the step cadence.
 *
 * <p><b>What the cadence actually is.</b> A step commits every {@code speedTicksPerStep} ticks
 * and a tick is {@code SpeedSetting.BASE_TICK_MILLIS / multiplier} ms. Every actor type in
 * {@code content/raws/actors} is {@code speedTicksPerStep} 1 or 2, so the whole set of real
 * cadences is 200, 100, 50 and 25 ms — a driven serf or watchman at real time steps every
 * <b>200 ms</b>, which is twelve frames at 60 Hz. That is measured off the shipped raws and the
 * shipped speed table, not guessed; {@code FirstPersonStrideTest} reads both and re-derives it.
 *
 * <p><b>What the first cut did with those twelve frames.</b> The ease ran for 0.62 x 200 ms =
 * 124 ms, seven frames, and ease-out cubic put 58% of the cell behind you in the first two of
 * them and 88% in the first four. The remaining five frames of the cadence the eye did not move
 * at all. Read back to back that is a lurch and a stop, twelve times a second — the opposite of
 * what was asked for.
 *
 * <p><b>What it does now.</b> The ease fills {@link #STEP_ARRIVAL_FRACTION} of the measured
 * cadence — nearly all of it, one frame of settle at 60 Hz — and the curve is
 * {@link #strideEase}, mostly linear with a light smoothstep blended in. Speed over a step
 * therefore runs between 0.65x and 1.18x of the average instead of between 3.5x and zero: a
 * push off the back foot and a settle onto the front one, with the eye always moving. Chained
 * steps are near-constant velocity, because each new commit re-bases the ease from wherever the
 * eye currently is rather than from the cell behind it.
 *
 * <p>The duration is still <em>measured, not assumed</em> — a duration hard-coded for 100 ms
 * ticks turns into a glide the moment someone presses fast-forward, because the next step
 * commits while the last ease is still running. The camera times the interval between committed
 * steps and sizes each ease off the last one, clamped, so it lands before the next step is due
 * at any sim speed without being told what the sim speed is.
 *
 * <p>A vertical band change gets its own, longer ease ({@link #CLIMB_ARRIVAL_FRACTION}): a
 * climb should read as a climb, and an instant band-height translation is the most jarring
 * thing this view can do. And each lateral stride carries a small head dip, phase-locked to
 * the ease itself rather than free-running, so the bob is the walk instead of a wobble laid
 * over it — now that the ease spans the whole cadence, so does the dip.
 *
 * <h2>Yaw is the client's, facing is the sim's</h2>
 *
 * <p>{@link #yaw()} is a continuous, client-side view direction. {@code Actor.facing()} is
 * four-way sim state, written only on a committed step, and on a diagonal stride it takes the
 * x component — so a north-east step reports EAST. Reading the camera's look direction off
 * that every step would snap it ninety degrees sideways while you walk, which is why the yaw
 * is the client's own number and {@link #snapTo} seeds it from {@code facing()} exactly once.
 * Nothing here ever calls {@code Actor.setFacing} — facing is serialized sim state that the
 * twin-run gate compares byte-for-byte, and a camera that wrote it would show up as a
 * nondeterminism bug rather than as the camera bug it is.
 *
 * <p><b>But the yaw is not frozen either.</b> It is drawn on the tile view as the facing wedge,
 * and a wedge that never moves while the body walks under it is worse than no wedge: it
 * promises the first-person frame will open one way and opens it another. So
 * {@link #followCell(int, int, int, boolean)} takes {@code adoptTravelYaw}, and while the tile
 * view is the one on screen the yaw swings — continuously, at
 * {@link #TRAVEL_TURN_DEGREES_PER_SECOND}, shortest way round — to the direction of each
 * committed step. That direction is taken from the cells themselves, not from
 * {@code facing()}, so a north-east stride points north-east.
 *
 * <p>In first person {@code adoptTravelYaw} is false and the yaw is the player's alone,
 * because there the movement keys are already relative to it: adopting the travel direction
 * would quantise the aim to the eight grid directions every time you took a step.
 * {@link #turn} and {@link #setYaw} cancel any swing in flight — an explicit aim always wins,
 * until the next step.
 */
public final class FirstPersonCamera {

    /**
     * Fraction of the measured step interval a lateral ease takes. Nearly all of it: at the
     * 200 ms real-time cadence that is 184 ms of moving and 16 ms — one frame at 60 Hz — of
     * standing on the new cell before the next step commits. Anything much lower is the lurch
     * this replaced; 1.0 exactly would leave no slack for frame jitter to arrive late into.
     */
    public static final float STEP_ARRIVAL_FRACTION = 0.92f;

    /** Fraction of the measured step interval a band change is allowed to take — longer, so
     * a climb reads as climbing, but still short of the next step. */
    public static final float CLIMB_ARRIVAL_FRACTION = 0.92f;

    /**
     * Shortest an ease may be: below this a step is a teleport again.
     *
     * <p>It has to stay under the <b>shortest real cadence</b>, which is 25 ms — fast-forward
     * (4x) with a {@code speedTicksPerStep=1} actor. The previous 45 ms was nearly twice that,
     * so at the fastest speed the ease could never finish before the next step arrived and the
     * eye trailed the body by a permanent third of a cell. That is a slow leak, not a glide —
     * each re-based ease covers a little more ground so the lag settles rather than growing —
     * but it is still the camera quietly disagreeing with the sim about where you are.
     */
    public static final float MIN_EASE_SECONDS = 0.020f;

    /** Longest a lateral ease may be, however slow the sim is running. Sized to clear the
     * slowest real cadence (a {@code speedTicksPerStep=2} actor at real time: 200 ms) with
     * room for frame jitter, so the arrival fraction is what shapes the stride rather than
     * this clamp. */
    public static final float MAX_STEP_EASE_SECONDS = 0.26f;

    /**
     * How much of the stride curve is smoothstep rather than straight line. Zero is a constant
     * slide with a visible corner at each cell boundary; one is a full smoothstep, which comes
     * to a dead stop on every boundary and reads as a hop. This much gives a push-off and a
     * settle while leaving the eye moving at 0.65x average at its slowest.
     */
    public static final float STRIDE_EASE_WEIGHT = 0.35f;

    /** The slowest the eye ever moves during a stride, as a fraction of its own average —
     * {@code 1 - STRIDE_EASE_WEIGHT}, the derivative of {@link #strideEase} at both ends. */
    public static final float MIN_STRIDE_SPEED_FRACTION = 1f - STRIDE_EASE_WEIGHT;

    /** Longest a band-change ease may be. */
    public static final float MAX_CLIMB_EASE_SECONDS = 0.42f;

    /** Ease used for the very first step, before any interval has been measured. */
    private static final float DEFAULT_EASE_SECONDS = 0.16f;

    /** Depth of the head dip at mid-stride, in world units. */
    public static final float BOB_DIP = 0.055f;

    /**
     * How fast the view yaw swings to follow a walk it did not aim. Faster than the player's
     * own {@code FirstPersonInput.TURN_DEGREES_PER_SECOND} because it is chasing a target that
     * moves in 45-degree jumps and should have caught it well inside one step's cadence: at
     * 300 deg/s a 45-degree turn lands in 150 ms, a 90-degree one in 300 ms.
     */
    public static final float TRAVEL_TURN_DEGREES_PER_SECOND = 300f;

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

    /** Where the yaw is swinging to while the walk is aiming it; NaN when nothing is. */
    private float yawTarget = Float.NaN;

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
        this.yawTarget = Float.NaN;
        this.seeded = true;
    }

    /** Whether {@link #snapTo} has ever run — the eye has nowhere to be until it has. */
    public boolean isPlaced() {
        return seeded;
    }

    /**
     * Tells the camera where the sim has actually put the actor, without letting the walk aim
     * the view — what the first-person camera wants, where the yaw is the player's.
     */
    public void followCell(int tileX, int tileY, int bandZ) {
        followCell(tileX, tileY, bandZ, false);
    }

    /**
     * Tells the camera where the sim has actually put the actor. Call every frame with
     * {@code Actor.cell()}; a repeat of the current cell is free. A change starts a fresh
     * ease from wherever the eye currently is, so chained steps read as continuous walking
     * rather than as a queue of hops.
     *
     * @param adoptTravelYaw whether this step may aim the view yaw along the direction
     *                       actually travelled — true while the tile view (and its facing
     *                       wedge) is what is on screen, false in first person
     */
    public void followCell(int tileX, int tileY, int bandZ, boolean adoptTravelYaw) {
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
        int travelX = tileX - targetTileX;
        int travelY = tileY - targetTileY;
        if (adoptTravelYaw && (travelX != 0 || travelY != 0)) {
            // From the cells, not from Actor.facing(): facing takes the x component of a
            // diagonal, so a north-east stride would report EAST and the wedge would lie.
            yawTarget = normalize((float) Math.atan2(travelY, travelX));
        }
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

    /** Advances the ease, the head bob and any travel-yaw swing by a frame. */
    public void advance(float deltaSeconds) {
        if (!seeded || deltaSeconds <= 0f) {
            return;
        }
        sinceLastStep += deltaSeconds;
        advanceTravelYaw(deltaSeconds);
        if (easeDuration <= 0f) {
            x = toX;
            y = toY;
            feetHeight = toHeight;
            return;
        }
        easeElapsed = Math.min(easeDuration, easeElapsed + deltaSeconds);
        float t = easeElapsed / easeDuration;
        float e = strideEase(t);
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

    /**
     * The stride curve: a straight line with {@link #STRIDE_EASE_WEIGHT} of smoothstep blended
     * into it.
     *
     * <p>Neither pure curve is a walk. Pure linear has a corner in the velocity at every cell
     * boundary, which the head bob then punctuates. Pure smoothstep has zero velocity at both
     * ends, so a chain of steps stops dead on every boundary — a hop. Ease-out cubic, the
     * previous choice, is worse than either for chained motion: its velocity starts at 3x
     * average and decays to nothing, so the walk is a series of lunges.
     *
     * <p>The blend keeps the eye moving throughout ({@code e'(0) = e'(1) =}
     * {@link #MIN_STRIDE_SPEED_FRACTION}, peaking at {@code 1.175} mid-stride) while still
     * pushing off and settling. Exact at both ends, monotone in between.
     */
    static float strideEase(float t) {
        float smooth = t * t * (3f - 2f * t);
        return t + (smooth - t) * STRIDE_EASE_WEIGHT;
    }

    /**
     * Swings the yaw toward the direction of the last committed step, shortest way round, at
     * {@link #TRAVEL_TURN_DEGREES_PER_SECOND}. A no-op unless a step asked for it, and it
     * finishes exactly on target rather than asymptoting.
     */
    private void advanceTravelYaw(float deltaSeconds) {
        if (Float.isNaN(yawTarget)) {
            return;
        }
        float delta = shortestTurn(yaw, yawTarget);
        float step = (float) Math.toRadians(TRAVEL_TURN_DEGREES_PER_SECOND) * deltaSeconds;
        if (Math.abs(delta) <= step) {
            yaw = yawTarget;
            yawTarget = Float.NaN;
            return;
        }
        yaw = normalize(yaw + Math.signum(delta) * step);
    }

    /**
     * Turns the view. Purely a client-side look direction — never written back to the sim.
     * An explicit turn cancels any travel-yaw swing in flight: aiming beats following.
     */
    public void turn(float radians) {
        yaw = normalize(yaw + radians);
        yawTarget = Float.NaN;
    }

    /** Sets the view direction outright (the mode switch seeding it from a facing). */
    public void setYaw(float radians) {
        yaw = normalize(radians);
        yawTarget = Float.NaN;
    }

    /** The signed shortest angular distance from {@code from} to {@code to}, in {@code
     * (-pi, pi]}. */
    public static float shortestTurn(float from, float to) {
        float delta = normalize(to - from);
        return delta > (float) Math.PI ? delta - TWO_PI : delta;
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

    /** Whether the view yaw is still swinging to catch up with the way the actor walked. */
    public boolean isTurningToTravel() {
        return !Float.isNaN(yawTarget);
    }

    /** Wraps an angle into {@code [0, 2pi)}. */
    public static float normalize(float radians) {
        float r = radians % TWO_PI;
        return r < 0f ? r + TWO_PI : r;
    }
}
