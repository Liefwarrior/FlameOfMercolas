package com.trojia.client.inspect;

/**
 * The population feed's view filter (Sprint 5 "the torrent"): one key cycles
 * {@code ALL -> GROWTH -> CRIME -> QUESTS -> ALL}, so the observer can pull one lane out
 * of the mixed feed without losing the others (they stay in the buffer). Pure state
 * vocabulary — {@link InspectorState} holds the current value, the renderer passes
 * {@link #only()} to {@link EventLog#recentNewestFirst(int, EventLog.Channel)}.
 */
public enum FeedFilter {

    ALL(null),
    GROWTH(EventLog.Channel.GROWTH),
    CRIME(EventLog.Channel.CRIME),
    QUESTS(EventLog.Channel.QUEST);

    private final EventLog.Channel only;

    FeedFilter(EventLog.Channel only) {
        this.only = only;
    }

    /** The single channel this filter shows, or {@code null} for every channel. */
    public EventLog.Channel only() {
        return only;
    }

    /** The next filter in the cycle (wraps back to {@link #ALL}). */
    public FeedFilter next() {
        FeedFilter[] values = values();
        return values[(ordinal() + 1) % values.length];
    }
}
