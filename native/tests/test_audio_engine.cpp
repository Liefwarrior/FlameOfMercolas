// The audio engine, proved headless — every case runs on the null backend,
// because the docker gate has no sound card and never will.
//
// TWO KINDS OF CASE IN HERE, deliberately:
//   * pure engine/mixer math and the material table — run EVERYWHERE,
//     including the docker host check, against SoundBank::synthetic() (the
//     build context deliberately excludes the 1.2GB Kenney tree);
//   * manifest-vs-disk and real-OGG decode — run wherever the vendored audio
//     actually exists, which verify-windows.ps1's native pass always does.
//     Where the tree is absent they assert the graceful-silence contract
//     instead, which is the behaviour that checkout actually ships.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "granadad/audio/audio_engine.hpp"
#include "granadad/audio/decode.hpp"
#include "granadad/audio/mixer.hpp"
#include "granadad/audio/sound_bank.hpp"
#include "granadad/content/content_dir.hpp"
#include "granadad/render/atlas.hpp"

using granadad::audio::AudioEngine;
using granadad::audio::BedId;
using granadad::audio::Bus;
using granadad::audio::kChannels;
using granadad::audio::kSampleRate;
using granadad::audio::kSoundIdCount;
using granadad::audio::Mixer;
using granadad::audio::ProcLayer;
using granadad::audio::Sample;
using granadad::audio::SoundBank;
using granadad::audio::SoundId;
using granadad::audio::Surface;

namespace {

[[nodiscard]] std::filesystem::path audioRoot() {
    return granadad::content::contentDir() /
           std::filesystem::path(granadad::audio::kAudioRootRel);
}

[[nodiscard]] bool vendoredAudioPresent() {
    std::error_code ec;
    return std::filesystem::is_directory(audioRoot(), ec);
}

[[nodiscard]] std::shared_ptr<const Sample> constantSample(std::size_t frames,
                                                           float value) {
    Sample s;
    s.mono.assign(frames, value);
    return std::make_shared<const Sample>(std::move(s));
}

struct BlockStats {
    float peak = 0.0F;
    float peakL = 0.0F;
    float peakR = 0.0F;
    std::size_t nonZero = 0;
};

[[nodiscard]] BlockStats renderStats(Mixer& mixer, int frames) {
    std::vector<float> buf(static_cast<std::size_t>(frames) * 2U);
    mixer.render(buf.data(), frames);
    BlockStats stats;
    for (std::size_t i = 0; i < buf.size(); ++i) {
        const float a = std::fabs(buf[i]);
        stats.peak = std::max(stats.peak, a);
        if (i % 2 == 0) {
            stats.peakL = std::max(stats.peakL, a);
        } else {
            stats.peakR = std::max(stats.peakR, a);
        }
        if (a > 1.0e-6F) {
            ++stats.nonZero;
        }
    }
    return stats;
}

[[nodiscard]] BlockStats engineStats(AudioEngine& engine, int frames) {
    return renderStats(engine.mixer(), frames);
}

}  // namespace

// ---------------------------------------------------------------------------
// The material table — the footstep half of the Morrowind directive.
// ---------------------------------------------------------------------------

TEST_CASE("the footstep table names the render registry's materials, row for row") {
    // The registry id IS the row index. If a material raw is added, renamed
    // or re-sorted, this is the case that goes red instead of every footstep
    // in the ward silently landing on the wrong surface.
    const auto table = granadad::audio::materialSurfaceTable();
    const auto ids = granadad::render::materialIds();
    REQUIRE(table.size() == ids.size());
    for (std::size_t i = 0; i < ids.size(); ++i) {
        CAPTURE(i);
        CHECK(table[i].materialId == ids[i]);
    }
}

