package com.trojia.tools;

import com.trojia.tools.palette.PaletteGenerationException;
import com.trojia.tools.palette.RawsPaletteGenerator;
import com.trojia.tools.tmx.TmxParseException;
import com.trojia.tools.validate.MapCheckContext;
import com.trojia.tools.validate.RawsLoadResult;
import com.trojia.tools.validate.RawsLoader;
import com.trojia.tools.validate.TiledValidator;
import com.trojia.tools.validate.ValidationIssue;
import com.trojia.tools.validate.ValidationReport;

import java.io.IOException;
import java.io.PrintStream;
import java.io.UncheckedIOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;

/**
 * CLI entry point for content tooling (ARCHITECTURE.md section 3, tools).
 *
 * <pre>
 *   check-map &lt;file.tmx&gt; [--raws &lt;dir&gt;]   validate a Tiled map against the raws
 *   check-raws [&lt;dir&gt;]                    validate the raws files themselves
 *   gen-palette &lt;rawsDir&gt; &lt;out.tsx&gt;       regenerate the material palette tileset
 * </pre>
 *
 * <p>{@code gen-palette} is deterministic (identical raws produce a byte-identical
 * file) and writes atomically (temp file + rename), so a crash never leaves a
 * truncated palette. When {@code --raws} is omitted the {@code content/raws}
 * directory is located by walking up from the map file (respectively the working
 * directory). M1 adds {@code import-map}.</p>
 *
 * <p><strong>Exit codes:</strong> 0 success (warnings allowed) · 1 command failed
 * (validation errors, missing input, I/O error) · 2 usage error (unknown command,
 * wrong argument shape).</p>
 */
public final class ToolsLauncher {

    private static final int EXIT_OK = 0;
    private static final int EXIT_FAILURE = 1;
    private static final int EXIT_USAGE = 2;

    private static final String USAGE = """
            trojia-tools commands:
              check-map <file.tmx> [--raws <dir>]   validate a Tiled map against the raws
              check-raws [<dir>]                    validate the raws files themselves
              gen-palette <rawsDir> <out.tsx>       regenerate the material palette tileset
            exit code: 0 = success (warnings allowed), 1 = command failed, 2 = usage error""";

    private ToolsLauncher() {
    }

    /**
     * Process entry point; delegates to {@link #run} and exits with its code.
     *
     * @param args command line as documented in the class comment
     */
    public static void main(String[] args) {
        System.exit(run(args, System.out, System.err));
    }

    /**
     * Testable command dispatcher.
     *
     * @param args command line as documented in the class comment, never {@code null}
     * @param out  sink for findings and summaries, never {@code null}
     * @param err  sink for usage and I/O errors, never {@code null}
     * @return process exit code (see class contract)
     */
    public static int run(String[] args, PrintStream out, PrintStream err) {
        if (args.length == 0) {
            err.println(USAGE);
            return EXIT_USAGE;
        }
        try {
            return switch (args[0]) {
                case "check-map" -> checkMap(args, out, err);
                case "check-raws" -> checkRaws(args, out, err);
                case "gen-palette" -> genPalette(args, out, err);
                default -> {
                    err.println("unknown command \"" + args[0] + "\"");
                    err.println(USAGE);
                    yield EXIT_USAGE;
                }
            };
        } catch (TmxParseException e) {
            err.println("parse error: " + e.getMessage());
            return EXIT_FAILURE;
        } catch (UncheckedIOException e) {
            err.println("i/o error: " + e.getMessage());
            return EXIT_FAILURE;
        }
    }

    // ------------------------------------------------------------- commands

