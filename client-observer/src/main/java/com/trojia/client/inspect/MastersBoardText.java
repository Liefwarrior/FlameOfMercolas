package com.trojia.client.inspect;

import com.trojia.client.scenario.IdentityRegistry;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.SkillTrackRegistry;

import java.util.ArrayList;
import java.util.List;

/**
 * Pure formatting for the WARD MASTERS board (Sprint 5 item 3, "population growth made
 * legible"): per skill, the district's best by name and the count of souls at or past the
 * adept band; then the top climbers since dawn. GL-free — the renderer draws these lines
 * in the journal pane's DF panel; content recomputes live on every open frame (the
 * no-staleness contract; 692 x 18 level reads is trivial).
 *
 * <p><b>Deterministic ordering.</b> Skills list raw-ascending (the registry's canonical
 * key-alphabetical order); a best-of tie breaks to the LOWEST actor id; climbers order by
 * climb descending then actor id ascending.
 *
 * <p><b>Presented identity respected.</b> The board is a ward-facing surface: the best
 * soul is named by the face the ward sees ({@code presentedId} through
 * {@link PersonNames}), while the LEVEL is the true body's — the ward knows the work, not
 * the paperwork.
 *
 * <p><b>Band vocabulary.</b> {@link #ADEPT_LEVEL}/{@link #MASTER_LEVEL} mirror the World
 * lane's frozen {@code mastery.<skill>.<band>} bark bands (novice 1-9, adept 10-29,
 * master 30+) so board, barks and gazetteer speak one language.
 */
public final class MastersBoardText {

    /** First level of the adept band (the World lane's frozen bark-band contract). */
    public static final int ADEPT_LEVEL = 10;
    /** First level of the master band (same frozen contract). */
    public static final int MASTER_LEVEL = 30;
    /** How many climbers-since-dawn rows the board shows. */
    public static final int CLIMBERS_SHOWN = 3;

    private static final int NAME_COLUMN = 14;

    private MastersBoardText() {
    }

    /** The whole board as flat text lines — the renderer's (and the tests') surface. */
    public static List<String> lines(SkillTrackRegistry tracks, ActorRegistry registry,
            IdentityRegistry identity, MastersBoardSnapshot snapshot) {
        List<String> lines = new ArrayList<>();
        lines.add("The Ward's Masters");
        lines.add("");
        if (!tracks.isWired()) {
            lines.add("(the ward keeps no craft rolls here)");
            return lines;
        }
        for (int raw = 0; raw < tracks.skills().size(); raw++) {
            lines.add(skillRow(raw, tracks, registry, identity));
        }
        lines.add("");
        lines.add("-- CLIMBERS SINCE DAWN --");
        lines.addAll(climberRows(tracks, registry, identity, snapshot));
        return lines;
    }

    /** One craft row: {@code "Seacraft      Capt. Vane 50   (7 adept+)"} or unschooled. */
    private static String skillRow(int raw, SkillTrackRegistry tracks,
            ActorRegistry registry, IdentityRegistry identity) {
        int bestId = -1;
        int bestLevel = 0;
        int adepts = 0;
        for (int i = 0; i < registry.size(); i++) {
            int level = tracks.level(i, raw);
            if (level > bestLevel) {
                bestLevel = level;
                bestId = i;
            }
            if (level >= ADEPT_LEVEL) {
                adepts++;
            }
        }
        StringBuilder out = new StringBuilder(pad(tracks.skills().get(raw).displayName()));
        if (bestId < 0) {
            return out.append("(unschooled)").toString();
        }
        out.append(wardName(bestId, registry, identity)).append(' ').append(bestLevel);
        if (adepts > 0) {
            out.append("   (").append(adepts).append(" adept+)");
        }
        return out.toString();
    }

    /** Top climbers since the dawn baseline, or one "(no growth yet today)" line. */
    private static List<String> climberRows(SkillTrackRegistry tracks,
            ActorRegistry registry, IdentityRegistry identity, MastersBoardSnapshot snapshot) {
        List<int[]> climbers = new ArrayList<>(); // {actorId, climb}
        for (int i = 0; i < registry.size(); i++) {
            int climb = snapshot.climbSinceDawn(i);
            if (climb > 0) {
                climbers.add(new int[] {i, climb});
            }
        }
        climbers.sort((a, b) -> a[1] != b[1]
                ? Integer.compare(b[1], a[1]) : Integer.compare(a[0], b[0]));
        if (climbers.isEmpty()) {
            return List.of("(no growth yet today)");
        }
        List<String> rows = new ArrayList<>();
        for (int i = 0; i < Math.min(CLIMBERS_SHOWN, climbers.size()); i++) {
            int[] row = climbers.get(i);
            rows.add(wardName(row[0], registry, identity) + "  +" + row[1]
                    + (row[1] == 1 ? " level" : " levels"));
        }
        return rows;
    }

    /** The ward-facing name: the PRESENTED identity's, per the Persona rule. */
    private static String wardName(int actorId, ActorRegistry registry,
            IdentityRegistry identity) {
        return PersonNames.fullNameOf(registry.get(actorId).identity().presentedId(),
                registry, identity);
    }

    private static String pad(String name) {
        StringBuilder out = new StringBuilder(name);
        while (out.length() < NAME_COLUMN) {
            out.append(' ');
        }
        out.append(' ');
        return out.toString();
    }
}
