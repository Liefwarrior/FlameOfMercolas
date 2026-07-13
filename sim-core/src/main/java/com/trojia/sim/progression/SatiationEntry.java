package com.trojia.sim.progression;

/**
 * The stored satiation state for one {@link SatiationKey}: the tier reached
 * after the last award, and the tick it was recorded at (PROGRESSION-SPEC.md
 * &sect;3.3). Decay is computed lazily from {@code lastTick} at the next
 * award &mdash; no per-tick ticking of idle contexts is needed.
 *
 * @param tier     the satiation tier after the last award, {@code 0..4}
 * @param lastTick the tick the entry was last written
 */
public record SatiationEntry(int tier, long lastTick) {
}
