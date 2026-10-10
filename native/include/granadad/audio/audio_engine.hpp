#pragma once

// The audio engine — ambient beds, footsteps by surface, UI and combat
// one-shots. Elder-Scrolls-shaped: region+time ambience (Morrowind's weather
// beds), footstep-per-surface (Oblivion's material table), and a restrained UI
// sound language.
//
// ---------------------------------------------------------------------------
// DETERMINISM — read this before touching anything.
// ---------------------------------------------------------------------------
// Audio is strictly RENDER/CLIENT-SIDE. Nothing under granadad/audio/ may be
// included by, linked into, or otherwise observable from the sim
// (native/src/sim, the gate targets). The twin-run gate and the world hash
// must be provably unaffected, and the guarantee is STRUCTURAL: granadad-sim
// and granadad-gate do not link granadad-audio, so a sim file including this
// header fails to build. Keep it that way. The engine's own RNG (variant
// pick, sparse-bed timers, pitch jitter) is seeded from the caller — wall
// clock in the client, a constant in tests — and never from the sim's
// streams; nothing computed here is ever handed back to the sim.
//
// ---------------------------------------------------------------------------
// THE WIRING PLAN — for the later pass, once the tree quiets. This engine is
// finished and tested standalone; the hooks below are deliberately NOT made
// yet because other teams own these files right now.
// ---------------------------------------------------------------------------
//
// CMake: link granadad-audio-sdl into the `granadad` client target
//   (native/CMakeLists.txt, the GRANADAD_BUILD_CLIENT block):
//       target_link_libraries(granadad PRIVATE granadad-render
//                             granadad-audio-sdl ${GRANADAD_SDL_TARGET})
//
// native/src/client/main.cpp — INIT, once, after the existing SDL_Init call
//   (createSdlAudioEngine does its own SDL_InitSubSystem(SDL_INIT_AUDIO), so
//   main.cpp's SDL_INIT_VIDEO | SDL_INIT_GAMEPAD line does not change):
//       #include "granadad/audio/backend_sdl.hpp"
//       auto audio = granadad::audio::createSdlAudioEngine();
//   `audio` may be null only on allocation failure; no device just means
//   deviceOpen() is false and every call is a cheap no-op. Skip creating it
//   entirely under --smoke/--screenshot if a flag is preferred.
//
// PER FRAME (main loop or Session::advance caller):
//       audio->setTimeOfDay(session.timeOfDay());   // session.hpp:154
//       audio->update(dtSeconds);                    // beds + sparse + gates
//
// native/src/render/session.cpp — EVENT HOOKS:
//   * Footsteps: where the player position advances a whole tile (or on the
//     step cadence the movement code already has), with TileQuery q:
//         audio->footstep(q.material(px, py, pz - 1),   // tile UNDER the feet
//                         running,
//                         q.fluidDepth(px, py, pz));     // wading layer
//     tile_query.hpp:106/110; Session already includes tile_query.hpp. The
//     engine rate-limits internally, so calling it every frame while moving
//     is fine.
//   * UI: menu open/close -> UiOpen/UiClose; focus move -> UiTick; accept ->
//     UiConfirm; refused -> UiError; page cycle in the tiled Menu ->
//     BookFlip; casebook/letters open -> BookOpen/BookClose; barter settle ->
//     CoinHandle. Wherever an EasedToggle target flips or an ImpactPulse is
//     triggered is exactly where the matching one-shot belongs.
//   * Combat: brawl hit lands -> PunchMedium/PunchHeavy by blow weight (the
//     same events that trigger the HUD's ImpactPulse); a swing that misses ->
//     Whoosh; knockdown -> ThudHeavy.
//   * Beds: outdoors in the Docks -> startBed(BedId::Harbour); under a roof
//     -> startBed(BedId::Interior); crossfades handle the transition, so the
//     hook is just "call startBed with whichever is right when it changes".
//
// Settings screen, later: setBusGain(Bus::..., v) is the volume sliders.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <memory>

