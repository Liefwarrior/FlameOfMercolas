#include "granadad/content/pack_write.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <span>
#include <vector>

#include "granadad/content/crc32c.hpp"
#include "granadad/content/pack_extract.hpp"

// miniz's include path differs between the FetchContent build tree and an
// installed copy -- same dance as trojsav.cpp, same reason.
#if __has_include(<miniz/miniz.h>)
#include <miniz/miniz.h>
#else
#include <miniz.h>
#endif

namespace granadad::content {

namespace {

/// Every entry carries this one timestamp -- SOURCE_DATE_EPOCH's value from
/// docker/build.Dockerfile, hardcoded so a pack built anywhere says the same
/// thing. A real mtime here would make the same tree produce different bytes
/// on different days, and the standalone's whole claim is "strip, cat, done"
/// reproducibility.
constexpr MZ_TIME_T kFixedEntryTime = 1700000000;

void explain(std::string* error, std::string what) {
    if (error != nullptr) {
        *error = std::move(what);
    }
}

[[nodiscard]] bool readWholeFile(const std::filesystem::path& path,
                                 std::vector<unsigned char>* out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0) {
        return false;
    }
    in.seekg(0, std::ios::beg);
    out->resize(static_cast<std::size_t>(size));
    if (size > 0) {
        in.read(reinterpret_cast<char*>(out->data()),
                static_cast<std::streamsize>(out->size()));
    }
    return static_cast<bool>(in);
}

}  // namespace

bool writeContentPack(const std::filesystem::path& contentRoot,
                      std::vector<std::string> relPaths,
                      const std::filesystem::path& outFile,
                      std::string* error) {
    // Sorted BYTEWISE and deduplicated, so the entry order -- and therefore
    // the archive bytes -- are a function of the set of paths alone, never of
    // the order some directory walk happened to produce them in.
    std::sort(relPaths.begin(), relPaths.end());
    relPaths.erase(std::unique(relPaths.begin(), relPaths.end()), relPaths.end());
    if (relPaths.empty()) {
        explain(error, "refusing to write an empty content pack");
        return false;
    }

    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof zip);
    if (mz_zip_writer_init_heap(&zip, 0, 0) == MZ_FALSE) {
        explain(error, "cannot initialise the zip writer");
        return false;
    }

    bool ok = true;
    for (const std::string& rel : relPaths) {
        // The writer holds itself to the extractor's own rule, so a pack that
        // ships is a pack that extracts.
        if (!packEntryNameIsSafe(rel)) {
            explain(error, "refusing unsafe pack entry name: " + rel);
            ok = false;
            break;
        }
        std::vector<unsigned char> bytes;
        if (!readWholeFile(contentRoot / std::filesystem::path(rel), &bytes)) {
            // Refused, not skipped: a pack quietly missing a file would ship a
            // game quietly missing a sound.
            explain(error, "cannot read " + (contentRoot / rel).string());
            ok = false;
            break;
        }
        MZ_TIME_T fixed = kFixedEntryTime;
        // Compression level pinned, timestamp fixed: same tree in -> same
        // bytes out, which the suite proves by writing twice and comparing.
        if (mz_zip_writer_add_mem_ex_v2(
                &zip, rel.c_str(), bytes.data(), bytes.size(), nullptr, 0,
                MZ_BEST_COMPRESSION, 0, 0, &fixed, nullptr, 0, nullptr,
                0) == MZ_FALSE) {
            explain(error, "cannot add pack entry: " + rel);
            ok = false;
            break;
        }
    }

    void* buf = nullptr;
    std::size_t bufSize = 0;
    if (ok && mz_zip_writer_finalize_heap_archive(&zip, &buf, &bufSize) == MZ_FALSE) {
        explain(error, "cannot finalise the pack archive");
        ok = false;
    }
    mz_zip_writer_end(&zip);
    if (!ok) {
        if (buf != nullptr) {
            mz_free(buf);
        }
        return false;
    }

    PackFooter footer;
    footer.packSize = bufSize;
    footer.packCrc32c = crc32c(
        std::span<const std::uint8_t>(static_cast<const std::uint8_t*>(buf), bufSize));
    const std::array<std::uint8_t, kPackFooterSize> tail = buildPackFooter(footer);

    std::ofstream out(outFile, std::ios::binary | std::ios::trunc);
    out.write(static_cast<const char*>(buf), static_cast<std::streamsize>(bufSize));
    out.write(reinterpret_cast<const char*>(tail.data()),
              static_cast<std::streamsize>(tail.size()));
    mz_free(buf);
    if (!out) {
        explain(error, "cannot write " + outFile.string());
        return false;
    }
    return true;
}

}  // namespace granadad::content
