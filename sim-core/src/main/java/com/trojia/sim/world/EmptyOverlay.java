package com.trojia.sim.world;

/**
 * The immutable empty {@link OverlayView}, shared by every chunk that has no
 * cells stored for an overlay (including the flyweight VOID border chunks).
 * Stateless, hence safe as a static constant.
 */
final class EmptyOverlay implements OverlayView {

    /** The sole, immutable instance. */
    static final EmptyOverlay INSTANCE = new EmptyOverlay();

    private EmptyOverlay() {
    }

    @Override
    public int size() {
        return 0;
    }

    @Override
    public int localIdxAt(int i) {
        throw new IndexOutOfBoundsException("empty overlay: " + i);
    }

    @Override
    public int valueAt(int i) {
        throw new IndexOutOfBoundsException("empty overlay: " + i);
    }

    @Override
    public int get(int localIdx, int absentValue) {
        return absentValue;
    }

    @Override
    public boolean contains(int localIdx) {
        return false;
    }
}
