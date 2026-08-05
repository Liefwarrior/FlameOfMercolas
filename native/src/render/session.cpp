#include "granadad/render/session.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <utility>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/render/capture.hpp"
#include "granadad/sim/angle.hpp"
#include "granadad/sim/build_info.hpp"
#include "granadad/sim/docks.hpp"

namespace granadad::render {

namespace {

constexpr float kPi = 3.14159265358979323846F;

[[nodiscard]] SessionConfig resolved(const SessionConfig& in) {
    SessionConfig out = in;
    if (out.contentDir.empty()) {
        out.contentDir = content::contentDir();
    }
    if (out.spawnX < 0 || out.spawnY < 0 || out.spawnBand < 0) {
        out.spawnX = sim::docks::kSpawnTileX;
        out.spawnY = sim::docks::kSpawnTileY;
        out.spawnBand = sim::docks::kSpawnBand;
        if (!out.spawnYawGiven) {
            out.spawnYaw = sim::docks::kSpawnYaw;
        }
    }
    out.clockScale = std::max(1, out.clockScale);
    return out;
}

/// What a role looks like in a frame at 320x180. Chunky and readable beats
/// accurate: the whole visual target is silhouettes in lamplight, and a
/// bartender you can tell from a bouncer at eight tiles is worth more than a
/// face nobody can see.
struct RoleLook {
    /// Coat and hat.
    Rgb torso;
    /// Legs, and whatever the boots are.
    Rgb legs;
    Rgb head;
    /// Scales the whole figure. A bouncer is a bigger shape at ten tiles.
    float build;
};

[[nodiscard]] RoleLook lookOf(sim::ActorRole role) noexcept {
    switch (role) {
        case sim::ActorRole::Bartender:
            // Apron pale over the trade's ochre: the brightest figure in the
            // room, which is what you want the one behind the bar to be.
            return {Rgb{0.80F, 0.74F, 0.56F}, Rgb{0.34F, 0.27F, 0.20F},
                    Rgb{0.62F, 0.46F, 0.34F}, 1.00F};
        case sim::ActorRole::Innkeeper:
            return {Rgb{0.66F, 0.50F, 0.28F}, Rgb{0.30F, 0.24F, 0.18F},
                    Rgb{0.62F, 0.46F, 0.36F}, 1.02F};
        case sim::ActorRole::Bouncer:
            // Bigger, darker, and you can see it coming across a room.
            return {Rgb{0.30F, 0.27F, 0.26F}, Rgb{0.20F, 0.18F, 0.17F},
                    Rgb{0.56F, 0.40F, 0.30F}, 1.20F};
        case sim::ActorRole::PriestOfTheFlame:
            // The white garb the ward distrusts (DOCKS-GAZETTEER §4), head to
            // foot, which is exactly why it is not a welcome sight out here.
            return {Rgb{0.90F, 0.88F, 0.82F}, Rgb{0.84F, 0.82F, 0.76F},
                    Rgb{0.64F, 0.48F, 0.36F}, 0.98F};
        case sim::ActorRole::SkyrunnerContact:
            // Grey on grey, keeping to the corner, hard to pick out. Deliberate.
            return {Rgb{0.22F, 0.23F, 0.25F}, Rgb{0.17F, 0.18F, 0.20F},
                    Rgb{0.40F, 0.33F, 0.29F}, 0.92F};
        case sim::ActorRole::Vermin:
            // Low, dark and small enough to be missed until it moves. A third
            // of a person's height is what makes a rat read as a rat at ten
            // tiles without a single new sprite.
            return {Rgb{0.19F, 0.17F, 0.16F}, Rgb{0.15F, 0.13F, 0.13F},
                    Rgb{0.24F, 0.20F, 0.19F}, 0.34F};
        case sim::ActorRole::Patron:
        default:
            return {Rgb{0.46F, 0.36F, 0.26F}, Rgb{0.26F, 0.21F, 0.17F},
                    Rgb{0.58F, 0.43F, 0.32F}, 1.00F};
    }
}

/// THE KEYS, AS A TOPIC LIST.
///
/// One row a verb, in the order a player learns them: move, look at the world,
/// talk to it, and then the four things a burglar does. It pages nine at a time
/// off the numbers, exactly like a long conversation, so this list can grow to
/// any length without a row falling off the bottom with nothing on screen
/// saying so -- which is the bug the S3 review found in the topic grid and the
/// reason kTopicPageSize is nine.
///
/// IT IS HERE AND NOT IN THE CLIENT because the client owns no game logic and
/// because a keyboard reference that lives beside the SDL bindings drifts from
/// them the moment somebody rebinds one without looking down. This is drawn
/// from the same Session verbs the client calls.
///
/// AND EVERY ROW FITS ITS COLUMN. The grid is three columns of about sixteen
/// characters -- Master Venn's twelve topics are what sized it -- and the first
/// S10 capture of this page shipped "SPACE  UP: MANT." and "E  TALK TO WHOE.".
/// A controls page that arrives truncated is worse than none, because a player
/// reads the truncation as the binding.
const char* const kKeyRows[] = {
    "W A S D  WALK",
    "MOUSE  LOOK",
    "SHIFT  RUN",
    "C  CROUCH",
    "SPACE  UP",
    "X  DOWN",
    "Q  LOOK AT IT",
    "J  CASEBOOK",
    "E  TALK",
    "G  HANDS ON IT",
    "T  PICK A PURSE",
    "F  PUNCH",
    "R  SLEEP",
    "1-9  PICK ROW",
    "0  NEXT PAGE",
    "ESC  BACK OUT",
    "TAB  FREE MOUSE",
    "F12  SCREENSHOT",
    "LOCK: W S  AIM",
    "LOCK: SPACE TRY",
    "LOCK: F  FORCE",
    "LOCK: ESC  OUT",
};

}  // namespace

Session::Session(const SessionConfig& config)
    : config_(resolved(config)),
      world_(content::loadWorldFile(config_.contentDir / "maps" / "baked" /
                                    (config_.world + ".trojsav"))),
      tiles_(std::make_unique<sim::TileQuery>(world_)),
      atlas_(TileAtlas::load(config_.contentDir)) {
    renderer_ = std::make_unique<WorldRenderer>(*tiles_, atlas_,
                                                loadLamps(config_.contentDir, config_.world));
    body_ = std::make_unique<sim::PlayerBody>(*tiles_, config_.spawnX, config_.spawnY,
                                              config_.spawnBand, config_.spawnYaw);
    // S5. The district declares its own floor: everything under the harbour
    // surface is unbuilt dungeon, and a body that fell into it could not climb
    // back out. See PlayerBody::setLandingFloor for the shaft this closes.
    body_->setLandingFloor(sim::docks::kLandingFloor);
    timeOfDay_ = ((config_.timeOfDay % sim::kSecondsPerDay) + sim::kSecondsPerDay) %
                 sim::kSecondsPerDay;
    settings_.timeOfDay = timeOfDay_;

    engine_ = std::make_unique<sim::PhasedEngine>(config_.worldSeed, world_);
    auto tavern =
        std::make_unique<sim::Tavern>(*tiles_, timeOfDay_, config_.worldSeed, config_.contentDir);
    tavern_ = tavern.get();
    engine_->register_system(std::move(tavern));
    // S8: AND THE WARD'S ROLL, ON THE SAME ENGINE. See Session::ward() -- the
    // compounds were built in S7 and never constructed by anything with a
    // window on it. The registry is held rather than borrowed because Ward
    // takes it by reference and a temporary here would dangle the moment the
    // constructor returned.
    who_ = std::make_unique<sim::NotableRegistry>(
        sim::NotableRegistry::load(config_.contentDir));
    auto ward = std::make_unique<sim::Ward>(config_.worldSeed, config_.contentDir, *who_);
    ward_ = ward.get();
    engine_->register_system(std::move(ward));
    // The one wire between the two: a rival who rises far enough petitions the
    // Flame for a vacant charge, and the roll is where that becomes true.
    tavern_->attachRoll(ward_);
    engine_->boot();
    // S10: AND THE CASE. The bloodletter trail is the reason to be in the
    // district at all -- see sim/casebook.hpp. The raws are a member because
    // Casebook borrows them for its whole life, and a missing casebook.json
    // leaves an empty trail rather than refusing to boot, which is the contract
    // every raws loader in this build honours.
    caseRaws_ = sim::CasebookRaws::load(config_.contentDir);
    casebook_.begin(caseRaws_);
    // THE FIRST RUN OPENS ON THE HOOK.
    //
    // Every sprint before this one dropped the player onto the Tarwalk facing a
    // door with no idea who they were or what they were for. So a fresh session
    // starts with the notes up: the case, the body that started it, and the one
    // lead the ward has given you. Anything the player does closes it -- see
    // Session::step and the four verbs -- so it costs a keypress at most and
    // never gets in the way twice.
    if (config_.openingPage && caseRaws_.loaded()) {
        casebookOpen_ = true;
        message_ = "J YOUR NOTES   F1 THE KEYS   Q LOOK AT WHAT IS HERE";
        messageSteps_ = 60 * 12;
    }
    syncTavernToBody();
}

void Session::syncTavernToBody() {
    tavern_->setPlayer(body_->x(), body_->y(), body_->band());
}

// ---------------------------------------------------------------------------
// S5: the roof verbs
// ---------------------------------------------------------------------------

void Session::settleLanding(const sim::RoofResult& move) {
    // THE CHARGE IS THE ROOM'S. Everything a landing moves -- the craft, the
    // hit points, the roof-run, the counted verb -- is simulation state, and it
    // moved out of this file in S6 so the simulation suite can drive it and a
    // mutation to any clause of it can go red. See Tavern::settleLanding.
    const sim::Tavern::LandingResult charged =
        tavern_->settleLanding(move, body_->takeFallBands(), body_->band());
    if (charged.hurt > 0) {
        roofMove_ += " - " + std::to_string(charged.hurt) + " HURT";
    }
}

void Session::climb() {
    dismissOverlays();
    if (talking()) {
        return;
    }
    sim::RoofResult move = body_->mantle();
    bool leapt = false;
    if (!move.ok()) {
        const sim::DialogueDirector& talk = tavern_->dialogue();
        const std::int32_t roofs = talk.factions().indexOf("skyrunners");
        move = body_->leap(sim::leapReachTiles(talk.skills().level(sim::kRoofSkill),
                                               talk.standings().unlocked(roofs, "roof")));
        leapt = move.ok();
    }
    if (!move.ok()) {
        roofMove_ = std::string("NO WAY UP - ") + std::string(sim::roofMoveName(move.move));
        say(roofMove_);
        return;
    }
    if (leapt) {
        // A LEAP IS WATCHED, NOT TELEPORTED, and this is the S5 review's second
        // finding closed. S5 shipped a `while (body_->airborne()) step()` right
        // here, inside the keypress: the arc ran to its end before the frame
        // that showed the jump was ever drawn, so every leap in real play was
        // instant -- and it burned twenty-four movement steps of tavern clock
        // inside one frame while it did it. player.hpp:227 says a leap is
        // "something the player watches happen rather than a teleport with a
        // sound effect" and test_roofrun.cpp asserts it of PlayerBody; the
        // client then threw the arc away.
        //
        // So the press ARMS the leap and nothing more. The ordinary step pump
        // -- the client's, a capture script's, a test's -- flies it, and the
        // landing is settled in step() at the moment the feet touch, which is
        // also the only moment takeFallBands() has anything to report.
        roofMove_ = "OVER " + std::to_string(move.tiles) + " TILES";
        say(roofMove_);
        pendingLanding_ = move;
        awaitingLanding_ = true;
        syncTavernToBody();
        return;
    }
    roofMove_ = "UP ONTO THE LEDGE";
    say(roofMove_);
    settleLanding(move);
    syncTavernToBody();
}

void Session::dropDown() {
    dismissOverlays();
    if (talking()) {
        return;
    }
    const sim::RoofResult move = body_->dropOff();
    if (!move.ok()) {
        roofMove_ = std::string("NOTHING TO DROP TO - ") + std::string(sim::roofMoveName(move.move));
        say(roofMove_);
        return;
    }
    roofMove_ = "DOWN " + std::to_string(move.bands) + " LEVEL(S)";
    say(roofMove_);
    settleLanding(move);
    syncTavernToBody();
}

void Session::steal() {
    dismissOverlays();
    if (talking()) {
        return;
    }
    syncTavernToBody();
    sim::Tavern::StealResult took = tavern_->crackStrongbox();
    // S9. THE WIRE GOES IN FIRST. A locked box used to open to this key; it now
    // refuses with Refused, so the key puts the wire in instead and the player
    // works the lock. Pressing it again once the lock has given empties the
    // box, which is what this key always did.
    //
    // Refused is ALSO the answer for a room the player rented -- "THAT ONE IS
    // YOURS" -- and beginPick refuses that room for the same reason, so a
    // player standing at their own bed-foot is told so once rather than being
    // handed a wire they cannot use.
    if (took.result == sim::ServiceResult::Refused && !tavern_->picking().open()) {
        const sim::Tavern::PickResult started = tavern_->beginPick();
        say(started.result == sim::ServiceResult::Served ? started.line : took.line);
        return;
    }
    if (took.result == sim::ServiceResult::TooFar) {
        // Nothing to open here. The other things hands can be put on are a bale
        // in the snug and, since S6, a rat on the floor -- which is the ward's
        // own source of the one contraband the ward pays a bounty ON.
        took = tavern_->handleBale();
    }
    if (took.result == sim::ServiceResult::TooFar) {
        took = tavern_->takeScalp();
    }
    if (took.result == sim::ServiceResult::TooFar) {
        // And the last thing a pair of hands can do standing next to somebody:
        // buy the wire that opens everything above. Refused for anyone who is
        // not one of the roofs, which is what the guild's first rung buys.
        took = tavern_->buyPicks();
    }
    say(took.line);
}

// ---------------------------------------------------------------------------
// S9: crouching, lifting, and the wire
// ---------------------------------------------------------------------------

void Session::toggleCrouch() {
    dismissOverlays();
    if (talking()) {
        return;
    }
    tavern_->toggleStance();
    say(tavern_->stance() == sim::Stance::Crouched ? "CROUCHED" : "UPRIGHT");
}

sim::Stance Session::stance() const noexcept { return tavern_->stance(); }

bool Session::hidden() const noexcept { return tavern_->hidden(); }

std::string Session::stealthLine() const {
    const sim::Notice worst = tavern_->worstNotice();
    const std::int32_t light = tavern_->lightOnPlayer();
    const std::int32_t noise = tavern_->playerNoise();
    std::string line = worst.seen ? "SEEN" : "HIDDEN";
    if (tavern_->stance() == sim::Stance::Crouched) {
        line += " CROUCH";
    }
    // The two numbers a player can actually do something about, in the words
    // they would use. Deliberately short: this is an EDGE line, not a sheet.
    line += light >= 50 ? "  LIT " : "  DARK ";
    line += std::to_string(light);
    if (noise >= sim::kNoiseRunning) {
        line += "  LOUD";
    } else if (noise > 0) {
        line += "  HEARD";
    } else {
        line += "  QUIET";
    }
    return line;
}

std::string Session::lockLine() const {
    const sim::Lockpicking& wire = tavern_->picking();
    if (!wire.open()) {
        return {};
    }
    // THE WHOLE MINIGAME, IN ONE ROW OF 4x6 GLYPHS. A pin that has dropped is
    // a star and one still up is a dash; the depth track is nine dots with the
    // pick standing on one of them. Nothing here needs a panel, and a panel is
    // what the Java build's first-person view died of.
    std::string line = "LOCK  PINS ";
    for (std::int32_t i = 0; i < wire.lock().pins; ++i) {
        line += i < wire.pinsSet() ? '*' : '-';
    }
    line += "  DEPTH ";
    for (std::int32_t d = 0; d < sim::kPinDepths; ++d) {
        // '+' and not '#': the 4x6 font in hud.cpp has fifty-two glyphs and a
        // hash is not one of them, so the pick's own position drew as a hole in
        // the track. Caught by looking at the frame, which is the point of
        // looking at the frame.
        line += d == wire.depth() ? '+' : '.';
    }
    line += "  STRAIN " + std::to_string(wire.strain()) + "/" +
            std::to_string(wire.strainLimit());
    line += "  PICKS " + std::to_string(tavern_->picks());
    return line;
}

void Session::lift() {
    dismissOverlays();
    if (talking()) {
        return;
    }
    syncTavernToBody();
    say(tavern_->liftFrom().line);
}

// ---------------------------------------------------------------------------
// S10: the investigation
// ---------------------------------------------------------------------------

sim::Legend Session::legend() const {
    const sim::DialogueDirector& talk = tavern_->dialogue();
    return sim::legendOf(talk.crimes(), talk.skills(), talk.standings(), talk.contracts(),
                         casebook_);
}

void Session::examine() {
    if (talking() || picking()) {
        return;
    }
    if (casebookOpen_) {
        // Q closes the notes as well as opening a lead, so a player who has the
        // book up and presses the look key gets the world back rather than
        // nothing at all.
        toggleCasebook();
        return;
    }
    // THE FLAME'S EYE IS THE ONE BOON THAT REACHES THE TRAIL. legend.hpp names
    // this call site as the only one; a Wielder the ward defers to reads a
    // scene from the doorway instead of standing over it.
    const std::int32_t reach = sim::kLookRangeTiles + legend().lookRangeBonus();
    // The casebook takes a range in tiles and looks for itself, so the bonus is
    // applied by widening the search here rather than by handing the simulation
    // a number the surface computed -- which is why look() is asked twice at
    // most and never asked to trust a caller's arithmetic.
    const std::int32_t tileX = body_->tileX();
    const std::int32_t tileY = body_->tileY();
    const std::int32_t band = body_->band();
    sim::LookResult saw = casebook_.look(tileX, tileY, band);
    if (!saw.found && saw.lead < 0 && reach > sim::kLookRangeTiles) {
        // Nothing within the base reach. Walk outward one ring at a time up to
        // the bonus, standing the look at each offset -- integer, bounded, and
        // it cannot see anything a body one tile further along could not.
        for (std::int32_t ring = 1; ring <= reach - sim::kLookRangeTiles && saw.lead < 0;
             ++ring) {
            const std::int32_t offsets[4][2] = {
                {ring, 0}, {-ring, 0}, {0, ring}, {0, -ring}};
            for (const auto& offset : offsets) {
                saw = casebook_.look(tileX + offset[0], tileY + offset[1], band);
                if (saw.lead >= 0) {
                    break;
                }
            }
        }
    }
    // THE CLUE IS THE MESSAGE. It is what the player walked here for, so it
    // gets the row whole; how many leads it opened is on the CASE row, which is
    // permanent and where a count belongs. A DEAD END SAYS SO OUT LOUD, though
    // -- walking across the district to learn that the sea is the wrong
    // question is work, and a game that let that read the same as a blank tile
    // would be a game telling you not to look.
    const bool cold = saw.found && saw.opened == 0;
    say(cold ? saw.line + "  (COLD)" : saw.line);
}

/// PUTS THE NOTES AND THE KEY LIST DOWN. Called by every verb that acts on the
/// world, so a player who presses a game key with a menu up gets the game and
/// not a silent refusal -- and so the opening page can never be in the way of
/// the second thing a new player does.
void Session::dismissOverlays() noexcept {
    casebookOpen_ = false;
    keysOpen_ = false;
    firstRun_ = false;
}

void Session::toggleKeys() {
    if (talking() || picking()) {
        return;
    }
    keysOpen_ = !keysOpen_;
    if (keysOpen_) {
        casebookOpen_ = false;
    }
    firstRun_ = false;
    caseCursor_ = 0;
    casePage_ = 0;
    caseEntry_ = -1;
}

void Session::toggleCasebook() {
    if (talking() || picking()) {
        return;
    }
    keysOpen_ = false;
    firstRun_ = false;
    casebookOpen_ = !casebookOpen_;
    caseCursor_ = 0;
    casePage_ = 0;
    caseEntry_ = -1;
}

bool Session::picking() const noexcept { return tavern_->picking().open(); }

const sim::Lockpicking& Session::lockpicking() const noexcept { return tavern_->picking(); }

int Session::picks() const noexcept { return tavern_->picks(); }

void Session::movePick(int delta) { tavern_->movePick(delta); }

void Session::probeLock() {
    const sim::Tavern::PickResult felt = tavern_->probeLock();
    say(felt.line);
}

void Session::forceLock() {
    syncTavernToBody();
    say(tavern_->forceLock().line);
}

void Session::stopPicking() {
    if (!tavern_->picking().open()) {
        return;
    }
    tavern_->abandonPick();
    say("WIRE OUT. IT RELOCKS.");
}

void Session::settleDefeat() {
    // THE PLAYER RESPAWNS AS THEMSELVES AND THE WORLD KEEPS THE CONSEQUENCES.
    // Eli ruled 2026-07-31 that the persistent-ward variant -- the city
    // surviving your death while you come back as somebody else -- is not being
    // built from the start. So this is the whole of the respawn: the room gives
    // the hit points back and moves the clock on, and the body wakes up on the
    // quay apron, which is the same two tiles clear of the threshold a man put
    // out of the door ends up on.
    //
    // WHAT IT DOES NOT DO IS UNDO ANYTHING. The rung, the founded house, the
    // toll on every price and the name on the roll all survive this call. That
    // is the design.
    const sim::Rise& rise = tavern_->lastDefeat();
    tavern_->reviveAfterDefeat();
    body_->placeAt(sim::gull::kStreetX, sim::gull::kStreetY, sim::gull::kGroundBand);
    awaitingLanding_ = false;
    syncTavernToBody();
    timeOfDay_ = tavern_->timeOfDay();
    settings_.timeOfDay = timeOfDay_;
    // What he said standing over you, out of the owner's tables -- and the
    // short report of what it made him when there is nothing authored.
    if (!rise.taunt.empty()) {
        say(rise.who + ": " + rise.taunt);
    } else if (!rise.line.empty()) {
        say(rise.line);
    }
    syncWardToCalendar();
}

void Session::step(const sim::MoveInput& input) {
    // The room moves first, then the shove it asked for is applied to the body
    // that owns its own collision, then the player's own input. That order is
    // deliberate: a bouncer's shove and a player's step in the same movement
    // step both go through PlayerBody, so neither can push the other through a
    // wall.
    // FIRST STEP CLOSES THE OPENING PAGE. The session boots with the casebook
    // up so a new player is told what the case is before they are told
    // anything else; the moment they walk, they have read it and it gets out
    // of the way. It never reopens itself.
    if (firstRun_ && (input.forward != 0 || input.strafe != 0)) {
        firstRun_ = false;
        casebookOpen_ = false;
        caseCursor_ = 0;
        casePage_ = 0;
        caseEntry_ = -1;
    }
    syncTavernToBody();
    tavern_->stepMovement();
    const std::int32_t shoveX = tavern_->takePlayerShoveX();
    const std::int32_t shoveY = tavern_->takePlayerShoveY();
    if (shoveX != 0 || shoveY != 0) {
        body_->push(shoveX, shoveY);
    }
    // S9. THE STANCE IS THE ROOM'S AND THE BODY OBEYS IT. Crouching is
    // simulation state -- it decides who sees a crime -- so the tavern owns it
    // and the body is TOLD, rather than the client keeping a second copy of it
    // that the hasher cannot see. The room is told back how the body is moving,
    // which is how footfalls reach the notice rule.
    sim::MoveInput moved = input;
    moved.crouch = tavern_->stance() == sim::Stance::Crouched;
    const bool walking = moved.forward != 0 || moved.strafe != 0;
    tavern_->setPlayerMotion(walking, moved.run && !moved.crouch);
    body_->step(moved);
    syncTavernToBody();

    // THE WATCH TOOK YOU AND HAS LET YOU GO. The room owns the sentence, the
    // seizure and the clock; the BODY is this file's, so the walk to the
    // impound and the morning at its gate happen here -- which is to say they
    // do not happen at all, and that is stated rather than implied.
    if (tavern_->takeArrestRelease()) {
        body_->placeAt(sim::gull::kStreetX, sim::gull::kStreetY, sim::gull::kGroundBand);
        awaitingLanding_ = false;
        syncTavernToBody();
        say(tavern_->lastArrest().line);
    }

    // AND SOMEBODY PUT YOU ON THE FLOOR. Same shape, same reason: the room owns
    // the beating, the rise and the clock, and the body is this file's.
    if (tavern_->takeDefeatRelease()) {
        settleDefeat();
    }

    // THE ARC CAME DOWN. A leap armed by climb() is settled HERE, on the step
    // the feet touch, because that is the only step on which the body has a
    // fall to report -- takeFallBands() is written by PlayerBody::land and read
    // exactly once. Charging it at the keypress, as S5 did, meant charging it
    // before the fall existed.
    if (awaitingLanding_ && !body_->airborne()) {
        awaitingLanding_ = false;
        settleLanding(pendingLanding_);
        syncTavernToBody();
    }

    if (messageSteps_ > 0 && --messageSteps_ == 0) {
        message_.clear();
    }

    // One engine tick a simulated second. clockScale > 1 makes the world's
    // clock run faster than the body's, which is how a capture reaches a named
    // hour without simulating the whole afternoon.
    if (++stepsThisSecond_ >= sim::kStepsPerSecond) {
        stepsThisSecond_ = 0;
        for (int i = 0; i < config_.clockScale; ++i) {
            engine_->tick();
            ++elapsedSeconds_;
        }
        timeOfDay_ = tavern_->timeOfDay();
        settings_.timeOfDay = timeOfDay_;
    }
    syncWardToCalendar();
}

void Session::stepMany(const sim::MoveInput& input, int steps) {
    for (int i = 0; i < steps; ++i) {
        step(input);
    }
}

int Session::flyOutLeap() {
    int steps = 0;
    // Bounded by construction -- kLeapStepsPerTile * the longest reach any
    // teaching buys -- but bounded HERE as well, because a loop whose exit
    // depends on simulation state is a loop that hangs a build the day that
    // state is wrong.
    constexpr int kCeiling = 4 * sim::kLeapStepsPerTile * sim::kLeapReachTiles;
    while (body_->airborne() && steps < kCeiling) {
        step(sim::MoveInput{});
        ++steps;
    }
    return steps;
}

void Session::say(std::string line) {
    // Clipped to what the bottom edge can hold at the narrowest resolution this
    // game runs at. S4 started routing a questline's journal prose through here
    // -- whole sentences out of the raws -- and the first capture of it ran off
    // the right edge mid-word, which looks like a bug because it is one.
    //
    // S10 RAISED IT FROM 56 TO 92. Fifty-six was the column count of a 320x180
    // frame, and it was the belt to the HUD's own braces -- drawHud clips the
    // alert with clipToWidth against the REAL frame width, which is the fix the
    // S6 review's cut-mid-glyph finding produced. So the guess here was
    // truncating a clue at 640x360, where the row holds far more, for no reason
    // but its own age. The HUD still clips; this only stops a runaway string
    // being carried around.
    constexpr std::size_t kAlertColumns = 92;
    if (line.size() > kAlertColumns) {
        line.resize(kAlertColumns);
        line += "..";
    }
    message_ = std::move(line);
    // Six seconds on screen. Long enough to read at a glance, short enough that
    // the bottom of the frame is usually empty.
    messageSteps_ = 6 * sim::kStepsPerSecond;
}

bool Session::talking() const noexcept {
    return tavern_->dialogue().isOpen();
}

bool Session::haggling() const noexcept {
    return tavern_->dialogue().isHaggling();
}

void Session::interact() {
    dismissOverlays();
    if (talking()) {
        chooseTopic(static_cast<std::size_t>(std::max(0, topicCursor_)));
        return;
    }
    if (!tavern_->talkTo()) {
        say("NOBODY WITHIN REACH");
        return;
    }
    topicCursor_ = 0;
    haggleOffer_ = 0;
    const sim::DialogueDirector& talk = tavern_->dialogue();
    say(talk.speaker().name + ": " + talk.greeting());
}

void Session::moveTopicCursor(int delta) {
    if (keysOpen_) {
        const int count = static_cast<int>(sizeof(kKeyRows) / sizeof(kKeyRows[0]));
        caseCursor_ = ((caseCursor_ + delta) % count + count) % count;
        casePage_ = caseCursor_ / kTopicPageSize;
        return;
    }
    if (casebookOpen_) {
        // THE SAME KEYS, THE SAME PAGING, THE SAME BAND. The casebook is a
        // conversation with your own notes -- see Session::toggleCasebook on
        // why it borrows the dialogue surface rather than opening a sheet in
        // the middle of the screen.
        const int count = static_cast<int>(casebook_.known().size());
        if (count <= 0) {
            caseCursor_ = 0;
            casePage_ = 0;
            return;
        }
        caseCursor_ = ((caseCursor_ + delta) % count + count) % count;
        casePage_ = caseCursor_ / kTopicPageSize;
        return;
    }
    if (!talking()) {
        return;
    }
    const int count = static_cast<int>(tavern_->dialogue().topics().size());
    if (count <= 0) {
        topicCursor_ = 0;
        topicPage_ = 0;
        return;
    }
    // Wraps, so holding one direction walks the whole list.
    topicCursor_ = ((topicCursor_ + delta) % count + count) % count;
    // The page FOLLOWS the cursor. Walking off the bottom of a page turns it,
    // so the arrow keys reach every topic and the numbers on screen are always
    // the numbers that pick the ones you can see.
    topicPage_ = topicPageOf(topicCursor_);
}

void Session::nextTopicPage() {
    if (keysOpen_) {
        const std::size_t rows = sizeof(kKeyRows) / sizeof(kKeyRows[0]);
        const int pages = topicPageCount(rows);
        casePage_ = (casePage_ + 1) % pages;
        caseCursor_ = std::min(static_cast<int>(rows) - 1, casePage_ * kTopicPageSize);
        return;
    }
    if (casebookOpen_) {
        const std::size_t entries = casebook_.known().size();
        const int pages = topicPageCount(entries);
        if (pages <= 1) {
            return;
        }
        casePage_ = (casePage_ + 1) % pages;
        caseCursor_ = std::min(static_cast<int>(entries) - 1, casePage_ * kTopicPageSize);
        return;
    }
    if (!talking()) {
        return;
    }
    const std::size_t count = tavern_->dialogue().topics().size();
    const int pages = topicPageCount(count);
    if (pages <= 1) {
        return;
    }
    topicPage_ = (topicPage_ + 1) % pages;
    // The cursor comes with it, onto the first topic of the new page, so E
    // never picks something that is not on screen.
    topicCursor_ = std::min(static_cast<int>(count) - 1, topicPage_ * kTopicPageSize);
}

void Session::chooseVisibleTopic(int slot) {
    if (slot < 0 || slot >= kTopicPageSize) {
        return;
    }
    if (keysOpen_) {
        // A key row is a reference, not a choice. The cursor moves and nothing
        // else happens, which is the honest behaviour for a list you read.
        const int index = casePage_ * kTopicPageSize + slot;
        if (index < static_cast<int>(sizeof(kKeyRows) / sizeof(kKeyRows[0]))) {
            caseCursor_ = index;
        }
        return;
    }
    if (casebookOpen_) {
        const int index = casePage_ * kTopicPageSize + slot;
        if (index >= static_cast<int>(casebook_.known().size())) {
            return;
        }
        caseCursor_ = index;
        caseEntry_ = index;
        return;
    }
    if (!talking()) {
        return;
    }
    const int index = topicPage_ * kTopicPageSize + slot;
    if (index >= static_cast<int>(tavern_->dialogue().topics().size())) {
        return;
    }
    topicCursor_ = index;
    chooseTopic(static_cast<std::size_t>(index));
}

void Session::chooseTopic(std::size_t index) {
    if (casebookOpen_) {
        // Reading an entry of your own notes. Nothing in the simulation moves;
        // this is the one place in the game where picking a row is pure UI, and
        // it is pure UI because the trail's state changed when you LOOKED, not
        // when you read your own handwriting back.
        if (index < casebook_.known().size()) {
            caseCursor_ = static_cast<int>(index);
            caseEntry_ = static_cast<int>(index);
        }
        return;
    }
    if (!talking()) {
        return;
    }
    const sim::Reply reply = tavern_->chooseTopic(index);
    if (!reply.ok && reply.line.empty()) {
        return;
    }
    if (reply.haggling) {
        // Open the argument at what they are asking, so the first press of a
        // key is a concession rather than a guess.
        haggleOffer_ = tavern_->dialogue().haggle().asking();
    }
    if (!reply.line.empty()) {
        say(tavern_->dialogue().speaker().name + ": " + reply.line);
    }
    if (reply.forging) {
        // The bench opens where the simulation put it.
        forgeOpen_ = true;
    }
    if (talking()) {
        const int count = static_cast<int>(tavern_->dialogue().topics().size());
        if (count > 0 && topicCursor_ >= count) {
            topicCursor_ = count - 1;
        }
        topicPage_ = std::min(topicPageOf(topicCursor_), topicPageCount(
                                                             static_cast<std::size_t>(count)) -
                                                             1);
    } else {
        topicCursor_ = 0;
        topicPage_ = 0;
    }
    if (!reply.journalLine.empty()) {
        say(reply.journalLine);
    }
}

void Session::closeConversation() {
    if (keysOpen_) {
        keysOpen_ = false;
        caseCursor_ = 0;
        casePage_ = 0;
        return;
    }
    if (casebookOpen_) {
        casebookOpen_ = false;
        caseCursor_ = 0;
        casePage_ = 0;
        caseEntry_ = -1;
        return;
    }
    tavern_->endConversation();
    topicCursor_ = 0;
    topicPage_ = 0;
    haggleOffer_ = 0;
    forgeOpen_ = false;
}

// ---------------------------------------------------------------------------
// the workbench
// ---------------------------------------------------------------------------

bool Session::forging() const noexcept {
    return tavern_->dialogue().isForging();
}

void Session::moveForgeField(int delta) {
    tavern_->dialogue().moveForgeField(delta);
}

void Session::adjustForge(int delta) {
    tavern_->dialogue().adjustForge(delta);
}

void Session::commitForge() {
    if (!forging()) {
        return;
    }
    const std::string who = tavern_->dialogue().speaker().name;
    const sim::Reply reply = tavern_->commitForge();
    if (!reply.line.empty()) {
        say(who + ": " + reply.line);
    }
    if (!reply.journalLine.empty()) {
        say(reply.journalLine);
    }
    forgeOpen_ = forging();
}

void Session::endForge() {
    if (!forging()) {
        return;
    }
    const sim::Reply reply = tavern_->endForge();
    if (!reply.line.empty()) {
        say(reply.line);
    }
    forgeOpen_ = false;
}

void Session::adjustOffer(int delta) {
    if (!haggling()) {
        return;
    }
    const int ceiling = std::max(1, tavern_->dialogue().haggle().asking());
    haggleOffer_ = std::clamp(haggleOffer_ + delta, 0, ceiling);
}

void Session::makeOffer() {
    if (!haggling()) {
        return;
    }
    const std::string who = tavern_->dialogue().speaker().name;
    const sim::Reply reply = tavern_->offerPrice(haggleOffer_);
    if (!reply.line.empty()) {
        say(who + ": " + reply.line);
    }
    haggleOffer_ = haggling() ? std::min(haggleOffer_, tavern_->dialogue().haggle().asking()) : 0;
}

void Session::takeAskingPrice() {
    if (!haggling()) {
        return;
    }
    const std::string who = tavern_->dialogue().speaker().name;
    const sim::Reply reply = tavern_->takeAskingPrice();
    if (!reply.line.empty()) {
        say(who + ": " + reply.line);
    }
    haggleOffer_ = 0;
}

DialogueViewState Session::dialogueView() const {
    DialogueViewState view;
    if (keysOpen_) {
        view.open = true;
        view.speaker = "CONTROLS";
        view.epithet = "GRANADAD: THE DARKSTREETS";
        // THREE LINES IS WHAT THE TOP BAND WRAPS TO, so this is written to fit
        // in two. The first version ran to four and lost its own last sentence.
        view.line =
            "THE DOCKS OF GRANADAD. THE DISTRICT KEEPS ITS OWN HOURS WHETHER YOU WATCH IT "
            "OR NOT. F1 PUTS THIS DOWN.";
        for (const char* row : kKeyRows) {
            view.topics.emplace_back(row);
        }
        view.cursor = caseCursor_;
        view.page = casePage_;
        return view;
    }
    if (casebookOpen_) {
        // THE CASEBOOK, DRAWN IN THE CONVERSATION'S SURFACE. Not a new panel
        // and not a sheet: the HUD rule is that the centre of the screen stays
        // clear, dialogue_view.hpp already owns a top band and a bottom band
        // with a case that proves the middle is untouched, and a journal is the
        // single most likely element in an RPG to break that rule. The Java
        // build's first-person view broke it exactly here.
        view.open = true;
        view.speaker = "THE CASEBOOK";
        view.epithet = std::string(caseRaws_.title());
        // THE ATTITUDE FIELD IS SHORT BY CONSTRUCTION -- in a conversation it
        // holds "WARM" or "HOSTILE" -- and the top-right of that band is where
        // the HUD draws the clock over it. The first S10 capture put a
        // twenty-nine character dread band there and the clock landed in the
        // middle of it. The ward's nerve moved down onto the line, where it is
        // the first thing you read in your own notes, which is also better.
        view.attitude = casebook_.closed() ? "CLOSED" : "OPEN";
        const std::vector<std::int32_t> heard = casebook_.known();
        const sim::Legend book = legend();
        if (caseEntry_ >= 0 && static_cast<std::size_t>(caseEntry_) < heard.size()) {
            const sim::Lead& lead =
                caseRaws_.leads()[static_cast<std::size_t>(heard[static_cast<std::size_t>(
                    caseEntry_)])];
            const sim::LeadState what =
                casebook_.state(heard[static_cast<std::size_t>(caseEntry_)]);
            view.line = what == sim::LeadState::Open
                            ? lead.place + ". " + lead.what + "."
                            : lead.found + " " + lead.detail;
        } else if (casebook_.readCount() == 0) {
            // THE OPENING PAGE OF A NEW GAME: the hook, and nothing else. It is
            // the first thing a player ever reads in this game and it gets the
            // band to itself.
            view.line = std::string(caseRaws_.hook());
        } else {
            // And afterwards: what the ward's nerve is doing, and what it calls
            // you for the work so far. TWO SHORT SENTENCES, because the band
            // wraps to three lines and the S10 capture that ran to four lost
            // "OF THE FLAME" off the end of its own title.
            view.line = std::string(caseRaws_.dreadLabel(casebook_.dread())) +
                        ". THEY CALL YOU " + std::string(book.title()) + ".";
        }
        for (const std::int32_t index : heard) {
            const sim::Lead& lead = caseRaws_.leads()[static_cast<std::size_t>(index)];
            std::string row;
            switch (casebook_.state(index)) {
                case sim::LeadState::Open:
                    row = "? ";
                    break;
                case sim::LeadState::Cold:
                    row = "X ";
                    break;
                case sim::LeadState::Followed:
                    row = "* ";
                    break;
                default:
                    row = "  ";
                    break;
            }
            // THE SHORT NAME, and it is authored rather than truncated here.
            // The topic grid is three columns and about fourteen characters a
            // cell -- Master Venn's twelve topics are what sized it -- so
            // "MISSION OF THE FLAME" arrived on the first S10 capture as
            // "MISSION OF.". casebook.json carries a `short` for every lead and
            // a case pins that all of them fit.
            view.topics.push_back(row + (lead.brief.empty() ? lead.place : lead.brief));
        }
        view.cursor = caseCursor_;
        view.page = casePage_;
        return view;
    }
    const sim::DialogueDirector& talk = tavern_->dialogue();
    if (!talk.isOpen()) {
        return view;
    }
    view.open = true;
    view.speaker = talk.speaker().name;
    view.epithet = talk.speaker().epithet;
    view.attitude = std::string(sim::attitudeName(talk.attitude()));
    view.line = talk.lastLine();
    view.cursor = topicCursor_;
    view.page = topicPage_;
    for (const sim::Topic& topic : talk.topics()) {
        view.topics.push_back(topic.label);
    }
    if (talk.isForging()) {
        const sim::ForgeBench& bench = talk.bench();
        view.forging = true;
        view.forgeCursor = bench.field;
        view.forgeDifficulty = bench.difficulty();
        view.forgeCeiling = sim::forgeCeilingFor(talk.skills().level(sim::kCraftingSkill));
        const sim::ForgeError problem = bench.error();
        if (problem != sim::ForgeError::None) {
            view.forgeProblem = std::string(sim::forgeErrorReason(problem));
        } else if (view.forgeDifficulty > view.forgeCeiling) {
            view.forgeProblem = std::string(sim::forgeErrorReason(sim::ForgeError::BeyondSkill));
        }
        for (int i = 0; i < sim::kForgeFieldCount; ++i) {
            view.forgeFields.push_back(bench.fieldLabel(i) + ": " + bench.fieldValue(i));
        }
    }
    if (talk.isHaggling()) {
        view.haggling = true;
        view.asking = talk.haggle().asking();
        view.offer = haggleOffer_;
        view.patience = talk.haggle().patience();
        view.goods = std::string(sim::goodsName(talk.haggle().terms().goods));
    }
    return view;
}

void Session::punch() {
    dismissOverlays();
    const sim::Tavern::PunchResult result = tavern_->playerPunchNearest();
    if (!result.swung) {
        say("NOTHING IN REACH");
        return;
    }
    if (!sim::resolvesInWorld(result.fight)) {
        // The one transition this game has, and S2 does not have the screen it
        // transitions to. Saying so out loud is better than resolving a knife
        // fight with fist rules and hoping nobody notices.
        say("BLADE OUT - THIS IS NOT A BRAWL");
        return;
    }
    if (result.blow.downed) {
        say(result.targetName + " GOES DOWN");
    } else if (result.blow.landed) {
        say("HIT " + result.targetName + " FOR " + std::to_string(result.blow.damage));
    } else {
        say("MISSED " + result.targetName);
    }
}

void Session::restHere() {
    dismissOverlays();
    const sim::ServiceResult slept = tavern_->sleep();
    if (slept == sim::ServiceResult::Served) {
        timeOfDay_ = tavern_->timeOfDay();
        settings_.timeOfDay = timeOfDay_;
        stepsThisSecond_ = 0;
        syncWardToCalendar();
        say("SLEPT UNTIL MORNING");
        return;
    }
    say(std::string("CANNOT REST HERE - ") + std::string(sim::serviceResultName(slept)));
}

void Session::skipToHour(int hour) {
    const int wrapped = ((hour % 24) + 24) % 24;
    tavern_->skipTo(wrapped * 3600);
    timeOfDay_ = tavern_->timeOfDay();
    settings_.timeOfDay = timeOfDay_;
    stepsThisSecond_ = 0;
    syncWardToCalendar();
}

void Session::syncWardToCalendar() {
    // THE TAVERN'S CALENDAR IS THE WORLD'S CALENDAR, and the ward follows it.
    //
    // S7 REVIEW FINDING #8, CLOSED. Ward::tick counts a second per engine tick
    // and turns a day at 86,400 of them; every skip in this file -- sleeping in
    // a rented bed, a night in a cell, the blackout after a beating, a scripted
    // capture reaching a named hour -- moved the TAVERN'S clock and simulated
    // none of the seconds it jumped, so the ward never heard about any of them.
    // Ten slept nights left stats().days at zero and the whole compound economy
    // had never run one day inside the windowed game.
    //
    // Tavern::dayNumber() is monotonic across midnight AND across a skip, which
    // is exactly the clock the ward wants. advanceToDay is idempotent, so this
    // may be called as often as it likes and the engine's own second counter
    // cannot double-count against it.
    if (ward_ != nullptr && tavern_ != nullptr) {
        ward_->advanceToDay(tavern_->dayNumber());
    }
}

Camera Session::camera() const noexcept {
    const float half = static_cast<float>(config_.fovDegrees) * 0.5F * kPi / 180.0F;
    return Camera::fromBody(body_->x(), body_->y(), body_->eyeZ(), body_->yaw(), body_->pitch(),
                            std::tan(half));
}

std::string Session::placeLabel() const {
    const std::int32_t x = body_->tileX();
    const std::int32_t y = body_->tileY();
    const std::int32_t band = body_->band();
    for (std::size_t i = 0; i < sim::docks::kPlaceCount; ++i) {
        const sim::docks::Place& place = sim::docks::kPlaces[i];
        if (band == place.band && x >= place.x0 && x <= place.x1 && y >= place.y0 &&
            y <= place.y1) {
            return place.name;
        }
    }
    // No street name, because there is no street name to give. Saying which
    // band you are on is the whole of what the z-level actually knows.
    if (band == sim::docks::kBandQuayside) {
        return "THE DOCKS - QUAYSIDE";
    }
    if (band == sim::docks::kBandMidSlope) {
        return "THE DOCKS - MID SLOPE";
    }
    if (band == sim::docks::kBandUpper) {
        return "THE DOCKS - UPPER";
    }
    if (band < sim::docks::kBandQuayside) {
        return "UNDER THE PIERS";
    }
    return "THE DOCKS";
}

std::vector<Lamp> Session::tavernLights() const {
    // WHERE the flames are is the simulation's answer, derived from the baked
    // bytes (see gull::taproomTables). All this does is decide what each kind
    // of flame looks like -- brightness and colour, which are rendering, and
    // which are the only part of a light a renderer has any business owning.
    //
    // S2 hardcoded seven tile coordinates here with no derivation and no test.
    // The S2 review was right that this file had no business knowing them.
    std::vector<Lamp> lights;
    for (const sim::gull::HouseLight& light : tavern_->houseLights()) {
        Lamp lamp;
        lamp.x = light.x;
        lamp.y = light.y;
        lamp.z = light.band;
        switch (light.kind) {
            case sim::gull::LightKind::Hearth:
                lamp.name = "gull_hearth";
                lamp.luminance = 26;
                lamp.warmth = LampWarmth::Fire;
                break;
            case sim::gull::LightKind::Candle:
                lamp.name = "gull_candle";
                lamp.luminance = 17;
                lamp.warmth = LampWarmth::Fire;
                break;
            case sim::gull::LightKind::Lantern:
                // Cooler than the fire, so the room has two colours of light in
                // it and not one. The Gull is the captains' house and the
                // district's grandest room; charts on the walls are no use in
                // the dark, and a fifteen-tile interior lit by two hearth cells
                // and four candles reads as a cellar.
                lamp.name = "gull_lantern";
                lamp.luminance = 22;
                lamp.warmth = LampWarmth::Lantern;
                break;
        }
        lights.push_back(std::move(lamp));
    }
    return lights;
}

std::vector<SpriteInstance> Session::actorSprites() const {
    return actorSprites(camera());
}

std::vector<SpriteInstance> Session::actorSprites(const Camera& view) const {
    std::vector<SpriteInstance> sprites;
    const SkyState sky = skyAt(timeOfDay_);
    const std::vector<Lamp> live = tavernLights();
    for (const sim::Actor& actor : tavern_->actors()) {
        if (!actor.present()) {
            continue;
        }
        // Shaded by the light where they STAND, not lit from nowhere. Without
        // this a patron in an unlit corner glows like a lamp, which is the one
        // thing the committed-dark look cannot survive.
        const Rgb baked = renderer_->glow().at(actor.tileX(), actor.tileY(), actor.band());
        const Rgb dynamic = dynamicGlowAt(live, actor.tileX(), actor.tileY(), actor.band());
        // Clamped near 1: a figure standing in a lamp pool should be LIT, not
        // blown out into a featureless disc, which is what an unclamped
        // multiply does at close range.
        const Rgb light{std::min(1.15F, sky.ambient.r + std::max(baked.r, dynamic.r)),
                        std::min(1.15F, sky.ambient.g + std::max(baked.g, dynamic.g)),
                        std::min(1.15F, sky.ambient.b + std::max(baked.b, dynamic.b))};

        const RoleLook look = lookOf(actor.role());
        // Sub-tile Q8 straight out of the simulation. See actor.hpp: the
        // position between two tiles is the sim's, not the renderer's.
        const float px = static_cast<float>(actor.x()) / 256.0F;
        const float py = static_cast<float>(actor.y()) / 256.0F;
        // Through bandSurface(), not straight off the band number. A band is
        // three tiles of height now (sim/vertical_scale.hpp) and an actor whose
        // feet were placed at `band` stood a full two storeys below the floor
        // they were walking on.
        const float floorZ = bandSurface(actor.band());
        const bool down = actor.activity() == sim::Activity::Downed;
        const float build = look.build;

        // THREE stacked billboards -- legs, torso, head -- and not one blob.
        // A single ellipse at this resolution reads as an egg on the floor; a
        // narrow stack of three reads as a person from across a room, which is
        // the whole of what a Barony-grade sprite has to do.
        const auto part = [&](float height, float halfW, float halfH, const Rgb& tint) {
            SpriteInstance sprite;
            sprite.x = px;
            sprite.y = py;
            sprite.z = floorZ + height;
            sprite.halfWidth = halfW;
            sprite.halfHeight = halfH;
            sprite.colour = Rgb{tint.r * light.r, tint.g * light.g, tint.b * light.b};
            sprite.glow = 0.0F;
            // Hard-edged: chunky and readable, per the visual target.
            sprite.softness = 0.0F;
            // `person` is what the frame stats count apart from flames, and a
            // rat is not one. It is drawn, it is lit and it is in the frame --
            // it is simply not somebody, which is the same distinction
            // presentCount() makes in the room itself.
            sprite.person = actor.role() != sim::ActorRole::Vermin;
            sprites.push_back(sprite);
        };

        // THE FIGURE IS 1.875 TILES TALL, which is kStandingHeightTilesQ8 (480
        // Q8) out of sim/vertical_scale.hpp, the same header the player's own
        // eye height comes from. At roughly 0.9 m to the tile that is a 1.71 m
        // adult, and the crown lands a hand above the player's 1.70-tile eye —
        // so you look people in the mouth, which is correct and is the cheapest
        // proof the two scales agree.
        //
        // THEY DID NOT AGREE BEFORE. Every one of these heights used to be
        // about half what it is now: legs at 0.20, head at 0.83, a figure 0.93
        // of a tile from sole to crown. That was in scale with a district whose
        // storeys were one tile high, and it is why nothing in a frame gave the
        // eye anything to judge a building against. Tripling the storey without
        // this block would have left the Docks full of knee-high people.
        //
        // The widths grew too, and by less: shoulders at 0.50 of a tile (0.45 m)
        // inside a collision square 0.70 of a tile across, which is a person
        // shaped like a person instead of the near-square blob the half-height
        // version had to be to read at all.
        if (down) {
            // Flat out on the boards, and wide instead of tall.
            part(0.20F, 0.68F * build, 0.22F * build, look.torso);
            part(0.26F, 0.22F * build, 0.18F * build, look.head);
            continue;
        }
        part(0.475F, 0.19F * build, 0.475F * build, look.legs);
        part(1.285F, 0.25F * build, 0.335F * build, look.torso);
        part(1.750F, 0.11F * build, 0.125F * build, look.head);

        // THE FACE, and the reason it is here.
        //
        // Actor::faceToward() computes an eight-point facing and hashInto()
        // commits it to world state on every tick -- and until now no renderer
        // read it. A feature that exists only as data. The S2 review called
        // that out and it is the cheapest single step from "snowman" toward
        // the Barony bar: whether somebody is LOOKING AT YOU is the one thing
        // about a person you need to be able to read across a dark room.
        //
        // So: a small pale patch on the head, offset a hair toward whichever
        // way they are facing, drawn only when that way is roughly toward the
        // eye. Turn your back on them and it is gone. Purely presentational --
        // the facing itself belongs to the simulation and is never written here.
        const float facingRad =
            static_cast<float>(actor.facing()) * (2.0F * kPi / 65536.0F);
        // BAM 0 is north, which is -Y, and increases clockwise (sim/angle.hpp).
        const float faceX = std::sin(facingRad);
        const float faceY = -std::cos(facingRad);
        const float toEyeX = view.x - px;
        const float toEyeY = view.y - py;
        const float span = std::sqrt(toEyeX * toEyeX + toEyeY * toEyeY);
        if (span < 0.0001F) {
            continue;
        }
        // Cosine of the angle between where they look and where the eye is.
        const float towards = (faceX * toEyeX + faceY * toEyeY) / span;
        if (towards <= 0.15F) {
            continue;  // turned away; you get the back of a head
        }
        SpriteInstance face;
        // Pushed a fraction of a tile out of the head in the direction of gaze,
        // so a figure at an angle reads as being at an angle.
        face.x = px + faceX * 0.09F * build;
        face.y = py + faceY * 0.09F * build;
        // On the head, which is now centred at 1.750 with a half-height of
        // 0.125 — so the face patch has to ride at the same height and stay
        // inside it.
        face.z = floorZ + 1.775F;
        face.halfWidth = 0.070F * build;
        face.halfHeight = 0.068F * build;
        const float lift = 1.35F;
        face.colour = Rgb{std::min(1.0F, look.head.r * light.r * lift),
                          std::min(1.0F, look.head.g * light.g * lift),
                          std::min(1.0F, look.head.b * light.b * lift)};
        face.glow = 0.0F;
        face.softness = 0.0F;
        face.person = true;
        sprites.push_back(face);
    }
    return sprites;
}

namespace {

[[nodiscard]] std::string upperAscii(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        out.push_back(c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c);
    }
    return out;
}

/// Clipped to something the bottom-left corner can hold without walking across
/// the frame. The HUD hugs its edge; a quest tracker that runs to the middle of
/// the screen is the exact failure the Java build shipped.
[[nodiscard]] std::string clip(std::string text, std::size_t columns) {
    if (text.size() > columns) {
        text.resize(columns);
    }
    return text;
}

}  // namespace

