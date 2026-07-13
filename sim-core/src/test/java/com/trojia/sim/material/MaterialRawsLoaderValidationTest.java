package com.trojia.sim.material;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * One failing fixture per ARCHITECTURE.md §10 loader validation: every rule
 * must fail fast with a {@link RawsValidationException} naming the offending
 * file and field. Positive fixtures pin the blessed exemptions (provenance and
 * notes ignored, absent reaction wear = no wear per BLESSING-QUEUE ruling 9,
 * treatment mint semantics per ruling 2).
 */
final class MaterialRawsLoaderValidationTest {

    @TempDir
    Path root;

    // ------------------------------------------------------------- structure

    @Test
    void malformedJsonNamesTheFile() throws IOException {
        write("materials/bad.json", "{ \"id\": ");
        expectFailure("materials/bad.json", RawsValidationException.NO_FIELD);
    }

    @Test
    void topLevelMustBeAnObject() throws IOException {
        write("materials/list.json", "[1,2]");
        expectFailure("materials/list.json", RawsValidationException.NO_FIELD);
    }

    @Test
    void duplicateMaterialIdNamesBothFiles() throws IOException {
        write("materials/a.json", material("twin"));
        write("materials/b.json", material("twin"));
        RawsValidationException e = expectFailure("materials/b.json", "id");
        assertTrue(e.getMessage().contains("materials/a.json"), e.getMessage());
    }

    @Test
    void unknownTopLevelFieldRejected() throws IOException {
        write("materials/m.json", material("m", "wat", "1"));
        expectFailure("materials/m.json", "wat");
    }

    @Test
    void provenanceAndNotesAreIgnored() throws IOException {
        write("materials/m.json",
                material("m", "provenance", "\"docs\"", "notes", "\"docs\""));
        assertEquals(1, MaterialRawsLoader.load(root).materials().size());
    }

    // ------------------------------------------------------- material ranges

    @Test
    void missingIdRejected() throws IOException {
        write("materials/m.json", material("m", "id", null));
        expectFailure("materials/m.json", "id");
    }

    @Test
    void unknownPhaseRejected() throws IOException {
        write("materials/m.json", material("m", "phase", "\"PLASMA\""));
        expectFailure("materials/m.json", "phase");
    }

    @Test
    void densityMustFitSixteenBits() throws IOException {
        write("materials/m.json", material("m", "density", "70000"));
        expectFailure("materials/m.json", "density");
    }

    @Test
    void hardnessRangeEnforced() throws IOException {
        write("materials/m.json", material("m", "hardness", "300"));
        expectFailure("materials/m.json", "hardness");
    }

    @Test
    void flammabilitySeverityCapsAtThree() throws IOException {
        write("materials/m.json", material("m", "flammability", "4"));
        expectFailure("materials/m.json", "flammability");
    }

    @Test
    void conductivityMustNotExceed256() throws IOException {
        write("materials/m.json", material("m", "conductivityQ8", "257"));
        expectFailure("materials/m.json", "conductivityQ8");
    }

    @Test
    void heatCapacityStabilityFloorEnforced() throws IOException {
        write("materials/m.json", material("m", "heatCapacityQ8", "11"));
        expectFailure("materials/m.json", "heatCapacityQ8");
    }

    @Test
    void temperatureDeciKMustFitTheLane() throws IOException {
        write("materials/m.json", material("m",
                "meltK", "7000", "meltsTo", "\"m\"", "meltYieldUnits", "1"));
        expectFailure("materials/m.json", "meltK");
    }

    @Test
    void opacityRangeEnforced() throws IOException {
        write("materials/m.json", material("m", "light", "{\"opacity\":32}"));
        expectFailure("materials/m.json", "light.opacity");
    }

    // ------------------------------------------------------- FLAMMABLE triple

    @Test
    void flammableRequiresIgnition() throws IOException {
        write("materials/m.json", material("m",
                "flammability", "2", "fuelTicks", "100", "burnsTo", "\"m2\""));
        expectFailure("materials/m.json", "ignitionK");
    }

    @Test
    void flammableFuelTicksCappedAt4095() throws IOException {
        write("materials/m.json", material("m", "flammability", "2",
                "ignitionK", "500", "fuelTicks", "5000", "burnsTo", "\"m2\""));
        expectFailure("materials/m.json", "fuelTicks");
    }

