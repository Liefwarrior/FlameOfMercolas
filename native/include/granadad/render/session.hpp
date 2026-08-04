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
#include "granadad/sim/compound.hpp"
#include "granadad/sim/engine.hpp"
#include "granadad/sim/notables.hpp"
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
    /// True when somebody NAMED the hour. A scripted line whose cast is not in
    /// the room at the default hour sets its own clock when this is false, and
    /// never overrides an hour that was asked for. See scriptedStartHour().
    bool timeOfDayGiven = false;
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

    /// THE WARD'S OWN ROLL, IN THE WINDOWED GAME. S7 built the compounds and
    /// nothing outside a batch text report ever constructed one: the S7
    /// review's eighth finding, verbatim -- "3,303 lines of economy that the
    /// player cannot see, touch, or be affected by". A Session builds one now
    /// and registers it on the same engine the Gull runs on, so the roll is
    /// ticking while the player stands in the taproom, and the first thing in
    /// the game that reaches into it is the man who put them on the floor
    /// taking the Gullet's vacant charge.
    [[nodiscard]] sim::Ward& ward() noexcept { return *ward_; }
    [[nodiscard]] const sim::Ward& ward() const noexcept { return *ward_; }

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

    // --- S5: the three roof verbs, and the one thieving one ------------------
    //
    // On Session for exactly the reason the first three are: the suite drives
    // the code a keypress drives, and the client owns no game logic.

    /// SPACE. Gets you UP. Tries the mantle first -- a wall in front of you
    /// with a surface on top of it -- and a leap second, because those are the
    /// two answers to "get me across or over" and a player pressing one key
    /// should not have to know which of them the geometry wants.
    void climb();
    /// X. Steps off the ledge in front and takes the fall.
    void dropDown();
    /// G. Puts hands on whatever is here: the strongbox at a bed-foot, or the
    /// bale in the snug.
    ///
    /// S9: and the wire goes in first. A box whose lock is still shut opens the
    /// lockpicking surface rather than refusing; the box itself is emptied by
    /// the same key once the lock gives.
    void steal();

    // --- S9: the three verbs of a burglar ------------------------------------
    //
    // On Session for the reason all nine before them are: the suite drives the
    // code a keypress drives, and the client owns no game logic.

    /// C. Down on your haunches, or back up. Halves the walk and is worth more
    /// than twenty levels of skill -- see sim/stealth.hpp.
    void toggleCrouch();
    [[nodiscard]] sim::Stance stance() const noexcept;
    /// True when nobody present can currently make the player out.
    [[nodiscard]] bool hidden() const noexcept;
    /// "HIDDEN  DARK 12  QUIET" or "SEEN  LIT 71  LOUD", or empty when there is
    /// no room around the player. PUBLIC for the same reason stashLine is: the
    /// HUD rule is a testable claim, and a case pins what this says.
    [[nodiscard]] std::string stealthLine() const;
    /// "LOCK  PINS *-- DEPTH 0........ STRAIN 1/3 PICKS 4", or empty when no
    /// wire is in anything. PUBLIC for the same reason: the surface a
    /// lockpicking minigame draws is a claim about the HUD rule, and a case
    /// pins both what it says and that it stays on its edge.
    [[nodiscard]] std::string lockLine() const;

    /// T. Lifts from whoever is at your elbow, with no conversation open.
    void lift();

    /// True while the wire is in a lock and the client should be routing keys
    /// to the lockpicking surface instead of to movement.
    [[nodiscard]] bool picking() const noexcept;
    /// Everything that surface draws.
    [[nodiscard]] const sim::Lockpicking& lockpicking() const noexcept;
    /// How many picks are in the roll.
    [[nodiscard]] int picks() const noexcept;
    /// W/S while picking: raises and lowers the pick.
    void movePick(int delta);
    /// SPACE while picking: one probe at the depth the pick is held at.
    void probeLock();
    /// F while picking, or standing at a jammed box: the shoulder, and the
    /// noise.
    void forceLock();
    /// ESC while picking: the wire comes out and the lock relocks.
    void stopPicking();

    /// S8. Puts the player back on their feet after somebody has put them on
    /// the floor: the room revives them and moves the clock on, the body goes
    /// out onto the quay apron where an ejected man ends up, and the message
    /// line carries whatever the winner said standing over them.
    ///
    /// ON Session AND NOT IN THE CLIENT for the same reason the six verbs are:
    /// the suite drives exactly the code a real defeat drives. It is not bound
    /// to a key -- nothing the player presses reaches it -- because losing a
    /// fight is not a verb.
    void settleDefeat();

    /// What the last roof move did, in words. Exposed so a test can assert on
    /// the REPORT and not only on where the body ended up.
    [[nodiscard]] const std::string& lastRoofMove() const noexcept { return roofMove_; }

    /// True between a leap being armed and the feet touching down: the arc is
    /// in the air and its landing has not been charged yet. Exposed so a test
    /// can prove the press did NOT resolve the jump.
    [[nodiscard]] bool awaitingLanding() const noexcept { return awaitingLanding_; }
    /// Runs the step pump until an armed leap has landed, and answers how many
    /// steps that took. Zero when nothing was in the air.
    ///
    /// This is not a shortcut past the simulation: it is the ordinary step()
    /// in a loop, the same one the client's pump calls, which is what makes a
    /// scripted capture a picture of the game rather than of a harness.
    int flyOutLeap();

    /// The last thing that happened, for the HUD. Fades after a few seconds.
    [[nodiscard]] const std::string& lastMessage() const noexcept { return message_; }

    /// "3 FLOWER  24DR", or empty when the sack is empty. Public because the
    /// HUD rule is a testable claim and not a preference: an inventory in this
    /// game is ONE LINE on an edge until it has earned more, and a case pins
    /// both its content and its width.
    [[nodiscard]] std::string stashLine() const;
    /// "RUN 4 FLOWER FOR SQUALL 3/4", or empty when no job is open.
    [[nodiscard]] std::string contractLine() const;
    /// "RIVAL TARN WRENHALE - CRAFTLORD x3  HUNTING", or empty when nobody has
    /// ever put the player down. PUBLIC for the same reason stashLine is: the
    /// HUD rule is a testable claim and not a preference, and a case pins both
    /// what this says and that it stays on its edge.
    [[nodiscard]] std::string rivalLine() const;

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
    /// Runs the ward's day forward to the tavern's calendar. Called after every
    /// step and after every jump of the clock -- see the note on the definition
    /// for why the ward could not previously see a slept night.
    void syncWardToCalendar();
    void say(std::string line);
    /// Charges a landing to the body: the skill, the guild's teaching, the hit
    /// points and the roof-run tally, in the one place a landing is resolved.
    void settleLanding(const sim::RoofResult& move);
    /// "THE TEMPLE OF THE FLAME - DISCIPLE", or empty when on no rung.
    [[nodiscard]] std::string guildLine() const;
    /// "WANTED  HEAT 62  LOOT 3", or empty when the ward has heard nothing.
    [[nodiscard]] std::string heatLine() const;
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
    /// The compound roll, also owned by the engine and borrowed here.
    sim::Ward* ward_ = nullptr;
    /// The notables registry the roll is refused against. Held because Ward
    /// takes it by reference and the reference has to outlive the constructor.
    std::unique_ptr<sim::NotableRegistry> who_;
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
    /// What the last roof move did, for the HUD and for the tests.
    std::string roofMove_;
    /// The leap that is in the air, and whether one is. Settled by step() on
    /// the movement step the feet touch -- see the note there.
    sim::RoofResult pendingLanding_;
    bool awaitingLanding_ = false;
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
    /// S7. Put the topic cursor on this row (1-based, as the numbers printed
    /// beside the topics) without picking it. Zero leaves the cursor where the
    /// conversation put it.
    ///
    /// A CAPTURE FLAG THAT EXISTS BECAUSE OF A DEFECT. S6's shipped frame
    /// printed "7 SIGN ON: THE." and the fix -- a detail line spelling the
    /// picked row out in full -- can only be PHOTOGRAPHED with the cursor on
    /// the row that does not fit. Without this, the frame that proves the fix
    /// cannot be taken and the claim goes back to being a paragraph.
    int cursorRow = 0;
    /// Close whatever conversation is open and start it again. This is how a
    /// capture shows the SAME person greeting you differently after you have
    /// done something to them -- rob them, then say hello.
    bool again = false;
    /// S5. Climb onto the Gilded Gull's roof and look down at the ward: in at
    /// the door, up the stair, out over the north wall, and turn round. WHERE
    /// is "roof" (standing on the lead), "leap" (across the alley onto the next
    /// house) or "street" (the drop back down).
    bool roofs = false;
    std::string roofsEnd = "roof";
    /// S5. Play the Skyrunner line: sign on with Finch, take two purses, crack
    /// a box above the stair, get on the roof, cross the alley, sell what was
    /// taken, lean on somebody, and run a bale out past the Watch.
    bool skyrun = false;
    std::string skyrunEnd = "talk";
    /// S6. Play the ward's own bounty end to end: take it off Watchman Cull,
    /// get the Flame's mark from Father Maell before he goes home, skip to the
    /// hour the rats are out, hunt them on the taproom floor and hand them back
    /// across the same table. WHERE is "talk" (the finished conversation) or
    /// "away" (closed, so the HUD's own sack and job lines are visible).
    bool contract = false;
    std::string contractEnd = "talk";
    /// S8. Play the nemesis arc: pick a fight with a named labourer, lose it,
    /// wake on the quay, come back the next evening and lose it twice more --
    /// by which time he has a rung, a trade house with members in it, a
    /// permanent cut of the ward's prices and his name on the compound roll as
    /// a Den Duke. WHERE is "talk" (standing in front of what he became) or
    /// "away" (closed, so the HUD's own RIVAL line is visible).
    bool nemesis = false;
    std::string nemesisEnd = "away";
    /// S9. Play a burglary: crouch, cross a dark taproom unseen, lift a purse
    /// off somebody who does not feel it, up the stair, wire into a guest's
    /// strongbox, work the pins, and empty it. WHERE is "box" (standing over
    /// the box you have just opened), "lock" (the wire in the NEXT box, so the
    /// lockpicking surface itself is on screen), "taproom" (back down among the
    /// people who did not hear you) or "street" (out of the door with it).
    bool burgle = false;
    std::string burgleEnd = "box";
    /// Run the Priest of the Flame line end to end and capture wherever it
    /// finishes: the oath, the night pot, the captain's word, the report, the
    /// teaching, and a crafting composed at the bench. Driven through the same
    /// Session calls a keypress makes, walking the body with real movement
    /// steps between parties -- nothing here reaches into the simulation
    /// sideways, which is the only way a captured frame is evidence.
    bool flame = false;
    /// How the scripted line leaves the screen for the shutter: "talk" is the
    /// finished conversation with its topic list, "bench" opens the priest's
    /// workbench, "away" closes the conversation so the HUD's own guild line
    /// and objective are visible. Only read when `flame` is set.
    std::string flameEnd = "talk";
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
    /// The same for the Skyrunner line.
    std::int32_t skyrunStages = 0;
    /// How many of the six beats of the bounty run landed.
    std::int32_t contractBeats = 0;
    /// How many of the seven beats of the nemesis arc landed.
    std::int32_t nemesisBeats = 0;
    /// And of the seven beats of the burglary.
    std::int32_t burgleBeats = 0;
    /// WHICH of them landed, one bit each, in order. A count says how many; a
    /// mask says which, and a case that cares about one specific claim -- beat
    /// 2, "somebody awake was in reach and did not make me out" -- can name it.
    std::int32_t burgleBeatMask = 0;
    /// CRACKSMANSHIP at the moment of capture, and how many pins are down on
    /// whatever lock is still under the wire. Both zero when no lock is open.
    /// Exposed so the acceptance can assert the ARC -- hands taught by the
    /// first box opening the second -- rather than only the beat count.
    std::int32_t craftLevel = 0;
    std::int32_t pinsSet = 0;
    /// How many people were awake, upright, on the player's floor and in range
    /// at the moment the burglar's stealth beat was judged. A "nobody saw me"
    /// with this at zero is a fact about the hour, not about stealth.
    std::int32_t watchersInReach = 0;
    /// What a scripted line WANTED to land, and what it did.
    ///
    /// S5 ADDS THESE BECAUSE S4'S CAPTURE PATH LIED. runFlameLine returned a
    /// stage count that runSmoke threw on the floor: a run that landed ZERO of
    /// six stages, photographed the wrong person and exited 0. The S4 review
    /// found it. A capture tool that reports success while photographing the
    /// wrong thing will mislabel a future sprint's evidence, so a short run now
    /// says so in the summary AND fails the process.
    std::int32_t scriptedWanted = 0;
    std::int32_t scriptedLanded = 0;
    [[nodiscard]] bool scriptFellShort() const noexcept {
        return scriptedWanted > 0 && scriptedLanded < scriptedWanted;
    }
};

/// The hour of the clock a scripted line NEEDS, or -1 when it does not care.
///
/// S5's `--skyrun` shipped broken at its own documented invocation and the S6
/// review found it: the flag defaults to eight in the evening, Finch is
/// authored to keep the snug from ten (tavern.cpp, hourOfDay(22)), and a line
/// whose first beat is an oath sworn to a man who is not in the building lands
/// zero of nine beats and exits 1. `--flame` had exactly the same shape in S4
/// and was fixed by hand; this is the same fix, stated once, for every scripted
/// line there will ever be.
///
/// A line SETS the clock only when nobody named an hour. `--time=4 --skyrun`
/// still runs at four in the morning and still fails loudly, because an hour
/// the caller asked for is an hour the caller meant.
[[nodiscard]] int scriptedStartHour(const SmokeRunConfig& config) noexcept;

/// Runs a scripted session and, optionally, writes a PNG. No window, no GPU,
/// no display server: this is the path every later sprint proves itself with.
[[nodiscard]] SmokeRunResult runSmoke(const SmokeRunConfig& config);

}  // namespace granadad::render
