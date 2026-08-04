#pragma once

// One place that knows how to stand in the Docks, look at them, and let them
// carry on without you.
//
// The client's SDL loop, the `--screenshot` path and the test suite all need
// the same thing: load the world, place the body, build the renderer, advance
// some movement steps, draw a frame. If each of them assembled that itself they
// would drift, and the screenshot a sprint proves itself with would stop being
// a picture of the game.
//
// So it is assembled once, here, with no SDL anywhere in sight.
//
// WHAT S2 ADDED: THE WORLD NOW TICKS. S1's session was a diorama -- it stepped
// the body and nothing else, and the time of day was frozen at construction. It
// now runs the real PhasedEngine on the real two clocks: sixty movement steps
// to one simulated second, one engine tick a second, and the clock on the wall
// moves with it. The Gilded Gull is registered as a system on that engine, so
// its fourteen actors keep their hours whether the player is in the room or on
// the other side of the district.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "granadad/content/world.hpp"
#include "granadad/render/atlas.hpp"
#include "granadad/render/dialogue_view.hpp"
#include "granadad/render/framebuffer.hpp"
#include "granadad/render/hud.hpp"
#include "granadad/render/lamps.hpp"
#include "granadad/render/world_renderer.hpp"
#include "granadad/sim/engine.hpp"
#include "granadad/sim/player.hpp"
#include "granadad/sim/tavern.hpp"
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
    /// Seconds since midnight AT THE START. It moves from there.
    int timeOfDay = 20 * 3600;
    /// Horizontal field of view in degrees.
    int fovDegrees = 90;
    /// The only persisted RNG state there is.
    std::uint64_t worldSeed = 0x4752414E41444144ull;  // "GRANADAD"
    /// How many simulated seconds pass per simulated second of movement.
    /// 1 is real time. Raising it is how a capture reaches a different hour
    /// without running the whole afternoon.
    int clockScale = 1;
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

    /// The room, and the fourteen people in it.
    [[nodiscard]] sim::Tavern& tavern() noexcept { return *tavern_; }
    [[nodiscard]] const sim::Tavern& tavern() const noexcept { return *tavern_; }

    /// Advances the body by one movement step, and the world with it.
    void step(const sim::MoveInput& input);

    /// Advances by `steps` movement steps with the same input.
    void stepMany(const sim::MoveInput& input, int steps);

    /// Seconds since midnight, right now.
    [[nodiscard]] int timeOfDay() const noexcept { return timeOfDay_; }
    /// Simulated seconds since the session began.
    [[nodiscard]] std::int64_t elapsedSeconds() const noexcept { return elapsedSeconds_; }
    /// Jumps the clock, without simulating what happened in between. What
    /// sleeping in a rented room does, and what a capture at a named hour does.
    void skipToHour(int hour);

    /// The camera the body is currently looking through.
    [[nodiscard]] Camera camera() const noexcept;

    /// Draws the world, everybody in it, and the HUD into `target`.
    FrameStats drawFrame(Framebuffer& target) const;

    /// Where the player is, in words the player would use. Derived from x, y
    /// AND the band — see sim::docks::kPlaces for why that is worth saying.
    [[nodiscard]] std::string placeLabel() const;

    // --- the three verbs ----------------------------------------------------
    //
    // On Session and not in the client, so the test suite drives exactly the
    // code a keypress does. The client binds E, F and R to these and owns no
    // game logic of its own.

    /// E. Opens a conversation with whoever is in reach; picks the topic under
    /// the cursor when one is already open.
    ///
    /// S3 CHANGED WHAT THIS KEY MEANS, deliberately. In S2 it produced one
    /// sentence and, for the two people who sell things, silently completed a
    /// purchase. Buying is now a topic on a list beside asking them about the
    /// vanished clerk, which is what "topics, not a single greeting" means.
    void interact();
    /// F. Throws a punch. In a taproom that is an offence, and the house has
    /// opinions about it.
    void punch();
    /// R. Sleeps, if there is a rented room and you are standing in it.
    void restHere();

    /// The last thing that happened, for the HUD. Fades after a few seconds.
    [[nodiscard]] const std::string& lastMessage() const noexcept { return message_; }

    // --- the conversation ---------------------------------------------------
    //
    // All of it on Session, for the same reason the three verbs are: the test
    // suite drives exactly the code a keypress does, and the client owns no
    // game logic of its own.

    [[nodiscard]] bool talking() const noexcept;
    /// True when the conversation has turned into an argument about a price.
    [[nodiscard]] bool haggling() const noexcept;
    /// Everything the surface draws. Empty and closed when nobody is talking.
    [[nodiscard]] DialogueViewState dialogueView() const;
    /// Which topic the cursor is on. An index into the WHOLE list.
    [[nodiscard]] int topicCursor() const noexcept { return topicCursor_; }
    /// Which page of the list is showing.
    [[nodiscard]] int topicPage() const noexcept { return topicPage_; }
    void moveTopicCursor(int delta);
    /// Turns to the next page and puts the cursor on its first topic. Bound to
    /// 0, which is the key the "0 MORE (2/3)" row on screen names.
    void nextTopicPage();
    /// Picks the topic printed with this number, 0-based within the visible
    /// page. THIS is what a number key does -- chooseTopic() takes an index
    /// into the whole list, and the two are only the same on page one.
    void chooseVisibleTopic(int slot);
    /// Picks a topic by index into the whole list. Out of range does nothing.
    void chooseTopic(std::size_t index);
    void closeConversation();

    // --- the workbench ------------------------------------------------------

    /// True when the priest has a composition half-made on the table.
    [[nodiscard]] bool forging() const noexcept;
    void moveForgeField(int delta);
    void adjustForge(int delta);
    /// Says "make it".
    void commitForge();
    /// Puts the tools down without making anything.
    void endForge();

    /// What the player is about to offer, while haggling.
    [[nodiscard]] int haggleOffer() const noexcept { return haggleOffer_; }
    void adjustOffer(int delta);
    /// Says the number.
    void makeOffer();
    /// Takes the price on the table without argument.
    void takeAskingPrice();

    /// The lights the tavern is currently showing: its hearth while the fire is
    /// lit, its table candles while the doors are open. Empty when the house is
    /// dark. Exposed so a test can assert the room goes dark rather than
    /// inferring it from pixels.
    [[nodiscard]] std::vector<Lamp> tavernLights() const;

    /// Every actor in view, as billboards, already shaded by the light where
    /// they stand.
    ///
    /// Takes the CAMERA, because one part of a person is view-dependent: the
    /// face only shows when they are looking roughly your way. See the note in
    /// the implementation on why the eight-point facing the simulation has been
    /// hashing since S2 finally gets drawn.
    [[nodiscard]] std::vector<SpriteInstance> actorSprites(const Camera& view) const;
    /// The same, through the body's own eye.
    [[nodiscard]] std::vector<SpriteInstance> actorSprites() const;

