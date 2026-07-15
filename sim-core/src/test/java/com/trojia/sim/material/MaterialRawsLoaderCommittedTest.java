package com.trojia.sim.material;

import com.trojia.sim.fluid.FluidDefinition;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Full load of the committed raws tree ({@code content/raws}): counts,
 * deterministic id assignment, treatment minting, and spot-checked properties
 * against the raw sources (chromatis charge 60000, minted getilia hardness 6,
 * phorys no-wear per BLESSING-QUEUE ruling 9).
 */
final class MaterialRawsLoaderCommittedTest {

    /** All 22 material ids in id order: sorted string keys, first key = 0. */
    private static final List<String> EXPECTED_IDS = List.of(
            "ash", "brick", "brick_facade", "chromatis", "chromatis_melt", "cloth", "dirt",
            "glowstone", "granite", "granite_facade", "ice", "leather", "lightstone",
            "lightstone_shards", "oak", "phorys", "reman_concrete", "reman_facade", "steel",
            "thatch", "trudgeon_wood", "trudgeon_wood@getilia_soak");

    private static RawsBundle bundle;

    @BeforeAll
    static void loadCommittedRaws() {
        bundle = MaterialRawsLoader.load(locateRawsDir());
    }

    @Test
    void loadsTwentyTwoMaterialsOneFluidOneTreatmentOneReaction() {
        assertEquals(22, bundle.materials().size());
        assertEquals(1, bundle.fluids().size());
        assertEquals(1, bundle.treatments().size());
        assertEquals("getilia_soak", bundle.treatments().get(0).key());
        assertEquals(1, bundle.materials().reactions().size());
    }

    @Test
    void idAssignmentIsSortedStringKeyOrder() {
        MaterialRegistry registry = bundle.materials();
        for (int i = 0; i < EXPECTED_IDS.size(); i++) {
            assertEquals(EXPECTED_IDS.get(i), registry.get(i).key(), "id " + i);
            assertEquals(i, registry.id(EXPECTED_IDS.get(i)).raw());
        }
        assertEquals((short) 0, bundle.fluids().id("water").raw());
    }

    @Test
    void chromatisSpotCheck() {
        Material chromatis = bundle.materials().get(bundle.materials().id("chromatis"));
        MaterialFeature.Chargeable chargeable =
                chromatis.feature(MaterialFeature.Chargeable.class);
        assertEquals(60000, chargeable.capacityCu());
        assertEquals(600, chargeable.maxSafeDischargePerTick());
        assertEquals(95, chargeable.saturationPct());
        assertEquals(20, chargeable.saturationHeatDeciKPerTick());
        assertEquals(6000, chargeable.equilibriumDeciK());
        // Fill ramp per BLESSING-QUEUE ruling 5: silver/blue -> pale gold -> gold.
        assertEquals(3, chargeable.colorStops().size());
        assertEquals(new MaterialFeature.Chargeable.ColorStop(60, 0x9FB8D8, 0),
                chargeable.colorStops().get(0));
        assertEquals(new MaterialFeature.Chargeable.ColorStop(95, 0xE3CE7A, 4),
                chargeable.colorStops().get(1));
        assertEquals(new MaterialFeature.Chargeable.ColorStop(100, 0xF5C542, 8),
                chargeable.colorStops().get(2));
        assertEquals(26000, chromatis.meltDeciK());
        assertEquals("chromatis_melt", chromatis.meltsTo());
        assertEquals(7, chromatis.meltYieldUnits());
        assertEquals(MaterialPhase.SOLID, chromatis.phase());
        assertEquals(8, chromatis.hardness());
    }

    @Test
    void hotTablesMatchDefinitions() {
        MaterialRegistry registry = bundle.materials();
        int chromatis = registry.id("chromatis").raw();
        assertEquals(200, registry.conductivityQ8(chromatis));
        assertEquals(96, registry.heatCapacityQ8(chromatis));
        assertEquals((1 << 24) / 96, registry.invCapQ16(chromatis));
        assertEquals(31, registry.opacity(chromatis));
        int oak = registry.id("oak").raw();
        assertEquals(2, registry.flammability(oak));
        assertEquals(5500, registry.ignitionDeciK(oak));
        int granite = registry.id("granite").raw();
        assertEquals(Material.NONE, registry.ignitionDeciK(granite));
    }

