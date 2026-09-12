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

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "granadad/content/world.hpp"
#include "granadad/render/actor_sheet.hpp"
#include "granadad/render/anim.hpp"
#include "granadad/render/atlas.hpp"
#include "granadad/render/casebook_page.hpp"
#include "granadad/render/controls.hpp"
#include "granadad/render/creation_page.hpp"
#include "granadad/render/dialogue_view.hpp"
#include "granadad/render/framebuffer.hpp"
#include "granadad/render/hearing_page.hpp"
#include "granadad/render/hud.hpp"
#include "granadad/render/keys_page.hpp"
#include "granadad/render/lamps.hpp"
#include "granadad/render/map_view.hpp"
#include "granadad/render/menu_view.hpp"
#include "granadad/render/viewmodel_machine.hpp"
#include "granadad/render/world_renderer.hpp"
#include "granadad/sim/casebook.hpp"
#include "granadad/sim/compound.hpp"
#include "granadad/sim/engine.hpp"
#include "granadad/sim/legend.hpp"
#include "granadad/sim/letters.hpp"
#include "granadad/sim/notables.hpp"
#include "granadad/sim/player.hpp"
#include "granadad/sim/region_path.hpp"
#include "granadad/sim/tavern.hpp"
#include "granadad/sim/tile_query.hpp"
#include "granadad/sim/ward_actors.hpp"
#include "granadad/sim/ward_voice.hpp"

// FORWARD-DECLARED, NEVER INCLUDED HERE. The audio wiring pass gave Session
// an optional borrowed AudioEngine (see setAudio below), and keeping the type
// opaque in this header keeps every audio include confined to session.cpp --
// the same one-way-arrow discipline the CMake seam notes argue for: nothing
// that includes session.hpp learns anything about audio by doing so.
namespace granadad::audio {
class AudioEngine;
}

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
    /// BAM pitch at spawn (positive looks up), clamped by the body exactly as
    /// mouse look is. Only applied when given: a capture can look down at a
    /// doorstep or up at a roof line without a player at the mouse.
    std::int32_t spawnPitch = 0;
    bool spawnPitchGiven = false;
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
    /// OPEN THE CASE ON THE FIRST FRAME. What a new game does: the notes come
    /// up with the hook on them and the one lead the ward has given you, and
    /// the first step the player takes puts them away for good.
    ///
    /// OFF BY DEFAULT AND ON IN THE CLIENT, deliberately. A Session is built by
    /// the game, by every scripted capture and by two hundred test cases, and
    /// most of those want a frame of the world rather than a frame of a menu
    /// over it. main.cpp sets it; tests/test_firstrun.cpp sets the same flag
    /// and drives the same code, which is the pattern SmokeRunConfig already
    /// uses for --talk and --burgle.
    bool openingPage = false;
    /// COURIER CASE. RUN THE COURIER BEAT: a few seconds after the player's
    /// first world verb, Onna presses the mission sheet into their hand --
    /// the sheet lands in the Letters, the errand lands in the book, and the
    /// second authored case (content/raws/quests/mission_sheet.json) is
    /// live. OFF BY DEFAULT AND ON IN THE CLIENT, exactly openingPage's own
    /// pattern and for the same reason: a Session is built by two hundred
    /// test cases and every scripted capture, and a courier interrupting a
    /// committed demo route would move frames that must not move. main.cpp
    /// sets it for the windowed game (and keeps it OFF under --demo);
    /// --case sets it for the scripted line.
    bool courier = false;
    /// How many simulated seconds pass per simulated second of movement.
    /// 1 is real time. Raising it is how a capture reaches a different hour
    /// without running the whole afternoon.
    int clockScale = 1;
    /// Draw the HUD and the conversation surface at all. --nohud turns it off.
    ///
    /// THIS IS THE MEASURING INSTRUMENT AND IT IS THE WHOLE REASON IT EXISTS.
    /// "The HUD takes up too much room" is an opinion until somebody can put a
    /// number on it, and the only honest number is the difference between the
    /// same frame with the interface and without it. Capture a scene twice,
    /// once with this false and once with it true, and every pixel that differs
    /// is interface -- no estimating, no counting glyphs by hand, and the
    /// answer is checkable by anybody who runs the two commands.
    bool hud = true;
};

// ---------------------------------------------------------------------------
// CASE WATCH -- the drive on tape
// ---------------------------------------------------------------------------
//
// `--case` proves THE QUIET TENANT at CPU speed and `--case-watch` has to show
// the identical errand at a human one. The only way both can be true of ONE
// route is for the route to be recorded rather than re-invented: while the
// scripted drive runs, every public verb it spends lands on this tape -- one
// entry per Session::step (with the yaw the walker set before it, because
// stepToward steers the body's head directly) and one entry per instantaneous
// call (a punch, the letters toggle, the clock skip). Replaying the tape onto
// a fresh Session of the same config reproduces the drive step for step, and
// the frames a watcher spends BETWEEN entries cost the simulation nothing --
// which is the whole determinism argument, and test_case_watch.cpp holds it.
/// How tall a body of each kind stands, in tiles, and the width its drawing is
/// scaled to at that height. Defined in session.cpp (the figure tables); public
/// since the 3D build so the actor instancer sizes its rigs off the same rows.
struct FigureScale {
    float heightTiles;
    float widthTiles;
};
[[nodiscard]] FigureScale figureScaleOf(sim::WardType type) noexcept;
/// Which drawn figure a TAVERN role wears -- one look for a person in this
/// game, whether they stand in the Gull or on the quay. Patrons split by id.
[[nodiscard]] sim::WardType figureForRole(sim::ActorRole role, std::int32_t id) noexcept;

enum class WatchOpKind : std::uint8_t {
    /// One Session::step. `move` is the input, `a` the body yaw the drive's
    /// walker had set before stepping (stepToward writes yaw outside step).
    Step,
    Climb,
    /// dropDown -- how comeDownstairs leaves the guest floor. Missing from
    /// the first cut of this tape, and the twin gate caught it exactly as
    /// designed: the replay stood at the stair-head forever, beats 6-8 dead.
    Drop,
    Punch,
    Interact,
    Examine,
    CourierNow,
    /// toggleLetters -- opens on the first, closes on the second.
    Letters,
    /// chooseVisibleTopic(a).
    Topic,
    Casebook,
    /// skipToHour(a).
    SkipHour,
    Crouch,
    /// Not a verb: runCaseLine announcing which of its eight beats is about
    /// to play (`a` = beat index). The watch director hangs its caption on it.
    Chapter,
    /// Not a verb: a beat's own mark() verdict (`a` = beat index, `ok` = did
    /// it land). The watch director's shutter goes here -- the payoff frame.
    BeatLanded,
};

struct WatchOp {
    WatchOpKind kind = WatchOpKind::Step;
    sim::MoveInput move{};
    std::int32_t a = 0;
    bool ok = false;
    [[nodiscard]] bool operator==(const WatchOp&) const = default;
};

/// THE ARREST BEAT'S TWO HOLDS, in movement steps (sixty a second). The
/// officer's line is a sentence and a half on the alert row: two and a half
/// seconds, the message row's own four-second hold not yet run out when the
/// cut takes it. The plate is one line over black: a second and three
/// quarters, the rope plate's register at a fraction of its hold. Both are
/// presentation's numbers, read by the suite and the drive; the sim never
/// waits on either.
inline constexpr std::int32_t kTakenOfficerSteps = 150;
inline constexpr std::int32_t kTakenPlateSteps = 105;

/// A loaded, standing, drawable session.
class Session {
public:
    /// Loads everything. Throws content::FormatError or std::runtime_error if
    /// the world cannot be read — a missing world is fatal, a missing art pack
    /// or lamp bake is not.
    explicit Session(const SessionConfig& config);

    [[nodiscard]] const SessionConfig& config() const noexcept { return config_; }

    /// A FULL-SCREEN SURFACE THIS SESSION DOES NOT OWN IS UP; stand the
    /// furniture down for it.
    ///
    /// Every composed page in here already does this from the inside -- the
    /// ward map, the controls page and the casebook each zero the compass, the
    /// clock, the purse and the plates before they draw, because their own
    /// breadcrumb and their own right-aligned readout live exactly where those
    /// sit. The demo's title card is the same kind of surface with one
    /// difference: it is drawn by the director AFTER drawFrame has returned, so
    /// it cannot stand anything down from the inside. This is how it says so.
    ///
    /// It takes the WORLD SIGNAGE with it, which config_.hud deliberately does
    /// not. Signage is world content and the --nohud instrument must keep
    /// measuring it -- but a card is a presentation surface and a building name
    /// hanging off its border is the same collision the compass makes, one step
    /// further out.
    void setHudStandDown(bool down) noexcept { hudStandDown_ = down; }
    [[nodiscard]] bool hudStandDown() const noexcept { return hudStandDown_; }
    [[nodiscard]] const sim::TileQuery& tiles() const noexcept { return *tiles_; }
    [[nodiscard]] sim::PlayerBody& body() noexcept { return *body_; }
    [[nodiscard]] const sim::PlayerBody& body() const noexcept { return *body_; }
    /// Movement steps into the current simulated second, 0..kStepsPerSecond-1.
    /// THE RENDERER'S SLIDE: wardSprites() interpolates a ward body from
    /// prevX to x by this over kStepsPerSecond, and the 3D actor instancer
    /// (render3d/actor_instances.hpp) must use the identical fraction so a
    /// body stands in the same place on both paths. Read-only; nothing here
    /// is ever written back into the sim.
    [[nodiscard]] std::int32_t stepsThisSecond() const noexcept { return stepsThisSecond_; }
    /// 3D BUILD, V LANE. What the player's own hands are doing -- the
    /// viewmodel machine's pose, stepped once per movement step in step()
    /// off the combat sim's public getters and the three edges this session
    /// already sees (a swing released in attackUp, a cast thrown in
    /// castEquipped, a blow taken off the hp comparison). Render-only, never
    /// hashed, never written back; render3d/viewmodel.hpp turns it into the
    /// posed hands the 3D pass draws. See viewmodel_machine.hpp.
    [[nodiscard]] const ViewmodelPose& viewmodel() const noexcept { return viewmodel_.pose(); }
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

    /// TIME-AND-TENURE BUILD. The roll's plot index for the ground under the
    /// player's feet, or -1 where no compound claims it. The survey's own
    /// c1_..c4_ sign-footprint correlation (docks_signs.hpp's plotIdUnder)
    /// resolved against the live ward -- x/y only, any band, because a charge
    /// is a claim on the ground and everything standing on it. This is what
    /// gates the priest's ROLL topic, per the no-ownership-overlay ruling:
    /// tenure stays in conversation and signage, so the question "whose
    /// ground am I on" is asked of a priest, never of a HUD. (The PETITION
    /// topic is gated on the roll itself, not the feet -- see
    /// DialogueDirector::setVacantCharge.)
    [[nodiscard]] std::int32_t plotIndexUnderfoot() const noexcept;

    /// THE DISTRICT'S OWN PEOPLE. #78: the owner played the build and said
    /// "there were no people, even at night there should be people like guards
    /// urchins thieves taverns etc", and he was right -- the whole roll of
    /// bodies in the game was the Gilded Gull's seventeen. This is the ward's
    /// population, registered on the same engine, and it EXTENDS the taproom
    /// rather than competing with it: nobody here is spawned inside the Gull.
    [[nodiscard]] sim::WardPopulation& people() noexcept { return *people_; }
    [[nodiscard]] const sim::WardPopulation& people() const noexcept { return *people_; }
    /// The art the ward is drawn with.
    [[nodiscard]] const ActorSheet& actorSheet() const noexcept { return actorSheet_; }

    /// THE AUDIO WIRING PASS. Borrows (never owns) an AudioEngine and speaks
    /// to it from the event sites audio_engine.hpp's own plan names:
    /// footsteps in step(), panel open/close and cursor/confirm one-shots at
    /// the same places the EasedToggles and ImpactPulses already fire, the
    /// brawl's own blows, the harbour/interior bed off tavern().playerInside(),
    /// and a coin handle when the purse moves. Null (the default, and what
    /// every test and every --smoke capture keeps) makes every hook a no-op.
    ///
    /// STRICTLY ONE-WAY, per the determinism note in audio_engine.hpp: the
    /// hooks read already-public sim state and hand nothing back. Nothing
    /// audible is hashed and the sim cannot observe the engine at all --
    /// granadad-sim and the gate targets still link no audio.
    ///
    /// The caller keeps the engine alive for as long as the Session might
    /// step or toggle -- main.cpp detaches (setAudio(nullptr)) before its
    /// engine goes away.
    void setAudio(audio::AudioEngine* engine);

    /// CASE WATCH. Points the recorder at a tape (null detaches, the default
    /// every test and every ordinary run keeps -- the no-op-hook pattern
    /// setAudio states above). While attached, every top-level public verb
    /// this session is driven through appends one WatchOp; verbs a verb calls
    /// internally (interact()'s own examine(), a courier the step pump fires)
    /// are NOT recorded, because the replay's own call will make them again.
    void setWatchRecorder(std::vector<WatchOp>* tape) noexcept { watchTape_ = tape; }
    /// CASE WATCH. The scripted drive announcing "beat `index` plays now" /
    /// "beat `index` landed". Recorder-only marks: with no tape attached both
    /// are no-ops, and neither touches a byte of simulation either way.
    void watchChapter(std::int32_t index);
    void watchBeatLanded(std::int32_t index, bool ok);

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
    /// FAST TRAVEL (TRAVEL lane). The same jump, in whole seconds: advances
    /// the clock THROUGH THE WAIT MACHINERY -- Tavern::skipTo plus
    /// syncClockAfterSkip(), the identical pair skipToHour() spends -- so a
    /// travel and a wait are ONE time system, never two. skipToHour truncates
    /// to the top of an hour because its page prints hours; a walk is minutes,
    /// so this twin takes the seconds whole. Zero or less does nothing.
    void skipSeconds(int seconds);
    /// JUSTICE BUILD (SENTENCES LANE). Serves the judged hearing's sentence
    /// THROUGH THE SAME WAIT MACHINERY: Tavern::serveSentence (the coin, the
    /// cell and the yard as skipHours, the mirror, the temple, the body, then
    /// the ledger's record after the skip) plus syncClockAfterSkip(), so the
    /// ward's roll runs every day the sentence cost (Ward::advanceToDay --
    /// the ground penny falls due inside a sentence like inside a rented bed)
    /// and the district's people follow the clock. One time system, never
    /// two. The release that follows is the arrest's own (takeArrestRelease,
    /// read in step()); THE ROPE fires none and sets tavern().runEnd(). The
    /// page that calls this and stages what it returns is presentation's.
    const sim::Tavern::SentenceReport& serveSentence();

    // --- JUSTICE BUILD (HEARING PAGE LANE): the court, the rope, the end ----
    //
    // THE HEARING IS A PAGE, drawn into the Framebuffer like every other page
    // (render/hearing_page.hpp) over the hashed HearingState the crime ledger
    // keeps. TAKEN: the step that reads the arrest's release with a hearing
    // pending puts the body on the Mission's own arrival tile (the walk
    // elided, dressInstantCut's dip), says "TAKEN TO THE MISSION. HH:MM." and
    // opens the page -- modal, un-backable: the player cannot walk out of
    // custody (PLAY-MODE-SPEC's own rule; step() drops the movement input and
    // every world verb and page toggle is refused while inCustody()). The
    // plea is a stepped input through the page's own rows (armed on the
    // first press, confirmed on the second -- the QUIT pattern), the
    // weighing is shown in the check block, and the sentence row appears
    // after sim::kJudgmentHoldSteps. Serving it is Session::serveSentence
    // and the release that follows closes the page at the Mission's door
    // (SPARED, FINED) or on the Tarwalk. THE ROPE arms the rope ceremony:
    // the held black dip, the plate that never fades, then the end rows.
    //
    // ALL OF IT ON SESSION AND NOT IN THE CLIENT, for the reason the six
    // verbs are: the suite drives exactly the code a keypress drives
    // (routeCourtKey is the whole grammar, and main.cpp calls it first).

    /// The hearing page is up: taken with paper, not yet released.
    [[nodiscard]] bool courtOpen() const noexcept { return courtOpen_; }
    /// The officer's hand is on you, the page, the rope ceremony or the end
    /// rows own the frame: the body is the ward's, not the player's.
    [[nodiscard]] bool inCustody() const noexcept {
        return takenHold_ > 0 || courtOpen_ || ropeCeremonySteps_ > 0 || ropeRowsUp_;
    }

    /// THE ARREST, ON SCREEN (JUSTICE-SPEC 2.2, the two beats before the
    /// page). Taken at reach with paper, the player SEES it before the bench
    /// does anything: first the officer's own line -- watch.held / maimed /
    /// condemned, "There is paper out on you and I am the man holding it.
    /// Walk." -- said on the alert row IN THE ROOM, over the world, with his
    /// hand on you, held for kTakenOfficerSteps; then the cut, an instant
    /// black with one line on it in the rope plate's own register, "TAKEN TO
    /// THE MISSION. 23:00.", held for kTakenPlateSteps; and only then the
    /// body at the Mission's door and the page. Nothing is drawn under a page
    /// that hides the alert row: the line the spec promised is on the frame
    /// for as long as it takes to read. A record reopened between the arrest
    /// and the plea (a hearing pending with no release fired) skips both
    /// beats and opens the page at once.
    /// The officer's beat: his line on the row, the room still around you.
    [[nodiscard]] bool takenBeatUp() const noexcept { return takenHold_ > kTakenPlateSteps; }
    /// The plate: black, the one line, before the page.
    [[nodiscard]] bool takenPlateUp() const noexcept {
        return takenHold_ > 0 && takenHold_ <= kTakenPlateSteps;
    }
    /// "TAKEN TO THE MISSION. 23:00." -- what the plate reads, empty until it
    /// is up.
    [[nodiscard]] const std::string& takenPlate() const noexcept { return takenPlate_; }
    /// What the pause menu's WAIT row reads, and says when pressed, while in
    /// custody: "THE PRIEST IS WAITING." at the bench, "THE WATCH HAS YOU."
    /// under the officer's hand before it. Empty means nobody has you.
    [[nodiscard]] std::string custodyWaitLine() const;
    /// The plate over the finished frame: the black and the line, at full
    /// alpha for the whole hold. A no-op while it is not up. Public, like
    /// composeRopeCeremony, so a case can compose the expected frame itself
    /// and pin the rendered one against it.
    void composeTakenPlate(Framebuffer& target) const;
    /// Everything the page draws, off the ledger's record. Built here so a
    /// case can assert what the page SAYS without a framebuffer.
    [[nodiscard]] HearingPageState hearingPageState() const;
    [[nodiscard]] int courtCursor() const noexcept { return courtCursor_; }
    /// HEAR THE PAPER is open in the detail pane; 0 - BACK returns.
    [[nodiscard]] bool courtPaperOpen() const noexcept { return courtPaperOpen_; }
    /// A plea row was pressed once and waits for its second press.
    [[nodiscard]] bool courtPleaArmed() const noexcept { return courtArmed_ >= 0; }
    /// The judgment has landed and the hold has run: the one row that serves
    /// the sentence (or takes the drop) is on the list.
    [[nodiscard]] bool courtSentenceOffered() const noexcept;
    /// The rows as printed, "1 - I DID IT." first. Exposed for the suite.
    [[nodiscard]] std::vector<std::string> courtRows() const;
    void moveCourtCursor(int delta);
    /// ENTER / A: arms a plea, confirms an armed one, opens the paper, backs
    /// out of it, serves the sentence, takes the drop, picks an end row.
    void chooseCourtRow();
    /// The printed digit picks the row it prints. 0 is BACK while the paper
    /// is open (the digit it prints) and nothing otherwise.
    void chooseCourtVisibleRow(int slot);
    /// ESC / B: closes the paper, disarms an armed plea, and does NOTHING
    /// else -- the grammar exception (JUSTICE-SPEC 6.3): the hearing has no
    /// opener and no back.
    void courtBack();
    /// THE WHOLE PAGE GRAMMAR, keyboard and pad: arrows and the D-pad move,
    /// digits pick, ENTER/A confirm, ESC/B back (PadEast arrives already
    /// remapped to Escape by the client's pageBackRemap, and is also read
    /// raw here so a test can press it). Returns true when the page took the
    /// key -- and while in custody it takes EVERY key but Pause, so no world
    /// verb can reach the body from the bench.
    bool routeCourtKey(Key key);

    /// THE ROPE, STAGED: true from the drop until the run ends -- the dip,
    /// the held plate, then the end rows.
    [[nodiscard]] bool ropeCeremonyUp() const noexcept {
        return ropeCeremonySteps_ > 0 || ropeRowsUp_;
    }
    /// The plate's hold has run and the two rows are live under it.
    [[nodiscard]] bool ropeRowsUp() const noexcept { return ropeRowsUp_; }
    [[nodiscard]] int ropeCursor() const noexcept { return ropeCursor_; }
    /// The end row pressed once and waiting for its second press, or -1.
    /// THE QUIT PATTERN on the two rows that end a run: a leaned-on ENTER
    /// after the plate's hold cannot start a new man or leave the game; the
    /// armed row carries "-- SURE? <key>" on its tail (the pause card's own
    /// QUIT row), and ESC/B or moving the cursor disarms it. The armed row is
    /// the one state of the plate a player sees that is not the plate.
    [[nodiscard]] int ropeRowArmed() const noexcept { return ropeArmed_; }
    /// "1 - A NEW MAN", "2 - LEAVE" -- the armed one with its SURE tail. No
    /// save row: no save exists.
    [[nodiscard]] std::vector<std::string> ropeRows() const;
    /// The plate's three lines: "HANGED AT THE SALTGATE POST." / "BY THE
    /// WARD. FOR CANNIC." / "THE FOURTH DAY. 23:52."
    [[nodiscard]] const std::string& ropePlateTop() const noexcept { return ropePlateTop_; }
    [[nodiscard]] const std::string& ropePlateMid() const noexcept { return ropePlateMid_; }
    [[nodiscard]] const std::string& ropePlateFoot() const noexcept { return ropePlateFoot_; }
    /// The veil, the plate and (after the hold) the rows, over the finished
    /// frame -- composeDeathCeremony's primitives with the fade-out removed.
    /// The veil holds. A no-op while nothing is armed.
    void composeRopeCeremony(Framebuffer& target) const;
    void moveRopeCursor(int delta);
    void chooseRopeRow();
    void chooseRopeVisibleRow(int slot);

    /// THE END, CHOSEN. A NEW MAN routes to the creation window for a fresh
    /// run (main.cpp's loop reads this beside quitRequested); LEAVE is the
    /// shipped quit. Never set by anything but the end rows.
    enum class RunEndChoice : std::uint8_t { None = 0, NewMan = 1, Leave = 2 };
    [[nodiscard]] bool runEnded() const noexcept { return runEnded_; }
    [[nodiscard]] RunEndChoice runEndReason() const noexcept { return runEndReason_; }

    /// The camera the body is currently looking through.
    [[nodiscard]] Camera camera() const noexcept;

    /// 3D BUILD. Which passes drawFrame runs. `world` off skips the software
    /// world pass (the sky, the tiles, the sprites) and clears the target to
    /// transparent first, so everything drawn after it -- signage, washes,
    /// the HUD, every page -- lands on an OVERLAY the raylib backend
    /// composites over its own 3D frame. On by default: every test and the
    /// software capture path draw the frame they always drew.
    struct FramePasses {
        bool world = true;
    };

    /// Draws the world, everybody in it, and the HUD into `target`.
    FrameStats drawFrame(Framebuffer& target) const { return drawFrame(target, FramePasses{}); }
    FrameStats drawFrame(Framebuffer& target, FramePasses passes) const;

    /// Where the player is, in words the player would use. Derived from x, y
    /// AND the band — see sim::docks::kPlaces for why that is worth saying.
    [[nodiscard]] std::string placeLabel() const;

    // --- the three verbs ----------------------------------------------------
    //
    // On Session and not in the client, so the test suite drives exactly the
    // code a keypress does. The client binds Attack, Interact and Vertical to
    // these and owns no game logic of its own.

