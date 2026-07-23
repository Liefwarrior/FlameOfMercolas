package com.trojia.client.inspect;

import com.trojia.client.scenario.CompoundBlockPopulation;
import com.trojia.client.scenario.IdentityRegistry;
import com.trojia.sim.bark.BarkTableRegistry;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * {@link TalkText}'s degraded modes and key parsing (the un-forged compound fixture: EMPTY
 * identity, EMPTY bark tables, UNWIRED standings) — the named/authored surfaces are pinned
 * by {@code DocksTalkTest} against the forged bake. Headless, no GL.
 */
class TalkTextTest {

    @Test
    void dispositionTagParsesTheSelectorsKeyShapes() {
        assertEquals("[cold]", TalkText.dispositionTag("greet.watch.cold.night"));
        assertEquals("[kin]", TalkText.dispositionTag("greet.serf.kin"));
        assertEquals("[held]", TalkText.dispositionTag("mood.held"));
        assertEquals("[?]", TalkText.dispositionTag("weird"));
    }

    @Test
    void unauthoredTablesDegradeToSaysNothingNeverSilence() {
        CompoundBlockPopulation p = CompoundBlockPopulation.build(1234L);
        int listener = strangerTo(p, 2); // no kin/friend tie: the neutral greeting lane
        TalkText.Exchange exchange = TalkText.greet(1234L, 100L, 2, listener, p.registry(),
                p.jobs(), IdentityRegistry.EMPTY, p.system().factionStandings(),
                p.relationships(), BarkTableRegistry.EMPTY);

        assertEquals(TalkText.SAYS_NOTHING, exchange.barkLine(),
                "no authored tables: the panel still shows SOMETHING");
        assertTrue(exchange.nameLine().startsWith("Serf #2"),
                "un-forged fixture: the pre-names fallback style: " + exchange.nameLine());
        assertEquals("[neutral]", exchange.contextLine(), "unwired standings read neutral");

        List<String> lines = exchange.panelLines();
        assertEquals(exchange.nameLine(), lines.get(0));
        assertTrue(lines.get(1).endsWith("[neutral]"), lines.get(1));
        assertEquals(TalkText.SAYS_NOTHING, lines.get(lines.size() - 1));
    }

    /** The lowest-id actor with NO relationship edge at all to {@code selfId}. */
    private static int strangerTo(CompoundBlockPopulation p, int selfId) {
        outer:
        for (int i = 0; i < p.registry().size(); i++) {
            if (i == selfId) {
                continue;
            }
            for (int e = 0; e < p.relationships().size(); e++) {
                var edge = p.relationships().get(e);
                if ((edge.fromId() == selfId && edge.toId() == i)
                        || (edge.fromId() == i && edge.toId() == selfId)) {
                    continue outer;
                }
            }
            return i;
        }
        throw new AssertionError("everyone knows #" + selfId);
    }
}
