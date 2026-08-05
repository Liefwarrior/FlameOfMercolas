// AND WHETHER THE WARD CAN BE SPOKEN TO.
//
// #78 put six hundred and sixty-one people in the Docks. `E` reached fourteen
// of them, all of them inside one building, and the other six hundred and
// forty-seven were scenery you could walk through the middle of. These cases
// are the proof that they are not any more, and each of them is written so that
// it can go red for one reason.
//
// WHAT IS ASSERTED, and why each one is a claim worth a case:
//
//   THE FORTY ARE HERE          the roster has been claiming a keeper for K01,
//                               K04, K05 ... since #78 and the owner's raws
//                               have been naming that keeper since before that.
//                               If the binding rots, the district's
//                               harbourmaster silently becomes an anonymous
//                               shopkeeper again and every personal table, every
//                               micro-history and the whole rumor domain go back
//                               to reaching one building.
//   NOBODY IS NAMED FROM        every name a player reads is a row of
//   NOWHERE                     content/raws/names/names.json or of
//                               notables.json. A case that only checked "the
//                               name is not empty" would pass against a name
//                               this code made up.
//   THEY DO NOT SOUND ALIKE     the quality bar, stated as a test: a dockhand,
//                               a watchman and a priest greet you with three
//                               different sentences out of three different
//                               authored tables. Sampling one is not evidence.
//   THE STATE IS AUDIBLE        a starving body says so. This is the one thing
//                               210 authored tables could not cover, because
//                               hunger is a number this simulation owns.
//   A CAT IS NOT ASKED ABOUT    greet.beast is authored and a beast answering
//   THE VANISHED CLERK          is right; a beast being offered PICK THEIR
//                               POCKET is a menu built by a machine that was
//                               not looking at what it was talking to.

#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <set>
#include <string>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/engine.hpp"
#include "granadad/sim/ward_actors.hpp"
#include "granadad/sim/ward_voice.hpp"

using namespace granadad;

namespace {

constexpr std::uint64_t kSeed = 0x4752414E41444144ull;  // "GRANADAD"

struct WardRun {
    content::World world;
    sim::TileQuery tiles;
    sim::PhasedEngine engine;
    sim::WardPopulation* people = nullptr;

    explicit WardRun(std::int32_t startHour)
        : world(content::loadWorldFile(content::bakedMap(sim::docks::kWorldName))),
          tiles(world),
          engine(kSeed, world) {
        auto owned = std::make_unique<sim::WardPopulation>(
            tiles, sim::hourOfDay(startHour), kSeed, content::contentDir());
        people = owned.get();
        engine.register_system(std::move(owned));
        engine.boot();
    }
};

/// The first living body of a type, or nullptr. What "find me a watchman" is.
[[nodiscard]] const sim::WardActor* firstOfType(const sim::WardPopulation& people,
                                                sim::WardType type) {
    for (const sim::WardActor& actor : people.actors()) {
        if (actor.type == type && !actor.dead) {
            return &actor;
        }
    }
    return nullptr;
}

/// The same body, fed, rested and standing still.
///
/// A CASE ABOUT ONE THING AT A TIME. The mood a body is in outranks its
/// greeting -- deliberately, and there is a case below that proves it -- so a
/// case about job families or time bands has to hold the state still, or it is
/// silently a case about whoever happened to be hungry at the hour it ran.
[[nodiscard]] sim::WardActor settled(const sim::WardActor& actor) {
    sim::WardActor out = actor;
    for (std::size_t n = 0; n < sim::kNeedCount; ++n) {
        out.needs[n] = static_cast<std::int16_t>(sim::kNeedMax);
    }
    out.policy = sim::WardPolicy::Loiter;
    out.dead = false;
    return out;
}

}  // namespace

// ===========================================================================
// THE FORTY ARE ALREADY STANDING HERE
// ===========================================================================