TEST_CASE("every material id resolves to a footstep sound with manifest entries") {
    const auto table = granadad::audio::materialSurfaceTable();
    for (std::size_t i = 0; i < table.size(); ++i) {
        CAPTURE(table[i].materialId);
        const Surface s = granadad::audio::surfaceForMaterial(
            static_cast<std::uint16_t>(i));
        CHECK(s == table[i].surface);
        const SoundId step = granadad::audio::footstepSoundFor(s);
        CHECK_FALSE(granadad::audio::soundPaths(step).empty());
    }
    // Out-of-range (a VOID read, a future id this table has not met) lands on
    // stone rather than out of bounds.
    CHECK(granadad::audio::surfaceForMaterial(0xFFFF) == Surface::Stone);
}

// ---------------------------------------------------------------------------
// The manifest against the vendored files.
// ---------------------------------------------------------------------------

TEST_CASE("every sound id has manifest paths, and every path exists where the Kenney tree is vendored") {
    for (std::size_t i = 0; i < kSoundIdCount; ++i) {
        const SoundId id = static_cast<SoundId>(i);
        CAPTURE(i);
        CHECK_FALSE(granadad::audio::soundPaths(id).empty());
    }
    if (!vendoredAudioPresent()) {
        // The docker build context excludes content/art on purpose; the
        // native verify pass is where this half always runs.
        MESSAGE("vendored audio absent; file-existence half skipped");
        return;
    }
    const std::filesystem::path root = audioRoot();
    for (std::size_t i = 0; i < kSoundIdCount; ++i) {
        const SoundId id = static_cast<SoundId>(i);
        for (const std::string_view rel : granadad::audio::soundPaths(id)) {
            const std::filesystem::path p = root / std::filesystem::path(rel);
            CAPTURE(rel);
            CHECK(std::filesystem::exists(p));
        }
    }
}

