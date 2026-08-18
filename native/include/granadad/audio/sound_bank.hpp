#pragma once

// The manifest and the decoded samples behind it.
//
// SoundId -> real files: the mapping lives HERE, in native/ code, because
// content/ is read-only canon — no manifest json is added to it. The paths
// point at the vendored CC0 Kenney packs under
// content/art/kenney-all-in-1/Audio, resolved against
// granadad::content::contentDir() at load time.
//
// WHEN THE FILES ARE NOT THERE. Same contract as the tile atlas: a checkout
// without content/art still has to run, and the docker build context
// deliberately excludes the Kenney tree. load() therefore degrades per-file to
// silence — a missing or malformed file simply contributes no variant, one
// summary line goes to stderr, and nothing throws. An id with zero loaded
// variants plays nothing.
//
// Everything is decoded up front (mono float 48k, see decode.hpp). The whole
// vocabulary is ~170 small OGGs; paying the decode once at init keeps the
// mixer allocation-free at play time.

#include <array>
#include <filesystem>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "granadad/audio/sample.hpp"
#include "granadad/audio/sound_ids.hpp"

namespace granadad::audio {

/// Where the vendored CC0 audio lives, relative to contentDir().
inline constexpr std::string_view kAudioRootRel = "art/kenney-all-in-1/Audio";

/// The manifest: every variant file for `id`, relative to kAudioRootRel.
/// Never empty for a valid id — the test suite iterates the whole vocabulary
/// over this and checks each file exists when the Kenney tree is vendored.
[[nodiscard]] std::span<const std::string_view> soundPaths(SoundId id) noexcept;

class SoundBank {
public:
    /// Decodes every manifest file under `contentDir`. Missing/malformed files
    /// degrade to silence with one stderr summary line; never throws.
    [[nodiscard]] static SoundBank load(const std::filesystem::path& contentDir);

    /// A bank with nothing in it. Everything played through it is silence.
    [[nodiscard]] static SoundBank empty();

    /// A bank of GENERATED blips (two per id, deterministic, no files) so
    /// tests and the offline probe can exercise real voices on checkouts —
    /// the docker gate included — that have no content/art at all.
    [[nodiscard]] static SoundBank synthetic();

    /// How many variants actually loaded for `id` (0 = that id is silent).
    [[nodiscard]] std::size_t variantCount(SoundId id) const noexcept;

    /// Variant `variant` of `id`; nullptr when out of range / not loaded.
    [[nodiscard]] std::shared_ptr<const Sample> sample(SoundId id,
                                                       std::size_t variant) const noexcept;

    [[nodiscard]] bool anyLoaded() const noexcept;

    /// Manifest files that failed to load in load() (0 for empty()/synthetic()).
    [[nodiscard]] std::size_t missingFiles() const noexcept { return missing_; }

private:
    std::array<std::vector<std::shared_ptr<const Sample>>, kSoundIdCount> variants_;
    std::size_t missing_ = 0;
};

}  // namespace granadad::audio
