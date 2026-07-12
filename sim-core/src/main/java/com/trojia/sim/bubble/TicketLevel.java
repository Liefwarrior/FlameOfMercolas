package com.trojia.sim.bubble;

/**
 * The three chunk ticket levels (ARCHITECTURE.md §7). ACTIVE and BORDER are
 * concrete with identical physics; FROZEN chunks live as summaries + snapshot
 * blobs (a 1-chunk FROZEN_RESIDENT rind around BORDER keeps readable storage
 * but is still FROZEN — writes there are rejected by ChunkWriter and routed
 * to BoundaryFlux).
 */
public enum TicketLevel {

    /** Full physics, inside the focus bubble. */
    ACTIVE,

    /** Full physics, 1-ring flux apron around ACTIVE. */
    BORDER,

    /** Abstract: summary + (possibly resident, read-only) snapshot. */
    FROZEN
}
