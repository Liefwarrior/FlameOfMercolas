#include "granadad/sim/dialogue.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace granadad::sim {

namespace {

/// The one authored quest. Its general table is spoken by anybody -- "the
/// clerk? aye, everyone's heard" -- and a notable with an authored beat speaks
/// theirs instead, which is what the fallback chain is for.
constexpr std::string_view kQuestId = "vanished-clerk";

/// #82. How far a dialled register moves the MOMENTARY read of a standing
/// used to gate what is on the table right now -- never the ledger's own
/// number, which only ever moves through a recorded Deed. Big enough to tip a
/// standing sitting near a threshold either way (a Cold docker just shy of
/// -10, a Neutral one just past it); small enough that it can never walk a
/// Hostile all the way up to Warm on tone alone. See
/// DialogueDirector::toneAttitude.
constexpr std::int32_t kToneGateShift = 15;

[[nodiscard]] std::string upper(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        out.push_back(c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c);
    }
    return out;
}

/// What a coin count looks like on a menu line.
[[nodiscard]] std::string coins(std::int32_t amount) {
    return std::to_string(amount) + "C";
}

/// #82. One leaf of the TELL ME ABOUT tree's LOCATION or THING branch: an id
/// the authored topic_barks.json speaks from, and the label the player reads.
struct TopicIdLabel {
    std::string_view id;
    std::string_view label;
};

/// A curated, compiled-in roster rather than a raws file of its own: every one
/// of these is public, common-knowledge -- the same reason `gossip`'s generic
/// table needs no notableId -- so nobody has to be a party to knowing the
/// Weighhouse exists. Every id and every fact behind it is DOCKS-GAZETTEER's
/// own (K01, K03, K17, K13, K21); nothing here invents a place.
constexpr TopicIdLabel kLocationTopics[] = {
    {"weighhouse", "THE WEIGHHOUSE"},
    {"gilded_gull", "THE GILDED GULL"},
    {"mission", "THE MISSION"},
    {"drowned_hold", "THE DROWNED HOLD"},
    {"saltgate_watch", "THE WATCH-POST"},
};

/// Same shape, for the ward's own things -- the tenure ruling's ground penny,
/// the Flame's public office, a passport, a scalp bounty, the tide. Every
/// fact behind these traces to DECISIONS.md, MAGIC-CANON.md or the crime
/// layer already shipped; nothing here invents a mechanic.
constexpr TopicIdLabel kThingTopics[] = {
    {"ground_penny", "THE GROUND PENNY"},
    {"the_flame", "THE FLAME"},
    {"a_passport", "A PASSPORT"},
    {"a_scalp", "A SCALP"},
    {"the_tide", "THE TIDE"},
};

}  // namespace

std::string_view topicKindName(TopicKind kind) noexcept {
    switch (kind) {
        case TopicKind::Personal:
            return "personal";
        case TopicKind::WardTalk:
            return "ward talk";
        case TopicKind::History:
            return "history";
        case TopicKind::Quest:
            return "quest";
        case TopicKind::Mastery:
            return "mastery";
        case TopicKind::Trade:
            return "trade";
        case TopicKind::BuyDrinkFor:
            return "buy a drink";
        case TopicKind::PickPocket:
            return "pick pocket";
        case TopicKind::Leave:
            return "leave";
        case TopicKind::Buy:
            return "buy";
        case TopicKind::Join:
            return "join";
        case TopicKind::Advance:
            return "advance";
        case TopicKind::QuestBeat:
            return "quest beat";
        case TopicKind::Learn:
            return "learn";
        case TopicKind::Forge:
            return "forge";
        case TopicKind::Fence:
            return "fence";
        case TopicKind::Lean:
            return "lean";
        case TopicKind::Favour:
            return "favour";
        case TopicKind::TakeContract:
            return "take work";
        case TopicKind::TurnIn:
            return "turn in";
        case TopicKind::Sanction:
            return "sanction";
        case TopicKind::Rival:
            return "rival";
        case TopicKind::Ask:
            return "ask";
        case TopicKind::Category:
            return "category";
        case TopicKind::Location:
            return "location";
        case TopicKind::Thing:
            return "thing";
        case TopicKind::Back:
            return "back";
    }
    return "?";
}

DialogueDirector::DialogueDirector() : factions_(std::make_shared<FactionRegistry>()) {
    standings_.attach(factions_);
}

DialogueDirector DialogueDirector::load(const std::filesystem::path& contentDir) {
    DialogueDirector out;
    out.barks_ = BarkTables::load(contentDir);
    out.notables_ = NotableRegistry::load(contentDir);
    out.skills_ = SkillTrack::load(contentDir);
    out.factions_ = std::make_shared<const FactionRegistry>(FactionRegistry::load(contentDir));
    out.standings_.attach(out.factions_);
    out.quests_ = QuestBook::load(contentDir);
    out.spellbook_ = Spellbook::load(contentDir);
    // S6. The board's templates are read AFTER the notables and the factions,
    // because it refuses its own rows by name against both of them.
    out.contractRaws_ = std::make_shared<const ContractRaws>(
        ContractRaws::load(contentDir, out.notables_, *out.factions_));
    out.board_.attach(out.contractRaws_);
    return out;
}

void DialogueDirector::postContracts(std::int32_t day, std::uint64_t worldSeed) {
    board_.refresh(day, worldSeed, standings_);
}

bool DialogueDirector::brokerWillTalk(const ContractBroker& broker) const noexcept {
    if (broker.needs == "none") {
        // A public bounty. The ward wants the rats gone and does not care who
        // brings them.
        return true;
    }
    if (broker.needs == "acquaintance") {
        // A landlord who has to not dislike you. This is the one gate in the
        // build that reads ATTITUDE rather than a rung, and it is the right
        // shape for it: buying for your own cellar off somebody is a favour,
        // and nobody does a favour for a man they have thrown out.
        //
        // #82: which side of "not disliked" you land on can move with the
        // register you ask in, the same as it would across a real counter --
        // toneAttitude() reads the MOMENTARY standing, not the ledger's own
        // number, so asking politely can open this list and a flat "no" can
        // close it, without either ever touching what he actually remembers
        // of you.
        return toneAttitude() >= Attitude::Neutral;
    }
    const std::int32_t index = factions_->indexOf(broker.faction);
    return standings_.isMember(index);
}

std::int32_t DialogueDirector::speakerFaction() const noexcept {
    return factions_->indexOf(speaker_.factionId);
}

std::vector<std::string> DialogueDirector::toned(const std::vector<std::string>& chain) const {
    // TONE::NORMAL WIDENS NOTHING. This is the one invariant every other
    // guarantee here rests on: every line spoken and every gate read before
    // #82 stays exactly what it was, for the entire game, unless the player
    // actually reaches for POLITE or BLUNT.
    if (tone_ == Tone::Normal) {
        return chain;
    }
    std::vector<std::string> out;
    out.reserve(chain.size() * 2);
    const std::string suffix = "." + std::string(toneKey(tone_));
    for (const std::string& candidate : chain) {
        // The tone-tagged variant goes FIRST and the untagged line right
        // behind it, so a register with nothing authored under this exact key
        // degrades to precisely the line that key already spoke -- never to
        // silence, and never to a less specific key jumping ahead of a more
        // specific untagged one.
        out.push_back(candidate + suffix);
        out.push_back(candidate);
    }
    return out;
}

Attitude DialogueDirector::toneAttitude() const noexcept {
    std::int32_t shift = 0;
    if (tone_ == Tone::Polite) {
        shift = kToneGateShift;
    } else if (tone_ == Tone::Blunt) {
        shift = -kToneGateShift;
    }
    const std::int32_t shifted = std::clamp(
        ledger_.dispositionOf(speaker_.actorId) + shift, kDispositionMin, kDispositionMax);
    return attitudeFor(shifted);
}

Deed DialogueDirector::toneListenDeed() const noexcept {
    switch (tone_) {
        case Tone::Polite:
            return Deed::SpokePolitely;
        case Tone::Blunt:
            return Deed::SpokeBluntly;
        case Tone::Normal:
            return Deed::Listened;
    }
    return Deed::Listened;
}

void DialogueDirector::setTone(Tone tone) noexcept {
    if (tone_ == tone) {
        return;
    }
    tone_ = tone;
    // #82's whole point: dialling this can put a topic on the table that was
    // not there a breath ago, or take one off it. Rebuilt immediately rather
    // than on the next choose(), so a player watching the list sees it move
    // the instant they turn the dial. If nothing is open there is no list
    // yet to change -- the next open() builds one with whatever this is set
    // to.
    //
    // REBUILDS WHICHEVER LEVEL THE PLAYER IS ACTUALLY LOOKING AT, not always
    // the root: the topic tree (also #82) can put a player three menus deep
    // into TELL ME ABOUT -> A PERSON, and turning the register there must
    // move THAT list, not silently walk them back out to the root behind
    // their own cursor.
    if (open_) {
        rebuildCurrentLevel();
    }
}

