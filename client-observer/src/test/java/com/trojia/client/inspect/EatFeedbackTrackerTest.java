package com.trojia.client.inspect;

import com.trojia.client.scenario.CompoundBlockPopulation;
import com.trojia.client.scenario.DocksPopulation;
import com.trojia.sim.actor.ReasonCode;

import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;

/**
 * {@link EatFeedbackTracker}'s Sprint-5 growth: a COUNTER BUY additionally toasts the
 * barter-quote decomposition when the skill/standing tables are wired; the pre-Sprint-5
 * (unwired) constructor keeps the outcome sentence alone, byte-identical. The arm/expiry
 * machinery itself is pinned by {@code EatInputTest}. Headless, crafted stamps.
 */
class EatFeedbackTrackerTest {

    private final CompoundBlockPopulation p = CompoundBlockPopulation.build(1234L);
    private final ToastQueue toasts = new ToastQueue();

    @Test
    void wiredCounterBuyToastsTheDecomposedQuote() {
        EatFeedbackTracker tracker = new EatFeedbackTracker(p.registry(), toasts, () -> 2,
                DocksPopulation.freshSkillTracks(), DocksPopulation.freshFactionStandings());
        tracker.arm();
        p.registry().get(2).setLastReasonCode(ReasonCode.BOUGHT_FOOD);
        tracker.afterTick(1L);

        List<ToastQueue.Toast> visible = toasts.visible();
        assertEquals(2, visible.size());
        assertEquals(EatFeedbackTracker.BOUGHT, visible.get(0).text());
        assertEquals("[Streetwise 0: -0 haggle, -0 standing, +0 surcharge -- 5R]",
                visible.get(1).text());
    }

    @Test
    void unwiredTablesKeepTheOutcomeSentenceAlone() {
        EatFeedbackTracker tracker = new EatFeedbackTracker(p.registry(), toasts, () -> 2);
        tracker.arm();
        p.registry().get(2).setLastReasonCode(ReasonCode.BOUGHT_FOOD);
        tracker.afterTick(1L);
        assertEquals(1, toasts.visible().size());
        assertEquals(EatFeedbackTracker.BOUGHT, toasts.visible().get(0).text());
    }

    @Test
    void nonBuyOutcomesNeverCarryAQuote() {
        EatFeedbackTracker tracker = new EatFeedbackTracker(p.registry(), toasts, () -> 2,
                DocksPopulation.freshSkillTracks(), DocksPopulation.freshFactionStandings());
        tracker.arm();
        p.registry().get(2).setLastReasonCode(ReasonCode.SCAVENGED_FOOD);
        tracker.afterTick(1L);
        assertEquals(1, toasts.visible().size());
        assertEquals(EatFeedbackTracker.SCAVENGED, toasts.visible().get(0).text());
    }
}
