// One binary, two jobs, on purpose.
//
//   granadad-content-tests                    -> the 57-case doctest suite
//   granadad-content-tests --fingerprint FILE -> the cross-toolchain report
//
// Two binaries would have been simpler to write and worse to trust: the whole
// question task #75 asks is whether THIS build of the reader decodes the same
// world state as the Linux build, and the answer is only worth something if the
// thing that emits the report is the same executable that ran the suite.
//
// The content directory comes from $GRANADAD_CONTENT_DIR at run time now (see
// fixtures.hpp). That is what makes the mingw .exe useful at all: the path
// compiled into it is the build container's, and it does not exist on Windows.

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>

#include "fingerprint.hpp"
#include "fixtures.hpp"

namespace {

constexpr std::string_view kFingerprintFlag = "--fingerprint";

/// The platform banner. Deliberately NOT part of the report file — this is the
/// information that is supposed to differ between the two runs.
void printBanner(const std::filesystem::path& contentDir) {
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
    std::printf("granadad-content-tests --fingerprint\n");
    std::printf("  platform:    %s\n", platform);
    std::printf("  compiler:    %s\n", compiler);
    std::printf("  content dir: %s\n", contentDir.string().c_str());
}

/// Fails early and legibly when the content directory is wrong.
///
/// Without this the first symptom of an unset GRANADAD_CONTENT_DIR on Windows
/// is 57 failing cases all complaining they cannot open a Linux path, which
/// reads like a broken reader rather than a broken invocation.
bool contentDirIsUsable(const std::filesystem::path& baked) {
    std::error_code ec;
    return std::filesystem::is_directory(baked, ec);
}

int complainAboutContentDir(const std::filesystem::path& baked) {
    std::fprintf(stderr,
                 "FATAL: no baked worlds at %s\n"
                 "       Set %s to the repo's content/ directory. On Windows:\n"
                 "           $env:%s = 'C:\\path\\to\\repo\\content'\n"
                 "       The path compiled into this binary is the build\n"
                 "       container's and does not exist here.\n",
                 baked.string().c_str(), granadad::content::testing::kContentDirEnvVar,
                 granadad::content::testing::kContentDirEnvVar);
    return 2;
}

int runFingerprint(const std::string& outputPath) {
    namespace testing = granadad::content::testing;
    const std::filesystem::path dir = testing::contentDir();
    const std::filesystem::path baked = dir / "maps" / "baked";
    printBanner(dir);
    if (!contentDirIsUsable(baked)) {
        return complainAboutContentDir(baked);
    }
    if (outputPath.empty()) {
        std::fprintf(stderr,
                     "FATAL: %s needs a file to write.\n"
                     "       Usage: granadad-content-tests %s <file>\n"
                     "       It is a file and not stdout on purpose: shells "
                     "rewrite line\n"
                     "       endings and re-encode redirected output, and this "
                     "report is\n"
                     "       compared between platforms byte for byte.\n",
                     std::string(kFingerprintFlag).c_str(), std::string(kFingerprintFlag).c_str());
        return 2;
    }
    std::string text;
    try {
        text = testing::fingerprintReport();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FATAL: could not build the fingerprint: %s\n", error.what());
        return 1;
    }
    std::string writeError;
    if (!testing::writeReportFile(outputPath, text, &writeError)) {
        std::fprintf(stderr, "FATAL: %s\n", writeError.c_str());
        return 1;
    }
    // %s and not %zu: mingw's printf and glibc's do not have identical format
    // support, and a banner is not worth a portability argument.
    const std::string sizeText = std::to_string(text.size());
    std::printf("  report:      %s (%s bytes)\n", outputPath.c_str(), sizeText.c_str());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    bool wantFingerprint = false;
    std::string outputPath;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == kFingerprintFlag) {
            wantFingerprint = true;
            if (i + 1 < argc) {
                outputPath = argv[++i];
            }
        } else if (arg.starts_with("--fingerprint=")) {
            wantFingerprint = true;
            outputPath = std::string(arg.substr(std::string_view("--fingerprint=").size()));
        }
    }
    if (wantFingerprint) {
        return runFingerprint(outputPath);
    }

    // The suite gets the same hint, but AFTER the fact rather than instead of
    // running. A pre-flight return would also break `--list-test-cases`, which
    // is how doctest_discover_tests enumerates the cases at build time, and a
    // gate that cannot enumerate its own tests is the failure mode this build
    // has already been bitten by once.
    doctest::Context context(argc, argv);
    const int result = context.run();
    if (result != 0) {
        const std::filesystem::path baked =
            granadad::content::testing::contentDir() / "maps" / "baked";
        if (!contentDirIsUsable(baked)) {
            complainAboutContentDir(baked);
        }
    }
    return result;
}
