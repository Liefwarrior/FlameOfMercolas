#pragma once

// A decoded sound: mono float PCM at the mixer's own rate.
//
// Everything is normalised at LOAD time — channels downmixed, sample rate
// converted — so the mixer's hot loop never branches on format. Pan is applied
// at mix time (constant-power), which is why mono is enough: none of the
// vendored one-shots carries meaningful stereo, and a positional pan computed
// from the game beats a baked one anyway.

#include <vector>

namespace granadad::audio {

/// The engine's one sample rate. Everything is converted to this at load.
inline constexpr int kSampleRate = 48000;

/// The engine's output channel count (interleaved stereo).
inline constexpr int kChannels = 2;

struct Sample {
    /// Mono PCM at kSampleRate, nominally in [-1, 1].
    std::vector<float> mono;
};

}  // namespace granadad::audio
