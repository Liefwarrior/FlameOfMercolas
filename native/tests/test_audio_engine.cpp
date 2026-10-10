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
#include "granadad/audio/music_director.hpp"
#include "granadad/audio/sound_bank.hpp"
#include "granadad/content/content_dir.hpp"
#include "granadad/render/atlas.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/player.hpp"
#include "granadad/sim/tavern.hpp"

using granadad::audio::AudioEngine;
using granadad::audio::BedId;
using granadad::audio::Bus;
using granadad::audio::kChannels;
using granadad::audio::kSampleRate;
using granadad::audio::kSoundIdCount;
using granadad::audio::kSurfaceCount;
using granadad::audio::kTrackIdCount;
using granadad::audio::Mixer;
using granadad::audio::MusicDirector;
using granadad::audio::MusicMood;
using granadad::audio::MusicZone;
using granadad::audio::ProcLayer;
using granadad::audio::Sample;
using granadad::audio::SoundBank;
using granadad::audio::SoundId;
using granadad::audio::Surface;
using granadad::audio::TrackId;

namespace {

[[nodiscard]] std::filesystem::path audioRoot() {
    return granadad::content::contentDir() /
           std::filesystem::path(granadad::audio::kAudioRootRel);
}

[[nodiscard]] bool vendoredAudioPresent() {
    std::error_code ec;
    return std::filesystem::is_directory(audioRoot(), ec);
}

/// THE LOT PASS: the second root. Staged, gitignored, absent from every
/// gate by design -- a case that needs it SKIPS, never fails, without it.
[[nodiscard]] std::filesystem::path lotAudioRoot() {
    return granadad::content::contentDir() /
           std::filesystem::path(granadad::audio::kLotAudioRootRel);
}

[[nodiscard]] bool lotAudioPresent() {
    std::error_code ec;
    return std::filesystem::is_directory(lotAudioRoot(), ec);
}

/// How many variants SoundBank::load is expected to hold for `id` on THIS
/// checkout: the LOT rows where the LOT tree is staged and the id has any,
/// else the Kenney rows where the Kenney tree is vendored, else none.
[[nodiscard]] std::size_t expectedVariants(SoundId id) {
    if (lotAudioPresent() && !granadad::audio::lotSoundPaths(id).empty()) {
        return granadad::audio::lotSoundPaths(id).size();
    }
    if (vendoredAudioPresent()) {
        return granadad::audio::soundPaths(id).size();
    }
    return 0;
}

/// A short stereo loop per track, generated -- the director's loader in every
/// test, so the machine is proved with no files. Runs on the director's
/// worker thread; captures nothing.
[[nodiscard]] std::shared_ptr<const Sample> syntheticTrack(TrackId id) {
    constexpr std::size_t kFrames = 4800;  // 100 ms
    Sample s;
    s.stereo.resize(kFrames * 2U);
    const float hz = 110.0F + 20.0F * static_cast<float>(granadad::audio::trackIndex(id));
    const float step = 2.0F * 3.14159265358979323846F * hz / static_cast<float>(kSampleRate);
    for (std::size_t n = 0; n < kFrames; ++n) {
        const float v = 0.3F * std::sin(step * static_cast<float>(n));
        s.stereo[n * 2U] = v;
        s.stereo[n * 2U + 1U] = -v;
    }
    return std::make_shared<const Sample>(std::move(s));
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

TEST_CASE("every sound id has manifest paths under one root or the other, and every path exists where its tree is vendored") {
    for (std::size_t i = 0; i < kSoundIdCount; ++i) {
        const SoundId id = static_cast<SoundId>(i);
        CAPTURE(i);
        // THE LOT PASS: a Kenney row, a LOT row, or both -- never neither.
        CHECK((!granadad::audio::soundPaths(id).empty() ||
               !granadad::audio::lotSoundPaths(id).empty()));
    }
    if (vendoredAudioPresent()) {
        const std::filesystem::path root = audioRoot();
        for (std::size_t i = 0; i < kSoundIdCount; ++i) {
            const SoundId id = static_cast<SoundId>(i);
            for (const std::string_view rel : granadad::audio::soundPaths(id)) {
                const std::filesystem::path p = root / std::filesystem::path(rel);
                CAPTURE(rel);
                CHECK(std::filesystem::exists(p));
            }
        }
    } else {
        // The docker build context excludes content/art on purpose; the
        // native verify pass is where this half always runs.
        MESSAGE("vendored audio absent; Kenney file-existence half skipped");
    }
    if (lotAudioPresent()) {
        const std::filesystem::path root = lotAudioRoot();
        for (std::size_t i = 0; i < kSoundIdCount; ++i) {
            const SoundId id = static_cast<SoundId>(i);
            for (const granadad::audio::LotSoundFile& file :
                 granadad::audio::lotSoundPaths(id)) {
                const std::filesystem::path p = root / std::filesystem::path(file.rel);
                CAPTURE(file.rel);
                CHECK(std::filesystem::exists(p));
            }
        }
        for (std::size_t i = 1; i < kTrackIdCount; ++i) {
            const std::string_view rel = granadad::audio::trackPath(static_cast<TrackId>(i));
            CAPTURE(rel);
            CHECK_FALSE(rel.empty());
            CHECK(std::filesystem::exists(root / std::filesystem::path(rel)));
        }
    } else {
        MESSAGE("LOT audio absent (the gate's normal state); LOT file-existence half skipped");
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

TEST_CASE("the full bank loads every manifest file where the tree is vendored, LOT rows first") {
    if (!vendoredAudioPresent() && !lotAudioPresent()) {
        MESSAGE("no vendored audio at all; skipped");
        return;
    }
    const SoundBank bank = SoundBank::load(granadad::content::contentDir());
    if (vendoredAudioPresent()) {
        CHECK(bank.missingFiles() == 0);
    }
    if (lotAudioPresent()) {
        CHECK(bank.lotMissingFiles() == 0);
        CHECK(bank.lotFiles() > 0);
    } else {
        CHECK(bank.lotFiles() == 0);
    }
    for (std::size_t i = 0; i < kSoundIdCount; ++i) {
        const SoundId id = static_cast<SoundId>(i);
        CAPTURE(i);
        CHECK(bank.variantCount(id) == expectedVariants(id));
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
    CHECK(bank.lotMissingFiles() > 0);
    CHECK(bank.lotFiles() == 0);
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
    // NOTE: update() clamps dt to 0.25s (the anti-hitch clamp), so a long gap
    // has to be fed as several updates — exactly what a frame loop does.
    auto engine = AudioEngine::createNull(5, SoundBank::synthetic());
    engine->update(0.25F);
    engine->update(0.25F);
    CHECK(engine->footstep(8 /*granite*/, false, 0));
    // Same frame, next frame: swallowed.
    CHECK_FALSE(engine->footstep(8, false, 0));
    engine->update(0.1F);
    CHECK_FALSE(engine->footstep(8, false, 0));
    // A walking gap later: the next step lands.
    engine->update(0.2F);
    engine->update(0.2F);
    CHECK(engine->footstep(8, false, 0));
    // Running cadence is tighter than walking.
    engine->update(0.12F);
    engine->update(0.12F);
    CHECK(engine->footstep(8, true, 0));
}

TEST_CASE("wading layers a splash voice under the footstep") {
    auto engine = AudioEngine::createNull(9, SoundBank::synthetic());
    engine->update(0.25F);
    engine->update(0.25F);
    CHECK(engine->footstep(6 /*dirt*/, false, 0));
    CHECK(engine->mixer().activeVoices() == 1);
    // dt is clamped to 0.25s per update, so open the cadence gate in steps.
    engine->update(0.25F);
    engine->update(0.25F);
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
    // Not an exact zero: update() re-targets the ramp every frame from the
    // CURRENT gain, so the tail is a geometric approach (measured ~1e-23
    // after three seconds). Audibly and numerically silence.
    CHECK(engine->mixer().proceduralGain(ProcLayer::WaterLap) < 1.0e-4F);
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
    // starts over 8 simulated seconds -- ABOVE the bed's own loop voice,
    // which the LOT pass keeps running for as long as the bed does.
    bool sawVoice = false;
    for (int i = 0; i < 160; ++i) {
        engine->update(0.05F);
        if (engine->mixer().activeVoices() > 1) {
            sawVoice = true;
        }
        (void)engineStats(*engine, 2400);
    }
    CHECK(sawVoice);
}

// ---------------------------------------------------------------------------
// THE WIRING PASS, PROVED HEADLESS. Session's hooks (session.cpp) speak to a
// borrowed engine; here that engine is the null backend with the synthetic
// bank, so "the hook fired" is a voice count moving -- no device, no ears, no
// content/art needed. Voices are never rendered in this case, so nothing is
// ever reaped and every count comparison is monotone by construction.
// ---------------------------------------------------------------------------

TEST_CASE("session hooks reach an attached engine, and a detached one stays silent") {
    granadad::render::SessionConfig config;
    config.contentDir = granadad::content::contentDir();
    granadad::render::Session session(config);

    auto engine = AudioEngine::createNull(0xA11D10u, SoundBank::synthetic());
    REQUIRE(engine != nullptr);
    session.setAudio(engine.get());

    // The bed starts the moment there are ears: the authored Tarwalk spawn is
    // out of doors, so the harbour is what plays.
    CHECK(engine->currentBed() == BedId::Harbour);

    // A walking step speaks a footstep through the material-under-feet table
    // (the cadence clock boots ready, so the very first step sounds).
    const int atAttach = engine->mixer().activeVoices();
    granadad::sim::MoveInput forward;
    forward.forward = 1;
    session.step(forward);
    const int afterStep = engine->mixer().activeVoices();
    CHECK(afterStep > atAttach);

    // Opening the tiled Menu speaks the book pair's open half...
    session.toggleMenu();
    const int afterOpen = engine->mixer().activeVoices();
    CHECK(afterOpen > afterStep);
    // ...paging its tiles is paper...
    session.menuPageNext();
    const int afterFlip = engine->mixer().activeVoices();
    CHECK(afterFlip > afterOpen);
    // ...and the key that opened it closes it with the pair's other half.
    session.toggleMenu();
    CHECK(engine->mixer().activeVoices() > afterFlip);

    // Detached, the identical calls are inert -- which is what keeps every
    // other test and every headless capture exactly as quiet as before the
    // wiring pass existed.
    session.setAudio(nullptr);
    const int detached = engine->mixer().activeVoices();
    session.toggleMenu();
    session.step(forward);
    session.toggleMenu();
    CHECK(engine->mixer().activeVoices() == detached);
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

// ---------------------------------------------------------------------------
// THE LOT PASS. The second root, the new surfaces, the owner's beds and vox,
// the music director -- every case below runs on the synthetic bank or on
// whichever trees this checkout has, and NONE of them requires a LOT file:
// the docker gate has no LOT audio, and that is the law.
// ---------------------------------------------------------------------------

TEST_CASE("the second root: LOT variants stand in front of Kenney rows per id, and without the LOT tree the bank is exactly the Kenney bank") {
    SoundBank bank = SoundBank::load(granadad::content::contentDir());
    if (lotAudioPresent()) {
        // Twelve concrete steps, not Kenney's five: the LOT rows won the id.
        CHECK(bank.variantCount(SoundId::FootstepStone) ==
              granadad::audio::lotSoundPaths(SoundId::FootstepStone).size());
        CHECK(bank.variantCount(SoundId::FootstepStone) !=
              granadad::audio::soundPaths(SoundId::FootstepStone).size());
        CHECK(bank.variantCount(SoundId::PlayerHurt) == 4);
        CHECK(bank.variantCount(SoundId::AmbienceCoastal) == 1);
    } else {
        // The gate: nothing LOT-only loaded, and the LOT-only ids are silent.
        CHECK(bank.lotFiles() == 0);
        CHECK(bank.variantCount(SoundId::PlayerHurt) == 0);
        CHECK(bank.variantCount(SoundId::AmbienceCoastal) == 0);
        if (vendoredAudioPresent()) {
            CHECK(bank.variantCount(SoundId::FootstepStone) ==
                  granadad::audio::soundPaths(SoundId::FootstepStone).size());
            // A LOT-added id with a Kenney stand-in speaks the stand-in.
            CHECK(bank.variantCount(SoundId::Sheathe) ==
                  granadad::audio::soundPaths(SoundId::Sheathe).size());
        }
    }
    if (vendoredAudioPresent()) {
        // An id the LOT pass never touched is Kenney in both worlds.
        CHECK(granadad::audio::lotSoundPaths(SoundId::UiClick).empty());
        CHECK(bank.variantCount(SoundId::UiClick) ==
              granadad::audio::soundPaths(SoundId::UiClick).size());
    }
    // And whichever world this is, every LOT-pass id plays or stays silent
    // through an engine without a crash -- the missing-content contract.
    auto engine = AudioEngine::createNull(41, std::move(bank));
    for (std::size_t i = granadad::audio::soundIndex(SoundId::FootstepMetal);
         i < kSoundIdCount; ++i) {
        engine->playOneShot(static_cast<SoundId>(i));
        engine->playOneShotRoundRobin(static_cast<SoundId>(i));
    }
    engine->update(0.05F);
    (void)engineStats(*engine, 480);
}

TEST_CASE("a LOT row's gain is baked into the PCM at load") {
    Sample s;
    s.mono.assign(100, 0.25F);
    granadad::audio::applyGainDb(s, 6.0206F);  // x2
    CHECK(s.mono[50] == doctest::Approx(0.5F).epsilon(0.001));
    Sample st;
    st.stereo.assign(100, 0.5F);
    granadad::audio::applyGainDb(st, -6.0206F);  // /2
    CHECK(st.stereo[3] == doctest::Approx(0.25F).epsilon(0.001));
    granadad::audio::applyGainDb(st, 0.0F);  // the common case is a no-op
    CHECK(st.stereo[3] == doctest::Approx(0.25F).epsilon(0.001));
    CHECK(st.isStereo());
    CHECK(st.frames() == 50);
    CHECK_FALSE(s.isStereo());
    CHECK(s.frames() == 100);
}

TEST_CASE("a stereo sample plays its own two channels through the mixer") {
    Mixer mixer;
    Sample s;
    s.stereo.resize(48000 * 2U);
    for (std::size_t f = 0; f < 48000; ++f) {
        s.stereo[f * 2U] = 0.5F;   // left only
        s.stereo[f * 2U + 1U] = 0.0F;
    }
    const auto id = mixer.play(std::make_shared<const Sample>(std::move(s)),
                               Bus::Music, 1.0F, 0.0F, 1.0F, true);
    CHECK(id != Mixer::kNoVoice);
    // A loop fades in over 50 ms; read the second block.
    (void)renderStats(mixer, 4800);
    const BlockStats stats = renderStats(mixer, 480);
    CHECK(stats.peakL > 0.1F);
    CHECK(stats.peakR < 1.0e-4F);
    CHECK(mixer.voiceGain(id) == doctest::Approx(1.0F).epsilon(0.01));
    CHECK(mixer.voiceGain(Mixer::kNoVoice) == 0.0F);
    CHECK(mixer.voiceGain(id + 1000) == 0.0F);
}

TEST_CASE("a real LOT track decodes stereo, and a real LOT one-shot decodes mono, where the tree is staged") {
    if (!lotAudioPresent()) {
        MESSAGE("LOT audio absent; skipped");
        return;
    }
    const auto track = granadad::audio::loadTrack(granadad::content::contentDir(),
                                                  TrackId::InteriorExplore);
    REQUIRE(track != nullptr);
    CHECK(track->isStereo());
    // 35 tomb of echoes is 112 s at 48 kHz.
    CHECK(track->frames() > static_cast<std::size_t>(kSampleRate) * 100U);
    CHECK(track->frames() < static_cast<std::size_t>(kSampleRate) * 120U);
    for (std::size_t i = 0; i < track->stereo.size(); i += 997) {
        REQUIRE(std::isfinite(track->stereo[i]));
        REQUIRE(std::fabs(track->stereo[i]) <= 1.5F);
    }
    CHECK(granadad::audio::loadTrack(granadad::content::contentDir(), TrackId::None) ==
          nullptr);
    const SoundBank bank = SoundBank::load(granadad::content::contentDir());
    const auto coastal = bank.sample(SoundId::AmbienceCoastal, 0);
    REQUIRE(coastal != nullptr);
    CHECK_FALSE(coastal->isStereo());
    CHECK(coastal->frames() == static_cast<std::size_t>(kSampleRate) * 6U);  // 6.00 s
}

TEST_CASE("the LOT surfaces: steel is metal, the shards are gravel, the melt is mud, and every surface has a footstep id with rows") {
    const auto table = granadad::audio::materialSurfaceTable();
    const auto surfaceOf = [&](std::string_view material) {
        for (const auto& row : table) {
            if (row.materialId == material) {
                return row.surface;
            }
        }
        FAIL("material not in the table: " << material);
        return Surface::Stone;
    };
    CHECK(surfaceOf("steel") == Surface::Metal);
    CHECK(surfaceOf("lightstone_shards") == Surface::Gravel);
    CHECK(surfaceOf("chromatis_melt") == Surface::Mud);
    // The five that were never in question stay where they were.
    CHECK(surfaceOf("granite") == Surface::Stone);
    CHECK(surfaceOf("oak") == Surface::Wood);
    CHECK(surfaceOf("dirt") == Surface::Earth);
    CHECK(surfaceOf("cloth") == Surface::Cloth);
    CHECK(surfaceOf("ice") == Surface::Ice);
    CHECK(granadad::audio::footstepSoundFor(Surface::Metal) == SoundId::FootstepMetal);
    CHECK(granadad::audio::footstepSoundFor(Surface::Gravel) == SoundId::FootstepGravel);
    CHECK(granadad::audio::footstepSoundFor(Surface::Mud) == SoundId::FootstepMud);
    for (std::size_t i = 0; i < kSurfaceCount; ++i) {
        const Surface surface = static_cast<Surface>(i);
        const SoundId step = granadad::audio::footstepSoundFor(surface);
        CAPTURE(i);
        // Every surface keeps a Kenney row, so the gate steps; the six the
        // Footsteps Pack covers carry twelve LOT variants each, and Wood and
        // Cloth stay Kenney by design (no LOT set was staged for them).
        const bool lotSet = surface != Surface::Wood && surface != Surface::Cloth;
        CHECK(granadad::audio::lotSoundPaths(step).size() == (lotSet ? 12U : 0U));
        CHECK_FALSE(granadad::audio::soundPaths(step).empty());
        CHECK(granadad::audio::busFor(step) == Bus::Footsteps);
    }
    CHECK(granadad::audio::lotSoundPaths(SoundId::WadeSplash).size() == 12);
}

TEST_CASE("the beds carry the owner's loops: coastal on the wharf, stone under a roof, and the crossfade retires the old loop") {
    CHECK(granadad::audio::bedHasLoop(BedId::Harbour));
    CHECK(granadad::audio::bedHasLoop(BedId::Interior));
    CHECK_FALSE(granadad::audio::bedHasLoop(BedId::None));
    CHECK(granadad::audio::bedLoopSound(BedId::Harbour) == SoundId::AmbienceCoastal);
    CHECK(granadad::audio::bedLoopSound(BedId::Interior) == SoundId::AmbienceStone);
    CHECK(granadad::audio::busFor(SoundId::AmbienceCoastal) == Bus::Ambient);
    CHECK(granadad::audio::busFor(SoundId::AmbienceStone) == Bus::Ambient);
    CHECK(granadad::audio::busFor(SoundId::AmbienceOrganic) == Bus::Ambient);
    // The organic loop is registered and held: no bed reads it.
    CHECK(granadad::audio::bedLoopSound(BedId::Harbour) != SoundId::AmbienceOrganic);
    CHECK(granadad::audio::bedLoopSound(BedId::Interior) != SoundId::AmbienceOrganic);

    auto engine = AudioEngine::createNull(29, SoundBank::synthetic());
    engine->setTimeOfDay(12 * 3600);
    // Outside: the harbour bed's loop voice starts the moment the bed does.
    engine->startBed(BedId::Harbour, 0.2F);
    CHECK(engine->mixer().activeVoices() == 1);
    // Under a roof: the stone loop rises while the coastal one fades...
    engine->startBed(BedId::Interior, 0.2F);
    CHECK(engine->currentBed() == BedId::Interior);
    CHECK(engine->mixer().activeVoices() == 2);
    // ...and 0.8 s later (fade 0.2 s + the voice's own 0.2 s stop) the old
    // loop is gone. Before the first sparse creak can fire (primed >= 1 s).
    for (int i = 0; i < 16; ++i) {
        engine->update(0.05F);
        (void)engineStats(*engine, 2400);
    }
    CHECK(engine->mixer().activeVoices() == 1);
}

TEST_CASE("the owner's hurt vox plays in strict turn, and the random path never repeats a variant") {
    auto engine = AudioEngine::createNull(37, SoundBank::synthetic());  // 2 per id
    CHECK(engine->lastVariant(SoundId::PlayerHurt) == -1);
    engine->playOneShotRoundRobin(SoundId::PlayerHurt);
    CHECK(engine->lastVariant(SoundId::PlayerHurt) == 0);
    engine->playOneShotRoundRobin(SoundId::PlayerHurt);
    CHECK(engine->lastVariant(SoundId::PlayerHurt) == 1);
    engine->playOneShotRoundRobin(SoundId::PlayerHurt);
    CHECK(engine->lastVariant(SoundId::PlayerHurt) == 0);
    engine->playOneShotRoundRobin(SoundId::PlayerHurt);
    CHECK(engine->lastVariant(SoundId::PlayerHurt) == 1);
    CHECK(engine->mixer().activeVoices() == 4);
    // The random draw: with two variants, never the same twice running.
    int last = engine->lastVariant(SoundId::UiClick);
    CHECK(last == -1);
    for (int i = 0; i < 12; ++i) {
        engine->playOneShot(SoundId::UiClick);
        const int now = engine->lastVariant(SoundId::UiClick);
        CHECK(now >= 0);
        CHECK(now != last);
        last = now;
    }
    // A silent id (empty bank) neither advances nor crashes.
    auto mute = AudioEngine::createNull(1, SoundBank::empty());
    mute->playOneShotRoundRobin(SoundId::PlayerHurt);
    CHECK(mute->lastVariant(SoundId::PlayerHurt) == -1);
    CHECK(mute->mixer().activeVoices() == 0);
}

TEST_CASE("the music director: the pair is what is held, the combat edge swaps it, calm brings it back, the crossfade is monotone, and off is silent") {
    auto engine = AudioEngine::createNull(31, SoundBank::synthetic());
    MusicDirector& music = engine->music();

    // No loader: nothing wanted, nothing played, nothing loaded.
    music.setZone(MusicZone::Docks);
    engine->update(0.05F);
    CHECK(music.wanted() == TrackId::None);
    CHECK(music.playing() == TrackId::None);
    CHECK(music.loadedTracks() == 0);
    CHECK(engine->mixer().activeVoices() == 0);

    // The curation itself.
    CHECK(granadad::audio::musicCueFor(MusicZone::Docks).explore == TrackId::DocksExplore);
    CHECK(granadad::audio::musicCueFor(MusicZone::Docks).combat == TrackId::DocksCombat);
    CHECK(granadad::audio::musicCueFor(MusicZone::Interior).explore == TrackId::InteriorExplore);
    CHECK(granadad::audio::musicCueFor(MusicZone::Interior).combat == TrackId::InteriorExplore);
    CHECK(granadad::audio::trackName(TrackId::DocksExplore) == "13_whispers_of_the_abyss_loop.ogg");
    CHECK(granadad::audio::trackName(TrackId::DocksCombat) == "14_chains_of_the_damned_loop.ogg");
    CHECK(granadad::audio::trackName(TrackId::InteriorExplore) == "35_tomb_of_echoes_loop.ogg");
    CHECK(granadad::audio::trackName(TrackId::Climax) == "15_the_final_eclipse_loop.ogg");

    // A loader: the active pair is requested at once and nothing else.
    music.setTrackLoader(&syntheticTrack);
    engine->update(0.05F);
    music.finishLoading();
    CHECK(music.loadedTracks() == 2);
    CHECK(music.pendingLoads() == 0);
    engine->update(0.05F);
    CHECK(music.wanted() == TrackId::DocksExplore);
    CHECK(music.playing() == TrackId::DocksExplore);
    CHECK(engine->mixer().activeVoices() == 1);

    // The rise is monotone and arrives at the track gain (2.5 s; render 4 s).
    float last = 0.0F;
    for (int i = 0; i < 40; ++i) {
        (void)engineStats(*engine, 4800);
        const float g = engine->mixer().voiceGain(music.voice());
        CHECK(g >= last - 1.0e-5F);
        last = g;
    }
    CHECK(last == doctest::Approx(granadad::audio::kMusicTrackGain).epsilon(0.01));

    // The combat edge: the pair swaps, one voice rising as the other fades,
    // both monotone, and the faded one is reaped.
    music.noteCombat();
    CHECK(music.mood() == MusicMood::Combat);
    engine->update(0.05F);
    CHECK(music.playing() == TrackId::DocksCombat);
    CHECK(engine->mixer().activeVoices() == 2);
    const Mixer::VoiceId rising = music.voice();
    const Mixer::VoiceId fading = music.fadingVoice();
    CHECK(rising != fading);
    CHECK(fading != Mixer::kNoVoice);
    float up = 0.0F;
    float down = granadad::audio::kMusicTrackGain;
    for (int i = 0; i < 40; ++i) {
        (void)engineStats(*engine, 4800);
        const float gu = engine->mixer().voiceGain(rising);
        const float gd = engine->mixer().voiceGain(fading);
        CHECK(gu >= up - 1.0e-5F);
        CHECK(gd <= down + 1.0e-5F);
        up = gu;
        down = gd;
    }
    CHECK(up == doctest::Approx(granadad::audio::kMusicTrackGain).epsilon(0.01));
    CHECK(down == 0.0F);
    engine->update(0.05F);
    CHECK(engine->mixer().activeVoices() == 1);
    CHECK(music.fadingVoice() == Mixer::kNoVoice);
    CHECK(music.loadedTracks() == 2);  // still just the pair

    // The calm clock: brawlers standing hold it at zero; a swing thrown
    // resets it without starting anything; kMusicCalmSteps quiet steps end it.
    for (int i = 0; i < 100; ++i) {
        music.step(true);
    }
    CHECK(music.mood() == MusicMood::Combat);
    CHECK(music.calmSteps() == 0);
    for (int i = 0; i < 300; ++i) {
        music.step(false);
    }
    CHECK(music.calmSteps() == 300);
    music.noteSwing();
    CHECK(music.calmSteps() == 0);
    CHECK(music.mood() == MusicMood::Combat);
    for (int i = 0; i < granadad::audio::kMusicCalmSteps - 1; ++i) {
        music.step(false);
    }
    CHECK(music.mood() == MusicMood::Combat);
    music.step(false);
    CHECK(music.mood() == MusicMood::Exploration);
    CHECK(music.calmSteps() == 0);
    engine->update(0.05F);
    CHECK(music.playing() == TrackId::DocksExplore);
    // In exploration a swing at air starts nothing.
    music.noteSwing();
    CHECK(music.mood() == MusicMood::Exploration);
    for (int i = 0; i < 60; ++i) {
        (void)engineStats(*engine, 4800);
    }
    engine->update(0.05F);
    CHECK(engine->mixer().activeVoices() == 1);

    // Under a roof the interior pair replaces the docks pair -- one track,
    // both cues -- and the cache never exceeds two.
    music.setZone(MusicZone::Interior);
    music.setZone(MusicZone::Interior);  // re-asserting is a no-op
    engine->update(0.05F);
    music.finishLoading();
    engine->update(0.05F);
    CHECK(music.playing() == TrackId::InteriorExplore);
    CHECK(music.loadedTracks() <= 2);
    music.noteCombat();
    engine->update(0.05F);
    CHECK(music.wanted() == TrackId::InteriorExplore);
    CHECK(music.playing() == TrackId::InteriorExplore);  // same track: no swap
    for (int i = 0; i < 60; ++i) {
        (void)engineStats(*engine, 4800);
    }
    engine->update(0.05F);
    CHECK(engine->mixer().activeVoices() == 1);

    // --music-off: everything fades to nothing and nothing is wanted.
    music.setEnabled(false);
    CHECK_FALSE(music.enabled());
    engine->update(0.05F);
    CHECK(music.wanted() == TrackId::None);
    CHECK(music.playing() == TrackId::None);
    for (int i = 0; i < 40; ++i) {
        (void)engineStats(*engine, 4800);
    }
    engine->update(0.05F);
    CHECK(engine->mixer().activeVoices() == 0);
    CHECK(music.describe() == "music off (--music-off)");
    music.setEnabled(true);
    CHECK(music.describe().find("13_whispers_of_the_abyss_loop.ogg") != std::string::npos);
    CHECK(music.describe().find("35_tomb_of_echoes_loop.ogg") != std::string::npos);
    CHECK(music.describe().find("stereo") != std::string::npos);
}

TEST_CASE("the music director survives a cue whose track is missing, and a bus at zero mutes it") {
    auto engine = AudioEngine::createNull(43, SoundBank::synthetic());
    MusicDirector& music = engine->music();
    // A loader that has only the interior track: the docks cues stay silent
    // (the loader said null, once, and is never asked again), no wait, no
    // throw -- the missing-content contract, for music.
    music.setTrackLoader([](TrackId id) {
        return id == TrackId::InteriorExplore ? syntheticTrack(id)
                                              : std::shared_ptr<const Sample>();
    });
    music.setZone(MusicZone::Docks);
    engine->update(0.05F);
    music.finishLoading();
    engine->update(0.05F);
    CHECK(music.loadedTracks() == 0);
    CHECK(music.playing() == TrackId::None);
    CHECK(engine->mixer().activeVoices() == 0);
    music.noteCombat();
    engine->update(0.05F);
    CHECK(music.playing() == TrackId::None);
    music.setZone(MusicZone::Interior);
    engine->update(0.05F);
    music.finishLoading();
    engine->update(0.05F);
    CHECK(music.playing() == TrackId::InteriorExplore);
    CHECK(engine->mixer().activeVoices() == 1);
    // Bus::Music at zero silences the loop and only the loop.
    engine->setBusGain(Bus::Music, 0.0F);
    (void)engineStats(*engine, 4800);
    const BlockStats muted = engineStats(*engine, 480);
    CHECK(muted.nonZero == 0);
    engine->playOneShot(SoundId::UiConfirm);
    const BlockStats withUi = engineStats(*engine, 480);
    CHECK(withUi.nonZero > 0);
}

TEST_CASE("the stance speaks both edges through the session: the draw on hands up, the store on hands down") {
    granadad::render::SessionConfig config;
    config.contentDir = granadad::content::contentDir();
    granadad::render::Session session(config);
    auto engine = AudioEngine::createNull(0x10F0u, SoundBank::synthetic());
    session.setAudio(engine.get());
    CHECK(engine->lastVariant(SoundId::SwordDraw) == -1);
    CHECK(engine->lastVariant(SoundId::Sheathe) == -1);
    CHECK_FALSE(session.tavern().playerHandsUp());

    // The down-edge raises the hands in the sim; the step catches the edge.
    const granadad::sim::MoveInput still{};
    session.attackDown();
    CHECK(session.tavern().playerHandsUp());
    session.step(still);
    CHECK(engine->lastVariant(SoundId::SwordDraw) >= 0);
    CHECK(engine->lastVariant(SoundId::Sheathe) == -1);
    // A tap at nobody: the swing gets its air, and the hands stay up.
    session.attackUp();
    session.step(still);
    CHECK(session.tavern().playerHandsUp());
    CHECK(engine->lastVariant(SoundId::Sheathe) == -1);
    // The lull: kLowerHandsSteps of nothing (after the swing's own recovery)
    // lowers them, and the falling edge is the store.
    for (int i = 0; i < granadad::sim::kLowerHandsSteps + 200 && session.tavern().playerHandsUp(); ++i) {
        session.step(still);
    }
    CHECK_FALSE(session.tavern().playerHandsUp());
    session.step(still);
    CHECK(engine->lastVariant(SoundId::Sheathe) >= 0);
    // Detached, the identical edges are inert.
    session.setAudio(nullptr);
    const int detached = engine->mixer().activeVoices();
    session.attackDown();
    session.step(still);
    session.attackUp();
    session.step(still);
    CHECK(engine->mixer().activeVoices() == detached);
}
