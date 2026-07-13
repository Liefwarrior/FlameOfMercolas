package com.trojia.client.atlas;

import com.trojia.client.art.RegionNameGrammar;

/**
 * A placeholder region name decomposed back into the grammar of TILE-ART-SPEC
 * section 3 ({@code regionName := <materialId> [ "." <form> ] [ ".a" <bucket> ]}) —
 * the inverse of {@link RegionNameGrammar#compose}.
 *
 * <p>Parsing is suffix-first: a trailing {@code .a<digit>} segment is the appearance
 * bucket (absent means bucket 0); after that, the text after the last remaining
 * {@code '.'} is the form token (absent means {@link RegionNameGrammar#DEFAULT_FORM});
 * what is left is the material id, verbatim ({@code @} and all). This assumes material
 * ids contain no {@code '.'} — true for the whole canon vocabulary and required for
 * the grammar itself to be invertible.
 *
 * <p>Only the placeholder <em>generator</em> parses names; the render path never does
 * (names come baked from the mapping file).
 *
 * @param materialId the canonical raws id, non-blank, verbatim
 * @param form       lowercase form token; {@link RegionNameGrammar#DEFAULT_FORM} when
 *                   the segment was omitted
 * @param bucket     appearance bucket ordinal 0..9 as written ({@code 0} when omitted)
 */
public record PlaceholderRegionKey(String materialId, String form, int bucket) {

    /**
     * Validates the components.
     *
     * @throws IllegalArgumentException if {@code materialId} or {@code form} is null
     *                                  or blank, or {@code bucket} is negative
     */
    public PlaceholderRegionKey {
        if (materialId == null || materialId.isBlank()) {
            throw new IllegalArgumentException("materialId must be non-blank");
        }
        if (form == null || form.isBlank()) {
            throw new IllegalArgumentException("form must be non-blank");
        }
        if (bucket < 0) {
            throw new IllegalArgumentException("bucket " + bucket + " < 0");
        }
    }

    /**
     * Parses a region name per the grammar (see class javadoc for the suffix-first
     * rule).
     *
     * @param regionName e.g. {@code "granite"}, {@code "chromatis.floor.a1"},
     *                   {@code "trudgeon_wood@getilia_soak.floor"}
     * @return the decomposed key
     * @throws IllegalArgumentException if {@code regionName} is null, blank, or
     *                                  leaves an empty material id or form token
     *                                  (e.g. {@code ".floor"} or {@code "granite."})
     */
    public static PlaceholderRegionKey parse(String regionName) {
        if (regionName == null || regionName.isBlank()) {
            throw new IllegalArgumentException("regionName must be non-blank");
        }
        String rest = regionName;
        int bucket = 0;
        int len = rest.length();
        if (len >= 3 && rest.charAt(len - 3) == '.' && rest.charAt(len - 2) == 'a'
                && rest.charAt(len - 1) >= '0' && rest.charAt(len - 1) <= '9') {
            bucket = rest.charAt(len - 1) - '0';
            rest = rest.substring(0, len - 3);
        }
        String form = RegionNameGrammar.DEFAULT_FORM;
        int dot = rest.lastIndexOf('.');
        if (dot >= 0) {
            form = rest.substring(dot + 1);
            rest = rest.substring(0, dot);
        }
        if (rest.isEmpty() || form.isEmpty()) {
            throw new IllegalArgumentException(
                    "region name \"" + regionName + "\" does not fit the grammar"
                            + " <materialId>[.<form>][.a<bucket>]");
        }
        return new PlaceholderRegionKey(rest, form, bucket);
    }
}
