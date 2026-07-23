package com.trojia.client.inspect;

import com.trojia.client.hud.icons.HudToken;
import com.trojia.client.hud.icons.IconKey;
import com.trojia.client.scenario.IdentityRegistry;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.BarkSelector;
import com.trojia.sim.actor.Barter;
import com.trojia.sim.actor.FactionStandings;
import com.trojia.sim.actor.Home;
import com.trojia.sim.actor.HomeRegistry;
import com.trojia.sim.actor.ItemsLiteEntry;
import com.trojia.sim.actor.ItemsLiteRegistry;
import com.trojia.sim.actor.RelationshipRegistry;
import com.trojia.sim.actor.SkillTrackRegistry;
import com.trojia.sim.actor.faction.FactionRegistry;
import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.job.JobRegistry;
import com.trojia.sim.progression.SkillRegistry;
import com.trojia.sim.world.Dir;
import com.trojia.sim.world.PackedPos;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Pure formatting for the observer's CHARACTER SHEET (Sprint 1 "Click a person, meet a
 * person" — supersedes the debug-flavored {@code InspectorText} layout, keeping its
 * no-staleness contract): given a selected actor and the side-tables, produces the name +
 * epithet header, the one-line bio, and the IDENTITY / NEEDS / SKILLS / TIES sections the
 * panel renders as separate DF blocks. GL-free so the exact content is unit-testable
 * independent of font rendering (the {@code HudText} split, applied to the sheet).
 *
 * <p><b>Presented identity respected (PLAY-MODE-SPEC.md §5.3).</b> The header — name,
 * epithet, bio, and the portrait the renderer pairs with them — resolves through
 * {@code Actor#identity().presentedId()}: a disguised actor's sheet is headed by WHO THE
 * WARD SEES. The IDENTITY section underneath stays the observer's omniscient block (true
 * type, true job, the {@code presents:}/{@code (secret)} tells), so the disguise is
 * legible as a disguise rather than invisible.
 *
 * <p><b>No staleness.</b> Every line is computed from live {@code registry}/side-table
 * reads at call time; nothing is cached across frames. The caller passes the current
 * selected {@code ActorId} each frame and re-fetches — the same contract
 * {@code ActorRenderer} holds.
 */
public final class CharacterSheetText {

    /**
     * Short labels for the five-slot needs vector, indexed by {@code Need#ordinal()}. Public:
     * also used by {@code InspectorRenderer}'s Zelda-II-style segmented need-bar grid, which
     * renders these values as bars inside the NEEDS section panel (2026-07-15 stat-box
     * redesign, carried over from {@code InspectorText}).
     */
    public static final String[] NEED_LABELS = {"HUNGER", "REST", "COIN", "SAFETY", "DUTY"};

    /** One titled sheet section — the renderer draws each in its own DF panel block. */
    public record Section(String title, List<String> lines) {
    }

    private CharacterSheetText() {
    }

    /**
     * Icon-augmented version of the "nothing selected" hint (a mouse-left-click icon and a
     * {@code C} key icon in place of the words) — for the draw call site only;
     * {@link #describe} keeps the plain-text version below for headless testing.
     */
    public static List<HudToken> selectionHintTokens() {
        return List.of(
                HudToken.text("("),
                HudToken.icon(IconKey.MOUSE_LEFT_CLICK),
                HudToken.text(" click an actor to inspect  ·  "),
                HudToken.icon(IconKey.C),
                HudToken.text(" follows selection)"));
    }

    /**
     * Icon-augmented version of the "[FOLLOW]" badge shown while the camera follows the
     * selected actor — a {@code C} key icon in place of the word.
     */
    public static List<HudToken> followBadgeTokens() {
        return List.of(
                HudToken.text("[FOLLOW]  "),
                HudToken.icon(IconKey.C),
                HudToken.text(" to release"));
    }

    /**
     * The sheet's header line: the PRESENTED identity's name + quoted epithet — a disguised
     * actor is headed by who the ward sees (matching the portrait the renderer already
     * resolves from the presented actor).
     */
    public static String nameLine(int selectedId, ActorRegistry registry,
            IdentityRegistry identity) {
        int presentedId = registry.get(selectedId).identity().presentedId();
        return PersonNames.nameWithEpithet(presentedId, registry, identity);
    }

