#pragma once

// A decoded sound: float PCM at the mixer's own rate.
//
// Everything is normalised at LOAD time — sample rate converted, and for
// every one-shot the channels downmixed — so the mixer's hot loop never
// branches on format beyond one mono/stereo pick per voice. Pan is applied at
// mix time (constant-power), which is why mono is enough for the one-shots:
// none of the vendored one-shots carries meaningful stereo, and a positional
// pan computed from the game beats a baked one anyway.
//
// THE LOT PASS added the one exception: MUSIC. The Dark Fantasy loops are
// authored stereo and a downmix flattens them, so a Sample may instead carry
// interleaved stereo. Exactly one of the two vectors is non-empty.

#include <cstddef>
#include <vector>

namespace granadad::audio {

/// The engine's one sample rate. Everything is converted to this at load.
inline constexpr int kSampleRate = 48000;

/// The engine's output channel count (interleaved stereo).
inline constexpr int kChannels = 2;

struct Sample {
    /// Mono PCM at kSampleRate, nominally in [-1, 1]. Empty for a stereo sample.
    std::vector<float> mono;
    /// Interleaved L/R PCM at kSampleRate. Non-empty ONLY for music tracks.
    std::vector<float> stereo;

    [[nodiscard]] bool isStereo() const noexcept { return !stereo.empty(); }

    /// Frames, whichever layout is carried. 0 means "nothing to play".
    [[nodiscard]] std::size_t frames() const noexcept {
        return stereo.empty() ? mono.size() : stereo.size() / 2U;
    }
};

}  // namespace granadad::audio
