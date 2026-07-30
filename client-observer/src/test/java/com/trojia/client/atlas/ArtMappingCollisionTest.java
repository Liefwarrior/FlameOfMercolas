package com.trojia.client.atlas;

import com.badlogic.gdx.utils.JsonReader;
import com.badlogic.gdx.utils.JsonValue;

import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.NavigableMap;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The palette has to tell the truth (quality slice 1, art-mapping.json NINTH revision).
 *
 * <p>An {@code art-mapping.json} can silently point two region names at ONE atlas cell, and
 * for three revisions the shipped Kenney mapping did: {@code floor_tile}, {@code roof_thatch}
 * and {@code floor_pave}'s first variant all resolved to {@code [3,19]}, so the ward's brick
 * arterial shared half its tiles with the paver sidewalk beside it and a thatched roof drew as
 * a road; {@code wall_rubble}'s random scatter contained {@code wall_plank}'s and
 * {@code wall_masonry}'s own cells, so a mudbrick hovel drew as timber or Reman concrete about
 * one tile in five, which quietly erased the wealth gradient DECISIONS.md's Compounds ruling
 * builds the district on.
 *
 * <p>None of that failed anything. It could not: a duplicate cell is a perfectly valid mapping.
 * So this test makes it a build failure instead. The rule is that every region name owns its
 * cells outright, and the only way to share is to say so out loud — a set listed in the
 * mapping's own {@code intentionalAliases} block, which is where the genuinely-one-substance
 * groups (the pool-water trio, the grass pair) are declared and where a future reviewer can
 * see, in one place, every share the pack believes in.
 */
class ArtMappingCollisionTest {

    /** {@code regions} entries of the shipped Kenney mapping, name → its {@code [col,row]} set. */
    private static NavigableMap<String, Set<String>> cellsByRegion() {
        JsonValue root = new JsonReader().parse(ShippedArtMapping.kenneyJson());
        JsonValue regions = root.get("regions");
        NavigableMap<String, Set<String>> byName = new TreeMap<>();
        for (JsonValue region = regions.child; region != null; region = region.next) {
            Set<String> cells = new LinkedHashSet<>();
            if (region.child != null && region.child.isNumber()) {
                cells.add(cell(region));
            } else {
                for (JsonValue pair = region.child; pair != null; pair = pair.next) {
                    cells.add(cell(pair));
                }
            }
            byName.put(region.name, cells);
        }
        return byName;
    }

    private static String cell(JsonValue pair) {
        return "[" + pair.child.asInt() + "," + pair.child.next.asInt() + "]";
    }

    /** The declared alias sets, as a name → group-representative lookup. */
    private static Map<String, String> aliasGroups() {
        JsonValue root = new JsonReader().parse(ShippedArtMapping.kenneyJson());
        JsonValue declared = root.get("intentionalAliases");
        Map<String, String> group = new TreeMap<>();
        if (declared == null) {
            return group;
        }
        for (JsonValue set = declared.child; set != null; set = set.next) {
            String representative = set.child.asString();
            for (JsonValue member = set.child; member != null; member = member.next) {
                group.put(member.asString(), representative);
            }
        }
        return group;
    }

    @Test
    void noTwoRegionsShareACellUnlessTheAliasIsDeclared() {
        Map<String, String> alias = aliasGroups();
        Map<String, String> owner = new TreeMap<>();
        List<String> collisions = new ArrayList<>();
        for (Map.Entry<String, Set<String>> entry : cellsByRegion().entrySet()) {
            String name = entry.getKey();
            for (String cell : entry.getValue()) {
                String previous = owner.putIfAbsent(cell, name);
                if (previous == null) {
                    continue;
                }
                String a = alias.get(previous);
                String b = alias.get(name);
                if (a == null || !a.equals(b)) {
                    collisions.add(cell + " is claimed by both \"" + previous + "\" and \""
                            + name + "\"");
                }
            }
        }
        assertEquals(List.of(), collisions,
                "two region names resolving to one atlas cell makes the map draw a lie; "
                        + "either move one, or declare the share in intentionalAliases");
    }

    /**
     * The mudbrick-hovel collision specifically, named rather than left to the general rule —
     * this one was not merely untidy, it was contradicting a locked design ruling every time it
     * fired.
     */
    @Test
    void rubbleNeverScattersOntoTimberOrRemanConcrete() {
        Map<String, Set<String>> cells = cellsByRegion();
        Set<String> rubble = cells.get("wall_rubble");
        assertFalse(rubble.contains("[9,7]"),
                "wall_rubble must not scatter wall_plank's cell: a mudbrick hovel would draw as timber");
        assertFalse(rubble.contains("[9,6]"),
                "wall_rubble must not scatter wall_masonry's cell: a mudbrick hovel would draw as "
                        + "Reman concrete, the compounds' wealth-HIGH material");
        assertTrue(rubble.size() > 1, "wall_rubble is a rough surface and keeps its scatter");
    }

    /**
     * Slice 4 (the ground under the buildings) needs a threshold vocabulary to spend, and a
     * register is only usable if the cell behind it is FULLY opaque — a partly-transparent floor
     * cell shows the true-black void through the ground. These five were verified opaque against
     * the sheet and are held ready; only {@code floor_grit} is referenced today.
     */
    @Test
    void theExteriorThresholdRegistersAreDeclaredAndReady() {
        Set<String> declared = new TreeSet<>(cellsByRegion().keySet());
        for (String register : List.of("floor_grit", "floor_sand", "floor_litter",
                "floor_kerb", "floor_drain")) {
            assertTrue(declared.contains(register),
                    () -> "exterior threshold register " + register + " is missing; declared: "
                            + declared);
            assertEquals(1, cellsByRegion().get(register).size(),
                    register + " is a single homogeneous cell, not a scatter");
        }
    }
}
