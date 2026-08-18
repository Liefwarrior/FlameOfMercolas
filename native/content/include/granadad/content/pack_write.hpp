#pragma once

// The DETERMINISTIC pack writer -- the build-time half of pack_extract.hpp.
//
// Lives in granadad-content rather than in the packer tool so that the
// round trip (write a pack, read it back through the same footer parse and
// extraction the shipped exe uses) is testable headless in this module's own
// suite, and so the tool under native/src/tools/ stays a thin main().
//
// Determinism is a requirement, not a nicety: the docker build stamps and
// sha256s every artifact, and a pack whose bytes moved with the clock or the
// directory-walk order would make granadad-standalone.exe unreproducible.
// So: entry paths are sorted bytewise, every entry carries the same fixed
// timestamp (SOURCE_DATE_EPOCH's spirit, hardcoded), and the compression
// level is pinned. Same tree in -> same bytes out, and the suite proves it.

#include <filesystem>
#include <string>
#include <vector>

namespace granadad::content {

/// Zips `relPaths` (forward-slash paths relative to `contentRoot`, duplicates
/// tolerated and dropped) into `outFile` and closes it with the
/// pack_extract.hpp footer -- so the output is ready to be concatenated onto
/// an executable as-is. Refuses (false, *error explains) on the first path
/// that does not name a readable regular file: a pack quietly missing a file
/// would ship a game quietly missing a sound.
[[nodiscard]] bool writeContentPack(const std::filesystem::path& contentRoot,
                                    std::vector<std::string> relPaths,
                                    const std::filesystem::path& outFile,
                                    std::string* error);

}  // namespace granadad::content