TEST_CASE("the owner's notables are the bodies keeping their own authored sites") {
    WardRun run(8);
    const sim::NotableRegistry notables = sim::NotableRegistry::load(content::contentDir());
    REQUIRE(notables.loaded());

    // Collected by id, so the claim is about WHICH of the Forty are here and
    // not merely how many rows got a string in them.
    std::set<std::string> bound;
    for (const sim::WardIdentity& who : run.people->identities()) {
        if (!who.notableId.empty()) {
            // Every binding resolves. A typo in the keeper table drops the
            // binding at bake rather than leaving a body claiming a personal
            // table it has no right to -- see WardPopulation::bakeIdentities.
            INFO("bound notable id: ", who.notableId);
            CHECK(notables.find(who.notableId) != nullptr);
            bound.insert(who.notableId);
        }
    }

    // The count, and then the ones that matter by name. A count alone would
    // survive twenty-nine bodies all being bound to the same notable.
    CHECK(bound.size() >= 28);
    for (const char* id : {"crell", "sethra", "maell", "onna", "redda", "harl", "hemp", "brann",
                           "dagny", "fenner", "cull", "vess", "brakk", "weyland", "gilt",
                           "withy", "wake", "haddie"}) {
        INFO("expected in the ward: ", id);
        CHECK(bound.count(id) == 1);
    }

    // AND EXACTLY ONE OF EACH. Two bodies bound to Ottavan Crell would put two
    // harbourmasters in one district, which is the failure a set alone hides.
    for (const std::string& id : bound) {
        std::int32_t copies = 0;
        for (const sim::WardIdentity& who : run.people->identities()) {
            copies += who.notableId == id ? 1 : 0;
        }
        INFO("notable: ", id);
        CHECK(copies == 1);
    }

    // AND NOT THE GULL'S. Master Venn keeps K03 and Finch keeps its snug; both
    // are the Tavern's people, and naming a second one out here would put two
    // of each in the district.
    CHECK(bound.count("venn") == 0);
    CHECK(bound.count("finch") == 0);
}

// ===========================================================================
// NOBODY IS NAMED FROM NOWHERE
// ===========================================================================

TEST_CASE("every name in the ward is a row of the owner's own raws") {
    WardRun run(8);
    const sim::NameRaws names = sim::NameRaws::load(content::contentDir());
    const sim::NotableRegistry notables = sim::NotableRegistry::load(content::contentDir());
    REQUIRE(names.loaded());
    // Seven given-name pools and seven epithet pools, which is what
    // content/raws/names/names.json actually holds. A loader that silently read
    // none of them would otherwise let every case below pass against fallbacks.
    CHECK(names.givenPools() == 7);
    CHECK(names.epithetPools() == 7);
    CHECK(names.surnames().size() == 96);
    CHECK(names.kennel().size() == 20);

    const auto isAuthored = [&](const std::string& word, std::string_view group) {
        // A given name, a surname, or a kennel name. Checked by walking the
        // pools rather than by asking the loader, so the case would still catch
        // a lookup that returned something the file does not contain.
        for (std::int32_t i = 0; i < 200; ++i) {
            if (names.given(group, i) == word) {
                return true;
            }
        }
        for (const std::string& row : names.surnames()) {
            if (row == word) {
                return true;
            }
        }
        for (const std::string& row : names.kennel()) {
            if (row == word) {
                return true;
            }
        }
        return false;
    };

    std::int32_t checked = 0;
    std::int32_t withEpithet = 0;
    std::int32_t kennelled = 0;
    for (const sim::WardActor& actor : run.people->actors()) {
        const sim::WardIdentity& who = run.people->identity(actor.id);
        INFO("actor ", actor.id, " (", sim::wardTypeName(actor.type), ") is called '", who.name,
             "'");
        REQUIRE_FALSE(who.name.empty());
        if (!who.notableId.empty()) {
            const sim::Notable* notable = notables.find(who.notableId);
            REQUIRE(notable != nullptr);
            CHECK(who.name == notable->name);
            CHECK(who.epithet == notable->epithet);
            continue;
        }
        if (!sim::isPerson(actor.type)) {
            // A DOG HAS A NAME AND A MOUSE DOES NOT, which is a fact about this
            // district and not an oversight. The owner authored a twenty-name
            // kennel pool -- Grip, Tar, Bell, Nettle -- for the animals the
            // ward keeps; nobody on this quay has ever called a mouse anything,
            // so it answers to its own noun and to nothing else.
            if (actor.type == sim::WardType::Mouse) {
                CHECK(who.name == sim::wardTypeName(actor.type));
            } else {
                bool inKennel = false;
                for (const std::string& row : names.kennel()) {
                    inKennel = inKennel || row == who.name;
                }
                CHECK(inKennel);
                ++kennelled;
            }
            CHECK(who.epithet.empty());
            continue;
        }
        withEpithet += who.epithet.empty() ? 0 : 1;
        // Split on the single space a surname is joined with, and check both
        // halves against the pools they must have come from.
        const std::size_t space = who.name.find(' ');
        const std::string given = who.name.substr(0, space);
        CHECK(isAuthored(given, sim::wardTypeRawsId(actor.type)));
        if (space != std::string::npos) {
            CHECK(isAuthored(who.name.substr(space + 1), sim::wardTypeRawsId(actor.type)));
        }
        ++checked;
    }
    CHECK(checked > 500);
    // The cats, the dogs and the quay's strays. Not the mice.
    CHECK(kennelled > 10);
    // Most of the ward carries a by-name. Nobody has to, and the pools are only
    // eight to twenty-four deep, so this is a claim that they are USED rather
    // than a claim about how many.
    CHECK(withEpithet > 400);
}

