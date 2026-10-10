// The headless backend — no device, no SDL, no thread. The caller pulls
// render() when it wants samples, which is exactly what the doctest suite and
// the offline probe do inside the docker gate (a container with no sound
// card). This file also owns the createNull factories so audio_engine.cpp
// stays backend-agnostic.

#include <filesystem>

#include "granadad/audio/audio_engine.hpp"
#include "granadad/audio/backend.hpp"
#include "granadad/audio/sound_bank.hpp"
#include "granadad/content/content_dir.hpp"

namespace granadad::audio {

namespace {

class NullBackend final : public Backend {
public:
    bool start(Mixer& /*mixer*/) override {
        // Nothing to attach; the engine runs pull-driven and deviceOpen()
        // truthfully reports that no real device exists.
        return false;
    }

    [[nodiscard]] const char* name() const override { return "null"; }
};

}  // namespace

std::unique_ptr<AudioEngine> AudioEngine::createNull(std::uint64_t rngSeed) {
    const std::filesystem::path dir = content::contentDir();
    std::unique_ptr<AudioEngine> engine =
        create(SoundBank::load(dir), std::make_unique<NullBackend>(), rngSeed);
    // The real bank gets the real track loader too (the LOT root); a
    // checkout without the music simply never starts a music voice.
    engine->music().setTrackLoader(
        [dir](TrackId id) { return loadTrack(dir, id); });
    return engine;
}

std::unique_ptr<AudioEngine> AudioEngine::createNull(std::uint64_t rngSeed,
                                                     SoundBank bank) {
    return create(std::move(bank), std::make_unique<NullBackend>(), rngSeed);
}

}  // namespace granadad::audio