    @Test
    void flammableRequiresBurnsTo() throws IOException {
        write("materials/m.json", material("m",
                "flammability", "1", "ignitionK", "500", "fuelTicks", "100"));
        expectFailure("materials/m.json", "burnsTo");
    }

    @Test
    void inertMaterialMustNotDeclareIgnition() throws IOException {
        write("materials/m.json", material("m", "ignitionK", "500"));
        expectFailure("materials/m.json", "ignitionK");
    }

    // ------------------------------------------------------------ melt / boil

    @Test
    void meltRequiresMeltsTo() throws IOException {
        write("materials/m.json", material("m", "meltK", "1000", "meltYieldUnits", "1"));
        expectFailure("materials/m.json", "meltsTo");
    }

    @Test
    void meltRequiresYield() throws IOException {
        write("materials/m.json", material("m", "meltK", "1000", "meltsTo", "\"m\""));
        expectFailure("materials/m.json", "meltYieldUnits");
    }

    @Test
    void boilsToRequiresBoilK() throws IOException {
        write("materials/m.json", material("m", "boilsTo", "\"m\""));
        expectFailure("materials/m.json", "boilsTo");
    }

    @Test
    void liquidTaggedMaterialRequiresBoilsTo() throws IOException {
        write("materials/m.json", material("m", "tags", "[\"liquid\"]"));
        expectFailure("materials/m.json", "boilsTo");
    }

    // --------------------------------------------------------------- features

    @Test
    void unknownFeatureKindRejected() throws IOException {
        write("materials/m.json", material("m", "features", "{\"sparkly\":{}}"));
        expectFailure("materials/m.json", "features.sparkly");
    }

    @Test
    void chargeableCapacityMustFitSixteenBits() throws IOException {
        write("materials/m.json", material("m", "features",
                chargeable("70000", "600", stops("{\"uptoPct\":100,\"tint\":\"#FFFFFF\",\"lightLevel\":0}"))));
        expectFailure("materials/m.json", "features.chargeable.capacityCu");
    }

    @Test
    void spikeMustFitSixteenBits() throws IOException {
        write("materials/m.json", material("m", "features",
                "{\"shatterOnSpike\":{\"spikeCuPerTick\":70000,\"shattersTo\":\"m\",\"radiusChebyshev\":2}}"));
        expectFailure("materials/m.json", "features.shatterOnSpike.spikeCuPerTick");
    }

    @Test
    void colorStopsMustBeStrictlyMonotone() throws IOException {
        write("materials/m.json", material("m", "features", chargeable("100", "10", stops(
                "{\"uptoPct\":60,\"tint\":\"#FFFFFF\",\"lightLevel\":0}",
                "{\"uptoPct\":50,\"tint\":\"#FFFFFF\",\"lightLevel\":0}",
                "{\"uptoPct\":100,\"tint\":\"#FFFFFF\",\"lightLevel\":0}"))));
        expectFailure("materials/m.json", "features.chargeable.colorStops[1].uptoPct");
    }

    @Test
    void lastColorStopMustBeAt100() throws IOException {
        write("materials/m.json", material("m", "features", chargeable("100", "10", stops(
                "{\"uptoPct\":60,\"tint\":\"#FFFFFF\",\"lightLevel\":0}",
                "{\"uptoPct\":95,\"tint\":\"#FFFFFF\",\"lightLevel\":0}"))));
        expectFailure("materials/m.json", "features.chargeable.colorStops");
    }

    @Test
    void tintMustBeHashRrGgBb() throws IOException {
        write("materials/m.json", material("m", "features",
                "{\"emissive\":{\"lightLevel\":7,\"tint\":\"#GGGGGG\"}}"));
        expectFailure("materials/m.json", "features.emissive.tint");
    }

    // ------------------------------------------------------------- treatments

    @Test
    void treatmentTargetMustExist() throws IOException {
        write("treatments/t.json", treatment("t", "ghost", "ghost@t"));
        expectFailure("treatments/t.json", "target");
    }

