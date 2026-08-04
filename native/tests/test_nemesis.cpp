// The nemesis: the man who put the player on the floor, and what the ward gave
// him for it.
//
// Four kinds of case, in the order the rest of this suite uses:
//
//   RAWS    content/raws/factions/chapters.json against the owner's own
//           faction registry -- the same refusal the compound roll and the
//           contract board make, for the same reason.
//   RISE    what one defeat moves. A rung off ranks.json, weight through the
//           mirror, a trade house with real members, a permanent toll on a real
//           price, and a name on the ward's roll.
//   MEMORY  that he behaves differently afterwards: a different authored
//           greeting, something heavier in his hands, and a topic that only
//           exists because he beat you.
//   ARC     the acceptance. A named labourer puts the player down in a fist
//           fight, the player gets up, and the labourer is not who he was.

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/sim/actor.hpp"
#include "granadad/sim/compound.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/engine.hpp"
#include "granadad/sim/nemesis.hpp"
#include "granadad/sim/notables.hpp"
#include "granadad/sim/player.hpp"
#include "granadad/sim/tavern.hpp"

using namespace granadad::sim;
namespace content = granadad::content;

namespace {

constexpr std::uint64_t kSeed = 0x4752414E41444144ull;

const NotableRegistry& who() {
    static const NotableRegistry loaded = NotableRegistry::load(content::contentDir());
    return loaded;
}

std::shared_ptr<const FactionRegistry> registry() {
    static const std::shared_ptr<const FactionRegistry> shared =
        std::make_shared<const FactionRegistry>(FactionRegistry::load(content::contentDir()));
    return shared;
}

/// A room with a body in it and the ward's roll under it -- the same three
/// things a Session assembles, with no renderer anywhere near them.
class Room {
public:
    explicit Room(std::int32_t timeOfDay, std::int32_t tileX, std::int32_t tileY,
                  bool withRoll = true)
        : world_(content::loadWorldFile(content::bakedMap(docks::kWorldName))),
          tiles_(std::make_unique<TileQuery>(world_)),
          engine_(std::make_unique<PhasedEngine>(kSeed, world_)),
          body_(std::make_unique<PlayerBody>(*tiles_, tileX, tileY, gull::kGroundBand,
                                             kFacingSouth)) {
        auto tavern = std::make_unique<Tavern>(*tiles_, timeOfDay, kSeed, content::contentDir());
        tavern_ = tavern.get();
        engine_->register_system(std::move(tavern));
        if (withRoll) {
            auto ward = std::make_unique<Ward>(kSeed, content::contentDir(), who());
            ward_ = ward.get();
            engine_->register_system(std::move(ward));
            tavern_->attachRoll(ward_);
        }
        engine_->boot();
        tavern_->setPlayer(body_->x(), body_->y(), body_->band());
    }

    [[nodiscard]] Tavern& tavern() noexcept { return *tavern_; }
    [[nodiscard]] Ward* ward() noexcept { return ward_; }
    [[nodiscard]] PlayerBody& body() noexcept { return *body_; }

    /// The player comes to, and comes BACK -- which is Eli's own framing of
    /// the whole feature: "next time the player returns that person might be
    /// promoted". The room gives the hit points back and moves the clock on,
    /// and the player walks in again the following evening, when the same
    /// people are on the same shifts.
    void returnAtDusk() {
        tavern_->reviveAfterDefeat();
        tavern_->skipTo(hourOfDay(20));
    }

    /// Puts the body down somewhere and tells the room about it, which is what
    /// a movement step does sixty times a second.
    void stand(std::int32_t tileX, std::int32_t tileY) {
        body_->placeAt(tileX, tileY, gull::kGroundBand);
        tavern_->setPlayer(body_->x(), body_->y(), body_->band());
    }

    void run(int seconds) {
        for (int second = 0; second < seconds; ++second) {
            for (int step = 0; step < kStepsPerSecond; ++step) {
                tavern_->setPlayer(body_->x(), body_->y(), body_->band());
                tavern_->stepMovement();
                const std::int32_t shoveX = tavern_->takePlayerShoveX();
                const std::int32_t shoveY = tavern_->takePlayerShoveY();
                if (shoveX != 0 || shoveY != 0) {
                    body_->push(shoveX, shoveY);
                }
                body_->step(MoveInput{});
            }
            tavern_->setPlayer(body_->x(), body_->y(), body_->band());
            engine_->tick();
        }
    }

private:
    content::World world_;
    std::unique_ptr<TileQuery> tiles_;
    std::unique_ptr<PhasedEngine> engine_;
    std::unique_ptr<PlayerBody> body_;
    Tavern* tavern_ = nullptr;
    Ward* ward_ = nullptr;
};

/// A body in the room, BY NAME. Every case here names who beat the player,
/// because "some actor" is not a nemesis.
[[nodiscard]] const Actor* find(const Tavern& tavern, std::string_view name) {
    for (const Actor& actor : tavern.actors()) {
        if (actor.name() == name) {
            return &actor;
        }
    }
    return nullptr;
}

}  // namespace

// ===========================================================================
// RAWS
// ===========================================================================

