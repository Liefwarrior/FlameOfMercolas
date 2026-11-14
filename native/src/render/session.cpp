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

// ---------------------------------------------------------------------------
// S5: the roof verbs
// ---------------------------------------------------------------------------

void Session::settleLanding(const sim::RoofResult& move) {
    // THE CHARGE IS THE ROOM'S. Everything a landing moves -- the craft, the
    // hit points, the roof-run, the counted verb -- is simulation state, and it
    // moved out of this file in S6 so the simulation suite can drive it and a
    // mutation to any clause of it can go red. See Tavern::settleLanding.
    const sim::Tavern::LandingResult charged = tavern_->settleLanding(
        move, body_->takeFallBands(), body_->band(), body_->tileX(), body_->tileY());
    if (charged.hurt > 0) {
        // IN METRES, because the fall curve is in metres now and a player who is
        // told "50 HURT" learns nothing they can act on. "5M - 50 HURT" tells
        // them the roof they are standing on is the last one they can come off.
        roofMove_ += " - " + std::to_string(charged.fellMm / 1000) + "M";
        if (charged.softLanding) {
            roofMove_ += " INTO WATER";
        }
        roofMove_ += " - " + std::to_string(charged.hurt) + " HURT";
    }
}

sim::RoofResult Session::tryClimb() {
    // FATIGUE BUILD: an empty pool gates the climb verbs before the body is
    // ever asked -- the body knows geometry, not stamina, which is why
    // RoofMove::Winded is the one refusal PlayerBody itself never returns
    // (see the enum's own note). A refused climb costs nothing, exactly as a
    // NoLedge costs nothing.
    if (tavern_->playerWinded()) {
        return sim::RoofResult{sim::RoofMove::Winded, 0, 0};
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
        // NOTHING SAID. The caller decides what a failed climb means: climb()
        // itself reports the refusal, vertical() moves on to tryDropDown().
        return move;
    }
    if (leapt) {
        // FATIGUE BUILD: the leap's wind is spent when the jump is armed --
        // the moment the body commits -- AGI- and skyrunning-scaled through
        // the one charge path (Tavern::chargePlayerLeap).
        tavern_->chargePlayerLeap();
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
        // A leap down onto a roof one tile away is a real move -- PlayerBody's
        // second pass allows it -- and it used to announce itself as "OVER 1
        // TILES".
        roofMove_ =
            "OVER " + std::to_string(move.tiles) + (move.tiles == 1 ? " TILE." : " TILES.");
        say(roofMove_);
        // A MOMENT, NOT NEWS: this line's clock runs under the rung plate.
        messageHeld_ = false;
        pendingLanding_ = move;
        awaitingLanding_ = true;
        syncTavernToBody();
        return move;
    }
    roofMove_ = "UP ONTO THE LEDGE.";
    // FATIGUE BUILD: the haul's wind, spent on a climb that happened -- the
    // same bill the automatic path pays in step().
    tavern_->chargePlayerMantle();
    // Same order as dropDown, and for the same reason: a mantle onto a LOWER
    // ledge is a fall too, and the line has to be said after it is charged.
    settleLanding(move);
    say(roofMove_);
    messageHeld_ = false;
    syncTavernToBody();
    return move;
}

void Session::climb() {
    if (refusedInCustody()) {
        return;
    }
    recordWatchOp(WatchOpKind::Climb);
    const WatchDepthGuard watchGuard(watchDepth_);
    dismissOverlays();
    if (talking()) {
        return;
    }
    const sim::RoofResult move = tryClimb();
    if (!move.ok()) {
        roofMove_ = std::string(sim::roofRefusal(move.move));
        say(roofMove_);
    }
}

sim::RoofResult Session::tryDropDown() {
    const sim::RoofResult move = body_->dropOff();
    if (!move.ok()) {
        return move;
    }
    roofMove_ =
        "DOWN " + std::to_string(move.bands) + (move.bands == 1 ? " LEVEL." : " LEVELS.");
    // SETTLE FIRST, THEN SAY IT. #77 found this by driving the real window and
    // stepping off the Gull's lead: the alert read "DOWN 2 LEVELS." and the
    // health bar quietly halved with nothing on screen connecting the two.
    // settleLanding appends the height and the injury to roofMove_, so saying
    // the line before charging the fall throws away the only part of it a player
    // can act on. What they see now is "DOWN 2 LEVELS. - 5M - 50 HURT".
    // NEWS, not a moment: the hurt is the part a player acts on, so this
    // line is held under a rung plate like the take off a box.
    settleLanding(move);
    say(roofMove_);
    syncTavernToBody();
    return move;
}

void Session::dropDown() {
    if (refusedInCustody()) {
        return;
    }
    recordWatchOp(WatchOpKind::Drop);
    const WatchDepthGuard watchGuard(watchDepth_);
    dismissOverlays();
    if (talking()) {
        return;
    }
    const sim::RoofResult move = tryDropDown();
    if (!move.ok()) {
        roofMove_ = std::string(sim::roofRefusal(move.move));
        say(roofMove_);
    }
}

void Session::vertical() {
    if (refusedInCustody()) {
        return;
    }
    dismissOverlays();
    if (talking() || picking()) {
        return;
    }
    // #85. ONE BUTTON, RESOLVED BY WHAT IS DIRECTLY AHEAD OR BELOW: climb
    // (mantle, or the leap it falls back to) first, a drop if there is a
    // ledge to step off, an ordinary standing jump when neither is there.
    // Each of the first two already refuses cleanly on ground that is
    // neither -- RoofMove::NoLedge/NoGap for a climb, "the tile ahead is
    // walkable" for a drop -- so trying them first costs nothing on flat
    // ground, which is where this actually resolves to a jump.
    if (tryClimb().ok()) {
        return;
    }
    if (tryDropDown().ok()) {
        return;
    }
    if (!body_->jump()) {
        return;
    }
    // FATIGUE BUILD: the reference prices every jump (5 points, AGI-scaled)
    // and so does this one -- charged only on a hop that actually left the
    // ground, never on the buffered request jump() banks instead.
    tavern_->chargePlayerJump();
    // NOTHING IS SAID -- see jump()'s own note: a jump that announced itself
    // on the alert row every time would be the noisiest thing in the game.
    tavern_->setPlayerMotion(true, true);
}

bool Session::stealNearestThing() {
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
        return true;
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
    // NOTHING IN REACH SAYS NOTHING. The chain's last link is buyPicks(),
    // whose TooFar line is "NOBODY HERE SELLS WIRE." -- and it used to be
    // spoken for EVERY press of USE at nothing, on every street, before the
    // walk went on to LOWER HANDS or LOOK. A player lowering their fists
    // was told about wire. Only a link that resolved (or refused something
    // real) has a line worth the alert row; the walk's own next slot is the
    // answer otherwise.
    if (took.result != sim::ServiceResult::TooFar) {
        say(took.line);
        return true;
    }
    return false;
}

void Session::steal() {
    if (refusedInCustody()) {
        return;
    }
    dismissOverlays();
    if (talking()) {
        return;
    }
    syncTavernToBody();
    stealNearestThing();
}

// ---------------------------------------------------------------------------
// S9: crouching, lifting, and the wire
// ---------------------------------------------------------------------------

void Session::toggleCrouch() {
    if (refusedInCustody()) {
        return;
    }
    recordWatchOp(WatchOpKind::Crouch);
    const WatchDepthGuard watchGuard(watchDepth_);
    dismissOverlays();
    if (talking()) {
        return;
    }
    tavern_->toggleStance();
    say(tavern_->stance() == sim::Stance::Crouched ? "CROUCHED" : "UPRIGHT");
}

sim::Stance Session::stance() const noexcept { return tavern_->stance(); }

bool Session::hidden() const noexcept { return tavern_->hidden(); }

void Session::lift() {
    if (refusedInCustody()) {
        return;
    }
    dismissOverlays();
    if (talking()) {
        return;
    }
    syncTavernToBody();
    say(tavern_->liftFrom().line);
}

void Session::examine() {
    if (refusedInCustody()) {
        return;
    }
    recordWatchOp(WatchOpKind::Examine);
    const WatchDepthGuard watchGuard(watchDepth_);
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
    const std::int64_t now = caseNowSeconds();
    const auto lookIn = [&](sim::Casebook& book) {
        sim::LookResult got = book.look(tileX, tileY, band, now);
        if (!got.found && got.lead < 0 && reach > sim::kLookRangeTiles) {
            // Nothing within the base reach. Walk outward one ring at a time
            // up to the bonus, standing the look at each offset -- integer,
            // bounded, and it cannot see anything a body one tile further
            // along could not.
            for (std::int32_t ring = 1; ring <= reach - sim::kLookRangeTiles && got.lead < 0;
                 ++ring) {
                const std::int32_t offsets[4][2] = {
                    {ring, 0}, {-ring, 0}, {0, ring}, {0, -ring}};
                for (const auto& offset : offsets) {
                    got = book.look(tileX + offset[0], tileY + offset[1], band, now);
                    if (got.lead >= 0) {
                        break;
                    }
                }
            }
        }
        return got;
    };
    sim::LookResult saw = lookIn(casebook_);
    // COURIER CASE. THE SECOND BOOK GETS THE IDENTICAL LOOK, asked only when
    // the first found nothing at all here -- member order is the tiebreak,
    // fixed and deterministic. The two files share exactly one site (the
    // Mission's back room), and there the Bloodletter always answers first;
    // the errand's own close never rides this key anyway -- delivery is
    // stepSheetCase()'s scripted arrival, because walking in with the man IS
    // the act, and no press should be owed on top of it.
    if (saw.lead < 0) {
        saw = lookIn(sheetBook_);
    }
    // EVICTION CASE. And the third book third, the same member-order
    // tiebreak one book deeper. Its two close leads never ride this key
    // either -- both are heard and read only by the case's own scripted
    // beats (stepEvictCase, settleYieldWrit) -- and every non-close lead of
    // its sits over four tiles from every lead of the other two books, so
    // the order here can never actually decide anything a player sees.
    if (saw.lead < 0) {
        saw = lookIn(evictBook_);
    }
    // THE CLUE IS THE MESSAGE. It is what the player walked here for, so it
    // gets the row whole; how many leads it opened is on the CASE row, which is
    // permanent and where a count belongs. A DEAD END SAYS SO OUT LOUD, though
    // -- walking across the district to learn that the sea is the wrong
    // question is work, and a game that let that read the same as a blank tile
    // would be a game telling you not to look.
    const bool cold = saw.found && saw.opened == 0;
    say(cold ? saw.line + "  (COLD)" : saw.line);
    // THE CASEBOOK PASS: THE MOMENT LEADS OPEN, ANNOUNCED.
    //
    // `saw.opened` has been computed correctly since S10 and thrown away
    // everywhere but the (COLD) suffix above. This is the whole of the defect
    // the owner's playthrough found: he read Crell's ledger, THREE leads went
    // into the book, and the only thing on the frame that said so was a dim
    // grey corner row changing from CASE 4/6 to CASE 4/9.
    //
    // THE RISING EDGE AND NOTHING ELSE. `saw.opened` counts leads that were not
    // already known (Casebook::hear's own return), so a lead the trail
    // CONVERGES on -- the Drowned Hold is named by four separate leads -- opens
    // nothing new the second time and fires nothing. Looking at an
    // already-read site returns before any of this. There is no way to make
    // this notice repeat by standing still and pressing the key.
    if (saw.opened > 0) {
        // THROUGH promptLabel, NOT keyName(primary[Menu]): the plate has to
        // name the key in the hand that is holding the machine -- "J" on a
        // keyboard, "D-PAD UP" on a pad -- and the raw primary slot is
        // keyboard-only wording, the exact literal class the opening hint
        // already left behind (openingHintLine). UI-EA: the notice grammar
        // is armCasePlate's -- "3 NEW LEADS - J", the spec's own line.
        armCasePlate(std::to_string(saw.opened) +
                     (saw.opened == 1 ? " NEW LEAD" : " NEW LEADS"));
    }
}

/// PUTS THE NOTES AND THE KEY LIST DOWN. Called by every verb that acts on the
/// world, so a player who presses a game key with a menu up gets the game and
/// not a silent refusal -- and so the opening page can never be in the way of
/// the second thing a new player does.
void Session::dismissOverlays() noexcept {
    casebookOpen_ = false;
    keysOpen_ = false;
    grimoireOpen_ = false;
    waitOpen_ = false;
    districtMapOpen_ = false;
    optionsOpen_ = false;
    awaitingKey_ = false;
    firstRun_ = false;
    pauseOpen_ = false;
    quitArmed_ = false;
    menuFocus_ = kMenuFocusJournal;
    // TASK #83. Ten call sites deep (climb, dropDown, steal, toggleCrouch,
    // lift, setCrouched, jump, interact, punch, restHere) and every one of
    // them can be the thing that closes a panel a player left open. One call
    // here catches all ten rather than repeating it at each.
    syncPanelAnim();
}

// ---------------------------------------------------------------------------
// #77: the controls, and the page that changes them
// ---------------------------------------------------------------------------

void Session::noteInputDevice(InputDevice device) {
    if (device == promptDevice_) {
        return;
    }
    // THE ONE STORED PROMPT. Every other prompt in this file is assembled at
    // draw time and re-words itself by construction; the opening hint is
    // state with a twelve-second life, so the hand-over re-words it in
    // place -- but ONLY while it is still the opening hint. A bark or an
    // alert that has since taken message_ is not this function's to touch.
    const bool hintUp = !message_.empty() && message_ == openingHintLine(controls_, promptDevice_);
    promptDevice_ = device;
    if (hintUp) {
        message_ = openingHintLine(controls_, promptDevice_);
        // UI-EA (LANE HUD): the tutor tier raises in full on a device
        // change. The hint's four seconds shrank from twelve in the word
        // diet, so a hand-over mid-read gets the whole read back in the new
        // vocabulary rather than the tail of the old one's clock.
        messageSteps_ = std::max(messageSteps_, 60 * 4);
    }
}