    /// #85. ONE BUTTON, RESOLVED BY STANCE AND BY WHAT IS FACED. Folds
    /// Interact + Examine + Steal + Lift + Rest into a single verb, per
    /// Eli's own brief: "a button to 'interact (pickpocket if sneaking)'".
    ///
    /// THE RESOLUTION ORDER, walked every press:
    ///
    ///   1. Already talking: picks the topic under the cursor (unchanged
    ///      from before the consolidation -- a conversation already owns
    ///      the keyboard, and Interact confirming its own list is the same
    ///      "a verb reused contextually while a mode is active" pattern
    ///      lockpicking already used).
    ///   2. NOT SNEAKING + AT YOUR OWN RENTED BED = REST. Checked first,
    ///      not because it is the most likely case but because it is the
    ///      one exact-tile trigger nothing else could also mean.
    ///   3. PERSON IN REACH = TALK (not sneaking) or PICKPOCKET (sneaking).
    ///   4. ITEM/FIXTURE = the same chain steal() always tried: the box (or
    ///      its lock -- crackStrongbox() does not read stance, so "facing a
    ///      lock picks it, sneaking or not" is already true with no branch
    ///      here), the bale, the rat, the wire that buys more picks.
    ///   5. HANDS UP AND NOTHING IN REACH = LOWER HANDS (STANCE & ROOM
    ///      BUILD, oblivion-roadmap.md 3.2 lower rule 1): after the person,
    ///      the fixture and a lead the book has heard of, before the
    ///      never-refusing look. Tavern::lowerPlayerHands(); the reticle says
    ///      LOWER HANDS before the press through lowerHandsResolves().
    ///   6. NOTHING RESOLVED: the investigation look (examine()), which
    ///      never refuses.
    ///
    /// interactPrompt() BELOW WALKS THE IDENTICAL ORDER on read-only queries,
    /// so the HUD can show the verb THIS press is about to run before the
    /// player commits to it -- Eli's own brief: "a static INTERACT label
    /// defeats the whole point; the player must SEE what pressing it will
    /// do before they press it." If this order ever changes, that method's
    /// order has to change with it or the HUD starts lying; see its own
    /// header for the one place they are allowed to (documented) disagree.
    ///
    /// S3's original note on why this key opens topics rather than a single
    /// greeting is still true and still the shape talk resolves into.
    void interact();
    /// #85. THE LIVE LABEL Interact is about to resolve to -- "TALK",
    /// "PICKPOCKET", "PICK LOCK", "TAKE", "TAKE QUIETLY", "REST", "LOOK", or
    /// empty while a page already owns the keyboard (talking, picking,
    /// paused, or Menu's options page is listening for a key). CONST and
    /// read-only by construction: every query it makes (Tavern::nearestTo,
    /// WardPopulation::nearestTo, Tavern::rentedRoom/crackedBoxes/
    /// openedLocks, sim::gull::roomAtStand) is a lookup, never a mutation,
    /// which is what lets drawFrame() call this every frame with no side
    /// effect on the world it is describing.
    ///
    /// VERIFICATION GAP (#85): THE BALE, THE RAT AND buyPicks() ARE NOT
    /// PREVIEWED. Those three live behind handleBale()/takeScalp()/
    /// buyPicks(), which -- like crackStrongbox() -- MUTATE the room the
    /// moment they are asked (a bale count decrements, a rat is marked
    /// skinned), so there is no read-only query to peek through without
    /// re-deriving their geometry a second time for a label alone. Standing
    /// at the bale or a downed rat still resolves CORRECTLY when the button
    /// is actually pressed -- interact() tries them for real -- the HUD
    /// prompt just falls back to "LOOK" there instead of naming them in
    /// advance. The two scenarios the brief's own acceptance capture is
    /// built around -- a person (TALK/PICKPOCKET) and a lock ("facing a
    /// lock=pick it") -- are both exact.
    [[nodiscard]] std::string interactPrompt() const;
    /// THE CROSSHAIR PASS. THE SAME WALK, ANSWERING WHAT IT IS ABOUT TO ACT
    /// ON as well as what it is about to do.
    ///
    /// The owner's note: "It needs to be improved to be properly contextual."
    /// #85 already resolved the VERB exactly -- TALK/PICKPOCKET/PICK LOCK --
    /// and that is half of contextual. A bare verb still cannot tell a player
    /// which of three bodies in a doorway is the one in reach, or that the
    /// box under the crosshair is their own, or that the thing they are about
    /// to LOOK at is the lead the casebook has been asking them to find. So
    /// the walk now also names its object, in the ward's own words, off the
    /// same read-only queries the verb came from.
    ///
    /// interactPrompt() ABOVE IS THIS METHOD'S `verb` AND NOTHING ELSE, so
    /// there is exactly one description of the resolution order in this file
    /// and the HUD cannot be shown a verb the key would not run.
    ///
    /// STILL CONST AND STILL READ-ONLY. The one query added is the casebook's
    /// -- and it is a walk over Lead::site and Casebook::state(), NOT a call
    /// to look(), which mutates the book by construction. Naming a lead must
    /// not read it.
    struct InteractTarget {
        /// "TALK", "PICK LOCK", "LOOK" ... or empty while a page owns the
        /// keyboard, exactly as interactPrompt() has always answered.
        std::string verb;
        /// "GERTA SALTCOTTE", "THE MISSION", "THE GILDED GULL", or empty when
        /// nothing in reach has a name worth printing.
        std::string subject;
        /// The qualifier: a trade, a state, what the lead is for. May be empty
        /// with a subject present.
        std::string note;
        /// Which accent the reticle and the subject take. See AimKind.
        AimKind kind = AimKind::Nothing;
    };
    [[nodiscard]] InteractTarget interactTarget() const;
    /// The raw walk, before interactTarget() tidies what it answered. Split
    /// out for one reason and it is a real one: the walk has eight exits and a
    /// rule that applies to all eight (the note must not repeat the subject --
    /// see saysTheSame in session.cpp) written at each of them is eight places
    /// for the ninth exit to forget it. Nothing outside interactTarget() has
    /// any business calling this.
    [[nodiscard]] InteractTarget resolveInteract() const;
    /// STANCE & ROOM BUILD. THE LOWER HANDS SLOT'S ONE PREDICATE, shared by
    /// the act walk (interact()) and the prompt walk (resolveInteract()) so
    /// the reticle cannot say LOWER HANDS on a press that would LOOK, or the
    /// reverse: true exactly when the hands are up and no lead the book has
    /// heard of stands under the crosshair (a named lead is something in
    /// reach, and the look outranks the lower). It is the SLOT'S predicate,
    /// asked by both walks only after the person and the fixture slots ahead
    /// of it have not resolved -- it does not re-ask those. Read-only.
    [[nodiscard]] bool lowerHandsResolves() const;
    /// #85. Was Jump + Traverse + DropDown. ONE BUTTON, RESOLVED BY WHAT IS
    /// DIRECTLY AHEAD OR BELOW: climb (mantle, or the leap it falls back to)
    /// first, a drop if there is a ledge to step off, an ordinary standing
    /// jump when neither is there -- see tryClimb()/tryDropDown() below,
    /// which climb() and dropDown() also use so the three public verbs and
    /// this one can never resolve a press differently.
    ///
    /// THE MANTLE HALF OF "CLIMB" ALMOST NEVER FIRES FROM A KEYPRESS AT ALL.
    /// MoveInput::autoTraverse (on by default for the player) already hauls
    /// the body over a ledge it walks into, every ordinary step -- "you get
    /// onto things by trying to go there, not by learning a verb key" is
    /// player.hpp's own note on that flag. What a standing body still needs
    /// a button for is the LEAP, which is climb()'s documented fallback.
    void vertical();
    /// F. Throws a punch. In a taproom that is an offence, and the house has
    /// opinions about it.
    ///
    /// ACTION-COMBAT BUILD: now a TAP of the new Attack verbs -- attackDown()
    /// then an immediate attackUp() with no charge behind it -- so the swing
    /// runs through the same sightline/lethal path a mouse press does, and the
    /// six existing call sites (the scripted drives, case_watch, the render
    /// suite) drive real combat rather than the retired legacy tap. A tap
    /// never reaches the hard tier, so it is always a Subdue swing: it can down
    /// a man but the intent-by-verb rule never lets a tap alone turn a bar
    /// fight lethal.
    void punch();
    /// Attack DOWN-EDGE. Starts the sim's hold clock (Tavern::playerAttackDown)
    /// -- the charge is measured in movement steps by the room, so the tier is
    /// deterministic. Overlays are dismissed the same way punch() dismisses
    /// them. INPUT calls this on the Attack key's down-edge; a page-consumed
    /// press must be guarded out on the client's side (the attack-armed
    /// tracker), the same shape the QuickWheel self-guards.
    void attackDown();
    /// Attack RELEASE-EDGE. Resolves the swing through Tavern::playerAttackUp()
    /// -- hard iff the hold reached kHardSwingHoldSteps -- and speaks the diet's
    /// surviving lines (downs, a crowning, a kill, the refusals), never the
    /// per-blow HIT/MISSED log the say-row diet retired. Fires the connecting
    /// wash and the by-band swing audio. A release with no charge behind it
    /// (an edge that arrived in recovery or idle) says nothing and does nothing.
    void attackUp();
    /// C. Casts the equipped crafting -- sim::Tavern::playerCastEquipped() is
    /// the whole resolution including every refusal, and every outcome is
    /// said on the alert row, because a key that can silently do nothing is a
    /// key the player reads as broken.
    void castEquipped();
    /// RMB, HELD. The client pushes the edge (down is blocking) and step()
    /// pushes the effective state into the room every step, gated inert while
    /// talking()/picking() the same way movement keys are -- a guard raised
    /// inside a menu would be a fact about the world nobody could see being
    /// made.
    void setBlocking(bool held);
    /// FATIGUE BUILD, the boot seam chargen's attribute pool finally lands
    /// through: hands the sheet to the room (Tavern::setPlayerAttributes --
    /// which sizes and fills the fatigue pool) and sets the legs' own AGI
    /// multiplier on the body (PlayerBody::setSpeedScaleQ8), the one reader
    /// that cannot live in the room because the room does not own the legs.
    /// One call, both halves, so no caller can apply half a sheet.
    void applyPlayerAttributes(const sim::AttributeBlock& attributes);
    /// R. Sleeps, if there is a rented room and you are standing in it.
    ///
    /// NOT BOUND TO A KEY OF ITS OWN ANY MORE -- #85 folded this into
    /// interact()'s own resolution -- but kept public and unchanged for the
    /// suite, and for interact()'s own use through settleSleep() (private,
    /// below) for the shared "you slept" half of both call sites.
    void restHere();

    // --- S5: the three roof verbs, and the one thieving one ------------------
    //
    // On Session for exactly the reason the first three are: the suite drives
    // the code a keypress drives, and the client owns no game logic.

    /// SPACE. Gets you UP. Tries the mantle first -- a wall in front of you
    /// with a surface on top of it -- and a leap second, because those are the
    /// two answers to "get me across or over" and a player pressing one key
    /// should not have to know which of them the geometry wants.
    ///
    /// NOT BOUND TO A KEY OF ITS OWN ANY MORE -- #85 folded this into
    /// vertical()'s resolution, through the shared private tryClimb(). Kept
    /// public and unchanged for the suite.
    void climb();
    /// X. Steps off the ledge in front and takes the fall.
    ///
    /// NOT BOUND TO A KEY OF ITS OWN ANY MORE -- see climb()'s identical note.
    void dropDown();
    /// G. Puts hands on whatever is here: the strongbox at a bed-foot, or the
    /// bale in the snug.
    ///
    /// S9: and the wire goes in first. A box whose lock is still shut opens the
    /// lockpicking surface rather than refusing; the box itself is emptied by
    /// the same key once the lock gives.
    ///
    /// NOT BOUND TO A KEY OF ITS OWN ANY MORE -- #85 folded this into
    /// interact()'s item/fixture branch, through the shared private
    /// stealNearestThing(). Kept public and unchanged for the suite.
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
    ///
    /// NOT BOUND TO A KEY OF ITS OWN ANY MORE -- #85 folded this into
    /// interact()'s sneaking + person branch. Kept public and unchanged for
    /// the suite.
    void lift();

    // --- S10: the investigation ----------------------------------------------
    //
    // On Session for the reason all eleven verbs before them are: the suite
    // drives the code a keypress drives, and the client owns no game logic.

    /// Q. LOOK AT WHAT IS HERE. The investigation verb, and the only one there
    /// is: the gazetteer's design law (section 5.3) is that the gate is knowing
    /// WHERE to ask, never persuasion, so there is no check to pass and no roll
    /// to lose. Standing at a lead you have been told about gives you what is
    /// there; standing at one nobody has pointed you at gives you a corner of
    /// the ward.
    void examine();
    /// The player's own notes, and the ward's nerve.
    [[nodiscard]] sim::Casebook& casebook() noexcept { return casebook_; }
    [[nodiscard]] const sim::Casebook& casebook() const noexcept { return casebook_; }
    /// COURIER CASE. The second book and its raws -- THE QUIET TENANT, the
    /// errand the mission sheet opens. A separate pair rather than a vector
    /// of cases on purpose: two authored cases are two members in fixed
    /// declaration order, which is a deterministic order nobody had to
    /// invent a container for. Empty (no leads heard) until the courier
    /// beat fires; see SessionConfig::courier.
    [[nodiscard]] sim::Casebook& sheetBook() noexcept { return sheetBook_; }
    [[nodiscard]] const sim::Casebook& sheetBook() const noexcept { return sheetBook_; }
    [[nodiscard]] const sim::CasebookRaws& sheetRaws() const noexcept { return sheetRaws_; }
    /// True while the errand is the one in front: at least one of its leads
    /// heard and its close not yet read. This is the WHOLE case-switching
    /// rule -- the casebook page, the Journal tile, the HUD's objective row
    /// and the map's lead rows all show the live errand while it runs and
    /// the Bloodletter otherwise. No manual switcher in this pass, and that
    /// is a flagged scope cut, not an oversight.
    [[nodiscard]] bool sheetCaseLive() const noexcept {
        return sheetBook_.active() && !sheetBook_.known().empty() && !sheetBook_.closed();
    }
    /// Where the courier beat has got to: 0 not fired, 1 hailed (sheet in
    /// hand, lead heard), 2 the follow-up line said. See stepSheetCase().
    [[nodiscard]] int courierStage() const noexcept { return courierStage_; }
    /// True from the moment Finch is taken up until the back room takes him.
    [[nodiscard]] bool sheetCarry() const noexcept { return sheetCarry_; }
    /// True while the quiet tenant is on the boards and can be taken up -- what
    /// the scripted line and its twin-run test watch for. Public because the
    /// --case line is a free function; the private sheetQuarry() is its
    /// engine.
    [[nodiscard]] bool tenantDown() const { return sheetQuarry() != nullptr; }
    /// COURIER CASE. Fire the courier beat NOW, countdown skipped -- what the
    /// --case scripted line uses so a headless run does not walk in place for
    /// six seconds. A no-op once the beat has fired; requires the raws loaded.
    void courierDeliverNow();
    /// EVICTION CASE. The third book and its raws -- EVICTION, the writ
    /// Maell's own conversation opens (TopicKind::TakeWrit; no courier, no
    /// start lead: the hire IS the delivery). A third fixed member pair, the
    /// courier chassis's own "two authored cases are two members" rule grown
    /// by exactly one -- see the flagged note on activeCaseRaws() below.
    [[nodiscard]] sim::Casebook& evictBook() noexcept { return evictBook_; }
    [[nodiscard]] const sim::Casebook& evictBook() const noexcept { return evictBook_; }
    [[nodiscard]] const sim::CasebookRaws& evictRaws() const noexcept { return evictRaws_; }
    /// True while the writ is the errand in front: at least one lead heard,
    /// close not read -- sheetCaseLive()'s exact rule over the third book.
    [[nodiscard]] bool evictCaseLive() const noexcept {
        return evictBook_.active() && !evictBook_.known().empty() && !evictBook_.closed();
    }
    /// True from the moment the paper changes hands at the Marrow door until
    /// the case closes at the Mission -- the eviction's sheetCarry_: what the
    /// HUD's objective swings on and what stepEvictCase() completes.
    [[nodiscard]] bool writServed() const noexcept { return writServed_; }
    /// True once the door has answered tonight's knock -- the serve press is
    /// only offered on an answered door, so the choice is always spoken
    /// before it is spent.
    [[nodiscard]] bool evictKnocked() const noexcept { return evictKnocked_; }
    /// True once the door has EVER answered -- the fact the record keeps when
    /// the live flag re-arms as the player steps off the step. The disrupt
    /// path's whole ledger entry ("the family had an evening's warning") reads
    /// this, not the door's own state machine.
    [[nodiscard]] bool evictEverKnocked() const noexcept { return evictEverKnocked_; }
    /// What the counters add up to. Derived on every call and held by nobody --
    /// see legend.hpp on why that is the design and not a shortcut.
    [[nodiscard]] sim::Legend legend() const;

    /// MORROWIND ROUND. Opens and closes the tiled Menu, focused on the
    /// Journal tile -- see the "ONE MENU, FOUR TILES" block below for what
    /// the tiled Menu actually is now. Kept as its own named entry point
    /// (rather than folded away once #85's per-page toggles stopped being
    /// individually key-bound) because the test suite and every scripted
    /// capture still open pages by name, and "open the Menu with the Journal
    /// focused" is a real, distinct thing to ask for.
    ///
    /// IT IS DRAWN IN THE CONVERSATION SURFACE'S OWN VOCABULARY, but the tiled
    /// Menu as a WHOLE deliberately does NOT hug the HUD's centre-clear rule
    /// the way a live conversation still does -- see menu_view.hpp's own
    /// header on why a Morrowind-style overview is allowed to cover the
    /// middle of the screen and a conversation is not.
    void toggleCasebook();
    /// True while the tiled Menu is open, REGARDLESS of which of its four
    /// tiles currently has focus -- see characterOpen()/mapOpen()/
    /// lettersOpen() below, which are the identical bool. The four tiles are
    /// drawn SIMULTANEOUSLY now (Character top-left, Map top-centre, Letters
    /// top-right, Journal full-width along the bottom, per Eli's own brief:
    /// "just show them like Morrowind does"), so there is no longer a
    /// meaningful sense in which one tile is "open" and the others are not --
    /// menuFocus() is the only thing that still distinguishes them.
    [[nodiscard]] bool casebookOpen() const noexcept { return casebookOpen_; }
    // --- THE CASEBOOK PASS: the book as the master/detail frame ---------------
    //
    // THE DEFECT THIS ANSWERS is the owner's own playthrough: he followed the
    // Bloodletter to Crell at the Weighhouse and it "seemed to stop there". It
    // did not -- `weighhouse-ledger` opens three leads and the simulation opened
    // all three -- but the only thing on the frame that said so was a dim grey
    // corner row changing from CASE 4/6 to CASE 4/9. See casebook_page.hpp.
    //
    // The book is DRAWN as a composed page (render::drawCasebookPage) whenever
    // the tiled Menu is focused on the Journal tile, which is the tile it opens
    // on and the one the Menu key reaches. The other three tiles are unchanged
    // and still drawn by menu_view.hpp.
    //
    // ALL OF IT IS PURE RENDER STATE. Nothing below reaches PhasedEngine and
    // nothing below is hashed -- commitCasebookLead() is the one method that
    // can change the world, and the only way it does is by calling examine(),
    // which is the same call the look key already makes.

    /// True while the composed casebook page is what the Menu is drawing.
    [[nodiscard]] bool casebookPageOpen() const noexcept {
        return casebookOpen_ && menuFocus_ == kMenuFocusJournal;
    }
    /// Which view is over the selection: the lead, or the case.
    [[nodiscard]] CasebookTab casebookTab() const noexcept { return casebookTab_; }
    /// Which lead the page's cursor is on -- an index into Casebook::known(),
    /// which is the same order the page lists.
    ///
    /// ITS OWN FIELD, NOT the Journal TILE's caseCursor_. The tile's list is
    /// leads PLUS the contract and log rows under them (journalWorkRows), and
    /// two lists of different lengths sharing one cursor is how a cursor ends
    /// up selecting a row nothing draws.
    [[nodiscard]] int casebookLeadCursor() const noexcept { return casePageCursor_; }
    /// UP/DOWN. Wraps, so holding one direction walks the whole book.
    void moveCasebookCursor(int delta);
    /// A printed digit, or a mouse click. Clamped; out of range does nothing.
    void setCasebookCursor(int index);
    /// LEFT/RIGHT. Wraps between the two views. See casebook_page.hpp on why
    /// the views are not on a printed hotkey.
    void cycleCasebookTab(int delta);
    /// Puts the cursor on a lead by its casebook.json id. False when no lead of
    /// that id is in the book -- which is what a capture flag with a typo in it
    /// should do rather than silently photograph a different lead.
    [[nodiscard]] bool selectCasebookLead(std::string_view leadId);
    /// ENTER, the commit verb at the foot of the detail pane. STATE CHOOSES:
    /// standing in reach of an unread lead it LOOKS (the same examine() the
    /// look key calls); anywhere else it routes to the ward map.
    void commitCasebookLead();
    /// THE ROUTE. Closes the book, opens the WARD MAP and puts the map's own
    /// cursor on the lead's place -- handing off to the map pass's selection
    /// machinery rather than building a second way to point at a building.
    /// False when the plan has no named place for that lead.
    bool showLeadOnMap(std::int32_t leadIndex);
    /// Which named place on the ward map a lead belongs to, or -1. Public
    /// because a case asserts every authored lead resolves to one -- a lead the
    /// map cannot point at is a lead the player cannot be sent to.
    [[nodiscard]] int mapPlaceForLead(std::int32_t leadIndex) const;
    /// The whole page, ready to draw. Public for keysPageState()'s own reason:
    /// a case reads it instead of a screenshot.
    [[nodiscard]] CasebookPageState casebookPageState() const;

    // --- THE MOMENT LEADS OPEN ------------------------------------------------
    //
    // A NOTICE, NOT A NAG. It fires on the RISING EDGE of a look that opened
    // something -- Session::examine() is the only place that arms it -- holds
    // for a couple of seconds and is gone. Nothing re-arms it, nothing queues
    // it, and every stand-down rule the threshold plate keeps it keeps too
    // (see syncPanelAnim): a page owning the keyboard suppresses it outright
    // rather than storing it up to pop when the page closes.
    //
    // It borrows the threshold plate's EXACT idiom -- HudState::casePlate, one
    // line, rising through its resting place under the compass ribbon -- and
    // outranks it, because a lead opening is rarer and is the thing this
    // programme exists to make visible. Only one of the two is ever on a frame.

    /// What the plate is announcing, or empty. Held through the fade so the
    /// notice finishes with its own words on it.
    [[nodiscard]] std::string_view casePlateLabel() const noexcept {
        return std::string_view{casePlateText_};
    }
    /// True while it is WANTED -- the couple of seconds after a look that
    /// opened something. The drawn alpha is its EasedToggle's business; this is
    /// the target a case asserts on.
    [[nodiscard]] bool casePlateWanted() const noexcept { return casePlateShowSteps_ > 0; }

    /// "CASE 4/12  COLD 1  THE WARD IS TALKING", or empty before the trail
    /// starts. Bottom-left, one row, on the edge.
    [[nodiscard]] std::string caseLine() const;
    /// "LEGEND CUTPURSE  6 RUNGS", or empty at rung zero across the board.
    [[nodiscard]] std::string legendLine() const;

    /// TASK #82, MORROWIND ROUND. Opens and closes the tiled Menu, focused on
    /// the Letters tile -- see toggleCasebook()'s own note on why this stays
    /// a named entry point. content/raws/quests/bloodletter_letters.json is
    /// the authored file (sim/letters.hpp); unlockedLetters() decides which
    /// of it the player has actually earned, off the casebook lead each one
    /// is tied to -- nothing here has a "read" flag of its own to fall out of
    /// sync with the book that gates it.
    void toggleLetters();
    /// True while the tiled Menu is open -- the same bool casebookOpen() is;
    /// see that accessor's own note on why the four tiles no longer have
    /// four different answers to "is this one open".
    [[nodiscard]] bool lettersOpen() const noexcept { return casebookOpen_; }

    /// RELOCATED TO PAUSE'S OWN CONTROLS ROW. The keys page itself, and
    /// dialogueView()'s keysOpen_ branch, are UNCHANGED -- still the same
    /// single-panel conversation-surface widget, still respecting the HUD's
    /// centre-clear rule, exactly as it always has. What moved is only how a
    /// player REACHES it: it used to be one stop of Menu's own six-page
    /// cycle (#85); the Morrowind round's tiled Menu has no "pages" left to
    /// cycle through (its four tiles are all on screen at once), so Keys
    /// followed Options' own precedent and took a numbered row on Pause
    /// instead -- see pauseRows()'s own header.
    void toggleKeys();
    [[nodiscard]] bool keysOpen() const noexcept { return keysOpen_; }