    /**
     * The PRESENTED identity's bio (the NameForge template line, or a notable's authored
     * sentences); {@code ""} when the identity table has no row — the caller skips the line.
     */
    public static String bioLine(int selectedId, ActorRegistry registry,
            IdentityRegistry identity) {
        int presentedId = registry.get(selectedId).identity().presentedId();
        if (presentedId >= 0 && presentedId < identity.size()) {
            return identity.get(presentedId).bio();
        }
        return "";
    }

    /**
     * The IDENTITY section: the observer's omniscient block — true id/type, the Persona
     * {@code presents:} line, true vs presented job with the {@code (secret)} tell, goal +
     * reason (the WHY, ACTORS-SPEC.md §7.2's legibility mandate), vitals, position, home,
     * and a compact carried-items line.
     */
    public static Section identitySection(int selectedId, ActorRegistry registry,
            HomeRegistry homes, JobRegistry jobs, ItemsLiteRegistry items) {
        Actor actor = registry.get(selectedId);
        List<String> lines = new ArrayList<>();
        lines.add("id:     #" + actor.id() + "  " + actor.typeId().key()
                + "  '" + actor.stats().glyph() + "'");
        lines.add("presents: " + presentedTypeLine(actor, registry));

        Job job = actor.jobOrdinal() >= 0 ? jobs.get(actor.jobOrdinal()) : null;
        String trueJob = JobDisplay.trueJobId(job);
        String presented = JobDisplay.presentedJobId(job);
        String secret = JobDisplay.isSecret(job) ? "   (secret)" : "";
        lines.add("job:    " + trueJob + "   presents: " + presented + secret);

        lines.add("goal:   " + actor.goalState() + "   progress: " + actor.goalProgress());
        lines.add("reason: " + reason(actor));
        lines.add("hp: " + actor.hp() + "   status: 0x"
                + Integer.toHexString(actor.statusBits() & 0xFFFF)
                + "   facing: " + facing(actor.facing()));
        lines.add("pos:    " + xyz(actor.cell()));
        lines.add(homeLine(actor, homes, registry));
        lines.add(carriesLine(actor, items));
        return new Section("IDENTITY", lines);
    }

    /**
     * The SKILLS section, read live off the Sim team's {@link SkillTrackRegistry} (Sprint 1
     * "the character sheet comes alive"): every nonzero skill as {@code "DisplayName level"},
     * best first (ties by raw id — deterministic); {@code "(unschooled)"} when the actor has
     * levelled nothing or the registry is unwired (the degraded placeholder behind the same
     * interface, per the sprint plan).
     *
     * <p>Skills are keyed on the SELECTED (true) body, never the presented id — XP lands on
     * the body that did the deed; only SOCIAL reads follow the disguise.
     */
    public static Section skillsSection(int selectedId, SkillTrackRegistry tracks) {
        List<String> lines = new ArrayList<>();
        if (tracks.isWired()) {
            SkillRegistry skills = tracks.skills();
            List<int[]> rows = new ArrayList<>();
            for (int raw = 0; raw < skills.size(); raw++) {
                int level = tracks.level(selectedId, raw);
                if (level > 0) {
                    rows.add(new int[] {raw, level});
                }
            }
            rows.sort((a, b) -> a[1] != b[1]
                    ? Integer.compare(b[1], a[1]) : Integer.compare(a[0], b[0]));
            for (int[] row : rows) {
                lines.add(skills.get(row[0]).displayName() + " " + row[1]);
            }
        }
        if (lines.isEmpty()) {
            lines.add("(unschooled)");
        }
        return new Section("SKILLS", lines);
    }

