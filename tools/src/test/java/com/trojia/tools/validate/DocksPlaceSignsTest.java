package com.trojia.tools.validate;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.List;
import java.util.Set;
import java.util.TreeSet;

import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import com.trojia.tools.tmx.TmxLayerGroup;
import com.trojia.tools.tmx.TmxObject;
import com.trojia.tools.tmx.TmxObjectLayer;

/**
 * The ward stops being anonymous, checked against the committed district map
 * ({@code content/maps/src/docks_surface.tmx}, regenerated from
 * {@code tools/scripts/gen_docks_surface.py}): every authored {@code place_sign} passes the
 * marker contract, and the roster covers the DOCKS-GAZETTEER's establishment keys — including
 * the two sites the gazetteer forbids a sign, which must stay unsigned.
 */
class DocksPlaceSignsTest {

    private static MapCheckContext docks;

    @BeforeAll
    static void loadDocks() {
        RawsIndex raws = new RawsLoader().load(TestRepo.rawsDir()).index();
        docks = TiledValidator.loadContext(
                TestRepo.mapsDir().resolve("docks_surface.tmx"), raws);
    }

    @Test
    void everyPlaceSignPassesTheMarkerContract() {
        ValidationReport report =
                new TiledValidator(List.of(new MarkerContractPass())).validate(docks);
        assertEquals(0, report.errors().size(), report::render);
    }

    @Test
    void theWholeEstablishmentRosterIsSigned() {
        Set<String> signed = signNames();
        // K01-K34 + K36. K35 is deliberately absent (below), and there is no K37.
        for (int k = 1; k <= 36; k++) {
            if (k == 35) {
                continue;
            }
            String key = String.format("sign_k%02d_", k);
            assertTrue(signed.stream().anyMatch(name -> name.startsWith(key)),
                    () -> "gazetteer establishment " + key + " has no place_sign; signed: " + signed);
        }
        // The four Compounds (gazetteer 2.5) carry a sign at their gate.
        for (String compound : new String[] {"sign_c1_", "sign_c2_", "sign_c3_", "sign_c4_"}) {
            assertTrue(signed.stream().anyMatch(name -> name.startsWith(compound)),
                    () -> "compound " + compound + " has no place_sign; signed: " + signed);
        }
        // 39 gates and doorways, plus ONE house plaque: the Netter mansion's own door
        // (quality slice 5, gazetteer 2.8/3.3). It is the single place in the ward where
        // the plot and the house give different answers -- the gate twenty tiles away says
        // the charge is pledged at Fenner's, the door says "hers; the ground is not" --
        // and signing the other three great houses would teach a player the opposite,
        // that the two lines always agree.
        assertEquals(40, signed.stream().filter(n -> !n.startsWith("sign_w_")).count(),
                () -> "unexpected building-sign roster: " + signed);
    }

    /**
     * The streets stop being anonymous too (S8 round 2, Eli: "THE STREETS HAVE NO NAMES ... a
     * player pans the ward and every road is anonymous", his ask naming "the fishbone pier").
     * Every named street and lane of gazetteer 2.3 and every waterfront feature of 2.2 carries
     * at least one {@code way} fingerpost.
     */
    @Test
    void everyNamedStreetAndWaterfrontFeatureIsSigned() {
        Set<String> ways = new TreeSet<>();
        for (TmxObject object : placeSigns()) {
            if ("way".equals(property(object, "kind"))) {
                ways.add(property(object, "place"));
            }
        }
        for (String named : new String[] {
            // 2.3 named streets and lanes
            "Tarwalk", "Saltgate Rise", "Ropewynd", "Herring Lane", "The Gullet",
            // 2.2 the waterfront line
            "The Long Quay", "Pier Row", "Wormwood Pier", "The Beaching Strand", "The Outfall",
            // 3.2 the fishbone pier and its three fingers
            "The Fishbone Pier", "First Finger", "Second Finger", "Third Finger",
        }) {
            assertTrue(ways.contains(named),
                    () -> "gazetteer way \"" + named + "\" has no sign; signed ways: " + ways);
        }
    }

    /**
     * A way's mark is a fingerpost, a building's is a hanging plaque, and every sign says
     * which it is — the client picks the silhouette straight off this property.
     */
    @Test
    void everySignDeclaresWhichKindOfMarkItStands() {
        for (TmxObject object : placeSigns()) {
            String kind = property(object, "kind");
            assertTrue("door".equals(kind) || "way".equals(kind),
                    () -> "place_sign " + object.name() + " has illegal kind \"" + kind + "\"");
        }
    }

