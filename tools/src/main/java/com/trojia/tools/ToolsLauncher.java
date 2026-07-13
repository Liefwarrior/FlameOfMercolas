package com.trojia.tools;

import java.io.PrintStream;
import java.io.UncheckedIOException;
import java.nio.file.Files;
import java.nio.file.Path;

import com.trojia.tools.tmx.TmxParseException;
import com.trojia.tools.validate.MapCheckContext;
import com.trojia.tools.validate.RawsLoadResult;
import com.trojia.tools.validate.RawsLoader;
import com.trojia.tools.validate.TiledValidator;
import com.trojia.tools.validate.ValidationIssue;
import com.trojia.tools.validate.ValidationReport;

/**
 * CLI entry point for content tooling (ARCHITECTURE.md section 3, tools).
 *
 * <pre>
 *   check-map &lt;file.tmx&gt; [--raws &lt;dir&gt;]   validate a Tiled map against the raws
 *   check-raws [&lt;dir&gt;]                    validate the raws files themselves
 * </pre>
 *
 * <p>Exit code {@code 0} when no errors were found (warnings allowed), {@code 1}
 * otherwise. When {@code --raws} is omitted the {@code content/raws} directory is
 * located by walking up from the map file (respectively the working directory).
 * M1 will add {@code import-map}.</p>
 */
public final class ToolsLauncher {

    private static final String USAGE = """
            trojia-tools commands:
              check-map <file.tmx> [--raws <dir>]   validate a Tiled map against the raws
              check-raws [<dir>]                    validate the raws files themselves
            exit code: 0 = no errors (warnings allowed), 1 = errors or bad invocation""";

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
     * @return {@code 0} when validation found no errors; {@code 1} on errors,
     *         unreadable input, or bad invocation
     */
    public static int run(String[] args, PrintStream out, PrintStream err) {
        if (args.length == 0) {
            err.println(USAGE);
            return 1;
        }
        try {
            return switch (args[0]) {
                case "check-map" -> checkMap(args, out, err);
                case "check-raws" -> checkRaws(args, out, err);
                default -> {
                    err.println("unknown command \"" + args[0] + "\"");
                    err.println(USAGE);
                    yield 1;
                }
            };
        } catch (TmxParseException e) {
            err.println("parse error: " + e.getMessage());
            return 1;
        } catch (UncheckedIOException e) {
            err.println("i/o error: " + e.getMessage());
            return 1;
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
                    return 1;
                }
                rawsDir = Path.of(args[++i]);
            } else if (mapFile == null) {
                mapFile = Path.of(args[i]);
            } else {
                err.println("unexpected argument \"" + args[i] + "\"");
                err.println(USAGE);
                return 1;
            }
        }
        if (mapFile == null) {
            err.println("check-map needs a .tmx file argument");
            err.println(USAGE);
            return 1;
        }
        if (!Files.isRegularFile(mapFile)) {
            err.println("no such map file: " + mapFile);
            return 1;
        }
        if (rawsDir == null) {
            rawsDir = locateRawsDir(mapFile.toAbsolutePath().getParent());
            if (rawsDir == null) {
                err.println("cannot locate content/raws above " + mapFile.toAbsolutePath().getParent()
                        + " or the working directory; pass --raws <dir>");
                return 1;
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
        return failed ? 1 : 0;
    }

    private static int checkRaws(String[] args, PrintStream out, PrintStream err) {
        Path rawsDir;
        if (args.length >= 2) {
            rawsDir = Path.of(args[1]);
        } else {
            rawsDir = locateRawsDir(Path.of("").toAbsolutePath());
            if (rawsDir == null) {
                err.println("cannot locate content/raws above the working directory; pass the directory explicitly");
                return 1;
            }
        }
        if (!Files.isDirectory(rawsDir)) {
            err.println("no such raws directory: " + rawsDir);
            return 1;
        }
        RawsLoadResult result = new RawsLoader().load(rawsDir);
        for (ValidationIssue issue : result.issues()) {
            out.println(issue.format());
        }
        out.println("raws: " + result.index() + ", " + result.issues().size() + " finding(s)"
                + (result.hasErrors() ? " -- FAIL" : " -- OK"));
        return result.hasErrors() ? 1 : 0;
    }

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
}