    @Test
    void derivedIdMustNotCollideWithMaterial() throws IOException {
        write("materials/a.json", material("a"));
        write("materials/b.json", material("b"));
        write("treatments/t.json", treatment("t", "a", "b"));
        expectFailure("treatments/t.json", "derivedId");
    }

    @Test
    void scaleOfMissingMemberNamesTreatmentFile() throws IOException {
        write("materials/a.json", material("a"));
        write("treatments/t.json", treatment("t", "a", "a@t",
                "scaleQ8", "{\"nope\":128}"));
        RawsValidationException e =
                expectFailure("treatments/t.json", RawsValidationException.NO_FIELD);
        assertTrue(e.getMessage().contains("scaleQ8.nope"), e.getMessage());
    }

    @Test
    void mintAppliesOverridesScaleAndAddTags() throws IOException {
        write("materials/a.json", material("a"));
        write("treatments/t.json", treatment("t", "a", "a@t",
                "overrides", "{\"valueCp\":999}",
                "scaleQ8", "{\"density\":128}",
                "addTags", "[\"cured\"]"));
        MaterialRegistry registry = MaterialRawsLoader.load(root).materials();
        Material minted = registry.get(registry.id("a@t"));
        assertEquals(999, minted.valueCp());
        assertEquals(500, minted.density(), "floor(1000 * 128 / 256)");
        assertEquals(List.of("stone", "cured"), minted.tags());
    }

    // ---------------------------------------------------- dangling references

    @Test
    void meltsToMustResolveInTheUnitedNamespace() throws IOException {
        write("materials/m.json", material("m",
                "meltK", "1000", "meltsTo", "\"ghost\"", "meltYieldUnits", "1"));
        expectFailure("materials/m.json", "meltsTo");
    }

    @Test
    void burnsToMustResolveToAMaterial() throws IOException {
        write("materials/m.json", material("m", "flammability", "1",
                "ignitionK", "500", "fuelTicks", "100", "burnsTo", "\"ghost\""));
        expectFailure("materials/m.json", "burnsTo");
    }

    @Test
    void shattersToMustResolveToAMaterial() throws IOException {
        write("materials/m.json", material("m", "features",
                "{\"shatterOnSpike\":{\"spikeCuPerTick\":2000,\"shattersTo\":\"ghost\",\"radiusChebyshev\":2}}"));
        expectFailure("materials/m.json", "features.shatterOnSpike.shattersTo");
    }

    @Test
    void materialAndFluidIdsShareOneNamespace() throws IOException {
        write("materials/water.json", material("water"));
        write("fluids/water.json", fluid("water"));
        expectFailure("fluids/water.json", "id");
    }

    // ----------------------------------------------------------------- fluids

    @Test
    void fluidConductivityMustNotExceed256() throws IOException {
        write("fluids/f.json", fluid("f", "conductivityQ8", "300"));
        expectFailure("fluids/f.json", "conductivityQ8");
    }

    @Test
    void freezeRequiresFreezesTo() throws IOException {
        write("fluids/f.json", fluid("f", "freezeK", "273", "freezeMinDepth", "7"));
        expectFailure("fluids/f.json", "freezesTo");
    }

    @Test
    void freezeMinDepthCappedAtSeven() throws IOException {
        write("materials/ice.json", material("ice"));
        write("fluids/f.json", fluid("f",
                "freezeK", "273", "freezesTo", "\"ice\"", "freezeMinDepth", "8"));
        expectFailure("fluids/f.json", "freezeMinDepth");
    }

    @Test
    void freezesToMustResolveInTheUnitedNamespace() throws IOException {
        write("fluids/f.json", fluid("f",
                "freezeK", "273", "freezesTo", "\"ghost\"", "freezeMinDepth", "7"));
        expectFailure("fluids/f.json", "freezesTo");
    }

    @Test
    void evaporationRequiresChance() throws IOException {
        write("fluids/f.json", fluid("f", "evapMaxDepth", "1", "evapMinK", "333"));
        expectFailure("fluids/f.json", "evapChanceQ16");
    }

    @Test
    void fluidWithoutBoilsToIsBlessed() throws IOException {
        // BLESSING-QUEUE ruling 3: liquid tag => boilsTo is NOT binding for fluids.
        write("fluids/f.json", fluid("f"));
        assertEquals(1, MaterialRawsLoader.load(root).fluids().size());
    }

