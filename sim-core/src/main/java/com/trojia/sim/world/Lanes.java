package com.trojia.sim.world;

/**
 * Canonical names and widths of the core dense lanes (ARCHITECTURE.md §8).
 * These are name constants only — actual {@link LaneId}s are minted by the
 * world's {@link LaneRegistry}, which registers the core lanes first, in the
 * declaration order below, giving them stable indices 0..6. Extension lanes
 * (e.g. a future aether lane) are later registrations.
 */
public final class Lanes {

    /** Material id per tile; 2 bytes (short {@code MaterialId}). Index 0. */
    public static final String MATERIAL = "material";
    /** {@link TileForm} ordinal per tile; 1 byte. Index 1. */
    public static final String FORM = "form";
    /** Derived + system flag bits per tile ({@link FlagBits}); 1 byte. Index 2. */
    public static final String FLAGS = "flags";
    /** Temperature per tile, unsigned 16-bit deci-Kelvin; 2 bytes. Index 3. */
    public static final String TEMPERATURE = "temperature";
    /** Bit-packed fluid state (depth 0–2, fluidId 3–5, SETTLED 6); 2 bytes. Index 4. */
    public static final String FLUID = "fluid";
    /** Packed light levels, sky(5b)+block(5b); 2 bytes. Index 5. */
    public static final String LIGHT = "light";
    /** Light-owned opacity extension lane, rebuilt from change logs; 1 byte. Index 6. */
    public static final String OPACITY = "opacity";

    /** Number of core lanes registered by every world before any extension lane. */
    public static final int CORE_LANE_COUNT = 7;

    /** Stable registry index of {@link #MATERIAL}. */
    public static final int MATERIAL_INDEX = 0;
    /** Stable registry index of {@link #FORM}. */
    public static final int FORM_INDEX = 1;
    /** Stable registry index of {@link #FLAGS}. */
    public static final int FLAGS_INDEX = 2;
    /** Stable registry index of {@link #TEMPERATURE}. */
    public static final int TEMPERATURE_INDEX = 3;
    /** Stable registry index of {@link #FLUID}. */
    public static final int FLUID_INDEX = 4;
    /** Stable registry index of {@link #LIGHT}. */
    public static final int LIGHT_INDEX = 5;
    /** Stable registry index of {@link #OPACITY}. */
    public static final int OPACITY_INDEX = 6;

    private Lanes() {
    }
}