std::vector<std::string> DialogueDirector::factionChain(std::string_view factionId,
                                                        std::string_view verb) const {
    std::vector<std::string> chain;
    if (!factionId.empty()) {
        chain.push_back("faction." + std::string(factionId) + "." + std::string(verb));
    }
    chain.push_back("faction." + std::string(verb));
    return chain;
}

void DialogueDirector::recordDeed(Deed deed) {
    ledger_.record(speaker_.actorId, deed);
    // The guild hears about it too. ONE call site, so a deed can never reach
    // the person and miss the people they belong to -- which is the whole
    // mechanism by which a stranger becomes joinable in the first place.
    standings_.recordDeed(speakerFaction(), deed);
}

// ---------------------------------------------------------------------------
// opening
// ---------------------------------------------------------------------------

bool DialogueDirector::open(const Speaker& speaker, std::int32_t secondOfDay) {
    if (speaker.name.empty()) {
        return false;
    }
    haggle_.reset();
    speaker_ = speaker;
    secondOfDay_ = secondOfDay;
    open_ = true;
    ++conversations_;
    // The counter that rotates the authored rows is bumped BEFORE the greeting
    // is picked, so speaking to somebody twice in a row is two sentences.
    talkIndex_ = ledger_.noteConversation(speaker.actorId);
    attitude_ = ledger_.attitudeOf(speaker.actorId);

    // A mood override outranks everything: somebody on the floor of a taproom
    // does not greet you by your standing with them.
    std::vector<std::string> chain;
    if (!speaker_.moodKey.empty()) {
        chain.push_back(speaker_.moodKey);
    }
    // #82: which register you walk up talking in colours the greeting itself
    // -- toned() is a no-op unless the player has already dialled off NORMAL,
    // so this reproduces every greeting exactly as before until they do.
    const std::vector<std::string> greetKeys =
        toned(greetChain(speaker_.family, attitude_, timeBandOf(secondOfDay)));
    chain.insert(chain.end(), greetKeys.begin(), greetKeys.end());

    greetingKey_ = std::string(barks_.resolve(chain));
    greeting_ = std::string(barks_.line(greetingKey_, rowIndexFor(TopicKind::Personal, 0)));
    if (greeting_.empty()) {
        // The raws are missing or being edited. Say so plainly rather than
        // inventing a line and passing it off as canon.
        greeting_ = "...";
    }
    lastLine_ = greeting_;
    // Talking to somebody civilly is worth almost nothing, and that is the
    // point: you cannot chat your way into being liked.
    recordDeed(Deed::Spoke);
    attitude_ = ledger_.attitudeOf(speaker_.actorId);
    buildTopics();
    return true;
}

void DialogueDirector::close() noexcept {
    open_ = false;
    haggle_.reset();
    bench_.reset();
    topics_.clear();
    menu_ = DialogueMenu::Root;
}

std::int32_t DialogueDirector::rowIndexFor(TopicKind kind, std::int32_t payload) const noexcept {
    // Deterministic and draw-free: who, how many times, which topic. Small
    // co-prime multipliers so two topics in one conversation do not land on the
    // same row of two different tables in lockstep.
    return talkIndex_ * 7 + static_cast<std::int32_t>(kind) * 3 + payload * 5 + speaker_.actorId;
}

std::string DialogueDirector::speak(const std::vector<std::string>& chain, TopicKind kind,
                                    std::int32_t payload) {
    // #82: every caller of speak() gets tone for free -- the guild verbs, the
    // refusals, the rival's own table. A no-op today wherever nobody has
    // authored a tone-tagged row for that key; the day somebody does, it
    // speaks without another line of C++.
    const std::string key(barks_.resolve(toned(chain)));
    if (key.empty()) {
        return {};
    }
    return std::string(barks_.line(key, rowIndexFor(kind, payload)));
}

