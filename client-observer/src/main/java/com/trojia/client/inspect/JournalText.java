package com.trojia.client.inspect;

import com.trojia.client.scenario.IdentityRegistry;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.DailyRhythm;
import com.trojia.sim.actor.quest.QuestLog;
import com.trojia.sim.actor.quest.QuestRegistry;

import java.util.ArrayList;
import java.util.List;

/**
 * Pure content for the JOURNAL pane (Sprint 3 "The Vanished Clerk"): given the bake-bound
 * {@link QuestRegistry} and the live {@link QuestLog}, produces per quest entry the three
 * authored blocks — the title + owner line, the current OBJECTIVE, and THE STORY SO FAR
 * (each completed stage's authored log line stamped with its completion day, oldest
 * first). GL-free so the exact journal is unit-testable ({@code CharacterSheetText}'s
 * split); {@code JournalRenderer} only wraps and draws these lines.
 *
 * <p><b>No staleness.</b> Every line is computed from live {@code QuestLog} reads at call
 * time; nothing is cached across frames (the sheet's contract).
 *
 * <p><b>The owner is a PERSON.</b> The story belongs to the TRUE body that bound the quest
 * ({@code first_talker}), named through {@link PersonNames} — {@code "The Vanished Clerk -
 * Ottavan Crell's story"} style — with {@code "(no one has taken this up)"} while unbound.
 * Day stamps use the HUD clock's own day arithmetic ({@code tick / DailyRhythm.DAY + 1}).
 */
public final class JournalText {

    /** Shown when the booted fixture bakes no quests at all (compound, tavern). */
    public static final String EMPTY_JOURNAL = "(the journal is empty)";

    /** The STORY block's placeholder before any stage has completed. */
    public static final String NOTHING_YET = "(nothing yet)";

    /** The owner line's placeholder while {@code first_talker} has bound nobody. */
    public static final String UNCLAIMED = "(no one has taken this up)";

    private JournalText() {
    }

    /**
     * The whole journal as flat text lines, every quest entry in ascending order:
     * {@code "<title> - <owner>'s story"}, the {@code -- OBJECTIVE --} block (the current
     * stage's authored objective), and the {@code -- THE STORY SO FAR --} block (completed
     * stages' log lines in completion-tick order, stamped {@code "Day N - "}). Entries are
     * separated by one blank line; a quest-less fixture reads {@link #EMPTY_JOURNAL}.
     */
    public static List<String> lines(QuestRegistry quests, QuestLog log,
            ActorRegistry registry, IdentityRegistry identity) {
        List<String> lines = new ArrayList<>();
        if (log.entryCount() == 0) {
            lines.add(EMPTY_JOURNAL);
            return lines;
        }
        for (int e = 0; e < log.entryCount(); e++) {
            if (e > 0) {
                lines.add("");
            }
            appendEntry(lines, quests, log, e, registry, identity);
        }
        return lines;
    }

    private static void appendEntry(List<String> lines, QuestRegistry quests, QuestLog log,
            int e, ActorRegistry registry, IdentityRegistry identity) {
        int q = log.questOrdinalOf(e);
        int owner = log.ownerOf(e);
        String whose = owner == Actor.NONE ? UNCLAIMED
                : PersonNames.fullNameOf(owner, registry, identity) + "'s story";
        lines.add(quests.questTitle(q) + " - " + whose);

        lines.add(marker("OBJECTIVE"));
        lines.add(quests.objective(q, log.stageOf(e)));

        lines.add(marker("THE STORY SO FAR"));
        List<long[]> completed = completedInOrder(quests, log, e, q);
        if (completed.isEmpty()) {
            lines.add(NOTHING_YET);
        }
        for (long[] row : completed) {
            lines.add("Day " + (row[0] / DailyRhythm.DAY + 1) + " - "
                    + quests.logLine(q, (int) row[1]));
        }
    }

    /** Completed stages as {@code {completedTick, stageOrdinal}}, completion order. */
    private static List<long[]> completedInOrder(QuestRegistry quests, QuestLog log, int e,
            int q) {
        List<long[]> rows = new ArrayList<>();
        for (int s = 0; s < quests.stageCount(q); s++) {
            long at = log.completedTickOf(e, s);
            if (at >= 0) {
                rows.add(new long[] {at, s});
            }
        }
        // Completion ticks are unique (one advance per entry per tick), so this order is
        // total and deterministic: the story reads oldest first.
        rows.sort((a, b) -> Long.compare(a[0], b[0]));
        return rows;
    }

    private static String marker(String title) {
        return "-- " + title + " --";
    }
}
