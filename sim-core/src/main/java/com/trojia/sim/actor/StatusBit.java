package com.trojia.sim.actor;

/**
 * Bit flags packed into {@link Actor#statusBits()} (ACTORS-SPEC.md §1.1).
 * Plain {@code short} constants — no enum-set/bitset object, so the field
 * stays a primitive (ArchUnit purity).
 */
public final class StatusBit {

    public static final short ON_FIRE = 1;
    public static final short WET = 1 << 1;
    public static final short DOWNED = 1 << 2;
    public static final short ORPHANED = 1 << 3;
    public static final short PANICKED = 1 << 4;
    public static final short ALERTED = 1 << 5;

    private StatusBit() {
    }

    public static boolean isSet(short bits, short bit) {
        return (bits & bit) != 0;
    }

    public static short set(short bits, short bit) {
        return (short) (bits | bit);
    }

    public static short clear(short bits, short bit) {
        return (short) (bits & ~bit);
    }
}
