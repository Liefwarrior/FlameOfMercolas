package com.trojia.sim.actor;

import org.junit.jupiter.api.Test;

import java.lang.reflect.Field;
import java.lang.reflect.Modifier;
import java.util.ArrayList;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assertions.fail;

/**
 * The S8 trade vocabulary's guard rails. The load-bearing one is
 * {@link #everyItemKindHasARow()}: {@link ItemKinds} is append-only and rides the save format,
 * so a kind appended WITHOUT a {@link TradeGoods} row would be an item with no weight, no
 * category and — worse — no raws symbol, silently unnameable from content. Reflection makes
 * that impossible to forget.
 */
final class TradeGoodsTest {

    private static List<Field> itemKindConstants() {
        List<Field> out = new ArrayList<>();
        for (Field f : ItemKinds.class.getDeclaredFields()) {
            if (Modifier.isPublic(f.getModifiers()) && Modifier.isStatic(f.getModifiers())
                    && f.getType() == short.class) {
                out.add(f);
            }
        }
        return out;
    }

    @Test
    void everyItemKindHasARow() throws Exception {
        List<Field> kinds = itemKindConstants();
        assertTrue(kinds.size() >= 14, "expected the whole S8 vocabulary, saw " + kinds.size());
        for (Field f : kinds) {
            short kind = f.getShort(null);
            TradeGoods.Entry row = TradeGoods.entryOf(kind);
            assertNotNull(row, "ItemKinds." + f.getName() + " (" + kind
                    + ") has no TradeGoods row — append the row with the kind");
            assertTrue(row.weight() > 0, f.getName() + " must carry a positive weight");
            assertNotNull(row.category(), f.getName() + " must carry a category");
        }
        assertEquals(kinds.size(), TradeGoods.count(),
                "TradeGoods must have exactly one row per ItemKinds constant");
    }

    @Test
    void rowsAreInAscendingKindOrder() {
        for (int i = 1; i < TradeGoods.count(); i++) {
            assertTrue(TradeGoods.at(i - 1).kind() < TradeGoods.at(i).kind(),
                    "rows must ascend by kind id at index " + i);
        }
    }

    @Test
    void symbolsAreUniqueAndRoundTrip() {
        for (int i = 0; i < TradeGoods.count(); i++) {
            TradeGoods.Entry a = TradeGoods.at(i);
            assertEquals(a.kind(), TradeGoods.kindForSymbol(a.symbol()),
                    "symbol " + a.symbol() + " must resolve back to its own kind");
            assertEquals(a.symbol(), TradeGoods.symbolOf(a.kind()));
            for (int j = i + 1; j < TradeGoods.count(); j++) {
                if (a.symbol().equals(TradeGoods.at(j).symbol())) {
                    fail("duplicate symbol " + a.symbol());
                }
            }
        }
    }

    @Test
    void royalsIsAnAliasOfCoinAndUnknownSymbolsResolveToNoKind() {
        assertEquals(ItemKinds.COIN, TradeGoods.kindForSymbol("royals"));
        assertEquals(ItemKinds.COIN, TradeGoods.kindForSymbol("coin"));
        assertEquals(TradeGoods.NO_KIND, TradeGoods.kindForSymbol("wastrel_scalp"));
        assertEquals(TradeGoods.NO_KIND, TradeGoods.kindForSymbol(null));
        // symbolOf never reports an alias as the canonical spelling
        assertEquals("coin", TradeGoods.symbolOf(ItemKinds.COIN));
    }

    @Test
    void theFourGoodsAndEveryScalpAreMaterials() {
        short[] materials = {ItemKinds.CORDAGE, ItemKinds.PITCH, ItemKinds.BARREL_STOCK,
                ItemKinds.SALT, ItemKinds.RAT_SCALP, ItemKinds.GULL_SCALP, ItemKinds.CAT_SCALP};
        for (short kind : materials) {
            assertSame(TradeGoods.Category.MATERIALS, TradeGoods.categoryOf(kind),
                    "kind " + kind + " must sell as MATERIALS (Eli's ruling: a scalp is a raw "
                            + "harvested by-product like a hide, not a commodity)");
            assertTrue(TradeGoods.isCategory(kind, TradeGoods.Category.MATERIALS));
        }
        assertSame(TradeGoods.Category.FOOD, TradeGoods.categoryOf(ItemKinds.FOOD));
        assertSame(TradeGoods.Category.FOOD, TradeGoods.categoryOf(ItemKinds.FISH));
        assertSame(TradeGoods.Category.COMMODITIES, TradeGoods.categoryOf(ItemKinds.COIN));
    }

    @Test
    void thereAreExactlyFourCategoriesAndServicesHoldsNoItem() {
        assertEquals(4, TradeGoods.Category.values().length,
                "exactly four trade categories — no fifth, no renames (Eli's ruling)");
        for (int i = 0; i < TradeGoods.count(); i++) {
            if (TradeGoods.at(i).category() == TradeGoods.Category.SERVICES) {
                fail("a SERVICE is a quest (S10), never a carried stack: "
                        + TradeGoods.at(i).symbol());
            }
        }
    }

    @Test
    void anUntabledKindDegradesQuietly() {
        short bogus = 30_000;
        assertNull(TradeGoods.entryOf(bogus));
        assertNull(TradeGoods.categoryOf(bogus));
        assertNull(TradeGoods.symbolOf(bogus));
        assertEquals(0, TradeGoods.weightOf(bogus));
    }
}
