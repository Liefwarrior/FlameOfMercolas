package com.trojia.sim.actor;

import java.util.Objects;

/**
 * A type's composed, priority-ordered behavior stack (ACTORS-SPEC.md §1.4): a
 * thin {@link Actor} subclass declares exactly one of these as a static
 * constant. Stack position is the tie-break order for equal scores (§1.2).
 */
public final class PolicyStack {

    private final BehaviorPolicy[] policies;

    private PolicyStack(BehaviorPolicy[] policies) {
        this.policies = policies;
    }

    /** Builds an immutable stack in priority-declaration order. */
    public static PolicyStack of(BehaviorPolicy... policies) {
        Objects.requireNonNull(policies, "policies");
        if (policies.length == 0) {
            throw new IllegalArgumentException("a policy stack must have at least one policy "
                    + "(every stack must end in an always-applicable IDLE fallback)");
        }
        return new PolicyStack(policies.clone());
    }

    public int size() {
        return policies.length;
    }

    public BehaviorPolicy get(int stackIndex) {
        return policies[stackIndex];
    }

    /** {@code true} iff this stack contains a policy with the given id. */
    public boolean contains(PolicyId id) {
        for (BehaviorPolicy policy : policies) {
            if (policy.id() == id) {
                return true;
            }
        }
        return false;
    }

    /**
     * Selects the winning stack index: maximum {@link BehaviorPolicy#score};
     * ties broken by earlier stack position (ACTORS-SPEC.md §1.2, test A10).
     * Every stack must contain an always-applicable fallback (score {@code >= 1}
     * always), so this never returns {@code -1} for a well-formed stack.
     */
    public int selectIndex(Actor self, ActorContext ctx) {
        int bestIndex = -1;
        int bestScore = 0;
        for (int i = 0; i < policies.length; i++) {
            int score = policies[i].score(self, ctx);
            if (score > bestScore) {
                bestScore = score;
                bestIndex = i;
            }
        }
        return bestIndex;
    }
}