void Session::noteInputKey(Key key) {
    if (key == Key::None) {
        return;  // nobody spoke; Key::None must not read as the keyboard
    }
    noteInputDevice(deviceOfKey(key));
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

namespace {

/// WHICH FAMILY A VERB BELONGS TO, and therefore what colour its row is on the
/// controls page. Movement, the things you do, the screens you open, the quick
/// bar. See render/keys_page.hpp -- one accent per family is what makes a
/// thirty-row page scannable before a word of it is read.
[[nodiscard]] int keyGroupFor(Action action) noexcept {
    switch (action) {
        case Action::Forward:
        case Action::Back:
        case Action::StrafeLeft:
        case Action::StrafeRight:
        case Action::TurnLeft:
        case Action::TurnRight:
            return kKeysGroupMove;
        case Action::Menu:
        case Action::Pause:
        case Action::Map:
        case Action::Wait:
        case Action::Screenshot:
            return kKeysGroupScreen;
        case Action::QuickNext:
        case Action::QuickPrev:
            return kKeysGroupQuick;
        default:
            break;
    }
    if (action >= Action::QuickSlot1 && action <= Action::QuickSlot0) {
        return kKeysGroupQuick;
    }
    return kKeysGroupAct;
}

/// THE ROWS THE CONTROLS PAGE ADDS TO THE BINDINGS, and the only ones it still
/// hard-codes.
///
/// Everything a key is BOUND to is generated from ControlSettings -- see the
/// note in keyPageRows(). These are not bindings: they are what walking into
/// a ledge does, what the movement keys, JUMP, SWING and BACK do WHILE A
/// WIRE IS IN A LOCK, and -- NINE AND THE STICKS -- the PAGE GRAMMAR every
/// page reads raw ahead of any binding (confirm, back, the ring, the
/// sub-tabs, the second commit, the cursor). They carry bindable=false, and
/// the detail pane says so with a state label where the rebind verb would
/// otherwise be. Every keycap is the LIVE hand's (a pad reads A / B / LB RB /
/// LT RT / X here, a keyboard the return motif / ESC / < > / TAB / T), the
/// way every foot in the game reads.
///
/// `listRow` is the exact string the flat keyRows() list has always printed for
/// them, kept because two cases assert on it and because the one-line wording
/// genuinely differs from the two-column wording -- see KeysPageRow::listRow.
[[nodiscard]] std::vector<KeysPageRow> contextualRows(const ControlSettings& controls,
                                                      InputDevice device) {
    const bool pad = device == InputDevice::Pad;
    const std::string jump(promptLabel(controls, Action::Vertical, device));
    // MOUSE1 does not fit the flat list's sixteen cells beside "LOCK:" and
    // "FORCE"; the drawn page prints the full keycap, the flat row says LMB.
    const std::string swing(promptLabel(controls, Action::Attack, device));
    const std::string swingShort = swing == "MOUSE1" ? std::string("LMB") : swing;
    const std::string back(promptBackKey(device));
    const std::string confirm(promptConfirmKey(device));
    const std::string pages(promptPageKeys(device));
    const std::string tabs(promptTabKeys(device));
    const std::string second(promptAltCommitKey(device));
    const std::string cursor(promptMoveKeys(device));
    std::vector<KeysPageRow> rows;

    // ON EVERY PAGE -- the grammar, said once in controls.hpp and printed
    // here so a stranger can read it before they need it.
    rows.push_back(KeysPageRow{confirm, "CONFIRM", "",
                               "PICKS THE ROW UNDER THE CURSOR. ON A PAGE WITH A COMMIT "
                               "VERB AT ITS FOOT, THIS IS IT.",
                               false, confirm + "  CONFIRM", kKeysGroupPage});
    rows.push_back(KeysPageRow{back, "BACK", "",
                               "OUT OF WHATEVER IS OPEN, ONE LAYER AT A TIME. THE PAUSE MENU "
                               "COMES BACK BEFORE THE STREET DOES.",
                               false, back + "  BACK", kKeysGroupPage});
    rows.push_back(KeysPageRow{pages, "PAGE THE NOTES", "",
                               "THROUGH YOUR PAPERS: THE SHEET, THE CHART, THE LETTERS, THE "
                               "CASEBOOK, THE WARD MAP, THE GRIMOIRE, AND ROUND AGAIN.",
                               false, pages + "  PAGE", kKeysGroupPage});
    rows.push_back(KeysPageRow{tabs, "NEXT VIEW", "",
                               "THE VIEWS INSIDE ONE PAGE: THE WARD MAP'S FOUR, THE "
                               "CASEBOOK'S LEADS AND CASE, KEYS AND OPTIONS.",
                               false, tabs + "  VIEW", kKeysGroupPage});
    rows.push_back(KeysPageRow{second, "TRAVEL / TAKE", "",
                               "THE PAGE'S SECOND VERB. ON THE WARD MAP IT TRAVELS TO THE "
                               "PLACE UNDER THE CURSOR. ACROSS A COUNTER IT TAKES THEIR PRICE.",
                               false, second + "  TRAVEL", kKeysGroupPage});
    rows.push_back(KeysPageRow{cursor, "CURSOR", "",
                               pad ? "THE D-PAD AND THE LEFT STICK WALK EVERY LIST."
                                   : "THE ARROWS WALK EVERY LIST, AND THE DIGITS PICK A ROW "
                                     "OUTRIGHT. 0 IS MORE WHERE A LIST PAGES.",
                               false, cursor + "  CURSOR", kKeysGroupPage});

    // WHILE PICKING A LOCK -- a mode the simulation is in, not a verb with a
    // key of its own.
    rows.push_back(KeysPageRow{"WALK", "CLIMB A LEDGE", "",
                               "NO KEY FOR THIS -- THAT IS THE POINT. WALK AT A LOW LEDGE "
                               "AND YOU HAUL YOURSELF UP.",
                               false, "WALK AT A LEDGE", kKeysGroupNote});
    rows.push_back(KeysPageRow{pad ? std::string("\x06\x02 \x06\x03") : std::string("W  S"),
                               "AIM THE WIRE", "",
                               "TURNS THE WIRE IN THE LOCK. THE WARDS ARE NOT ALL AT THE "
                               "SAME DEPTH AND YOU HAVE TO FEEL FOR EACH ONE.",
                               false, pad ? "LOCK: \x06\x02\x06\x03  AIM" : "LOCK: W S  AIM",
                               kKeysGroupNote});
    rows.push_back(KeysPageRow{jump, "TRY THE PIN", "",
                               "PUSHES AT THE PIN YOU ARE AIMED AT. GET IT WRONG AND THE "
                               "WIRE COMPLAINS, LOUDLY, TO ANYBODY IN THE ROOM.",
                               false, "LOCK: " + jump + " TRY", kKeysGroupNote});
    rows.push_back(KeysPageRow{swing, "FORCE IT", "",
                               "STOPS BEING SUBTLE. FAST, CRUDE, AND IT COSTS YOU THE WIRE "
                               "MORE OFTEN THAN NOT.",
                               false, "LOCK: " + swingShort + " FORCE", kKeysGroupNote});
    rows.push_back(KeysPageRow{back, "LEAVE THE LOCK", "",
                               "TAKES THE WIRE BACK OUT. THE LOCK REMEMBERS NOTHING AND "
                               "NEITHER DOES THE WATCH, IF YOU ARE QUICK.",
                               false, "LOCK: " + back + "  OUT", kKeysGroupNote});
    return rows;
}

}  // namespace

std::vector<KeysPageRow> Session::keyPageRows() const {
    // GENERATED FROM THE LIVE BINDINGS, and that is the whole point of the
    // change. This page used to be a static array of strings sitting a hundred
    // lines away from a switch statement in the client, and its own comment
    // said it lived here so it "does not drift from them the moment somebody
    // rebinds one without looking down". It could not help drifting: nothing
    // connected the two. Now a rebinding shows up here by construction, because
    // this IS the binding table read out loud.
    //
    // NINE AND THE STICKS: IN THE ORDER A PLAYER READS, NOT THE ENUM'S. The
    // nine first, the map and WAIT, then movement, the bar, the shutter --
    // so the nine are the first thing on the first screen at every size.
    // And in the LIVE HAND'S OWN WORDS: the KEY column is the device that
    // last spoke (promptKey's own order), spelled the way every foot in the
    // game spells a key (promptKeyName: RT, SELECT, the D-pad cross, the
    // return motif -- never the settings file's PAD_RT), and the ALSO column
    // is the other hand. A pad's movement is its two sticks, not six axes it
    // does not have; a pad's map is a page of NOTES and its row says so.
    const InputDevice dev = promptDevice_;
    const bool pad = dev == InputDevice::Pad;
    std::vector<KeysPageRow> rows;
    rows.reserve(kActionCount + 14);
    const auto bound = [&](Action action) {
        const std::size_t i = static_cast<std::size_t>(action);
        KeysPageRow row;
        row.verb = std::string(actionLabel(action));
        row.help = std::string(actionHelp(action));
        row.group = keyGroupFor(action);
        row.bindable = true;
        const Key first = promptKey(controls_, action, dev);
        const Key other =
            first == controls_.primary[i] ? controls_.secondary[i] : controls_.primary[i];
        if (first != Key::None) {
            row.binding = std::string(promptKeyName(first));
        }
        if (other != Key::None) {
            row.alternate = std::string(promptKeyName(other));
        }
        return row;
    };

    // THE NINE.
    for (const Action action : {Action::Attack, Action::Block, Action::Cast, Action::Interact,
                                Action::Crouch, Action::Vertical, Action::Sprint, Action::Menu,
                                Action::Pause}) {
        rows.push_back(bound(action));
    }
    // THE TENTH, and how a pad actually reaches it: NOTES, then RB.
    {
        KeysPageRow map = bound(Action::Map);
        if (pad) {
            map.alternate = map.binding;
            map.binding = std::string(promptKeyName(Key::PadUp)) + " RB";
            map.bindable = false;
        }
        rows.push_back(map);
    }
    rows.push_back(bound(Action::Wait));

    // MOVEMENT: the sticks, or the mouse and the six axes.
    if (pad) {
        rows.push_back(KeysPageRow{std::string(promptKeyName(Key::PadLeftStick)), "MOVE", "",
                                   "PUSH TO WALK, FURTHER TO JOG, ALL THE WAY TO SPRINT. HOW "
                                   "FAR YOU PUSH IS YOUR GAIT.",
                                   false, "LS  MOVE", kKeysGroupMove});
        rows.push_back(KeysPageRow{std::string(promptKeyName(Key::PadRightStick)), "LOOK", "",
                                   "WHERE YOU POINT IS WHERE YOU LOOK. A SMALL PUSH AIMS, A "
                                   "FULL PUSH TURNS ROUND.",
                                   false, "RS  LOOK", kKeysGroupMove});
    } else {
        rows.push_back(KeysPageRow{"MOUSE", "LOOK", "",
                                   "WHERE YOU POINT IS WHERE YOU LOOK. RAW, WITH NO SMOOTHING "
                                   "AND NO ACCELERATION -- THE SENSITIVITY IS ON THE OPTIONS "
                                   "PAGE.",
                                   false, "MOUSE  LOOK", kKeysGroupMove});
        for (const Action action : {Action::Forward, Action::Back, Action::StrafeLeft,
                                    Action::StrafeRight, Action::TurnLeft, Action::TurnRight}) {
            rows.push_back(bound(action));
        }
    }

    // THE QUICK BAR: one row for the ten digits (keyboard only), the two
    // steps, and the shutter (a keyboard's F12).
    if (!pad) {
        KeysPageRow bar;
        bar.binding =
            std::string(keyName(controls_.primary[static_cast<std::size_t>(Action::QuickSlot1)])) +
            "-" +
            std::string(keyName(controls_.primary[static_cast<std::size_t>(Action::QuickSlot0)]));
        bar.verb = "QUICK BAR";
        bar.help = "THE NUMBER ROW READIES A CRAFTING WITH NOTHING OPEN. THE "
                   "OPTIONS PAGE LISTS ALL TEN.";
        bar.group = kKeysGroupQuick;
        bar.bindable = true;
        rows.push_back(std::move(bar));
    }
    rows.push_back(bound(Action::QuickNext));
    rows.push_back(bound(Action::QuickPrev));
    if (!pad) {
        rows.push_back(bound(Action::Screenshot));
    }

    for (KeysPageRow& row : contextualRows(controls_, dev)) {
        rows.push_back(std::move(row));
    }
    return rows;
}

KeysPageState Session::keysPageState() const {
    KeysPageState state;
    state.open = keysOpen_;
    state.title = "CONTROLS";
    // AND THE BUILD, HERE, WHERE A PLAYER GOES LOOKING FOR IT -- right-aligned
    // in the tab row, which is where the reference puts the number you want on
    // screen while you read the page. It used to be burnt into the top-left
    // corner of every captured frame at full HUD scale.
    state.readout = "GRANADAD " + std::string(sim::build_info().version);
    // The intro prose is retired (UI-EA-SPEC 1.7 #36): the tab row is the
    // header, the table is the page, and the universal grammar (ESC/B backs
    // out one layer) needs no sentence. The PAGE keeps printing BOTH devices'
    // bindings side by side -- the table's two slots ARE the two devices.
    state.rows = keyPageRows();
    state.cursor = caseCursor_;
    // NINE AND THE STICKS: the foot's keys in the live hand's own words. The
    // MORE key is the keyboard's `0`; a pad pages this list by walking the
    // cursor off the screen's end, so its foot names no digit.
    state.navMoveKeys = std::string(promptMoveKeys(promptDevice_));
    state.navTabKeys = std::string(promptTabKeys(promptDevice_));
    state.navMoreKey = promptDevice_ == InputDevice::Pad ? std::string() : std::string("0");
    return state;
}

std::vector<std::string> Session::keyRows() const {
    // DERIVED, NOT WRITTEN TWICE. See KeysPageRow::listRow: the flat list and
    // the drawn page were separate once, and separate lists of different
    // lengths is how a cursor ends up able to select a row nothing draws.
    std::vector<std::string> rows;
    for (const KeysPageRow& row : keyPageRows()) {
        rows.push_back(row.listRow.empty() ? row.binding + "  " + row.verb : row.listRow);
    }
    return rows;
}

std::vector<std::string> Session::optionRows() const {
    std::vector<std::string> rows;
    rows.reserve(kSliderRows + kActionCount);
    // THE FOUR SLIDERS, in the order somebody looking for them expects.
    rows.push_back("SENSITIVITY  " + std::to_string(controls_.mouse.sensitivity));
    rows.push_back(std::string("INVERT Y  ") + (controls_.mouse.invertY ? "ON" : "OFF"));
    rows.push_back("VIEW ANGLE  " + std::to_string(controls_.fovDegrees));
    rows.push_back("PAD DEADZONE " + std::to_string(controls_.pad.deadzonePercent));
    static_assert(Session::kSliderRows == 4, "the slider rows and adjustOption must agree");
    for (std::size_t i = 0; i < kActionCount; ++i) {
        std::string row(actionLabel(static_cast<Action>(i)));
        row += "  ";
        // The row the page is waiting on says so in place of a key, so there is
        // never a moment where the game is listening and nothing on screen says
        // it is.
        const bool listening =
            awaitingKey_ && optionCursor_ == static_cast<int>(kSliderRows + i);
        // NINE AND THE STICKS: the live hand's half of the row, promptKey's
        // own order -- a pad player reads and rebinds PAD_RT beside SWING,
        // a keyboard player MOUSE1 (see bindAwaited for the slot a press
        // lands in).
        row += listening ? "..."
                         : std::string(keyName(
                               promptKey(controls_, static_cast<Action>(i), promptDevice_)));
        rows.push_back(row);
    }
    return rows;
}

void Session::toggleOptions() {
    if (talking() || picking()) {
        return;
    }
    const bool willOpen = !optionsOpen_;
    if (willOpen) {
        // UI-EA-SPEC sec. 4 violation #4: REMEMBER THE OPENER, before the
        // stand-down below erases the evidence. Opened over the pause menu
        // (its SETTINGS row) means back returns there; opened over the
        // sibling Keys page (the TAB swap) inherits whatever that page owed
        // -- the pair is one tabbed surface as far as the way out is
        // concerned; opened from the street (F2) owes nothing.
        pageOpenedFromPause_ = pauseOpen_ || ((keysOpen_ || waitOpen_) && pageOpenedFromPause_);
    }
    optionsOpen_ = willOpen;
    if (willOpen) {
        // EVERY OTHER OVERLAY STANDS DOWN, INCLUDING THE PAUSE MENU. This used
        // to clear only the casebook and the keys page, which left a real hole:
        // open options with F2, then press F1, and keysOpen_ went true right
        // alongside it -- the keys page drew (dialogueView() checks it first)
        // while route_menu_key kept routing to the OPTIONS branch underneath,
        // because that check runs BEFORE the keys/casebook one. The page on
        // screen and the page reading your keystrokes were two different pages.
        // Reached this build via the pause menu's SETTINGS row, which is one
        // more way to get here than F2 alone used to have.
        casebookOpen_ = false;
        keysOpen_ = false;
        grimoireOpen_ = false;
        waitOpen_ = false;
        districtMapOpen_ = false;
        pauseOpen_ = false;
        quitArmed_ = false;
        menuFocus_ = kMenuFocusJournal;
    }
    awaitingKey_ = false;
    firstRun_ = false;
    optionCursor_ = 0;
    optionPage_ = 0;
    syncPanelAnim();
}

void Session::moveOptionCursor(int delta) {
    if (!optionsOpen_ || awaitingKey_) {
        return;
    }
    // AUDIO WIRING: the same one quiet tick moveTopicCursor speaks.
    if (audio_ != nullptr && delta != 0) {
        audio_->playOneShot(audio::SoundId::UiTick);
    }
    const int count = static_cast<int>(optionRows().size());
    if (count <= 0) {
        return;
    }
    optionCursor_ = ((optionCursor_ + delta) % count + count) % count;
    optionPage_ = optionCursor_ / kTopicPageSize;
}

void Session::adjustOption(int delta) {
    if (!optionsOpen_ || awaitingKey_ || delta == 0) {
        return;
    }
    switch (optionCursor_) {
        case 0:
            controls_.mouse.sensitivity += delta * kSensitivityStep;
            break;
        case 1:
            controls_.mouse.invertY = !controls_.mouse.invertY;
            break;
        case 2:
            controls_.fovDegrees += delta * kFovStep;
            break;
        case 3:
            controls_.pad.deadzonePercent += delta;
            break;
        default:
            // A BINDING ROW IS NOT A SLIDER. Nudging left on one is a player
            // looking for a value that is not there, and doing nothing is the
            // honest answer -- silently rebinding it would be worse.
            return;
    }
    controls_.sanitise();
}

void Session::chooseOption() {
    if (!optionsOpen_ || awaitingKey_) {
        return;
    }
    // AUDIO WIRING: the same accept every other list speaks -- a slider nudge
    // and a rebind-arm are both ENTER doing something.
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::UiConfirm);
    }
    if (optionCursor_ < kSliderRows) {
        // ENTER ALWAYS DOES SOMETHING. On a slider it is a nudge up, so a player
        // who has learnt ENTER on the conversation surface is not met with a
        // key that silently refuses on half the rows of this one.
        adjustOption(1);
        return;
    }
    awaitingKey_ = true;
}

void Session::bindAwaited(Key key) {
    if (!optionsOpen_ || !awaitingKey_) {
        return;
    }
    awaitingKey_ = false;
    if (key == Key::None) {
        return;
    }
    const int index = optionCursor_ - kSliderRows;
    if (index < 0 || index >= static_cast<int>(kActionCount)) {
        return;
    }
    // STEALS, VISIBLY. See ControlSettings::bind: two verbs sharing a key is a
    // game where one of them stops working and the player cannot find out
    // which. The row it was taken from shows "--" on this very page.
    //
    // NINE AND THE STICKS: INTO THE SLOT OF THE KEY'S OWN DEVICE. The two
    // slots of a verb are its two hands -- a keyboard key and a pad key -- so
    // a pad button pressed at the prompt replaces the verb's PAD half and
    // leaves MOUSE1 where it was, and a keyboard key replaces the keyboard
    // half and leaves RT. It used to land in the primary slot whichever hand
    // pressed it, which cost a pad player their keyboard key for every verb
    // they touched. A verb with no key of that device yet takes the free
    // slot (pad keys go second, keyboard keys first, the shipped order).
    const Action action = static_cast<Action>(index);
    const std::size_t slot = static_cast<std::size_t>(index);
    const bool padKey = keyIsPad(key);
    const Key first = controls_.primary[slot];
    const Key second = controls_.secondary[slot];
    bool asSecondary = padKey;
    if (first != Key::None && keyIsPad(first) == padKey) {
        asSecondary = false;
    } else if (second != Key::None && keyIsPad(second) == padKey) {
        asSecondary = true;
    }
    controls_.bind(action, key, asSecondary);
}

// ---------------------------------------------------------------------------
// the pause menu
// ---------------------------------------------------------------------------

void Session::togglePause() {
    if (talking() || picking()) {
        return;
    }
    // JUSTICE BUILD: PAUSE still opens over the hearing (a player can always
    // quit the game) -- and is REFUSED on the rope's plate and the end rows,
    // whose two rows are the only live input (spec 5).
    if (ropeCeremonyUp()) {
        return;
    }
    const bool willOpen = !pauseOpen_;
    // EVERY OTHER OVERLAY STANDS DOWN, same as toggleOptions -- opening the
    // pause menu over an already-open casebook or keys page would be the same
    // split-brain bug that fix closes there: a page on screen that is not the
    // page reading the keyboard.
    casebookOpen_ = false;
    keysOpen_ = false;
    grimoireOpen_ = false;
    waitOpen_ = false;
    districtMapOpen_ = false;
    optionsOpen_ = false;
    awaitingKey_ = false;
    menuFocus_ = kMenuFocusJournal;
    pauseOpen_ = willOpen;
    pauseCursor_ = 0;
    quitArmed_ = false;
    firstRun_ = false;
    syncPanelAnim();
}

std::string Session::custodyWaitLine() const {
    // THE WAIT ROW'S OWN REFUSAL, by who has you: the priest at the bench,
    // the officer under the arrest beat before it. The rope's plate and the
    // end rows refuse the pause menu outright (togglePause), so no line is
    // needed there; the priest's stands for any custody the two above miss.
    if (courtOpen_) {
        return "THE PRIEST IS WAITING.";
    }
    if (takenHold_ > 0) {
        return "THE WATCH HAS YOU.";
    }
    return "THE PRIEST IS WAITING.";
}

std::vector<std::string> Session::pauseRows() const {
    // MORROWIND ROUND: CONTROLS IS NEW. The tiled Menu's four tiles have no
    // room for a long, read-top-to-bottom reference list (session.hpp's own
    // note), so Keys followed Options' own precedent -- SETTINGS has always
    // opened Menu's rebinding screen through the identical optionsOpen_ state
    // -- and took a row here too, rather than being left with no door into
    // the tiled Menu at all.
    // TIME-AND-TENURE BUILD: WAIT is new, second, and a door rather than a
    // page swap -- it opens the hour-select list (see openWait). Second
    // because the two rows a player reaches for mid-game (resume, pass the
    // hours) belong above the two they visit once (controls, settings), and
    // QUIT stays last where a quit belongs.
    // JUSTICE BUILD: at the bench the WAIT row is the refusal itself --
    // state changes the label (UI-REFERENCE: never a greyed-out row), and
    // choosePause says the same line and does nothing. The row keeps its
    // place so CONTROLS, SETTINGS and QUIT keep their digits.
    return {
        "RESUME",
        inCustody() ? custodyWaitLine() : std::string("WAIT"),
        "CONTROLS",
        "SETTINGS",
        // SHIP NOTE MOVE 3: the armed row names the device's own confirm.
        quitArmed_ ? "QUIT -- SURE? " + std::string(promptConfirmKey(promptDevice_))
                   : std::string("QUIT GRANADAD"),
    };
}

void Session::movePauseCursor(int delta) {
    if (!pauseOpen_) {
        return;
    }
    // MOVING THE CURSOR DISARMS QUIT. A player who backed off the row rather
    // than pressing it again plainly changed their mind, and leaving the arm
    // set for whichever row they land on next would fire QUIT off a key that
    // was never pressed twice.
    quitArmed_ = false;
    // AUDIO WIRING: the same one quiet tick every other list speaks.
    if (audio_ != nullptr && delta != 0) {
        audio_->playOneShot(audio::SoundId::UiTick);
    }
    const int count = static_cast<int>(pauseRows().size());
    if (count <= 0) {
        return;
    }
    pauseCursor_ = ((pauseCursor_ + delta) % count + count) % count;
}

void Session::choosePause() {
    if (!pauseOpen_) {
        return;
    }
    // AUDIO WIRING: the same accept every other list speaks. CONTROLS/
    // SETTINGS' page swaps add nothing further (conversingNow() stays true
    // across a swap, so panelAnim_ never flips); RESUME's close is spoken by
    // the panel pair in syncPanelAnim(), as every close is.
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::UiConfirm);
    }
    switch (pauseCursor_) {
        case 0:
            // RESUME. The same thing ESC does from here, spelled out as a row
            // for the player who would rather click or press ENTER than learn
            // that ESC backs out of everything in this game.
            pauseOpen_ = false;
            pauseCursor_ = 0;
            quitArmed_ = false;
            syncPanelAnim();
            return;
        case 1:
            // WAIT (TIME-AND-TENURE BUILD). openWait() opens the hour-select
            // page and puts this menu down in the same call, the identical
            // shape the two rows below have. Wait mode, never sleep: the
            // healing door is the bed's Interact press and only that -- the
            // owner's ruling, enforced by which door you walked through.
            // JUSTICE BUILD: refused out loud in custody -- at the bench, and
            // under the officer's hand before it -- the page staying up; the
            // row already reads the refusal.
            if (inCustody()) {
                say(custodyWaitLine());
                return;
            }
            openWait(false);
            return;
        case 2:
            // CONTROLS. toggleKeys() opens it and puts this page down in the
            // same call, the identical shape SETTINGS below already has.
            // Relocated here from #85's own Menu cycle -- see toggleKeys()'s
            // own header on why the page itself is unchanged and only the
            // door moved.
            toggleKeys();
            return;
        case 3:
            // SETTINGS. toggleOptions() opens it and puts this page down in the
            // same call -- see its own comment on why every overlay does that,
            // AND syncs the panel anim itself -- no need to repeat it here.
            // This is the controls round's rebinding screen, reachable from the
            // menu a player actually pauses on rather than only from F2.
            toggleOptions();
            return;
        case 4:
            // QUIT. ITS OWN NUMBERED CASE, and now `default` truly is the
            // loud-failure arm the CONTROLS insertion promised: a future
            // seventh pause row that nobody wires here does NOTHING rather
            // than quietly arming QUIT.
            break;
        default:
            return;
    }
    // QUIT. Armed on the first press and confirmed on the second, so leaning on
    // ENTER once cannot close the window -- see quitArmed().
    if (!quitArmed_) {
        quitArmed_ = true;
        return;
    }
    quitArmed_ = false;
    quitRequested_ = true;
}

void Session::setCrouched(bool crouched) {
    if (refusedInCustody()) {
        return;
    }
    // A CALL THAT CHANGES NOTHING TOUCHES NOTHING. The client calls this on
    // every release of the sneak key with the HoldToggle's current answer --
    // including the release of a B whose press was Escape on a page -- and
    // dismissOverlays() ran first, unconditionally, so a release that left
    // the stance exactly as it was still put every page down: CONTROLS, B
    // landed on the street instead of the pause menu it had just returned
    // to. The stance check comes first now; only a press that actually
    // changes the stance is a world verb that puts the pages away.
    if ((tavern_->stance() == sim::Stance::Crouched) == crouched) {
        return;
    }
    dismissOverlays();
    if (talking()) {
        return;
    }
    tavern_->toggleStance();
    say(crouched ? "CROUCHED" : "UPRIGHT");
}

int Session::equippedQuickSlot() const noexcept {
    // The equipped cell derived fresh from the ONE equipped id sim::Tavern
    // holds -- the same id spellLine() reads for the CAST row, so the two
    // can never disagree about what the hand is holding.
    if (const sim::Spell* held = tavern_->equippedSpell(); held != nullptr) {
        for (std::int32_t slot = 0; slot < sim::Tavern::kQuickSlotCount; ++slot) {
            const sim::Spell* bound = tavern_->slotSpell(slot);
            if (bound != nullptr && bound->id == held->id) {
                return slot;
            }
        }
    }
    // THE CELLS PASS: ONE MARK FOR "IN YOUR HAND", CAST OR THING. The knife
    // walked onto slot 3 and pressed is as much in the hand as a crafting
    // is, and both critics read the strip's inverted cell as that mark and
    // found it lit for casts only -- so an item slot whose item is the one
    // held inverts the same way, off the same heldItem() the sheet's IN
    // HAND row reads, and gold stays the wheel's cursor alone.
    if (const sim::ItemDef* heldThing = tavern_->heldItem(); heldThing != nullptr) {
        for (std::int32_t slot = 0; slot < sim::Tavern::kQuickSlotCount; ++slot) {
            const sim::ItemDef* bound = tavern_->slotItem(slot);
            if (bound != nullptr && bound->id == heldThing->id) {
                return slot;
            }
        }
    }
    return -1;
}

void Session::selectQuickSlot(int slot) {
    if (slot < 0 || slot > 9) {
        return;
    }
    quickSlot_ = slot;
    showQuickBar();
    // SPELLS BUILD: a LOADED slot equips its crafting -- through
    // Tavern::equipSlot, which routes through the same equipSpellAt the
    // Grimoire page uses, so the CAST row, the strip's highlight and the next
    // press of C all read the one equipped id. An empty slot still says so
    // out loud: the binding is real, and a key that silently does nothing is
    // a key the player reads as broken.
    if (tavern_->equipSlot(slot)) {
        // AUDIO WIRING: the same accept every list speaks -- the press
        // changed what the hand holds.
        if (audio_ != nullptr) {
            audio_->playOneShot(audio::SoundId::UiConfirm);
        }
        // `SLOT 3 - CLEAR THE HEAD` (UI-EA-SPEC sec. 5) used to be shouted
        // here on the alert row, at the glance register, directly under a
        // strip that already names the slot in the reading register -- all
        // three critics read the shout as outranking the thing the hand is
        // on. THE CELLS PASS: a loaded slot's press is the strip's to answer
        // (the digit inverts, the name sits beside the digits, the CAST or
        // hands row reads the same id) and the alert row stays quiet for it.
        // An empty slot and a thing no longer carried still say so below: a
        // key that silently does nothing is a key the player reads as broken.
        return;
    }
    if (tavern_->slotItem(slot) != nullptr) {
        // KIT BUILD: the slot names a thing no longer carried -- said, not
        // swallowed, the empty slot's own rule.
        if (audio_ != nullptr) {
            audio_->playOneShot(audio::SoundId::UiTick);
        }
        say("SLOT " + std::to_string(slot + 1) + " - " + tavern_->slotItem(slot)->name +
            ". NOT ON YOU.");
        return;
    }
    // AUDIO WIRING: the quiet tick a cursor move gets -- nothing changed.
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::UiTick);
    }
    say("SLOT " + std::to_string(slot + 1) + " - EMPTY. THE GRIMOIRE BINDS.");
}

