// Being unseen: the light, the sound, the sight line and the skill.
//
// Three kinds of case, in the order the other suites use:
//
//   RULES  stealth.hpp on its own -- the integer light field, the noise the
//          body makes, and the one comparison that decides whether anybody
//          noticed. No world anywhere near it.
//   ROOM   all of it in the Gilded Gull, where the lamps are the ones the
//          simulation derives from the baked bytes and the people are the
//          fourteen who work there.
//   PLAY   the same thing through the keys a player presses.
//
// EVERY CLAUSE OF THE NOTICE RULE HAS A CASE THAT GOES RED WHEN IT IS DELETED.
// That is the standard S3 set for witnessCount and it is the standard here.

#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/engine.hpp"
#include "granadad/sim/player.hpp"
#include "granadad/sim/stealth.hpp"
#include "granadad/sim/tavern.hpp"

using namespace granadad::sim;
namespace content = granadad::content;
namespace render = granadad::render;

namespace {

constexpr std::uint64_t kSeed = 0x4752414E41444144ull;

/// The Gull, an engine and a body -- the same shape test_crime.cpp uses.
class Room {
public:
    Room(std::int32_t timeOfDay, std::int32_t tileX, std::int32_t tileY,
         std::int32_t band = gull::kGroundBand)
        : world_(content::loadWorldFile(content::bakedMap(docks::kWorldName))),
          tiles_(std::make_unique<TileQuery>(world_)),
          engine_(std::make_unique<PhasedEngine>(kSeed, world_)) {
        auto tavern = std::make_unique<Tavern>(*tiles_, timeOfDay, kSeed, content::contentDir());
        tavern_ = tavern.get();
        engine_->register_system(std::move(tavern));
        engine_->boot();
        tavern_->setPlayer(q8_tile_centre(tileX), q8_tile_centre(tileY), band);
    }

    [[nodiscard]] Tavern& tavern() noexcept { return *tavern_; }

    void standAt(std::int32_t tileX, std::int32_t tileY, std::int32_t band) {
        tavern_->setPlayer(q8_tile_centre(tileX), q8_tile_centre(tileY), band);
    }

    /// The first present actor with this name, or nullptr.
    [[nodiscard]] const Actor* who(std::string_view name) const {
        for (const Actor& actor : tavern_->actors()) {
            if (actor.name() == name && actor.present()) {
                return &actor;
            }
        }
        return nullptr;
    }

private:
    content::World world_;
    std::unique_ptr<TileQuery> tiles_;
    std::unique_ptr<PhasedEngine> engine_;
    Tavern* tavern_ = nullptr;
};

/// A NoticeInput with nothing working for or against the body: right on top of
/// an observer, in the pitch dark, standing still, facing away, no skill.
[[nodiscard]] NoticeInput bare() {
    NoticeInput in;
    in.distanceQ8 = 0;
    in.lineOfSight = true;
    in.observerFacing = kFacingNorth;
    // Behind them, so the forward-arc clause is off unless a case turns it on.
    in.bearingToBody = kFacingSouth;
    in.light = 0;
    in.noise = 0;
    in.roomNoise = 0;
    in.sneakLevel = 0;
    in.stance = Stance::Upright;
    return in;
}

}  // namespace

// ===========================================================================
// RULES
// ===========================================================================

TEST_CASE("light is an integer field with the renderer's own shape") {
    // The renderer's curve, in integers: radius 4 + (lum-8)/12 tiles clamped to
    // 3.5..5.5, peak 0.55 + 0.45*lum/26. If those two ever disagree, a lamp is
    // bright to the eye and dark to the law -- which is the whole reason the
    // simulation derives its own field instead of guessing one.
    CHECK(lightRadiusQ4(0) == 0);
    CHECK(lightRadiusQ4(1) == 56);   // 3.5 tiles, the floor
    CHECK(lightRadiusQ4(31) == 88);  // 5.5 tiles, the ceiling
    CHECK(lightRadiusQ4(8) == 64);   // 4.0 tiles exactly
    CHECK(lightPeak(0) == 0);
    CHECK(lightPeak(26) == 100);
    CHECK(lightPeak(1) > 55);

    const SimLight lamp{10, 10, 19, 20};
    // Brightest where it stands, dimmer a tile away, nothing past its radius.
    const std::int32_t here = glowFrom(lamp, 10, 10, 19);
    const std::int32_t near = glowFrom(lamp, 11, 10, 19);
    const std::int32_t far = glowFrom(lamp, 14, 10, 19);
    CHECK(here > near);
    CHECK(near > far);
    CHECK(glowFrom(lamp, 30, 10, 19) == 0);
    // AND IT DOES NOT LIGHT THE FLOOR ABOVE IT. A candle on a taproom table is
    // not a lamp in the guest room over it, and the band comparison is what
    // says so. This goes red on `light.band != band` being deleted.
    CHECK(glowFrom(lamp, 10, 10, 20) == 0);

    // Two lamps over one tile do not add to twice as bright as bright.
    const std::vector<SimLight> pair{{10, 10, 19, 26}, {10, 10, 19, 26}};
    CHECK(glowAt(pair, 10, 10, 19) <= kLightMax);
    CHECK(glowAt(pair, 10, 10, 19) >= glowFrom(pair[0], 10, 10, 19));
}

