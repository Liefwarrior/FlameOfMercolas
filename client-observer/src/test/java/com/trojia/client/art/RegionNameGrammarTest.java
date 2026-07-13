package com.trojia.client.art;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

/** {@link RegionNameGrammar} composition rules (TILE-ART-SPEC section 3). */
class RegionNameGrammarTest {

    @Test
    void defaultFormAndBucketZeroAreOmitted() {
        assertEquals("granite", RegionNameGrammar.compose("granite", "block", 0));
    }

    @Test
    void nonDefaultFormIsAppended() {
        assertEquals("granite.floor", RegionNameGrammar.compose("granite", "floor", 0));
    }

    @Test
    void nonZeroBucketIsAppendedAfterTheForm() {
        assertEquals("chromatis.a2", RegionNameGrammar.compose("chromatis", "block", 2));
        assertEquals("chromatis.floor.a1", RegionNameGrammar.compose("chromatis", "floor", 1));
    }

    @Test
    void derivedIdsKeepTheAtSignVerbatim() {
        assertEquals("trudgeon_wood@getilia_soak.floor",
                RegionNameGrammar.compose("trudgeon_wood@getilia_soak", "floor", 0));
    }

    @Test
    void rejectsBlankSegmentsAndOutOfRangeBuckets() {
        assertThrows(IllegalArgumentException.class,
                () -> RegionNameGrammar.compose(" ", "block", 0));
        assertThrows(IllegalArgumentException.class,
                () -> RegionNameGrammar.compose("granite", null, 0));
        assertThrows(IllegalArgumentException.class,
                () -> RegionNameGrammar.compose("granite", "block", -1));
        assertThrows(IllegalArgumentException.class,
                () -> RegionNameGrammar.compose("granite", "block", 4));
    }
}