TEST_CASE("the ward's poor go by one name, and its trades carry two") {
    WardRun run(8);
    std::int32_t wastrelsWithSurnames = 0;
    std::int32_t wastrels = 0;
    std::int32_t serfsWithSurnames = 0;
    std::int32_t serfs = 0;
    for (const sim::WardActor& actor : run.people->actors()) {
        const sim::WardIdentity& who = run.people->identity(actor.id);
        if (!who.notableId.empty()) {
            continue;
        }
        const bool twoNames = who.name.find(' ') != std::string::npos;
        if (actor.type == sim::WardType::Wastrel || actor.type == sim::WardType::Urchin ||
            actor.type == sim::WardType::Thief) {
            ++wastrels;
            wastrelsWithSurnames += twoNames ? 1 : 0;
        }
        if (actor.type == sim::WardType::Serf) {
            ++serfs;
            serfsWithSurnames += twoNames ? 1 : 0;
        }
    }
    REQUIRE(wastrels > 20);
    REQUIRE(serfs > 100);
    // A surname is a thing you have if the ward keeps track of you. The owner's
    // own wastrel pool is written that way -- Sniv, Tatter, Moll, Grib -- and
    // "Sniv Coldquay" would be this build inventing a class of person the raws
    // did not author.
    CHECK(wastrelsWithSurnames == 0);
    CHECK(serfsWithSurnames == serfs);
}

// ===========================================================================
// THEY DO NOT SOUND ALIKE
// ===========================================================================

TEST_CASE("a dockhand, a watchman and a priest greet you with three different sentences") {
    WardRun run(8);
    const sim::DialogueDirector loaded =
        sim::DialogueDirector::load(content::contentDir());
    REQUIRE(loaded.barks().loaded());

    struct Case {
        sim::WardType type;
        sim::JobFamily family;
    };
    const Case wanted[] = {
        {sim::WardType::Serf, sim::JobFamily::Serf},
        {sim::WardType::MilitiaWatch, sim::JobFamily::Watch},
        {sim::WardType::PriestOfTheFlame, sim::JobFamily::Clergy},
        {sim::WardType::Shopkeeper, sim::JobFamily::Trade},
        {sim::WardType::Fisher, sim::JobFamily::Maritime},
        {sim::WardType::Wastrel, sim::JobFamily::Wastrel},
        {sim::WardType::AnimalKeeper, sim::JobFamily::Husbandry},
    };

    std::set<std::string> keys;
    std::set<std::string> lines;
    for (const Case& want : wanted) {
        const sim::WardActor* actor = firstOfType(*run.people, want.type);
        INFO("no ", sim::wardTypeName(want.type), " in the ward at all");
        REQUIRE(actor != nullptr);
        CHECK(sim::wardJobFamily(want.type) == want.family);

        sim::DialogueDirector talk = sim::DialogueDirector::load(content::contentDir());
        const sim::WardActor calm = settled(*actor);
        const sim::Speaker speaker =
            sim::wardSpeakerFor(calm, run.people->identity(actor->id), talk.notables(),
                                talk.factions(), talk.barks());
        REQUIRE(talk.open(speaker, sim::hourOfDay(8)));
        INFO(sim::wardTypeName(want.type), " opened with key '", talk.greetingKey(), "' saying '",
             talk.greeting(), "'");
        // It came out of the AUTHORED tables for that family, and not out of
        // the "..." the director falls back to when the raws are missing.
        CHECK(talk.greetingKey().find(std::string(sim::jobFamilyKey(want.family))) !=
              std::string::npos);
        CHECK(talk.greeting() != "...");
        CHECK(talk.greeting().size() > 8);
        keys.insert(talk.greetingKey());
        lines.insert(talk.greeting());
    }
    // Seven trades, seven different authored tables, seven different sentences.
    // Sampling two would have passed against a build that had collapsed five of
    // them into one.
    CHECK(keys.size() == 7);
    CHECK(lines.size() == 7);
}

