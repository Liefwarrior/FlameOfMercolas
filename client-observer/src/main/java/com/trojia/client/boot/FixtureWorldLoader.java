package com.trojia.client.boot;

import com.trojia.sim.material.MaterialRawsLoader;
import com.trojia.sim.material.MaterialRegistry;
import com.trojia.sim.material.RawsBundle;
import com.trojia.sim.world.Coords;
import com.trojia.sim.world.TickableWorld;
import com.trojia.sim.world.io.TrojSav;
import com.trojia.sim.world.io.WorldLoader;

import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.file.Path;

/**
 * Boots the M1 tavern fixture: loads the repo's material raws ({@code content/raws})
 * and the importer's baked TROJSAV ({@code content/maps/baked/tavern_fixture.trojsav})
 * through {@link WorldLoader}. GL-free — safe to call before a graphics context exists.
 *
 * <p>Per {@code content/maps/README.md}: "the client never reads [Tiled] files — only
 * the importer's baked TROJSAV output." This class is the client side of that contract;
 * it never touches {@code tools}' {@code TmxReader}/{@code TiledWorldImporter} — those
 * only run at bake time, in {@code TavernFixtureBakeTest} (test-only dependency, see
 * {@code build.gradle.kts}). Run that test to regenerate the baked file after an
 * authoring change to {@code tavern_fixture.tmx} or a material raws change.
 */
public final class FixtureWorldLoader {

    /** File name of the baked tavern world under {@code content/maps/baked/}. */
    public static final String TAVERN_FILE = "tavern_fixture.trojsav";

    /** File name of the baked compound-block world under {@code content/maps/baked/}. */
    public static final String COMPOUND_FILE = "compound_block.trojsav";

    /**
     * World z of the tavern fixture's authored {@code z:+0} street level:
     * {@code Coords.CHUNK_SIZE_Z + (0 - minZ)} per {@code TiledWorldImporter}'s
     * placement rule, with the fixture's authored z-range {@code -1..+1}
     * (content/maps/README.md) giving {@code minZ = -1}. Hardcoded to this one fixture —
     * a future multi-map loader should carry the street-level z in save metadata
     * instead of a client-side constant (flagged for later milestones).
     */
    public static final int TAVERN_STREET_LEVEL_Z = Coords.CHUNK_SIZE_Z + 1;

    /**
     * World z of the compound-block fixture's authored {@code z:+0} ground level.
     * Same placement rule ({@code Coords.CHUNK_SIZE_Z + (0 - minZ)}), but this map's
     * authored z-range is {@code +0..+2} (content/maps/README.md), so {@code minZ = 0}
     * and the ground level lands at {@code Coords.CHUNK_SIZE_Z} exactly (the upper floor
     * is {@code +1}, the roof slum {@code +2}). Same hardcoded-per-fixture caveat as
     * {@link #TAVERN_STREET_LEVEL_Z}.
     */
    public static final int COMPOUND_GROUND_LEVEL_Z = Coords.CHUNK_SIZE_Z;

    private FixtureWorldLoader() {
    }

    /**
     * The loaded world, the material registry it was baked against, and the world seed
     * its TROJSAV header carries (the only persisted RNG state — see {@code EngineConfig}
     * javadoc — needed to wrap this world in a {@code SimulationEngine}).
     */
    public record Loaded(TickableWorld world, MaterialRegistry materials, long worldSeed) {
    }

    /**
     * Loads the tavern fixture world.
     *
     * @return the world and the registry its MATERIAL-lane raw ids resolve against
     * @throws UncheckedIOException  if the raws or the baked file cannot be read
     * @throws IllegalStateException if the baked file's raws fingerprint does not match
     *                                the currently loaded raws — rebake via
     *                                {@code TavernFixtureBakeTest}
     */
    public static Loaded loadTavern() {
        return load(TAVERN_FILE);
    }

    /**
     * Loads the compound-block fixture world (the wealth-stratified Trojian
     * Compound population stage — content/maps/README.md). Same contract as
     * {@link #loadTavern()}: raws-fingerprint guarded, rebake via
     * {@code CompoundBlockBakeTest} on a mismatch.
     *
     * @return the world and the registry its MATERIAL-lane raw ids resolve against
     */
    public static Loaded loadCompoundBlock() {
        return load(COMPOUND_FILE);
    }

    private static Loaded load(String bakedFileName) {
        try {
            RawsBundle raws = MaterialRawsLoader.load(RepoPaths.locate("content", "raws"));
            MaterialRegistry materials = raws.materials();

            Path bakedFile = RepoPaths.locate("content", "maps", "baked", bakedFileName);
            TrojSav save = TrojSav.read(bakedFile);
            long fingerprint = materials.fingerprint();
            if (save.header().rawsFingerprint() != fingerprint) {
                throw new IllegalStateException(
                        bakedFileName + " was baked against a different raws fingerprint ("
                                + save.header().rawsFingerprint() + " != " + fingerprint
                                + ") -- rerun the matching bake test to rebake it");
            }
            TickableWorld world = new WorldLoader().load(save);
            return new Loaded(world, materials, save.header().worldSeed());
        } catch (IOException e) {
            throw new UncheckedIOException("failed to load the fixture world " + bakedFileName, e);
        }
    }
}