    // --- ONE MENU, FOUR TILES (Morrowind round) -------------------------------
    //
    // Eli, having seen a real Morrowind screenshot: "I like how we've got a UI
    // that's nice and chunky like from the 90s, BUT even Daggerfall knew when
    // to scale it back to fit more words on the screen. Fix the multi-page
    // menu thing, just show them like Morrowind does." Morrowind's own layout:
    // three roughly-square panels across the top (Stats, Map, Magic) plus one
    // full-width panel along the bottom (Inventory), all visible at once, no
    // paging between them.
    //
    // #85's SIX-PAGE CYCLE IS GONE. Journal, Character, Map and Letters are
    // no longer four of six pages a bumper flips between one at a time --
    // they are FOUR TILES drawn every frame the Menu is open, by
    // menu_view.hpp's drawMenuTiles(): Character top-left, Map top-centre,
    // Letters top-right, Journal full-width along the bottom. casebookOpen_
    // is the one flag that opens and closes all four together now, which is
    // why casebookOpen()/characterOpen()/mapOpen()/lettersOpen() all read the
    // identical bool -- see casebookOpen()'s own note.
    //
    // KEYS AND OPTIONS MOVED TO PAUSE. Neither fits a tile (both are long,
    // single lists meant to be read top to bottom, not a quarter-screen
    // panel), and Options already had a Pause-side door (SETTINGS) before
    // this round -- see controls.hpp's own note on why that is one system
    // reached two ways, never two. Keys gained the identical kind of door
    // (CONTROLS) so the pattern is uniform: pauseRows()'s own header.
    //
    // PAGEPREV/PAGENEXT ARE REPURPOSED, NOT REBOUND. With all four tiles
    // visible at once, "which page is showing" no longer exists, so these two
    // actions now step WHICH TILE HAS FOCUS instead -- menuFocus() below --
    // the same two buttons, the same bindings (see controls.hpp, untouched),
    // a different job. Arrow keys, the printed numbers and ENTER all continue
    // to act on whichever tile currently has focus; see moveTopicCursor()/
    // chooseVisibleTopic()/chooseTopic(), which now dispatch on menuFocus_
    // instead of on which of four now-identical bools happened to be true.

    /// True while the tiled Menu, the keys page or the options page is open --
    /// the one predicate this build's "is a page eating the keyboard" checks
    /// (main.cpp's `listening`, among others -- see its own comment) fold
    /// into instead of each hand-listing the same three flags.
    [[nodiscard]] bool menuOpen() const noexcept;
    /// Opens the tiled Menu (focused on the Journal tile) if nothing is open;
    /// closes it otherwise -- the same "the key that opened it closes it"
    /// rule Keys, Options and Pause each still have on their own.
    void toggleMenu();
    /// NINE AND THE STICKS: THE RING. Steps one page forward through your
    /// papers -- the four tiles (Character -> Map -> Letters -> Journal),
    /// then the WARD MAP, then the GRIMOIRE, then round to Character. On a
    /// pad the ward map and the grimoire are reachable by NO button of their
    /// own (SELECT is WAIT, the QuickWheel is cut), so this ring is how a
    /// controller reaches them: NOTES, then the bumpers -- Oblivion's own
    /// tabbed menu, where the map is a tab. Does nothing while none of the
    /// three surfaces is open (a bumper with nothing open opens nothing) and
    /// nothing from Keys/Options/Pause, which are not pages of NOTES.
    void menuPageNext();
    /// The same ring, backward.
    void menuPagePrev();
    /// WAIT, the verb: opens the hour-select page (openWait(false)) or closes
    /// it if it is up -- T and SELECT's own toggle; the pause menu's WAIT row
    /// is the same door.
    void toggleWait();
    /// One turn of the paper -- the BookFlip every step of the ring and every
    /// tile focus change speaks, in one place.
    void pageTurnSound();
    /// THE POINTER PASS: focus by NAME rather than by cycling -- a hover or a
    /// click landing on a tile, setCasebookCursor's "a printed digit, or a
    /// mouse click" shape. One BookFlip when the focus actually moves; a
    /// no-op while the tiled Menu is not open, out of range, or already
    /// there.
    void setMenuFocus(int focus);
    /// Which of the tiled Menu's four tiles currently has input focus --
    /// kMenuFocusCharacter/Map/Letters/Journal (menu_view.hpp). Arrow keys,
    /// the printed numbers and ENTER all act on this one; the other three
    /// keep drawing whatever they last showed, unread but not reset, exactly
    /// the way Morrowind's own four panes hold their own scroll position
    /// while only one has the keyboard.
    [[nodiscard]] int menuFocus() const noexcept { return menuFocus_; }
    /// PLANNING SPRINT (item #1). 0 (unfocused) .. 1 (focused) -- the exact
    /// value INNOVATION SPRINT ITEM #2's own characterFocusAnim_ etc. are
    /// sitting at right now. Exposed so a scripted `--refocus` capture (see
    /// SmokeRunConfig::refocus below) can report the NUMBER a mid-crossfade
    /// screenshot is a picture of, not just the picture -- a PNG proves a
    /// border is some shade of the accent colour; this proves which shade.
    [[nodiscard]] float characterFocusValue() const noexcept { return characterFocusAnim_.value(); }
    [[nodiscard]] float mapFocusValue() const noexcept { return mapFocusAnim_.value(); }
    [[nodiscard]] float lettersFocusValue() const noexcept { return lettersFocusAnim_.value(); }
    [[nodiscard]] float journalFocusValue() const noexcept { return journalFocusAnim_.value(); }

    // --- #77: the controls, and the page that changes them -------------------

    /// The live bindings and preferences. The keys page is DRAWN FROM THESE, so
    /// a rebinding shows up on the reference page by construction and the two
    /// cannot drift -- which is the failure the old page had by being a static
    /// array of strings beside a switch statement in the client.
    [[nodiscard]] const ControlSettings& controls() const noexcept { return controls_; }
    void setControls(const ControlSettings& settings);

    // --- ship note move 3: the prompts name the device holding them ---------

    /// The device whose vocabulary every prompt speaks this frame -- the
    /// most recently active one. "Active" means A PRESS: the last keyboard
    /// key, mouse button, wheel notch or pad button the client translated
    /// into a Key, with mouse and keyboard counting as one device (see
    /// InputDevice). Analogue motion deliberately does not count -- a
    /// nudged mouse or a stick resting a hair past its deadzone would flap
    /// every label on screen, and a discrete press is an unambiguous claim.
    ///
    /// CLIENT STATE, NEVER SIM STATE. Read at draw time by every prompt
    /// assembly site, so the labels switch live the moment the other hand
    /// speaks -- no menu visit, no reopen.
    [[nodiscard]] InputDevice promptDevice() const noexcept { return promptDevice_; }
    /// The client's note that a device spoke. Cheap and idempotent; called
    /// from main.cpp's event loop on every translated press.
    void noteInputDevice(InputDevice device);
    /// Convenience: classify `key` and note its device. Key::None is nobody
    /// and is ignored rather than counted as the keyboard.
    void noteInputKey(Key key);

    /// UI-EA-SPEC sec. 2, cross-lane contract (c): THE TUTOR WAKE EDGE.
    /// FLOW's half of the tutor-band contract -- the client calls this on
    /// the two wake events only IT can see: the prompt device changing
    /// hands, and an unrecognized press (a key that resolved to no action
    /// and no page verb -- the player asking a question the game did not
    /// answer, which is the request for help). HUD's countdown/toggle
    /// helper re-raises every tutor band whenever this serial moves; a
    /// serial rather than a countdown so the signal composes with however
    /// many bands exist without this side knowing their holds. CLIENT
    /// state, promptDevice_'s own contract: never hashed, never fed to
    /// MoveInput.
    void noteTutorWake() noexcept { ++tutorWakeSerial_; }
    [[nodiscard]] std::uint32_t tutorWakeSerial() const noexcept { return tutorWakeSerial_; }

    /// UI-EA-SPEC sec. 3 rule 5, cross-lane contract (b): THE COMMIT BEAT.
    /// The client arms this at commit routing -- the press that fires FACE
    /// IT, TRAVEL, GO TO IT, REBIND, a wait or grimoire row, the
    /// quit-confirm -- and the page drawers read the value back for one
    /// restrained flash on the inverted fill (ImpactPulse's own decay,
    /// kPageEaseSteps). One pulse for all pages -- at most one page owns
    /// the input, so at most one commit lands per press. Render-side flow
    /// state: never hashed, never fed to MoveInput.
    void armCommitPulse() noexcept { commitPulse_.trigger(); }
    [[nodiscard]] float commitPulseValue() const noexcept { return commitPulse_.value(); }

    /// THE CONTROLS PAGE, as the terminal-panel surface actually draws it:
    /// binding, verb, second binding, one sentence of help, and which family
    /// the row belongs to. Built from the live bindings, so a rebinding shows
    /// up here by construction. See render/keys_page.hpp.
    [[nodiscard]] std::vector<KeysPageRow> keyPageRows() const;

    /// The whole page, ready to draw: the rows above plus the title, the build
    /// readout, the instruction and the cursor.
    [[nodiscard]] KeysPageState keysPageState() const;

    /// The keys page as a flat list of one-line strings -- what the old
    /// topic-grid layout read, and what two cases still assert against.
    /// DERIVED from keyPageRows() rather than built a second time, so the two
    /// can never come out different lengths.
    [[nodiscard]] std::vector<std::string> keyRows() const;

    /// THE OPTIONS PAGE. Sensitivity, invert-Y, field of view and the pad's
    /// deadzone on the first rows; every verb in the game, rebindable, under
    /// them. Same surface, same paging and same keys as a conversation, for the
    /// same reason the casebook borrows it: one list widget, proved once.
    void toggleOptions();
    [[nodiscard]] bool optionsOpen() const noexcept { return optionsOpen_; }
    [[nodiscard]] std::vector<std::string> optionRows() const;
    void moveOptionCursor(int delta);
    /// LEFT and RIGHT on a slider row. Does nothing on a binding row.
    void adjustOption(int delta);
    /// ENTER on a binding row: the next key pressed takes it. On a slider row,
    /// nudges it up, so ENTER always does something.
    void chooseOption();
    /// True while the page is waiting for a key to bind.
    [[nodiscard]] bool awaitingKey() const noexcept { return awaitingKey_; }
    /// Binds the key the page was waiting for. Key::None cancels.
    void bindAwaited(Key key);
    [[nodiscard]] int optionCursor() const noexcept { return optionCursor_; }

    /// How many rows of the options page are sliders rather than bindings.
    static constexpr int kSliderRows = 4;

    /// Field of view, degrees, clamped to kMinFov..kMaxFov. Live: the camera
    /// reads it every frame, so the slider moves the view while you watch.
    void setFov(int degrees);
    [[nodiscard]] int fovDegrees() const noexcept { return controls_.fovDegrees; }

    // --- the pause menu -------------------------------------------------------
    //
    // ESC USED TO QUIT THE MOMENT NOTHING ELSE WAS OPEN. That is the switch
    // statement's own comment, verbatim, from before this page existed:
    // pressing the one key every other game backs out with closed the window
    // outright, with no confirmation and no way back if a finger slipped. This
    // is the menu that key opens instead -- RESUME, SETTINGS and QUIT, drawn in
    // the same conversation surface the casebook and the keys page already
    // prove, so it is one more list widget and not a new kind of screen.
    //
    // IT DOES NOT STOP THE CLOCK. Same as the casebook, the keys page and the
    // options page: PhasedEngine keeps ticking underneath it, because "the
    // district keeps its own hours whether you watch it or not" (the keys
    // page's own F1 copy) is a design law with no carve-out for this page. It
    // is called "MENU" on screen rather than "PAUSED" for exactly that reason
    // -- a label that promised a freeze this build does not do would be the
    // same class of bug as an enum name leaking into a bark.
    void togglePause();
    [[nodiscard]] bool pauseOpen() const noexcept { return pauseOpen_; }
    /// RESUME, CONTROLS, SETTINGS, and QUIT -- the last one asking twice. See
    /// choosePause().
    ///
    /// MORROWIND ROUND: CONTROLS IS NEW HERE. The tiled Menu's four tiles
    /// (Character/Map/Letters/Journal) have no room for a fifth, long,
    /// read-top-to-bottom list, so Keys followed Options' own precedent --
    /// SETTINGS has always opened the identical optionsOpen_ state Menu's
    /// own Options page used to (controls.hpp's own note on why that reads
    /// as one system, not two) -- and took a numbered row here instead. The
    /// keys page ITSELF is unchanged: same single-panel widget, same
    /// centre-clear guarantee, reached a different way.
    [[nodiscard]] std::vector<std::string> pauseRows() const;
    void movePauseCursor(int delta);
    /// ENTER. RESUME closes the page; SETTINGS opens the options page in its
    /// place, so the rebinding screen from the controls round is one more press
    /// away from the menu a player actually pauses on; QUIT arms a second press
    /// rather than closing the window on the first one -- see quitArmed().
    void choosePause();
    /// True once QUIT has been chosen and is waiting on a confirming second
    /// press. The row itself says so, and ESC or moving the cursor disarms it
    /// without closing the menu -- a player who leant on a key by accident
    /// should not have to re-open anything to see they are still safe.
    [[nodiscard]] bool quitArmed() const noexcept { return quitArmed_; }
    /// True once QUIT has been confirmed. The client polls this once a frame
    /// and is the only thing that actually tears the window down -- Session
    /// never touches SDL, here or anywhere else in it.
    [[nodiscard]] bool quitRequested() const noexcept { return quitRequested_; }

    // --- the character sheet ---------------------------------------------------
    //
    // WHO YOU HAVE BECOME, ON THE FIVE TRACKS legend.hpp ALREADY ADDS UP, PLUS
    // THE HANDS THAT DID THE ADDING. Legend was built in S8 to answer "who am I
    // in this city yet" and has been derived every frame since -- see
    // legend.hpp's own design note -- but the only place it ever reached the
    // screen was one row on the HUD's own corner (Session::guildLine, the
    // track you happen to be highest on) and legendLine(), a second summary
    // line that has had exactly two callers since the sprint that wrote it and
    // both of them are tests. The other four tracks, and every rung of them,
    // were computed and thrown away every single frame.
    //
    // MORROWIND ROUND: THE TOP-LEFT TILE. One of the tiled Menu's four
    // simultaneous panels now (see "ONE MENU, FOUR TILES" above), drawn by
    // menu_view.hpp's drawMenuTiles() rather than filling the whole
    // conversation surface the way it did as one of #85's six pages.
    //
    // NO PAPER DOLL AND NO ARMOUR RATING, ON PURPOSE. There is no item and no
    // equipment-slot model in this build -- the quick bar's own comment says so
    // (VERIFICATION GAP #77, above) -- so a screen that drew ten empty slots
    // would be furniture claiming a state the simulation does not have. What
    // the simulation DOES have is five derived standings and four skills
    // actually wired to a verb (SKYRUNNING, CRACKSMANSHIP, STREETWISE,
    // LINKCRAFT -- the other sixteen entries in content/raws/skills/skills.json
    // are authored vocabulary with nothing in this build that levels them yet),
    // and this page is exactly that, no more.
    void toggleCharacter();
    /// True while the tiled Menu is open -- the same bool casebookOpen() is;
    /// see that accessor's own note.
    [[nodiscard]] bool characterOpen() const noexcept { return casebookOpen_; }
    /// The five Legend tracks (each with the score the next rung wants --
    /// legend.hpp's own promised "what the next one wants" line), the skills
    /// actually in play, the five faction ladders with the numbers on (rank
    /// title, standing, next-rung cost -- the owner's numbers-on-the-sheet
    /// ruling; NPCs still talk in words only), and what the ward and the
    /// purse currently say -- one row a line, built fresh from the same
    /// counters the HUD's corner rows read.
    [[nodiscard]] std::vector<std::string> characterRows() const;

    // --- #82: the district map ------------------------------------------------
    //
    // DAGGERFALL'S OWN TRAVEL MAP, SCALED TO ONE DISTRICT. There is no
    // cross-country fast travel to draw a screen for -- the whole game is the
    // Docks -- so what a Wielder needs here is not a map of the WORLD, it is a
    // map of the INVESTIGATION: the ground the trail has put a name to, the
    // leads still waiting on a look, and who among the named will actually
    // talk. All three are read out of state the game already keeps
    // (Casebook, NotableRegistry, FactionRegistry); nothing new is persisted
    // and nothing here is hashed, for the identical reason characterRows()
    // is not -- see that method's own note.
    //
    // SAME SURFACE, SAME REASON EVERY OTHER PAGE IS. One list widget, proved
    // once by the casebook and the character sheet; a seventh reason to draw
    // it in the middle of the screen would be a seventh way to break the HUD
    // rule the Java build broke once already.
    //
    // READ-ONLY, LIKE THE KEYS PAGE AND THE CHARACTER SHEET -- unlike the
    // casebook, nothing on this page is a choice to make, only ground to
    // read, so a number press moves the cursor and nothing else.
    //
    // MORROWIND ROUND: THE TOP-CENTRE TILE. See toggleCharacter()'s own note.
    //
    // CORE ACTION #13 SUPERSEDED THE TEXT-ONLY STANCE FOR NAVIGATION, and
    // only for navigation: the owner's direct ask ("It's too difficult to
    // locate places like the mission...") added the full-screen graphical
    // ward map below. THIS tile stays exactly what #82 built -- the
    // INVESTIGATION's map: leads, bearings, who talks -- and nothing here
    // moved.
    void toggleMap();
    /// True while the tiled Menu is open -- the same bool casebookOpen() is;
    /// see that accessor's own note.
    [[nodiscard]] bool mapOpen() const noexcept { return casebookOpen_; }
    /// Known ground, then open leads (each with a bearing and a range from
    /// where the player is standing), then who will talk -- see the .cpp for
    /// why each section is built from what Casebook already knows and no new
    /// state.
    [[nodiscard]] std::vector<std::string> mapRows() const;

    // --- the ward map (core action #13) ---------------------------------------
    //
    // THE OWNER'S ASK, VERBATIM: "It's too difficult to locate places like
    // the mission, let's give the player a map that they can press M to
    // see." A FULL-SCREEN top-down plan of the district, software-rendered
    // from the same TileQuery the first-person pass reads, with the authored
    // sign table's names on it -- see map_view.hpp for the page itself and
    // for why this is NOT the tiled Menu's Chart tile (that tile is the
    // INVESTIGATION's map -- leads, bearings, who talks -- and stays; this
    // is NAVIGATION, and the owner's direct ask supersedes the old "the text
    // map is settled" note for navigation purposes only).
    //
    // Pure render-layer reads: nothing here reaches the simulation, nothing
    // is hashed, and the world hash is byte-identical with the page open.

    /// M on a keyboard; on a pad the map is a page of NOTES (the ring, see
    /// menuPageNext) and has no button of its own. Toggles the ward map;
    /// inert while talking or picking, exactly like toggleGrimoire, and
    /// every other overlay stands down when it opens.
    void toggleDistrictMap();
    [[nodiscard]] bool districtMapOpen() const noexcept { return districtMapOpen_; }

    // --- THE MAP PASS: the page is driven now, not read -----------------------
    //
    // The owner's second complaint about it, verbatim: "Names are stacking up
    // on the map view. Makes it hard to figure out where the place you're
    // looking for is." The page it produced was a static picture with eighty
    // three floating nameplates fighting for room. It is a COMPOSED PANE now
    // (map_view.hpp) with a cursor that walks named PLACES, a tab row of views
    // over the selection, and a detail pane -- so the six methods below are the
    // whole of what a key, a pad button or a mouse click can do to it.
    //
    // The cursor is an index into map_view.hpp's mapPlaces(): every named place
    // in the ward, deduped and alphabetical. Still pure render state; nothing
    // here reaches the simulation and the world hash cannot move.

    /// Which named place the cursor is on -- an index into mapPlaces().
    [[nodiscard]] int districtMapSelected() const noexcept { return districtMapSelected_; }
    /// Which view is over the selection.
    [[nodiscard]] MapTab districtMapTab() const noexcept { return districtMapTab_; }
    /// Which rung of the zoom ladder. 0 is the whole ward.
    [[nodiscard]] int districtMapZoom() const noexcept { return districtMapZoom_; }

    /// An arrow press: moves to the nearest named place that way. On the Index
    /// tab, UP/DOWN walk the list in its own alphabetical order instead, which
    /// is what a list you are reading should do under an arrow key.
    void moveDistrictMapCursor(MapStep step);
    /// TAB (delta +1) and shift-TAB (-1). Wraps.
    void cycleDistrictMapTab(int delta);
    /// The printed digits 1-4 on the tab row, which really do select.
    void setDistrictMapTab(int index);
    /// `+` and `-`. Clamped to the ladder.
    void adjustDistrictMapZoom(int delta);
    /// Puts the cursor on a place by its authored name. Does nothing if there
    /// is no such place -- which is what a capture flag with a typo in it
    /// should do rather than silently photograph the wrong building.
    [[nodiscard]] bool selectDistrictMapPlace(std::string_view name);
    /// ENTER: turns the body to face the selected place and closes the page.
    /// The commit verb at the foot of the detail pane, and the honest first
    /// half of "going to a place" -- you cannot walk somewhere you cannot face.
    void faceDistrictMapSelection();

    // --- FAST TRAVEL (TRAVEL lane) -------------------------------------------
    //
    // THE OWNER'S ASK, this session: he read the ward map's cursor, named
    // selection and FACE IT as a fast-travel screen, was told it is only a
    // compass, and said plainly he wants the real thing -- Daggerfall's map
    // travels. So the page gets a second commit verb, TRAVEL, built out of
    // parts already spent: the cost is the route the district's own PathFinder
    // answers, priced at the shipped walking pace (the travel* functions at
    // the bottom of this file); the clock advances through skipSeconds() --
    // THE WAIT MACHINERY PLUS A RELOCATION, one time system, not two; the body
    // lands by the same PlayerBody::placeAt the rented bed and the Watch's
    // morning release already make; and the threshold plate announces the
    // arrival exactly as a walked crossing would be announced.
    //
    // WHAT REFUSES, AND WHY IT IS THE WAIT PAGE'S OWN LIST PLUS TWO. Every
    // waitRefusal() clause holds verbatim (a travel IS a wait). On top:
    // carrying the courier case's man -- stepSheetCase() completes the
    // delivery the moment the body is near the back room, so a permitted
    // travel-while-carrying would teleport-finish the case's whole final act;
    // and WatchStance::Closing -- you do not stroll off mid-witness. Mere
    // heat or a warrant deliberately does NOT refuse, consistent with the
    // owner's wait ruling that waiting one out is a tactic.

    /// Everything the TRAVEL verb knows about the current selection: the cost
    /// if the walk is honest, or the one-line reason it is not.
    struct TravelPlan {
        /// The selection contains the body: nothing to travel to, no verb --
        /// the foot's "YOU ARE STANDING IN IT" already words it.
        bool standingIn = false;
        /// Route found, ground standable, nothing refusing: the verb is live.
        bool available = false;
        /// Why not, in the city register, or empty. ONE string for the pane's
        /// verb row and the press's spoken line, so the page and the key can
        /// never name different doors -- waitRefusal()'s own contract.
        std::string refusal;
        /// The route the cost was derived from: steps, octile units, honest
        /// seconds, and the whole minutes the clock will actually advance.
        std::int32_t routeSteps = 0;
        std::int32_t units = 0;
        std::int32_t seconds = 0;
        std::int32_t minutes = 0;
        /// Where the body lands: the selection's own aim point (the door you
        /// knock on) snapped to the nearest standable tile on the place's
        /// band -- never inside geometry.
        std::int32_t toX = 0;
        std::int32_t toY = 0;
        std::int32_t toBand = 0;
    };
    /// The plan for the cursor's place, recomputed on demand -- a pure read;
    /// nothing moves until travelDistrictMapSelection() spends it.
    [[nodiscard]] TravelPlan districtMapTravelPlan() const;
    /// Why travel is refused here and now, or "" -- the carry clause first
    /// (the one refusal that is load-bearing for the courier case), then the
    /// Watch closing, then waitRefusal()'s own list verbatim. Re-checked on
    /// the press, not only at draw, exactly as the wait page re-checks.
    [[nodiscard]] std::string travelRefusal() const;
    /// The TRAVEL commit: refuses out loud (the page staying up), or advances
    /// the clock by the plan's exact minutes through the wait machinery,
    /// relocates, faces the door, arms the threshold plate, dips the frame to
    /// black to ease up at the destination, and says the arrival line.
    void travelDistrictMapSelection();

