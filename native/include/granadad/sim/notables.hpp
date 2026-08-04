#pragma once

// The Forty Notables, the stories between them, and who is allowed to tell
// which one.
//
// Three authored files, one registry, because they are one fact split three
// ways and reading any of them alone gives you a half-truth:
//
//   content/raws/names/notables.json   42 named people: name, epithet, type,
//                                      site, bio, and the skills their bio
//                                      already claims.
//   content/raws/names/histories.json  15 hand-authored feuds, debts, romances,
//                                      pacts and secrets AMONG those 42, each
//                                      with its own gossip table in barks.json.
//   content/raws/rumors/rumors.json    the rumor verb: for each history, which
//                                      OTHER notables may speak of it. A
//                                      history's two parties always know their
//                                      own story and are never re-declared.
//
// That last file is the whole reason the investigation works the way
// DOCKS-GAZETTEER section 5.3 says it must: "the investigation is never
// persuasion -- it is knowing WHERE to ask." A topic is not unlocked by a
// dice roll against a persuasion skill. It is unlocked by standing in front of
// somebody the authored knowledge domain says can tell you.
//
// NOTHING HERE INVENTS CONTENT. Every string is read from the owner's raws;
// this file only indexes them. It is read-only and never writes.
//
// NO FLOATS. Sorted vectors and binary search, never a hash map.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace granadad::sim {

/// One skill a notable's bio already claims, at the level the raws seed.
struct NotableSkill {
    std::string skill;
    std::int32_t level = 0;
};

/// One of the Forty (now forty-two: the vanished-clerk pass added Widow Sedge).
struct Notable {
    std::string id;
    std::string name;
    std::string epithet;
    /// An actors/*.json raw id: shopkeeper, militia_watch, wastrel, ...
    std::string type;
    /// The authored map site they are bound to at bake.
    std::string site;
    std::string bio;
    /// Ascending by skill id.
    std::vector<NotableSkill> skills;

    /// The best skill this notable has, or an empty view. What they will talk
    /// shop about.
    [[nodiscard]] std::string_view bestSkill() const noexcept;
    [[nodiscard]] std::int32_t bestSkillLevel() const noexcept;
    [[nodiscard]] std::int32_t skillLevel(std::string_view id) const noexcept;
};

/// One authored micro-history between two notables.
struct History {
    std::string id;
    /// feud | debt | romance | pact | secret.
    std::string kind;
    std::string a;
    std::string b;
    /// The RelationshipKind the bake realizes: RIVAL, DEBTOR, ROMANCE, FRIEND.
    std::string edge;
    /// The barks.json table that tells it. Asserted to exist.
    std::string gossipKey;
    /// The one-line addendum each side's bio carries.
    std::string bioA;
    std::string bioB;

    [[nodiscard]] bool involves(std::string_view notableId) const noexcept {
        return a == notableId || b == notableId;
    }
};

/// Who, beyond the two parties, may gossip a history.
struct RumorDomain {
    std::string historyId;
    /// Ascending, deduplicated.
    std::vector<std::string> knowers;
};

/// Every authored notable, history and rumor domain, indexed for lookup.
class NotableRegistry {
public:
    /// Reads all three files. NEVER throws -- a missing file leaves that part
    /// empty and every query returns nothing, so the game boots with silent
    /// notables rather than not at all.
    [[nodiscard]] static NotableRegistry load(const std::filesystem::path& contentDir);

    [[nodiscard]] bool loaded() const noexcept { return !notables_.empty(); }
    [[nodiscard]] const std::vector<Notable>& notables() const noexcept { return notables_; }
    [[nodiscard]] const std::vector<History>& histories() const noexcept { return histories_; }
    [[nodiscard]] const std::vector<RumorDomain>& domains() const noexcept { return domains_; }

    [[nodiscard]] const Notable* find(std::string_view id) const noexcept;
    [[nodiscard]] const History* history(std::string_view id) const noexcept;

    /// Every history `notableId` may tell: the ones they are PARTY to first
    /// (their own story, which they always know), then the ones a rumor domain
    /// licenses them to repeat. Ascending by history id within each group, so
    /// the topic list a player sees is stable.
    [[nodiscard]] std::vector<const History*> tellableBy(std::string_view notableId) const;

    /// True when the domain file names `notableId` as a knower of `historyId`.
    /// Being a PARTY does not count -- that is the implicit rule the raws note.
    [[nodiscard]] bool isKnower(std::string_view historyId,
                                std::string_view notableId) const noexcept;

private:
    /// All three sorted by id. Never reordered after load.
    std::vector<Notable> notables_;
    std::vector<History> histories_;
    std::vector<RumorDomain> domains_;
};

[[nodiscard]] std::filesystem::path notableRawsPath(const std::filesystem::path& contentDir);
[[nodiscard]] std::filesystem::path historyRawsPath(const std::filesystem::path& contentDir);
[[nodiscard]] std::filesystem::path rumorRawsPath(const std::filesystem::path& contentDir);

}  // namespace granadad::sim