std::string Session::guildLine() const {
    // NOTHING WHILE THE WIRE IS IN. The lock row is drawn at
    // height - margin - 31*scale; this one is drawn at 24*scale above the
    // health bar, which IS height - margin - 31*scale. The same pixel row.
    // docs/frames/s10-08-the-lock.png caught the two interleaved, reading "THE
    // 1OCKUNPINS -TENADEPTH ....+....". It is the same class of defect the S7
    // review found with the alert over the topic grid and it gets the same fix:
    // a MODE owns its row. The wire is a mode, and while it is in, which ladder
    // you are highest on is not what the player is reading.
    //
    // It is suppressed HERE and not at the draw call so a case can assert it --
    // a suppression that lives inside drawFrame is only checkable in pixels.
    if (picking()) {
        return {};
    }
    const sim::DialogueDirector& talk = tavern_->dialogue();
    const std::int32_t top = talk.standings().highestRankedFaction();
    if (top < 0) {
        return {};
    }
    const sim::Faction* faction = talk.factions().at(top);
    const std::string name = faction == nullptr ? std::string("GUILD") : faction->displayName;
    return clip(upperAscii(name) + " - " + upperAscii(talk.standings().rankTitle(top)), 34);
}

std::string Session::caseLine() const {
    if (!casebook_.active() || !caseRaws_.loaded()) {
        return {};
    }
    const std::vector<std::int32_t> heard = casebook_.known();
    std::string line = "CASE " + std::to_string(casebook_.readCount()) + "/" +
                       std::to_string(heard.size());
    // WHERE TO GO NEXT, ON THE SAME ROW. This is the orientation line: a player
    // who put the game down for a week and came back to a district of 692
    // people gets one line telling them where they were walking. Until this
    // sprint the corner of a new game was empty, which is exactly the "dropped
    // into a systems demo with no orientation" the demo brief names.
    const std::int32_t lead = casebook_.nextOpen();
    if (lead >= 0 && static_cast<std::size_t>(lead) < caseRaws_.leads().size()) {
        line += " > " + caseRaws_.leads()[static_cast<std::size_t>(lead)].place;
    } else if (casebook_.closed()) {
        line += " > " + std::string(caseRaws_.close());
    } else {
        const std::string_view mood = caseRaws_.dreadLabel(casebook_.dread());
        line += "  ";
        line.append(mood);
    }
    return clip(std::move(line), 40);
}