    // --- RELOCATED: SNAP THE VIEW -- the one seam for every instant jump ----
    //
    // THE OWNER'S BUG, run to ground: "teleporting through walls ... double
    // vision/ghosting" on the scripted routes. The BODY has always jumped
    // clean (PlayerBody::placeAt zeroes every arc, haul and velocity, and the
    // camera reads the body raw -- there is no eased camera position in this
    // build to race across the district). What DID survive a jump was the
    // render side's eased, position-derived furniture: the crosshair's
    // subject, the lock row, the room row -- each on its own EasedToggle --
    // kept easing OUT over the NEW street for eight frames, a label from a
    // place three hundred tiles away ghosting over the arrival. These three
    // calls are the whole cure, and every relocation seam funnels through
    // them so the next one (fast travel's arrival is already here; whatever
    // comes after it will not be) cannot re-open the bug.

    /// Snaps every eased, position-derived piece of VIEW state to what the
    /// body's new ground actually offers -- which, on the frame of a jump, is
    /// nothing: the stale crosshair subject, lock row and room row close NOW
    /// instead of easing out over ground they were never true of. The next
    /// step()'s own sync re-opens whichever of them the new place earns.
    /// Render-only: nothing here reaches the simulation, the hash, or the
    /// gate. EXPORTED as the arrival half of any future instant relocation --
    /// a caller that moves the body by any means other than placeBodyAt()
    /// calls this the same instant.
    void snapViewAfterRelocation();

    /// PlayerBody::placeAt plus snapViewAfterRelocation(), as one verb --
    /// so a relocation cannot forget its snap. Every scripted placement
    /// (the demo's cuts, the capture lines' stagings, the respawn, the
    /// arrest release, the travel commit) goes through here.
    void placeBodyAt(std::int32_t tileX, std::int32_t tileY, std::int32_t band);

    /// DRESSES an instant cut in the travel seam's own cloth: the frame of
    /// the cut is already fully black and the destination eases up under the
    /// plate (see travelFadeAnim_'s header -- the owner called raw cuts
    /// "teleporting"; this is the difference). Separate from the snap
    /// deliberately: the capture lines relocate and must photograph the
    /// world, not the veil, so they snap without dressing; a SCRIPTED cut a
    /// viewer watches (a demo cut, a clock jump, a travel) snaps AND
    /// dresses. Advanced in step(), so it serves any caller whose steps run
    /// one per frame -- the demo, travel, live play. --case-watch's holds
    /// step zero times a frame, so the watch wears its own veil at the same
    /// width (see CaseWatchDirector).
    void dressInstantCut();

    /// The seam veil's current strength, 0 (clear) to 1 (black) -- so a case
    /// can pin that a scripted cut was dressed and that no shutter ever
    /// fires through it.
    [[nodiscard]] float cutVeil() const noexcept { return travelFadeAnim_.value(); }

    /// The whole page, ready to draw. Public because a case reads it and
    /// because it is the same shape keysPageState() already has.
    [[nodiscard]] DistrictMapState districtMapState() const;

    /// A STANDING JUMP. Half a metre, and it gets you onto nothing -- see
    /// sim::PlayerBody::jump. Bound to space, which is where a jump goes.
    ///
    /// IT IS NOT THE CLIMB KEY. S5 put the mantle on space and a player who
    /// pressed it expecting a jump got a climb. Walking into a ledge climbs it
    /// now; this jumps.
    void jump();

    /// Sets the stance directly rather than flipping it.
    ///
    /// WHY BOTH EXIST. toggleCrouch is what a TAP does; this is what a HOLD
    /// does, and a hold that had to be expressed as a toggle would desynchronise
    /// the moment a key-up event went missing behind an alt-tab. See
    /// render::HoldToggle, which owns the rule that decides which one a press
    /// was.
    void setCrouched(bool crouched);

    /// THE QUICK BAR. Ten slots off the number row, and the wheel walks them.
    ///
    /// VERIFICATION GAP #77 IS CLOSED (SPELLS BUILD): THE SLOTS HOLD CRAFTINGS
    /// NOW. sim::Tavern owns the contents (bindSpellToSlot/slotSpell/equipSlot
    /// -- hashed, because what a number key readies decides what the next cast
    /// does); selecting a LOADED slot equips its crafting through the same
    /// equipSpellAt path the Grimoire page uses, and an empty slot still says
    /// so out loud rather than letting the key read as broken. There is still
    /// no item model: a slot holds a spell id and nothing else.
    void selectQuickSlot(int slot);
    [[nodiscard]] int quickSlot() const noexcept { return quickSlot_; }
    /// Keeps the bottom-centre quick bar strip on screen for a couple of
    /// seconds -- called by selectQuickSlot itself (every digit, wheel and
    /// D-pad step lands there), so the strip is up exactly while it is being
    /// used and stands down after (its own EasedToggle does the easing).
    void showQuickBar();
    /// True while the strip is WANTED (a recent selection). The
    /// drawn alpha is its EasedToggle's business; this is the target a test
    /// can assert on.
    [[nodiscard]] bool quickBarWanted() const noexcept { return quickBarShowSteps_ > 0; }

    // --- the Law of Earned Text (UI-EA-SPEC sec. 2, LANE HUD) ----------------
    //
    // TEXT PRINTS WHEN IT CHANGES, WHEN IT IS AIMED AT, OR WHEN THE PLAYER
    // HESITATES -- NEVER MERELY BECAUSE IT IS TRUE. The street HUD's
    // reference rows (clock, purse, case, room, guild, objective) used to be
    // furniture: up every frame, saying that nothing had happened. Each is an
    // EVENT row now -- a countdown armed on its own edge in syncPanelAnim(),
    // spent once a step in step(), eased by its own EasedToggle, exactly the
    // quickBarShowSteps_ machinery generalized. All render-side, steps-based,
    // unhashed. These accessors are the targets tests assert on, the same
    // deal quickBarWanted()/placePlateWanted() already offer.

    /// True while the clock is WANTED: ~2.5s after an hour tick or a time
    /// charge (travel, a wait pick, a sleep), for the life of the wait page
    /// (whose rows price the very hours the clock shows), and on the pause
    /// stack (the spec's #34 keeps rows, clock and title there).
    [[nodiscard]] bool clockWanted() const noexcept {
        return waitOpen_ || pauseOpen_ || clockShowSteps_ > 0;
    }
    /// True while the purse is WANTED: ~2.5s after any coin delta.
    [[nodiscard]] bool purseWanted() const noexcept { return purseShowSteps_ > 0; }
    /// True while the case row is WANTED: ~2.5s after a beat/lead change or a
    /// casebook close -- and never while the lead-opened plate is up, which
    /// says the same news louder ("the notice IS the case news").
    [[nodiscard]] bool caseRowWanted() const noexcept { return caseShowSteps_ > 0; }
    /// True while the room row is WANTED: ~2.5s after a room entry or the
    /// room's loudness turning over. A head-count drifting is not an event.
    [[nodiscard]] bool roomRowWanted() const noexcept { return roomShowSteps_ > 0; }
    /// True while the guild/objective rows are WANTED: ~2.5s after a change.
    [[nodiscard]] bool guildRowWanted() const noexcept { return guildShowSteps_ > 0; }
    [[nodiscard]] bool objectiveRowWanted() const noexcept {
        return objectiveShowSteps_ > 0;
    }
    /// The Q-hold tutor toast: what it says while it is up, and whether it is
    /// wanted. Armed on the quick bar's first two risings ever, riding the
    /// strip's own countdown, then retired for the session.
    [[nodiscard]] std::string_view wheelHintLabel() const noexcept {
        return std::string_view{wheelHintText_};
    }
    [[nodiscard]] bool wheelHintWanted() const noexcept { return wheelHint_.wanted(); }

    // --- the threshold plate (DISTRICT PHASE D) -------------------------------
    //
    // CROSSING INTO A NAMED PLACE IS AN EVENT AND THIS BUILD HAS NEVER SAID SO.
    // placeLabel() has answered "where am I" since S2 and the HUD has drawn it
    // as the compass ribbon's own dim sub-label ever since -- correct for a
    // fact you look up, and it means the ward's authored names (docks.hpp's
    // kPlaces) announce themselves by quietly changing four small words that
    // nobody is looking at. The plate is the same fact, said once, at the
    // moment it becomes true.
    //
    // PURE RENDER. Nothing here reaches PhasedEngine, nothing here is hashed,
    // and the world it reads is the baked map's own authored table. A session
    // that never draws behaves identically with and without it.

    /// What the threshold plate is currently announcing, or empty. The last
    /// name it FIRED on -- held through the fade, the identical reason every
    /// *Cache_ member exists, so the plate finishes fading with its own words
    /// still on it rather than with the words of wherever you have got to
    /// since.
    [[nodiscard]] std::string_view placePlateLabel() const noexcept {
        return std::string_view{placePlateName_};
    }
    /// True while the plate is WANTED -- the couple of seconds after a
    /// crossing, and no longer. The drawn alpha is its EasedToggle's business;
    /// this is the target a test can assert on, exactly as quickBarWanted() is
    /// for the strip.
    [[nodiscard]] bool placePlateWanted() const noexcept { return placePlateShowSteps_ > 0; }
    /// The last NAMED place the body stood in -- what the next crossing is
    /// compared against. Empty only before the body has ever stood in one.
    ///
    /// IT REMEMBERS NAMES AND NEVER THE GAPS BETWEEN THEM, and that is the
    /// whole restraint of this feature. Two thirds of the district is
    /// compounds, yards and back lanes nobody has named (placeNameAt's own
    /// note), and Tarwalk itself is authored as three abutting rectangles;
    /// updating this on an unnamed tile would re-announce TARWALK every time
    /// the player stepped into an alley and back out, and re-announce SALTGATE
    /// RISE at every seam. Only a non-empty name ever lands here, so walking
    /// TARWALK -> an alley -> TARWALK is one place, once, which is what it is.
    [[nodiscard]] std::string_view lastPlaceName() const noexcept {
        return std::string_view{lastPlaceName_};
    }

    // --- the Grimoire page (SPELLS BUILD) ------------------------------------
    //
    // A list page, NO new Menu tile. The page is the same DialogueViewState/
    // drawDialogue panel every other page is -- one list widget, proven once.
    // NINE AND THE STICKS: it is a PAGE OF NOTES now -- one bumper past the
    // ward map in the ring menuPageNext()/menuPagePrev() walk -- since the
    // QuickWheel tap that used to open it is cut. No binding of its own.

    /// Opens or closes the page. Inert while talking or picking, exactly like
    /// toggleKeys; every other overlay stands down when it opens.
    void toggleGrimoire();
    [[nodiscard]] bool grimoireOpen() const noexcept { return grimoireOpen_; }
    /// One row per known crafting, grimoire order: name, its difficulty out
    /// of the cost model, FORGED for a composition of your own, the slot it
    /// is bound to, and READY on the one the hand is holding.
    [[nodiscard]] std::vector<std::string> grimoireRows() const;
    [[nodiscard]] int grimoireCursor() const noexcept { return grimoireCursor_; }
    [[nodiscard]] int grimoirePage() const noexcept { return grimoirePage_; }
    void moveGrimoireCursor(int delta);
    void nextGrimoirePage();
    /// Picks the row printed with this number on the visible page -- and
    /// picking a crafting EQUIPS it, through the same Tavern::equipSpellAt a
    /// loaded quick slot uses. One source of truth for what the hand holds.
    /// ENTER does the same to the cursor's row, exactly like a topic list.
    void chooseGrimoireRow(int slot);
    /// LEFT/RIGHT on the cursor's row: walks WHICH SLOT that crafting is
    /// bound to, through NONE and back round -- the options page's own
    /// slider shape, and pad-reachable for the same reason (numbers are not
    /// on a pad; the D-pad is). Moving a crafting off a slot clears the old
    /// slot, so the bar never promises the same crafting twice.
    void adjustGrimoireSlot(int delta);

    // --- the Wait page (TIME-AND-TENURE BUILD) -------------------------------
    //
    // OWNER RULING, 2026-08-18: WAIT works anywhere safe -- time passes,
    // nothing mends; SLEEP (which heals) stays bed-only through rented rooms.
    // ONE page serves both, the same DialogueViewState/drawDialogue widget
    // every page is, reached through two doors: the pause menu's WAIT row
    // (wait mode, anywhere safe) and the Interact press at your own rented
    // bed (sleep mode, where sleep() has always answered). Twelve rows, one
    // per hour ahead; picking one is Session::skipToHour -- through
    // Tavern::sleepUntil first in sleep mode, which is the only path that
    // touches a hit point. Heat keeps cooling on the elapsed seconds exactly
    // as Tavern::skipTo always has: waiting out a warrant is an intended
    // tactic, so no refusal here reads the Watch's ledger.
    //
    // "ANYWHERE SAFE", DEFINED (conservatively, and in one place --
    // waitRefusal()): not while anybody is swinging at you, not with a
    // HOSTILE body within three talk-reaches (six tiles), not from the
    // floor, not in mid-air, and not standing in water. Everything else --
    // a dark alley, a rooftop, a warrant on your name -- counts as safe:
    // danger in this build is a person or a fall, not a place.

    /// Opens the hour-select page; every other overlay stands down.
    /// `sleepMode` true is the rented-bed door (healing skip through
    /// Tavern::sleepUntil); false is the pause row's plain WAIT.
    void openWait(bool sleepMode);
    [[nodiscard]] bool waitOpen() const noexcept { return waitOpen_; }
    [[nodiscard]] bool waitSleeping() const noexcept { return waitSleep_; }
    /// Twelve rows: "N HOURS  TO HH:00", dawn/noon/dusk/midnight named.
    [[nodiscard]] std::vector<std::string> waitRows() const;
    [[nodiscard]] int waitCursor() const noexcept { return waitCursor_; }
    [[nodiscard]] int waitPage() const noexcept { return waitPage_; }
    void moveWaitCursor(int delta);
    void nextWaitPage();
    /// Picks the row printed with this number on the visible page and passes
    /// the hours -- or says, out loud, why not. ENTER does the same to the
    /// cursor's row, exactly like a topic list.
    void chooseWaitRow(int slot);
    /// Why waiting is refused HERE, or "" when this spot counts as safe.
    /// The page prints it and chooseWaitRow enforces it, off the one
    /// definition above, so the page and the key can never name different
    /// doors. Sleep mode never consults this: the bed's own
    /// sleepReadiness() is that door's gate, as it always was.
    [[nodiscard]] std::string waitRefusal() const;

    /// TRUE UNTIL THE PLAYER HAS DONE ANYTHING AT ALL. A fresh session opens
    /// with the casebook up and the hook on screen, because "dropped into a
    /// systems demo with no orientation" is the thing this build has always
    /// done and the demo is not allowed to.
    [[nodiscard]] bool firstRun() const noexcept { return firstRun_; }

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

    /// ACTION-COMBAT BUILD (section 4.5). THE DEATH CEREMONY, drawn over
    /// everything the instant a defeat lands under lethal rules (steel was out
    /// -- tavern_->escalated()). Fills the frame toward black, holds the
    /// epitaph plate (killer, weapon, place) for kDeathHoldSteps, then eases
    /// back to reveal the quay revive settleDefeat already performed under it.
    /// Render-only, never hashed: the sim revived at the moment of defeat, this
    /// is the OCCASION laid over the top -- Barony's lesson that ceremony at the
    /// end buys the frictionless beginning. A no-op while deathCeremonySteps_ is
    /// zero, so it is harmless in every drawFrame return path it rides.
    void composeDeathCeremony(Framebuffer& target) const;
    /// Arms the ceremony above from the last defeat's Rise -- the killer's name,
    /// a weapon word off his hand, and the place it happened. Called by
    /// settleDefeat only on an escalated (lethal) defeat.
    void armDeathCeremony();

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
    /// "THE SKYRUNNERS - TENANT", or empty when the player is on no rung AND
    /// whenever a lock is open -- the wire's own row is the same pixel row as
    /// this one. PUBLIC so that suppression is a claim a case can make rather
    /// than a thing only pixels could catch.
    [[nodiscard]] std::string guildLine() const;
    /// What the questline in progress wants next, and empty for the same
    /// reason while the wire is in.
    [[nodiscard]] std::string objectiveLine() const;
    /// THE CROSSHAIR PASS. WHICH LEAD A LOOK FROM HERE WOULD BE A LOOK AT, or
    /// -1. The index into CasebookRaws::leads().
    ///
    /// IT MUST NOT BE Casebook::look(). That call READS the lead -- it opens
    /// what the lead opens, stamps heardAt() and moves the ward's dread -- and
    /// a HUD label that read the case by being drawn would finish the trail
    /// for a player who walked past a door. So this is a walk over Lead::site
    /// and nothing else, with examine()'s own reach (kLookRangeTiles plus the
    /// Flame's eye) so the crosshair names exactly what the key would find.
    /// PUBLIC so the reach rule is a claim a case can make.
    [[nodiscard]] int leadInLookReach() const;
    /// THE CASEBOOK PASS. True when the body could LOOK at this lead from where
    /// it is standing right now -- the same reach examine() uses, boon
    /// included. Read by the page to choose between its two commit verbs; see
    /// CasebookLeadRow::here on why it must mean exactly what LOOK means.
    [[nodiscard]] bool bodyCanLookAt(const sim::Lead& lead) const;
    /// "THE WARD WANTS YOU GONE", or empty exactly when reputationLabel()
    /// reads kReputationUnremarkable -- the identical "absence costs
    /// nothing" rule every other row on this stack already keeps. PLANNING
    /// SPRINT (item #2): the one row of this HUD's top-right stack still
    /// missing the treatment every bottom-left row already has -- see
    /// syncPanelAnim()'s own note on standingAnim_/heatAnim_/stashAnim_.
    [[nodiscard]] std::string standingLine() const;
    /// "CAST  STING" -- the crafting the next press of C will spend, with
    /// "(12S)" appended while the link is still cooling. Empty with an empty
    /// grimoire: absence costs nothing, the row appears the moment there is a
    /// crafting to ready. PUBLIC for the same reason stashLine is: a case
    /// pins both what this says and that it stays on its edge.
    [[nodiscard]] std::string spellLine() const;
    /// "GUARD UP" exactly while the room's own playerBlocking() is true, and
    /// empty otherwise -- the held state made visible, not the keypress.
    /// PUBLIC for the identical reason.
    [[nodiscard]] std::string blockLine() const;
    /// ACTION-COMBAT BUILD. "HELD HARD -- CUDGEL 14-18" while the swing is
    /// charged past the hard threshold (Tavern::playerChargeHard), naming the
    /// held weapon in the sheet's own weaponSheetLine grammar. Empty at rest,
    /// at a light charge, and in recovery -- the row is the hard tier made
    /// visible, the reticle carries the light one. PUBLIC so a case can pin
    /// what it says and that it stays on its edge, the same as blockLine.
    [[nodiscard]] std::string chargeLine() const;
    /// STANCE & ROOM BUILD. "FISTS UP" / "CUDGEL UP" / "THE EVICTOR UP" /
    /// "STEEL UP" exactly while the room's own playerHandsUp() is true, and
    /// empty otherwise -- fighting mode made visible, the ONE presentation
    /// touch the stance lane makes, on the blockLine pattern: the ROOM's fact,
    /// never the keypress. PUBLIC for the identical reason blockLine is.
    [[nodiscard]] std::string handsLine() const;
    /// HELD-EFFECTS BUILD. "STEADY THE HAND 842S" -- the slot-th live hold on
    /// the player, its name out of the grimoire and the seconds it has left,
    /// counting down continuously. Empty past the table's end, which is the
    /// usual state of all four slots. PUBLIC for the identical reason: a case
    /// pins what the row says and that it stays on its edge.
    [[nodiscard]] std::string effectLine(std::size_t slot) const;
    /// How many active-effect rows the HUD will ever stack -- the top-right
    /// edge's own budget, matching HudState::effectLabels.
    static constexpr std::size_t kEffectRows = 4;

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

    /// UI-EA-SPEC 1.7 (LANE PAGES, strip->card): true while one of the four
    /// pause-stack strips (pause, wait, options, grimoire) is up -- the family
    /// that now draws as ONE composed card (drawCreationPage's own generic
    /// composition) instead of a HUD strip, the ship note's standing item
    /// extended to the family. The card's content comes from stripCard().
    [[nodiscard]] bool stripCardOpen() const noexcept {
        return pauseOpen_ || waitOpen_ || optionsOpen_ || grimoireOpen_;
    }
    /// The composed card for whichever pause-stack page is up. Rows, cursor
    /// and digit windows keep exactly the meaning the strip gave them --
    /// wrapCursorAndPage's nine-key arithmetic included -- so every input
    /// path lands where it always did; only the drawing changed register.
    [[nodiscard]] CreationPage stripCard() const;
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
    /// Who in the WARD is being spoken to, or -1. The Gilded Gull's own
    /// fourteen are Tavern::talkingTo()'s business and never appear here.
    ///
    /// Exposed so a case can assert WHICH body a conversation opened on. #79's
    /// whole failure mode is a menu built by a machine that was not looking at
    /// what it was talking to, and "somebody answered" is not evidence against
    /// it.
    [[nodiscard]] std::int32_t wardTalkingTo() const noexcept { return wardTalkId_; }

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

    /// THE WARD'S OWN PEOPLE, as billboards. One drawn figure each, out of the
    /// owner's own sprite sheet, tile-stepped in the simulation and
    /// interpolated HERE -- the sim owns which two tiles a body is between and
    /// the renderer owns where between them it is drawn this frame, which is
    /// the 2026-07-31 ruling read exactly as written.
    [[nodiscard]] std::vector<SpriteInstance> wardSprites(const Camera& view) const;
    [[nodiscard]] std::vector<SpriteInstance> wardSprites() const;

    /// SHEETS BUILD. The Journal tile's rows UNDER the leads: every live
    /// contract off the board contractLine() already reads (ALL of them,
    /// where that HUD row only shows the soonest), every TAKEN radiant
    /// errand (RADIANT BUILD), then every finished stage's authored
    /// QuestStage::log line in the order it was earned. Pure derived reads
    /// of already-hashed state; a work row is something to read, never a
    /// choice. PUBLIC since the RADIANT BUILD: runRadiantLine's journal beat
    /// asserts the tile's own rows carry a taken errand, and a scripted
    /// proof that re-derived the rows itself would be proving its own copy.
    [[nodiscard]] std::vector<std::string> journalWorkRows() const;

private:
    /// How close you have to be to address somebody on the street. Two tiles,
    /// which is the same reach Tavern::talkTo uses across a taproom -- one rule
    /// for "within arm's length and a word", not two.
    static constexpr std::int32_t kWardTalkReachTiles = 2;
    /// And how far a crime carries on an open street. The taproom's own witness
    /// range is eight tiles; a quay has no walls to shorten it and no reason to
    /// lengthen it.
    static constexpr std::int32_t kWardWitnessTiles = 8;

    void syncTavernToBody();
    /// Opens a conversation with the nearest body in the WARD, or answers
    /// false. Tried only after the taproom's own roster has said no.
    bool talkToWard();
    /// The ward's settlement of a chosen topic: the purse, the coin and whether
    /// the street saw it. Everything a taproom would also do -- a drink off the
    /// bar, a bouncer sent over, a room rented -- has nobody out here to do it.
    sim::Reply chooseWardTopic(std::size_t index);
    /// Puts the casebook and the key list down. Every verb that acts on the
    /// world calls it first.
    void dismissOverlays() noexcept;

    // --- #85: the shared halves of the consolidated verbs ---------------------
    //
    // interact() and vertical() are ORCHESTRATORS: each tries a short list of
    // narrower attempts in priority order and stops at the first one that
    // resolves. These are the attempts, factored out so the STANDALONE public
    // verbs (climb/dropDown/restHere/steal) and the new orchestrators run the
    // identical code rather than two copies of it that could drift apart.

    /// The success half of climb(): mantle, or the leap it falls back to.
    /// SAYS THE LINE AND MUTATES STATE ONLY WHEN IT SUCCEEDS -- a failure is
    /// returned silently (RoofResult::ok() false) so a caller trying several
    /// things in sequence is not left explaining a wrong guess to the player.
    /// climb() itself says the refusal when this fails; vertical() instead
    /// moves on to tryDropDown().
    [[nodiscard]] sim::RoofResult tryClimb();
    /// The identical shape for dropDown(): steps off the ledge in front and
    /// falls to the first floor under it, or fails silently.
    [[nodiscard]] sim::RoofResult tryDropDown();
    /// Charges a landing and says "SLEPT UNTIL MORNING." -- the half of
    /// restHere() that only runs when tavern_->sleep() actually returned
    /// Served, shared with interact()'s own bed branch so neither has to
    /// duplicate what happens once the room agrees.
    void settleSleep();
    /// The chain steal() has always tried, tried in order until one of them
    /// is not TooFar: the box (or its lock -- crackStrongbox() does not read
    /// stance, so a locked box resolves to picking it whether interact()'s
    /// caller is sneaking or not), the bale, the rat, the wire that buys more
    /// picks. SAYS THE LINE ITSELF, always -- steal() relies on that to
    /// preserve its own pre-#85 behaviour exactly -- and answers whether the
    /// final attempt was anything other than TooFar, which is interact()'s
    /// cue to fall through to the investigation look instead.
    bool stealNearestThing();

