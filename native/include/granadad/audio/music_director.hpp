#pragma once

// THE LOT PASS: the music director — exploration <-> combat, a crossfade at a
// time, over the owner's own Dark Fantasy loops (sound_ids.hpp: TrackId).
//
// WHAT IT IS. A small state machine the wiring in session.cpp pokes with the
// edges it already latches (the escalation rising edge, a blow landed or
// taken, a swing thrown, brawlers present under a roof or on the wharf), and
// a pair of loop voices on Bus::Music that it crossfades between. It decides
// nothing the sim can see: pure audio-side state, never hashed, exactly as
// the determinism note in audio_engine.hpp requires.
//
// TRACKS ARE LAZY AND AT MOST TWO ARE HELD. A decoded two-minute stereo loop
// is ~50 MB of float (half that mono), so the bank never touches them:
// the director decodes the ACTIVE PAIR — the zone's exploration and combat
// tracks — off the game thread (std::async over the TrackLoader) the moment
// the zone is set, and drops whatever the previous zone held once the new
// pair is what is wanted. A cue whose file is absent (the docker gate, a
// checkout without content/art/lot/audio) simply never starts a voice;
// nothing waits on it and nothing throws.
//
// THE MACHINE.
//   * setZone(z)             which pair: Docks (wharf, street) or Interior.
//                            Re-asserting is a no-op — legal every step.
//   * noteCombat()           combat NOW: the escalation edge, or the first
//                            landed/taken blow of a fight.
//   * noteSwing()            a blow thrown at air: keeps combat alive
//                            without starting it.
//   * step(brawlers)         once per SIM step: in combat, kMusicCalmSteps
//                            (10 s at 60 Hz) with no blow thrown or taken
//                            and no brawlers standing is the way back out.
//   * update()               once per FRAME: polls the loads, starts the
//                            wanted track, runs the crossfade.
//
// In tests the loader is a synthetic generator and finishLoading() joins the
// pending decodes, so "the combat edge swaps the track" is a voice-id and a
// gain the doctest can read, no files, no ears.

#include <array>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <string>

#include "granadad/audio/mixer.hpp"
#include "granadad/audio/sample.hpp"
#include "granadad/audio/sound_ids.hpp"

namespace granadad::audio {

enum class MusicZone : std::uint8_t {
    Docks = 0,  ///< out of doors: the wharf, the street
    Interior,   ///< under a roof: the Gull, any interior band
};

enum class MusicMood : std::uint8_t {
    Exploration = 0,
    Combat,
};

/// The active pair for a zone. explore == combat is legal (the interior).
struct MusicCue {
    TrackId explore;
    TrackId combat;
};

/// The owner's curation: Docks = 13 whispers / 14 chains; Interior = 35 tomb
/// for both. 15 the final eclipse (Climax) is reserved and never cued.
[[nodiscard]] MusicCue musicCueFor(MusicZone zone) noexcept;

/// Sim steps of calm (no blow thrown/taken, no brawlers) before combat music
/// gives way to exploration again. 600 = 10 s at the 60 Hz step.
inline constexpr std::int32_t kMusicCalmSteps = 600;

/// The exploration <-> combat crossfade, both halves.
inline constexpr float kMusicCrossfadeSec = 2.5F;

/// The gain every track plays at on Bus::Music. The loops peak -3..-11 dBFS.
inline constexpr float kMusicTrackGain = 0.40F;

/// Decodes one track, or returns null. Called OFF the game thread, so it must
/// be safe to run concurrently with the mixer (a file read + decode is).
using TrackLoader = std::function<std::shared_ptr<const Sample>(TrackId)>;

class MusicDirector {
public:
    explicit MusicDirector(Mixer& mixer);
    ~MusicDirector();
    MusicDirector(const MusicDirector&) = delete;
    MusicDirector& operator=(const MusicDirector&) = delete;

    /// No loader (the default for a test engine) means no music at all.
    void setTrackLoader(TrackLoader loader);

    /// --music-off. Off fades the playing voice out and loads nothing more.
    void setEnabled(bool on) noexcept;
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }

    // -- the edges session.cpp latches ---------------------------------------
    void setZone(MusicZone zone);
    void noteCombat() noexcept;
    void noteSwing() noexcept;
    void step(bool brawlersPresent) noexcept;

    /// Once per frame. Polls finished decodes, starts the wanted track,
    /// retires the faded one. Cheap when nothing changed.
    void update();

    // -- observers -----------------------------------------------------------
    [[nodiscard]] MusicZone zone() const noexcept { return zone_; }
    [[nodiscard]] MusicMood mood() const noexcept { return mood_; }
    [[nodiscard]] std::int32_t calmSteps() const noexcept { return calmSteps_; }
    /// The track the machine wants playing right now (None when off).
    [[nodiscard]] TrackId wanted() const noexcept;
    /// The track whose voice is the live one (None until a load lands).
    [[nodiscard]] TrackId playing() const noexcept { return playing_; }
    [[nodiscard]] Mixer::VoiceId voice() const noexcept { return voice_; }
    [[nodiscard]] Mixer::VoiceId fadingVoice() const noexcept { return fading_; }
    /// Decoded samples held right now — never more than the pair.
    [[nodiscard]] int loadedTracks() const noexcept;
    [[nodiscard]] int pendingLoads() const noexcept;

    /// TEST HOOK: blocks until every pending decode has landed, so the next
    /// update() is deterministic. The client never calls it.
    void finishLoading();

    /// The one start line main.cpp prints: which pairs, which layout.
    [[nodiscard]] std::string describe() const;

private:
    struct Slot {
        TrackId id = TrackId::None;
        std::shared_ptr<const Sample> sample;
        std::future<std::shared_ptr<const Sample>> pending;
        bool failed = false;  ///< the loader said null once; never retried
    };

    [[nodiscard]] Slot* slotFor(TrackId id) noexcept;
    void request(TrackId id);
    void poll();
    void crossfadeTo(Slot& slot);
    void stopAll(float fadeSec);

    Mixer& mixer_;
    TrackLoader loader_;
    bool enabled_ = true;
    MusicZone zone_ = MusicZone::Docks;
    MusicMood mood_ = MusicMood::Exploration;
    std::int32_t calmSteps_ = 0;
    std::array<Slot, 2> cache_{};
    Mixer::VoiceId voice_ = Mixer::kNoVoice;
    Mixer::VoiceId fading_ = Mixer::kNoVoice;
    TrackId playing_ = TrackId::None;
};

/// The file name a TrackId resolves to, for describe() and the report.
[[nodiscard]] std::string_view trackName(TrackId id) noexcept;

}  // namespace granadad::audio