TEST_CASE("a trade house can only ever rise inside a faction the owner's file has") {
    const FactionRegistry& factions = *registry();
    REQUIRE(factions.loaded());
    const ChapterRaws chapters = ChapterRaws::load(content::contentDir(), factions);
    REQUIRE(chapters.loaded());
    // Nothing in the file was refused, which is the claim: every row names one
    // of the owner's five.
    CHECK(chapters.refused() == 0);

    const SkillTrack skills = SkillTrack::load(content::contentDir());
    REQUIRE(skills.loaded());
    for (std::size_t i = 0; i < chapters.chapters().size(); ++i) {
        const ChapterRaw& row = chapters.chapters()[i];
        INFO("chapter ", row.id);
        // A faction the registry has, by index and not by hope.
        CHECK(factions.indexOf(row.faction) >= 0);
        // A trade skills.json has. A guild of a trade nobody in the ward
        // practises is a guild nobody can found.
        CHECK(skills.find(row.trade) != nullptr);
        CHECK_FALSE(row.displayName.empty());
        CHECK_FALSE(row.founderTitle.empty());
        CHECK(row.toll > 0);
        CHECK(row.influence > 0);
        // Sorted by id, so the index a nemesis stores means the same thing on
        // every machine and in every save.
        if (i > 0) {
            CHECK(chapters.chapters()[i - 1].id < row.id);
        }
    }

    // AND THE REFUSAL IS REAL, not a comment. Load the same file against a
    // registry with nothing in it and every row must be turned away -- which is
    // what stops this file growing a sixth faction by typo.
    const FactionRegistry empty;
    const ChapterRaws orphans = ChapterRaws::load(content::contentDir(), empty);
    CHECK_FALSE(orphans.loaded());
    CHECK(orphans.refused() == static_cast<std::int32_t>(chapters.chapters().size()));
}

TEST_CASE("a guild is a guild OF something: the trade picks the house, not the faction") {
    const ChapterRaws chapters = ChapterRaws::load(content::contentDir(), *registry());
    REQUIRE(chapters.loaded());
    // kit_keeping is a cooper's skill AND a watchman's, and the two must not
    // found the same house. This is the pair lookup, and it is why forTrade
    // takes both.
    const std::int32_t cooper = chapters.forTrade("kit_keeping", "dockhands");
    const std::int32_t watch = chapters.forTrade("kit_keeping", "watch");
    REQUIRE(cooper >= 0);
    REQUIRE(watch >= 0);
    CHECK(cooper != watch);
    CHECK(chapters.at(cooper)->faction == "dockhands");
    CHECK(chapters.at(watch)->faction == "watch");
    // A trade nobody has a house for still finds one inside his own faction --
    // a man rises into the nearest thing there is rather than into nothing.
    const std::int32_t fallback = chapters.forTrade("bladework", "merchants");
    REQUIRE(fallback >= 0);
    CHECK(chapters.at(fallback)->faction == "merchants");
    // And a man with no trade and no faction founds nothing at all.
    CHECK(chapters.forTrade("", "") < 0);
    CHECK(chapters.at(-1) == nullptr);

    // S9 CLOSES THE S8 REVIEW'S THIRD FINDING. The trade-only pass used to
    // match a chapter of ANY faction, so a DOCKHAND whose trade is streetwise
    // -- Sella Brinewall and Kled Tarbeck are both on the Gull's own roster --
    // founded The Chandlers' Row, which chapters.json says belongs to the
    // MERCHANTS. recordDefeat then booked her influence and her toll against
    // the dockhands: a hand founded a merchants' house and taxed the quay gang
    // for it. This goes red on the faction clause being removed.
    const std::int32_t merchantHouse = chapters.forTrade("streetwise", "merchants");
    const std::int32_t dockHouse = chapters.forTrade("streetwise", "dockhands");
    REQUIRE(merchantHouse >= 0);
    REQUIRE(dockHouse >= 0);
    CHECK(chapters.at(merchantHouse)->faction == "merchants");
    CHECK(chapters.at(dockHouse)->faction == "dockhands");
    CHECK(merchantHouse != dockHouse);
    // Whatever a rising body founds, it is a house of THEIR OWN GUILD. Said
    // over every chapter in the owner's file, so a new one cannot slip through.
    for (const char* guild : {"dockhands", "merchants", "watch", "skyrunners", "temple"}) {
        for (const char* trade : {"streetwise", "kit_keeping", "fieldcraft", "seacraft",
                                  "fishing", "skyrunning", "channeling", "bladework"}) {
            const std::int32_t found = chapters.forTrade(trade, guild);
            if (found < 0) {
                continue;
            }
            INFO(guild, " / ", trade, " -> ", chapters.at(found)->id);
            CHECK(chapters.at(found)->faction == guild);
        }
    }
}

