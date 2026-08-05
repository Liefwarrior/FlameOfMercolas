#pragma once

// THE WARD CAN BE SPOKEN TO.
//
// What this closes. #78 put six hundred and sixty-one people in the Docks with
// needs, jobs, hours and a bed each -- and `E` reached fourteen of them, all of
// them inside one building. Six hundred and forty-seven bodies you could walk
// through the middle of and not address. A city you cannot talk to is a
// diorama.
//
// WHAT THIS FILE IS NOT. It is not a second dialogue system. sim/dialogue.hpp
// already runs a conversation from a flat `Speaker` and knows nothing about a
// tavern, a world or an Actor -- that separation is the whole reason it can be
// reused, and it says so in its own header. This file only builds the Speaker:
// it answers "who is this body, in the terms the director already understands"
// and hands it over. Every spoken line still comes out of the owner's raws
// through the same fallback chain the Gilded Gull's fourteen use.
//
// WHERE A WARD VOICE COMES FROM, in the order the raws are consulted:
//
//   content/raws/names/notables.json   twenty-nine of the Forty are ALREADY in
//                                      the ward -- the roster claims a keeper
//                                      for K01, K04, K05, K06 ... at the map's
//                                      own authored anchors, and those keepers
//                                      are Crell, Redda, Sethra, Harl and the
//                                      rest by name. They get personal.<id>,
//                                      their own micro-histories, and whatever
//                                      rumors.json licenses them to repeat.
//   content/raws/names/names.json      everybody else. Seven given-name pools
//                                      keyed by the same actors/*.json group id
//                                      the ward already reads its needs from,
//                                      ninety-six surnames, and an epithet pool
//                                      per group. Nobody is named out of
//                                      nowhere and nothing here invents a name.
//   content/raws/barks/barks.json      the voice. Nine job families x six
//                                      attitudes x four time bands, already
//                                      authored, already covering exactly the
//                                      distinctions that matter: a dockhand at
//                                      dawn, a watchman at four in the morning
//                                      and a priest at any hour are three
//                                      different sentences and always were.
//                                      This build never had a way to ASK for
//                                      them.
//   content/raws/barks/ward_barks.json what the ward's own state sounds like --
//                                      hunger, exhaustion, and the walk home.
//                                      ADDED, not edited: barks.json is the
//                                      owner's canon and is never touched. See
//                                      barkRawsFiles() for the seam.
//
// NO FLOATS, NO DRAWS. Which name a body has and which authored row it speaks
// are pure functions of its id. Nothing here touches an RNG stream, so talking
// to somebody cannot shift the simulation's draw sequence -- the same rule
// dialogue.hpp already holds itself to, for the same reason.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/sim/barks.hpp"
#include "granadad/sim/dialogue.hpp"
#include "granadad/sim/faction.hpp"
#include "granadad/sim/notables.hpp"
#include "granadad/sim/ward_actors.hpp"

namespace granadad::sim {

// ---------------------------------------------------------------------------
// the authored name pools
// ---------------------------------------------------------------------------

/// content/raws/names/names.json, read and indexed.
///
/// The pools are keyed by the SAME group id content/raws/actors is keyed by --
/// serf, wastrel, shopkeeper, militia_watch, priest_of_the_flame,
/// disciple_of_the_flame, animal_keeper -- which is why a ward type can find
/// its own names with wardTypeRawsId() and no second mapping table.
class NameRaws {
public:
    /// NEVER throws. A missing or malformed file leaves every pool empty and
    /// the ward goes by trade alone, exactly like every other raws loader here:
    /// a content file being edited must not stop the game booting.
    [[nodiscard]] static NameRaws load(const std::filesystem::path& contentDir);

    [[nodiscard]] bool loaded() const noexcept { return !surnames_.empty(); }
    /// How many given-name pools were read. Pinned by a case, because a loader
    /// that is silent about a missing pool lets a whole suite pass against
    /// nothing.
    [[nodiscard]] std::size_t givenPools() const noexcept { return given_.size(); }
    [[nodiscard]] std::size_t epithetPools() const noexcept { return epithets_.size(); }
    [[nodiscard]] const std::vector<std::string>& surnames() const noexcept {
        return surnames_;
    }
    [[nodiscard]] const std::vector<std::string>& kennel() const noexcept { return kennel_; }

