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
    /** In custody, walking to (or waiting at) the arrest holding cell (ARREST-SPEC addendum). */
    public static final short HELD = 1 << 6;
    /** Permanent, cosmetic-only: a Skyrunner's hand cut off on a 1st repeat offense. */
    public static final short MAIMED = 1 << 7;
    /** Permanent: a Skyrunner hanged on a 2nd repeat offense — inert forever, never removed. */
    public static final short EXECUTED = 1 << 8;

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