TEST_CASE("a rival keeps his record when the roster hands him a different id") {
    // S9 CLOSES THE S8 REVIEW'S SECOND FINDING, and the door it was found
    // behind is the one nemesis.hpp advertises: "a name that is already in the
    // book keeps its record and takes the new id". A save reloaded against a
    // bigger cast is exactly that path, and of() used to `break` out of the
    // scan the moment an id sorted past the one it was asked for -- which is
    // only correct while rivals_ is sorted by id, and entryFor() reassigns ids
    // in place without re-sorting.
    NemesisBook book;
    book.attach(registry());

    const auto beat = [&](std::int32_t actorId, const char* who) {
        Defeat blow;
        blow.actorId = actorId;
        blow.who = who;
        blow.epithet = "the Steady";
        blow.jobPrefix = "serf";
        blow.trade = "fieldcraft";
        blow.day = 1;
        RiseWorld world;
        (void)book.recordDefeat(blow, world);
    };

    // Two rivals, entered in ascending id order, exactly as one room hands
    // them out.
    beat(4, "Tarn Wrenhale");
    beat(9, "Sella Brinewall");
    REQUIRE(book.of(4) != nullptr);
    REQUIRE(book.of(9) != nullptr);

    // NOW THE WORLD IS REBUILT AND THE SAME MAN IS SOMEBODY ELSE'S NUMBER.
    // Tarn comes back as id 11, past Sella; the list is now {11, 9}.
    beat(11, "Tarn Wrenhale");
    CHECK(book.byName("Tarn Wrenhale") != nullptr);
    CHECK(book.byName("Tarn Wrenhale")->actorId == 11);
    CHECK(book.of(4) == nullptr);
    // This is the assertion that used to read CHECK( nullptr != nullptr ).
    REQUIRE(book.of(11) != nullptr);
    CHECK(book.of(11)->who == "Tarn Wrenhale");
    CHECK(book.of(11)->wins == 2);
    // And the man who did not move is still where he was.
    REQUIRE(book.of(9) != nullptr);
    CHECK(book.of(9)->who == "Sella Brinewall");
}

// ===========================================================================
// RISE
// ===========================================================================

TEST_CASE("a labourer who puts the player down rises on the ward's own ladder") {
    // ELI'S OWN EXAMPLE, run: "the player could be killed by a laborer in a
    // fist fight but next time the player returns that person might be promoted
    // to a carpenter who's the head of a new Carpenter's Guild".
    Room room(hourOfDay(20), 152, 70);
    const Actor* tarn = find(room.tavern(), "Tarn Wrenhale");
    REQUIRE(tarn != nullptr);
    const std::int32_t id = tarn->id();
    // A dockhand, by the owner's own factions.json and not by a table here.
    const std::int32_t dockhands = registry()->indexOf("dockhands");
    REQUIRE(dockhands >= 0);
    REQUIRE(room.tavern().factionOf(*tarn) == dockhands);

    // Nobody is anybody's nemesis until they have done something.
    CHECK(room.tavern().nemesis().of(id) == nullptr);
    CHECK(room.tavern().nemesis().defeats() == 0);
    CHECK(room.tavern().nemesis().worst() == nullptr);

    const std::int32_t influenceBefore =
        room.tavern().dialogue().standings().influence(dockhands);
    const std::int32_t purseBefore = room.tavern().playerCoin();
    REQUIRE(purseBefore > 0);

    room.tavern().concedeTo(id);

    const Nemesis* rival = room.tavern().nemesis().of(id);
    REQUIRE(rival != nullptr);
    CHECK(rival->who == "Tarn Wrenhale");
    CHECK(rival->wins == 1);
    CHECK(rival->faction == dockhands);
    CHECK(room.tavern().nemesis().defeats() == 1);
    CHECK(room.tavern().nemesis().worst() == rival);

    // A RUNG, AND ITS TITLE IS THE OWNER'S. ranks.json's dockhand ladder starts
    // at Bondsworn; nothing in the C++ writes that word.
    CHECK(rival->rank == 1);
    const FactionLadder* ladder = registry()->ladder(dockhands);
    REQUIRE(ladder != nullptr);
    REQUIRE_FALSE(ladder->ranks.empty());
    CHECK(rival->title == ladder->ranks[0].title);
    CHECK(room.tavern().lastDefeat().promoted);

    // AND HIS GUILD IS HEAVIER IN THE WARD FOR IT.
    //
    // S9 REWRITES THIS ASSERTION, and it is the S8 review's "minor" finding
    // closed. It used to read `influenceBefore + kInfluencePerWin`, which is
    // the constant it exists to detect changes in: zero that constant and the
    // case stayed green while a win stopped moving the ward at all. It now
    // asserts the BEHAVIOUR -- a win makes his guild strictly heavier -- and
    // pins the size separately, so either half can go red on its own.
    const std::int32_t influenceAfter =
        room.tavern().dialogue().standings().influence(dockhands);
    CHECK(influenceAfter > influenceBefore);
    CHECK(influenceAfter - influenceBefore == kInfluencePerWin);
    CHECK(kInfluencePerWin > 0);

    // He went through the player's coat on the way past.
    CHECK(room.tavern().playerCoin() < purseBefore);
    CHECK(room.tavern().lastDefeat().coinTaken == purseBefore - room.tavern().playerCoin());
    // And he said something, out of the owner's tables rather than out of here.
    CHECK_FALSE(room.tavern().lastDefeat().taunt.empty());
}

