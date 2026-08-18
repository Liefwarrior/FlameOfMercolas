#include "granadad/content/pack_extract.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string_view>
#include <system_error>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/crc32c.hpp"

// miniz's include path differs between the FetchContent build tree and an
// installed copy -- same dance as trojsav.cpp, same reason.
#if __has_include(<miniz/miniz.h>)
#include <miniz/miniz.h>
#else
#include <miniz.h>
#endif

namespace granadad::content {

namespace {

void putU64le(std::uint8_t* out, std::uint64_t v) noexcept {
    for (int i = 0; i < 8; ++i) {
        out[i] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu);
    }
}

void putU32le(std::uint8_t* out, std::uint32_t v) noexcept {
    for (int i = 0; i < 4; ++i) {
        out[i] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu);
    }
}

[[nodiscard]] std::uint64_t getU64le(const std::uint8_t* in) noexcept {
    std::uint64_t v = 0;
    for (int i = 7; i >= 0; --i) {
        v = (v << 8) | in[i];
    }
    return v;
}

[[nodiscard]] std::uint32_t getU32le(const std::uint8_t* in) noexcept {
    std::uint32_t v = 0;
    for (int i = 3; i >= 0; --i) {
        v = (v << 8) | in[i];
    }
    return v;
}

void explain(std::string* error, std::string what) {
    if (error != nullptr) {
        *error = std::move(what);
    }
}

/// A name no two processes share, for the extract-then-rename dance.
[[nodiscard]] std::string uniqueSuffix() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_string(static_cast<long long>(now));
}

/// The uncached body of extractedPackDir().
[[nodiscard]] std::filesystem::path resolveExtractedPackDir() {
    const std::filesystem::path self = executablePath();
    if (self.empty()) {
        return {};
    }
    const std::optional<PackFooter> footer = readPackFooter(self);
    if (!footer.has_value()) {
        return {};  // an ordinary repo binary; the everyday case
    }
    const std::filesystem::path root = packCacheRoot();
    if (root.empty()) {
        return {};
    }
    const std::filesystem::path dest = root / packCacheDirName(*footer);

    std::error_code ec;
    if (std::filesystem::is_directory(dest / "maps" / "baked", ec)) {
        return dest;  // this exact pack is already extracted; nothing to do
    }

    // Extract into a sibling nobody else is writing, then rename into place.
    // If the rename loses a race to another first launch, the winner's tree is
    // just as good -- take it and clean up ours.
    const std::filesystem::path staging =
        root / (packCacheDirName(*footer) + ".extract-" + uniqueSuffix());
    std::string error;
    std::fprintf(stderr, "granadad: extracting embedded content pack -> %s\n",
                 dest.string().c_str());
    if (!extractPack(self, *footer, staging, &error)) {
        std::fprintf(stderr, "granadad: content pack extraction FAILED: %s\n",
                     error.c_str());
        std::filesystem::remove_all(staging, ec);
        return {};
    }
    std::filesystem::rename(staging, dest, ec);
    if (ec) {
        std::filesystem::remove_all(staging, ec);
        if (std::filesystem::is_directory(dest / "maps" / "baked", ec)) {
            return dest;  // somebody else finished first; same pack, same bytes
        }
        return {};
    }
    return dest;
}

}  // namespace

bool packEntryNameIsSafe(const std::string& name) {
    // The pack is our own build product, but a check this cheap does not get
    // to be a trust decision.
    if (name.empty() || name.front() == '/' || name.find('\\') != std::string::npos) {
        return false;
    }
    if (name.size() >= 2 && name[1] == ':') {
        return false;  // "C:..." -- a Windows drive is an absolute path too
    }
    std::size_t start = 0;
    while (start <= name.size()) {
        const std::size_t slash = name.find('/', start);
        const std::size_t end = slash == std::string::npos ? name.size() : slash;
        const std::string_view part(name.data() + start, end - start);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }
    return true;
}

std::optional<PackFooter> parsePackFooter(
    std::span<const std::uint8_t> tail) noexcept {
    if (tail.size() != kPackFooterSize) {
        return std::nullopt;
    }
    if (std::memcmp(tail.data(), kPackMagic.data(), kPackMagic.size()) != 0) {
        return std::nullopt;
    }
    const std::uint32_t selfCrc = getU32le(tail.data() + 20);
    if (crc32c(tail.first(20)) != selfCrc) {
        return std::nullopt;
    }
    PackFooter footer;
    footer.packSize = getU64le(tail.data() + 8);
    footer.packCrc32c = getU32le(tail.data() + 16);
    return footer;
}

std::array<std::uint8_t, kPackFooterSize> buildPackFooter(
    const PackFooter& footer) noexcept {
    std::array<std::uint8_t, kPackFooterSize> bytes{};
    std::memcpy(bytes.data(), kPackMagic.data(), kPackMagic.size());
    putU64le(bytes.data() + 8, footer.packSize);
    putU32le(bytes.data() + 16, footer.packCrc32c);
    putU32le(bytes.data() + 20,
             crc32c(std::span<const std::uint8_t>(bytes.data(), 20)));
    return bytes;
}