    /**
     * The STANDINGS section (Sprint 2 item 2, the reputation pane): the sheet-owner's
     * standing with every faction the raws know, one row each —
     * {@code The Watch        -32  [cold]} — followed by the disposition CONTEXT those
     * numbers buy at the ward's counters (the {@link Barter} surcharge/refusal bands, so
     * the sheet explains the prices the actor is about to be quoted).
     *
     * <p>Standings key on the PRESENTED id (every ledger read does — the Persona rule):
     * a disguised actor's sheet shows the standings of the face it wears, matching the
     * header above it. The attitude words are {@link BarkSelector}'s own published
     * thresholds, so sheet, barks and barter all speak one vocabulary. Renders a single
     * "(no ledgers)" line when the fixture wires no faction universe.
     */
    public static Section standingsSection(int selectedId, ActorRegistry registry,
            FactionStandings standings) {
        List<String> lines = new ArrayList<>();
        if (!standings.isWired()) {
            lines.add("(the ward keeps no ledgers here)");
            return new Section("STANDINGS", lines);
        }
        int presentedId = registry.get(selectedId).identity().presentedId();
        FactionRegistry factions = standings.factions();
        for (int f = 0; f < factions.size(); f++) {
            int value = standings.standingOf(presentedId, f);
            lines.add(padded(factions.get(f).displayName()) + signed(value)
                    + "  [" + attitudeWord(value) + "]");
        }
        int watch = standings.watchStanding(presentedId);
        if (watch <= Barter.REFUSAL_WATCH_STANDING) {
            lines.add("every counter refuses this face");
        } else if (watch < 0 && -watch / Barter.WATCH_PER_SURCHARGE > 0) {
            lines.add("counters surcharge this face +"
                    + (-watch / Barter.WATCH_PER_SURCHARGE));
        }
        return new Section("STANDINGS", lines);
    }

    /** {@link BarkSelector}'s attitude buckets, as the sheet's standing words. */
    static String attitudeWord(int standing) {
        if (standing <= BarkSelector.HOSTILE_STANDING) {
            return "hostile";
        }
        if (standing <= BarkSelector.COLD_STANDING) {
            return "cold";
        }
        if (standing >= BarkSelector.WARM_STANDING) {
            return "warm";
        }
        return "neutral";
    }

    private static String padded(String name) {
        StringBuilder out = new StringBuilder(name);
        while (out.length() < 18) {
            out.append(' ');
        }
        return out.toString();
    }

    private static String signed(int value) {
        return value >= 0 ? "+" + value : Integer.toString(value);
    }

    /**
     * The TIES section — relationships as PEOPLE (the sprint's "Wife — Maera (shopkeeper)"
     * ask): {@code Household -- Ceffa Quayward (shopkeeper.chandlery)} when the other soul
     * has a forged name, with the pre-names {@code "HOUSEHOLD -> #12 (serf)"} debug style as
     * the deliberate fallback for un-forged fixtures. The other soul's job renders as its
     * PRESENTED job — covers read as covers (§1.1).
     */
    public static Section tiesSection(int selectedId, ActorRegistry registry,
            RelationshipRegistry relationships, JobRegistry jobs, IdentityRegistry identity) {
        List<RelationshipRegistry.View> views = relationships.relationshipsOf(selectedId);
        List<String> lines = new ArrayList<>();
        if (views.isEmpty()) {
            lines.add("(no ties)");
        }
        for (RelationshipRegistry.View view : views) {
            int otherId = view.otherId();
            if (otherId < identity.size() && identity.get(otherId).named()) {
                Actor other = registry.get(otherId);
                Job otherJob = other.jobOrdinal() >= 0 ? jobs.get(other.jobOrdinal()) : null;
                String jobId = JobDisplay.presentedJobId(otherJob);
                String job = JobDisplay.NONE_LABEL.equals(jobId) ? "" : " (" + jobId + ")";
                lines.add(titleCase(view.kindAsSeen().name()) + " -- "
                        + PersonNames.nameWithEpithet(otherId, registry, identity) + job);
            } else {
                lines.add(view.kindAsSeen() + " -> #" + otherId
                        + " (" + registry.get(otherId).typeId().key() + ")");
            }
        }
        return new Section("TIES", lines);
    }

