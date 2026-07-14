package com.trojia.client.face;

import com.trojia.client.art.ArtMappingException;
import com.trojia.client.face.FaceComposition.PlacedPart;
import com.trojia.client.sprite.SpriteIndex;
import com.trojia.client.sprite.SpriteRef;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Set;

/**
 * FaceGen LITE, amended to tile parts (unified art spec §4; FACES-SPEC §3–§4 composition
 * logic retained verbatim, medium amended from ASCII bands to 16 px-grid sprite parts).
 * GL-free and pure: a face is a function of {@code (worldSeed, actorId, archetype pools)}
 * — no tick term, no state across calls, nothing serialized.
 *
 * <p><b>Determinism (binding, FACES-SPEC §4.2):</b> the pinned SplitMix64 finalizer
 * ({@link SpriteIndex#mix64}), {@code base = mix64(mix64(worldSeed ^ FACEGEN_SALT) +
 * actorId)}, {@code draw(k) = mix64(base + k)}. Draws are archetype-independent — the
 * archetype shapes the <em>pool</em>, never the draw value. Weighted pick: pool in
 * ascending id order (ASCII ordinal), integer cumulative weights,
 * {@code Long.remainderUnsigned}; no floats anywhere in facegen.
 *
 * <p><b>Draw-index map (append-only — never renumber):</b> {@code 0} headwear class ·
 * {@code 1} base · {@code 2} eyes · {@code 3} brow · {@code 4} nose · {@code 5} mouth ·
 * {@code 6} hair · {@code 7} headwear · {@code 8} hair-color class · {@code 9} scar count
 * (weights 13/2/1 for 0/1/2) · {@code 10+2i / 11+2i} scar pick / anchor. Future features
 * append &ge; 14. Draw-value independence means gating draws (0, 8) may be consumed
 * "after" the picks they filter — order is irrelevant in a stateless map.
 *
 * <p><b>Class gating (spec §4.3):</b> the k=0 headwear class filters the
 * {@code face_headwear} pool by {@code hw_*} tag; {@code BARE} composes hair instead. The
 * hair pick is <em>evaluated but not composed</em> under headwear (fixed draws; occlusion
 * replaced the ASCII {@code gear:} lists). The k=8 hair color filters {@code face_hair},
 * {@code face_brow} and bearded {@code face_mouth}: a part carrying any {@code hair_*} tag
 * is eligible iff it matches; untagged parts are always eligible.
 *
 * <p><b>Scars (spec §4.4):</b> composed at layer 2 at a fixed anchor-table position; the
 * ASCII blank-cell drop rule is RETIRED — every rolled scar is composed and hair/headwear
 * may legitimately hide it.
 *
 * <p><b>Named faces (spec §4.7):</b> an index entry tagged {@code face_named} with id
 * {@code face_named_<npcId>} wins over the generator with zero draws consumed.
 */
public final class FaceGen {

    /** {@code "FACEGEN1"} — FACES-SPEC §4.2's pinned salt, retained (placeholder). */
    public static final long FACEGEN_SALT = 0x4641434547454E31L;

    // Draw-index map (append-only).
    private static final int K_HEADWEAR_CLASS = 0;
    private static final int K_BASE = 1;
    private static final int K_EYES = 2;
    private static final int K_BROW = 3;
    private static final int K_NOSE = 4;
    private static final int K_MOUTH = 5;
    private static final int K_HAIR = 6;
    private static final int K_HEADWEAR = 7;
    private static final int K_HAIR_COLOR = 8;
    private static final int K_SCAR_COUNT = 9;
    private static final int K_SCAR_FIRST = 10;

    /** Scar anchor table (spec §4.4, fixed, append-only, placeholder): x px, y px. */
    private static final int[] SCAR_ANCHOR_X = {4, 28, 16, 16};
    private static final int[] SCAR_ANCHOR_Y = {22, 22, 10, 36};

    // Slot anchors (spec §4.1, placeholder pending golden bless).
    private static final int MOUTH_X = 8;
    private static final int MOUTH_Y = 30;
    private static final int NOSE_X = 16;
    private static final int NOSE_Y = 22;
    private static final int EYES_X = 8;
    private static final int EYES_Y = 16;
    private static final int BROW_X = 8;
    private static final int BROW_Y = 8;

    private static final Set<String> Q_BASE = Set.of("face_base");
    private static final Set<String> Q_EYES = Set.of("face_eyes");
    private static final Set<String> Q_BROW = Set.of("face_brow");
    private static final Set<String> Q_NOSE = Set.of("face_nose");
    private static final Set<String> Q_MOUTH = Set.of("face_mouth");
    private static final Set<String> Q_HAIR = Set.of("face_hair");
    private static final Set<String> Q_SCAR = Set.of("face_scar");
    private static final String NAMED_TAG = "face_named";
    private static final String NAMED_ID_PREFIX = "face_named_";
    private static final String HAIR_TAG_PREFIX = "hair_";