void Session::showQuickBar() {
    // A couple of seconds past the last touch -- the strip is up exactly
    // while the wheel or the number row is being used. The countdown's own
    // decay is quickBarAnim_'s business, driven in syncPanelAnim()/step()
    // like every other row.
    quickBarShowSteps_ = kQuickBarShowSteps;
    // KIT FIX PASS. A SLOT-JUST-CHANGED EVENT SNAPS THE STRIP OPEN, THE SAME
    // WAY say() SNAPS message_ ON RATHER THAN LEAVING IT TO THE NEXT step().
    //
    // Before this, opening the strip from a dead stop (quickBarAnim_ never
    // targeted true before -- true of the very first slot press in a fresh
    // session) rode EasedToggle::setTarget's own "first tick free" bump
    // (1/riseSteps_, an eighth) and NOTHING ELSE, because nothing here ever
    // called advance() -- that only happens once a step(), and most callers
    // of this function do not take one before the caller returns. A
    // --settle-steps=0 capture right after a fresh selectQuickSlot() (see
    // docs/frames/kit/kit-slot-960x540.png, the strip at a bare 0.125 fade)
    // photographs exactly that one-eighth-open frame and calls it the
    // feature's own showcase, which is a real defect even though a live
    // player would never notice eight steps (~130ms) of ramp.
    //
    // KITFIX LANE. THE GUARD WAS TOO NARROW: value() <= 0.0F ONLY CAUGHT A
    // DEAD STOP. A capture off SmokeRunConfig::quickbar's own scripted
    // sequence -- the Grimoire opened, a crafting walked onto a slot, the
    // page closed, the number pressed, all with --settle-steps=0 and not
    // one step() between them -- showed no strip at all
    // (docs/frames/kit/, the --quickbar shutter): the countdown was
    // genuinely armed (quickBarWanted() true) but VALUE was whatever the
    // strip's LAST rise-and-fall had left it at, because closing the
    // Grimoire mid-sequence stands the strip down first (conversingNow()
    // counts an open Grimoire), and reopening it a moment later hit
    // setTarget(true)'s "reopening mid-close resumes from wherever it had
    // got to" rule -- correct for a live player watching the ease, wrong
    // for a shutter with no frames left to resume ACROSS.
    //
    // SO: every call snaps, not just the one off a dead stop. A player never
    // sees the difference -- snapping an already-open strip back to fully
    // open is a no-op, and snapping a mid-close strip back up is the same
    // "the hand touched it again" event setTarget(true) already fires on,
    // just resolved in one frame instead of a few -- and a --settle-steps=0
    // capture now shows exactly what showQuickBar()'s own promise always
    // said: the strip is up, snapped, the instant a slot is chosen. The
    // CLOSE still eases the ordinary way: syncPanelAnim()'s
    // quickBarAnim_.setTarget(barWanted) below still owns the fall once
    // quickBarShowSteps_ runs out, so the strip's one-transition-grammar
    // veil on the way OUT is untouched.
    quickBarAnim_.snapTo(true);
}

void Session::jump() {
    if (refusedInCustody()) {
        return;
    }
    dismissOverlays();
    if (talking() || picking()) {
        return;
    }
    if (!body_->jump()) {
        return;
    }
    // NOTHING IS SAID. A jump that announced itself on the alert row every time
    // would be the noisiest thing in the game, and the player can see it.
    tavern_->setPlayerMotion(true, true);
}

void Session::toggleKeys() {
    if (talking() || picking()) {
        return;
    }
    const bool willOpen = !keysOpen_;
    if (willOpen) {
        // Violation #4: remember the opener -- see toggleOptions(). Opened
        // over the pause menu's CONTROLS row, back returns there; the
        // sibling-tab swap from Options inherits the debt.
        pageOpenedFromPause_ =
            pauseOpen_ || ((optionsOpen_ || waitOpen_) && pageOpenedFromPause_);
    }
    keysOpen_ = willOpen;
    if (willOpen) {
        // See toggleOptions' comment: an overlay that opens without putting the
        // others down is how a page draws on screen while a different one is
        // still the one reading the keyboard.
        casebookOpen_ = false;
        grimoireOpen_ = false;
        waitOpen_ = false;
        districtMapOpen_ = false;
        optionsOpen_ = false;
        pauseOpen_ = false;
        quitArmed_ = false;
        awaitingKey_ = false;
        menuFocus_ = kMenuFocusJournal;
    }
    firstRun_ = false;
    caseCursor_ = 0;
    casePage_ = 0;
    caseEntry_ = -1;
    syncPanelAnim();
}

// ---------------------------------------------------------------------------
// SPELLS BUILD: the Grimoire page, and the quick bar it loads
// ---------------------------------------------------------------------------

void Session::toggleGrimoire() {
    if (refusedInCustody()) {
        return;
    }
    if (talking() || picking()) {
        return;
    }
    const bool willOpen = !grimoireOpen_;
    grimoireOpen_ = willOpen;
    // KIT BUILD: the widget opened by the wheel's tap is the Grimoire, never
    // a search list left behind by a body walked away from.
    searchActorId_ = -1;
    if (willOpen) {
        // See toggleOptions' comment: an overlay that opens without putting the
        // others down is how a page draws on screen while a different one is
        // still the one reading the keyboard.
        casebookOpen_ = false;
        keysOpen_ = false;
        waitOpen_ = false;
        districtMapOpen_ = false;
        optionsOpen_ = false;
        pauseOpen_ = false;
        quitArmed_ = false;
        awaitingKey_ = false;
        menuFocus_ = kMenuFocusJournal;
    }
    firstRun_ = false;
    grimoireCursor_ = 0;
    grimoirePage_ = 0;
    syncPanelAnim();
}

// ---------------------------------------------------------------------------
// the ward map (core action #13)
// ---------------------------------------------------------------------------

void Session::toggleDistrictMap() {
    if (refusedInCustody()) {
        return;
    }
    // Inert while a conversation or the wire owns the keyboard, exactly like
    // toggleGrimoire -- a page opening under a topic list is the split-brain
    // bug toggleOptions' own comment describes.
    if (talking() || picking()) {
        return;
    }
    const bool willOpen = !districtMapOpen_;
    districtMapOpen_ = willOpen;
    if (willOpen) {
        // Every other overlay stands down -- toggleOptions' rule, same reason.
        casebookOpen_ = false;
        keysOpen_ = false;
        grimoireOpen_ = false;
        waitOpen_ = false;
        optionsOpen_ = false;
        pauseOpen_ = false;
        quitArmed_ = false;
        awaitingKey_ = false;
        menuFocus_ = kMenuFocusJournal;
        // THE MAP PASS. OPENING PUTS THE CURSOR WHERE YOU ARE STANDING, so the
        // first thing the page says is true of the ground under your feet and
        // the plan is already panned to your quarter. A body standing on no
        // named footprint at all (mid-harbour, on a roof) gets the nearest
        // place instead -- never a stale index from the last time it was open,
        // because "the map opened somewhere else" is the exact disorientation
        // this pass exists to remove.
        // A footprint first (the building you are standing in), then the
        // STREET you are standing on -- mapWayUnder walks every authored
        // segment, not the one segment mapPlaces() kept for its label, so a
        // body anywhere along the Tarwalk selects the Tarwalk. Only a stand on
        // neither (mid-harbour, a roof deck) falls back to the nearest place.
        int here = mapPlaceUnder(body_->tileX(), body_->tileY());
        if (here < 0) {
            here = mapWayUnder(body_->tileX(), body_->tileY(), 0);
        }
        districtMapSelected_ = here >= 0 ? here : nearestDistrictMapPlace();
        districtMapDetailFirst_ = 0;
    }
    firstRun_ = false;
    syncPanelAnim();
}

// ---------------------------------------------------------------------------
// THE MAP PASS: the page's cursor
// ---------------------------------------------------------------------------

int Session::nearestDistrictMapPlace() const {
    const std::vector<MapPlace>& places = mapPlaces();
    if (places.empty()) {
        return 0;
    }
    const std::int32_t px = body_->tileX();
    const std::int32_t py = body_->tileY();
    int best = 0;
    std::int64_t bestD = 0;
    for (std::size_t i = 0; i < places.size(); ++i) {
        const std::int64_t dx = static_cast<std::int64_t>(places[i].anchorX) - px;
        const std::int64_t dy = static_cast<std::int64_t>(places[i].anchorY) - py;
        const std::int64_t d = dx * dx + dy * dy;
        if (i == 0 || d < bestD) {
            best = static_cast<int>(i);
            bestD = d;
        }
    }
    return best;
}

void Session::moveDistrictMapCursor(MapStep step) {
    const std::vector<MapPlace>& places = mapPlaces();
    if (places.empty()) {
        return;
    }
    const int count = static_cast<int>(places.size());
    districtMapSelected_ = std::clamp(districtMapSelected_, 0, count - 1);
    if (districtMapTab_ == MapTab::Index &&
        (step == MapStep::North || step == MapStep::South)) {
        // ON A LIST, AN ARROW WALKS THE LIST. The Index is the same selection
        // the plan shows, read alphabetically, and a reader arrowing down it
        // means "the next name", not "the next place northward". Left and right
        // still step the ward, so the geographic cursor is never out of reach.
        districtMapSelected_ =
            std::clamp(districtMapSelected_ + (step == MapStep::North ? -1 : 1), 0, count - 1);
        return;
    }
    districtMapSelected_ = mapPlaceToward(districtMapSelected_, step);
}

void Session::cycleDistrictMapTab(int delta) {
    int at = static_cast<int>(districtMapTab_) + delta;
    while (at < 0) {
        at += kMapTabCount;
    }
    districtMapTab_ = static_cast<MapTab>(at % kMapTabCount);
    districtMapDetailFirst_ = 0;
}

void Session::setDistrictMapTab(int index) {
    if (index < 0 || index >= kMapTabCount) {
        return;
    }
    districtMapTab_ = static_cast<MapTab>(index);
    districtMapDetailFirst_ = 0;
}

void Session::adjustDistrictMapZoom(int delta) {
    districtMapZoom_ = std::clamp(districtMapZoom_ + delta, 0, mapZoomSteps() - 1);
}

bool Session::selectDistrictMapPlace(std::string_view name) {
    const int at = mapPlaceIndex(name);
    if (at < 0) {
        return false;
    }
    districtMapSelected_ = at;
    districtMapDetailFirst_ = 0;
    return true;
}

void Session::faceDistrictMapSelection() {
    const std::vector<MapPlace>& places = mapPlaces();
    if (places.empty()) {
        return;
    }
    const MapPlace& place =
        places[static_cast<std::size_t>(std::clamp(districtMapSelected_, 0,
                                                   static_cast<int>(places.size()) - 1))];
    // TURNING IS NOT SIMULATION STATE THAT CAN DIVERGE -- setYaw is the same
    // call every scripted line in this file already makes to point the body at
    // somebody before it talks to them, and yaw is integer BAM.
    // THE SAME POINT THE PANE PRINTED A BEARING TO -- mapAimPoint, once, so
    // the body cannot turn one way while the page says another.
    std::int32_t aimX = 0;
    std::int32_t aimY = 0;
    mapAimPoint(place, body_->tileX(), body_->tileY(), aimX, aimY);
    body_->setYaw(sim::bearingTo(body_->tileX(), body_->tileY(), aimX, aimY));
    districtMapOpen_ = false;
    syncPanelAnim();
}

namespace {

/// The HERE clause's reach: squared tile distance from the body to the
/// selection's aim point, at or under which a travel plan says standingIn
/// instead of pricing a walk. Six IS the map pane's "within two paces" --
/// the pane prints lround(sqrt(dx*dx + dy*dy)) as the paces count and calls
/// HERE at <= 2, and sqrt(6) rounds to 2 while sqrt(7) rounds to 3 -- so
/// this integer and the pane's float read are the same predicate, stated
/// once each side of the render/plan line. Change one, change both.
constexpr std::int64_t kTravelHerePacesSq = 6;

}  // namespace

std::string Session::travelRefusal() const {
    // THE CARRY CLAUSE FIRST, because it is the one refusal that is
    // load-bearing rather than circumstantial: stepSheetCase() completes the
    // delivery the moment the body is within look range of the back room, so
    // a travel permitted while carrying would teleport-finish the courier
    // case's whole final act. The nervous walk across the night district IS
    // that case's payoff, and the line says so in the ward's voice.
    if (sheetCarry_) {
        return "NOT WITH THE FINCH. WALK HIM.";
    }
    // THE WATCH, ACTIVELY NOTICING: Closing is a watchman crossing the room
    // about you, and you do not stroll off mid-witness. Mere heat or a
    // warrant deliberately does NOT refuse -- the owner ruled waiting one out
    // a tactic, and waitRefusal() ignores them for the same reason.
    if (tavern_->watchStance() == sim::Tavern::WatchStance::Closing) {
        return "NOT WITH THE WATCH CLOSING.";
    }
    // AND THE WAIT PAGE'S OWN DEFINITION OF "ANYWHERE SAFE", VERBATIM -- a
    // travel is a wait plus a relocation, so every door that refuses the one
    // refuses the other, in the same words.
    return waitRefusal();
}

Session::TravelPlan Session::districtMapTravelPlan() const {
    TravelPlan plan;
    const std::vector<MapPlace>& places = mapPlaces();
    if (places.empty()) {
        return plan;
    }
    const MapPlace& place =
        places[static_cast<std::size_t>(std::clamp(districtMapSelected_, 0,
                                                   static_cast<int>(places.size()) - 1))];
    const std::int32_t px = body_->tileX();
    const std::int32_t py = body_->tileY();
    if (place.contains(px, py)) {
        plan.standingIn = true;
        return plan;
    }
    // THE HERE CLAUSE -- the ship note's "zero-paces re-travel", closed on
    // the KEY's side to match the page. The map pane already stopped selling
    // the doorstep: within two paces of the aim the badge reads HERE and the
    // travel row sleeps (map_view's zero-paces predicate, UI-EA-SPEC sec. 4
    // #12). But the pane only hid the row -- the T press re-plans through
    // THIS function, and a place's door (the anchor, the aim) can sit just
    // OUTSIDE the footprint contains() answers for (the Counting-House's is
    // one row north of its own rectangle), so the arrival ring lands a body
    // at a door the strict check disowns and the key still charged a real
    // minute to shuffle one step and re-fire the plate, with no row on
    // screen saying it would. The predicate here is the pane's own, in the
    // sim's integers: lround(sqrt(d2)) <= 2 paces is exactly d2 <= 6, so the
    // page and the key can never name different doors. No band guard, also
    // to match the page. Checked before the refusals, the way contains()
    // above already is -- a body with nowhere to go is not refused travel,
    // it is told it has arrived.
    std::int32_t aimX = 0;
    std::int32_t aimY = 0;
    mapAimPoint(place, px, py, aimX, aimY);
    {
        const std::int64_t dx = aimX - px;
        const std::int64_t dy = aimY - py;
        if (dx * dx + dy * dy <= kTravelHerePacesSq) {
            plan.standingIn = true;
            return plan;
        }
    }
    // THE STATE REFUSALS -- they are cheap, they are the ones a player
    // needs told about, and a refused plan owes no route.
    plan.refusal = travelRefusal();
    if (!plan.refusal.empty()) {
        return plan;
    }
    // THE ARRIVAL: the same aim point the pane's bearing and FACE IT already
    // use (the door you knock on, computed above), snapped to standable
    // ground on the place's own band -- the demo Cut's own landing rule.
    if (!travelStandable(*tiles_, aimX, aimY, place.band, &plan.toX, &plan.toY)) {
        plan.refusal = "NO GROUND TO STAND ON.";
        return plan;
    }
    plan.toBand = place.band;
    // THE WALK: the district's own PathFinder, salt 0 (no jitter,
    // deterministic), Gait::Walk (the pace being charged). kSearchMaxSpan was
    // sized to admit a route from one end of the district to the other and
    // the worst measured district leg expands under 7,000 of the 10,000-node
    // budget -- and a search that still fails is an honest refusal, not a
    // fallback to the crow.
    sim::PathFinder router(*tiles_);
    std::vector<sim::PathStep> route;
    const sim::PathStep from{px, py, body_->band()};
    const sim::PathStep to{plan.toX, plan.toY, plan.toBand};
    if (!router.find(from, to, 0, route)) {
        plan.refusal = "NO WAY THERE ON FOOT.";
        return plan;
    }
    plan.routeSteps = static_cast<std::int32_t>(route.size());
    plan.units = travelRouteUnits(from, route);
    plan.seconds = travelWalkSeconds(plan.units);
    plan.minutes = travelClockMinutes(plan.seconds);
    plan.available = true;
    return plan;
}

void Session::travelDistrictMapSelection() {
    // THE VERB LIVES ON THE PAGE. Inert with the map down, exactly as the
    // other map commits are -- a key that teleported with no page up would be
    // a debug verb wearing a binding.
    if (!districtMapOpen_) {
        return;
    }
    const std::vector<MapPlace>& places = mapPlaces();
    if (places.empty()) {
        return;
    }
    // RE-CHECKED ON THE PRESS, not only at draw -- chooseWaitRow's own rule: a
    // watchman can start closing while the map is up, and the page and the key
    // must name the same door.
    const TravelPlan plan = districtMapTravelPlan();
    if (plan.standingIn) {
        return;
    }
    // AUDIO: the press is what is acknowledged, refusals included --
    // chooseWaitRow's rule again.
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::UiConfirm);
    }
    if (!plan.available) {
        // REFUSED OUT LOUD, THE PAGE STAYING UP. One string, the same one the
        // pane's verb row is printing.
        if (!plan.refusal.empty()) {
            say(plan.refusal);
        }
        return;
    }
    const MapPlace& place =
        places[static_cast<std::size_t>(std::clamp(districtMapSelected_, 0,
                                                   static_cast<int>(places.size()) - 1))];

    // 1. THE PAGE GOES DOWN FIRST. syncPanelAnim() suppresses the threshold
    // plate under any open panel and assigns lastPlaceName_ even while
    // suppressed, so a relocate-then-close would eat the plate silently.
    districtMapOpen_ = false;

    // 2. THE WAIT MACHINERY, PLUS A RELOCATION. The clock advances by exactly
    // the minutes the verb restated -- heat cools, the cellar restocks, the
    // ward's calendar and all six hundred bodies settle to the new hour, all
    // of it the same spent code every WAIT pick runs -- and the body makes
    // the same honest jump sleeping in a rented bed already makes.
    skipSeconds(plan.minutes * 60);
    placeBodyAt(plan.toX, plan.toY, plan.toBand);

    // 3. FACING THE DOOR. You arrive looking at the threshold you came for --
    // the place's own anchor -- not wherever the walk left your eyes.
    const std::int32_t doorX = static_cast<std::int32_t>(place.anchorX);
    const std::int32_t doorY = static_cast<std::int32_t>(place.anchorY);
    if (doorX != plan.toX || doorY != plan.toY) {
        body_->setYaw(sim::bearingTo(plan.toX, plan.toY, doorX, doorY));
    }

    // 4. THE THRESHOLD PLATE, ARMED EXPLICITLY. A walked crossing arms it on
    // the rising edge of the ground's own name; a travel is a crossing whose
    // walk was elided, so it is armed by hand -- with the ground's name where
    // the landing has one, the authored name where the ring pushed the body
    // just outside its own footprint. If the next step's ground disagrees,
    // syncPanelAnim()'s newest-crossing rule corrects it, exactly as it
    // corrects a walked seam.
    const std::string_view ground =
        sim::docks::placeNameAt(plan.toX, plan.toY, plan.toBand);
    lastPlaceName_.assign(!ground.empty() ? std::string(ground) : upperAscii(place.name));
    placePlateName_ = lastPlaceName_;
    placePlateShowSteps_ = kPlacePlateShowSteps;

    // 5. THE SEAM. Snapped fully black on the commit frame -- the origin is
    // never seen again after the press -- easing up on the destination with
    // the plate and the arrival line already on it. The owner called the raw
    // cut "teleporting"; this is the difference. The dressing is
    // dressInstantCut() -- the same cloth every scripted cut wears now --
    // and the view snap rode in with placeBodyAt() above.
    dressInstantCut();

    say("WALKED TO " + upperAscii(place.name) + ". " + travelClockText(timeOfDay_) + ".");
    syncPanelAnim();
}

// ---------------------------------------------------------------------------
// RELOCATED: SNAP THE VIEW -- the one seam for every instant jump
// ---------------------------------------------------------------------------
//
// See the header's own block. The three functions are deliberately small:
// the SNAP is the bug fix, the DRESSING is the owner's read of even a clean
// cut ("teleporting"), and placeBodyAt is what keeps the next relocation
// feature from having to remember either.

void Session::snapViewAfterRelocation() {
    // THE GHOST, EXACTLY: each of these rows eases out over ~8 steps when its
    // subject goes away, which is right for a subject that walked out of
    // reach and wrong for a BODY that jumped -- the old street's crosshair
    // subject, lock and room name have no business fading over the new one.
    // snapTo, not setTarget: the whole point is that nothing eases here.
    // The caches are cleared the same instant for the same reason
    // clearIfClosed() clears them -- a label must not survive the verb it
    // hung off, and after a jump there is no verb left.
    interactAnim_.snapTo(false);
    interactSubjectCache_.clear();
    interactNoteCache_.clear();
    interactKindCache_ = AimKind::Nothing;
    lockAnim_.snapTo(false);
    lockCache_.clear();
    roomAnim_.snapTo(false);
    roomCache_.clear();
    // VOICE LANE. The voice row too: a line is what a body within reach of
    // the old feet said, and a jump leaves it behind with the street it was
    // said on. Snapped, not faded, for the reason above; the channel keeps
    // its clocks and its memory of who spoke.
    voice_.standDown();
    voiceAnim_.snapTo(false);
    // NOT the case trail, the guild row, the standing, the heat, the stash,
    // the stance: those are facts about the PLAYER, and the player made the
    // journey. Only what was derived from the ground under the old feet.
    //
    // AND NOTHING FOR THE CAMERA, stated so the next reader does not hunt
    // for it: the camera is Camera::fromBody(body_->...) recomputed raw
    // every frame -- there is no eased camera position in this build, which
    // test_demo's "the frame after a cut" case now pins. If one is ever
    // added, THIS is where its snap belongs.
}

void Session::placeBodyAt(std::int32_t tileX, std::int32_t tileY, std::int32_t band) {
    body_->placeAt(tileX, tileY, band);
    snapViewAfterRelocation();
}

void Session::dressInstantCut() {
    travelFadeAnim_.snapTo(true);
    travelFadeAnim_.setTarget(false);
}

