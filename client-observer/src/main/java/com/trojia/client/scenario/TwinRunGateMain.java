package com.trojia.client.scenario;

import java.io.ByteArrayOutputStream;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Locale;

/**
 * THE TWIN-RUN GATE (S8 harness slice) — the determinism harness this project spent five
 * sprints claiming to have and never had.
 *
 * <p>Runs the Docks soak <em>twice</em> from one seed and compares the two runs on BOTH
 * comparators:
 *
 * <ol>
 *   <li><b>the combined world hash</b> — {@code WorldHasher.hashWorld} folded with the ACTORS
 *       section sub-hash, i.e. every byte of the persisted triad. Catches state divergence
 *       that never reaches a printed line.</li>
 *   <li><b>the full report text, byte-for-byte</b> — catches divergence that lives only in
 *       the reporting path (unordered iteration over a HashSet, an identity hash leaking into
 *       an ordering, a locale-formatted number), which the state hash cannot see.</li>
 * </ol>
 *
 * <p>Neither comparator subsumes the other, which is exactly why the gate runs both. Any
 * divergence exits non-zero and names the first differing line (and, for the hash, the two
 * hex values) rather than "some byte differs".
 *
 * <p><b>Why one JVM, two sequential runs.</b> The two runs share a process deliberately: the
 * textbook desync shapes this arc will introduce — a daily price tick, a docket queue,
 * per-soul perception — leak through static mutable state and through identity-hash-ordered
 * collections, and both of those diverge between two runs in the SAME JVM (the second run's
 * objects get different identity hashes and see the first run's leftover statics). A gate
 * that forked two pristine JVMs would MISS static leakage entirely, because each fresh JVM
 * would leak identically. The trade-off is stated plainly: this gate does not catch
 * divergence that is stable within a JVM but varies across JVM instances or machines
 * (JIT-dependent floating point, {@code System.getProperty} reads). sim-core's no-float rule
 * is what covers that side.
 *
 * <p>Run: {@code ./gradlew.bat :client-observer:twinRunGate}
 * (or {@code -PtwinTicks=2000} while iterating).
 */
public final class TwinRunGateMain {

    private TwinRunGateMain() {
    }

    /** The soak length the arc gates on — the same 15k the quest twin test has always used. */
    private static final int DEFAULT_TICKS = 15_000;

    /** Committed pre-S8 baseline, reported (never enforced) so arc slices are not blocked. */
    private static final String BASELINE_DOC_NAME = "BASELINE-WORLD-HASH.md";
    private static final String BASELINE_DOC = "docs/" + BASELINE_DOC_NAME;
    private static final String BASELINE_TAG = "COMBINED WORLD HASH: 0x";

    /** One captured run: the report exactly as stdout saw it, plus its combined world hash. */
    private record Run(String report, String worldHash) {
    }

    public static void main(String[] args) {
        int ticks = parseTicks(args, DEFAULT_TICKS);
        PrintStream out = System.out;
        out.println("TWIN-RUN GATE: two " + ticks + "-tick Docks soaks from one seed; "
                + "comparing combined world hash AND full report text.");

        Run a = capture(ticks);
        out.println("  run A: " + a.report.length() + " report chars;  world hash 0x"
                + a.worldHash);
        Run b = capture(ticks);
        out.println("  run B: " + b.report.length() + " report chars;  world hash 0x"
                + b.worldHash);

        boolean hashOk = a.worldHash.equals(b.worldHash);
        boolean textOk = a.report.equals(b.report);

        out.println();
        out.println("  [1/2] combined world hash identical: " + hashOk);
        if (!hashOk) {
            out.println("        A=0x" + a.worldHash + "  B=0x" + b.worldHash);
        }
        out.println("  [2/2] report text byte-identical:    " + textOk
                + (textOk ? "" : "  " + firstTextDivergence(a.report, b.report)));

        reportBaseline(out, a.worldHash);

        if (hashOk && textOk) {
            out.println();
            out.println("TWIN-RUN GATE: PASS (both comparators).");
            return;
        }
        out.println();
        out.println("TWIN-RUN GATE: FAIL -- the Docks soak is not deterministic. "
                + "Nothing in the economy arc may merge on top of this.");
        if (!textOk) {
            dumpDivergence(a.report, b.report);
        }
        System.exit(1);
    }