    private final SpriteIndex parts;
    private final FaceArchetypes archetypes;

    public FaceGen(SpriteIndex parts, FaceArchetypes archetypes) {
        this.parts = Objects.requireNonNull(parts, "parts");
        this.archetypes = Objects.requireNonNull(archetypes, "archetypes");
    }

    /**
     * Composes the generated face for {@code (worldSeed, actorId)} under
     * {@code archetypeId}'s pools. Pure and stateless; identical inputs give identical
     * compositions on any machine.
     *
     * @throws IllegalArgumentException if the archetype id is unknown
     */
    public FaceComposition compose(long worldSeed, long actorId, String archetypeId) {
        FaceArchetype archetype = archetypes.archetype(archetypeId);
        long base = SpriteIndex.mix64(SpriteIndex.mix64(worldSeed ^ FACEGEN_SALT) + actorId);

        HeadwearClass headwear = pickHeadwearClass(archetype, draw(base, K_HEADWEAR_CLASS));
        HairColor hairColor = pickHairColor(draw(base, K_HAIR_COLOR));

        List<PlacedPart> placed = new ArrayList<>();
        placed.add(new PlacedPart(
                pick(archetype, Q_BASE, null, draw(base, K_BASE), "face_base"), 0, 0));

        int scarRoll = (int) Long.remainderUnsigned(draw(base, K_SCAR_COUNT), 16);
        int scarCount = scarRoll < 13 ? 0 : (scarRoll < 15 ? 1 : 2);
        for (int i = 0; i < scarCount; i++) {
            SpriteRef scar = pick(archetype, Q_SCAR, null,
                    draw(base, K_SCAR_FIRST + 2 * i), "face_scar");
            int anchor = (int) Long.remainderUnsigned(
                    draw(base, K_SCAR_FIRST + 1 + 2 * i), SCAR_ANCHOR_X.length);
            placed.add(new PlacedPart(scar, SCAR_ANCHOR_X[anchor], SCAR_ANCHOR_Y[anchor]));
        }

        placed.add(new PlacedPart(
                pick(archetype, Q_MOUTH, hairColor, draw(base, K_MOUTH), "face_mouth"),
                MOUTH_X, MOUTH_Y));
        placed.add(new PlacedPart(
                pick(archetype, Q_NOSE, null, draw(base, K_NOSE), "face_nose"),
                NOSE_X, NOSE_Y));
        placed.add(new PlacedPart(
                pick(archetype, Q_EYES, null, draw(base, K_EYES), "face_eyes"),
                EYES_X, EYES_Y));
        placed.add(new PlacedPart(
                pick(archetype, Q_BROW, hairColor, draw(base, K_BROW), "face_brow"),
                BROW_X, BROW_Y));

        if (headwear == HeadwearClass.BARE) {
            placed.add(new PlacedPart(
                    pick(archetype, Q_HAIR, hairColor, draw(base, K_HAIR), "face_hair"), 0, 0));
        } else {
            placed.add(new PlacedPart(
                    pick(archetype, Set.of("face_headwear", headwear.hwTag()), null,
                            draw(base, K_HEADWEAR), "face_headwear"), 0, 0));
        }
        return new FaceComposition(placed);
    }

    /**
     * The hand-authored portrait for a named NPC ({@code face_named_<npcId>}), or
     * {@code null} when none is authored. Zero draws consumed — named faces never perturb
     * the generator (FACES-SPEC §2 lookup rule, retained).
     */
    public FaceComposition composeNamed(String npcId) {
        SpriteRef named = npcId == null ? null : parts.byId(NAMED_ID_PREFIX + npcId);
        if (named == null || !named.tags().contains(NAMED_TAG)) {
            return null;
        }
        return new FaceComposition(List.of(new PlacedPart(named, 0, 0)));
    }

    /**
     * The face for an actor: the named portrait when {@code npcIdOrNull} has one,
     * otherwise the generated composition.
     */
    public FaceComposition compose(long worldSeed, long actorId, String archetypeId,
                                   String npcIdOrNull) {
        FaceComposition named = composeNamed(npcIdOrNull);
        return named != null ? named : compose(worldSeed, actorId, archetypeId);
    }

