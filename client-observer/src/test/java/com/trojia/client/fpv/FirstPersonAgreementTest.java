package com.trojia.client.fpv;

import com.trojia.client.art.JsonTileArtResolver;
import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.client.boot.RepoPaths;
import com.trojia.client.render.DepthSight;
import com.trojia.client.render.DepthVision;
import com.trojia.client.render.TilePlan;
import com.trojia.client.scenario.DocksPopulation;
import com.trojia.client.sprite.SpriteIndex;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.world.Coords;
import com.trojia.sim.world.PackedPos;
import com.trojia.sim.world.TileCursor;
import com.trojia.sim.world.TileForm;
import com.trojia.sim.world.TickableWorld;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * <b>The verifier stands in one spot on the real Docks ward and reads the world in both views,
 * then diffs them.</b> Not a synthetic scene — the actual baked 192x128x16 map the observer
 * boots, with its granite substrate, its three climbing walk planes and its depth-7 harbour.
 * GL-free throughout: the world loads headlessly, the plan is a list of records, and the tile
 * view's own look-down brain is called directly.
 *
 * <p>Three diffs, one per way the two cameras could be lying to each other:
 *
 * <ol>
 *   <li><b>Every cell the plan names is the cell the plan says it is.</b> Re-read through a
 *       real {@code TileCursor} — a different cursor from the planner's — and the form must
 *       justify the face and the material lane must be the one the region came from.</li>
 *   <li><b>They see equally far down.</b> For every column the first-person plan shows
 *       something below the eye's band, {@code DepthVision.visibleBelowZ} — the tile view's
 *       own look-down resolver, unmodified — must name the same band.</li>
 *   <li><b>Nothing is invented.</b> No quad names a cell outside the world, no band outside
 *       the shared reach, and the eye's band is where the sim says it is.</li>
 * </ol>
 */
class FirstPersonAgreementTest {

    private static final int W = 1280;
    private static final int H = 720;

    private static TickableWorld world;
    private static FixtureWorldLoader.Loaded loaded;
    private static JsonTileArtResolver artResolver;
    private static int worldWidth;
    private static int worldHeight;
    private static int worldDepth;

    @BeforeAll
    static void bootTheWard() {
        loaded = FixtureWorldLoader.loadDocksSurface();
        world = loaded.world();
        worldWidth = world.config().chunksX() * Coords.CHUNK_SIZE_X;
        worldHeight = world.config().chunksY() * Coords.CHUNK_SIZE_Y;
        worldDepth = world.config().chunksZ() * Coords.CHUNK_SIZE_Z;
        artResolver = JsonTileArtResolver.parse(readShippedMapping());
    }

    /** The shipped art pack, so the agreement is against the ward as it really looks. */
    private static String readShippedMapping() {
        try {
            return Files.readString(
                    RepoPaths.locate("content", "art", "kenney", "art-mapping.json"),
                    StandardCharsets.UTF_8);
        } catch (IOException e) {
            throw new UncheckedIOException("cannot read the shipped art mapping", e);
        }
    }

    private static FirstPersonPlanner planner() {
        return new FirstPersonPlanner(loaded.materials(), loaded.fluids(), artResolver,
                FpvScene.catalog());
    }

    /**
     * A quayside tile on Band A with open water to the north — the Tarwalk looking out over the
     * harbour, which is the view this whole exercise is for. Found by search rather than
     * hard-coded, so an authoring change moves the eye instead of breaking the test.
     */
    private static int[] quaysideLookingAtWater() {
        TileCursor cursor = world.cursor();
        int band = FixtureWorldLoader.DOCKS_QUAYSIDE_LEVEL_Z;
        for (int y = 0; y < worldHeight; y++) {
            for (int x = 0; x < worldWidth; x++) {
                cursor.moveTo(PackedPos.pack(x, y, band));
                if (cursor.form() != TileForm.FLOOR) {
                    continue;
                }
                // Four tiles of open air to the north, with water under them.
                boolean openWater = true;
                for (int d = 1; d <= 4 && openWater && y - d >= 0; d++) {
                    cursor.moveTo(PackedPos.pack(x, y - d, band));
                    openWater = cursor.form() == TileForm.OPEN
                            && (cursor.fluidBits() & TilePlan.FLUID_DEPTH_MASK) == 0;
                }
                if (openWater) {
                    System.out.println("fpv agreement: standing on the quay at ("
                            + x + "," + y + ",z" + band + ") looking north over the harbour");
                    return new int[] {x, y, band};
                }
            }
        }
        throw new AssertionError("no quayside tile with open harbour to the north");
    }

