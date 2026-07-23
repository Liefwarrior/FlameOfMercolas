package com.trojia.client.inspect;

/**
 * The two-press quit confirmation (Sprint 4 CLIENT, the playtest fix pass: {@code ESC}
 * used to close the observer INSTANTLY — one keystroke ended a session with no save verb
 * in existence). First {@code ESC} arms a short confirmation window (the caller toasts
 * {@link #CONFIRM_TOAST}); a second {@code ESC} inside the window quits. The window decays
 * by rendered wall-clock seconds (readable at any sim speed, the {@link ToastQueue}
 * convention). Pure state, no libGDX dependency.
 */
public final class QuitGuard {

    /** How long the second {@code ESC} has to land, in real (rendered) seconds. */
    public static final float CONFIRM_WINDOW_SECONDS = 2f;

    /** What the caller toasts on the arming press. */
    public static final String CONFIRM_TOAST = "Press ESC again to quit.";

    private float remainingSeconds;

    /**
     * Registers one {@code ESC} press: returns {@code true} when it lands inside a live
     * confirmation window (quit now), else arms the window and returns {@code false}
     * (the caller toasts {@link #CONFIRM_TOAST}).
     */
    public boolean press() {
        if (remainingSeconds > 0f) {
            return true;
        }
        remainingSeconds = CONFIRM_WINDOW_SECONDS;
        return false;
    }

    /** Whether a confirmation window is currently live. */
    public boolean armed() {
        return remainingSeconds > 0f;
    }

    /** Decays the window by one frame's rendered seconds. */
    public void update(float deltaSeconds) {
        remainingSeconds = Math.max(0f, remainingSeconds - deltaSeconds);
    }
}