    /**
     * One mark per cell, across the whole map. The client's "the cell a mark stands on always
     * names its own place" rule is only unambiguous while this holds.
     */
    @Test
    void noTwoSignsShareAMarkCell() {
        Set<String> cells = new TreeSet<>();
        for (TmxLayerGroup group : MapStructure.zGroups(docks.map())) {
            TmxObjectLayer markers = MapStructure.objectSublayer(group, MapStructure.MARKERS);
            if (markers == null) {
                continue;
            }
            for (TmxObject object : markers.objects()) {
                if (!"place_sign".equals(object.typeName())) {
                    continue;
                }
                String cell = group.name() + ":" + (int) (object.x() / 16)
                        + "," + (int) (object.y() / 16);
                assertTrue(cells.add(cell),
                        () -> "two place_signs share the cell " + cell);
            }
        }
    }

    /**
     * "Every string is the gazetteer's own wording, trimmed" is a claim the pass makes about
     * itself, and round 1 shipped one sign that did not hold it (S8 round 2 finding: "one sign
     * has no gazetteer source though the pass claims every string has one"). This checks it
     * instead of asserting it: every word of every {@code place}/{@code what} line longer than
     * four characters must appear somewhere in DOCKS-GAZETTEER.md, case-folded. Short words
     * (the, and, one, of) are skipped as noise, and the two listed stems are the document's
     * own words in another inflection.
     */
    @Test
    void everySignLineIsTheGazetteersOwnWording() throws java.io.IOException {
        String gazetteer = java.nio.file.Files.readString(
                TestRepo.designDocsDir().resolve("DOCKS-GAZETTEER.md"))
                .toLowerCase(java.util.Locale.ROOT);
        // "condo-like apartments" / "condo c04" is the document's own word; the signs say
        // "condos". Same stem, different inflection -- allowed, and named here rather than
        // silently stemmed, so a genuinely invented word cannot hide behind a clever matcher.
        Set<String> inflections = Set.of("condos");
        List<String> unsourced = new java.util.ArrayList<>();
        for (TmxObject sign : placeSigns()) {
            for (String key : new String[] {"place", "what"}) {
                for (String word : property(sign, key).split("[^A-Za-z'-]+")) {
                    String lower = word.toLowerCase(java.util.Locale.ROOT);
                    if (lower.length() <= 4 || inflections.contains(lower)) {
                        continue;
                    }
                    if (!gazetteer.contains(lower)) {
                        unsourced.add(sign.name() + " " + key + ": \"" + word + "\"");
                    }
                }
            }
        }
        assertEquals(List.of(), unsourced,
                "these sign words appear nowhere in the gazetteer");
    }

    private static List<TmxObject> placeSigns() {
        List<TmxObject> signs = new java.util.ArrayList<>();
        for (TmxLayerGroup group : MapStructure.zGroups(docks.map())) {
            TmxObjectLayer markers = MapStructure.objectSublayer(group, MapStructure.MARKERS);
            if (markers == null) {
                continue;
            }
            for (TmxObject object : markers.objects()) {
                if ("place_sign".equals(object.typeName())) {
                    signs.add(object);
                }
            }
        }
        return signs;
    }

    private static String property(TmxObject object, String key) {
        return object.properties().find(key)
                .map(com.trojia.tools.tmx.TmxProperty::value).orElse("");
    }

    @Test
    void theUnmarkedSitesStayUnmarked() {
        Set<String> signed = signNames();
        // DOCKS-GAZETTEER 3.1, K35 The Skyrunner's Roost: "(unmarked -- no sign, no door on
        // the establishments layer) ... must never appear on any discoverable establishments
        // list -- that IS the design."
        assertFalse(signed.stream().anyMatch(name -> name.startsWith("sign_k35")),
                "K35 The Skyrunner's Roost must never be signed");
        // DOCKS-GAZETTEER 3.2, Cache Row: unlicensed, "no lamps, NO door onto any street
        // layer" -- an off-grid smuggling shed does not hang a sign either.
        assertFalse(signed.stream().anyMatch(name -> name.contains("cache")),
                "Cache Row is unlicensed and must never be signed");
    }

    private static Set<String> signNames() {
        Set<String> names = new TreeSet<>();
        for (TmxLayerGroup group : MapStructure.zGroups(docks.map())) {
            TmxObjectLayer markers = MapStructure.objectSublayer(group, MapStructure.MARKERS);
            if (markers == null) {
                continue;
            }
            for (TmxObject object : markers.objects()) {
                if ("place_sign".equals(object.typeName())) {
                    names.add(object.name());
                }
            }
        }
        return names;
    }
}
