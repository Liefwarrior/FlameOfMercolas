// The content-directory resolver, which is the reason the shipped game starts.
//
// S1 shipped a dist\granadad.exe that could not open its own world:
//
//     granadad: cannot open TROJSAV: /src/content\maps\baked\docks_surface.trojsav
//
// /src/content is the BUILD CONTAINER's path. Nothing sets
// $GRANADAD_CONTENT_DIR for the game -- scripts/verify-windows.ps1 sets it for
// the test binaries only -- so the compile-time default was the whole answer,
// and it named a directory that exists on no machine the binary ships to.
//
// The fix is that the executable finds its own data. These cases exercise the
// search against a SYNTHETIC tree rather than against wherever the test binary
// was built, so they mean the same thing in the container and on the owner's
// machine.

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "granadad/content/content_dir.hpp"

namespace fs = std::filesystem;

namespace {

/// A throwaway directory tree that cleans itself up. Named from the test's own
/// name so two cases running back to back cannot collide.
class TempTree {
public:
    explicit TempTree(const std::string& name)
        : root_(fs::temp_directory_path() / ("granadad-content-dir-" + name)) {
        std::error_code error;
        fs::remove_all(root_, error);
        fs::create_directories(root_, error);
    }
    ~TempTree() {
        std::error_code error;
        fs::remove_all(root_, error);
    }
    TempTree(const TempTree&) = delete;
    TempTree& operator=(const TempTree&) = delete;

    [[nodiscard]] const fs::path& root() const noexcept { return root_; }

    /// Creates `relative` as a directory and returns it.
    fs::path dir(const std::string& relative) const {
        const fs::path made = root_ / relative;
        std::error_code error;
        fs::create_directories(made, error);
        return made;
    }

private:
    fs::path root_;
};

}  // namespace

TEST_CASE("the executable finds a content tree one level above itself") {
    // This is dist/ exactly: <repo>/dist/granadad.exe, <repo>/content/maps/baked.
    const TempTree tree("upward");
    const fs::path dist = tree.dir("dist");
    tree.dir("content/maps/baked");

    const fs::path found = granadad::content::searchForContentDir(dist);
    REQUIRE_FALSE(found.empty());
    CHECK(fs::equivalent(found, tree.root() / "content"));
}

TEST_CASE("a content tree beside the executable wins over one further up") {
    // An installed build: content/ ships next to the binary. The nearer tree is
    // the right one, and finding the further one first would silently load
    // somebody else's worlds.
    const TempTree tree("nearest");
    const fs::path app = tree.dir("app");
    tree.dir("app/content/maps/baked");
    tree.dir("content/maps/baked");

    const fs::path found = granadad::content::searchForContentDir(app);
    REQUIRE_FALSE(found.empty());
    CHECK(fs::equivalent(found, app / "content"));
}

TEST_CASE("the executable's own directory may itself be the content tree") {
    const TempTree tree("self");
    const fs::path here = tree.dir("payload");
    tree.dir("payload/maps/baked");

    const fs::path found = granadad::content::searchForContentDir(here);
    REQUIRE_FALSE(found.empty());
    CHECK(fs::equivalent(found, here));
}

TEST_CASE("a directory named content without baked worlds is not a content tree") {
    // The test is on maps/baked, not on the word "content". A half-copied tree
    // must fail here rather than load a world and then not find the next one.
    const TempTree tree("empty");
    const fs::path dist = tree.dir("dist");
    tree.dir("content/art");

    CHECK(granadad::content::searchForContentDir(dist).empty());
}

TEST_CASE("the search gives up rather than climbing to the root of the disk") {
    // Three levels, and the tree is deliberately deeper than that. Without a
    // limit this walks to C:\ or / and picks up whatever content directory it
    // meets on the way.
    const TempTree tree("depth");
    const fs::path deep = tree.dir("a/b/c/d/e");
    tree.dir("content/maps/baked");

    CHECK(granadad::content::searchForContentDir(deep).empty());
}

TEST_CASE("an empty starting directory resolves to nothing") {
    CHECK(granadad::content::searchForContentDir({}).empty());
}

TEST_CASE("the environment variable outranks the executable's own tree") {
    // The order matters and the docker build depends on it: it proves the
    // variable is honoured by pointing it at a relocated copy and at a bogus
    // path. If the exe-relative search came first, both of those proofs would
    // quietly become proofs about the build container's /src/content.
    const TempTree tree("override");
    const fs::path chosen = tree.dir("elsewhere");

    const char* previous = std::getenv(granadad::content::kContentDirEnvVar);
    const std::string saved = previous == nullptr ? std::string() : std::string(previous);

#if defined(_WIN32)
    _putenv_s(granadad::content::kContentDirEnvVar, chosen.string().c_str());
#else
    setenv(granadad::content::kContentDirEnvVar, chosen.string().c_str(), 1);
#endif
    CHECK(granadad::content::contentDir() == chosen);

#if defined(_WIN32)
    _putenv_s(granadad::content::kContentDirEnvVar, saved.c_str());
#else
    if (saved.empty()) {
        unsetenv(granadad::content::kContentDirEnvVar);
    } else {
        setenv(granadad::content::kContentDirEnvVar, saved.c_str(), 1);
    }
#endif
}

TEST_CASE("executableDir names a directory that actually exists") {
    // Weak on purpose -- the path differs on every machine. The claim is only
    // that the platform answered at all, which is what the search needs.
    const fs::path where = granadad::content::executableDir();
    REQUIRE_FALSE(where.empty());
    CHECK(fs::is_directory(where));
}