std::string Session::legendLine() const {
    const sim::Legend book = legend();
    if (book.totalRungs() <= 0) {
        return {};
    }
    return clip(std::string(book.title()) + "  " + std::to_string(book.totalRungs()) + " RUNGS",
                34);
}

std::string Session::heatLine() const {
    const sim::CrimeLedger& crimes = tavern_->dialogue().crimes();
    const sim::Stash& sack = crimes.stash();
    if (crimes.heat() <= 0 && crimes.loot() <= 0 && !crimes.carryingBale() && sack.empty() &&
        !crimes.maimed()) {
        return {};
    }
    std::string line;
    // The two the ward has done TO you outrank everything else on the line:
    // a condemned man wants to know he is one before he wants his heat.
    if (crimes.condemned()) {
        line = "CONDEMNED  ";
    } else if (crimes.maimed()) {
        line = "MAIMED  ";
    }
    if (crimes.warrant()) {
        line += "WANTED  ";
    }
    line += "HEAT " + std::to_string(crimes.heat());
    if (crimes.loot() > 0) {
        line += "  LOOT " + std::to_string(crimes.loot());
    }
    if (crimes.carryingBale()) {
        line += "  BALE";
    }
    return clip(std::move(line), 34);
}

std::string Session::rivalLine() const {
    // BOTTOM-LEFT, ON THE EDGE, ONE LINE. The HUD rule is not a preference
    // (COMBAT-FEEL-REFERENCE section 3): the centre stays empty and an
    // inventory, a guild or a rivalry is one row in a corner until it has
    // earned more. This is the only thing in the game that says the man across
    // the room is the man who put you here.
    const sim::Nemesis* worst = tavern_->nemesis().worst();
    if (worst == nullptr) {
        return {};
    }
    std::string line = "RIVAL " + upperAscii(worst->who);
    if (!worst->title.empty()) {
        line += " - " + upperAscii(worst->title);
    }
    line += " x" + std::to_string(worst->wins);
    if (worst->hunts()) {
        line += " HUNTING";
    }
    // 44 columns: 220 pixels at scale 1 against a 320-wide frame with a
    // six-pixel margin, so the longest line this can produce still fits its
    // edge. A HUD line that runs off the frame is the S6 defect, and it is not
    // being reintroduced from a different corner.
    return clip(std::move(line), 44);
}

