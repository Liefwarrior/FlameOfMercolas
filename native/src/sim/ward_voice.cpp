#include "granadad/sim/ward_voice.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace granadad::sim {

namespace {

/// Non-negative row index into a pool of `count`. The same guard
/// BarkTables::line uses, and for the same reason: a negative counter must not
/// index off the front of a table.
[[nodiscard]] std::size_t rowOf(std::int32_t index, std::size_t count) noexcept {
    if (count == 0) {
        return 0;
    }
    std::int32_t at = index % static_cast<std::int32_t>(count);
    if (at < 0) {
        at += static_cast<std::int32_t>(count);
    }
    return static_cast<std::size_t>(at);
}

/// A stable scramble of an id. Fibonacci hashing on a 32-bit word: no RNG
/// stream is touched, so naming the ward cannot shift a single draw the
/// twin-run gate accounts for, and the answer is the same on every toolchain
/// because it is integer arithmetic and nothing else.
[[nodiscard]] std::uint32_t mix(std::int32_t id, std::uint32_t salt) noexcept {
    std::uint32_t h = static_cast<std::uint32_t>(id) + salt * 0x9E3779B9u;
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

/// Reads one { "group": [ ... ] } object into a sorted pool list.
void readPools(const nlohmann::json& node, std::vector<std::pair<std::string,
                                                                 std::vector<std::string>>>& out) {
    if (!node.is_object()) {
        return;
    }
    for (const auto& [group, rows] : node.items()) {
        if (!rows.is_array() || group.empty()) {
            continue;
        }
        std::vector<std::string> pool;
        for (const nlohmann::json& row : rows) {
            if (row.is_string()) {
                std::string text = foldToAscii(row.get<std::string>());
                if (!text.empty()) {
                    pool.push_back(std::move(text));
                }
            }
        }
        if (!pool.empty()) {
            out.emplace_back(group, std::move(pool));
        }
    }
}

void readList(const nlohmann::json& node, std::vector<std::string>& out) {
    if (!node.is_array()) {
        return;
    }
    for (const nlohmann::json& row : node) {
        if (row.is_string()) {
            std::string text = foldToAscii(row.get<std::string>());
            if (!text.empty()) {
                out.push_back(std::move(text));
            }
        }
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// the pools
// ---------------------------------------------------------------------------

NameRaws NameRaws::load(const std::filesystem::path& contentDir) {
    NameRaws out;
    const std::filesystem::path path = contentDir / "raws" / "names" / "names.json";
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

    std::vector<std::pair<std::string, std::vector<std::string>>> given;
    std::vector<std::pair<std::string, std::vector<std::string>>> epithets;
    if (const auto it = document.find("givenByGroup"); it != document.end()) {
        readPools(*it, given);
    }
    if (const auto it = document.find("epithetsByGroup"); it != document.end()) {
        readPools(*it, epithets);
    }
    if (const auto it = document.find("surnames"); it != document.end()) {
        readList(*it, out.surnames_);
    }
    if (const auto it = document.find("kennel"); it != document.end()) {
        readList(*it, out.kennel_);
    }

    // Sorted by group so lookup is a binary search over a vector and iteration
    // order is the CONTENT's rather than the standard library's. nlohmann's
    // object iteration is already ordered, but relying on that would make the
    // ward's names a property of a dependency's container choice.
    const auto adopt = [](std::vector<std::pair<std::string, std::vector<std::string>>>& from,
                          std::vector<Pool>& to) {
        std::sort(from.begin(), from.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        to.reserve(from.size());
        for (auto& [group, rows] : from) {
            to.push_back(Pool{group, std::move(rows)});
        }
    };
    adopt(given, out.given_);
    adopt(epithets, out.epithets_);
    return out;
}

std::string_view NameRaws::given(std::string_view group, std::int32_t index) const noexcept {
    const auto at = std::lower_bound(
        given_.begin(), given_.end(), group,
        [](const Pool& pool, std::string_view want) { return pool.group < want; });
    if (at == given_.end() || at->group != group || at->rows.empty()) {
        return {};
    }
    return at->rows[rowOf(index, at->rows.size())];
}

std::string_view NameRaws::epithet(std::string_view group, std::int32_t index) const noexcept {
    const auto at = std::lower_bound(
        epithets_.begin(), epithets_.end(), group,
        [](const Pool& pool, std::string_view want) { return pool.group < want; });
    if (at == epithets_.end() || at->group != group || at->rows.empty()) {
        return {};
    }
    return at->rows[rowOf(index, at->rows.size())];
}

std::string_view NameRaws::surname(std::int32_t index) const noexcept {
    if (surnames_.empty()) {
        return {};
    }
    return surnames_[rowOf(index, surnames_.size())];
}

// ---------------------------------------------------------------------------
// what a body presents as
// ---------------------------------------------------------------------------

JobFamily wardJobFamily(WardType type) noexcept {
    switch (type) {
        case WardType::Serf:
            return JobFamily::Serf;
        case WardType::Carter:
            // A drayman hauls for a living. He is labour with a cart, not a
            // counter -- greet.trade is authored for somebody who sells you
            // something and he has nothing to sell.
            return JobFamily::Serf;
        case WardType::Shopkeeper:
            return JobFamily::Trade;
        case WardType::Sailor:
        case WardType::Fisher:
            return JobFamily::Maritime;
        case WardType::MilitiaWatch:
            return JobFamily::Watch;
        case WardType::Wastrel:
        case WardType::Urchin:
        case WardType::Thief:
            // PRESENTED, not true. See the header: a cutpurse on the kerb reads
            // to the ward as one more body with no work today, which is exactly
            // what greet.wastrel is authored for.
            return JobFamily::Wastrel;
        case WardType::PriestOfTheFlame:
        case WardType::DiscipleOfTheFlame:
            return JobFamily::Clergy;
        case WardType::AnimalKeeper:
            return JobFamily::Husbandry;
        case WardType::Dog:
        case WardType::Stray:
        case WardType::Cat:
        case WardType::Mouse:
            return JobFamily::Beast;
    }
    return JobFamily::Serf;
}

std::string_view wardSkillId(WardType type) noexcept {
    switch (type) {
        case WardType::Fisher:
            return "fishing";
        case WardType::Sailor:
            return "seacraft";
        case WardType::MilitiaWatch:
            // notables.json gives Watchman Cull kit_keeping because he
            // inventories seized cargo for a living, and a search is what a
            // watchman's hands are actually for.
            return "kit_keeping";
        case WardType::PriestOfTheFlame:
        case WardType::DiscipleOfTheFlame:
            return "channeling";
        case WardType::Shopkeeper:
        case WardType::Wastrel:
        case WardType::Urchin:
        case WardType::Thief:
            // Knowing the ward is the trade all four of these live on, from
            // opposite ends of it.
            return "streetwise";
        case WardType::AnimalKeeper:
        case WardType::Serf:
        case WardType::Carter:
            // fieldcraft is the ward's outdoor working knowledge -- weather,
            // beasts, ground and what will hold.
            return "fieldcraft";
        default:
            break;
    }
    return {};
}

std::int32_t wardTradeLevel(const WardActor& actor) noexcept {
    if (!isPerson(actor.type)) {
        return 0;
    }
    // 4..49, so all three authored bands occur and roughly a fifth of the ward
    // is below the novice threshold and says nothing about its work at all.
    // See the header on what this number is and is not.
    return 4 + static_cast<std::int32_t>(mix(actor.id, 3u) % 46u);
}

// ---------------------------------------------------------------------------
// the mood
// ---------------------------------------------------------------------------

std::string wardMoodKey(const WardActor& actor, JobFamily family, const BarkTables& barks) {
    // The candidates, worst first. Each is tried as `<key>.<family>` and then
    // as `<key>`, so a hungry watchman can have his own sentence and a hungry
    // rope-hand can fall back to the ward's.
    std::vector<std::string> wanted;
    if (actor.dead) {
        // Authored in the owner's own barks.json, and it outranks everything.
        return "mood.dead";
    }
    if (family == JobFamily::Beast) {
        // A HUNGRY CAT DOES NOT SAY IT IS HUNGRY. The ward tracks a beast's
        // appetite like everybody else's, and every table below is a sentence
        // in English -- so without this a stray under the piers would greet you
        // with "I am thinking about bread." greet.beast is the whole of what a
        // beast has, and it is right.
        return {};
    }
    if (actor.policy == WardPolicy::Flee) {
        wanted.emplace_back("mood.panicked");
    }
    if (actor.need(Need::Hunger) <= kNeedCritical) {
        wanted.emplace_back("ward.starving");
    } else if (actor.need(Need::Hunger) < kNeedLow) {
        wanted.emplace_back("ward.hungry");
    }
    if (actor.need(Need::Rest) < kNeedLow) {
        wanted.emplace_back("ward.weary");
    }
    if (actor.policy == WardPolicy::ReturnHome && !actor.atHome()) {
        wanted.emplace_back("ward.homebound");
    }

    const std::string_view familyKey = jobFamilyKey(family);
    for (const std::string& base : wanted) {
        const std::string specific = base + "." + std::string(familyKey);
        if (barks.has(specific)) {
            return specific;
        }
        if (barks.has(base)) {
            return base;
        }
    }
    // An ordinary day. The greeting tables already know what the hour and the
    // trade sound like, and overriding them here would flatten the ward into
    // one voice.
    return {};
}

// ---------------------------------------------------------------------------
// the bake
// ---------------------------------------------------------------------------

void WardPopulation::bakeIdentities(const std::filesystem::path& contentDir) {
    // The roster has already written a notableId into the rows it bound. Extend
    // to the final roll -- the beasts were spawned after that -- without
    // touching what is there.
    identities_.resize(actors_.size());

    const NameRaws names = NameRaws::load(contentDir);
    const NotableRegistry notables = NotableRegistry::load(contentDir);

    for (std::size_t i = 0; i < actors_.size(); ++i) {
        const WardActor& actor = actors_[i];
        WardIdentity& who = identities_[i];

        if (!who.notableId.empty()) {
            // THE OWNER'S OWN NAME WINS OUTRIGHT. A notable is not a body with
            // a nickname bolted on: name, epithet and bio are all authored, and
            // if the id does not resolve the binding is dropped rather than
            // half-applied, so a typo in kKeepers surfaces as an anonymous
            // keeper instead of as a body claiming a personal table it has no
            // right to.
            if (const Notable* notable = notables.find(who.notableId); notable != nullptr) {
                who.name = notable->name;
                who.epithet = notable->epithet;
                continue;
            }
            who.notableId.clear();
        }

        if (!isPerson(actor.type)) {
            // A beast is named out of the kennel pool the owner authored for
            // exactly this, and a mouse is not named at all -- nothing in the
            // ward has ever called one anything.
            if (actor.type == WardType::Mouse || names.kennel().empty()) {
                who.name = std::string(wardTypeName(actor.type));
            } else {
                who.name = names.kennel()[rowOf(static_cast<std::int32_t>(mix(actor.id, 11u)),
                                                names.kennel().size())];
            }
            continue;
        }

        const std::string_view group = wardTypeRawsId(actor.type);
        const std::string_view given =
            names.given(group, static_cast<std::int32_t>(mix(actor.id, 1u) & 0x7FFFFFFF));
        who.name = std::string(given.empty() ? wardTypeName(actor.type) : given);

        // A SURNAME IS A THING YOU HAVE IF THE WARD KEEPS TRACK OF YOU. The
        // trades, the counters and the Watch have one; the ward's poor, its
        // children and its thieves go by one name, which is how the owner's own
        // wastrel pool is written -- Sniv, Tatter, Moll, Grib. Nobody named
        // "Sniv Coldquay" appears in this district.
        const bool surnamed = group == "serf" || group == "shopkeeper" ||
                              group == "militia_watch" || group == "animal_keeper";
        if (surnamed && !given.empty()) {
            const std::string_view family =
                names.surname(static_cast<std::int32_t>(mix(actor.id, 2u) & 0x7FFFFFFF));
            if (!family.empty()) {
                who.name += ' ';
                who.name += family;
            }
        }

        const std::string_view epithet =
            names.epithet(group, static_cast<std::int32_t>(mix(actor.id, 5u) & 0x7FFFFFFF));
        who.epithet = std::string(epithet);
    }
}

// ---------------------------------------------------------------------------
// the speaker
// ---------------------------------------------------------------------------

Speaker wardSpeakerFor(const WardActor& actor, const WardIdentity& who,
                       const NotableRegistry& notables, const FactionRegistry& factions,
                       const BarkTables& barks) {
    Speaker speaker;
    speaker.actorId = kWardSpeakerIdBase + actor.id;
    speaker.name = who.name;
    speaker.epithet = who.epithet;
    speaker.notableId = who.notableId;
    speaker.family = wardJobFamily(actor.type);
    speaker.beast = !wardSpeaks(actor.type);

    speaker.skillId = std::string(wardSkillId(actor.type));
    speaker.skillLevel = wardTradeLevel(actor);
    if (const Notable* notable = notables.find(who.notableId); notable != nullptr) {
        // A notable's own bio is the authority on what they are good at, and it
        // outranks the ward's stable standing outright. Crell reads paperwork
        // for a living and the raws say so; nothing here gets to guess.
        if (!notable->bestSkill().empty()) {
            speaker.skillId = std::string(notable->bestSkill());
            speaker.skillLevel = notable->bestSkillLevel();
        }
        const std::int32_t street = notable->skillLevel("streetwise");
        if (street > 0) {
            speaker.haggleSkill = street;
            speaker.awareness = street;
        }
    }
    if (speaker.awareness == 0) {
        // Their own streetwise, which is what a hand in a purse is fought
        // with. A body on its own doorstep at four in the morning is no less
        // alert than one at noon; what changes is who else is watching, and
        // that is the room's business rather than the speaker's.
        speaker.awareness = speaker.skillId == "streetwise" ? speaker.skillLevel
                                                            : speaker.skillLevel / 2;
        speaker.haggleSkill = speaker.awareness;
    }

    speaker.purse = actor.coin;
    // NOBODY IN THE WARD KEEPS A COUNTER YET, and saying so is better than
    // inventing a price. The stalls and the shops are simulated as POSTS -- a
    // body standing at an anchor doing work -- and there is no stock behind
    // them to sell. A shopkeeper who offered to sell you a drink out of thin
    // air would be the dialogue layer lying about the world.
    speaker.trades = false;

    if (const Faction* faction = factions.at(factions.factionForJobPrefix(
            jobFamilyKey(speaker.family)));
        faction != nullptr) {
        speaker.factionId = faction->id;
    }
    // Nobody in the ward recruits, teaches or fences. Those three are jobs
    // somebody has, and the bodies who have them are in the Gilded Gull.
    speaker.recruitsFor.clear();
    speaker.teaches = false;
    speaker.buysStolen = false;
    // And leaning on the law is not a thing that could work.
    speaker.leanable = speaker.family != JobFamily::Watch && !speaker.beast;

    speaker.moodKey = wardMoodKey(actor, speaker.family, barks);
    return speaker;
}

}  // namespace granadad::sim