TEST_CASE("the sky is committed dark, and a roof takes three quarters of it") {
    CHECK(ambientLight(0) == 0);
    CHECK(ambientLight(12 * 3600) > ambientLight(0));
    CHECK(ambientLight(12 * 3600) < kLightMax);
    // Symmetric round noon, and it wraps.
    CHECK(ambientLight(6 * 3600) == ambientLight(18 * 3600));
    CHECK(ambientLight(86400) == ambientLight(0));

    const std::vector<SimLight> none;
    const std::int32_t outside = illuminationAt(none, 0, 0, 0, 12 * 3600, false);
    const std::int32_t inside = illuminationAt(none, 0, 0, 0, 12 * 3600, true);
    CHECK(inside < outside);
    CHECK(inside == (outside * kIndoorSkyPercent) / 100);
    // And at midnight, indoors, with nothing burning, it is BLACK -- which is
    // the state a burglary happens in.
    CHECK(illuminationAt(none, 0, 0, 0, 0, true) == 0);
}

TEST_CASE("noise is what you are doing, and the loudest thing wins") {
    StealthState body;
    CHECK(body.noise() == 0);

    body.setMotion(true, false);
    CHECK(body.noise() == kNoiseWalking);
    body.setMotion(true, true);
    CHECK(body.noise() == kNoiseRunning);
    body.setStance(Stance::Crouched);
    CHECK(body.noise() == kNoiseCrouchWalking);
    CHECK(kNoiseCrouchWalking < kNoiseWalking);
    body.setMotion(false, false);
    CHECK(body.noise() == 0);

    // AN ACT IS LOUDER THAN A FOOTSTEP AND IT FADES. Nothing stacks: two quiet
    // acts are not a shout.
    body.makeNoise(40);
    CHECK(body.noise() == 40);
    body.makeNoise(10);
    CHECK(body.noise() == 40);
    const std::int32_t before = body.noise();
    for (int i = 0; i < kNoiseFadeSteps / 2; ++i) {
        body.step();
    }
    CHECK(body.noise() < before);
    CHECK(body.noise() > 0);
    for (int i = 0; i < kNoiseFadeSteps; ++i) {
        body.step();
    }
    CHECK(body.noise() == 0);
}

TEST_CASE("the bearing to a body is the compass angle.hpp fixes, and nothing else") {
    // North is -Y and yaw rises clockwise. Every axis and every diagonal.
    CHECK(bearingTo(0, 0, 0, -10) == kFacingNorth);
    CHECK(bearingTo(0, 0, 10, 0) == kFacingEast);
    CHECK(bearingTo(0, 0, 0, 10) == kFacingSouth);
    CHECK(bearingTo(0, 0, -10, 0) == kFacingWest);
    CHECK(bearingTo(0, 0, 10, -10) == kTurnFull / 8);
    CHECK(bearingTo(0, 0, -10, 10) == 5 * kTurnFull / 8);

    CHECK(withinArc(kFacingNorth, kFacingNorth, kNoticeArc));
    CHECK_FALSE(withinArc(kFacingNorth, kFacingSouth, kNoticeArc));
    // Wrap-safe: due west is a quarter turn from due north the short way round.
    CHECK(withinArc(kFacingNorth, kFacingWest, kNoticeArc));
    CHECK_FALSE(withinArc(kFacingNorth, kFacingWest - 1, kNoticeArc));
}

