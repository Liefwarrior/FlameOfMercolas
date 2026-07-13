package com.trojia.tools.validate;

import java.util.List;
import java.util.Objects;

/**
 * Result of {@link RawsLoader#load}: the distilled {@link RawsIndex} plus every
 * consistency finding, in deterministic emission order.
 *
 * <p>The index is always usable — files that failed to parse simply contribute no
 * ids — so map validation can proceed (and report against the partial universe)
 * even when {@code check-raws} would fail.</p>
 *
 * @param index  the distilled id universe, never {@code null}
 * @param issues raws findings in emission order; defensively copied, immutable
 */
public record RawsLoadResult(RawsIndex index, List<ValidationIssue> issues) {

    /** @throws NullPointerException if any component or list element is {@code null} */
    public RawsLoadResult {
        Objects.requireNonNull(index, "index");
        issues = List.copyOf(issues);
    }

    /** @return {@code true} iff at least one finding is an error */
    public boolean hasErrors() {
        return issues.stream().anyMatch(i -> i.severity() == ValidationIssue.Severity.ERROR);
    }
}
