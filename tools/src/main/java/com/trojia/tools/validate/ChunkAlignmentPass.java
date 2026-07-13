package com.trojia.tools.validate;

import java.util.function.Consumer;

import com.trojia.tools.validate.ValidationIssue.Severity;

/**
 * Pass 6: map dimensions versus the 32x32 chunk grid (ARCHITECTURE.md section 9:
 * chunks are 32x32x8).
 *
 * <p><strong>Warning, never an error</strong> (assignment ruling): ragged maps import
 * fine — the importer pads the edge — but chunk-aligned maps freeze/thaw and hash
 * without partially-VOID chunks, so authors should prefer multiples of 32.</p>
 */
public final class ChunkAlignmentPass implements ValidationPass {

    /** Chunk edge length in tiles. */
    public static final int CHUNK_EDGE = 32;

    /** Creates the pass. */
    public ChunkAlignmentPass() {
    }

    @Override
    public String id() {
        return "chunk-align";
    }

    @Override
    public void run(MapCheckContext context, Consumer<ValidationIssue> out) {
        int width = context.map().width();
        int height = context.map().height();
        if (width % CHUNK_EDGE != 0 || height % CHUNK_EDGE != 0) {
            int paddedWidth = ((width + CHUNK_EDGE - 1) / CHUNK_EDGE) * CHUNK_EDGE;
            int paddedHeight = ((height + CHUNK_EDGE - 1) / CHUNK_EDGE) * CHUNK_EDGE;
            out.accept(new ValidationIssue(Severity.WARNING, id(), context.mapName(), "",
                    ValidationIssue.NO_COORD, ValidationIssue.NO_COORD,
                    "map is " + width + "x" + height + ", not aligned to the " + CHUNK_EDGE + "x"
                            + CHUNK_EDGE + " chunk grid.",
                    "consider resizing to " + paddedWidth + "x" + paddedHeight
                            + "; the importer pads the ragged edge, but aligned maps avoid partially-void chunks."));
        }
    }
}
