package com.trojia.client.inspect;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** {@link QuitGuard} — the Sprint-4 two-press ESC confirmation contract. GL-free. */
class QuitGuardTest {

    @Test
    void firstPressArmsSecondPressQuits() {
        QuitGuard guard = new QuitGuard();
        assertFalse(guard.press(), "the first ESC must not quit");
        assertTrue(guard.armed());
        assertTrue(guard.press(), "the second ESC inside the window quits");
    }

    @Test
    void theWindowExpiresAndTheNextPressArmsAgain() {
        QuitGuard guard = new QuitGuard();
        assertFalse(guard.press());
        guard.update(QuitGuard.CONFIRM_WINDOW_SECONDS + 0.01f);
        assertFalse(guard.armed(), "the window decays by rendered seconds");
        assertFalse(guard.press(), "a press after expiry re-arms instead of quitting");
    }

    @Test
    void updateInsideTheWindowKeepsItLive() {
        QuitGuard guard = new QuitGuard();
        guard.press();
        guard.update(QuitGuard.CONFIRM_WINDOW_SECONDS / 2f);
        assertTrue(guard.armed());
        assertTrue(guard.press());
    }
}
