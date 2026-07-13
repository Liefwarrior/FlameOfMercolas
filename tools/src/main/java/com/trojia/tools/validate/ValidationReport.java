package com.trojia.tools.validate;

import java.util.List;
import java.util.stream.Collectors;

/**
 * Ordered result of a validation run.
 *
 * <p>Issue order is the deterministic emission order of the passes (pass list order,
 * then document order within each pass).</p>
 *
 * @param issues all findings in emission order; defensively copied, immutable
 */
public record ValidationReport(List<ValidationIssue> issues) {

    /** @throws NullPointerException if the list or any element is {@code null} */
    public ValidationReport {
        issues = List.copyOf(issues);
    }

    /** @return only the {@link ValidationIssue.Severity#ERROR} findings, in order */
    public List<ValidationIssue> errors() {
        return issues.stream()
                .filter(i -> i.severity() == ValidationIssue.Severity.ERROR)
                .toList();
    }

    /** @return only the {@link ValidationIssue.Severity#WARNING} findings, in order */
    public List<ValidationIssue> warnings() {
        return issues.stream()
                .filter(i -> i.severity() == ValidationIssue.Severity.WARNING)
                .toList();
    }

    /** @return {@code true} iff at least one finding is an error */
    public boolean hasErrors() {
        return issues.stream().anyMatch(i -> i.severity() == ValidationIssue.Severity.ERROR);
    }

    /** @return every finding formatted one per line ({@link ValidationIssue#format()}); empty string when clean */
    public String render() {
        return issues.stream().map(ValidationIssue::format).collect(Collectors.joining(System.lineSeparator()));
    }

    /** @return one-line tally, e.g. {@code "2 errors, 1 warning"} */
    public String summary() {
        long e = errors().size();
        long w = warnings().size();
        return e + (e == 1 ? " error, " : " errors, ") + w + (w == 1 ? " warning" : " warnings");
    }
}
