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

    /** The base lifetime of a short toast, in real (rendered) seconds. */
    public static final float LIFETIME_SECONDS = 3f;

    /** The tail of the lifetime over which the toast fades to nothing. */
    public static final float FADE_SECONDS = 1f;

    /** At most this many toasts show at once; a burst evicts the oldest beyond it. */
    public static final int MAX_VISIBLE = 4;

    /** Text at/under this length gets the base lifetime (the pre-Sprint-4 contract). */
    public static final int SHORT_TEXT_CHARS = 40;

    /** Extra lifetime per character past {@link #SHORT_TEXT_CHARS}, seconds. */
    public static final float EXTRA_SECONDS_PER_CHAR = 0.05f;

    /** The lifetime ceiling, seconds — even a novel of a check line expires eventually. */
    public static final float MAX_LIFETIME_SECONDS = 8f;

    /**
     * The lifetime of a toast carrying {@code text} (Sprint 4 playtest fix: a 100-char
     * Disco-style check line used to get the exact 3 seconds of "Grit increased" — long
     * lines now live proportionally longer, capped): the base for short text, plus
     * {@link #EXTRA_SECONDS_PER_CHAR} per character past {@link #SHORT_TEXT_CHARS}.
     */
    public static float lifetimeFor(String text) {
        float extra = Math.max(0, text.length() - SHORT_TEXT_CHARS) * EXTRA_SECONDS_PER_CHAR;
        return Math.min(MAX_LIFETIME_SECONDS, LIFETIME_SECONDS + extra);
    }

    /** One live toast: its text, how long it has shown, and its own full lifetime. */
    public record Toast(String text, float ageSeconds, float lifetimeSeconds) {

        /** Draw alpha: full until the fade tail, then linear to 0 at end of life. */
        public float alpha() {
            float remaining = lifetimeSeconds - ageSeconds;
            if (remaining <= 0f) {
                return 0f;
            }
            return Math.min(1f, remaining / FADE_SECONDS);
        }
    }

    private final List<String> texts = new ArrayList<>();
    private final List<Float> ages = new ArrayList<>();
    private final List<Float> lifetimes = new ArrayList<>();

    /** Queues a toast (age 0), evicting the oldest if over {@link #MAX_VISIBLE}. */
    public void add(String text) {
        texts.add(text);
        ages.add(0f);
        lifetimes.add(lifetimeFor(text));
        while (texts.size() > MAX_VISIBLE) {
            texts.remove(0);
            ages.remove(0);
            lifetimes.remove(0);
        }
    }

    /** Ages every toast by {@code deltaSeconds} and expires the ones past their lifetime. */
    public void update(float deltaSeconds) {
        for (int i = texts.size() - 1; i >= 0; i--) {
            float age = ages.get(i) + deltaSeconds;
            if (age >= lifetimes.get(i)) {
                texts.remove(i);
                ages.remove(i);
                lifetimes.remove(i);
            } else {
                ages.set(i, age);
            }
        }
    }

    /** The live toasts, oldest first — the render stack order (oldest at the top). */
    public List<Toast> visible() {
        List<Toast> out = new ArrayList<>(texts.size());
        for (int i = 0; i < texts.size(); i++) {
            out.add(new Toast(texts.get(i), ages.get(i), lifetimes.get(i)));
        }
        return out;
    }
}
