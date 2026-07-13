package com.trojia.tools.validate;

import java.util.function.Consumer;

/**
 * One ordered step of {@link TiledValidator}.
 *
 * <p><strong>Contract.</strong> A pass never mutates the model it inspects and never
 * throws for content defects — every defect becomes a {@link ValidationIssue}.
 * Determinism: for a given {@link MapCheckContext} the pass emits an identical issue
 * sequence on every run (layers walked in document order, tile grids row-major).</p>
 *
 * <p>Passes may assume nothing about earlier passes having "fixed" the map: each pass
 * skips — rather than re-reports — structures that a different pass owns (e.g. the
 * material pass ignores out-of-range gids, which belong to the gid-bounds pass).</p>
 */
public interface ValidationPass {

    /** @return stable machine-readable pass id used in {@link ValidationIssue#passId()} */
    String id();

    /**
     * Inspects the map and reports findings.
     *
     * @param context immutable map + tileset + raws view, never {@code null}
     * @param out     issue sink, never {@code null}; invoked in deterministic order
     */
    void run(MapCheckContext context, Consumer<ValidationIssue> out);
}