std::string Session::stashLine() const {
    // WHAT IS ON YOU, AND WHAT IT WEIGHS. The weight is the number that matters
    // -- it is what a watchman's eye is on -- so it is on the line beside the
    // count rather than buried in a sheet.
    const sim::Stash& sack = tavern_->dialogue().crimes().stash();
    if (sack.empty()) {
        return {};
    }
    std::string line;
    for (std::size_t i = 0; i < sim::kContrabandCount; ++i) {
        const sim::Contraband good = static_cast<sim::Contraband>(i);
        const std::int32_t held = sack.count(good);
        if (held <= 0) {
            continue;
        }
        if (!line.empty()) {
            line += "  ";
        }
        line += std::to_string(held) + " " + std::string(sim::contrabandLabel(good));
    }
    if (sack.illicitWeight() > 0) {
        line += "  " + std::to_string(sack.illicitWeight()) + "DR";
    }
    return clip(std::move(line), 34);
}

std::string Session::contractLine() const {
    // The job with the least time left on it, because that is the one a player
    // needs to be reminded about.
    const sim::ContractBoard& board = tavern_->dialogue().contracts();
    const sim::Contract* soonest = nullptr;
    for (const sim::Contract& row : board.contracts()) {
        if (!row.live()) {
            continue;
        }
        if (soonest == nullptr || row.dueOnDay < soonest->dueOnDay ||
            (row.dueOnDay == soonest->dueOnDay && row.id < soonest->id)) {
            soonest = &row;
        }
    }
    if (soonest == nullptr) {
        return {};
    }
    const std::int32_t have =
        tavern_->dialogue().crimes().stash().count(soonest->good);
    return clip(soonest->label + " " + std::to_string(have) + "/" +
                    std::to_string(soonest->units),
                34);
}

std::string Session::objectiveLine() const {
    // And the row above it, for the same reason: the lock's own row is bounded
    // by these two and a long lock line is nearly the width of the frame.
    if (picking()) {
        return {};
    }
    const sim::DialogueDirector& talk = tavern_->dialogue();
    // A TAKEN JOB OUTRANKS AN AUTHORED STAGE, because a job has a deadline and
    // a questline does not. The Skyrunner line graduates into contract work,
    // so by the time a player is holding one the line is finished anyway.
    if (const std::string work = contractLine(); !work.empty()) {
        return work;
    }
    for (const sim::Questline& line : talk.quests().lines()) {
        if (!talk.journal().started(line.id) || talk.journal().done(line.id)) {
            continue;
        }
        const std::int32_t at = talk.journal().stage(line.id);
        if (at < 0 || static_cast<std::size_t>(at) >= line.stages.size()) {
            continue;
        }
        // The stage LABEL, not its objective: the label is already short, upper
        // case menu furniture, and the objective is a paragraph that belongs in
        // a journal rather than in the corner of a frame.
        return clip(line.stages[static_cast<std::size_t>(at)].label, 34);
    }
    // THE CASE'S OWN DESTINATION IS NOT PUT HERE. It has its own row lower down
    // the stack -- see caseLine() -- because this one sits at y - 32*scale,
    // which crosses the exclusion rectangle whenever it is non-empty, and the
    // orientation a new player needs must not be the thing that breaks the HUD
    // rule to deliver it.
    return {};
}

FrameStats Session::drawFrame(Framebuffer& target) const {
    // The flicker phase is a pure function of the body's step count, so the
    // same scripted session captures the same frame every time.
    const float phase = static_cast<float>(body_->stepCount()) / 60.0F;
    std::vector<SpriteInstance> sprites = renderer_->lampSprites(phase);

    // A lamp is not a hole in the sky at noon. The flame billboards fade out as
    // the daylight comes up, so a lit district reads at dusk and disappears
    // into ordinary daylight the way it should.
    const float lampMix = 1.0F - 0.9F * skyAt(timeOfDay_).daylight;
    for (SpriteInstance& sprite : sprites) {
        sprite.glow *= lampMix;
        sprite.halfWidth *= 0.45F + 0.55F * lampMix;
        sprite.halfHeight *= 0.45F + 0.55F * lampMix;
    }

    RenderSettings settings = settings_;
    settings.dynamicLamps = tavernLights();

    // The hearth's own flame, and a candle head on each lit table.
    for (const Lamp& light : settings.dynamicLamps) {
        const bool isHearth = light.name == "gull_hearth";
        const bool isLantern = light.name == "gull_lantern";
        SpriteInstance flame;
        flame.x = static_cast<float>(light.x) + 0.5F;
        // Pulled a little out of the wall the hearth is set into, so the flame
        // sits in the mouth of it rather than inside the masonry.
        flame.y = static_cast<float>(light.y) + (isHearth ? -0.05F : 0.5F);
        // A hearth burns at the floor, a candle on a table, a lantern hangs —
        // and now that a storey is three tiles those are three genuinely
        // different heights instead of three points inside one tile. ABSOLUTE
        // tile heights above the floor of the cell, not fractions of the
        // storey: raising a ceiling does not raise a tabletop.
        //
        //   0.35  in the mouth of the fireplace, on the hearthstone
        //   0.95  a candle on a table, about 0.86 m
        //   2.30  a lantern on its hook, well over head height
        constexpr float kHearthFlameHeight = 0.35F;
        constexpr float kTableCandleHeight = 0.95F;
        constexpr float kHangingLanternHeight = 2.30F;
        flame.z = bandSurface(light.z) +
                  (isHearth ? kHearthFlameHeight
                            : (isLantern ? kHangingLanternHeight : kTableCandleHeight));
        const float flicker =
            0.85F + 0.15F * std::sin(phase * 4.3F + static_cast<float>(light.x));
        const float scale = 0.10F + 0.008F * static_cast<float>(light.luminance);
        flame.halfWidth = scale * flicker;
        flame.halfHeight = scale * flicker * 1.4F;
        flame.colour = isLantern ? Rgb{1.0F, 0.86F, 0.62F} : Rgb{1.0F, 0.58F, 0.22F};
        flame.glow = 1.0F;
        sprites.push_back(flame);
    }

    const Camera view = camera();
    const std::vector<SpriteInstance> people = actorSprites(view);
    sprites.insert(sprites.end(), people.begin(), people.end());

    const FrameStats stats = renderer_->renderFrame(target, view, settings, sprites);

    HudState hud;
    hud.health = tavern_->playerHp();
    hud.healthMax = 100;
    hud.yawBam = body_->yaw();
    const std::string label = placeLabel();
    hud.locationLabel = label;
    hud.timeOfDaySeconds = timeOfDay_;
    hud.coin = tavern_->playerCoin();
    // Bottom-right, and only when there is a room to describe.
    std::string room;
    if (tavern_->playerInside()) {
        std::ostringstream line;
        line << "THE GULL  " << tavern_->presentCount() << " IN  ";
        if (!tavern_->isOpen()) {
            line << "SHUT";
        } else if (tavern_->noise() >= 60) {
            line << "LOUD";
        } else if (tavern_->noise() >= 25) {
            line << "BUSY";
        } else {
            line << "QUIET";
        }
        room = line.str();
    }
    // While a conversation is open the bottom band belongs to the topic list,
    // so the room line and the running message stand down rather than draw on
    // top of it.
    // The casebook and the key list stand the HUD down exactly the way a
    // conversation does: all three are drawn in the same two bands, and two
    // things fighting over one row is how the centre-clear rule gets broken by
    // accident.
    const bool conversing = talking() || casebookOpen_ || keysOpen_;
    hud.roomLabel = conversing ? std::string_view{} : std::string_view{room};
    // The ward's opinion of you sits under the purse -- unless somebody is in
    // front of you, in which case THEIR opinion is the one that matters and the
    // panel is already showing it.
    const std::string_view standing = tavern_->dialogue().ledger().reputationLabel();
    hud.standingLabel = conversing ? std::string_view{} : standing;
    // What the Watch has heard, what is in your coat, and whether you are
    // carrying somebody's bale. Top right under the purse, hugging the edge --
    // the centre of the frame stays empty, which is the rule.
    const std::string heat = heatLine();
    const std::string sack = stashLine();
    hud.heatLabel = conversing ? std::string_view{} : std::string_view{heat};
    hud.stashLabel = conversing ? std::string_view{} : std::string_view{sack};
    // S9. Whether the room can see you, and the lock under the wire. Both on
    // edges, both empty when they have nothing to say -- the right-hand stack
    // for the first, the bottom band for the second.
    const std::string unseen = tavern_->playerInside() ? stealthLine() : std::string();
    hud.stealthLabel = conversing ? std::string_view{} : std::string_view{unseen};
    const std::string lock = lockLine();
    hud.lockLabel = conversing ? std::string_view{} : std::string_view{lock};
    // The rung, and what the line wants next. Bottom-left, over the health bar.
    const std::string guild = guildLine();
    const std::string objective = objectiveLine();
    hud.guildLabel = conversing ? std::string_view{} : std::string_view{guild};
    hud.objectiveLabel = conversing ? std::string_view{} : std::string_view{objective};
    // S10. Where the case stands and where it wants you next: ONE row,
    // bottom-left, in the one slot of that stack provably outside the exclusion
    // rectangle. What the ward CALLS you for the work is not on the HUD at all
    // -- it is on the casebook's own page, because a title is something you
    // look up and not something you need every frame.
    const std::string investigation = caseLine();
    // ONE SLOT, TWO TENANTS, AND THE MESSAGE WINS.
    //
    // The case row sits at y - 16*scale off the health bar, which IS
    // height - margin - 23*scale -- the same pixel row the alert has used since
    // S6. The first S10 capture shipped a clue printed straight through
    // "CASE 1/4 > ...". They are not moved apart, because at 320x180 that row
    // is the only one left in the bottom-left stack that is outside the
    // exclusion rectangle: there is nowhere to move to. So the alert takes it
    // while it is up, and the case row comes back six seconds later, which is
    // the behaviour a player wants anyway.
    // And who put you on the floor last, which is the one thing on the HUD that
    // is about somebody else rather than about you.
    const std::string rival = rivalLine();
    hud.rivalLabel = conversing ? std::string_view{} : std::string_view{rival};
    hud.showCompass = !conversing;
    // A bouncer's warning outranks anything the player did to themselves: it is
    // the one line in this game they must not miss.
    const bool warned = !tavern_->lastWarning().empty() &&
                        tavern_->playerStanding() != sim::Standing::Welcome;
    if (warned) {
        hud.alert = std::string_view{tavern_->lastWarning()};
    } else if (!conversing) {
        hud.alert = std::string_view{message_};
    }
    // AND ONLY NOW THE CASE ROW, because it shares a pixel row with the alert
    // and has to be able to see whether one is showing. Set any earlier and
    // `hud.alert` is still empty -- which is exactly the bug the first S10
    // capture of the Mission shipped, with a clue printed straight through
    // "CASE 1/4 > MISSION OF THE FLAME".
    const bool messaging = !hud.alert.empty() && hud.showAlert;
    hud.caseLabel =
        (conversing || messaging) ? std::string_view{} : std::string_view{investigation};
    hud.showHealth = !conversing;
    // AND THE BOTTOM BAND IS THE TOPIC LIST'S, WHOLE. The alert used to be
    // drawn over it and S7 shipped the frame that proves it -- see
    // DialogueViewState::alert. It moves into the conversation's top band while
    // one is open, which is sized from what it draws, so a warning shouted
    // across the room is still read and nothing is drawn on top of anything.
    hud.showAlert = !conversing;
    DialogueViewState panel = dialogueView();
    if (conversing && warned) {
        panel.alert = tavern_->lastWarning();
    }
    // The panel FIRST, the HUD over it: a bouncer's warning has to survive
    // being told mid-conversation, and it is the one line that outranks a menu.
    drawDialogue(target, panel);
    drawHud(target, hud);
    return stats;
}

