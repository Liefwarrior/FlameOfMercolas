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
    return out;
}

std::int32_t DialogueDirector::speakerFaction() const noexcept {
    return factions_->indexOf(speaker_.factionId);
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
    const std::vector<std::string> greetKeys =
        greetChain(speaker_.family, attitude_, timeBandOf(secondOfDay));
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
}

std::int32_t DialogueDirector::rowIndexFor(TopicKind kind, std::int32_t payload) const noexcept {
    // Deterministic and draw-free: who, how many times, which topic. Small
    // co-prime multipliers so two topics in one conversation do not land on the
    // same row of two different tables in lockstep.
    return talkIndex_ * 7 + static_cast<std::int32_t>(kind) * 3 + payload * 5 + speaker_.actorId;
}

std::string DialogueDirector::speak(const std::vector<std::string>& chain, TopicKind kind,
                                    std::int32_t payload) {
    const std::string key(barks_.resolve(chain));
    if (key.empty()) {
        return {};
    }
    return std::string(barks_.line(key, rowIndexFor(kind, payload)));
}

void DialogueDirector::buildTopics() {
    topics_.clear();

    // 1. Their own business.
    const std::vector<std::string> personal = personalChain(speaker_.notableId);
    if (!barks_.resolve(personal).empty()) {
        Topic topic;
        topic.kind = TopicKind::Personal;
        topic.label = "THEIR BUSINESS";
        topic.barkKey = std::string(barks_.resolve(personal));
        topics_.push_back(std::move(topic));
    }

    // 2. Every authored story this person is allowed to tell. Party first, then
    //    whatever the rumor domains license -- and NOTHING else, which is the
    //    social-topological gate the gazetteer requires.
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

    // 3. What the ward is saying.
    if (barks_.has("gossip")) {
        Topic topic;
        topic.kind = TopicKind::WardTalk;
        topic.label = "THE WARD";
        topic.barkKey = "gossip";
        topics_.push_back(std::move(topic));
    }

    // 4. The vanished clerk. Everybody has heard.
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

    // 5. Shop talk, if they are good enough at anything to have any.
    {
        const std::string key(
            barks_.resolve(masteryChain(speaker_.skillId, speaker_.skillLevel)));
        if (!key.empty()) {
            const SkillTrack::Entry* entry = skills_.find(speaker_.skillId);
            Topic topic;
            topic.kind = TopicKind::Mastery;
            topic.label = upper(entry == nullptr ? speaker_.skillId : entry->displayName);
            topic.barkKey = key;
            topics_.push_back(std::move(topic));
        }
    }

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

    {
        Topic topic;
        topic.kind = TopicKind::Leave;
        topic.label = "SAY NO MORE";
        topics_.push_back(std::move(topic));
    }
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
        case TopicKind::Personal:
        case TopicKind::WardTalk:
        case TopicKind::History:
        case TopicKind::Quest:
        case TopicKind::Mastery: {
            std::string line(barks_.line(topic.barkKey, rowIndexFor(topic.kind, topic.payload)));
            if (line.empty()) {
                line = "...";
            }
            // Hearing somebody out is worth a little. It is the only way to
            // raise a standing that costs nothing, and it is nearly nothing.
            recordDeed(Deed::Listened);
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
            // Every attempt teaches the hands something, caught or not.
            skills_.use(kThieverySkill, caught ? 1 : 2);
            if (caught) {
                recordDeed(Deed::Robbed);
                out = reply(TopicKind::PickPocket,
                            upper(speaker_.name) + " CATCHES YOUR WRIST.");
                out.offence = true;
                out.closes = true;
                close();
            } else {
                const std::int32_t lifted =
                    std::min(speaker_.purse, 1 + craft / 8 + speaker_.purse / 4);
                speaker_.purse -= lifted;
                out = reply(TopicKind::PickPocket, "LIFTED " + coins(lifted) + ".");
                out.coinDelta = lifted;
            }
            break;
        }
        case TopicKind::Buy: {
            out = reply(TopicKind::Buy, {});
            out.wantsPurchase = true;
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
            if (stage.kind == StageKind::Alms && journal_.counter(line->id) < stage.count) {
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
            // The authored voice, then a bracketed note of WHAT was handed
            // over. The bracket is menu furniture and reads as one.
            out.line += " [" + upper(pick->displayName) + "]";
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
    // Leave, which has already closed the conversation.
    switch (topic.kind) {
        case TopicKind::Join:
        case TopicKind::Advance:
        case TopicKind::QuestBeat:
        case TopicKind::Learn:
        case TopicKind::Forge:
            if (open_) {
                buildTopics();
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
    for (const Questline& line : quests_.lines()) {
        if (!journal_.started(line.id) || journal_.done(line.id)) {
            continue;
        }
        const std::int32_t at = journal_.stage(line.id);
        if (at < 0 || static_cast<std::size_t>(at) >= line.stages.size()) {
            continue;
        }
        if (line.stages[static_cast<std::size_t>(at)].kind == StageKind::Alms) {
            journal_.bumpCounter(line.id, 1);
        }
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
        // Refused OUT LOUD, in the priest's own authored voice, with the
        // machine reason bracketed after it -- and the bench stays open, so a
        // refusal is a lesson rather than a dead end.
        Reply out = reply(TopicKind::Forge,
                          speak({std::string("forge.refused")}, TopicKind::Forge,
                                static_cast<std::int32_t>(result.error)));
        if (out.line.empty()) {
            out.line = std::string(forgeErrorReason(result.error));
        }
        out.line += " [" + upper(std::string(forgeErrorName(result.error))) + "]";
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
    out.line += " [COST " + std::to_string(result.difficulty) + "]";
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
    bench_.hashInto(sink);
    sink.put_byte(open_ ? 1U : 0U);
    sink.put_int(static_cast<std::uint32_t>(speaker_.actorId));
    sink.put_int(static_cast<std::uint32_t>(speaker_.purse));
    sink.put_int(static_cast<std::uint32_t>(conversations_));
    sink.put_int(static_cast<std::uint32_t>(talkIndex_));
    sink.put_byte(static_cast<std::uint32_t>(attitude_));
    sink.put_int(static_cast<std::uint32_t>(topics_.size()));
}

}  // namespace granadad::sim
