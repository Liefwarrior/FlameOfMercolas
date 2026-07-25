package com.trojia.client.render;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.FishingSpots;
import com.trojia.sim.actor.SkillTrackRegistry;
import com.trojia.sim.world.PackedPos;

import java.util.ArrayList;
import java.util.List;

/**
 * The GL-free half of the fishing-spot overlay (Sprint 6 fishing, Eli's bug 6 made
 * visible): given the Sim team's live {@link FishingSpots} registry and the camera's view,
 * plans which water cells draw a spot marker this frame — the first renderer of a sim-side
 * REGISTRY in world space (the tile lanes had {@code WorldRenderer}, the actors
 * {@code ActorRenderer}; a spot is neither, so it gets its own pass between them:
 * terrain &rarr; water &rarr; SPOTS &rarr; actors).
 *
 * <p><b>Depth-vision aware.</b> The harbor surface lies 1-2 z below the default quayside
 * view plane (z:+10 water under the z:+11 walk plane), so a spot's water cells are usually
 * seen THROUGH empty air: a marker below the view z draws only where the terrain
 * look-down actually resolves that column to the spot's own z' ({@link DepthSight} — same
 * rule as {@code ActorRenderer}'s depth actors), carrying its depth so the GL side applies
 * the shared {@link DepthVision#shade} treatment. A spot on the viewed z itself (the
 * strand casts, or scrubbing down to the water plane) plans at depth 0.
 *
 * <p><b>The visibility split (perception made visible).</b> With no viewer
 * ({@link Actor#NONE}) every live spot plans — the observer's omniscient god-view. With a
 * viewer (the PLAYED soul), only spots that soul PERCEIVES plan — read from the sim's own
 * {@link FishingSpots#visibleTo} (the stable per-(spot, soul) named draw the AI fishers
 * and the play-mode cast gate on). The client never rolls visibility itself: asking is a
 * pure function of {@code (worldSeed, spawnTick, actorId, zone)} and cannot perturb the
 * sim ({@code NameplateText.platesAt}'s viewerId convention, applied to water).
 */
public final class FishingSpotOverlay {

    /**
     * One planned marker: a water cell of a live spot, visible at the camera's view z
     * either directly ({@code depth == 0}) or through the air-depth look-down
     * ({@code depth >= 1}), with the spot's size class for the size-classed treatment.
     */
    public record Marker(int tileX, int tileY, int depth, int sizeClass) {
    }

    private FishingSpotOverlay() {
    }

    /**
     * Plans this frame's markers: every water cell of every live (and, for a real viewer,
     * PERCEIVED) spot inside the camera bounds whose cell is actually visible at
     * {@code viewZ} — its own plane, or resolved by the look-down. Ascending slot then
     * ascending water-cell order — deterministic every frame.
     *
     * @param spots     the live sim-side registry
     * @param viewZ     the camera's viewed z-level
     * @param depth     the look-down column resolver ({@code ActorRenderer}'s own)
     * @param viewerId  {@link Actor#NONE} for the omniscient observer view; the PLAYED
     *                  soul's id in Play mode (only sim-confirmed-perceived spots plan)
     * @param worldSeed the boot world seed {@link FishingSpots#visibleTo} keys on
     * @param tracks    the skill table the perception threshold reads
     */
    public static List<Marker> plan(FishingSpots spots, int viewZ,
            int minX, int maxX, int minY, int maxY, DepthSight depth,
            int viewerId, long worldSeed, SkillTrackRegistry tracks) {
        List<Marker> markers = new ArrayList<>();
        for (int slot = 0; slot < spots.slotCapacity(); slot++) {
            if (!spots.isLive(slot)) {
                continue;
            }
            if (viewerId != Actor.NONE
                    && !spots.visibleTo(slot, worldSeed, viewerId, tracks)) {
                continue; // the played soul has not read this water (sim's own gate)
            }
            int zone = spots.zoneAt(slot);
            int sizeClass = spots.sizeClassAt(slot);
            for (int j = 0; j < spots.zones().waterCellCount(zone); j++) {
                int cell = spots.zones().waterCellAt(zone, j);
                int tx = PackedPos.x(cell);
                int ty = PackedPos.y(cell);
                if (tx < minX || tx > maxX || ty < minY || ty > maxY) {
                    continue;
                }
                int cellZ = PackedPos.z(cell);
                if (cellZ == viewZ) {
                    markers.add(new Marker(tx, ty, 0, sizeClass));
                } else if (cellZ < viewZ && depth.visibleBelowZ(viewZ, tx, ty) == cellZ) {
                    markers.add(new Marker(tx, ty, viewZ - cellZ, sizeClass));
                }
                // A cell ABOVE the view z (or an occluded column) draws nothing — the
                // exact rule the terrain look-down and depth actors already obey.
            }
        }
        return markers;
    }
}
