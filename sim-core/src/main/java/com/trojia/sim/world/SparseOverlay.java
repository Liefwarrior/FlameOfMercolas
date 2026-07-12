package com.trojia.sim.world;

import java.util.Objects;

/**
 * Mutable sparse 16-bit overlay storage for one chunk: parallel {@code char}
 * arrays kept sorted by localIdx (keys 0..8191, values 0..65535), so reads
 * iterate in the canonical ascending-localIdx order with zero extra work.
 * Insert/remove are O(n) shifts — overlays are sparse by contract (a handful
 * of charged tiles per chunk), and all bulk paths only read.
 *
 * <p>Mutation is {@link ChunkWriter}-only via the owning {@link Chunk}.
 */
final class SparseOverlay implements OverlayView {

    private static final int INITIAL_CAPACITY = 8;

    private char[] keys = new char[INITIAL_CAPACITY];
    private char[] values = new char[INITIAL_CAPACITY];
    private int size;

    @Override
    public int size() {
        return size;
    }

    @Override
    public int localIdxAt(int i) {
        Objects.checkIndex(i, size);
        return keys[i];
    }

    @Override
    public int valueAt(int i) {
        Objects.checkIndex(i, size);
        return values[i];
    }

    @Override
    public int get(int localIdx, int absentValue) {
        int i = indexOf(localIdx);
        return i >= 0 ? values[i] : absentValue;
    }

    @Override
    public boolean contains(int localIdx) {
        return indexOf(localIdx) >= 0;
    }

    /** Stores {@code value} (unsigned 16-bit) at {@code localIdx}, inserting or replacing. */
    void put(int localIdx, int value) {
        int i = indexOf(localIdx);
        if (i >= 0) {
            values[i] = (char) value;
            return;
        }
        int at = -(i + 1);
        if (size == keys.length) {
            grow();
        }
        System.arraycopy(keys, at, keys, at + 1, size - at);
        System.arraycopy(values, at, values, at + 1, size - at);
        keys[at] = (char) localIdx;
        values[at] = (char) value;
        size++;
    }

    /** Removes the cell at {@code localIdx}; returns whether a cell was present. */
    boolean remove(int localIdx) {
        int i = indexOf(localIdx);
        if (i < 0) {
            return false;
        }
        System.arraycopy(keys, i + 1, keys, i, size - i - 1);
        System.arraycopy(values, i + 1, values, i, size - i - 1);
        size--;
        return true;
    }

    /** Binary search: the cell's index, or {@code -(insertionPoint + 1)} when absent. */
    private int indexOf(int localIdx) {
        int lo = 0;
        int hi = size - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >>> 1;
            int key = keys[mid];
            if (key < localIdx) {
                lo = mid + 1;
            } else if (key > localIdx) {
                hi = mid - 1;
            } else {
                return mid;
            }
        }
        return -(lo + 1);
    }

    private void grow() {
        int capacity = keys.length * 2;
        char[] newKeys = new char[capacity];
        char[] newValues = new char[capacity];
        System.arraycopy(keys, 0, newKeys, 0, size);
        System.arraycopy(values, 0, newValues, 0, size);
        keys = newKeys;
        values = newValues;
    }
}