TEST_CASE("the same trade at four in the morning is not the same trade at noon") {
    WardRun run(8);
    const sim::WardActor* watchman = firstOfType(*run.people, sim::WardType::MilitiaWatch);
    REQUIRE(watchman != nullptr);

    const sim::WardActor calm = settled(*watchman);
    const auto greetAt = [&](std::int32_t hour) {
        sim::DialogueDirector talk = sim::DialogueDirector::load(content::contentDir());
        const sim::Speaker speaker =
            sim::wardSpeakerFor(calm, run.people->identity(watchman->id), talk.notables(),
                                talk.factions(), talk.barks());
        REQUIRE(talk.open(speaker, sim::hourOfDay(hour)));
        return talk.greetingKey();
    };
    // The owner authored greet.<family>.neutral.<band> for all four bands of
    // all nine families. Nothing in this build had a way to ASK for them until
    // #79, and a watchman who says the same thing at four in the morning as at
    // noon is that gap, still open.
    CHECK(greetAt(7) != greetAt(13));
    CHECK(greetAt(13) != greetAt(20));
    CHECK(greetAt(20) != greetAt(3));
    CHECK(greetAt(3).find("night") != std::string::npos);
}

// ===========================================================================
// THE STATE IS AUDIBLE
// ===========================================================================

TEST_CASE("a starving body says so, and says it in its own trade's voice") {
    WardRun run(8);
    const sim::BarkTables barks = sim::BarkTables::load(content::contentDir());
    REQUIRE(barks.loaded());

    const sim::WardActor* hand = firstOfType(*run.people, sim::WardType::Serf);
    REQUIRE(hand != nullptr);

    // The ward's own numbers, driven straight rather than starved for three
    // days: this is a case about what a STATE sounds like, and reaching it by
    // simulating seventy-two hours would make it a case about the food economy.
    sim::WardActor fed = *hand;
    sim::WardActor hungry = *hand;
    sim::WardActor starving = *hand;
    sim::WardActor tired = *hand;
    fed.needs[static_cast<std::size_t>(sim::Need::Hunger)] = sim::kNeedMax;
    fed.needs[static_cast<std::size_t>(sim::Need::Rest)] = sim::kNeedMax;
    // Standing at a post rather than walking home, so the baseline is a body
    // with nothing on its mind and the greeting tables get to speak.
    fed.policy = sim::WardPolicy::Loiter;
    fed.dead = false;
    hungry = fed;
    hungry.needs[static_cast<std::size_t>(sim::Need::Hunger)] = sim::kNeedLow - 1;
    starving = fed;
    starving.needs[static_cast<std::size_t>(sim::Need::Hunger)] = sim::kNeedCritical - 1;
    tired = fed;
    tired.needs[static_cast<std::size_t>(sim::Need::Rest)] = sim::kNeedLow - 1;

    CHECK(sim::wardMoodKey(fed, sim::JobFamily::Serf, barks).empty());
    CHECK(sim::wardMoodKey(hungry, sim::JobFamily::Serf, barks) == "ward.hungry.serf");
    CHECK(sim::wardMoodKey(starving, sim::JobFamily::Serf, barks) == "ward.starving");
    CHECK(sim::wardMoodKey(tired, sim::JobFamily::Serf, barks) == "ward.weary.serf");

    // AND THE FAMILY DECIDES THE SENTENCE. The whole quality bar in one check:
    // hunger is one state, and a rope-hand, a watchman and a priest do not
    // report it with the same words.
    CHECK(sim::wardMoodKey(hungry, sim::JobFamily::Watch, barks) == "ward.hungry.watch");
    CHECK(sim::wardMoodKey(hungry, sim::JobFamily::Clergy, barks) == "ward.hungry.clergy");
    CHECK(sim::wardMoodKey(hungry, sim::JobFamily::Maritime, barks) == "ward.hungry.maritime");
    // AND A BEAST SAYS NOTHING ABOUT IT. The ward tracks a cat's appetite like
    // everybody else's, and every one of these tables is a sentence in English:
    // without the guard, a hungry stray greets you with a remark about bread.
    CHECK(sim::wardMoodKey(hungry, sim::JobFamily::Beast, barks).empty());
    CHECK(sim::wardMoodKey(starving, sim::JobFamily::Beast, barks).empty());

    // ORDER MATTERS AND IS PART OF THE CLAIM. Starving outranks weary: a man
    // who has not eaten in days and has not slept talks about the food.
    sim::WardActor both = starving;
    both.needs[static_cast<std::size_t>(sim::Need::Rest)] = sim::kNeedLow - 1;
    CHECK(sim::wardMoodKey(both, sim::JobFamily::Serf, barks) == "ward.starving");
    // And running for your life outranks all of it.
    both.policy = sim::WardPolicy::Flee;
    CHECK(sim::wardMoodKey(both, sim::JobFamily::Serf, barks) == "mood.panicked");
    // The dead do not greet anybody, and that table is the owner's own.
    both.dead = true;
    CHECK(sim::wardMoodKey(both, sim::JobFamily::Serf, barks) == "mood.dead");
}