DistrictMapState Session::districtMapState() const {
    DistrictMapState plan;
    plan.tiles = tiles_.get();
    plan.palette = &mapPalette_;
    plan.bounds = mapBounds_;
    plan.playerX = static_cast<float>(body_->x()) / static_cast<float>(sim::kSubOne);
    plan.playerY = static_cast<float>(body_->y()) / static_cast<float>(sim::kSubOne);
    plan.band = body_->band();
    plan.yawBam = body_->yaw();
    plan.title = placeLabel();
    plan.openAmount = districtMapAnim_.value();
    plan.selected = districtMapSelected_;
    plan.zoom = districtMapZoom_;
    plan.tab = districtMapTab_;
    plan.detailFirst = districtMapDetailFirst_;
    // THE READOUT: the clock and the band, right-aligned and permanent. The
    // two facts a map reader wants on screen the whole time -- and the clock
    // prints its LIVE MINUTE (UI-EA-SPEC sec. 4 #10): the HUD says 08:01 and a
    // header that answers 08:00 is a page disagreeing with the world it maps.
    const int hour = ((timeOfDay_ / 3600) % 24 + 24) % 24;
    const int minute = ((timeOfDay_ / 60) % 60 + 60) % 60;
    plan.readout = (hour < 10 ? std::string("0") : std::string()) + std::to_string(hour) + ":" +
                   (minute < 10 ? std::string("0") : std::string()) + std::to_string(minute) +
                   "   BAND " + std::to_string(body_->band());

    // SHIP NOTE MOVE 3: the nav band's keys, in the device's own vocabulary.
    // NINE AND THE STICKS: each wording is what main.cpp's map branch
    // ACTUALLY routes, off the grammar's own choke points: the D-pad walks
    // places, the SUB-TAB keys (TAB / LT RT) cycle the views, the zoom rides
    // `=` `-` and the right stick, the PAGE keys (< > / LB RB) leave for the
    // map's neighbours in NOTES, the universal back (ESC / B) shuts the page
    // -- M on a keyboard too, but a pad has no map button to print -- and A
    // commits. The movement keys are the KEYCAP MOTIFS (UI-EA-SPEC sec. 5):
    // four arrowheads for the keyboard, the d-pad cross for the pad.
    plan.navMoveKeys = std::string(kGlyphMoveKeys);
    plan.navTabKeys = std::string(promptTabKeys(promptDevice_));
    // THE FIFTH SLOT ON BOTH HANDS: the map is a page of NOTES and the page
    // keys walk on to its neighbours -- `< >` on a keyboard, LB RB on a pad.
    // The ring is learnable from every page of it or it is not learnable.
    plan.navPageKeys = std::string(promptPageKeys(promptDevice_));
    // The digits on the tab row are a keyboard's; a pad steps views on the
    // triggers and has no number to press.
    plan.showDigits = promptDevice_ != InputDevice::Pad;
    if (promptDevice_ == InputDevice::Pad) {
        plan.navMoveKeys = std::string(kGlyphCross);
        plan.navZoomKeys = "RS";
        plan.navCloseKey = std::string(promptBackKey(promptDevice_));
    } else {
        plan.navCloseKey = std::string(promptLabel(controls_, Action::Map, promptDevice_));
    }
    plan.commitKey = std::string(promptConfirmKey(promptDevice_));

    // FAST TRAVEL (TRAVEL lane): the page's second commit, on the grammar's
    // one second-commit key -- T on a keyboard (a raw map-page key, Tab/=/-'s
    // own precedent), X on a pad (the face button the nine leave free). Cost
    // and refusal come off the SAME plan the commit spends, so the row and
    // the press can never name different doors.
    plan.travelKey = std::string(promptAltCommitKey(promptDevice_));
    const TravelPlan travel = districtMapTravelPlan();
    if (!travel.standingIn) {
        if (!travel.refusal.empty()) {
            plan.travelRefusal = travel.refusal;
        } else if (travel.available) {
            plan.travelCost = travelCostLabel(travel.minutes);
        }
    }

    // WHO IS IN THERE RIGHT NOW -- the People view, and the direct answer to
    // "finding the person or thing I want at that place". A const walk over the
    // already-public roster; nothing is written and nothing is hashed.
    const std::vector<MapPlace>& places = mapPlaces();
    if (!places.empty() && people_) {
        const MapPlace& place =
            places[static_cast<std::size_t>(std::clamp(districtMapSelected_, 0,
                                                       static_cast<int>(places.size()) - 1))];
        const std::vector<sim::WardActor>& roster = people_->actors();
        for (const sim::WardActor& actor : roster) {
            if (!actor.visible() || !place.contains(actor.x, actor.y)) {
                continue;
            }
            // ANY BAND, deliberately. A footprint is a claim on the GROUND and
            // everything standing on it -- the same rule plotIndexUnderfoot
            // already uses for tenure -- and a reader asking "who is in the
            // Gilded Gull" means the taproom and the rooms over it alike.
            MapPersonRow row;
            const sim::WardIdentity& who = people_->identity(actor.id);
            row.name = who.name.empty() ? std::string("SOMEBODY") : who.name;
            row.what = std::string(sim::wardTypeName(actor.type));
            row.x = actor.x;
            row.y = actor.y;
            plan.people.push_back(std::move(row));
        }
        // AND THE TAPROOM'S OWN PEOPLE, WHICH THE ROSTER DELIBERATELY DOES NOT
        // HOLD. WardPopulation spawns nobody inside the Gilded Gull -- its own
        // header says so, because the Gull already had seventeen bodies of its
        // own before the ward had any -- so a People view built off the roster
        // alone reported "NOBODY IS IN THERE" for the one address in the
        // district a player is most likely to be looking somebody up in. The
        // first capture of this page said exactly that at one in the
        // afternoon. Two systems, one question, one list.
        for (const sim::Actor& actor : tavern_->actors()) {
            if (!actor.present() || !place.contains(actor.tileX(), actor.tileY())) {
                continue;
            }
            MapPersonRow row;
            row.name = actor.name();
            row.what = std::string(sim::actorRoleName(actor.role()));
            row.x = actor.tileX();
            row.y = actor.tileY();
            plan.people.push_back(std::move(row));
        }
        // NAME ORDER, so a page turn is stable and a capture reproducible. The
        // roster's own index order is bake order, which means a body that walks
        // out of a room reshuffles every row under it.
        std::sort(plan.people.begin(), plan.people.end(),
                  [](const MapPersonRow& a, const MapPersonRow& b) {
                      if (a.name != b.name) {
                          return a.name < b.name;
                      }
                      return a.what < b.what;
                  });
    }
    return plan;
}

std::vector<std::string> Session::grimoireRows() const {
    if (searchOpen()) {
        // KIT BUILD: the corpse's kit, less what was taken -- name, weight,
        // worth -- and TAKE ALL as the last row, a row being a choice.
        std::vector<std::string> rows;
        const std::vector<sim::Tavern::CorpseRow> carried = tavern_->corpseRows(searchActorId_);
        for (const sim::Tavern::CorpseRow& row : carried) {
            const sim::ItemDef* thing = tavern_->items().at(row.item);
            if (thing == nullptr) {
                continue;
            }
            std::string line = thing->name + "  " + std::to_string(thing->drams) + "DR";
            if (thing->royals > 0) {
                line += "  " + std::to_string(thing->royals) + "C";
            }
            rows.push_back(std::move(line));
        }
        if (!carried.empty()) {
            rows.push_back("TAKE ALL");
        }
        return rows;
    }
    // ONE ROW PER KNOWN CRAFTING, grimoire order -- the same order
    // equipSpellAt counts in, so the number printed beside a row IS the index
    // the equip spends. Name, the cost model's own difficulty (the number the
    // linkcraft check is rolled against -- information, never a buff), FORGED
    // on a composition of your own (forgedSpellId's "forged." prefix is the
    // honest tag; craftedCount() only counts), the slot it is bound to, and
    // READY on the one the hand is holding.
    std::vector<std::string> rows;
    const sim::Grimoire& book = tavern_->dialogue().grimoire();
    const sim::Spell* held = tavern_->equippedSpell();
    rows.reserve(book.spells().size());
    for (const sim::Spell& spell : book.spells()) {
        std::string row = upperAscii(spell.displayName);
        row += "  D" + std::to_string(sim::spellDifficulty(spell));
        if (spell.id.rfind("forged.", 0) == 0) {
            row += "  FORGED";
        }
        for (std::int32_t slot = 0; slot < sim::Tavern::kQuickSlotCount; ++slot) {
            const sim::Spell* bound = tavern_->slotSpell(slot);
            if (bound != nullptr && bound->id == spell.id) {
                row += "  SLOT " + std::to_string(slot + 1);
                break;
            }
        }
        if (held != nullptr && held->id == spell.id) {
            row += "  READY";
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

void Session::moveGrimoireCursor(int delta) {
    if (!grimoireOpen_) {
        return;
    }
    // AUDIO WIRING: the same one quiet tick every other list speaks.
    if (audio_ != nullptr && delta != 0) {
        audio_->playOneShot(audio::SoundId::UiTick);
    }
    wrapCursorAndPage(grimoireCursor_, grimoirePage_, delta,
                      static_cast<int>(searchOpen() ? grimoireRows().size()
                                                    : tavern_->dialogue().grimoire().spells().size()));
}

void Session::nextGrimoirePage() {
    if (!grimoireOpen_) {
        return;
    }
    advancePage(grimoirePage_, grimoireCursor_,
                searchOpen() ? grimoireRows().size()
                             : tavern_->dialogue().grimoire().spells().size());
}

void Session::chooseGrimoireRow(int slot) {
    if (!grimoireOpen_ || slot < 0 || slot >= kTopicPageSize) {
        return;
    }
    const int index = grimoirePage_ * kTopicPageSize + slot;
    if (searchOpen()) {
        // KIT BUILD: TAKE the row, or TAKE ALL on the last one. The list is
        // re-read after every take (a pure function of the room), so the
        // cursor lands on what is left; an emptied body closes the list.
        const std::vector<sim::Tavern::CorpseRow> carried = tavern_->corpseRows(searchActorId_);
        if (carried.empty()) {
            closeConversation();
            return;
        }
        std::string line;
        bool anyTaken = false;
        if (index >= static_cast<int>(carried.size())) {
            if (index != static_cast<int>(carried.size())) {
                return;
            }
            std::int32_t taken = 0;
            for (const sim::Tavern::CorpseRow& row : carried) {
                const sim::Tavern::StealResult took =
                    tavern_->takeFromCorpse(searchActorId_, row.kitRow);
                if (took.result == sim::ServiceResult::Served) {
                    ++taken;
                    anyTaken = true;
                } else {
                    line = took.line;
                    break;
                }
            }
            if (line.empty()) {
                line = "TAKEN - ALL OF IT. " + std::to_string(taken) +
                       (taken == 1 ? " THING." : " THINGS.");
            } else if (taken > 0) {
                line = std::to_string(taken) + " TAKEN, THEN " + line;
            }
        } else {
            const sim::Tavern::StealResult took = tavern_->takeFromCorpse(
                searchActorId_, carried[static_cast<std::size_t>(index)].kitRow);
            anyTaken = took.result == sim::ServiceResult::Served;
            line = took.line;
        }
        if (audio_ != nullptr) {
            audio_->playOneShot(anyTaken ? audio::SoundId::ClothRustle : audio::SoundId::UiTick);
        }
        say(line);
        const int left = static_cast<int>(grimoireRows().size());
        if (left == 0) {
            closeConversation();
            return;
        }
        grimoireCursor_ = std::min(grimoireCursor_, left - 1);
        grimoirePage_ = topicPageOf(grimoireCursor_);
        return;
    }
    const std::vector<sim::Spell>& spells = tavern_->dialogue().grimoire().spells();
    if (index >= static_cast<int>(spells.size())) {
        return;
    }
    grimoireCursor_ = index;
    // THE SAME LOCK THE QUICK BAR TURNS. equipSpellAt is the one door into
    // the hand; the CAST row, the strip's highlight and the next press of C
    // all read the one id it re-points.
    if (tavern_->equipSpellAt(index)) {
        // AUDIO WIRING: the same accept every list speaks.
        if (audio_ != nullptr) {
            audio_->playOneShot(audio::SoundId::UiConfirm);
        }
        say("READY: " + upperAscii(spells[static_cast<std::size_t>(index)].displayName) + ".");
    }
}

void Session::adjustGrimoireSlot(int delta) {
    if (!grimoireOpen_ || delta == 0 || searchOpen()) {
        return;
    }
    const std::vector<sim::Spell>& spells = tavern_->dialogue().grimoire().spells();
    if (grimoireCursor_ < 0 || grimoireCursor_ >= static_cast<int>(spells.size())) {
        return;
    }
    const sim::Spell& spell = spells[static_cast<std::size_t>(grimoireCursor_)];
    // Which slot holds this crafting now, or -1 -- the cycle walks
    // NONE, 1..10 and wraps, so LEFT from NONE is slot 10 and RIGHT off
    // slot 10 is NONE again. Moving it clears the slot it leaves.
    std::int32_t current = -1;
    for (std::int32_t slot = 0; slot < sim::Tavern::kQuickSlotCount; ++slot) {
        const sim::Spell* bound = tavern_->slotSpell(slot);
        if (bound != nullptr && bound->id == spell.id) {
            current = slot;
            break;
        }
    }
    const std::int32_t positions = sim::Tavern::kQuickSlotCount + 1;
    const std::int32_t next =
        ((current + 1 + delta) % positions + positions) % positions - 1;
    if (current >= 0) {
        tavern_->clearSlot(current);
    }
    if (next >= 0) {
        tavern_->bindSpellToSlot(next, spell.id);
    }
    // AUDIO WIRING: the same one quiet tick a slider nudge gets -- the
    // restraint note stands, no new sound design.
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::UiTick);
    }
    say(next >= 0 ? upperAscii(spell.displayName) + " -- SLOT " + std::to_string(next + 1) + "."
                  : upperAscii(spell.displayName) + " -- NO SLOT.");
}

namespace {

// ---------------------------------------------------------------------------
// TIME-AND-TENURE BUILD: the Wait page
// ---------------------------------------------------------------------------
/// One row per hour ahead, round the WHOLE clock (UI-EA-SPEC 1.7 #38:
/// 12 -> 24). Any named hour is reachable in one pick from anywhere -- the
/// same skipSeconds machinery per row, rows offered only, no sim change.
constexpr int kWaitHours = 24;

/// "HH:00", two digits, the clock the compass row already speaks.
[[nodiscard]] std::string hourLabel(int hour) {
    const int wrapped = ((hour % 24) + 24) % 24;
    return (wrapped < 10 ? "0" : "") + std::to_string(wrapped) + ":00";
}

/// The name an hour is known by out loud, or "".
[[nodiscard]] const char* hourName(int hour) noexcept {
    switch (((hour % 24) + 24) % 24) {
        case 0: return "MIDNIGHT";
        case 6: return "DAWN";
        case 12: return "NOON";
        case 18: return "DUSK";
        default: return "";
    }
}

}  // namespace

void Session::openWait(bool sleepMode) {
    if (refusedInCustody()) {
        return;
    }
    if (talking() || picking()) {
        return;
    }
    // Violation #4: remember the opener -- see toggleOptions(). The pause
    // menu's WAIT row is a door, and back walks back through it.
    pageOpenedFromPause_ = pauseOpen_;
    // Every other overlay stands down -- toggleOptions' rule, same reason.
    casebookOpen_ = false;
    keysOpen_ = false;
    grimoireOpen_ = false;
    districtMapOpen_ = false;
    optionsOpen_ = false;
    pauseOpen_ = false;
    quitArmed_ = false;
    awaitingKey_ = false;
    menuFocus_ = kMenuFocusJournal;
    firstRun_ = false;
    waitOpen_ = true;
    waitSleep_ = sleepMode;
    waitCursor_ = 0;
    waitPage_ = 0;
    syncPanelAnim();
}

std::string Session::waitRefusal() const {
    // THE WHOLE OF "ANYWHERE SAFE", in check order -- see the header block in
    // session.hpp. Each clause reads state something else already owns and
    // hashes; nothing here is new simulation. Deliberately NOT consulted:
    // heat, warrants, darkness, altitude. Waiting out a warrant is an
    // intended tactic (the owner's ruling), and a roof at midnight is only as
    // dangerous as whoever is on it -- which the HOSTILE clause already asks.
    if (tavern_->playerFloored()) {
        return "NOT FROM THE FLOOR.";
    }
    if (tavern_->playerInBrawl()) {
        return "NOT WHILE FISTS ARE UP.";
    }
    // A HOSTILE body close enough to cross the room while your eyes are off
    // it. Three times a conversation's reach -- six tiles -- and Chebyshev
    // like every reach in this build. Band deliberately ignored: a hostile
    // one floor up a stair you cannot see is the conservative read.
    constexpr std::int32_t kHostileReachQ8 = 3 * sim::kReachQ8;
    for (const sim::Actor& actor : tavern_->actors()) {
        if (!actor.present()) {
            continue;
        }
        if (tavern_->dialogue().ledger().attitudeOf(actor.id()) != sim::Attitude::Hostile) {
            continue;
        }
        if (actor.distanceTo(body_->x(), body_->y()) <= kHostileReachQ8) {
            return "NOT WITH AN ENEMY THIS CLOSE.";
        }
    }
    if (body_->airborne()) {
        return "NOT IN MID-AIR.";
    }
    if (tiles_->fluidDepth(body_->tileX(), body_->tileY(), body_->band()) > 0) {
        return "NOT STANDING IN WATER.";
    }
    return {};
}

std::vector<std::string> Session::waitRows() const {
    // Twelve rows off the LIVE clock, so the hour printed is the hour a pick
    // lands on -- skipToHour truncates to the top of the hour, and a list
    // that said "1 HOUR" while delivering forty minutes would be lying.
    std::vector<std::string> rows;
    rows.reserve(kWaitHours);
    const int nowHour = timeOfDay_ / 3600;
    for (int ahead = 1; ahead <= kWaitHours; ++ahead) {
        const int target = (nowHour + ahead) % 24;
        // `1 - DAWN 06:00` (UI-EA-SPEC 1.7 #38): the hours-ahead count, the
        // hour's name where it has one, and the clock it lands on. The unit
        // word went; the numbers did not.
        std::string row = std::to_string(ahead) + " - ";
        if (hourName(target)[0] != '\0') {
            row += hourName(target);
            row += ' ';
        }
        row += hourLabel(target);
        rows.push_back(std::move(row));
    }
    return rows;
}

void Session::moveWaitCursor(int delta) {
    if (!waitOpen_) {
        return;
    }
    // AUDIO WIRING: the same one quiet tick every other list speaks.
    if (audio_ != nullptr && delta != 0) {
        audio_->playOneShot(audio::SoundId::UiTick);
    }
    wrapCursorAndPage(waitCursor_, waitPage_, delta, kWaitHours);
}

void Session::nextWaitPage() {
    if (!waitOpen_) {
        return;
    }
    advancePage(waitPage_, waitCursor_, static_cast<std::size_t>(kWaitHours));
}

void Session::chooseWaitRow(int slot) {
    if (!waitOpen_ || slot < 0 || slot >= kTopicPageSize) {
        return;
    }
    const int index = waitPage_ * kTopicPageSize + slot;
    if (index >= kWaitHours) {
        return;
    }
    waitCursor_ = index;
    const int hours = index + 1;
    const int target = ((timeOfDay_ / 3600) + hours) % 24;
    // AUDIO WIRING: the same accept every list speaks, refusals included --
    // the press is what is acknowledged, exactly chooseTopic's rule.
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::UiConfirm);
    }
    if (waitSleep_) {
        // THE BED'S OWN GATE, not waitRefusal(): sleep answers where sleep
        // has always answered. Refused out loud in restRefusal's words, the
        // page staying up -- the same door the R verb names.
        const sim::ServiceResult slept = tavern_->sleepUntil(target);
        if (slept != sim::ServiceResult::Served) {
            say(std::string(sim::restRefusal(slept)));
            return;
        }
        syncClockAfterSkip();
    } else {
        // RE-CHECKED ON THE PRESS, not only at the page's opening: a brawler
        // can close the distance while the list is up, and the page and the
        // key must name the same door.
        const std::string refusal = waitRefusal();
        if (!refusal.empty()) {
            say(refusal);
            return;
        }
        // skipToHour is Tavern::skipTo plus the calendar sync -- heat cools
        // on the elapsed seconds in there, which is what makes waiting out a
        // warrant a tactic rather than an exploit.
        skipToHour(target);
    }
    waitOpen_ = false;
    waitCursor_ = 0;
    waitPage_ = 0;
    syncPanelAnim();
    say((waitSleep_ ? std::string("SLEPT UNTIL ") : std::string("WAITED UNTIL ")) +
        hourLabel(target) + ".");
}

void Session::toggleMenuFocused(int focus) {
    if (refusedInCustody()) {
        return;
    }
    if (talking() || picking()) {
        return;
    }
    const int wrapped = ((focus % kMenuFocusCount) + kMenuFocusCount) % kMenuFocusCount;
    if (casebookOpen_) {
        if (menuFocus_ == wrapped) {
            // THE KEY THAT OPENED IT CLOSES IT -- the same rule every one of
            // #85's six pages used to have on its own, now scoped to "the tile
            // you are already looking at" rather than to a page that no
            // longer exists on its own.
            casebookOpen_ = false;
            caseCursor_ = 0;
            casePage_ = 0;
            caseEntry_ = -1;
            characterCursor_ = 0;
            characterPage_ = 0;
            mapCursor_ = 0;
            mapPage_ = 0;
            lettersCursor_ = 0;
            lettersPage_ = 0;
            lettersEntry_ = -1;
            lettersBodyPage_ = 0;
            menuFocus_ = kMenuFocusJournal;
        } else {
            // ALREADY OPEN, DIFFERENT TILE ASKED FOR: MOVE FOCUS, DO NOT
            // RESET. Each tile keeps its own cursor/page/picked-entry --
            // switching focus to read the Letters tile and back should not
            // have thrown away where the Journal tile's cursor was.
            menuFocus_ = wrapped;
        }
    } else {
        // OPENING FRESH. Every other overlay stands down, same as
        // toggleOptions()/toggleKeys()/togglePause() already do on their own.
        keysOpen_ = false;
        grimoireOpen_ = false;
        waitOpen_ = false;
        districtMapOpen_ = false;
        optionsOpen_ = false;
        pauseOpen_ = false;
        quitArmed_ = false;
        awaitingKey_ = false;
        casebookOpen_ = true;
        menuFocus_ = wrapped;
        caseCursor_ = 0;
        casePage_ = 0;
        caseEntry_ = -1;
        characterCursor_ = 0;
        characterPage_ = 0;
        mapCursor_ = 0;
        mapPage_ = 0;
        lettersCursor_ = 0;
        lettersPage_ = 0;
        lettersEntry_ = -1;
        lettersBodyPage_ = 0;
    }
    firstRun_ = false;
    syncPanelAnim();
}

void Session::toggleCasebook() {
    if (refusedInCustody()) {
        return;
    }
    recordWatchOp(WatchOpKind::Casebook);
    const WatchDepthGuard watchGuard(watchDepth_);
    toggleMenuFocused(kMenuFocusJournal);
}

void Session::toggleCharacter() { toggleMenuFocused(kMenuFocusCharacter); }

void Session::toggleMap() { toggleMenuFocused(kMenuFocusMap); }

