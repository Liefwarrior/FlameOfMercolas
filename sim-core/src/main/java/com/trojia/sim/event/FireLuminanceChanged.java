package com.trojia.sim.event;

/**
 * A burning tile crossed a luminance bucket boundary (4 buckets — bucketing
 * prevents relight storms). Consumed by the light system same tick.
 *
 * @param cell      packed position of the burning tile
 * @param oldBucket previous luminance bucket (0..3)
 * @param newBucket new luminance bucket (0..3)
 */
public record FireLuminanceChanged(int cell, int oldBucket, int newBucket) implements SimEvent {
}
