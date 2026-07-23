package com.trojia.sim.bark;

import java.util.Arrays;
import java.util.List;
import java.util.Objects;

/**
 * The immutable, boot-built bark-text universe (Sprint 2 bark selection core): the
 * WORLD-authored text tables ({@code content/raws/barks/barks.json}) behind the sim's pure
 * {@code BarkSelector}. Mirrors {@link com.trojia.sim.actor.faction.FactionRegistry}'s
 * conventions — deterministic lookup by sorted string key, deeply immutable, safe to share.
 *
 * <p><b>Division of labor</b> (the Sprint-2 seam): the SIM owns the selection function and
 * THIS schema; the WORLD team owns the text content; the CLIENT renders the resolved row.
 * {@link #EMPTY} is the wired-but-unauthored degraded mode — selection still yields a
 * deterministic {@code (tableKey, rowDraw)}, and a consumer simply has no text to print
 * until the tables land.
 */
public final class BarkTableRegistry {

    /** The no-tables registry: every lookup misses (unauthored/degraded mode). */
    public static final BarkTableRegistry EMPTY =
            new BarkTableRegistry(new String[0], new String[0][]);

    private final String[] sortedKeys;
    private final String[][] rows;

    private BarkTableRegistry(String[] sortedKeys, String[][] rows) {
        this.sortedKeys = sortedKeys;
        this.rows = rows;
    }

    /** One authored table: a selector key and its non-empty text rows. */
    public record BarkTable(String key, List<String> rowTexts) {
    }

    /**
     * Builds a registry from authored tables. Input order is irrelevant (sorted-key
     * canonical order); duplicate keys and empty row lists fail loudly.
     */
    public static BarkTableRegistry of(List<BarkTable> tables) {
        Objects.requireNonNull(tables, "tables");
        BarkTable[] sorted = tables.toArray(new BarkTable[0]);
        Arrays.sort(sorted, (a, b) -> a.key().compareTo(b.key()));
        String[] keys = new String[sorted.length];
        String[][] rows = new String[sorted.length][];
        for (int i = 0; i < sorted.length; i++) {
            if (i > 0 && sorted[i].key().equals(sorted[i - 1].key())) {
                throw new IllegalArgumentException("duplicate bark table key: " + sorted[i].key());
            }
            if (sorted[i].rowTexts().isEmpty()) {
                throw new IllegalArgumentException(
                        "bark table has no rows: " + sorted[i].key());
            }
            keys[i] = sorted[i].key();
            rows[i] = sorted[i].rowTexts().toArray(new String[0]);
        }
        return new BarkTableRegistry(keys, rows);
    }

    /** Number of authored tables. */
    public int size() {
        return sortedKeys.length;
    }

    /** Whether a table with this selector key is authored. */
    public boolean contains(String key) {
        return Arrays.binarySearch(sortedKeys, Objects.requireNonNull(key, "key")) >= 0;
    }

    /** Row count of a table, or {@code 0} when the key is unauthored. */
    public int rowCount(String key) {
        int index = Arrays.binarySearch(sortedKeys, Objects.requireNonNull(key, "key"));
        return index < 0 ? 0 : rows[index].length;
    }

    /**
     * The text of one row.
     *
     * @throws IllegalArgumentException if the key is unauthored or the row is out of range
     */
    public String row(String key, int rowIndex) {
        int index = Arrays.binarySearch(sortedKeys, Objects.requireNonNull(key, "key"));
        if (index < 0) {
            throw new IllegalArgumentException("unknown bark table key: " + key);
        }
        if (rowIndex < 0 || rowIndex >= rows[index].length) {
            throw new IllegalArgumentException(
                    "bark row out of range: " + key + "[" + rowIndex + "]");
        }
        return rows[index][rowIndex];
    }

    /** All keys in canonical (sorted) order; immutable. */
    public List<String> keys() {
        return List.of(sortedKeys);
    }
}