void Session::toggleLetters() {
    recordWatchOp(WatchOpKind::Letters);
    const WatchDepthGuard watchGuard(watchDepth_);
    toggleMenuFocused(kMenuFocusLetters);
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

// ---------------------------------------------------------------------------
// Morrowind round: one Menu, four tiles (Keys and Options moved to Pause)
// ---------------------------------------------------------------------------

bool Session::menuOpen() const noexcept {
    // districtMapOpen_ joined this list with core action #13: the ward map
    // is a full-screen page, so the movement keys stand down under it the
    // way they do under every other page (main.cpp's `listening`).
    return casebookOpen_ || keysOpen_ || grimoireOpen_ || optionsOpen_ || districtMapOpen_;
}

void Session::toggleMenu() {
    // THE KEY THAT OPENED IT CLOSES IT -- toggleMenuFocused() already carries
    // this rule for "the tiled Menu, focused on the tile it is already
    // focused on", which is exactly what re-pressing the single Menu action
    // (never any of the four toggle*() entry points a real key no longer
    // reaches) always asks for.
    //
    // NINE AND THE STICKS: the ward map and the grimoire are pages of NOTES
    // (the ring), so NOTES pressed on either of them puts the papers DOWN --
    // back to the street -- rather than flipping back to the tiles over
    // them. J puts your papers down whichever page you are on.
    if (districtMapOpen_) {
        toggleDistrictMap();
        return;
    }
    if (grimoireOpen_) {
        toggleGrimoire();
        return;
    }
    toggleMenuFocused(menuFocus_);
}

void Session::menuPageNext() {
    // NINE AND THE STICKS: THE RING -- see the declaration. Four tiles, then
    // the ward map, then the grimoire, then round again. The two full-screen
    // pages open through their own toggles (which put every other overlay
    // down, the tiled Menu included) and the tiles reopen through
    // toggleMenuFocused, so no new state is kept: the ring is read off which
    // surface is up.
    //
    // EVERY STEP OF THE RING IS ONE PAGE-TURN, and it sounds like one: the
    // BookFlip the tile cycle has always played, once per step. The two
    // steps INTO the tiles clear the full-screen page's own flag directly
    // rather than toggling it shut first -- a close-then-open would flip
    // syncPanelAnim's one panel toggle twice inside a step and speak
    // BookClose + BookOpen for what is one turn of the paper (the same
    // sound bug setMenuFocus's own header names).
    //
    // Keys and Options have no tiles of their own to step between, so this
    // is a no-op while either of them is what is open; NOT OPEN AT ALL is
    // the same no-op it always was, so a bumper press with nothing open
    // still does not open anything.
    if (districtMapOpen_) {
        toggleGrimoire();  // ward map -> grimoire (the toggle puts the map down)
        pageTurnSound();
        return;
    }
    if (grimoireOpen_) {
        grimoireOpen_ = false;  // grimoire -> the first tile
        grimoireCursor_ = 0;
        grimoirePage_ = 0;
        toggleMenuFocused(kMenuFocusCharacter);
        pageTurnSound();
        return;
    }
    if (!casebookOpen_) {
        return;
    }
    if (menuFocus_ == kMenuFocusJournal) {
        toggleDistrictMap();  // the last tile -> ward map (the toggle puts the tiles down)
        pageTurnSound();
        return;
    }
    // AUDIO WIRING: the plan's "page cycle in the tiled Menu -> BookFlip".
    pageTurnSound();
    menuFocus_ = ((menuFocus_ + 1) % kMenuFocusCount + kMenuFocusCount) % kMenuFocusCount;
}

void Session::setMenuFocus(int focus) {
    // THE POINTER PASS (pointer lane, flagged): the mouse's own door onto the
    // focus the bumpers cycle -- setCasebookCursor's "a printed digit, or a
    // mouse click" shape, applied to tiles. ONE page-turn when the focus
    // actually moves, not one per step a cycle would have taken: a hover
    // crossing from Character to Letters is one gesture, and three stacked
    // BookFlips for it would be a sound bug a case cannot see.
    if (!casebookOpen_ || focus < 0 || focus >= kMenuFocusCount || focus == menuFocus_) {
        return;
    }
    pageTurnSound();
    menuFocus_ = focus;
}

void Session::menuPagePrev() {
    // The ring, backward -- see menuPageNext.
    if (districtMapOpen_) {
        districtMapOpen_ = false;  // ward map -> the last tile
        toggleMenuFocused(kMenuFocusJournal);
        pageTurnSound();
        return;
    }
    if (grimoireOpen_) {
        toggleDistrictMap();  // grimoire -> ward map (the toggle puts the grimoire down)
        pageTurnSound();
        return;
    }
    if (!casebookOpen_) {
        return;
    }
    if (menuFocus_ == kMenuFocusCharacter) {
        toggleGrimoire();  // the first tile -> grimoire (the toggle puts the tiles down)
        pageTurnSound();
        return;
    }
    // AUDIO WIRING: the same page-turn, backward.
    pageTurnSound();
    menuFocus_ =
        ((menuFocus_ - 1) % kMenuFocusCount + kMenuFocusCount) % kMenuFocusCount;
}

void Session::pageTurnSound() {
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::BookFlip);
    }
}

void Session::toggleWait() {
    if (waitOpen_) {
        closeConversation();
        return;
    }
    openWait(/*sleepMode=*/false);
}

std::vector<std::int32_t> Session::unlockedLetters() const {
    std::vector<std::int32_t> out;
    // A LETTER'S GATE IS THE SAME GATE ITS LEAD ALREADY HAS -- Cold or
    // Followed, never merely Open. An Open lead is a name in the book with
    // nobody standing over it yet; the sentence that says a letter exists at
    // all ("he wrote three unanswered letters...") is inside the lead's OWN
    // `detail` text, which the player has not read until the state has moved
    // past Open. Showing the letter earlier would hand over a document
    // before the game has told the player it exists.
    for (std::size_t i = 0; i < letterRaws_.letters().size(); ++i) {
        const sim::Letter& letter = letterRaws_.letters()[i];
        const std::int32_t lead = caseRaws_.indexOf(letter.lead);
        if (lead < 0) {
            continue;
        }
        const sim::LeadState state = casebook_.state(lead);
        if (state == sim::LeadState::Cold || state == sim::LeadState::Followed) {
            out.push_back(static_cast<std::int32_t>(i));
        }
    }
    // COURIER CASE. The sheet file rides the same shelf in COMBINED indices
    // (see letterAt), gated on ITS OWN book -- and a `handed` document is
    // post the player was SENT, readable the moment its lead is heard: the
    // courier put it in their hand, and a gate that made them walk to the
    // Gull before they could read the sheet that names the Gull would be
    // the tutorial eating itself.
    const std::int32_t base = static_cast<std::int32_t>(letterRaws_.letters().size());
    for (std::size_t i = 0; i < sheetLetterRaws_.letters().size(); ++i) {
        const sim::Letter& letter = sheetLetterRaws_.letters()[i];
        const std::int32_t lead = sheetRaws_.indexOf(letter.lead);
        if (lead < 0) {
            continue;
        }
        const sim::LeadState state = sheetBook_.state(lead);
        const bool readable =
            letter.handed
                ? state != sim::LeadState::Unheard
                : (state == sim::LeadState::Cold || state == sim::LeadState::Followed);
        if (readable) {
            out.push_back(base + static_cast<std::int32_t>(i));
        }
    }
    // EVICTION CASE. The third file rides the same shelf above the sheet's,
    // gated on ITS OWN book by the identical handed/read-not-received rule:
    // the writ is `handed` (readable the moment the hire lead is heard); the
    // widow's petition and the served notice keep the Cold-or-Followed gate,
    // so they surface only once the gate and door leads are actually walked.
    const std::int32_t evictBase =
        base + static_cast<std::int32_t>(sheetLetterRaws_.letters().size());
    for (std::size_t i = 0; i < evictLetterRaws_.letters().size(); ++i) {
        const sim::Letter& letter = evictLetterRaws_.letters()[i];
        const std::int32_t lead = evictRaws_.indexOf(letter.lead);
        if (lead < 0) {
            continue;
        }
        const sim::LeadState state = evictBook_.state(lead);
        const bool readable =
            letter.handed
                ? state != sim::LeadState::Unheard
                : (state == sim::LeadState::Cold || state == sim::LeadState::Followed);
        if (readable) {
            out.push_back(evictBase + static_cast<std::int32_t>(i));
        }
    }
    return out;
}

const sim::Letter& Session::letterAt(std::int32_t combined) const {
    // COURIER + EVICTION CASE. One shelf over THREE files, in fixed member
    // order: the Bloodletter's letters, then the sheet's, then the
    // eviction's above both. Callers only ever hand back indices
    // unlockedLetters() produced, so the subscripts below hold by
    // construction.
    const std::int32_t base = static_cast<std::int32_t>(letterRaws_.letters().size());
    const std::int32_t evictBase =
        base + static_cast<std::int32_t>(sheetLetterRaws_.letters().size());
    if (combined >= evictBase) {
        return evictLetterRaws_.letters()[static_cast<std::size_t>(combined - evictBase)];
    }
    if (combined >= base) {
        return sheetLetterRaws_.letters()[static_cast<std::size_t>(combined - base)];
    }
    return letterRaws_.letters()[static_cast<std::size_t>(combined)];
}

std::int32_t Session::letterCount() const noexcept {
    return static_cast<std::int32_t>(letterRaws_.letters().size() +
                                     sheetLetterRaws_.letters().size() +
                                     evictLetterRaws_.letters().size());
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

int Session::flyOutLeap() {
    int steps = 0;
    // Bounded by construction -- kLeapStepsPerTile * the longest reach any
    // teaching buys, or the haul -- but bounded HERE as well, because a loop
    // whose exit depends on simulation state is a loop that hangs a build the
    // day that state is wrong.
    //
    // #77 ADDS THE HAUL, and leaving it out cost four scripted lines their
    // beats before the gate caught it. A mantle is no longer instantaneous: the
    // band changes at once but the legs are locked for kHaulSteps while the eye
    // rises, because a 2.7 m wall is a climb and not a hop. A capture script
    // that drained only the LEAP walked away from the ledge on the very next
    // call, found the body would not move, counted four stuck steps and gave up
    // -- so `--skyrun` and the burglary both stopped on the roof.
    constexpr int kCeiling = 4 * sim::kLeapStepsPerTile * sim::kLeapReachTiles + sim::kHaulSteps;
    while ((body_->airborne() || body_->hauling()) && steps < kCeiling) {
        step(sim::MoveInput{});
        ++steps;
    }
    return steps;
}

void Session::interact() {
    if (refusedInCustody()) {
        return;
    }
    recordWatchOp(WatchOpKind::Interact);
    const WatchDepthGuard watchGuard(watchDepth_);
    dismissOverlays();
    if (picking()) {
        // Not this key's mode to answer for -- main.cpp's route_menu_key
        // intercepts Space/Attack/Menu while the wire is in, and Interact
        // was never one of the three it reinterprets. Refusing quietly here
        // is safer now than it was before #85: this key does a great deal
        // more than it used to, and none of it belongs mid-pick.
        return;
    }
    if (talking()) {
        chooseTopic(static_cast<std::size_t>(std::max(0, topicCursor_)));
        return;
    }
    syncTavernToBody();
    const bool sneaking = stance() == sim::Stance::Crouched;

    // #85. THE WHOLE RESOLUTION RULE. See this method's own header in
    // session.hpp for the numbered order in prose; interactPrompt() below
    // walks the identical order on read-only queries.

    // 1. NOT SNEAKING + AT YOUR OWN BED = REST. Checked first: it is the one
    // exact-tile trigger nothing else here could also mean.
    //
    // TIME-AND-TENURE BUILD: the press now opens the hour-select page in
    // sleep mode instead of committing a fixed night on the spot -- the
    // owner's Rest/Wait ruling gives the bed a chosen waking hour, and a
    // single press that irrevocably burned eight hours was always one
    // mispress from a lost evening. sleepReadiness() is sleep()'s own checks
    // with the hands kept still; the R verb (restHere) keeps the old
    // one-press night for the suite and the muscle memory.
    if (!sneaking) {
        const sim::ServiceResult bed = tavern_->sleepReadiness();
        if (bed == sim::ServiceResult::Served) {
            openWait(true);
            return;
        }
        // NobodyThere (no room ever rented) or TooFar (not at the bed) --
        // the readiness has no third answer, so anything else falls through.
    }

    // 2. PERSON IN REACH: TALK upright, PICKPOCKET sneaking.
    wardTalkId_ = -1;
    // COURIER CASE, checked ahead of TALK for the reason the rest-check runs
    // first: it is the one thing this press could mean that nothing else here
    // could also mean. A downed man is not a conversation.
    if (!sneaking && caseTakeReady()) {
        sheetCarry_ = true;
        sheetTakeSaid_ = false;
        const std::int32_t close = sheetRaws_.indexOf("bring-him-in");
        if (close >= 0) {
            // The book learns where this ends the moment the man is in hand,
            // so the objective row swings to the Mission on the same press.
            (void)sheetBook_.hear(close, caseNowSeconds());
        }
        say("YOU HAVE HIM. THE MISSION'S BACK ROOM, AND NOTHING EDGED ON THE WAY.");
        return;
    }
    // EVICTION CASE, checked ahead of TALK for caseTakeReady's own reason:
    // a served door is the one thing this press could mean that nothing else
    // here could also mean. Knock first, serve second -- two acts, one verb,
    // and the serve is never forced: a player who means to disrupt knocks
    // (or does not) and walks back to the priest to give the writ up.
    if (!sneaking && evictDoorReady()) {
        if (!evictKnocked_) {
            evictKnocked_ = true;
            evictEverKnocked_ = true;
            evictDoorSaid_ = false;
            say("YOU KNOCK. A CHAIR SCRAPES. THE MARROW DOOR OPENS ON A TIRED MAN AND THE "
                "SMELL OF THIN SOUP.");
            return;
        }
        // THE SERVE. The paper changes hands; nobody swings, which is what
        // the good hand was hired to make true. The objective swings to the
        // Mission on this press -- the book learns where the errand ends the
        // moment the writ is served.
        writServed_ = true;
        evictDoorSaid_ = false;
        const std::int32_t served = evictRaws_.indexOf("served");
        if (served >= 0) {
            (void)evictBook_.hear(served, caseNowSeconds());
        }
        say("THE WRIT INTO HIS HAND. HE READS THE ROLL'S OWN WORDS AND DOES NOT ARGUE WITH "
            "PAPER. CARRY IT BACK SIGNED.");
        return;
    }
    // 2a. THE DEAD (KIT BUILD): a corpse in reach, when no living roster
    // body is NEARER, opens its search list -- ahead of the living for the
    // person walk's own reason (a body at your feet is the one thing this
    // press could mean that the man beside it could not also mean), and
    // behind them when they are closer, so a crowded taproom still talks.
    if (searchNearestCorpse()) {
        return;
    }
    if (!sneaking) {
        // TIME-AND-TENURE BUILD: the director is told what ground the feet
        // are on BEFORE the conversation opens, so a priest's topic list is
        // built knowing whether there is a roll to read here. See
        // syncGroundPlot().
        syncGroundPlot();
        // EVICTION CASE: and the writ's stage, so Maell's own list carries
        // the hire (or the walk-back) the moment his conversation opens.
        syncEvictionTopics();
        const auto opened = [this]() {
            topicCursor_ = 0;
            haggleOffer_ = 0;
            // VOICE LANE, THE ADDRESS: where the talk was opened from, so
            // the walk-off close can tell who left -- see step().
            talkOpenTileX_ = body_->tileX();
            talkOpenTileY_ = body_->tileY();
            const sim::DialogueDirector& talk = tavern_->dialogue();
            sayAs(talk.speaker().name, talk.greeting(), false);
        };
        if (tavern_->talkTo()) {
            opened();
            return;
        }
        // 2b. A THING ON THE GROUND (KIT BUILD) sits between the house's
        // roster and the street's people: the thing you walked to outranks
        // a passer-by (and a mouse), and a named lead outranks the thing
        // (lowerHandsResolves' own rule). After the box chain would have
        // been the tidier seat, but the street's TALK is inside this walk
        // and a rope on the Tarwalk must beat it. Upright only: crouched,
        // the press is a lift or the box chain's TAKE QUIETLY below.
        if (!leadNamedInReach() && takeNearestItem()) {
            return;
        }
        if (talkToWard()) {
            opened();
            return;
        }
    } else {
        const sim::Tavern::StealResult lifted = tavern_->liftFrom();
        if (lifted.result != sim::ServiceResult::TooFar) {
            say(lifted.line);
            return;
        }
    }

    // 3. ITEM/FIXTURE: the box (or its lock), the bale, the rat, the wire
    // that buys more picks -- the same chain steal() always tried. Stance
    // changes what the NOTICE rule does with this (stealth.hpp), never which
    // function answers, so "facing a lock picks it, sneaking or not" is
    // already true with no branch here.
    if (stealNearestThing()) {
        return;
    }

    // 3b. A THING ON THE GROUND (KIT BUILD), the crouched half: upright the
    // walk took it at 2b; crouched it comes after the box chain (a bed-foot
    // strongbox keeps its stand) as TAKE QUIETLY -- the notice rule, never
    // the verb, is what the stance moves. The look still outranks the take.
    if (sneaking && !leadNamedInReach() && takeNearestItem()) {
        return;
    }

    // 4. HANDS UP AND NOTHING IN REACH: LOWER HANDS (STANCE & ROOM BUILD,
    // section 3.2 lower rule 1). After the person, the fixture and a lead the
    // book has heard of -- lowerHandsResolves() is the ONE predicate the
    // prompt walk answers LOWER HANDS with, so the reticle and the key agree
    // by construction. No line: the FISTS UP row going down is the feedback.
    if (lowerHandsResolves()) {
        tavern_->lowerPlayerHands();
        return;
    }

    // 5. NOTHING RESOLVED: the investigation look, which never refuses.
    // examine() re-checks talking()/picking()/casebookOpen_ on its own, all
    // of which dismissOverlays() above already settled, so this is safe to
    // call unconditionally.
    examine();
}

bool Session::leadNamedInReach() const {
    // A LEAD THE CROSSHAIR WOULD NAME IS SOMETHING IN REACH -- the identical
    // three-book walk, with the identical "an unheard lead is not named"
    // line, that resolveInteract() names the subject with. Read-only:
    // leadInLookReach() and its two siblings are walks over Lead::site,
    // never Casebook::look().
    const auto named = [](const sim::Casebook& book, int lead) {
        return lead >= 0 && book.raws() != nullptr &&
               book.state(static_cast<std::int32_t>(lead)) != sim::LeadState::Unheard;
    };
    return named(casebook_, leadInLookReach()) || named(sheetBook_, sheetLeadInLookReach()) ||
           named(evictBook_, evictLeadInLookReach());
}

bool Session::lowerHandsResolves() const {
    if (!tavern_->playerHandsUp()) {
        return false;
    }
    // The look outranks the lower.
    return !leadNamedInReach();
}

const sim::Actor* Session::corpseToSearch() const {
    // The corpse in reach of the BODY, and only when no living roster body
    // is nearer -- ties to the dead, so standing on him is a search.
    const sim::Actor* corpse = tavern_->corpseInReachOf(body_->x(), body_->y(), body_->band());
    if (corpse == nullptr) {
        return nullptr;
    }
    const bool sneaking = stance() == sim::Stance::Crouched;
    const std::int32_t reach = sneaking ? sim::kLiftReachQ8 : sim::kReachQ8;
    const sim::Actor* living = tavern_->nearestTo(body_->x(), body_->y(), reach);
    if (living != nullptr &&
        living->distanceTo(body_->x(), body_->y()) < corpse->distanceTo(body_->x(), body_->y())) {
        return nullptr;
    }
    return corpse;
}

namespace {

/// HOW CLOSE TO A THING THE RETICLE HAS TO BE, in degrees between the eye's
/// line and the nearest point of the span the thing is drawn over. A thing
/// inside it is on screen (the 90-degree lens at 16:9 sees 29.4 degrees down
/// from the reticle) and near the middle of it; the coil two tiles ahead of
/// a level eye is 34.6 degrees under it, off the frame, and is not named.
constexpr float kThingAimDegrees = 25.0F;

}  // namespace

std::int32_t Session::groundItemUnderReticle() const {
    const Camera eye = Camera::fromBody(body_->x(), body_->y(), body_->eyeZ(), body_->yaw(), body_->pitch(), 1.0F);
    const float level = std::cos(eye.pitch);
    const float fx = std::sin(eye.yaw) * level;
    const float fy = -std::cos(eye.yaw) * level;
    const float fz = std::sin(eye.pitch);
    const float cone = std::cos(kThingAimDegrees * (3.14159265358979323846F / 180.0F));
    const std::vector<sim::GroundItem>& ground = tavern_->groundItems();
    std::int32_t best = -1;
    float bestCos = cone;
    for (std::size_t i = 0; i < ground.size(); ++i) {
        const sim::GroundItem& entry = ground[i];
        if (entry.band != body_->band()) {
            continue;
        }
        // The take's own reach (Tavern::groundItemInReachOf).
        const std::int32_t dx = sim::q8_tile_centre(entry.x) - body_->x();
        const std::int32_t dy = sim::q8_tile_centre(entry.y) - body_->y();
        if ((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) > sim::kReachQ8) {
            continue;
        }
        const sim::ItemDef* thing = tavern_->items().at(entry.item);
        if (thing == nullptr) {
            continue;
        }
        // The nearest of the span's foot, middle and head: a lantern on a
        // barrel is looked at, not the boards under the barrel.
        const float vx = static_cast<float>(entry.x) + 0.5F - eye.x;
        const float vy = static_cast<float>(entry.y) + 0.5F - eye.y;
        const float boards = bandSurface(entry.band);
        float here = -2.0F;
        for (int k = 0; k < 3; ++k) {
            const std::int32_t cm = k == 0   ? thing->drawnLoCm
                                    : k == 2 ? thing->drawnHiCm
                                             : (thing->drawnLoCm + thing->drawnHiCm) / 2;
            const float vz = boards + static_cast<float>(cm) * 0.01F - eye.z;
            const float length = std::sqrt(vx * vx + vy * vy + vz * vz);
            here = std::max(here, length < 1.0e-3F ? 1.0F : (vx * fx + vy * fy + vz * fz) / length);
        }
        // Strictly nearer the line wins; a tie keeps the list's order.
        if (here >= cone && (best < 0 || here > bestCos)) {
            best = static_cast<std::int32_t>(i);
            bestCos = here;
        }
    }
    return best;
}

bool Session::groundTargetFor(InteractTarget& out) const {
    if (leadNamedInReach()) {
        return false;
    }
    const std::int32_t at = groundItemUnderReticle();
    if (at < 0) {
        return false;
    }
    const sim::GroundItem& entry = tavern_->groundItems()[static_cast<std::size_t>(at)];
    const sim::ItemDef* thing = tavern_->items().at(entry.item);
    // A fixed thing (the strongbox) is not named here; the box block is.
    if (thing == nullptr || thing->fixed) {
        return false;
    }
    out = InteractTarget{};
    out.verb = stance() == sim::Stance::Crouched ? "TAKE QUIETLY" : "TAKE";
    out.subject = entry.count > 1 ? std::to_string(entry.count) + " " + thing->name : thing->name;
    out.note = entry.owned ? std::string("THEIRS") : std::to_string(thing->drams) + "DR";
    out.kind = entry.owned ? AimKind::Owned : AimKind::Thing;
    return true;
}

std::string Session::interactPrompt() const {
    // ONE DESCRIPTION OF THE RESOLUTION ORDER, NOT TWO. The crosshair pass
    // needed the walk to answer WHAT as well as WHICH VERB, and a second copy
    // of the order in this file is precisely the drift that lets the HUD start
    // lying about the key -- see interact()'s own header. So the walk moved
    // whole into interactTarget() and this is the half of its answer every
    // caller before the crosshair pass wanted.
    return interactTarget().verb;
}

namespace {

/// THE FURTHEST A CROSSHAIR REACHES FOR A BUILDING'S NAME, in tiles.
///
/// FIVE, and it is the same measurement kLookRangeTiles is: a shop in this
/// ward is seven by eight (DOCKS-GAZETTEER 3.1), so five tiles down the line
/// of sight is "the frontage I am standing at" and not "the warehouse across
/// the quay". It is deliberately one tile longer than the look range, because
/// a player reads a door from the pavement and looks at a lead from inside.
constexpr std::int32_t kAimPlaceReachTiles = 5;

/// Whether two labels say the same word, ignoring case.
///
/// THE WARD HAS BODIES WHOSE NAME IS THEIR TRADE. A mouse is named "MOUSE" and
/// its trade is "MOUSE", and the first capture of the crosshair over one read
/// "MOUSE  MOUSE" -- a qualifier qualifying nothing, which is the same "absence
/// costs nothing" rule the top-right stack has kept since polish-1, missed in
/// a new place. See interactTarget().
[[nodiscard]] bool saysTheSame(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const char lhs = a[i] >= 'a' && a[i] <= 'z' ? static_cast<char>(a[i] - 32) : a[i];
        const char rhs = b[i] >= 'a' && b[i] <= 'z' ? static_cast<char>(b[i] - 32) : b[i];
        if (lhs != rhs) {
            return false;
        }
    }
    return true;
}

/// A Q16 offset, rounded to the nearest whole tile in both directions.
/// Truncation toward zero would make a body facing due west name a place one
/// tile east of the one it is looking at.
[[nodiscard]] std::int32_t tilesFromQ16(std::int32_t q16, std::int32_t steps) noexcept {
    const std::int64_t scaled = static_cast<std::int64_t>(q16) * steps;
    const std::int64_t half = sim::kTrigOne / 2;
    return static_cast<std::int32_t>(scaled >= 0 ? (scaled + half) / sim::kTrigOne
                                                 : (scaled - half) / sim::kTrigOne);
}

}  // namespace

