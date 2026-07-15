package com.trojia.client.inspect;

import com.trojia.client.hud.icons.HudToken;
import com.trojia.client.hud.icons.IconKey;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.ActorTypeStats;
import com.trojia.sim.actor.Home;
import com.trojia.sim.actor.HomeRegistry;
import com.trojia.sim.actor.ItemsLiteEntry;
import com.trojia.sim.actor.ItemsLiteRegistry;
import com.trojia.sim.actor.Need;
import com.trojia.sim.actor.RelationshipRegistry;
import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.job.JobRegistry;
import com.trojia.sim.world.Dir;
import com.trojia.sim.world.PackedPos;

import java.util.ArrayList;
import java.util.List;

/**
 * Pure formatting for the observer's selection inspector panel (ACTORS-SPEC.md §7.2, the
 * legibility acceptance surface): given a selected actor and the side-tables, produces the
 * readable text block the panel renders — id, type, true/presented job, goal + reason (the
 * WHY), the five needs, position, home + housemates, inventory and relationships. GL-free
 * so the exact content is unit-testable independent of font rendering (the {@code HudText}
 * split, applied to the inspector).
 *
 * <p><b>No staleness.</b> Every line is computed from live {@code registry}/side-table
 * reads at call time; nothing is cached across frames. The caller passes the current
 * selected {@code ActorId} each frame and re-fetches — the same contract
 * {@code ActorRenderer} holds.
 */
public final class InspectorText {

    /**
     * Short labels for the five-slot needs vector, indexed by {@link Need#ordinal()}. Public:
     * also used by {@code InspectorRenderer}'s Zelda-II-style segmented need-bar grid, which
     * renders these values as bars instead of the plain-number lines this class used to emit
     * (2026-07-15 stat-box redesign).
     */
    public static final String[] NEED_LABELS = {"HUNGER", "REST", "COIN", "SAFETY", "DUTY"};

    private InspectorText() {
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
     * The panel's text lines for {@code selectedId}, or a single "nothing selected" hint
     * when it is {@link Actor#NONE}. One entry per rendered line (the caller draws them
     * top-down in a screen corner).
     */
    public static List<String> describe(int selectedId, ActorRegistry registry, HomeRegistry homes,
            RelationshipRegistry relationships, JobRegistry jobs, ItemsLiteRegistry items) {
        List<String> lines = new ArrayList<>();
        if (selectedId == Actor.NONE) {
            lines.add("(click an actor to inspect  ·  C follows selection)");
            return lines;
        }
        Actor actor = registry.get(selectedId);
        ActorTypeStats stats = actor.stats();

        lines.add("ACTOR #" + actor.id() + "  " + stats.displayName() + "  '" + stats.glyph() + "'");
        lines.add("type:   " + actor.typeId().key());
        lines.add("presents: " + presentedTypeLine(actor, registry));

        Job job = actor.jobOrdinal() >= 0 ? jobs.get(actor.jobOrdinal()) : null;
        String trueJob = JobDisplay.trueJobId(job);
        String presented = JobDisplay.presentedJobId(job);
        String secret = JobDisplay.isSecret(job) ? "   (secret)" : "";
        lines.add("job:    " + trueJob + "   presents: " + presented + secret);

        lines.add("goal:   " + actor.goalState() + "   progress: " + actor.goalProgress());
        lines.add("reason: " + reason(actor));
        lines.add("hp: " + actor.hp() + "   status: 0x" + Integer.toHexString(actor.statusBits() & 0xFFFF)
                + "   facing: " + facing(actor.facing()));

        // Needs are rendered by the caller as the Zelda-II-style segmented bar grid
        // (InspectorRenderer#drawNeedsBarGrid), not as plain-number lines here — this class
        // stays the GL-free text formatter for everything else in the panel.

        lines.add("pos:    " + xyz(actor.cell()));
        lines.add(homeLine(actor, homes, registry));

        appendInventory(lines, actor, items);
        appendRelationships(lines, actor, registry, relationships);
        return lines;
    }

    /**
     * The Persona seam (PLAY-MODE-SPEC.md §5.3, distinct from the Job-cover "presents:" line
     * above — that one is {@code JobDisplay}'s type-level cover, this is {@code Persona}'s
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

    private static void appendInventory(List<String> lines, Actor actor, ItemsLiteRegistry items) {
        List<ItemsLiteEntry> carried = items.carriedBy(actor.id());
        lines.add("inventory (" + carried.size() + "):");
        if (carried.isEmpty()) {
            lines.add("  (empty)");
            return;
        }
        for (ItemsLiteEntry entry : carried) {
            lines.add("  kind " + entry.kindId() + " x" + entry.quantity());
        }
    }

    private static void appendRelationships(List<String> lines, Actor actor, ActorRegistry registry,
            RelationshipRegistry relationships) {
        List<RelationshipRegistry.View> views = relationships.relationshipsOf(actor.id());
        lines.add("relationships (" + views.size() + "):");
        if (views.isEmpty()) {
            lines.add("  (none)");
            return;
        }
        for (RelationshipRegistry.View view : views) {
            String otherType = registry.get(view.otherId()).typeId().key();
            lines.add("  " + view.kindAsSeen() + " -> #" + view.otherId() + " (" + otherType + ")");
        }
    }

    private static String facing(byte facing) {
        Dir[] dirs = Dir.values();
        return (facing >= 0 && facing < dirs.length) ? dirs[facing].name() : ("?" + facing);
    }

    private static String xyz(int cell) {
        return "(" + PackedPos.x(cell) + "," + PackedPos.y(cell) + "," + PackedPos.z(cell) + ")";
    }
}
