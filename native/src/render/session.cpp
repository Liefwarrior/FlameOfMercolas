#include "granadad/render/session.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <utility>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/render/capture.hpp"
#include "granadad/sim/angle.hpp"
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
    engine_->boot();
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
    if (talking()) {
        return;
    }
    syncTavernToBody();
    sim::Tavern::StealResult took = tavern_->crackStrongbox();
    if (took.result == sim::ServiceResult::TooFar) {
        // Nothing to open here. The other things hands can be put on are a bale
        // in the snug and, since S6, a rat on the floor -- which is the ward's
        // own source of the one contraband the ward pays a bounty ON.
        took = tavern_->handleBale();
    }
    if (took.result == sim::ServiceResult::TooFar) {
        took = tavern_->takeScalp();
    }
    say(took.line);
}

void Session::step(const sim::MoveInput& input) {
    // The room moves first, then the shove it asked for is applied to the body
    // that owns its own collision, then the player's own input. That order is
    // deliberate: a bouncer's shove and a player's step in the same movement
    // step both go through PlayerBody, so neither can push the other through a
    // wall.
    syncTavernToBody();
    tavern_->stepMovement();
    const std::int32_t shoveX = tavern_->takePlayerShoveX();
    const std::int32_t shoveY = tavern_->takePlayerShoveY();
    if (shoveX != 0 || shoveY != 0) {
        body_->push(shoveX, shoveY);
    }
    body_->step(input);
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
    constexpr std::size_t kAlertColumns = 56;
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
    if (!talking() || slot < 0 || slot >= kTopicPageSize) {
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
    const sim::ServiceResult slept = tavern_->sleep();
    if (slept == sim::ServiceResult::Served) {
        timeOfDay_ = tavern_->timeOfDay();
        settings_.timeOfDay = timeOfDay_;
        stepsThisSecond_ = 0;
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
        const float floorZ = static_cast<float>(actor.band());
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

        if (down) {
            // Flat out on the boards, and wide instead of tall.
            part(0.10F, 0.34F * build, 0.11F * build, look.torso);
            part(0.13F, 0.11F * build, 0.09F * build, look.head);
            continue;
        }
        part(0.20F, 0.17F * build, 0.20F * build, look.legs);
        part(0.55F, 0.21F * build, 0.20F * build, look.torso);
        part(0.83F, 0.12F * build, 0.10F * build, look.head);

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
        face.z = floorZ + 0.85F;
        face.halfWidth = 0.075F * build;
        face.halfHeight = 0.055F * build;
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
    const sim::DialogueDirector& talk = tavern_->dialogue();
    const std::int32_t top = talk.standings().highestRankedFaction();
    if (top < 0) {
        return {};
    }
    const sim::Faction* faction = talk.factions().at(top);
    const std::string name = faction == nullptr ? std::string("GUILD") : faction->displayName;
    return clip(upperAscii(name) + " - " + upperAscii(talk.standings().rankTitle(top)), 34);
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
        // A hearth burns at the floor, a candle on a table, a lantern hangs.
        flame.z = static_cast<float>(light.z) + (isHearth ? 0.30F : (isLantern ? 0.82F : 0.55F));
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
    const bool conversing = talking();
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
    // The rung, and what the line wants next. Bottom-left, over the health bar.
    const std::string guild = guildLine();
    const std::string objective = objectiveLine();
    hud.guildLabel = conversing ? std::string_view{} : std::string_view{guild};
    hud.objectiveLabel = conversing ? std::string_view{} : std::string_view{objective};
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
    hud.showHealth = !conversing;
    // The panel FIRST, the HUD over it: a bouncer's warning has to survive
    // being told mid-conversation, and it is the one line that outranks a menu.
    drawDialogue(target, dialogueView());
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
    // Finch keeps the snug from ten at night; the whole Skyrunner line is sworn
    // to him and cannot start without him in the room.
    if (config.skyrun) {
        return 22;
    }
    // Father Maell takes an evening hour in the Gull between seven and half
    // past nine. Eight is the middle of it, which is also the default.
    if (config.flame) {
        return 20;
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

    if (config.skyrun) {
        result.skyrunStages = runSkyrunLine(session, config.skyrunEnd);
        result.talking = session.talking();
        const sim::Questline* line =
            session.tavern().dialogue().quests().find("skyrunner-tenant");
        result.scriptedWanted += line == nullptr ? 0 : static_cast<std::int32_t>(
                                                           line->stages.size());
        result.scriptedLanded += result.skyrunStages;
    }

    Framebuffer frame(config.session.width, config.session.height);
    result.stats = session.drawFrame(frame);
    result.lampCount = session.lampCount();
    result.endTileX = session.body().tileX();
    result.endTileY = session.body().tileY();
    result.endBand = session.body().band();
    result.actorsInFrame = session.tavern().presentCount();

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
                << " skyrunning=" << talk.skills().level(sim::kRoofSkill);
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
    if (config.stamp && !result.talking) {
        const int scale = std::max(1, frame.height() / 180);
        drawText(frame, 4 * scale, 4 * scale, "GRANADAD S5", Rgb{0.55F, 0.53F, 0.46F}, 0.7F,
                 scale);
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
