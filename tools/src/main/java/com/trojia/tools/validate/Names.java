package com.trojia.tools.validate;

import java.util.Collection;
import java.util.Optional;

/**
 * Package-private nearest-name suggestion for "unknown id" errors.
 *
 * <p>Deterministic tiebreak per ARCHITECTURE.md section 3 (tools): smallest edit
 * distance first, then lexicographically smallest candidate. Suggestions farther
 * than {@link #MAX_DISTANCE} edits away are suppressed (no hint beats a wild one).</p>
 */
final class Names {

    /** Maximum Levenshtein distance still offered as a suggestion. */
    static final int MAX_DISTANCE = 3;

    private Names() {
    }

    /**
     * @param unknown    the id that failed to resolve
     * @param candidates known ids (any iteration order; result is order-independent)
     * @return the closest candidate within {@link #MAX_DISTANCE} edits, tiebroken
     *         (distance, lex); empty when nothing is close enough
     */
    static Optional<String> nearest(String unknown, Collection<String> candidates) {
        String best = null;
        int bestDistance = MAX_DISTANCE + 1;
        for (String candidate : candidates) {
            int d = levenshtein(unknown, candidate);
            if (d < bestDistance || (d == bestDistance && best != null && candidate.compareTo(best) < 0)) {
                best = candidate;
                bestDistance = d;
            }
        }
        return Optional.ofNullable(best);
    }

    /** Classic two-row Levenshtein edit distance. */
    static int levenshtein(String a, String b) {
        int[] prev = new int[b.length() + 1];
        int[] curr = new int[b.length() + 1];
        for (int j = 0; j <= b.length(); j++) {
            prev[j] = j;
        }
        for (int i = 1; i <= a.length(); i++) {
            curr[0] = i;
            for (int j = 1; j <= b.length(); j++) {
                int substitution = prev[j - 1] + (a.charAt(i - 1) == b.charAt(j - 1) ? 0 : 1);
                curr[j] = Math.min(substitution, Math.min(prev[j] + 1, curr[j - 1] + 1));
            }
            int[] swap = prev;
            prev = curr;
            curr = swap;
        }
        return prev[b.length()];
    }
}
