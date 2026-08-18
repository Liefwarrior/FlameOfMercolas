// The appended content pack -- the mechanism behind granadad-standalone.exe.
//
// These cases prove the round trip the shareable build rests on, against
// SYNTHETIC trees and synthetic carriers: a pack written from a tree, appended
// to arbitrary bytes exactly the way the packer appends it to the stripped
// exe, found again through the footer, and extracted to the same bytes that
// went in. Determinism gets its own case -- write the same tree twice and the
// pack must be byte-identical, because the docker build sha256s every artifact
// and the cache directory is KEYED on those bytes.
//
// Nothing here touches the repo's content/, which is read-only canon. Every
// tree is a throwaway under the system temp directory.

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

#include "granadad/content/pack_extract.hpp"
#include "granadad/content/pack_write.hpp"

namespace fs = std::filesystem;

using granadad::content::PackFooter;
using granadad::content::buildPackFooter;
using granadad::content::extractPack;
using granadad::content::kPackFooterSize;
using granadad::content::packCacheDirName;
using granadad::content::packEntryNameIsSafe;
using granadad::content::parsePackFooter;
using granadad::content::readPackFooter;
using granadad::content::writeContentPack;

namespace {

/// A throwaway directory tree that cleans itself up -- test_content_dir.cpp's
/// own helper, with a file-writing verb added.
class TempTree {
public:
    explicit TempTree(const std::string& name)
        : root_(fs::temp_directory_path() / ("granadad-pack-" + name)) {
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

    void file(const std::string& relative, const std::string& bytes) const {
        const fs::path target = root_ / relative;
        std::error_code error;
        fs::create_directories(target.parent_path(), error);
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

private:
    fs::path root_;
};

[[nodiscard]] std::vector<std::uint8_t> slurp(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(static_cast<bool>(in));
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                     std::istreambuf_iterator<char>());
}

void dump(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    REQUIRE(static_cast<bool>(out));
}

/// The synthetic content subset every case here packs: nested directories,
/// a binary-ish file with a zero byte in it, and an empty file.
void authorTree(const TempTree& tree) {
    tree.file("maps/baked/tiny.trojsav", std::string("TROJSAV\x00 bytes", 15));
    tree.file("raws/skills/skills.json", "{\"skills\":[]}\n");
    tree.file("art/custom/tiles.png", "not really a png but real bytes");
    tree.file("art/kenney-all-in-1/Audio/RPG Audio/Audio/chop.ogg", "OggS fake");
    tree.file("empty.txt", "");
}

[[nodiscard]] std::vector<std::string> treeManifest() {
    return {
        "maps/baked/tiny.trojsav",
        "raws/skills/skills.json",
        "art/custom/tiles.png",
        "art/kenney-all-in-1/Audio/RPG Audio/Audio/chop.ogg",
        "empty.txt",
    };
}

}  // namespace

TEST_CASE("the pack footer round-trips and refuses every corrupted shape") {
    PackFooter footer;
    footer.packSize = 0x0123456789ABCDEFull;
    footer.packCrc32c = 0xDEADBEEFu;

    const std::array<std::uint8_t, kPackFooterSize> bytes = buildPackFooter(footer);
    const auto parsed = parsePackFooter(bytes);
    REQUIRE(parsed.has_value());
    CHECK(parsed->packSize == footer.packSize);
    CHECK(parsed->packCrc32c == footer.packCrc32c);

    // Every single-byte corruption is refused: the magic, the fields (whose
    // self-CRC stops agreeing) and the self-CRC itself.
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        std::array<std::uint8_t, kPackFooterSize> bent = bytes;
        bent[i] ^= 0x01u;
        CHECK_FALSE(parsePackFooter(bent).has_value());
    }
    // And a buffer of any other size is not a footer at all.
    CHECK_FALSE(parsePackFooter(std::span<const std::uint8_t>(bytes.data(),
                                                              bytes.size() - 1))
                    .has_value());
}

TEST_CASE("a pack written twice from one tree is byte-identical, in any list order") {
    const TempTree tree("determinism");
    authorTree(tree);

    const fs::path a = tree.root() / "a.pack";
    const fs::path b = tree.root() / "b.pack";
    std::string error;
    REQUIRE(writeContentPack(tree.root(), treeManifest(), a, &error));

    // The same set of paths handed over in REVERSE, with a duplicate thrown
    // in: the writer owns the ordering, so the bytes may not move.
    std::vector<std::string> shuffled = treeManifest();
    std::reverse(shuffled.begin(), shuffled.end());
    shuffled.push_back("empty.txt");
    REQUIRE(writeContentPack(tree.root(), shuffled, b, &error));

    CHECK(slurp(a) == slurp(b));
}

