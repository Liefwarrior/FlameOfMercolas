// See music_director.hpp. Audio-side state only; never included by the sim.

#include "granadad/audio/music_director.hpp"

#include <chrono>
#include <utility>

#include "granadad/audio/sound_bank.hpp"

namespace granadad::audio {

MusicCue musicCueFor(MusicZone zone) noexcept {
    switch (zone) {
        case MusicZone::Docks:
            return {TrackId::DocksExplore, TrackId::DocksCombat};
        case MusicZone::Interior:
            return {TrackId::InteriorExplore, TrackId::InteriorExplore};
    }
    return {TrackId::None, TrackId::None};
}

std::string_view trackName(TrackId id) noexcept {
    const std::string_view path = trackPath(id);
    const std::size_t slash = path.rfind('/');
    if (slash == std::string_view::npos) {
        return path.empty() ? std::string_view("(none)") : path;
    }
    return path.substr(slash + 1);
}

MusicDirector::MusicDirector(Mixer& mixer) : mixer_(mixer) {}

MusicDirector::~MusicDirector() {
    // A std::async future joins its thread on destruction; the samples it
    // hands back are dropped unread. The voices die with the mixer, which
    // outlives this object by construction (AudioEngine declares it later).
}

void MusicDirector::setTrackLoader(TrackLoader loader) {
    loader_ = std::move(loader);
}

void MusicDirector::setEnabled(bool on) noexcept {
    enabled_ = on;
}

void MusicDirector::setZone(MusicZone zone) {
    if (zone == zone_) {
        return;  // re-asserting every step is legal wiring
    }
    zone_ = zone;
    // The new pair is wanted now; the old pair's slots are evicted lazily by
    // request() as the new tracks need room, so a voice still fading on the
    // old sample keeps its shared_ptr alive exactly as long as it needs it.
}

void MusicDirector::noteCombat() noexcept {
    mood_ = MusicMood::Combat;
    calmSteps_ = 0;
}

void MusicDirector::noteSwing() noexcept {
    calmSteps_ = 0;
}

void MusicDirector::step(bool brawlersPresent) noexcept {
    if (mood_ != MusicMood::Combat) {
        return;
    }
    if (brawlersPresent) {
        calmSteps_ = 0;
        return;
    }
    if (++calmSteps_ >= kMusicCalmSteps) {
        mood_ = MusicMood::Exploration;
        calmSteps_ = 0;
    }
}

TrackId MusicDirector::wanted() const noexcept {
    if (!enabled_ || !loader_) {
        return TrackId::None;
    }
    const MusicCue cue = musicCueFor(zone_);
    return mood_ == MusicMood::Combat ? cue.combat : cue.explore;
}

MusicDirector::Slot* MusicDirector::slotFor(TrackId id) noexcept {
    if (id == TrackId::None) {
        return nullptr;
    }
    for (Slot& slot : cache_) {
        if (slot.id == id) {
            return &slot;
        }
    }
    return nullptr;
}

void MusicDirector::request(TrackId id) {
    if (id == TrackId::None || !loader_ || slotFor(id) != nullptr) {
        return;
    }
    // A free slot, else one the current cue does not name and no voice is
    // playing from. ONLY THE ACTIVE PAIR IS EVER HELD: with two slots and a
    // cue of at most two tracks there is always one to take.
    const MusicCue cue = musicCueFor(zone_);
    Slot* victim = nullptr;
    for (Slot& slot : cache_) {
        if (slot.id == TrackId::None) {
            victim = &slot;
            break;
        }
    }
    if (victim == nullptr) {
        for (Slot& slot : cache_) {
            if (slot.id != cue.explore && slot.id != cue.combat &&
                slot.id != playing_) {
                victim = &slot;
                break;
            }
        }
    }
    if (victim == nullptr) {
        return;
    }
    if (victim->pending.valid()) {
        // A zone flipped back before its load landed: the rare hitch is
        // the honest cost of never holding a third decode.
        (void)victim->pending.get();
    }
    victim->id = id;
    victim->sample.reset();
    victim->failed = false;
    victim->pending = std::async(std::launch::async, loader_, id);
}

void MusicDirector::poll() {
    for (Slot& slot : cache_) {
        if (!slot.pending.valid()) {
            continue;
        }
        if (slot.pending.wait_for(std::chrono::seconds(0)) !=
            std::future_status::ready) {
            continue;
        }
        slot.sample = slot.pending.get();
        if (!slot.sample || slot.sample->frames() == 0) {
            slot.sample.reset();
            slot.failed = true;  // absent file: this cue stays silent
        }
    }
}

void MusicDirector::crossfadeTo(Slot& slot) {
    if (voice_ != Mixer::kNoVoice) {
        if (fading_ != Mixer::kNoVoice) {
            mixer_.stop(fading_, 0.2F);  // a double switch mid-fade drops the older
        }
        fading_ = voice_;
        mixer_.stop(fading_, kMusicCrossfadeSec);
    }
    // A loop voice starts at gain 0 by the mixer's own rule; the ramp to the
    // track gain IS the crossfade's rising half.
    voice_ = mixer_.play(slot.sample, Bus::Music, 0.0F, 0.0F, 1.0F, /*loop=*/true);
    mixer_.setVoiceGain(voice_, kMusicTrackGain, kMusicCrossfadeSec);
    playing_ = slot.id;
}

void MusicDirector::stopAll(float fadeSec) {
    if (fading_ != Mixer::kNoVoice) {
        mixer_.stop(fading_, fadeSec);
        fading_ = Mixer::kNoVoice;
    }
    if (voice_ != Mixer::kNoVoice) {
        mixer_.stop(voice_, fadeSec);
        voice_ = Mixer::kNoVoice;
    }
    playing_ = TrackId::None;
}

void MusicDirector::update() {
    if (!enabled_ || !loader_) {
        if (voice_ != Mixer::kNoVoice || fading_ != Mixer::kNoVoice) {
            stopAll(kMusicCrossfadeSec);
        }
        return;
    }
    // The active pair is requested the moment the zone is known, so the
    // combat edge finds its track already decoded and swaps at once.
    const MusicCue cue = musicCueFor(zone_);
    request(cue.explore);
    request(cue.combat);
    poll();

    const TrackId want = wanted();
    if (want != playing_) {
        Slot* slot = slotFor(want);
        if (slot != nullptr && slot->sample) {
            crossfadeTo(*slot);
        }
    }
    // The mixer reaps a stopped voice at silence; forget its id once gone.
    if (fading_ != Mixer::kNoVoice && mixer_.voiceGain(fading_) <= 0.0F) {
        fading_ = Mixer::kNoVoice;
    }
}

int MusicDirector::loadedTracks() const noexcept {
    int n = 0;
    for (const Slot& slot : cache_) {
        if (slot.sample) {
            ++n;
        }
    }
    return n;
}

int MusicDirector::pendingLoads() const noexcept {
    int n = 0;
    for (const Slot& slot : cache_) {
        if (slot.pending.valid()) {
            ++n;
        }
    }
    return n;
}

void MusicDirector::finishLoading() {
    for (Slot& slot : cache_) {
        if (slot.pending.valid()) {
            slot.pending.wait();
        }
    }
    poll();
}

std::string MusicDirector::describe() const {
    if (!enabled_) {
        return "music off (--music-off)";
    }
    if (!loader_) {
        return "music off (no track loader)";
    }
    const MusicCue docks = musicCueFor(MusicZone::Docks);
    const MusicCue inside = musicCueFor(MusicZone::Interior);
    std::string line = "music director on -- docks: ";
    line += trackName(docks.explore);
    line += " / ";
    line += trackName(docks.combat);
    line += "; interior: ";
    line += trackName(inside.explore);
    if (inside.combat != inside.explore) {
        line += " / ";
        line += trackName(inside.combat);
    }
    line += " (stereo loop voices, crossfade ";
    line += std::to_string(static_cast<int>(kMusicCrossfadeSec * 10.0F) / 10);
    line += ".";
    line += std::to_string(static_cast<int>(kMusicCrossfadeSec * 10.0F) % 10);
    line += " s, calm after ";
    line += std::to_string(kMusicCalmSteps);
    line += " steps)";
    return line;
}

}  // namespace granadad::audio