TEST_CASE("what one guild gains in the ward, its declared rival loses") {
    // THE MIRROR, UNCHANGED, APPLIED TO A NEW CAUSE. ranks.json declares the
    // Skyrunners and the Watch each other's rivals and shiftInfluence has taken
    // one off the other since S4. A man rising over the player's body is
    // another reason for it to move, and not a second mechanism.
    Room room(hourOfDay(23), 158, 69);
    const Actor* finch = find(room.tavern(), "Finch");
    REQUIRE(finch != nullptr);
    const std::int32_t roofs = registry()->indexOf("skyrunners");
    const std::int32_t watch = registry()->indexOf("watch");
    REQUIRE(roofs >= 0);
    REQUIRE(watch >= 0);
    REQUIRE(room.tavern().factionOf(*finch) == roofs);

    const FactionLedger& standings = room.tavern().dialogue().standings();
    const std::int32_t roofsBefore = standings.influence(roofs);
    const std::int32_t watchBefore = standings.influence(watch);
    REQUIRE(watchBefore > 0);

    room.tavern().concedeTo(finch->id());

    CHECK(standings.influence(roofs) > roofsBefore);
    CHECK(standings.influence(watch) < watchBefore);
}

TEST_CASE("the second win founds a house with real members, and a mug never costs what it did") {
    Room room(hourOfDay(20), 152, 70);
    const Actor* tarn = find(room.tavern(), "Tarn Wrenhale");
    REQUIRE(tarn != nullptr);
    const std::int32_t id = tarn->id();

    room.tavern().concedeTo(id);
    room.returnAtDusk();
    REQUIRE(room.tavern().nemesis().of(id)->chapter < 0);
    room.tavern().concedeTo(id);

    const Nemesis* rival = room.tavern().nemesis().of(id);
    REQUIRE(rival != nullptr);
    REQUIRE(rival->wins == kFoundsAtWins);
    // A HOUSE, AND IT IS ONE OF THE OWNER'S AUTHORED ONES.
    REQUIRE(rival->foundedAHouse());
    const ChapterRaw* house = room.tavern().nemesis().chapters().at(rival->chapter);
    REQUIRE(house != nullptr);
    CHECK(house->faction == "dockhands");
    CHECK(room.tavern().lastDefeat().founded);
    CHECK(room.tavern().lastDefeat().chapterName == house->displayName);
    // He answers to the house's own title now and not to a ladder rung.
    CHECK(rival->title == house->founderTitle);

    // REAL MEMBERS, ENLISTED OUT OF THE ROOM. Not a number -- named bodies who
    // were standing there, all of them of his own faction, and never himself.
    REQUIRE_FALSE(rival->members.empty());
    for (const std::int32_t member : rival->members) {
        INFO("member ", member);
        CHECK(member != id);
        const Actor* enlisted = room.tavern().actorById(member);
        REQUIRE(enlisted != nullptr);
        CHECK(room.tavern().factionOf(*enlisted) == rival->faction);
        CHECK(room.tavern().nemesis().enlisted(member));
    }
    CHECK(room.tavern().lastDefeat().members ==
          static_cast<std::int32_t>(rival->members.size()));

    // AND THE TRADE IMPACT, WHICH IS THE POINT. A house takes a cut of every
    // price its parent faction's counters quote a stranger, and only its
    // parent's.
    CHECK(room.tavern().nemesis().tollPercent(rival->faction) == house->toll);
    CHECK(room.tavern().nemesis().tollPercent(registry()->indexOf("temple")) == 0);
    CHECK(room.tavern().nemesis().tollPercent(-1) == 0);

    // The Gull's bar is the MERCHANTS' counter, so a dockhand house does not
    // move it -- and a house founded by one of Master Venn's own does. Both
    // halves, because a toll that moved every price would be a tax on the ward
    // and not a guild taking a cut.
    Room row(hourOfDay(20), 152, 70);
    const Actor* gerta = find(row.tavern(), "Gerta Saltcotte");
    REQUIRE(gerta != nullptr);
    const std::int32_t merchants = registry()->indexOf("merchants");
    REQUIRE(row.tavern().factionOf(*gerta) == merchants);
    const std::int32_t before = row.tavern().drinkPriceForPlayer();
    REQUIRE(before > 0);
    row.tavern().concedeTo(gerta->id());
    row.returnAtDusk();
    row.tavern().concedeTo(gerta->id());
    REQUIRE(row.tavern().nemesis().of(gerta->id())->foundedAHouse());
    REQUIRE(row.tavern().nemesis().tollPercent(merchants) > 0);
    const std::int32_t after = row.tavern().drinkPriceForPlayer();
    INFO("a mug was ", before, " and is ", after);
    CHECK(after > before);
}

