package com.trojia.client.render;

import com.trojia.sim.world.PackedPos;
import com.trojia.sim.world.TileCursor;
import com.trojia.sim.world.World;

import java.util.function.IntPredicate;

/**
 * The world-backed {@link DepthSight}: resolves an empty-air tile column to the z-level the
 * air-depth look-down pass shows there, and owns the shared <b>depth treatment</b> math the
 * actor depth pass applies (Sprint 4 EPIC — depth vision for actors). Everything here
 * delegates to {@link WorldRenderer}'s existing look-down brain
 * ({@code cellDrawsSomething} / {@code findLookdownZ} / {@code depthDim} and its cool-haze
 * constants), so terrain and actors can never disagree about what a column shows or how
 * deep it reads — one curve, two consumers.
 *
 * <p><b>Column resolution.</b> {@link #visibleBelowZ} applies the exact rule the renderer's
 * per-cell loop applies: if the cell at the view z itself draws something (a base tile or
 * pooled fluid), the column is occluded and resolves to {@link DepthSight#NONE}; otherwise
 * the probe walks down up to {@code WorldRenderer.MAX_LOOKDOWN} levels for the first cell
 * that would draw. An actor standing at {@code (x, y, z')} is depth-visible from
 * {@code viewZ} exactly when this resolves to its own {@code z'} — actors stand on
 * FLOOR/RAMP/STAIR forms, which always draw a base tile, so the found terrain cell IS the
 * ground under the actor's feet.
 *
 * <p><b>Depth treatment.</b> {@link #shade} is the per-depth dim + cool-haze factor the
 * terrain look-down multiplies into its draws, extracted verbatim (the renderer now calls
 * it too, so the extraction is pixel-identical by construction): brightness
 * {@code depthDim(d)} with red pulled down by the cool bias, green half as much, blue
 * untouched. Applied ON TOP of the source cell's lit ambient — a lamplit figure below
 * reads warm through the dark, exactly like the lamplit floor it stands on.
 *
 * <p>Presentation-only (never feeds {@code WorldHasher}); pure functions of the world's
 * tile state, deterministic across runs and machines. One flyweight {@link TileCursor} is
 * reused across calls (single render thread, the {@code World.cursor()} convention).
 */
public final class DepthVision implements DepthSight {

    /** How deep the look-down (and so the actor depth pass) can see — the terrain pass's
     * own {@link WorldRenderer#MAX_LOOKDOWN}, re-exported for consumers outside the
     * package. */
    public static final int MAX_LOOKDOWN = WorldRenderer.MAX_LOOKDOWN;

    private final TileCursor cursor;

    public DepthVision(World world) {
        this.cursor = world.cursor();
    }

    @Override
    public int visibleBelowZ(int viewZ, int tileX, int tileY) {
        return resolveThrough(viewZ, zPrime -> {
            cursor.moveTo(PackedPos.pack(tileX, tileY, zPrime));
            return WorldRenderer.cellDrawsSomething(cursor);
        });
    }

    /**
     * The pure core of {@link #visibleBelowZ}, over a synthetic column predicate (the
     * headless test surface, mirroring {@code findLookdownZ}'s own): occluded when the
     * view-z cell itself draws, else the nearest drawing z' within
     * {@code MAX_LOOKDOWN}, else {@link DepthSight#NONE}.
     */
    static int resolveThrough(int viewZ, IntPredicate drawsAt) {
        if (drawsAt.test(viewZ)) {
            return NONE; // the view cell draws its own content — no look-down here
        }
        return WorldRenderer.findLookdownZ(viewZ, WorldRenderer.MAX_LOOKDOWN, drawsAt);
    }

    /**
     * One depth level's full colour treatment: the per-channel multiply a look-down draw
     * (terrain or actor) applies on top of its lit ambient.
     */
    public record Shade(float r, float g, float b) {
    }

    /**
     * The depth shade for a cell (or actor) {@code depth} z-levels below empty air — the
     * exact factor triple the terrain look-down has always used: {@code depthDim(depth)}
     * brightness with the faint blue-ward haze (red pulled down by the full cool bias,
     * green by half, blue by none). {@code WorldRenderer.draw}'s look-down branch calls
     * this same method, so actor and terrain depth treatment are one function.
     */
    public static Shade shade(int depth) {
        float dim = WorldRenderer.depthDim(depth);
        float cool = Math.min(WorldRenderer.COOL_MAX, WorldRenderer.COOL_PER_DEPTH * depth);
        return new Shade(dim * (1f - cool), dim * (1f - 0.5f * cool), dim);
    }

    /**
     * The blur-pyramid level for something {@code depth} z-levels down, given a pyramid of
     * {@code blurLevelCount} levels: {@code clamp(depth - 1, 0, blurLevelCount - 1)} — the
     * same policy as the tile atlas pyramid (depth 1 stays sharp; each level deeper steps
     * one blur level until the pyramid is exhausted; a sharp-only pyramid always picks 0).
     */
    public static int blurLevelFor(int depth, int blurLevelCount) {
        int last = blurLevelCount - 1;
        int level = depth - 1;
        if (level < 0) {
            return 0;
        }
        return level > last ? last : level;
    }
}