TEST_CASE("append to a carrier, find through the footer, extract the same bytes") {
    const TempTree tree("roundtrip");
    authorTree(tree);

    const fs::path packFile = tree.root() / "content.pack";
    std::string error;
    REQUIRE(writeContentPack(tree.root(), treeManifest(), packFile, &error));

    // The carrier is "an exe": arbitrary leading bytes, then the pack file
    // exactly as written -- which is exactly what the packer tool does to the
    // stripped granadad.exe.
    const std::vector<std::uint8_t> packBytes = slurp(packFile);
    std::vector<std::uint8_t> carrierBytes = {'M', 'Z', 0x90, 0x00, 0x03, 0x77};
    carrierBytes.insert(carrierBytes.end(), packBytes.begin(), packBytes.end());
    const fs::path carrier = tree.root() / "carrier.exe";
    dump(carrier, carrierBytes);

    const auto footer = readPackFooter(carrier);
    REQUIRE(footer.has_value());
    CHECK(footer->packSize == packBytes.size() - kPackFooterSize);
    CHECK(packCacheDirName(*footer).rfind("content-", 0) == 0);

    const fs::path dest = tree.root() / "extracted";
    REQUIRE(extractPack(carrier, *footer, dest, &error));
    for (const std::string& rel : treeManifest()) {
        CAPTURE(rel);
        REQUIRE(fs::is_regular_file(dest / rel));
        CHECK(slurp(dest / rel) == slurp(tree.root() / rel));
    }

    // A truncated download must fail LOUDLY, before anything touches the
    // disk: chop one byte out of the pack body and re-append the footer.
    std::vector<std::uint8_t> truncated(carrierBytes.begin(),
                                        carrierBytes.end() - kPackFooterSize - 1);
    truncated.insert(truncated.end(), carrierBytes.end() - kPackFooterSize,
                     carrierBytes.end());
    const fs::path corrupt = tree.root() / "corrupt.exe";
    dump(corrupt, truncated);
    const auto corruptFooter = readPackFooter(corrupt);
    if (corruptFooter.has_value()) {
        CHECK_FALSE(extractPack(corrupt, *corruptFooter,
                                tree.root() / "never-written", &error));
        CHECK_FALSE(fs::exists(tree.root() / "never-written"));
    }
}

TEST_CASE("an ordinary binary carries no footer, and that is not an error") {
    // This is every repo checkout's granadad.exe, and the everyday branch of
    // contentDir(): the tail bytes of a normal file must never parse.
    const TempTree tree("no-footer");
    tree.file("plain.exe", std::string(4096, 'x'));
    CHECK_FALSE(readPackFooter(tree.root() / "plain.exe").has_value());
    tree.file("short.bin", "tiny");
    CHECK_FALSE(readPackFooter(tree.root() / "short.bin").has_value());
}

TEST_CASE("the writer refuses a missing file rather than shipping without it") {
    const TempTree tree("refusal");
    authorTree(tree);

    std::vector<std::string> manifest = treeManifest();
    manifest.push_back("raws/skills/not-actually-there.json");
    std::string error;
    CHECK_FALSE(writeContentPack(tree.root(), manifest, tree.root() / "out.pack",
                                 &error));
    CHECK(error.find("not-actually-there") != std::string::npos);

    // And an empty manifest is refused too -- a pack with nothing in it would
    // ship a game with nothing in it.
    CHECK_FALSE(writeContentPack(tree.root(), {}, tree.root() / "out.pack", &error));
}

TEST_CASE("entry names that could escape the cache are refused on both sides") {
    CHECK(packEntryNameIsSafe("maps/baked/docks_surface.trojsav"));
    CHECK(packEntryNameIsSafe("art/kenney-all-in-1/Audio/RPG Audio/Audio/chop.ogg"));
    CHECK_FALSE(packEntryNameIsSafe(""));
    CHECK_FALSE(packEntryNameIsSafe("/etc/passwd"));
    CHECK_FALSE(packEntryNameIsSafe("..\\up"));
    CHECK_FALSE(packEntryNameIsSafe("../up"));
    CHECK_FALSE(packEntryNameIsSafe("raws/../../up"));
    CHECK_FALSE(packEntryNameIsSafe("raws//double"));
    CHECK_FALSE(packEntryNameIsSafe("C:evil"));
    CHECK_FALSE(packEntryNameIsSafe("raws/."));
    CHECK_FALSE(packEntryNameIsSafe("raws/"));
}
