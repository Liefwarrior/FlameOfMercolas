package com.trojia.client.render;

import java.util.ArrayList;
import java.util.List;

/**
 * The GL-free half of the building labels (2026-07-28, Eli: "We also need to label buildings!
 * Let's use NES style black box pop ups with signs"): given the ward's authored
 * {@link PlaceSign}s and the camera's view, plans which doors draw a hanging sign this frame
 * and which single place — if any — gets the pop-up box.
 *
 * <p>Same shape as {@code FishingSpotOverlay}: pure, deterministic, headlessly testable,
 * no draws and no allocation beyond the frame's own plan. Nothing here is sim state; a place
 * name is a way of looking at the ward, not a fact in it.
 *
 * <p><b>Depth rule (identical to the fishing overlay's, so the ward can never disagree with
 * itself).</b> A sign on the viewed z plans at depth 0. A sign BELOW the view plane plans only
 * where the terrain look-down actually resolves that column to the sign's own z'
 * ({@link DepthSight}) — so a Compound two bands down whose roof you are currently looking at
 * resolves to NONE and vanishes, sign and box together, instead of floating over the roof. A
 * sign ABOVE the view plane never plans at all.
 *
 * <p><b>The clutter rule, and it is the whole design.</b> The hanging signs are MARKS, never
 * text: forty of them across the district read as a legend, not a crowd, and they are culled
 * to the camera box. The words are a strict singleton — <b>at most one box exists in any
 * frame</b>, for the one place nearest the viewer's attention. That holds at the widest zoom
 * by construction: there is no zoom at which a second box can appear.
 *
 * <p><b>What "attention" means, and why it differs by mode.</b> In OBSERVER mode it is the
 * tile under the cursor (the ward's existing hover idiom — {@code NameplateRenderer}'s own),
 * with the generous {@link #OBSERVER_APPROACH_TILES} reach: a free camera is asking "what is
 * that?", so pointing anywhere near a building answers. In PLAY mode it is the played actor's
 * own tile with the tight {@link #PLAY_APPROACH_TILES} reach: you must be inside the place or
 * standing at its wall before it speaks, which is the doorway rule the NES boxes this is
 * modelled on always used. Point at open water in either mode and the ward stays silent.
 *
 * <p>Distance is measured to a place's FOOTPRINT, not to its door (see
 * {@link PlaceSign#distanceTo}) — otherwise the middle of the 64-tile Ropewalk would be
 * nameless. Ties break on ascending sign order, which the loader fixes deterministically.
 */
public final class PlaceSignOverlay {

    /**
     * Observer reach, tiles: a cursor this far outside a footprint still names it. Three is
     * the ward's widest ordinary street (the Tarwalk apron), so pointing at a street names the
     * frontage beside it while the open harbor names nothing.
     */
    public static final int OBSERVER_APPROACH_TILES = 3;

    /**
     * Play-mode reach, tiles: 1 is inside-the-footprint or standing on the cell its wall
     * faces — the doorway rule. Walking the Tarwalk does not stream boxes; walking UP TO a
     * door does.
     */
    public static final int PLAY_APPROACH_TILES = 1;

    /**
     * One planned hanging sign: a named place's door cell, visible at the camera's view z
     * either directly ({@code depth == 0}) or through the air-depth look-down
     * ({@code depth >= 1}). {@code named} marks the one sign the frame's box is describing —
     * the renderer lights it, which is how a reader knows which door the words belong to
     * without drawing a tail on the box (there were no tails on a 1988 cartridge).
     */
    public record Marker(int tileX, int tileY, int depth, boolean named) {
    }

    /**
     * The frame's single pop-up: the place's two lines, anchored on the ATTENTION tile (not
     * the door — the door of a long shed is often off screen while you stand in the middle of
     * it), carrying the same depth the sign planned at so the renderer can recede its border
     * exactly like the terrain under it.
     */
    public record Box(String place, String what, int anchorTileX, int anchorTileY, int depth) {
    }