    /// MORROWIND ROUND. Opens/refocuses the tiled Menu on tile `focus`
    /// (kMenuFocusCharacter/Map/Letters/Journal, mod 4): if the Menu is
    /// already open, this only moves menuFocus_ (or closes the whole Menu
    /// when `focus` is already the one that has it, the same "press it again
    /// to close" rule every one of #85's six pages used to have on its own);
    /// if it is not open, this opens it fresh, standing every other overlay
    /// down first, exactly as toggleOptions()/toggleKeys()/togglePause()
    /// still do on their own. The one function toggleCasebook()/
    /// toggleCharacter()/toggleMap()/toggleLetters() all forward to.
    void toggleMenuFocused(int focus);
    /// Builds the DialogueViewState for one of the tiled Menu's four panels,
    /// independent of which (if any) currently has focus -- drawMenuTiles()
    /// needs all four every frame, not only the focused one, and
    /// dialogueView() reads whichever one menuFocus_ names so a caller that
    /// only ever asked about "the open page" (every pre-Morrowind-round test)
    /// keeps seeing exactly the content it always did.
    /// MERGE FIX: public -- the pointer lane's session_pointer (main.cpp)
    /// assembles the tiled Menu's hit-test state from these four, the same
    /// way drawFrame assembles its own; const views, no mutation offered.
public:
    [[nodiscard]] DialogueViewState characterPanelView() const;
    [[nodiscard]] DialogueViewState mapPanelView() const;
    [[nodiscard]] DialogueViewState lettersPanelView() const;
    [[nodiscard]] DialogueViewState journalPanelView() const;

private:
    /// TASK #82. Every letter whose `lead` (sim::Letter::lead, a
    /// casebook.json lead id) has actually been investigated -- Cold or
    /// Followed, never merely Open -- in authored order. What the letters
    /// page lists, and what the letters navigation bounds the cursor
    /// against. MERGE FIX: public -- the courier lane's runCaseLine drive
    /// and main.cpp's run summary both read it from outside, the same way
    /// they read sheetBook()/sheetCarry().
public:
    [[nodiscard]] std::vector<std::int32_t> unlockedLetters() const;

private:
    /// COURIER CASE. unlockedLetters() answers in COMBINED indices now that
    /// there are two authored files: [0, letterRaws_.letters().size()) is the
    /// Bloodletter file, and everything above it is the mission sheet file at
    /// (index - size). These two resolve a combined index back to a document
    /// and count the whole shelf, so every consumer walks one list and the
    /// two files stay two files. Fixed member order = deterministic order.
    [[nodiscard]] const sim::Letter& letterAt(std::int32_t combined) const;
    [[nodiscard]] std::int32_t letterCount() const noexcept;
    /// TASK #82. The casebook's own clock, in the unit Casebook::heardAt and
    /// Casebook::look/hear/begin all take: seconds since midnight on the day
    /// the session started PLUS every simulated second since. Two calls
    /// apart in wall-clock time but made on the same movement step read the
    /// same value, which is the whole point -- a dateline is a fact about
    /// the SIMULATED moment, not about how long a player paused there.
    [[nodiscard]] std::int64_t caseNowSeconds() const noexcept;
    /// CASE WATCH. Appends one call-op to the attached tape, top-level calls
    /// only -- see setWatchRecorder. The depth counter is what "top-level"
    /// means: every recorded verb holds it raised for its own body (session.cpp,
    /// WatchDepthGuard), so a verb reached THROUGH another verb records nothing.
    void recordWatchOp(WatchOpKind kind, std::int32_t a = 0, bool ok = false);
    std::vector<WatchOp>* watchTape_ = nullptr;
    int watchDepth_ = 0;
    /// Runs the ward's day forward to the tavern's calendar. Called after every
    /// step and after every jump of the clock -- see the note on the definition
    /// for why the ward could not previously see a slept night.
    void syncWardToCalendar();
    /// TIME-AND-TENURE BUILD -- the shared tail of every clock jump: pulls
    /// timeOfDay_ off the tavern, resets the step counter and runs the ward
    /// and the population forward. skipToHour(), settleSleep() and the Wait
    /// page's sleep pick all end here, so a fourth jump can never forget the
    /// calendar the way the S7 review's eighth finding did.
    void syncClockAfterSkip();
    /// TIME-AND-TENURE BUILD. Tells the director what ground the feet are on
    /// (plotIndexUnderfoot against the live roll) AND whether the roll
    /// carries a vacant charge to petition for, before a conversation opens
    /// and again after a petition settles. The one writer of
    /// DialogueDirector::setGroundPlot and setVacantCharge.
    void syncGroundPlot();
    /// The priest's reading of the roll for the plot underfoot -- name,
    /// tenure, who holds it, and what a vacant charge asks. Composed here
    /// because the roll is the render session's borrowed ward, exactly the
    /// Buy contract: the director declared the intent, whoever owns the
    /// counter fills in the line.
    [[nodiscard]] std::string groundRollLine() const;
    /// Settles TopicKind::Petition through the REAL Ward::petitionForCharge
    /// against the roll's first vacant charge -- purse synced both ways,
    /// ground context refreshed -- and answers with the line the priest
    /// says. The verb's first caller.
    [[nodiscard]] std::string settleGroundPetition();
    void say(std::string line);
    /// Charges a landing to the body: the skill, the guild's teaching, the hit
    /// points and the roof-run tally, in the one place a landing is resolved.
    void settleLanding(const sim::RoofResult& move);
    /// "WANTED  HEAT 62  LOOT 3", or empty when the ward has heard nothing.
    /// JUSTICE BUILD: "WANTED FOR BLOOD  HEAT 60" while the paper has a
    /// corpse behind it, "CONDEMNED  HEAT 12" after a commutation. This row
    /// is the criminal tag's one presentation, so it is reachable to be
    /// proved (test_court.cpp) the way unlockedLetters() is.
public:
    [[nodiscard]] std::string heatLine() const;

private:
    /// "THE GULL  14 IN  BUSY", or empty when the player is not inside.
    /// Pulled out of drawFrame() into its own Line() method, the same shape
    /// every other bottom-band row already had, so syncPanelAnim() can call
    /// it too -- see that method's own header on why it now needs to.
    [[nodiscard]] std::string roomLine() const;

    // --- task #83: panel and prompt easing -----------------------------------
    //
    // ONE WIDGET, ONE ANIMATION. dialogueView() already draws the
    // conversation, the casebook, the keys page, options, the pause menu, the
    // character sheet, (#82) the district map and (#82) the letters through
    // the identical DialogueViewState -- see that struct's own header -- so
    // the one moment any of the eight is open or closed is the one moment
    // this build has a "panel" at all, and one EasedToggle is what every one
    // of them eases through.
    /// True while ANY of the eight pages the panel widget draws is up. The one
    /// formula drawFrame() and syncPanelAnim() both read, so the two can never
    /// quietly disagree about what "conversing" means.
    [[nodiscard]] bool conversingNow() const noexcept;
    /// Re-reads conversingNow() and pushes it at panelAnim_, and re-reads
    /// whatever the HUD's alert would currently be showing and pushes THAT at
    /// alertAnim_. Called at the end of every verb that can open or close one
    /// of the eight pages or start or clear a message, so the very first frame
    /// drawn after a keypress already carries visible motion rather than
    /// waiting for the next step() to catch up -- see EasedToggle::setTarget's
    /// own note on why opening from rest is never exactly zero.
    ///
    /// HARDENING PASS. ALSO RE-READS EVERY OTHER HUD ROW hud.hpp:178-187 named
    /// as still snapping instead of easing -- the interact prompt, the lock
    /// line, the case banner, the room label, the rival/guild/objective lines
    /// and the stealth readout -- and pushes each at its OWN EasedToggle
    /// (interactAnim_ etc., below), the same reasoning panelAnim_ and
    /// alertAnim_ already argue for: sharing one toggle across rows that
    /// appear and disappear on unrelated triggers would make one row's fade
    /// restart every time an unrelated row changed. Folded into this
    /// function, not a second one with its own call sites, so every place
    /// that already calls syncPanelAnim() to catch a change the simulation
    /// made (not a keypress) keeps every row honest for free.
    void syncPanelAnim() noexcept;

    SessionConfig config_;
    /// See setHudStandDown. Frame-scoped: the client sets it every frame.
    bool hudStandDown_ = false;
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
    /// The district's population, also owned by the engine and borrowed here.
    sim::WardPopulation* people_ = nullptr;
    /// The figures the ward is drawn with, read once at boot.
    ActorSheet actorSheet_;
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
    /// The line the top band was last asked to speak, and how many movement
    /// steps it has had to arrive. Drives the typewriter reveal in
    /// DialogueViewState::speechRevealChars -- NOT simulation state,
    /// deliberately not hashed, for the identical reason wardTalkId_ is not:
    /// it is a courtesy to whoever is watching the screen live and carries no
    /// fact about the world. A test that wants the line whole steps a few
    /// times, exactly as it already does to let messageSteps_ run out.
    std::string lastSpokenLine_;
    std::int32_t speechAgeSteps_ = 0;
    /// How many movement steps the reveal takes to finish -- about a third of
    /// a second at kStepsPerSecond, fast enough that a scripted capture a
    /// handful of steps later reads the line as fully arrived.
    static constexpr std::int32_t kSpeechRevealSteps = 20;
    /// Which topic the cursor is on, and what number the player is about to
    /// name across a counter. Both are pure UI state -- the standing, the
    /// prices and the memory all live in the simulation.
    int topicCursor_ = 0;
    /// Which page of a long topic list is showing. Pure UI state: the list
    /// itself is the simulation's and paging never reorders it.
    int topicPage_ = 0;
    int haggleOffer_ = 0;
    /// Which body in the WARD the open conversation is with, or -1 for none --
    /// which includes every conversation held inside the Gilded Gull, because
    /// the taproom keeps its own. NOT simulation state and deliberately not
    /// hashed: it decides which of two settlements a reply is routed through
    /// and nothing about the world. The director's own `open` flag is the fact
    /// that a conversation exists, and the twin-run gate already covers that.
    std::int32_t wardTalkId_ = -1;
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
    /// S10. The trail, its authored file, and which page of the notes is open.
    /// The raws are held because Casebook borrows them for its whole life.
    sim::CasebookRaws caseRaws_;
    sim::Casebook casebook_;
    /// MORROWIND ROUND. THE ONE FLAG THAT OPENS AND CLOSES THE TILED MENU.
    /// casebookOpen()/characterOpen()/mapOpen()/lettersOpen() all read this
    /// SAME bool now -- the four tiles are drawn simultaneously (see "ONE
    /// MENU, FOUR TILES" up in the public section), so there is no longer a
    /// separate "is this one open" per tile, only menuFocus_ below to say
    /// which one currently has the keyboard.
    bool casebookOpen_ = false;
    /// TASK #82. The authored letters. No per-letter "has this been read"
    /// flag lives here: a letter's visibility is DERIVED, every call, off
    /// whether the casebook lead it is tied to has actually been
    /// investigated -- see unlockedLetters(). That is one less thing to hash
    /// and one less thing that could disagree with the book that gates it.
    sim::LetterRaws letterRaws_;
    /// COURIER CASE. The second case's raws, book and paper, plus the whole
    /// of the errand's own presentation state. ALL SESSION-SIDE AND NONE OF
    /// IT HASHED: the twin gate's workloads never construct a Session (the
    /// pinned population baseline hashes WardPopulation and the bare engine
    /// only), sheetBook_.hashInto has no caller, and the tavern is only ever
    /// driven through the same public verbs a keypress drives. Determinism
    /// is proven the scripted way instead -- test_case_line runs the errand
    /// twice and requires identical state.
    sim::CasebookRaws sheetRaws_;
    sim::Casebook sheetBook_;
    sim::LetterRaws sheetLetterRaws_;
    /// EVICTION CASE. The third case's raws, book and paper, held to the
    /// sheet trio's exact contract above: ALL SESSION-SIDE AND NONE OF IT
    /// HASHED -- the twin gate's workloads never construct a Session, and
    /// determinism is proven the scripted way (test_eviction_line runs both
    /// paths twice and requires identical books).
    sim::CasebookRaws evictRaws_;
    sim::Casebook evictBook_;
    sim::LetterRaws evictLetterRaws_;
    /// True from the paper changing hands at the door until the Mission
    /// close -- sheetCarry_'s sibling, worn by the objective row, completed
    /// by stepEvictCase().
    bool writServed_ = false;
    /// Tonight's knock: false until the door answers, cleared when the
    /// serve lands or the evening gate refuses. The serve press is only
    /// offered on an answered door.
    bool evictKnocked_ = false;
    /// The record's copy of the knock: set with the first answered knock and
    /// never cleared, whatever the live flag does when the player steps away.
    bool evictEverKnocked_ = false;
    /// The one-per-arming nudge naming the door verbs -- sheetTakeSaid_'s
    /// sibling, re-armed when the player steps out of reach.
    bool evictDoorSaid_ = false;
    /// Where the courier beat has got to (0 unfired / 1 hailed / 2 said the
    /// follow-up), and the countdown of world-steps before it fires. The
    /// countdown starts once the opening page is down (firstRun_ false) and
    /// only ticks while the world has the keys -- never under a menu, a
    /// conversation or a lock.
    int courierStage_ = 0;
    int courierSteps_ = 0;
    /// True from TAKE HIM UP until the Mission's back room. The bale's own
    /// shape: a flag the HUD wears, not a body the renderer carries -- the
    /// slung-over-the-shoulder drawing is flagged follow-up work, and the
    /// heat row's "CARRYING FINCH" is the honest interim. TRAVEL lane also
    /// reads this: fast travel refuses while it is set (travelRefusal), so
    /// the nervous walk to the Mission cannot be skipped.
    bool sheetCarry_ = false;
    /// The one-per-downing nudge that names the take verb, re-armed when the
    /// quarry is back on his feet -- see stepSheetCase().
    bool sheetTakeSaid_ = false;
    /// COURIER CASE, the whole errand's moving parts. stepSheetCase() is the
    /// per-step machine (courier countdown, the take nudge, the delivery);
    /// caseTakeReady()/sheetQuarry() answer whether TAKE HIM UP is the verb
    /// right now (case live past the snug lead, Finch present and Downed and
    /// in reach, nothing already in hand); sheetLeadInLookReach() is
    /// leadInLookReach()'s exact walk over the second book, so the crosshair
    /// can name the errand's own sites; activeCaseRaws()/activeCasebook()
    /// are the one case-switching rule (sheetCaseLive()) applied everywhere
    /// a page, a row or the HUD reads "the case".
    void stepSheetCase();
    [[nodiscard]] bool caseTakeReady() const;
    [[nodiscard]] const sim::Actor* sheetQuarry() const;
    [[nodiscard]] int sheetLeadInLookReach() const;
    /// EVICTION CASE, the errand's own moving parts, each the courier
    /// machinery's exact sibling: stepEvictCase() is the per-step machine
    /// (the return-to-Mission close and the Evictor grant); evictDoorReady()
    /// answers whether the knock/serve press is the verb right now (case
    /// live, writ unserved, within reach of the family-door lead's own
    /// site); evictLeadInLookReach() is sheetLeadInLookReach()'s walk over
    /// the third book; syncEvictionTopics() feeds the dialogue director the
    /// case's stage (the setGroundPlot contract) so Maell's writ topics
    /// appear and retire honestly; settleTakeWrit()/settleYieldWrit() are
    /// the session halves of the two intent replies.
    void stepEvictCase();
    [[nodiscard]] bool evictDoorReady() const;
    [[nodiscard]] int evictLeadInLookReach() const;
    void syncEvictionTopics();
    void settleTakeWrit();
    void settleYieldWrit();
    /// EVICTION CASE. THE REWARD SEAM, and the one place in this build that
    /// arms the player at all: the agreed weapon id is "the_evictor", the
    /// weapon itself is the weapon lane's to define. Until that lane's enum
    /// lands, this grants the class the brawl already has on the Evictor's
    /// own side of the lethal line -- Blunt, Subdue intent (the player's
    /// default, so nothing meaningful is stomped) -- and the integrate step
    /// is one identifier swap HERE and nowhere else.
    void grantEvictor();
    /// COURIER CASE + EVICTION CASE: the one case-switching rule, applied
    /// everywhere a page, a row or the HUD reads "the case". THREE books
    /// now: the eviction fronts while it lives (it begins by an explicit
    /// deed mid-play, so it is always the newest thing the player did),
    /// then the courier's errand, then the Bloodletter. Still fixed members
    /// in fixed declaration order, still no switcher UI -- the same flagged
    /// scope cut, one book deeper.
    [[nodiscard]] const sim::CasebookRaws& activeCaseRaws() const noexcept {
        return evictCaseLive() ? evictRaws_ : (sheetCaseLive() ? sheetRaws_ : caseRaws_);
    }
    [[nodiscard]] const sim::Casebook& activeCasebook() const noexcept {
        return evictCaseLive() ? evictBook_ : (sheetCaseLive() ? sheetBook_ : casebook_);
    }
    bool keysOpen_ = false;
    /// SPELLS BUILD. The Grimoire page: whether it is up, which crafting the
    /// cursor is on, and which page of a long list is showing. The same
    /// overlay family keysOpen_ belongs to: one flag, one surface, every
    /// other overlay stands down when it opens.
    bool grimoireOpen_ = false;
    int grimoireCursor_ = 0;
    int grimoirePage_ = 0;
    bool firstRun_ = true;
    /// THE JOURNAL TILE'S OWN CURSOR, PAGE AND PICKED ENTRY -- unchanged
    /// names and unchanged meaning from #85: which lead the cursor is on,
    /// which page of a long list is showing, and which entry (if any) the
    /// player has picked to read in full. No longer shared with Character,
    /// Map or Letters -- each tile keeps its own now that all four are drawn
    /// (and can each be mid-read) at once; see the four fields below.
    int caseCursor_ = 0;
    int casePage_ = 0;
    int caseEntry_ = -1;
    /// THE CASEBOOK PASS. The COMPOSED PAGE's own cursor and view -- kept apart
    /// from caseCursor_/casePage_ above for the reason casebookLeadCursor()
    /// states: the tile's list is longer than the page's, and one cursor over
    /// two lists of different lengths is a cursor that can select a row nothing
    /// draws. Both are pure UI state and neither is hashed.
    ///
    /// KEPT BETWEEN OPENINGS, exactly as districtMapSelected_ is, so a book put
    /// down on the ledger opens on the ledger.
    int casePageCursor_ = 0;
    CasebookTab casebookTab_ = CasebookTab::Leads;
    /// THE CASEBOOK PASS. The lead-opened notice: what it says, how many steps
    /// it is still wanted for, and its own ease. The threshold plate's exact
    /// three fields (placePlateName_/placePlateShowSteps_/placePlateAnim_) for
    /// the identical reason -- an EVENT is a countdown plus a toggle, and the
    /// string is held through the fade so the notice ends with its own words.
    std::string casePlateText_;
    int casePlateShowSteps_ = 0;
    /// UI-EA (LANE HUD): THE ONE NOTICE GRAMMAR -- "<NEWS> - <key>", the
    /// spec's own "3 NEW LEADS - J". The news in the ward's words, a dash,
    /// the live key that opens the book, through promptLabel so a pad reads
    /// its own button. "YOUR CASEBOOK"/"YOUR LETTERS" died in the diet: the
    /// key IS the pointer, and every notice ends at the same door (the Menu).
    /// One place builds it so six announcement sites cannot drift apart.
    void armCasePlate(std::string news);
    /// THE CHARACTER TILE'S OWN CURSOR AND PAGE. Read-only (nothing on this
    /// tile is a choice to make), so there is no "entry" to remember.
    int characterCursor_ = 0;
    int characterPage_ = 0;
    /// THE MAP TILE'S OWN CURSOR AND PAGE. Also read-only.
    int mapCursor_ = 0;
    int mapPage_ = 0;
    /// THE LETTERS TILE'S OWN CURSOR, PAGE AND PICKED ENTRY -- the title
    /// list's own state, mirroring the Journal tile's caseCursor_/casePage_/
    /// caseEntry_ exactly. lettersBodyPage_ is a FIFTH field the other three
    /// tiles have no equivalent of: once a letter is picked (lettersEntry_
    /// >= 0) the tile stops showing a list at all and starts showing that
    /// letter's own wrapped, paged prose, and this is which page of IT is
    /// showing -- kept apart from lettersPage_ rather than reusing it and
    /// having it change meaning mid-flight, which is what #82's original,
    /// single-panel version of this page did to casePage_ and warned its own
    /// callers about in the header letters kept while it was one of six
    /// pages rather than a tile.
    int lettersCursor_ = 0;
    int lettersPage_ = 0;
    int lettersEntry_ = -1;
    int lettersBodyPage_ = 0;
    /// MORROWIND ROUND. Which of the tiled Menu's four tiles currently has
    /// input focus -- see menuFocus()'s own header. Defaults to the Journal
    /// tile, the same tile #85's Menu always opened on first, so a caller
    /// that opens the Menu and never touches PagePrev/PageNext lands on
    /// exactly the tile it always landed on.
    int menuFocus_ = kMenuFocusJournal;
    /// #77. The live bindings, the options page and the row it is on.
    ControlSettings controls_ = ControlSettings::defaults();
    /// Ship note move 3. Which device last pressed something -- see
    /// promptDevice(). CLIENT state: never hashed, never fed to MoveInput,
    /// and the keyboard vocabulary is the shipped default, so every caller
    /// that never heard of it (every test, every capture flag) draws the
    /// exact frames it always drew.
    InputDevice promptDevice_ = InputDevice::KeyboardMouse;
    /// Contract (c)'s wake edge -- see noteTutorWake(). Client state.
    std::uint32_t tutorWakeSerial_ = 0;
    bool optionsOpen_ = false;
    int optionCursor_ = 0;
    int optionPage_ = 0;
    bool awaitingKey_ = false;
    int quickSlot_ = 0;
    /// The pause menu: whether it is up, which row the cursor is on, whether
    /// QUIT is one press from happening, and whether it has landed. The last
    /// two are separate on purpose -- see quitArmed()/quitRequested().
    bool pauseOpen_ = false;
    int pauseCursor_ = 0;
    bool quitArmed_ = false;
    bool quitRequested_ = false;
    /// TIME-AND-TENURE BUILD. The Wait page: whether it is up, whether it is
    /// the rented bed's healing door (sleep mode) or the pause row's plain
    /// one, and its own cursor/page -- the Grimoire page's exact shape. UI
    /// state, not simulation state, and not hashed, like every page flag
    /// here: what it DOES on a pick (skipToHour/sleepUntil) is simulation
    /// and is hashed where it lives.
    bool waitOpen_ = false;
    bool waitSleep_ = false;
    int waitCursor_ = 0;
    int waitPage_ = 0;
    /// THE WARD MAP (core action #13). One flag, one full-screen page, every
    /// other overlay stands down when it opens -- the same overlay family
    /// grimoireOpen_ belongs to. UI state, never hashed: the page is a pure
    /// read of the world.
    bool districtMapOpen_ = false;
    /// The plan's own boot-time facts: per-material tones derived from the
    /// atlas, and the authored extent with the VOID border cropped away.
    /// Both computed once in the constructor -- see map_view.hpp.
    MapPalette mapPalette_;
    MapBounds mapBounds_;
    /// THE MAP PASS. The page's own cursor: which named place is selected,
    /// which view is over it, which rung of the zoom ladder, and where a
    /// scrolling tab has been paged to. UI state, never hashed, exactly like
    /// districtMapOpen_ above -- and deliberately KEPT between openings, so a
    /// map closed while reading the Weighhouse opens on the Weighhouse.
    int districtMapSelected_ = 0;
    MapTab districtMapTab_ = MapTab::Overview;
    int districtMapZoom_ = 0;
    int districtMapDetailFirst_ = 0;
    /// The named place nearest the body, for a stand that is on none of them.
    [[nodiscard]] int nearestDistrictMapPlace() const;