TEST_CASE("the third win puts his name on the ward's roll") {
    // SECTION 2.8: a vacant charge is a prize and "any actor -- including the
    // player -- may petition for it". Until S8 only the player could. This is
    // the man who beat you three times doing it, on the same roll, with the
    // same consequences for everybody living on that ground.
    Room room(hourOfDay(20), 152, 70);
    REQUIRE(room.ward() != nullptr);
    REQUIRE(room.ward()->loaded());
    const Actor* tarn = find(room.tavern(), "Tarn Wrenhale");
    REQUIRE(tarn != nullptr);
    const std::int32_t id = tarn->id();

    const std::int32_t gullet = room.ward()->plotNamed("C4_GULLET");
    REQUIRE(gullet >= 0);
    // Vacant on the roll at day zero, and the families on it owe nobody a penny
    // because there is nobody standing there to owe it to.
    REQUIRE(room.ward()->plots()[static_cast<std::size_t>(gullet)].tenure == Tenure::Vacant);
    std::int32_t owedBefore = 0;
    for (const Household& home : room.ward()->households()) {
        if (home.plot == gullet && home.ownsHouse()) {
            owedBefore += home.groundPenny;
        }
    }
    CHECK(owedBefore == 0);

    for (int win = 0; win < kTakesChargeAtWins; ++win) {
        room.tavern().concedeTo(id);
        room.returnAtDusk();
    }
    const Nemesis* rival = room.tavern().nemesis().of(id);
    REQUIRE(rival != nullptr);
    REQUIRE(rival->wins == kTakesChargeAtWins);
    REQUIRE(rival->holdsGround());
    CHECK(rival->plot == gullet);

    const Plot& plot = room.ward()->plots()[static_cast<std::size_t>(gullet)];
    CHECK(plot.tenure == Tenure::Charged);
    CHECK(plot.heldBy == "Tarn Wrenhale");
    CHECK_FALSE(plot.playerIsDuke);
    // AND EVERY HOUSE-OWNER ON THAT GROUND NOW OWES SOMEBODY. That is what a
    // charge IS, and it is the difference between a title and a fact.
    std::int32_t owedAfter = 0;
    for (const Household& home : room.ward()->households()) {
        if (home.plot == gullet && home.ownsHouse()) {
            owedAfter += home.groundPenny;
        }
    }
    CHECK(owedAfter > 0);

    // The roll does not hand the same charge out twice.
    CHECK(room.ward()->grantCharge(gullet, "Somebody Else") == TenureResult::NotVacant);
    // Nor to nobody: a thing is true in Granadad when it is on the roll, and
    // "somebody" is not a name.
    const std::int32_t saltgate = room.ward()->plotNamed("C3_SALTGATE");
    REQUIRE(saltgate >= 0);
    CHECK(room.ward()->grantCharge(saltgate, "") == TenureResult::NoSuchThing);
    // Nor for Church ground, which is never let to anyone.
    const std::int32_t glebe = room.ward()->plotNamed("GLEBE_HOVELS");
    REQUIRE(glebe >= 0);
    CHECK(room.ward()->grantCharge(glebe, "Tarn Wrenhale") == TenureResult::NoCause);
    // Nor for a plot that is not there.
    CHECK(room.ward()->grantCharge(999, "Tarn Wrenhale") == TenureResult::NoSuchThing);
    CHECK(room.ward()->plotNamed("NO_SUCH_PLOT") < 0);
}

TEST_CASE("a rise that cannot reach the roll still ranks and still founds") {
    // The gate's workload and every synthetic case run without a Ward. A
    // nemesis has to be a nemesis anyway, or the book would be a system that
    // only works when another one happens to be registered beside it.
    Room room(hourOfDay(20), 152, 70, /*withRoll=*/false);
    REQUIRE(room.tavern().roll() == nullptr);
    const Actor* tarn = find(room.tavern(), "Tarn Wrenhale");
    REQUIRE(tarn != nullptr);
    for (int win = 0; win < kTakesChargeAtWins; ++win) {
        room.tavern().concedeTo(tarn->id());
        room.returnAtDusk();
    }
    const Nemesis* rival = room.tavern().nemesis().of(tarn->id());
    REQUIRE(rival != nullptr);
    CHECK(rival->rank > 0);
    CHECK(rival->foundedAHouse());
    CHECK_FALSE(rival->holdsGround());
    CHECK_FALSE(room.tavern().lastDefeat().tookCharge);
}

// ===========================================================================
// MEMORY
// ===========================================================================

