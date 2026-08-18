// The single translation unit that instantiates stb_vorbis — same pattern as
// render/stb_impl.cpp for stb_image. Everywhere else calls decodeOggToMono and
// never sees the decoder.
//
// stb_vorbis.c is BOTH header and implementation in one file, so unlike the
// other stb libraries there is no IMPLEMENTATION macro: including it once,
// here, is the whole instantiation. It comes off the repo's existing stb pin
// (Dependencies.cmake fetches the full clone; stb_vorbis.c sits at its root on
// the stb::stb include path) — zero new dependencies.
//
// NO_STDIO: we hand it bytes we read ourselves, so its FILE* paths never
// compile — one less platform surface to differ on.

#include "granadad/audio/decode.hpp"

#include <cstdlib>
#include <vector>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include <stb_vorbis.c>

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace granadad::audio {

std::optional<Sample> decodeOggToMono(const unsigned char* bytes,
                                      std::size_t size) {
    if (bytes == nullptr || size == 0 ||
        size > static_cast<std::size_t>(INT32_MAX)) {
        return std::nullopt;
    }
    int channels = 0;
    int rate = 0;
    short* pcm = nullptr;
    const int frames = stb_vorbis_decode_memory(
        bytes, static_cast<int>(size), &channels, &rate, &pcm);
    if (frames <= 0 || pcm == nullptr || channels <= 0 || rate <= 0) {
        if (pcm != nullptr) {
            std::free(pcm);
        }
        return std::nullopt;
    }

    // Downmix to mono float.
    const std::size_t frameCount = static_cast<std::size_t>(frames);
    const std::size_t channelCount = static_cast<std::size_t>(channels);
    std::vector<float> mono(frameCount);
    const float channelScale =
        1.0F / (32768.0F * static_cast<float>(channelCount));
    for (std::size_t f = 0; f < frameCount; ++f) {
        float acc = 0.0F;
        for (std::size_t c = 0; c < channelCount; ++c) {
            acc += static_cast<float>(pcm[f * channelCount + c]);
        }
        mono[f] = acc * channelScale;
    }
    std::free(pcm);

    if (rate == kSampleRate) {
        return Sample{std::move(mono)};
    }

    // Linear-interp resample to kSampleRate. These are one-shot SFX; linear is
    // audibly fine and keeps the loader trivial.
    const double step = static_cast<double>(rate) / kSampleRate;
    const std::size_t outLen = static_cast<std::size_t>(
        static_cast<double>(frameCount) * kSampleRate / rate);
    std::vector<float> resampled(outLen);
    double pos = 0.0;
    for (std::size_t i = 0; i < outLen; ++i) {
        const std::size_t i0 = static_cast<std::size_t>(pos);
        const std::size_t i1 = (i0 + 1 < frameCount) ? i0 + 1 : i0;
        const float frac = static_cast<float>(pos - static_cast<double>(i0));
        resampled[i] = mono[i0] * (1.0F - frac) + mono[i1] * frac;
        pos += step;
        if (pos > static_cast<double>(frameCount - 1)) {
            pos = static_cast<double>(frameCount - 1);
        }
    }
    return Sample{std::move(resampled)};
}

}  // namespace granadad::audio