void DialogueDirector::buildTopics() {
    topics_.clear();
    menu_ = DialogueMenu::Root;

    // 0. #79 -- WHAT HAS NO WORDS GETS NO LIST.
    //
    // A beast has already answered, out of greet.beast, and that answer is the
    // whole of what it has. Everything below assumes somebody who can be asked
    // a question: a cat offered THE VANISHED CLERK and PICK THEIR POCKET is a
    // menu built by a machine that was not looking at what it was talking to.
    // Only the way out is left, so the surface still opens, still says what it
    // said, and still closes on the same key as every other conversation.
    if (speaker_.beast) {
        Topic away;
        away.kind = TopicKind::Leave;
        away.label = "LEAVE IT BE";
        topics_.push_back(std::move(away));
        return;
    }

    // 1. #82 -- TELL ME ABOUT. THEIR BUSINESS, every story they may tell, the
    //    ward's own talk and their shop talk all moved behind this door and
    //    into the tree's PERSON and WORK branches -- confirmed dead in every
    //    consumer outside this file before they moved, so nothing at the root
    //    reaches for them any more. Shown only when the tree actually answers
    //    to at least one of its five branches, the same "no topic leads to
    //    nothing" law every branch below applies to itself.
    if (personAvailable() || locationAvailable() || thingAvailable() || workAvailable() ||
        questAvailable()) {
        Topic topic;
        topic.kind = TopicKind::Ask;
        topic.label = "TELL ME ABOUT...";
        topics_.push_back(std::move(topic));
    }

    // 4. The vanished clerk. Everybody has heard. #82: STAYS AT THE ROOT,
    //    deliberately -- too much already reaches for it here (scripted
    //    captures, the crime and faction suites) to relocate it. It is
    //    MIRRORED into the tree's QUEST branch instead; see buildQuestTopics().
    {
        std::vector<std::string> chain;
        const std::string base = "quest." + std::string(kQuestId);
        if (!speaker_.notableId.empty()) {
            chain.push_back(base + ".rumor." + speaker_.notableId);
        }
        chain.push_back(base);
        const std::string key(barks_.resolve(chain));
        if (!key.empty()) {
            Topic topic;
            topic.kind = TopicKind::Quest;
            topic.label = "THE VANISHED CLERK";
            topic.barkKey = key;
            topics_.push_back(std::move(topic));
        }
    }

    // 4b. Any authored line this person is the party to, at its CURRENT stage.
    //     Built before the guild topics, because the Mission's oath is both a
    //     quest beat and a sign-on and the quest's own label wins.
    bool joinOffered = false;
    bool learnOffered = false;
    bool forgeOffered = false;
    for (const Questline& line : quests_.lines()) {
        if (journal_.done(line.id)) {
            continue;
        }
        const std::int32_t at = journal_.stage(line.id);
        if (at < 0 || static_cast<std::size_t>(at) >= line.stages.size()) {
            continue;
        }
        const QuestStage& stage = line.stages[static_cast<std::size_t>(at)];
        // THE GATE IS SOCIAL-TOPOLOGICAL AND NOTHING ELSE: this topic is here
        // because you are standing in front of the party to it. No roll.
        if (speaker_.notableId.empty() || stage.party != speaker_.notableId) {
            continue;
        }
        Topic topic;
        topic.payload = at;
        topic.arg = line.id;
        topic.barkKey = stage.barkKey;
        topic.label = stage.label;
        switch (stage.kind) {
            case StageKind::Oath:
                topic.kind = TopicKind::Join;
                joinOffered = line.faction == speaker_.recruitsFor;
                break;
            case StageKind::Alms:
            case StageKind::Tally:
                topic.kind = TopicKind::QuestBeat;
                // The count is on the label, so a player never has to guess how
                // much of a counted stage is behind them.
                topic.label += " (" + std::to_string(journal_.counter(line.id)) + "/" +
                               std::to_string(stage.count) + ")";
                break;
            case StageKind::Talk:
                topic.kind = TopicKind::QuestBeat;
                break;
            case StageKind::Teach:
                topic.kind = TopicKind::Learn;
                learnOffered = true;
                break;
            case StageKind::Forge:
                topic.kind = TopicKind::Forge;
                forgeOffered = true;
                break;
            case StageKind::Unknown:
                continue;
        }
        topics_.push_back(std::move(topic));
    }

    // 4c. The guilds: signing on, and climbing. Belonging to a faction and
    //     speaking for it are different facts -- every patron in the Gull is a
    //     dockhand and exactly one of them recruits.
    if (!speaker_.recruitsFor.empty()) {
        const std::int32_t index = factions_->indexOf(speaker_.recruitsFor);
        if (factions_->ladder(index) != nullptr) {
            const Faction* faction = factions_->at(index);
            const std::string name =
                upper(faction == nullptr ? speaker_.recruitsFor : faction->displayName);
            if (!standings_.isMember(index)) {
                if (!joinOffered) {
                    Topic topic;
                    topic.kind = TopicKind::Join;
                    topic.label = "SIGN ON: " + name;
                    topic.arg = speaker_.recruitsFor;
                    topics_.push_back(std::move(topic));
                }
            } else if (standings_.nextRung(index) != nullptr) {
                Topic topic;
                topic.kind = TopicKind::Advance;
                const FactionRank* rung = standings_.nextRung(index);
                topic.label = "ASK TO BE MADE " + upper(rung->title);
                topic.arg = speaker_.recruitsFor;
                topics_.push_back(std::move(topic));
            }
        }
    }

    // 4d. What a rung actually BUYS, on the one person who can give it. These
    //     stand outside the questline on purpose: when the line is finished the
    //     priest still teaches, because a guild you have used up is not a guild.
    if (speaker_.teaches) {
        const std::int32_t index = speakerFaction();
        if (!learnOffered && standings_.unlocked(index, "teaching")) {
            Topic topic;
            topic.kind = TopicKind::Learn;
            topic.label = "LEARN A CRAFTING";
            topics_.push_back(std::move(topic));
        }
        if (!forgeOffered && standings_.unlocked(index, "forge")) {
            Topic topic;
            topic.kind = TopicKind::Forge;
            topic.label = "COMPOSE A CRAFTING";
            topics_.push_back(std::move(topic));
        }
    }

    // 4e. S6 -- THE WORK. A broker's own jobs, waiting jobs first so a player
    //     who came back with a full sack is not scrolling past tomorrow's
    //     offers to find tonight's. Every label names the good, the number and
    //     the person who wants it, and every one of those came out of the
    //     owner's files.
    if (const ContractBroker* broker = brokerFor(speaker_.notableId); broker != nullptr) {
        if (!brokerWillTalk(*broker)) {
            // ASKING IS ALWAYS ALLOWED, and being told no is an answer. A
            // broker who will not deal with you yet still has to be visibly a
            // broker, or the guild ladder has nothing at the top of it that a
            // player can see before they climb.
            Topic topic;
            topic.kind = TopicKind::TakeContract;
            topic.label = "ASK ABOUT WORK";
            topic.payload = -1;
            topics_.push_back(std::move(topic));
        } else {
            for (const std::int32_t id : board_.takenBy(broker->id)) {
                const Contract* row = board_.find(id);
                if (row == nullptr) {
                    continue;
                }
                Topic topic;
                topic.kind = TopicKind::TurnIn;
                topic.label = "HAND OVER " + std::to_string(row->units) + " " +
                              std::string(contrabandLabelFor(row->good, row->units));
                topic.payload = id;
                topic.arg = row->offerId;
                topics_.push_back(std::move(topic));
            }
            for (const std::int32_t id : board_.offeredBy(broker->id)) {
                const Contract* row = board_.find(id);
                if (row == nullptr) {
                    continue;
                }
                Topic topic;
                topic.kind = TopicKind::TakeContract;
                topic.label = row->label;
                topic.payload = id;
                topic.arg = row->offerId;
                topics_.push_back(std::move(topic));
            }
        }
    }

    // #82: shop talk moved into the tree's WORK branch -- see
    // buildWorkTopics(). It never appeared at the root after this point.

    // 6. Trade -- take the price on the board, or argue about it.
    if (speaker_.trades && speaker_.basePrice > 0) {
        Topic buy;
        buy.kind = TopicKind::Buy;
        buy.label = "BUY " + std::string(goodsName(speaker_.goods));
        buy.payload = speaker_.basePrice;
        topics_.push_back(std::move(buy));

        Topic topic;
        topic.kind = TopicKind::Trade;
        topic.label = "HAGGLE:" + std::string(goodsName(speaker_.goods));
        topic.payload = speaker_.basePrice;
        topics_.push_back(std::move(topic));
    }

    // 7. The two things that actually change how somebody feels about you.
    {
        Topic topic;
        topic.kind = TopicKind::BuyDrinkFor;
        topic.payload = kBoughtDrinkCost;
        topic.label = "BUY THEM A DRINK";
        topics_.push_back(std::move(topic));
    }
    if (speaker_.purse > 0) {
        Topic topic;
        topic.kind = TopicKind::PickPocket;
        topic.label = "PICK THEIR POCKET";
        topics_.push_back(std::move(topic));
    }

    // 8. S5 -- the two crimes that are a CONVERSATION rather than a hand.
    //
    // Leaning on somebody and selling them somebody else's property are the
    // only acts on the ward's list that happen across a table, which is why
    // they live here and the other four live in the room. Both are offered
    // without a rung, and both ANSWER differently once you have one: that is
    // the difference between a topic being gated and a topic being worth
    // choosing.
    if (speaker_.leanable && speaker_.purse > 0) {
        Topic topic;
        topic.kind = TopicKind::Lean;
        topic.label = "LEAN ON THEM";
        topics_.push_back(std::move(topic));
    }
    if (speaker_.buysStolen) {
        Topic topic;
        topic.kind = TopicKind::Fence;
        topic.label = "SELL WHAT YOU TOOK " + std::to_string(crimes_.loot());
        topics_.push_back(std::move(topic));
    }

    // 9. S5 -- what a TOP rung is actually for.
    //
    // Two tokens, two very different favours, and only the person who speaks
    // for that guild can be asked. This is the whole of what `lair` and
    // `warrant` buy, and it is the answer to the S4 review's complaint that
    // eleven of fourteen unlock tokens had no reader: they now have three
    // between them, and the ones that still do not are named out loud in
    // ranks.json rather than implied.
    if (!speaker_.recruitsFor.empty()) {
        const std::int32_t index = factions_->indexOf(speaker_.recruitsFor);
        if (standings_.unlocked(index, "lair")) {
            Topic topic;
            topic.kind = TopicKind::Favour;
            topic.label = "GO TO GROUND";
            topic.arg = speaker_.recruitsFor;
            topics_.push_back(std::move(topic));
        } else if (standings_.unlocked(index, "warrant")) {
            Topic topic;
            topic.kind = TopicKind::Favour;
            topic.label = "LOSE THE FILE";
            topic.arg = speaker_.recruitsFor;
            topics_.push_back(std::move(topic));
        }
    }

    // 9a2. S8 -- the man who put you on the floor. He is not a quest-giver and
    //      he has nothing to sell; the topic exists because he has BEATEN you,
    //      which is a fact about the world and not about a roll. It is the one
    //      row on this list that the player did not earn.
    if (speaker_.rivalWins > 0) {
        Topic topic;
        topic.kind = TopicKind::Rival;
        topic.label = "WHAT HE WANTS NOW";
        topic.payload = speaker_.rivalWins;
        topics_.push_back(std::move(topic));
    }

    // 9b. S6 -- the Flame's mark. DECISIONS.md: the Church "sanctions the
    //     redemption of a scalp". A priest offers it when you are carrying
    //     something that wants signing for and nothing else; a bounty that
    //     needed no conscience under it would not be this setting's bounty.
    if (speaker_.family == JobFamily::Clergy && wantsSanction()) {
        Topic topic;
        topic.kind = TopicKind::Sanction;
        topic.label = "ASK THE FLAME TO SIGN";
        topics_.push_back(std::move(topic));
    }

    {
        Topic topic;
        topic.kind = TopicKind::Leave;
        topic.label = "SAY NO MORE";
        topics_.push_back(std::move(topic));
    }
}

// ---------------------------------------------------------------------------
// #82: the "TELL ME ABOUT" tree
// ---------------------------------------------------------------------------

void DialogueDirector::pushBack() {
    Topic topic;
    topic.kind = TopicKind::Back;
    topic.label = "(BACK)";
    topics_.push_back(std::move(topic));
}

bool DialogueDirector::personAvailable() const {
    if (!barks_.resolve(toned(personalChain(speaker_.notableId))).empty()) {
        return true;
    }
    if (!notables_.tellableBy(speaker_.notableId).empty()) {
        return true;
    }
    return barks_.has("gossip");
}

bool DialogueDirector::locationAvailable() const {
    const TimeBand band = timeBandOf(secondOfDay_);
    for (const TopicIdLabel& entry : kLocationTopics) {
        const std::vector<std::string> chain =
            toned(topicChain("location", entry.id, speaker_.family, attitude_, band));
        if (!barks_.resolve(chain).empty()) {
            return true;
        }
    }
    return false;
}

bool DialogueDirector::thingAvailable() const {
    const TimeBand band = timeBandOf(secondOfDay_);
    for (const TopicIdLabel& entry : kThingTopics) {
        const std::vector<std::string> chain =
            toned(topicChain("thing", entry.id, speaker_.family, attitude_, band));
        if (!barks_.resolve(chain).empty()) {
            return true;
        }
    }
    return false;
}

