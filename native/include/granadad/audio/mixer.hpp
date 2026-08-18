#pragma once

// The voice mixer — float32 stereo at 48000 Hz.
//
// Everything audible passes through render(): one-shot voices, looped bed
// layers, and the two PROCEDURAL ambience layers (filtered-noise water-lap and
// wind — the vendored asset set has no ambience recordings, see sound_ids.hpp
// on BedId). Per-voice: linear-interp pitch, constant-power pan, a per-sample
// gain ramp so nothing clicks. Per-bus gains plus Master, then a tanh soft
// clip so a pile-up of impacts saturates instead of wrapping.
//
// THREAD SAFETY. Every public method takes the one internal mutex. That is
// the whole design: the SDL backend's device callback pulls render() on the
// audio thread while the game thread plays one-shots and moves gains. The
// audio thread holds the lock only for one block render (<= ~21ms of samples,
// typically far less), and the game thread's calls are O(voices).
//
// FLOATS ARE LEGAL HERE and this code is CLIENT-SIDE ONLY — same standing as
// granadad-render. Nothing in native/src/sim may include this header; the
// mixer's own noise RNG is seeded by the caller (wall clock in the client,
// a constant in tests) and never touches the sim's streams.

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "granadad/audio/sample.hpp"
#include "granadad/audio/sound_ids.hpp"

namespace granadad::audio {

/// The synthesized ambience layers. Gains are moved by the bed scheduler in
/// AudioEngine::update; DSP state lives in the mixer because that is where the
/// samples are made.
enum class ProcLayer : std::uint8_t {
    WaterLap = 0,  ///< low-passed brown noise with a slow two-sine swell
    Wind = 1,      ///< low-passed white noise with a slower gust swell
};

inline constexpr std::size_t kProcLayerCount = 2;

class Mixer {
public:
    using VoiceId = std::uint32_t;
    static constexpr VoiceId kNoVoice = 0;

    /// More one-shots than this and the oldest non-loop voice is stolen.
    static constexpr std::size_t kMaxVoices = 48;

    Mixer();

    /// Starts a voice. `pan` is -1 (left) .. +1 (right), constant-power.
    /// `pitch` is a playback-rate multiplier. Loop voices start at gain 0 and
    /// ramp up (use setVoiceGain to steer them); one-shots start at `gain`.
    /// A null or empty sample is a silent no-op returning kNoVoice.
    VoiceId play(std::shared_ptr<const Sample> sample, Bus bus, float gain,
                 float pan, float pitch, bool loop = false);

    /// Fades a voice to silence over fadeSec and removes it. Unknown ids are
    /// ignored (the voice may simply have finished).
    void stop(VoiceId voice, float fadeSec);

    /// Ramps a voice's gain to `gain` over rampSec.
    void setVoiceGain(VoiceId voice, float gain, float rampSec);

    void setBusGain(Bus bus, float gain);
    [[nodiscard]] float busGain(Bus bus) const;

    /// Ramps a procedural layer's gain to `target` over rampSec. The ramp
    /// advances inside render(), per sample, so it is click-free by
    /// construction. Procedural layers sit on Bus::Ambient.
    void setProceduralGain(ProcLayer layer, float target, float rampSec);

    /// The layer's CURRENT (mid-ramp) gain — observable so tests can prove a
    /// bed crossfade is monotone and actually arrives.
    [[nodiscard]] float proceduralGain(ProcLayer layer) const;

    /// Renders `frames` frames of interleaved stereo into `out` (2*frames
    /// floats), overwriting it. Output is soft-clipped to (-1, 1).
    void render(float* out, int frames);

    [[nodiscard]] int activeVoices() const;

    /// Seeds the procedural-noise RNG. Client code seeds from the wall clock;
    /// tests seed with a constant. NEVER from the sim's streams.
    void setNoiseSeed(std::uint32_t seed);

private:
    struct Voice {
        std::shared_ptr<const Sample> sample;
        double pos = 0.0;
        double step = 1.0;
        float gain = 0.0F;
        float targetGain = 0.0F;
        float gainStep = 0.0F;  ///< per-sample increment toward targetGain
        float panL = 1.0F;
        float panR = 1.0F;
        Bus bus = Bus::Master;
        VoiceId id = kNoVoice;
        bool loop = false;
        bool stopping = false;
    };

    struct Proc {
        float gain = 0.0F;
        float target = 0.0F;
        float step = 0.0F;
        // DSP state.
        float brown = 0.0F;
        float lp = 0.0F;
        float phaseA = 0.0F;
        float phaseB = 0.0F;
    };

    /// White noise in [-1, 1). Callers hold mutex_.
    [[nodiscard]] float noise() noexcept;

    /// One mono sample of a procedural layer at unit gain. Callers hold mutex_.
    [[nodiscard]] float procSample(std::size_t layer) noexcept;

    mutable std::mutex mutex_;
    std::array<float, kBusCount> busGain_{};
    std::array<Proc, kProcLayerCount> proc_{};
    std::vector<Voice> voices_;
    VoiceId nextId_ = 1;
    std::uint32_t noiseState_ = 0x9E3779B9U;
};

}  // namespace granadad::audio