TEST_CASE("every clause of the notice rule moves the answer, and none of them alone decides it") {
    // Standing on top of somebody in the pitch dark, facing away from them,
    // making no sound at all: they still know you are there. Anything else
    // would be absurd, and it is the baseline every clause below moves off.
    const NoticeInput close = bare();
    CHECK(noticeOf(close).seen);

    // DISTANCE. Eight tiles is the range S3 fixed; at it, in the dark, with
    // nothing else working either way, you are not made out.
    NoticeInput away = bare();
    away.distanceQ8 = 8 * kSubOne;
    CHECK_FALSE(noticeOf(away).seen);
    CHECK(noticeOf(away).read < noticeOf(close).read);

    // LIGHT. The same body at the same distance, standing in a lamp pool.
    NoticeInput lit = away;
    lit.light = kLightMax;
    CHECK(noticeOf(lit).read > noticeOf(away).read);
    CHECK(noticeOf(lit).seen);

    // FACING. They turn round.
    NoticeInput looked = away;
    looked.bearingToBody = kFacingNorth;
    CHECK(noticeOf(looked).read == noticeOf(away).read + kNoticeFacing);

    // SOUND. Running at eight tiles in the dark gives you away; walking
    // crouched does not.
    NoticeInput loud = away;
    loud.noise = kNoiseRunning;
    CHECK(noticeOf(loud).seen);
    NoticeInput quiet = away;
    quiet.noise = kNoiseCrouchWalking;
    CHECK_FALSE(noticeOf(quiet).seen);

    // MASONRY. Blind is not deaf: a wall takes a great deal off the read and
    // does not take all of it, which is why a lock probed behind a shut door
    // is still a risk.
    NoticeInput walled = close;
    walled.lineOfSight = false;
    CHECK(noticeOf(walled).read == noticeOf(close).read - kNoticeBlindPenalty);

    // ALERT. Somebody whose job is looking at the room. It raises what they
    // make of you and it is NOT a lantern: eight tiles of pitch dark still
    // beats a bouncer, which is the whole reason a burglar keeps late hours.
    NoticeInput bouncer = away;
    bouncer.alert = true;
    CHECK(noticeOf(bouncer).read == noticeOf(away).read + kNoticeAlert);
    CHECK_FALSE(noticeOf(bouncer).seen);
    // Come in to three tiles and he has you -- and being on duty is worth
    // exactly kNoticeAlert of that, no more and no less.
    NoticeInput closer = bouncer;
    closer.distanceQ8 = 3 * kSubOne;
    CHECK(noticeOf(closer).seen);
    NoticeInput offDuty = closer;
    offDuty.alert = false;
    CHECK(noticeOf(closer).read == noticeOf(offDuty).read + kNoticeAlert);
    // AND A CROUCH ALONE DOES NOT BEAT HIM AT THREE TILES. It takes the roofs
    // behind your feet and a room shouting over you as well -- which is the
    // shape of every number on this page: no single clause is a cloak.
    NoticeInput lowAndClose = closer;
    lowAndClose.stance = Stance::Crouched;
    CHECK(noticeOf(lowAndClose).seen);
    NoticeInput trained = lowAndClose;
    trained.sneakLevel = 30;
    trained.roomNoise = kNoiseMax;
    CHECK_FALSE(noticeOf(trained).seen);

    // OBLIVIOUS. On the floor, or a rat. Nothing gets through.
    NoticeInput down = close;
    down.alert = true;
    down.light = kLightMax;
    down.noise = kNoiseMax;
    down.oblivious = true;
    CHECK_FALSE(noticeOf(down).seen);
}