#include "granadad/audio/backend.hpp"
#include "granadad/audio/mixer.hpp"
#include "granadad/audio/music_director.hpp"
#include "granadad/audio/sound_bank.hpp"
#include "granadad/audio/sound_ids.hpp"

namespace granadad::audio {

/// One row of the material -> footstep-surface table. Exposed so the test
/// suite can pin the table's names against render::materialIds() — a new
/// material raw then fails a test instead of silently thudding on stone.
struct MaterialSurfaceRow {
    std::string_view materialId;
    Surface surface;
};

/// The full 22-row table, index == material registry id.
[[nodiscard]] std::span<const MaterialSurfaceRow> materialSurfaceTable() noexcept;

/// Footstep surface for a MATERIAL-lane registry id. Out-of-range ids
/// (including VOID reads) land on Stone.
[[nodiscard]] Surface surfaceForMaterial(std::uint16_t materialId) noexcept;

/// The footstep one-shot for a surface.
[[nodiscard]] SoundId footstepSoundFor(Surface surface) noexcept;

/// THE LOT PASS: the loop-file layer a bed runs under its procedural layers
/// (SoundId::AmbienceCoastal for the Harbour, AmbienceStone for the
/// Interior). None for BedId::None. Exposed so the test suite can pin which
/// bed reads which loop.
[[nodiscard]] SoundId bedLoopSound(BedId bed) noexcept;
[[nodiscard]] bool bedHasLoop(BedId bed) noexcept;

/// 0 at night, 1 in full day, smooth ramps over dawn (05:00-07:00) and dusk
/// (19:00-21:00). Drives per-layer bed gains; exposed for tests.
[[nodiscard]] float dayness(int secondsSinceMidnight) noexcept;

class AudioEngine {
public:
    /// The generic factory: a bank, a backend, a seed. The convenience
    /// factories below are what call sites actually use.
    [[nodiscard]] static std::unique_ptr<AudioEngine> create(
        SoundBank bank, std::unique_ptr<Backend> backend, std::uint64_t rngSeed);

    /// Headless: loads the real bank from content::contentDir(), no device.
    /// render() is pulled by the caller. (Defined in backend_null.cpp.)
    [[nodiscard]] static std::unique_ptr<AudioEngine> createNull(std::uint64_t rngSeed);

    /// Headless with a caller-supplied bank — tests use SoundBank::synthetic()
    /// so voice behaviour is provable on checkouts with no content/art.
    [[nodiscard]] static std::unique_ptr<AudioEngine> createNull(std::uint64_t rngSeed,
                                                                 SoundBank bank);

    // createSdlAudioEngine() lives in backend_sdl.hpp / granadad-audio-sdl —
    // the ONLY audio code that touches SDL.

    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    /// Fire-and-forget one-shot on its default bus (busFor). Silent no-op for
    /// an id with no loaded variants. Variants are drawn at random, never the
    /// same variant twice in a row when more than one exists.
    void playOneShot(SoundId id, float gain = 1.0F, float pan = 0.0F,
                     float pitch = 1.0F);

    /// THE LOT PASS: the same, with the variants taken in STRICT ROUND-ROBIN
    /// order (0, 1, 2, ... wrapping) rather than at random — the owner's own
    /// hurt vox is four takes meant to be heard in turn.
    void playOneShotRoundRobin(SoundId id, float gain = 1.0F, float pan = 0.0F,
                               float pitch = 1.0F);

    /// The variant `id` last played through this engine, or -1 if never.
    /// Observable so a test can prove the round-robin and the session edges
    /// without ears.
    [[nodiscard]] int lastVariant(SoundId id) const noexcept;

    /// A movement step: picks the surface set from the material registry id,
    /// rate-limits to a walk/run cadence, jitters pitch, layers a WadeSplash
    /// when fluidDepth > 0. Returns whether a step actually sounded (the
    /// cadence gate may swallow it) — callers may simply ignore it.
    bool footstep(std::uint16_t materialId, bool running, int fluidDepth);