    /// Task #83. The panel widget's own open/close ease -- see
    /// conversingNow()/syncPanelAnim() -- and the HUD alert row's fade in and
    /// out. Render-adjacent bookkeeping and deliberately NOT simulation state:
    /// nothing here reaches PhasedEngine and nothing here is hashed, for the
    /// identical reason wardTalkId_ above is not -- it is a courtesy to
    /// whoever is watching the screen live and carries no fact about the
    /// world. See render::EasedToggle's own header on why it is driven in
    /// movement steps rather than draw calls, which is what keeps a captured
    /// frame reproducible.
    EasedToggle panelAnim_;
    EasedToggle alertAnim_;
    /// HARDENING PASS. hud.hpp:178-187's own gap: alertFade multiplied only
    /// the alert row, so every row below it still popped. One EasedToggle per
    /// row, not one shared, because the interact prompt, the lock line, the
    /// case banner, the room label, the rival/guild/objective lines and the
    /// stealth readout all appear and disappear independently of each other --
    /// see syncPanelAnim()'s own header. Advanced once a step, in step(),
    /// exactly like panelAnim_/alertAnim_ above.
    EasedToggle interactAnim_;
    EasedToggle lockAnim_;
    EasedToggle caseAnim_;
    EasedToggle roomAnim_;
    EasedToggle rivalAnim_;
    EasedToggle guildAnim_;
    EasedToggle objectiveAnim_;
    EasedToggle stealthAnim_;
    /// PLANNING SPRINT (item #2, the sweep). hud.hpp:73-105's own top-right
    /// stack had THREE rows still popping on/off with `conversing` -- see
    /// hudTopRightReserve()'s own comment in hud.cpp, which says so out loud:
    /// "Standing, heat, the sack and the stealth line all stand down for the
    /// length of a conversation." stealthLabel got its own EasedToggle
    /// (stealthAnim_ above) the same pass every bottom-left row did; these
    /// three did not, and a real sweep of this file for exactly the class of
    /// bug the prior two sprints fixed found them. Same shape, same reason:
    /// one EasedToggle per row, because a purse appearing has nothing to do
    /// with the ward's opinion of you appearing.
    EasedToggle standingAnim_;
    EasedToggle heatAnim_;
    EasedToggle stashAnim_;
    /// FIRST-PERSON COMBAT (S13). The two rows the Cast/Block task added,
    /// each with its OWN EasedToggle per the convention DECISIONS.md pinned:
    /// the equipped-crafting readout (top-right stack, under the sack) and
    /// the held-guard indicator (bottom band). A readied spell appearing has
    /// nothing to do with a guard going up, so they do not share one.
    EasedToggle spellAnim_;
    EasedToggle blockAnim_;
    /// ACTION-COMBAT BUILD. The "HELD HARD -- <weapon> <span>" charge row's own
    /// EasedToggle, on the bottom band adjacent to the guard row -- the same
    /// per-row convention. A guard going up and a swing charging hard are
    /// unrelated, so they do not share a toggle; and a charge is mutually
    /// exclusive with a guard by construction (the guard only holds in Idle),
    /// so the two rows never both want the band at once.
    EasedToggle chargeAnim_;
    /// STANCE & ROOM BUILD. The "<WEAPON> UP" fighting-mode row's own
    /// EasedToggle, the per-row convention: hands coming up has nothing to do
    /// with a guard going up (a guard RAISES the hands, but a swing raises
    /// them too and the guard does not follow), so they do not share one.
    EasedToggle handsAnim_;
    /// FATIGUE BUILD. The fatigue bar's own visibility ease -- its OWN
    /// EasedToggle per the pinned convention, mirroring the health bar's one
    /// visibility rule (down for the length of a conversation, up otherwise)
    /// without borrowing the health bar's snap: the bar eases out when a
    /// panel takes the bottom band and eases back when it is returned.
    /// Snapped fully open at construction, because the session boots with a
    /// rested body and nothing to fade in from.
    EasedToggle fatigueAnim_;
    /// THE WARD MAP's own open/close ease -- its OWN toggle per the settled
    /// convention (DECISIONS.md UI rule 1), driven from syncPanelAnim() and
    /// advanced in step() exactly like every sibling above.
    EasedToggle districtMapAnim_;
    /// FAST TRAVEL's arrival seam (TRAVEL lane). The owner called the demo's
    /// raw placeAt cuts "teleporting", so a travel does not snap: the commit
    /// SNAPS this toggle fully open -- the first frame after the press is
    /// already black, so the origin is never seen again -- and eases it back
    /// down over half a second while the destination, its place plate and the
    /// arrival line come up underneath. drawFrame() dips its finished
    /// composition by value() as its last act on every path. NEW RENDER
    /// VOCABULARY -- no full-screen fade existed anywhere in the build before
    /// this -- flagged for the owner's eye. Render-only, hash-free, advanced
    /// in step() like every sibling.
    static constexpr int kTravelFadeSteps = 36;
    EasedToggle travelFadeAnim_{8, kTravelFadeSteps};
    /// SPELLS BUILD. The bottom-centre quick bar strip's own ease -- per the
    /// pinned convention, its OWN toggle: the strip appearing (a wheel held,
    /// a slot picked) has nothing to do with any other row's trigger. The
    /// countdown is what keeps it up for a couple of seconds after the last
    /// touch; the names are cached here so the strip can finish fading with
    /// its labels still on it, the identical reason every *Cache_ below
    /// exists.
    EasedToggle quickBarAnim_;
    /// Two seconds at the 60 Hz step cadence -- long enough to read, short
    /// enough that the strip never becomes furniture.
    static constexpr int kQuickBarShowSteps = 120;
    int quickBarShowSteps_ = 0;
    std::array<std::string, 10> quickBarNames_{};
    /// UI-EA (LANE HUD): THE LAW OF EARNED TEXT's own state. One wake
    /// countdown per reference row (the quickBarShowSteps_ shape, one each,
    /// because a purse waking has nothing to do with a room waking), the two
    /// toggles the rows that draw live numbers need (clock, purse -- no
    /// cache, numbers do not go away mid-fade), and the last-seen values the
    /// edges are detected against. hudEdgesSeeded_ is lastPlaceName_'s own
    /// reasoning for the whole family: the first syncPanelAnim() call seeds
    /// every last-value and arms nothing, so a session cannot boot with its
    /// corner rows all announcing themselves. EVENT tier holds
    /// kPlateHoldSteps (~2.5s) -- hud.hpp's shared constant, named once per
    /// the spec's own rule. All render-side, none of it hashed.
    static constexpr int kHudWakeSteps = kPlateHoldSteps;
    EasedToggle clockAnim_;
    EasedToggle purseAnim_;
    int clockShowSteps_ = 0;
    int purseShowSteps_ = 0;
    int caseShowSteps_ = 0;
    int roomShowSteps_ = 0;
    int guildShowSteps_ = 0;
    int objectiveShowSteps_ = 0;
    bool hudEdgesSeeded_ = false;
    int lastClockHour_ = 0;
    int lastClockTod_ = 0;
    std::int32_t lastCoinSeen_ = 0;
    std::string lastCaseSeen_;
    std::string lastRoomSeen_;
    std::string lastGuildSeen_;
    std::string lastObjectiveSeen_;
    bool lastBookOpen_ = false;
    /// UI-EA (LANE HUD): THE QUICK BAR'S TUTOR TOAST. Nine and the sticks
    /// cut the QuickWheel hold, so what is taught is the STEP -- "WHEEL -
    /// STEP", through promptLabel so a pad names its own D-pad halves and a
    /// rebind re-words it, raised on the quick bar's first TWO risings ever and
    /// riding the strip's own countdown, then retired for the session. Two
    /// exposures because one can land while the player is looking at the
    /// street, and a third is nagging. The band itself is hud.hpp's
    /// TutorBand -- the countdown/toggle helper the cross-lane contract has
    /// this lane land, used here first so the shape PAGES instantiates per
    /// band and FLOW wakes is a shape that demonstrably works.
    static constexpr int kWheelHintShows = 2;
    int wheelHintShows_ = 0;
    bool lastQuickBarUp_ = false;
    std::string wheelHintText_;
    TutorBand wheelHint_;
    /// UI-EA contract (c), joined at integration: one tutor band per composed
    /// page this Session itself feeds (the ward map, the casebook page, the
    /// keys page -- creation drives its own, see CreationFlow::tutorValue()).
    /// PAGES draws the raised verb words off each state's `tutor`; FLOW bumps
    /// tutorWakeSerial_ on device change and unrecognized presses; step()
    /// raises these on the page's own open edge and on any serial move, and
    /// advances them once a step like every countdown in the family.
    TutorBand mapTutor_;
    TutorBand casebookTutor_;
    TutorBand keysTutor_;
    std::uint32_t tutorWakeSeen_ = 0;
    bool mapTutorWasOpen_ = false;
    bool casebookTutorWasOpen_ = false;
    bool keysTutorWasOpen_ = false;
    /// DISTRICT PHASE D. The threshold plate's own ease -- its OWN toggle per
    /// the settled convention (DECISIONS.md UI rule 1): crossing a boundary
    /// has nothing to do with any other row's trigger, and sharing one would
    /// restart somebody else's fade every time the player walked round a
    /// corner.
    ///
    /// AN EasedToggle AND NOT AN ImpactPulse, deliberately, and the brief said
    /// so. A crossing IS an event -- which is exactly what ImpactPulse is for
    /// -- but what the plate does is not a flash: it is a notice that comes up,
    /// STAYS UP long enough to be read, and then goes. That is a held state
    /// with a countdown holding it, which is what the quick bar strip right
    /// above already is, so it is built the way the strip is built.
    EasedToggle placePlateAnim_;
    /// Two seconds at the 60 Hz step cadence, the strip's own number and for
    /// the strip's own reason: long enough to read a place name, short enough
    /// that it can never be mistaken for furniture. The brief's "a couple of
    /// seconds", in the one unit this engine measures animation in (steps, not
    /// milliseconds -- anim.hpp's header on why).
    static constexpr int kPlacePlateShowSteps = 120;
    int placePlateShowSteps_ = 0;
    /// THE CASEBOOK PASS. The lead-opened notice's own ease, built exactly the
    /// way placePlateAnim_ above is built and for exactly its reasons -- a held
    /// state with a countdown holding it, on its OWN toggle because a lead
    /// opening has nothing to do with a boundary being crossed.
    EasedToggle casePlateAnim_;
    /// THREE seconds and not the plate's two. This notice carries a COUNT and a
    /// key to press, which is two facts rather than one name, and it is the
    /// thing the whole pass exists to make unmissable -- so it is given the
    /// extra second and nothing else. It is still a notice that leaves.
    static constexpr int kCasePlateShowSteps = 180;
    /// What the plate says, cached on the FIRING edge -- see placePlateLabel().
    std::string placePlateName_;
    /// The last named place the body stood in -- see lastPlaceName().
    std::string lastPlaceName_;
    /// The last non-empty text each row above showed, held onto through the
    /// row's own fade-out -- the identical reason message_ outlives
    /// messageSteps_ (see step()'s own note by the alert's clear): an alpha
    /// cannot fade a string that is already gone. Cleared once its toggle has
    /// actually finished easing to closed, not the instant the row's own
    /// condition goes false -- see step()'s clearIfClosed.
    std::string interactCache_;
    /// THE CROSSHAIR PASS. The other three parts of the aim prompt, cached on
    /// the identical edge and cleared by the identical rule -- see
    /// interactCache_ directly above, which is now the VERB alone. Three
    /// strings rather than one composed row because the HUD colours them by
    /// three different roles, and a pre-joined string cannot be un-joined
    /// without the renderer parsing what Session wrote, which is exactly the
    /// kind of second description of a layout the panel pass banned.
    std::string interactSubjectCache_;
    std::string interactNoteCache_;
    AimKind interactKindCache_ = AimKind::Nothing;
    std::string lockCache_;
    std::string caseCache_;
    std::string roomCache_;
    std::string rivalCache_;
    std::string guildCache_;
    std::string objectiveCache_;
    std::string stealthCache_;
    std::string standingCache_;
    std::string heatCache_;
    std::string stashCache_;
    std::string spellCache_;
    std::string blockCache_;
    /// ACTION-COMBAT BUILD. The charge row's cache, filled while the swing is
    /// held hard and kept through the fade-out the identical way blockCache_ is
    /// -- an alpha cannot fade a string that is already gone.
    std::string chargeCache_;
    /// STANCE & ROOM BUILD. The fighting-mode row's cache, kept through the
    /// fade-out the identical way blockCache_ is.
    std::string handsCache_;
    /// HELD-EFFECTS BUILD. One toggle and one cache PER ROW, the pinned
    /// convention: a warmth lapsing has nothing to do with a tuning arriving,
    /// so slot i eases on its own. Slots are table order (oldest hold first);
    /// when a hold lapses the rows above it shift down and each slot's toggle
    /// eases toward its slot's new truth.
    // No brace-initializer: EasedToggle's defaulted-argument constructor is
    // explicit, so `{}` on the array would be a -Werror conversion under
    // GCC; default-initialization calls the same constructor and says so.
    std::array<EasedToggle, kEffectRows> effectAnims_;
    std::array<std::string, kEffectRows> effectCaches_{};

    /// INNOVATION SPRINT ITEM #2. Which of the tiled Menu's four tiles is
    /// easing toward or away from input focus, one EasedToggle a tile --
    /// see MenuTileState::characterFocus's own header on why each tile gets
    /// its own instance rather than one shared value. SHORT rise/fall
    /// (kMenuFocusRiseFallSteps below), not EasedToggle's own default eight:
    /// a focus swap has to read as "quick, with a little give", not as the
    /// same leisurely open/close a whole panel gets -- see the constructor.
    /// Re-targeted every syncPanelAnim() call (menuFocus_ is re-read there
    /// exactly like every other row this pass drives), so a caller that
    /// steps focus without going through toggleMenuFocused() (menuPageNext/
    /// menuPagePrev) still gets a fresh target the moment step() next runs
    /// syncPanelAnim(), the same "catches a change even without an explicit
    /// call" guarantee syncPanelAnim()'s own header already promises for
    /// every other row.
    static constexpr std::int32_t kMenuFocusRiseFallSteps = 4;
    EasedToggle characterFocusAnim_{kMenuFocusRiseFallSteps, kMenuFocusRiseFallSteps};
    EasedToggle mapFocusAnim_{kMenuFocusRiseFallSteps, kMenuFocusRiseFallSteps};
    EasedToggle lettersFocusAnim_{kMenuFocusRiseFallSteps, kMenuFocusRiseFallSteps};
    EasedToggle journalFocusAnim_{kMenuFocusRiseFallSteps, kMenuFocusRiseFallSteps};
    // All four are snapped in the constructor body to whatever
    // syncPanelAnim() computes their real starting target to be (open on
    // Menu tile is not itself a bump-worthy "was closed, is now open" -- see
    // the constructor's own note for why journalFocusAnim_ specifically
    // needs the real answer rather than an assumed one).

    /// UI-EA-SPEC sec. 3 rule 4 -- CLOSE HONESTY: a page fades as what it
    /// WAS. drawFrame() remembers, per frame, whether the surface it drew
    /// the shared panel fade for was the tiled Menu, so the few frames of
    /// close tail after casebookOpen_ goes false can keep drawing the tiles
    /// easing out instead of the empty single panel the close used to hand
    /// over to (the seam session.cpp:7455's own comment admitted).
    /// `mutable` because drawFrame() is const and this is a memo about what
    /// drawFrame itself just drew -- written and read nowhere else, so it
    /// cannot desynchronize anything; a run's frame sequence is a pure
    /// function of its steps and draws exactly as before. Never hashed.
    mutable bool panelTailTiles_ = false;
    /// JUSTICE BUILD: the same memo for the hearing page's close tail, so
    /// the bench fades as the bench (UI-EA-SPEC sec. 3 rule 4) rather than
    /// as the empty single panel the dialogue path would otherwise draw.
    mutable bool panelTailCourt_ = false;

    // --- INNOVATION SPRINT ITEM #3: some impact, tastefully -----------------
    //
    // Two real moments this build had never given any physical weight to --
    // see render::ImpactPulse's own header on why an EVENT (happens once, at
    // an instant) is the right shape and EasedToggle (a held STATE) is not.
    // Both render-adjacent, deliberately not hashed, for the identical reason
    // panelAnim_ above is not: neither carries a fact about the world, only a
    // courtesy to whoever is watching the screen live.

    /// A brawl finally has SOME weight: a brief flash the instant the
    /// player's own punch connects (Session::punch()), and a second, separate
    /// instance for a blow landing ON the player (step(), off a drop in
    /// tavern_->playerHp() -- see that call site's own note on why a
    /// comparison and not a new sim-side flag). Two instances, not one,
    /// because a mutual exchange can trigger both in the same step and each
    /// needs to run its own decay without restarting the other's.
    ImpactPulse punchLandedPulse_;
    ImpactPulse punchTakenPulse_;
    /// FIRST-PERSON COMBAT (S13). A blow the GUARD caught -- told apart from
    /// an unguarded hit by tavern_->blowsBlocked() moving (the same
    /// comparison-not-flag shape lastPlayerHp_ uses, one line below), and
    /// drawn as its own cooler, quieter wash INSTEAD of punchTakenPulse_'s
    /// blooded one for that step: the guard working should read different
    /// from the guard failing, and two washes for one blow would be the
    /// scattershot the restraint note forbids. Not hashed, same reason as
    /// its two siblings.
    ImpactPulse blockPulse_;
    std::int32_t lastBlowsBlocked_ = 0;
    /// STREET PANIC BUILD (feel/build, 9a). What the room looked like last
    /// step, so step() can tell the street what just happened to it: the sum
    /// of every non-vermin hp on the tavern roster (a drop under lethal rules
    /// is a landed blow) and how many of them are corpses (a rise is a
    /// killing). The identical comparison-not-flag shape lastPlayerHp_ keeps
    /// for the player's own body, and for the identical reason: nothing new
    /// reaches into the simulation and nothing here is hashed. -1 until the
    /// first step has read the room, so boot never reads as a blow.
    std::int32_t lastRoomHpForAlarm_ = -1;
    std::int32_t lastCorpsesForAlarm_ = -1;
    /// BARKS LANE (feel/build). The last halt / join / panic line step() put
    /// on the alert row, so a line the sim composed once is said once: the
    /// same comparison-not-flag shape as the two latches above. Unhashed.
    std::string lastDemandSpoken_;
    std::string lastJoinSpoken_;
    std::string lastFleeSpoken_;
    /// ACTION-COMBAT BUILD (section 5, channel 5). THE CAMERA IMPULSE, composed
    /// as a render-only BAM offset inside Session::camera() -- never written to
    /// sim yaw. Three events, three pulses: a HARD swing's own forward dip on
    /// release (this one, triggered in attackUp), the taken-jolt (punchTakenPulse_
    /// above) and the block-nudge (blockPulse_ above) reused, since those already
    /// fire on exactly their events. Decays over kPageEaseSteps like its siblings;
    /// its peak angle is capped in camera() well under the spec's <=2deg. Not
    /// hashed -- a camera impulse is a courtesy to the eye, like the washes.
    ImpactPulse hardSwingDipPulse_;
    /// 3D BUILD, V LANE. THE HANDS. The machine is stepped in step() beside
    /// the pulses above, fed the room's charge/guard getters plus the two
    /// edges below, which attackUp() and castEquipped() latch for the very
    /// next step (the edges arrive between steps, from input; the machine
    /// only moves on a step). Not hashed, same reason as its siblings: what
    /// the hands look like is a courtesy to the eye. See viewmodel().
    ViewmodelMachine viewmodel_;
    /// 0 = no swing released since the last step, 1 = light, 2 = hard.
    std::uint8_t viewmodelSwingPending_ = 0;
    /// A cast was ATTEMPTED since the last step (a fizzle counts, a refusal
    /// does not -- the hand only moves when the room let it try).
    bool viewmodelCastPending_ = false;
    /// Whether the Block key is physically down, straight off the client's
    /// edge events. Render-side bookkeeping, NOT the fact the sim hashes --
    /// step() derives that (held AND not talking/picking) and pushes it into
    /// the room every step, so a guard raised before a menu opened cannot
    /// stay silently raised underneath it.
    bool blockHeld_ = false;
    /// FATIGUE BUILD: whether the sprint-refusal line has been said for the
    /// CURRENT stretch of windedness. Edge-latched, cleared the moment the
    /// pool recovers: a refusal on the alert row once per exhaustion is
    /// feedback, sixty a second is a strobe -- the exact restraint
    /// mantleToward's own "silently, because..." note pinned for the climb.
    /// Render-side bookkeeping, not hashed: the REFUSAL ITSELF is sim state
    /// (the gated sprint never reaches the body), this only remembers
    /// whether it was announced.
    bool windedSprintSaid_ = false;
    /// Last step's own hit points, purely so step() can tell "the player was
    /// just hit" apart from every OTHER reason playerHp() could differ from
    /// one frame to the next (there is only the one today, but comparing
    /// rather than assuming keeps this honest if a second one is ever added).
    /// Initialised from the real starting hp in the constructor, not 100,
    /// so a session that boots the player already hurt does not read as
    /// having just been struck on its very first frame.
    std::int32_t lastPlayerHp_ = 0;

    /// UI-EA-SPEC sec. 3 rule 5, cross-lane contract (b): THE COMMIT BEAT.
    /// One restrained ImpactPulse for every page commit -- FACE IT, TRAVEL,
    /// GO TO IT, REBIND, a wait row, a grimoire row, the quit-confirm --
    /// triggered by the client at commit ROUTING (route_menu_key and
    /// session_pointer's click commits, main.cpp), decayed here beside its
    /// pulse siblings, and read back by the page drawers on the inverted
    /// fill (PAGES' half of the contract). CreationFlow::commitPulse_ is
    /// the same idea on the first window; this is the world's copy. One
    /// instance for all pages, deliberately: at most one page owns the
    /// input, so at most one commit can land per press, and per-page
    /// instances would be state for a collision that cannot happen.
    /// Render-side, never hashed.
    ImpactPulse commitPulse_;

    /// UI-EA-SPEC sec. 4 violation #4: WHO OPENED THE PAGE. True while the
    /// Keys, Options or Wait page on screen was entered from the pause menu
    /// (its CONTROLS/SETTINGS/WAIT rows) rather than by a direct shortcut,
    /// re-derived at every open -- toggleKeys()/toggleOptions()/openWait()
    /// set it from "was the pause menu up when I opened", and the keys<->
    /// options sibling-tab swap carries it across (either of the pair still
    /// owes its close to the same opener). closeConversation() reads it:
    /// back returns to the OPENER -- the pause menu, cursor on the row that
    /// opened the page, or the street for F1/F2 -- instead of always
    /// skipping to the street. Render-side flow state, never hashed.
    bool pageOpenedFromPause_ = false;

    /// A bouncer's warning finally lands with a little weight: the alert
    /// row's own legibility plate (drawTextPlate, this same sprint) briefly
    /// overshoots its settled size and eases back down the instant a NEW
    /// warning arrives -- see syncPanelAnim()'s own rising-edge check and
    /// HudState::alertPulse's header for where this is read back.
    ImpactPulse alertPulse_;
    /// Whether a bouncer's warning was showing as of the LAST syncPanelAnim()
    /// call, so that call can tell "a fresh warning just arrived" (the rising
    /// edge alertPulse_ triggers on) apart from "the same warning is still
    /// showing" (re-read every step, must NOT retrigger the pulse every
    /// step) and "warned went from true back to false" (no pulse either way).
    bool lastWarned_ = false;
    /// ACTION-COMBAT BUILD (section 4.1). Whether the fight had crossed the
    /// brawl line as of the last step, so step() can catch the RISING EDGE of
    /// tavern_->escalated() -- the one instant steel comes out -- and speak the
    /// flip once ("STEEL OUT. THE ROOM STANDS BACK." + the SwordDraw) rather
    /// than every step the latch stays up. The same remember-the-edge shape
    /// lastWarned_ uses; clearEscalation() drops the latch and this follows it
    /// down, ready to fire again on the next fight that draws.
    bool lastEscalated_ = false;
    /// ACTION-COMBAT BUILD (section 4.5). THE DEATH CEREMONY's own countdown,
    /// in movement steps, and the two epitaph lines it holds. Nonzero only
    /// between an escalated (lethal) defeat and the reveal kDeathHoldSteps +
    /// the two eases later; armDeathCeremony() sets it, step() spends it, and
    /// composeDeathCeremony() reads it to draw the dip and the plate. Render-
    /// only, never hashed -- the sim revived at the defeat; this is the veil
    /// laid over the top. NOT travelFadeAnim_ (an ease that cannot hold).
    std::int32_t deathCeremonySteps_ = 0;
    std::string deathEpitaphTop_;
    std::string deathEpitaphBottom_;