    @Test
    void getiliaSoakMintsFireproofHardenedTrudgeon() {
        Material minted = bundle.materials()
                .get(bundle.materials().id("trudgeon_wood@getilia_soak"));
        assertEquals("Getilia-Soaked Trudgeon Wood", minted.displayName());
        assertEquals(6, minted.hardness(), "granite-tier hardness per ruling 8");
        assertEquals(0, minted.flammability());
        assertFalse(minted.flammable());
        assertEquals(Material.NONE, minted.ignitionDeciK(), "null override erased ignitionK");
        assertEquals(0, minted.fuelTicks());
        assertNull(minted.burnsTo());
        // scaleQ8 density 294: floor(900 * 294 / 256) = 1033.
        assertEquals(1033, minted.density());
        assertEquals(List.of("wood", "treated", "fireproof"), minted.tags());
    }

    @Test
    void phorysReactionHasNoWear() {
        ReactionDefinition reaction = bundle.materials().reactions().get(0);
        assertEquals("phorys_hydration", reaction.key());
        assertEquals("phorys", reaction.solidId());
        assertEquals("liquid", reaction.triggerFluidTag());
        assertEquals(240, reaction.expansion());
        // BLESSING-QUEUE ruling 9: absent wear fields = no wear.
        assertFalse(reaction.wears());
        assertEquals(0, reaction.wearPerUnit());
        assertEquals(0, reaction.wearCapacity());
        assertNull(reaction.pulseGasId());
        assertEquals(1680, reaction.pulseMagnitudeCap());
        Material phorys = bundle.materials().get(bundle.materials().id("phorys"));
        assertEquals(new MaterialFeature.ContactReactive("liquid"),
                phorys.feature(MaterialFeature.ContactReactive.class));
    }

    @Test
    void waterAndIceCrossRegistryRoundTrip() {
        FluidDefinition water = bundle.fluids().get(bundle.fluids().id("water"));
        assertEquals(2730, water.freezeDeciK());
        assertEquals("ice", water.freezesTo());
        assertEquals(7, water.freezeMinDepth());
        assertEquals(3730, water.boilDeciK());
        assertNull(water.boilsTo(), "vapor is the reserved steam seam (ruling 3)");
        assertEquals(1, water.evapMaxDepth());
        assertEquals(3330, water.evapMinDeciK());
        assertEquals(2048, water.evapChanceQ16());
        assertTrue(water.tagged("liquid"));
        Material ice = bundle.materials().get(bundle.materials().id("ice"));
        assertEquals(2730, ice.meltDeciK());
        assertEquals("water", ice.meltsTo(), "cross-registry melt target (ruling 3)");
        assertEquals(7, ice.meltYieldUnits(), "freeze/melt round trip conserves to the unit");
    }

    @Test
    void twoLoadsYieldIdenticalIdsAndFingerprints() {
        RawsBundle second = MaterialRawsLoader.load(locateRawsDir());
        assertEquals(bundle.materials().size(), second.materials().size());
        for (int i = 0; i < bundle.materials().size(); i++) {
            assertEquals(bundle.materials().get(i).key(), second.materials().get(i).key());
        }
        assertEquals(bundle.materials().fingerprint(), second.materials().fingerprint());
        assertEquals(bundle.fluids().fingerprint(), second.fluids().fingerprint());
        assertEquals(bundle.fingerprint(), second.fingerprint());
    }

    /**
     * Walks up from the test working directory (the sim-core module dir under
     * Gradle) to the repo root containing {@code content/raws}.
     */
    static Path locateRawsDir() {
        Path dir = Path.of("").toAbsolutePath();
        for (int i = 0; i < 6 && dir != null; i++, dir = dir.getParent()) {
            Path candidate = dir.resolve("content").resolve("raws");
            if (Files.isDirectory(candidate)) {
                return candidate;
            }
        }
        throw new IllegalStateException(
                "content/raws not found above " + Path.of("").toAbsolutePath());
    }
}