TEST_CASE("he remembers, and he does not greet you the way he did") {
    Room room(hourOfDay(20), 152, 70);
    const Actor* tarn = find(room.tavern(), "Tarn Wrenhale");
    REQUIRE(tarn != nullptr);
    const std::int32_t id = tarn->id();

    // Before: a stranger in a taproom, out of the neutral table.
    REQUIRE(room.tavern().talkTo());
    REQUIRE(room.tavern().dialogue().speaker().actorId == id);
    const std::string strangerKey = room.tavern().dialogue().greetingKey();
    const std::size_t strangerTopics = room.tavern().dialogue().topics().size();
    CHECK(room.tavern().dialogue().attitude() == Attitude::Neutral);
    CHECK_FALSE(strangerKey.empty());
    for (const Topic& topic : room.tavern().dialogue().topics()) {
        CHECK(topic.kind != TopicKind::Rival);
    }
    room.tavern().endConversation();

    room.tavern().concedeTo(id);
    room.returnAtDusk();
    room.stand(152, 70);

    // After: the same man, a different authored table. This is standing being
    // AUDIBLE, which S3 proved for deeds the player did. The player did not do
    // this one.
    REQUIRE(room.tavern().talkTo());
    CHECK(room.tavern().dialogue().attitude() == Attitude::Hostile);
    CHECK(room.tavern().dialogue().greetingKey() != strangerKey);
    CHECK_FALSE(room.tavern().dialogue().greetingKey().empty());
    CHECK(room.tavern().speakerFor(*room.tavern().actorById(id)).rivalWins == 1);

    // AND THERE IS A ROW ON THE LIST THAT WAS NOT THERE. The one topic in this
    // game that exists because of something done TO the player.
    const std::vector<Topic>& topics = room.tavern().dialogue().topics();
    CHECK(topics.size() > strangerTopics);
    std::size_t rival = topics.size();
    for (std::size_t i = 0; i < topics.size(); ++i) {
        if (topics[i].kind == TopicKind::Rival) {
            rival = i;
        }
    }
    REQUIRE(rival < topics.size());
    const Reply said = room.tavern().chooseTopic(rival);
    CHECK(said.kind == TopicKind::Rival);
    CHECK_FALSE(said.line.empty());
    // Out of the owner's own table, which means the line is one of its rows and
    // not a fallback string composed in the C++.
    const std::vector<std::string>* rows =
        room.tavern().dialogue().barks().rows("nemesis.topic");
    REQUIRE(rows != nullptr);
    CHECK(std::find(rows->begin(), rows->end(), said.line) != rows->end());
    room.tavern().endConversation();
}

TEST_CASE("he comes prepared, and a man with a blade is not a brawl any more") {
    // brawl.hpp's rule, unchanged, meeting a man who has beaten you three
    // times. B1 says nothing edged is out; nemesisWeapon says that is exactly
    // what he is carrying now. The room stops resolving it and says so.
    CHECK(nemesisWeapon(0) == Weapon::Fists);
    CHECK(nemesisWeapon(1) == Weapon::Fists);
    CHECK(nemesisWeapon(2) == Weapon::Blunt);
    CHECK(nemesisWeapon(3) == Weapon::Edged);
    CHECK(nemesisIntent(0) == Intent::Subdue);
    CHECK(nemesisIntent(1) == Intent::Harm);
    CHECK(nemesisIntent(3) == Intent::Kill);

    Room room(hourOfDay(20), 152, 70);
    const Actor* tarn = find(room.tavern(), "Tarn Wrenhale");
    REQUIRE(tarn != nullptr);
    const std::int32_t id = tarn->id();
    REQUIRE(tarn->weapon() == Weapon::Fists);
    REQUIRE(tarn->intent() == Intent::Subdue);

    for (int win = 0; win < 3; ++win) {
        room.tavern().concedeTo(id);
        room.returnAtDusk();
    }
    room.stand(152, 70);

    const Actor* armed = room.tavern().actorById(id);
    REQUIRE(armed != nullptr);
    CHECK(armed->weapon() == Weapon::Edged);
    CHECK(armed->intent() == Intent::Kill);

    // And swinging at him is now the combat screen's business, not the room's.
    const Tavern::PunchResult swung = room.tavern().playerPunchNearest();
    REQUIRE(swung.swung);
    CHECK(swung.targetId == id);
    CHECK(swung.fight == FightClass::Lethal);
    CHECK_FALSE(resolvesInWorld(swung.fight));
    CHECK(room.tavern().escalated());
}

TEST_CASE("past the grudge he stops keeping his own hours") {
    Room room(hourOfDay(20), 152, 70);
    const Actor* tarn = find(room.tavern(), "Tarn Wrenhale");
    REQUIRE(tarn != nullptr);
    const std::int32_t id = tarn->id();
    const std::int32_t postX = tarn->tileX();
    const std::int32_t postY = tarn->tileY();

    // Two wins is enough grudge to hunt; one is not. That pins the threshold
    // rather than trusting the constant.
    room.tavern().concedeTo(id);
    room.returnAtDusk();
    CHECK_FALSE(room.tavern().nemesis().of(id)->hunts());
    room.tavern().concedeTo(id);
    room.returnAtDusk();
    REQUIRE(room.tavern().nemesis().of(id)->hunts());

    // Put the body somewhere that is emphatically not his post -- his stool is
    // by the hearth end of the room -- and let the room run.
    room.stand(150, 76);
    REQUIRE(std::max(std::abs(postX - 150), std::abs(postY - 76)) > 2);
    room.run(60);

    const Actor* hunter = room.tavern().actorById(id);
    REQUIRE(hunter != nullptr);
    INFO("his stool is ", postX, ",", postY, "; he is at ", hunter->tileX(), ",",
         hunter->tileY(), " and you are at ", room.body().tileX(), ",", room.body().tileY());
    const std::int32_t gap = std::max(std::abs(hunter->tileX() - room.body().tileX()),
                                      std::abs(hunter->tileY() - room.body().tileY()));
    CHECK(gap <= 2);
}