Session::InteractTarget Session::interactTarget() const {
    InteractTarget out = resolveInteract();
    // ONE RULE OVER ALL EIGHT EXITS OF THE WALK -- see resolveInteract()'s own
    // header. A note that repeats its subject is a row of the HUD holding
    // twice the width to say one thing, which is the exact defect polish-1
    // deleted "NOBODY IN PARTICULAR" for.
    if (saysTheSame(out.subject, out.note)) {
        out.note.clear();
    }
    return out;
}

Session::InteractTarget Session::resolveInteract() const {
    InteractTarget out;
    // #85. THE LABEL Interact IS ABOUT TO RESOLVE TO -- see interactPrompt()'s
    // own header in session.hpp for the verification gap (the bale, the rat and
    // buyPicks() are not previewed) and for why the order below has to track
    // interact()'s own order exactly.
    if (talking() || picking() || pauseOpen() || menuOpen() || waitOpen() || inCustody()) {
        // JUSTICE BUILD: in custody -- the officer's hand on you, the page,
        // the plate -- Interact does nothing, so the crosshair promises
        // nothing: "E - TALK" over the watchman who has just taken you would
        // be a prompt for a verb that is refused.
        // The topic list / the tiled Menu's own rows already show what
        // Interact (or ENTER) does on this row -- menuOpen() covers the
        // tiled Menu, Keys AND Options, not only Options, which an earlier
        // pass of this check missed (caught by test_interact_context.cpp:
        // toggleMenu() opens the casebook first, and the label kept
        // computing a real prompt behind it). A second, floating label would
        // say the same thing twice in two different places on the same
        // frame. waitOpen() joined the list with the Wait page, whose rows
        // print their own numbers the same way.
        return out;
    }
    const bool sneaking = stance() == sim::Stance::Crouched;

    // 1. REST. Tile-exact rather than sleep()'s own Q8 Chebyshev distance --
    // close enough for a label, and body_ has no reason to expose Q8 here
    // when the tile the body is standing on already answers it.
    if (!sneaking && tavern_->rentedRoom() >= 0 && body_->band() == sim::gull::kUpperBand) {
        const sim::gull::GuestRoom& room = sim::gull::kRooms[tavern_->rentedRoom()];
        if (body_->tileX() == room.standX && body_->tileY() == room.standY) {
            out.verb = "REST";
            out.subject = "YOUR BED";
            out.note = "ROOM " + std::to_string(tavern_->rentedRoom() + 1);
            out.kind = AimKind::Thing;
            return out;
        }
    }

    // 2. PERSON. The identical reach each verb would actually use --
    // sim::kReachQ8 is talkTo()'s own 2*kSubOne, sim::kLiftReachQ8 is
    // liftFrom()'s. Nearest is a read-only query on both Tavern and
    // WardPopulation; neither talks to anybody by being asked.
    const std::int32_t reach = sneaking ? sim::kLiftReachQ8 : sim::kReachQ8;
    // COURIER CASE, mirrored EXACTLY where interact() checks it: ahead of the
    // person walk, so the crosshair names the take on the same frame the key
    // would perform it.
    if (!sneaking && caseTakeReady()) {
        const sim::Actor* quarry = sheetQuarry();
        out.verb = "TAKE HIM UP";
        out.subject = quarry == nullptr ? std::string("FINCH") : quarry->name();
        out.note = "DOWN, AND COMING WITH YOU";
        out.kind = AimKind::Person;
        return out;
    }
    // EVICTION CASE, mirrored where interact() checks it: the knock, then the
    // serve, named on the frame the key would perform it.
    if (!sneaking && evictDoorReady()) {
        out.verb = evictKnocked_ ? "SERVE THE WRIT" : "KNOCK";
        out.subject = "THE MARROW DOOR";
        out.note = evictKnocked_ ? "THEY ARE AT THE DOOR" : "A LIGHT IS ON";
        out.kind = AimKind::Place;
        return out;
    }
    // 2a. THE DEAD (KIT BUILD), mirrored where interact() checks it: the
    // corpse in reach, when no living roster body is nearer, is a SEARCH,
    // named, a body's own accent, and DEAD where a living man's trade would
    // print.
    if (const sim::Actor* corpse = corpseToSearch(); corpse != nullptr) {
        out.verb = "SEARCH";
        out.subject = corpse->name();
        out.note = "DEAD";
        out.kind = AimKind::Person;
        return out;
    }
    // NAMED IN THE ORDER interact() WOULD REACH THEM. The taproom's own roster
    // is asked first because talkTo() is, so the body the crosshair names is
    // the body the key would actually speak to -- a prompt that named the
    // wrong one of two people in a doorway would be worse than the bare verb
    // it replaced.
    if (const sim::Actor* inHouse = tavern_->nearestTo(body_->x(), body_->y(), reach);
        inHouse != nullptr) {
        // Pickpocketing a WARD actor is not implemented (lift() only ever
        // reached the Tavern's own roster) -- matched here rather than
        // previewing a verb the button cannot actually perform.
        out.verb = sneaking ? "PICKPOCKET" : "TALK";
        out.subject = inHouse->name();
        out.note = std::string(sim::actorRoleName(inHouse->role()));
        out.kind = AimKind::Person;
        return out;
    }
    // 2b. A THING ON THE GROUND (KIT BUILD), mirrored where interact()
    // reaches it upright -- between the house's roster and the street's
    // people: TAKE names the thing, the note is its weight, or THEIRS with
    // the Owned accent when taking it is theft (the reference's red hand,
    // before the press). A named lead in reach outranks it. The BODY's own
    // position, as every other query on this walk, and the body's own eye:
    // only the thing the reticle is on (groundItemUnderReticle). Crouched,
    // the same read comes after the box (TAKE QUIETLY), where interact()
    // takes it.
    if (!sneaking) {
        if (InteractTarget thing; groundTargetFor(thing)) {
            return thing;
        }
        if (const sim::WardActor* outside = wardAddressee(); outside != nullptr) {
            out.verb = "TALK";
            // #79's own point, on the HUD at last: the ward HAS names. A body
            // the bake never named is "SOMEBODY", which is the ward map's own
            // wording for the same gap rather than a second invention.
            const sim::WardIdentity& who = people_->identity(outside->id);
            out.subject = who.name.empty() ? std::string("SOMEBODY") : who.name;
            out.note = std::string(sim::wardTypeName(outside->type));
            // NINE AND THE STICKS: THE RETICLE DOES NOT LIE ABOUT THE HAND.
            // A street body is in TALK reach and OUT of the swing's: the
            // sightline a blow walks (Tavern::sightlineTarget) is the Gull's
            // roster only, so SWING here whiffs with NOBODY IN REACH. While
            // the fists are up the note says so BEFORE the press, in the
            // same breath TALK is offered -- the street becomes hittable
            // when the STREET SENSES lane lands, and this note goes with it.
            if (tavern_->playerHandsUp()) {
                out.note += " - NO BLOW REACHES THEM";
            }
            out.kind = AimKind::Person;
            return out;
        }
    }

    // 3. THE BOX -- the one item this can preview exactly, because its
    // geometry (gull::roomAtStand) and its state (rentedRoom/crackedBoxes/
    // openedLocks) are all public, read-only, and the same ones
    // crackStrongbox() itself reads.
    if (body_->band() == sim::gull::kUpperBand) {
        const std::int32_t room = sim::gull::roomAtStand(body_->tileX(), body_->tileY());
        if (room >= 0) {
            const std::int32_t bit = 1 << room;
            out.subject = "THE STRONGBOX";
            out.note = "ROOM " + std::to_string(room + 1);
            out.kind = AimKind::Thing;
            if (room == tavern_->rentedRoom()) {
                // Resolves to a refusal ("THAT ONE IS YOURS"), but the box is
                // still what the press is about, so the button still names
                // an action rather than falling back to LOOK. STATE CHANGES
                // THE NOTE, which is the reference's own rule -- and it is
                // the difference between "that key does nothing" and "that
                // one is mine".
                out.verb = "TAKE";
                out.note += "  YOURS";
                return out;
            }
            if ((tavern_->crackedBoxes() & bit) == 0) {
                if ((tavern_->openedLocks() & bit) == 0) {
                    out.verb = "PICK LOCK";
                    out.note += "  LOCKED";
                    return out;
                }
                out.verb = sneaking ? "TAKE QUIETLY" : "TAKE";
                out.note += "  OPEN";
                return out;
            }
            // Cracked already. LOOK is what the key falls through to, and the
            // note says why rather than leaving the player to press it twice.
            out.note += "  EMPTIED";
        }
    }

    // 3b. A THING ON THE GROUND (KIT BUILD), the crouched half: TAKE QUIETLY
    // after the box's own stand, exactly where interact() takes it crouched.
    if (sneaking) {
        if (InteractTarget thing; groundTargetFor(thing)) {
            return thing;
        }
    }

    // 4. THE LOOK, AND WHAT IT WOULD BE LOOKING AT.
    //
    // interactPrompt() has answered a bare "LOOK" here since #85 and that is
    // the single biggest reason the owner could not tell a failed search from
    // absent content -- his Bloodletter run stalled at the Weighhouse. The
    // verb is unchanged; what is new is that the crosshair NAMES THE LEAD when
    // one is standing under it.
    out.verb = "LOOK";
    if (out.subject.empty()) {
        if (const int lead = leadInLookReach(); lead >= 0 && casebook_.raws() != nullptr) {
            const sim::Lead& site = casebook_.raws()->leads()[static_cast<std::size_t>(lead)];
            const sim::LeadState state = casebook_.state(static_cast<std::int32_t>(lead));
            // AN UNHEARD LEAD IS NOT NAMED, and that is a design line rather
            // than an oversight. Unheard means nobody has told the player this
            // exists; a crosshair that named it would hand them the trail for
            // walking past a door and turn an investigation into a sweep. Once
            // it IS in the book -- which is what `start` and every `opens` list
            // do -- naming it is exactly what the owner's stall at the
            // Weighhouse needed.
            if (state != sim::LeadState::Unheard) {
                // `what` and not `short`: the authored line is the OBJECT, in
                // the ward's own words -- "THE HARBORMASTER'S LEDGER", "THE
                // SEWER MOUTH IN THE SEAWALL", "THE BODY, AND WHOEVER FOUND
                // IT". `short` is a casebook ROW label ("THE BODY"), authored
                // for a three-column grid, and pairing the two would print the
                // same noun twice on one row.
                out.subject = site.what.empty() ? site.place : site.what;
                // STATE CHANGES THE NOTE, which is the reference's own rule
                // and the whole point here: a player sweeping a room they have
                // already worked must not be sent round it a second time.
                // ABSENCE COSTS NOTHING (this file's own polish-1 rule) -- an
                // unread lead says nothing extra, because "not read" is what
                // the ordinary case is.
                if (state != sim::LeadState::Open) {
                    out.note = "ALREADY READ";
                }
                out.kind = AimKind::Clue;
                return out;
            }
        }
    }
    // COURIER CASE. The second book's sites get the identical naming, asked
    // only when the first named nothing -- the same member-order tiebreak
    // examine() keeps.
    if (out.subject.empty()) {
        if (const int lead = sheetLeadInLookReach(); lead >= 0 && sheetBook_.raws() != nullptr) {
            const sim::Lead& site = sheetBook_.raws()->leads()[static_cast<std::size_t>(lead)];
            const sim::LeadState state = sheetBook_.state(static_cast<std::int32_t>(lead));
            if (state != sim::LeadState::Unheard) {
                out.subject = site.what.empty() ? site.place : site.what;
                if (state != sim::LeadState::Open) {
                    out.note = "ALREADY READ";
                }
                out.kind = AimKind::Clue;
                return out;
            }
        }
    }
    // EVICTION CASE. The third book's sites, the same member-order tiebreak
    // one book deeper -- its two close leads are excluded by
    // evictLeadInLookReach itself, so a look here can only ever name an
    // investigation lead.
    if (out.subject.empty()) {
        if (const int lead = evictLeadInLookReach(); lead >= 0 && evictBook_.raws() != nullptr) {
            const sim::Lead& site = evictBook_.raws()->leads()[static_cast<std::size_t>(lead)];
            const sim::LeadState state = evictBook_.state(static_cast<std::int32_t>(lead));
            if (state != sim::LeadState::Unheard) {
                out.subject = site.what.empty() ? site.place : site.what;
                if (state != sim::LeadState::Open) {
                    out.note = "ALREADY READ";
                }
                out.kind = AimKind::Clue;
                return out;
            }
        }
    }
    // 5. LOWER HANDS (STANCE & ROOM BUILD): every lead block above returned
    // when it named one, so reaching here with the hands up is exactly the
    // press interact() lowers them on -- the same lowerHandsResolves() it
    // reads. A verb with no subject: "nothing in reach" is the whole point,
    // and a box note or a place name would be the HUD naming a thing the key
    // is not about to touch.
    if (lowerHandsResolves()) {
        out.verb = "LOWER HANDS";
        out.subject.clear();
        out.note.clear();
        out.kind = AimKind::Nothing;
        return out;
    }
    if (out.subject.empty()) {
        // THE DOOR YOU ARE ACTUALLY FACING. The ward map pass built the
        // district's own place index off the authored footprints -- see
        // mapPlaceUnder -- so "what building is this" is a lookup here rather
        // than a second walk over the sign table. WAYS ARE SKIPPED: the
        // compass ribbon already prints the street's name every frame, and a
        // crosshair repeating it would be the HUD saying one thing twice.
        const std::int32_t fx = sim::forward_x_q16(body_->yaw());
        const std::int32_t fy = sim::forward_y_q16(body_->yaw());
        for (std::int32_t step = 0; step <= kAimPlaceReachTiles; ++step) {
            const std::int32_t tx = body_->tileX() + tilesFromQ16(fx, step);
            const std::int32_t ty = body_->tileY() + tilesFromQ16(fy, step);
            const int place = mapPlaceUnder(tx, ty);
            if (place < 0) {
                continue;
            }
            const MapPlace& named = mapPlaces()[static_cast<std::size_t>(place)];
            if (named.way) {
                continue;
            }
            out.subject = named.name;
            out.kind = AimKind::Place;
            break;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Morrowind round: the tiled Menu's four panels, each built independently of
// which (if any) currently has focus -- drawMenuTiles() needs all four every
// frame, and dialogueView() returns whichever one menuFocus_ names so a
// caller that only ever asked about "the open page" keeps seeing exactly the
// content it always did. CONTENT UNCHANGED FROM #85's SIX-PAGE MENU; only
// the per-tile cursor/page fields (characterCursor_/mapCursor_/
// lettersCursor_ instead of a shared caseCursor_) are new -- see session.hpp's
// own note on why each tile needs its own now that all four can be mid-read
// at once.
// ---------------------------------------------------------------------------

DialogueViewState Session::characterPanelView() const {
    DialogueViewState view;
    view.phase = static_cast<float>(body_->stepCount()) / 60.0F;
    view.open = true;
    view.speaker = "CHARACTER";
    const std::string title = legendLine();
    view.epithet = title.empty() ? std::string(sim::kReputationUnremarkable) : title;
    // KIT BUILD: over a carried row the epithet is the verbs, in the
    // device's own words -- the Grimoire page's "LEFT RIGHT BIND A SLOT"
    // idiom -- so the tile never advertises a press it will not take. The
    // reputation line stands on every sheet row.
    if (const std::optional<KitRow> row = highlightedKitRow(); row.has_value() && row->inKit) {
        const sim::ItemDef* thing = tavern_->items().at(row->item);
        const InputDevice dev = promptDevice_;
        // The arrows as motifs on a keyboard (the tile is thirty-odd cells
        // wide at 960 and "LEFT RIGHT" pushed DROP off it), the bare cross on
        // a pad -- UI-EA-SPEC sec. 5's own substitutions.
        std::string verbs;
        if (thing != nullptr && thing->slot != sim::ItemSlot::None) {
            verbs = std::string(promptConfirmKey(dev)) +
                    (tavern_->kit().isWorn(row->item) ? " BARE  " : " WEAR  ") +
                    (dev == InputDevice::Pad ? std::string(1, kMotifDPad)
                                             : std::string{kMotifLeft, kMotifRight}) +
                    " SLOT  ";
        }
        verbs += "X DROP";
        view.epithet = verbs;
    }
    view.line = "WHAT THE STREETS HAVE MADE OF YOU, AND THE HANDS THAT DID IT.";
    for (const std::string& row : characterRows()) {
        view.topics.push_back(row);
    }
    view.cursor = characterCursor_;
    view.page = characterPage_;
    return view;
}

DialogueViewState Session::mapPanelView() const {
    // #82. THE DISTRICT MAP. Same reason every other tile is drawn through
    // this shared vocabulary: one content shape, proved once.
    DialogueViewState view;
    view.phase = static_cast<float>(body_->stepCount()) / 60.0F;
    view.open = true;
    view.speaker = "THE CHART";
    // WHERE YOU ARE, right where a map's own "you are here" would go.
    view.epithet = placeLabel();
    view.line = "KNOWN GROUND, OPEN LEADS AND WHO WILL TALK, RECKONED FROM WHERE YOU STAND.";
    // AND WHAT THE BLANK HALF OF THE PANEL IS WAITING FOR. All three sections
    // above are built out of casebook_.known() and nothing else -- see
    // mapRows' own header -- so the honest sentence is that the chart is a
    // reading of the book, and the book is what the player fills. True at
    // every point in a run, which is why it is not gated on the row count:
    // this panel is never finished until the case is.
    // Empty states run six words or fewer now (UI-EA-SPEC): the reading is
    // the book's, said in four.
    view.emptyLine = "WHAT THE CASEBOOK KNOWS.";
    for (const std::string& row : mapRows()) {
        view.topics.push_back(row);
    }
    view.cursor = mapCursor_;
    view.page = mapPage_;
    return view;
}

DialogueViewState Session::lettersPanelView() const {
    // TASK #82. A LETTER, DRAWN AS A DOCUMENT INSTEAD OF A LIST. Same content
    // shape every other tile uses; menu_view.cpp switches to the parchment
    // palette and pages through the body as wrapped prose instead of a row
    // list once DialogueViewState::letter is set -- see that field's own
    // header.
    DialogueViewState view;
    view.phase = static_cast<float>(body_->stepCount()) / 60.0F;
    view.open = true;
    const std::vector<std::int32_t> unlocked = unlockedLetters();
    // COURIER CASE. Whether anything on the shelf right now was put into the
    // player's own hand -- the epithet and the pick line below stop claiming
    // "read, not received" the moment that stops being the whole truth.
    bool anyHanded = false;
    for (const std::int32_t index : unlocked) {
        if (letterAt(index).handed) {
            anyHanded = true;
            break;
        }
    }
    if (lettersEntry_ >= 0 && static_cast<std::size_t>(lettersEntry_) < unlocked.size()) {
        const sim::Letter& read =
            letterAt(unlocked[static_cast<std::size_t>(lettersEntry_)]);
        view.speaker = read.from.empty() ? std::string("A LETTER") : read.from;
        view.line = "A LETTER, READ IN FULL BELOW.";
        view.letter = true;
        // ONE ENTRY A PARAGRAPH, RAW -- see DialogueViewState::letterLines'
        // own header on why WRAPPING happens at draw time rather than
        // here: Session has no window size to wrap prose against, and a
        // machine that pre-decided where a sentence breaks at every
        // resolution is the exact "picks badly" case casebook.hpp's own
        // `brief` field warns about for a proper noun -- prose gets to
        // wrap for real.
        if (!read.to.empty()) {
            view.letterLines.push_back("TO " + read.to);
        }
        if (!read.dateline.empty()) {
            view.letterLines.push_back(read.dateline);
        }
        if (!read.salutation.empty()) {
            view.letterLines.push_back(read.salutation);
        }
        for (const std::string& paragraph : read.body) {
            view.letterLines.push_back(paragraph);
        }
        if (!read.closing.empty()) {
            view.letterLines.push_back(read.closing);
        }
        if (!read.signature.empty()) {
            view.letterLines.push_back("- " + read.signature);
        }
        view.page = lettersBodyPage_;
    } else {
        view.speaker = "THE LETTERS";
        // COURIER CASE. "READ, NOT RECEIVED" survives only while it is true:
        // a handed mission sheet IS received post, so the epithet and the
        // pick line change with the shelf rather than lying about it.
        view.epithet = anyHanded ? "THE WARD'S PAPER" : "READ, NOT RECEIVED";
        view.line = unlocked.empty()
                        ? "YOU HAVE READ NOBODY'S POST YET."
                        : (anyHanded
                               ? "YOUR OWN PAPER, AND OTHER PEOPLE'S. PICK ONE AND READ IT WHOLE."
                               : "OTHER PEOPLE'S PAPER. PICK ONE AND READ IT WHOLE.");
        // THE ONE PANEL IN THE BUILD THAT SHIPS WITH NOTHING IN IT AT ALL, and
        // the tile draws no `line`, so until this the first thing a stranger
        // saw here was a title over a quarter-screen of black. The gate is
        // unlockedLetters()' own -- a letter turns up when its lead has been
        // stood over, never merely heard -- so the sentence names exactly the
        // act that fills the panel, and stops claiming nobody has written to
        // you the moment somebody has.
        //
        // "READ, NOT RECEIVED" is the epithet above it, and the wording keeps
        // faith with it: none of these five documents is addressed to the
        // player, so the copy talks about paper the leads keep, never about
        // post the player was sent.
        // Six words or fewer (UI-EA-SPEC empty-state law), the same paper-
        // not-post wording, shorter.
        view.emptyLine = unlocked.empty() ? "NO PAPER YET. STAND OVER LEADS."
                                          : "STAND OVER MORE LEADS.";
        view.page = lettersPage_;
    }
    for (const std::int32_t index : unlocked) {
        const sim::Letter& letter = letterAt(index);
        // MAELL'S THREE SHARE ONE NAME, so the title list numbers them
        // against every OTHER letter tied to the same lead rather than
        // showing "FATHER MAELL" three times over with no way to tell
        // which press opens which. Combined indices (letterAt) -- lead ids
        // are per-file strings, so the count can never mix the two books.
        std::int32_t total = 0;
        std::int32_t position = 0;
        for (std::int32_t i = 0; i < letterCount(); ++i) {
            if (letterAt(i).lead == letter.lead) {
                ++total;
                if (i == index) {
                    position = total;
                }
            }
        }
        std::string label = letter.from.empty() ? std::string("A LETTER") : letter.from;
        if (total > 1) {
            label += " " + std::to_string(position) + "/" + std::to_string(total);
        }
        view.topics.push_back(label);
    }
    view.cursor = lettersCursor_;
    return view;
}

std::vector<std::string> Session::journalWorkRows() const {
    // WHAT THE PLAYER OWES AND WHAT THEY HAVE DONE, under the leads: every
    // LIVE contract off the same board the HUD's contractLine reads -- all of
    // them, where that corner row only ever shows the soonest -- and then
    // every finished stage's own authored log line, in the order they were
    // earned. Both are pure derived reads (the board and the QuestJournal are
    // already hashed sim state; this hashes nothing new), and both rows are
    // SOMETHING TO READ, not a choice -- picking one is the same honest no-op
    // a character-sheet row gives a number press.
    std::vector<std::string> rows;
    const sim::DialogueDirector& talk = tavern_->dialogue();
    const sim::ContractBoard& board = talk.contracts();
    for (const sim::Contract& row : board.contracts()) {
        if (!row.live()) {
            continue;
        }
        // The same have/want fraction contractLine() prints, so the journal
        // and the HUD corner can never disagree about the one job they both
        // show. The deadline rides the row in nights left rather than a raw
        // day index, because "DUE TONIGHT" is the fact a player acts on.
        const std::int32_t have = talk.crimes().stash().count(row.good);
        std::string line = "- " + row.label + " " + std::to_string(have) + "/" +
                           std::to_string(row.units);
        const std::int32_t nights = row.dueOnDay - board.day();
        if (nights <= 0) {
            line += "  DUE TONIGHT";
        } else if (nights == 1) {
            line += "  1 NIGHT";
        } else {
            line += "  " + std::to_string(nights) + " NIGHTS";
        }
        rows.push_back(std::move(line));
    }
    // RADIANT BUILD: the ward's own errands, under the broker's jobs. Every
    // TAKEN radiant objective, with the same have/want fraction a contract
    // row gets (a deliver has no goods to count, so its row names the two
    // ends of the walk instead), and NO deadline -- an errand has none, and
    // printing a fake one would be worse than printing the truth. The same
    // read-only contract as every row here: something to read, not a choice.
    for (const sim::RadiantObjective& row : talk.radiant().objectives()) {
        if (!row.live()) {
            continue;
        }
        std::string line = "- ";
        if (row.isFetch()) {
            const std::int32_t have = talk.crimes().stash().count(row.good);
            line += row.verb + " " + std::to_string(row.units) + " " +
                    std::string(sim::contrabandLabelFor(row.good, row.units)) + " " +
                    std::to_string(have) + "/" + std::to_string(row.units) + "  FOR " +
                    upperAscii(row.giverName);
        } else {
            line += row.verb + " TO " + upperAscii(row.targetName) + "  FROM " +
                    upperAscii(row.giverName);
        }
        rows.push_back(std::move(line));
    }
    // "* " is the journal's own "followed" mark -- a finished stage is a lead
    // that paid off, and it reads with the same glyph.
    for (const std::string& entry : talk.journal().log()) {
        rows.push_back("* " + entry);
    }
    return rows;
}

DialogueViewState Session::journalPanelView() const {
    // THE JOURNAL. Not a new panel and not a sheet: one more content shape on
    // the widget family every other tile already uses.
    // COURIER CASE. The tile shows the live errand while one runs -- the
    // identical active-case rule the composed page keeps; see
    // casebookPageState.
    const sim::CasebookRaws& raws = activeCaseRaws();
    const sim::Casebook& book = activeCasebook();
    DialogueViewState view;
    view.phase = static_cast<float>(body_->stepCount()) / 60.0F;
    view.open = true;
    view.speaker = "THE CASEBOOK";
    view.epithet = std::string(raws.title());
    // THE ATTITUDE FIELD IS SHORT BY CONSTRUCTION -- in a conversation it
    // holds "WARM" or "HOSTILE" -- and the top-right of that band is where
    // the HUD draws the clock over it. The first S10 capture put a
    // twenty-nine character dread band there and the clock landed in the
    // middle of it. The ward's nerve moved down onto the line, where it is
    // the first thing you read in your own notes, which is also better.
    view.attitude = book.closed() ? "CLOSED" : "OPEN";
    const std::vector<std::int32_t> heard = book.known();
    // MERGE FIX: `book` is the active casebook now (courier lane); the legend
    // -- what the ward calls you -- takes its own name.
    const sim::Legend called = legend();
    if (caseEntry_ >= 0 && static_cast<std::size_t>(caseEntry_) < heard.size()) {
        const std::int32_t leadIndex = heard[static_cast<std::size_t>(caseEntry_)];
        const sim::Lead& lead = raws.leads()[static_cast<std::size_t>(leadIndex)];
        const sim::LeadState what = book.state(leadIndex);
        view.line = what == sim::LeadState::Open ? lead.place + ". " + lead.what + "."
                                                  : lead.found + " " + lead.detail;
        // TASK #82. THE DATELINE, AND THE CROSS-REFERENCE -- what a
        // detective's log keeps that a bare topic list does not: when
        // this went in the book, what told you to come here, and (once
        // followed) what it put in the book next. See
        // DialogueViewState::caseRef's own header on why this is a
        // separate row rather than folded into `line`.
        std::string ref = formatCaseDay(book.heardAt(leadIndex));
        const std::vector<std::int32_t> from = raws.openedBy(leadIndex);
        if (from.empty()) {
            // THE ONE LEAD WITH NO OPENER. Not blank: a log that omits
            // the start of its own case reads as missing a page, not as
            // having none to show.
            ref += "  THE CASE OPENED HERE";
        } else {
            ref += "  FROM ";
            for (std::size_t i = 0; i < from.size(); ++i) {
                if (i > 0) {
                    ref += ", ";
                }
                const sim::Lead& opener = raws.leads()[static_cast<std::size_t>(from[i])];
                ref += opener.brief.empty() ? opener.place : opener.brief;
            }
        }
        if (what == sim::LeadState::Followed && !lead.opens.empty()) {
            ref += "  OPENED ";
            for (std::size_t i = 0; i < lead.opens.size(); ++i) {
                if (i > 0) {
                    ref += ", ";
                }
                const std::int32_t opened = raws.indexOf(lead.opens[i]);
                if (opened >= 0) {
                    const sim::Lead& next = raws.leads()[static_cast<std::size_t>(opened)];
                    ref += next.brief.empty() ? next.place : next.brief;
                }
            }
        } else if (what == sim::LeadState::Cold) {
            ref += "  DEAD END";
        }
        // A HINT AT THE LETTERS, when this exact lead unlocked one. The
        // player has already earned the right to read it -- see
        // unlockedLetters() -- so the casebook says where to press
        // rather than making them discover the key by accident.
        for (const std::int32_t li : unlockedLetters()) {
            if (letterAt(li).lead == lead.id) {
                ref += "  L READS HIS LETTERS";
                break;
            }
        }
        view.caseRef = ref;
    } else if (book.readCount() == 0) {
        // THE OPENING PAGE OF A NEW GAME: the hook, and nothing else. It is
        // the first thing a player ever reads in this game and it gets the
        // band to itself.
        view.line = std::string(raws.hook());
    } else {
        // And afterwards: what the ward's nerve is doing, and what it calls
        // you for the work so far. TWO SHORT SENTENCES, because the band
        // wraps to three lines and the S10 capture that ran to four lost
        // "OF THE FLAME" off the end of its own title.
        view.line = std::string(raws.dreadLabel(book.dread())) + ". THEY CALL YOU " +
                    std::string(called.title()) + ".";
    }
    for (const std::int32_t index : heard) {
        const sim::Lead& lead = raws.leads()[static_cast<std::size_t>(index)];
        std::string row;
        switch (book.state(index)) {
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
        // casebook.json carries a `short` for every lead and a case pins
        // that all of them fit.
        view.topics.push_back(row + (lead.brief.empty() ? lead.place : lead.brief));
    }
    // THE WORK, UNDER THE TRAIL: live contracts and the finished stages' own
    // log lines -- see journalWorkRows(). After the leads on purpose, so
    // every existing index into the list (caseEntry_, chooseTopic's own lead
    // guard) still means the lead it always meant.
    for (std::string& row : journalWorkRows()) {
        view.topics.push_back(std::move(row));
    }
    // THE WAITING SENTENCE IS RETIRED HERE TOO (UI-EA-SPEC 1.5, prose 14->0,
    // and the two-surfaces-one-wording rule cuts both ways): the casebook
    // page stopped saying it, so the tile stops with it. The stippled field
    // still says "left on purpose"; the hook on a fresh book still leads.
    view.cursor = caseCursor_;
    view.page = casePage_;
    return view;
}

CreationPage Session::stripCard() const {
    // UI-EA-SPEC 1.7 (LANE PAGES, strip->card): the pause stack's four strips
    // -- pause, wait, options, grimoire -- drawn as ONE composed card through
    // the same generic composition every creation step already uses
    // (drawCreationPage): framed, seated, measured, a numbered master list
    // with an inverted-fill cursor. The SHIP NOTE's standing item ("the
    // options page onto the master/detail card"), extended to the family.
    //
    // INPUT IS UNTOUCHED: rows, cursor and the nine-key digit windows keep
    // wrapCursorAndPage's own arithmetic, so every press lands where it
    // always did -- only the drawing changed register. The digits print on
    // the current nine-key window and on nothing else, the same honesty the
    // tiles and the casebook keep.
    CreationPage out;
    out.alpha = 1.0F;
    out.hasDetail = false;
    out.shape = CreationListShape::Columns;
    out.maxColumns = 1;
    out.stipple = false;
    const InputDevice dev = promptDevice_;
    const std::string back(promptBackKey(dev));

    std::vector<std::string> rows;
    int cursor = 0;
    int window = 0;
    if (pauseOpen_) {
        // THE BUILD STAMP RIDES THE PAUSE TITLE (UI-EA-SPEC sec. 2) -- the
        // HUD's own corner is quiet now, and the one page a player opens to
        // stand still is where a version belongs.
        out.title = "GRANADAD " + std::string(sim::build_info().version);
        rows = pauseRows();
        cursor = pauseCursor_;
        out.bodyHoldRows = static_cast<int>(rows.size());
    } else if (waitOpen_) {
        out.title = waitSleep_ ? "SLEEP" : "WAIT";
        // The flavour is four words (spec #38, 15 -> 4) -- or the live
        // refusal, which outranks it and is register, not chrome. Sleep keeps
        // its one honest clause: it is the only wait that mends.
        const std::string refusal = waitSleep_ ? std::string() : waitRefusal();
        if (!refusal.empty()) {
            out.instruction = refusal;
        } else {
            out.instruction = waitSleep_ ? "SLEEP MENDS. PICK THE HOUR." : "TIME PASSES. HEAT COOLS.";
        }
        rows = waitRows();
        cursor = waitCursor_;
        window = waitPage_;
        out.bodyHoldRows = 12;
    } else if (optionsOpen_) {
        // NINE AND THE STICKS: THE KEYS PAGE'S OWN TAB STRIP, so the sibling
        // pair reads as one surface from both sides -- CONTROLS, then KEYS
        // and OPTIONS as tabs with this one lit, exactly what drawKeysPage
        // prints over the other. The sub-tab key that swaps them is named
        // on the foot below.
        out.title = "CONTROLS";
        out.tabs = {PanelTab{"", "KEYS"}, PanelTab{"", "OPTIONS"}};
        out.currentTab = 1;
        // Instruction 22 -> 4 (spec #37): the value hint, or the capture
        // state's own modal line, which is load-bearing while the game is
        // listening for a raw key -- and TRUE on both hands: ESC cancels on
        // a keyboard, B (and START) on a pad, and none of the three can be
        // captured into a verb (main.cpp's pressed() drops them).
        if (awaitingKey_) {
            out.instruction = dev == InputDevice::Pad ? "PRESS A KEY. B CANCELS."
                                                      : "PRESS A KEY. ESC CANCELS.";
        } else {
            out.instruction = "REBIND STEALS THE KEY.";
        }
        rows = optionRows();
        cursor = optionCursor_;
        window = optionPage_;
        out.bodyHoldRows = 14;
    } else if (searchOpen()) {
        // KIT BUILD: the corpse's kit as the composed card.
        const sim::Actor* corpse = tavern_->actorById(searchActorId_);
        out.title = corpse != nullptr ? upperAscii(corpse->name()) : std::string("THE DEAD");
        rows = grimoireRows();
        out.instruction = rows.empty() ? "NOTHING ON HIM." : "DR - DRAMS.";
        cursor = grimoireCursor_;
        window = grimoirePage_;
    } else if (grimoireOpen_) {
        out.title = "GRIMOIRE";
        rows = grimoireRows();
        // Instruction: the one column that needs naming, and the one verb
        // this page has that no other list has -- LEFT/RIGHT binds a slot --
        // named in the live hand's keycaps. The empty state is the cast
        // refusal's OWN words -- the page and the C key must name the same
        // door or one of them is lying (test_tavern pins the line), so the
        // register literal outranks the six-word rule here.
        const std::string sideways = dev == InputDevice::Pad ? std::string("\x06\x04\x05")
                                                              : std::string("\x04\x05");
        out.instruction = rows.empty() ? "NO CRAFTING HELD. THE PRIEST OF THE FLAME TEACHES."
                                       : "D IS THE ASK. " + sideways + " BINDS A SLOT.";
        cursor = grimoireCursor_;
        window = grimoirePage_;
        out.bodyHoldRows = 8;
    }

    // THE CARD PAGES AT TWELVE (UI-EA-SPEC 1.7: the wait clock over two
    // pages, the options table likewise) -- a screenful anchored on the
    // cursor, `+N` as the last, unselectable row saying what waits past the
    // fold, and the digit keys printed only on the nine-key window the
    // direct-select arithmetic really answers to.
    constexpr int kCardRows = 12;
    const int total = static_cast<int>(rows.size());
    const int screen = total > kCardRows ? std::clamp(cursor, 0, total - 1) / kCardRows : 0;
    const int first = screen * kCardRows;
    const int last = std::min(total, first + kCardRows);
    out.rows.reserve(static_cast<std::size_t>(last - first) + 1);
    const int windowFirst = window * kTopicPageSize;
    // The wait rows number THEMSELVES (`7 - DAWN 06:00`), so a printed key
    // column would say every digit twice -- digits pick what they print, and
    // those rows already print them.
    // NINE AND THE STICKS: no digit column with a pad in hand -- a pad has
    // no number row, so a printed `1` beside RESUME is a key nothing on the
    // pad answers to. The wait rows keep their number: it is the hours
    // ahead, information, not a key.
    const bool selfNumbered = waitOpen_;
    const bool digits = dev != InputDevice::Pad;
    for (int i = first; i < last; ++i) {
        CreationPageRow row;
        const int slot = i - windowFirst;
        if (digits && !selfNumbered && slot >= 0 && slot < kTopicPageSize) {
            row.key = std::to_string(slot + 1);
        }
        row.label = rows[static_cast<std::size_t>(i)];
        row.accent = panelInk().accent;
        out.rows.push_back(std::move(row));
    }
    if (last < total) {
        CreationPageRow more;
        more.label = "+" + std::to_string(total - last);
        more.accent = panelInk().dim;
        more.selectable = false;
        out.rows.push_back(std::move(more));
    }
    out.cursor = std::clamp(cursor, 0, std::max(0, total - 1)) - first;
    // THE FOOT: the page's own keys, then BACK. Bare keycaps at rest; the
    // words ride the tutor tier, which the pause stack leaves at rest --
    // these pages ARE their rows. NINE AND THE STICKS: the grimoire is a
    // page of NOTES, so its foot carries the page keys that walk on to the
    // sheet (or back to the ward map); the options page names the sub-tab
    // key that swaps to KEYS and the sideways key that changes a value.
    const auto footKey = [&out](std::string key, const char* label) {
        PanelOption foot;
        foot.key = std::move(key);
        foot.label = label;
        foot.valueInk = InkRole::Dim;
        foot.selectable = false;
        out.nav.push_back(std::move(foot));
    };
    if (grimoireOpen_) {
        footKey(std::string(promptPageKeys(dev)), "NOTES");
        footKey(std::string(promptConfirmKey(dev)), "READY");
    } else if (optionsOpen_) {
        footKey(std::string(promptTabKeys(dev)), "KEYS");
        footKey(dev == InputDevice::Pad ? std::string("\x06\x04\x05") : std::string("\x04\x05"),
                "CHANGE");
        footKey(std::string(promptConfirmKey(dev)), "REBIND");
    }
    footKey(back, "BACK");
    return out;
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

// ---------------------------------------------------------------------------
// FAST TRAVEL: the cost of a walk, in the sim's own integers
// ---------------------------------------------------------------------------

std::int32_t travelRouteUnits(const sim::PathStep& from,
                              const std::vector<sim::PathStep>& route) noexcept {
    // The router's own octile currency, recomputed over the route it returned:
    // 10 per orthogonal step, 14 per diagonal (path_finder.hpp's kStepCost
    // pair, restated in test_travel.cpp's own pins). A walk-gait band change
    // -- a stair -- rides its step at no surcharge, exactly as PathFinder
    // charges it under Gait::Walk.
    std::int32_t units = 0;
    const sim::PathStep* at = &from;
    for (const sim::PathStep& step : route) {
        const std::int32_t dx = step.x > at->x ? step.x - at->x : at->x - step.x;
        const std::int32_t dy = step.y > at->y ? step.y - at->y : at->y - step.y;
        units += (dx != 0 && dy != 0) ? 14 : 10;
        at = &step;
    }
    return units;
}

std::int32_t travelWalkSeconds(std::int32_t units) noexcept {
    if (units <= 0) {
        return 0;
    }
    // One octile unit is a tenth of a tile: 256 Q8 / 10. The body walks
    // kWalkSpeed Q8 per movement step, kStepsPerSecond steps a second, so
    // seconds = units * 256 / (10 * kWalkSpeed * kStepsPerSecond), rounded UP
    // -- a walk is never free and never rounds itself shorter.
    const std::int64_t num = static_cast<std::int64_t>(units) * 256;
    const std::int64_t den =
        10LL * sim::kWalkSpeed * sim::kStepsPerSecond;
    return static_cast<std::int32_t>((num + den - 1) / den);
}

std::int32_t travelClockMinutes(std::int32_t seconds) noexcept {
    // NEVER FREE (the owner's ruling, item 1): the floor is one whole minute,
    // and the verb's restated minutes are exactly the minutes delivered.
    if (seconds <= 60) {
        return 1;
    }
    return (seconds + 59) / 60;
}

std::string travelCostLabel(std::int32_t minutes) {
    if (minutes >= 60) {
        return "ABOUT AN HOUR";
    }
    return std::to_string(minutes) + " MIN";
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

namespace {

// --- #82: the district map --------------------------------------------------

/// The job-family prefix content/raws/factions/factions.json's own
/// memberJobs speaks, for a notable's authored TYPE
/// (content/raws/names/notables.json). Four of the Forty's seven authored
/// types have no family at all here, and that is not a gap this table failed
/// to cover: a wastrel answers to nobody, which is factions.json's own
/// documented rule ("wastrel.streetlife ... deliberately unaffiliated"), and
/// the same silence is the honest answer for anything else this build has
/// not met yet.
[[nodiscard]] std::string_view notableJobFamily(std::string_view type) noexcept {
    if (type == "shopkeeper") {
        return "trade";
    }
    if (type == "militia_watch") {
        return "watch";
    }
    if (type == "priest_of_the_flame" || type == "disciple_of_the_flame") {
        return "clergy";
    }
    if (type == "serf") {
        return "serf";
    }
    if (type == "animal_keeper") {
        return "husbandry";
    }
    return {};
}

/// "NW 31T", or "HERE" standing on it -- the eight-point word and the
/// straight-line tile range from one point to another. This is the whole
/// reason the district map exists over the casebook's own list: a lead there
/// is a NAME; here it is a DIRECTION from where the player is actually
/// standing.
///
/// INTEGER THROUGHOUT, deliberately. sim::bearingTo is the identical call
/// PlayerBody's own stealth arc is judged by (sim/stealth.hpp) -- not a fresh
/// trig call invented for a menu -- and the range is Chebyshev, the tile
/// count a body that can step diagonally actually walks to close it, not a
/// ruler laid across the map.
[[nodiscard]] std::string bearingLabel(std::int32_t fromX, std::int32_t fromY, std::int32_t toX,
                                       std::int32_t toY) {
    const std::int32_t dx = toX - fromX;
    const std::int32_t dy = toY - fromY;
    if (dx == 0 && dy == 0) {
        return "HERE";
    }
    const std::int32_t ax = dx < 0 ? -dx : dx;
    const std::int32_t ay = dy < 0 ? -dy : dy;
    const std::int32_t tiles = ax > ay ? ax : ay;
    return std::string(sim::compass_point(sim::bearingTo(fromX, fromY, toX, toY))) + " " +
           std::to_string(tiles) + "T";
}

}  // namespace

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

namespace {

/// KITFIX LANE. THE ONE PLACE A NUMBER GETS PADDED. A capture of the Kit
/// tile (docs/frames/kit/) found the DR column starting at four different x
/// and the C column at four more -- KNIFE's 8DR sits three cells left of
/// LANTERN's 30DR because each row's number followed straight off ITS OWN
/// name, and a name is not a column. Every numeric suffix on the sheet is
/// supposed to compose through here instead of a caller hand-padding a
/// std::to_string in place: right-justified into a field exactly as wide as
/// the widest value the CALLER'S OWN LIST can show, so a five-row page never
/// reserves a sixth row's width and a hundred-drams row never crowds a
/// single-digit one.
[[nodiscard]] std::string rightAlignedDigits(std::int32_t value, std::size_t width) {
    std::string digits = std::to_string(value);
    if (digits.size() < width) {
        digits.insert(digits.begin(), width - digits.size(), ' ');
    }
    return digits;
}

}  // namespace

std::vector<std::string> Session::characterRows() const {
    // FIVE ROWS, THEN FOUR, THEN FIVE, THEN THREE: the five Legend tracks
    // (S8's own "who am I in this city yet", derived and thrown away every
    // frame until this page existed -- see the header), the four skills a
    // verb in this build actually levels, the five faction ladders, and what
    // the ward and the purse currently say. The tracks and skills fill page
    // one exactly -- see kTopicPageSize -- and the ladders open page two, so
    // neither block is ever split by a page turn a player has to go looking
    // for.
    std::vector<std::string> rows;
    // The five tracks, up to twenty skills, four attributes, five ladders,
    // the three always-printed rows and the IN HAND row the armed sheet adds.
    rows.reserve(sim::kLegendTracks + 20 + sim::kAttributeCount + 5 + 4);
    const sim::Legend book = legend();
    for (std::size_t i = 0; i < sim::kLegendTracks; ++i) {
        const sim::LegendRow& row = book.rows()[i];
        std::string_view name = sim::legendTrackName(row.track);
        // EVERY TRACK NAME STARTS "THE ", legend.hpp's own table, and five
        // copies of the one word they share is four words spent out of an
        // eighteen-glyph column for nothing. Dropped on the way to the screen
        // only -- legendTrackName() still returns the full "THE WIRE"
        // everywhere else that reads it, because this is a display choice and
        // not a rename.
        if (name.substr(0, 4) == "THE ") {
            name.remove_prefix(4);
        }
        std::string line = std::string(name) + " - " + std::string(row.title);
        // WHAT THE NEXT RUNG WANTS, kept at last. legend.hpp's own header has
        // promised since S8 that "the panel prints what the next one wants in
        // the same units the player already sees" -- LegendRow::nextAt has
        // carried the number all along and no panel ever printed it. Score
        // over threshold, the notation every counted stage already uses; a
        // topped-out track shows its score alone, because a target it has
        // passed forever would read as work still owed.
        line += "  " + std::to_string(row.score);
        if (row.nextAt > 0) {
            line += "/" + std::to_string(row.nextAt);
        }
        rows.push_back(std::move(line));
    }
    const sim::DialogueDirector& talk = tavern_->dialogue();
    // THE HONEST SHEET (THE PULL PACK). The rows come off the SkillTrack
    // itself, not a hand-typed table of four: every skill the track has
    // actually MOVED -- a level above zero, or uses banked toward one --
    // plus the four this build has always shown (the ones a chargen sheet
    // designates and a stranger expects to find), in the raws' own id order.
    // The old constexpr listed four while the combat build was already
    // training shieldwall on a held guard and the scalp skill on a take, so
    // two lived skills never printed; and the sixteen a verb has never
    // touched still stay off the sheet, because a wall of LV 0 would claim
    // the game is watching a skill it is not.
    //
    // "LV 7  NEXT 12": what the next level costs, off the track's OWN
    // scaledUsesForLevel -- aptitude and the difficulty dagger composed
    // exactly as use() charges them -- less what is banked, so the number
    // is the uses still owed and never a second copy of the arithmetic.
    static constexpr std::string_view kAlwaysShown[] = {
        sim::kRoofSkill,
        sim::kThieverySkill,
        sim::kHaggleSkill,
        sim::kCraftingSkill,
    };
    for (const sim::SkillTrack::Entry& skill : talk.skills().entries()) {
        const bool always = std::find(std::begin(kAlwaysShown), std::end(kAlwaysShown),
                                      std::string_view{skill.id}) != std::end(kAlwaysShown);
        if (!always && skill.level <= 0 && skill.uses <= 0) {
            continue;
        }
        // MORROWIND ROUND: ONE SPACE, NOT TWO -- the Character tile's own
        // column is narrower than the old full-width panel's, and at 320x180
        // every glyph matters; CRACKSMANSHIP still cuts there and clipLabel()
        // marks the cut (the contract every clipped row keeps).
        std::string line = upperAscii(skill.displayName.empty() ? skill.id : skill.displayName) +
                           " LV " + std::to_string(skill.level);
        // "3 TO NEXT" -- uses, not standing, so it cannot be read as the
        // ladders' own NEXT price -- and only once the skill has MOVED: a
        // fresh sheet's four bare LV 0 rows stay bare (and stay whole in the
        // narrowest tile), and the cost appears the moment there is a count
        // to set it against.
        if (skill.level < 100 && (skill.level > 0 || skill.uses > 0)) {
            const std::int32_t owed =
                std::max(1, talk.skills().scaledUsesForLevel(skill.id, skill.level) - skill.uses);
            line += "  " + std::to_string(owed) + " TO NEXT";
        }
        rows.push_back(std::move(line));
    }
    // MGT / AGI / VIG / WIT, with the held delta beside the base: the
    // effective sheet (base plus every held tuning, clamped +/-2 -- what the
    // AGI gait and the punch actually read) against the chargen base, so a
    // STEADY THE HAND on the HUD's effect rows is a "(+2)" here and never a
    // number that silently disagrees with the one the world is using.
    const sim::AttributeBlock& base = tavern_->playerAttributes();
    const sim::AttributeBlock live = tavern_->effectiveAttributes();
    for (std::size_t i = 0; i < sim::kAttributeCount; ++i) {
        const sim::AttributeId attribute = static_cast<sim::AttributeId>(i);
        std::string line = std::string(sim::attributeAbbrev(attribute)) + " " +
                           std::to_string(live.value(attribute));
        const std::int32_t delta = live.value(attribute) - base.value(attribute);
        if (delta != 0) {
            line += " (" + std::string(delta > 0 ? "+" : "") + std::to_string(delta) + ")";
        }
        rows.push_back(std::move(line));
    }
    // THE FIVE LADDERS, WITH THE NUMBERS ON. Owner ruling for this build: the
    // player's OWN sheet shows rank title, the standing number and what the
    // next rung costs -- the word-only ruling still governs how NPCs and the
    // ward-reputation lines TALK about you, and nothing there changed. Every
    // figure is read out of FactionLedger and the raws' own ladder: the rung
    // nextRung() answers with is the exact rung join()/advance() will measure
    // (checkRung's), so the sheet can never promise a price the ladder does
    // not charge. Cost notation is standing, then "/LV n" when the rung is
    // also measured in the ladder's own skill -- the same LV the four skill
    // rows above already taught. INFORMATION, NEVER A DISCOUNT: nothing here
    // moves a number, it only stops the climb being a slot machine.
    const sim::FactionLedger& standings = talk.standings();
    if (const sim::FactionRegistry* guilds = standings.registry(); guilds != nullptr) {
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(standings.size()); ++i) {
            const sim::Faction* who = guilds->at(i);
            if (who == nullptr) {
                continue;
            }
            // The id, not the display name: "TEMPLE OF THE FLAME" would spend
            // the whole column on its own name, and the map tile's "who will
            // talk" list already prints these same ids -- one vocabulary.
            std::string line = upperAscii(who->id);
            if (const std::string_view title = standings.rankTitle(i); !title.empty()) {
                line += " " + upperAscii(title);
            }
            line += " " + std::to_string(standings.standing(i));
            if (const sim::FactionRank* next = standings.nextRung(i); next != nullptr) {
                // JOIN for an outsider, NEXT for a member -- the first rung is
                // earned exactly like every later one (faction.hpp's own rule),
                // so both read the same rung the ledger will actually check.
                line += standings.isMember(i) ? "  NEXT " : "  JOIN ";
                line += std::to_string(next->standing);
                if (next->skillLevel > 0) {
                    line += "/LV" + std::to_string(next->skillLevel);
                }
            } else if (standings.rank(i) > 0) {
                // On a ladder with no rung above you. Said plainly rather than
                // left blank, so "nothing after the number" always means "no
                // ladder" and never "top" -- two facts, two spellings.
                line += "  TOP";
            }
            rows.push_back(std::move(line));
        }
    }
    // ABSENCE COSTS NOTHING ON THE HUD; IT COSTS NOTHING HERE EITHER, but for
    // the opposite reason. The HUD drops a row that has nothing to say because
    // it is read every frame; a sheet a player opened ON PURPOSE to look
    // themselves up is the one place a zero is worth printing, so REPUTATION,
    // COIN and HEAT are always here, not just when they are interesting.
    rows.push_back("REPUTATION  " + std::string(talk.ledger().reputationLabel()));
    rows.push_back("COIN  " + std::to_string(tavern_->playerCoin()));
    rows.push_back("HEAT  " + std::to_string(talk.crimes().heat()));
    // IN HAND -- the reference sheet's w slot ("cane 1-6 Impact"). KIT
    // BUILD (D10): ALWAYS printed now, because fists are a real state once
    // an equipment model exists -- the sheet says what the hand holds, the
    // item by name when a Kit row is worn there, the class line when a
    // sheet armed the hand directly, FISTS 3-5 IMPACT otherwise.
    if (const sim::ItemDef* held = tavern_->heldItem(); held != nullptr) {
        rows.push_back("IN HAND  " + sim::itemSheetLine(*held));
    } else {
        rows.push_back("IN HAND  " + sim::weaponSheetLine(tavern_->playerWeapon()));
    }
    // THE WORN SLOTS, printed ONLY for a slot the raws hold at least one
    // item for (D10's own rule against furniture): the thing worn there by
    // name, or NOTHING -- the reference's "no trinket" wording.
    const sim::ItemRegistry& items = tavern_->items();
    for (std::int32_t slotIndex = 0; slotIndex < static_cast<std::int32_t>(sim::kWornSlotCount);
         ++slotIndex) {
        const sim::ItemSlot slot = sim::wornSlotAt(slotIndex);
        if (slot == sim::ItemSlot::Hand || !items.slotHasItems(slot)) {
            continue;
        }
        const sim::ItemDef* worn = items.at(tavern_->kit().worn(slot));
        std::string line = std::string(sim::itemSlotSheetLabel(slot)) + "  ";
        if (worn != nullptr) {
            line += worn->name;
            if (worn->dr > 0) {
                line += "  DR " + std::to_string(worn->dr);
            }
        } else {
            line += "NOTHING";
        }
        rows.push_back(std::move(line));
    }
    // THE LOAD, the one visible budget, then every carried row.
    rows.push_back(loadLine());
    // KITFIX LANE. THE LIST READ TWICE: once to learn what every row's
    // prefix, drams and worth actually are (and how wide the widest of
    // each runs), once to compose the lines -- rightAlignedDigits()'s own
    // header on why a row cannot pad itself against a list it cannot see.
    // Copied out of kitRows() the one time rather than called twice, which
    // would ask the registry the same question under two different lists if
    // anything about it were ever to change between calls.
    const std::vector<KitRow> kit = kitRows();
    struct KitLine {
        const KitRow* row = nullptr;
        const sim::ItemDef* thing = nullptr;
        std::string prefix;
        std::int32_t drams = 0;
        std::int32_t royals = 0;
    };
    std::vector<KitLine> lines;
    lines.reserve(kit.size());
    std::size_t prefixWidth = 0;
    std::size_t dramsWidth = 0;
    std::size_t royalsWidth = 0;
    for (const KitRow& row : kit) {
        const sim::ItemDef* thing = items.at(row.item);
        if (thing == nullptr) {
            continue;
        }
        KitLine entry;
        entry.row = &row;
        entry.thing = thing;
        if (row.count > 1) {
            entry.prefix = std::to_string(row.count) + " ";
        }
        entry.prefix += thing->name;
        // Weight and worth, the reference's right-aligned numbers, in the
        // font's own letters (no icons): DR is drams, C is coin.
        entry.drams = row.item == tavern_->items().indexOf("bale")
                          ? tavern_->dialogue().crimes().baleUnits() *
                                sim::contrabandWeight(tavern_->dialogue().crimes().baleGood())
                          : thing->drams * row.count;
        entry.royals = thing->royals > 0 ? thing->royals * row.count : 0;
        prefixWidth = std::max(prefixWidth, entry.prefix.size());
        dramsWidth = std::max(dramsWidth, std::to_string(entry.drams).size());
        if (entry.royals > 0) {
            royalsWidth = std::max(royalsWidth, std::to_string(entry.royals).size());
        }
        lines.push_back(std::move(entry));
    }
    for (const KitLine& entry : lines) {
        const KitRow& row = *entry.row;
        const sim::ItemDef& thing = *entry.thing;
        // The name column PADDED to the widest name this page holds, so
        // every row's "  NNDR" starts under the last one -- the fix itself,
        // everything above just measured for it.
        std::string line = entry.prefix;
        line.append(prefixWidth - entry.prefix.size(), ' ');
        line += "  " + rightAlignedDigits(entry.drams, dramsWidth) + "DR";
        if (entry.royals > 0) {
            line += "  " + rightAlignedDigits(entry.royals, royalsWidth) + "C";
        }
        if (row.inKit && tavern_->kit().isWorn(row.item)) {
            // HELD, not IN HAND (the cells pass): at the register's floor of
            // 85 cells the Character tile is 27 wide, and `IN HAND` was the
            // word the row lost -- `KNIFE   8DR   4C  IN..` on every common
            // monitor. HELD and WORN are a pair. The SLOT tag below is the
            // column that falls off there, whole (panel.hpp, clipColumns),
            // and the strip says which slot.
            line += thing.slot == sim::ItemSlot::Hand ? "  HELD" : "  WORN";
        }
        for (std::int32_t slot = 0; slot < sim::Tavern::kQuickSlotCount; ++slot) {
            if (tavern_->slotItemIndex(slot) == row.item) {
                line += "  SLOT " + std::to_string(slot + 1);
                break;
            }
        }
        if (thing.heat > 0) {
            line += "  HOT";
        }
        rows.push_back(std::move(line));
    }
    return rows;
}

// ---------------------------------------------------------------------------
// KIT BUILD: the Kit on the Character tile, and the search list
// ---------------------------------------------------------------------------

std::vector<Session::KitRow> Session::kitRows() const {
    // ONE LIST OUT OF THREE STORES, in the registry's document order: the
    // Kit's own count, or the sack's for a contraband row, or the bale, or
    // the picks. Rows with nothing on them are not rows.
    std::vector<KitRow> rows;
    const sim::ItemRegistry& items = tavern_->items();
    const sim::CrimeLedger& crimes = tavern_->dialogue().crimes();
    const std::int32_t picks = items.indexOf("picks");
    const std::int32_t bale = items.indexOf("bale");
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(items.size()); ++i) {
        const sim::ItemDef& thing = items.items()[static_cast<std::size_t>(i)];
        KitRow row;
        row.item = i;
        if (!thing.contraband.empty()) {
            sim::Contraband good = sim::Contraband::Scalp;
            if (sim::contrabandFromSymbol(thing.contraband, good)) {
                row.count = crimes.stash().count(good);
            }
        } else if (i == picks) {
            row.count = tavern_->picks();
        } else if (i == bale) {
            row.count = crimes.carryingBale() ? 1 : 0;
        } else {
            row.count = tavern_->kit().count(i);
            row.inKit = row.count > 0;
        }
        if (row.count > 0) {
            rows.push_back(row);
        }
    }
    return rows;
}

std::size_t Session::characterKitOffset() const {
    // The standings block (five tracks, four skills, four attributes, five
    // ladders, three ward rows), IN HAND, one row per worn slot the raws
    // populate, LOAD. MERGE FIX: this fell out of step with characterRows()
    // when THE HONEST SHEET (pull pack) inserted the four attribute rows --
    // nothing here counted them, so every cursor-vs-offset comparison in
    // highlightedKitRow() was off by kAttributeCount the moment both landed
    // on the same tree.
    std::size_t offset = sim::kLegendTracks + 4 + sim::kAttributeCount + 5 + 3 + 1 + 1;
    for (std::int32_t slotIndex = 0; slotIndex < static_cast<std::int32_t>(sim::kWornSlotCount);
         ++slotIndex) {
        const sim::ItemSlot slot = sim::wornSlotAt(slotIndex);
        if (slot != sim::ItemSlot::Hand && tavern_->items().slotHasItems(slot)) {
            ++offset;
        }
    }
    return offset;
}

std::optional<Session::KitRow> Session::highlightedKitRow() const {
    const std::size_t offset = characterKitOffset();
    if (characterCursor_ < 0 || static_cast<std::size_t>(characterCursor_) < offset) {
        return std::nullopt;
    }
    const std::vector<KitRow> rows = kitRows();
    const std::size_t at = static_cast<std::size_t>(characterCursor_) - offset;
    if (at >= rows.size()) {
        return std::nullopt;
    }
    return rows[at];
}

std::string Session::loadLine() const {
    return "LOAD  " + std::to_string(tavern_->loadDrams()) + " / " +
           std::to_string(tavern_->loadBudget()) + " DRAMS";
}

void Session::wearHighlightedKitRow() {
    const std::optional<KitRow> row = highlightedKitRow();
    if (!row.has_value()) {
        return;
    }
    const sim::ItemDef* thing = tavern_->items().at(row->item);
    if (thing == nullptr) {
        return;
    }
    if (!row->inKit) {
        // The sack, the bale, the picks: not worn, and the room says so in
        // the words wearItem would.
        if (audio_ != nullptr) {
            audio_->playOneShot(audio::SoundId::UiTick);
        }
        say("THE " + thing->name + " IS NOT WORN.");
        return;
    }
    const bool wasWorn = tavern_->kit().isWorn(row->item);
    const sim::Tavern::StealResult done = tavern_->wearItem(row->item);
    if (audio_ != nullptr) {
        if (done.result != sim::ServiceResult::Served) {
            audio_->playOneShot(audio::SoundId::UiTick);
        } else if (thing->slot == sim::ItemSlot::Hand) {
            // A blade leaves its sheath; a cudgel is picked up off the table.
            audio_->playOneShot(thing->weaponClass >= sim::kFirstLethalWeapon
                                    ? audio::SoundId::KnifeDraw
                                    : audio::SoundId::MetalClick);
        } else {
            audio_->playOneShot(audio::SoundId::ClothRustle);
        }
    }
    (void)wasWorn;
    say(done.line);
}

void Session::dropHighlightedKitRow() {
    const std::optional<KitRow> row = highlightedKitRow();
    if (!row.has_value()) {
        return;
    }
    syncTavernToBody();
    const sim::Tavern::StealResult done = tavern_->dropItem(row->item);
    if (audio_ != nullptr) {
        audio_->playOneShot(done.result == sim::ServiceResult::Served
                                ? audio::SoundId::ThudMedium
                                : audio::SoundId::UiTick);
    }
    say(done.line);
    // The cursor stays on the row; a row that emptied is gone from the
    // list, so keep the cursor inside what is left.
    const int count = static_cast<int>(characterRows().size());
    if (count > 0 && characterCursor_ >= count) {
        characterCursor_ = count - 1;
        characterPage_ = topicPageOf(characterCursor_);
    }
}

void Session::adjustKitSlot(int delta) {
    if (delta == 0) {
        return;
    }
    const std::optional<KitRow> row = highlightedKitRow();
    if (!row.has_value() || !row->inKit) {
        return;
    }
    const sim::ItemDef* thing = tavern_->items().at(row->item);
    if (thing == nullptr) {
        return;
    }
    if (thing->slot == sim::ItemSlot::None) {
        if (audio_ != nullptr) {
            audio_->playOneShot(audio::SoundId::UiTick);
        }
        say("THE " + thing->name + " IS NOT WORN. NO SLOT.");
        return;
    }
    // Which slot holds it now, or -1 -- the Grimoire's own cycle: NONE,
    // 1..10 and round. Moving it clears the slot it leaves.
    std::int32_t current = -1;
    for (std::int32_t slot = 0; slot < sim::Tavern::kQuickSlotCount; ++slot) {
        if (tavern_->slotItemIndex(slot) == row->item) {
            current = slot;
            break;
        }
    }
    const std::int32_t positions = sim::Tavern::kQuickSlotCount + 1;
    const std::int32_t next = ((current + 1 + delta) % positions + positions) % positions - 1;
    if (current >= 0) {
        tavern_->clearSlot(current);
    }
    if (next >= 0) {
        tavern_->bindItemToSlot(next, row->item);
    }
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::UiTick);
    }
    say(next >= 0 ? thing->name + " -- SLOT " + std::to_string(next + 1) + "."
                  : thing->name + " -- NO SLOT.");
}

bool Session::takeNearestItem() {
    // The thing the reticle is on, the one the prompt named -- not the
    // nearest in reach whatever the eye is on.
    const std::int32_t at = groundItemUnderReticle();
    if (at < 0) {
        return false;
    }
    const sim::Tavern::StealResult took = tavern_->takeGroundItem(at);
    if (took.result == sim::ServiceResult::TooFar) {
        return false;
    }
    if (audio_ != nullptr) {
        // A pickup click; a refusal ticks. The sack's own CoinHandle stays
        // the purse's.
        audio_->playOneShot(took.result == sim::ServiceResult::Served
                                ? audio::SoundId::UiSelect
                                : audio::SoundId::UiTick);
    }
    say(took.line);
    return true;
}

bool Session::searchNearestCorpse() {
    if (refusedInCustody() || talking() || picking()) {
        return false;
    }
    syncTavernToBody();
    const sim::Actor* corpse = corpseToSearch();
    if (corpse == nullptr) {
        return false;
    }
    // The Grimoire widget, wearing the search list: every other overlay
    // stands down exactly as toggleGrimoire does, and the id says which
    // list it is.
    casebookOpen_ = false;
    keysOpen_ = false;
    waitOpen_ = false;
    districtMapOpen_ = false;
    optionsOpen_ = false;
    pauseOpen_ = false;
    quitArmed_ = false;
    awaitingKey_ = false;
    menuFocus_ = kMenuFocusJournal;
    grimoireOpen_ = true;
    searchActorId_ = corpse->id();
    grimoireCursor_ = 0;
    grimoirePage_ = 0;
    syncPanelAnim();
    return true;
}

std::vector<std::string> Session::mapRows() const {
    // THREE SECTIONS, EACH BUILT FROM WHAT THE CASEBOOK ALREADY KNOWS -- see
    // toggleMap's own header on why nothing here is new state. All three grow
    // by the identical act: hearing a lead. There is no separate "have you
    // been here" or "have you met them" flag to invent, forget to update, or
    // let drift from the trail itself.
    std::vector<std::string> rows;
    // COURIER CASE. The map's three sections follow the live errand, the one
    // active-case rule again -- see casebookPageState.
    const sim::CasebookRaws& raws = activeCaseRaws();
    const sim::Casebook& book = activeCasebook();
    if (!book.active()) {
        return rows;
    }
    const std::vector<std::int32_t> heard = book.known();
    const std::int32_t px = body_->tileX();
    const std::int32_t py = body_->tileY();

    // -- known ground: the named places a heard lead has put in the book,
    // once each, in the order they were first heard of. The casebook's own
    // `place` field, authored, never derived -- these are the district's
    // named sites and the street table (docks::kPlaces) only knows the
    // streets between them.
    std::vector<std::string_view> places;
    for (const std::int32_t index : heard) {
        const std::string_view place = raws.leads()[static_cast<std::size_t>(index)].place;
        if (std::find(places.begin(), places.end(), place) == places.end()) {
            places.push_back(place);
        }
    }
    for (const std::string_view place : places) {
        // "- ", not "@ " -- the 4x6 font carries no glyph for '@' (hud.cpp's
        // own table), and a hole punched through a place name by a symbol
        // nobody chose to support is exactly the class of bug the standing
        // quality bar exists to catch. See test_map.cpp's own copy case.
        rows.push_back("- " + std::string(place));
    }

    // -- open leads: heard, not yet looked at, each with a bearing from HERE.
    // The one thing this page has that the casebook's own list does not: the
    // casebook prints WHAT you have heard, unplaced; this prints WHERE it is
    // from where you are standing right now.
    for (const std::int32_t index : heard) {
        if (book.state(index) != sim::LeadState::Open) {
            continue;
        }
        const sim::Lead& lead = raws.leads()[static_cast<std::size_t>(index)];
        rows.push_back(bearingLabel(px, py, lead.site.x, lead.site.y) + "  " +
                       (lead.brief.empty() ? lead.place : lead.brief));
    }

    // -- who will talk: every named person a heard lead has pointed you at,
    // once each, who actually answers to somebody -- a wastrel answers to
    // nobody, which is factions.json's own rule (see notableJobFamily) and
    // not a gap in this list.
    const sim::NotableRegistry& notables = tavern_->dialogue().notables();
    const sim::FactionRegistry& factions = tavern_->dialogue().factions();
    std::vector<std::string_view> named;
    for (const std::int32_t index : heard) {
        const std::string_view who = raws.leads()[static_cast<std::size_t>(index)].who;
        if (who.empty() || std::find(named.begin(), named.end(), who) != named.end()) {
            continue;
        }
        named.push_back(who);
        const sim::Notable* notable = notables.find(who);
        if (notable == nullptr) {
            continue;
        }
        const sim::Faction* faction =
            factions.at(factions.factionForJobPrefix(notableJobFamily(notable->type)));
        if (faction == nullptr) {
            continue;
        }
        rows.push_back("! " + notable->name + "  " + upperAscii(faction->id));
    }
    return rows;
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