    /// Crossfades to `bed` over crossfadeSec. Re-asserting the current bed is
    /// a no-op, so calling this every frame with "whichever bed is right" is
    /// legal wiring.
    void startBed(BedId bed, float crossfadeSec = 2.0F);
    void stopBed(float fadeSec = 2.0F);
    [[nodiscard]] BedId currentBed() const noexcept { return activeBed_; }

    /// Session::timeOfDay() seconds (0..86399). Shapes per-layer bed gains.
    void setTimeOfDay(int secondsSinceMidnight) noexcept;

    /// WEATHER (render-side, one-way, like the clock): a multiplier on the
    /// Wind layer's bed gain -- 1 is the bed as authored, over 2 a blow,
    /// under 1 a still fog (render::Weather::windGain). Clamped 0..4; the
    /// mixer still caps the layer at one. Pushed by the client every frame
    /// beside setTimeOfDay; nothing here reaches the sim.
    void setWind(float gain) noexcept;
    [[nodiscard]] float wind() const noexcept { return wind_; }

    void setBusGain(Bus bus, float gain);

    /// Once per frame: advances bed crossfades, day/night layer gains, sparse
    /// one-shot timers and the footstep cadence clock. dt is wall-clock
    /// seconds (clamped internally against pauses/hitches).
    void update(float dtSec);

    /// Pulls mixed output — the null backend's whole output path. Under the
    /// SDL backend the device callback pulls the mixer directly and this is
    /// only useful for diagnostics.
    void render(float* interleavedStereo, int frames);

    /// Whether a real output device is attached (false for null, and for the
    /// SDL backend on a machine with no audio device).
    [[nodiscard]] bool deviceOpen() const noexcept { return deviceOpen_; }

    [[nodiscard]] const SoundBank& bank() const noexcept { return bank_; }
    [[nodiscard]] Mixer& mixer() noexcept { return mixer_; }

    /// THE LOT PASS: the music director (music_director.hpp). The client
    /// factories give it the file loader over contentDir(); the test factory
    /// gives it none, so a test engine is music-silent until it sets one.
    [[nodiscard]] MusicDirector& music() noexcept { return music_; }

private:
    AudioEngine(SoundBank bank, std::unique_ptr<Backend> backend,
                std::uint64_t rngSeed);

    static constexpr std::size_t kMaxSparse = 3;

    struct BedSlot {
        BedId bed = BedId::None;
        float env = 0.0F;       ///< 0..1 crossfade envelope
        float envRate = 0.0F;   ///< per second; negative = fading out
        std::array<float, kMaxSparse> sparseTimer{};
        std::array<Mixer::VoiceId, kMaxSparse> loopVoice{};
    };

    [[nodiscard]] std::uint64_t nextRandom() noexcept;
    [[nodiscard]] float rand01() noexcept;
    void playOn(Bus bus, SoundId id, float gain, float pan, float pitch);
    void primeSlot(BedSlot& slot);
    void retireSlot(BedSlot& slot);
    void advanceSlot(BedSlot& slot, float dt, float day);

    SoundBank bank_;
    Mixer mixer_;  ///< declared BEFORE backend_: the backend pulls it, so it
                   ///< must outlive the backend on destruction.
    std::unique_ptr<Backend> backend_;
    MusicDirector music_;  ///< after mixer_ (holds Mixer&; dies before it)
    bool deviceOpen_ = false;
    std::uint64_t rng_ = 0;
    int timeOfDay_ = 12 * 3600;
    float wind_ = 1.0F;
    float sinceStep_ = 1.0e6F;
    std::array<std::uint8_t, kSoundIdCount> lastVariant_{};
    BedSlot active_;
    BedSlot fading_;
    BedId activeBed_ = BedId::None;
};

}  // namespace granadad::audio