    // --- JUSTICE BUILD (HEARING PAGE LANE) --------------------------------
    /// The page is up. NOT in dismissOverlays(): the ten world verbs cannot
    /// put a hearing down. Render-side state over the ledger's hashed
    /// HearingState; a hearing pending with no page up opens one on the next
    /// step, so a run reopened between the arrest and the plea reopens at
    /// the bench.
    bool courtOpen_ = false;
    int courtCursor_ = 0;
    bool courtPaperOpen_ = false;
    /// The armed plea row, or -1. The QUIT pattern: one press arms, the next
    /// confirms; moving the cursor or ESC disarms.
    int courtArmed_ = -1;
    /// Steps before the sentence row is offered after the judgment lands
    /// (sim::kJudgmentHoldSteps): the badge is held first, so a leaned-on
    /// ENTER cannot plead and serve in one breath.
    std::int32_t courtJudgmentHold_ = 0;
    /// The page served the sentence and waits for the release step() reads
    /// to close it -- at the Mission's door or on the Tarwalk.
    bool courtServing_ = false;
    TutorBand courtTutor_;
    bool courtTutorWasOpen_ = false;
    /// THE ROPE. The dip and the held plate, counted down once a step like
    /// the death ceremony's; then the rows. Never fades back: the veil holds.
    std::int32_t ropeCeremonySteps_ = 0;
    bool ropeRowsUp_ = false;
    int ropeCursor_ = 0;
    int ropeArmed_ = -1;
    std::string ropePlateTop_;
    std::string ropePlateMid_;
    std::string ropePlateFoot_;
    bool runEnded_ = false;
    RunEndChoice runEndReason_ = RunEndChoice::None;
    /// THE ARREST BEAT, counted down once a step like the ceremonies: the
    /// officer's line in the room while it is above kTakenPlateSteps, the
    /// plate while it is at or below, the page when it reaches zero. Zero
    /// while nobody's hand is on you. Render-only, never hashed: the sim's
    /// hearing is already open on the ledger.
    std::int32_t takenHold_ = 0;
    std::string takenPlate_;
    /// TAKEN: the arrest's release read with a hearing pending. Puts the
    /// body on the Mission's arrival tile, dips, says the line, opens the
    /// page over everything.
    void openCourt();
    /// The page down, the body where the sentence left it, the line said.
    void closeCourt();
    void armRopeCeremony();
    /// The Mission's own arrival tile: the sign's aim point snapped to
    /// standable ground, the travel verb's own landing rule.
    [[nodiscard]] bool missionArrivalTile(std::int32_t& outX, std::int32_t& outY,
                                          std::int32_t& outBand) const;
    /// A world verb or a page toggle asked from the bench: refused, quietly.
    [[nodiscard]] bool refusedInCustody() const noexcept { return inCustody(); }

    // --- the audio wiring pass ----------------------------------------------
    //
    // All render-side, none of it hashed, and all of it inert while audio_ is
    // null -- see setAudio()'s own header and the determinism note in
    // audio_engine.hpp.

    /// Borrowed, never owned. Null for every test and every headless capture.
    audio::AudioEngine* audio_ = nullptr;
    /// Whether the panel that last OPENED was the tiled Menu, so the close
    /// half of the pair can speak BookClose after casebookOpen_ has already
    /// gone false -- the same remember-the-edge shape lastWarned_ uses.
    bool audioPanelWasMenu_ = false;
    /// The purse as of the last step, so a change -- any change: a haggle
    /// settled, a pocket picked, rent paid -- is one CoinHandle, caught by
    /// comparison exactly the way lastPlayerHp_ catches a blow.
    std::int32_t lastCoinForAudio_ = 0;
    /// THE LOT PASS. The STANCE as of the last step, so step() can catch BOTH
    /// edges of tavern_->playerHandsUp(): rising = hands up (SwordDraw, the
    /// Malbers draw), falling = hands down (Sheathe, the Malbers store) --
    /// however they came down: the lull, LOWER HANDS, the cancel list. The
    /// same remember-the-edge shape lastEscalated_ uses.
    bool lastHandsUp_ = false;
    /// THE LOT PASS. How many of the three casebooks (the Hold, the sheet,
    /// the eviction) read closed() as of the last step, so a case closing --
    /// whichever book, whatever lead closed it -- is one CaseClosed sting,
    /// caught by comparison exactly the way the purse is.
    std::int32_t lastClosedBooks_ = 0;

    /// The count behind lastClosedBooks_.
    [[nodiscard]] std::int32_t closedBookCount() const noexcept;
};

// ---------------------------------------------------------------------------
// FAST TRAVEL (TRAVEL lane): the cost of a walk, in the sim's own integers
// ---------------------------------------------------------------------------
//
// THE COST BASIS IS THE ROUTE, NEVER THE CROW. The Overview pane's "54 PACES"
// is a render-layer float straight line and map_view.hpp bars the simulation
// from reading anything in that file -- and the travel cost lands in
// timeOfDay_, which the twin gate compares byte for byte. So the cost is
// derived from the route the district's own PathFinder answers (salt 0, no
// jitter; Gait::Walk, the pace being charged), counted in the router's own
// octile currency (10 per straight step, 14 per diagonal), and converted at
// the shipped walking pace through human_scale.hpp's constants. Pure integer
// functions of their arguments, so a case pins the numbers rather than
// adjectives.

/// The octile units of a walked route: 10 per orthogonal step, 14 per
/// diagonal -- path_finder.hpp's own kStepCost pair, recomputed from the
/// returned route so the charge is exactly the distance the router chose.
/// Walk-gait band changes (a stair) ride their step at no surcharge, which is
/// what the router itself charges them under Gait::Walk.
[[nodiscard]] std::int32_t travelRouteUnits(const sim::PathStep& from,
                                            const std::vector<sim::PathStep>& route) noexcept;

/// Seconds a walk of `units` costs at the shipped walking pace (human_scale's
/// kWalkSpeed, 1.48 m/s), rounded UP -- travel is never free. One octile unit
/// is 25.6 Q8 tile-widths; the body walks kWalkSpeed Q8 per movement step at
/// kStepsPerSecond steps a second.
[[nodiscard]] std::int32_t travelWalkSeconds(std::int32_t units) noexcept;

/// The whole minutes the clock actually advances -- seconds rounded UP, never
/// below one, so the verb's restated cost and the delivered skip are the same
/// number: the Wait page's own honesty rule ("a list that said 1 HOUR while
/// delivering forty minutes would be lying").
[[nodiscard]] std::int32_t travelClockMinutes(std::int32_t seconds) noexcept;

/// The verb's cost restatement: "4 MIN", or "ABOUT AN HOUR" from sixty up.
[[nodiscard]] std::string travelCostLabel(std::int32_t minutes);

/// What a scripted capture run was asked to do.
struct SmokeRunConfig {
    SessionConfig session;
    /// Movement steps to run before the frame is taken. 0 captures the spawn.
    int steps = 0;
    /// Where the PNG goes. Empty writes nothing.
    std::filesystem::path screenshot;
    /// Integer upscale applied to the captured PNG. 1 writes the raw buffer.
    int captureScale = 2;
    /// 3D BUILD. THE CLIENT'S OWN SHUTTER. When set, runSmoke hands the
    /// finished session and the software frame it just drew to this instead
    /// of writing the PNG itself: the client composites the frame through
    /// the raylib backend and writes what the window would have shown. It
    /// returns whether the PNG was written. Null keeps the software capture,
    /// which is what every test and the docker host check use.
    std::function<bool(const Session&, const Framebuffer&)> shutter;
    /// UI-EA (LANE HUD): RETIRED, ACCEPTED AS A NO-OP. This used to burn
    /// "GRANADAD <version>" into the corner of every capture; the word diet
    /// deleted the stamp outright (it rides the pause/keys title rule only),
    /// and the census transcribes captures, so it had to leave this path too.
    /// The field stays so every existing caller and capture script parses.
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
    /// PANES PASS. Put the CONTROLS page's cursor on this row (1-based) so the
    /// converted master/detail layout can be photographed with the detail pane
    /// showing something other than its first entry.
    ///
    /// A SEPARATE FLAG FROM cursorRow, AND IT HAD TO BE. `--cursor` implies
    /// `--talk` (main.cpp sets both, because a topic cursor with no
    /// conversation open names nothing), and a conversation REFUSES to let the
    /// keys page open over it -- so `--pause=controls --cursor=27` photographs
    /// a conversation, which is exactly what the first attempt produced. This
    /// flag implies `--pause=controls` instead.
    ///
    /// The whole claim of a master/detail layout is that moving the cursor
    /// swaps the detail pane and MOVES NOTHING ELSE, and a claim like that is
    /// settled by two frames at two cursor rows or it is not settled.
    int keysRow = 0;
    /// Close whatever conversation is open and start it again. This is how a
    /// capture shows the SAME person greeting you differently after you have
    /// done something to them -- rob them, then say hello.
    bool again = false;
    /// Opens the pause menu before the shutter goes.
    ///
    /// WHY A CAPTURE FLAG EXISTS FOR A PAGE ESC OPENS FOR FREE. Every other
    /// page this build has grown was first proven on screen by
    /// scripts/drive-windowed.ps1 driving a REAL window with real SendInput --
    /// see docs/frames/p77-controls's own header on why a headless
    /// `--screenshot` cannot prove feel. That tool needs a desktop able to
    /// grant a window the foreground, and it correctly REFUSES to drive
    /// anything when one is not there rather than typing into whatever
    /// happens to be in front -- which some environments this build gets
    /// verified in do not have. Without this flag the pause menu would have
    /// no headless path to a photograph at all in exactly the environment
    /// where the real one cannot run, and "trust me, I read the code" is not
    /// what a screenshot is for.
    ///
    /// WHERE is "menu" (RESUME/CONTROLS/SETTINGS/QUIT, freshly opened),
    /// "controls" (CONTROLS chosen, so the keys page it opens -- Keys' own
    /// Morrowind-round Pause-side door -- is what gets photographed),
    /// "settings" (SETTINGS chosen, so the rebinding screen it opens is what
    /// gets photographed) or "armed" (the cursor on QUIT with the first of
    /// its two presses already in, so the "SURE? ENTER" row is on screen).
    bool pause = false;
    std::string pauseEnd = "menu";
    /// VERIFICATION. Opens the tiled Menu, focused on the Character tile,
    /// before the shutter goes -- Session::toggleCharacter(). Added while
    /// adversarially verifying an earlier round's menu/HUD polish: the
    /// character sheet shipped with no headless capture path at all, for
    /// exactly the reason `pause` above states its own -- this environment
    /// cannot drive a real window, so without a flag the page could be
    /// unit-tested for its text but never actually LOOKED AT.
    bool character = false;
    /// VERIFICATION (#82). Opens the tiled Menu, focused on the Map tile,
    /// before the shutter goes -- Session::toggleMap(). The identical reason
    /// `character` exists: no flag here means the page could be unit-tested
    /// for its rows and never actually looked at.
    bool map = false;
    /// VERIFICATION. Every scripted overlay above (`pause`, `character`, and
    /// the panel any conversation opens) toggles on the LAST beat of the
    /// script, with no session.step() left to call afterward -- so every
    /// headless capture this project has ever taken of an overlay panel froze
    /// EasedToggle at its opening bump (one riseStep's worth, ~1/8 open) and
    /// none has ever shown the settled, fully-open panel a player spends the
    /// rest of the transition looking at. This runs the panel's own animation
    /// to completion (plain zero-input steps, nothing walks) before the
    /// shutter, so a capture can show the finished page instead of always
    /// re-proving the same first frame.
    ///
    /// HARDENING PASS (#85 IN THE BRIEF, NOT THE SOURCE): ON BY DEFAULT NOW,
    /// FOR A SCREENSHOT. This flag was wired to `--settle` on the CLI
    /// (main.cpp) the day it was added and NOTHING has ever passed it --
    /// grep the repo -- so every screenshot this project has ever taken of
    /// an overlay panel (the pause menu, the character sheet, the casebook,
    /// the keys/options pages) has silently been the opening bump this field
    /// exists to run past, and every review of one of those PNGs has been
    /// reviewing that bump, not the panel. A capture flag nobody remembers
    /// to set is not a fix, it is a diagnosis with a knob nobody turns.
    ///
    /// runSmoke() only actually spends the sixteen extra steps this arms
    /// when `screenshot` (below) is non-empty -- see its own gate on this
    /// field -- so a scripted run that reads back scriptedWanted/
    /// scriptedLanded and never asked for a picture is untouched by this
    /// defaulting true; only a capture that writes a PNG is. Off is the
    /// special case now, spelled `--no-settle` on the CLI, for the one
    /// caller that genuinely wants the opening bump on purpose -- proving the
    /// transition itself does not flash on its very first frame.
    bool settle = true;
    /// VERIFICATION ONLY (INNOVATION SPRINT). -1 (the default) leaves
    /// `settle` above as the whole story: 16 zero-input steps or none. A
    /// non-negative value overrides that count exactly -- so a capture can
    /// stop the panel/border eases this sprint added PARTWAY through their
    /// own transition (say, four steps into an eight-step rise) rather than
    /// only ever photographing "not started" (`--no-settle`) or "finished"
    /// (`--settle`). Neither of those two proves a RECT is moving and not
    /// only fading -- at the opening bump alpha is already low enough that a
    /// slid-in-but-dim panel and a not-there panel can look the same in a
    /// screenshot -- so this exists to let a mid-transition frame actually
    /// be taken, the same reasoning `cursorRow` and every other verification
    /// flag in this struct already state for themselves.
    int settleSteps = -1;
    /// PLANNING SPRINT (item #1). VERIFICATION ONLY -- extends settleSteps'
    /// own spirit to prove the exact thing it could not.
    ///
    /// THE GAP THIS CLOSES. An adversarial review confirmed the Menu's
    /// focus-swap crossfade (kMenuFocusRiseFallSteps, INNOVATION SPRINT ITEM
    /// #2) is genuinely correct -- an isolated test inserting real step()
    /// calls between two toggleCharacter()/toggleMap() calls proves a clean
    /// crossfade -- but `--character --map` TOGETHER on this CLI calls both
    /// toggles back to back with ZERO step() calls between them.
    /// toggleMenuFocused() only ever moves menuFocus_; the EasedToggle
    /// targets it drives are not re-read until step()'s own syncPanelAnim()
    /// call (session.cpp) next runs, so with nothing between the two
    /// toggles, characterFocusAnim_'s target is set to open and then to
    /// closed on the SAME call, before a single advance() has ever moved it
    /// off zero -- a screenshot taken that way can only ever show Map's own
    /// animation from a cold start, never a real mid-crossfade frame with
    /// Character actually falling away.
    ///
    /// `refocus` (character/map/letters/journal, by name) is the tile to
    /// switch focus TO, once the tile `character` or `map` above opened has
    /// been given `settleSteps`-worth of real step() calls to actually
    /// settle open (the ORDINARY CLI opener is still `--character`/`--map`;
    /// there is no separate `--letters`/`--casebook` flag, so `refocus`
    /// itself is the only CLI-reachable way to land on those two tiles for a
    /// capture). `refocusSteps` is how many further zero-input step() calls
    /// to run AFTER that switch, before the shutter -- the same "stop
    /// partway through an eight-step rise" job settleSteps already does for
    /// panel geometry, aimed at the four-step focus swap instead. Runs
    /// entirely inside runSmoke() (session.cpp), through the SAME public
    /// toggle*() methods a keypress calls -- toggleCharacter()/toggleMap()/
    /// toggleLetters()/toggleCasebook() -- so the file's own reasoning for
    /// why a captured frame is evidence applies unchanged.
    ///
    /// Left empty (the default), this does nothing and runSmoke() falls
    /// straight through to its ordinary settleSteps-only path -- a caller
    /// that never heard of this is unaffected.
    std::string refocus;
    int refocusSteps = 0;
    /// SHEETS BUILD. VERIFICATION ONLY, the identical reasoning every flag
    /// above states: the character sheet's faction-ladder rows live on the
    /// tile's SECOND page (page one is the legend tracks and the skills,
    /// exactly full -- see characterRows()'s own header), and nothing on
    /// this CLI could ever turn a tile's page, so the one row-set the
    /// numbers-on-the-sheet ruling is about had no headless capture path at
    /// all. N presses of the same public nextTopicPage() the 0/MORE key
    /// makes, after the menu-opening flags above have put a tile up.
    int tilePage = 0;
    /// VERIFICATION ONLY (INNOVATION SPRINT). Closes any open conversation,
    /// WALKS UP TO whoever is nearest, puts them on the crosshair and throws
    /// the player's own punch, retrying up to sixteen times (a full recovery
    /// lockout apart, re-facing them each time) until one actually LANDS --
    /// a miss leaves nothing to photograph, and the swing itself is a coin
    /// flip this flag has no business hardcoding around. Exists for the
    /// identical reason `character`/`map` do: item #3's brawl-impact flash
    /// (and, as a natural side effect of throwing a punch inside a taproom
    /// with bouncers watching, the alert plate's own pulse once the house
    /// notices) had no headless capture path at all before this.
    ///
    /// ACTION-COMBAT BUILD: the walk-up is not decoration. A swing hits the
    /// first body on the LOOK-RAY (Tavern::sightlineTarget), not the nearest
    /// body in reach, so a punch thrown from wherever the smoke walk left the
    /// body is a punch at air -- which is how this flag came to land 0/1 at
    /// the Tarwalk spawn. It now drives the same walk-up / face / tap /
    /// recovery beat the nemesis and tenant lines throw.
    bool punch = false;
    /// 3D SLICE ONE (ship lane). VERIFICATION ONLY, the identical reason
    /// `punch` exists: the hard swing's WIND-UP -- the viewmodel's Charging
    /// and ChargedHard states -- had no headless capture path, because every
    /// scripted swing is a tap. Walks up to the nearest person and puts them
    /// on the crosshair exactly as --punch does, then presses the Attack key
    /// DOWN through Session::attackDown() -- the same call a held left mouse
    /// button makes -- and holds it for this many steps WITHOUT releasing,
    /// so the shutter photographs the charge: under sim::kHardSwingHoldSteps
    /// the wind-up mid-scrub, at or past it the hard tier held with its
    /// tremor. --settle-steps=0 keeps the count exact; the default settle
    /// steps on with the key still down.
    int chargeSteps = 0;
    /// FATIGUE BUILD. VERIFICATION ONLY, the identical reason every flag
    /// above states: the fatigue bar's mid and empty states, the winded
    /// sprint gate and its one refusal line had no headless capture path.
    /// Drives this many REAL sprint steps -- the same held forward-plus-
    /// sprint input a player drains the pool with, through the same
    /// Session::step() -- before the shutter. The pool drains on intent, so
    /// the wall the body ends up pressed against changes nothing; around
    /// 1,950 steps the base sheet's pool is empty, the gate downgrades the
    /// held sprint to the jog and the refusal line lands on the alert row,
    /// which is exactly the frame the flag exists to photograph.
    int sprintSteps = 0;
    /// FIRST-PERSON COMBAT (S13). VERIFICATION ONLY, the identical reason
    /// `punch` exists: the held-guard indicator and its blocked-blow wash had
    /// no headless capture path. Starts a brawl exactly the way --punch does
    /// (walked up to, faced and tapped through the same Session::punch() a
    /// keypress calls, until the tap connects and there is a man in the fight
    /// to throw the blows the guard is meant to catch), raises the guard
    /// through the same Session::setBlocking() the right mouse button calls,
    /// and holds it until at least one blow has actually been SOFTENED (the
    /// room's own blowsBlocked() moving) or a bounded wait runs out -- a
    /// brawl whose every swing whiffed leaves nothing on screen to prove.
    bool block = false;
    /// FIRST-PERSON COMBAT (S13). VERIFICATION ONLY. One press of Cast --
    /// Session::castEquipped(), the same call C makes -- after whatever the
    /// other flags scripted. Paired with --flame (whose line ends with the
    /// priest's teaching) the grimoire is stocked and the equipped-crafting
    /// HUD row has something to show; alone, the photographed truth is the
    /// empty-grimoire refusal on the alert row, which is the COMMON state and
    /// worth a picture of its own.
    bool cast = false;
    /// HELD-EFFECTS BUILD. VERIFICATION ONLY, the identical reason `cast`
    /// exists: the active-effects rows and the held-tuning outcome shift had
    /// no headless capture path. A comma-separated list of spell ids; each in
    /// turn is equipped BY ID through the grimoire-order door
    /// (Tavern::equipSpellAt) and cast through the same Session call C makes,
    /// waiting out fizzle and success cooldowns through real steps, until its
    /// link opens or a bounded retry count runs dry. Pair with --flame, whose
    /// teaching loop stocks the grimoire deep enough to hand these over.
    /// `--held=steady_the_hand` photographs a live hold with its clock;
    /// `--held=clear_the_head,steady_the_hand` photographs the held-WIT mind
    /// shortening the second crafting's printed recovery -- the before/after
    /// pair against the single-id run.
    std::string heldSpells;
    /// SPELLS BUILD. VERIFICATION ONLY, the identical reason `cast` exists:
    /// the Grimoire page had no headless capture path. Opens it through the
    /// same Session::toggleGrimoire() a tap of the QuickWheel key calls --
    /// pair with --flame so the list has craftings on it; alone it
    /// photographs the page's own empty-grimoire line, the common state.
    bool grimoire = false;
    /// SPELLS BUILD. VERIFICATION ONLY. Binds the first known crafting to
    /// slot 3 through the Grimoire page's own adjust verb, closes the page,
    /// and presses the slot's number -- the same public calls a keypress
    /// makes -- so the bottom-centre strip, its equipped highlight and the
    /// CAST row can be photographed agreeing with each other. Pair with
    /// --flame for the same reason as --grimoire.
    bool quickbar = false;
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
    /// across the same table. WHERE is "talk" (the finished conversation),
    /// "away" (closed, so the HUD's own sack and job lines are visible) or
    /// "held" (SHEETS BUILD: stop after the take and the mark with the job
    /// still LIVE -- the one state the Journal tile's contract rows have
    /// anything to show for, and one the full line never passes through the
    /// shutter in).
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
    /// BARKS LANE (feel/build). Play the Watch on seen violence: walk into
    /// the Gull at Watchman Cull's hour, draw steel, swing at a patron he
    /// can SEE, and stand there -- he closes with the halt (watch.halt) on
    /// the alert row and takes you at reach, cause VIOLENCE, and the impound
    /// turns you loose on the Tarwalk. WHERE is "halt" (stop the step he
    /// starts Closing, so the frame holds the halt) or "street" (the whole
    /// arc, the arrest line on the row).
    bool watchHalt = false;
    std::string watchHaltEnd = "street";
    /// JUSTICE BUILD (HEARING PAGE LANE). Play the court: walk into the Gull
    /// at Cull's hour, lift in his sight until the row reads WANTED, stand
    /// still with steel up until he takes you at reach -- his own line on
    /// the row with his hand on you, the plate (TAKEN TO THE MISSION over
    /// black), and then the page. WHERE is "page" (the rows, the default),
    /// "cull" (the arrest's first beat, his line in the room), "taken" (the
    /// plate), "paper" (HEAR THE PAPER open), "armed" (I DID IT pressed
    /// once: SURE on its tail, the priest pressing), "plea" (I DID IT
    /// pleaded, the check block and the sentence row), "deny" (I DID NOT,
    /// the priest's doubt in the block), "hand" (the Skyrunners' oath off
    /// Finch first, so the paper asks for the hand and I DID IT lands THE
    /// HAND), "serve" (I DID IT and the sentence row taken: the coin, the
    /// clock, TURNED LOOSE with the day on the row), "bloodtag" (a killing
    /// at eight, WANTED FOR BLOOD on the row), "ropepage" (the killing, the
    /// wait to ten, taken to a rope hearing, THE ROPE passed and THE DROP
    /// offered), "rope" (the drop taken: the plate with A NEW MAN / LEAVE
    /// under it), "newman" (the rope, then A NEW MAN armed by its row and
    /// taken -- the answer main() loops on) or "wanted" (the tag alone, the
    /// first beat, nobody's hand on you). With `flame` set first the court
    /// waits for Cull through the wait page and THE DOOR is weighed.
    bool court = false;
    std::string courtEnd = "page";
    /// S9. Play a burglary: crouch, cross a dark taproom unseen, lift a purse
    /// off somebody who does not feel it, up the stair, wire into a guest's
    /// strongbox, work the pins, and empty it. WHERE is "box" (standing over
    /// the box you have just opened), "lock" (the wire in the NEXT box, so the
    /// lockpicking surface itself is on screen), "taproom" (back down among the
    /// people who did not hear you) or "street" (out of the door with it).
    bool burgle = false;
    std::string burgleEnd = "box";
    /// S10. WALK THE BLOODLETTER TRAIL. Every beat is the same two calls a
    /// keyboard makes -- the district's own breadth-first router to the site,
    /// then Q -- and the run reports how many leads it read against how many it
    /// walked to. Those two differing is a real failure: it means a site in
    /// casebook.json cannot be got to on foot.
    ///
    /// `trailEnd` is where the shutter goes: notes (the casebook open over the
    /// last place), keys (the in-game controls page), mission, weighhouse or
    /// hold (stop after that lead), letters (TASK #82: stop after
    /// mission-flagstones and open the first of Maell's letters), or empty
    /// for the whole walk.
    bool trail = false;
    std::string trailEnd;
    /// COURIER CASE. PLAY THE QUIET TENANT END TO END: the courier's sheet
    /// into the hand, the sheet read on the Letters tile, the Gull walked to
    /// by day, the wait to the small hours, the box lead on the guest floor,
    /// the bouncer and the tenant put down with fists (the brawl line's own
    /// Subdue), TAKE HIM UP, and the walk to the Mission's back room. Every
    /// beat is the same Session verbs a keypress drives. `caseEnd` is where
    /// the shutter goes: "sheet" (stop with the mission sheet open on the
    /// Letters tile), "gull" (stop after the door lead, book open), "night"
    /// (stop crouched on the guest floor over the box lead), "down" (stop
    /// the step Finch goes down, before the take), "taken" (TRAVEL lane:
    /// stop with the man genuinely in hand, after TAKE HIM UP -- the one
    /// state the errand never otherwise parks in, so the travel probe can
    /// press its verb against the carry refusal for real), or empty for the
    /// whole errand delivered.
    bool caseRun = false;
    std::string caseEnd;
    /// EVICTION CASE (lane: eviction). PLAY THE OWNER'S THIRD CASE, both
    /// paths, through the same Session verbs a keypress makes: the hire off
    /// Father Maell's evening table (the open-hand measure read live), the
    /// writ on the Letters tile, the walk east to the Netters' gate, the
    /// knock at the Marrow door once they are home, and then EITHER the
    /// serve and the walk back for The Evictor, OR the walk-back unserved.
    /// `evictionEnd` is where the shutter goes: "writ" (stop with the writ
    /// open on the Letters tile), "gate" (stop after the gate lead, book
    /// open), "knock" (stop at the answered door, the choice live), "served"
    /// (stop the moment the paper changes hands, before the walk home), or
    /// "refused" (the DISRUPT path: knock, then carry the writ back whole --
    /// the case closes with no weapon granted). Empty serves and returns:
    /// the whole participate path, The Evictor in hand.
    bool evictionRun = false;
    std::string evictionEnd;
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

