// One binary, two jobs, for the same reason granadad-content-tests has two:
//
//   granadad-twin-gate                    -> run the workload twice, compare
//   granadad-twin-gate --fingerprint FILE -> the cross-toolchain report
//
// Both answer "is this deterministic", from opposite directions -- once against
// itself in one process, once against the other toolchain. Splitting them would
// mean the report came from a binary other than the one the gate proved.

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>

#include "granadad/content/ascii.hpp"
#include "granadad/content/content_dir.hpp"
#include "granadad/gate/twin_run.hpp"
#include "granadad/gate/workload.hpp"
#include "granadad/gate/world_hash_report.hpp"
#include "granadad/sim/compound.hpp"

namespace {

constexpr std::string_view kFingerprintFlag = "--fingerprint";

/// The information that is SUPPOSED to differ between the two platforms, which
/// is exactly why it goes to stdout and never into the report file.
void print_banner() {
#if defined(_WIN32)
    const char* platform = "windows-x86_64";
#else
    const char* platform = "linux-x86_64";
#endif
#if defined(__VERSION__)
    const char* compiler = __VERSION__;
#elif defined(_MSC_FULL_VER)
    const char* compiler = "msvc";
#else
    const char* compiler = "unknown";
#endif
    std::printf("granadad-twin-gate\n");
    std::printf("  platform:    %s\n", platform);
    std::printf("  compiler:    %s\n", compiler);
    std::printf("  content dir: %s\n", granadad::content::contentDir().string().c_str());
}

/// Fails early and legibly when the content directory is wrong.
///
/// Without this the first symptom of an unset GRANADAD_CONTENT_DIR on Windows
/// is a FormatError about a path that does not exist, which reads like a broken
/// reader rather than a broken invocation.
bool content_dir_is_usable() {
    std::error_code ec;
    return std::filesystem::is_directory(granadad::content::bakedDir(), ec);
}

int complain_about_content_dir() {
    std::fprintf(stderr,
                 "FATAL: no baked worlds at %s\n"
                 "       Set %s to the repo's content/ directory. On Windows:\n"
                 "           $env:%s = 'C:\\path\\to\\repo\\content'\n"
                 "       The path compiled into this binary is the build\n"
                 "       container's and does not exist here.\n",
                 granadad::content::bakedDir().string().c_str(),
                 granadad::content::kContentDirEnvVar, granadad::content::kContentDirEnvVar);
    return 2;
}

int run_fingerprint(const std::string& output_path) {
    if (output_path.empty()) {
        std::fprintf(stderr,
                     "FATAL: --fingerprint needs a file to write.\n"
                     "       It is a file and not stdout on purpose: shells rewrite line\n"
                     "       endings and re-encode redirected output, and this report is\n"
                     "       compared between platforms byte for byte.\n");
        return 2;
    }
    std::string text;
    try {
        text = granadad::gate::world_hash_report();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FATAL: could not build the world-hash report: %s\n", error.what());
        return 1;
    }
    std::string write_error;
    if (!granadad::content::writeTextFile(output_path, text, &write_error)) {
        std::fprintf(stderr, "FATAL: %s\n", write_error.c_str());
        return 1;
    }
    std::printf("  report:      %s (%s bytes)\n", output_path.c_str(),
                granadad::content::dec(static_cast<std::uint64_t>(text.size())).c_str());
    return 0;
}

/// S7. Two years of the compounds, and the balance bar enforced by the build.
///
/// THIS IS A GATE AND NOT A REPORT. The Java build held serf starvation at or
/// below 5%; sim::kStarvationBarPermille is that number, and a ward that
/// starves past it exits non-zero. So does one whose courtyards drown the ward
/// in surplus -- an economy with nothing scarce in it is not balanced, it is
/// switched off. A balance claim nobody can fail is a balance claim nobody has
/// made.
int run_ward_soak(std::int64_t days) {
    const granadad::sim::WardSoakResult soak = granadad::sim::runWardSoak(
        days, granadad::content::contentDir(), 0x4752414E41444144ull);
    std::fputs(soak.report.c_str(), stdout);
    if (!soak.passed) {
        std::fprintf(stderr, "FATAL: the ward's economy is out of balance: %s\n",
                     soak.problem.c_str());
        return 1;
    }
    std::printf("\n  the ward fed itself for %lld days.\n", static_cast<long long>(soak.days));
    return 0;
}

int run_gate(const granadad::gate::WorkloadConfig& config) {
    granadad::gate::TwinRunOutcome outcome;
    try {
        outcome = granadad::gate::twin_run(config);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FATAL: the twin run could not complete: %s\n", error.what());
        return 1;
    }
    std::fputs(outcome.log.c_str(), stdout);
    // Non-zero on failure, and nothing about this is retryable. A determinism
    // gate that can be made green by running it again is not a gate.
    return outcome.passed ? 0 : 1;
}

std::int64_t parse_int(const char* text, std::int64_t fallback) {
    char* end = nullptr;
    const long long parsed = std::strtoll(text, &end, 10);
    if (end == text || *end != '\0') {
        return fallback;
    }
    return static_cast<std::int64_t>(parsed);
}

}  // namespace

int main(int argc, char** argv) {
    bool want_fingerprint = false;
    bool want_ward_soak = false;
    std::int64_t soak_days = 730;
    std::string output_path;
    granadad::gate::WorkloadConfig config;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == kFingerprintFlag) {
            want_fingerprint = true;
            if (i + 1 < argc) {
                output_path = argv[++i];
            }
        } else if (arg.starts_with("--fingerprint=")) {
            want_fingerprint = true;
            output_path = std::string(arg.substr(std::string_view("--fingerprint=").size()));
        } else if (arg == "--ticks" && i + 1 < argc) {
            config.ticks = parse_int(argv[++i], config.ticks);
        } else if (arg == "--walkers" && i + 1 < argc) {
            config.walkers = static_cast<std::int32_t>(parse_int(argv[++i], config.walkers));
        } else if (arg == "--world" && i + 1 < argc) {
            config.world = argv[++i];
        } else if (arg == "--ward-soak") {
            want_ward_soak = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                soak_days = parse_int(argv[++i], soak_days);
            }
        } else if (arg.starts_with("--ward-soak=")) {
            want_ward_soak = true;
            soak_days = parse_int(
                std::string(arg.substr(std::string_view("--ward-soak=").size())).c_str(),
                soak_days);
        } else if (arg == "--ward") {
            // The compounds registered as another system and ticked, so the
            // twin run compares the roll, the courtyards and the bonds the way
            // it already compares the taproom.
            config.with_ward = true;
            config.world = "docks_surface";
        } else if (arg == "--tavern") {
            // Registers the Gilded Gull and drives its movement clock. Forces
            // the world to docks_surface; see WorkloadConfig::with_tavern for
            // why this is a flag and not the default.
            config.with_tavern = true;
            config.world = "docks_surface";
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", argv[i]);
            std::fprintf(stderr,
                         "usage: granadad-twin-gate [--ticks N] [--walkers N] [--world NAME]\n"
                         "       granadad-twin-gate [--tavern] [--ward]\n"
                         "       granadad-twin-gate --ward-soak [DAYS]\n"
                         "       granadad-twin-gate --fingerprint FILE\n");
            return 2;
        }
    }

    print_banner();
    if (!content_dir_is_usable()) {
        return complain_about_content_dir();
    }

    if (want_ward_soak) {
        return run_ward_soak(soak_days);
    }
    return want_fingerprint ? run_fingerprint(output_path) : run_gate(config);
}
