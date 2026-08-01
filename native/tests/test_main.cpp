// The sim suite's entry point.
//
// Separate from the cases so every other file in tests/ is nothing but cases.
// It also gets to say something useful when the failure is the invocation and
// not the code: tests/test_world_hash.cpp opens the real baked worlds, and an
// unset GRANADAD_CONTENT_DIR on Windows would otherwise surface as a pile of
// filesystem errors that read like a broken reader.
//
// The hint is printed AFTER the run, never instead of it. A pre-flight return
// would also break --list-test-cases, which is how doctest_discover_tests
// enumerates the cases at build time, and a gate that cannot enumerate its own
// tests is a failure mode this build has already been bitten by once.

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <cstdio>
#include <filesystem>

#include "granadad/content/content_dir.hpp"

int main(int argc, char** argv) {
    doctest::Context context(argc, argv);
    const int result = context.run();
    if (result != 0) {
        std::error_code ec;
        const std::filesystem::path baked = granadad::content::bakedDir();
        if (!std::filesystem::is_directory(baked, ec)) {
            std::fprintf(stderr,
                         "\nFATAL: no baked worlds at %s\n"
                         "       Set %s to the repo's content/ directory. On Windows:\n"
                         "           $env:%s = 'C:\\path\\to\\repo\\content'\n"
                         "       The path compiled into this binary is the build\n"
                         "       container's and does not exist here.\n",
                         baked.string().c_str(), granadad::content::kContentDirEnvVar,
                         granadad::content::kContentDirEnvVar);
        }
    }
    return result;
}
