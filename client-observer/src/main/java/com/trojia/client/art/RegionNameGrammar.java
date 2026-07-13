package com.trojia.client.art;

/**
 * The atlas region-name grammar of TILE-ART-SPEC section 3:
 *
 * <pre>regionName := &lt;materialId&gt; [ "." &lt;form&gt; ] [ ".a" &lt;bucket&gt; ]</pre>
 *
 * <ul>
 *   <li>{@code materialId} — canonical raws id, verbatim (derived ids keep their
 *       {@code @}: {@code trudgeon_wood@getilia_soak}).</li>
 *   <li>{@code form} — lowercase form token; <em>omitted</em> for the default solid
 *       form {@link #DEFAULT_FORM} ({@code "block"}).</li>
 *   <li>{@code bucket} — appearance bucket ordinal; <em>omitted</em> for bucket 0.
 *       Segment order is always material, then form, then bucket.</li>
 * </ul>
 *
 * <p>Examples: {@code granite} &middot; {@code granite.floor} &middot; {@code chromatis.a2}
 * &middot; {@code chromatis.floor.a1} &middot; {@code trudgeon_wood@getilia_soak.floor}.
 *
 * <p>This class only <em>composes</em> canonical placeholder names — the resolver never
 * synthesizes names at render time (they come verbatim from the mapping file, which is
 * the alias layer for real packs whose names need not follow this grammar). It exists so
 * the placeholder generator and tests share one deterministic naming rule.
 */
public final class RegionNameGrammar {

    /** The default solid form whose token is omitted from region names. */
    public static final String DEFAULT_FORM = "block";

    /** Smallest legal appearance bucket. */
    public static final int MIN_BUCKET = 0;

    /** Largest legal appearance bucket (TILE-ART-SPEC section 2: ordinal 0..3). */
    public static final int MAX_BUCKET = 3;

    private RegionNameGrammar() {
    }

    /**
     * Composes the canonical region name for {@code (materialId, form, bucket)}.
     *
     * @param materialId canonical raws id, non-blank, used verbatim
     * @param form       lowercase form token, non-blank; {@link #DEFAULT_FORM} omits
     *                   the segment
     * @param bucket     appearance bucket, {@link #MIN_BUCKET}..{@link #MAX_BUCKET};
     *                   0 omits the segment
     * @return the composed name, e.g. {@code "chromatis.floor.a1"}
     * @throws IllegalArgumentException on blank ids/forms or out-of-range buckets
     */
    public static String compose(String materialId, String form, int bucket) {
        if (materialId == null || materialId.isBlank()) {
            throw new IllegalArgumentException("materialId must be non-blank");
        }
        if (form == null || form.isBlank()) {
            throw new IllegalArgumentException("form must be non-blank");
        }
        if (bucket < MIN_BUCKET || bucket > MAX_BUCKET) {
            throw new IllegalArgumentException(
                    "bucket " + bucket + " outside " + MIN_BUCKET + ".." + MAX_BUCKET);
        }
        StringBuilder name = new StringBuilder(materialId);
        if (!DEFAULT_FORM.equals(form)) {
            name.append('.').append(form);
        }
        if (bucket != 0) {
            name.append(".a").append(bucket);
        }
        return name.toString();
    }
}
