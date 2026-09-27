#include "granadad/render/street_voice.hpp"

#include <algorithm>
#include <cstdlib>

#include "granadad/sim/angle.hpp"
#include "granadad/sim/crime.hpp"
#include "granadad/sim/tavern.hpp"
#include "granadad/sim/tile_query.hpp"
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

/// A ward body that did not move this tick: standing, not walking past.
[[nodiscard]] bool wardStanding(const sim::WardActor& actor) noexcept {
    return actor.prevX == actor.x && actor.prevY == actor.y && actor.prevBand == actor.band;
}

/// A stable scramble of three integers -- Fibonacci hashing on a 32-bit
/// word, ward_voice.cpp's own mix -- so a body's row offset is its own and
/// two mouths in one bucket are two rows. Integer arithmetic only.
[[nodiscard]] std::uint32_t mixThree(std::int32_t a, std::int32_t b, std::int32_t c) noexcept {
    std::uint32_t h = static_cast<std::uint32_t>(a) * 0x9E3779B9u;
    h ^= static_cast<std::uint32_t>(b) * 0x85EBCA6Bu;
    h ^= static_cast<std::uint32_t>(c) * 0xC2B2AE35u;
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
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
        case VoiceChannel::React:
            return "react";
        case VoiceChannel::Watch:
            return "watch";
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

std::int32_t voiceRowIndex(std::int32_t actorId, std::int32_t step, std::int32_t talks,
                           std::int32_t skip) noexcept {
    // The mouth's own offset (who, and how often talked to), then the
    // ten-second bucket of the step ADDED, not multiplied: one is co-prime
    // with every table width, so the next bucket is always the next row,
    // whatever the width. A line is held four to nine seconds, so one bucket
    // is one line and the same mouth in the next bucket is the next row.
    const std::int32_t offset =
        static_cast<std::int32_t>(mixThree(actorId, talks, 0x564F4943) & 0x0FFFFFFF);
    return offset + step / 600 + skip;
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

std::string fillVictim(std::string_view row, std::string_view victim) {
    static constexpr std::string_view kToken = "<victim>";
    std::string out;
    std::size_t at = 0;
    while (true) {
        const std::size_t found = row.find(kToken, at);
        if (found == std::string_view::npos) {
            out.append(row.substr(at));
            return out;
        }
        if (victim.empty()) {
            return {};  // a row that names a victim nobody can name
        }
        out.append(row.substr(at, found - at));
        out.append(victim);
        at = found + kToken.size();
    }
}

// ---------------------------------------------------------------------------
// the emitter
// ---------------------------------------------------------------------------

void StreetVoice::say(VoiceLine line) {
    if (line.empty()) {
        return;
    }
    holdSteps_ = holdFor(line.text);
    capped_ = false;
    lastOnChannel_[static_cast<std::size_t>(line.channel)] = line.step;
    last_[static_cast<std::size_t>(line.channel)] = line;
    if (isEvent(line.channel)) {
        lastEventStep_ = line.step + holdSteps_;
    }
    noteSpeaker(line.actorId, line.step);
    noteSaid(line.text, line.step);
    ++spoken_;
    current_ = std::move(line);
}

bool StreetVoice::saidLately(std::string_view text, std::int32_t step) const noexcept {
    for (const Said& said : said_) {
        if (said.text == text && step - said.step < kRowMemorySteps) {
            return true;
        }
    }
    return false;
}

void StreetVoice::noteSaid(const std::string& text, std::int32_t step) {
    for (Said& said : said_) {
        if (said.text == text) {
            said.step = step;
            return;
        }
    }
    if (said_.size() < kRowMemoryRows) {
        said_.push_back(Said{text, step});
        return;
    }
    Said* oldest = &said_.front();
    for (Said& said : said_) {
        if (said.step < oldest->step) {
            oldest = &said;
        }
    }
    *oldest = Said{text, step};
}

VoiceLine StreetVoice::pickFresh(const sim::BarkTables& barks,
                                 const std::vector<std::string>& chain, std::int32_t actorId,
                                 std::int32_t step, std::int32_t talks,
                                 std::string_view victim) const {
    VoiceLine out;
    // THE CHAIN'S RUNGS IN ORDER, each rung's rows from the mixed start,
    // the first row nobody has said inside two minutes. A spent cell falls
    // to the next rung -- the trade's cell to the family's, the family's to
    // the floor -- and a spent chain answers nothing, which the caller reads
    // as silence: the street going quiet beats the street repeating itself.
    for (const std::string& key : chain) {
        const std::vector<std::string>* rows = barks.rows(key);
        if (rows == nullptr || rows->empty()) {
            continue;
        }
        const std::int32_t count = static_cast<std::int32_t>(rows->size());
        for (std::int32_t skip = 0; skip < count; ++skip) {
            const std::string text =
                fillVictim(barks.line(key, voiceRowIndex(actorId, step, talks, skip)), victim);
            if (text.empty() || saidLately(text, step)) {
                continue;
            }
            out.key = key;
            out.text = text;
            out.actorId = actorId;
            out.step = step;
            return out;
        }
    }
    return out;
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

bool StreetVoice::dropIfSpeaker(std::string_view name) noexcept {
    if (name.empty() || !up() || current_.name != name) {
        return false;
    }
    holdSteps_ = 0;
    return true;
}

void StreetVoice::standDown() noexcept {
    holdSteps_ = 0;
    current_ = VoiceLine{};
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
    // The plate holds an event line's clock (VoiceScene::holdClock); a
    // passer-by's bark is a moment and runs.
    if (holdSteps_ > 0 && !(scene.holdClock && isEvent(current_.channel))) {
        --holdSteps_;
    }
    if (scene.room == nullptr) {
        return;
    }
    // The witnesses are read EVERY step, talking or not: a blow lands on the
    // step it lands, and a memory that moved is a fact whether or not the
    // row was free to say so.
    readWitnesses(scene);
    if (stepWatch(scene)) {
        return;
    }
    if (stepReact(scene)) {
        return;
    }
    if (scene.talking) {
        // The person in front of you owns the panel; the street holds its
        // tongue, and what it was saying steps aside -- the row is not drawn
        // under a panel anyway (Session::syncVoiceAnim), so a line left
        // holding there is state disagreeing with the screen. The goodbye
        // arrives through speakFarewell as the panel closes.
        standDown();
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
    // WHO SAW, BY THE SIM'S OWN RULE. SocialLedger::witness moves the
    // disposition and stamps the deed; SocialLedger::record does the same
    // and counts an injury. A memory whose deed or disposition moved WITHOUT
    // an injury counted is a witness, exactly the set Tavern::spreadWitness
    // wrote this step; the one with the injury is the victim, and the
    // witnesses of this step name him.
    std::vector<std::int32_t> witnessed;
    std::int32_t victimId = -1;
    for (const sim::Memory& memory : memories) {
        const MemoryRead* before = nullptr;
        for (const MemoryRead& read : memories_) {
            if (read.actorId == memory.actorId) {
                before = &read;
                break;
            }
        }
        const bool moved = before == nullptr ||
                           before->disposition != memory.disposition ||
                           before->lastDeed != memory.lastDeed;
        const bool victim = before == nullptr ? memory.injuries > 0
                                              : memory.injuries != before->injuries;
        if (reactDeedKey(memory.lastDeed).empty()) {
            continue;
        }
        if (victim && (before == nullptr || memory.injuries != before->injuries)) {
            victimId = memory.actorId;
        } else if (moved && !victim) {
            witnessed.push_back(memory.actorId);
        }
    }
    for (const std::int32_t actorId : witnessed) {
        const sim::Memory* memory = scene.room->dialogue().ledger().memoryOf(actorId);
        if (memory == nullptr) {
            continue;
        }
        bool queued = false;
        for (Witness& waiting : queue_) {
            if (waiting.actorId == actorId) {
                waiting.deed = memory->lastDeed;
                waiting.step = scene.step;
                waiting.victimId = victimId;
                queued = true;
                break;
            }
        }
        if (!queued) {
            queue_.push_back(Witness{actorId, memory->lastDeed, scene.step, victimId});
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
    // has his own lines (brawl.join); the Closing watchman has his halt; a
    // bouncer's reaction is his ladder (house.warning, house.eject, on the
    // alert row in his own name) and a witness line a second before it
    // would be the same man saying two things.
    const sim::Actor* chosen = nullptr;
    const Witness* chosenWitness = nullptr;
    std::int32_t chosenDistance = 0;
    for (const Witness& waiting : queue_) {
        const sim::Actor* actor = room.actorById(waiting.actorId);
        if (actor == nullptr || !gullSpeaker(*actor, room) || !gullPeaceable(*actor) ||
            actor->id() == closingId || actor->role() == sim::ActorRole::Bouncer ||
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
    std::string deed(reactDeedKey(chosenWitness->deed));
    if (deed == "struck" &&
        (room.escalated() ||
         (room.playerHandsUp() && room.playerWeapon() >= sim::kFirstLethalWeapon))) {
        // A blow landed with steel out is seen as the steel: the flip and
        // the blow can land on one step and the ledger keeps the last deed
        // written, but what the witness is looking at is the blade.
        deed = "steel";
    }
    // THE VICTIM, BY NAME. The ledger's own victim of this step; for a
    // killing, the corpse the room names (slainName reads the same roster).
    std::string victim;
    if (const sim::Actor* hurt = room.actorById(chosenWitness->victimId); hurt != nullptr) {
        victim = hurt->name();
    } else if (deed == "slew") {
        victim = room.slainName();
    }
    const std::string family(sim::jobFamilyKey(who.family));
    VoiceLine line = pickFresh(room.dialogue().barks(),
                               {"react." + family + "." + deed, "react." + deed}, chosen->id(),
                               scene.step, talksWith(room.dialogue().ledger(), chosen->id()),
                               victim);
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
        approachSaid_ = false;
        return false;
    }
    if (!closingSeen_) {
        closingSeen_ = true;
        closingSince_ = scene.step;
        approachSaid_ = false;
    }
    if (approachSaid_) {
        return false;
    }
    // ONE LINE PER MAN. His halt is on the alert row in his name and it is
    // the moment's line (the feel build's). The readback WAITS for it to
    // leave the row and follows as he crosses -- or, when he is at reach
    // and the halt has had its second on the row, REPLACES it (Session
    // stands the halt down the step the readback lands). Never stacked
    // under his name. A taproom is small and a Closing watchman is quick,
    // so a readback that only ever waited would never be heard indoors; a
    // readback that never waited would talk over the halt. With no halt on
    // the row (a page hid it, or he closed on paper without one) it follows
    // a breath after the edge.
    const std::int32_t distanceTiles =
        officer->distanceTo(scene.playerXQ8, scene.playerYQ8) / sim::kSubOne;
    const bool haltUp = scene.alertSpeaker == officer->name();
    const bool atReach = distanceTiles <= kWatchApproachNearTiles;
    const std::int32_t since = scene.step - closingSince_;
    if (haltUp && !(atReach && since >= kHaltReadSteps)) {
        return false;
    }
    if (!haltUp && since < 30) {
        return false;
    }
    // The readback keeps its own cadence rather than the channel's
    // five-second cooldown, which is for remarks in passing; it still yields
    // to nothing, being the top rank.
    if (up() && current_.channel > VoiceChannel::Watch) {
        return false;
    }
    // Keyed by the cause the room recorded, and for violence by WHAT IS UP:
    // a blade in the hands is the steel key, fists the bare one. "Put your
    // hands down" to a man with a knife out was the critic's own catch.
    std::vector<std::string> chain;
    switch (room.watchInterest()) {
        case sim::WatchCause::Violence:
            if (room.playerHandsUp() && room.playerWeapon() >= sim::kFirstLethalWeapon) {
                chain.emplace_back("watch.approach.violence.steel");
            }
            chain.emplace_back("watch.approach.violence");
            break;
        case sim::WatchCause::Contraband:
            chain.emplace_back("watch.approach.goods");
            break;
        case sim::WatchCause::Warrant:
        case sim::WatchCause::Both:
            chain.emplace_back("watch.approach.paper");
            break;
        case sim::WatchCause::None:
            break;
    }
    chain.emplace_back("watch.approach");
    VoiceLine line = pickFresh(room.dialogue().barks(), chain, officer->id(), scene.step,
                               talksWith(room.dialogue().ledger(), officer->id()));
    approachSaid_ = true;
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
        /// On the Gull's roster: the room's own pass cells, never the quay's.
        bool indoors;
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
                      true, true};
        // In the Gull a remark in passing is the speaker's EYES landing on
        // you: the three-clause notice rule, asked and not assumed, so a man
        // with his back to you or one the dark and the din hide you from
        // says nothing -- and for a watchman it is the whole of his cause.
        who.sees = room.noticeBy(actor).seen;
        if (!who.sees && who.family != sim::JobFamily::Watch) {
            continue;
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
                          sim::wardJobFamily(actor.type), true, false};
            // The street has no notice rule by ruling (the STREET SENSES
            // lane), so the one honest clause it can ask is the facing: a
            // man says "I see you" only to what is in front of him, and a
            // body creeping past crouched is greeted only by one facing it.
            const bool ahead =
                inFrontOf(actor.x, actor.y, actor.facing, scene.playerTileX, scene.playerTileY);
            if (who.family == sim::JobFamily::Watch) {
                who.sees = ahead;
            } else if (scene.crouched && !ahead) {
                continue;
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
    // first and absolute (DOCKS-GAZETTEER 4.1); then the paper; then what
    // the post has heard; then what his eyes are telling him, split by what
    // they see (a man crouching is not a man with his fists up) -- speaks
    // ahead of a nearer passer-by. A demand or a seethe, never a blow: the
    // arrest is the room's, at reach. A watchman with nothing on you is a
    // passer-by like any other.
    std::string watchKey;
    if (room.playerPresentsAsWielder()) {
        watchKey = "watch.wielder";
    } else if (crimes.warrant()) {
        watchKey = "watch.paper";
    } else if (crimes.heat() >= kWatchHeatNoticedAt) {
        watchKey = "watch.heat";
    } else if (scene.handsUp) {
        watchKey = "watch.notice.hands";
    } else if (scene.crouched) {
        watchKey = "watch.notice.sneak";
    }
    if (!watchKey.empty() && channelFree(VoiceChannel::Watch, scene.step)) {
        for (const Candidate& who : near) {
            if (who.family != sim::JobFamily::Watch || !who.sees ||
                !speakerFree(who.speakerId, scene.step, kWatchSpeakerCooldownSteps)) {
                continue;
            }
            VoiceLine line = pickFresh(room.dialogue().barks(), {watchKey}, who.speakerId,
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
    // to you lately and has something nobody has said lately: standing in a
    // crowd gets each neighbour once, and a crowd that has all had its say
    // goes quiet. A stool speaks from the room's own cells (pass.room),
    // never the quay's.
    if (!channelFree(VoiceChannel::Pass, scene.step)) {
        return false;
    }
    for (const Candidate& who : near) {
        if (!speakerFree(who.speakerId, scene.step, kPassSpeakerCooldownSteps)) {
            continue;
        }
        const sim::Attitude attitude = attitudeToward(ledger, who.speakerId);
        const std::vector<std::string> chain =
            who.indoors ? sim::voiceChain("pass.room", "", who.family, attitude)
                        : sim::voiceChain("pass", who.typeKey, who.family, attitude);
        VoiceLine line = pickFresh(room.dialogue().barks(), chain, who.speakerId, scene.step,
                                   talksWith(ledger, who.speakerId));
        if (line.empty()) {
            continue;
        }
        line.name = who.name;
        line.channel = VoiceChannel::Pass;
        say(std::move(line));
        return true;
    }
    return false;
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
    // EARSHOT IS A ROOM OR A STREET, AND A SIGHT LINE. Inside the Gull the
    // taproom is what you overhear; outside, the street -- a pair at a
    // table is not heard through the wall by a man on the Tarwalk, and a
    // pair round a corner is not heard at all. The same supercover ray the
    // witness rule asks of the tiles. AND A PAIR IS STANDING: a man at his
    // stool or his post, a body that did not move this tick -- three
    // walkers in file are not a conversation.
    const bool inside = room.playerInside();
    const auto heard = [&](std::int32_t x, std::int32_t y) {
        return scene.tiles == nullptr ||
               scene.tiles->lineOfSight(scene.playerTileX, scene.playerTileY, x, y,
                                        scene.playerBand);
    };
    std::vector<Body> near;
    if (inside) {
        for (const sim::Actor& actor : room.actors()) {
            if (!gullSpeaker(actor, room) || !gullPeaceable(actor) ||
                actor.band() != scene.playerBand) {
                continue;
            }
            const sim::Activity doing = actor.activity();
            if (doing != sim::Activity::Drinking && doing != sim::Activity::Working) {
                continue;  // a man walking to his stool is not mid-sentence
            }
            const std::int32_t apart =
                tilesApart(actor.tileX(), actor.tileY(), scene.playerTileX, scene.playerTileY);
            if (apart <= kOverheardEarshotTiles && heard(actor.tileX(), actor.tileY())) {
                near.push_back(
                    Body{actor.id(), actor.tileX(), actor.tileY(), apart, &actor, nullptr});
            }
        }
    } else if (scene.people != nullptr) {
        for (const sim::WardActor& actor : scene.people->actors()) {
            if (actor.band != scene.playerBand || !wardSpeaker(actor, scene.wardTalkId) ||
                !wardStanding(actor)) {
                continue;
            }
            const std::int32_t apart =
                tilesApart(actor.x, actor.y, scene.playerTileX, scene.playerTileY);
            if (apart <= kOverheardEarshotTiles && heard(actor.x, actor.y)) {
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
        std::int32_t company = -1;
        for (const Body& other : near) {
            if (other.speakerId != speaker.speakerId &&
                tilesApart(speaker.x, speaker.y, other.x, other.y) <= 1) {
                company = other.speakerId;
                break;
            }
        }
        if (company < 0) {
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
        VoiceLine line = pickFresh(
            room.dialogue().barks(),
            {"overheard." + std::string(sim::jobFamilyKey(family)), "overheard"},
            speaker.speakerId, scene.step, talksWith(room.dialogue().ledger(), speaker.speakerId));
        if (line.empty()) {
            continue;  // this mouth's cells are spent; the next pair may not be
        }
        line.name = std::move(name);
        line.partnerId = company;
        line.channel = VoiceChannel::Overheard;
        say(std::move(line));
        return true;
    }
    return false;
}

}  // namespace granadad::render
