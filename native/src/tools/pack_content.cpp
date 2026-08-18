// The content packer -- the build-time tool behind granadad-standalone.exe.
//
//     granadad-pack-content --content <dir> --out <pack>
//
// Same standing as bake_lamps and the audio probe: a dev tool, run by the
// docker build, no window. It assembles the MINIMAL content subset the exe
// actually opens into one deterministic zip closed by the pack footer
// (pack_extract.hpp), ready to be concatenated onto a stripped exe as-is.
//
// WHAT GOES IN, AND WHY EXACTLY THIS:
//
//   raws/            whole, recursively. Three loaders ENUMERATE directories
//                    (barks, quests twice, ward actors), so byte-identical
//                    directory contents is the cheap way to guarantee the
//                    standalone behaves identically to the repo exe. ~607 KB.
//   maps/baked/      whole. The .trojsav worlds are the one thing boot dies
//                    without, plus their .lamps.json sidecars. ~25 KB.
//   art/custom/      the two files atlas.cpp opens: art-mapping.json,
//                    tiles.png. Without them the game draws the procedural
//                    fallback, which is the one thing that never ships.
//   art/sprites/     the two files actor_sheet.cpp opens: sprite-index.json,
//                    sprites.png.
//   the audio        EVERY file soundPaths() names, and NOTHING else. The
//   manifest         subset is the compiled manifest itself, so it cannot
//                    drift: a sound added to sound_bank.cpp is in the next
//                    pack by construction, and a manifest file absent from
//                    the tree fails THIS tool loudly rather than shipping a
//                    game quietly missing a sound. ~2 MB of a 1.2 GB tree.
//
// The kenney dump as a whole never comes in. The Audio subtree alone is
// 31 MB and the game opens 2 MB of it.
//
// Determinism is writeContentPack's contract (sorted entries, fixed
// timestamps, pinned compression): same tree in, same bytes out, so the
// pack digest -- and with it the per-user cache directory name -- moves
// only when the content itself does.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "granadad/audio/sound_bank.hpp"
#include "granadad/audio/sound_ids.hpp"
#include "granadad/content/pack_extract.hpp"
#include "granadad/content/pack_write.hpp"

namespace fs = std::filesystem;

namespace {

/// Every regular file under contentRoot/relDir, as forward-slash paths
/// relative to contentRoot. Order does not matter -- the writer sorts -- but
/// existence does: a missing directory is a broken build context, not a
/// smaller pack.
[[nodiscard]] bool collectTree(const fs::path& contentRoot, const std::string& relDir,
                               std::vector<std::string>* out) {
    const fs::path root = contentRoot / fs::path(relDir);
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        std::fprintf(stderr, "granadad-pack-content: %s is not a directory under %s\n",
                     relDir.c_str(), contentRoot.string().c_str());
        return false;
    }
    for (fs::recursive_directory_iterator it(root, ec), end; it != end && !ec;
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) {
            continue;
        }
        const std::string rel =
            fs::relative(it->path(), contentRoot, ec).generic_string();
        if (ec) {
            std::fprintf(stderr, "granadad-pack-content: cannot relativise %s\n",
                         it->path().string().c_str());
            return false;
        }
        out->push_back(rel);
    }
    if (ec) {
        std::fprintf(stderr, "granadad-pack-content: walking %s failed: %s\n",
                     relDir.c_str(), ec.message().c_str());
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    fs::path contentRoot;
    fs::path outFile;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--content" && i + 1 < argc) {
            contentRoot = fs::path(argv[++i]);
        } else if (arg == "--out" && i + 1 < argc) {
            outFile = fs::path(argv[++i]);
        } else {
            std::fprintf(stderr,
                         "usage: granadad-pack-content --content <dir> --out <pack>\n");
            return 2;
        }
    }
    if (contentRoot.empty() || outFile.empty()) {
        std::fprintf(stderr,
                     "usage: granadad-pack-content --content <dir> --out <pack>\n");
        return 2;
    }

    std::vector<std::string> manifest;

    // The two subtrees taken whole, and the reason is in the file header:
    // enumerated directories must match the repo byte for byte.
    if (!collectTree(contentRoot, "raws", &manifest) ||
        !collectTree(contentRoot, "maps/baked", &manifest)) {
        return 1;
    }

    // The four art files the two loaders open by name. Existence is checked by
    // writeContentPack, which refuses rather than skips.
    manifest.emplace_back("art/custom/art-mapping.json");
    manifest.emplace_back("art/custom/tiles.png");
    manifest.emplace_back("art/sprites/sprite-index.json");
    manifest.emplace_back("art/sprites/sprites.png");

    // The audio subset IS the compiled manifest: every variant of every
    // SoundId, under the same root sound_bank.cpp resolves them against.
    const std::string audioRoot(granadad::audio::kAudioRootRel);
    std::size_t audioFiles = 0;
    for (std::size_t i = 0; i < granadad::audio::kSoundIdCount; ++i) {
        const auto id = static_cast<granadad::audio::SoundId>(i);
        for (const std::string_view rel : granadad::audio::soundPaths(id)) {
            manifest.push_back(audioRoot + "/" + std::string(rel));
            ++audioFiles;
        }
    }

    std::string error;
    if (!granadad::content::writeContentPack(contentRoot, manifest, outFile, &error)) {
        std::fprintf(stderr, "granadad-pack-content: %s\n", error.c_str());
        return 1;
    }

    // Read our own product back through the extractor's footer parse, so what
    // gets printed -- and recorded in the build manifest -- is what the
    // shipped exe will actually see at boot.
    const auto footer = granadad::content::readPackFooter(outFile);
    if (!footer.has_value()) {
        std::fprintf(stderr,
                     "granadad-pack-content: the pack just written carries no "
                     "readable footer -- refusing to trust it\n");
        return 1;
    }
    std::printf("pack file:    %s\n", outFile.string().c_str());
    std::printf("pack entries: %zu (%zu audio manifest files)\n", manifest.size(),
                audioFiles);
    std::printf("pack bytes:   %llu (+%zu footer)\n",
                static_cast<unsigned long long>(footer->packSize),
                granadad::content::kPackFooterSize);
    std::printf("pack cache:   %s\n",
                granadad::content::packCacheDirName(*footer).c_str());
    return 0;
}