TEST_CASE("the mood a body is in replaces the greeting it would have given") {
    WardRun run(8);
    const sim::WardActor* hand = firstOfType(*run.people, sim::WardType::Serf);
    REQUIRE(hand != nullptr);

    const auto openWith = [&](const sim::WardActor& actor) {
        sim::DialogueDirector talk = sim::DialogueDirector::load(content::contentDir());
        const sim::Speaker speaker = sim::wardSpeakerFor(
            actor, run.people->identity(actor.id), talk.notables(), talk.factions(), talk.barks());
        REQUIRE(talk.open(speaker, sim::hourOfDay(13)));
        return talk.greetingKey();
    };

    sim::WardActor fed = *hand;
    fed.needs[static_cast<std::size_t>(sim::Need::Hunger)] = sim::kNeedMax;
    fed.needs[static_cast<std::size_t>(sim::Need::Rest)] = sim::kNeedMax;
    fed.policy = sim::WardPolicy::Loiter;
    sim::WardActor starving = fed;
    starving.needs[static_cast<std::size_t>(sim::Need::Hunger)] = 0;

    CHECK(openWith(fed).find("greet.serf") != std::string::npos);
    CHECK(openWith(starving) == "ward.starving");
}

// ===========================================================================
// WHAT HAS NO WORDS GETS NO LIST
// ===========================================================================

TEST_CASE("a cat answers, and is not asked about the vanished clerk") {
    WardRun run(8);
    const sim::WardActor* cat = firstOfType(*run.people, sim::WardType::Cat);
    REQUIRE(cat != nullptr);

    sim::DialogueDirector talk = sim::DialogueDirector::load(content::contentDir());
    const sim::Speaker speaker = sim::wardSpeakerFor(
        *cat, run.people->identity(cat->id), talk.notables(), talk.factions(), talk.barks());
    CHECK(speaker.beast);
    CHECK(speaker.family == sim::JobFamily::Beast);
    REQUIRE(talk.open(speaker, sim::hourOfDay(13)));

    // It answered, out of the owner's own greet.beast, which is three stage
    // directions in brackets and is exactly right.
    CHECK(talk.greetingKey().find("beast") != std::string::npos);
    CHECK(talk.greeting() != "...");
    // And the list is the way out and nothing else. Before this, every topic
    // the director builds was offered to it: a cat with an opinion about the
    // harbourmaster's ledger, and a purse to pick.
    REQUIRE(talk.topics().size() == 1);
    CHECK(talk.topics()[0].kind == sim::TopicKind::Leave);

    // A PERSON standing in the same district gets the whole list, so the case
    // above is about the beast and not about an empty topic builder.
    const sim::WardActor* hand = firstOfType(*run.people, sim::WardType::Serf);
    REQUIRE(hand != nullptr);
    sim::DialogueDirector second = sim::DialogueDirector::load(content::contentDir());
    const sim::Speaker person = sim::wardSpeakerFor(
        *hand, run.people->identity(hand->id), second.notables(), second.factions(),
        second.barks());
    REQUIRE(second.open(person, sim::hourOfDay(13)));
    CHECK(second.topics().size() > 3);
}

