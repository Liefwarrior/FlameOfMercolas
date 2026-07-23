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
    /** The numbered topic rows offered with the open exchange (S4 item 2); never null. */
    private java.util.List<TalkTopics.Topic> topics = java.util.List.of();

    /** Whether the speech panel is up. */
    public boolean open() {
        return exchange != null;
    }

    /** The frozen exchange the panel shows; {@code null} while closed. */
    public TalkText.Exchange exchange() {
        return exchange;
    }

    /** The topic rows the open panel offers (empty for un-storied souls / closed panel). */
    public java.util.List<TalkTopics.Topic> topics() {
        return topics;
    }

    /** Opens (or re-greets) the panel on a fresh exchange; clears any stale check line.
     * The pre-topics overload — offers no topic rows (test doubles, degraded callers). */
    public void open(TalkText.Exchange exchange) {
        open(exchange, java.util.List.of());
    }

    /** Opens (or re-greets) the panel with its topic rows (S4); clears the check line. */
    public void open(TalkText.Exchange exchange, java.util.List<TalkTopics.Topic> topics) {
        this.exchange = exchange;
        this.topics = java.util.List.copyOf(topics);
        this.checkLine = null;
    }

    /**
     * Replaces the shown exchange with an asked topic's line (S4 item 2), keeping the
     * topic rows so the player can keep asking; clears the check line. A no-op while
     * closed (a stale scripted ask cannot resurrect a dropped panel).
     */
    public void setExchange(TalkText.Exchange exchange) {
        if (open()) {
            this.exchange = exchange;
            this.checkLine = null;
        }
    }

    /** Closes the panel. */
    public void close() {
        this.exchange = null;
        this.topics = java.util.List.of();
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