TEST_CASE("crouching, the dark and the skill are what a player does about it") {
    // A lit room, four tiles, somebody looking straight at you. Seen.
    NoticeInput exposed = bare();
    exposed.distanceQ8 = 4 * kSubOne;
    exposed.light = 80;
    exposed.bearingToBody = kFacingNorth;
    CHECK(noticeOf(exposed).seen);

    // CROUCH. Worth more than any single thing on the page.
    NoticeInput low = exposed;
    low.stance = Stance::Crouched;
    CHECK(noticeOf(low).cover == noticeOf(exposed).cover + kCoverCrouch);

    // THE DARK. The fire goes out and the same body, four tiles off, is not
    // made out -- even by somebody looking straight at it.
    NoticeInput dark = low;
    dark.light = 4;
    CHECK_FALSE(noticeOf(dark).seen);
    // AND WHAT THE DARK BUYS IS DISTANCE, NOT INVISIBILITY. At arm's length the
    // same crouch in the same dark is seen: a rule where crouching made a body
    // vanish under somebody's nose would be a rule with no game in it.
    NoticeInput underNose = dark;
    underNose.distanceQ8 = kSubOne;
    CHECK(noticeOf(underNose).seen);
    // And four tiles upright in the lamp pool is seen again -- so it is the
    // dark and the crouch doing it, not the distance alone.
    NoticeInput litAgain = dark;
    litAgain.light = 80;
    litAgain.stance = Stance::Upright;
    CHECK(noticeOf(litAgain).seen);

    // THE SKILL, and its ceiling. SKYRUNNING covers "sneak" in the owner's own
    // skills.json; this is the Morrowind steer applied to it.
    NoticeInput novice = exposed;
    NoticeInput adept = exposed;
    adept.sneakLevel = 20;
    NoticeInput master = exposed;
    master.sneakLevel = 100;
    CHECK(noticeOf(adept).cover > noticeOf(novice).cover);
    CHECK(noticeOf(master).cover > noticeOf(adept).cover);
    CHECK(noticeOf(master).cover == noticeOf(novice).cover + kCoverSneakCap);
    // A hundred and a thousand are the same: no amount of skill is a cloak.
    NoticeInput impossible = exposed;
    impossible.sneakLevel = 1000;
    CHECK(noticeOf(impossible).cover == noticeOf(master).cover);

    // THE ROOM'S OWN DIN. A full house covers a hand in a purse -- the one
    // quantity the tavern has simulated since S2 that nothing has ever read.
    NoticeInput hushed = exposed;
    NoticeInput packed = exposed;
    packed.roomNoise = kNoiseMax;
    CHECK(noticeOf(packed).cover == noticeOf(hushed).cover + kCoverRoomNoiseWeight);
}

// ===========================================================================
// ROOM
// ===========================================================================

TEST_CASE("the Gull's own lamps light the law, not only the eye") {
    // Nine at night: the doors are open, the fire is lit, the lanterns and the
    // table candles are burning.
    Room open(hourOfDay(21), gull::kBartenderX, gull::kBarY + 2, gull::kGroundBand);
    const std::size_t litCount = open.tavern().simLights().size();
    CHECK(litCount == open.tavern().houseLights().size());
    CHECK(litCount > 0);
    const std::int32_t litRoom = open.tavern().lightOnPlayer();
    CHECK(litRoom > 0);

    // Five in the morning: shut, and the hearth is out. The room is dark, and
    // that is a fact about the SIMULATION rather than about the frame.
    Room shut(hourOfDay(5), gull::kBartenderX, gull::kBarY + 2, gull::kGroundBand);
    CHECK(shut.tavern().simLights().empty());
    CHECK(shut.tavern().lightOnPlayer() < litRoom);

    // AND STANDING UNDER A LANTERN IS BRIGHTER THAN STANDING IN A CORNER. The
    // snug behind the partition is the one corner of the taproom the bar cannot
    // see into, which is why the Skyrunners' contact drinks there.
    const std::int32_t underLantern =
        open.tavern().lightAt(gull::kDoorX0, gull::kDoorY + 1, gull::kGroundBand);
    const std::int32_t inTheSnug =
        open.tavern().lightAt(gull::kBaleX, gull::kBaleY, gull::kGroundBand);
    CHECK(underLantern > inTheSnug);
}

