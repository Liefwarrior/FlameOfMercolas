#include "granadad/sim/contract.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

#include <nlohmann/json.hpp>

#include "granadad/sim/barks.hpp"
#include "granadad/sim/rng.hpp"

namespace granadad::sim {

namespace {

[[nodiscard]] std::string stringField(const nlohmann::json& node, const char* key) {
    const auto found = node.find(key);
    if (found == node.end() || !found->is_string()) {
        return {};
    }
    // Folded the same way every other authored string in this project is, so a
    // hand edit that reintroduces a smart quote cannot put a blank column in
    // the middle of a brief.
    return foldToAscii(found->get<std::string>());
}

[[nodiscard]] std::int32_t intField(const nlohmann::json& node, const char* key,
                                    std::int32_t fallback = 0) {
    const auto found = node.find(key);
    if (found == node.end() || !found->is_number_integer()) {
        return fallback;
    }
    return found->get<std::int32_t>();
}

/// The authored ids in an array field, folded and deduplicated, ascending.
[[nodiscard]] std::vector<std::string> stringArray(const nlohmann::json& node, const char* key) {
    std::vector<std::string> out;
    const auto found = node.find(key);
    if (found == node.end() || !found->is_array()) {
        return out;
    }
    for (const nlohmann::json& row : *found) {
        if (row.is_string()) {
            out.push_back(foldToAscii(row.get<std::string>()));
        }
    }
    return out;
}

void put_string(HashSink& sink, std::string_view text) {
    sink.put_int(static_cast<std::uint32_t>(text.size()));
    for (const char c : text) {
        sink.put_byte(static_cast<std::uint32_t>(static_cast<unsigned char>(c)));
    }
}

[[nodiscard]] std::string upperAscii(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return out;
}

/// contrabandLabelFor's own lower-cased sibling, so a brief that ever names
/// {good} gets a counted noun ("3 scalps") rather than the raws-facing
/// snake_case spelling contrabandSymbol carries ("scalp", verbatim what
/// `"good": "scalp"` says in this file) -- no authored brief in
/// contracts.json reaches this token today, but a template that added one
/// would otherwise print the raws' own key at the player, the exact class of
/// leak radiant_quest.cpp's placeProse was just written to close.
[[nodiscard]] std::string lowerAscii(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

/// The name a topic row has room for.
///
/// A topic column is EIGHTEEN GLYPHS WIDE at every resolution this game runs
/// at -- 320x180, 640x360 and 1280x720 all compute the same eighteen (see
/// drawDialogue's columnWidth / glyphAdvance) -- and two of those are spent on
/// the number that picks the row. "GOODMAN TARL SALTGATE" cannot be printed in
/// sixteen and neither can "KEEPER VETCH"; "SALTGATE" and "VETCH" can.
///
/// So the row gets the LAST word of the authored name and nothing else. It is
/// still the owner's own string -- no programmer named anybody -- and it is
/// what the ward calls these people anyway: Vetch, Saltgate, Mag, Crumb.
[[nodiscard]] std::string shortName(std::string_view name) {
    const std::size_t at = name.rfind(' ');
    return upperAscii(at == std::string_view::npos ? name : name.substr(at + 1));
}

/// Replaces every occurrence of `token` with `value`. Small and linear: a brief
/// is one sentence and there are five tokens.
void substitute(std::string& text, std::string_view token, std::string_view value) {
    std::size_t at = text.find(token);
    while (at != std::string::npos) {
        text.replace(at, token.size(), value);
        at = text.find(token, at + value.size());
    }
}

/// The salt every contract draw hangs off. Named, never positional -- see the
/// note on stream_salt: renaming this re-rolls every board there has ever been.
const std::uint64_t kBoardSalt = stream_salt("contract.board");

}  // namespace

// ---------------------------------------------------------------------------
// the raws
// ---------------------------------------------------------------------------

std::filesystem::path contractRawsPath(const std::filesystem::path& contentDir) {
    return contentDir / "raws" / "contracts" / "contracts.json";
}

std::filesystem::path contractRankRawsPath(const std::filesystem::path& contentDir) {
    return contentDir / "raws" / "contracts" / "contract_ranks.json";
}

const ContractBroker* ContractRaws::broker(std::string_view id) const noexcept {
    for (const ContractBroker& row : brokers_) {
        if (row.id == id) {
            return &row;
        }
    }
    return nullptr;
}

std::size_t ContractRaws::offersFor(std::string_view brokerId) const noexcept {
    std::size_t count = 0;
    for (const ContractOffer& offer : offers_) {
        if (offer.broker == brokerId) {
            ++count;
        }
    }
    return count;
}

std::string ContractRaws::siteName(std::string_view siteId) const {
    for (const auto& row : sites_) {
        if (row.first == siteId) {
            return row.second;
        }
    }
    // Not in the table. Strip a leading key ("K25_", "C1_") and open the
    // underscores out, so an unlisted site still reads as English rather than
    // as a database row.
    std::string rest(siteId);
    const std::size_t underscore = rest.find('_');
    if (underscore != std::string::npos && underscore <= 4) {
        rest = rest.substr(underscore + 1);
    }
    for (char& c : rest) {
        if (c == '_') {
            c = ' ';
        } else {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return rest;
}

ContractRaws ContractRaws::load(const std::filesystem::path& contentDir,
                                const NotableRegistry& notables,
                                const FactionRegistry& factions) {
    ContractRaws out;
    std::error_code error;
    const std::filesystem::path path = contractRawsPath(contentDir);
    if (!std::filesystem::is_regular_file(path, error)) {
        return out;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return out;
    }
    std::ostringstream text;
    text << file.rdbuf();
    const nlohmann::json document = nlohmann::json::parse(text.str(), nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return out;
    }

    // --- the sites the ward has its own words for --------------------------
    const auto sites = document.find("sites");
    if (sites != document.end() && sites->is_object()) {
        for (auto row = sites->begin(); row != sites->end(); ++row) {
            if (row.value().is_string()) {
                out.sites_.emplace_back(foldToAscii(row.key()),
                                        foldToAscii(row.value().get<std::string>()));
            }
        }
        std::sort(out.sites_.begin(), out.sites_.end());
    }

    // --- who hands work out -------------------------------------------------
    //
    // REFUSED BY NAME, both ways: a broker who is not one of the Forty, or who
    // speaks for a faction the owner's registry does not have, is not a broker.
    const auto brokers = document.find("brokers");
    if (brokers != document.end() && brokers->is_array()) {
        for (const nlohmann::json& node : *brokers) {
            if (!node.is_object()) {
                continue;
            }
            ContractBroker row;
            row.id = stringField(node, "id");
            row.faction = stringField(node, "faction");
            row.label = upperAscii(stringField(node, "label"));
            row.needs = stringField(node, "needs");
            if (row.needs.empty()) {
                // Silence means the strictest reading. A broker whose gate the
                // file forgot to state does not become a public bounty by
                // omission.
                row.needs = "member";
            }
            if (row.id.empty() || notables.find(row.id) == nullptr) {
                continue;
            }
            if (!row.faction.empty() && factions.indexOf(row.faction) < 0) {
                continue;
            }
            out.brokers_.push_back(std::move(row));
        }
        std::sort(out.brokers_.begin(), out.brokers_.end(),
                  [](const ContractBroker& a, const ContractBroker& b) { return a.id < b.id; });
    }

    // --- the templates ------------------------------------------------------
    const auto offers = document.find("offers");
    if (offers == document.end() || !offers->is_array()) {
        return out;
    }
    const auto keepNamed = [&notables](std::vector<std::string> ids) {
        std::vector<std::string> kept;
        for (std::string& id : ids) {
            if (notables.find(id) != nullptr) {
                kept.push_back(std::move(id));
            }
        }
        std::sort(kept.begin(), kept.end());
        kept.erase(std::unique(kept.begin(), kept.end()), kept.end());
        return kept;
    };
    for (const nlohmann::json& node : *offers) {
        if (!node.is_object()) {
            continue;
        }
        ContractOffer offer;
        offer.id = stringField(node, "id");
        offer.broker = stringField(node, "broker");
        if (offer.id.empty() || out.broker(offer.broker) == nullptr) {
            continue;
        }
        if (!contrabandFromSymbol(stringField(node, "good"), offer.good)) {
            continue;
        }
        offer.verb = upperAscii(stringField(node, "verb"));
        offer.brief = stringField(node, "brief");
        offer.unitsMin = std::max(1, intField(node, "unitsMin", 1));
        offer.unitsMax = std::max(offer.unitsMin, intField(node, "unitsMax", offer.unitsMin));
        offer.payPerUnit = std::max(0, intField(node, "payPerUnit"));
        offer.days = std::max(1, intField(node, "days", 1));
        offer.patrons = keepNamed(stringArray(node, "patrons"));
        offer.sources = keepNamed(stringArray(node, "sources"));
        offer.things = stringArray(node, "things");
        if (offer.patrons.empty() || offer.sources.empty() || offer.verb.empty()) {
            // A job with nobody to want it or nobody to take it from is not a
            // job. Dropping it here is what makes every generated contract name
            // somebody who exists.
            continue;
        }
        out.offers_.push_back(std::move(offer));
    }
    std::sort(out.offers_.begin(), out.offers_.end(),
              [](const ContractOffer& a, const ContractOffer& b) { return a.id < b.id; });

    // --- #81: which of those templates a rung is asked for -------------------
    //
    // A sibling file, read AFTER offers_ so a gate can be matched against a
    // real one and refused by name otherwise -- the same shape a broker or a
    // patron this file does not have is refused at load, above. Absent file,
    // absent "gates" array, or a row naming an offer that does not exist all
    // leave minRank at the 0 every ContractOffer is already built with, which
    // means the broker's own `needs` field stays the only gate on it.
    {
        std::ifstream ranksFile(contractRankRawsPath(contentDir), std::ios::binary);
        if (ranksFile) {
            std::ostringstream ranksText;
            ranksText << ranksFile.rdbuf();
            const nlohmann::json ranksDoc = nlohmann::json::parse(ranksText.str(), nullptr, false);
            if (!ranksDoc.is_discarded() && ranksDoc.is_object()) {
                const auto gates = ranksDoc.find("gates");
                if (gates != ranksDoc.end() && gates->is_array()) {
                    for (const nlohmann::json& node : *gates) {
                        if (!node.is_object()) {
                            continue;
                        }
                        const std::string offerId = stringField(node, "offer");
                        const auto found = std::lower_bound(
                            out.offers_.begin(), out.offers_.end(), offerId,
                            [](const ContractOffer& row, const std::string& probe) {
                                return row.id < probe;
                            });
                        if (found == out.offers_.end() || found->id != offerId) {
                            // Names an offer contracts.json does not carry.
                            // Refused, the same as a broker or a patron would
                            // be, rather than left to invent a ninth offer.
                            continue;
                        }
                        // Clamped to the broker's own ladder, so a typo in
                        // this file cannot ask for a rung the faction's own
                        // ladder does not go up to.
                        std::int32_t cap = 0;
                        if (const ContractBroker* offerBroker = out.broker(found->broker);
                            offerBroker != nullptr) {
                            const std::int32_t index = factions.indexOf(offerBroker->faction);
                            if (const FactionLadder* ladder = factions.ladder(index);
                                ladder != nullptr) {
                                cap = static_cast<std::int32_t>(ladder->ranks.size());
                            }
                        }
                        found->minRank =
                            std::clamp(intField(node, "minRank"), 0, cap);
                    }
                }
            }
        }
    }

    // --- and everybody those templates are allowed to name -------------------
    //
    // Copied out of the owner's registry here, once, so a brief is composed
    // from authored names and authored sites without the board holding a second
    // reference to a registry it has no other use for.
    const auto remember = [&out, &notables](const std::string& id) {
        for (const ContractPerson& row : out.people_) {
            if (row.id == id) {
                return;
            }
        }
        const Notable* who = notables.find(id);
        if (who == nullptr) {
            return;
        }
        out.people_.push_back(ContractPerson{who->id, who->name, out.siteName(who->site)});
    };
    for (const ContractBroker& row : out.brokers_) {
        remember(row.id);
    }
    for (const ContractOffer& offer : out.offers_) {
        for (const std::string& id : offer.patrons) {
            remember(id);
        }
        for (const std::string& id : offer.sources) {
            remember(id);
        }
    }
    std::sort(out.people_.begin(), out.people_.end(),
              [](const ContractPerson& a, const ContractPerson& b) { return a.id < b.id; });
    return out;
}

const ContractPerson* ContractRaws::person(std::string_view id) const noexcept {
    for (const ContractPerson& row : people_) {
        if (row.id == id) {
            return &row;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// the vocabulary
// ---------------------------------------------------------------------------

std::string_view contractStateName(ContractState state) noexcept {
    switch (state) {
        case ContractState::Offered:
            return "offered";
        case ContractState::Taken:
            return "taken";
        case ContractState::Paid:
            return "paid";
        case ContractState::Expired:
            return "expired";
        case ContractState::Seized:
            return "seized";
    }
    return "?";
}

std::string_view takeResultName(TakeResult result) noexcept {
    switch (result) {
        case TakeResult::Taken:
            return "taken";
        case TakeResult::NoSuchContract:
            return "no such contract";
        case TakeResult::NotOffered:
            return "not on the board";
        case TakeResult::HandsFull:
            return "hands full";
    }
    return "?";
}

std::string_view turnInResultName(TurnInResult result) noexcept {
    switch (result) {
        case TurnInResult::Paid:
            return "paid";
        case TurnInResult::NoSuchContract:
            return "no such contract";
        case TurnInResult::NotYours:
            return "not yours";
        case TurnInResult::Short:
            return "short";
        case TurnInResult::Late:
            return "late";
        case TurnInResult::NeedsSanction:
            return "unsanctioned";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// the board
// ---------------------------------------------------------------------------

void ContractBoard::attach(std::shared_ptr<const ContractRaws> raws) {
    raws_ = std::move(raws);
}

const Contract* ContractBoard::find(std::int32_t id) const noexcept {
    for (const Contract& row : rows_) {
        if (row.id == id) {
            return &row;
        }
    }
    return nullptr;
}

Contract* ContractBoard::rowFor(std::int32_t id) noexcept {
    for (Contract& row : rows_) {
        if (row.id == id) {
            return &row;
        }
    }
    return nullptr;
}

std::int32_t ContractBoard::takenCount() const noexcept {
    std::int32_t taken = 0;
    for (const Contract& row : rows_) {
        if (row.live()) {
            ++taken;
        }
    }
    return taken;
}

std::vector<std::int32_t> ContractBoard::offeredBy(std::string_view brokerId) const {
    std::vector<std::int32_t> ids;
    for (const Contract& row : rows_) {
        if (row.broker == brokerId && row.state == ContractState::Offered) {
            ids.push_back(row.id);
        }
    }
    return ids;
}

std::vector<std::int32_t> ContractBoard::takenBy(std::string_view brokerId) const {
    std::vector<std::int32_t> ids;
    for (const Contract& row : rows_) {
        if (row.broker == brokerId && row.live()) {
            ids.push_back(row.id);
        }
    }
    return ids;
}

void ContractBoard::expireStale() {
    for (Contract& row : rows_) {
        if (row.live() && day_ > row.dueOnDay) {
            row.state = ContractState::Expired;
            ++failed_;
        }
    }
}

void ContractBoard::refresh(std::int32_t day, std::uint64_t worldSeed,
                            const FactionLedger& standings) {
    if (day == day_) {
        return;
    }
    day_ = day;
    expireStale();
    // Everything that is nobody's business any more goes. A TAKEN contract
    // survives the sunrise -- a job with three nights on it is not cancelled
    // because one of them has gone.
    rows_.erase(std::remove_if(rows_.begin(), rows_.end(),
                               [](const Contract& row) { return !row.live(); }),
                rows_.end());
    if (raws_ == nullptr || raws_->offers().empty()) {
        return;
    }

    const CounterRandomSource source(worldSeed, kBoardSalt);
    CounterRandomSource night = source;
    night.begin_tick(static_cast<std::uint64_t>(day < 0 ? 0 : day));

    // #81. A rung this offer's broker's own faction ladder has not been
    // climbed to yet keeps that offer out of every pool below, in the
    // broker-restricted slots and in the wildcard one alike -- a player who
    // has not sworn far enough in never sees the work exist, rather than
    // seeing it and being turned away for it. minRank 0 (every offer this
    // sprint did not name in contract_ranks.json) is unconditionally true,
    // so this changes nothing for a board with no gated offers on it.
    const auto meetsRank = [this, &standings](const ContractOffer& offer) noexcept {
        if (offer.minRank <= 0) {
            return true;
        }
        const ContractBroker* offerBroker = raws_->broker(offer.broker);
        if (offerBroker == nullptr || standings.registry() == nullptr) {
            return true;
        }
        const std::int32_t index = standings.registry()->indexOf(offerBroker->faction);
        if (index < 0) {
            return true;
        }
        return standings.rank(index) >= offer.minRank;
    };

    const std::vector<ContractOffer>& templates = raws_->offers();
    for (std::int32_t slot = 0; slot < kOffersPerDay; ++slot) {
        const auto key = static_cast<std::uint64_t>(slot);

        // EVERY BROKER HAS SOMETHING TONIGHT, and the last slot is whoever the
        // tide favoured. Four jobs drawn freely out of ten templates would
        // leave the ward with no bounty on two nights in five and no work for
        // the roofs on nearly as many, which reads as a broken board rather
        // than as a quiet night -- and it would make the whole board a lottery
        // a player cannot plan against. The pool is narrowed; nothing else
        // about the draw changes.
        std::vector<const ContractOffer*> pool;
        const std::vector<ContractBroker>& brokers = raws_->brokers();
        for (const ContractOffer& row : templates) {
            if (!meetsRank(row)) {
                continue;
            }
            if (static_cast<std::size_t>(slot) >= brokers.size() ||
                row.broker == brokers[static_cast<std::size_t>(slot)].id) {
                pool.push_back(&row);
            }
        }
        if (pool.empty()) {
            continue;
        }
        // THE DRAW SCHEDULE. Appended, never inserted -- the same rule the
        // engine's own systems follow, and for the same reason: a new draw put
        // in front of an existing one re-rolls every board of every world.
        const ContractOffer& offer =
            *pool[static_cast<std::size_t>(night.draw(key, 0) % pool.size())];
        const std::string& patron =
            offer.patrons[static_cast<std::size_t>(night.draw(key, 1) % offer.patrons.size())];
        std::string sourceId =
            offer.sources[static_cast<std::size_t>(night.draw(key, 2) % offer.sources.size())];
        if (sourceId == patron && offer.sources.size() > 1) {
            // Nobody is asked to steal from themselves. Stepped rather than
            // re-drawn, so the schedule stays a fixed length.
            const std::size_t at = static_cast<std::size_t>(
                std::find(offer.sources.begin(), offer.sources.end(), sourceId) -
                offer.sources.begin());
            sourceId = offer.sources[(at + 1) % offer.sources.size()];
        }
        const std::int32_t span = offer.unitsMax - offer.unitsMin + 1;
        const std::int32_t units =
            offer.unitsMin +
            static_cast<std::int32_t>(night.draw(key, 3) % static_cast<std::uint64_t>(span));
        const std::string thing =
            offer.things.empty()
                ? std::string{}
                : offer.things[static_cast<std::size_t>(night.draw(key, 4) %
                                                        offer.things.size())];

        Contract row;
        row.id = day * kOffersPerDay + slot;
        row.offerId = offer.id;
        row.broker = offer.broker;
        row.patron = patron;
        row.source = sourceId;
        row.good = offer.good;
        row.units = units;
        row.postedOnDay = day;
        row.dueOnDay = day + offer.days - 1;
        row.state = ContractState::Offered;

        // THE PAY IS THE WARD'S OWN. guildPricePercent is a DISCOUNT on a
        // purchase, so its negation is a premium on a wage: a guild that sells
        // to you as one of its own also pays you as one of its own, in the same
        // three terms and with the same clamp. A stranger to a guild with the
        // district in its pocket is paid a stranger's rate.
        const ContractBroker* broker = raws_->broker(offer.broker);
        const std::int32_t guild =
            broker == nullptr ? -1 : standings.registry() == nullptr
                                         ? -1
                                         : standings.registry()->indexOf(broker->faction);
        const std::int32_t premium = guild < 0 ? 0 : -guildPricePercent(standings, guild);
        row.pay = std::max(1, units * offer.payPerUnit * (100 + premium) / 100);

        // EVERY PROPER NOUN COMES OUT OF THE OWNER'S FILE. The template says
        // "{patron} is overrun at {patronSite}"; what goes in is a name and a
        // place the notables' registry actually carries, which is the whole
        // difference between a radiant job and a slot machine.
        const ContractPerson* wants = raws_->person(patron);
        const ContractPerson* from = raws_->person(sourceId);
        // THE PATRON LEADS, AND THAT IS THE WHOLE FIX.
        //
        // S6 built "TAKE 4 SCALPS FOR KEEPER VETCH" and the eighteen-column
        // topic grid printed "8 TAKE 3 SCALPS." beside "9 TAKE 4 SCALPS." --
        // two rows differing by one integer, with the authored proper noun the
        // sprint existed to prove cut off the end. The name went last, so the
        // name was what the column ate.
        //
        // It goes first now, and the units and the good follow it:
        // "VETCH - 4 SCALPS" is sixteen glyphs, fits the column WITH its
        // number, names the person, and two bounties on one board are told
        // apart by the first word instead of the last. The verb the offer
        // authors is not lost -- it is in the brief, which is what the row
        // shows once it is picked, and the full label is on the detail line
        // under the grid either way (dialogueDetailLine).
        row.label = (wants == nullptr ? std::string("THE WARD") : shortName(wants->name)) + " - " +
                    std::to_string(units) + " " +
                    std::string(contrabandLabelFor(offer.good, units));
        row.thing = thing;
        row.brief = offer.brief;
        substitute(row.brief, "{patron}", wants == nullptr ? "somebody" : wants->name);
        substitute(row.brief, "{patronSite}", wants == nullptr ? "the ward" : wants->place);
        substitute(row.brief, "{source}", from == nullptr ? "somebody" : from->name);
        substitute(row.brief, "{sourceSite}", from == nullptr ? "the ward" : from->place);
        substitute(row.brief, "{units}", std::to_string(units));
        substitute(row.brief, "{good}", lowerAscii(contrabandLabelFor(offer.good, units)));
        substitute(row.brief, "{thing}", thing);
        rows_.push_back(std::move(row));
    }
    std::sort(rows_.begin(), rows_.end(),
              [](const Contract& a, const Contract& b) { return a.id < b.id; });
}

TakeResult ContractBoard::take(std::int32_t id) {
    Contract* row = rowFor(id);
    if (row == nullptr) {
        return TakeResult::NoSuchContract;
    }
    if (row->state != ContractState::Offered) {
        return TakeResult::NotOffered;
    }
    if (takenCount() >= kMaxTakenContracts) {
        return TakeResult::HandsFull;
    }
    row->state = ContractState::Taken;
    return TakeResult::Taken;
}

Settlement ContractBoard::turnIn(std::int32_t id, Stash& stash, std::int32_t day) {
    Settlement out;
    Contract* row = rowFor(id);
    if (row == nullptr) {
        out.result = TurnInResult::NoSuchContract;
        return out;
    }
    if (!row->live()) {
        out.result = TurnInResult::NotYours;
        return out;
    }
    if (day > row->dueOnDay) {
        row->state = ContractState::Expired;
        ++failed_;
        out.result = TurnInResult::Late;
        return out;
    }
    if (stash.count(row->good) < row->units) {
        out.result = TurnInResult::Short;
        return out;
    }
    if (row->needsSanction()) {
        // The Church signs for blood money or the Watch does not pay for it.
        out.result = TurnInResult::NeedsSanction;
        return out;
    }
    if (row->good == Contraband::Artifact && row->recovered < row->units) {
        // THE PIECE IS THE PIECE. A recovery job named an object out of the
        // owner's own file -- "a sea-chart with the wrong soundings inked over
        // the right ones" -- and pieces already in the sack are not it. Short,
        // and for the same reason a job for four jars is short at three.
        out.result = TurnInResult::Short;
        return out;
    }
    out.unitsTaken = stash.take(row->good, row->units);
    out.pay = row->pay;
    out.result = TurnInResult::Paid;
    row->state = ContractState::Paid;
    ++paid_;
    earned_ += row->pay;
    return out;
}

const Contract* ContractBoard::recoverPiece() {
    // Ascending by id, and rows_ is kept ascending by id -- so "the earliest
    // live recovery job still short" is the first match in a forward walk and
    // no sort is needed to make the order a fact rather than a habit.
    for (Contract& row : rows_) {
        if (!row.live() || row.good != Contraband::Artifact || row.recovered >= row.units) {
            continue;
        }
        ++row.recovered;
        return &row;
    }
    return nullptr;
}

std::int32_t ContractBoard::sanction() {
    std::int32_t marked = 0;
    for (Contract& row : rows_) {
        if (row.live() && contrabandNeedsSanction(row.good) && !row.sanctioned) {
            row.sanctioned = true;
            ++marked;
        }
    }
    return marked;
}

std::int32_t ContractBoard::seizeFor(const Stash& before) {
    std::int32_t lost = 0;
    for (Contract& row : rows_) {
        if (!row.live() || contrabandLegal(row.good)) {
            continue;
        }
        if (before.count(row.good) <= 0) {
            // Nothing of this job's goods was in the sack, so the seizure did
            // not touch it. A job you had not started yet is a job you can
            // still do.
            continue;
        }
        row.state = ContractState::Seized;
        ++failed_;
        ++lost;
    }
    return lost;
}

void ContractBoard::hashInto(HashSink& sink) const {
    sink.put_int(static_cast<std::uint32_t>(day_));
    sink.put_int(static_cast<std::uint32_t>(rows_.size()));
    for (const Contract& row : rows_) {
        sink.put_int(static_cast<std::uint32_t>(row.id));
        put_string(sink, row.offerId);
        put_string(sink, row.patron);
        put_string(sink, row.source);
        sink.put_byte(static_cast<std::uint32_t>(row.good));
        sink.put_int(static_cast<std::uint32_t>(row.units));
        sink.put_int(static_cast<std::uint32_t>(row.pay));
        sink.put_int(static_cast<std::uint32_t>(row.dueOnDay));
        sink.put_byte(static_cast<std::uint32_t>(row.state));
        sink.put_byte(row.sanctioned ? 1U : 0U);
        // S7: how many of the named pieces this job actually has. It decides
        // whether the job can be settled, so it is state and the twin-run gate
        // has to be able to see it.
        sink.put_int(static_cast<std::uint32_t>(row.recovered));
    }
    sink.put_int(static_cast<std::uint32_t>(paid_));
    sink.put_int(static_cast<std::uint32_t>(failed_));
    sink.put_int(static_cast<std::uint32_t>(earned_));
}

}  // namespace granadad::sim
