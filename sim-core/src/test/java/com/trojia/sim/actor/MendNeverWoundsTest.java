package com.trojia.sim.actor;

import com.trojia.sim.actor.spell.ActiveEffects;
import com.trojia.sim.actor.spell.EffectComponent;
import com.trojia.sim.actor.spell.EffectKind;
import com.trojia.sim.actor.spell.EffectMode;
import com.trojia.sim.actor.spell.SpellDefinition;
import com.trojia.sim.actor.spell.SpellRegistry;
import com.trojia.sim.actor.spell.SpellVerb;
import com.trojia.sim.actor.spell.TargetShape;
import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.world.PackedPos;

import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * THE SIGN OF THE MAGNITUDE IS THE DIRECTION THE BODY MOVES — swept across the whole range a
 * VITALITY component may legally author, on both signs, rather than checked at one worked case.
 *
 * <p><b>What used to happen.</b> {@link ActiveEffects#applyOnce} read
 * {@code Math.max(FLOOR, Math.min(max, target.hp() + magnitude))}, and the addition ran in
 * {@code int} before either clamp could look at it. Nothing bounds a VITALITY magnitude — the
 * vitality floor is precisely what is supposed to make that safe — so a mend authored large
 * wrapped negative on the way in, the clamps then clamped the wrapped number perfectly, and a
 * crafting that toasted "On them: +2000000000 hit points" and "[Linkcraft 40 vs Great Mending:
 * 30% -- THE LINK HOLDS]" put the body on the vitality floor. A heal that wounds while the screen
 * narrates a heal is the loudest version there is of a crafting lying about what it did.
 *
 * <p><b>What this file pins.</b> For every magnitude on the ladder, on both signs: a positive
 * magnitude never lowers hit points and a negative one never raises them, the landed value is
 * exactly the clamp of the honest sum, and the two structural bounds hold at every step (never
 * under {@link ActiveEffects#VITALITY_FLOOR}, never over the body's authored maximum).
 */
final class MendNeverWoundsTest {

    private static final int Z = 10;
    private static final int HERE = PackedPos.pack(50, 50, Z);
    private static final int NEXT_DOOR = PackedPos.pack(51, 50, Z);

    /** Small values, powers of two to the int ceiling, and both extremes. */
    private static final int[] MAGNITUDE_LADDER = magnitudeLadder();

    private static int[] magnitudeLadder() {
        int[] ladder = new int[64];
        int at = 0;
        for (int small = 1; small <= 10; small++) {
            ladder[at++] = small;
        }
        for (int shift = 4; shift <= 30; shift++) {
            ladder[at++] = 1 << shift;
        }
        ladder[at++] = 32_767;
        ladder[at++] = 32_768;
        ladder[at++] = 65_535;
        ladder[at++] = Integer.MAX_VALUE - 1;
        ladder[at++] = Integer.MAX_VALUE;
        int[] trimmed = new int[at];
        System.arraycopy(ladder, 0, trimmed, 0, at);
        java.util.Arrays.sort(trimmed);
        return trimmed;
    }

    // ==================================================================
    // The sweep, through the effect table's own dose path
    // ==================================================================

    /**
     * EVERY MAGNITUDE, BOTH SIGNS, THROUGH THE REAL DOSE PATH. A row is filed and the table's own
     * cadence tick lands exactly one dose through the one function that can write a hit point.
     */
    @Test
    void hitPointsAlwaysMoveTheWayTheSignSaysAtEveryMagnitude() {
        ActorRegistry registry = new ActorRegistry();
        Actor body = spawn(registry, HERE);
        int max = body.stats().hp();
        assertTrue(max > 2, "the sweep needs a body with room to move in both directions");
        short midpoint = (short) (max / 2 + 1);

        for (int sign : new int[] {1, -1}) {
            for (int step = 0; step < MAGNITUDE_LADDER.length; step++) {
                int magnitude = sign * MAGNITUDE_LADDER[step];
                assertDoseLandsHonestly(registry, body, midpoint, magnitude, max);
            }
            // Integer.MIN_VALUE has no positive twin, so it is swept on its own.
            if (sign < 0) {
                assertDoseLandsHonestly(registry, body, midpoint, Integer.MIN_VALUE, max);
            }
        }
    }

    private static void assertDoseLandsHonestly(ActorRegistry registry, Actor body,
            short startingHp, int magnitude, int max) {
        body.setHp(startingHp);
        ActiveEffects effects = new ActiveEffects();
        effects.add(body.id(), EffectKind.VITALITY, EffectMode.OVER_TIME, 0, magnitude, 0L,
                ActiveEffects.OVER_TIME_PERIOD_TICKS);
        short before = body.hp();
        effects.tick(registry, ActiveEffects.OVER_TIME_PERIOD_TICKS);
        short after = body.hp();

        String where = "magnitude " + magnitude + " on a body at " + before + "/" + max;
        long honest = Math.max(ActiveEffects.VITALITY_FLOOR,
                Math.min(max, (long) before + magnitude));
        assertEquals(honest, after, where + ": the landed value is not the clamp of the honest"
                + " sum -- the addition wrapped before the clamps could see it");
        if (magnitude > 0) {
            assertTrue(after >= before,
                    where + ": a MEND took hit points off (" + before + " -> " + after + ")");
        } else {
            assertTrue(after <= before,
                    where + ": a WOUND put hit points on (" + before + " -> " + after + ")");
        }
        assertTrue(after >= ActiveEffects.VITALITY_FLOOR,
                where + ": below the vitality floor (" + after + ")");
        assertTrue(after <= max, where + ": above the body's own maximum (" + after + ")");
    }

    // ==================================================================
    // The same thing through the verb the button drives
    // ==================================================================

    /**
     * THE REGRESSION, END TO END. A mend authored at the int ceiling, cast through the shared
     * verb the craftings bar drives, on a body one hit point off death: it must arrive at FULL
     * health. Before the fix it arrived on the vitality floor while the toast said the link held.
     */
    @Test
    void aMendAuthoredAtTheIntCeilingLandsAtFullHealthNotOnTheFloor() {
        ActorRegistry registry = new ActorRegistry();
        Actor caster = spawn(registry, HERE);
        Actor patient = spawn(registry, NEXT_DOOR);
        int max = patient.stats().hp();
        patient.setHp((short) ActiveEffects.VITALITY_FLOOR);

        SpellDefinition greatMending = new SpellDefinition("great_mending", "Great Mending",
                "linkcraft", 0, 0, 0, TargetShape.TOUCH, 1, 0,
                List.of(new EffectComponent(EffectKind.VITALITY, EffectMode.INSTANT,
                        Integer.MAX_VALUE, 0, 0)));
        MendContext ctx = new MendContext(registry, SpellRegistry.of(List.of(greatMending)));
        ctx.tracks.seedLevel(caster.id(), ctx.tracks.linkcraftRaw(), 100);

        castUntilItWorks(caster, ctx, 0, patient.id());
        assertEquals(max, patient.hp(),
                "a mend the size of the int range must mend, not wound -- this is the exact"
                        + " case where the int addition wrapped and the clamps clamped the wrap");
    }

    /** The mirror: a wound the size of the int range stops at the floor, and never below it. */
    @Test
    void aWoundAuthoredAtTheIntFloorStopsExactlyOnTheVitalityFloor() {
        ActorRegistry registry = new ActorRegistry();
        Actor caster = spawn(registry, HERE);
        Actor victim = spawn(registry, NEXT_DOOR);

        SpellDefinition unmaking = new SpellDefinition("unmaking", "Unmaking", "linkcraft", 0, 0,
                0, TargetShape.TOUCH, 1, 0,
                List.of(new EffectComponent(EffectKind.VITALITY, EffectMode.INSTANT,
                        Integer.MIN_VALUE, 0, 0)));
        MendContext ctx = new MendContext(registry, SpellRegistry.of(List.of(unmaking)));
        ctx.tracks.seedLevel(caster.id(), ctx.tracks.linkcraftRaw(), 100);

        castUntilItWorks(caster, ctx, 0, victim.id());
        assertEquals(ActiveEffects.VITALITY_FLOOR, victim.hp(),
                "the floor is structural, at any magnitude an author can type");
        assertFalse(victim.isDead(),
                "so magic still never touches the ecology or the death feed");
    }

    // ==================================================================
    // helpers
    // ==================================================================

    /** A context with a wired skill table, a real effect table and a spell universe. */
    private static final class MendContext extends NoOpActorContext {
        final SkillTrackRegistry tracks = new SkillTrackRegistry(
                com.trojia.sim.progression.SkillRawsLoader.load(locateRawsDir()));
        final ActiveEffects effects = new ActiveEffects();
        private final SpellRegistry spells;

        MendContext(ActorRegistry registry, SpellRegistry spells) {
            super(registry);
            this.spells = spells;
            tracks.bindActiveEffects(effects);
        }

        @Override
        public SkillTrackRegistry skillTracks() {
            return tracks;
        }

        @Override
        public SpellRegistry spells() {
            return spells;
        }

        @Override
        public ActiveEffects activeEffects() {
            return effects;
        }
    }

    private static Actor spawn(ActorRegistry registry, int cell) {
        return registry.spawn(Serf.TYPE, ActorTestFixtures.statsWithDefer(Serf.TYPE, true), cell);
    }

    /**
     * Presses until the link opens (the {@code SpellcraftTest} idiom — no check is certain).
     * The two craftings here are the most expensive ones that can exist, so they sit on the
     * check's FLOOR by construction and need a long press: 500 attempts at the floor is a
     * deterministic certainty, not a hope.
     */
    private static void castUntilItWorks(Actor caster, MendContext ctx, int spellRaw,
            int targetId) {
        for (int attempt = 0; attempt < 500; attempt++) {
            ctx.setTick(ctx.tick() + 1);
            caster.setCastUntilTick(0L);
            if (SpellVerb.resolveCast(caster, ctx, spellRaw, targetId)) {
                return;
            }
        }
        throw new AssertionError("500 attempts and the link never opened for spell raw "
                + spellRaw);
    }

    private static java.nio.file.Path locateRawsDir() {
        java.nio.file.Path dir = java.nio.file.Path.of("").toAbsolutePath();
        for (int i = 0; i < 6 && dir != null; i++, dir = dir.getParent()) {
            java.nio.file.Path candidate = dir.resolve("content").resolve("raws");
            if (java.nio.file.Files.isDirectory(candidate)) {
                return candidate;
            }
        }
        throw new IllegalStateException("content/raws not found above "
                + java.nio.file.Path.of("").toAbsolutePath());
    }
}
