package com.trojia.client.inspect;

/**
 * The TALK surface's observer-only UI state (Sprint 2 item 1): whether the speech panel is
 * open, the frozen {@link TalkText.Exchange} it shows, and the most recent skill-check
 * result line rendered inside it (pickpocket attempt feedback while the panel is up). Pure
 * state, no libGDX dependency — mirrors {@link PlayModeState}'s shape.
 *
 * <p>The exchange is FROZEN at open (computed once, at its tick) so the panel does not
 * reroll as ticks pass; {@code T} re-greets, {@code ESC} closes. Presentation-only:
 * nothing reads this state back into the sim.
 */
public final class TalkState {

    private TalkText.Exchange exchange;
    private String checkLine;

    /** Whether the speech panel is up. */
    public boolean open() {
        return exchange != null;
    }

    /** The frozen exchange the panel shows; {@code null} while closed. */
    public TalkText.Exchange exchange() {
        return exchange;
    }

    /** Opens (or re-greets) the panel on a fresh exchange; clears any stale check line. */
    public void open(TalkText.Exchange exchange) {
        this.exchange = exchange;
        this.checkLine = null;
    }

    /** Closes the panel. */
    public void close() {
        this.exchange = null;
        this.checkLine = null;
    }

    /** The check-result line shown under the bark, or {@code null} when none rolled yet. */
    public String checkLine() {
        return checkLine;
    }

    /**
     * Lands a skill-check result on the open panel (a closed panel ignores it — the toast
     * feedback path covers that case).
     */
    public void setCheckLine(String line) {
        if (open()) {
            this.checkLine = line;
        }
    }
}
