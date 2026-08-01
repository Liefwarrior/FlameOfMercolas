#include "granadad/gate/twin_run.hpp"

#include <cstddef>
#include <string>
#include <vector>

#include "granadad/content/ascii.hpp"

namespace granadad::gate {
namespace {

using content::dec;
using content::hex64;

/// Splits on '\n'. A trailing newline does NOT produce a final empty line, so
/// two reports that differ only in whether they end with one still differ by
/// length and are caught by the byte comparison rather than silently equal.
[[nodiscard]] std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t stop = text.find('\n', start);
        if (stop == std::string::npos) {
            if (start < text.size()) {
                lines.push_back(text.substr(start));
            }
            break;
        }
        lines.push_back(text.substr(start, stop - start));
        start = stop + 1;
    }
    return lines;
}

}  // namespace

TextDivergence first_text_divergence(const std::string& a, const std::string& b) {
    const std::vector<std::string> left = lines_of(a);
    const std::vector<std::string> right = lines_of(b);
    const std::size_t shared = left.size() < right.size() ? left.size() : right.size();
    for (std::size_t i = 0; i < shared; ++i) {
        if (left[i] != right[i]) {
            return TextDivergence{true, i + 1, left[i], right[i]};
        }
    }
    if (left.size() != right.size()) {
        const std::size_t line = shared + 1;
        return TextDivergence{true, line,
                              shared < left.size() ? left[shared] : std::string("<missing>"),
                              shared < right.size() ? right[shared] : std::string("<missing>")};
    }
    return TextDivergence{};
}

std::string render_divergence(const std::string& a, const std::string& b, std::size_t limit) {
    const std::vector<std::string> left = lines_of(a);
    const std::vector<std::string> right = lines_of(b);
    const std::size_t longest = left.size() > right.size() ? left.size() : right.size();
    std::string out;
    std::size_t shown = 0;
    for (std::size_t i = 0; i < longest && shown < limit; ++i) {
        const std::string l = i < left.size() ? left[i] : std::string("<missing>");
        const std::string r = i < right.size() ? right[i] : std::string("<missing>");
        if (l == r) {
            continue;
        }
        out += "  line " + dec(static_cast<std::uint64_t>(i + 1)) + "\n";
        out += "    run A: " + l + "\n";
        out += "    run B: " + r + "\n";
        ++shown;
    }
    return out;
}

TwinRunOutcome twin_run(const WorkloadConfig& config) {
    TwinRunOutcome outcome;

    outcome.log += "=== granadad twin-run determinism gate ===\n";
    outcome.log += "  world   " + config.world + "\n";
    outcome.log += "  seed    " + hex64(config.seed) + "\n";
    outcome.log += "  ticks   " + dec(config.ticks) + "\n";
    outcome.log += "  walkers " + dec(config.walkers) + "\n";
    outcome.log += "\n";

    // Two full runs, back to back, in this process. Each builds its own world,
    // its own engine and its own systems and drops them before the next starts
    // -- which is exactly the situation in which a leaked static, a reused
    // address or an allocator-dependent ordering shows itself.
    const RunResult a = run_workload(config);
    const RunResult b = run_workload(config);

    outcome.hash_a = a.combined_hash;
    outcome.hash_b = b.combined_hash;
    outcome.hashes_agree = a.combined_hash == b.combined_hash;
    outcome.reports_agree = a.report == b.report;

    outcome.log += "[1/2] combined world hash\n";
    outcome.log += "  run A  " + hex64(a.combined_hash) + "\n";
    outcome.log += "  run B  " + hex64(b.combined_hash) + "\n";
    if (outcome.hashes_agree) {
        outcome.log += "  IDENTICAL\n";
    } else {
        outcome.log += "  DIVERGED. The two runs reached different STATE from the same seed.\n";
        outcome.log += "  Sub-hashes name which system:\n";
        outcome.log += "    WRLD  A=" + hex64(a.world_hash) + "  B=" + hex64(b.world_hash) + "\n";
    }
    outcome.log += "\n";

    outcome.log += "[2/2] report text, byte for byte\n";
    outcome.log += "  run A  " + dec(static_cast<std::uint64_t>(a.report.size())) + " bytes\n";
    outcome.log += "  run B  " + dec(static_cast<std::uint64_t>(b.report.size())) + " bytes\n";
    if (outcome.reports_agree) {
        outcome.log += "  IDENTICAL\n";
    } else {
        const TextDivergence divergence = first_text_divergence(a.report, b.report);
        outcome.log += "  DIVERGED at line "
                       + dec(static_cast<std::uint64_t>(divergence.line)) + "\n";
        outcome.log += render_divergence(a.report, b.report, 8);
        if (outcome.hashes_agree) {
            // The case that justifies having two comparators at all.
            outcome.log += "  The state hash AGREED. This divergence lives entirely in the\n";
            outcome.log += "  reporting path -- an undefined iteration order, an address\n";
            outcome.log += "  printed as an identifier, a locale, a clock. The hash cannot\n";
            outcome.log += "  see it because none of it was ever fed to a sink.\n";
        }
    }
    outcome.log += "\n";

    outcome.passed = outcome.hashes_agree && outcome.reports_agree;
    if (outcome.passed) {
        outcome.log += "=== PASS ===\n";
        outcome.log += "  two runs, one process, one seed, identical state and identical text.\n";
    } else {
        outcome.log += "=== FAIL ===\n";
        outcome.log += "  This is a real finding. The same seed produced two different runs\n";
        outcome.log += "  in the same process, which means saves do not reload, goldens do\n";
        outcome.log += "  not hold, and the world hash means nothing. Do not retry it until\n";
        outcome.log += "  it passes; find what is not a pure function of the seed.\n";
    }
    return outcome;
}

}  // namespace granadad::gate
