#include "granadad/render/street_voice.hpp"

#include <algorithm>
#include <cstdlib>

#include "granadad/sim/angle.hpp"
#include "granadad/sim/crime.hpp"
#include "granadad/sim/tavern.hpp"
#include "granadad/sim/ward_actors.hpp"
#include "granadad/sim/ward_voice.hpp"
#include "granadad/sim/watch.hpp"

namespace granadad::render {

namespace {

/// Chebyshev distance in whole tiles -- the metric witnessesAround and the
/// alarm use, and the one a "within two tiles" reach means.
[[nodiscard]] std::int32_t tilesApart(std::int32_t ax, std::int32_t ay, std::int32_t bx,
                                      std::int32_t by) noexcept {
    return std::max(std::abs(ax - bx), std::abs(ay - by));
}

/// Whether the point (px, py) lies in the half-plane a body faces. A man says
/// "I see you" only to what is in front of him. Integer: the facing's Q16
/// forward vector against the tile offset, no trigonometry at the call.
[[nodiscard]] bool inFrontOf(std::int32_t ax, std::int32_t ay, sim::Angle facing, std::int32_t px,
                             std::int32_t py) noexcept {
    const std::int64_t fx = sim::forward_x_q16(facing);
    const std::int64_t fy = sim::forward_y_q16(facing);
    const std::int64_t dx = px - ax;
    const std::int64_t dy = py - ay;
    return fx * dx + fy * dy > 0;
}

[[nodiscard]] std::int32_t holdFor(const std::string& text) noexcept {
    const std::int32_t wanted =
        kVoiceHoldMinSteps + static_cast<std::int32_t>(text.size()) * kVoiceHoldPerGlyphSteps;
    return std::clamp(wanted, kVoiceHoldMinSteps, kVoiceHoldMaxSteps);
}

[[nodiscard]] bool isEvent(VoiceChannel channel) noexcept {
    return channel >= VoiceChannel::Farewell;
}

[[nodiscard]] std::int32_t talksWith(const sim::SocialLedger& ledger, std::int32_t actorId) noexcept {
    const sim::Memory* memory = ledger.memoryOf(actorId);
    return memory == nullptr ? 0 : memory->talks;
}

/// A Gull actor who can be a speaker at all: present, upright, a person, and
/// not the one you are talking to. The fight's own men and the Closing
/// watchman are ruled out by the caller that needs to.
[[nodiscard]] bool gullSpeaker(const sim::Actor& actor, const sim::Tavern& room) noexcept {
    return actor.present() && !sim::isFloored(actor.activity()) &&
           actor.role() != sim::ActorRole::Vermin && actor.id() != room.talkingTo();
}

[[nodiscard]] bool gullPeaceable(const sim::Actor& actor) noexcept {
    const sim::Activity doing = actor.activity();
    return doing != sim::Activity::Warning && doing != sim::Activity::Ejecting &&
           doing != sim::Activity::Brawling;
}

/// A ward body who can be a speaker: a living person on the board, not
/// running for its life, not the one you are talking to.
[[nodiscard]] bool wardSpeaker(const sim::WardActor& actor, std::int32_t wardTalkId) noexcept {
    return actor.visible() && sim::isPerson(actor.type) && actor.policy != sim::WardPolicy::Flee &&
           actor.id != wardTalkId;
}

}  // namespace

// ---------------------------------------------------------------------------
// the vocabulary
// ---------------------------------------------------------------------------

std::string_view voiceChannelName(VoiceChannel channel) noexcept {
    switch (channel) {
        case VoiceChannel::Overheard:
            return "overheard";
        case VoiceChannel::Pass:
            return "pass";
        case VoiceChannel::Farewell:
            return "farewell";
        case VoiceChannel::Watch:
            return "watch";
        case VoiceChannel::React:
            return "react";
    }
    return "?";
}

std::string_view reactDeedKey(sim::Deed deed) noexcept {
    switch (deed) {
        case sim::Deed::Struck:
            return "struck";
        case sim::Deed::DrewSteel:
            return "steel";
        case sim::Deed::Slew:
            return "slew";
        default:
            return {};
    }
}

// ---------------------------------------------------------------------------
// the selection
// ---------------------------------------------------------------------------

std::int32_t voiceRowIndex(std::int32_t actorId, std::int32_t step, std::int32_t talks) noexcept {
    // Ten-second buckets: a line is held four to nine seconds, so one bucket
    // is one line, and the same mouth in the next bucket is the next row.
    const std::int32_t bucket = step / 600;
    return actorId * 7 + talks * 3 + bucket * 5;
}

VoiceLine pickVoiceLine(const sim::BarkTables& barks, const std::vector<std::string>& chain,
                        std::int32_t actorId, std::int32_t step, std::int32_t talks) {
    VoiceLine out;
    const std::string_view key = barks.resolve(chain);
    if (key.empty()) {
        return out;
    }
    out.key = std::string(key);
    out.text = std::string(barks.line(key, voiceRowIndex(actorId, step, talks)));
    out.actorId = actorId;
    out.step = step;
    return out;
}

sim::Attitude attitudeToward(const sim::SocialLedger& ledger, std::int32_t actorId) noexcept {
    return ledger.knows(actorId) ? ledger.attitudeOf(actorId) : ledger.wardAttitude();
}

std::string wordedReaction(std::int32_t before, std::int32_t after) {
    if (after == before) {
        return {};
    }
    const sim::Attitude was = sim::attitudeFor(before);
    const sim::Attitude now = sim::attitudeFor(after);
    if (now != was) {
        // A band crossed: the header's own word changed colour, and the row
        // says which way, in the same word.
        const std::string word(sim::attitudeName(now));
        return now > was ? "(THAT WENT DOWN WELL. " + word + " NOW)"
                         : "(THAT LANDED BADLY. " + word + " NOW)";
    }
    const std::int32_t delta = after - before;
    if (delta >= 5) {
        return "(A NOD. THAT WENT DOWN WELL)";
    }
    if (delta > 0) {
        return "(THE FACE SOFTENS A HAIR)";
    }
    if (delta <= -5) {
        return "(THE SHOULDER TURNS)";
    }
    return "(THAT LANDED BADLY)";
}

// ---------------------------------------------------------------------------
// the emitter
// ---------------------------------------------------------------------------

void StreetVoice::say(VoiceLine line) {
    if (line.empty()) {
        return;
    }
    holdSteps_ = holdFor(line.text);
    lastOnChannel_[static_cast<std::size_t>(line.channel)] = line.step;
    last_[static_cast<std::size_t>(line.channel)] = line;
    if (isEvent(line.channel)) {
        lastEventStep_ = line.step + holdSteps_;
    }
    noteSpeaker(line.actorId, line.step);
    ++spoken_;
    current_ = std::move(line);
}

bool StreetVoice::channelFree(VoiceChannel channel, std::int32_t step) const noexcept {
    std::int32_t cooldown = 0;
    switch (channel) {
        case VoiceChannel::Overheard:
            cooldown = kOverheardCooldownSteps;
            break;
        case VoiceChannel::Pass:
            cooldown = kPassCooldownSteps;
            break;
        case VoiceChannel::Farewell:
            cooldown = 0;
            break;
        case VoiceChannel::Watch:
            cooldown = kWatchCooldownSteps;
            break;
        case VoiceChannel::React:
            cooldown = kReactCooldownSteps;
            break;
    }
    if (step - lastOnChannel_[static_cast<std::size_t>(channel)] < cooldown) {
        return false;
    }
    // THE ROW'S RANK. A line still up yields only to its own rank or above;
    // an ambient line also waits a breath past the last event line.
    if (up() && channel < current_.channel) {
        return false;
    }
    if (!isEvent(channel) && step < lastEventStep_ + kAmbientAfterEventSteps) {
        return false;
    }
    return true;
}

bool StreetVoice::speakerFree(std::int32_t actorId, std::int32_t step,
                              std::int32_t cooldown) const noexcept {
    for (const Spoken& spoken : speakers_) {
        if (spoken.actorId == actorId) {
            return step - spoken.step >= cooldown;
        }
    }
    return true;
}

void StreetVoice::noteSpeaker(std::int32_t actorId, std::int32_t step) {
    for (Spoken& spoken : speakers_) {
        if (spoken.actorId == actorId) {
            spoken.step = step;
            return;
        }
    }
    if (speakers_.size() < kVoiceSpeakerMemory) {
        speakers_.push_back(Spoken{actorId, step});
        return;
    }
    // Full: the oldest entry gives way, which only shortens one cooldown.
    Spoken* oldest = &speakers_.front();
    for (Spoken& spoken : speakers_) {
        if (spoken.step < oldest->step) {
            oldest = &spoken;
        }
    }
    *oldest = Spoken{actorId, step};
}

void StreetVoice::clearIfSettled(bool faded) noexcept {
    if (faded && holdSteps_ <= 0) {
        current_ = VoiceLine{};
    }
}

void StreetVoice::speakFarewell(std::string name, std::string line, std::string key,
                                std::int32_t actorId, std::int32_t step) {
    if (line.empty()) {
        return;
    }
    VoiceLine spoken;
    spoken.name = std::move(name);
    spoken.text = std::move(line);
    spoken.key = std::move(key);
    spoken.actorId = actorId;
    spoken.channel = VoiceChannel::Farewell;
    spoken.step = step;
    if (!channelFree(VoiceChannel::Farewell, step)) {
        // A react or a watch line holds the row; the goodbye is not worth
        // talking over a killing. Recorded on its channel all the same.
        last_[static_cast<std::size_t>(VoiceChannel::Farewell)] = spoken;
        return;
    }
    say(std::move(spoken));
}

// ---------------------------------------------------------------------------
// the step
// ---------------------------------------------------------------------------

void StreetVoice::step(const VoiceScene& scene) {
    if (holdSteps_ > 0) {
        --holdSteps_;
    }
    if (scene.room == nullptr) {
        return;
    }
    // The witnesses are read EVERY step, talking or not: a blow lands on the
    // step it lands, and a memory that moved is a fact whether or not the
    // row was free to say so.
    readWitnesses(scene);
    if (stepReact(scene)) {
        return;
    }
    if (stepWatch(scene)) {
        return;
    }
    if (scene.talking) {
        // The person in front of you owns the panel; the street holds its
        // tongue. The goodbye arrives through speakFarewell as the panel
        // closes.
        return;
    }
    if (stepPass(scene)) {
        return;
    }
    (void)stepOverheard(scene);
}

// --- react -----------------------------------------------------------------

void StreetVoice::readWitnesses(const VoiceScene& scene) {
    const std::vector<sim::Memory>& memories = scene.room->dialogue().ledger().memories();
    if (!primed_) {
        // The first read is a baseline, never an event: a session does not
        // boot with the room remarking on deeds the ledger was seeded with.
        primed_ = true;
        memories_.clear();
        for (const sim::Memory& memory : memories) {
            memories_.push_back(
                MemoryRead{memory.actorId, memory.disposition, memory.lastDeed, memory.injuries});
        }
        return;
    }
    for (const sim::Memory& memory : memories) {
        const MemoryRead* before = nullptr;
        for (const MemoryRead& read : memories_) {
            if (read.actorId == memory.actorId) {
                before = &read;
                break;
            }
        }
        // WHO SAW, BY THE SIM'S OWN RULE. SocialLedger::witness moves the
        // disposition and stamps the deed; SocialLedger::record does the
        // same and counts an injury. A memory whose deed or disposition
        // moved WITHOUT an injury counted is a witness, exactly the set
        // Tavern::spreadWitness wrote this step; the victim has the injury.
        const bool moved = before == nullptr ||
                           before->disposition != memory.disposition ||
                           before->lastDeed != memory.lastDeed;
        const bool victim = before == nullptr ? memory.injuries > 0
                                              : memory.injuries != before->injuries;
        if (moved && !victim && !reactDeedKey(memory.lastDeed).empty()) {
            bool queued = false;
            for (Witness& waiting : queue_) {
                if (waiting.actorId == memory.actorId) {
                    waiting.deed = memory.lastDeed;
                    waiting.step = scene.step;
                    queued = true;
                    break;
                }
            }
            if (!queued) {
                queue_.push_back(Witness{memory.actorId, memory.lastDeed, scene.step});
            }
        }
    }
    memories_.clear();
    for (const sim::Memory& memory : memories) {
        memories_.push_back(
            MemoryRead{memory.actorId, memory.disposition, memory.lastDeed, memory.injuries});
    }
}

bool StreetVoice::stepReact(const VoiceScene& scene) {
    // Old news drops out whether or not the channel is free.
    queue_.erase(std::remove_if(queue_.begin(), queue_.end(),
                                [&](const Witness& waiting) {
                                    return scene.step - waiting.step > kReactQueueSteps;
                                }),
                 queue_.end());
    if (queue_.empty() || !channelFree(VoiceChannel::React, scene.step)) {
        return false;
    }
    const sim::Tavern& room = *scene.room;
    const sim::Actor* officer = room.respondingWatchman();
    const std::int32_t closingId =
        (officer != nullptr && room.watchStance() == sim::Tavern::WatchStance::Closing)
            ? officer->id()
            : -1;
    // The nearest witness speaks: everybody who saw it feels the same about
    // it, and the one beside you is the one you hear. A man in the fight
    // has his own lines (brawl.join); the Closing watchman has his halt.
    const sim::Actor* chosen = nullptr;
    const Witness* chosenWitness = nullptr;
    std::int32_t chosenDistance = 0;
    for (const Witness& waiting : queue_) {
        const sim::Actor* actor = room.actorById(waiting.actorId);
        if (actor == nullptr || !gullSpeaker(*actor, room) || !gullPeaceable(*actor) ||
            actor->id() == closingId ||
            !speakerFree(actor->id(), scene.step, kReactSpeakerCooldownSteps)) {
            continue;
        }
        const std::int32_t distance = actor->distanceTo(scene.playerXQ8, scene.playerYQ8);
        if (chosen == nullptr || distance < chosenDistance ||
            (distance == chosenDistance && actor->id() < chosen->id())) {
            chosen = actor;
            chosenWitness = &waiting;
            chosenDistance = distance;
        }
    }
    if (chosen == nullptr) {
        return false;
    }
    const sim::Speaker who = room.speakerFor(*chosen);
    const std::string deed(reactDeedKey(chosenWitness->deed));
    const std::string family(sim::jobFamilyKey(who.family));
    VoiceLine line = pickVoiceLine(room.dialogue().barks(),
                                   {"react." + family + "." + deed, "react." + deed},
                                   chosen->id(), scene.step,
                                   talksWith(room.dialogue().ledger(), chosen->id()));
    const std::int32_t spokenId = chosen->id();
    queue_.erase(std::remove_if(queue_.begin(), queue_.end(),
                                [spokenId](const Witness& waiting) {
                                    return waiting.actorId == spokenId;
                                }),
                 queue_.end());
    if (line.empty()) {
        return false;
    }
    line.name = chosen->name();
    line.channel = VoiceChannel::React;
    say(std::move(line));
    return true;
}

// --- the Watch ---------------------------------------------------------------

bool StreetVoice::stepWatch(const VoiceScene& scene) {
    const sim::Tavern& room = *scene.room;
    const sim::Actor* officer = room.respondingWatchman();
    const bool closing =
        officer != nullptr && room.watchStance() == sim::Tavern::WatchStance::Closing;
    if (!closing) {
        closingSeen_ = false;
        approachFarSaid_ = false;
        approachNearSaid_ = false;
        return false;
    }
    if (!closingSeen_) {
        closingSeen_ = true;
        closingSince_ = scene.step;
        approachFarSaid_ = false;
        approachNearSaid_ = false;
    }
    if (approachNearSaid_) {
        return false;
    }
    // The readback keeps its own cadence (the two delays above) rather than
    // the channel's five-second cooldown, which is for remarks in passing;
    // it still yields to nothing, being the top rank.
    if (up() && current_.channel > VoiceChannel::Watch) {
        return false;
    }
    // THE READBACK, keyed by the cause the room recorded: violence, paper
    // (a warrant, with or without goods) or goods alone.
    std::string cause;
    switch (room.watchInterest()) {
        case sim::WatchCause::Violence:
            cause = "violence";
            break;
        case sim::WatchCause::Contraband:
            cause = "goods";
            break;
        case sim::WatchCause::Warrant:
        case sim::WatchCause::Both:
            cause = "paper";
            break;
        case sim::WatchCause::None:
            break;
    }
    const std::int32_t distanceTiles =
        officer->distanceTo(scene.playerXQ8, scene.playerYQ8) / sim::kSubOne;
    std::int32_t band = -1;
    if (!approachFarSaid_ && scene.step - closingSince_ >= kWatchApproachDelaySteps) {
        band = 0;
    } else if (approachFarSaid_ && distanceTiles <= kWatchApproachNearTiles &&
               scene.step - lastOnChannel_[static_cast<std::size_t>(VoiceChannel::Watch)] >=
                   kWatchApproachNearDelaySteps) {
        band = 1;
    }
    if (band < 0) {
        return false;
    }
    std::vector<std::string> chain;
    if (!cause.empty()) {
        chain.push_back("watch.approach." + cause);
    }
    chain.emplace_back("watch.approach");
    // The band rides the step's bucket (a whole bucket on, so the row index
    // moves by five and never by a multiple of the table's width) so the two
    // lines of one approach are two different rows of the same table.
    VoiceLine line = pickVoiceLine(room.dialogue().barks(), chain, officer->id(),
                                   scene.step + band * 600,
                                   talksWith(room.dialogue().ledger(), officer->id()));
    line.step = scene.step;
    if (band == 0) {
        approachFarSaid_ = true;
    } else {
        approachNearSaid_ = true;
    }
    if (line.empty()) {
        return false;
    }
    line.name = officer->name();
    line.channel = VoiceChannel::Watch;
    say(std::move(line));
    return true;
}

// --- pass ------------------------------------------------------------------

bool StreetVoice::stepPass(const VoiceScene& scene) {
    const sim::Tavern& room = *scene.room;
    const sim::SocialLedger& ledger = room.dialogue().ledger();
    const sim::CrimeLedger& crimes = room.dialogue().crimes();

    // Everybody within reach, the Gull's roster first at a tie and the
    // lowest id after that: a rule, so two runs hear the same mouth.
    struct Candidate {
        std::int32_t speakerId;
        std::int32_t apart;
        std::string name;
        std::string typeKey;
        sim::JobFamily family;
        /// A watchman's eyes: whether he has you. Always true for anybody
        /// else -- a passer-by needs no line of sight to say good day.
        bool sees;
    };
    std::vector<Candidate> near;
    for (const sim::Actor& actor : room.actors()) {
        if (!gullSpeaker(actor, room) || !gullPeaceable(actor) || actor.band() != scene.playerBand) {
            continue;
        }
        const std::int32_t apart =
            tilesApart(actor.tileX(), actor.tileY(), scene.playerTileX, scene.playerTileY);
        if (apart > kPassReachTiles) {
            continue;
        }
        Candidate who{actor.id(), apart, actor.name(), std::string(), room.speakerFor(actor).family,
                      true};
        // In the Gull a watchman's remark is his EYES landing on you: the
        // three-clause notice rule, asked and not assumed.
        if (who.family == sim::JobFamily::Watch) {
            who.sees = room.noticeBy(actor).seen;
        }
        near.push_back(std::move(who));
    }
    if (scene.people != nullptr) {
        for (const sim::WardActor& actor : scene.people->actors()) {
            if (actor.band != scene.playerBand || !wardSpeaker(actor, scene.wardTalkId)) {
                continue;
            }
            const std::int32_t apart =
                tilesApart(actor.x, actor.y, scene.playerTileX, scene.playerTileY);
            if (apart > kPassReachTiles) {
                continue;
            }
            Candidate who{sim::kWardSpeakerIdBase + actor.id, apart,
                          scene.people->identity(actor.id).name,
                          std::string(sim::wardVoiceKey(actor.type)),
                          sim::wardJobFamily(actor.type), true};
            if (who.family == sim::JobFamily::Watch) {
                // The street Watch has no eyes by ruling (the STREET SENSES
                // lane); a man says "I see you" only to what is in front of
                // him.
                who.sees = inFrontOf(actor.x, actor.y, actor.facing, scene.playerTileX,
                                     scene.playerTileY);
            }
            near.push_back(std::move(who));
        }
    }
    if (near.empty()) {
        return false;
    }
    std::sort(near.begin(), near.end(), [](const Candidate& a, const Candidate& b) {
        return a.apart != b.apart ? a.apart < b.apart : a.speakerId < b.speakerId;
    });

    // THE WATCH READS YOU BEFORE ANYBODY GREETS YOU. A watchman within reach
    // who has you in his eyes and something to say about you -- deference
    // first and absolute (DOCKS-GAZETTEER 4.1), then the paper, then what
    // the post has heard, then what his eyes are telling him -- speaks ahead
    // of a nearer passer-by. A demand or a seethe, never a blow: the arrest
    // is the room's, at reach. A watchman with nothing on you is a passer-by
    // like any other.
    std::string watchKey;
    if (room.playerPresentsAsWielder()) {
        watchKey = "watch.wielder";
    } else if (crimes.warrant()) {
        watchKey = "watch.paper";
    } else if (crimes.heat() >= kWatchHeatNoticedAt) {
        watchKey = "watch.heat";
    } else if (scene.crouched || scene.handsUp) {
        watchKey = "watch.notice";
    }
    if (!watchKey.empty() && channelFree(VoiceChannel::Watch, scene.step)) {
        for (const Candidate& who : near) {
            if (who.family != sim::JobFamily::Watch || !who.sees ||
                !speakerFree(who.speakerId, scene.step, kWatchSpeakerCooldownSteps)) {
                continue;
            }
            VoiceLine line = pickVoiceLine(room.dialogue().barks(), {watchKey}, who.speakerId,
                                           scene.step, talksWith(ledger, who.speakerId));
            if (line.empty()) {
                break;
            }
            line.name = who.name;
            line.channel = VoiceChannel::Watch;
            say(std::move(line));
            return true;
        }
    }

    // THE ONE LINE AS YOU PASS, from the nearest mouth that has not spoken
    // to you lately. Only the nearest: if that one is on cooldown the street
    // stays quiet this step rather than reaching past it for a second, which
    // is how a crowd would sound.
    if (!channelFree(VoiceChannel::Pass, scene.step)) {
        return false;
    }
    const Candidate& who = near.front();
    if (!speakerFree(who.speakerId, scene.step, kPassSpeakerCooldownSteps)) {
        return false;
    }
    const sim::Attitude attitude = attitudeToward(ledger, who.speakerId);
    VoiceLine line = pickVoiceLine(room.dialogue().barks(),
                                   sim::voiceChain("pass", who.typeKey, who.family, attitude),
                                   who.speakerId, scene.step, talksWith(ledger, who.speakerId));
    if (line.empty()) {
        return false;
    }
    line.name = who.name;
    line.channel = VoiceChannel::Pass;
    say(std::move(line));
    return true;
}

// --- overheard ---------------------------------------------------------------

bool StreetVoice::stepOverheard(const VoiceScene& scene) {
    if (!channelFree(VoiceChannel::Overheard, scene.step)) {
        return false;
    }
    const sim::Tavern& room = *scene.room;
    struct Body {
        std::int32_t speakerId;
        std::int32_t x;
        std::int32_t y;
        std::int32_t apart;
        const sim::Actor* gull;
        const sim::WardActor* ward;
    };
    std::vector<Body> near;
    for (const sim::Actor& actor : room.actors()) {
        if (!gullSpeaker(actor, room) || !gullPeaceable(actor) || actor.band() != scene.playerBand) {
            continue;
        }
        const sim::Activity doing = actor.activity();
        if (doing != sim::Activity::Drinking && doing != sim::Activity::Working) {
            continue;  // a man walking to his stool is not mid-sentence
        }
        const std::int32_t apart =
            tilesApart(actor.tileX(), actor.tileY(), scene.playerTileX, scene.playerTileY);
        if (apart <= kOverheardEarshotTiles) {
            near.push_back(Body{actor.id(), actor.tileX(), actor.tileY(), apart, &actor, nullptr});
        }
    }
    if (scene.people != nullptr) {
        for (const sim::WardActor& actor : scene.people->actors()) {
            if (actor.band != scene.playerBand || !wardSpeaker(actor, scene.wardTalkId)) {
                continue;
            }
            const std::int32_t apart =
                tilesApart(actor.x, actor.y, scene.playerTileX, scene.playerTileY);
            if (apart <= kOverheardEarshotTiles) {
                near.push_back(Body{sim::kWardSpeakerIdBase + actor.id, actor.x, actor.y, apart,
                                    nullptr, &actor});
            }
        }
    }
    if (near.size() < 2) {
        return false;
    }
    // Nearest first, the lowest id at a tie: the same pair in both runs.
    std::sort(near.begin(), near.end(), [](const Body& a, const Body& b) {
        return a.apart != b.apart ? a.apart < b.apart : a.speakerId < b.speakerId;
    });
    for (const Body& speaker : near) {
        if (!speakerFree(speaker.speakerId, scene.step, kOverheardSpeakerCooldownSteps)) {
            continue;
        }
        bool company = false;
        for (const Body& other : near) {
            if (other.speakerId != speaker.speakerId &&
                tilesApart(speaker.x, speaker.y, other.x, other.y) <= 1) {
                company = true;
                break;
            }
        }
        if (!company) {
            continue;
        }
        sim::JobFamily family = sim::JobFamily::Serf;
        std::string name;
        if (speaker.gull != nullptr) {
            family = room.speakerFor(*speaker.gull).family;
            name = speaker.gull->name();
        } else {
            family = sim::wardJobFamily(speaker.ward->type);
            name = scene.people->identity(speaker.ward->id).name;
        }
        VoiceLine line = pickVoiceLine(
            room.dialogue().barks(),
            {"overheard." + std::string(sim::jobFamilyKey(family)), "overheard"},
            speaker.speakerId, scene.step, talksWith(room.dialogue().ledger(), speaker.speakerId));
        if (line.empty()) {
            return false;
        }
        line.name = std::move(name);
        line.channel = VoiceChannel::Overheard;
        say(std::move(line));
        return true;
    }
    return false;
}

}  // namespace granadad::render