    // -------------------------------------------------------------- reactions

    @Test
    void reactionSolidMustExist() throws IOException {
        write("fluids/water.json", fluid("water"));
        write("reactions/r.json", reaction("r", "ghost"));
        expectFailure("reactions/r.json", "solid");
    }

    @Test
    void reactionSolidMustBeContactReactive() throws IOException {
        write("materials/inert.json", material("inert"));
        write("fluids/water.json", fluid("water"));
        write("reactions/r.json", reaction("r", "inert"));
        expectFailure("reactions/r.json", "solid");
    }

    @Test
    void reactionTriggerTagMustMatchTheSolidsReagentTag() throws IOException {
        write("materials/s.json", contactReactiveMaterial("s", "goo"));
        write("fluids/water.json", fluid("water"));
        write("reactions/r.json", reaction("r", "s"));
        expectFailure("reactions/r.json", "trigger.fluidTag");
    }

    @Test
    void reactionTriggerTagMustMatchSomeFluid() throws IOException {
        write("materials/s.json", contactReactiveMaterial("s", "goo"));
        write("fluids/water.json", fluid("water"));
        write("reactions/r.json", reaction("r", "s",
                "trigger", "{\"kind\":\"FLUID_CONTACT\",\"fluidTag\":\"goo\"}"));
        expectFailure("reactions/r.json", "trigger.fluidTag");
    }

    @Test
    void reactionPulseGasMustResolveToAFluid() throws IOException {
        write("materials/s.json", contactReactiveMaterial("s", "liquid"));
        write("fluids/water.json", fluid("water"));
        write("reactions/r.json", reaction("r", "s",
                "pulse", "{\"gasId\":\"steam\",\"magnitudeCap\":1680}"));
        expectFailure("reactions/r.json", "pulse.gasId");
    }

    @Test
    void reactionExpansionMustBePositive() throws IOException {
        write("materials/s.json", contactReactiveMaterial("s", "liquid"));
        write("fluids/water.json", fluid("water"));
        write("reactions/r.json", reaction("r", "s", "expansion", "0"));
        expectFailure("reactions/r.json", "expansion");
    }

    @Test
    void reactionMagnitudeCapMustFitSixteenBits() throws IOException {
        write("materials/s.json", contactReactiveMaterial("s", "liquid"));
        write("fluids/water.json", fluid("water"));
        write("reactions/r.json", reaction("r", "s",
                "pulse", "{\"gasId\":null,\"magnitudeCap\":70000}"));
        expectFailure("reactions/r.json", "pulse.magnitudeCap");
    }

    @Test
    void absentWearFieldsMeanNoWearAndPresentOnesParse() throws IOException {
        // BLESSING-QUEUE ruling 9: absent wear fields = no wear.
        write("materials/s.json", contactReactiveMaterial("s", "liquid"));
        write("fluids/water.json", fluid("water"));
        write("reactions/r.json", reaction("r", "s"));
        ReactionDefinition noWear = MaterialRawsLoader.load(root).materials().reactions().get(0);
        assertFalse(noWear.wears());
        assertEquals(0, noWear.wearPerUnit());
        assertEquals(0, noWear.wearCapacity());

        write("reactions/r.json", reaction("r", "s",
                "wearPerUnit", "5", "wearCapacity", "1200"));
        ReactionDefinition wearing = MaterialRawsLoader.load(root).materials().reactions().get(0);
        assertTrue(wearing.wears());
        assertEquals(5, wearing.wearPerUnit());
        assertEquals(1200, wearing.wearCapacity());
    }

    // ---------------------------------------------------------------- helpers

    private void write(String relative, String json) throws IOException {
        Path file = root.resolve(relative);
        Files.createDirectories(file.getParent());
        Files.writeString(file, json);
    }

    private RawsValidationException expectFailure(String file, String field) {
        RawsValidationException e = assertThrows(RawsValidationException.class,
                () -> MaterialRawsLoader.load(root));
        assertEquals(file, e.file(), e.getMessage());
        assertEquals(field, e.field(), e.getMessage());
        return e;
    }