    /**
     * Runs one soak with stdout diverted into a buffer, then reads the combined world hash
     * back out of the captured report. A run whose report carries no hash tag is a hard
     * failure, not a skipped comparator — an un-hooked gate that prints PASS is precisely the
     * defect this slice exists to remove.
     */
    private static Run capture(int ticks) {
        PrintStream real = System.out;
        ByteArrayOutputStream buffer = new ByteArrayOutputStream(1 << 20);
        PrintStream sink = new PrintStream(buffer, true, StandardCharsets.UTF_8);
        try {
            System.setOut(sink);
            DocksActorsMain.main(new String[] {"--ticks", String.valueOf(ticks)});
        } finally {
            System.setOut(real);
            sink.flush();
        }
        String report = buffer.toString(StandardCharsets.UTF_8);
        return new Run(report, extractHash(report));
    }

    /** Pulls the combined hash off the tagged line; throws if the report never printed one. */
    static String extractHash(String report) {
        int at = report.lastIndexOf(DocksActorsMain.WORLD_HASH_TAG);
        if (at < 0) {
            throw new IllegalStateException("the soak report carries no '"
                    + DocksActorsMain.WORLD_HASH_TAG.trim()
                    + "' line -- the twin-run gate's hash comparator is un-hooked");
        }
        int from = at + DocksActorsMain.WORLD_HASH_TAG.length();
        int to = from;
        while (to < report.length() && isHex(report.charAt(to))) {
            to++;
        }
        return report.substring(from, to);
    }

    private static boolean isHex(char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }

    /** One-line summary of where the two reports first parted company. */
    static String firstTextDivergence(String a, String b) {
        String[] linesA = a.split("\n", -1);
        String[] linesB = b.split("\n", -1);
        int n = Math.min(linesA.length, linesB.length);
        for (int i = 0; i < n; i++) {
            if (!linesA[i].equals(linesB[i])) {
                return "(first divergence at line " + (i + 1) + " of "
                        + linesA.length + "/" + linesB.length + ")";
            }
        }
        return "(identical for " + n + " lines; lengths differ "
                + linesA.length + " vs " + linesB.length + ")";
    }

    /** Prints the first few differing line pairs so a failure names itself. */
    private static void dumpDivergence(String a, String b) {
        String[] linesA = a.split("\n", -1);
        String[] linesB = b.split("\n", -1);
        int n = Math.min(linesA.length, linesB.length);
        int shown = 0;
        for (int i = 0; i < n && shown < 8; i++) {
            if (!linesA[i].equals(linesB[i])) {
                System.out.println("    line " + (i + 1) + ":");
                System.out.println("      A| " + linesA[i]);
                System.out.println("      B| " + linesB[i]);
                shown++;
            }
        }
        if (linesA.length != linesB.length) {
            System.out.println("    report line counts differ: A=" + linesA.length
                    + " B=" + linesB.length);
        }
    }

    /**
     * Informational only. The committed pre-S8 baseline says what the ward hashed to before
     * this arc touched it; a later arc slice that legitimately changes the sim SHOULD drift
     * from it. Drift is reported, never failed, so the gate stays a determinism gate and
     * does not become an accidental freeze on the whole economy arc.
     */
    private static void reportBaseline(PrintStream out, String hash) {
        String baseline = readBaseline();
        if (baseline == null) {
            out.println("  [--] pre-S8 baseline: not recorded (" + BASELINE_DOC + " absent)");
            return;
        }
        boolean match = baseline.equalsIgnoreCase(hash);
        out.println("  [--] pre-S8 baseline 0x" + baseline + ": "
                + (match ? "MATCH (this slice disturbed nothing)"
                         : "DRIFT (the sim changed -- informational, not a failure)"));
    }

    private static String readBaseline() {
        try {
            Path doc = com.trojia.client.boot.RepoPaths.locate("docs", BASELINE_DOC_NAME);
            if (!Files.isRegularFile(doc)) {
                return null;
            }
            for (String line : Files.readAllLines(doc, StandardCharsets.UTF_8)) {
                int at = line.indexOf(BASELINE_TAG);
                if (at < 0) {
                    continue;
                }
                int from = at + BASELINE_TAG.length();
                int to = from;
                while (to < line.length() && isHex(line.charAt(to))) {
                    to++;
                }
                if (to > from) {
                    return line.substring(from, to).toLowerCase(Locale.ROOT);
                }
            }
            return null;
        } catch (Exception e) {
            return null;
        }
    }

    private static int parseTicks(String[] args, int fallback) {
        for (int i = 0; i < args.length - 1; i++) {
            if (args[i].equals("--ticks")) {
                return Integer.parseInt(args[i + 1]);
            }
        }
        return fallback;
    }
}
