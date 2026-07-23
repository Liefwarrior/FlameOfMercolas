package com.trojia.client.inspect;

import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** {@link ToastQueue} lifetime/fade/eviction contract (Sprint 1 item 3). GL-free. */
class ToastQueueTest {

    @Test
    void freshToastIsVisibleAtFullAlpha() {
        ToastQueue q = new ToastQueue();
        q.add("Skyrunning increased to 32");
        List<ToastQueue.Toast> visible = q.visible();
        assertEquals(1, visible.size());
        assertEquals("Skyrunning increased to 32", visible.get(0).text());
        assertEquals(1f, visible.get(0).alpha(), 1e-6);
    }

    @Test
    void toastFadesOverItsFinalSecondThenExpires() {
        ToastQueue q = new ToastQueue();
        q.add("Grit increased to 5");

        // Age to the fade boundary: still fully opaque.
        q.update(ToastQueue.LIFETIME_SECONDS - ToastQueue.FADE_SECONDS);
        assertEquals(1f, q.visible().get(0).alpha(), 1e-4);

        // Halfway into the fade tail: half alpha.
        q.update(ToastQueue.FADE_SECONDS / 2f);
        assertEquals(0.5f, q.visible().get(0).alpha(), 1e-4);

        // Past end of life: gone.
        q.update(ToastQueue.FADE_SECONDS / 2f + 0.01f);
        assertTrue(q.visible().isEmpty());
    }

    @Test
    void toastsStackOldestFirstAndAgeIndependently() {
        ToastQueue q = new ToastQueue();
        q.add("first");
        q.update(1f);
        q.add("second");
        List<ToastQueue.Toast> visible = q.visible();
        assertEquals(List.of("first", "second"),
                visible.stream().map(ToastQueue.Toast::text).toList());
        assertEquals(1f, visible.get(0).ageSeconds(), 1e-6);
        assertEquals(0f, visible.get(1).ageSeconds(), 1e-6);
    }

    @Test
    void longTextLivesProportionallyLongerCapped() {
        // Short text keeps the base lifetime (the pre-Sprint-4 contract, byte-for-byte).
        assertEquals(ToastQueue.LIFETIME_SECONDS, ToastQueue.lifetimeFor("Grit increased to 5"),
                1e-6);
        // A 100-char check line gets base + 60 chars over the threshold * per-char extra.
        String checkLine = "x".repeat(100);
        assertEquals(ToastQueue.LIFETIME_SECONDS + 60 * ToastQueue.EXTRA_SECONDS_PER_CHAR,
                ToastQueue.lifetimeFor(checkLine), 1e-6);
        // The ceiling holds for a novel.
        assertEquals(ToastQueue.MAX_LIFETIME_SECONDS, ToastQueue.lifetimeFor("y".repeat(500)),
                1e-6);

        // And the queue actually keeps the long toast alive past the short lifetime.
        ToastQueue q = new ToastQueue();
        q.add(checkLine);
        q.update(ToastQueue.LIFETIME_SECONDS + 0.5f);
        assertEquals(1, q.visible().size(), "a long check line outlives the base lifetime");
        assertEquals(ToastQueue.lifetimeFor(checkLine), q.visible().get(0).lifetimeSeconds(),
                1e-6);
        q.update(ToastQueue.MAX_LIFETIME_SECONDS);
        assertTrue(q.visible().isEmpty());
    }

    @Test
    void aBurstEvictsTheOldestBeyondTheVisibleCap() {
        ToastQueue q = new ToastQueue();
        for (int i = 0; i < ToastQueue.MAX_VISIBLE + 2; i++) {
            q.add("toast " + i);
        }
        List<ToastQueue.Toast> visible = q.visible();
        assertEquals(ToastQueue.MAX_VISIBLE, visible.size());
        assertEquals("toast 2", visible.get(0).text(), "the two oldest were evicted");
        assertEquals("toast " + (ToastQueue.MAX_VISIBLE + 1),
                visible.get(visible.size() - 1).text());
    }
}