    /**
     * The whole sheet as flat text lines — header, bio, then every section behind a
     * {@code "-- TITLE --"} marker (NEEDS as a compact numeric line here; the renderer
     * draws bars instead). The headless/unit-test surface; a single "nothing selected"
     * hint when {@code selectedId} is {@link Actor#NONE}.
     */
    public static List<String> describe(int selectedId, ActorRegistry registry,
            HomeRegistry homes, RelationshipRegistry relationships, JobRegistry jobs,
            ItemsLiteRegistry items, IdentityRegistry identity, SkillTrackRegistry tracks,
            FactionStandings standings) {
        List<String> lines = new ArrayList<>();
        if (selectedId == Actor.NONE) {
            lines.add("(click an actor to inspect  ·  C follows selection)");
            return lines;
        }
        lines.add(nameLine(selectedId, registry, identity));
        String bio = bioLine(selectedId, registry, identity);
        if (!bio.isBlank()) {
            lines.add(bio);
        }
        append(lines, identitySection(selectedId, registry, homes, jobs, items));
        lines.add(marker("NEEDS"));
        lines.add(needsLine(registry.get(selectedId)));
        append(lines, skillsSection(selectedId, tracks));
        append(lines, standingsSection(selectedId, registry, standings));
        append(lines, tiesSection(selectedId, registry, relationships, jobs, identity));
        return lines;
    }

    private static void append(List<String> lines, Section section) {
        lines.add(marker(section.title()));
        lines.addAll(section.lines());
    }

    private static String marker(String title) {
        return "-- " + title + " --";
    }

    /** The five needs as one compact line, for {@link #describe}'s headless surface. */
    private static String needsLine(Actor actor) {
        short[] needs = actor.needsSnapshot();
        StringBuilder out = new StringBuilder("H/R/C/S/D: ");
        for (int i = 0; i < needs.length; i++) {
            if (i > 0) {
                out.append('/');
            }
            out.append(needs[i]);
        }
        return out.toString();
    }

    /**
     * The Persona seam (PLAY-MODE-SPEC.md §5.3, distinct from the Job-cover "presents:" line
     * — that one is {@code JobDisplay}'s type-level cover, this is {@code Persona}'s
     * ActorId-shaped disguise): {@code "(self)"} when not disguised, else the presented
     * actor's type key — proving {@code setActAs} genuinely changes what the panel shows.
     */
    private static String presentedTypeLine(Actor actor, ActorRegistry registry) {
        if (!actor.identity().isDisguised()) {
            return "(self)";
        }
        return registry.get(actor.identity().presentedId()).typeId().key();
    }

    private static String reason(Actor actor) {
        return actor.lastReasonCode() == null ? "(none yet)" : actor.lastReasonCode().name();
    }

    private static String homeLine(Actor actor, HomeRegistry homes, ActorRegistry registry) {
        int homeId = actor.homeId();
        if (homeId == Actor.NONE) {
            return "home:   (none)";
        }
        Home home = homes.get(homeId);
        int housemates = Math.max(0, homes.occupantsOf(homeId, registry).size() - 1);
        boolean atHome = actor.cell() == home.homeCell();
        return "home:   #" + homeId + " @ " + xyz(home.homeCell())
                + "   housemates: " + housemates + (atHome ? "   [home now]" : "");
    }

    /** Carried items compacted to one sheet line: {@code carries: kind 5 x1, kind 2 x3}. */
    private static String carriesLine(Actor actor, ItemsLiteRegistry items) {
        List<ItemsLiteEntry> carried = items.carriedBy(actor.id());
        if (carried.isEmpty()) {
            return "carries: (nothing)";
        }
        StringBuilder out = new StringBuilder("carries: ");
        for (int i = 0; i < carried.size(); i++) {
            if (i > 0) {
                out.append(", ");
            }
            out.append("kind ").append(carried.get(i).kindId())
                    .append(" x").append(carried.get(i).quantity());
        }
        return out.toString();
    }

    private static String titleCase(String enumName) {
        return enumName.charAt(0) + enumName.substring(1).toLowerCase(Locale.ROOT);
    }

    private static String facing(byte facing) {
        Dir[] dirs = Dir.values();
        return (facing >= 0 && facing < dirs.length) ? dirs[facing].name() : ("?" + facing);
    }

    private static String xyz(int cell) {
        return "(" + PackedPos.x(cell) + "," + PackedPos.y(cell) + "," + PackedPos.z(cell) + ")";
    }
}
