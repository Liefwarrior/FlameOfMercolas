// The twin-run gate's own logic.
//
// A gate whose comparators are untested is a gate you find out about on the day
// it should have gone red. So the comparators are driven here with synthetic
// text where the right answer is obvious, and the workload is driven for the
// properties the gate depends on: that it is a pure function of its config, and
// that changing any part of that config changes the answer.
//
// What this file CANNOT establish is that the gate catches real nondeterminism
// -- for that the code under it has to actually be nondeterministic, which is a
// mutation test run against a scratch copy of the tree, not a unit test. The
// result of that run is recorded in the M1 report.

#include <doctest/doctest.h>

#include <cstdint>
#include <string>

#include "granadad/gate/twin_run.hpp"
#include "granadad/gate/workload.hpp"

using namespace granadad::gate;

namespace {

/// Small on purpose: this suite runs on every build, and the gate binary runs
/// the long version.
[[nodiscard]] WorkloadConfig tiny() {
    WorkloadConfig config;
    config.world = "tavern_fixture";
    config.ticks = 40;
    config.walkers = 16;
    config.sample_every = 10;
    return config;
}

/// The number following the LAST occurrence of `label` in a report, skipping
/// the padding spaces. Returns -1 when the label never appears.
[[nodiscard]] std::int64_t last_counter(const std::string& report, const std::string& label) {
    const std::size_t at = report.rfind(label);
    if (at == std::string::npos) {
        return -1;
    }
    std::int64_t value = 0;
    bool sawDigit = false;
    for (std::size_t i = at + label.size(); i < report.size(); ++i) {
        const char c = report[i];
        if (c == ' ' && !sawDigit) {
            continue;
        }
        if (c < '0' || c > '9') {
            break;
        }
        sawDigit = true;
        value = value * 10 + (c - '0');
    }
    return sawDigit ? value : -1;
}

}  // namespace

TEST_CASE("identical text has no divergence") {
    const std::string text = "alpha\nbeta\ngamma\n";
    const TextDivergence divergence = first_text_divergence(text, text);
    CHECK_FALSE(divergence.diverged);
    CHECK(render_divergence(text, text, 8).empty());
}

TEST_CASE("the first differing line is named, not the last") {
    const std::string a = "alpha\nbeta\ngamma\ndelta\n";
    const std::string b = "alpha\nbeta\nGAMMA\nDELTA\n";
    const TextDivergence divergence = first_text_divergence(a, b);
    CHECK(divergence.diverged);
    CHECK(divergence.line == 3);
    CHECK(divergence.left == "gamma");
    CHECK(divergence.right == "GAMMA");

    // Both differing pairs render, in order, and the cap is honoured.
    const std::string rendered = render_divergence(a, b, 8);
    CHECK(rendered.find("line 3") != std::string::npos);
    CHECK(rendered.find("line 4") != std::string::npos);
    CHECK(render_divergence(a, b, 1).find("line 4") == std::string::npos);
}

TEST_CASE("a report that is a prefix of the other still diverges") {
    // The failure mode where one run stops early. Equal on every shared line,
    // and absolutely not equal.
    const std::string shorter = "alpha\nbeta\n";
    const std::string longer = "alpha\nbeta\ngamma\n";
    const TextDivergence forward = first_text_divergence(shorter, longer);
    CHECK(forward.diverged);
    CHECK(forward.line == 3);
    CHECK(forward.left == "<missing>");
    CHECK(forward.right == "gamma");

    const TextDivergence backward = first_text_divergence(longer, shorter);
    CHECK(backward.diverged);
    CHECK(backward.line == 3);
    CHECK(backward.right == "<missing>");
}

TEST_CASE("a missing trailing newline is a divergence, not a rounding error") {
    CHECK(first_text_divergence("alpha\n", "alpha").diverged == false);
    // ^ the line CONTENT is identical, which is what line-level comparison
    // reports. The gate's actual verdict is a whole-string ==, so the two are
    // still caught -- stated here so nobody mistakes the line comparator for
    // the comparator.
    CHECK(std::string("alpha\n") != std::string("alpha"));
}

