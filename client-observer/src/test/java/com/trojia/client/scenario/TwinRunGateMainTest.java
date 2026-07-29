package com.trojia.client.scenario;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Unit cover for the twin-run gate's comparator plumbing — the parts that must not quietly
 * no-op. The soak-scale proof is the {@code :client-observer:twinRunGate} task itself; this
 * test guards the two ways a harness rots into a rubber stamp: a hash tag that stops matching
 * (must throw, never "skip the hash check and pass") and a divergence locator that reports the
 * wrong line.
 */
class TwinRunGateMainTest {

    @Test
    void extractsTheCombinedHashOffTheTaggedLine() {
        String report = "some rows\n"
                + "  at tick 400;  WRLD=5f23f797e04de292  ACTORS=30075e657a6f4f8d\n"
                + DocksActorsMain.WORLD_HASH_TAG + "f2d2f86753d8ee73\n"
                + "====\n";
        assertEquals("f2d2f86753d8ee73", TwinRunGateMain.extractHash(report));
    }

    @Test
    void aReportWithNoHashLineIsAHardFailureNotASkippedComparator() {
        IllegalStateException boom = assertThrows(IllegalStateException.class,
                () -> TwinRunGateMain.extractHash("a report that never printed a hash\n"));
        assertTrue(boom.getMessage().contains("un-hooked"),
                "the message must say the gate lost its hash comparator: " + boom.getMessage());
    }

    @Test
    void namesTheFirstDivergentLine() {
        String a = "one\ntwo\nthree\n";
        String b = "one\nTWO\nthree\n";
        assertTrue(TwinRunGateMain.firstTextDivergence(a, b).contains("line 2"),
                TwinRunGateMain.firstTextDivergence(a, b));
    }

    @Test
    void reportsAPureLengthDifferenceRatherThanClaimingIdentity() {
        String a = "one\ntwo";           // no trailing newline: a strict line-array prefix of b
        String b = "one\ntwo\nthree";
        String verdict = TwinRunGateMain.firstTextDivergence(a, b);
        assertTrue(verdict.contains("lengths differ"), verdict);
    }
}
