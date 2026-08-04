#pragma once

// One place that knows how to stand in the Docks and look at them.
//
// The client's SDL loop, the `--screenshot` path and the test suite all need
// the same thing: load the world, place the body, build the renderer, advance
// some movement steps, draw a frame. If each of them assembled that itself they
// would drift, and the screenshot a sprint proves itself with would stop being
// a picture of the game.
//
// So it is assembled once, here, with no SDL anywhere in sight.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "granadad/content/world.hpp"
#include "granadad/render/atlas.hpp"
#include "granadad/render/framebuffer.hpp"
#include "granadad/render/hud.hpp"
#include "granadad/render/lamps.hpp"
#include "granadad/render/world_renderer.hpp"
#include "granadad/sim/player.hpp"
#include "granadad/sim/tile_query.hpp"

namespace granadad::render {

/// Everything a session needs to know before it starts.
struct SessionConfig {
    /// Repo content/ directory. Defaults to granadad::content::contentDir().
    std::filesystem::path contentDir;
    std::string world = "docks_surface";
    /// Spawn tile. Negative means "use the authored Docks spawn".
    std::int32_t spawnX = -1;
    std::int32_t spawnY = -1;
    std::int32_t spawnBand = -1;
    /// BAM facing at spawn.
    std::int32_t spawnYaw = 0;
    bool spawnYawGiven = false;
    /// Internal render resolution, before any window upscale.
    int width = 640;
    int height = 360;
    /// Seconds since midnight.
    int timeOfDay = 20 * 3600;
    /// Horizontal field of view in degrees.
    int fovDegrees = 90;
};

/// A loaded, standing, drawable session.
class Session {
public:
    /// Loads everything. Throws content::FormatError or std::runtime_error if
    /// the world cannot be read — a missing world is fatal, a missing art pack
    /// or lamp bake is not.
    explicit Session(const SessionConfig& config);

    [[nodiscard]] const SessionConfig& config() const noexcept { return config_; }
    [[nodiscard]] const sim::TileQuery& tiles() const noexcept { return *tiles_; }
    [[nodiscard]] sim::PlayerBody& body() noexcept { return *body_; }
    [[nodiscard]] const sim::PlayerBody& body() const noexcept { return *body_; }
    [[nodiscard]] const WorldRenderer& renderer() const noexcept { return *renderer_; }
    [[nodiscard]] const TileAtlas& atlas() const noexcept { return atlas_; }
    [[nodiscard]] std::size_t lampCount() const noexcept { return renderer_->lamps().size(); }

    /// Advances the body by one movement step.
    void step(const sim::MoveInput& input);

    /// Advances by `steps` movement steps with the same input.
    void stepMany(const sim::MoveInput& input, int steps);

    /// The camera the body is currently looking through.
    [[nodiscard]] Camera camera() const noexcept;

    /// Draws the world and the HUD into `target`.
    FrameStats drawFrame(Framebuffer& target) const;

    /// The HUD line under the compass: which band the player is on, in words.
    [[nodiscard]] std::string bandLabel() const;

private:
    SessionConfig config_;
    content::World world_;
    std::unique_ptr<sim::TileQuery> tiles_;
    TileAtlas atlas_;
    std::unique_ptr<WorldRenderer> renderer_;
    std::unique_ptr<sim::PlayerBody> body_;
    RenderSettings settings_;
};

/// What a scripted capture run was asked to do.
struct SmokeRunConfig {
    SessionConfig session;
    /// Movement steps to run before the frame is taken. 0 captures the spawn.
    int steps = 0;
    /// Where the PNG goes. Empty writes nothing.
    std::filesystem::path screenshot;
    /// Integer upscale applied to the captured PNG. 1 writes the raw buffer.
    int captureScale = 2;
    /// A one-line stamp burnt into the corner of the capture.
    bool stamp = true;
};

struct SmokeRunResult {
    bool ok = false;
    FrameStats stats;
    std::string summary;
    std::size_t lampCount = 0;
    std::int32_t endTileX = 0;
    std::int32_t endTileY = 0;
    std::int32_t endBand = 0;
};

/// Runs a scripted session and, optionally, writes a PNG. No window, no GPU,
/// no display server: this is the path every later sprint proves itself with.
[[nodiscard]] SmokeRunResult runSmoke(const SmokeRunConfig& config);

}  // namespace granadad::render