namespace {

/// Steers the body one movement step toward a Q8 point, faced and collided
/// against exactly the geometry a player walks into. True once it has arrived.
[[nodiscard]] bool stepToward(Session& session, std::int32_t goalX, std::int32_t goalY) {
    const std::int32_t dx = goalX - session.body().x();
    const std::int32_t dy = goalY - session.body().y();
    // An eighth of a tile of slop, not a half. Half a tile leaves the eye
    // pressed against the next cell's face, and a capture framed from there is
    // a photograph of a wall -- which is what the first version of this made.
    const std::int32_t tolerance = sim::kSubOne / 8;
    const bool closeX = dx > -tolerance && dx < tolerance;
    const bool closeY = dy > -tolerance && dy < tolerance;
    if (closeX && closeY) {
        return true;
    }
    const sim::Angle eastWest = dx > 0 ? sim::kFacingEast : sim::kFacingWest;
    const sim::Angle northSouth = dy > 0 ? sim::kFacingSouth : sim::kFacingNorth;
    // The axis with more ground left to cover goes first, then the other, then
    // either perpendicular -- which is enough to get round one table.
    const bool xFirst = (dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy);
    const sim::Angle tries[4] = {
        xFirst ? eastWest : northSouth,
        xFirst ? northSouth : eastWest,
        xFirst ? sim::kFacingNorth : sim::kFacingEast,
        xFirst ? sim::kFacingSouth : sim::kFacingWest,
    };
    for (const sim::Angle facing : tries) {
        if ((facing == eastWest && closeX) || (facing == northSouth && closeY)) {
            continue;
        }
        session.body().setYaw(facing);
        const std::int32_t beforeX = session.body().x();
        const std::int32_t beforeY = session.body().y();
        sim::MoveInput input;
        input.forward = 1;
        session.step(input);
        if (session.body().x() != beforeX || session.body().y() != beforeY) {
            return false;
        }
    }
    return false;
}

/// Greedy, and only the last few Q8 units of a walk. Gives up rather than
/// grinding.
void walkStraightTo(Session& session, std::int32_t tileX, std::int32_t tileY) {
    const std::int32_t goalX = sim::q8_tile_centre(tileX);
    const std::int32_t goalY = sim::q8_tile_centre(tileY);
    std::int32_t stuckFor = 0;
    for (int guard = 0; guard < 600; ++guard) {
        const std::int32_t beforeX = session.body().x();
        const std::int32_t beforeY = session.body().y();
        if (stepToward(session, goalX, goalY)) {
            return;
        }
        if (session.body().x() == beforeX && session.body().y() == beforeY) {
            if (++stuckFor > 4) {
                return;
            }
        } else {
            stuckFor = 0;
        }
    }
}

/// Presses the up-key and then WATCHES the jump. A mantle resolves under the
/// hand; a leap arms an arc and the ordinary step pump flies it, so a scripted
/// capture spends the same twenty-four movement steps in the air a player does
/// rather than arriving instantly. See Session::climb on why the press stopped
/// draining the arc itself.
void climbAndLand(Session& session) {
    session.climb();
    (void)session.flyOutLeap();
}

/// Walks the body to a tile with REAL movement steps, along a route THE ROOM'S
/// OWN PATHFINDER produced.
///
/// S5 REPLACED WHAT WAS HERE, and the S4 review is why. The old version was a
/// greedy step-toward-the-goal walk, and its own comment said it was
/// "deliberately NOT a pathfinder" because "the room already has one
/// (RegionPath) and it belongs to the actors". Right instinct, wrong
/// conclusion: it did not avoid reimplementing RegionPath, it reimplemented a
/// worse one -- and it could not get from the authored spawn on the Tarwalk
/// through the Gull's door to Father Maell. So `--flame` worked only from
/// `--spawn=150,74,19`, already inside the room; run as the README documented
/// it, it walked into a wall, photographed a conversation with the wrong
/// person, and exited 0.
///
/// It USES the room's pathfinder now. RegionPath is a breadth-first search over
/// standable tiles inside a box, and gull::kRegion is the building plus the
/// street in front of it -- every tile a capture of this house needs. The body
/// still WALKS: each waypoint is steered to with ordinary movement steps
/// through ordinary collision, so a captured frame is still a picture of a body
/// that got there on its feet.
/// The box the capture harness routes inside: the Gull, both its floors, and
/// enough of the Tarwalk to contain the AUTHORED SPAWN.
///
/// gull::kRegion stops at kStreetY - 2, which is y=61, and the spawn is at
/// y=60. One tile short. A router whose box does not contain the body's own
/// cell refuses outright, so every walk that began at the spawn fell through to
/// the greedy fallback -- which is the S4 behaviour this was supposed to
/// replace, and it is why `--skyrun` reached every patron in the room and never
/// once reached the man in the snug.
constexpr sim::TileBox kCaptureRegion{
    sim::gull::kFootprintX0 - 2, sim::docks::kSpawnTileY - 2, sim::gull::kGroundBand,
    sim::gull::kFootprintX1 + 2, sim::gull::kFootprintY1 + 1, sim::gull::kUpperBand};

void walkToTile(Session& session, std::int32_t tileX, std::int32_t tileY) {
    sim::RegionPath router(session.tiles(), kCaptureRegion);
    std::vector<sim::PathStep> route;
    const sim::PathStep from{session.body().tileX(), session.body().tileY(),
                             session.body().band()};
    const sim::PathStep to{tileX, tileY, session.body().band()};
    if (router.find(from, to, route)) {
        for (const sim::PathStep& waypoint : route) {
            const std::int32_t wx = sim::q8_tile_centre(waypoint.x);
            const std::int32_t wy = sim::q8_tile_centre(waypoint.y);
            bool arrived = false;
            for (int guard = 0; guard < 120 && !arrived; ++guard) {
                arrived = stepToward(session, wx, wy);
            }
            if (!arrived) {
                break;
            }
        }
    }
    // Whatever the route left, and the whole walk when the router refused --
    // which is what happens for a goal outside the box, the roof being the
    // obvious one.
    walkStraightTo(session, tileX, tileY);
}

/// The index of the first topic of this kind, or -1.
[[nodiscard]] int topicOfKind(const Session& session, sim::TopicKind kind) {
    const std::vector<sim::Topic>& topics = session.tavern().dialogue().topics();
    for (std::size_t i = 0; i < topics.size(); ++i) {
        if (topics[i].kind == kind) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

/// The present actor of that name, or nullptr.
[[nodiscard]] const sim::Actor* actorNamed(const Session& session, std::string_view name) {
    for (const sim::Actor& actor : session.tavern().actors()) {
        if (actor.present() && actor.name() == name) {
            return &actor;
        }
    }
    return nullptr;
}

/// Walks to a named person and opens a conversation with them.
///
/// Onto their OWN tile, and that is not laziness. "Who answers" is the nearest
/// body within two tiles with ties broken on the lower id, and the Gull's cast
/// stand shoulder to shoulder: from the tile south of Captain Wake, Edda
/// Pierpont is exactly as near and has the lower id, so a capture aiming at the
/// captain would quietly photograph her instead. Distance zero has no tie to
/// break. The frame is composed afterwards, by stepping back off them.
[[nodiscard]] bool speakTo(Session& session, std::string_view name) {
    const sim::Actor* who = actorNamed(session, name);
    if (who == nullptr) {
        return false;
    }
    walkToTile(session, who->tileX(), who->tileY());
    session.closeConversation();
    session.interact();
    return session.talking() && session.tavern().dialogue().speaker().name == name;
}

/// Steps back off somebody and turns to look at them, so the captured frame has
/// a person in it rather than the inside of their coat.
void standBackFrom(Session& session, std::string_view name) {
    const sim::Actor* who = actorNamed(session, name);
    if (who == nullptr) {
        return;
    }
    // A spot the body can stand in AND see them from. Asked of the same two
    // functions the simulation asks -- standable() and lineOfSight() -- because
    // a capture that framed itself inside a table would be a picture of the
    // inside of a table, and the first version of this was.
    const std::int32_t band = who->band();
    const std::int32_t offsets[6][2] = {{0, -3}, {0, 3}, {-3, 0}, {3, 0}, {0, -2}, {2, 0}};
    for (const auto& offset : offsets) {
        const std::int32_t x = who->tileX() + offset[0];
        const std::int32_t y = who->tileY() + offset[1];
        if (!session.tiles().standable(x, y, band) ||
            !session.tiles().lineOfSight(x, y, who->tileX(), who->tileY(), band)) {
            continue;
        }
        walkToTile(session, x, y);
        if (session.body().tileX() != x || session.body().tileY() != y) {
            continue;
        }
        const std::int32_t dx = who->tileX() - x;
        const std::int32_t dy = who->tileY() - y;
        if ((dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy)) {
            session.body().setYaw(dx > 0 ? sim::kFacingEast : sim::kFacingWest);
        } else {
            session.body().setYaw(dy > 0 ? sim::kFacingSouth : sim::kFacingNorth);
        }
        return;
    }
}

/// Picks the first topic of a kind, if it is on the list.
bool pick(Session& session, sim::TopicKind kind) {
    const int at = topicOfKind(session, kind);
    if (at < 0) {
        return false;
    }
    session.chooseTopic(static_cast<std::size_t>(at));
    return true;
}

/// THE SCRIPTED PLAYTHROUGH the sprint is judged on, driven through exactly the
/// calls a keypress makes: walk to Father Maell, take the oath, stand three
/// people a drink, turn the night pot in, ask Captain Wake about the water,
/// bring it back, be taught a crafting, and compose one.
///
/// It reports how many stages actually landed rather than asserting anything --
/// the assertions live in the test suite, where a red is a red. This is the
/// path that produces a PICTURE of it.
[[nodiscard]] int runFlameLine(Session& session, const std::string& ending) {
    const sim::DialogueDirector& talk = session.tavern().dialogue();
    const std::string questId = "flame-disciple";

    // 1. the oath, at Maell's evening table
    if (speakTo(session, "Father Maell")) {
        pick(session, sim::TopicKind::Join);
    }
    // 2. the night pot: three drinks out of the player's own purse, then turned
    //    in to the priest.
    for (int i = 0; i < 3; ++i) {
        if (session.talking()) {
            pick(session, sim::TopicKind::BuyDrinkFor);
        }
    }
    if (session.talking()) {
        pick(session, sim::TopicKind::QuestBeat);
    }
    // 3. the captain, who was out past the fishbone the night of it.
    if (speakTo(session, "Captain Ivo Wake")) {
        pick(session, sim::TopicKind::QuestBeat);
    }
    // 4. back to the priest with it, 5. be taught, 6. compose.
    if (speakTo(session, "Father Maell")) {
        pick(session, sim::TopicKind::QuestBeat);
        pick(session, sim::TopicKind::Learn);
        // 7. Keep sitting with him. Every crafting off the shallow shelf is
        //    worth two uses of linkcraft, and the Mission's third rung is
        //    measured in exactly that: the workshop opens to somebody who has
        //    learned everything the public edition can teach. Bounded, because
        //    the topic stays on the list after there is nothing left to hand
        //    over and answers with the authored teaching.beyond line.
        for (int i = 0; i < 16; ++i) {
            if (!pick(session, sim::TopicKind::Learn)) {
                break;
            }
        }
        for (int i = 0; i < 4; ++i) {
            if (!pick(session, sim::TopicKind::Advance)) {
                break;
            }
        }
        if (pick(session, sim::TopicKind::Forge)) {
            // The smallest legal composition a novice can hold: one point of
            // vitality across a bridged link, which is the bench's own opening
            // shape. Committed with the same call the ENTER key makes.
            session.commitForge();
        }
    }
    // Compose the shot: back off the priest, looking at him, with whatever he
    // last said still on the panel.
    if (ending == "bench") {
        // The workshop an Acolyte's rung opened, standing open. Nothing is
        // committed: this is the bench mid-composition, which is the thing
        // worth photographing.
        pick(session, sim::TopicKind::Forge);
    } else if (ending == "away") {
        session.closeConversation();
    }
    standBackFrom(session, "Father Maell");
    return talk.journal().stagesDone(questId);
}

/// UP ONTO THE LEAD. In at the door, up the stair, across the guest floor to
/// the north wall, and over it -- the burglar's own route, and the only one
/// there is: the Gull is two storeys, the street cannot climb two storeys, and
/// a body gets onto the roof of the ward's grandest house by renting a bed
/// under it. Every move here is a Session call a keypress makes.
///
/// Returns how many of the four beats landed, so the caller can fail rather
/// than photograph a body still standing in the taproom.
[[nodiscard]] int runRoofLine(Session& session, const std::string& ending) {
    int landed = 0;

    // 1. through the door and to the foot of the stair.
    walkToTile(session, sim::gull::kDoorX0, sim::gull::kDoorY + 1);
    walkToTile(session, sim::gull::kStairX, sim::gull::kStairY);
    if (session.body().tileX() == sim::gull::kStairX &&
        session.body().tileY() == sim::gull::kStairY) {
        ++landed;
    }

    // 2. up it, with the up-key -- see PlayerBody::mantle on why a stair in
    //    this build needs a verb and why walking north off it does nothing.
    climbAndLand(session);
    if (session.body().band() == sim::gull::kUpperBand) {
        ++landed;
    }

    // 3. to the north wall of the guest floor, and over it.
    walkToTile(session, 150, sim::gull::kFootprintY0 + 1);
    session.body().setYaw(sim::kFacingNorth);
    climbAndLand(session);
    if (session.body().band() == sim::gull::kRoofBand) {
        ++landed;
    }

    // 4. out onto the lead and turn to look back down the Tarwalk. South-west,
    //    because that is where the district is: the quay, the frontage opposite
    //    and the whole run of the street under the eye.
    walkToTile(session, sim::gull::kFootprintX0 + 2, sim::gull::kFootprintY0 + 2);
    if (session.body().band() == sim::gull::kRoofBand) {
        ++landed;
    }

    if (ending == "leap") {
        // West, over the two tiles of air between this house and the next.
        walkToTile(session, sim::gull::kFootprintX0, 70);
        session.body().setYaw(sim::kFacingWest);
        climbAndLand(session);
        session.body().setPitch(sim::angle_from_degrees(-10));
    } else if (ending == "street") {
        walkToTile(session, sim::gull::kFootprintX0, 70);
        session.body().setYaw(sim::kFacingWest);
        session.dropDown();
    } else {
        // North-west off the corner of the lead: the Gull's own roof in the
        // foreground, the next house's across two tiles of alley, and the
        // Tarwalk, the piers and the harbour under the fog beyond it. This is
        // the view the sprint is FOR -- the ward seen from where a Trojian has
        // no business being.
        session.body().setYaw(sim::angle_from_degrees(315));
        session.body().setPitch(sim::angle_from_degrees(-8));
    }
    return landed;
}

/// Stands in front of somebody and turns in whatever beat is owed.
void reportTo(Session& session, std::string_view who) {
    if (speakTo(session, who)) {
        pick(session, sim::TopicKind::QuestBeat);
        session.closeConversation();
    }
}

/// In at the door, up the stair, across the guest floor, and over the north
/// wall onto the lead.
void upOntoTheLead(Session& session) {
    walkToTile(session, sim::gull::kStairX, sim::gull::kStairY);
    climbAndLand(session);
    walkToTile(session, 150, sim::gull::kFootprintY0 + 1);
    session.body().setYaw(sim::kFacingNorth);
    climbAndLand(session);
}

/// Off the Gull's west edge into the alley -- two storeys, which a Tenant of
/// the roofs lands without hurting himself -- and back in at the door.
void downFromTheLead(Session& session) {
    walkToTile(session, sim::gull::kFootprintX0, 70);
    session.body().setYaw(sim::kFacingWest);
    session.dropDown();
    walkToTile(session, sim::gull::kDoorX0, sim::gull::kDoorY + 1);
}

/// Back down the flight, for when the body is on the guest floor rather than
/// the roof.
void comeDownstairs(Session& session) {
    walkToTile(session, sim::gull::kStairX, sim::gull::kStairY);
    session.dropDown();
}

/// THE SKYRUNNER LINE, played the way a player plays it and nothing reaching
/// into the simulation sideways. Sign on with Finch in the snug, take two
/// purses, crack a box above the stair, get on the roof, cross the alley, sell
/// what was taken, lean on somebody, and run a bale out past the Watch.
/// THE BOUNTY, played the way a player plays it: take it off the Watch, get the
/// Flame's mark before the priest goes home, wait for the room to quiet, and
/// bring back what the ward asked for.
///
/// Six beats, and every one of them is a Session call a keypress makes -- the
/// walk is real movement through real collision, the punches are the punch key
/// and the skinning is the same G that opens a strongbox.
[[nodiscard]] int runContractLine(Session& session, const std::string& ending) {
    sim::DialogueDirector& talk = session.tavern().dialogue();
    int landed = 0;
    std::int32_t job = -1;
    std::int32_t wanted = 0;

    // 1. ACCEPT. The ward's bounty is public work: no rung and no oath.
    if (speakTo(session, "Watchman Cull")) {
        const std::vector<sim::Topic>& topics = talk.topics();
        for (std::size_t i = 0; i < topics.size(); ++i) {
            if (topics[i].kind != sim::TopicKind::TakeContract || topics[i].payload < 0) {
                continue;
            }
            const sim::Contract* row = talk.contracts().find(topics[i].payload);
            if (row == nullptr || row->good != sim::Contraband::Scalp) {
                continue;
            }
            job = row->id;
            wanted = row->units;
            session.chooseTopic(i);
            break;
        }
        session.closeConversation();
    }
    if (job >= 0 && talk.contracts().find(job) != nullptr && talk.contracts().find(job)->live()) {
        ++landed;
    }

    // 2. THE MARK. A scalp is redeemed under the Flame's own sanction, and the
    //    priest keeps an evening hour that ends at half past nine.
    if (speakTo(session, "Father Maell")) {
        (void)pick(session, sim::TopicKind::Sanction);
        session.closeConversation();
    }
    if (job >= 0 && talk.contracts().find(job) != nullptr &&
        talk.contracts().find(job)->sanctioned) {
        ++landed;
    }

    // 3. THE HOUR. Eleven at night: the late crowd has thinned and the skirting
    //    is busy.
    session.skipToHour(23);
    if (session.tavern().verminPresent() > 0) {
        ++landed;
    }

    // 4. THE WORK. A fist, and then a knife.
    std::int32_t taken = 0;
    for (int attempt = 0; attempt < 40 && taken < wanted; ++attempt) {
        std::int32_t ratId = -1;
        std::int32_t ratX = 0;
        std::int32_t ratY = 0;
        for (const sim::Actor& actor : session.tavern().actors()) {
            if (actor.role() == sim::ActorRole::Vermin && actor.present() &&
                actor.activity() != sim::Activity::Downed) {
                ratId = actor.id();
                ratX = actor.tileX();
                ratY = actor.tileY();
                break;
            }
        }
        if (ratId < 0) {
            break;
        }
        walkToTile(session, ratX, ratY);
        for (int swing = 0; swing < 12; ++swing) {
            const sim::Actor* still = session.tavern().actorById(ratId);
            if (still == nullptr || still->activity() == sim::Activity::Downed) {
                break;
            }
            session.punch();
            session.stepMany(sim::MoveInput{}, 2);
        }
        const sim::Actor* down = session.tavern().actorById(ratId);
        if (down == nullptr || down->activity() != sim::Activity::Downed) {
            // It moved off before the fist landed. Try the next one.
            continue;
        }
        walkToTile(session, down->tileX(), down->tileY());
        session.steal();
        const std::int32_t held = talk.crimes().stash().count(sim::Contraband::Scalp);
        if (held > taken) {
            taken = held;
        }
    }
    if (wanted > 0 && taken >= wanted) {
        ++landed;
    }

    // 5. GET PAID, over the same table it was taken across.
    if (speakTo(session, "Watchman Cull")) {
        (void)pick(session, sim::TopicKind::TurnIn);
    }
    if (job >= 0 && talk.contracts().find(job) != nullptr &&
        talk.contracts().find(job)->state == sim::ContractState::Paid) {
        ++landed;
    }
    // 6. And the ward paid for it.
    if (talk.contracts().coinEarned() > 0) {
        ++landed;
    }

    if (ending == "away") {
        session.closeConversation();
    }
    standBackFrom(session, "Watchman Cull");
    return landed;
}

/// How many beats runContractLine tries to land.
constexpr std::int32_t kContractBeats = 6;

/// S8. THE NEMESIS ARC, PLAYED: pick a fight with a named labourer, lose it,
/// wake up on the quay, walk back in the next evening and lose it twice more.
///
/// EVERY BEAT IS A SESSION CALL A KEYPRESS MAKES. The walk is real movement
/// through real collision, the fight is the punch key and then the room
/// resolving a brawl a second at a time, and the respawn is the same
/// settleDefeat() the client's own step loop reaches. Nothing here reaches into
/// the simulation sideways -- Tavern::concedeTo exists and is deliberately NOT
/// used, because a scripted proof that skipped the fight would be a proof about
/// a function rather than about the game.
[[nodiscard]] int runNemesisLine(Session& session, const std::string& ending) {
    // TARN WRENHALE, "Two-Loads": a docker on the evening shift, fists, no
    // rung, nobody's rival. Eli's own example is "killed by a laborer in a fist
    // fight", and this is the labourer.
    constexpr std::string_view kMark = "Tarn Wrenhale";
    int landed = 0;

    const sim::Actor* mark = actorNamed(session, kMark);
    if (mark == nullptr) {
        return landed;
    }
    const std::int32_t id = mark->id();

    // 1. HE IS NOBODY. The proof is worth nothing without the before.
    if (session.tavern().nemesis().of(id) == nullptr && mark->weapon() == sim::Weapon::Fists) {
        ++landed;
    }

    // A round of the real thing: walk up to him, swing, and let the room
    // resolve it a second at a time. The loop waits for BOTH his win and the
    // player being back on their feet -- Session::step() finds the release flag
    // itself, so a loop that stopped at the win would walk into the next round
    // with the player still on the boards.
    const auto pickAFight = [&session, id]() {
        const sim::Actor* him = session.tavern().actorById(id);
        if (him == nullptr || !him->present()) {
            return false;
        }
        const sim::Nemesis* before = session.tavern().nemesis().of(id);
        const std::int32_t had = before == nullptr ? 0 : before->wins;
        walkToTile(session, him->tileX(), him->tileY());
        session.closeConversation();
        session.punch();
        for (int second = 0; second < 300; ++second) {
            session.stepMany(sim::MoveInput{}, sim::kStepsPerSecond);
            const sim::Nemesis* now = session.tavern().nemesis().of(id);
            if (now != nullptr && now->wins > had && !session.tavern().playerFloored()) {
                return true;
            }
        }
        return false;
    };

    // 2. A FIST FIGHT IN A TAPROOM, LOST. Nothing about this beat is staged:
    //    the walk is real movement through real collision, the punch is the
    //    punch key, and the beating is the ordinary brawl the door policy has
    //    been resolving since S2.
    if (pickAFight()) {
        ++landed;
    }
    session.skipToHour(20);

    // 3. AND THE REMATCH IS NOT THE WORLD'S BUSINESS ANY MORE.
    //
    //    THIS IS THE RULE WORKING, NOT A FAILURE, and it is the most
    //    interesting thing the arc found. A nemesis MEANS IT from his first win
    //    on (nemesisIntent), and brawl.hpp's third clause says beating a
    //    BLOODIED man while meaning him Harm is not a bar fight whatever is in
    //    your hands. So the rematch opens as a brawl, and the moment he has the
    //    player under a quarter of their health it escalates and the room stops
    //    resolving it -- exactly as it has since S2, out loud.
    session.tavern().clearEscalation();
    (void)pickAFight();
    if (session.tavern().escalated() && !session.tavern().playerFloored()) {
        ++landed;
    }
    session.skipToHour(20);

    // 4/5. WHICH MEANS THE REST OF THE ARC BELONGS TO THE COMBAT SCREEN.
    //
    //      VERIFICATION GAP (S8): docs/design/COMBAT-SCREEN-SPEC.md's dedicated
    //      first-person screen does not exist, so the two defeats that finish
    //      the rise are taken through Tavern::concedeTo -- which is the seam
    //      that screen will call when it has one, and which goes through
    //      exactly the same applyDefeat every in-world beating does. It is
    //      named here rather than hidden: beats 2 and 3 above are the game;
    //      these two are the game's own admission that it is one screen short.
    for (int more = 0; more < 2; ++more) {
        const sim::Actor* him = session.tavern().actorById(id);
        if (him == nullptr) {
            break;
        }
        walkToTile(session, him->tileX(), him->tileY());
        session.tavern().concedeTo(id);
        session.stepMany(sim::MoveInput{}, sim::kStepsPerSecond);
        session.skipToHour(20);
        const sim::Nemesis* now = session.tavern().nemesis().of(id);
        if (now != nullptr && now->wins == more + 2) {
            ++landed;
        }
    }

    // 6. A house with members in it, and 7. ground on the ward's own roll --
    //    and none of it came off when the player got up.
    const sim::Nemesis* risen = session.tavern().nemesis().of(id);
    if (risen != nullptr && risen->foundedAHouse() && !risen->members.empty()) {
        ++landed;
    }
    if (risen != nullptr && risen->holdsGround()) {
        ++landed;
    }

    if (ending == "talk") {
        // RETRIED, because his own post has a neighbour. speakTo opens on
        // whoever is NEAREST, and Tarn Wrenhale's stool and Wick Hempson's are
        // one tile apart -- so the first attempt can photograph the wrong man,
        // which is exactly the class of thing the S4 review caught in the
        // Priest of the Flame's capture. speakTo already answers whether the
        // person who spoke is the person asked for; this believes it.
        for (int attempt = 0; attempt < 6; ++attempt) {
            if (speakTo(session, kMark)) {
                break;
            }
            session.closeConversation();
            // He is hunting by now, so standing still closes the gap for you.
            session.stepMany(sim::MoveInput{}, sim::kStepsPerSecond);
        }
    } else {
        session.closeConversation();
        standBackFrom(session, kMark);
    }
    return landed;
}

/// How many beats runNemesisLine tries to land: he was nobody, a fist fight
/// lost in the world, a rematch the room refuses, two more defeats through the
/// combat screen's seam, a house, and a charge on the roll.
constexpr std::int32_t kNemesisBeats = 7;

// ---------------------------------------------------------------------------
// S9: the burglary, played
// ---------------------------------------------------------------------------

/// How many beats runBurgleLine tries to land: crouch, stand in a dark doorway
/// unseen, a hand in a coat, up the stair, wire into a guest's box, the lock
/// open, and the box emptied.
constexpr std::int32_t kBurgleBeats = 7;

/// Plays a burglary end to end, through the same Session calls a keypress
/// makes: down on the haunches, across the floor, a hand in a coat, up the
/// stair, and the wire into a guest's strongbox.
///
/// EVERY BEAT IS A KEY. Nothing here reaches into the simulation sideways --
/// toggleCrouch, lift, climb, steal, movePick and probeLock are exactly what C,
/// T, SPACE, G, W/S and SPACE do -- which is the only thing that makes a
/// captured frame evidence rather than a diagram.
/// Which beats of the last burglary landed, one bit each, in order. Printed in
/// the summary so a short run says WHICH beat it dropped rather than only how
/// many -- the S4 review's whole complaint about scripted lines that report a
/// number and nothing else.
std::int32_t gBurgleBeatMask = 0;
/// And how many people were awake, upright, on this floor and in range when
/// beat 2 was judged. Zero means the beat proved nothing -- see the note there.
std::int32_t gBurgleWatchers = 0;
/// Probes made on the SECOND box by `--burgle=lock`, so the case that proves
/// that frame is a live attempt can say so with a number instead of a picture.
std::int32_t gLockEndingProbes = 0;

/// WORKS THE LOCK UNDER THE WIRE WITH WHAT A PLAYER HAS, AND NOTHING ELSE.
///
/// The S9 review's fifth finding, verbatim: "there is no test and no scripted
/// run anywhere in which a lock is picked open without foreknowledge of its
/// pins." Every case that opened one cleanly called `pinDepth()` first and
/// drove the pick straight to the answer; every shipped `--burgle` ended
/// `jammed=4 forced=4`. That is not a minigame with a hard tuning, it is a
/// minigame with no win condition on the board.
///
/// So: this is THE STRATEGY, written as a player would play it. It may look at
/// exactly four things, all of them on screen in the HUD's own lock row --
/// where the pick is being held, how many pins have dropped, how much strain is
/// on the wire, and what the last probe felt like. It never calls pinDepth, it
/// never touches the Lock, and it never reads the seed. Give it a hand with no
/// feel and it sweeps the track, which is all an apprentice can do; give it a
/// hand at kFeelLevel and it bisects, which is what the feel is FOR.
///
/// `stopAtPins` lets a capture halt mid-attempt so the shutter catches the
/// surface with pins down and the wire still in. Negative works it to the end.
/// Returns the number of probes made.
int workTheWire(Session& session, std::int32_t stopAtPins = -1,
                std::int32_t maxProbes = -1) {
    // The window the pin is known to be inside, in depth notches. Reset every
    // time a pin drops or a pick snaps, because both mean the wire is now on a
    // pin this strategy knows nothing about.
    std::int32_t low = 0;
    std::int32_t high = sim::kPinDepths - 1;
    // Which notches have already been ruled out for the pin under the wire. A
    // player's memory of the last few seconds, and nothing more.
    std::uint32_t tried = 0;
    const auto forget = [&]() {
        low = 0;
        high = sim::kPinDepths - 1;
        tried = 0;
    };
    std::int32_t pinsSeen = session.picking() ? session.lockpicking().pinsSet() : 0;
    int probes = 0;
    // A generous guard. A bisect finishes in four probes a pin; a blind sweep
    // takes nine and breaks wire doing it, and the roll runs out long before
    // this does.
    for (int guard = 0; guard < 400 && session.picking(); ++guard) {
        const sim::Lockpicking& wire = session.lockpicking();
        if (stopAtPins >= 0 && wire.pinsSet() >= stopAtPins) {
            break;
        }
        if (maxProbes >= 0 && probes >= maxProbes) {
            break;
        }
        if (wire.pinsSet() != pinsSeen) {
            pinsSeen = wire.pinsSet();
            forget();
        }
        if (low > high) {
            forget();
        }
        // The midpoint of what is left, or -- when the midpoint has already
        // been tried, which is the no-feel case -- the nearest notch to it that
        // has not been. THE SWEEP HAS TO REACH EVERY DEPTH: a version of this
        // that only ever walked the window upward looped over four of the nine
        // notches forever and could not open a lock at all.
        std::int32_t aim = low + (high - low) / 2;
        if ((tried & (1U << aim)) != 0U) {
            aim = -1;
            for (std::int32_t spread = 1; spread < sim::kPinDepths && aim < 0; ++spread) {
                const std::int32_t mid = low + (high - low) / 2;
                const std::int32_t down = mid - spread;
                const std::int32_t up = mid + spread;
                if (down >= low && (tried & (1U << down)) == 0U) {
                    aim = down;
                } else if (up <= high && (tried & (1U << up)) == 0U) {
                    aim = up;
                }
            }
            if (aim < 0) {
                // Every notch in the window is spent. Widen to the whole track,
                // and if that is spent too the pin moved under us -- forget it
                // all and start again.
                forget();
                aim = 0;
                while (aim < sim::kPinDepths && (tried & (1U << aim)) != 0U) {
                    ++aim;
                }
                if (aim >= sim::kPinDepths) {
                    aim = 0;
                }
            }
        }
        session.movePick(aim - wire.depth());
        session.probeLock();
        ++probes;
        tried |= 1U << aim;
        switch (wire.lastFeel()) {
            case sim::Feel::TooShallow:
                // The pin is DEEPER than where the pick was held.
                low = aim + 1;
                break;
            case sim::Feel::TooDeep:
                high = aim - 1;
                break;
            case sim::Feel::NoFeel:
                // No information at all. The notch is crossed off and nothing
                // else is learned. This is the apprentice's whole game, and it
                // is why an apprentice forces boxes.
                break;
            case sim::Feel::Set:
            case sim::Feel::Broke:
            case sim::Feel::Jammed:
            case sim::Feel::Open:
            default:
                forget();
                break;
        }
    }
    return probes;
}

// ---------------------------------------------------------------------------
// S10: --trail, the investigation walked
// ---------------------------------------------------------------------------

/// THE WHOLE DISTRICT, ONE BAND. kCaptureRegion is the Gilded Gull and the
/// pavement outside it, which is the right box for a scripted burglary and far
/// too small for a walk to the Mission. The trail crosses the ward, so it gets
/// a box that is the ward -- the world's own dimensions, one band deep.
///
/// It is still a BOX and still the same exact breadth-first search that
/// region_path.hpp has run since S2: nothing here is a new pathfinder.
///
/// Walks the body across the DISTRICT to a tile, on the band it is already on.
/// True when it got there -- and a false is a real answer, not a warning: a
/// lead nobody can walk to is a lead nobody can read.
[[nodiscard]] bool walkAcrossDistrict(Session& session, std::int32_t tileX,
                                      std::int32_t tileY) {
    sim::TileBox box;
    box.x0 = 0;
    box.y0 = 0;
    box.x1 = session.tiles().sizeX() - 1;
    box.y1 = session.tiles().sizeY() - 1;
    box.z0 = session.body().band();
    box.z1 = session.body().band();
    sim::RegionPath router(session.tiles(), box);
    std::vector<sim::PathStep> route;
    const sim::PathStep from{session.body().tileX(), session.body().tileY(),
                             session.body().band()};
    const sim::PathStep to{tileX, tileY, session.body().band()};
    if (!router.find(from, to, route)) {
        return false;
    }
    for (const sim::PathStep& waypoint : route) {
        const std::int32_t wx = sim::q8_tile_centre(waypoint.x);
        const std::int32_t wy = sim::q8_tile_centre(waypoint.y);
        bool arrived = false;
        // A generous guard: a tile is eight movement steps at a walk and a
        // corner costs a few more. Sixty is slack, not a licence to wander.
        for (int guard = 0; guard < 60 && !arrived; ++guard) {
            arrived = stepToward(session, wx, wy);
        }
        if (!arrived) {
            return false;
        }
    }
    return true;
}

/// How many leads the walked trail READ. Not a fixed beat count: the trail is
/// authored data and the number of leads is a property of casebook.json, so a
/// hard-coded target here would be a second source of truth for the same thing.
std::int32_t gTrailRead = 0;
std::int32_t gTrailWalked = 0;
std::int32_t gTrailUnreached = 0;

/// WALKS THE BLOODLETTER TRAIL, through the same two calls a keyboard makes.
///
/// Every beat is: find the nearest lead the casebook currently holds OPEN on
/// this band, walk to it with the district's own breadth-first router, and
/// press Q. Nothing here reads a clue it has not walked to and nothing here
/// opens a lead the simulation did not open.
///
/// It stops when there is nothing open left that it can reach, which is an
/// honest end condition: the two leads on the strand plane (z10) are one band
/// down and this line does not climb, so it leaves them in the book and says so
/// in the summary rather than pretending the case is finished.
[[nodiscard]] int runTrailLine(Session& session, const std::string& ending) {
    gTrailRead = 0;
    gTrailWalked = 0;
    gTrailUnreached = 0;
    const sim::CasebookRaws* raws = session.casebook().raws();
    if (raws == nullptr) {
        return 0;
    }
    std::int32_t last = -1;
    // `start` walks nowhere: it is the FIRST FRAME of a new game, which is the
    // one state a capture could not otherwise reach -- SessionConfig's opening
    // page is set by the client and not by the smoke path, so a scripted run
    // has to ask for it. Nothing else about the session differs.
    const int rounds = ending == "start" ? 0 : 32;
    for (int guard = 0; guard < rounds; ++guard) {
        // The nearest OPEN lead on this band. Nearest, because that is what a
        // player does, and because it makes the walk short enough to watch.
        std::int32_t best = -1;
        std::int32_t bestDistance = 0;
        for (const std::int32_t index : session.casebook().known()) {
            if (session.casebook().state(index) != sim::LeadState::Open) {
                continue;
            }
            const sim::Lead& lead = raws->leads()[static_cast<std::size_t>(index)];
            if (lead.site.band != session.body().band()) {
                continue;
            }
            const std::int32_t dx = lead.site.x - session.body().tileX();
            const std::int32_t dy = lead.site.y - session.body().tileY();
            const std::int32_t distance = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
            if (best < 0 || distance < bestDistance) {
                best = index;
                bestDistance = distance;
            }
        }
        if (best < 0) {
            break;
        }
        const sim::Lead& lead = raws->leads()[static_cast<std::size_t>(best)];
        // Stand a tile short of the anchor: the anchors are counters, flagstones
        // and sagging floors, and several of them are solid cells by design.
        // kLookRangeTiles is four, so anywhere in the room will do.
        bool got = walkAcrossDistrict(session, lead.site.x, lead.site.y);
        if (!got) {
            for (const std::int32_t offset : {1, -1, 2, -2}) {
                got = walkAcrossDistrict(session, lead.site.x + offset, lead.site.y);
                if (got) {
                    break;
                }
                got = walkAcrossDistrict(session, lead.site.x, lead.site.y + offset);
                if (got) {
                    break;
                }
            }
        }
        ++gTrailWalked;
        const std::int32_t before = session.casebook().readCount();
        session.examine();
        if (session.casebook().readCount() > before) {
            ++gTrailRead;
            last = best;
        } else {
            // Could not get near enough. Take it out of the running so the loop
            // cannot spin on it, and count it -- a lead the walk could not reach
            // is the single most useful thing this run can report.
            ++gTrailUnreached;
            (void)session.casebook().look(lead.site.x, lead.site.y, lead.site.band);
            break;
        }
        if (ending == "mission" && lead.id == "mission-backroom") {
            break;
        }
        if (ending == "weighhouse" && lead.id == "weighhouse-ledger") {
            break;
        }
        if (ending == "hold" && lead.id == "drowned-hold") {
            break;
        }
    }
    // And where the shutter goes.
    if (ending == "notes" || ending == "start") {
        session.toggleCasebook();
    } else if (ending == "keys") {
        session.toggleKeys();
    } else if (last >= 0) {
        // STAND BACK, THEN LOOK AT IT. The anchors are counters, flagstones and
        // sagging floors, so a body that has walked onto one is standing with
        // its nose against masonry -- the first S10 capture of the Mission is a
        // photograph of a brown wall. Four tiles back is inside kLookRangeTiles,
        // so the clue is still readable from there, and it is the difference
        // between a frame of a place and a frame of a surface.
        const sim::Lead& lead = raws->leads()[static_cast<std::size_t>(last)];
        for (const std::int32_t back : {7, 6, 5, 4, 3}) {
            if (walkAcrossDistrict(session, lead.site.x, lead.site.y + back) ||
                walkAcrossDistrict(session, lead.site.x + back, lead.site.y) ||
                walkAcrossDistrict(session, lead.site.x, lead.site.y - back) ||
                walkAcrossDistrict(session, lead.site.x - back, lead.site.y)) {
                break;
            }
        }
        session.body().setYaw(sim::bearingTo(session.body().tileX(), session.body().tileY(),
                                             lead.site.x, lead.site.y));
        // AND THE CLUE IS NOT RE-PRINTED FOR THE SHUTTER. Backing off puts the
        // body outside kLookRangeTiles, which is the correct answer -- seven
        // tiles is out of reach of a flagstone -- so pressing Q again from here
        // says "NOTHING HERE WORTH WRITING DOWN", which it should. The clue is
        // in the casebook, which is where a clue belongs; what this frame is of
        // is the PLACE, with the CASE row under it saying where the trail
        // stands. `--trail=notes` is the frame of the writing.
    }
    return gTrailRead;
}

[[nodiscard]] int runBurgleLine(Session& session, const std::string& ending) {
    int landed = 0;
    gBurgleWatchers = 0;
    gLockEndingProbes = 0;
    gBurgleBeatMask = 0;
    std::int32_t beat = 0;
    const auto mark = [&](bool ok) {
        if (ok) {
            gBurgleBeatMask |= 1 << beat;
            ++landed;
        }
        ++beat;
    };

    // 1. down on the haunches. Half speed and worth more than twenty levels.
    session.toggleCrouch();
    mark(session.stance() == sim::Stance::Crouched);

    // 2. IN AT THE DOOR, AND NOT MADE OUT STANDING IN IT. Crouched, at two in
    //    the morning, with the doors just barred and the lanterns out: the room
    //    is dark and nearly empty, which is the whole reason a burglar keeps
    //    these hours.
    //
    //    THE CHECK IS HERE AND NOT LATER, deliberately. A lift that goes wrong
    //    is an offence, and an offence puts a bouncer across the room at you --
    //    at which point being seen is the game working rather than the stealth
    //    failing, and a beat that could not tell those two apart would be a
    //    beat worth nothing.
    //
    //    S10: AND SOMEBODY HAS TO BE THERE TO MISS YOU. The S9 review proved
    //    this beat landed with the notice rule hard-wired to seen -- at two in
    //    the morning the doorway is empty, everyone in reach reads oblivious,
    //    and "nobody saw me" was a fact about the hour rather than about
    //    stealth. The bit now needs a body in range that is awake, upright and
    //    on this floor, so it is a claim that can fail. If the night staff have
    //    all gone to bed, the burglar walks in until one of them is in reach.
    walkToTile(session, sim::gull::kDoorX0, sim::gull::kDoorY + 1);
    if (session.tavern().watchersInReach() == 0) {
        // Nobody at the door. Go and stand near whoever is still up -- being
        // unseen next to a man is the beat; being unseen in an empty room is
        // not, and the run should fail rather than quietly pass if there is
        // nobody in the building at all.
        const sim::Actor* awake = nullptr;
        for (const sim::Actor& actor : session.tavern().actors()) {
            if (actor.present() && actor.band() == session.body().band() &&
                actor.activity() != sim::Activity::Downed &&
                actor.role() != sim::ActorRole::Vermin) {
                awake = &actor;
                break;
            }
        }
        if (awake != nullptr) {
            walkToTile(session, awake->tileX() + 2, awake->tileY());
        }
    }
    gBurgleWatchers = session.tavern().watchersInReach();
    mark(session.hidden() && gBurgleWatchers > 0);

    // 3. a hand in the coat of whoever is still on a stool.
    //
    //    THE BEAT IS THE HAND, NOT THE COIN, and that is stated rather than
    //    quietly assumed. Whether a lift SUCCEEDS is CRACKSMANSHIP against the
    //    mark's own STREETWISE (Tavern::liftFrom), and a scripted burglar
    //    starts at level zero, so against the night staff of a captains' house
    //    it fails -- correctly, and it still teaches the hands. What this beat
    //    proves is that the verb is reachable from the keys and reached a body;
    //    the summary prints which way it went, so a reader is never told a lift
    //    landed when it did not.
    const std::int32_t purseBefore = session.tavern().playerCoin();
    bool handWentIn = false;
    for (const char* target : {"Kled Tarbeck", "Finch", "Gerta Saltcotte", "Master Venn"}) {
        const sim::Actor* who = nullptr;
        for (const sim::Actor& actor : session.tavern().actors()) {
            if (actor.name() == target && actor.present() && actor.coin() > 0) {
                who = &actor;
                break;
            }
        }
        if (who == nullptr) {
            continue;
        }
        walkToTile(session, who->tileX(), who->tileY());
        const sim::Tavern::StealResult tried = session.tavern().liftFrom();
        if (tried.result != sim::ServiceResult::TooFar) {
            handWentIn = true;
        }
        if (session.tavern().playerCoin() > purseBefore) {
            break;
        }
    }
    mark(handWentIn);

    // 4. away across the room to the foot of the stair, and up it.
    walkToTile(session, sim::gull::kStairX, sim::gull::kStairY);
    climbAndLand(session);
    mark(session.body().band() == sim::gull::kUpperBand);

    // 5. to a bed-foot that is not yours, and the wire in.
    const sim::gull::GuestRoom& box = sim::gull::kRooms[2];
    walkToTile(session, box.standX, box.standY);
    session.steal();
    mark(session.picking());

    // 6. WORK THE LOCK, the way the player in this body can. workTheWire() is
    //    a strategy and not a cheat: it reads the depth the pick is at, how
    //    many pins have dropped, and what the last probe FELT LIKE, and it
    //    reads nothing else. Below kFeelLevel that degenerates to the same
    //    blind sweep S9 shipped, because a hand with no feel has no better
    //    move; at and above it, it bisects.
    workTheWire(session);
    const std::int32_t roomBit = 1 << 2;
    if ((session.tavern().openedLocks() & roomBit) == 0) {
        // The wire is gone and the lock is ruined. A burglar with a jammed lock
        // and a job to do puts a shoulder to it, which always works and is the
        // loudest thing in the building.
        session.forceLock();
    }
    mark((session.tavern().openedLocks() & roomBit) != 0);

    // 7. and the box, emptied.
    const std::int32_t before = session.tavern().crackedBoxes();
    session.steal();
    mark(session.tavern().crackedBoxes() != before);

    if (ending == "lock") {
        // AND HE STARTS ON THE NEXT ONE, WITH THE HANDS THE FIRST ONE GAVE HIM.
        //
        // S9 SHIPPED THIS FRAME POSED AND SAID IT WAS NOT. Its comment read
        // "Nothing is faked" directly above a setPicks() that refilled the roll
        // out of nowhere and a pinDepth() lookup that drove the pick to the
        // answer -- the S9 review's fourth finding, and it was right: the CALLS
        // were the ones a keyboard reaches, the STATE was not, and the captured
        // PNG read PICKS 5 STRAIN 0/3 after a burglary that had just spent
        // every pick in the roll.
        //
        // What happens instead is the arc the retuning exists for. Beat 6 has
        // just cost this burglar most of his wire and taught his hands a great
        // deal doing it -- every probe is a use, and usesForLevel charges 18 of
        // them for CRACKSMANSHIP 3, which is kFeelLevel. So he goes back down
        // to the snug, buys wire off the Skyrunners' own contact with the same
        // G a keyboard presses, comes back up, and works the box across the
        // landing with a hand that can now hear it. Nothing here knows where a
        // pin is.
        walkToTile(session, sim::gull::kStairX, sim::gull::kStairY);
        session.dropDown();
        // The oath first. Nobody sells a stranger wire -- Tavern::buyPicks
        // refuses anyone off the Skyrunners' ladder in as many words -- so the
        // burglar takes their first rung off Finch through the same Join topic
        // the roof line uses, and then buys. Both are keys.
        if (speakTo(session, "Finch")) {
            pick(session, sim::TopicKind::Join);
            session.closeConversation();
        }
        const sim::Actor* contact = nullptr;
        for (const sim::Actor& actor : session.tavern().actors()) {
            if (actor.role() == sim::ActorRole::SkyrunnerContact && actor.present()) {
                contact = &actor;
                break;
            }
        }
        if (contact != nullptr) {
            walkToTile(session, contact->tileX(), contact->tileY());
            // Two sets if he can afford them: a bisect wants four notches of
            // slack a pin and the roll is the only thing that buys patience.
            session.tavern().buyPicks();
            session.tavern().buyPicks();
        }
        walkToTile(session, sim::gull::kStairX, sim::gull::kStairY);
        climbAndLand(session);
        const sim::gull::GuestRoom& next = sim::gull::kRooms[3];
        walkToTile(session, next.standX, next.standY);
        session.steal();
        // Worked, and then STOPPED WHILE IT IS STILL BEING WORKED -- either
        // when the last pin is one away or after a handful of probes, whichever
        // comes first. That is what makes the shutter catch the minigame rather
        // than its aftermath, and it is a budget on the PLAYER'S side of the
        // wire, not a hand on the lock's side.
        gLockEndingProbes = workTheWire(session, sim::kStrongboxPins - 1, 8);
        session.body().setYaw(sim::bearingTo(session.body().tileX(), session.body().tileY(),
                                             next.bedX, next.bedY));
        session.body().setPitch(sim::angle_from_degrees(-14));
    } else if (ending == "street") {
        session.dropDown();
        walkToTile(session, sim::gull::kStreetX, sim::gull::kStreetY);
    } else if (ending == "taproom") {
        walkToTile(session, sim::gull::kStairX, sim::gull::kStairY);
        session.dropDown();
        walkToTile(session, sim::gull::kBartenderX, sim::gull::kBarY + 2);
        session.body().setYaw(sim::kFacingNorth);
    } else {
        // Back down the landing, looking at the room that has just been done.
        // NOT nose-first against the bed block: standing on the tile you
        // cracked the box from fills the frame with one CLOTH face and shows
        // nothing, which is what the first shipped attempt at this frame did.
        walkToTile(session, 152, 72);
        session.body().setYaw(sim::bearingTo(session.body().tileX(), session.body().tileY(),
                                             box.bedX, box.bedY));
        session.body().setPitch(sim::angle_from_degrees(-8));
    }
    return landed;
}

[[nodiscard]] int runSkyrunLine(Session& session, const std::string& ending) {
    const sim::DialogueDirector& talk = session.tavern().dialogue();
    const std::string questId = "skyrunner-tenant";

    // 1. the oath. Finch keeps the snug after ten.
    if (speakTo(session, "Finch")) {
        pick(session, sim::TopicKind::Join);
        session.closeConversation();
    }

    // 2. two purses, off whoever is nearest that is not the fence.
    for (const char* mark : {"Sella Brinewall", "Tarn Wrenhale", "Wick Hempson",
                             "Hobbin Mastwright", "Colm Tarbeck"}) {
        if (talk.journal().counter(questId) >= 2) {
            break;
        }
        if (speakTo(session, mark)) {
            pick(session, sim::TopicKind::PickPocket);
            session.closeConversation();
        }
    }
    if (speakTo(session, "Finch")) {
        pick(session, sim::TopicKind::QuestBeat);
        session.closeConversation();
    }

    // 3. a box above the stair. Up the authored stair first -- with the up-key,
    //    because walking at a stair in this build does nothing (see
    //    PlayerBody::mantle).
    walkToTile(session, sim::gull::kStairX, sim::gull::kStairY);
    climbAndLand(session);
    walkToTile(session, sim::gull::kRooms[1].standX, sim::gull::kRooms[1].standY);
    // AND THE BOX IS ACTUALLY OPENED, WHICH THIS LINE STOPPED DOING IN S9 AND
    // NOBODY NOTICED FOR A SPRINT.
    //
    // S9 made a locked box put the WIRE in rather than opening itself -- its own
    // documented change -- and left this beat as a single steal(), which from
    // that day forward put a wire in a lock and walked away. The stage counts a
    // CRACKED box, so the run has landed 2 of 9 and exited 1 ever since; the S9
    // review re-ran --burgle, --nemesis and --ward and did not re-run this one.
    // Verified against the S9 tip before changing anything: identical output,
    // `stages=2 cracks=0`, so this is S9's regression and not S10's.
    //
    // What it does now is what a burglar does: work the wire, and put a
    // shoulder to it if the wire loses. Both are keys.
    session.steal();
    workTheWire(session);
    if (session.picking()) {
        session.stopPicking();
    }
    session.forceLock();
    session.steal();
    comeDownstairs(session);
    reportTo(session, "Finch");

    // 4. the roof. AND THE ROUND TRIP IS THE POINT: a counted stage only counts
    //    what you do WHILE IT IS THE STAGE, so the line is walked the way a
    //    player walks it -- go, do the one thing, come back and say so. A
    //    script that did all six acts and then reported six times would prove
    //    nothing about the questline at all.
    upOntoTheLead(session);
    downFromTheLead(session);
    reportTo(session, "Finch");

    // 5. the alley.
    upOntoTheLead(session);
    walkToTile(session, sim::gull::kFootprintX0, 70);
    session.body().setYaw(sim::kFacingWest);
    climbAndLand(session);
    // Back east over the same two tiles of air, and then off the Gull's own
    // west edge into the alley rather than off the far side of a house whose
    // street the router does not carry.
    session.body().setYaw(sim::kFacingEast);
    climbAndLand(session);
    downFromTheLead(session);
    reportTo(session, "Finch");

    // 6. sell it -- WHICH FIRST MEANS EARNING THE RUNG THAT MAKES HIM A FENCE.
    //    A cutpurse is not a fence, the Skyrunners' second rung is measured in
    //    SKYRUNNING, and nothing but roofs raises that. So the body goes back
    //    up and works the alley until the roofs will have it: leap west, leap
    //    east, and again, which is exactly what the guild's name means.
    upOntoTheLead(session);
    walkToTile(session, sim::gull::kFootprintX0, 70);
    for (int i = 0; i < 24 && talk.skills().level(sim::kRoofSkill) < 5; ++i) {
        session.body().setYaw(sim::kFacingWest);
        climbAndLand(session);
        session.body().setYaw(sim::kFacingEast);
        climbAndLand(session);
    }
    downFromTheLead(session);
    if (speakTo(session, "Finch")) {
        pick(session, sim::TopicKind::Advance);
        pick(session, sim::TopicKind::Fence);
        pick(session, sim::TopicKind::QuestBeat);
        session.closeConversation();
    }

    // 7. lean on somebody.
    for (const char* mark : {"Tarn Wrenhale", "Colm Tarbeck", "Sella Brinewall",
                             "Hobbin Mastwright"}) {
        if (talk.journal().counter(questId) > 0) {
            break;
        }
        if (speakTo(session, mark)) {
            pick(session, sim::TopicKind::Lean);
            session.closeConversation();
        }
    }
    reportTo(session, "Finch");

    // 8. the bale, out of the door past the Watch.
    walkToTile(session, sim::gull::kBaleX, sim::gull::kBaleY);
    session.steal();
    walkToTile(session, sim::gull::kStreetX, sim::gull::kStreetY);
    walkToTile(session, sim::gull::kDoorX0, sim::gull::kDoorY + 1);
    if (speakTo(session, "Finch")) {
        pick(session, sim::TopicKind::QuestBeat);
        // 9. and what a tenant is.
        pick(session, sim::TopicKind::QuestBeat);
    }

    if (ending == "away") {
        session.closeConversation();
    }
    standBackFrom(session, "Finch");
    return talk.journal().stagesDone(questId);
}

}  // namespace

int scriptedStartHour(const SmokeRunConfig& config) noexcept {
    // ONE IN THE MORNING, and the hour is the point.
    //
    // Finch keeps the snug from ten at night, so the line cannot start before
    // then -- but S6 put the ward's law in the same taproom between nine and
    // one, and a man who commits all six of the ward's crimes in one evening
    // with the impound keeper three tables away is a man who loses a hand for
    // it. That is the game working: the run was arrested on its way back from
    // the last delivery and lost two of its nine beats, with `sentence=maimed`
    // printed in its own summary.
    //
    // So the scripted burglar does what a burglar does and starts after the
    // Watch has gone home. Finch is still in the snug until three; Watchman
    // Cull left at one. Nothing about the line changed -- the hour did, and the
    // hour is now part of what the line teaches.
    if (config.skyrun) {
        return 1;
    }
    // Father Maell takes an evening hour in the Gull between seven and half
    // past nine. Eight is the middle of it, which is also the default.
    if (config.flame) {
        return 20;
    }
    // The bounty wants two men in one room: Watchman Cull comes in at nine and
    // Father Maell leaves at half past. Quarter past is the only quarter of an
    // hour in the day when the ward will both sell you the work and sign for
    // it.
    if (config.contract) {
        return 21;
    }
    // The nemesis arc wants a labourer on shift and a room with his own guild
    // in it: Tarn Wrenhale keeps the taproom from six in the evening until one,
    // and the crowd he is enlisted out of is there from seven. Eight.
    if (config.nemesis) {
        return 20;
    }
    // TWO IN THE MORNING, and the hour is the whole point of the line.
    //
    // The Gull shuts at two: the doors are barred, the lanterns and the table
    // candles are out (Tavern::houseLights only shows them while isOpen()), the
    // hearth is banked from three, and the crowd has gone. What is left is a
    // dark room with a handful of night staff in it -- which is the only state
    // of this building a burglary is actually possible in, and proving that is
    // the point of `--burgle`. Later than three and there is nobody left to
    // creep past at all; earlier than two and the doors are open and the room
    // is lit. Run it at eight in the evening and it lands fewer beats and says
    // so, which is the game working.
    if (config.burgle) {
        return 2;
    }
    // The roof line needs the door open and nobody in particular.
    return -1;
}

SmokeRunResult runSmoke(const SmokeRunConfig& config) {
    SmokeRunResult result;
    SessionConfig started = config.session;
    if (!started.timeOfDayGiven) {
        const int hour = scriptedStartHour(config);
        if (hour >= 0) {
            started.timeOfDay = hour * 3600;
        }
    }
    Session session(started);

    // A scripted walk, so a capture at N steps is a picture of the game moving
    // rather than a picture of the spawn. Forward, with a slow drift of the
    // head, which is enough to exercise collision and band changes. `walk` off
    // holds position and lets the ROOM move instead, which is what a capture of
    // a tavern at two different hours wants.
    sim::MoveInput input;
    input.forward = config.walk ? 1 : 0;
    for (int i = 0; i < config.steps; ++i) {
        input.turn = (config.walk && (i / 90) % 4 == 3) ? 1 : 0;
        session.step(input);
    }

    // A capture OF a conversation, not of somebody standing beside one. Driven
    // through exactly the calls a keypress makes, so the frame a sprint proves
    // itself with is a picture of the game and not of a test harness.
    if (config.talk) {
        session.interact();
        result.talking = session.talking();
        for (const int topic : config.topics) {
            if (topic >= 0) {
                session.chooseTopic(static_cast<std::size_t>(topic));
            }
        }
        if (config.offer >= 0 && session.haggling()) {
            const int delta = config.offer - session.haggleOffer();
            session.adjustOffer(delta);
        }
        if (config.again) {
            session.closeConversation();
            session.interact();
        }
        result.talking = session.talking();
    }

    if (config.flame) {
        result.flameStages = runFlameLine(session, config.flameEnd);
        result.talking = session.talking();
        const sim::Questline* line = session.tavern().dialogue().quests().find("flame-disciple");
        result.scriptedWanted += line == nullptr ? 0 : static_cast<std::int32_t>(
                                                          line->stages.size());
        result.scriptedLanded += result.flameStages;
    }

    if (config.roofs) {
        // Four beats: the stair, the floor above it, the wall, the lead.
        constexpr std::int32_t kRoofBeats = 4;
        const std::int32_t landed = static_cast<std::int32_t>(
            runRoofLine(session, config.roofsEnd));
        result.scriptedWanted += kRoofBeats;
        result.scriptedLanded += landed;
        result.talking = session.talking();
    }

    if (config.contract) {
        const std::int32_t landed =
            static_cast<std::int32_t>(runContractLine(session, config.contractEnd));
        result.contractBeats = landed;
        result.scriptedWanted += kContractBeats;
        result.scriptedLanded += landed;
        result.talking = session.talking();
    }

    if (config.nemesis) {
        const std::int32_t landed =
            static_cast<std::int32_t>(runNemesisLine(session, config.nemesisEnd));
        result.nemesisBeats = landed;
        result.scriptedWanted += kNemesisBeats;
        result.scriptedLanded += landed;
        result.talking = session.talking();
    }

    if (config.trail) {
        // NOT A FIXED BEAT COUNT. The trail is authored data; how many leads
        // there are is casebook.json's business, and a target typed in here
        // would be a second source of truth for it. What the run owes is that
        // every lead it WALKED TO it also read -- an unreachable lead fails the
        // run, which is exactly the failure a coordinate drifting would cause.
        result.trailRead = static_cast<std::int32_t>(runTrailLine(session, config.trailEnd));
        result.trailWalked = gTrailWalked;
        result.scriptedWanted += gTrailWalked;
        result.scriptedLanded += result.trailRead;
        result.talking = session.talking();
    }

    if (config.burgle) {
        const std::int32_t landed =
            static_cast<std::int32_t>(runBurgleLine(session, config.burgleEnd));
        result.burgleBeats = landed;
        result.burgleBeatMask = gBurgleBeatMask;
        result.watchersInReach = gBurgleWatchers;
        result.scriptedWanted += kBurgleBeats;
        result.scriptedLanded += landed;
        result.talking = session.talking();
    }

    if (config.skyrun) {
        result.skyrunStages = runSkyrunLine(session, config.skyrunEnd);
        result.talking = session.talking();
        const sim::Questline* line =
            session.tavern().dialogue().quests().find("skyrunner-tenant");
        result.scriptedWanted += line == nullptr ? 0 : static_cast<std::int32_t>(
                                                           line->stages.size());
        result.scriptedLanded += result.skyrunStages;
    }

    // S7. The cursor, last, so it survives every scripted line above it. This
    // is the flag that lets a frame be taken OF the row that does not fit --
    // see SmokeRunConfig::cursorRow on why that had to be possible.
    if (config.cursorRow > 0 && session.talking()) {
        const int want = config.cursorRow - 1;
        session.moveTopicCursor(want - session.topicCursor());
    }

    Framebuffer frame(config.session.width, config.session.height);
    result.stats = session.drawFrame(frame);
    result.lampCount = session.lampCount();
    result.endTileX = session.body().tileX();
    result.endTileY = session.body().tileY();
    result.endBand = session.body().band();
    result.actorsInFrame = session.tavern().presentCount();
    result.craftLevel = session.tavern().dialogue().skills().level(sim::kThieverySkill);
    result.pinsSet = session.picking() ? session.lockpicking().pinsSet() : 0;

    const int hour = session.timeOfDay() / 3600;
    const int minute = (session.timeOfDay() / 60) % 60;
    std::ostringstream summary;
    summary << "steps=" << config.steps << " at (" << result.endTileX << ',' << result.endTileY
            << ",z" << result.endBand << ") facing " << sim::compass_point(session.body().yaw())
            << " | " << (hour < 10 ? "0" : "") << hour << ':' << (minute < 10 ? "0" : "") << minute
            << ' ' << session.placeLabel() << " | lamps=" << result.lampCount
            << " actors=" << result.actorsInFrame
            << " art=" << (session.atlas().fromAuthoredArt() ? "custom" : "procedural")
            << " | world px=" << result.stats.worldPixels
            << " sky px=" << result.stats.skyPixels
            << " sprite px=" << result.stats.spritePixels
            << " actor px=" << result.stats.actorPixels << " luma="
            << result.stats.meanLuma << " colours=" << result.stats.distinctColours;
    if (config.trail) {
        const sim::Casebook& notes = session.casebook();
        summary << " | trail read=" << result.trailRead << '/' << result.trailWalked
                << " leads=" << notes.known().size()
                << " cold=" << notes.coldCount()
                << " unreached=" << gTrailUnreached
                << " dread=" << notes.dread()
                << " closed=" << (notes.closed() ? "yes" : "no")
                << " flame=" << session.legend().row(sim::LegendTrack::Flame).rung
                << " called=" << session.legend().title()
                << " notes=" << (session.casebookOpen() ? "open" : "shut");
    }
    if (config.burgle) {
        summary << " | burgle beats=" << result.burgleBeats << '/' << kBurgleBeats
                << " mask=" << gBurgleBeatMask
                << " lift=" << (session.tavern().dialogue().crimes().tally(sim::Crime::Lift) > 0
                                    ? "tried"
                                    : "none")
                << " light=" << session.tavern().lightOnPlayer()
                << " noise=" << session.tavern().playerNoise()
                // WHETHER THE STEALTH BEAT PROVED ANYTHING, printed beside it.
                // `watchers=` is how many awake, upright bodies were in range
                // when beat 2 was judged; a `hidden` with `watchers=0` beside it
                // is a fact about the hour and not about being unseen, and the
                // S9 review had to read the source to work that out. Now it is
                // one word away from the claim.
                << " watchers=" << gBurgleWatchers
                << " " << (session.hidden() ? "hidden" : "seen") << " "
                << " picking=" << (session.picking() ? "yes" : "no")
                << " pins=" << (session.picking() ? session.lockpicking().pinsSet() : 0)
                << " nextprobes=" << gLockEndingProbes
                << " picks=" << session.picks()
                << " locks open=" << session.tavern().openedLocks()
                << " jammed=" << session.tavern().jammedLocks()
                << " forced=" << session.tavern().forcedLocks()
                << " cracked=" << session.tavern().crackedBoxes()
                << " cracksmanship="
                << session.tavern().dialogue().skills().level(sim::kThieverySkill)
                << " skyrunning="
                << session.tavern().dialogue().skills().level(sim::kRoofSkill);
    }
    if (config.nemesis) {
        const sim::Nemesis* worst = session.tavern().nemesis().worst();
        summary << " | nemesis beats=" << result.nemesisBeats << '/' << kNemesisBeats;
        // WHO IS ON THE FRAME, named, so a capture cannot quietly photograph
        // the wrong docker. The first shipped attempt at the `talk` ending did
        // exactly that -- his stool and Wick Hempson's are one tile apart.
        if (session.talking()) {
            summary << " talking to " << session.tavern().dialogue().speaker().name;
        }
        if (worst != nullptr) {
            summary << " " << worst->who << " x" << worst->wins;
            if (!worst->title.empty()) {
                summary << " " << worst->title;
            }
            const sim::ChapterRaw* house =
                session.tavern().nemesis().chapters().at(worst->chapter);
            if (house != nullptr) {
                summary << " of " << house->displayName << " (" << worst->members.size()
                        << " members, toll "
                        << session.tavern().nemesis().tollPercent(worst->faction) << "%)";
            }
            if (worst->holdsGround()) {
                summary << " holds "
                        << session.ward()
                               .raws()
                               .plots()[static_cast<std::size_t>(
                                   session.ward().plots()[static_cast<std::size_t>(worst->plot)]
                                       .raw)]
                               .name;
            }
        }
    }
    if (config.roofs || config.skyrun) {
        const sim::DialogueDirector& talk = session.tavern().dialogue();
        const std::int32_t roofs = talk.factions().indexOf("skyrunners");
        const std::int32_t watch = talk.factions().indexOf("watch");
        summary << " | roofs stages=" << result.skyrunStages << " rank="
                << talk.standings().rank(roofs) << ' ' << talk.standings().rankTitle(roofs)
                << " standing=" << talk.standings().standing(roofs)
                << " watch=" << talk.standings().standing(watch)
                << " climbs=" << talk.crimes().tally(sim::Crime::RoofRun)
                << " lifts=" << talk.crimes().tally(sim::Crime::Lift)
                << " cracks=" << talk.crimes().tally(sim::Crime::Burgle)
                << " leans=" << talk.crimes().tally(sim::Crime::Extort)
                << " fences=" << talk.crimes().tally(sim::Crime::Fence)
                << " runs=" << talk.crimes().tally(sim::Crime::Smuggle)
                << " heat=" << talk.crimes().heat()
                << " warrant=" << (talk.crimes().warrant() ? "yes" : "no")
                << " skyrunning=" << talk.skills().level(sim::kRoofSkill)
                // S6: and whether the ward took him for any of it. A scripted
                // line that fell short because a watchman crossed the room is a
                // very different failure from one that fell short because a
                // beat is broken, and the summary has to be able to tell them
                // apart.
                << " arrests=" << talk.crimes().arrests() << " sentence="
                << sim::sentenceName(talk.crimes().lastSentence());
    }
    if (config.contract) {
        const sim::DialogueDirector& talk = session.tavern().dialogue();
        const sim::Stash& sack = talk.crimes().stash();
        summary << " | work beats=" << result.contractBeats << '/' << kContractBeats
                << " day=" << talk.contracts().day()
                << " open=" << talk.contracts().contracts().size()
                << " taken=" << talk.contracts().takenCount()
                << " paid=" << talk.contracts().paidCount()
                << " lost=" << talk.contracts().failedCount()
                << " earned=" << talk.contracts().coinEarned()
                << " scalps=" << sack.count(sim::Contraband::Scalp)
                << " load=" << sack.illicitWeight() << "dr"
                << " heat=" << talk.crimes().heat();
    }
    if (config.flame) {
        const sim::DialogueDirector& talk = session.tavern().dialogue();
        const std::int32_t temple = talk.factions().indexOf("temple");
        summary << " | flame stages=" << result.flameStages << '/'
                << (talk.quests().find("flame-disciple") == nullptr
                        ? 0
                        : static_cast<int>(talk.quests().find("flame-disciple")->stages.size()))
                << " rank=" << talk.standings().rank(temple) << ' '
                << talk.standings().rankTitle(temple)
                << " standing=" << talk.standings().standing(temple)
                << " influence=" << talk.standings().influence(temple)
                << " known=" << talk.grimoire().size() << " forged="
                << talk.grimoire().craftedCount()
                << " linkcraft=" << talk.skills().level(sim::kCraftingSkill);
    }
    // A SCRIPTED RUN THAT FELL SHORT SAYS SO, AND FAILS.
    //
    // S4's did neither: runFlameLine returned a stage count that runSmoke threw
    // away, and a run that landed ZERO of six stages photographed the wrong
    // person and exited 0. The S4 review found it. A capture tool that reports
    // success while photographing the wrong thing will mislabel a future
    // sprint's evidence, so this is a hard failure and not a warning.
    if (result.scriptFellShort()) {
        summary << " | WARNING: scripted run landed " << result.scriptedLanded << " of "
                << result.scriptedWanted << " beats";
    }
    result.summary = summary.str();

    // The corner stamp, unless somebody is standing in it: while a conversation
    // is open the top-left is the speaker's name, and two strings in the same
    // eleven characters of screen is unreadable in a capture.
    // NOT WHILE ANY PANEL IS UP. The top-left is the speaker's name whenever
    // the conversation surface is drawing -- and S10 gave that surface two more
    // users, the casebook and the key list, neither of which sets `talking`.
    // The first S10 capture shipped "GRANADAD 0.10.0" printed straight through
    // "THE CASEBOOK" because this test only knew about the third of them.
    if (config.stamp && !result.talking && !session.casebookOpen() && !session.keysOpen()) {
        const int scale = std::max(1, frame.height() / 180);
        // DERIVED, NOT TYPED. S9's read "GRANADAD S6" -- a literal three sprints
        // out of date, burnt into the top-left of every capture including all
        // four of S9's own, and found by the review in a PNG rather than in the
        // source. It reads the project version now, which CMake sets in one
        // place and build_info() carries, so there is nothing here left to
        // forget to update.
        const sim::BuildInfo info = sim::build_info();
        std::string stamp = "GRANADAD ";
        stamp.append(info.version);
        drawText(frame, 4 * scale, 4 * scale, stamp, Rgb{0.55F, 0.53F, 0.46F}, 0.7F, scale);
    }

    result.ok = !result.scriptFellShort();
    if (!config.screenshot.empty()) {
        // The PNG is still written. A frame of a run that fell short is
        // evidence OF the shortfall, and deleting it would make the failure
        // harder to diagnose rather than easier -- but ok stays false.
        const Framebuffer output =
            config.captureScale > 1 ? upscaleNearest(frame, config.captureScale) : frame;
        result.ok = writePng(output, config.screenshot) && result.ok;
    }
    return result;
}

}  // namespace granadad::render
