#pragma once

// The seam between the audio engine and any actual sound device.
//
// Two implementations:
//   backend_null.cpp  no device at all. render() is pulled by the caller —
//                     tests and the offline probe run the WHOLE engine this
//                     way, headless, in the docker gate.
//   backend_sdl.cpp   SDL3 device stream, pull-callback driven. Lives in its
//                     own tiny library (granadad-audio-sdl) so the core
//                     granadad-audio library NEVER links SDL — the sim-only
//                     configuration builds it untouched.

namespace granadad::audio {

class Mixer;

class Backend {
public:
    virtual ~Backend() = default;

    /// Attach to a real output device and start pulling `mixer`. Returns
    /// false when there is no device to open (the null backend always, the
    /// SDL backend on a machine with no audio) — the engine then simply runs
    /// silent-but-functional, the same degradation the atlas makes for
    /// missing art.
    virtual bool start(Mixer& mixer) = 0;

    [[nodiscard]] virtual const char* name() const = 0;
};

}  // namespace granadad::audio