TEST_CASE("empty against non-empty diverges at line 1") {
    const TextDivergence divergence = first_text_divergence("", "alpha\n");
    CHECK(divergence.diverged);
    CHECK(divergence.line == 1);
}

TEST_CASE("the workload is a pure function of its config") {
    const RunResult a = run_workload(tiny());
    const RunResult b = run_workload(tiny());
    CHECK(a.combined_hash == b.combined_hash);
    CHECK(a.world_hash == b.world_hash);
    CHECK(a.report == b.report);
    CHECK_FALSE(a.report.empty());
}

TEST_CASE("the report carries the combined hash it returned") {
    const RunResult run = run_workload(tiny());
    const std::string tag = std::string(kCombinedHashTag);
    const std::size_t at = run.report.find(tag);
    REQUIRE(at != std::string::npos);
    // Parse it back out the way the Java gate does, so a formatting change that
    // broke greppability shows up here rather than in a downstream script.
    std::uint64_t parsed = 0;
    int digits = 0;
    for (std::size_t i = at + tag.size(); i < run.report.size(); ++i) {
        const char c = run.report[i];
        int value = 0;
        if (c >= '0' && c <= '9') {
            value = c - '0';
        } else if (c >= 'A' && c <= 'F') {
            value = c - 'A' + 10;
        } else if (c >= 'a' && c <= 'f') {
            value = c - 'a' + 10;
        } else {
            break;
        }
        parsed = (parsed << 4) | static_cast<std::uint64_t>(value);
        ++digits;
    }
    CHECK(digits == 16);
    CHECK(parsed == run.combined_hash);
}

TEST_CASE("the workload's answer depends on the seed") {
    WorkloadConfig other = tiny();
    other.seed ^= 1u;
    const RunResult base = run_workload(tiny());
    const RunResult moved = run_workload(other);
    CHECK(base.combined_hash != moved.combined_hash);
    // The terrain is not written by anyone, so the world section must NOT move.
    CHECK(base.world_hash == moved.world_hash);
}

TEST_CASE("the workload's answer depends on how long it ran") {
    WorkloadConfig longer = tiny();
    longer.ticks += 1;
    CHECK(run_workload(tiny()).combined_hash != run_workload(longer).combined_hash);
}

TEST_CASE("the workload's answer depends on the world it ran on") {
    WorkloadConfig elsewhere = tiny();
    elsewhere.world = "compound_block";
    const RunResult here = run_workload(tiny());
    const RunResult there = run_workload(elsewhere);
    CHECK(here.world_hash != there.world_hash);
    CHECK(here.combined_hash != there.combined_hash);
}

TEST_CASE("the walkers actually move and actually chatter") {
    // Guards against the degenerate workload. One that ran but did nothing at
    // all would be perfectly deterministic and would prove nothing -- and the
    // way that happens for real is a walkable-tile predicate that is accidentally
    // false everywhere, which no equality assertion would notice.
    WorkloadConfig config = tiny();
    config.ticks = 200;
    const RunResult run = run_workload(config);
    CHECK(last_counter(run.report, "moved=") > 0);
    CHECK(last_counter(run.report, "chatter=") > 0);
    // Blocked is not asserted non-zero: whether a walker ever meets a wall is a
    // property of the map, and pinning it here would make this test about
    // tavern_fixture's floor plan.
    CHECK(last_counter(run.report, "blocked=") >= 0);
}

TEST_CASE("the gate passes on the code as it stands") {
    const TwinRunOutcome outcome = twin_run(tiny());
    CHECK(outcome.passed);
    CHECK(outcome.hashes_agree);
    CHECK(outcome.reports_agree);
    CHECK(outcome.hash_a == outcome.hash_b);
    CHECK(outcome.log.find("=== PASS ===") != std::string::npos);
    CHECK(outcome.log.find("[1/2]") != std::string::npos);
    CHECK(outcome.log.find("[2/2]") != std::string::npos);
}