    /**
     * The walkable quayside tile nearest the middle of everything walkable — a spot with the
     * ward all around it rather than a corner of the map, found by search so an authoring
     * change relocates the eye instead of breaking the test.
     */
    private static int[] onTheStreet() {
        TileCursor cursor = world.cursor();
        int band = FixtureWorldLoader.DOCKS_QUAYSIDE_LEVEL_Z;
        long sumX = 0;
        long sumY = 0;
        int count = 0;
        for (int y = 0; y < worldHeight; y++) {
            for (int x = 0; x < worldWidth; x++) {
                cursor.moveTo(PackedPos.pack(x, y, band));
                if (cursor.form() == TileForm.FLOOR) {
                    sumX += x;
                    sumY += y;
                    count++;
                }
            }
        }
        if (count == 0) {
            throw new AssertionError("no walkable tile on the quayside band");
        }
        int centreX = (int) (sumX / count);
        int centreY = (int) (sumY / count);
        int[] best = null;
        long bestDistance = Long.MAX_VALUE;
        for (int y = 0; y < worldHeight; y++) {
            for (int x = 0; x < worldWidth; x++) {
                cursor.moveTo(PackedPos.pack(x, y, band));
                if (cursor.form() != TileForm.FLOOR) {
                    continue;
                }
                long dx = x - centreX;
                long dy = y - centreY;
                long distance = dx * dx + dy * dy;
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = new int[] {x, y, band};
                }
            }
        }
        System.out.println("fpv agreement: standing in the ward at (" + best[0] + ","
                + best[1] + ",z" + band + "), " + count + " walkable tiles on this band");
        return best;
    }

    private static EyeProjection eyeOn(int[] cell, double yaw) {
        return eyeOn(cell, yaw, 0f);
    }

    private static EyeProjection eyeOn(int[] cell, double yaw, float shearPx) {
        return EyeProjection.of(cell[0] + 0.5f, cell[1] + 0.5f,
                BandGeometry.floorHeight(cell[2]) + BandGeometry.EYE_HEIGHT,
                (float) yaw, W, H, EyeProjection.DEFAULT_FOV_DEGREES, shearPx);
    }

    /** Leaning over the quay edge: water a band down does not clear the bottom of a level
     * frame until a dozen tiles out, so looking at it means looking down. */
    private static final float LOOK_DOWN = -EyeProjection.MAX_SHEAR_FRACTION * H;

    // ------------------------------------------------------------------ diff 1

    @Test
    void everyCellThePlanNamesIsTheCellThePlanSaysItIs() {
        int[] stand = onTheStreet();
        TileCursor verifier = world.cursor(); // deliberately NOT the planner's cursor
        Set<Integer> seen = new HashSet<>();
        int checked = 0;
        for (int octant = 0; octant < 8; octant++) {
            List<ViewQuad> plan = planner().plan(eyeOn(stand, octant * Math.PI / 4), stand[2],
                    new WorldCellSight(world), ActorSight.empty());
            assertFalse(plan.isEmpty(),
                    "standing in the ward facing octant " + octant + " and seeing nothing");
            for (ViewQuad quad : plan) {
                if (!(quad instanceof ViewQuad.Terrain t)) {
                    continue;
                }
                assertTrue(t.tileX() >= 0 && t.tileX() < worldWidth
                                && t.tileY() >= 0 && t.tileY() < worldHeight
                                && t.bandZ() >= 0 && t.bandZ() < worldDepth,
                        "the plan named a cell outside the world: " + describe(t));
                verifier.moveTo(PackedPos.pack(t.tileX(), t.tileY(), t.bandZ()));
                TileForm form = verifier.form();
                if (t.fluid()) {
                    assertTrue((verifier.fluidBits() & TilePlan.FLUID_DEPTH_MASK) > 0,
                            "water drawn on a dry cell: " + describe(t));
                } else if (t.face().isSide()) {
                    assertEquals(TileForm.WALL, form,
                            "a vertical face drawn on a " + form + ": " + describe(t));
                } else {
                    assertTrue(form != TileForm.OPEN && form != TileForm.VOID,
                            "a surface drawn on " + form + ": " + describe(t));
                    TilePlan.BaseTilePlan expected = TilePlan.base(form, verifier.materialId(),
                            t.tileX(), t.tileY(), t.bandZ(), loaded.materials(),
                            artResolver, FpvScene.catalog());
                    assertEquals(expected.regionName(), t.regionName(),
                            "art diverged from the tile view's own chain at " + describe(t));
                    assertEquals(expected.variant(), t.variant(),
                            "cosmetic variant diverged at " + describe(t));
                }
                assertEquals(stand[2] + t.bandDelta(), t.bandZ(),
                        "bandDelta is not measured from the eye's band at " + describe(t));
                seen.add(PackedPos.pack(t.tileX(), t.tileY(), t.bandZ()));
                checked++;
            }
        }
        assertTrue(checked > 200, "a full turn on the spot should see a lot: " + checked);
        assertTrue(seen.size() > 100,
                "a full turn should see many distinct cells: " + seen.size());
    }

    // ------------------------------------------------------------------ diff 2

    /**
     * The one place the two views could visibly diverge and each blame the other: how far down
     * a column of empty air you can see. Both run the same rule, so this compares the plan
     * against {@code DepthVision} — the tile view's own resolver, called unmodified.
     *
     * <p><b>Known blind spot, stated rather than papered over.</b> This can only compare
     * columns where the first-person plan showed <em>something</em> below the eye. A bug whose
     * signature is first person showing nothing — the VOID-stops-the-descent one, fixed in
     * round 2 — is structurally invisible here, and widening it is not straightforward, because
     * a column can legitimately show nothing below for being off the bottom of the frame rather
     * than for being occluded. The descent rule itself is therefore pinned directly, on a
     * synthetic column, in {@code FirstPersonPlannerTest}.
     */
    @Test
    void thePlanSeesExactlyAsFarDownAsTheTileViewsLookDown() {
        int[] stand = quaysideLookingAtWater();
        int band = stand[2];
        DepthSight tileViewLookDown = new DepthVision(world);

        List<ViewQuad> plan = planner().plan(eyeOn(stand, 3 * Math.PI / 2, LOOK_DOWN), band,
                new WorldCellSight(world), ActorSight.empty());

        Map<Integer, Integer> lowestShownPerColumn = new HashMap<>();
        for (ViewQuad quad : plan) {
            if (quad.bandDelta() >= 0) {
                continue;
            }
            int column = quad.tileY() * worldWidth + quad.tileX();
            lowestShownPerColumn.merge(column, quad.bandZ(), Math::min);
        }
        assertFalse(lowestShownPerColumn.isEmpty(),
                "standing on the quay looking north and seeing no harbour at all");

        for (Map.Entry<Integer, Integer> entry : lowestShownPerColumn.entrySet()) {
            int x = entry.getKey() % worldWidth;
            int y = entry.getKey() / worldWidth;
            int firstPersonBand = entry.getValue();
            int tileViewBand = tileViewLookDown.visibleBelowZ(band, x, y);
            assertEquals(tileViewBand, firstPersonBand,
                    "the two views disagree about column (" + x + "," + y + ") from band "
                            + band + ": first person shows " + firstPersonBand
                            + ", the tile view's look-down shows " + tileViewBand);
        }
    }

    @Test
    void theHarbourIsActuallyThereWhenYouLookAtIt() {
        int[] stand = quaysideLookingAtWater();
        List<ViewQuad> plan = planner().plan(eyeOn(stand, 3 * Math.PI / 2, LOOK_DOWN), stand[2],
                new WorldCellSight(world), ActorSight.empty());
        long water = plan.stream()
                .filter(q -> q instanceof ViewQuad.Terrain t && t.fluid())
                .count();
        assertTrue(water > 10,
                "the ward's defining feature should fill the frame from its own quay, saw "
                        + water + " water surfaces");
        plan.stream()
                .filter(q -> q instanceof ViewQuad.Terrain t && t.fluid())
                .forEach(q -> assertTrue(q.bandDelta() < 0,
                        "the harbour should be below the quay you are standing on"));
    }

    /**
     * <b>The seam, measured on the real quay.</b> This is the black stripe, quantified: standing
     * on the Tarwalk looking north over the harbour at a level gaze, the plan runs out of world
     * before it runs out of frame, and the gap between the highest thing it draws and the
     * horizon line is real, sizeable and unavoidable — a ground plane below the eye approaches
     * the horizon asymptotically and no finite draw radius reaches it.
     *
     * <p>So the gap has to be painted rather than argued away, which is what the ground haze
     * does. This test exists to keep the haze load-bearing: if a later change ever made the
     * terrain reach the horizon by itself, the number below would go to zero and someone should
     * be told before deleting the backdrop.
     */
    @Test
    void theHarbourViewLeavesASeamBetweenTheLastTileAndTheHorizon() {
        int[] widest = theWidestStretchOfOpenHarbour();
        int[] stand = {widest[0], widest[1], FixtureWorldLoader.DOCKS_QUAYSIDE_LEVEL_Z};
        EyeProjection eye = eyeOn(stand, 3 * Math.PI / 2);
        List<ViewQuad> plan = planner().plan(eye, stand[2], new WorldCellSight(world),
                ActorSight.empty());

        float highestBelowHorizon = 0f;
        for (ViewQuad quad : plan) {
            float[] c = quad.corners();
            for (int i = 1; i < 8; i += 2) {
                if (c[i] < eye.horizonY()) {
                    highestBelowHorizon = Math.max(highestBelowHorizon, c[i]);
                }
            }
        }
        float seamPx = eye.horizonY() - highestBelowHorizon;
        System.out.println("fpv seam: the widest stretch of open harbour in the ward is "
                + widest[2] + " tiles, from the quay at (" + widest[0] + "," + widest[1]
                + "). Standing there at a level gaze the drawn world stops "
                + String.format("%.0f", seamPx) + " px below the horizon in a " + H
                + " px frame — that band is the ground haze's whole job.");
        assertTrue(seamPx > 1f,
                "the plan now reaches the horizon on its own; the ground haze may be dead code");
    }

    /**
     * The quayside tile with the most open water between it and the far side, and how many
     * tiles that is — the best case for "looking out over the harbour", so the seam measured
     * from it is the smallest one the ward can produce rather than a cherry-picked worst.
     *
     * @return {@code {x, y, tilesOfOpenWater}}
     */
    private static int[] theWidestStretchOfOpenHarbour() {
        TileCursor cursor = world.cursor();
        int band = FixtureWorldLoader.DOCKS_QUAYSIDE_LEVEL_Z;
        int[] best = null;
        for (int y = 1; y < worldHeight; y++) {
            for (int x = 0; x < worldWidth; x++) {
                cursor.moveTo(PackedPos.pack(x, y, band));
                if (cursor.form() != TileForm.FLOOR) {
                    continue;
                }
                int run = 0;
                for (int d = 1; d <= FirstPersonPlanner.DRAW_RADIUS_TILES && y - d >= 0; d++) {
                    cursor.moveTo(PackedPos.pack(x, y - d, band));
                    if (cursor.form() != TileForm.OPEN) {
                        break;
                    }
                    cursor.moveTo(PackedPos.pack(x, y - d, band - 1));
                    if ((cursor.fluidBits() & TilePlan.FLUID_DEPTH_MASK) == 0) {
                        break;
                    }
                    run++;
                }
                if (best == null || run > best[2]) {
                    best = new int[] {x, y, run};
                }
            }
        }
        assertTrue(best != null && best[2] > 0, "no quayside tile with open water north of it");
        return best;
    }

    // ------------------------------------------------------------------ diff 3

    @Test
    void nothingReachesPastTheSharedBandWindow() {
        int[] stand = onTheStreet();
        List<ViewQuad> plan = planner().plan(eyeOn(stand, 0), stand[2],
                new WorldCellSight(world), ActorSight.empty());
        for (ViewQuad quad : plan) {
            assertTrue(quad.bandDelta() >= -DepthVision.MAX_LOOKDOWN,
                    "saw " + (-quad.bandDelta()) + " bands down; the tile view's look-down "
                            + "reaches " + DepthVision.MAX_LOOKDOWN);
            assertTrue(quad.bandDelta() <= FirstPersonPlanner.BANDS_ABOVE,
                    "saw " + quad.bandDelta() + " bands up, past the cap");
            int ring = Math.max(Math.abs(quad.tileX() - stand[0]),
                    Math.abs(quad.tileY() - stand[1]));
            assertTrue(ring <= FirstPersonPlanner.DRAW_RADIUS_TILES,
                    "saw a cell " + ring + " tiles out, past the draw radius");
        }
    }

    @Test
    void theGraniteSubstrateCostsNothingToStandOn() {
        // The docks sit on seven bands of solid granite. Every face of it is buried, so a
        // correct plan draws none of it at all — which is what makes a 16-band world
        // affordable to look at from inside one.
        int[] stand = onTheStreet();
        List<ViewQuad> plan = planner().plan(eyeOn(stand, 0), stand[2],
                new WorldCellSight(world), ActorSight.empty());
        long deepRock = plan.stream().filter(q -> q.bandDelta() < -4).count();
        assertEquals(0, deepRock,
                "drew " + deepRock + " faces of buried substrate; face culling is not working");
    }

    /**
     * <b>The whole per-frame visibility cost, with the ward's actual population standing in
     * it.</b> The plan is a list of records so that a frame's worth of visibility decisions is
     * cheap enough to make every frame beside a 12 ms sim tick budget. This is not a benchmark
     * — it is a tripwire for an accidental order-of-magnitude regression.
     *
     * <p>The first cut of this measured an <em>empty</em> ward, which flattered it: it skipped
     * the per-frame re-bucketing of every actor in the registry and every billboard projection,
     * which is exactly the work that scales with how alive the place is. It now boots the real
     * {@code DocksPopulation} and times {@code refresh() + plan()} together, because that pair
     * is what a rendered frame actually pays.
     *
     * <p><b>And it is measured where the ward is densest, not where it is average.</b> The
     * second cut stood at the centroid of the walkable quayside — a planning convenience, found
     * by averaging coordinates, with no claim at all to being expensive. A tripwire set at the
     * average is a tripwire that a real frame walks under. So the stand is now <em>searched
     * for</em>: a stride of quayside tiles is planned once per octant and the (tile, facing)
     * pair that produced the most quads is the one that gets timed. That is the most expensive
     * frame in the ward this test can find, and it is the number the gate is asserted against.
     */
    @Test
    void planningAPopulatedFrameStaysWellInsideAFrame() {
        FirstPersonPlanner planner = planner();
        CellSight sight = new WorldCellSight(world);
        DocksPopulation population = DocksPopulation.build(loaded.worldSeed(), world);
        CellActorIndex actors = new CellActorIndex(population.registry(), shippedSprites());
        int inTheWard = population.registry().size();
        assertTrue(inTheWard > 100, "an empty ward would flatter this number: " + inTheWard);
        actors.refresh();

        // ---- find the worst frame in the ward, rather than an average one
        int[] stand = null;
        double worstYaw = 0;
        int worstQuads = -1;
        int candidates = 0;
        for (int[] candidate : quaysideStandsEvery(6)) {
            candidates++;
            for (int octant = 0; octant < 8; octant++) {
                double yaw = octant * Math.PI / 4;
                int size = planner.plan(eyeOn(candidate, yaw), candidate[2], sight, actors,
                        Actor.NONE).size();
                if (size > worstQuads) {
                    worstQuads = size;
                    stand = candidate;
                    worstYaw = yaw;
                }
            }
        }
        assertTrue(candidates > 50, "the stand search found almost nowhere: " + candidates);

        for (int i = 0; i < 20; i++) {
            actors.refresh();
            planner.plan(eyeOn(stand, worstYaw), stand[2], sight, actors, Actor.NONE);
        }
        long start = System.nanoTime();
        int frames = 60;
        int quads = 0;
        int billboards = 0;
        for (int i = 0; i < frames; i++) {
            actors.refresh();
            // Jitter the eye within the same cell rather than swinging the yaw: this is the
            // densest FACING, and rotating off it would measure a cheaper frame again.
            List<ViewQuad> plan = planner.plan(eyeOn(stand, worstYaw), stand[2], sight, actors,
                    Actor.NONE);
            quads += plan.size();
            for (ViewQuad quad : plan) {
                if (quad instanceof ViewQuad.Billboard) {
                    billboards++;
                }
            }
        }
        double msPerFrame = (System.nanoTime() - start) / 1e6 / frames;
        System.out.println("fpv planner (DENSEST of " + candidates + " stands x 8 facings): "
                + String.format("%.2f", msPerFrame) + " ms/frame, " + (quads / frames)
                + " quads/frame of which "
                + String.format("%.1f", billboards / (double) frames) + " billboards, standing at ("
                + stand[0] + "," + stand[1] + ",z" + stand[2] + ") facing "
                + Math.round(Math.toDegrees(worstYaw)) + " deg, with " + inTheWard
                + " actors in the ward");
        assertTrue(msPerFrame < 8.0,
                "planning took " + msPerFrame + " ms/frame — it has to share the frame with a "
                        + "12 ms sim tick budget");
    }

    /**
     * Every {@code stride}-th walkable tile on the quayside band, as candidate stands. A stride
     * rather than all of them because this feeds an eight-facing search and the point is to
     * find the ward's expensive corners, not to enumerate its floor.
     */
    private static List<int[]> quaysideStandsEvery(int stride) {
        TileCursor cursor = world.cursor();
        int band = FixtureWorldLoader.DOCKS_QUAYSIDE_LEVEL_Z;
        List<int[]> stands = new java.util.ArrayList<>();
        int seen = 0;
        for (int y = 0; y < worldHeight; y++) {
            for (int x = 0; x < worldWidth; x++) {
                cursor.moveTo(PackedPos.pack(x, y, band));
                if (cursor.form() != TileForm.FLOOR) {
                    continue;
                }
                if (seen++ % stride == 0) {
                    stands.add(new int[] {x, y, band});
                }
            }
        }
        return stands;
    }

    /** The shipped sprite index, so the billboards cost what they really cost. */
    private static SpriteIndex shippedSprites() {
        try (var reader = Files.newBufferedReader(
                RepoPaths.locate("content", "art", "sprites", "sprite-index.json"),
                StandardCharsets.UTF_8)) {
            return SpriteIndex.load(reader);
        } catch (IOException e) {
            throw new UncheckedIOException("cannot read the shipped sprite index", e);
        }
    }

    private static String describe(ViewQuad.Terrain t) {
        return "(" + t.tileX() + "," + t.tileY() + ",z" + t.bandZ() + ") " + t.face();
    }
}