private:
    void syncTavernToBody();
    void say(std::string line);
    /// "THE TEMPLE OF THE FLAME - DISCIPLE", or empty when on no rung.
    [[nodiscard]] std::string guildLine() const;
    /// What the questline in progress wants next, in its own short label.
    [[nodiscard]] std::string objectiveLine() const;

    SessionConfig config_;
    content::World world_;
    std::unique_ptr<sim::TileQuery> tiles_;
    TileAtlas atlas_;
    std::unique_ptr<WorldRenderer> renderer_;
    std::unique_ptr<sim::PlayerBody> body_;
    std::unique_ptr<sim::PhasedEngine> engine_;
    /// Owned by the engine; borrowed here.
    sim::Tavern* tavern_ = nullptr;
    RenderSettings settings_;
    int timeOfDay_ = 0;
    std::int64_t elapsedSeconds_ = 0;
    std::int32_t stepsThisSecond_ = 0;
    std::string message_;
    /// Movement steps the message has left to live.
    std::int32_t messageSteps_ = 0;
    /// Which topic the cursor is on, and what number the player is about to
    /// name across a counter. Both are pure UI state -- the standing, the
    /// prices and the memory all live in the simulation.
    int topicCursor_ = 0;
    /// Which page of a long topic list is showing. Pure UI state: the list
    /// itself is the simulation's and paging never reorders it.
    int topicPage_ = 0;
    int haggleOffer_ = 0;
    /// Whether the client should be routing keys to the workbench. The bench
    /// itself lives in the simulation; this is only which keyboard mode the
    /// client is in.
    bool forgeOpen_ = false;
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
    /// Walk the scripted route forward. Off holds position, which is what a
    /// capture of a room wants.
    bool walk = true;
    /// Open a conversation with whoever is in reach before the shutter goes.
    /// This is how a sprint captures a frame OF a conversation rather than a
    /// frame of somebody standing next to one.
    bool talk = false;
    /// Topics to pick once the conversation is open, in order. Out-of-range
    /// entries are ignored, so a capture script cannot crash on a speaker who
    /// happens to have fewer things to say.
    std::vector<int> topics;
    /// A number to name across a counter once a haggle is open. Negative names
    /// nothing and leaves the counter showing.
    int offer = -1;
    /// Close whatever conversation is open and start it again. This is how a
    /// capture shows the SAME person greeting you differently after you have
    /// done something to them -- rob them, then say hello.
    bool again = false;
    /// Run the Priest of the Flame line end to end and capture wherever it
    /// finishes: the oath, the night pot, the captain's word, the report, the
    /// teaching, and a crafting composed at the bench. Driven through the same
    /// Session calls a keypress makes, walking the body with real movement
    /// steps between parties -- nothing here reaches into the simulation
    /// sideways, which is the only way a captured frame is evidence.
    bool flame = false;
};

struct SmokeRunResult {
    bool ok = false;
    FrameStats stats;
    std::string summary;
    std::size_t lampCount = 0;
    std::int32_t endTileX = 0;
    std::int32_t endTileY = 0;
    std::int32_t endBand = 0;
    /// Who was in the room when the shutter went.
    std::int32_t actorsInFrame = 0;
    /// True when a conversation was open at the moment of capture.
    bool talking = false;
    /// How many stages of the Priest of the Flame line the scripted
    /// playthrough actually finished. Zero when --flame was not asked for.
    std::int32_t flameStages = 0;
};

/// Runs a scripted session and, optionally, writes a PNG. No window, no GPU,
/// no display server: this is the path every later sprint proves itself with.
[[nodiscard]] SmokeRunResult runSmoke(const SmokeRunConfig& config);

}  // namespace granadad::render