TEST_CASE("a real vendored ogg decodes to mono 48k samples in range") {
    if (!vendoredAudioPresent()) {
        MESSAGE("vendored audio absent; skipped");
        return;
    }
    const std::filesystem::path p =
        audioRoot() / "Impact Sounds" / "Audio" / "footstep_wood_000.ogg";
    REQUIRE(std::filesystem::exists(p));
    std::ifstream in(p, std::ios::binary);
    REQUIRE(static_cast<bool>(in));
    std::vector<unsigned char> bytes(
        (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    REQUIRE_FALSE(bytes.empty());
    const auto sample = granadad::audio::decodeOggToMono(bytes.data(), bytes.size());
    REQUIRE(sample.has_value());
    // A footstep is a fraction of a second: at 48k that is thousands of
    // samples, and every one of them a sane amplitude.
    CHECK(sample->mono.size() > 1000U);
    CHECK(sample->mono.size() < static_cast<std::size_t>(kSampleRate) * 5U);
    for (const float s : sample->mono) {
        REQUIRE(std::isfinite(s));
        REQUIRE(std::fabs(s) <= 1.5F);
    }
}

TEST_CASE("the full bank loads every manifest file where the tree is vendored") {
    if (!vendoredAudioPresent()) {
        MESSAGE("vendored audio absent; skipped");
        return;
    }
    const SoundBank bank = SoundBank::load(granadad::content::contentDir());
    CHECK(bank.missingFiles() == 0);
    for (std::size_t i = 0; i < kSoundIdCount; ++i) {
        const SoundId id = static_cast<SoundId>(i);
        CAPTURE(i);
        CHECK(bank.variantCount(id) ==
              granadad::audio::soundPaths(id).size());
    }
}

// ---------------------------------------------------------------------------
// Graceful no-content behaviour — what a stripped checkout actually ships.
// ---------------------------------------------------------------------------

TEST_CASE("a checkout with no audio constructs, plays silence, and never throws") {
    SoundBank bank =
        SoundBank::load(std::filesystem::path("granadad-no-such-dir"));
    CHECK_FALSE(bank.anyLoaded());
    CHECK(bank.missingFiles() > 0);
    auto engine = AudioEngine::createNull(1, std::move(bank));
    REQUIRE(engine != nullptr);
    CHECK_FALSE(engine->deviceOpen());
    engine->update(1.0F);
    engine->playOneShot(SoundId::UiClick);
    engine->footstep(8, false, 2);
    engine->startBed(BedId::Harbour, 0.5F);
    engine->update(0.1F);
    // The procedural bed still hums — it needs no files — but no VOICE ever
    // starts, and nothing crashes. That is the contract.
    CHECK(engine->mixer().activeVoices() == 0);
}

// ---------------------------------------------------------------------------
// Mixer math.
// ---------------------------------------------------------------------------

TEST_CASE("a one-shot voice sounds, ends, and reaps itself") {
    Mixer mixer;
    const auto id = mixer.play(constantSample(1000, 0.5F), Bus::Ui, 1.0F, 0.0F,
                               1.0F, false);
    CHECK(id != Mixer::kNoVoice);
    CHECK(mixer.activeVoices() == 1);
    const BlockStats first = renderStats(mixer, 480);
    CHECK(first.nonZero > 0);
    // 1000 samples at unit pitch: gone within three more blocks.
    (void)renderStats(mixer, 480);
    (void)renderStats(mixer, 480);
    CHECK(mixer.activeVoices() == 0);
    const BlockStats after = renderStats(mixer, 480);
    CHECK(after.nonZero == 0);
}

TEST_CASE("constant-power pan sends a hard-left voice to the left channel only") {
    Mixer mixer;
    mixer.play(constantSample(48000, 0.5F), Bus::Ui, 1.0F, -1.0F, 1.0F, false);
    const BlockStats stats = renderStats(mixer, 480);
    CHECK(stats.peakL > 0.1F);
    CHECK(stats.peakR < 1.0e-4F);
}

TEST_CASE("centre pan splits a voice equally") {
    Mixer mixer;
    mixer.play(constantSample(48000, 0.5F), Bus::Ui, 1.0F, 0.0F, 1.0F, false);
    const BlockStats stats = renderStats(mixer, 480);
    CHECK(stats.peakL > 0.1F);
    CHECK(stats.peakR == doctest::Approx(stats.peakL).epsilon(0.01));
}

TEST_CASE("a bus at zero silences its voices and only its voices") {
    Mixer mixer;
    mixer.setBusGain(Bus::Ui, 0.0F);
    mixer.play(constantSample(48000, 0.5F), Bus::Ui, 1.0F, 0.0F, 1.0F, false);
    mixer.play(constantSample(48000, 0.5F), Bus::Combat, 1.0F, 0.0F, 1.0F,
               false);
    const BlockStats stats = renderStats(mixer, 480);
    CHECK(stats.nonZero > 0);  // the Combat voice
    mixer.setBusGain(Bus::Combat, 0.0F);
    const BlockStats muted = renderStats(mixer, 480);
    CHECK(muted.nonZero == 0);
}

TEST_CASE("master at zero silences everything, procedural layers included") {
    Mixer mixer;
    mixer.setNoiseSeed(42);
    mixer.setProceduralGain(ProcLayer::WaterLap, 0.8F, 0.0F);
    mixer.play(constantSample(48000, 0.5F), Bus::Ui, 1.0F, 0.0F, 1.0F, false);
    mixer.setBusGain(Bus::Master, 0.0F);
    const BlockStats stats = renderStats(mixer, 480);
    CHECK(stats.nonZero == 0);
}

TEST_CASE("the mix never leaves (-1, 1) even under a deliberate pile-up") {
    Mixer mixer;
    for (int i = 0; i < 20; ++i) {
        mixer.play(constantSample(48000, 0.9F), Bus::Combat, 2.0F, 0.0F, 1.0F,
                   false);
    }
    const BlockStats stats = renderStats(mixer, 480);
    CHECK(stats.peak > 0.5F);    // definitely loud
    CHECK(stats.peak <= 1.0F);   // and still clipped — tanh saturates; float
                                 // rounding may land exactly on 1.0
}

TEST_CASE("the procedural water layer hums at gain, in range, and dies at zero") {
    Mixer mixer;
    mixer.setNoiseSeed(7);
    mixer.setProceduralGain(ProcLayer::WaterLap, 0.6F, 0.01F);
    // Let the ramp arrive and the filters warm up.
    BlockStats stats{};
    for (int i = 0; i < 20; ++i) {
        stats = renderStats(mixer, 480);
    }
    CHECK(stats.nonZero > 0);
    CHECK(stats.peak < 1.0F);
    CHECK(mixer.proceduralGain(ProcLayer::WaterLap) ==
          doctest::Approx(0.6F).epsilon(0.01));
    mixer.setProceduralGain(ProcLayer::WaterLap, 0.0F, 0.05F);
    for (int i = 0; i < 20; ++i) {
        stats = renderStats(mixer, 480);
    }
    // The ramp clamps exactly onto its target, so this is an exact zero.
    CHECK(mixer.proceduralGain(ProcLayer::WaterLap) == 0.0F);
    CHECK(stats.nonZero == 0);
}

// ---------------------------------------------------------------------------
// The engine over a synthetic bank — provable on every checkout.
// ---------------------------------------------------------------------------

TEST_CASE("the synthetic bank covers the whole vocabulary") {
    const SoundBank bank = SoundBank::synthetic();
    CHECK(bank.anyLoaded());
    for (std::size_t i = 0; i < kSoundIdCount; ++i) {
        CAPTURE(i);
        CHECK(bank.variantCount(static_cast<SoundId>(i)) >= 2);
    }
}

TEST_CASE("a one-shot through the engine reaches the output and then decays to silence") {
    auto engine = AudioEngine::createNull(3, SoundBank::synthetic());
    engine->playOneShot(SoundId::UiConfirm, 1.0F);
    const BlockStats first = engineStats(*engine, 480);
    CHECK(first.nonZero > 0);
    (void)engineStats(*engine, 480);
    const BlockStats after = engineStats(*engine, 480);
    CHECK(after.nonZero == 0);
    CHECK(engine->mixer().activeVoices() == 0);
}

TEST_CASE("the footstep cadence gate turns a stream of calls into a walk") {
    auto engine = AudioEngine::createNull(5, SoundBank::synthetic());
    engine->update(1.0F);
    CHECK(engine->footstep(8 /*granite*/, false, 0));
    // Same frame, next frame: swallowed.
    CHECK_FALSE(engine->footstep(8, false, 0));
    engine->update(0.1F);
    CHECK_FALSE(engine->footstep(8, false, 0));
    // A walking gap later: the next step lands.
    engine->update(0.3F);
    CHECK(engine->footstep(8, false, 0));
    // Running cadence is tighter than walking.
    engine->update(0.25F);
    CHECK(engine->footstep(8, true, 0));
}

TEST_CASE("wading layers a splash voice under the footstep") {
    auto engine = AudioEngine::createNull(9, SoundBank::synthetic());
    engine->update(1.0F);
    CHECK(engine->footstep(6 /*dirt*/, false, 0));
    CHECK(engine->mixer().activeVoices() == 1);
    engine->update(1.0F);
    (void)engineStats(*engine, 4800);  // drain the first step's blip
    CHECK(engine->footstep(6, false, 3));
    CHECK(engine->mixer().activeVoices() == 2);
}

TEST_CASE("the harbour bed crossfades in monotonically and arrives at its day gain") {
    auto engine = AudioEngine::createNull(11, SoundBank::synthetic());
    engine->setTimeOfDay(12 * 3600);  // full day
    engine->startBed(BedId::Harbour, 1.0F);
    CHECK(engine->currentBed() == BedId::Harbour);
    float last = 0.0F;
    for (int i = 0; i < 60; ++i) {
        engine->update(0.05F);
        (void)engineStats(*engine, 2400);  // 50ms — advances the mixer ramps
        const float g = engine->mixer().proceduralGain(ProcLayer::WaterLap);
        CHECK(g >= last - 1.0e-4F);
        last = g;
    }
    // kHarbourProcs says WaterLap's day gain is 0.5.
    CHECK(last == doctest::Approx(0.5F).epsilon(0.02));

    // And back out.
    engine->stopBed(0.5F);
    CHECK(engine->currentBed() == BedId::None);
    for (int i = 0; i < 60; ++i) {
        engine->update(0.05F);
        (void)engineStats(*engine, 2400);
    }
    // Exact: the mixer's ramp clamps onto its target.
    CHECK(engine->mixer().proceduralGain(ProcLayer::WaterLap) == 0.0F);
}

TEST_CASE("night rebalances the harbour: less water-lap, more wind") {
    auto dayEngine = AudioEngine::createNull(13, SoundBank::synthetic());
    dayEngine->setTimeOfDay(12 * 3600);
    dayEngine->startBed(BedId::Harbour, 0.1F);
    for (int i = 0; i < 40; ++i) {
        dayEngine->update(0.05F);
        (void)engineStats(*dayEngine, 2400);
    }
    auto nightEngine = AudioEngine::createNull(13, SoundBank::synthetic());
    nightEngine->setTimeOfDay(2 * 3600);
    nightEngine->startBed(BedId::Harbour, 0.1F);
    for (int i = 0; i < 40; ++i) {
        nightEngine->update(0.05F);
        (void)engineStats(*nightEngine, 2400);
    }
    CHECK(dayEngine->mixer().proceduralGain(ProcLayer::WaterLap) >
          nightEngine->mixer().proceduralGain(ProcLayer::WaterLap));
    CHECK(dayEngine->mixer().proceduralGain(ProcLayer::Wind) <
          nightEngine->mixer().proceduralGain(ProcLayer::Wind));
}

TEST_CASE("re-asserting the current bed every frame is a no-op, as the wiring plan promises") {
    auto engine = AudioEngine::createNull(17, SoundBank::synthetic());
    engine->setTimeOfDay(12 * 3600);
    engine->startBed(BedId::Harbour, 0.2F);
    for (int i = 0; i < 20; ++i) {
        engine->startBed(BedId::Harbour, 0.2F);  // every frame
        engine->update(0.05F);
        (void)engineStats(*engine, 2400);
    }
    const float settled = engine->mixer().proceduralGain(ProcLayer::WaterLap);
    CHECK(settled == doctest::Approx(0.5F).epsilon(0.02));
    engine->startBed(BedId::Harbour, 0.2F);
    engine->update(0.05F);
    (void)engineStats(*engine, 2400);
    // No restart-from-zero dip.
    CHECK(engine->mixer().proceduralGain(ProcLayer::WaterLap) >=
          settled - 1.0e-3F);
}

TEST_CASE("a bed's sparse one-shots actually fire over time") {
    auto engine = AudioEngine::createNull(23, SoundBank::synthetic());
    engine->setTimeOfDay(12 * 3600);
    engine->startBed(BedId::Harbour, 0.1F);
    // The first sparse fire is primed to land within ~4 seconds. Count voice
    // starts over 8 simulated seconds.
    bool sawVoice = false;
    for (int i = 0; i < 160; ++i) {
        engine->update(0.05F);
        if (engine->mixer().activeVoices() > 0) {
            sawVoice = true;
        }
        (void)engineStats(*engine, 2400);
    }
    CHECK(sawVoice);
}

TEST_CASE("dayness is 0 at night, 1 at noon, and ramps through dawn") {
    using granadad::audio::dayness;
    CHECK(dayness(0) == 0.0F);
    CHECK(dayness(3 * 3600) == 0.0F);
    CHECK(dayness(12 * 3600) == 1.0F);
    CHECK(dayness(22 * 3600) == 0.0F);
    const float early = dayness(5 * 3600 + 1800);
    const float late = dayness(6 * 3600 + 1800);
    CHECK(early > 0.0F);
    CHECK(early < 1.0F);
    CHECK(late > early);
    // And a wrapped engine-clock total behaves.
    CHECK(dayness(86400 + 12 * 3600) == 1.0F);
    CHECK(dayness(-3600) == dayness(23 * 3600));
}
