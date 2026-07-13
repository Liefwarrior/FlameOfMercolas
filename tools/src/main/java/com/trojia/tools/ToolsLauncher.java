package com.trojia.tools;

import com.trojia.tools.palette.PaletteGenerationException;
import com.trojia.tools.palette.RawsPaletteGenerator;

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
 * <p>Implemented subcommands:</p>
 * <ul>
 *   <li>{@code gen-palette <rawsDir> <out.tsx>} — regenerate the shared material
 *       palette tileset from the raws directory
 *       ({@link com.trojia.tools.palette.PaletteGenerator}). Deterministic: identical
 *       raws produce a byte-identical file. The output is written atomically
 *       (temp file + rename) so a crash never leaves a truncated palette.</li>
 * </ul>
 *
 * <p>M1 adds: {@code import-map} (bake a Tiled map into a .tregion),
 * {@code check-raws} (validate material raws against the registry).</p>
 *
 * <p><strong>Exit codes:</strong> 0 success · 1 command failed (bad raws, I/O error)
 * · 2 usage error (unknown command, wrong argument count).</p>
 */
public final class ToolsLauncher {

    private static final int EXIT_OK = 0;
    private static final int EXIT_FAILURE = 1;
    private static final int EXIT_USAGE = 2;

    private ToolsLauncher() {
    }

    public static void main(String[] args) {
        System.exit(run(args, System.out, System.err));
    }

    /**
     * Testable dispatch: parses {@code args}, executes the subcommand, and reports to
     * the given streams instead of terminating the JVM.
     *
     * @param args raw CLI arguments, never {@code null}
     * @param out  sink for normal output, never {@code null}
     * @param err  sink for diagnostics, never {@code null}
     * @return process exit code (see class contract)
     */
    static int run(String[] args, PrintStream out, PrintStream err) {
        if (args.length == 0) {
            err.println(usage());
            return EXIT_USAGE;
        }
        return switch (args[0]) {
            case "gen-palette" -> genPalette(args, out, err);
            default -> {
                err.println("unknown command: " + args[0]);
                err.println(usage());
                yield EXIT_USAGE;
            }
        };
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

    private static String usage() {
        return """
                usage: trojia-tools <command>
                  gen-palette <rawsDir> <out.tsx>   regenerate the material palette tileset
                (M1: import-map, check-raws)""";
    }
}
