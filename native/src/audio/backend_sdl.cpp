// The ONLY audio file that touches SDL. See backend_sdl.hpp.

#include "granadad/audio/backend_sdl.hpp"

#include <SDL3/SDL.h>

#include <chrono>
#include <filesystem>

#include "granadad/audio/backend.hpp"
#include "granadad/audio/mixer.hpp"
#include "granadad/audio/sound_bank.hpp"
#include "granadad/content/content_dir.hpp"

namespace granadad::audio {

namespace {

class SdlBackend final : public Backend {
public:
    ~SdlBackend() override {
        if (stream_ != nullptr) {
            // Destroying the stream also closes the device this call opened.
            SDL_DestroyAudioStream(stream_);
        }
        if (ownsSubsystem_) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
        }
    }

    bool start(Mixer& mixer) override {
        mixer_ = &mixer;
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            SDL_Log("granadad-audio: SDL audio subsystem unavailable (%s); "
                    "running silent",
                    SDL_GetError());
            return false;
        }
        ownsSubsystem_ = true;
        const SDL_AudioSpec spec{SDL_AUDIO_F32, kChannels, kSampleRate};
        stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                            &spec, &SdlBackend::pull, this);
        if (stream_ == nullptr) {
            SDL_Log("granadad-audio: no playback device (%s); running silent",
                    SDL_GetError());
            return false;
        }
        // SDL_OpenAudioDeviceStream starts the device paused.
        SDL_ResumeAudioStreamDevice(stream_);
        return true;
    }

    [[nodiscard]] const char* name() const override { return "sdl"; }

private:
    static void SDLCALL pull(void* userdata, SDL_AudioStream* stream,
                             int additionalAmount, int /*totalAmount*/) {
        auto* self = static_cast<SdlBackend*>(userdata);
        constexpr int kFrameBytes =
            static_cast<int>(sizeof(float)) * kChannels;
        int frames = (additionalAmount + kFrameBytes - 1) / kFrameBytes;
        while (frames > 0) {
            const int chunk = frames < kChunkFrames ? frames : kChunkFrames;
            self->mixer_->render(self->scratch_, chunk);
            SDL_PutAudioStreamData(stream, self->scratch_,
                                   chunk * kFrameBytes);
            frames -= chunk;
        }
    }

    static constexpr int kChunkFrames = 1024;

    Mixer* mixer_ = nullptr;
    SDL_AudioStream* stream_ = nullptr;
    bool ownsSubsystem_ = false;
    float scratch_[static_cast<std::size_t>(kChunkFrames) * 2U] = {};
};

}  // namespace

std::unique_ptr<AudioEngine> createSdlAudioEngine() {
    // Wall-clock seed: audio jitter is client-side flavour and must never be
    // reproducible FROM the sim nor feed anything back into it.
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const std::uint64_t seed = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
    const std::filesystem::path dir = content::contentDir();
    std::unique_ptr<AudioEngine> engine = AudioEngine::create(
        SoundBank::load(dir), std::make_unique<SdlBackend>(), seed | 1ULL);
    // THE LOT PASS: the music director's loader, over the LOT root. Decodes
    // run off the game thread inside the director; a missing loop is a
    // silent cue, never a wait.
    engine->music().setTrackLoader(
        [dir](TrackId id) { return loadTrack(dir, id); });
    return engine;
}

}  // namespace granadad::audio