TEST_CASE("you can win the rematch, and you still cannot un-found his guild") {
    // PERMANENCE IS THE POINT. Beating him afterwards is worth exactly one
    // thing -- he stops looking for you -- and it is worth nothing else.
    NemesisBook book = NemesisBook::load(content::contentDir(), registry());
    FactionLedger guilds;
    guilds.attach(registry());
    SocialLedger ledger;
    RiseWorld world;
    world.guilds = &guilds;
    world.ledger = &ledger;
    world.presentIds = {4, 7};
    world.presentFactions = {registry()->indexOf("dockhands"), registry()->indexOf("dockhands")};

    Defeat blow;
    blow.actorId = 4;
    blow.who = "Tarn Wrenhale";
    blow.jobPrefix = "serf";
    blow.trade = "fieldcraft";
    for (int win = 0; win < kTakesChargeAtWins; ++win) {
        (void)book.recordDefeat(blow, world);
    }
    const Nemesis risen = *book.of(4);
    REQUIRE(risen.grudge > 0);
    REQUIRE(risen.hunts());
    REQUIRE(risen.foundedAHouse());
    const std::int32_t toll = book.tollPercent(risen.faction);
    REQUIRE(toll > 0);

    for (int round = 0; round < 6; ++round) {
        book.recordVictory(4);
    }
    const Nemesis beaten = *book.of(4);
    CHECK(beaten.grudge == 0);
    CHECK_FALSE(beaten.hunts());
    CHECK(beaten.losses == 6);
    // And NOTHING else moved.
    CHECK(beaten.rank == risen.rank);
    CHECK(beaten.title == risen.title);
    CHECK(beaten.chapter == risen.chapter);
    CHECK(beaten.members == risen.members);
    CHECK(beaten.wins == risen.wins);
    CHECK(book.tollPercent(risen.faction) == toll);
    // A victory over somebody who never beat you is not a thing the book has an
    // opinion about.
    book.recordVictory(999);
    CHECK(book.of(999) == nullptr);

    // And in the room, the charge is still his after the same treatment.
    Room room(hourOfDay(20), 152, 70);
    const Actor* tarn = find(room.tavern(), "Tarn Wrenhale");
    REQUIRE(tarn != nullptr);
    for (int win = 0; win < kTakesChargeAtWins; ++win) {
        room.tavern().concedeTo(tarn->id());
        room.returnAtDusk();
    }
    const Nemesis* held = room.tavern().nemesis().of(tarn->id());
    REQUIRE(held != nullptr);
    REQUIRE(held->holdsGround());
    CHECK(room.ward()->plots()[static_cast<std::size_t>(held->plot)].heldBy == "Tarn Wrenhale");
}

TEST_CASE("the book survives its own codec, which is the seam a save file uses") {
    NemesisBook book = NemesisBook::load(content::contentDir(), registry());
    FactionLedger guilds;
    guilds.attach(registry());
    SocialLedger ledger;
    RiseWorld world;
    world.guilds = &guilds;
    world.ledger = &ledger;
    world.presentIds = {4, 7, 9};
    world.presentFactions = {registry()->indexOf("dockhands"), registry()->indexOf("dockhands"),
                             registry()->indexOf("temple")};

    Defeat first;
    first.actorId = 4;
    first.who = "Tarn Wrenhale";
    first.epithet = "the Steady";
    first.jobPrefix = "serf";
    first.trade = "fieldcraft";
    first.day = 3;
    Defeat second;
    second.actorId = 11;
    second.who = "Watchman Cull";
    second.epithet = "the impound keeper";
    second.jobPrefix = "watch";
    second.trade = "kit_keeping";
    second.day = 5;
    (void)book.recordDefeat(first, world);
    (void)book.recordDefeat(first, world);
    (void)book.recordDefeat(second, world);
    REQUIRE(book.rivals().size() == 2);
    // Sorted by actor id, always -- a vector and never a hash, so two machines
    // walk the book in the same order.
    CHECK(book.rivals()[0].actorId < book.rivals()[1].actorId);
    // Two wins beats one, whatever order they happened in.
    REQUIRE(book.worst() != nullptr);
    CHECK(book.worst()->who == "Tarn Wrenhale");

    const std::vector<std::uint8_t> bytes = book.encode();
    NemesisBook back;
    back.attach(registry());
    REQUIRE(NemesisBook::decode(bytes, back));
    REQUIRE(back.rivals().size() == book.rivals().size());
    CHECK(back.defeats() == book.defeats());
    for (std::size_t i = 0; i < book.rivals().size(); ++i) {
        const Nemesis& mine = book.rivals()[i];
        const Nemesis& theirs = back.rivals()[i];
        INFO("rival ", mine.who);
        CHECK(mine.actorId == theirs.actorId);
        CHECK(mine.who == theirs.who);
        CHECK(mine.epithet == theirs.epithet);
        CHECK(mine.rank == theirs.rank);
        CHECK(mine.title == theirs.title);
        CHECK(mine.chapter == theirs.chapter);
        CHECK(mine.plot == theirs.plot);
        CHECK(mine.wins == theirs.wins);
        CHECK(mine.grudge == theirs.grudge);
        CHECK(mine.members == theirs.members);
    }
    CHECK(back.encode() == bytes);

    // A byte string it does not recognise is REFUSED rather than reinterpreted.
    NemesisBook refused;
    refused.attach(registry());
    CHECK_FALSE(NemesisBook::decode({}, refused));
    std::vector<std::uint8_t> wrongVersion = bytes;
    wrongVersion[0] = 99;
    CHECK_FALSE(NemesisBook::decode(wrongVersion, refused));
    std::vector<std::uint8_t> truncated = bytes;
    truncated.pop_back();
    CHECK_FALSE(NemesisBook::decode(truncated, refused));

    // THE NAME IS THE KEY AND THE ID IS THE ADDRESS -- the door the
    // persistent-ward variant walks through later. The same man under a new
    // roster id keeps his record instead of becoming a second stranger.
    Defeat moved = first;
    moved.actorId = 40;
    (void)book.recordDefeat(moved, world);
    CHECK(book.rivals().size() == 2);
    const Nemesis* same = book.byName("Tarn Wrenhale");
    REQUIRE(same != nullptr);
    CHECK(same->actorId == 40);
    CHECK(same->wins == 3);
    CHECK(book.byName("Nobody At All") == nullptr);
}