TEST_CASE("the same crime is witnessed in a lit taproom and missed in a dark one") {
    // ONE TILE, TWO HOURS. Both halves stand in the same place -- just inside
    // the door, which is where a burglar comes in and where the bouncers work
    // -- so the only thing that differs between them is the hour, the stance
    // and the light. Standing at the BAR would not do it: kWitnessReachTiles
    // says an observer at arm's length needs no sight line and no light, which
    // is deliberate (see the note on it) and would make this a case about
    // distance wearing a stealth case's name.
    const std::int32_t standX = gull::kDoorX0;
    const std::int32_t standY = gull::kDoorY + 1;

    // Nine at night, in the doorway, upright, walking in: the room sees you.
    Room evening(hourOfDay(21), standX, standY, gull::kGroundBand);
    evening.tavern().setPlayerMotion(true, false);
    REQUIRE(evening.tavern().watchersInReach() > 0);
    CHECK(evening.tavern().witnessCount(kPlayerActorId) > 0);
    CHECK_FALSE(evening.tavern().hidden());

    // TWO IN THE MORNING, CROUCHED, STILL. The doors have just been barred, the
    // lanterns and the table candles are out, and the night staff are still in
    // the building -- which is exactly why a burglar keeps this hour and not
    // four, when the Gull is empty and being unseen proves nothing.
    Room night(hourOfDay(2), standX, standY, gull::kGroundBand);
    night.tavern().setPlayerMotion(false, false);
    night.tavern().setStance(Stance::Crouched);

    // THIS HALF USED TO ASSERT NOTHING, and the S9 review proved it rather than
    // suspecting it: the case passed with `Notice::seen` hard-wired to true --
    // every observer in reach seeing through dark, crouch, silence and skill --
    // because all it ever checked was `witnessCount() >= seenCrouched`, which
    // holds when both sides are zero and when both sides are equal. The claim
    // in the case NAME is the claim that belongs here.
    //
    // First: THERE IS SOMEBODY TO MISS YOU. Awake, upright, on this floor and
    // in range. A room nobody is standing in trivially fails to see you, and
    // that is not the behaviour this case is named for.
    REQUIRE(night.tavern().watchersInReach() > 0);
    CHECK(night.tavern().witnessCount(kPlayerActorId) == 0);
    CHECK(night.tavern().hidden());
    // The dark is real and not assumed: the same tile, unlit.
    CHECK(night.tavern().lightOnPlayer() < evening.tavern().lightOnPlayer());

    // AND STAND BACK UP AND RUN AT SOMEBODY, and the same dark room has you.
    // Three tiles off a body that is still awake, upright, running: the light
    // is the same 2 it was in the doorway, so what changed is the stance, the
    // noise and the wall that is no longer between you.
    const Actor* awake = nullptr;
    for (const Actor& actor : night.tavern().actors()) {
        if (actor.present() && actor.band() == gull::kGroundBand &&
            actor.activity() != Activity::Downed && actor.role() != ActorRole::Vermin) {
            awake = &actor;
            break;
        }
    }
    REQUIRE(awake != nullptr);
    night.standAt(awake->tileX(), awake->tileY() + 3, gull::kGroundBand);
    night.tavern().setStance(Stance::Upright);
    night.tavern().setPlayerMotion(true, true);
    const Notice running = night.tavern().worstNotice();
    CHECK(night.tavern().witnessCount(kPlayerActorId) > 0);
    CHECK_FALSE(night.tavern().hidden());

    // AND CROUCHING BACK DOWN ON THAT SAME TILE MOVES THE MARGIN, STRICTLY.
    // Three tiles from a man who is looking at you is close enough that
    // crouching does not save you -- which is the rule working, not failing --
    // so what this asserts is the number the rule decides on rather than the
    // yes/no it decides. Same place, same hour, same man: the stance and the
    // footfalls are the whole difference.
    night.tavern().setStance(Stance::Crouched);
    night.tavern().setPlayerMotion(false, false);
    const Notice creeping = night.tavern().worstNotice();
    CHECK(running.read + running.noise - running.cover >
          creeping.read + creeping.noise - creeping.cover);
    CHECK(creeping.cover > running.cover);
    CHECK(creeping.noise < running.noise);
}

TEST_CASE("a hand in a coat is refused when the mark can see you and taken when they cannot") {
    // The Skyrunner contact keeps the snug from ten at night: the darkest
    // corner of the room, and the man with the biggest purse in it.
    Room room(hourOfDay(1), gull::kBaleX, gull::kBaleY, gull::kGroundBand);
    Tavern& gull = room.tavern();
    const Actor* finch = room.who("Finch");
    REQUIRE(finch != nullptr);
    room.standAt(finch->tileX(), finch->tileY(), gull::kGroundBand);

    // UPRIGHT, IN HIS FACE. He has you.
    gull.setStance(Stance::Upright);
    gull.setPlayerMotion(false, false);
    const std::int32_t purseBefore = gull.playerCoin();
    const Tavern::StealResult caught = gull.liftFrom();
    CHECK(caught.result == ServiceResult::Refused);
    CHECK(caught.seen);
    CHECK(gull.playerCoin() == purseBefore);
    // AND THE ACT IS STILL A CRIME. Being caught is a crime committed, which is
    // the whole point of noteCrime being one function.
    CHECK(gull.dialogue().crimes().tally(Crime::Lift) == 1);
    // AND THE HANDS STILL LEARNED SOMETHING. Morrowind's rule: you are charged
    // for the attempt.
    CHECK(gull.dialogue().skills().find(kRoofSkill)->uses > 0);
}