bool DialogueDirector::workAvailable() const {
    return !barks_.resolve(toned(masteryChain(speaker_.skillId, speaker_.skillLevel))).empty();
}

std::vector<std::string> DialogueDirector::vanishedClerkChain() const {
    std::vector<std::string> chain;
    const std::string base = "quest." + std::string(kQuestId);
    if (!speaker_.notableId.empty()) {
        chain.push_back(base + ".rumor." + speaker_.notableId);
    }
    chain.push_back(base);
    return chain;
}

bool DialogueDirector::questAvailable() const {
    if (!barks_.resolve(vanishedClerkChain()).empty()) {
        return true;
    }
    // ...or an active stage this speaker is the party to, of the kind the
    // QUEST branch actually mirrors (see buildQuestTopics()).
    for (const Questline& line : quests_.lines()) {
        if (journal_.done(line.id)) {
            continue;
        }
        const std::int32_t at = journal_.stage(line.id);
        if (at < 0 || static_cast<std::size_t>(at) >= line.stages.size()) {
            continue;
        }
        const QuestStage& stage = line.stages[static_cast<std::size_t>(at)];
        if (speaker_.notableId.empty() || stage.party != speaker_.notableId) {
            continue;
        }
        if (stage.kind == StageKind::Talk || stage.kind == StageKind::Alms ||
            stage.kind == StageKind::Tally) {
            return true;
        }
    }
    return false;
}

void DialogueDirector::buildCategoryTopics() {
    topics_.clear();
    menu_ = DialogueMenu::Category;
    if (locationAvailable()) {
        Topic topic;
        topic.kind = TopicKind::Category;
        topic.label = "A PLACE";
        topic.arg = std::string(kAskLocation);
        topics_.push_back(std::move(topic));
    }
    if (personAvailable()) {
        Topic topic;
        topic.kind = TopicKind::Category;
        topic.label = "A PERSON";
        topic.arg = std::string(kAskPerson);
        topics_.push_back(std::move(topic));
    }
    if (thingAvailable()) {
        Topic topic;
        topic.kind = TopicKind::Category;
        topic.label = "A THING";
        topic.arg = std::string(kAskThing);
        topics_.push_back(std::move(topic));
    }
    if (workAvailable()) {
        Topic topic;
        topic.kind = TopicKind::Category;
        topic.label = "THEIR WORK";
        topic.arg = std::string(kAskWork);
        topics_.push_back(std::move(topic));
    }
    if (questAvailable()) {
        Topic topic;
        topic.kind = TopicKind::Category;
        topic.label = "A QUEST";
        topic.arg = std::string(kAskQuest);
        topics_.push_back(std::move(topic));
    }
    pushBack();
}

void DialogueDirector::buildLocationTopics() {
    topics_.clear();
    menu_ = DialogueMenu::Location;
    const TimeBand band = timeBandOf(secondOfDay_);
    std::int32_t index = 0;
    for (const TopicIdLabel& entry : kLocationTopics) {
        const std::vector<std::string> chain =
            toned(topicChain("location", entry.id, speaker_.family, attitude_, band));
        const std::string key(barks_.resolve(chain));
        if (!key.empty()) {
            Topic topic;
            topic.kind = TopicKind::Location;
            topic.label = std::string(entry.label);
            topic.barkKey = key;
            topic.arg = std::string(entry.id);
            topic.payload = index;
            topics_.push_back(std::move(topic));
        }
        ++index;
    }
    pushBack();
}

void DialogueDirector::buildThingTopics() {
    topics_.clear();
    menu_ = DialogueMenu::Thing;
    const TimeBand band = timeBandOf(secondOfDay_);
    std::int32_t index = 0;
    for (const TopicIdLabel& entry : kThingTopics) {
        const std::vector<std::string> chain =
            toned(topicChain("thing", entry.id, speaker_.family, attitude_, band));
        const std::string key(barks_.resolve(chain));
        if (!key.empty()) {
            Topic topic;
            topic.kind = TopicKind::Thing;
            topic.label = std::string(entry.label);
            topic.barkKey = key;
            topic.arg = std::string(entry.id);
            topic.payload = index;
            topics_.push_back(std::move(topic));
        }
        ++index;
    }
    pushBack();
}

void DialogueDirector::buildPersonTopics() {
    topics_.clear();
    menu_ = DialogueMenu::Person;

    // Their own business. #82: toned() lets a register colour WHICH TABLE
    // this comes out of -- the topic itself is offered to nearly anybody, so
    // this is content, not a gate.
    const std::string personalKey(barks_.resolve(toned(personalChain(speaker_.notableId))));
    if (!personalKey.empty()) {
        Topic topic;
        topic.kind = TopicKind::Personal;
        topic.label = "THEIR OWN BUSINESS";
        topic.barkKey = personalKey;
        topics_.push_back(std::move(topic));
    }

    // Every authored story this person is allowed to tell. Party first, then
    // whatever the rumor domains license -- and NOTHING else, which is the
    // social-topological gate the gazetteer requires. DELIBERATELY NOT
    // TONED: which micro-history somebody is licensed to repeat is a fact
    // about who they are, not about how nicely you asked.
    const std::vector<const History*> tellable = notables_.tellableBy(speaker_.notableId);
    for (const History* history : tellable) {
        if (history == nullptr) {
            continue;
        }
        const Notable* a = notables_.find(history->a);
        const Notable* b = notables_.find(history->b);
        if (a == nullptr || b == nullptr) {
            continue;
        }
        const std::string key(barks_.resolve(gossipChain(history->id)));
        if (key.empty() || key == "gossip") {
            // No table of its own means no story to tell. The generic ward
            // chatter is a separate topic and should not stand in for one.
            continue;
        }
        Topic topic;
        topic.kind = TopicKind::History;
        // The ward's own short names, because a topic list is a menu and
        // "ASK ABOUT SERGEANT VESS AND MASTER VENN" does not fit in a
        // column that has to leave the middle of the screen alone.
        topic.label = upper(a->id) + " AND " + upper(b->id);
        topic.barkKey = key;
        topic.payload = static_cast<std::int32_t>(topics_.size());
        topics_.push_back(std::move(topic));
    }

    // What the ward is saying.
    if (barks_.has("gossip")) {
        Topic topic;
        topic.kind = TopicKind::WardTalk;
        topic.label = "WHAT THE WARD SAYS";
        topic.barkKey = "gossip";
        topics_.push_back(std::move(topic));
    }

    pushBack();
}

void DialogueDirector::buildWorkTopics() {
    topics_.clear();
    menu_ = DialogueMenu::Work;

    // Shop talk, if they are good enough at anything to have any. #82: toned,
    // for the same reason THEIR OWN BUSINESS is -- how a master talks shop
    // with you is fair game for a register, whether the topic is offered at
    // all is not.
    const std::string key(
        barks_.resolve(toned(masteryChain(speaker_.skillId, speaker_.skillLevel))));
    if (!key.empty()) {
        const SkillTrack::Entry* entry = skills_.find(speaker_.skillId);
        Topic topic;
        topic.kind = TopicKind::Mastery;
        topic.label = upper(entry == nullptr ? speaker_.skillId : entry->displayName);
        topic.barkKey = key;
        topics_.push_back(std::move(topic));
    }

    pushBack();
}

void DialogueDirector::buildQuestTopics() {
    topics_.clear();
    menu_ = DialogueMenu::Quest;

    // THE VANISHED CLERK, mirrored from the root -- see the note on
    // buildTopics() itself for why this is a mirror and not a move.
    {
        const std::string key(barks_.resolve(vanishedClerkChain()));
        if (!key.empty()) {
            Topic topic;
            topic.kind = TopicKind::Quest;
            topic.label = "THE VANISHED CLERK";
            topic.barkKey = key;
            topics_.push_back(std::move(topic));
        }
    }

    // Any active Talk/Alms/Tally stage this speaker is the party to -- the
    // three StageKinds that are things you ASK rather than things you DO.
    // Oath/Teach/Forge stay exclusively at the root: signing on, being
    // taught and opening the bench are verbs, not conversation.
    for (const Questline& line : quests_.lines()) {
        if (journal_.done(line.id)) {
            continue;
        }
        const std::int32_t at = journal_.stage(line.id);
        if (at < 0 || static_cast<std::size_t>(at) >= line.stages.size()) {
            continue;
        }
        const QuestStage& stage = line.stages[static_cast<std::size_t>(at)];
        if (speaker_.notableId.empty() || stage.party != speaker_.notableId) {
            continue;
        }
        if (stage.kind != StageKind::Talk && stage.kind != StageKind::Alms &&
            stage.kind != StageKind::Tally) {
            continue;
        }
        Topic topic;
        topic.kind = TopicKind::QuestBeat;
        topic.payload = at;
        topic.arg = line.id;
        topic.barkKey = stage.barkKey;
        topic.label = stage.label;
        if (stage.kind == StageKind::Alms || stage.kind == StageKind::Tally) {
            // The count is on the label, so a player never has to guess how
            // much of a counted stage is behind them.
            topic.label += " (" + std::to_string(journal_.counter(line.id)) + "/" +
                           std::to_string(stage.count) + ")";
        }
        topics_.push_back(std::move(topic));
    }

    pushBack();
}

