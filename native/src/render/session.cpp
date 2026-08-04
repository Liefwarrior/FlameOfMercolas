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

void Session::say(std::string line) {
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
        return;
    }
    // Wraps, so holding one direction walks the whole list.
    topicCursor_ = ((topicCursor_ + delta) % count + count) % count;
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
    if (talking()) {
        const int count = static_cast<int>(tavern_->dialogue().topics().size());
        if (count > 0 && topicCursor_ >= count) {
            topicCursor_ = count - 1;
        }
    } else {
        topicCursor_ = 0;
    }
}

void Session::closeConversation() {
    tavern_->endConversation();
    topicCursor_ = 0;
    haggleOffer_ = 0;
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
    for (const sim::Topic& topic : talk.topics()) {
        view.topics.push_back(topic.label);
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
            sprite.person = true;
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
    const std::string_view standing = tavern_->dialogue().ledger().reputationLabel();
    hud.standingLabel = standing;
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

SmokeRunResult runSmoke(const SmokeRunConfig& config) {
    SmokeRunResult result;
    Session session(config.session);

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
            << " sprite px=" << result.stats.spritePixels << " luma="
            << result.stats.meanLuma << " colours=" << result.stats.distinctColours;
    result.summary = summary.str();

    if (config.stamp) {
        const int scale = std::max(1, frame.height() / 180);
        drawText(frame, 4 * scale, 4 * scale, "GRANADAD S2", Rgb{0.55F, 0.53F, 0.46F}, 0.7F,
                 scale);
    }

    result.ok = true;
    if (!config.screenshot.empty()) {
        const Framebuffer output =
            config.captureScale > 1 ? upscaleNearest(frame, config.captureScale) : frame;
        result.ok = writePng(output, config.screenshot);
    }
    return result;
}

}  // namespace granadad::render
