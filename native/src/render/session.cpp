#include "granadad/render/session.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <sstream>
#include <utility>

// THE AUDIO WIRING PASS. The one render file that speaks to the audio engine,
// and it speaks only ids -- see setAudio()'s header in session.hpp, and the
// determinism note in audio_engine.hpp for why this include may never spread
// to a header or to anything under src/sim.
#include "granadad/audio/audio_engine.hpp"
#include "granadad/content/content_dir.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/render/capture.hpp"
#include "granadad/render/signage_renderer.hpp"
#include "granadad/sim/angle.hpp"
#include "granadad/sim/build_info.hpp"
#include "granadad/sim/docks.hpp"
// TIME-AND-TENURE BUILD: the compound sign footprints, for the one question
// the tier-3 correlation was ever going to answer -- whose ground is under
// the feet. See plotIndexUnderfoot().
#include "granadad/sim/docks_signs.hpp"
#include "granadad/sim/path_finder.hpp"
#include "granadad/sim/stealth.hpp"

#include "session_internal.hpp"
#include "scripted/scripted.hpp"

namespace granadad::render {

namespace {

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

}  // namespace

// THE FIGURE TABLES ARE PUBLIC (3D BUILD, A lane): render3d's actor instancer
// draws the same sixteen kinds of body out of the same two tables, so a
// bouncer is the Watch's build in 3D exactly as he is on the sheet.

/// How tall a body of each kind stands, in tiles, and the width its drawing is
/// scaled to at that height.
///
/// A PERSON IS 1.875 TILES SOLE TO CROWN, which is kStandingHeightTilesQ8 out
/// of sim/vertical_scale.hpp -- the same header the player's own eye height
/// comes from. At roughly 0.9 m to the tile that is a 1.71 m adult and the
/// crown lands a hand above the player's 1.70-tile eye, so you look people in
/// the mouth. That is correct, and it is the cheapest proof the two scales
/// agree.
///
/// The WIDTH is not the drawing's own aspect. The sheet's figures are chunky
/// -- twelve texels of shoulder in a sixteen-texel cell -- and taken literally
/// that is a person a tile and a half across, standing in a collision square
/// 0.7 of a tile wide. So the height is the truth and the width is scaled to
/// put shoulders at about half a tile, which is a person shaped like a person.
FigureScale figureScaleOf(sim::WardType type) noexcept {
    switch (type) {
        case sim::WardType::MilitiaWatch: return {1.95F, 0.86F};
        case sim::WardType::Urchin: return {1.30F, 0.58F};
        case sim::WardType::Thief: return {1.80F, 0.72F};
        case sim::WardType::PriestOfTheFlame: return {1.90F, 0.84F};
        case sim::WardType::DiscipleOfTheFlame: return {1.82F, 0.80F};
        case sim::WardType::Dog: return {0.62F, 0.90F};
        case sim::WardType::Stray: return {0.46F, 0.66F};
        case sim::WardType::Cat: return {0.42F, 0.62F};
        case sim::WardType::Mouse: return {0.22F, 0.34F};
        default: return {1.875F, 0.80F};
    }
}

namespace {

/// How much darker somebody with their back to you is.
///
/// THE FACING IS FINALLY WORTH SOMETHING AGAIN. The sheet is drawn front-on --
/// there is no back view, and inventing one would be this build authoring art
/// -- so the eight-point facing the simulation has been hashing since S2 is
/// spent on light instead: a figure looking your way catches what light there
/// is on its face and a figure turned away is a silhouette. Which is both true
/// and, at eight tiles in lamplight, the one thing about a person you actually
/// need to read.
constexpr float kBackShade = 0.55F;

}  // namespace

/// WHICH DRAWN FIGURE A TAVERN ROLE WEARS.
///
/// #78 REPLACED THE ELLIPSE STACK, and it is worth recording what it replaced
/// and why. A tavern actor used to be three stacked ellipses -- legs, torso,
/// head -- with a pale patch for a face. That was the right first step and its
/// own header said so: a narrow stack of three reads as a person from across a
/// room where one blob reads as an egg. What it could not do is survive being
/// MULTIPLIED. The owner captured a conversation frame, looked at the figure
/// standing in the Gilded Gull's doorway, and called it what it was: a plain
/// egg shape with a round head, which will not read as polished with hundreds
/// of them on a street.
///
/// content/art/sprites was already in the repo -- twenty-five 16x16 figures in
/// the MERCOLAS-24 palette, front-on, with a helmet on the guard, white and red
/// on the priest, a hood on the vagrant and a hat on the merchant. Those are
/// silhouettes a player can tell apart at eight tiles in lamplight, which is
/// the whole visual target, and somebody had already drawn them.
///
/// So the taproom's fourteen are drawn out of the same sheet the ward's six
/// hundred are, and there is ONE look for a person in this game rather than two.
sim::WardType figureForRole(sim::ActorRole role, std::int32_t id) noexcept {
    switch (role) {
        case sim::ActorRole::Bartender:
        case sim::ActorRole::Innkeeper:
            return sim::WardType::Shopkeeper;
        // A bouncer is hired for his shoulders, and the sheet's guard is the
        // only figure on it built like that.
        case sim::ActorRole::Bouncer:
            return sim::WardType::MilitiaWatch;
        case sim::ActorRole::PriestOfTheFlame:
            return sim::WardType::PriestOfTheFlame;
        // Grey on grey, keeping to the corner, hard to pick out. The sheet's
        // ragged vagrant is exactly that and the Skyrunners dress like it.
        case sim::ActorRole::SkyrunnerContact:
            return sim::WardType::Thief;
        case sim::ActorRole::Vermin:
            return sim::WardType::Mouse;
        case sim::ActorRole::Patron:
        default:
            // THE GULL IS THE CAPTAINS' TAVERN -- charts on the walls, factors
            // doing deals, the good wine (DOCKS-GAZETTEER K03) -- and it is on
            // a working quay. So the room is a mix and which half a patron is
            // in is a pure function of his id, because a room where everybody
            // wears the same coat is the defect this is fixing.
            return (id & 1) == 0 ? sim::WardType::Shopkeeper : sim::WardType::Serf;
    }
}

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
    if (config_.spawnPitchGiven) {
        body_->setPitch(config_.spawnPitch);
    }
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
    // #78: AND THE PEOPLE. Registered AFTER the roll and the taproom, so
    // registration order inside the Actors phase is a stated fact rather than
    // an accident of which line was typed first, and so the bodies decide
    // against a ward whose day has already turned.
    auto people = std::make_unique<sim::WardPopulation>(*tiles_, timeOfDay_, config_.worldSeed,
                                                        config_.contentDir);
    people_ = people.get();
    engine_->register_system(std::move(people));
    actorSheet_ = ActorSheet::load(config_.contentDir);
    // THE WARD MAP's boot-time facts: the plan's per-material tones derived
    // from the atlas the first-person pass already draws with, and the
    // authored extent with the VOID border ring cropped away. Once each --
    // see map_view.hpp.
    mapPalette_ = MapPalette::fromAtlas(atlas_);
    mapBounds_ = mapContentBounds(*tiles_);
    // The one wire between the two: a rival who rises far enough petitions the
    // Flame for a vacant charge, and the roll is where that becomes true.
    tavern_->attachRoll(ward_);
    // RADIANT BUILD: and the wire the other way -- the room learns the
    // district's own people, so the radiant board posts off the live roster
    // (TASK #81's generator, constructed by a windowed client for the first
    // time). Attached before boot, exactly as the roll is, so the first
    // day's errands are up before anybody has taken a step.
    tavern_->attachPeople(people_);
    engine_->boot();
    // S10: AND THE CASE. The bloodletter trail is the reason to be in the
    // district at all -- see sim/casebook.hpp. The raws are a member because
    // Casebook borrows them for its whole life, and a missing casebook.json
    // leaves an empty trail rather than refusing to boot, which is the contract
    // every raws loader in this build honours.
    caseRaws_ = sim::CasebookRaws::load(config_.contentDir);
    // TASK #82. `caseNowSeconds()` needs timeOfDay_ set, which the block above
    // already did, so begin() is stamped with the game's real starting hour
    // rather than the 0-default every pre-#82 caller still gets for free.
    casebook_.begin(caseRaws_, caseNowSeconds());
    // TASK #82. THE LETTERS. A second, independent raws loader -- see
    // sim/letters.hpp's own header on why a second file rather than a second
    // schema bolted onto casebook.json. No `begin()` here: a Letter carries
    // no state of its own, so there is nothing to bind besides the raws
    // themselves, which unlockedLetters() reads straight off casebook_.
    letterRaws_ = sim::LetterRaws::load(config_.contentDir);
    // COURIER CASE. THE SECOND CASE AND ITS PAPER, loaded the same never-
    // throws way and bound the same way -- but with NO start lead in its
    // file, begin() leaves every lead Unheard, so the errand is invisible
    // until the courier beat calls hear(). A missing mission_sheet.json is
    // a session with one case in it, exactly as before this build.
    sheetRaws_ = sim::CasebookRaws::loadFile(sim::missionSheetRawsPath(config_.contentDir));
    sheetBook_.begin(sheetRaws_, caseNowSeconds());
    sheetLetterRaws_ =
        sim::LetterRaws::loadFile(sim::missionSheetLetterRawsPath(config_.contentDir));
    // EVICTION CASE. THE THIRD CASE AND ITS PAPER, the courier trio's exact
    // contract: never-throws loaders, begin() with no start lead so every
    // lead sits Unheard and the errand is invisible until Maell's own
    // conversation opens it (settleTakeWrit). A missing eviction.json is a
    // session with two cases in it, exactly as before this build.
    evictRaws_ = sim::CasebookRaws::loadFile(sim::evictionRawsPath(config_.contentDir));
    evictBook_.begin(evictRaws_, caseNowSeconds());
    evictLetterRaws_ =
        sim::LetterRaws::loadFile(sim::evictionLetterRawsPath(config_.contentDir));
    // THE FIRST RUN OPENS ON THE HOOK.
    //
    // Every sprint before this one dropped the player onto the Tarwalk facing a
    // door with no idea who they were or what they were for. So a fresh session
    // starts with the notes up: the case, the body that started it, and the one
    // lead the ward has given you. Anything the player does closes it -- see
    // Session::step and the four verbs -- so it costs a keypress at most and
    // never gets in the way twice.
    // #77. The field of view the session was ASKED for becomes the field of
    // view the options slider starts on, so --fov and the in-game slider are the
    // same number rather than two that disagree.
    controls_.fovDegrees = config_.fovDegrees;
    controls_.sanitise();
    if (config_.openingPage && caseRaws_.loaded()) {
        casebookOpen_ = true;
        // #85. UPDATED FOR THE CONSOLIDATED SCHEME: Keys and Options are pages
        // inside Menu now rather than their own F1/F2, and Examine is folded
        // into Interact. SHIP NOTE MOVE 3 CLOSED THE S3 VERIFICATION GAP this
        // comment used to state: the hint is GENERATED from the live bindings
        // now (openingHintLine, above), so a rebound Menu renames itself here
        // by construction -- and noteInputDevice() re-words it live when a
        // pad speaks while it is still up. On the shipped table this reads
        // "J NOTES  M MAP  E USE" -- J per the owner's own "use J for
        // journal since that's how it's done by convention".
        //
        // UI-EA (LANE HUD): FOUR SECONDS, NOT TWELVE. The hint is tutor
        // tier now -- the message-row narration hold, not a twelfth of a
        // minute of furniture. A device change while it is still on screen
        // re-words it AND re-raises it whole (noteInputDevice -- the tutor
        // tier's own "raised in full on device change"), so the four
        // seconds are four seconds in whichever hand holds the machine.
        message_ = openingHintLine(controls_, promptDevice_);
        messageSteps_ = 60 * 4;
    }
    // TASK #83. SNAPPED, NOT EASED. Nobody pressed a key to reach whichever of
    // these is true on frame one -- SessionConfig chose it -- so there is
    // nothing to animate from and the panel/alert draw at full strength
    // immediately, exactly as they always have. See EasedToggle::snapTo.
    panelAnim_.snapTo(conversingNow());
    alertAnim_.snapTo(!message_.empty());
    // DISTRICT PHASE D. SEEDED FROM WHERE THE BODY ACTUALLY IS, BEFORE
    // syncPanelAnim() BELOW EVER RUNS -- lastPlayerHp_'s own reasoning,
    // verbatim: a session that boots the player already standing in Tarwalk
    // (or, with --spawn, anywhere else that has a name) has not CROSSED into
    // it, and an empty lastPlaceName_ here would read the very first
    // syncPanelAnim() call as a crossing and open the game on a plate nobody
    // walked through a gate to earn. Nothing announces on frame one, ever.
    lastPlaceName_.assign(
        sim::docks::placeNameAt(body_->tileX(), body_->tileY(), body_->band()));
    syncTavernToBody();
    // HARDENING PASS. THE SAME SNAP, GENERALIZED. syncPanelAnim() below sets
    // the target and caches the text for every other row this pass eases too
    // (see its own header) -- calling it here is a no-op for panelAnim_/
    // alertAnim_, which are already sitting exactly on the target just
    // snapped above (EasedToggle::setTarget is idempotent), but the eight
    // new toggles have never been touched and would otherwise arm their own
    // opening bump on a session that starts with, say, a rung already held
    // or the player already standing inside the Gull. Snapped to whatever
    // target it just computed, for the identical reason panelAnim_/
    // alertAnim_ are snapped instead of merely targeted, above.
    syncPanelAnim();
    interactAnim_.snapTo(interactAnim_.target());
    lockAnim_.snapTo(lockAnim_.target());
    caseAnim_.snapTo(caseAnim_.target());
    roomAnim_.snapTo(roomAnim_.target());
    rivalAnim_.snapTo(rivalAnim_.target());
    guildAnim_.snapTo(guildAnim_.target());
    objectiveAnim_.snapTo(objectiveAnim_.target());
    stealthAnim_.snapTo(stealthAnim_.target());
    // PLANNING SPRINT (item #2, the sweep). THE SAME SNAP, FOR THE THREE ROWS
    // THE SWEEP FOUND. See standingAnim_'s own header.
    standingAnim_.snapTo(standingAnim_.target());
    heatAnim_.snapTo(heatAnim_.target());
    stashAnim_.snapTo(stashAnim_.target());
    // UI-EA (LANE HUD). The earned-text toggles: syncPanelAnim() above has
    // just seeded every last-seen edge (hudEdgesSeeded_) and armed nothing,
    // so all three targets are the family's frame-one truth -- closed.
    clockAnim_.snapTo(clockAnim_.target());
    purseAnim_.snapTo(purseAnim_.target());
    wheelHint_.anim.snapTo(wheelHint_.anim.target());
    // HELD-EFFECTS BUILD. The same snap, one per slot -- a session cannot
    // boot with a hold live today, but the rule is "snap to whatever
    // syncPanelAnim() just chose", not "assume empty".
    for (EasedToggle& anim : effectAnims_) {
        anim.snapTo(anim.target());
    }
    // FATIGUE BUILD. The same snap: syncPanelAnim() just targeted the bar's
    // real frame-one state (up unless the session booted mid-conversation),
    // and there is nothing to fade in from.
    fatigueAnim_.snapTo(fatigueAnim_.target());
    // THE WARD MAP. The same snap: a session never boots with the map up
    // today, but the rule is "snap to whatever syncPanelAnim() just chose",
    // not "assume closed".
    districtMapAnim_.snapTo(districtMapAnim_.target());
    // DISTRICT PHASE D. The same snap, and with lastPlaceName_ seeded above
    // the target syncPanelAnim() just computed is always closed -- which is
    // the point: the plate cannot arm its own opening bump on frame one.
    placePlateAnim_.snapTo(placePlateAnim_.target());
    // THE CASEBOOK PASS. The same snap, and with nothing ever having been
    // looked at on frame one the target syncPanelAnim() just chose is always
    // closed -- which is the point: the notice cannot arm its own opening bump
    // on the first frame of a session.
    casePlateAnim_.snapTo(casePlateAnim_.target());
    // INNOVATION SPRINT ITEM #2. SNAPPED, FOR THE IDENTICAL REASON THE ROWS
    // ABOVE ARE. NOT a hardcoded "journal starts focused" -- syncPanelAnim()
    // just computed the real answer off casebookOpen_/menuFocus_ (both true
    // only when config.openingPage put the Menu up focused on Journal from
    // the start; false the whole ordinary way a session boots, which
    // --smoke's captures always are, since openingPage is a run_client-only
    // default). Snapping to whatever setTarget() just chose is what makes
    // this correct in both cases without a special case for either.
    characterFocusAnim_.snapTo(characterFocusAnim_.target());
    mapFocusAnim_.snapTo(mapFocusAnim_.target());
    lettersFocusAnim_.snapTo(lettersFocusAnim_.target());
    journalFocusAnim_.snapTo(journalFocusAnim_.target());
    // INNOVATION SPRINT ITEM #3. THE STARTING HIT POINTS, NOT A HARD-CODED
    // 100 -- see lastPlayerHp_'s own header on why a session that boots the
    // player already hurt must not read its own first frame as a fresh hit.
    lastPlayerHp_ = tavern_->playerHp();
    // AUDIO WIRING. The starting purse, for the identical reason: a session
    // that boots with coin in the pocket has not just been paid.
    lastCoinForAudio_ = tavern_->playerCoin();
}

