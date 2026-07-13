package com.trojia.client.atlas;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

/**
 * Optional debug dump of the generated placeholder atlas for eyeballing:
 * {@code placeholder.png} + {@code placeholder.atlas} written to a directory
 * (conventionally under {@code build/}). GL-free — the PNG comes from
 * {@link PlaceholderPngWriter}, the atlas text from
 * {@link PlaceholderSheetRaster#atlasText} — and byte-deterministic (TILE-ART-SPEC
 * section 3: no timestamps, fixed encoder settings).
 *
 * <p>The runtime never reads these files; the observer boots from the in-memory
 * atlas of {@link PlaceholderAtlasFactory}.
 */
public final class PlaceholderAtlasDump {

    /** Page image file name, referenced from inside the atlas text. */
    public static final String PNG_FILE_NAME = "placeholder.png";

    /** Atlas text file name (standard libGDX atlas format). */
    public static final String ATLAS_FILE_NAME = "placeholder.atlas";

    private PlaceholderAtlasDump() {
    }

    /**
     * Writes {@value #PNG_FILE_NAME} and {@value #ATLAS_FILE_NAME} into
     * {@code directory}, creating it (and parents) as needed and overwriting
     * previous dumps.
     *
     * @param raster    the rastered sheet
     * @param directory the target directory
     * @return {@code directory}, for chaining
     * @throws IllegalArgumentException if either argument is null
     * @throws IOException              on any filesystem failure
     */
    public static Path dump(PlaceholderSheetRaster raster, Path directory) throws IOException {
        if (raster == null) {
            throw new IllegalArgumentException("raster must be non-null");
        }
        if (directory == null) {
            throw new IllegalArgumentException("directory must be non-null");
        }
        Files.createDirectories(directory);
        byte[] png = PlaceholderPngWriter.encodeArgb(
                raster.pixelsArgb(), raster.atlasSizePx(), raster.atlasSizePx());
        Files.write(directory.resolve(PNG_FILE_NAME), png);
        Files.writeString(directory.resolve(ATLAS_FILE_NAME),
                raster.atlasText(PNG_FILE_NAME), StandardCharsets.UTF_8);
        return directory;
    }

    /**
     * Command-line entry point for a manual dump.
     *
     * <p>Usage: {@code PlaceholderAtlasDump [mapping.json] [outDir]} — defaults are
     * {@code content/art/placeholder/art-mapping.json} and
     * {@code build/placeholder-atlas}, both resolved against the working directory.
     *
     * @param args optional mapping path and output directory
     * @throws IOException on any filesystem failure
     */
    public static void main(String[] args) throws IOException {
        Path mapping = args.length > 0 ? Path.of(args[0])
                : Path.of("content", "art", "placeholder", "art-mapping.json");
        Path outDir = args.length > 1 ? Path.of(args[1])
                : Path.of("build", "placeholder-atlas");
        String json = Files.readString(mapping, StandardCharsets.UTF_8);
        PlaceholderSheetRaster raster = PlaceholderAtlasFactory.buildRaster(json);
        dump(raster, outDir);
        System.out.println("placeholder atlas (" + raster.regionTable().size()
                + " regions) dumped to " + outDir.toAbsolutePath());
    }
}
