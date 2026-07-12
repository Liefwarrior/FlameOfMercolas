package com.trojia.tools.tmx;

/**
 * Receives non-fatal diagnostics from {@link TmxReader} and {@link TsxReader}
 * (masked gid flip bits, skipped unsupported elements).
 *
 * <p><strong>Determinism contract:</strong> for a given input document the reader
 * invokes this listener with an identical sequence of messages on every run, in
 * document order.</p>
 */
@FunctionalInterface
public interface TmxWarningListener {

    /**
     * @param message single-line human-readable warning, never {@code null}
     */
    void warn(String message);
}
