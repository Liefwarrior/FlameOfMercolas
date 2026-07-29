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
        assertEquals(39, signed.size(), () -> "unexpected sign roster: " + signed);
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
