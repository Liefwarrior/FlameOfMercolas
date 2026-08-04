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

void Session::interact() {
    const sim::TalkResult talk = tavern_->talkToNearest();
    if (talk.result == sim::ServiceResult::NobodyThere) {
        say("NOBODY WITHIN REACH");
        return;
    }
    const sim::Actor* who =
        tavern_->nearestTo(body_->x(), body_->y(), 2 * sim::kSubOne);
    if (who != nullptr && who->role() == sim::ActorRole::Bartender) {
        const sim::ServiceResult bought = tavern_->buyDrink();
        if (bought == sim::ServiceResult::Served) {
            say(talk.speaker + " POURS. -" + std::to_string(sim::kDrinkPrice) + " C");
            return;
        }
        say(talk.speaker + ": " + std::string(sim::serviceResultName(bought)));
        return;
    }
    if (who != nullptr && who->role() == sim::ActorRole::Innkeeper) {
        const sim::ServiceResult rented = tavern_->rentRoom();
        if (rented == sim::ServiceResult::Served) {
            say(talk.speaker + ": ROOM " + std::to_string(tavern_->rentedRoom() + 1) +
                " IS YOURS");
            return;
        }
        say(talk.speaker + ": " + std::string(sim::serviceResultName(rented)));
        return;
    }
    say(talk.speaker + ": " + talk.line);
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
    std::vector<Lamp> lights;
    if (tavern_->fireLit()) {
        // The hearth, in the south wall. Two cells of it, so the fire lights
        // the wall face it is set into as well as the floor in front.
        for (std::int32_t x = sim::gull::kHearthX0; x <= sim::gull::kHearthX1; ++x) {
            Lamp fire;
            fire.name = "gull_hearth";
            fire.x = x;
            fire.y = sim::gull::kHearthY;
            fire.z = sim::gull::kGroundBand;
            fire.luminance = 26;
            fire.warmth = LampWarmth::Fire;
            lights.push_back(fire);
        }
    }
    if (tavern_->isOpen()) {
        // A candle on each of the four taproom tables. Out with the doors.
        static constexpr std::int32_t kTableX[] = {148, 151, 148, 151};
        static constexpr std::int32_t kTableY[] = {69, 69, 74, 74};
        for (int i = 0; i < 4; ++i) {
            Lamp candle;
            candle.name = "gull_candle";
            candle.x = kTableX[i];
            candle.y = kTableY[i];
            candle.z = sim::gull::kGroundBand;
            candle.luminance = 17;
            candle.warmth = LampWarmth::Fire;
            lights.push_back(candle);
        }
        // Three hanging lanterns down the length of the taproom. The Gull is
        // the captains' house and the district's grandest room; charts on the
        // walls are no use in the dark, and a fifteen-tile interior lit by two
        // hearth cells and four candles reads as a cellar. Cooler than the
        // fire, so the room has two colours of light in it and not one.
        static constexpr std::int32_t kLanternX[] = {150, 154, 152};
        static constexpr std::int32_t kLanternY[] = {68, 68, 75};
        for (int i = 0; i < 3; ++i) {
            Lamp lantern;
            lantern.name = "gull_lantern";
            lantern.x = kLanternX[i];
            lantern.y = kLanternY[i];
            lantern.z = sim::gull::kGroundBand;
            lantern.luminance = 22;
            lantern.warmth = LampWarmth::Lantern;
            lights.push_back(lantern);
        }
    }
    return lights;
}

std::vector<SpriteInstance> Session::actorSprites() const {
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

    const std::vector<SpriteInstance> people = actorSprites();
    sprites.insert(sprites.end(), people.begin(), people.end());

    const FrameStats stats = renderer_->renderFrame(target, camera(), settings, sprites);

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
    hud.roomLabel = room;
    // A bouncer's warning outranks anything the player did to themselves: it is
    // the one line in this game they must not miss.
    const bool warned = !tavern_->lastWarning().empty() &&
                        tavern_->playerStanding() != sim::Standing::Welcome;
    hud.alert = warned ? std::string_view{tavern_->lastWarning()} : std::string_view{message_};
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