void Session::setAudio(audio::AudioEngine* engine) {
    audio_ = engine;
    if (audio_ == nullptr) {
        return;
    }
    // Baselines re-read at attach, so nothing that happened while no engine
    // was listening is replayed as if it just happened.
    lastCoinForAudio_ = tavern_->playerCoin();
    audioPanelWasMenu_ = casebookOpen_;
    lastHandsUp_ = tavern_->playerHandsUp();
    lastClosedBooks_ = closedBookCount();
    // And the bed starts the moment there are ears: the same
    // playerInside()-keyed choice step() re-asserts every step (re-asserting
    // the current bed is a documented no-op), so a body standing still on the
    // quay hears the harbour without having to move first.
    audio_->startBed(tavern_->playerInside() ? audio::BedId::Interior
                                             : audio::BedId::Harbour);
    // THE LOT PASS: and the music's zone, on the same signal.
    audio_->music().setZone(tavern_->playerInside() ? audio::MusicZone::Interior
                                                    : audio::MusicZone::Docks);
    audio_->setTimeOfDay(timeOfDay_);
    // WEATHER: and the wind, on the same one-way wire the clock rides. The
    // client re-pushes both every frame; this is the value the first frame
    // has before it does.
    audio_->setWind(weather().windGain());
}

Weather Session::weather() const noexcept {
    if (config_.liveWeather) {
        // The tavern's day is the ward's one monotonic calendar (see
        // Tavern::dayNumber); the session's clock is the same clock the
        // tavern keeps. Both read, neither written.
        return weatherFor(config_.worldSeed, tavern_->dayNumber(), timeOfDay_);
    }
    Weather pinned;
    pinned.kind = config_.weather;
    pinned.intensity = config_.weather == WeatherKind::Clear ? 0.0F : 1.0F;
    return pinned;
}

SkyState Session::sky() const noexcept { return skyAt(timeOfDay_, weather()); }

void Session::syncTavernToBody() {
    tavern_->setPlayer(body_->x(), body_->y(), body_->band());
    // ACTION-COMBAT BUILD. WHICH WAY THE BODY IS FACING, pushed beside the
    // position the same step. The sightline raycast (VETO 1) casts down
    // playerYaw_, so facing is authoritative exactly the way position is -- and
    // without this every swing and touch-cast aims due-north (yaw 0), which is
    // the SIM lane's one downstream requirement of this file. A room the client
    // never syncs (a pre-combat capture, the workload) keeps its default facing.
    tavern_->setPlayerYaw(body_->yaw());
}

void Session::setControls(const ControlSettings& settings) {
    // THE OPENING HINT FOLLOWS THE TABLE IT NAMES KEYS FROM -- the same
    // still-the-hint re-wording noteInputDevice does, for the same reason:
    // the hint is the one STORED prompt, and the constructor worded it off
    // the shipped defaults before the settings file arrived here. Without
    // this, a file that rebinds Menu leaves the first thing a player reads
    // naming the old key for its twelve seconds.
    const bool hintUp = !message_.empty() && message_ == openingHintLine(controls_, promptDevice_);
    controls_ = settings;
    controls_.sanitise();
    if (hintUp) {
        message_ = openingHintLine(controls_, promptDevice_);
    }
    // The camera reads the field of view every frame off controls_, so a
    // settings file with an FOV in it is applied by the act of loading it and
    // there is no second copy to forget to update.
}

std::uint64_t Session::simHash() const {
    // The engine's own combined hash (the world plus every registered system:
    // the Gull with the player's whole sheet inside it, the ward, the
    // population) folded with the three books -- the set of everything the
    // twin-run gate protects, so a render act that moved any of it would
    // move this number. test_pull.cpp compares it with and without a
    // followed lead, a fronted case and a toast on screen.
    sim::WorldHasher hasher;
    engine_->hash_into(hasher);
    sim::HashSink& books = hasher.section_sink(0x50554C4Cu);  // 'PULL'
    casebook_.hashInto(books);
    sheetBook_.hashInto(books);
    evictBook_.hashInto(books);
    books.put_int(static_cast<std::uint32_t>(body_->tileX()));
    books.put_int(static_cast<std::uint32_t>(body_->tileY()));
    books.put_int(static_cast<std::uint32_t>(body_->band()));
    return hasher.combined_hash();
}

std::string_view Session::alertSpeakerOf(std::string_view line) noexcept {
    // The sim quotes a body as "<name>: <row>" on the alert row (the halt,
    // the join, the flee, the arrest line, a reply); the name is the prefix.
    const std::size_t colon = line.find(": ");
    if (colon == std::string_view::npos || colon == 0) {
        return {};
    }
    return line.substr(0, colon);
}