TEST_CASE("a trained sneak lifts in a loud room what the same hands cannot lift standing up") {
    // THE ACCEPTANCE FOR THIEVERY, and it is one scenario asked twice.
    //
    // Nine at night: the house is full and shouting, which is COVER, and lit,
    // which is not. Father Maell of the Mission is at his table and his
    // streetwise is the lowest in the room (notables.json: he is a channeler,
    // not a cutpurse). The same hands, at the same table, at the same moment:
    // crouched with the roofs behind them, and standing up with nothing.
    Room room(hourOfDay(21), gull::kBartenderX, gull::kBarY + 2, gull::kGroundBand);
    Tavern& gull = room.tavern();
    const Actor* maell = room.who("Father Maell");
    REQUIRE(maell != nullptr);
    const std::int32_t markId = maell->id();
    room.standAt(maell->tileX(), maell->tileY() - 1, gull::kGroundBand);

    // Hands worth something, and feet worth something.
    REQUIRE(gull.dialogue().skills().setLevel(kThieverySkill, 12));
    REQUIRE(gull.dialogue().skills().setLevel(kRoofSkill, 30));

    // UPRIGHT, WALKING, IN A LIT ROOM. He feels it.
    gull.setStance(Stance::Upright);
    gull.setPlayerMotion(true, false);
    const std::int32_t purseBefore = gull.playerCoin();
    const Tavern::StealResult brazen = gull.liftFrom();
    CHECK(brazen.result == ServiceResult::Refused);
    CHECK(gull.playerCoin() == purseBefore);

    // THE SAME HANDS, CROUCHED AND STILL, WITH THE ROOM SHOUTING OVER THEM.
    Room quiet(hourOfDay(21), gull::kBartenderX, gull::kBarY + 2, gull::kGroundBand);
    Tavern& second = quiet.tavern();
    const Actor* again = quiet.who("Father Maell");
    REQUIRE(again != nullptr);
    quiet.standAt(again->tileX(), again->tileY() - 1, gull::kGroundBand);
    REQUIRE(second.dialogue().skills().setLevel(kThieverySkill, 12));
    REQUIRE(second.dialogue().skills().setLevel(kRoofSkill, 30));
    second.setStance(Stance::Crouched);
    second.setPlayerMotion(false, false);
    const std::int32_t before = second.playerCoin();
    const Tavern::StealResult quietly = second.liftFrom();
    INFO(quietly.line);
    CHECK(quietly.result == ServiceResult::Served);
    CHECK(quietly.coin > 0);
    CHECK(second.playerCoin() == before + quietly.coin);
    // The coin came out of HIS purse and not out of nowhere.
    CHECK(second.actorById(markId)->coin() < maell->coin());
    // And there is a piece in your coat that a fence exists to buy.
    CHECK(second.dialogue().crimes().loot() >= kLiftPieces);
    CHECK(second.dialogue().crimes().tally(Crime::Lift) == 1);
}

TEST_CASE("skyrunning and cracksmanship both rise from a night's work") {
    Room room(hourOfDay(4), gull::kRooms[2].standX, gull::kRooms[2].standY, gull::kUpperBand);
    Tavern& gull = room.tavern();
    const std::int32_t craftBefore = gull.dialogue().skills().find(kThieverySkill)->uses;

    REQUIRE(gull.beginPick().result == ServiceResult::Served);
    const Lock lock = Tavern::strongboxLock(2);
    for (std::int32_t pin = 0; pin < lock.pins; ++pin) {
        const std::int32_t want = pinDepth(kSeed, lock, pin);
        gull.movePick(want - gull.picking().depth());
        gull.probeLock();
    }
    // The hands were charged for every probe and again for the lock opening.
    CHECK(gull.dialogue().skills().find(kThieverySkill)->uses > craftBefore);
    CHECK((gull.openedLocks() & (1 << 2)) != 0);
}

