package com.trojia.client.inspect;

import java.util.ArrayList;
import java.util.List;

/**
 * The played actor's transient toast stack (Sprint 1 item 3, the Morrowind "Skyrunning
 * increased to 32" moment): a bounded, real-time-aged queue of short messages the
 * {@code ToastRenderer} draws bottom-center with a fade. GL-free and wall-clock-driven —
 * toasts age by RENDERED seconds ({@link #update} takes the frame delta), not by sim
 * ticks, so they stay readable at any speed setting including PAUSED.
 *
 * <p>Presentation-only: nothing reads this state back into the sim, and no determinism
 * surface hashes it.
 */
public final class ToastQueue {

    /** How long one toast lives, in real (rendered) seconds. */
    public static final float LIFETIME_SECONDS = 3f;

    /** The tail of the lifetime over which the toast fades to nothing. */
    public static final float FADE_SECONDS = 1f;

    /** At most this many toasts show at once; a burst evicts the oldest beyond it. */
    public static final int MAX_VISIBLE = 4;

    /** One live toast: its text and how long it has been showing. */
    public record Toast(String text, float ageSeconds) {

        /** Draw alpha: full until the fade tail, then linear to 0 at end of life. */
        public float alpha() {
            float remaining = LIFETIME_SECONDS - ageSeconds;
            if (remaining <= 0f) {
                return 0f;
            }
            return Math.min(1f, remaining / FADE_SECONDS);
        }
    }

    private final List<String> texts = new ArrayList<>();
    private final List<Float> ages = new ArrayList<>();

    /** Queues a toast (age 0), evicting the oldest if over {@link #MAX_VISIBLE}. */
    public void add(String text) {
        texts.add(text);
        ages.add(0f);
        while (texts.size() > MAX_VISIBLE) {
            texts.remove(0);
            ages.remove(0);
        }
    }

    /** Ages every toast by {@code deltaSeconds} and expires the ones past their lifetime. */
    public void update(float deltaSeconds) {
        for (int i = texts.size() - 1; i >= 0; i--) {
            float age = ages.get(i) + deltaSeconds;
            if (age >= LIFETIME_SECONDS) {
                texts.remove(i);
                ages.remove(i);
            } else {
                ages.set(i, age);
            }
        }
    }

    /** The live toasts, oldest first — the render stack order (oldest at the top). */
    public List<Toast> visible() {
        List<Toast> out = new ArrayList<>(texts.size());
        for (int i = 0; i < texts.size(); i++) {
            out.add(new Toast(texts.get(i), ages.get(i)));
        }
        return out;
    }
}
