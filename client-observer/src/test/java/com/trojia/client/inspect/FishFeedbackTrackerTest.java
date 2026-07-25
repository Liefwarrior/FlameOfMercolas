package com.trojia.client.inspect;

import com.trojia.client.scenario.CompoundBlockPopulation;
import com.trojia.client.scenario.DocksPopulation;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.FishingSpots;
import com.trojia.sim.actor.FishingZoneTable;
import com.trojia.sim.actor.ReasonCode;
import com.trojia.sim.actor.SkillTrackRegistry;
import com.trojia.sim.world.PackedPos;

import org.junit.jupiter.api.Test;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.UncheckedIOException;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * {@link FishFeedbackTracker}'s check-line growth over the base arm/expiry machinery
 * ({@code FishInputTest} pins that): a resolved CAST with wired tracks and the spot still
 * beside the fisher additionally toasts {@link CheckLineFormatter#fishingLine}'s visible
 * dice; a sunk spot or unwired tracks keep the outcome sentence alone. Headless, crafted
 * stamps + a registry state authored through the persisted triad's own {@code load}.
 */
class FishFeedbackTrackerTest {

    private final CompoundBlockPopulation p = CompoundBlockPopulation.build(1234L);
    private final ToastQueue toasts = new ToastQueue();

    /** One LARGE zone whose water laps the cell beside actor 2 (slotNear must find it). */
    private FishingSpots liveSpotBesideActor2() {
        int cell = p.registry().get(2).cell();
        int water = PackedPos.pack(PackedPos.x(cell) + 1, PackedPos.y(cell),
                PackedPos.z(cell));
        FishingZoneTable zones = new FishingZoneTable(
                new int[] {FishingZoneTable.LARGE}, new int[] {cell}, new int[][] {{water}});
        try {
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            DataOutputStream out = new DataOutputStream(bytes);
            out.writeInt(zones.zoneCount());
            out.writeInt(0);          // slot 0: the one zone, live
            out.writeLong(1_500L);
            out.writeLong(90_000L);
            for (int s = 1; s < FishingSpots.SLOT_CAPACITY; s++) {
                out.writeInt(Actor.NONE);
                out.writeLong(0L);
                out.writeLong(0L);
            }
            FishingSpots spots = new FishingSpots(zones);
            spots.load(new DataInputStream(new ByteArrayInputStream(bytes.toByteArray())));
            return spots;
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    @Test
    void resolvedCastToastsTheCatchCheckLineOffTheLiveSpot() {
        FishFeedbackTracker tracker = new FishFeedbackTracker(p.registry(), toasts, () -> 2,
                DocksPopulation.freshSkillTracks(), liveSpotBesideActor2());
        tracker.arm();
        p.registry().get(2).setLastReasonCode(ReasonCode.CAUGHT_FISH);
        tracker.afterTick(1L);

        List<ToastQueue.Toast> visible = toasts.visible();
        assertEquals(2, visible.size());
        assertEquals(FishFeedbackTracker.CAUGHT, visible.get(0).text());
        // Fishing 0 + AGI 10 vs deep-water resist 40: 350 + 10*(10-40) = 50 -> floor 100.
        assertEquals("[Fishing 0 vs deep water 40: 10% -- CAUGHT]", visible.get(1).text());
    }

    @Test
    void aSunkSpotSkipsTheCheckLineButKeepsTheSentence() {
        FishFeedbackTracker tracker = new FishFeedbackTracker(p.registry(), toasts, () -> 2,
                DocksPopulation.freshSkillTracks(), FishingSpots.EMPTY);
        tracker.arm();
        p.registry().get(2).setLastReasonCode(ReasonCode.FISH_GOT_AWAY);
        tracker.afterTick(1L);
        assertEquals(1, toasts.visible().size());
        assertEquals(FishFeedbackTracker.GOT_AWAY, toasts.visible().get(0).text());
    }

    @Test
    void unwiredTracksKeepTheOutcomeSentenceAlone() {
        FishFeedbackTracker tracker = new FishFeedbackTracker(p.registry(), toasts, () -> 2,
                SkillTrackRegistry.UNWIRED, liveSpotBesideActor2());
        tracker.arm();
        p.registry().get(2).setLastReasonCode(ReasonCode.CAUGHT_FISH);
        tracker.afterTick(1L);
        assertEquals(1, toasts.visible().size());
        assertEquals(FishFeedbackTracker.CAUGHT, toasts.visible().get(0).text());
    }

    @Test
    void aMissedCastNeverCarriesACheckLine() {
        FishFeedbackTracker tracker = new FishFeedbackTracker(p.registry(), toasts, () -> 2,
                DocksPopulation.freshSkillTracks(), liveSpotBesideActor2());
        tracker.arm();
        p.registry().get(2).setLastReasonCode(ReasonCode.NO_SPOT_IN_REACH);
        tracker.afterTick(1L);
        assertEquals(1, toasts.visible().size());
        assertTrue(toasts.visible().get(0).text().equals(FishFeedbackTracker.NO_SPOT));
    }
}
