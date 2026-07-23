package com.trojia.client.input;

import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The GL-free half of the scripted-playtest harness (Sprint 4 CLIENT): parsing, comment/
 * blank handling, frame querying and loud failure on malformed rows. The dispatcher itself
 * is a thin switch in {@code ObserverApp} over the already-tested {@code apply*} seams.
 */
class ObserverScriptTest {

    @Test
    void parsesVerbsArgsAndCommentsAndSortsByFrame() {
        ObserverScript script = ObserverScript.parse("""
                # a playtest tape
                40 hold=1,0
                30 select=371   # the K21 sergeant
                31 follow
                32 play

                90 hold=0,0
                95 talk
                120 shot=C:/tmp/a.png
                """);
        assertEquals(7, script.actions().size());
        assertEquals(120, script.lastFrame());
        // Sorted frame-ascending regardless of file order.
        assertEquals(30, script.actions().get(0).frame());
        assertEquals(ObserverScript.Verb.SELECT, script.actions().get(0).verb());
        assertEquals(371, script.actions().get(0).intArgs()[0]);

        List<ObserverScript.Action> at40 = script.at(40);
        assertEquals(1, at40.size());
        assertEquals(ObserverScript.Verb.HOLD, at40.get(0).verb());
        assertEquals(1, at40.get(0).intArgs()[0]);
        assertEquals(0, at40.get(0).intArgs()[1]);

        assertEquals("C:/tmp/a.png", script.at(120).get(0).args());
        assertTrue(script.at(999).isEmpty());
    }

    @Test
    void multipleActionsOnOneFrameKeepFileOrder() {
        ObserverScript script = ObserverScript.parse("""
                10 zoom=4
                10 center=75,105
                10 z=12
                """);
        List<ObserverScript.Action> at10 = script.at(10);
        assertEquals(3, at10.size());
        assertEquals(ObserverScript.Verb.ZOOM, at10.get(0).verb());
        assertEquals(ObserverScript.Verb.CENTER, at10.get(1).verb());
        assertEquals(ObserverScript.Verb.Z, at10.get(2).verb());
        assertEquals(75, at10.get(1).intArgs()[0]);
        assertEquals(105, at10.get(1).intArgs()[1]);
    }

    @Test
    void malformedRowsFailLoudlyWithTheOffendingLine() {
        assertThrows(IllegalArgumentException.class,
                () -> ObserverScript.parse("banana"));
        assertThrows(IllegalArgumentException.class,
                () -> ObserverScript.parse("12 frobnicate"));
        assertThrows(IllegalArgumentException.class,
                () -> ObserverScript.parse("x talk"));
        assertThrows(IllegalArgumentException.class,
                () -> ObserverScript.parse("-1 talk"));
    }

    @Test
    void theSprintFourVerbVocabularyParses() {
        ObserverScript script = ObserverScript.parse("""
                1 topic=2
                2 eat
                3 climb=up
                4 climb=down
                5 journal
                6 plates
                7 pickpocket
                """);
        assertEquals(ObserverScript.Verb.TOPIC, script.at(1).get(0).verb());
        assertEquals(ObserverScript.Verb.EAT, script.at(2).get(0).verb());
        assertEquals("up", script.at(3).get(0).args());
        assertEquals("down", script.at(4).get(0).args());
    }
}