void DialogueDirector::rebuildCurrentLevel() {
    switch (menu_) {
        case DialogueMenu::Root:
            buildTopics();
            break;
        case DialogueMenu::Category:
            buildCategoryTopics();
            break;
        case DialogueMenu::Location:
            buildLocationTopics();
            break;
        case DialogueMenu::Person:
            buildPersonTopics();
            break;
        case DialogueMenu::Thing:
            buildThingTopics();
            break;
        case DialogueMenu::Work:
            buildWorkTopics();
            break;
        case DialogueMenu::Quest:
            buildQuestTopics();
            break;
    }
}

const ContractBroker* DialogueDirector::brokerFor(std::string_view notableId) const noexcept {
    if (notableId.empty() || contractRaws_ == nullptr) {
        return nullptr;
    }
    return contractRaws_->broker(notableId);
}

bool DialogueDirector::wantsSanction() const noexcept {
    // THE MARK IS FOR THE TAKING, NOT FOR THE SACK. A priest signs before the
    // knife rather than after it, which is the only reading that a player can
    // actually act on: Father Maell keeps an evening hour and the rats do not
    // come out until he has gone home. It is also the more honest reading of
    // DECISIONS.md -- the Church "sanctions the redemption of a scalp", and a
    // sanction obtained afterwards would be an absolution.
    for (const Contract& row : board_.contracts()) {
        if (row.live() && row.needsSanction()) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// choosing
// ---------------------------------------------------------------------------

Reply DialogueDirector::reply(TopicKind kind, std::string line) {
    Reply out;
    out.ok = true;
    out.kind = kind;
    out.line = std::move(line);
    out.dispositionAfter = ledger_.dispositionOf(speaker_.actorId);
    out.attitude = attitudeFor(out.dispositionAfter);
    attitude_ = out.attitude;
    if (!out.line.empty()) {
        lastLine_ = out.line;
    }
    return out;
}

Reply DialogueDirector::choose(std::size_t index) {
    Reply out;
    if (!open_ || index >= topics_.size()) {
        return out;
    }
    const Topic topic = topics_[index];
    const std::int32_t before = ledger_.dispositionOf(speaker_.actorId);

    switch (topic.kind) {
        case TopicKind::Ask: {
            // #82 -- the door into the tree. Speaks nothing: lastLine_ stays
            // whatever it already was, exactly like Leave and Buy already do.
            out = reply(TopicKind::Ask, {});
            buildCategoryTopics();
            break;
        }
        case TopicKind::Category: {
            out = reply(TopicKind::Category, {});
            if (topic.arg == kAskLocation) {
                buildLocationTopics();
            } else if (topic.arg == kAskPerson) {
                buildPersonTopics();
            } else if (topic.arg == kAskThing) {
                buildThingTopics();
            } else if (topic.arg == kAskWork) {
                buildWorkTopics();
            } else if (topic.arg == kAskQuest) {
                buildQuestTopics();
            } else {
                // An unknown branch id is a bug elsewhere, not a crash here:
                // stand still on the category list rather than guess.
                buildCategoryTopics();
            }
            break;
        }
        case TopicKind::Back: {
            out = reply(TopicKind::Back, {});
            // One level up. Category steps back to the root; any of the five
            // leaves step back to the category list -- there is nowhere else
            // a Back topic can ever be standing.
            if (menu_ == DialogueMenu::Category) {
                buildTopics();
            } else {
                buildCategoryTopics();
            }
            break;
        }
        case TopicKind::Personal:
        case TopicKind::WardTalk:
        case TopicKind::History:
        case TopicKind::Quest:
        case TopicKind::Mastery:
        case TopicKind::Location:
        case TopicKind::Thing: {
            std::string line(barks_.line(topic.barkKey, rowIndexFor(topic.kind, topic.payload)));
            if (line.empty()) {
                line = "...";
            }
            // Hearing somebody out is worth a little -- and #82's dial says
            // HOW you heard them out. A polite ear rides the exact same talk
            // ceiling Listened always did; a flat one does not, and can cost
            // you standing you already had -- see SocialLedger::record's own
            // note on why the three split that way.
            recordDeed(toneListenDeed());
            out = reply(topic.kind, std::move(line));
            break;
        }
        case TopicKind::Trade: {
            HaggleTerms terms;
            terms.basePrice = topic.payload;
            terms.attitude = ledger_.attitudeOf(speaker_.actorId);
            terms.playerSkill = skills_.level(kHaggleSkill);
            terms.merchantSkill = speaker_.haggleSkill;
            terms.goods = speaker_.goods;
            // The same guild term the counter applies with no conversation
            // open, so arguing about a price cannot accidentally be a way to
            // escape the ladder you are or are not on.
            terms.guildPercent = guildPricePercent(standings_, speakerFaction());
            haggle_.open(terms);
            out = reply(TopicKind::Trade, std::string(goodsName(speaker_.goods)) + " IS " +
                                              coins(haggle_.asking()) + ". NAME YOUR NUMBER.");
            out.haggling = true;
            break;
        }
        case TopicKind::BuyDrinkFor: {
            if (playerCoin_ < topic.payload) {
                out = reply(TopicKind::BuyDrinkFor, "YOUR PURSE WILL NOT COVER IT.");
                out.ok = false;
                break;
            }
            recordDeed(Deed::BoughtDrink);
            // The Mission's night pot is counted here and nowhere else: alms is
            // a deed, not a topic, so the stage fills up while you are being
            // sociable and is turned in later.
            noteAlmsGiven();
            out = reply(TopicKind::BuyDrinkFor, upper(speaker_.name) + " TAKES THE DRINK.");
            out.coinDelta = -topic.payload;
            break;
        }
        case TopicKind::PickPocket: {
            const std::int32_t craft = skills_.level(kThieverySkill);
            // Deterministic, and skill against skill: no roll, because a roll
            // here would consume a draw the twin-run gate has to account for.
            const bool caught = craft < speaker_.awareness;
            // The hands are charged for the attempt by noteCrime, which is now
            // the one place a criminal act pays for itself. Caught or not.
            if (caught) {
                recordDeed(Deed::Robbed);
                out = reply(TopicKind::PickPocket,
                            upper(speaker_.name) + " CATCHES YOUR WRIST.");
                out.offence = true;
                out.closes = true;
                out.crime = Crime::Lift;
                out.criminal = true;
                close();
            } else {
                const std::int32_t lifted =
                    std::min(speaker_.purse, 1 + craft / 8 + speaker_.purse / 4);
                speaker_.purse -= lifted;
                // A purse carries something that is not coin, and that
                // something is what a fence is for.
                crimes_.takeLoot(1);
                out = reply(TopicKind::PickPocket,
                            "LIFTED " + coins(lifted) + " AND A TRINKET.");
                out.coinDelta = lifted;
                out.crime = Crime::Lift;
                out.criminal = true;
            }
            break;
        }
        case TopicKind::Buy: {
            out = reply(TopicKind::Buy, {});
            out.wantsPurchase = true;
            break;
        }
        case TopicKind::Fence: {
            const std::int32_t index = roofsIndex();
            if (!standings_.unlocked(index, "fence")) {
                // A cutpurse is not a fence. Refused in the roofs' own authored
                // voice, through the same chain every guild verb already uses.
                out = reply(TopicKind::Fence,
                            speak(factionChain("skyrunners", "blocked"), TopicKind::Fence, 0));
                if (out.line.empty()) {
                    out.line = "NOT TO YOU.";
                }
                out.ok = false;
                break;
            }
            if (crimes_.loot() <= 0) {
                out = reply(TopicKind::Fence, "YOU HAVE NOTHING TO SELL.");
                out.ok = false;
                break;
            }
            const std::int32_t rate =
                fenceRatePercent(standings_.rank(index), standings_.standing(index));
            const std::int32_t pieces = crimes_.loot();
            const std::int32_t paid = crimes_.sellLoot(pieces, rate);
            out = reply(TopicKind::Fence,
                        speak({std::string("crime.fence")}, TopicKind::Fence, rate));
            if (out.line.empty()) {
                out.line = "TAKEN.";
            }
            // PARENTHESES, NOT BRACKETS, HERE AND EVERYWHERE BELOW. The 4x6
            // font has glyphs for '(' and ')' and none for '[' and ']' -- see
            // the table in hud.cpp, which lists exactly the characters the
            // authored barks use. Every receipt in this file was framed in
            // brackets, so every one of them drew two holes in a line the
            // player reads on the same keypress that takes their money.
            out.line += " (" + std::to_string(pieces) + " AT " + std::to_string(rate) +
                        " PERCENT - " + coins(paid) + ")";
            out.coinDelta = paid;
            out.crime = Crime::Fence;
            out.criminal = true;
            break;
        }
        case TopicKind::TakeContract: {
            const ContractBroker* broker = brokerFor(speaker_.notableId);
            if (broker == nullptr) {
                out = reply(TopicKind::TakeContract, "NO WORK HERE.");
                out.ok = false;
                break;
            }
            if (topic.payload < 0 || !brokerWillTalk(*broker)) {
                // Asked, and brushed off. In the broker's own authored voice --
                // the topic exists so that being refused is a thing that
                // HAPPENS rather than a thing that is hidden.
                out = reply(TopicKind::TakeContract,
                            speak({"contract.blocked." + broker->id, "contract.blocked"},
                                  TopicKind::TakeContract, 0));
                if (out.line.empty()) {
                    out.line = "NOTHING FOR YOU.";
                }
                out.ok = false;
                break;
            }
            const TakeResult took = board_.take(topic.payload);
            const Contract* row = board_.find(topic.payload);
            if (took != TakeResult::Taken || row == nullptr) {
                out = reply(TopicKind::TakeContract,
                            speak({"contract.short"}, TopicKind::TakeContract, 0));
                if (out.line.empty() || took == TakeResult::HandsFull) {
                    out.line = "YOU ARE CARRYING ENOUGH ALREADY.";
                }
                out.ok = false;
                break;
            }
            out = reply(TopicKind::TakeContract,
                        speak({"contract.take." + broker->id, "contract.take"},
                              TopicKind::TakeContract, topic.payload));
            if (out.line.empty()) {
                out.line = "TAKEN.";
            }
            // The brief is the JOB, in the ward's own words, and it goes where
            // a questline's log line goes so the HUD and the journal need to
            // know nothing about contracts to show it.
            out.journalLine = row->brief;
            out.contractId = row->id;
            break;
        }
        case TopicKind::TurnIn: {
            const ContractBroker* broker = brokerFor(speaker_.notableId);
            const Contract* row = board_.find(topic.payload);
            if (broker == nullptr || row == nullptr) {
                out = reply(TopicKind::TurnIn, "NOT MY BUSINESS.");
                out.ok = false;
                break;
            }
            const std::int32_t pay = row->pay;
            const Settlement settled = board_.turnIn(topic.payload, crimes_.stash(), board_.day());
            out.contractId = topic.payload;
            if (settled.result != TurnInResult::Paid) {
                const char* chain = settled.result == TurnInResult::Late ? "contract.late"
                                                                        : "contract.short";
                out = reply(TopicKind::TurnIn,
                            speak({std::string(chain)}, TopicKind::TurnIn, topic.payload));
                if (out.line.empty()) {
                    out.line = "NOT THE NUMBER.";
                }
                if (settled.result == TurnInResult::NeedsSanction) {
                    // Named out loud, because a player who cannot find the
                    // reason will assume the mechanic is broken.
                    out.line = "THE FLAME HAS NOT SIGNED FOR THESE.";
                }
                out.ok = false;
                out.contractId = topic.payload;
                break;
            }
            // Every unit of it goes through the same hands that carried it, and
            // the skill those hands use is the good's own -- fieldcraft for a
            // knife, mixtures for a jar, cracksmanship for somebody else's
            // plate. The Morrowind steer, applied to a trade.
            skills_.use(contrabandSkill(row->good), settled.unitsTaken);
            out = reply(TopicKind::TurnIn,
                        speak({"contract.paid." + broker->id, "contract.paid"},
                              TopicKind::TurnIn, topic.payload));
            if (out.line.empty()) {
                out.line = "COUNTED.";
            }
            out.line += " (" + coins(settled.pay) + ")";
            out.coinDelta = settled.pay;
            out.contractId = topic.payload;
            (void)pay;
            // Delivering work for a guild is a deed done to that guild, and it
            // is the ONLY way a contract moves standing -- through exactly the
            // faction ledger a bought drink moves.
            const std::int32_t guild = factions_->indexOf(broker->faction);
            if (guild >= 0) {
                standings_.addStanding(guild, 2 + settled.unitsTaken / 2);
            }
            break;
        }
        case TopicKind::Sanction: {
            const std::int32_t marked = board_.sanction();
            out = reply(TopicKind::Sanction,
                        speak({marked > 0 ? "contract.sanction" : "contract.sanction.refused"},
                              TopicKind::Sanction, marked));
            if (out.line.empty()) {
                out.line = marked > 0 ? "MARKED." : "NOTHING TO WEIGH.";
            }
            out.ok = marked > 0;
            // The Mission hears about it. A priest who signs for blood money is
            // a priest who has been given a reason to remember you.
            if (marked > 0) {
                const std::int32_t temple = factions_->indexOf("temple");
                if (temple >= 0) {
                    standings_.addStanding(temple, 1);
                }
            }
            break;
        }
        case TopicKind::Rival: {
            // HE ANSWERS OUT OF THE OWNER'S OWN FILE, and which table depends
            // on what he has become since: a man who founded a house over your
            // body says a different thing from a man who has beaten you once.
            // The chain is most specific first, exactly like every other one.
            std::vector<std::string> chain;
            if (!speaker_.rivalHouse.empty()) {
                chain.push_back("nemesis.topic.house");
            }
            if (speaker_.rivalWins > 1) {
                chain.push_back("nemesis.topic.again");
            }
            chain.push_back("nemesis.topic");
            out = reply(TopicKind::Rival, speak(chain, TopicKind::Rival, speaker_.rivalWins));
            if (out.line.empty()) {
                out.line = "HE SAYS NOTHING. HE DOES NOT HAVE TO.";
            }
            // Talking to him does not make it better. A conversation is not an
            // apology and this one costs a point of what little is left.
            ledger_.record(speaker_.actorId, Deed::Spoke);
            break;
        }
        case TopicKind::Favour: {
            const std::int32_t index = factions_->indexOf(topic.arg);
            if (standings_.unlocked(index, "lair")) {
                // A brotherhood with a roost has somewhere to be for a week.
                // Heat to nothing and the paper with it.
                crimes_.lieLow();
                out = reply(TopicKind::Favour,
                            speak({std::string("crime.ground")}, TopicKind::Favour, index));
                if (out.line.empty()) {
                    out.line = "GONE TO GROUND.";
                }
            } else if (standings_.unlocked(index, "warrant")) {
                // A sergeant can lose his own file. What the ward SAW, it still
                // saw -- the heat stays exactly where it was.
                crimes_.quashWarrant();
                out = reply(TopicKind::Favour,
                            speak({std::string("crime.file")}, TopicKind::Favour, index));
                if (out.line.empty()) {
                    out.line = "THE FILE IS LOST.";
                }
            } else {
                out = reply(TopicKind::Favour,
                            speak(factionChain(topic.arg, "blocked"), TopicKind::Favour, index));
                if (out.line.empty()) {
                    out.line = "NOT SOMETHING YOU CAN ASK FOR.";
                }
                out.ok = false;
            }
            break;
        }
        case TopicKind::Lean: {
            // Deterministic, and NOT a roll: what leans on somebody is what
            // they know about you. The rung you hold on the roofs, what the
            // ward has heard, how hot you are and whether there is paper out on
            // your name -- against their own nerve, which is their streetwise.
            const std::int32_t index = roofsIndex();
            const std::int32_t nerve = speaker_.haggleSkill;
            const std::int32_t menace = standings_.rank(index) * 7 +
                                        std::max(0, -ledger_.reputation()) / 2 +
                                        crimes_.heat() / 8 + (crimes_.warrant() ? 6 : 0);
            recordDeed(Deed::Robbed);
            if (menace <= nerve) {
                out = reply(TopicKind::Lean,
                            upper(speaker_.name) + " LOOKS AT YOU AND WAITS. (" +
                                std::to_string(menace) + " AGAINST " + std::to_string(nerve) +
                                ")");
                out.offence = true;
                out.ok = false;
                out.crime = Crime::Extort;
                out.criminal = true;
                break;
            }
            const std::int32_t paid = 1 + speaker_.purse / 3;
            speaker_.purse -= paid;
            out = reply(TopicKind::Lean,
                        speak({std::string("crime.lean")}, TopicKind::Lean, menace));
            if (out.line.empty()) {
                out.line = "THEY PAY.";
            }
            out.line += " (" + coins(paid) + ")";
            out.coinDelta = paid;
            out.offence = true;
            out.crime = Crime::Extort;
            out.criminal = true;
            break;
        }
        case TopicKind::Leave: {
            out = reply(TopicKind::Leave, {});
            out.closes = true;
            close();
            break;
        }
        case TopicKind::Join: {
            // The oath and the sign-on are the same verb. When a questline owns
            // this beat, the line's own stage does the work -- standing, rung
            // and journal together -- and the plain path below is what every
            // guild without a written line uses.
            if (const Questline* line = quests_.find(topic.arg); line != nullptr) {
                out = completeStage(*line, topic.payload, TopicKind::Join);
                break;
            }
            const std::int32_t index = factions_->indexOf(topic.arg);
            const LadderResult verdict = standings_.join(index, skills_);
            const bool granted = verdict == LadderResult::Granted;
            out = reply(TopicKind::Join,
                        speak(factionChain(topic.arg, granted ? "join" : "blocked"),
                              TopicKind::Join, index));
            if (out.line.empty()) {
                out.line = upper(std::string(ladderResultName(verdict)));
            }
            out.ranked = granted;
            out.ok = granted;
            break;
        }
        case TopicKind::Advance: {
            const std::int32_t index = factions_->indexOf(topic.arg);
            const LadderResult verdict = standings_.advance(index, skills_);
            const bool granted = verdict == LadderResult::Granted;
            out = reply(TopicKind::Advance,
                        speak(factionChain(topic.arg, granted ? "rank" : "blocked"),
                              TopicKind::Advance, index));
            if (out.line.empty()) {
                out.line = upper(std::string(ladderResultName(verdict)));
            }
            out.ranked = granted;
            out.ok = granted;
            break;
        }
        case TopicKind::QuestBeat: {
            const Questline* line = quests_.find(topic.arg);
            if (line == nullptr || topic.payload < 0 ||
                static_cast<std::size_t>(topic.payload) >= line->stages.size()) {
                out = reply(TopicKind::QuestBeat, "...");
                out.ok = false;
                break;
            }
            const QuestStage& stage = line->stages[static_cast<std::size_t>(topic.payload)];
            const bool counted =
                stage.kind == StageKind::Alms || stage.kind == StageKind::Tally;
            if (counted && journal_.counter(line->id) < stage.count) {
                // Not enough of it done. He says the task again rather than
                // saying nothing, which is what a quest marker cannot do.
                std::string spoken(
                    barks_.line(stage.barkKey, rowIndexFor(TopicKind::QuestBeat, topic.payload)));
                if (spoken.empty()) {
                    spoken = "...";
                }
                out = reply(TopicKind::QuestBeat, std::move(spoken));
                out.ok = false;
                break;
            }
            out = completeStage(*line, topic.payload, TopicKind::QuestBeat);
            break;
        }
        case TopicKind::Learn: {
            const std::int32_t index = speakerFaction();
            if (!standings_.unlocked(index, "teaching")) {
                out = reply(TopicKind::Learn, speak(factionChain(speaker_.factionId, "blocked"),
                                                    TopicKind::Learn, 0));
                if (out.line.empty()) {
                    out.line = "NOT YET.";
                }
                out.ok = false;
                break;
            }
            // WHAT he teaches comes out of the raws and nowhere else, gated by
            // the authored minLevel -- the literacy tier of L2472, not a roll.
            const std::int32_t level = skills_.level(kCraftingSkill);
            const Spell* pick = nullptr;
            for (const Spell* candidate : spellbook_.teachableAt(kCraftingSkill, level)) {
                if (candidate != nullptr && !grimoire_.knows(candidate->id)) {
                    pick = candidate;
                    break;
                }
            }
            if (pick == nullptr) {
                out = reply(TopicKind::Learn,
                            speak({std::string("teaching.beyond")}, TopicKind::Learn, level));
                if (out.line.empty()) {
                    out.line = "NOTHING MORE TONIGHT.";
                }
                out.ok = false;
                break;
            }
            grimoire_.learn(*pick);
            skills_.use(kCraftingSkill, 2);
            if (const Questline* line = quests_.find(topic.arg); line != nullptr) {
                out = completeStage(*line, topic.payload, TopicKind::Learn);
            } else {
                out = reply(TopicKind::Learn,
                            speak({std::string("teaching.taken")}, TopicKind::Learn, level));
            }
            if (out.line.empty()) {
                out.line = "LEARNED.";
            }
            // The authored voice, then a note of WHAT was handed over. The
            // parentheses are menu furniture and read as one.
            out.line += " (" + upper(pick->displayName) + ")";
            break;
        }
        case TopicKind::Forge: {
            const std::int32_t index = speakerFaction();
            if (!standings_.unlocked(index, "forge")) {
                out = reply(TopicKind::Forge, speak(factionChain(speaker_.factionId, "blocked"),
                                                    TopicKind::Forge, 0));
                if (out.line.empty()) {
                    out.line = std::string(forgeErrorReason(ForgeError::BeyondRank));
                }
                out.ok = false;
                break;
            }
            bench_.reset();
            bench_.active = true;
            forgeQuestId_ = topic.arg;
            forgeStage_ = topic.payload;
            out = reply(TopicKind::Forge, speak({std::string("forge.opened")}, TopicKind::Forge, 0));
            if (out.line.empty()) {
                out.line = "COMPOSE.";
            }
            out.forging = true;
            break;
        }
    }

    // The list the player is looking at has just changed: a rung climbed
    // removes the topic that climbed it and a stage finished offers the next
    // one. Rebuilt for exactly the kinds that can move it, and never after a
    // Leave, which has already closed the conversation. #82: rebuilds
    // whichever level menu_ names rather than always the root -- QuestBeat is
    // reachable both at the root and from the tree's QUEST branch (see the
    // note on buildTopics()), and each has to refresh the list it is
    // ACTUALLY showing, not the one it always used to be the only one of.
    // Ask/Category/Back already rebuilt the level they moved to, above, so
    // they are deliberately not repeated here.
    switch (topic.kind) {
        case TopicKind::Join:
        case TopicKind::Advance:
        case TopicKind::QuestBeat:
        case TopicKind::Learn:
        case TopicKind::Forge:
        case TopicKind::Fence:
        case TopicKind::Lean:
        case TopicKind::Favour:
        // S6: taking a job moves it off the offered list and onto the waiting
        // one, handing one in takes it off both, and a priest's mark changes
        // whether a waiting one can be paid at all. All three change what the
        // list should say, so all three rebuild it.
        case TopicKind::TakeContract:
        case TopicKind::TurnIn:
        case TopicKind::Sanction:
            if (open_) {
                rebuildCurrentLevel();
            }
            break;
        default:
            break;
    }

    out.dispositionBefore = before;
    return out;
}

// ---------------------------------------------------------------------------
// questlines
// ---------------------------------------------------------------------------

void DialogueDirector::noteAlmsGiven() {
    noteTally("drinks");
}

void DialogueDirector::noteTally(std::string_view counter) {
    if (counter.empty()) {
        return;
    }
    for (const Questline& line : quests_.lines()) {
        if (!journal_.started(line.id) || journal_.done(line.id)) {
            continue;
        }
        const std::int32_t at = journal_.stage(line.id);
        if (at < 0 || static_cast<std::size_t>(at) >= line.stages.size()) {
            continue;
        }
        const QuestStage& stage = line.stages[static_cast<std::size_t>(at)];
        if (stage.kind != StageKind::Alms && stage.kind != StageKind::Tally) {
            continue;
        }
        if (stage.counter != counter) {
            continue;
        }
        journal_.bumpCounter(line.id, 1);
    }
}

std::int32_t DialogueDirector::roofsIndex() const noexcept {
    return factions_->indexOf("skyrunners");
}

void DialogueDirector::noteCrime(Crime crime, bool witnessed) {
    crimes_.commit(crime, witnessed);
    noteTally(crimeTally(crime));
    // The roofs warm to it and, through the mirror ranks.json declares, the
    // garrison cools by half. This is the only thing in the build that MOVES a
    // faction number without a conversation, which is what makes the mirror
    // something the player can feel rather than a note in a JSON file.
    standings_.addStanding(roofsIndex(), crimeStanding(crime));
    const std::string_view skill = crimeSkill(crime);
    if (!skill.empty()) {
        skills_.use(skill, witnessed ? 1 : 2);
    }
}

Reply DialogueDirector::completeStage(const Questline& line, std::int32_t stageIndex,
                                      TopicKind kind) {
    if (stageIndex < 0 || static_cast<std::size_t>(stageIndex) >= line.stages.size()) {
        Reply out = reply(kind, "...");
        out.ok = false;
        return out;
    }
    const QuestStage& stage = line.stages[static_cast<std::size_t>(stageIndex)];
    const std::int32_t index = factions_->indexOf(line.faction);

    // Standing FIRST, then the rung. A line that hands you a rung is handing
    // you what the standing it just paid you has bought -- so the ladder's own
    // requirement is still what decides it, and the oath is refused out loud if
    // the ladder says no.
    if (stage.standing != 0) {
        standings_.addStanding(index, stage.standing);
    }
    bool ranked = false;
    if (stage.grantsRank > 0) {
        const LadderResult verdict = standings_.isMember(index) ? standings_.advance(index, skills_)
                                                                : standings_.join(index, skills_);
        if (verdict != LadderResult::Granted && verdict != LadderResult::AlreadyThere) {
            Reply refused =
                reply(kind, speak(factionChain(line.faction, "blocked"), kind, stageIndex));
            if (refused.line.empty()) {
                refused.line = upper(std::string(ladderResultName(verdict)));
            }
            refused.ok = false;
            return refused;
        }
        ranked = verdict == LadderResult::Granted;
    }

    std::string spoken(barks_.line(stage.barkKey, rowIndexFor(kind, stageIndex)));
    if (spoken.empty()) {
        spoken = "...";
    }
    journal_.start(line.id);
    journal_.advance(line);
    Reply out = reply(kind, std::move(spoken));
    out.ranked = ranked;
    out.journalLine = stage.log;
    return out;
}

// ---------------------------------------------------------------------------
// the workbench
// ---------------------------------------------------------------------------

void DialogueDirector::moveForgeField(std::int32_t delta) {
    if (open_ && bench_.active) {
        bench_.moveField(delta);
    }
}

void DialogueDirector::adjustForge(std::int32_t delta) {
    if (open_ && bench_.active) {
        bench_.adjust(delta);
    }
}

Reply DialogueDirector::commitForge() {
    if (!open_ || !bench_.active) {
        return Reply{};
    }
    const std::int32_t before = ledger_.dispositionOf(speaker_.actorId);
    const ForgeResult result = forgeSpell(bench_.request(), skills_.level(kCraftingSkill));
    if (!result.ok) {
        // Refused OUT LOUD, in the priest's own authored voice -- and the bench
        // stays open, so a refusal is a lesson rather than a dead end.
        //
        // WHAT USED TO BE HERE was the ForgeError's own identifier, upper-cased
        // and bracketed onto the end: "HEAT AND TUNING ARE HELD, NOT DELIVERED.
        // [HELD AXIS NEEDS A HOLD]". That is the same sentence twice, once as
        // the priest says it and once as the enum spells it, and the second
        // copy is the machine's name for the fault rather than anybody's word
        // for it. forgeErrorReason is the player's answer and is authored for
        // all thirteen refusals; forgeErrorName stays where it belongs, in
        // diagnostics and in the cases that assert on it.
        Reply out = reply(TopicKind::Forge,
                          speak({std::string("forge.refused")}, TopicKind::Forge,
                                static_cast<std::int32_t>(result.error)));
        if (out.line.empty()) {
            out.line = std::string(forgeErrorReason(result.error));
        }
        out.ok = false;
        out.forging = true;
        out.dispositionBefore = before;
        return out;
    }
    grimoire_.inscribe(result.spell);
    skills_.use(kCraftingSkill, 3);
    const std::string questId = forgeQuestId_;
    const std::int32_t stageIndex = forgeStage_;
    bench_.reset();
    forgeQuestId_.clear();

    Reply out;
    if (const Questline* line = quests_.find(questId); line != nullptr) {
        out = completeStage(*line, stageIndex, TopicKind::Forge);
    } else {
        out = reply(TopicKind::Forge,
                    speak({std::string("forge.struck")}, TopicKind::Forge, result.difficulty));
    }
    if (out.line.empty()) {
        out.line = "MADE.";
    }
    out.line += " (COST " + std::to_string(result.difficulty) + ")";
    out.dispositionBefore = before;
    if (open_) {
        buildTopics();
    }
    return out;
}

Reply DialogueDirector::endForge() {
    if (!open_ || !bench_.active) {
        return Reply{};
    }
    bench_.reset();
    forgeQuestId_.clear();
    return reply(TopicKind::Forge, "TOOLS DOWN.");
}

// ---------------------------------------------------------------------------
// haggling
// ---------------------------------------------------------------------------

Reply DialogueDirector::settleHaggle(HaggleOutcome outcome) {
    const std::int32_t before = ledger_.dispositionOf(speaker_.actorId);
    Reply out;
    switch (outcome) {
        case HaggleOutcome::Idle:
            return out;
        case HaggleOutcome::Struck: {
            recordDeed(haggle_.lastDeed());
            if (haggle_.lastSkillEffort() > 0) {
                skills_.use(kHaggleSkill, haggle_.lastSkillEffort());
            }
            out = reply(TopicKind::Trade, "DONE - " + coins(haggle_.settledPrice()) + ".");
            out.coinDelta = -haggle_.settledPrice();
            break;
        }
        case HaggleOutcome::Countered:
            out = reply(TopicKind::Trade, "CALL IT " + coins(haggle_.asking()) + ".");
            out.haggling = true;
            break;
        case HaggleOutcome::Insulted:
            recordDeed(Deed::Lowballed);
            out = reply(TopicKind::Trade, "THAT IS NOT AN OFFER. " + coins(haggle_.asking()) + ".");
            out.haggling = true;
            break;
        case HaggleOutcome::Refused:
            recordDeed(haggle_.lastDeed());
            out = reply(TopicKind::Trade, "WE ARE DONE TALKING PRICE.");
            break;
        case HaggleOutcome::WalkedOut:
            recordDeed(Deed::WalkedOut);
            out = reply(TopicKind::Trade, "SUIT YOURSELF.");
            break;
        case HaggleOutcome::Open:
            out = reply(TopicKind::Trade, coins(haggle_.asking()) + ".");
            out.haggling = true;
            break;
    }
    out.dispositionBefore = before;
    return out;
}

Reply DialogueDirector::offerPrice(std::int32_t coinsOffered) {
    if (!open_ || !haggle_.active()) {
        return Reply{};
    }
    return settleHaggle(haggle_.offer(coinsOffered));
}

Reply DialogueDirector::takeAsking() {
    if (!open_ || !haggle_.active()) {
        return Reply{};
    }
    return settleHaggle(haggle_.takeAsking());
}

Reply DialogueDirector::endHaggle() {
    if (!open_ || !haggle_.active()) {
        return Reply{};
    }
    return settleHaggle(haggle_.walkAway());
}

// ---------------------------------------------------------------------------
// hashing
// ---------------------------------------------------------------------------

void DialogueDirector::hashInto(HashSink& sink) const {
    ledger_.hashInto(sink);
    skills_.hashInto(sink);
    haggle_.hashInto(sink);
    // S4: membership, rank, every faction's standing and its influence over the
    // ward; where every authored line has got to; what the player has been
    // taught and what they composed; and a half-finished composition on the
    // bench. All of it is simulation state, so all of it is in the hash -- a
    // guild the twin-run gate could not see would be a guild the gate does not
    // protect, and influence reaches out and changes prices.
    standings_.hashInto(sink);
    journal_.hashInto(sink);
    grimoire_.hashInto(sink);
    // S5. What the player has taken, what they are carrying and what the Watch
    // has heard. Heat reaches out and changes how the ward behaves, so two runs
    // that disagreed about it would be two different games.
    crimes_.hashInto(sink);
    // S6. Tonight's work, what has been taken off it, and what it has paid.
    // A board is regenerable from (day, seed, standings) and WHICH JOBS WERE
    // TAKEN is not, so it is state and it is hashed.
    board_.hashInto(sink);
    bench_.hashInto(sink);
    sink.put_byte(open_ ? 1U : 0U);
    sink.put_int(static_cast<std::uint32_t>(speaker_.actorId));
    sink.put_int(static_cast<std::uint32_t>(speaker_.purse));
    sink.put_int(static_cast<std::uint32_t>(conversations_));
    sink.put_int(static_cast<std::uint32_t>(talkIndex_));
    sink.put_byte(static_cast<std::uint32_t>(attitude_));
    // #82. The dialled register is an INPUT, like a topic index or a haggle
    // offer, and it moves what gets said and what gets shown -- a twin run
    // that disagreed about it after an identical scripted setTone() would be
    // a twin run the gate ought to catch, so it goes in the hash the same as
    // every other choice a run makes.
    sink.put_byte(static_cast<std::uint32_t>(tone_));
    // #82. Which level of the TELL ME ABOUT tree is on screen. Fully derived
    // from the choose() sequence already hashed above, but so is topics_.size()
    // right below it, and the same reasoning applies: a twin run that agreed
    // on every choice made and still disagreed about which list it was
    // looking at is exactly the class of bug this hash exists to catch.
    sink.put_byte(static_cast<std::uint32_t>(menu_));
    sink.put_int(static_cast<std::uint32_t>(topics_.size()));
}

}  // namespace granadad::sim