    /**
     * Proves every pool this generator can ever consult is non-empty: every archetype
     * &times; every reachable headwear class &times; every slot, under every hair-color
     * class (FACES-SPEC §3.2 invariant / §7-T10, re-targeted). No runtime fallbacks —
     * holes are a content build error. Call at boot after loading index + archetypes.
     *
     * @throws ArtMappingException listing every hole
     */
    public void validateCoverage() {
        List<String> holes = new ArrayList<>();
        for (FaceArchetype archetype : archetypes.all()) {
            for (HairColor color : HairColor.values()) {
                checkPool(holes, archetype, Q_BASE, null, "face_base");
                checkPool(holes, archetype, Q_EYES, null, "face_eyes");
                checkPool(holes, archetype, Q_BROW, color, "face_brow[" + color + "]");
                checkPool(holes, archetype, Q_NOSE, null, "face_nose");
                checkPool(holes, archetype, Q_MOUTH, color, "face_mouth[" + color + "]");
                checkPool(holes, archetype, Q_SCAR, null, "face_scar");
                for (HeadwearClass cls : archetype.headwearWeights().keySet()) {
                    if (cls == HeadwearClass.BARE) {
                        checkPool(holes, archetype, Q_HAIR, color, "face_hair[" + color + "]");
                    } else {
                        checkPool(holes, archetype, Set.of("face_headwear", cls.hwTag()),
                                null, "face_headwear[" + cls + "]");
                    }
                }
            }
        }
        if (!holes.isEmpty()) {
            throw new ArtMappingException(String.join("\n", holes));
        }
    }

    private void checkPool(List<String> holes, FaceArchetype archetype, Set<String> query,
                           HairColor color, String slot) {
        if (totalWeight(archetype, parts.query(query), color) <= 0) {
            holes.add("facegen coverage: archetype \"" + archetype.id() + "\" slot " + slot
                    + " resolves to an empty pool");
        }
    }

    private static long draw(long base, int k) {
        return SpriteIndex.mix64(base + k);
    }

    private static HeadwearClass pickHeadwearClass(FaceArchetype archetype, long draw) {
        int total = 0;
        for (HeadwearClass cls : HeadwearClass.values()) {
            Integer w = archetype.headwearWeights().get(cls);
            if (w != null) {
                total += w;
            }
        }
        long r = Long.remainderUnsigned(draw, total);
        int cum = 0;
        for (HeadwearClass cls : HeadwearClass.values()) {
            Integer w = archetype.headwearWeights().get(cls);
            if (w == null) {
                continue;
            }
            cum += w;
            if (r < cum) {
                return cls;
            }
        }
        throw new AssertionError("unreachable: cumulative weights cover the remainder");
    }

    private static HairColor pickHairColor(long draw) {
        int total = 0;
        for (HairColor color : HairColor.values()) {
            total += color.weight();
        }
        long r = Long.remainderUnsigned(draw, total);
        int cum = 0;
        for (HairColor color : HairColor.values()) {
            cum += color.weight();
            if (r < cum) {
                return color;
            }
        }
        throw new AssertionError("unreachable: cumulative weights cover the remainder");
    }

    /**
     * The id-ordinal weighted pick (FACES-SPEC §4.2, retained): pool in ascending id order
     * from {@link SpriteIndex#query}, integer cumulative effective weights,
     * {@code remainderUnsigned}. All integer.
     */
    private SpriteRef pick(FaceArchetype archetype, Set<String> query, HairColor color,
                           long draw, String slot) {
        List<SpriteRef> pool = parts.query(query);
        long total = totalWeight(archetype, pool, color);
        if (total <= 0) {
            throw new ArtMappingException("facegen: archetype \"" + archetype.id()
                    + "\" slot " + slot + " resolves to an empty pool (run validateCoverage"
                    + " at boot to catch this before composing)");
        }
        long r = Long.remainderUnsigned(draw, total);
        long cum = 0;
        for (SpriteRef ref : pool) {
            long w = effectiveWeight(archetype, ref, color);
            cum += w;
            if (w > 0 && r < cum) {
                return ref;
            }
        }
        throw new AssertionError("unreachable: cumulative weights cover the remainder");
    }

    private static long totalWeight(FaceArchetype archetype, List<SpriteRef> pool,
                                    HairColor color) {
        long total = 0;
        for (SpriteRef ref : pool) {
            total += effectiveWeight(archetype, ref, color);
        }
        return total;
    }

    /**
     * Integer effective weight: {@code part.weight × Π multipliers} over the part's tags
     * ({@code 0} excludes), and {@code 0} when a hair-color-gated part misses the drawn
     * class (spec §4.3: a part carrying any {@code hair_*} tag is eligible iff it matches;
     * untagged parts are always eligible).
     */
    private static long effectiveWeight(FaceArchetype archetype, SpriteRef ref,
                                        HairColor color) {
        if (color != null) {
            boolean colorGated = false;
            boolean matches = false;
            for (String tag : ref.tags()) {
                if (tag.startsWith(HAIR_TAG_PREFIX)) {
                    colorGated = true;
                    if (tag.equals(color.hairTag())) {
                        matches = true;
                    }
                }
            }
            if (colorGated && !matches) {
                return 0;
            }
        }
        long w = ref.weight();
        for (String tag : ref.tags()) {
            w *= archetype.multiplier(tag);
        }
        return w;
    }
}
