#pragma once

// The one audio entry point that reaches a real sound device.
//
// Declared here, defined in backend_sdl.cpp, which is the ONLY audio file
// that includes SDL — and it compiles into its own small library,
// granadad-audio-sdl, which only exists when GRANADAD_BUILD_CLIENT=ON. The
// core granadad-audio library never links SDL, so the sim-only configuration
// (the docker host check) builds and tests the whole engine without it.
//
// The wiring pass (see audio_engine.hpp's plan) calls this once from
// native/src/client/main.cpp after the existing SDL_Init:
//
//     auto audio = granadad::audio::createSdlAudioEngine();
//
// It does its own SDL_InitSubSystem(SDL_INIT_AUDIO) — main.cpp's init flags
// do not change. It loads the real sound bank from content::contentDir(),
// seeds the engine RNG from the wall clock (client-side jitter only; see the
// determinism note in audio_engine.hpp), opens the default playback device as
// a 48kHz float stream, and starts pulling the mixer on SDL's audio thread.
// On a machine with no audio device it logs one line and returns a fully
// functional engine whose deviceOpen() is false — every call is then a cheap
// no-op, never an error the caller has to handle.

#include <memory>

#include "granadad/audio/audio_engine.hpp"

namespace granadad::audio {

[[nodiscard]] std::unique_ptr<AudioEngine> createSdlAudioEngine();

}  // namespace granadad::audio
