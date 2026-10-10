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

/// THE LOT PASS: the SECOND audio root, relative to contentDir(). The staged
/// Lord-of-Trojia audio (tools/lot-pipeline/audio-manifest.json,
/// docs/asset-manifest-lot.md) lives here. It is GITIGNORED and licensed
/// per-seat, so it is never in the docker build context and never in the
/// standalone pack: the docker gate has no LOT audio, and NOTHING may require
/// a LOT file. Every LOT row is a replacement or an addition over the Kenney
/// rows, resolved at load: an id with any LOT variant on disk speaks LOT; an
/// id with none falls back to its Kenney rows, or to silence where it has
/// none (PlayerHurt, the beds' loops...). Never a crash.
inline constexpr std::string_view kLotAudioRootRel = "art/lot/audio";

/// The manifest: every variant file for `id`, relative to kAudioRootRel.
/// The Kenney rows — the ones the docker gate and the standalone pack carry.
/// May be empty for a LOT-only id (see lotSoundPaths); the test suite checks
/// each named file exists wherever the Kenney tree is vendored.
[[nodiscard]] std::span<const std::string_view> soundPaths(SoundId id) noexcept;

/// One LOT row: a file relative to kLotAudioRootRel and the trim it is
/// played at, in dB, BAKED INTO THE PCM at load. The manifest's gainDb is the
/// only loudness knob the pipeline has (no loudnorm, no limiter); these mirror
/// it, set from the measured peaks in docs/asset-manifest-lot.md.
struct LotSoundFile {
    std::string_view rel;
    float gainDb;
};

/// The LOT rows for `id` — empty for an id the LOT pass does not touch.
[[nodiscard]] std::span<const LotSoundFile> lotSoundPaths(SoundId id) noexcept;

/// The music track file for `id`, relative to kLotAudioRootRel; empty for
/// TrackId::None. Tracks are loaded by loadTrack, never by SoundBank::load.
[[nodiscard]] std::string_view trackPath(TrackId id) noexcept;

/// Decodes one music track (STEREO kept — see Sample) under contentDir's LOT
/// root. nullptr when the file is absent or malformed: the director then
/// simply has no music for that cue, and the game keeps going. Slow (a
/// two-minute loop is ~1 s of stb_vorbis) — the director calls it off the
/// game thread.
[[nodiscard]] std::shared_ptr<const Sample> loadTrack(
    const std::filesystem::path& contentDir, TrackId id);

class SoundBank {
public:
    /// Decodes every manifest file under `contentDir`, LOT rows first and
    /// Kenney rows for whatever the LOT tree did not cover. Missing/malformed
    /// files degrade to silence with one stderr summary line per root; never
    /// throws.
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

    /// KENNEY manifest files that failed to load in load() (0 for
    /// empty()/synthetic()). LOT rows are counted separately below, because
    /// their absence is the docker gate's normal state, not a finding.
    [[nodiscard]] std::size_t missingFiles() const noexcept { return missing_; }

    /// LOT rows that loaded / that did not. lotFiles() == 0 on every checkout
    /// without content/art/lot/audio, and the bank is exactly what it was
    /// before the LOT pass there.
    [[nodiscard]] std::size_t lotFiles() const noexcept { return lotLoaded_; }
    [[nodiscard]] std::size_t lotMissingFiles() const noexcept { return lotMissing_; }

private:
    std::array<std::vector<std::shared_ptr<const Sample>>, kSoundIdCount> variants_;
    std::size_t missing_ = 0;
    std::size_t lotLoaded_ = 0;
    std::size_t lotMissing_ = 0;
};

}  // namespace granadad::audio
