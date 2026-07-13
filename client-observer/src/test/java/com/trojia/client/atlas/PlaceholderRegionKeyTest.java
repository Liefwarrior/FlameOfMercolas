package com.trojia.client.atlas;

import com.trojia.client.art.RegionNameGrammar;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

/**
 * Headless tests for {@link PlaceholderRegionKey}: the inverse of
 * {@link RegionNameGrammar#compose} over the grammar of TILE-ART-SPEC section 3.
 */
class PlaceholderRegionKeyTest {

    @Test
    void bareMaterialIsBlockFormBucketZero() {
        assertEquals(new PlaceholderRegionKey("granite", "block", 0),
                PlaceholderRegionKey.parse("granite"));
    }

    @Test
    void floorSegmentParses() {
        assertEquals(new PlaceholderRegionKey("granite", "floor", 0),
                PlaceholderRegionKey.parse("granite.floor"));
    }

    @Test
    void bucketSuffixParsesWithAndWithoutForm() {
        assertEquals(new PlaceholderRegionKey("chromatis", "block", 2),
                PlaceholderRegionKey.parse("chromatis.a2"));
        assertEquals(new PlaceholderRegionKey("chromatis", "floor", 1),
                PlaceholderRegionKey.parse("chromatis.floor.a1"));
    }

    @Test
    void derivedIdsKeepTheAtSignVerbatim() {
        assertEquals(new PlaceholderRegionKey("trudgeon_wood@getilia_soak", "floor", 0),
                PlaceholderRegionKey.parse("trudgeon_wood@getilia_soak.floor"));
        assertEquals(new PlaceholderRegionKey("trudgeon_wood@getilia_soak", "block", 0),
                PlaceholderRegionKey.parse("trudgeon_wood@getilia_soak"));
    }

    @Test
    void roundTripsEveryComposeOutput() {
        for (String id : new String[]{"granite", "chromatis", "trudgeon_wood@getilia_soak"}) {
            for (String form : new String[]{"block", "floor"}) {
                for (int bucket = 0; bucket <= RegionNameGrammar.MAX_BUCKET; bucket++) {
                    String name = RegionNameGrammar.compose(id, form, bucket);
                    assertEquals(new PlaceholderRegionKey(id, form, bucket),
                            PlaceholderRegionKey.parse(name), name);
                }
            }
        }
    }

    @Test
    void rejectsNamesOutsideTheGrammar() {
        assertThrows(IllegalArgumentException.class,
                () -> PlaceholderRegionKey.parse(null));
        assertThrows(IllegalArgumentException.class,
                () -> PlaceholderRegionKey.parse("  "));
        assertThrows(IllegalArgumentException.class,
                () -> PlaceholderRegionKey.parse(".floor"));
        assertThrows(IllegalArgumentException.class,
                () -> PlaceholderRegionKey.parse("granite."));
    }

    @Test
    void rejectsInvalidComponents() {
        assertThrows(IllegalArgumentException.class,
                () -> new PlaceholderRegionKey(" ", "block", 0));
        assertThrows(IllegalArgumentException.class,
                () -> new PlaceholderRegionKey("granite", " ", 0));
        assertThrows(IllegalArgumentException.class,
                () -> new PlaceholderRegionKey("granite", "block", -1));
    }
}
