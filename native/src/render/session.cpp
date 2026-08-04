#include "granadad/render/session.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

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
    return out;
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
    settings_.timeOfDay = config_.timeOfDay;
}

void Session::step(const sim::MoveInput& input) {
    body_->step(input);
}

void Session::stepMany(const sim::MoveInput& input, int steps) {
    for (int i = 0; i < steps; ++i) {
        body_->step(input);
    }
}

Camera Session::camera() const noexcept {
    const float half = static_cast<float>(config_.fovDegrees) * 0.5F * kPi / 180.0F;
    return Camera::fromBody(body_->x(), body_->y(), body_->eyeZ(), body_->yaw(), body_->pitch(),
                            std::tan(half));
}

std::string Session::bandLabel() const {
    const std::int32_t band = body_->band();
    if (band == sim::docks::kBandQuayside) {
        return "TARWALK - QUAYSIDE";
    }
    if (band == sim::docks::kBandMidSlope) {
        return "ROPEWYND - MID SLOPE";
    }
    if (band == sim::docks::kBandUpper) {
        return "SALTGATE RISE - UPPER";
    }
    if (band < sim::docks::kBandQuayside) {
        return "UNDER THE PIERS";
    }
    return "THE DOCKS";
}

FrameStats Session::drawFrame(Framebuffer& target) const {
    // The flicker phase is a pure function of the body's step count, so the
    // same scripted session captures the same frame every time.
    const float phase = static_cast<float>(body_->stepCount()) / 60.0F;
    std::vector<SpriteInstance> sprites = renderer_->lampSprites(phase);

    // A lamp is not a hole in the sky at noon. The flame billboards fade out as
    // the daylight comes up, so a lit district reads at dusk and disappears
    // into ordinary daylight the way it should.
    const float lampMix = 1.0F - 0.9F * skyAt(settings_.timeOfDay).daylight;
    for (SpriteInstance& sprite : sprites) {
        sprite.glow *= lampMix;
        sprite.halfWidth *= 0.45F + 0.55F * lampMix;
        sprite.halfHeight *= 0.45F + 0.55F * lampMix;
    }

    const FrameStats stats = renderer_->renderFrame(target, camera(), settings_, sprites);

    HudState hud;
    hud.health = 100;
    hud.healthMax = 100;
    hud.yawBam = body_->yaw();
    const std::string label = bandLabel();
    hud.locationLabel = label;
    drawHud(target, hud);
    return stats;
}

SmokeRunResult runSmoke(const SmokeRunConfig& config) {
    SmokeRunResult result;
    Session session(config.session);

    // A scripted walk, so a capture at N steps is a picture of the game moving
    // rather than a picture of the spawn. Forward, with a slow drift of the
    // head, which is enough to exercise collision and band changes.
    sim::MoveInput input;
    input.forward = 1;
    for (int i = 0; i < config.steps; ++i) {
        input.turn = (i / 90) % 4 == 3 ? 1 : 0;
        session.step(input);
    }

    Framebuffer frame(config.session.width, config.session.height);
    result.stats = session.drawFrame(frame);
    result.lampCount = session.lampCount();
    result.endTileX = session.body().tileX();
    result.endTileY = session.body().tileY();
    result.endBand = session.body().band();

    std::ostringstream summary;
    summary << "steps=" << config.steps << " at (" << result.endTileX << ',' << result.endTileY
            << ",z" << result.endBand << ") facing " << sim::compass_point(session.body().yaw())
            << " | lamps=" << result.lampCount
            << " art=" << (session.atlas().fromAuthoredArt() ? "custom" : "procedural")
            << " | world px=" << result.stats.worldPixels
            << " sky px=" << result.stats.skyPixels
            << " sprite px=" << result.stats.spritePixels << " luma="
            << result.stats.meanLuma << " colours=" << result.stats.distinctColours;
    result.summary = summary.str();

    if (config.stamp) {
        const int scale = std::max(1, frame.height() / 180);
        drawText(frame, 4 * scale, 4 * scale, "GRANADAD S1", Rgb{0.55F, 0.53F, 0.46F}, 0.7F,
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
