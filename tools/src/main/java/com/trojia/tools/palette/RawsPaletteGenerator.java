package com.trojia.tools.palette;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Set;

/**
 * Default {@link PaletteGenerator}: reads the raws with {@link PaletteRawsLoader},
 * decides form relevance, and renders a TSX document layout-compatible with the
 * hand-authored {@code content/maps/src/materials.tsx}.
 *
 * <p><strong>Form-relevance rule (deterministic, raws-driven):</strong></p>
 * <ul>
 *   <li>{@code LIQUID} phase &rarr; {@link PaletteForm#FLOOR} only (a pooled melt
 *       lies on the ground; it is never shaped into architecture);</li>
 *   <li>{@code GAS} phase &rarr; no tiles (not paintable terrain in v0);</li>
 *   <li>{@code SOLID} phase &rarr; {@link PaletteForm#WALL} + {@link PaletteForm#FLOOR}
 *       always; additionally {@link PaletteForm#RAMP} + {@link PaletteForm#STAIR_UP} +
 *       {@link PaletteForm#STAIR_DOWN} when the material carries a structural tag
 *       ({@link #STRUCTURAL_TAGS}: {@code stone}, {@code wood}) — materials that are
 *       carved or joined into stairwork, matching the hand-authored palette (granite
 *       and oak carry stairs; thatch, steel, brick do not).</li>
 * </ul>
 *
 * <p>The rule generates a superset of the hand-authored palette by design: every
 * hand-authored {@code (material, form)} pair is emitted with identical properties;
 * additional pairs (e.g. {@code oak/RAMP}) are legal vocabulary that map authors may
 * simply not use. The structural comparison test reports extras without failing.</p>
 *
 * <p><strong>Fluid depth variants:</strong> {@link #FLUID_DEPTHS} = 7 (full), 4, 2 in
 * descending order — the palette convention fixed by the committed
 * {@code materials.tsx} and {@code content/maps/README.md} ("Provided depths: 7
 * (full), 4, 2"). Depths are a palette convention, not fluid raws data.</p>
 *
 * <p>Stateless, deterministic, safe to reuse; see {@link PaletteGenerator} for the
 * byte-identity contract.</p>
 */
public final class RawsPaletteGenerator implements PaletteGenerator {

    /**
     * Tags whose solid materials also get RAMP/STAIR_UP/STAIR_DOWN tiles.
     * {@code masonry}/{@code earth} joined for the docks_surface map: Saltgate
     * Rise climbs on brick RAMPs and the shipyard slipways on dirt RAMPs
     * (materials.tsx tile ids 35-36), and the hand-authored-tiles-exist-in-
     * generated-palette compatibility invariant requires the generator to know
     * every structural family the maps actually build ramps from.
     */
    static final Set<String> STRUCTURAL_TAGS = Set.of("stone", "wood", "masonry", "earth");

    /** Fixed pooled-depth variants per fluid, in canonical (descending) order. */
    static final int[] FLUID_DEPTHS = {7, 4, 2};

    private final PaletteRawsLoader loader = new PaletteRawsLoader();

    @Override
    public List<PaletteTile> plan(Path rawsDir) {
        Objects.requireNonNull(rawsDir, "rawsDir");
        PaletteRaws raws = loader.load(rawsDir);
        List<PaletteTile> tiles = new ArrayList<>();
        for (PaletteMaterial material : raws.materials()) {
            for (PaletteForm form : PaletteForm.values()) {
                if (isRelevant(material, form)) {
                    tiles.add(new PaletteTile.MaterialTile(material.id(), form));
                }
            }
        }
        for (PaletteFluid fluid : raws.fluids()) {
            for (int depth : FLUID_DEPTHS) {
                tiles.add(new PaletteTile.FluidTile(fluid.id(), depth));
            }
        }
        return List.copyOf(tiles);
    }

    @Override
    public String generate(Path rawsDir) {
        List<PaletteTile> tiles = plan(rawsDir);
        StringBuilder out = new StringBuilder(tiles.size() * 160 + 256);
        out.append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        out.append("<tileset version=\"1.10\" name=\"materials\" tilewidth=\"16\" tileheight=\"16\" tilecount=\"")
                .append(tiles.size()).append("\" columns=\"0\">\n");
        out.append(" <grid orientation=\"orthogonal\" width=\"16\" height=\"16\"/>\n");
        for (int id = 0; id < tiles.size(); id++) {
            appendTile(out, id, tiles.get(id));
        }
        out.append("</tileset>\n");
        return out.toString();
    }

    /** Applies the form-relevance rule documented on the class. */
    private static boolean isRelevant(PaletteMaterial material, PaletteForm form) {
        return switch (material.phase()) {
            case LIQUID -> form == PaletteForm.FLOOR;
            case GAS -> false;
            case SOLID -> switch (form) {
                case WALL, FLOOR -> true;
                case OPEN -> false;
                case RAMP, STAIR_UP, STAIR_DOWN ->
                        STRUCTURAL_TAGS.stream().anyMatch(material::hasTag);
            };
        };
    }

    private static void appendTile(StringBuilder out, int id, PaletteTile tile) {
        out.append(" <tile id=\"").append(id).append("\" type=\"")
                .append(escape(tile.typeName())).append("\">\n");
        out.append("  <properties>\n");
        switch (tile) {
            case PaletteTile.MaterialTile m -> {
                appendProperty(out, "material", null, m.materialId());
                appendProperty(out, "form", null, m.form().name());
            }
            case PaletteTile.FluidTile f -> {
                appendProperty(out, "fluid", null, f.fluidId());
                appendProperty(out, "depth", "int", Integer.toString(f.depth()));
            }
        }
        out.append("  </properties>\n");
        out.append(" </tile>\n");
    }

    private static void appendProperty(StringBuilder out, String name, String type, String value) {
        out.append("   <property name=\"").append(escape(name)).append('"');
        if (type != null) {
            out.append(" type=\"").append(escape(type)).append('"');
        }
        out.append(" value=\"").append(escape(value)).append("\"/>\n");
    }

    /** Minimal XML attribute escaping (ids are {@code [a-z0-9_@]} in practice). */
    private static String escape(String raw) {
        StringBuilder sb = null;
        for (int i = 0; i < raw.length(); i++) {
            char c = raw.charAt(i);
            String repl = switch (c) {
                case '&' -> "&amp;";
                case '<' -> "&lt;";
                case '>' -> "&gt;";
                case '"' -> "&quot;";
                default -> null;
            };
            if (repl != null && sb == null) {
                sb = new StringBuilder(raw.length() + 8).append(raw, 0, i);
            }
            if (sb != null) {
                if (repl != null) {
                    sb.append(repl);
                } else {
                    sb.append(c);
                }
            }
        }
        return sb == null ? raw : sb.toString();
    }
}