    private static int checkMap(String[] args, PrintStream out, PrintStream err) {
        Path mapFile = null;
        Path rawsDir = null;
        for (int i = 1; i < args.length; i++) {
            if (args[i].equals("--raws")) {
                if (i + 1 >= args.length) {
                    err.println("--raws needs a directory argument");
                    return EXIT_USAGE;
                }
                rawsDir = Path.of(args[++i]);
            } else if (mapFile == null) {
                mapFile = Path.of(args[i]);
            } else {
                err.println("unexpected argument \"" + args[i] + "\"");
                err.println(USAGE);
                return EXIT_USAGE;
            }
        }
        if (mapFile == null) {
            err.println("check-map needs a .tmx file argument");
            err.println(USAGE);
            return EXIT_USAGE;
        }
        if (!Files.isRegularFile(mapFile)) {
            err.println("no such map file: " + mapFile);
            return EXIT_FAILURE;
        }
        if (rawsDir == null) {
            rawsDir = locateRawsDir(mapFile.toAbsolutePath().getParent());
            if (rawsDir == null) {
                err.println("cannot locate content/raws above " + mapFile.toAbsolutePath().getParent()
                        + " or the working directory; pass --raws <dir>");
                return EXIT_FAILURE;
            }
        }

        RawsLoadResult raws = new RawsLoader().load(rawsDir);
        for (ValidationIssue issue : raws.issues()) {
            out.println(issue.format());
        }
        MapCheckContext context = TiledValidator.loadContext(mapFile, raws.index());
        ValidationReport report = TiledValidator.standard().validate(context);
        if (!report.issues().isEmpty()) {
            out.println(report.render());
        }
        boolean failed = report.hasErrors() || raws.hasErrors();
        out.println(context.mapName() + ": " + report.summary()
                + (raws.hasErrors() ? " (plus raws errors above)" : "")
                + (failed ? " -- FAIL" : " -- OK"));
        return failed ? EXIT_FAILURE : EXIT_OK;
    }

    private static int checkRaws(String[] args, PrintStream out, PrintStream err) {
        Path rawsDir;
        if (args.length >= 2) {
            rawsDir = Path.of(args[1]);
        } else {
            rawsDir = locateRawsDir(Path.of("").toAbsolutePath());
            if (rawsDir == null) {
                err.println("cannot locate content/raws above the working directory; pass the directory explicitly");
                return EXIT_FAILURE;
            }
        }
        if (!Files.isDirectory(rawsDir)) {
            err.println("no such raws directory: " + rawsDir);
            return EXIT_FAILURE;
        }
        RawsLoadResult result = new RawsLoader().load(rawsDir);
        for (ValidationIssue issue : result.issues()) {
            out.println(issue.format());
        }
        out.println("raws: " + result.index() + ", " + result.issues().size() + " finding(s)"
                + (result.hasErrors() ? " -- FAIL" : " -- OK"));
        return result.hasErrors() ? EXIT_FAILURE : EXIT_OK;
    }

    /** Executes {@code gen-palette <rawsDir> <out.tsx>}. */
    private static int genPalette(String[] args, PrintStream out, PrintStream err) {
        if (args.length != 3) {
            err.println("usage: gen-palette <rawsDir> <out.tsx>");
            return EXIT_USAGE;
        }
        Path rawsDir = Path.of(args[1]);
        Path outFile = Path.of(args[2]);
        try {
            String document = new RawsPaletteGenerator().generate(rawsDir);
            writeAtomically(outFile, document);
            out.println("gen-palette: wrote " + outFile);
            return EXIT_OK;
        } catch (PaletteGenerationException e) {
            err.println("gen-palette: " + e.getMessage());
            return EXIT_FAILURE;
        } catch (IOException | UncheckedIOException e) {
            err.println("gen-palette: cannot write " + outFile + ": " + e.getMessage());
            return EXIT_FAILURE;
        }
    }

    // -------------------------------------------------------------- helpers

    /**
     * Walks up from {@code start} (then from the working directory) looking for a
     * {@code content/raws} directory.
     *
     * @return the raws directory, or {@code null} when none exists on either path
     */
    private static Path locateRawsDir(Path start) {
        for (Path base : new Path[] { start, Path.of("").toAbsolutePath() }) {
            for (Path dir = base; dir != null; dir = dir.getParent()) {
                Path candidate = dir.resolve("content").resolve("raws");
                if (Files.isDirectory(candidate)) {
                    return candidate;
                }
            }
        }
        return null;
    }

    /**
     * Writes UTF-8 text via a sibling temp file and rename, so readers never observe
     * a partially written palette (atomic when the filesystem supports it).
     */
    private static void writeAtomically(Path target, String content) throws IOException {
        Path absolute = target.toAbsolutePath();
        Path parent = absolute.getParent();
        if (parent != null) {
            Files.createDirectories(parent);
        }
        Path temp = Files.createTempFile(parent, absolute.getFileName().toString(), ".tmp");
        try {
            Files.write(temp, content.getBytes(StandardCharsets.UTF_8));
            try {
                Files.move(temp, absolute, StandardCopyOption.REPLACE_EXISTING,
                        StandardCopyOption.ATOMIC_MOVE);
            } catch (AtomicMoveNotSupportedException e) {
                Files.move(temp, absolute, StandardCopyOption.REPLACE_EXISTING);
            }
        } finally {
            Files.deleteIfExists(temp);
        }
    }
}
