// The audio smoke probe — same standing as bake_lamps: a dev tool, run by
// hand (or by a verify step), no window, no device.
//
//     granadad-audio-probe [--out <wav>] [--seconds <n>] [--synthetic]
//
// It stands up the REAL engine on the null backend, plays a scripted couple
// of seconds of the Docks — the Harbour bed at dusk, a walk that crosses
// stone onto wood into shallow water, a punch, a UI click — offline-renders
// the mix, writes it as a 16-bit WAV you can LISTEN to, and inspects the
// samples programmatically: silence where there should be sound, or a sample
// that escaped the clip stage, is a non-zero exit. With no vendored audio
// (--synthetic, or a checkout without content/art) it runs the same script
// over generated blips so the mixer path is still proven end to end.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "granadad/audio/audio_engine.hpp"
#include "granadad/content/content_dir.hpp"

namespace {

using granadad::audio::AudioEngine;
using granadad::audio::BedId;
using granadad::audio::kChannels;
using granadad::audio::kSampleRate;
using granadad::audio::SoundBank;
using granadad::audio::SoundId;

void putU32(std::vector<unsigned char>& out, std::uint32_t v) {
    out.push_back(static_cast<unsigned char>(v & 0xFFU));
    out.push_back(static_cast<unsigned char>((v >> 8) & 0xFFU));
    out.push_back(static_cast<unsigned char>((v >> 16) & 0xFFU));
    out.push_back(static_cast<unsigned char>((v >> 24) & 0xFFU));
}

void putU16(std::vector<unsigned char>& out, std::uint16_t v) {
    out.push_back(static_cast<unsigned char>(v & 0xFFU));
    out.push_back(static_cast<unsigned char>((v >> 8) & 0xFFU));
}

void putTag(std::vector<unsigned char>& out, const char* tag) {
    out.insert(out.end(), tag, tag + 4);
}

bool writeWav16(const std::string& path, const std::vector<float>& samples) {
    std::vector<unsigned char> bytes;
    const std::uint32_t dataBytes =
        static_cast<std::uint32_t>(samples.size() * 2U);
    bytes.reserve(44U + dataBytes);
    putTag(bytes, "RIFF");
    putU32(bytes, 36U + dataBytes);
    putTag(bytes, "WAVE");
    putTag(bytes, "fmt ");
    putU32(bytes, 16U);
    putU16(bytes, 1U);  // PCM
    putU16(bytes, static_cast<std::uint16_t>(kChannels));
    putU32(bytes, static_cast<std::uint32_t>(kSampleRate));
    putU32(bytes, static_cast<std::uint32_t>(kSampleRate * kChannels * 2));
    putU16(bytes, static_cast<std::uint16_t>(kChannels * 2));
    putU16(bytes, 16U);
    putTag(bytes, "data");
    putU32(bytes, dataBytes);
    for (const float s : samples) {
        const float clamped = std::clamp(s, -1.0F, 1.0F);
        const auto v = static_cast<std::int16_t>(
            std::lround(clamped * 32767.0F));
        putU16(bytes, static_cast<std::uint16_t>(v));
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

}  // namespace

int main(int argc, char** argv) {
    std::string outPath = "audio-probe.wav";
    float seconds = 4.0F;
    bool forceSynthetic = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--out" && i + 1 < argc) {
            outPath = argv[++i];
        } else if (arg == "--seconds" && i + 1 < argc) {
            seconds = std::clamp(std::stof(argv[++i]), 1.0F, 60.0F);
        } else if (arg == "--synthetic") {
            forceSynthetic = true;
        } else {
            std::fprintf(stderr,
                         "usage: granadad-audio-probe [--out <wav>] "
                         "[--seconds <n>] [--synthetic]\n");
            return 2;
        }
    }

    SoundBank bank = forceSynthetic
                         ? SoundBank::synthetic()
                         : SoundBank::load(granadad::content::contentDir());
    const bool real = bank.anyLoaded() && !forceSynthetic;
    if (!bank.anyLoaded()) {
        std::fprintf(stderr,
                     "granadad-audio-probe: no vendored audio found; using "
                     "synthetic blips\n");
        bank = SoundBank::synthetic();
    }

    auto engine = AudioEngine::createNull(0x9AD05EEDULL, std::move(bank));

    // The script: dusk on the harbour, so both day and night layer gains are
    // in play; a walk stone -> wood -> shallow water; one punch; one click.
    engine->setTimeOfDay(19 * 3600 + 30 * 60);
    engine->startBed(BedId::Harbour, 1.0F);

    constexpr int kBlockFrames = 480;  // 10ms
    const int blocks =
        static_cast<int>(seconds * static_cast<float>(kSampleRate)) /
        kBlockFrames;
    std::vector<float> mix;
    mix.reserve(static_cast<std::size_t>(blocks) *
                static_cast<std::size_t>(kBlockFrames) * 2U);
    float block[static_cast<std::size_t>(kBlockFrames) * 2U];

    for (int b = 0; b < blocks; ++b) {
        const float t = static_cast<float>(b) * 0.01F;
        // A step every update while "moving"; the engine's own cadence gate
        // turns the stream of calls into a walk.
        if (t >= 0.4F && t < 1.6F) {
            engine->footstep(8 /*granite*/, false, 0);
        } else if (t >= 1.6F && t < 2.4F) {
            engine->footstep(14 /*oak*/, false, 0);
        } else if (t >= 2.4F && t < 3.2F) {
            engine->footstep(6 /*dirt*/, true, 2);  // running through shallows
        }
        if (b == 50) {
            engine->playOneShot(SoundId::UiClick, 0.8F);
        }
        if (b == 220) {
            engine->playOneShot(SoundId::PunchMedium, 1.0F);
        }
        engine->update(0.01F);
        engine->render(block, kBlockFrames);
        mix.insert(mix.end(), block, block + kBlockFrames * 2);
    }

    // Inspect the mix programmatically.
    float peak = 0.0F;
    double sumSq = 0.0;
    std::size_t nonZero = 0;
    for (const float s : mix) {
        const float a = std::fabs(s);
        peak = std::max(peak, a);
        sumSq += static_cast<double>(s) * static_cast<double>(s);
        if (a > 1.0e-6F) {
            ++nonZero;
        }
    }
    const double rms =
        mix.empty() ? 0.0 : std::sqrt(sumSq / static_cast<double>(mix.size()));

    std::printf("granadad-audio-probe: %s bank, %.1fs, %zu samples\n",
                real ? "vendored" : "synthetic", static_cast<double>(seconds),
                mix.size());
    std::printf("  peak    %.4f\n", static_cast<double>(peak));
    std::printf("  rms     %.5f\n", rms);
    std::printf("  nonzero %zu (%.1f%%)\n", nonZero,
                mix.empty() ? 0.0
                            : 100.0 * static_cast<double>(nonZero) /
                                  static_cast<double>(mix.size()));

    if (peak <= 0.0F) {
        std::fprintf(stderr,
                     "FAIL: the scripted mix rendered pure silence — the "
                     "engine is not making sound.\n");
        return 1;
    }
    if (peak > 1.0F) {
        std::fprintf(stderr,
                     "FAIL: a sample escaped the soft clip (peak %.4f > 1).\n",
                     static_cast<double>(peak));
        return 1;
    }

    if (!writeWav16(outPath, mix)) {
        std::fprintf(stderr, "FAIL: could not write %s\n", outPath.c_str());
        return 1;
    }
    std::printf("  wrote   %s\n", outPath.c_str());
    return 0;
}