// ===========================================================================
// WHAT A WARD SPEAKER IS
// ===========================================================================

TEST_CASE("a ward speaker cannot be confused with one of the Gull's") {
    WardRun run(8);
    const sim::WardActor* hand = firstOfType(*run.people, sim::WardType::Serf);
    REQUIRE(hand != nullptr);

    sim::DialogueDirector talk = sim::DialogueDirector::load(content::contentDir());
    const sim::Speaker speaker = sim::wardSpeakerFor(
        *hand, run.people->identity(hand->id), talk.notables(), talk.factions(), talk.barks());

    // THE LEDGER IS KEYED BY THIS NUMBER AND THE GULL OWNS 1..17. Handed over
    // unchanged, ward actor 7 and the Gull's bouncer would share one row of
    // standing: robbing a dockhand would make the bouncer hate you.
    CHECK(speaker.actorId >= sim::kWardSpeakerIdBase);
    CHECK(speaker.actorId == sim::kWardSpeakerIdBase + hand->id);

    // Nobody in the ward keeps a counter, recruits, teaches or fences. Those
    // are jobs somebody has, and the bodies who have them are in the Gull.
    CHECK_FALSE(speaker.trades);
    CHECK(speaker.recruitsFor.empty());
    CHECK_FALSE(speaker.teaches);
    CHECK_FALSE(speaker.buysStolen);
    // But the guilds do claim them, out of the owner's own factions.json, by
    // the job family they present as.
    CHECK_FALSE(speaker.factionId.empty());
    // And their purse is the coin the simulation says they are carrying, not a
    // number invented for the topic list.
    CHECK(speaker.purse == hand->coin);
}

TEST_CASE("the ward's trades talk shop about their own trade") {
    WardRun run(8);
    struct Want {
        sim::WardType type;
        const char* skill;
    };
    const Want wanted[] = {
        {sim::WardType::Fisher, "fishing"},
        {sim::WardType::Sailor, "seacraft"},
        {sim::WardType::MilitiaWatch, "kit_keeping"},
        {sim::WardType::PriestOfTheFlame, "channeling"},
        {sim::WardType::Shopkeeper, "streetwise"},
        {sim::WardType::Serf, "fieldcraft"},
    };
    for (const Want& want : wanted) {
        INFO("trade: ", sim::wardTypeName(want.type));
        CHECK(sim::wardSkillId(want.type) == want.skill);
    }

    // AND THE STANDING IS THEIR OWN. Six hundred hands all at one level would
    // give the whole ward one sentence about its work; this walks the roll and
    // requires all three authored bands to occur.
    std::int32_t novice = 0;
    std::int32_t adept = 0;
    std::int32_t master = 0;
    std::int32_t silent = 0;
    for (const sim::WardActor& actor : run.people->actors()) {
        if (!sim::isPerson(actor.type)) {
            continue;
        }
        const std::int32_t level = sim::wardTradeLevel(actor);
        CHECK(level >= 4);
        CHECK(level < 50);
        silent += level < 5 ? 1 : 0;
        novice += level >= 5 && level < 20 ? 1 : 0;
        adept += level >= 20 && level < 40 ? 1 : 0;
        master += level >= 40 ? 1 : 0;
    }
    CHECK(novice > 50);
    CHECK(adept > 50);
    CHECK(master > 20);
    CHECK(silent > 0);

    // A notable's own bio outranks it outright. Ottavan Crell reads paperwork
    // for a living and notables.json says streetwise 40; nothing here gets to
    // guess a different number for him.
    const sim::NotableRegistry notables = sim::NotableRegistry::load(content::contentDir());
    const sim::Notable* crell = notables.find("crell");
    REQUIRE(crell != nullptr);
    for (const sim::WardActor& actor : run.people->actors()) {
        if (run.people->identity(actor.id).notableId != "crell") {
            continue;
        }
        sim::DialogueDirector talk = sim::DialogueDirector::load(content::contentDir());
        const sim::Speaker speaker =
            sim::wardSpeakerFor(actor, run.people->identity(actor.id), talk.notables(),
                                talk.factions(), talk.barks());
        CHECK(speaker.skillId == crell->bestSkill());
        CHECK(speaker.skillLevel == crell->bestSkillLevel());
        // And he brings his own table and his own stories with him.
        REQUIRE(talk.open(speaker, sim::hourOfDay(13)));
        bool personal = false;
        bool history = false;
        for (const sim::Topic& topic : talk.topics()) {
            personal = personal || topic.barkKey == "personal.crell";
            history = history || topic.kind == sim::TopicKind::History;
        }
        CHECK(personal);
        CHECK(history);
    }
}