    /**
     * A frame's whole label plan: the hanging signs to draw, and the one box — or
     * {@code null} when the viewer's attention is not near any named place, which is most of
     * the harbor, the fields, and the open water.
     */
    public record Plan(List<Marker> markers, Box box) {

        /** The empty plan: nothing drawn, nothing said. */
        public static final Plan NOTHING = new Plan(List.of(), null);
    }

    private PlaceSignOverlay() {
    }

    /**
     * Plans this frame's signs and box.
     *
     * @param signs          the ward's authored places, in the loader's deterministic order
     * @param viewZ          the camera's viewed z-level
     * @param minX           camera box, inclusive west edge in world tiles
     * @param maxX           camera box, inclusive east edge
     * @param minY           camera box, inclusive north edge
     * @param maxY           camera box, inclusive south edge
     * @param depth          the look-down column resolver ({@code ActorRenderer}'s own)
     * @param attentionX     the viewer's attention tile x (cursor, or the played actor)
     * @param attentionY     the viewer's attention tile y
     * @param attentionLive  {@code false} when there is no attention point at all (cursor off
     *                       the world): signs still draw, no box is planned
     * @param approachTiles  {@link #OBSERVER_APPROACH_TILES} or {@link #PLAY_APPROACH_TILES}
     */
    public static Plan plan(List<PlaceSign> signs, int viewZ,
            int minX, int maxX, int minY, int maxY, DepthSight depth,
            int attentionX, int attentionY, boolean attentionLive, int approachTiles) {
        if (signs.isEmpty()) {
            return Plan.NOTHING;
        }
        List<Marker> markers = new ArrayList<>();
        // The frame's winner, chosen over EVERY visible sign (not just on-screen ones): you
        // can stand deep inside the Ropewalk with its west door far off the camera box.
        PlaceSign chosen = null;
        int chosenDistance = Integer.MAX_VALUE;
        int chosenDepth = 0;
        int chosenMarker = -1;

        for (PlaceSign sign : signs) {
            int signDepth = visibleDepth(sign, viewZ, depth);
            if (signDepth < 0) {
                continue;   // above the plane, or an occluded column: the place is not shown
            }
            int markerIndex = -1;
            if (sign.doorX() >= minX && sign.doorX() <= maxX
                    && sign.doorY() >= minY && sign.doorY() <= maxY) {
                markerIndex = markers.size();
                markers.add(new Marker(sign.doorX(), sign.doorY(), signDepth, false));
            }
            if (!attentionLive) {
                continue;
            }
            int distance = sign.distanceTo(attentionX, attentionY);
            if (distance <= approachTiles && distance < chosenDistance) {
                chosen = sign;
                chosenDistance = distance;
                chosenDepth = signDepth;
                chosenMarker = markerIndex;
            }
        }
        if (chosen == null) {
            return new Plan(List.copyOf(markers), null);
        }
        if (chosenMarker >= 0) {
            Marker plain = markers.get(chosenMarker);
            markers.set(chosenMarker,
                    new Marker(plain.tileX(), plain.tileY(), plain.depth(), true));
        }
        return new Plan(List.copyOf(markers),
                new Box(chosen.place(), chosen.what(), attentionX, attentionY, chosenDepth));
    }

    /**
     * How many z-levels below the view plane this place reads at, or {@code -1} when it is
     * not shown at all — the exact rule {@code FishingSpotOverlay} applies to a water cell,
     * measured at the sign's own door column.
     */
    private static int visibleDepth(PlaceSign sign, int viewZ, DepthSight depth) {
        if (sign.z() == viewZ) {
            return 0;
        }
        if (sign.z() > viewZ) {
            return -1;   // a sign on a plane above the one you are reading never draws
        }
        return depth.visibleBelowZ(viewZ, sign.doorX(), sign.doorY()) == sign.z()
                ? viewZ - sign.z() : -1;
    }
}