// ===========================================================================
// ARC -- the acceptance
// ===========================================================================

TEST_CASE("beaten by a named labourer in a fist fight, and he is not who he was") {
    // THE SPRINT'S ACCEPTANCE, driven through a REAL brawl: the player throws
    // the first punch, Tarn Wrenhale swings back, the room resolves it a second
    // at a time until the player is on the floor, and then the player gets up.
    // Nothing here is staged -- concedeTo is not used and the fight is the
    // ordinary one the door policy has been resolving since S2.
    Room room(hourOfDay(20), 152, 70);
    const Actor* tarn = find(room.tavern(), "Tarn Wrenhale");
    REQUIRE(tarn != nullptr);
    const std::int32_t id = tarn->id();
    REQUIRE(room.ward() != nullptr);

    // Who he is before: a docker on a stool, fists, no rung, nobody's rival.
    CHECK(tarn->weapon() == Weapon::Fists);
    CHECK(room.tavern().nemesis().of(id) == nullptr);
    CHECK(room.tavern().dialogue().ledger().attitudeOf(id) == Attitude::Neutral);
    const std::int32_t hpBefore = room.tavern().playerHp();
    const std::int32_t hourBefore = room.tavern().timeOfDay();
    REQUIRE(hpBefore > kPlayerBrawlFloor);

    // The player swings first. It is a fist fight in a taproom and the rule
    // says the world resolves it.
    const Tavern::PunchResult opened = room.tavern().playerPunchNearest();
    REQUIRE(opened.swung);
    REQUIRE(opened.targetId == id);
    REQUIRE(opened.fight == FightClass::Brawl);

    // And then he wins it. Bounded, because a loop whose exit depends on
    // simulation state is a loop that hangs a build the day that state is wrong.
    int seconds = 0;
    while (!room.tavern().playerFloored() && seconds < 400) {
        room.run(1);
        ++seconds;
    }
    INFO("the fight took ", seconds, " seconds");
    REQUIRE(room.tavern().playerFloored());
    CHECK(room.tavern().playerHp() == kPlayerBrawlFloor);

    // A NAMED MAN DID IT, and the room says which.
    const Rise& rise = room.tavern().lastDefeat();
    REQUIRE(rise.happened);
    CHECK(rise.actorId == id);
    CHECK(rise.who == "Tarn Wrenhale");
    CHECK(rise.promoted);
    CHECK_FALSE(rise.title.empty());
    CHECK_FALSE(rise.taunt.empty());

    // THE PLAYER GETS UP -- as themselves, on the quay apron, hours later and
    // lighter of purse. The world keeps every consequence.
    REQUIRE(room.tavern().takeDefeatRelease());
    CHECK_FALSE(room.tavern().takeDefeatRelease());
    room.tavern().reviveAfterDefeat();
    room.stand(gull::kStreetX, gull::kStreetY);
    CHECK(room.tavern().playerHp() == hpBefore);
    CHECK_FALSE(room.tavern().playerFloored());
    CHECK(room.tavern().timeOfDay() != hourBefore);

    // AND HE IS NOT WHO HE WAS. A rung with the owner's own title on it, his
    // guild heavier in the ward, and an opinion of the player that comes out of
    // a different authored table.
    const Nemesis* rival = room.tavern().nemesis().of(id);
    REQUIRE(rival != nullptr);
    CHECK(rival->wins == 1);
    CHECK(rival->rank >= 1);
    REQUIRE(registry()->ladder(rival->faction) != nullptr);
    CHECK(rival->title == registry()->ladder(rival->faction)->ranks[0].title);
    CHECK(room.tavern().dialogue().ledger().attitudeOf(id) == Attitude::Hostile);
    CHECK(room.tavern().dialogue().standings().influence(rival->faction) > 0);
}
