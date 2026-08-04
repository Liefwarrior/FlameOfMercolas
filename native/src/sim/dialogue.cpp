#include "granadad/sim/dialogue.hpp"

#include <algorithm>

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
    }
    return "?";
}

DialogueDirector DialogueDirector::load(const std::filesystem::path& contentDir) {
    DialogueDirector out;
    out.barks_ = BarkTables::load(contentDir);
    out.notables_ = NotableRegistry::load(contentDir);
    out.skills_ = SkillTrack::load(contentDir);
    return out;
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
    ledger_.record(speaker_.actorId, Deed::Spoke);
    attitude_ = ledger_.attitudeOf(speaker_.actorId);
    buildTopics();
    return true;
}

void DialogueDirector::close() noexcept {
    open_ = false;
    haggle_.reset();
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
            ledger_.record(speaker_.actorId, Deed::Listened);
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
            ledger_.record(speaker_.actorId, Deed::BoughtDrink);
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
                ledger_.record(speaker_.actorId, Deed::Robbed);
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
    }

    out.dispositionBefore = before;
    return out;
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
            ledger_.record(speaker_.actorId, haggle_.lastDeed());
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
            ledger_.record(speaker_.actorId, Deed::Lowballed);
            out = reply(TopicKind::Trade, "THAT IS NOT AN OFFER. " + coins(haggle_.asking()) + ".");
            out.haggling = true;
            break;
        case HaggleOutcome::Refused:
            ledger_.record(speaker_.actorId, haggle_.lastDeed());
            out = reply(TopicKind::Trade, "WE ARE DONE TALKING PRICE.");
            break;
        case HaggleOutcome::WalkedOut:
            ledger_.record(speaker_.actorId, Deed::WalkedOut);
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
    sink.put_byte(open_ ? 1U : 0U);
    sink.put_int(static_cast<std::uint32_t>(speaker_.actorId));
    sink.put_int(static_cast<std::uint32_t>(speaker_.purse));
    sink.put_int(static_cast<std::uint32_t>(conversations_));
    sink.put_int(static_cast<std::uint32_t>(talkIndex_));
    sink.put_byte(static_cast<std::uint32_t>(attitude_));
    sink.put_int(static_cast<std::uint32_t>(topics_.size()));
}

}  // namespace granadad::sim