    /// #79. STAND NEXT TO SOMEBODY IN THE WARD AND TALK TO THEM.
    ///
    /// `streetWho` is a trade -- hand, watch, priest, keeper, fisher, sailor,
    /// wastrel, urchin, carter, keeper of beasts, cat -- and the run finds the
    /// first living body of it out in the open district, stands the player on
    /// the tile beside them and presses the talk key.
    ///
    /// WHY IT EXISTS. Every other scripted line in this list plays out inside
    /// one building, and the capture harness's router (kCaptureRegion) is that
    /// building plus the street in front of it. "Talk to a goatherd on Gallows
    /// Row" is a walk of a hundred and thirty tiles across three bands, and a
    /// capture flag that spent thirty seconds of simulated time walking there
    /// would be photographing the pathfinder rather than the conversation.
    ///
    /// SO IT PLACES THE BODY, and says so out loud rather than implying a walk
    /// it did not take. It is the same PlayerBody::placeAt that `--spawn`
    /// uses -- one landing on a standable cell, checked -- and everything after
    /// it is the real thing: the real Session::interact(), the real director,
    /// the real authored tables.
    bool street = false;
    std::string streetWho = "hand";
    /// Which topic to pick once the street conversation is open, 1-based as the
    /// numbers on screen. Zero picks nothing and photographs the greeting.
    int streetTopic = 0;

    /// TIME-AND-TENURE BUILD, VERIFICATION ONLY: open the Wait page through
    /// the pause menu's own WAIT row -- togglePause, cursor down one, ENTER,
    /// the same three presses a hand makes -- and leave it up for the
    /// shutter. The page's twelve hour rows and its top-band ruling text are
    /// what the capture is evidence of.
    bool wait = false;

    /// TIME-AND-TENURE BUILD, VERIFICATION ONLY: play the leasehold petition
    /// -- find a member of the clergy answerable from compound ground
    /// (scanning the clock hour by hour until one is; the roster's day puts
    /// bodies where it puts them), stand beside them the same one-placement
    /// way --street does, talk, read the roll for the ground underfoot, and
    /// petition for the roll's vacant charge. Everything after the placement
    /// is the game: the real topics, the real Ward::petitionForCharge, the
    /// real purse. See PetitionLineResult for what it reports.
    bool petition = false;

    /// THE CASEBOOK PASS, VERIFICATION ONLY. The book is a composed page with
    /// a cursor, two views and a commit verb, and without these three a
    /// headless capture could only ever photograph the state opening it lands
    /// on -- the identical reason `mapPlace`/`mapTab`/`mapZoom` exist.
    ///
    /// EVERY ONE OF THEM GOES THROUGH THE SAME PUBLIC METHOD A KEY PRESS
    /// CALLS: selectCasebookLead, cycleCasebookTab, commitCasebookLead. A
    /// captured frame is a picture of the game and not of a capture path that
    /// happens to look like it.
    ///
    /// `caseLead` is a casebook.json lead id ("weighhouse-ledger"); it OPENS
    /// the book if nothing else has. `caseTab` is leads or case. `caseRoute`
    /// presses the commit verb, which is how the route from a lead to its place
    /// on the ward map gets photographed at all -- there is no other headless
    /// path to it.
    std::string caseLead;
    std::string caseTab;
    bool caseRoute = false;

    /// THE WARD MAP (core action #13), VERIFICATION ONLY: open the district
    /// map through the same Session::toggleDistrictMap() the M key calls,
    /// after every scripted line and the other menu-opening flags, so the
    /// page is photographable headless -- the identical reason `character`
    /// and `map` (the tiled Menu's Chart tile) have flags. `--map-overlay`
    /// on the CLI, following --creation/--tile-page's precedent.
    bool mapOverlay = false;

    /// THE MAP PASS, VERIFICATION ONLY, and the same reason every other flag in
    /// this struct exists: the page has a cursor, four views and a zoom ladder
    /// now, and without these a headless capture could only ever photograph the
    /// one state opening it lands on. A screenshot is this project's evidence,
    /// so every state a player can reach has to be reachable by the shutter.
    ///
    /// `mapPlace` is an authored place name, exactly as the .tmx spells it
    /// ("The Weighhouse", "Fenner's Pawn"); it goes through the same
    /// Session::selectDistrictMapPlace an Index row calls, and a name matching
    /// nothing is REPORTED rather than silently photographing the wrong
    /// building. `mapTab` is overview/people/index/legend. `mapZoom` is a rung
    /// of the ladder, 0 (the whole ward) up. All three are inert without
    /// `mapOverlay`.
    std::string mapPlace;
    std::string mapTab;
    int mapZoom = 0;

    /// FAST TRAVEL (TRAVEL lane), the scripted probe: put the ward map's
    /// cursor on this authored place name and press the TRAVEL verb, through
    /// the same public methods the T key spends -- toggleDistrictMap,
    /// selectDistrictMapPlace, travelDistrictMapSelection. Runs AFTER every
    /// scripted line and the map flags, so `--case=taken --travel=...` probes
    /// the carry refusal with the man genuinely in hand, and BEFORE `--face`,
    /// which would close the page this needs open. The summary's own
    /// `| travel` segment prints the plan, the clock either side of the
    /// press, the landing and the plate -- run twice, the segment must match
    /// to the byte, which is the twin-run check for a session-scripted
    /// feature (test_case_line's own discipline).
    std::string travelTo;

    /// THE CROSSHAIR PASS, VERIFICATION ONLY: TURN THE BODY TOWARD AN AUTHORED
    /// PLACE and leave it standing there, with no page open.
    ///
    /// The aim prompt only says anything about a door when a door is under the
    /// crosshair, and "under the crosshair" is a yaw -- there is no page to
    /// open, no key to press and no conversation to have, which is the same
    /// hole `threshold` states for the plate. Pair it with `--spawn` to stand
    /// somewhere and this names what you are looking at from there.
    ///
    /// It goes through the SAME selectDistrictMapPlace + faceDistrictMapSelection
    /// the ward map's own `ENTER - FACE IT` calls, so a captured frame is a
    /// picture of the game rather than of a capture path beside it. A name
    /// matching nothing is REPORTED (scriptedWanted moves, scriptedLanded does
    /// not) rather than silently photographing the wrong direction.
    std::string face;

    /// RADIANT BUILD, VERIFICATION ONLY: play a radiant errand -- read the
    /// board the session's own tavern posted off the live ward, find an
    /// offered objective whose giver is answerable where they are actually
    /// standing (scanning the clock hour by hour, the petition line's own
    /// walk), stand beside them the one-placement way --street does, talk,
    /// and take the errand off them. Everything after the placement is the
    /// game: the real interact, the real director, the real RadiantBoard.
    bool radiant = false;
    /// Where the line stops for the shutter. "offer" opens the conversation
    /// and leaves the giver's own TAKE row on the visible list (one beat);
    /// "taken" -- the default -- presses it and proves the board moved and
    /// the journal shows the errand (three beats).
    std::string radiantEnd = "taken";

    /// DISTRICT PHASE D, VERIFICATION ONLY: WALK ACROSS A NAMED BOUNDARY, so
    /// the threshold plate is actually on the frame when the shutter goes.
    ///
    /// WHY A FLAG AT ALL, for the hundredth time in this struct and for the
    /// same reason every time: the plate is up for two seconds after a
    /// crossing and for no other reason. There is no page to open, no key to
    /// press and no conversation to have. Without this, the one element this
    /// pass adds could be unit-tested for its state and NEVER LOOKED AT --
    /// which is the exact hole `character`, `map` and `punch` each state for
    /// themselves.
    ///
    /// The value names one of a small table of authored crossings (see
    /// kThresholds in session.cpp), each of which is one pair of world tiles
    /// either side of a docks::kPlaces boundary:
    ///
    ///   saltgate  out of the Quayward compound's east gate onto Saltgate Rise
    ///             -- UNDER DISTRICT PHASE B'S OWN GATE FRAME, which is the
    ///             frame this capture exists to photograph the plate in
    ///   gull      off the Tarwalk through the Gilded Gull's door
    ///   piers     off the Tarwalk north over the quay lip onto the Long Piers
    ///   gallows   west along Gallows Row onto the head of Saltgate Rise
    ///
    /// ONE PLACEMENT AND THEN A REAL WALK -- runStreetLine's own shape and its
    /// own reasoning (the capture router's box is the Gilded Gull, and walking
    /// a hundred and thirty tiles would be photographing the pathfinder). The
    /// body is PLACED on the near side, the placement's own plate is then run
    /// all the way out through real steps so nothing left over from it can be
    /// mistaken for the crossing, and the crossing itself is walked with the
    /// same movement steps a player's key produces. The frame is evidence of
    /// the walk, not of the placement.
    std::string threshold;
    /// Where the camera is left pointing once the crossing is walked. "in"
    /// (the default) keeps facing the way the body walked, looking into the
    /// place just entered; "back" turns a half circle to look at the doorway
    /// or gate just come through -- which for `saltgate` is the Phase B gate
    /// frame itself, standing over the mouth, with the plate up. Only read
    /// when `threshold` is set.
    std::string thresholdEnd = "in";
};

/// DISTRICT PHASE D. What a `--threshold` run actually did, so a case can
/// assert the crossing -- stood outside it, walked in, and the plate fired
/// with the right words -- rather than reading pixels.
struct ThresholdLineResult {
    /// The crossing keyword was one this table knows.
    bool found = false;
    /// The place the near tile was in (empty for unnamed ground), and the one
    /// the far tile is in.
    std::string from;
    std::string to;
    /// The body genuinely ended the walk standing inside `to`.
    bool crossed = false;
    /// The plate was WANTED WHEN THE WALK ENDED, and what it was saying.
    ///
    /// WHEN THE WALK ENDED, NOT AT THE SHUTTER, and the difference is a real
    /// capture rather than a quibble. runSmoke runs this line BEFORE the
    /// menu-opening flags on purpose, so `--threshold=piers --map-overlay`
    /// arms the plate by a real walk and then hands the screen to a page --
    /// and that run reports `up=yes` beside a frame with no plate on it,
    /// which is exactly right: the plate WAS wanted, and the stand-down rule
    /// is what the picture is evidence of. A field that answered for the
    /// shutter instead could not tell that frame apart from one where the
    /// crossing never fired at all.
    bool announced = false;
    std::string plate;
};

/// FAST TRAVEL (TRAVEL lane). What a `--travel` run planned and did, so a
/// case asserts the whole claim -- the honest cost, the exact clock advance,
/// the landing, the plate -- rather than reading pixels. A refusal is a real
/// answer, not a failure: `moved` false with `refusal` filled and the clock
/// unmoved is the feature working.
struct TravelLineResult {
    /// The name matched an authored place.
    bool found = false;
    /// What the plan said at the moment of the press.
    Session::TravelPlan plan;
    /// The clock either side of the press, seconds since midnight.
    std::int32_t clockFrom = 0;
    std::int32_t clockTo = 0;
    /// Where the body ended, and whether that is the plan's own landing with
    /// the page down -- i.e. a travel actually taken.
    std::int32_t endX = 0;
    std::int32_t endY = 0;
    std::int32_t endBand = 0;
    bool moved = false;
    /// The threshold plate at the shutter: wanted, and saying what.
    bool plateUp = false;
    std::string plate;
};

/// RADIANT BUILD. What a `--radiant` run actually did, so a case can assert
/// the arc -- the board had work, the giver answered, the take landed, the
/// journal carries it -- rather than reading pixels.
struct RadiantLineResult {
    /// An offered objective with an answerable giver was found.
    bool found = false;
    /// The board id and cast of the errand played.
    std::int32_t objectiveId = -1;
    std::string giver;
    std::string brief;
    /// The conversation opened ON the giver.
    bool opened = false;
    /// The giver's own TAKE row was on the list.
    bool offered = false;
    /// Chosen, and the board row genuinely reads Taken.
    bool taken = false;
    /// journalWorkRows() carries the errand's row.
    bool journal = false;
};

/// TIME-AND-TENURE BUILD. What a `--petition` run actually did, so a case can
/// assert the arc -- found somebody on the ground, opened, read, petitioned,
/// and what the roll now says -- rather than reading pixels.
struct PetitionLineResult {
    /// A clergy body was found answerable from compound ground.
    bool found = false;
    bool opened = false;
    /// The plot the conversation STOOD on (the roll reading's ground), by
    /// compounds.json id.
    std::string plotId;
    std::string speaker;
    /// The priest's reading of the roll, and the petition's answer.
    std::string rollLine;
    std::string petitionLine;
    /// Whether ANY plot's charge now reads playerIsDuke -- the petition names
    /// the roll's vacant plot, which need not be the one stood on.
    bool becameDuke = false;
};

/// What a `--street` run actually found and said. Returned so a case can
/// assert the conversation happened rather than reading it out of pixels.
struct StreetLineResult {
    bool found = false;
    bool opened = false;
    /// The ward actor id spoken to, or -1.
    std::int32_t actorId = -1;
    std::string name;
    /// The authored barks.json key the greeting came out of.
    std::string barkKey;
    std::string line;
};

struct SmokeRunResult {
    bool ok = false;
    FrameStats stats;
    std::string summary;
    std::size_t lampCount = 0;
    std::int32_t endTileX = 0;
    std::int32_t endTileY = 0;
    std::int32_t endBand = 0;
    /// How many of the Gilded Gull's own seventeen were inside K03 when the
    /// shutter went.
    ///
    /// RENAMED IN #79, and the old name was the whole defect. It was
    /// `actorsInFrame` and it printed as `actors=`, beside `ward=661` -- two
    /// counters of people, one of them reading 1, and neither name saying that
    /// the small one counts a DIFFERENT population in a DIFFERENT building and
    /// is not a subset of the large one. `actors=1  ward=661` reads as "one
    /// actor is on screen out of six hundred", which is not what either number
    /// means: this one never had anything to do with the frame.
    std::int32_t gullPresent = 0;
    /// #78. The ward's own roll, how many of it this frame could see, and how
    /// many stood within twelve tiles of the camera. EVIDENCE, not a gate: the
    /// owner ruled out "a frame must contain actors" outright, because a cellar
    /// or a back lane at four in the morning is legitimately empty.
    std::int32_t wardRoll = 0;
    std::int32_t wardDrawn = 0;
    std::int32_t wardNear = 0;
    /// #80. WHO LIVES ON THE ROOFS AND WHO IS UP THERE NOW, and how the food
    /// chain is doing. `roofHomed` counts beds on a deck -- a bed reached by
    /// climbing and by nothing else -- and `onRoofNow` counts the bodies
    /// actually standing off the ward's walking island at the moment of
    /// capture, which is what a night frame of the Gullet is evidence OF.
    /// `prey`/`preyUp` is mice on the roll against mice on the board, and
    /// `catches` is how many the district's cats and strays have taken.
    std::int32_t roofHomed = 0;
    std::int32_t onRoofNow = 0;
    std::int32_t prey = 0;
    std::int32_t preyUp = 0;
    std::int64_t catches = 0;
    /// #79. Who a `--street` run actually got hold of, and out of which
    /// authored table. Both printed, because "a conversation happened" is not
    /// evidence that the right person had it in the right voice.
    std::string streetSpeaker;
    std::string streetKey;
    /// True when a conversation was open at the moment of capture.
    bool talking = false;
    /// How many stages of the Priest of the Flame line the scripted
    /// playthrough actually finished. Zero when --flame was not asked for.
    std::int32_t flameStages = 0;
    /// The same for the Skyrunner line.
    std::int32_t skyrunStages = 0;
    /// BARKS LANE: beats of the Watch-on-violence line (--watch-halt).
    std::int32_t watchHaltBeats = 0;
    /// JUSTICE BUILD: beats of the court line (--court), by its ending.
    std::int32_t courtBeats = 0;
    /// How many of the six beats of the bounty run landed.
    std::int32_t contractBeats = 0;
    /// How many of the seven beats of the nemesis arc landed.
    std::int32_t nemesisBeats = 0;
    /// S10: how many leads the walked trail READ, and how many it walked to.
    /// The two differ only when a site could not be reached on foot, which is a
    /// failure and is reported as one.
    std::int32_t trailRead = 0;
    std::int32_t trailWalked = 0;
    /// And of the seven beats of the burglary.
    std::int32_t burgleBeats = 0;
    /// COURIER CASE: how many of the errand's beats landed, and which, one
    /// bit each in order -- the burglary's own count-plus-mask reporting
    /// discipline, for the same S4-review reason it has it.
    std::int32_t caseBeats = 0;
    std::int32_t caseBeatMask = 0;
    /// EVICTION CASE: how many of the --eviction beats landed, and which.
    std::int32_t evictBeats = 0;
    std::int32_t evictBeatMask = 0;
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
    /// PLANNING SPRINT (item #1). Only meaningful when SmokeRunConfig::refocus
    /// was non-empty -- every one of the tiled Menu's four focus values
    /// (Session::characterFocusValue() etc.) AT THE MOMENT OF CAPTURE, so a
    /// case or a review can assert the NUMBER a `--refocus` screenshot is a
    /// picture of, rather than reading it off pixels. 0 for every caller that
    /// never asked for a refocus, which is what a hand-built result already
    /// means.
    float characterFocusAtCapture = 0.0F;
    float mapFocusAtCapture = 0.0F;
    float lettersFocusAtCapture = 0.0F;
    float journalFocusAtCapture = 0.0F;
    /// TIME-AND-TENURE BUILD: what a --petition run found and did -- printed
    /// in the summary for the same reason streetSpeaker is: "a conversation
    /// happened" is not evidence the ROLL moved.
    PetitionLineResult petitionResult;
    /// RADIANT BUILD: what a --radiant run found and did, printed in the
    /// summary for the identical reason.
    RadiantLineResult radiantResult;
    /// DISTRICT PHASE D: what a --threshold run walked and whether the plate
    /// actually fired, printed in the summary for the identical reason. A
    /// screenshot of a plate that is not there looks exactly like a screenshot
    /// of a street, which is precisely the failure mode a picture cannot
    /// report on itself.
    ThresholdLineResult thresholdResult;
    /// TRAVEL lane: what a --travel run planned and did, printed in the
    /// summary for the identical reason again -- the clock moving by exactly
    /// the restated minutes is the one claim a PNG cannot make.
    TravelLineResult travelResult;
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

/// CASE WATCH. Where a session ended, in every coordinate the courier case's
/// summary reads -- the twin-run comparator for the tape: a replay that ends
/// equal to the drive in all of these ended equal in everything `--case`
/// prints. Compared whole, defaulted, C++20's own memberwise rule.
struct WatchFingerprint {
    std::int32_t timeOfDay = 0;
    std::int64_t elapsedSeconds = 0;
    std::int32_t tileX = 0;
    std::int32_t tileY = 0;
    std::int32_t band = 0;
    std::int32_t yaw = 0;
    std::int32_t read = 0;
    std::int32_t known = 0;
    std::int32_t dread = 0;
    std::int32_t letters = 0;
    bool closed = false;
    bool live = false;
    bool carry = false;
    bool tenantDown = false;
    [[nodiscard]] bool operator==(const WatchFingerprint&) const = default;
};

/// The fingerprint of a session as it stands right now.
[[nodiscard]] WatchFingerprint watchFingerprintOf(const Session& session);

/// CASE WATCH. One recorded run of the courier drive: the tape, what the
/// drive's own marks said it landed, and where it ended.
struct CaseWatchDrive {
    std::vector<WatchOp> ops;
    /// Beats landed / owed and the which-one mask, exactly as `--case` counts
    /// them (a short `ending` owes only the beats up to its shutter).
    std::int32_t beats = 0;
    std::int32_t beatsWanted = 0;
    std::int32_t mask = 0;
    /// How many of `ops` are Steps -- the watch run's minimum frame count.
    std::int32_t stepCount = 0;
    WatchFingerprint end;
};

/// CASE WATCH. Runs the SAME runCaseLine `--case` runs -- same session
/// construction, same scripted hour, same verbs -- with the recorder attached,
/// and hands back the tape. This is the drive; the watch is its replay.
[[nodiscard]] CaseWatchDrive recordCaseDrive(const SmokeRunConfig& config);

/// #79. Stands the body beside a body of the named trade out in the ward and
/// opens a conversation. Exposed rather than buried in runSmoke so a case can
/// drive it and assert WHO answered and out of WHICH authored table -- the
/// evidence for "a dockhand and a watchman do not sound alike" is a key and a
/// sentence, not a screenshot.
StreetLineResult runStreetLine(Session& session, const std::string& who, int topic);

/// TIME-AND-TENURE BUILD. Plays the leasehold petition end to end -- see
/// SmokeRunConfig::petition. Exposed for exactly runStreetLine's reason: the
/// evidence that a vacant charge can be petitioned for in conversation is a
/// pair of sentences and a changed roll, and a case asserts those directly.
/// `grantCoin` true stocks the purse with the plot's own charge-rent first
/// (capture plumbing, through the public purse setter; the priest's answer is
/// still the real verb's).
PetitionLineResult runPetitionLine(Session& session, bool grantCoin);

/// RADIANT BUILD. Plays a radiant errand's take -- see SmokeRunConfig::radiant.
/// Exposed for exactly runStreetLine's reason: the evidence that TASK #81's
/// generator is reachable is a topic on a real list, a board row that moved
/// and a journal that shows it, and a case asserts those directly.
/// `takeIt` false stops with the offer on the open list (the "offer" end).
RadiantLineResult runRadiantLine(Session& session, bool takeIt);

/// DISTRICT PHASE D. Walks the body across one authored place boundary -- see
/// SmokeRunConfig::threshold. Exposed for exactly runStreetLine's reason: the
/// evidence that crossing into a named place announces itself is a pair of
/// place names and a live plate, and a case asserts those directly rather than
/// counting pixels in a PNG.
ThresholdLineResult runThresholdLine(Session& session, const std::string& which,
                                     const std::string& end);

}  // namespace granadad::render
