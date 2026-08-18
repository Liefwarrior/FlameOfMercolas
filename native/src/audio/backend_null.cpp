// The headless backend — no device, no SDL, no thread. The caller pulls
// render() when it wants samples, which is exactly what the doctest suite and
// the offline probe do inside the docker gate (a container with no sound
// card). This file also owns the createNull factories so audio_engine.cpp
// stays backend-agnostic.

#include "granadad/audio/audio_engine.hpp"
#include "granadad/audio/backend.hpp"
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
    return create(SoundBank::load(content::contentDir()),
                  std::make_unique<NullBackend>(), rngSeed);
}

std::unique_ptr<AudioEngine> AudioEngine::createNull(std::uint64_t rngSeed,
                                                     SoundBank bank) {
    return create(std::move(bank), std::make_unique<NullBackend>(), rngSeed);
}

}  // namespace granadad::audio
