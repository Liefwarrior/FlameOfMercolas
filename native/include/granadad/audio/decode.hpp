#pragma once

// OGG/Vorbis -> mono 48k float. The one decoder the audio engine has.
//
// The vendored Kenney set is OGG end to end (surveyed: zero WAV files), and
// SDL3 ships no Vorbis decoder — so this wraps stb_vorbis, which the repo's
// existing stb pin already contains. decode_vorbis.cpp is the single
// implementation TU, the same pattern as render/stb_impl.cpp.

#include <cstddef>
#include <optional>

#include "granadad/audio/sample.hpp"

namespace granadad::audio {

/// Decodes a whole OGG file's bytes to mono float PCM at kSampleRate
/// (channels averaged, linear-interp rate conversion). Returns std::nullopt on
/// malformed input — never throws, never aborts: a bad asset degrades to
/// silence, the same contract the tile atlas honours for missing art.
[[nodiscard]] std::optional<Sample> decodeOggToMono(const unsigned char* bytes,
                                                    std::size_t size);

/// THE LOT PASS: the same decode with the channels KEPT when the file is
/// stereo (Sample::stereo, interleaved) — for the music loops, which are
/// authored stereo. A mono file still lands in Sample::mono. Same contract
/// on malformed input.
[[nodiscard]] std::optional<Sample> decodeOgg(const unsigned char* bytes,
                                              std::size_t size, bool keepStereo);

/// Scales a decoded sample by `gainDb` in place (the manifest's per-row trim,
/// baked once at load so the mixer never carries a per-variant gain).
void applyGainDb(Sample& sample, float gainDb) noexcept;

}  // namespace granadad::audio