    /**
     * A valid baseline material raw with member/value patches applied
     * ({@code name, literalJson} pairs; a {@code null} literal removes the member).
     */
    private static String material(String id, String... patches) {
        LinkedHashMap<String, String> members = new LinkedHashMap<>();
        members.put("id", "\"" + id + "\"");
        members.put("displayName", "\"" + id + "\"");
        members.put("phase", "\"SOLID\"");
        members.put("density", "1000");
        members.put("hardness", "3");
        members.put("flammability", "0");
        members.put("ignitionK", "null");
        members.put("meltK", "null");
        members.put("boilK", "null");
        members.put("conductivityQ8", "40");
        members.put("heatCapacityQ8", "64");
        members.put("fuelTicks", "0");
        members.put("burnsTo", "null");
        members.put("valueCp", "10");
        members.put("tags", "[\"stone\"]");
        members.put("light", "{\"opacity\":31}");
        members.put("features", "{}");
        return render(patch(members, patches));
    }

    private static String contactReactiveMaterial(String id, String reagentTag) {
        return material(id,
                "features", "{\"contactReactive\":{\"reagentTag\":\"" + reagentTag + "\"}}");
    }

    /** A valid baseline fluid raw (no freeze, no evaporation) with patches. */
    private static String fluid(String id, String... patches) {
        LinkedHashMap<String, String> members = new LinkedHashMap<>();
        members.put("id", "\"" + id + "\"");
        members.put("displayName", "\"" + id + "\"");
        members.put("density", "1000");
        members.put("conductivityQ8", "40");
        members.put("heatCapacityQ8", "208");
        members.put("boilK", "373");
        members.put("boilsTo", "null");
        members.put("evapMaxDepth", "0");
        members.put("tags", "[\"liquid\"]");
        return render(patch(members, patches));
    }

    /** A valid baseline treatment raw with patches. */
    private static String treatment(String id, String target, String derivedId,
            String... patches) {
        LinkedHashMap<String, String> members = new LinkedHashMap<>();
        members.put("id", "\"" + id + "\"");
        members.put("displayName", "\"" + id + "\"");
        members.put("target", "\"" + target + "\"");
        members.put("derivedId", "\"" + derivedId + "\"");
        members.put("derivedDisplayName", "\"" + derivedId + "\"");
        members.put("overrides", "{}");
        members.put("scaleQ8", "{}");
        members.put("addTags", "[]");
        return render(patch(members, patches));
    }

    /** A valid baseline FLUID_CONTACT reaction raw (no wear fields) with patches. */
    private static String reaction(String id, String solid, String... patches) {
        LinkedHashMap<String, String> members = new LinkedHashMap<>();
        members.put("id", "\"" + id + "\"");
        members.put("displayName", "\"" + id + "\"");
        members.put("solid", "\"" + solid + "\"");
        members.put("trigger", "{\"kind\":\"FLUID_CONTACT\",\"fluidTag\":\"liquid\"}");
        members.put("expansion", "240");
        members.put("pulse", "{\"gasId\":null,\"magnitudeCap\":1680}");
        return render(patch(members, patches));
    }

    private static String chargeable(String capacityCu, String maxSafe, String stops) {
        return "{\"chargeable\":{\"capacityCu\":" + capacityCu
                + ",\"maxSafeDischargePerTick\":" + maxSafe
                + ",\"saturationPct\":95,\"saturationHeatDeciKPerTick\":20"
                + ",\"equilibriumDeciK\":6000,\"colorStops\":" + stops + "}}";
    }

    private static String stops(String... stopLiterals) {
        return "[" + String.join(",", stopLiterals) + "]";
    }

    private static LinkedHashMap<String, String> patch(LinkedHashMap<String, String> members,
            String... patches) {
        for (int i = 0; i < patches.length; i += 2) {
            if (patches[i + 1] == null) {
                members.remove(patches[i]);
            } else {
                members.put(patches[i], patches[i + 1]);
            }
        }
        return members;
    }

    private static String render(LinkedHashMap<String, String> members) {
        StringBuilder json = new StringBuilder("{");
        boolean first = true;
        for (Map.Entry<String, String> member : members.entrySet()) {
            if (!first) {
                json.append(',');
            }
            first = false;
            json.append('"').append(member.getKey()).append("\":").append(member.getValue());
        }
        return json.append('}').toString();
    }
}