// ===========================================================================
// AND THE KEY ACTUALLY REACHES THEM
// ===========================================================================

TEST_CASE("pressing the talk key on a street corner reaches the body standing on it") {
    // THE WHOLE POINT, DRIVEN THROUGH THE SAME CALL A KEYPRESS MAKES. Every
    // case above tests a part; this one stands the player next to somebody in
    // the open district and presses the key.
    render::SessionConfig config;
    config.contentDir = content::contentDir();
    config.timeOfDay = 13 * 3600;
    render::Session session(config);

    // Find somebody out in the ward and stand on the tile beside them, WELL
    // CLEAR OF THE GULL. The roster refuses to put a ward body inside K03, but
    // the taproom's roster is asked first and its reach is two tiles: a
    // candidate on the pavement outside the door would make this case pass by
    // talking to a bouncer.
    const sim::WardActor* target = nullptr;
    for (const sim::WardActor& actor : session.people().actors()) {
        const bool nearTheGull = actor.x >= sim::gull::kFootprintX0 - 4 &&
                                 actor.x <= sim::gull::kFootprintX1 + 4 &&
                                 actor.y >= sim::gull::kFootprintY0 - 6 &&
                                 actor.y <= sim::gull::kFootprintY1 + 4;
        if (!actor.dead && sim::isPerson(actor.type) && !nearTheGull &&
            actor.band == sim::docks::kBandQuayside &&
            session.tiles().standable(actor.x + 1, actor.y, actor.band)) {
            target = &actor;
            break;
        }
    }
    REQUIRE(target != nullptr);
    const std::int32_t wanted = target->id;
    session.body().placeAt(target->x + 1, target->y, target->band);
    // One movement step, so the room's own idea of where the player is standing
    // catches up with the body. Sixty of these make a simulated second, so
    // nobody in the ward has moved.
    session.stepMany(sim::MoveInput{}, 1);

    CHECK_FALSE(session.talking());
    session.interact();
    INFO("last message: ", session.lastMessage());
    REQUIRE(session.talking());
    // The body that answered is the body that was standing there.
    CHECK(session.wardTalkingTo() == wanted);
    const sim::DialogueDirector& talk = session.tavern().dialogue();
    CHECK(talk.speaker().name == session.people().identity(wanted).name);
    CHECK_FALSE(talk.speaker().name.empty());
    // The message line names them and repeats what they said, exactly as it
    // does for the fourteen in the taproom.
    CHECK(session.lastMessage().find(talk.speaker().name) != std::string::npos);

    // And it closes on the same key it always did.
    session.closeConversation();
    CHECK_FALSE(session.talking());
    CHECK(session.wardTalkingTo() == -1);
}