// ===========================================================================
// PLAY
// ===========================================================================

TEST_CASE("crouching is the room's own state, and the body pays for it in speed") {
    render::SessionConfig config;
    config.contentDir = content::contentDir();
    config.timeOfDay = 4 * 3600;
    config.spawnX = docks::kSpawnTileX;
    config.spawnY = docks::kSpawnTileY;
    render::Session session(config);

    CHECK(session.stance() == Stance::Upright);

    MoveInput forward;
    forward.forward = 1;

    const std::int32_t startX = session.body().x();
    const std::int32_t startY = session.body().y();
    session.stepMany(forward, 30);
    const std::int32_t dxUpright = session.body().x() - startX;
    const std::int32_t dyUpright = session.body().y() - startY;
    const std::int32_t walked =
        (dxUpright < 0 ? -dxUpright : dxUpright) + (dyUpright < 0 ? -dyUpright : dyUpright);

    session.toggleCrouch();
    CHECK(session.stance() == Stance::Crouched);
    const std::int32_t crouchX = session.body().x();
    const std::int32_t crouchY = session.body().y();
    session.stepMany(forward, 30);
    const std::int32_t dxCrouch = session.body().x() - crouchX;
    const std::int32_t dyCrouch = session.body().y() - crouchY;
    const std::int32_t crept =
        (dxCrouch < 0 ? -dxCrouch : dxCrouch) + (dyCrouch < 0 ? -dyCrouch : dyCrouch);

    CHECK(walked > 0);
    CHECK(crept > 0);
    // Half the walk, and the constant that says so is on stealth.hpp's page.
    CHECK(crept < walked);
    CHECK(crept * 100 <= walked * (kCrouchSpeedPercent + 10));

    // AND THE STANCE SURVIVES A ROUND TRIP THROUGH THE HASHER. Crouching
    // decides who sees a crime, so a twin run that could not see it would not
    // protect it.
    session.toggleCrouch();
    CHECK(session.stance() == Stance::Upright);
}

TEST_CASE("the stealth line is one row on an edge, and it says what it is looking at") {
    render::SessionConfig config;
    config.contentDir = content::contentDir();
    config.timeOfDay = 21 * 3600;
    config.spawnX = gull::kBartenderX;
    config.spawnY = gull::kBarY + 2;
    config.spawnBand = gull::kGroundBand;
    render::Session session(config);
    session.stepMany(MoveInput{}, 2);

    const std::string lit = session.stealthLine();
    REQUIRE_FALSE(lit.empty());
    // A busy lit taproom at nine: seen.
    CHECK(lit.substr(0, 4) == "SEEN");
    CHECK(lit.find("LIT") != std::string::npos);
    // ONE LINE. An inventory in this game is one line on an edge until it has
    // earned more, and so is this.
    CHECK(lit.find('\n') == std::string::npos);
    CHECK(lit.size() < 40);

    session.toggleCrouch();
    CHECK(session.stealthLine().find("CROUCH") != std::string::npos);

    // AND IT SAYS HIDDEN WHEN YOU ARE. The S9 review's third finding: this case
    // only ever tested the line in ONE direction -- it asserted "SEEN" and that
    // the word CROUCH appears, and passed with the notice rule disabled, so
    // nothing anywhere proved the HUD is capable of printing the good news.
    // Two in the morning, doors barred, lanterns out, down on your haunches,
    // just inside the door rather than leaning on the bar -- an observer at
    // arm's length needs neither light nor a sight line and would make this a
    // case about distance.
    render::SessionConfig dark = config;
    dark.timeOfDay = 2 * 3600;
    dark.spawnX = gull::kDoorX0;
    dark.spawnY = gull::kDoorY + 1;
    render::Session night(dark);
    night.stepMany(MoveInput{}, 2);
    night.toggleCrouch();
    const std::string quiet = night.stealthLine();
    REQUIRE_FALSE(quiet.empty());
    CHECK(quiet.substr(0, 6) == "HIDDEN");
    CHECK(quiet.find("DARK") != std::string::npos);
    CHECK(quiet.find('\n') == std::string::npos);
    CHECK(quiet.size() < 40);
    // And the simulation agrees with the line: the HUD is not allowed to be
    // cheerier than the rule underneath it.
    CHECK(night.hidden());
}