    /// One row of a group's given-name pool, rotated by `index`, or empty.
    /// `index` is made non-negative first, so a negative counter cannot index
    /// off the front of a pool.
    [[nodiscard]] std::string_view given(std::string_view group,
                                         std::int32_t index) const noexcept;
    [[nodiscard]] std::string_view epithet(std::string_view group,
                                           std::int32_t index) const noexcept;
    [[nodiscard]] std::string_view surname(std::int32_t index) const noexcept;

private:
    struct Pool {
        std::string group;
        std::vector<std::string> rows;
    };
    /// Sorted by group and searched by binary search. Never a hash map -- the
    /// same rule barks.hpp holds, for the same determinism reason.
    std::vector<Pool> given_;
    std::vector<Pool> epithets_;
    std::vector<std::string> surnames_;
    std::vector<std::string> kennel_;
};

// ---------------------------------------------------------------------------
// what a ward body presents as
// ---------------------------------------------------------------------------

/// Which of the nine authored greet families this type wears in public.
///
/// PRESENTED IDENTITY, and the distinction is one this project has a ruling
/// about. A thief presents as a wastrel because that is what the ward sees
/// standing on the kerb, and wastrel.streetlife is deliberately unaffiliated in
/// the owner's factions.json -- the same reason the Gull's Skyrunner contact
/// presents as a wastrel and IS villain.skyrunner.
[[nodiscard]] JobFamily wardJobFamily(WardType type) noexcept;

/// The content/raws/skills id this type will talk shop about, or empty.
///
/// Six of the ward's trades have an authored mastery table -- fieldcraft,
/// seacraft, kit_keeping, streetwise, channeling, fishing -- and those six are
/// the ones handed out here. A type with no table is silent about its work
/// rather than borrowing somebody else's, which is what "nothing here invents
/// content" means when it is inconvenient.
[[nodiscard]] std::string_view wardSkillId(WardType type) noexcept;

/// How good this body is at its own trade.
///
/// THE WARD HAS NO SKILL TRACK YET, and this is not one. It is a stable
/// standing per body -- a pure function of the id, banded so that novice, adept
/// and master all occur -- so that the fisher on the third finger talks like
/// somebody who has done it for thirty years and the one beside him does not.
/// A notable overrides it outright with the level their own bio claims.
///
/// The honest alternative would be to give every unnamed hand the same number,
/// and that would make six hundred people say one sentence about their work.
[[nodiscard]] std::int32_t wardTradeLevel(const WardActor& actor) noexcept;

/// True when this body speaks in words. Beasts do not: greet.beast is authored
/// as three stage directions in brackets and that is the whole of what a cat
/// has to say, which is correct and is not a gap.
[[nodiscard]] constexpr bool wardSpeaks(WardType type) noexcept { return isPerson(type); }

// ---------------------------------------------------------------------------
// who is in front of you
// ---------------------------------------------------------------------------

// Who a body IS -- WardIdentity -- is declared in ward_actors.hpp beside the
// roster that bakes it, because it is a fact about the ward rather than about
// the conversation. WardPopulation::bakeIdentities is implemented in
// ward_voice.cpp, next to the name pools it reads.

/// THE ONE PLACE WARD ACTOR IDS AND TAVERN ACTOR IDS ARE KEPT APART.
///
/// SocialLedger is keyed by Speaker::actorId and the Gilded Gull's fourteen own
/// 1..17. The ward numbers its own from zero. Handed to the director unchanged,
/// ward actor 7 and the Gull's bouncer would share one row of the ledger:
/// robbing a dockhand on the Tarwalk would make the bouncer hate you, and
/// standing the bouncer a drink would warm up a stranger across the district.
/// So the ward's ids are lifted clear, once, here.
inline constexpr std::int32_t kWardSpeakerIdBase = 1000000;

/// Everything the dialogue director needs to know about a body in the ward.
///
/// `barks` is passed because the MOOD key is resolved here rather than in the
/// director: a hungry watchman and a hungry priest must not say the same
/// sentence, so the key is picked most-specific-first out of what is actually
/// authored (`ward.hungry.watch`, then `ward.hungry`) and the director is
/// handed the one that exists.
[[nodiscard]] Speaker wardSpeakerFor(const WardActor& actor, const WardIdentity& who,
                                     const NotableRegistry& notables,
                                     const FactionRegistry& factions, const BarkTables& barks);

/// The mood key this body's CURRENT STATE argues for, or empty when it is
/// having an ordinary day and the greeting tables should speak instead.
///
/// Ordered by how much it would be on somebody's mind: dead, running for their
/// life, starving, hungry, dead on their feet, walking home. Exposed rather
/// than hidden inside wardSpeakerFor so a case can assert the ORDER -- a
/// starving man who greets you with a remark about the weather is the failure
/// this exists to make impossible.
[[nodiscard]] std::string wardMoodKey(const WardActor& actor, JobFamily family,
                                      const BarkTables& barks);

}  // namespace granadad::sim
