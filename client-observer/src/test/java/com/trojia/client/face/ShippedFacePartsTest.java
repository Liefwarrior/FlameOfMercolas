package com.trojia.client.face;

import com.trojia.client.sprite.SpriteIndex;
import com.trojia.client.sprite.SpriteRef;
import org.junit.jupiter.api.Test;

import java.util.List;
import java.util.Set;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Inventory + shape contracts of the shipped {@code face-parts-index.json} against the
 * unified art spec: §4.5 part minima, §4.1 slot cell sizes, §4.7 named entries. (Schema
 * validation itself is {@code SpriteIndex}'s job and covered by its own tests — one index
 * format serves actor sprites and face parts.)
 */
class ShippedFacePartsTest {

    private static int count(SpriteIndex index, String... tags) {
        return index.query(Set.of(tags)).size();
    }

    @Test
    void inventory_meetsSpecMinima() {
        SpriteIndex index = ShippedFaceContent.index();
        assertTrue(count(index, "face_base") >= 9, "3 bases x 3 skins");
        assertEquals(3, count(index, "face_base", "skin_pale"));
        assertEquals(3, count(index, "face_base", "skin_tan"));
        assertEquals(3, count(index, "face_base", "skin_dark"));
        assertTrue(count(index, "face_eyes") >= 6);
        assertTrue(count(index, "face_brow") >= 4);
        assertTrue(count(index, "face_nose") >= 4);
        assertTrue(count(index, "face_mouth") >= 8);
        assertTrue(count(index, "face_scar") >= 3);
        for (String color : List.of("black", "brown", "grey", "white", "red")) {
            assertTrue(count(index, "face_hair", "hair_" + color) >= 5,
                    "5 hair styles in " + color);
            assertTrue(count(index, "face_mouth", "hair_" + color) >= 3,
                    "3 bearded mouths in " + color);
        }
        for (HeadwearClass cls : HeadwearClass.values()) {
            if (cls != HeadwearClass.BARE) {
                assertEquals(2, count(index, "face_headwear", cls.hwTag()),
                        "2 per headwear class: " + cls);
            }
        }
        assertEquals(2, count(index, "face_named"), "Devin + Minister John");
    }

    @Test
    void slotCellSizes_matchTheSlotTable() {
        SpriteIndex index = ShippedFaceContent.index();
        for (SpriteRef part : index.all()) {
            String id = part.id();
            int w = part.cellsW();
            int h = part.cellsH();
            if (id.startsWith("face_base_") || id.startsWith("face_named_")) {
                assertEquals("3x3", w + "x" + h, id);
            } else if (id.startsWith("face_hair_") || id.startsWith("face_headwear_")) {
                assertEquals("3x2", w + "x" + h, id);
            } else if (id.startsWith("face_eyes_") || id.startsWith("face_brow_")
                    || id.startsWith("face_mouth_")) {
                assertEquals("2x1", w + "x" + h, id);
            } else if (id.startsWith("face_nose_") || id.startsWith("face_scar_")) {
                assertEquals("1x1", w + "x" + h, id);
            } else {
                throw new AssertionError("unexpected part id family: " + id);
            }
        }
    }

    @Test
    void namedEntries_carryTheNamedTag_andFullCanvas() {
        SpriteIndex index = ShippedFaceContent.index();
        for (String npc : List.of("devin", "john")) {
            SpriteRef named = index.byId("face_named_" + npc);
            assertTrue(named != null && named.tags().contains("face_named"), npc);
            assertEquals(3, named.cellsW(), npc);
            assertEquals(3, named.cellsH(), npc);
        }
    }
}
