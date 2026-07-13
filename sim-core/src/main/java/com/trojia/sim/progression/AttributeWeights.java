package com.trojia.sim.progression;

import java.util.List;

/**
 * The normative per-attribute skill weight table (PROGRESSION-SPEC.md
 * &sect;5, post-merge rebalance 2026-07-12). Fixed content, not raws-driven:
 * the spec states these numbers directly as a table, not as a per-skill raws
 * field, so they live here as a pinned Java constant table mirroring the
 * spec verbatim.
 *
 * <p>Invariant (unit-tested, PROGRESSION-SPEC.md &sect;5 "unit-tested
 * invariant" / &sect;10 test 26): every attribute's weight row sums to
 * exactly 128.</p>
 */
public final class AttributeWeights {

    private AttributeWeights() {
    }

    private static final List<SkillWeight> MGT = List.of(
            new SkillWeight("heavy_arms", 40),
            new SkillWeight("lancework", 32),
            new SkillWeight("kit_keeping", 24),
            new SkillWeight("bladework", 16),
            new SkillWeight("grit", 16));

    private static final List<SkillWeight> AGI = List.of(
            new SkillWeight("skyrunning", 64),
            new SkillWeight("sidearms", 44),
            new SkillWeight("open_hand", 20));

    private static final List<SkillWeight> VIG = List.of(
            new SkillWeight("grit", 48),
            new SkillWeight("harness", 32),
            new SkillWeight("shieldwall", 28),
            new SkillWeight("skyrunning", 20));

    private static final List<SkillWeight> WIT = List.of(
            new SkillWeight("mixtures", 28),
            new SkillWeight("channeling", 28),
            new SkillWeight("cracksmanship", 24),
            new SkillWeight("streetwise", 24),
            new SkillWeight("dire_bows", 24));

    /**
     * Returns the weight row for an attribute, in the order listed in
     * PROGRESSION-SPEC.md &sect;5.
     *
     * @param attribute the attribute
     * @return the immutable weight list; sums to exactly 128
     */
    public static List<SkillWeight> weights(AttributeId attribute) {
        return switch (attribute) {
            case MGT -> MGT;
            case AGI -> AGI;
            case VIG -> VIG;
            case WIT -> WIT;
        };
    }
}