void Session::step(const sim::MoveInput& input) {
    // CASE WATCH. Recorded with the yaw the body carries INTO the step,
    // because the scripted walker (stepToward) steers by writing yaw directly
    // between steps -- an input tape without the yaw would replay every walk
    // in a straight line. Replay restores the yaw, then steps.
    if (watchTape_ != nullptr && watchDepth_ == 0) {
        WatchOp op;
        op.kind = WatchOpKind::Step;
        op.move = input;
        op.a = body_->yaw();
        watchTape_->push_back(op);
    }
    const WatchDepthGuard watchGuard(watchDepth_);
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
    // HELD-EFFECTS BUILD. AGI's gait reader, RE-READ EVERY STEP off the
    // effective sheet: a held Steady the Hand moves the legs the same step it
    // moves the sheet, and moves them back the second it lapses -- continuous
    // state driving continuous motion, the same seam applyPlayerAttributes
    // crossed at boot. Exactly the shipped scale whenever no hold is live, so
    // every pre-existing capture walks bit for bit.
    // KIT BUILD. THE LOAD, at the same seam: AGI's gait times the Q8 term
    // the drams on the body earn (items.hpp's loadSpeedScaleQ8 -- 256 to
    // half the budget, down to 128 at it), >> 8. Exactly the shipped scale
    // with nothing on the body, so every pre-existing capture walks bit for
    // bit; player-only, integer, the one movement seam the gap analysis
    // named. PlayerBody clamps the product to its own 64..512.
    body_->setSpeedScaleQ8(static_cast<std::int32_t>(
        (static_cast<std::int64_t>(sim::agilitySpeedScaleQ8(
             tavern_->effectiveAttributes().value(sim::AttributeId::Agility))) *
         tavern_->loadSpeedQ8()) >>
        8));
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
    // JUSTICE BUILD: THE PLAYER CANNOT WALK OUT OF CUSTODY (PLAY-MODE-SPEC's
    // own rule -- "you cannot just walk out of custody by holding a key").
    // While the hearing page, the rope's plate or the end rows own the frame
    // the movement input is dropped HERE, before the body hears it, so a
    // held key and a scripted stepMany alike move nothing; the world keeps
    // stepping underneath, as it does under every page.
    sim::MoveInput moved = inCustody() ? sim::MoveInput{} : input;
    moved.crouch = tavern_->stance() == sim::Stance::Crouched;
    const bool walking = moved.forward != 0 || moved.strafe != 0;
    // FATIGUE BUILD: THE SPRINT GATE, before the body ever hears the key. An
    // empty pool downgrades the sprint to the jog -- the input is edited, not
    // the body interrupted, so the legs brake through their own ordinary ramp
    // -- and the refusal is said ONCE per stretch of windedness (see
    // windedSprintSaid_'s own header on the restraint). The automatic climb
    // is gated by the same fact for the same reason the explicit verb is in
    // tryClimb(): no wind, no haul -- the wall just stops you, silently,
    // because the auto path fires sixty times a second against a held key.
    if (tavern_->playerWinded()) {
        if (moved.sprint && !moved.crouch && walking && !windedSprintSaid_) {
            say("TOO WINDED TO SPRINT.");
            windedSprintSaid_ = true;
        }
        moved.sprint = false;
        moved.autoTraverse = false;
    } else {
        windedSprintSaid_ = false;
    }
    const bool sprinting = moved.sprint && !moved.crouch;
    tavern_->setPlayerMotion(walking, sprinting);
    // The pool's own step, with the SAME flags the stealth model was just
    // told: the sprinting step pays its drain, every other one regenerates
    // (halved while the legs are working). See Tavern::stepPlayerFatigue.
    tavern_->stepPlayerFatigue(walking, sprinting);
    // FIRST-PERSON COMBAT (S13). THE EFFECTIVE GUARD, PUSHED EVERY STEP the
    // identical way the motion above is: the client only reports the key's
    // physical edge (setBlocking -> blockHeld_), and what the ROOM is told is
    // derived here so a page opening mid-hold lowers the guard the same step
    // it takes the keyboard, and a page closing under a still-held key raises
    // it again -- no edge event exists for either of those moments.
    // ACTION-COMBAT BUILD (section 1.3): the guard holds only while the hand is
    // FREE. A swing opens you -- the vulnerable window is the Oblivion rhythm --
    // so the derived block state gains the combat-idle clause, and the GUARD UP
    // row honestly vanishes for the ~15-step swing because the row reads this
    // very state (blockLine -> playerBlocking). The caught-blow wind cost is
    // the SIM's (kBlockCatchFatiguePoints, drained in stepBrawl), not ours.
    tavern_->setPlayerBlocking(blockHeld_ && !talking() && !picking() &&
                               tavern_->playerCombatIdle());
    // STANCE & ROOM BUILD (oblivion-roadmap.md 3.2, the cancel list). A page
    // owning the keyboard -- any page, a conversation, the wire in a lock --
    // drops a live charge AND lowers the hands, pushed every step the identical
    // way the guard above is derived: no edge event exists for a page opening
    // mid-hold, so the room is told the fact rather than the keypress. The sim
    // lowers them itself for talking, picking, sleeping, arrest and defeat;
    // this is the one clause only the client can see (its own pages).
    if (picking() || conversingNow()) {
        tavern_->cancelPlayerCharge();
        tavern_->lowerPlayerHands();
    }
    body_->step(moved);
    syncTavernToBody();

    // AUDIO WIRING: FOOTSTEPS, exactly per audio_engine.hpp's plan -- the
    // material of the tile UNDER the feet, whether the body is at a run, and
    // the wading layer at the feet themselves. Called every step while the
    // input says "moving" (the engine rate-limits to a walk/run cadence
    // internally, so this is the documented legal wiring); airborne feet
    // touch nothing and say nothing.
    if (audio_ != nullptr && walking && !body_->airborne()) {
        const std::int32_t px = body_->tileX();
        const std::int32_t py = body_->tileY();
        const std::int32_t pz = body_->band();
        (void)audio_->footstep(tiles_->material(px, py, pz - 1),
                               moved.sprint && !moved.crouch,
                               tiles_->fluidDepth(px, py, pz));
    }

    // #77. A CLIMB NOBODY PRESSED A KEY FOR IS STILL A CLIMB, and the room
    // charges it exactly as it charges one that was asked for: the craft, the
    // Skyrunners' regard, the roof-run the ward would have minded, and the fall
    // if the far side turned out to be lower. Routing the automatic path through
    // a different settlement than the explicit one is how the two quietly stop
    // agreeing about what a roof-run is.
    const sim::RoofResult climbed = body_->takeAutoMove();
    if (climbed.ok()) {
        // FATIGUE BUILD: a climb nobody pressed a key for still costs wind,
        // through the identical charge the explicit verb pays in tryClimb()
        // -- routing the two through different bills is how they would
        // quietly stop agreeing about what a climb costs.
        tavern_->chargePlayerMantle();
        roofMove_ = "UP AND OVER.";
        settleLanding(climbed);
        say(roofMove_);
        syncTavernToBody();
    }

    // THE WATCH TOOK YOU AND HAS LET YOU GO. The room owns the sentence, the
    // seizure and the clock; the BODY is this file's, so the walk to the
    // impound and the morning at its gate happen here -- which is to say they
    // do not happen at all, and that is stated rather than implied.
    //
    // JUSTICE BUILD (HEARING PAGE LANE). THE SAME RELEASE, THREE MEANINGS:
    // with a hearing pending it is "to the Mission's door" (openCourt below
    // takes the body there off the pending record); after the page served a
    // sentence it is "turned loose" -- at the Mission's door or on the
    // Tarwalk, by the terms; and a paperless search is the shipped Tarwalk
    // release exactly as it always was.
    if (tavern_->takeArrestRelease()) {
        if (courtServing_) {
            closeCourt();
        } else if (tavern_->hearingPending()) {
            // TAKEN, WITH PAPER -- THE ARREST HAS ITS MOMENT ON SCREEN before
            // the bench does anything (JUSTICE-SPEC 2.2): the officer's own
            // line (the room's watch.held / maimed / condemned row) said on
            // the alert row with his hand on you and the room still around
            // you, and held; then the plate; then the page. The body stays
            // where he took it until the cut. Nothing here touches the sim:
            // the hearing is already open on the ledger.
            say(tavern_->lastArrest().line);
            (void)voice_.dropIfSpeaker(alertSpeakerOf(tavern_->lastArrest().line));
            takenHold_ = kTakenOfficerSteps + kTakenPlateSteps;
            takenPlate_.clear();
        } else {
            placeBodyAt(sim::gull::kStreetX, sim::gull::kStreetY, sim::gull::kGroundBand);
            awaitingLanding_ = false;
            syncTavernToBody();
            say(tavern_->lastArrest().line);
            (void)voice_.dropIfSpeaker(alertSpeakerOf(tavern_->lastArrest().line));
        }
    }
    // THE ARREST BEAT, spent once a step. At the cut the plate is written
    // off the live clock and said (it is the line the spec promised, and
    // lastMessage() carries it for the drive); when the hold runs out the
    // page opens over it and the plate goes.
    if (takenHold_ > 0) {
        --takenHold_;
        if (takenHold_ == kTakenPlateSteps) {
            takenPlate_ = "TAKEN TO THE MISSION. " + travelClockText(timeOfDay_) + ".";
            say(takenPlate_);
        }
        if (takenHold_ == 0) {
            takenPlate_.clear();
            openCourt();
        }
    }
    // TAKEN. A hearing open on the ledger with no page up and no beat
    // running -- a record reopened between the arrest and the plea -- opens
    // the bench at once: the page is DERIVED from the hashed record, never
    // the other way round.
    if (!courtOpen_ && takenHold_ <= 0 && !courtServing_ && !ropeCeremonyUp() &&
        tavern_->hearingPending() && !tavern_->executed()) {
        openCourt();
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

    // TASK #83. THE STRING OUTLIVES ITS OWN COUNTDOWN BY A FEW STEPS, ON
    // PURPOSE. message_ used to clear the instant messageSteps_ reached zero,
    // which is exactly the pop this pass exists to close: an alpha cannot
    // fade a string that is already gone. So the countdown alone decides
    // whether the alert is WANTED (see syncPanelAnim, just below) and the
    // string itself is only cleared once alertAnim_ has actually finished
    // easing down to nothing -- which is a handful of steps later, not the
    // same step.
    // THE RUNG PLATE STOPS THIS CLOCK (rung_plate.hpp's rule): a line said
    // with the rung stands at half weight under the plate and gets what it
    // has left, capped, once the plate lifts -- stepRungPlate caps it.
    if (messageSteps_ > 0 && (!rungPlateOnScreen() || !messageHeld_)) {
        --messageSteps_;
    }

    // ACTION-COMBAT BUILD (section 4.5). THE DEATH CEREMONY's own hold, spent
    // once a step like every other countdown -- see composeDeathCeremony() for
    // the dip, the epitaph hold and the reveal it paces. The sim already
    // revived at the defeat (settleDefeat); this is only the veil over the top,
    // so nothing here touches sim state and the player is free underneath it.
    if (deathCeremonySteps_ > 0) {
        --deathCeremonySteps_;
    }
    // JUSTICE BUILD. The judgment's held badge, and the rope's dip and
    // plate, spent once a step the same way. When the plate's hold runs out
    // the end rows come up under it; nothing fades back.
    if (courtJudgmentHold_ > 0) {
        --courtJudgmentHold_;
    }
    if (ropeCeremonySteps_ > 0) {
        --ropeCeremonySteps_;
        if (ropeCeremonySteps_ == 0) {
            ropeRowsUp_ = true;
            ropeCursor_ = 0;
        }
    }

    // THE SPEECH REVEAL CLOCK. A courtesy for whoever is watching the screen
    // live, not a fact about the world -- see lastSpokenLine_'s own note. It
    // ticks only while a real conversation is open: the casebook, the keys
    // page and the options page all borrow this same widget to print
    // reference material nobody said out loud, and typing THAT out would be
    // decoration doing the opposite of its job. The clock resets the instant
    // the line changes, so a fresh reply always arrives fresh rather than
    // picking up wherever the last one left off.
    if (tavern_->dialogue().isOpen()) {
        const std::string& spoken = tavern_->dialogue().lastLine();
        if (spoken != lastSpokenLine_) {
            lastSpokenLine_ = spoken;
            speechAgeSteps_ = 0;
        } else if (speechAgeSteps_ < kSpeechRevealSteps) {
            ++speechAgeSteps_;
        }
    } else if (!lastSpokenLine_.empty()) {
        lastSpokenLine_.clear();
    }

    // TASK #83. THE PANEL AND THE ALERT EASE ONCE A STEP, IN STEP. Re-reading
    // the target every step (rather than only from the toggle methods that
    // set it eagerly) is what catches a conversation the SIMULATION closed --
    // a bouncer walking somebody out, say -- and not only one a keypress did.
    syncPanelAnim();
    panelAnim_.advance();
    alertAnim_.advance();
    // HARDENING PASS. THE SAME PER-STEP ADVANCE, ONE PER ROW.
    interactAnim_.advance();
    lockAnim_.advance();
    caseAnim_.advance();
    roomAnim_.advance();
    rivalAnim_.advance();
    guildAnim_.advance();
    objectiveAnim_.advance();
    stealthAnim_.advance();
    // PLANNING SPRINT (item #2, the sweep). THE SAME PER-STEP ADVANCE.
    standingAnim_.advance();
    heatAnim_.advance();
    stashAnim_.advance();
    // FIRST-PERSON COMBAT (S13). THE SAME PER-STEP ADVANCE.
    spellAnim_.advance();
    blockAnim_.advance();
    // ACTION-COMBAT BUILD. THE SAME PER-STEP ADVANCE for the HELD HARD row.
    chargeAnim_.advance();
    // STANCE & ROOM BUILD. THE SAME PER-STEP ADVANCE for the FISTS UP row.
    handsAnim_.advance();
    // VOICE LANE. THE SAME PER-STEP ADVANCE for the voice row.
    voiceAnim_.advance();
    // KIT BUILD. THE SAME PER-STEP ADVANCE for the turn row, and its hold
    // counted down here so a plate's worth of steps is the row's life.
    turnAnim_.advance();
    if (turnSteps_ > 0) {
        --turnSteps_;
    }
    // HELD-EFFECTS BUILD. THE SAME PER-STEP ADVANCE, ONE PER SLOT.
    for (EasedToggle& anim : effectAnims_) {
        anim.advance();
    }
    // FATIGUE BUILD. THE SAME PER-STEP ADVANCE.
    fatigueAnim_.advance();
    // THE WARD MAP (core action #13). THE SAME PER-STEP ADVANCE.
    districtMapAnim_.advance();
    // FAST TRAVEL's arrival seam eases back down here, one step at a time,
    // exactly like every sibling -- see travelFadeAnim_'s own header.
    travelFadeAnim_.advance();
    // SPELLS BUILD. The strip's own countdown and ease -- see showQuickBar().
    if (quickBarShowSteps_ > 0) {
        --quickBarShowSteps_;
    }
    quickBarAnim_.advance();
    // UI-EA (LANE HUD): THE EARNED-TEXT COUNTDOWNS, spent HERE and only
    // here, once a step -- placePlateShowSteps_'s own rule, whole family:
    // syncPanelAnim() can run several times in one step and a decrement in
    // there would make a row's 2.5 seconds depend on how many keys were
    // pressed during them.
    if (clockShowSteps_ > 0) {
        --clockShowSteps_;
    }
    if (purseShowSteps_ > 0) {
        --purseShowSteps_;
    }
    if (caseShowSteps_ > 0) {
        --caseShowSteps_;
    }
    if (roomShowSteps_ > 0) {
        --roomShowSteps_;
    }
    if (guildShowSteps_ > 0) {
        --guildShowSteps_;
    }
    if (objectiveShowSteps_ > 0) {
        --objectiveShowSteps_;
    }
    clockAnim_.advance();
    purseAnim_.advance();
    wheelHint_.advance();
    // UI-EA contract (c), joined at integration: the page tutor bands.
    // Raised in full on the page's own open edge and on any wake FLOW
    // signals (a device change, a press the page did not recognize), they
    // hold kTutorHoldSteps and ease back down to bare keycaps. Advanced
    // here, once a step, the family's own rule.
    {
        const bool mapUp = districtMapOpen_;
        const bool bookUp = casebookPageOpen();
        const bool keysUp = keysOpen_;
        const bool courtUp = courtOpen_;
        if (courtUp && !courtTutorWasOpen_) {
            courtTutor_.raise();
        }
        courtTutorWasOpen_ = courtUp;
        if (mapUp && !mapTutorWasOpen_) {
            mapTutor_.raise();
        }
        if (bookUp && !casebookTutorWasOpen_) {
            casebookTutor_.raise();
        }
        if (keysUp && !keysTutorWasOpen_) {
            keysTutor_.raise();
        }
        mapTutorWasOpen_ = mapUp;
        casebookTutorWasOpen_ = bookUp;
        keysTutorWasOpen_ = keysUp;
        if (tutorWakeSerial_ != tutorWakeSeen_) {
            tutorWakeSeen_ = tutorWakeSerial_;
            if (mapUp) {
                mapTutor_.raise();
            }
            if (bookUp) {
                casebookTutor_.raise();
            }
            if (keysUp) {
                keysTutor_.raise();
            }
            if (courtUp) {
                courtTutor_.raise();
            }
        }
        courtTutor_.sync(!courtUp);
        courtTutor_.advance();
        mapTutor_.sync(!mapUp);
        casebookTutor_.sync(!bookUp);
        keysTutor_.sync(!keysUp);
        mapTutor_.advance();
        casebookTutor_.advance();
        keysTutor_.advance();
    }
    // DISTRICT PHASE D. The plate's own countdown and ease -- the strip's
    // shape directly above, for the strip's reason. The countdown runs down
    // HERE and only here, once a step: syncPanelAnim() can be called several
    // times in one step (every toggle calls it) and a decrement in there would
    // make the plate's two seconds depend on how many keys were pressed
    // during them.
    // THE ANNOUNCE SLOT WAITS BEHIND THE RUNG PLATE (rung_plate.hpp's rule):
    // neither plate's clock runs while the rung plate is on screen, and their
    // targets drop with it (syncPanelAnim), so a notice already up
    // eases out, waits, and rises again on its own ease with what it had.
    if (placePlateShowSteps_ > 0 && !rungPlateOnScreen()) {
        --placePlateShowSteps_;
    }
    placePlateAnim_.advance();
    // THE CASEBOOK PASS. The lead-opened notice, on the identical countdown and
    // for the identical reason: run down HERE and only here, once a step, so
    // its three seconds do not depend on how many keys were pressed during
    // them.
    if (casePlateShowSteps_ > 0 && !rungPlateOnScreen()) {
        --casePlateShowSteps_;
    }
    casePlateAnim_.advance();
    // THE RUNG PLATE: the Legend diff, the queue and the hold -- BEFORE the
    // pull's own hook, so a rung and a skill level earned in the same step
    // resolve with the plate up and the toast waiting behind it.
    stepRungPlate();
    stepPull();  // THE PULL PACK: the one step hook (render/pull.hpp).
    // INNOVATION SPRINT ITEM #2. THE SAME PER-STEP ADVANCE, ONE PER TILE.
    characterFocusAnim_.advance();
    mapFocusAnim_.advance();
    lettersFocusAnim_.advance();
    journalFocusAnim_.advance();
    // INNOVATION SPRINT ITEM #3. A BLOW LANDING ON THE PLAYER, CAUGHT BY
    // COMPARISON RATHER THAN A NEW SIM-SIDE FLAG. tickBrawl() (tavern.cpp)
    // can drop tavern_->playerHp() during the stepMovement() call already
    // made above, from any of however many opponents are still swinging --
    // there is no single call site in THIS file that "throws the punch" the
    // way Session::punch() is for the player's own, so the render layer
    // reads the one number it already reads for the HUD's health bar
    // (drawFrame's hud.health) and watches it for a drop instead. Purely a
    // comparison of already-public state; nothing new reaches into the
    // simulation and nothing here is hashed -- see punchTakenPulse_'s own
    // header.
    const std::int32_t hpNow = tavern_->playerHp();
    // FIRST-PERSON COMBAT (S13). A GUARDED HIT IS ITS OWN, QUIETER MOMENT.
    // blowsBlocked() moving in the same step the hp dropped means the guard
    // caught what landed, so the cool blockPulse_ fires INSTEAD of the
    // blooded punchTakenPulse_ -- one wash per blow, per the restraint note
    // in drawFrame(), and the guard working reads different from the guard
    // failing. The same comparison-not-flag shape as lastPlayerHp_ itself.
    const std::int32_t blockedNow = tavern_->blowsBlocked();
    if (hpNow < lastPlayerHp_) {
        if (blockedNow > lastBlowsBlocked_) {
            blockPulse_.trigger();
            // AUDIO WIRING: the guard's clang, on the identical edge the cool
            // wash fires on -- one sound per blow, same as one wash per blow.
            if (audio_ != nullptr) {
                audio_->playOneShot(audio::SoundId::SwordClash);
            }
        } else {
            punchTakenPulse_.trigger();
            // AUDIO WIRING: the taken hit, beside its blooded wash -- and THE
            // LOT PASS lays the owner's own hurt vox over it, the four takes
            // in strict turn.
            if (audio_ != nullptr) {
                audio_->playOneShot(audio::SoundId::ThudMedium);
                audio_->playOneShotRoundRobin(audio::SoundId::PlayerHurt);
            }
        }
        // THE LOT PASS: a blow TAKEN, blocked or not, is a fight the music
        // has to know about.
        if (audio_ != nullptr) {
            audio_->music().noteCombat();
        }
    }
    // 3D BUILD, V LANE. THE HANDS, STEPPED HERE AND NOWHERE ELSE: once per
    // movement step, off the room's own charge and guard state plus the
    // edges latched since the last step. A blow the guard caught keeps the
    // guard up (the block nudge already says it landed); an unguarded blow
    // is the flinch, over everything. The latches clear whether or not the
    // machine took them -- an edge is a fact about THIS step.
    {
        ViewmodelInputs hands;
        hands.chargeSteps = tavern_->playerChargeSteps();
        hands.chargeHard = tavern_->playerChargeHard();
        hands.blocking = tavern_->playerBlocking();
        hands.releasedLight = viewmodelSwingPending_ == 1;
        hands.releasedHard = viewmodelSwingPending_ == 2;
        hands.cast = viewmodelCastPending_;
        hands.hit = hpNow < lastPlayerHp_ && blockedNow <= lastBlowsBlocked_;
        // STANCE & ROOM BUILD meets the 3D build: the room's own hashed
        // fighting-mode bit is what the hands read -- raised fists while
        // it is true, the arms hanging while it is false -- the identical
        // fact the FISTS UP row shows, so the row and the hands can never
        // disagree. Read here, on the step, like every input above.
        hands.handsUp = tavern_->playerHandsUp();
        (void)viewmodel_.step(hands);
        viewmodelSwingPending_ = 0;
        viewmodelCastPending_ = false;
    }
    // KIT BUILD -- DEFENCE v1 ON ITS OWN ROW. A blow the worn kit softened
    // says so, once per blow, on the same edge shape the guard's clang
    // rides: blowsTurned() moving. The piece named is the heaviest DR on the
    // body (the coat over the hood over the boots), the number is what the
    // blow lost to it. Its own centred row (hud.turnLabel) with a plate's
    // hold, not the alert: a bouncer's warning outranks the alert for as
    // long as the house minds you, which is exactly when blows land. An
    // EVENT, never furniture: composed only when a blow actually lands and
    // the kit actually turned some of it.
    if (const std::int32_t turnedNow = tavern_->blowsTurned(); turnedNow > lastBlowsTurned_) {
        std::string piece = "THE KIT";
        std::int32_t best = 0;
        for (const sim::ItemSlot slot :
             {sim::ItemSlot::Body, sim::ItemSlot::Head, sim::ItemSlot::Feet, sim::ItemSlot::Trinket}) {
            const sim::ItemDef* worn = tavern_->items().at(tavern_->kit().worn(slot));
            if (worn != nullptr && worn->dr > best) {
                best = worn->dr;
                piece = worn->name;
            }
        }
        turnLine_ = piece + " TURNS " + std::to_string(tavern_->lastTurned());
        turnSteps_ = kPlateHoldSteps;
    }
    lastBlowsTurned_ = tavern_->blowsTurned();
    lastPlayerHp_ = hpNow;
    lastBlowsBlocked_ = blockedNow;
    // ACTION-COMBAT BUILD (section 4.1). THE STEEL-OUT FLIP, spoken ONCE on the
    // rising edge of the fight crossing the brawl line -- whoever drew it (a
    // player's hard swing on a bloodied man, an NPC drawing, a Kill-intent
    // nemesis). This is the line and the sound that replace the dead refusal
    // "BLADE OUT - THIS IS NOT A BRAWL.": there is no screen to route to, the
    // fight keeps resolving in the world, so the flip is feedback, not a
    // transition. Edge-latched off tavern_->escalated(); clearEscalation()
    // drops the latch and lastEscalated_ follows it down, ready to fire again.
    const bool escalatedNow = tavern_->escalated();
    if (escalatedNow && !lastEscalated_) {
        say("STEEL OUT. THE ROOM STANDS BACK.");
        if (audio_ != nullptr) {
            audio_->playOneShot(audio::SoundId::SwordDraw);
            // THE LOT PASS: steel out IS the combat edge for the music.
            audio_->music().noteCombat();
        }
    }
    lastEscalated_ = escalatedNow;
    // THE LOT PASS: THE STANCE, BOTH EDGES. tavern_->playerHandsUp() is the
    // one hashed bit the room reads for "fighting mode"; it rises on the
    // first swing raised and falls on the lull (kLowerHandsSteps), on LOWER
    // HANDS, and on every cancel-list clause (a page, a conversation, a
    // pick, sleep, arrest, defeat). Rising = the Malbers draw (SwordDraw --
    // the same id the escalation edge speaks, so a fight that draws steel
    // may say it twice, once for the hands and once for the blade); falling
    // = the Malbers store (Sheathe). Latched here regardless of audio_, so an
    // engine attached mid-stance does not replay the edge.
    const bool handsUpNow = tavern_->playerHandsUp();
    if (audio_ != nullptr) {
        if (handsUpNow && !lastHandsUp_) {
            audio_->playOneShot(audio::SoundId::SwordDraw);
        } else if (!handsUpNow && lastHandsUp_) {
            audio_->playOneShot(audio::SoundId::Sheathe);
        }
    }
    lastHandsUp_ = handsUpNow;
    // THE LOT PASS: A CASE CLOSING, caught by comparison across all three
    // books -- the sting is the owner's own victory cue, spoken once.
    const std::int32_t closedBooksNow = closedBookCount();
    if (audio_ != nullptr && closedBooksNow > lastClosedBooks_) {
        audio_->playOneShot(audio::SoundId::CaseClosed);
    }
    lastClosedBooks_ = closedBooksNow;
    punchLandedPulse_.advance();
    punchTakenPulse_.advance();
    blockPulse_.advance();
    // ACTION-COMBAT BUILD. The hard-release camera dip decays here beside its
    // wash siblings; camera() reads its value() as a render-only pitch offset.
    hardSwingDipPulse_.advance();
    alertPulse_.advance();
    // UI-EA-SPEC sec. 3 rule 5 (contract b). THE SAME PER-STEP ADVANCE: the
    // commit beat decays here beside its pulse siblings; the client arms it
    // at commit routing and the page drawers read it on the inverted fill.
    commitPulse_.advance();
    // AUDIO WIRING: the purse and the bed, both by re-read rather than by
    // instrumenting every call site that could move either. Any purse change
    // -- a haggle settled, a pocket picked, rent paid, a bounty collected --
    // is one CoinHandle, the same comparison-not-flag shape the hp watch
    // above uses; and the ambient bed is re-asserted off the one cheap
    // signal this build already keeps for "under a roof"
    // (tavern_->playerInside()), which the engine's own header blesses:
    // re-asserting the current bed is a no-op, so every step is legal.
    if (audio_ != nullptr) {
        const std::int32_t coinNow = tavern_->playerCoin();
        if (coinNow != lastCoinForAudio_) {
            audio_->playOneShot(audio::SoundId::CoinHandle);
        }
        lastCoinForAudio_ = coinNow;
        audio_->startBed(tavern_->playerInside() ? audio::BedId::Interior
                                                 : audio::BedId::Harbour);
        // THE LOT PASS: the music's zone off the identical signal (re-
        // asserting is a no-op there too), and its calm clock, which counts
        // SIM steps -- brawlers standing keep it at zero; ten seconds of
        // nobody swinging and nobody standing is the way back to exploration.
        // Audio-side state only; the sim never reads any of it.
        audio_->music().setZone(tavern_->playerInside()
                                    ? audio::MusicZone::Interior
                                    : audio::MusicZone::Docks);
        audio_->music().step(tavern_->playerInBrawl());
    }
    // NOW the string can go. messageSteps_ reaching zero is what stopped
    // WANTING the alert on screen -- see the note above and syncPanelAnim's
    // own formula -- and alertAnim_ finishing its fade is what stopped
    // NEEDING the string to still be there to fade. Both, not either: a
    // message replaced by a fresh say() mid-fade re-arms messageSteps_ and
    // this never fires, which is correct -- the new line is what should be
    // showing, not a clear racing it.
    if (messageSteps_ == 0 && alertAnim_.settled() && !alertAnim_.target()) {
        message_.clear();
        messageMouth_.clear();
    }
    // THE SAME CLEAR, GENERALIZED. Once a row's own toggle has fully eased to
    // closed there is nothing left to fade, and clearing its cache is what
    // stops an old rung or an old room line from permanently claiming a
    // bottom-band slot it finished fading out of minutes ago -- see
    // BottomBand::take() in hud.cpp, which hands a row a slot by "is this
    // label non-empty", not by its alpha.
    const auto clearIfClosed = [](EasedToggle& anim, std::string& cache) {
        if (anim.settled() && !anim.target()) {
            cache.clear();
        }
    };
    clearIfClosed(interactAnim_, interactCache_);
    // THE CROSSHAIR PASS. The aim prompt's other three parts, cleared on the
    // identical edge and by the identical rule -- one element, one toggle, so
    // a name must not survive the verb it hung off.
    clearIfClosed(interactAnim_, interactSubjectCache_);
    clearIfClosed(interactAnim_, interactNoteCache_);
    if (interactAnim_.settled() && !interactAnim_.target()) {
        interactKindCache_ = AimKind::Nothing;
    }
    clearIfClosed(lockAnim_, lockCache_);
    clearIfClosed(caseAnim_, caseCache_);
    clearIfClosed(roomAnim_, roomCache_);
    clearIfClosed(rivalAnim_, rivalCache_);
    clearIfClosed(guildAnim_, guildCache_);
    clearIfClosed(objectiveAnim_, objectiveCache_);
    clearIfClosed(stealthAnim_, stealthCache_);
    clearIfClosed(standingAnim_, standingCache_);
    clearIfClosed(heatAnim_, heatCache_);
    clearIfClosed(stashAnim_, stashCache_);
    clearIfClosed(spellAnim_, spellCache_);
    clearIfClosed(blockAnim_, blockCache_);
    clearIfClosed(chargeAnim_, chargeCache_);
    clearIfClosed(handsAnim_, handsCache_);
    // VOICE LANE. The row's text goes once its fade has finished, the same
    // rule: a line that finished fading out cannot keep claiming a slot.
    voice_.clearIfSettled(voiceAnim_.settled() && !voiceAnim_.target());
    clearIfClosed(turnAnim_, turnCache_);

    // STREET SENSES (9a completion), AND CROWD FLOW: WHERE THE PLAYER IS,
    // pushed to the district every step the way syncTavernToBody pushes it to
    // the room -- so a frightened body flees AWAY from him (actFlee) and
    // cowers FACING him (actCower) instead of in a drawn direction, and so a
    // blocked body's tryEnter/sidestep refuse to route onto his own tile.
    // Whole tiles; the ward interpolates nothing off it. This is the client
    // side of the ONE declared population field; the sim hashes it, the
    // client only feeds it.
    //
    // PUSHED BEFORE engine_->tick(), NOT AFTER -- an auditor pass caught the
    // staleness the old order left in: pushed after the tick, the ward's own
    // movement this second always ran against WHERE THE PLAYER STOOD AS OF
    // THE START OF THE SECOND, up to a full second behind a player who is
    // sprinting. A body could route onto -- or refuse to leave -- a tile the
    // player had already left a second ago, or fail to refuse one he had
    // already reached. Pushed here, the tick that is about to run reads
    // wherever he is on THIS step, same as every other client-fed field.
    if (people_ != nullptr) {
        people_->setPlayer(body_->tileX(), body_->tileY(), body_->band());
        // STREET SENSES leg (c): and whether he PRESENTS AS A WIELDER -- the
        // tavern's own deference gate, pushed so the street Watch reads the
        // same answer the Gull's does (D6: absolute, Gull and street).
        people_->setPlayerPresentsAsWielder(tavern_->playerPresentsAsWielder());
        // AND WHETHER HE IS INDOORS -- a house's own brawl is not street
        // business (test_scripted_lines.cpp's nemesis line found this: the
        // alarm below already crosses the Gull's open door on purpose, for
        // the ordinary crowd, but the Watch has no jurisdiction over what
        // Watchman Cull's own separate watch already polices).
        people_->setPlayerIndoors(tavern_->playerInside());
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
    // STREET PANIC BUILD (feel/build, 9a). THE STREET REACTS TO WHAT IT CAN
    // SEE, and this is the ONE call site that tells it. The tavern's own
    // violence is read off public state by the comparison-not-flag shape the
    // hp watch above uses, tiered, and handed to the district's people at
    // the player's own tile -- the takeCoinFrom shape: the client moves a
    // WardActor through one named verb and hashes nothing of its own. Three
    // tiers, by severity (ward_actors.hpp names every constant and why):
    //   KILL   a body on the roster died this step -- a non-vermin corpse
    //          appeared. The edge, fired at once.
    //   BLOW   a blow landed on somebody under LETHAL rules this step -- the
    //          room's hp fell while a lethal fight was live or the fight had
    //          escalated. The edge, fired at once. A bar-fight punch is the
    //          house's business and frightens nobody outside.
    //   STEEL  the hands are up with steel in them, or up over a floored man
    //          in reach. Continuous, so it is re-asserted once a simulated
    //          second -- on the ward's own tick, just taken -- and the bubble
    //          follows a man walking the Tarwalk with a blade out.
    // Who actually runs is the ward's rule (same band, in range, line of
    // sight), which is why a killing inside the roofed Gull reaches the street
    // only through the door. people_ is null in a session without a district
    // (the tavern fixture), and then the street is nobody.
    if (people_ != nullptr) {
        std::int32_t roomHp = 0;
        std::int32_t corpses = 0;
        bool flooredInReach = false;
        for (const sim::Actor& actor : tavern_->actors()) {
            if (actor.role() == sim::ActorRole::Vermin) {
                continue;  // a rat is not a man, alive or dead
            }
            roomHp += actor.hp();
            if (actor.activity() == sim::Activity::Dead) {
                ++corpses;
            }
            if (actor.present() && sim::isFloored(actor.activity()) &&
                actor.distanceTo(body_->x(), body_->y()) <= sim::kMeleeReach) {
                flooredInReach = true;
            }
        }
        bool alarmed = false;
        sim::AlarmSeverity severity = sim::AlarmSeverity::Steel;
        if (lastCorpsesForAlarm_ >= 0 && corpses > lastCorpsesForAlarm_) {
            severity = sim::AlarmSeverity::Kill;
            alarmed = true;
        } else if (lastRoomHpForAlarm_ >= 0 && roomHp < lastRoomHpForAlarm_ &&
                   (tavern_->lethalFightLive() || tavern_->escalated())) {
            severity = sim::AlarmSeverity::Blow;
            alarmed = true;
        } else if (stepsThisSecond_ == 0 && tavern_->playerHandsUp() &&
                   (tavern_->playerWeapon() >= sim::kFirstLethalWeapon || flooredInReach)) {
            severity = sim::AlarmSeverity::Steel;
            alarmed = true;
        }
        if (alarmed) {
            people_->alarm(body_->tileX(), body_->tileY(), body_->band(),
                           sim::alarmRadius(severity), severity);
        }
        lastRoomHpForAlarm_ = roomHp;
        lastCorpsesForAlarm_ = corpses;
        // STREET SENSES leg (b) -- THE STREET SWINGS BACK. The blows fighting-
        // back bodies threw at the player on the ward's tick just taken, read
        // off the mailbox and landed on his sheet through the Gull's own
        // player-side rules (Tavern::takeStreetBlow: the hard band off the
        // thrower's roll, the guard, the floor by class, the defeat seam).
        // The reach is re-checked HERE in Q8 -- Actor::distanceTo's own
        // Chebyshev against kMeleeReach, same band -- so a player who stepped
        // back off the tile is whiffed at, never cancelled on: the roll was
        // spent on the ward's side either way, the Gull's committed-swing
        // rule. The hp watch above this block does the wash and the sound.
        for (const sim::StreetBlow& thrown : people_->takeStreetBlows()) {
            const sim::WardActor* attacker = people_->byId(thrown.attackerId);
            if (attacker == nullptr || !attacker->visible() || attacker->band != body_->band()) {
                continue;
            }
            const std::int32_t dx = sim::q8_tile_centre(attacker->x) - body_->x();
            const std::int32_t dy = sim::q8_tile_centre(attacker->y) - body_->y();
            if (std::max(std::abs(dx), std::abs(dy)) > sim::kMeleeReach) {
                continue;  // stepped back: a swing at air
            }
            sim::Fighter sheet;
            sheet.actorId = sim::kWardSpeakerIdBase + attacker->id;
            sheet.weapon = sim::Weapon::Fists;
            // STREET SENSES leg (c): a watchman you struck fights with
            // Intent::Kill (the Gull's own rule for a blow on the closing
            // Cull) -- Lethal by B2, so his blows can kill and the floor is
            // lifted. A sailor means Subdue: a fist fight on the quay.
            sheet.intent = attacker->type == sim::WardType::MilitiaWatch ? sim::Intent::Kill
                                                                          : sim::Intent::Subdue;
            sheet.hp = attacker->hp;
            sheet.hpMax = sim::kActorHealth;
            (void)tavern_->takeStreetBlow(sheet, thrown.roll);
        }
        // STREET SENSES leg (c) -- THE WATCH ON THE BEATS. What the closing
        // watchmen did on the ward's tick just taken: the halt and the demand
        // said on the alert row out of the street's own rows (a new file in
        // the salted register, falling through to the Gull's watch.* rows
        // until authored), the offence landed on the heat, and the ARREST AT
        // REACH landed through the Gull's one seam (arrestByStreetWatch ->
        // arrestPlayer -> the charge sheet, the seizure, the hearing) -- so
        // TAKEN TO THE MISSION and the page follow from the quay exactly as
        // they follow from Cull's stool. In custody already, or hanged, the
        // ward's arrest is refused by the room and nothing is written.
        for (const sim::WatchEvent& event : people_->takeWatchEvents()) {
            const sim::WardActor* officer = people_->byId(event.watchmanId);
            if (officer == nullptr) {
                continue;
            }
            const std::string name = people_->identity(officer->id).name;
            const sim::BarkTables& barks = tavern_->dialogue().barks();
            const std::int32_t salt = static_cast<std::int32_t>(elapsedSeconds_ % 1024) + officer->id;
            switch (event.kind) {
                case sim::WatchEventKind::Halt:
                    sayAs(name,
                        std::string(barks.line(
                            barks.resolve({std::string("street.halt"), std::string("watch.halt"),
                                           std::string("watch.demand")}),
                            salt)), false);
                    messageDemand_ = true;
                    break;
                case sim::WatchEventKind::Sheathe:
                    sayAs(name,
                        std::string(barks.line(
                            barks.resolve({std::string("street.sheathe"), std::string("watch.demand")}),
                            salt)), false);
                    messageDemand_ = true;
                    break;
                case sim::WatchEventKind::Offence:
                    tavern_->noteStreetOffence();
                    sayAs(name,
                        std::string(barks.line(
                            barks.resolve({std::string("street.offence"), std::string("watch.demand")}),
                            salt)), false);
                    messageDemand_ = true;
                    break;
                case sim::WatchEventKind::Arrest:
                    if (!inCustody() && !tavern_->hearingPending() && !tavern_->executed()) {
                        tavern_->arrestByStreetWatch(name);
                    }
                    break;
            }
        }
    }
    // BARKS LANE (feel/build). WHAT THE ROOM SAID THIS STEP, on the alert
    // row: the watchman's halt (or his contraband demand -- lastDemand was
    // composed by the sim since S6 and read by nobody but the tests), the man
    // stepping into the fight (brawl.join), the panic line (crowd.flee). Each
    // is a string the sim already owns; the client only notices it CHANGE --
    // the comparison-not-flag shape lastPlayerHp_ keeps, three unhashed
    // strings on this side and nothing new reaching into the simulation. The
    // halt is spoken last so it wins the row when all three land on one step.
    // VOICE LANE: a sim line landing on the alert row in a name that is on
    // the voice row takes the voice row down -- the arrest line under a
    // watchman's readback, a join under a witness's word -- so one man never
    // says two things at once.
    if (tavern_->lastJoin() != lastJoinSpoken_) {
        lastJoinSpoken_ = tavern_->lastJoin();
        say(lastJoinSpoken_);
        (void)voice_.dropIfSpeaker(alertSpeakerOf(lastJoinSpoken_));
    }
    if (tavern_->lastFlee() != lastFleeSpoken_) {
        lastFleeSpoken_ = tavern_->lastFlee();
        say(lastFleeSpoken_);
        (void)voice_.dropIfSpeaker(alertSpeakerOf(lastFleeSpoken_));
    }
    if (tavern_->lastDemand() != lastDemandSpoken_) {
        lastDemandSpoken_ = tavern_->lastDemand();
        // In the watchman's own name -- the sim writes it "<name>: <row>"
        // (Tavern::tickWatch), so the prefix IS his name, exactly -- and
        // through sayAs, so the row is his: his goodbye stands it down the
        // way it stands down his greeting (the 2026-10-07 auditor: the old
        // prefix test caught this line, and the mouth has to as well).
        const std::string watchman(alertSpeakerOf(lastDemandSpoken_));
        if (watchman.empty()) {
            say(lastDemandSpoken_);
        } else {
            sayAs(watchman, lastDemandSpoken_.substr(watchman.size() + 2), false);
        }
        // THE GULL'S OWN WATCHMAN'S DEMAND (Cull's halt, a contraband
        // demand, an arrest at reach behind it) outranks the plate exactly
        // as the street's does -- the auditor's finding, fifth pass.
        messageDemand_ = true;
        (void)voice_.dropIfSpeaker(alertSpeakerOf(lastDemandSpoken_));
    }
    // VOICE LANE, THE ADDRESS: THE WALK-OFF CLOSE. A conversation on foot
    // ends when you leave it: the reach is re-checked every step while the
    // director is open (neither talkTo path ever did, so a body you had
    // walked twenty tiles from was still "in front of you"), and past
    // kTalkWalkOffTiles the talk ends with the word thrown after you. The
    // Gull's man is measured in the room's own Q8 metric, the ward's in
    // tiles; a page over the panel (pause, the keys) is not a walk.
    if (tavern_->dialogue().isOpen() && !pauseOpen_ && !optionsOpen_ && !keysOpen_ &&
        !waitOpen_ && !casebookOpen_ && !grimoireOpen_ && !districtMapOpen_ && !courtOpen_) {
        bool walkedOff = false;
        if (wardTalkId_ >= 0) {
            const sim::WardActor* who = people_ == nullptr ? nullptr : people_->byId(wardTalkId_);
            walkedOff = who == nullptr || who->band != body_->band() ||
                        std::max(std::abs(who->x - body_->tileX()),
                                 std::abs(who->y - body_->tileY())) > kTalkWalkOffTiles;
        } else if (const sim::Actor* who = tavern_->actorById(tavern_->talkingTo());
                   who != nullptr) {
            walkedOff = who->band() != body_->band() ||
                        who->distanceTo(body_->x(), body_->y()) > kTalkWalkOffTiles * sim::kSubOne;
        }
        if (walkedOff) {
            // WHO LEFT. The word thrown after you is for the one who walks
            // off mid-sentence; a body that walked away from YOU (the ward's
            // hold is deferred, so a patrol keeps its beat) ends the talk
            // with its ordinary goodbye instead -- said as it goes.
            const bool playerLeft =
                std::max(std::abs(body_->tileX() - talkOpenTileX_),
                         std::abs(body_->tileY() - talkOpenTileY_)) > kWardTalkReachTiles;
            endTalk(playerLeft);
        }
    }
    // VOICE LANE. THE ONE VOICE CHANNEL, stepped here and nowhere else: after
    // the room and the ward have moved and after the sim's own lines went
    // on the alert row (the halt keeps its rank), from public state only.
    // A line landing re-arms the row's toggle eagerly, say()'s own rule.
    {
        VoiceScene scene;
        scene.room = tavern_;
        scene.people = people_;
        scene.tiles = tiles_.get();
        scene.playerTileX = body_->tileX();
        scene.playerTileY = body_->tileY();
        scene.playerBand = body_->band();
        scene.playerXQ8 = body_->x();
        scene.playerYQ8 = body_->y();
        scene.step = static_cast<std::int32_t>(body_->stepCount());
        scene.talking = tavern_->dialogue().isOpen();
        scene.wardTalkId = wardTalkId_;
        scene.crouched = tavern_->stance() == sim::Stance::Crouched;
        scene.handsUp = tavern_->playerHandsUp();
        // Who the alert row is quoting right now, so a watchman's readback
        // waits for his halt to leave the row instead of stacking under it.
        scene.alertSpeaker = alertSpeakerNow();
        // THE RUNG PLATE holds this row's clock as it holds the alert row's
        // (VoiceScene::holdClock) -- for an EVENT line only; a passer-by's
        // bark is a moment and runs (StreetVoice::step). stepRungPlate caps
        // whatever is left when the plate goes.
        scene.holdClock = rungPlateOnScreen();
        voice_.step(scene);
        // ONE MAN, ONE LINE. A readback that REPLACED the halt (the emitter
        // speaks over a standing halt only at reach, after its second) takes
        // the halt off the alert row the same step: its clock runs out here
        // and the row fades under the readback.
        if (voice_.watchSpokeThisStep(scene.step) && scene.alertSpeaker == voice_.current().name) {
            messageSteps_ = 0;
        }
        syncVoiceAnim();
    }
    // VOICE LANE, THE ADDRESS. The worded reaction's own countdown, spent
    // once a step like every sibling.
    if (reactionSteps_ > 0) {
        --reactionSteps_;
        if (reactionSteps_ == 0) {
            reactionLine_.clear();
        }
    }
    syncWardToCalendar();
    // COURIER CASE. Last, deliberately: the courier's countdown, the take
    // nudge and the scripted delivery all read the step the world just
    // finished taking, and anything they say() eases in on the next frame
    // exactly as every other verb's message does.
    stepSheetCase();
    // EVICTION CASE, alongside the courier's for the identical reason: the
    // door nudge and the scripted delivery read the step just finished.
    stepEvictCase();
}

void Session::stepMany(const sim::MoveInput& input, int steps) {
    for (int i = 0; i < steps; ++i) {
        step(input);
    }
}

void Session::syncClockAfterSkip() {
    // The shared tail of every clock jump -- see the declaration's header.
    timeOfDay_ = tavern_->timeOfDay();
    settings_.timeOfDay = timeOfDay_;
    stepsThisSecond_ = 0;
    syncWardToCalendar();
}

void Session::settleSleep() {
    syncClockAfterSkip();
    say("SLEPT UNTIL MORNING.");
}

void Session::skipToHour(int hour) {
    recordWatchOp(WatchOpKind::SkipHour, hour);
    const WatchDepthGuard watchGuard(watchDepth_);
    const int wrapped = ((hour % 24) + 24) % 24;
    tavern_->skipTo(wrapped * 3600);
    syncClockAfterSkip();
}

void Session::skipSeconds(int seconds) {
    // FAST TRAVEL (TRAVEL lane). skipToHour's own two calls with the truncation
    // taken out: Tavern::skipTo already measures the jump forward round the
    // clock face and cools heat on every second of it, and syncClockAfterSkip()
    // runs the ward's calendar and the population to the new moment. Nothing
    // here is a second time system -- a travel IS a wait, plus a relocation the
    // caller makes separately.
    if (seconds <= 0) {
        return;
    }
    tavern_->skipTo((timeOfDay_ + seconds) % sim::kSecondsPerDay);
    syncClockAfterSkip();
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
    // #78: AND THE PEOPLE FOLLOW IT TOO, for the identical reason. A skip that
    // moved the taproom's clock and not the population's would photograph a
    // district still keeping the hours of whenever the session booted -- the
    // Watch on its day beat at two in the morning, and nobody in bed.
    // skipToSecond is a no-op when the clocks already agree, which they do on
    // every ordinary step, so this is safe to call as often as it likes.
    if (people_ != nullptr && tavern_ != nullptr) {
        people_->skipToSecond(timeOfDay_);
    }
}

// ---------------------------------------------------------------------------
// TIME-AND-TENURE BUILD: the ground underfoot, and the Flame's roll
// ---------------------------------------------------------------------------

std::int32_t Session::plotIndexUnderfoot() const noexcept {
    if (ward_ == nullptr || !ward_->loaded()) {
        return -1;
    }
    const std::string_view plotId = sim::docks::plotIdUnder(body_->tileX(), body_->tileY());
    if (plotId.empty()) {
        return -1;
    }
    return ward_->plotNamed(plotId);
}

void Session::syncGroundPlot() {
    sim::DialogueDirector& talk = tavern_->dialogue();
    // The ground underfoot, for the roll reading.
    const std::int32_t plot = plotIndexUnderfoot();
    if (plot < 0) {
        talk.setGroundPlot(-1, {});
    } else {
        const sim::Plot& roll = ward_->plots()[static_cast<std::size_t>(plot)];
        talk.setGroundPlot(plot,
                           ward_->raws().plots()[static_cast<std::size_t>(roll.raw)].name);
    }
    // And the roll's open prize, for the petition -- the FIRST vacant charge
    // in roll order (the roll carries at most one today; if it ever carries
    // two, the Flame hears them one at a time, front of the register first).
    // VACANT here must be petitionForCharge's own yes, or the topic offers a
    // door the verb then refuses: tenure Vacant, never already the player's.
    std::int32_t vacant = -1;
    if (ward_ != nullptr && ward_->loaded()) {
        const std::vector<sim::Plot>& plots = ward_->plots();
        for (std::size_t i = 0; i < plots.size(); ++i) {
            if (plots[i].tenure == sim::Tenure::Vacant && !plots[i].playerIsDuke) {
                vacant = static_cast<std::int32_t>(i);
                break;
            }
        }
    }
    if (vacant < 0) {
        talk.setVacantCharge(-1, {});
    } else {
        const sim::Plot& roll = ward_->plots()[static_cast<std::size_t>(vacant)];
        talk.setVacantCharge(vacant,
                             ward_->raws().plots()[static_cast<std::size_t>(roll.raw)].name);
    }
}

std::string Session::groundRollLine() const {
    const std::int32_t plot = plotIndexUnderfoot();
    if (plot < 0 || ward_ == nullptr) {
        // Only reachable by walking off the plot between the topic being
        // built and the press -- the honest answer is the register's.
        return "THE ROLL DOES NOT NAME THIS GROUND.";
    }
    const sim::Plot& roll = ward_->plots()[static_cast<std::size_t>(plot)];
    const sim::PlotRaw& raw = ward_->raws().plots()[static_cast<std::size_t>(roll.raw)];
    std::string line = raw.name + ". ";
    switch (roll.tenure) {
        case sim::Tenure::Glebe:
            line += "GLEBE -- CHURCH GROUND, NEVER LET.";
            return line;
        case sim::Tenure::Vacant:
            // THE NUMBER IS THE REGISTER'S, not a judgement of you -- the
            // word-only ruling governs how the ward talks ABOUT the player,
            // and a rent on a public roll is signage. Information, never a
            // discount: petitionForCharge charges exactly this.
            line += "THE CHARGE IS VACANT. THE FIRST QUARTER'S CHARGE-RENT IS " +
                    std::to_string(raw.chargeRent) + ".";
            return line;
        case sim::Tenure::Pledged:
        case sim::Tenure::Charged:
        default: {
            if (roll.playerIsDuke) {
                line += "YOU HOLD THE CHARGE. THE GROUND PENNY UNDER YOUR HOUSES IS " +
                        std::to_string(raw.groundPenny) + " A QUARTER.";
                return line;
            }
            // Who the roll says, in the roll's own priority: a re-let charge
            // carries its holder's name; otherwise the raws' own Duke,
            // resolved through the registry the roll was refused against.
            std::string holder = roll.heldBy;
            if (holder.empty() && !raw.denDuke.empty()) {
                const sim::Notable* duke = tavern_->dialogue().notables().find(raw.denDuke);
                holder = duke != nullptr ? duke->name : raw.denDuke;
            }
            line += holder.empty() ? "THE CHARGE IS HELD."
                                   : upperAscii(holder) + " HOLDS THE CHARGE.";
            if (roll.tenure == sim::Tenure::Pledged) {
                line += " THE PAPER ON IT IS PLEDGED.";
            }
            return line;
        }
    }
}

std::string Session::settleGroundPetition() {
    // THE ROLL'S OWN OPEN PRIZE, re-derived here rather than trusted from the
    // topic: the same first-vacant rule syncGroundPlot() feeds the topic
    // list, so the row and the verb can never name different plots.
    std::int32_t plot = -1;
    if (ward_ != nullptr && ward_->loaded()) {
        const std::vector<sim::Plot>& plots = ward_->plots();
        for (std::size_t i = 0; i < plots.size(); ++i) {
            if (plots[i].tenure == sim::Tenure::Vacant && !plots[i].playerIsDuke) {
                plot = static_cast<std::int32_t>(i);
                break;
            }
        }
    }
    if (plot < 0 || ward_ == nullptr) {
        return "NO CHARGE ON THE ROLL STANDS VACANT.";
    }
    const sim::PlotRaw& raw =
        ward_->raws().plots()[static_cast<std::size_t>(
            ward_->plots()[static_cast<std::size_t>(plot)].raw)];
    // ONE PURSE. The tavern's playerCoin is the purse of record everywhere in
    // the render layer; the ward keeps its own copy for NPC-parity verbs, so
    // it is synced in, the verb spends from it, and the result is synced back
    // out -- both fields already hashed, nothing minted, nothing new.
    ward_->setPlayerCoin(tavern_->playerCoin());
    const sim::TenureResult got = ward_->petitionForCharge(plot);
    tavern_->setPlayerCoin(ward_->playerCoin());
    tavern_->dialogue().setPlayerCoin(tavern_->playerCoin());
    // The roll changed (or did not); the topic list must say so either way.
    // A granted petition takes its own row off the list mid-conversation.
    syncGroundPlot();
    switch (got) {
        case sim::TenureResult::Done:
            return "THE FLAME RE-LETS THE CHARGE. YOU ARE DEN DUKE OF " + raw.name +
                   ". FIRST QUARTER PAID: " + std::to_string(raw.chargeRent) + ".";
        case sim::TenureResult::CannotAfford:
            return "THE FIRST QUARTER'S CHARGE-RENT IS " + std::to_string(raw.chargeRent) +
                   ". YOU DO NOT CARRY IT.";
        case sim::TenureResult::NotVacant:
            return "THE CHARGE IS HELD. THERE IS NOTHING TO PETITION FOR.";
        case sim::TenureResult::AlreadyHeld:
            return "THE ROLL ALREADY NAMES YOU.";
        case sim::TenureResult::NoCause:
            return "CHURCH GROUND. IT IS NEVER LET.";
        case sim::TenureResult::NoSuchThing:
        default:
            return "THE ROLL DOES NOT NAME THIS GROUND.";
    }
}

Camera Session::camera() const noexcept {
    // FROM THE LIVE SETTINGS, not from the config the session was built with.
    // #77 puts a field-of-view slider on the options page and the camera has to
    // read it every frame or the slider is decoration; the constructor seeds
    // controls_.fovDegrees from config_ so --fov still works.
    const float half = static_cast<float>(controls_.fovDegrees) * 0.5F * kPi / 180.0F;
    // ACTION-COMBAT BUILD (section 5, channel 5). THE IMPACT IMPULSE, composed
    // at the one Camera::fromBody site as a BAM offset on RENDER pitch and never
    // on sim yaw -- a render-only courtesy the Camera contract permits here
    // (the field is a float, the sim never reads it back). Peaks are capped in
    // BAM well under the spec's angles: a hard-release forward dip ~2deg, a
    // blow-taken jolt ~1.5deg, a caught-blow nudge ~1deg. Every pulse decays to
    // zero within kPageEaseSteps, so the offset is exactly 0 at rest -- every
    // settled capture is byte-identical and --settle-steps photographs no
    // impulse. Pitch is radians, positive up: a hard release nods the eye DOWN,
    // a blow taken kicks it up, a caught blow nudges it up a touch less.
    constexpr std::int32_t kHardSwingDipBam = 364;  // ~2.0 deg
    constexpr std::int32_t kTakenJoltBam = 273;     // ~1.5 deg
    constexpr std::int32_t kBlockNudgeBam = 182;    // ~1.0 deg
    const std::int32_t dip =
        static_cast<std::int32_t>(std::lround(hardSwingDipPulse_.value() * kHardSwingDipBam));
    const std::int32_t taken =
        static_cast<std::int32_t>(std::lround(punchTakenPulse_.value() * kTakenJoltBam));
    const std::int32_t nudge =
        static_cast<std::int32_t>(std::lround(blockPulse_.value() * kBlockNudgeBam));
    const std::int32_t pitchBam = body_->pitch() - dip + taken + nudge;
    return Camera::fromBody(body_->x(), body_->y(), body_->eyeZ(), body_->yaw(), pitchBam,
                            std::tan(half));
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
    const SkyState sky = this->sky();
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
        Rgb light{std::min(1.15F, sky.ambient.r + std::max(baked.r, dynamic.r)),
                  std::min(1.15F, sky.ambient.g + std::max(baked.g, dynamic.g)),
                  std::min(1.15F, sky.ambient.b + std::max(baked.b, dynamic.b))};

        // Sub-tile Q8 straight out of the simulation. See actor.hpp: the
        // position between two tiles is the sim's, not the renderer's.
        const float px = static_cast<float>(actor.x()) / 256.0F;
        const float py = static_cast<float>(actor.y()) / 256.0F;
        // Through bandSurface(), not straight off the band number. A band is
        // three tiles of height (sim/vertical_scale.hpp) and an actor whose
        // feet were placed at `band` stood two storeys below the floor they
        // were walking on.
        const float floorZ = bandSurface(actor.band());
        const bool down = actor.activity() == sim::Activity::Downed;

        // WHICH WAY THEY ARE LOOKING, and the reason it is still here.
        //
        // Actor::faceToward computes an eight-point facing and hashInto commits
        // it to world state on every tick. The S2 review found that no renderer
        // read it -- a feature that existed only as data -- and S3 spent it on a
        // pale patch stuck to the side of the head. The sheet is drawn front-on
        // and has no back view, so it is spent on LIGHT instead: somebody
        // looking your way catches what light there is and somebody turned away
        // is a silhouette. Which is true, and at eight tiles in lamplight it is
        // the one thing about a person you actually need to read.
        const float facingRad = static_cast<float>(actor.facing()) * (2.0F * kPi / 65536.0F);
        // BAM 0 is north, which is -Y, and increases clockwise (sim/angle.hpp).
        const float faceX = std::sin(facingRad);
        const float faceY = -std::cos(facingRad);
        const float toEyeX = view.x - px;
        const float toEyeY = view.y - py;
        const float span = std::sqrt(toEyeX * toEyeX + toEyeY * toEyeY);
        if (span > 0.0001F) {
            const float towards = (faceX * toEyeX + faceY * toEyeY) / span;
            if (towards <= 0.15F) {
                light = Rgb{light.r * kBackShade, light.g * kBackShade, light.b * kBackShade};
            }
        }

        const sim::WardType figure = figureForRole(actor.role(), actor.id());
        const ActorSprite& art =
            actorSheet_.forType(figure, static_cast<std::uint32_t>(actor.id()));
        const FigureScale scale = figureScaleOf(figure);
        const float inkRows = std::max(1.0F, static_cast<float>(art.lastRow - art.firstRow + 1));
        const float inkCols = std::max(1.0F, static_cast<float>(art.lastCol - art.firstCol + 1));

        SpriteInstance sprite;
        sprite.x = px;
        sprite.y = py;
        // THE FIGURE IS 1.875 TILES TALL, which is kStandingHeightTilesQ8 (480
        // Q8) out of sim/vertical_scale.hpp, the same header the player's own
        // eye height comes from. At roughly 0.9 m to the tile that is a 1.71 m
        // adult, and the crown lands a hand above the player's 1.70-tile eye --
        // so you look people in the mouth, which is correct and is the cheapest
        // proof the two scales agree.
        sprite.halfHeight = scale.heightTiles * 0.5F;
        sprite.halfWidth = scale.widthTiles * 0.5F * (inkCols / inkRows);
        if (down) {
            // Flat out on the boards: wide instead of tall, and low enough that
            // somebody standing over them reads as standing over them.
            sprite.halfHeight = 0.22F;
            sprite.halfWidth = scale.heightTiles * 0.5F;
        }
        sprite.z = floorZ + sprite.halfHeight;
        sprite.colour = light;
        sprite.glow = 0.0F;
        sprite.softness = 0.0F;
        // `person` is what the frame stats count apart from flames, and a rat
        // is not one. It is drawn, it is lit and it is in the frame -- it is
        // simply not somebody, which is the same distinction presentCount()
        // makes in the room itself.
        sprite.person = actor.role() != sim::ActorRole::Vermin;
        sprite.art = art.texels;
        sprite.artSize = ActorSprite::kPx;
        sprite.artU0 = art.firstCol;
        sprite.artU1 = art.lastCol;
        sprite.artV0 = art.firstRow;
        sprite.artV1 = art.lastRow;
        sprites.push_back(sprite);
    }
    return sprites;
}

std::vector<SpriteInstance> Session::wardSprites() const {
    return wardSprites(camera());
}

std::vector<SpriteInstance> Session::wardSprites(const Camera& view) const {
    std::vector<SpriteInstance> sprites;
    if (people_ == nullptr) {
        return sprites;
    }
    const SkyState sky = this->sky();
    // WHERE BETWEEN THE TWO TILES. The simulation says which tile a body left
    // and which one it is on; this says how far along it is THIS FRAME, and it
    // is the only place that opinion exists. Nothing here is written back --
    // which is the 2026-07-31 ruling read exactly as written: NPCs are
    // tile-stepped in the sim and interpolated at draw time.
    const float slide =
        static_cast<float>(stepsThisSecond_) / static_cast<float>(sim::kStepsPerSecond);
    sprites.reserve(people_->actors().size());
    for (const sim::WardActor& actor : people_->actors()) {
        // #80: not the starved, and not a mouse a cat has just taken off the
        // board. sim::WardActor::visible answers both in one place, which is
        // what stops a caught mouse being drawn standing in its own den for
        // three hours of ward time.
        // STREET SENSES leg (b): AND THE MAN ON THE GROUND. A person struck
        // down on the brawl floor, or slain, is off the board (visible false)
        // and DRAWN FLAT where he fell -- floored(), the one other predicate
        // this loop reads -- the Gull's own Downed presentation (wide, low)
        // on a street body. A caught mouse is neither.
        const bool down = actor.floored();
        if (!actor.visible() && !down) {
            continue;
        }
        // Shaded by the light where they STAND. Without this a figure in an
        // unlit alley glows like a lamp, which is the one thing the
        // committed-dark look cannot survive.
        const Rgb baked = renderer_->glow().at(actor.x, actor.y, actor.band);
        Rgb light{std::min(1.15F, sky.ambient.r + baked.r),
                  std::min(1.15F, sky.ambient.g + baked.g),
                  std::min(1.15F, sky.ambient.b + baked.b)};

        // A floored body lies where it fell: no slide from the tile it was
        // walking off when the blow landed.
        const float px = down ? static_cast<float>(actor.x) + 0.5F
                              : static_cast<float>(actor.prevX) +
                                    static_cast<float>(actor.x - actor.prevX) * slide + 0.5F;
        const float py = down ? static_cast<float>(actor.y) + 0.5F
                              : static_cast<float>(actor.prevY) +
                                    static_cast<float>(actor.y - actor.prevY) * slide + 0.5F;
        const float floorZ = bandSurface(actor.band);

        // Which way they are looking, against where the eye is. The same
        // arithmetic the tavern's face patch uses: BAM 0 is north, which is -Y,
        // and it increases clockwise.
        const float facingRad = static_cast<float>(actor.facing) * (2.0F * kPi / 65536.0F);
        const float faceX = std::sin(facingRad);
        const float faceY = -std::cos(facingRad);
        const float toEyeX = view.x - px;
        const float toEyeY = view.y - py;
        const float span = std::sqrt(toEyeX * toEyeX + toEyeY * toEyeY);
        if (span > 0.0001F) {
            const float towards = (faceX * toEyeX + faceY * toEyeY) / span;
            if (towards <= 0.15F) {
                light = Rgb{light.r * kBackShade, light.g * kBackShade, light.b * kBackShade};
            }
        }

        const ActorSprite& art =
            actorSheet_.forType(actor.type, static_cast<std::uint32_t>(actor.id));
        const FigureScale scale = figureScaleOf(actor.type);
        const float inkRows = std::max(1.0F, static_cast<float>(art.lastRow - art.firstRow + 1));
        const float inkCols = std::max(1.0F, static_cast<float>(art.lastCol - art.firstCol + 1));

        SpriteInstance sprite;
        sprite.x = px;
        sprite.y = py;
        // Sized to the INK and stood on the pavement: the quad's bottom edge is
        // the floor, so a figure drawn short inside its own cell does not hover
        // two texels above the street.
        sprite.halfHeight = scale.heightTiles * 0.5F;
        sprite.halfWidth = scale.widthTiles * 0.5F * (inkCols / inkRows);
        if (down) {
            // Flat out on the quay: wide instead of tall, and low enough that
            // somebody standing over him reads as standing over him -- the
            // Gull's own Downed numbers (actorSprites).
            sprite.halfHeight = 0.22F;
            sprite.halfWidth = scale.heightTiles * 0.5F;
        }
        sprite.z = floorZ + sprite.halfHeight;
        sprite.colour = light;
        sprite.glow = 0.0F;
        sprite.softness = 0.0F;
        sprite.person = sim::isPerson(actor.type);
        sprite.ward = true;
        sprite.art = art.texels;
        sprite.artSize = ActorSprite::kPx;
        sprite.artU0 = art.firstCol;
        sprite.artU1 = art.lastCol;
        sprite.artV0 = art.firstRow;
        sprite.artV1 = art.lastRow;
        sprites.push_back(sprite);
    }
    return sprites;
}

/// Whether the LENS at one Q8 point has a clear line to a body at another --
/// the question a shot composed on somebody has to ask, and NOT the one
/// TileQuery::lineOfSight answers. The sim's rule is a supercover walk
/// between two TILE CENTRES that lets a ray slip through a lattice corner
/// when only one of the two cells it grazes is solid: the right rule for
/// "can he see me" between people, and exactly the wrong one for a camera,
/// which sits at a Q8 point and draws every solid cell as a full-height
/// block. The voice lane's watch reveal was a picture of the taproom table
/// at (151,69) for this reason: Cull could see the player past its corner by
/// the sim's rule, and the frame showed its west face.
///
/// So: a grid walk (Amanatides-Woo) from the exact eye to the exact body,
/// every cell the segment enters asked of solid(), a corner crossing blocked
/// if EITHER grazed cell is solid, and the walk run three times -- down the
/// middle and along two lines a quarter tile either side of it. `whole`
/// asks for all three, a body half a tile across in view and not a hair of
/// it (what a stand is COMPOSED on); without it the middle and either side
/// will do, which is "can you see them" for a body walking along a wall.
/// The endpoints' own cells never block: a body stands in a cell, it is
/// not one. Render side, doubles legal, nothing here is hashed or written.
bool cameraSightClear(const sim::TileQuery& tiles, std::int32_t fromXQ8, std::int32_t fromYQ8,
                      std::int32_t toXQ8, std::int32_t toYQ8, std::int32_t band,
                      bool whole) noexcept {
    const auto walk = [&](double ax, double ay, double bx, double by) {
        const double dx = bx - ax;
        const double dy = by - ay;
        std::int32_t cx = static_cast<std::int32_t>(std::floor(ax));
        std::int32_t cy = static_cast<std::int32_t>(std::floor(ay));
        const std::int32_t ex = static_cast<std::int32_t>(std::floor(bx));
        const std::int32_t ey = static_cast<std::int32_t>(std::floor(by));
        const std::int32_t stepX = dx > 0.0 ? 1 : (dx < 0.0 ? -1 : 0);
        const std::int32_t stepY = dy > 0.0 ? 1 : (dy < 0.0 ? -1 : 0);
        constexpr double kFar = 1.0e18;
        const double tDeltaX = stepX != 0 ? 1.0 / std::abs(dx) : kFar;
        const double tDeltaY = stepY != 0 ? 1.0 / std::abs(dy) : kFar;
        double tMaxX = stepX > 0 ? (static_cast<double>(cx + 1) - ax) * tDeltaX
                       : stepX < 0 ? (ax - static_cast<double>(cx)) * tDeltaX
                                   : kFar;
        double tMaxY = stepY > 0 ? (static_cast<double>(cy + 1) - ay) * tDeltaY
                       : stepY < 0 ? (ay - static_cast<double>(cy)) * tDeltaY
                                   : kFar;
        // Bounded by the cells the segment can cross, plus one for each
        // corner it might take diagonally.
        for (std::int32_t guard = std::abs(ex - cx) + std::abs(ey - cy) + 2; guard > 0; --guard) {
            if (cx == ex && cy == ey) {
                return true;
            }
            constexpr double kCorner = 1.0e-9;
            if (std::abs(tMaxX - tMaxY) < kCorner) {
                // Through a lattice corner: a shut corner is a wall to a
                // lens whichever cell of the two is the solid one.
                if (tiles.solid(cx + stepX, cy, band) || tiles.solid(cx, cy + stepY, band)) {
                    return false;
                }
                cx += stepX;
                cy += stepY;
                tMaxX += tDeltaX;
                tMaxY += tDeltaY;
            } else if (tMaxX < tMaxY) {
                cx += stepX;
                tMaxX += tDeltaX;
            } else {
                cy += stepY;
                tMaxY += tDeltaY;
            }
            if (cx == ex && cy == ey) {
                return true;
            }
            if (tiles.solid(cx, cy, band)) {
                return false;
            }
        }
        return cx == ex && cy == ey;
    };
    const double ax = static_cast<double>(fromXQ8) / static_cast<double>(sim::kSubOne);
    const double ay = static_cast<double>(fromYQ8) / static_cast<double>(sim::kSubOne);
    const double bx = static_cast<double>(toXQ8) / static_cast<double>(sim::kSubOne);
    const double by = static_cast<double>(toYQ8) / static_cast<double>(sim::kSubOne);
    const double dx = bx - ax;
    const double dy = by - ay;
    const double length = std::sqrt(dx * dx + dy * dy);
    if (length < 1.0e-6) {
        return true;
    }
    // A quarter tile either side, perpendicular to the line: the width of
    // the body the shot is about.
    constexpr double kHalfBody = 0.25;
    const double nx = -dy / length * kHalfBody;
    const double ny = dx / length * kHalfBody;
    if (!walk(ax, ay, bx, by)) {
        return false;
    }
    const bool left = walk(ax + nx, ay + ny, bx + nx, by + ny);
    const bool right = walk(ax - nx, ay - ny, bx - nx, by - ny);
    return whole ? (left && right) : (left || right);
}

}  // namespace granadad::render