std::string packCacheDirName(const PackFooter& footer) {
    char crcHex[9];
    std::snprintf(crcHex, sizeof crcHex, "%08x", footer.packCrc32c);
    return std::string("content-") + crcHex + "-" + std::to_string(footer.packSize);
}

std::optional<PackFooter> readPackFooter(const std::filesystem::path& carrier) {
    std::ifstream in(carrier, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < static_cast<std::streamoff>(kPackFooterSize)) {
        return std::nullopt;
    }
    in.seekg(size - static_cast<std::streamoff>(kPackFooterSize));
    std::array<std::uint8_t, kPackFooterSize> tail{};
    in.read(reinterpret_cast<char*>(tail.data()),
            static_cast<std::streamsize>(tail.size()));
    if (!in) {
        return std::nullopt;
    }
    const std::optional<PackFooter> footer = parsePackFooter(tail);
    if (!footer.has_value()) {
        return std::nullopt;
    }
    // The footer must actually FIT: pack bytes plus footer inside the file.
    const std::uint64_t fileSize = static_cast<std::uint64_t>(size);
    if (footer->packSize > fileSize - kPackFooterSize) {
        return std::nullopt;
    }
    return footer;
}

bool extractPack(const std::filesystem::path& carrier, const PackFooter& footer,
                 const std::filesystem::path& destDir, std::string* error) {
    std::ifstream in(carrier, std::ios::binary);
    if (!in) {
        explain(error, "cannot open " + carrier.string());
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    const std::uint64_t fileSize = size < 0 ? 0 : static_cast<std::uint64_t>(size);
    // Subtraction, never addition: packSize is untrusted input here and an
    // addition could wrap a u64 into a small number that passes the check.
    if (fileSize < kPackFooterSize ||
        footer.packSize > fileSize - kPackFooterSize) {
        explain(error, "carrier is smaller than the pack its footer describes");
        return false;
    }
    const std::uint64_t packStart = fileSize - kPackFooterSize - footer.packSize;
    std::vector<std::uint8_t> pack(static_cast<std::size_t>(footer.packSize));
    in.seekg(static_cast<std::streamoff>(packStart));
    in.read(reinterpret_cast<char*>(pack.data()),
            static_cast<std::streamsize>(pack.size()));
    if (!in) {
        explain(error, "short read of the pack bytes");
        return false;
    }
    // Verified BEFORE anything touches the disk: a truncated download must
    // fail here, loudly, not extract half a content tree that half works.
    if (crc32c(std::span<const std::uint8_t>(pack.data(), pack.size())) !=
        footer.packCrc32c) {
        explain(error, "pack CRC mismatch -- the file is corrupt or truncated");
        return false;
    }

    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof zip);
    if (mz_zip_reader_init_mem(&zip, pack.data(), pack.size(), 0) == MZ_FALSE) {
        explain(error, "the pack is not a readable zip archive");
        return false;
    }
    bool ok = true;
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; ok && i < count; ++i) {
        mz_zip_archive_file_stat stat;
        if (mz_zip_reader_file_stat(&zip, i, &stat) == MZ_FALSE) {
            explain(error, "cannot stat pack entry " + std::to_string(i));
            ok = false;
            break;
        }
        if (mz_zip_reader_is_file_a_directory(&zip, i) == MZ_TRUE) {
            continue;  // directories are implied by the file paths
        }
        const std::string name(stat.m_filename);
        if (!packEntryNameIsSafe(name)) {
            explain(error, "refusing unsafe pack entry name: " + name);
            ok = false;
            break;
        }
        std::size_t bytes = 0;
        void* data = mz_zip_reader_extract_to_heap(&zip, i, &bytes, 0);
        if (data == nullptr) {
            explain(error, "cannot inflate pack entry: " + name);
            ok = false;
            break;
        }
        // Written through std::filesystem paths, not through miniz's own
        // fopen: on Windows the cache root can hold non-ASCII (a user name),
        // and the narrow C path would go through the ANSI code page.
        const std::filesystem::path target = destDir / std::filesystem::path(name);
        std::error_code ec;
        std::filesystem::create_directories(target.parent_path(), ec);
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        out.write(static_cast<const char*>(data),
                  static_cast<std::streamsize>(bytes));
        mz_free(data);
        if (!out) {
            explain(error, "cannot write " + target.string());
            ok = false;
            break;
        }
    }
    mz_zip_reader_end(&zip);
    return ok;
}

std::filesystem::path packCacheRoot() {
#if defined(_WIN32)
    const char* base = std::getenv("LOCALAPPDATA");
    if (base == nullptr || *base == '\0') {
        return {};
    }
    return std::filesystem::path(base) / "Granadad";
#else
    // XDG first, the ~/.cache convention second. Lowercase on this side --
    // each platform gets its own native casing convention.
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    if (xdg != nullptr && *xdg != '\0') {
        return std::filesystem::path(xdg) / "granadad";
    }
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".cache" / "granadad";
    }
    return {};
#endif
}

std::filesystem::path extractedPackDir() {
    // Once per process: the executable's own bytes do not change while it
    // runs, and contentDir() is called by every loader.
    static const std::filesystem::path resolved = resolveExtractedPackDir();
    return resolved;
}

}  // namespace granadad::content