TEST_CASE("the street line reaches three trades and gets three different voices") {
    // THE CAPTURE FLAG, ASSERTED. `--skyrun` landed 2 of its 9 beats for a
    // whole sprint with nothing in the suite to notice, and every scripted line
    // has had a case since. This is the one that stops `--street` quietly
    // photographing a body it never got hold of.
    struct Want {
        const char* who;
        const char* family;
    };
    const Want wanted[] = {
        {"hand", "serf"},
        {"watch", "watch"},
        {"priest", "clergy"},
        {"keeper", "trade"},
    };

    std::set<std::string> keys;
    std::set<std::string> lines;
    for (const Want& want : wanted) {
        render::SessionConfig config;
        config.contentDir = content::contentDir();
        config.timeOfDay = 13 * 3600;
        render::Session session(config);

        const render::StreetLineResult said = render::runStreetLine(session, want.who, 0);
        INFO("--street=", want.who, " found=", said.found, " opened=", said.opened, " speaker='",
             said.name, "' key='", said.barkKey, "' line='", said.line, "'");
        CHECK(said.found);
        REQUIRE(said.opened);
        CHECK(session.talking());
        CHECK(session.wardTalkingTo() == said.actorId);
        CHECK_FALSE(said.name.empty());
        // Out of that trade's own authored family table, and not out of the
        // "..." the director falls back to when the raws are missing.
        CHECK(said.barkKey.find(want.family) != std::string::npos);
        CHECK(said.line != "...");
        keys.insert(said.barkKey);
        lines.insert(said.line);
    }
    CHECK(keys.size() == 4);
    CHECK(lines.size() == 4);
}

TEST_CASE("a hand in a ward purse takes the coin off a real body") {
    render::SessionConfig config;
    config.contentDir = content::contentDir();
    config.timeOfDay = 13 * 3600;
    render::Session session(config);

    const sim::WardActor* target = nullptr;
    for (const sim::WardActor& actor : session.people().actors()) {
        const bool nearTheGull = actor.x >= sim::gull::kFootprintX0 - 4 &&
                                 actor.x <= sim::gull::kFootprintX1 + 4 &&
                                 actor.y >= sim::gull::kFootprintY0 - 6 &&
                                 actor.y <= sim::gull::kFootprintY1 + 4;
        if (!actor.dead && sim::isPerson(actor.type) && actor.coin > 0 && !nearTheGull &&
            actor.band == sim::docks::kBandQuayside &&
            session.tiles().standable(actor.x + 1, actor.y, actor.band)) {
            target = &actor;
            break;
        }
    }
    REQUIRE(target != nullptr);
    const std::int32_t wanted = target->id;
    const std::int32_t purseBefore = target->coin;
    session.body().placeAt(target->x + 1, target->y, target->band);
    session.stepMany(sim::MoveInput{}, 1);
    session.interact();
    REQUIRE(session.talking());
    REQUIRE(session.wardTalkingTo() == wanted);

    std::size_t lift = 0;
    bool found = false;
    const std::vector<sim::Topic>& topics = session.tavern().dialogue().topics();
    for (std::size_t i = 0; i < topics.size(); ++i) {
        if (topics[i].kind == sim::TopicKind::PickPocket) {
            lift = i;
            found = true;
        }
    }
    REQUIRE(found);

    const std::int32_t coinBefore = session.tavern().playerCoin();
    const std::int32_t heatBefore = session.tavern().dialogue().crimes().heat();
    const sim::WardLedger ledgerBefore = session.people().ledger();
    session.chooseTopic(lift);

    const sim::WardActor* after = session.people().byId(wanted);
    REQUIRE(after != nullptr);
    // Either it landed and the coin MOVED, or it did not and the ward heard
    // about it. What must never happen is coin appearing in the player's purse
    // that no purse in the district lost.
    const std::int32_t gained = session.tavern().playerCoin() - coinBefore;
    const std::int32_t lost = purseBefore - after->coin;
    INFO("gained ", gained, ", the ward lost ", lost);
    CHECK(gained == lost);
    // And the act reached the one call site every criminal act goes through,
    // wherever it happened.
    CHECK(session.tavern().dialogue().crimes().tally(sim::Crime::Lift) > 0);
    CHECK(session.tavern().dialogue().crimes().heat() >= heatBefore);

    // THE WARD'S OWN LEDGER STILL BALANCES. `coinMinted - coinSunk` must equal
    // what the ward's purses hold, at every tick, and the player's purse is not
    // one of the ward's. Coin that crossed that boundary is coin the ward has
    // to have recorded leaving, or the first pickpocket in the district breaks
    // the identity the food economy's whole balance number rests on.
    const sim::WardLedger& ledgerAfter = session.people().ledger();
    std::int64_t purses = 0;
    for (const sim::WardActor& actor : session.people().actors()) {
        purses += actor.coin;
    }
    CHECK(ledgerAfter.coinMinted - ledgerAfter.coinSunk == purses);
    CHECK(ledgerAfter.coinSunk - ledgerBefore.coinSunk == lost);
}
