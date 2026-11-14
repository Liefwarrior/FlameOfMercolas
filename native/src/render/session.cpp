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

namespace {

/// Puts `cursor` on the row this number key names, if that row is actually on
/// the page showing -- chooseVisibleTopic()'s own shape. Out of range is a
/// no-op, the same as every list on this surface gives a number with nothing
/// under it.
void pickCursorIfVisible(int& cursor, int page, int slot, std::size_t total) noexcept {
    const int index = page * kTopicPageSize + slot;
    if (index < static_cast<int>(total)) {
        cursor = index;
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

std::string Session::roomLine() const {
    // HARDENING PASS. Pulled out of drawFrame()'s own inline block so
    // syncPanelAnim() can call the identical logic instead of a second copy
    // -- see that method's own header. Bottom-right, and only when there is
    // a room to describe.
    if (!tavern_->playerInside()) {
        return {};
    }
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
    return line.str();
}

std::string Session::stealthLine() const {
    const sim::Notice worst = tavern_->worstNotice();
    const std::int32_t light = tavern_->lightOnPlayer();
    const std::int32_t noise = tavern_->playerNoise();
    std::string line = worst.seen ? "SEEN" : "HIDDEN";
    if (tavern_->stance() == sim::Stance::Crouched) {
        line += " CROUCH";
    }
    // The two numbers a player can actually do something about, in the words
    // they would use. Deliberately short: this is an EDGE line, not a sheet.
    line += light >= 50 ? "  LIT " : "  DARK ";
    line += std::to_string(light);
    if (noise >= sim::kNoiseRunning) {
        line += "  LOUD";
    } else if (noise > 0) {
        line += "  HEARD";
    } else {
        line += "  QUIET";
    }
    return line;
}

std::string Session::lockLine() const {
    const sim::Lockpicking& wire = tavern_->picking();
    if (!wire.open()) {
        return {};
    }
    // THE WHOLE MINIGAME, IN ONE ROW OF 4x6 GLYPHS. A pin that has dropped is
    // a star and one still up is a dash; the depth track is nine dots with the
    // pick standing on one of them. Nothing here needs a panel, and a panel is
    // what the Java build's first-person view died of.
    std::string line = "LOCK  PINS ";
    for (std::int32_t i = 0; i < wire.lock().pins; ++i) {
        line += i < wire.pinsSet() ? '*' : '-';
    }
    line += "  DEPTH ";
    for (std::int32_t d = 0; d < sim::kPinDepths; ++d) {
        // '+' and not '#': the 4x6 font in hud.cpp has fifty-two glyphs and a
        // hash is not one of them, so the pick's own position drew as a hole in
        // the track. Caught by looking at the frame, which is the point of
        // looking at the frame.
        line += d == wire.depth() ? '+' : '.';
    }
    line += "  STRAIN " + std::to_string(wire.strain()) + "/" +
            std::to_string(wire.strainLimit());
    line += "  PICKS " + std::to_string(tavern_->picks());
    return line;
}

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

namespace {

// ---------------------------------------------------------------------------
// COURIER CASE: the quiet tenant
// ---------------------------------------------------------------------------
//
// The owner's second authored case, session-scripted end to end. Everything
// here is presentation-side state driving the SAME public verbs a keypress
// drives -- hear() on the second book, say() on the alert row, the identical
// plate machinery the lead-opened notice uses -- so the population baseline
// and both gate workloads (which never construct a Session) cannot see any of
// it. Determinism is the scripted line's business: --case runs the errand
// through walkAcrossDistrict and the ordinary verbs, and test_case_line runs
// it twice and requires identical books.
/// Six seconds of world-steps between the opening page going down and Onna's
/// hail, and the same again before the follow-up line: long enough that the
/// street exists first, short enough that the sheet IS the first thing that
/// happens to a new player.
constexpr int kCourierDelaySteps = 6 * granadad::sim::kStepsPerSecond;

/// How close TAKE HIM UP reaches, in tiles -- a body you are standing over,
/// the same arm's length the punch that put him down was thrown at.
constexpr std::int32_t kTakeReachTiles = 2;

}  // namespace

void Session::courierDeliverNow() {
    recordWatchOp(WatchOpKind::CourierNow);
    const WatchDepthGuard watchGuard(watchDepth_);
    if (courierStage_ != 0 || !sheetRaws_.loaded() || !sheetBook_.active()) {
        return;
    }
    // THE SHEET INTO THE HAND. Hearing the first lead is the whole delivery:
    // the errand lands in the book, and the letter is `handed`, so the same
    // act puts the sheet itself on the Letters tile -- unlockedLetters()
    // derives it, nothing is flagged.
    const std::int32_t first = sheetRaws_.indexOf("gull-door");
    if (first < 0) {
        return;
    }
    (void)sheetBook_.hear(first, caseNowSeconds());
    say("ONNA, AT YOUR ELBOW: PAPER FOR YOU, OUT OF THE MISSION. IT COULD NOT WAIT.");
    // The lead-opened plate, in its own words -- the same field, the same
    // countdown, the same live Menu binding the new-leads notice builds from,
    // all through armCasePlate so the device-aware key and the notice grammar
    // ("A MISSION SHEET - J") are one fact in one place.
    armCasePlate("A MISSION SHEET");
    courierStage_ = 1;
    courierSteps_ = 0;
}

const sim::Actor* Session::sheetQuarry() const {
    // The quiet tenant, by ROLE rather than by name: the roster names exactly
    // one SkyrunnerContact and the roster is never reordered. Present, on his
    // feet gone from under him, and still where he fell.
    for (const sim::Actor& actor : tavern_->actors()) {
        if (actor.role() == sim::ActorRole::SkyrunnerContact && actor.present() &&
            actor.activity() == sim::Activity::Downed) {
            return &actor;
        }
    }
    return nullptr;
}

bool Session::caseTakeReady() const {
    if (!sheetCaseLive() || sheetCarry_) {
        return false;
    }
    const sim::Actor* quarry = sheetQuarry();
    if (quarry == nullptr || quarry->band() != body_->band()) {
        return false;
    }
    const std::int32_t dx = quarry->tileX() - body_->tileX();
    const std::int32_t dy = quarry->tileY() - body_->tileY();
    return (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) <= kTakeReachTiles;
}

void Session::stepSheetCase() {
    if (!sheetRaws_.loaded() || !sheetBook_.active()) {
        return;
    }
    // 1. THE COURIER. Counts only while the world has the keys -- never under
    // the opening page, a menu, a conversation or a lock -- so the hail can
    // never land on a surface that would swallow it, and never lands in a
    // scripted capture at all unless the config asked for a courier.
    if (config_.courier && courierStage_ < 2 && !firstRun_ && !conversingNow() && !picking()) {
        if (courierStage_ == 0) {
            if (++courierSteps_ >= kCourierDelaySteps) {
                courierDeliverNow();
            }
        } else if (++courierSteps_ >= kCourierDelaySteps) {
            // The one follow-up, and then the paper does the teaching.
            say("THE SHEET IS IN YOUR LETTERS. MAELL DOES NOT WRITE TWICE.");
            courierStage_ = 2;
        }
    }
    // 2. THE TAKE, NAMED WHEN IT IS LIVE. Once per downing: the flag re-arms
    // when he is back on his feet, so a player who hesitated is told again
    // the next time they earn the moment, and never told twice for standing
    // still.
    if (caseTakeReady()) {
        if (!sheetTakeSaid_) {
            say("FINCH IS ON THE BOARDS. " +
                std::string(promptLabel(controls_, Action::Interact, promptDevice_)) +
                " TAKES HIM UP.");
            sheetTakeSaid_ = true;
        }
    } else {
        sheetTakeSaid_ = false;
    }
    // 3. THE DELIVERY. Walking into the back room with the man IS the act --
    // no press owed on top of it. The scripted look lands on the close lead's
    // own site, which this body is within look range of, so the book closes
    // through the one verb every lead closes through.
    if (sheetCarry_ && !sheetBook_.closed()) {
        const std::int32_t close = sheetRaws_.indexOf("bring-him-in");
        if (close >= 0) {
            const sim::Lead& lead =
                sheetRaws_.leads()[static_cast<std::size_t>(close)];
            const std::int32_t dx = lead.site.x - body_->tileX();
            const std::int32_t dy = lead.site.y - body_->tileY();
            if (lead.site.band == body_->band() &&
                (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) <= sim::kLookRangeTiles) {
                const std::int64_t now = caseNowSeconds();
                (void)sheetBook_.hear(close, now);
                const sim::LookResult done =
                    sheetBook_.look(lead.site.x, lead.site.y, lead.site.band, now);
                sheetCarry_ = false;
                say(done.line);
                // Device-aware through armCasePlate, the courier plate's
                // twin: "THE ERRAND IS PAID - J", the pad's own button when
                // a pad holds the machine (Ship phase).
                armCasePlate("THE ERRAND IS PAID");
            }
        }
    }
}

namespace {

// ---------------------------------------------------------------------------
// EVICTION CASE: the writ, the door, and the two ways it closes
// ---------------------------------------------------------------------------
/// The evening the door will answer a knock -- the rota's own home hours.
/// Serfs are on the quay to six and walk home after (ward_actors.cpp Anchor
/// window 07:00-18:00; ReturnHome night term at 22:00), so from six the
/// family is arriving and by midnight abed. Knock outside this and the lane
/// answers instead of the door -- the brief's "once people are home" made a
/// gate rather than a suggestion.
constexpr int kEvictHomeFromHour = 18;

constexpr int kEvictHomeUntilHour = 24;

/// How close the door verbs reach: the family-door lead's own look range, an
/// arm's length from the wood, the same measure every lead is stood at.
constexpr std::int32_t kEvictDoorReachTiles = sim::kLookRangeTiles;

}  // namespace

void Session::syncEvictionTopics() {
    // THE STAGE, HANDED TO THE DIRECTOR the way the ground plot is: 0 offers
    // the hire, 1 offers the walk-back, 2 offers neither -- and the one line
    // of continuity the design note allows rides the courier case having
    // closed. Fed before every conversation opens (interact, below), so
    // Maell's writ row is built knowing where the errand stands. A missing
    // eviction.json (the loader's never-throws contract) reads as stage 2:
    // no case, no topic, exactly the empty-trail state a missing file leaves
    // everywhere else.
    const int stage =
        !evictRaws_.loaded() ? 2 : (evictBook_.closed() ? 2 : (evictCaseLive() ? 1 : 0));
    tavern_->dialogue().setEvictionCase(stage, sheetBook_.closed());
}

void Session::settleTakeWrit() {
    // THE HIRE. The director already read the open-hand bar and answered the
    // speech (dialogue.cpp TakeWrit); this is the CASE half of the intent,
    // the ReadRoll/Petition split one book further. Hearing the first lead
    // is the whole delivery, exactly the courier's courierDeliverNow: the
    // errand lands in the book, and the writ letter is `handed`, so the same
    // act puts it on the Letters tile (unlockedLetters derives it).
    if (evictCaseLive() || evictBook_.closed() || !evictRaws_.loaded()) {
        return;
    }
    const std::int32_t first = evictRaws_.indexOf("writ-in-hand");
    if (first < 0) {
        return;
    }
    const std::int64_t now = caseNowSeconds();
    (void)evictBook_.hear(first, now);
    // AND READ, scripted, the same contract both closes keep: the docket is
    // Maell's own briefing, given to your face with the writ in your hand --
    // there is nothing left to walk to and discover about it. Reading it is
    // what opens the Netters' gate lead (the authored chain's first link);
    // without this the examine key at the gate reads an Unheard corner of
    // the ward and the trail never starts.
    const sim::Lead& lead = evictRaws_.leads()[static_cast<std::size_t>(first)];
    (void)evictBook_.look(lead.site.x, lead.site.y, lead.site.band, now);
    syncEvictionTopics();
    armCasePlate("A WRIT OF DISTRAINT");
}

void Session::settleYieldWrit() {
    // THE DISRUPT CLOSE. The director already priced the walk-back through
    // the ledger's own WalkedOut (dialogue.cpp YieldWrit); this closes the
    // book on the stood-down lead -- read HERE, never by the examine key,
    // the same scripted-look contract the courier's delivery keeps. The
    // family got whatever warning the knock bought them, and no weapon is
    // granted: the point of the path.
    if (!evictCaseLive() || evictBook_.closed()) {
        return;
    }
    const std::int32_t close = evictRaws_.indexOf("stood-down");
    if (close < 0) {
        return;
    }
    const sim::Lead& lead = evictRaws_.leads()[static_cast<std::size_t>(close)];
    const std::int64_t now = caseNowSeconds();
    (void)evictBook_.hear(close, now);
    const sim::LookResult done = evictBook_.look(lead.site.x, lead.site.y, lead.site.band, now);
    say(done.line);
    syncEvictionTopics();
    armCasePlate("THE WRIT IS GIVEN BACK");
}

int Session::evictLeadInLookReach() const {
    // sheetLeadInLookReach()'s exact walk over the third book, so the
    // crosshair names the eviction's sites with the identical honesty. The
    // two close leads are excluded here for the same reason the examine key
    // never reads them: they belong to the scripted beats, not to a look.
    if (evictBook_.raws() == nullptr) {
        return -1;
    }
    const std::vector<sim::Lead>& leads = evictBook_.raws()->leads();
    const std::int32_t band = body_->band();
    const auto nearestFrom = [&](std::int32_t fromX, std::int32_t fromY) {
        int best = -1;
        std::int32_t bestDistance = 0;
        for (std::size_t i = 0; i < leads.size(); ++i) {
            const sim::Lead& lead = leads[i];
            if (lead.site.band != band || lead.close) {
                continue;
            }
            const std::int32_t dx = lead.site.x - fromX;
            const std::int32_t dy = lead.site.y - fromY;
            const std::int32_t distance = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
            if (distance > sim::kLookRangeTiles) {
                continue;
            }
            if (best < 0 || distance < bestDistance) {
                best = static_cast<int>(i);
                bestDistance = distance;
            }
        }
        return best;
    };
    return nearestFrom(body_->tileX(), body_->tileY());
}

bool Session::evictDoorReady() const {
    // THE KNOCK/SERVE VERB IS LIVE when the writ is out and unserved, the body
    // is within an arm's length of the Marrow door, and it is evening -- the
    // rota's own home hours, the brief's "once people are home". Gated on the
    // case being LIVE (hired) rather than on the door lead being heard: the
    // writ letter names the door outright, so a hired hand can serve it
    // whether or not they walked the gate lead first. Outside the hours the
    // door is there but nobody is: interact falls through to its ordinary
    // meaning and the nudge says why.
    if (!evictCaseLive() || writServed_ || !evictRaws_.loaded()) {
        return false;
    }
    const std::int32_t door = evictRaws_.indexOf("family-door");
    if (door < 0) {
        return false;
    }
    const sim::Lead& lead = evictRaws_.leads()[static_cast<std::size_t>(door)];
    if (lead.site.band != body_->band()) {
        return false;
    }
    const std::int32_t dx = lead.site.x - body_->tileX();
    const std::int32_t dy = lead.site.y - body_->tileY();
    if ((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) > kEvictDoorReachTiles) {
        return false;
    }
    const int hour = static_cast<int>((timeOfDay_ / 3600) % 24);
    return hour >= kEvictHomeFromHour && hour < kEvictHomeUntilHour;
}

void Session::grantEvictor() {
    // THE ONE PLACE THIS BUILD ARMS THE PLAYER. The weapon lane's grant
    // seam, under the agreed id: arms The Evictor and touches nothing else
    // -- intent stays where the player left it (the weapon-only setter the
    // case lane's flag asked for is exactly what grantPlayerWeapon is).
    // Unknown ids refuse rather than disarm, so a misspelled reward fails
    // its test instead of quietly emptying the player's hand.
    (void)tavern_->grantPlayerWeapon(sim::kEvictorWeaponId);
}

void Session::stepEvictCase() {
    if (!evictRaws_.loaded() || !evictBook_.active()) {
        return;
    }
    // THE DOOR NUDGE, named when it is live and re-armed when the player
    // steps away -- caseTakeReady's own once-per-arming rule. Two verbs at
    // one press: knock first, serve second, the nudge naming whichever is
    // next.
    if (evictDoorReady()) {
        if (!evictDoorSaid_) {
            const std::string verb =
                std::string(promptLabel(controls_, Action::Interact, promptDevice_));
            say(evictKnocked_ ? "THE MARROWS ARE AT THE DOOR. " + verb + " SERVES THE WRIT."
                              : "A LIGHT UNDER THE MARROW DOOR. " + verb + " KNOCKS.");
            evictDoorSaid_ = true;
        }
    } else {
        evictDoorSaid_ = false;
        // The knock is a fact about tonight; a player who wandered off the
        // step loses the answered door and must knock again.
        if (!writServed_) {
            evictKnocked_ = false;
        }
    }
    // THE PARTICIPATE CLOSE. Walking back into the Mission with the served
    // writ IS the delivery -- no press owed, stepSheetCase's own contract --
    // so the book closes through the one scripted look, and The Evictor is
    // paid on the same step the roll is signed.
    if (writServed_ && !evictBook_.closed()) {
        const std::int32_t close = evictRaws_.indexOf("served");
        if (close < 0) {
            return;
        }
        const sim::Lead& lead = evictRaws_.leads()[static_cast<std::size_t>(close)];
        const std::int32_t dx = lead.site.x - body_->tileX();
        const std::int32_t dy = lead.site.y - body_->tileY();
        if (lead.site.band == body_->band() &&
            (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) <= sim::kLookRangeTiles) {
            const std::int64_t now = caseNowSeconds();
            (void)evictBook_.hear(close, now);
            const sim::LookResult done =
                evictBook_.look(lead.site.x, lead.site.y, lead.site.band, now);
            grantEvictor();
            say(done.line);
            armCasePlate("THE EVICTOR IS YOURS");
        }
    }
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

void Session::settleDefeat() {
    // THE PLAYER RESPAWNS AS THEMSELVES AND THE WORLD KEEPS THE CONSEQUENCES.
    // Eli ruled 2026-07-31 that the persistent-ward variant -- the city
    // surviving your death while you come back as somebody else -- is not being
    // built from the start. So this is the whole of the respawn: the room gives
    // the hit points back and moves the clock on, and the body wakes up on the
    // quay apron, which is the same two tiles clear of the threshold a man put
    // out of the door ends up on.
    //
    // WHAT IT DOES NOT DO IS UNDO ANYTHING. The rung, the founded house, the
    // toll on every price and the name on the roll all survive this call. That
    // is the design.
    const sim::Rise& rise = tavern_->lastDefeat();
    // ACTION-COMBAT BUILD (section 4.5). A DEFEAT UNDER LETHAL RULES (steel was
    // out -- tavern_->escalated()) is not a plain brawl KO: it earns the
    // OCCASION -- the dip, the epitaph naming killer, weapon and place, the held
    // hush of kDeathHoldSteps, then the reveal of the quay the revive below puts
    // you on. Armed HERE, from where the body fell, before reviveAfterDefeat and
    // placeBodyAt move it; composeDeathCeremony() paces the veil over the frames
    // that follow, render-only, while the sim revives underneath at once. A
    // plain brawl defeat (never escalated) keeps the bare settle it always had.
    if (tavern_->escalated()) {
        armDeathCeremony();
    }
    // AUDIO WIRING: the plan's "knockdown -> ThudHeavy" -- the one call every
    // path to the floor funnels through. THE LOT PASS: fall_land is what
    // ThudHeavy resolves to now, and the DEATH CEREMONY (an escalated
    // defeat, the occasion armed just above) gets the owner's own death vox
    // over it; a plain brawl KO keeps the bare thud.
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::ThudHeavy);
        if (tavern_->escalated()) {
            audio_->playOneShot(audio::SoundId::PlayerDown);
        }
    }
    tavern_->reviveAfterDefeat();
    // Through placeBodyAt: waking on the quay apron is a relocation like any
    // other, and the room you were beaten in must not ghost over the street.
    placeBodyAt(sim::gull::kStreetX, sim::gull::kStreetY, sim::gull::kGroundBand);
    awaitingLanding_ = false;
    syncTavernToBody();
    timeOfDay_ = tavern_->timeOfDay();
    settings_.timeOfDay = timeOfDay_;
    // What he said standing over you, out of the owner's tables -- and the
    // short report of what it made him when there is nothing authored.
    if (!rise.taunt.empty()) {
        sayAs(rise.who, rise.taunt, false);
    } else if (!rise.line.empty()) {
        say(rise.line);
    }
    syncWardToCalendar();
}

namespace {

/// ACTION-COMBAT BUILD (section 4.5). The two eases that bracket the death
/// ceremony's held epitaph: the frame dips to black over kDeathDipInSteps and
/// eases back over kDeathFadeOutSteps, with the sim's kDeathHoldSteps hush
/// between. The eases are one page-ease each; the HOLD is the sim's own number.
constexpr std::int32_t kDeathDipInSteps = kPageEaseSteps;

constexpr std::int32_t kDeathFadeOutSteps = kPageEaseSteps;

}  // namespace

void Session::armDeathCeremony() {
    // From the last defeat, captured before the revive moves the body: the
    // killer's name in the terminal register, a weapon word off his own hand,
    // and the place it happened. Two lines, the veto-blessed epitaph grammar
    // (section 5): "PUT DOWN IN THE GILDED GULL. / BY CANNIC. BY STEEL."
    const sim::Rise& rise = tavern_->lastDefeat();
    const std::string killer = rise.who.empty() ? std::string("THE DARK") : upperAscii(rise.who);
    // The weapon word is honest to what put you down, read off the killer's
    // rival record. A lethal fight is usually steel, but a bloodied man beaten
    // to death by fists is lethal too (B3), so the word follows the hand.
    std::string weaponWord = "STEEL";
    if (const sim::Nemesis* rival = tavern_->nemesis().of(rise.actorId); rival != nullptr) {
        switch (rival->weapon()) {
            case sim::Weapon::Edged:
                weaponWord = "STEEL";
                break;
            case sim::Weapon::Blunt:
            case sim::Weapon::Evictor:
                weaponWord = "THE CUDGEL";
                break;
            case sim::Weapon::Improvised:
                weaponWord = "WHAT CAME TO HAND";
                break;
            case sim::Weapon::Fists:
                weaponWord = "BARE HANDS";
                break;
        }
    }
    // Inside the house it is the Gull by name (the veto's own literal); on the
    // street it is the ground placeLabel() answers for.
    const std::string place =
        tavern_->playerInside() ? std::string("THE GILDED GULL") : placeLabel();
    deathEpitaphTop_ = "PUT DOWN IN " + place + ".";
    deathEpitaphBottom_ = "BY " + killer + ". BY " + weaponWord + ".";
    deathCeremonySteps_ = kDeathDipInSteps + sim::kDeathHoldSteps + kDeathFadeOutSteps;
}

void Session::composeDeathCeremony(Framebuffer& target) const {
    if (deathCeremonySteps_ <= 0) {
        return;
    }
    constexpr std::int32_t kTotal =
        kDeathDipInSteps + sim::kDeathHoldSteps + kDeathFadeOutSteps;
    const std::int32_t remaining = deathCeremonySteps_;
    // The veil's strength across the three phases: ramp up as it comes down,
    // full through the hush, ramp back as the quay comes up under it.
    float dip = 1.0F;
    if (remaining > sim::kDeathHoldSteps + kDeathFadeOutSteps) {
        const std::int32_t elapsed = kTotal - remaining;  // 0 .. kDeathDipInSteps
        dip = static_cast<float>(elapsed) / static_cast<float>(kDeathDipInSteps);
    } else if (remaining <= kDeathFadeOutSteps) {
        dip = static_cast<float>(remaining) / static_cast<float>(kDeathFadeOutSteps);
    }
    dip = dip < 0.0F ? 0.0F : (dip > 1.0F ? 1.0F : dip);
    // The veil itself -- the same black-fill primitive dipTravelSeam draws with,
    // over the whole finished frame (HUD included).
    target.fillRect(0, 0, target.width(), target.height(), Rgb{0.0F, 0.0F, 0.0F}, dip);
    // The epitaph, centred, in the plate's own bone register. The black field IS
    // its backing, so it needs no plate of its own; the text alpha rides the dip
    // so the words arrive as the room goes dark and leave as it comes back.
    const int scale = hudScale(target.height());
    const int glyphH = 6 * scale;  // the 4x6 font's own height, drawText's grid
    const int lineStep = glyphH + 2 * scale;
    const int cx = target.width() / 2;
    const int topY = target.height() / 2 - lineStep;
    const Rgb bone{0.86F, 0.82F, 0.72F};
    const auto centre = [&](const std::string& line, int y) {
        if (line.empty()) {
            return;
        }
        const int w = textWidth(line, scale);
        drawText(target, cx - w / 2, y, line, bone, dip, scale);
    };
    centre(deathEpitaphTop_, topY);
    centre(deathEpitaphBottom_, topY + lineStep);
}

void Session::recordWatchOp(WatchOpKind kind, std::int32_t a, bool ok) {
    if (watchTape_ == nullptr || watchDepth_ != 0) {
        return;
    }
    WatchOp op;
    op.kind = kind;
    op.a = a;
    op.ok = ok;
    watchTape_->push_back(op);
}

void Session::watchChapter(std::int32_t index) { recordWatchOp(WatchOpKind::Chapter, index); }

void Session::watchBeatLanded(std::int32_t index, bool ok) {
    recordWatchOp(WatchOpKind::BeatLanded, index, ok);
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

void Session::sayAs(const std::string& who, const std::string& line, bool report) {
    // THE SPEAKER'S LINE ON THE ALERT ROW. A REPORT is not their voice: the
    // director answers a deed with a short account of it written in the
    // speaker's own name, and prefixing that put the name on the row twice
    // -- "WATCHMAN CULL: WATCHMAN CULL TAKES THE DRINK.", the 2026-09-25
    // critic's D_law frame. The director says which it wrote (Reply::report,
    // set where the report is composed); the text is never read to guess,
    // so a rumour that opens on the speaker's own name keeps its "<name>: ".
    // say() clears the mouth and the demand flag; the mouth is set back
    // here, and a Watch site marks its demand (messageDemand_) after the
    // call, so a halt in a watchman's name is both his and a demand.
    say(report ? line : who + ": " + line);
    messageMouth_ = who;
}

void Session::say(std::string line) {
    // Clipped to what the bottom edge can hold at the narrowest resolution this
    // game runs at. S4 started routing a questline's journal prose through here
    // -- whole sentences out of the raws -- and the first capture of it ran off
    // the right edge mid-word, which looks like a bug because it is one.
    //
    // S10 RAISED IT FROM 56 TO 92. Fifty-six was the column count of a 320x180
    // frame, and it was the belt to the HUD's own braces -- drawHud clips the
    // alert with clipToWidth against the REAL frame width, which is the fix the
    // S6 review's cut-mid-glyph finding produced. So the guess here was
    // truncating a clue at 640x360, where the row holds far more, for no reason
    // but its own age. The HUD still clips; this only stops a runaway string
    // being carried around.
    constexpr std::size_t kAlertColumns = 92;
    if (line.size() > kAlertColumns) {
        line.resize(kAlertColumns);
        line += "..";
    }
    message_ = std::move(line);
    // Nobody's until sayAs says whose: a bare say() is a narration.
    messageMouth_.clear();
    // An ordinary line. The four Watch sites (the street's three in step()
    // and the Gull's own lastDemand, all through sayAs) set this back after
    // the call -- see rungPlateOutranked.
    messageDemand_ = false;
    // A new line, at whatever register the frame gives it, and news
    // unless the site says otherwise (messageHeld_).
    messageCapped_ = false;
    messageHeld_ = true;
    // UI-EA (LANE HUD): FOUR SECONDS, the spec's own message-row narration
    // hold (sec. 2, EVENT tier). Six was the pre-diet number; a line the
    // player is reading is re-armed by the next line anyway, and the bottom
    // of the frame is meant to be empty more than it is full. A bouncer's
    // WARNING is not on this clock -- `warned` holds the alert up for as
    // long as the house is still saying it (syncPanelAnim's own rule).
    messageSteps_ = 4 * sim::kStepsPerSecond;
    // TASK #83. EAGER, so the very first frame drawn after whichever verb
    // called this -- most of them do not call step() first, and the caller
    // here could be a test that never does -- already shows the prompt easing
    // in rather than waiting for the next real step() to notice messageSteps_
    // went from 0 to nonzero.
    syncPanelAnim();
}

bool Session::talking() const noexcept {
    return tavern_->dialogue().isOpen();
}

bool Session::haggling() const noexcept {
    return tavern_->dialogue().isHaggling();
}

const sim::WardActor* Session::wardAddresseeNow() const { return wardAddressee(); }

bool Session::wardAtCounterNow(const sim::WardActor& who) const noexcept {
    return wardAtCounter(who);
}

bool Session::wardAtCounter(const sim::WardActor& who) const noexcept {
    // SHOPS. At the post the bake gave them, give or take the room it stands
    // in: a keeper is behind the counter while they are working it, and met
    // anywhere else -- the street at midnight, their own doorway -- says so.
    return who.band == who.anchorBand && std::abs(who.x - who.anchorX) <= kCounterReachTiles &&
           std::abs(who.y - who.anchorY) <= kCounterReachTiles;
}

void Session::settleCounterReply(sim::Reply& reply) {
    // SHOPS (2026-10-09). The director said who wants what for how much; the
    // Kit's owner says whether it fits, and the keeper's own purse -- the
    // ward's -- takes the coin or pays it.
    sim::DialogueDirector& talk = tavern_->dialogue();
    const sim::WardActor* keeper = people_->byId(wardTalkId_);
    if (keeper == nullptr) {
        return;
    }
    std::string line;
    if (reply.kind == sim::TopicKind::BuyItem) {
        const sim::ServiceResult sold =
            tavern_->buyFromCounter(reply.itemId, reply.itemPrice, line);
        if (sold == sim::ServiceResult::Served) {
            people_->payCoinTo(wardTalkId_, reply.itemPrice);
        }
        reply.ok = sold == sim::ServiceResult::Served;
    } else {
        // A pawnbroker pays out of his own purse, and a short one is short.
        if (keeper->coin < reply.itemPrice) {
            line = upperAscii(people_->identity(wardTalkId_).name) + " HAS NOT GOT IT TODAY.";
            reply.ok = false;
        } else {
            const sim::ServiceResult bought =
                tavern_->sellToCounter(reply.itemId, reply.itemPrice, line);
            if (bought == sim::ServiceResult::Served) {
                (void)people_->takeCoinFrom(wardTalkId_, reply.itemPrice);
            }
            reply.ok = bought == sim::ServiceResult::Served;
        }
    }
    reply.line = line;
    reply.report = true;
    reply.coinDelta = 0;
    // The shelf and the pawn list read the Kit and the purse: rebuild them.
    talk.setPlayerCoin(tavern_->playerCoin());
    talk.setCounter(tavern_->counterViewFor(talk.speaker().notableId, wardAtCounter(*keeper)));
    talk.refreshTopics();
    if (audio_ != nullptr) {
        audio_->playOneShot(reply.ok ? audio::SoundId::UiConfirm : audio::SoundId::UiError);
    }
}

const sim::WardActor* Session::wardAddressee() const {
    // ERRANDS A HUMAN CAN FINISH (2026-10-09). Whoever E would address from
    // here: the body IN FRONT of the eye within talk reach, and only when
    // nobody stands in front, the nearest -- so a crowd at the Mission's
    // night pot no longer decides who you are talking to. The crosshair's
    // TALK prompt asks this same question, so the name it prints before the
    // press is the one that answers it.
    if (const sim::WardActor* ahead = people_->inFrontOf(
            body_->x(), body_->y(), body_->band(), body_->yaw(), kWardTalkReachTiles);
        ahead != nullptr) {
        return ahead;
    }
    return people_->nearestTo(body_->tileX(), body_->tileY(), body_->band(), kWardTalkReachTiles);
}

bool Session::talkToWard() {
    // #79. THE OTHER SIX HUNDRED AND FORTY-SEVEN.
    //
    // The Gull's fourteen are asked for first, above, because inside K03 the
    // taproom owns the room and its people carry a roster, a mood and a stock;
    // out here nobody does. Everything below is the SAME director, the same
    // authored tables and the same fallback chain -- only the Speaker is built
    // from a body in the street instead of from a body on a stool.
    const sim::WardActor* who = wardAddressee();
    if (who == nullptr) {
        return false;
    }
    sim::DialogueDirector& talk = tavern_->dialogue();
    talk.setPlayerCoin(tavern_->playerCoin());
    const sim::Speaker speaker = sim::wardSpeakerFor(
        *who, people_->identity(who->id), talk.notables(), talk.factions(), talk.barks());
    // SHOPS (2026-10-09). If they keep a counter, the shelf behind it -- open
    // only while they stand at their own post.
    talk.setCounter(tavern_->counterViewFor(speaker.notableId, wardAtCounter(*who)));
    if (!talk.open(speaker, tavern_->timeOfDay())) {
        talk.setCounter(sim::CounterView{});
        return false;
    }
    wardTalkId_ = who->id;
    // They look at you while you talk to them, exactly as the taproom's do.
    people_->faceToward(wardTalkId_, body_->tileX(), body_->tileY());
    return true;
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

void Session::moveTopicCursor(int delta) {
    // AUDIO WIRING: one quiet tick per cursor move, on every list this router
    // serves -- the restrained half of the plan's "focus move -> UiTick".
    if (audio_ != nullptr && delta != 0 && (talking() || casebookOpen_ || keysOpen_)) {
        audio_->playOneShot(audio::SoundId::UiTick);
    }
    if (keysOpen_) {
        wrapCursorAndPage(caseCursor_, casePage_, delta, static_cast<int>(keyRows().size()));
        return;
    }
    // MORROWIND ROUND: ROUTED BY WHICH TILE HAS FOCUS, not by which of four
    // now-simultaneous bools happens to be true -- see menuFocus()'s own
    // header. Each tile keeps its OWN cursor/page (and, for Journal and
    // Letters, its own picked entry), so reading one tile never disturbs
    // where the cursor was left on another.
    if (casebookOpen_) {
        switch (menuFocus_) {
            case kMenuFocusCharacter:
                // THE SAME KEYS, THE SAME PAGING -- see toggleCharacter.
                wrapCursorAndPage(characterCursor_, characterPage_, delta,
                                  static_cast<int>(characterRows().size()));
                return;
            case kMenuFocusMap:
                // THE SAME KEYS, THE SAME PAGING -- see toggleMap.
                wrapCursorAndPage(mapCursor_, mapPage_, delta,
                                  static_cast<int>(mapRows().size()));
                return;
            case kMenuFocusLetters:
                if (lettersEntry_ >= 0) {
                    // AN OPEN LETTER HAS NO CURSOR TO MOVE -- lettersBodyPage_
                    // is doing a different job here (which page of the BODY is
                    // showing; see nextTopicPage's own kMenuFocusLetters
                    // branch).
                    return;
                }
                // TASK #82. THE SAME KEYS, THE SAME PAGING -- see toggleLetters.
                wrapCursorAndPage(lettersCursor_, lettersPage_, delta,
                                  static_cast<int>(unlockedLetters().size()));
                return;
            case kMenuFocusJournal:
            default:
                // THE SAME KEYS, THE SAME PAGING. The Journal tile is a
                // conversation with your own notes -- see
                // Session::toggleCasebook on why it borrows the dialogue
                // surface's own vocabulary rather than being a sheet of its
                // own. The count is leads PLUS the work rows under them
                // (journalWorkRows), so the cursor can reach a contract or a
                // log line to read it -- picking one stays a no-op.
                wrapCursorAndPage(caseCursor_, casePage_, delta,
                                  static_cast<int>(activeCasebook().known().size() +
                                                   journalWorkRows().size()));
                return;
        }
    }
    if (!talking()) {
        return;
    }
    // Wraps, so holding one direction walks the whole list. The page FOLLOWS
    // the cursor: walking off the bottom of a page turns it, so the arrow
    // keys reach every topic and the numbers on screen are always the numbers
    // that pick the ones you can see.
    wrapCursorAndPage(topicCursor_, topicPage_, delta,
                       static_cast<int>(tavern_->dialogue().topics().size()));
}

int Session::menuCursor() const noexcept {
    // KIT POLISH. THE SAME ROUTE moveTopicCursor() TAKES, read instead of
    // moved -- see the header. Kept beside it on purpose: a list this
    // router learns to move is a list this has to learn to read.
    if (keysOpen_) {
        return caseCursor_;
    }
    if (casebookOpen_) {
        switch (menuFocus_) {
            case kMenuFocusCharacter:
                return characterCursor_;
            case kMenuFocusMap:
                return mapCursor_;
            case kMenuFocusLetters:
                return lettersCursor_;
            case kMenuFocusJournal:
            default:
                return caseCursor_;
        }
    }
    return topicCursor_;
}

void Session::nextTopicPage() {
    if (optionsOpen_) {
        const int pages = topicPageCount(optionRows().size());
        optionPage_ = (optionPage_ + 1) % pages;
        optionCursor_ = std::min(static_cast<int>(optionRows().size()) - 1,
                                 optionPage_ * kTopicPageSize);
        return;
    }
    if (keysOpen_) {
        const std::size_t rows = keyRows().size();
        const int pages = topicPageCount(rows);
        casePage_ = (casePage_ + 1) % pages;
        caseCursor_ = std::min(static_cast<int>(rows) - 1, casePage_ * kTopicPageSize);
        return;
    }
    // MORROWIND ROUND: ROUTED BY FOCUS -- see moveTopicCursor()'s own note.
    if (casebookOpen_) {
        // AUDIO WIRING: turning a page WITHIN a tile is the same paper the
        // tile cycle is -- one BookFlip, whichever tile pages.
        if (audio_ != nullptr) {
            audio_->playOneShot(audio::SoundId::BookFlip);
        }
        switch (menuFocus_) {
            case kMenuFocusCharacter:
                advancePage(characterPage_, characterCursor_, characterRows().size());
                return;
            case kMenuFocusMap:
                advancePage(mapPage_, mapCursor_, mapRows().size());
                return;
            case kMenuFocusLetters:
                if (lettersEntry_ >= 0) {
                    // PAGING THROUGH THE OPEN LETTER'S OWN BODY, not the
                    // title list -- lettersBodyPage_ is a field of its own
                    // (menu_view.cpp works out the true page count off the
                    // actual wrapped rows at the panel's real width, and
                    // CLAMPS it into range every frame, so incrementing past
                    // the end here is never observable).
                    ++lettersBodyPage_;
                    return;
                }
                advancePage(lettersPage_, lettersCursor_, unlockedLetters().size());
                return;
            case kMenuFocusJournal:
            default:
                advancePage(casePage_, caseCursor_,
                            activeCasebook().known().size() + journalWorkRows().size());
                return;
        }
    }
    if (!talking()) {
        return;
    }
    // The cursor comes with the page, onto its first topic, so E never picks
    // something that is not on screen.
    advancePage(topicPage_, topicCursor_, tavern_->dialogue().topics().size());
}

void Session::chooseVisibleTopic(int slot) {
    recordWatchOp(WatchOpKind::Topic, slot);
    const WatchDepthGuard watchGuard(watchDepth_);
    if (slot < 0 || slot >= kTopicPageSize) {
        return;
    }
    if (pauseOpen_) {
        // A MENU ROW IS AN ACTION, NOT A VALUE. Options' own numbered rows only
        // move the cursor -- ENTER is what rebinds or nudges a slider, and a
        // stray tenth-of-a-second double press should not fire a rebind. This
        // page has no sliders, only three rows the numbers already printed
        // beside on screen (topicRowsFor prints them for every list this
        // surface draws), so the honest behaviour for "press the number you
        // read" is the one talking() gets below: the press both moves the
        // cursor and acts, immediately.
        if (slot < static_cast<int>(pauseRows().size())) {
            pauseCursor_ = slot;
            choosePause();
        }
        return;
    }
    if (optionsOpen_) {
        pickCursorIfVisible(optionCursor_, optionPage_, slot, optionRows().size());
        return;
    }
    if (keysOpen_) {
        // A key row is a reference, not a choice. The cursor moves and nothing
        // else happens, which is the honest behaviour for a list you read.
        pickCursorIfVisible(caseCursor_, casePage_, slot, keyRows().size());
        return;
    }
    // MORROWIND ROUND: ROUTED BY FOCUS -- see moveTopicCursor()'s own note.
    if (casebookOpen_) {
        switch (menuFocus_) {
            case kMenuFocusCharacter:
                // A ROW ON THIS TILE IS SOMETHING TO READ, not a choice --
                // the same honest no-op the keys page gives a number press.
                pickCursorIfVisible(characterCursor_, characterPage_, slot,
                                    characterRows().size());
                return;
            case kMenuFocusMap:
                // A ROW ON THIS TILE IS SOMETHING TO READ, not a choice -- the
                // identical no-op the character tile and the keys page give a
                // number press.
                pickCursorIfVisible(mapCursor_, mapPage_, slot, mapRows().size());
                return;
            case kMenuFocusLetters: {
                if (lettersEntry_ >= 0) {
                    // THE TITLE LIST IS NOT ON SCREEN WHILE A LETTER IS OPEN
                    // -- see DialogueViewState::letter's own note -- so a
                    // number press here has nothing on this tile to have
                    // picked. ESC (closeConversation) is what steps back to
                    // the list.
                    return;
                }
                // TASK #82. A ROW HERE IS A CHOICE, THE SAME WAY A LEAD IS ON
                // THE JOURNAL TILE -- picking a title opens the letter under
                // it. Still pure UI: nothing in the simulation moves when a
                // letter is opened, exactly as reading a casebook entry does
                // not.
                const int index = lettersPage_ * kTopicPageSize + slot;
                if (index >= static_cast<int>(unlockedLetters().size())) {
                    return;
                }
                lettersCursor_ = index;
                lettersEntry_ = index;
                lettersBodyPage_ = 0;
                return;
            }
            case kMenuFocusJournal:
            default: {
                const int index = casePage_ * kTopicPageSize + slot;
                const int leads = static_cast<int>(activeCasebook().known().size());
                if (index >= leads + static_cast<int>(journalWorkRows().size())) {
                    return;
                }
                caseCursor_ = index;
                if (index < leads) {
                    // Only a LEAD opens as an entry; a work row under the
                    // trail is something to read, so the number press moves
                    // the cursor onto it and nothing else -- the identical
                    // no-op the character tile gives.
                    caseEntry_ = index;
                }
                return;
            }
        }
    }
    if (!talking()) {
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
    // MORROWIND ROUND: ROUTED BY FOCUS -- see moveTopicCursor()'s own note.
    if (casebookOpen_ && menuFocus_ == kMenuFocusJournal) {
        // Reading an entry of your own notes. Nothing in the simulation moves;
        // this is the one place in the game where picking a row is pure UI, and
        // it is pure UI because the trail's state changed when you LOOKED, not
        // when you read your own handwriting back.
        if (index < activeCasebook().known().size()) {
            caseCursor_ = static_cast<int>(index);
            caseEntry_ = static_cast<int>(index);
            // AUDIO WIRING: opening an entry of your own notes is a page, not
            // a menu -- BookFlip, not UiConfirm.
            if (audio_ != nullptr) {
                audio_->playOneShot(audio::SoundId::BookFlip);
            }
        } else if (index < activeCasebook().known().size() + journalWorkRows().size()) {
            // A work row under the trail -- a live contract, a finished
            // stage's log line -- is something to read, not a choice: the
            // cursor moves onto it, no entry opens and no page speaks. The
            // identical no-op the character tile gives a pick.
            caseCursor_ = static_cast<int>(index);
        }
        return;
    }
    if (casebookOpen_ && menuFocus_ == kMenuFocusCharacter) {
        // KIT BUILD. A CARRIED ROW IS A CHOICE NOW: ENTER wears it (or bares
        // it) through the room's own door. A sheet row stays something to
        // read -- the no-op the keys page gives -- so the index passed in
        // is ignored for the tile's OWN cursor, which is the row on screen.
        (void)index;
        wearHighlightedKitRow();
        return;
    }
    if (casebookOpen_ && menuFocus_ == kMenuFocusLetters) {
        // TASK #82. Opening a letter is exactly as pure-UI as opening a
        // casebook entry -- see that branch's own note. A no-op while a
        // letter is already open, for the identical reason
        // chooseVisibleTopic's own kMenuFocusLetters branch gives.
        if (lettersEntry_ < 0 && index < unlockedLetters().size()) {
            lettersCursor_ = static_cast<int>(index);
            lettersEntry_ = static_cast<int>(index);
            lettersBodyPage_ = 0;
            // AUDIO WIRING: unfolding a letter is paper too.
            if (audio_ != nullptr) {
                audio_->playOneShot(audio::SoundId::BookFlip);
            }
        }
        return;
    }
    if (!talking()) {
        return;
    }
    // AUDIO WIRING: the accept, spoken once per picked topic -- the plan's
    // "accept -> UiConfirm". Before the settlement on purpose: the press is
    // what is being acknowledged, and any coin the settlement moves speaks
    // for itself a step later (see the purse watch in step()).
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::UiConfirm);
    }
    // #79. WHICH ROOM OWNS THE CONSEQUENCE. The director decides what was SAID
    // and what the world now owes; whoever owns the body applies it. Inside the
    // Gull that is the Tavern, which has a bar to take a drink off and a
    // bouncer to send over. On the street it is the ward, which has neither --
    // so the ward has its own, much shorter, settlement.
    sim::Reply reply =
        wardTalkId_ >= 0 ? chooseWardTopic(index) : tavern_->chooseTopic(index);
    // TIME-AND-TENURE BUILD. The two ground topics are intents, exactly like
    // Buy: the director declared them and whoever owns the roll -- this
    // session, which borrowed the ward -- resolves them and fills in the
    // line. Settled HERE, after either settlement path above, because a
    // priest can be a street body (the ward path) or the Gull's own (the
    // tavern path) and the roll answers the same on both.
    if (reply.ok && reply.kind == sim::TopicKind::ReadRoll) {
        reply.line = groundRollLine();
        // Voiced into the panel too -- during a conversation the alert row
        // stands down (see drawFrame's hud.alert gate), so lastLine() is the
        // one surface the answer can actually be read on.
        tavern_->dialogue().speakResolved(reply.line);
    } else if (reply.ok && reply.kind == sim::TopicKind::Petition) {
        reply.line = settleGroundPetition();
        tavern_->dialogue().speakResolved(reply.line);
    } else if (reply.ok && reply.kind == sim::TopicKind::TakeWrit) {
        // EVICTION CASE. The director answered the speech (and gated the
        // open-hand bar); the CASE half is the session's, the ReadRoll split
        // one book on. The priest's own line is already in reply.line -- the
        // book opens under it.
        settleTakeWrit();
    } else if (reply.ok && reply.kind == sim::TopicKind::YieldWrit) {
        settleYieldWrit();
    }
    if (!reply.ok && reply.line.empty()) {
        return;
    }
    if (reply.haggling) {
        // Open the argument at what they are asking, so the first press of a
        // key is a concession rather than a guess.
        haggleOffer_ = tavern_->dialogue().haggle().asking();
    }
    if (!reply.line.empty()) {
        if (reply.kind == sim::TopicKind::Leave) {
            // VOICE LANE: THE GOODBYE rides the street channel as the panel
            // closes, in the speaker's name, never the amber alert row (a
            // goodbye is not a warning). The director has closed, but its
            // speaker still says who it was.
            voice_.speakFarewell(tavern_->dialogue().speaker().name, reply.line,
                                 tavern_->dialogue().farewellKey(false),
                                 tavern_->dialogue().speaker().actorId,
                                 static_cast<std::int32_t>(body_->stepCount()));
            syncVoiceAnim();
            // And the goodbye is the LAST thing they said -- endTalk's own
            // rule, for the panel LEAVE closed from inside: their greeting,
            // reply or the report of the drink you stood them, still on the
            // alert row's clock, stands down rather than resurfacing under
            // the goodbye. Whose the row is was recorded when it was said.
            if (!messageMouth_.empty() && messageMouth_ == tavern_->dialogue().speaker().name) {
                messageSteps_ = 0;
            }
        } else {
            sayAs(tavern_->dialogue().speaker().name, reply.line, reply.report);
        }
    }
    // VOICE LANE, THE ADDRESS: A WORDED REACTION when the reply moved them.
    // Never a number (DECISIONS.md: standing is never SHOWN as one): a
    // stage direction on the panel's dateline row for four seconds, so the
    // face reacts to every act, which is the one durable lesson of the
    // persuasion wheel and the whole of what the tone dial gets back.
    if (reply.ok && talking() && reply.dispositionAfter != reply.dispositionBefore) {
        reactionLine_ = wordedReaction(reply.dispositionBefore, reply.dispositionAfter);
        reactionSteps_ = reactionLine_.empty() ? 0 : kReactionHoldSteps;
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
    // TASK #83. Covers the one path through this function that can close the
    // conversation (reply.closes, when it lands on an empty reply.line) with
    // no call to say() to have carried the sync eagerly -- see say()'s own
    // note. Every other exit already got one from a say() above; this is a
    // no-op for those.
    syncPanelAnim();
}

void Session::closeConversation() {
    if (pauseOpen_) {
        // ESC out of QUIT's second press first, and out of the menu second --
        // the same shape as awaitingKey_ below, for the same reason: a player
        // who leant on ENTER by accident has to be able to back off it without
        // the whole page vanishing under them.
        if (quitArmed_) {
            quitArmed_ = false;
            return;
        }
        pauseOpen_ = false;
        pauseCursor_ = 0;
        syncPanelAnim();
        return;
    }
    // UI-EA-SPEC sec. 4 violation #4: BACK RETURNS TO THE OPENER. A page
    // entered through the pause menu -- CONTROLS, SETTINGS, WAIT -- backs
    // out ONE layer, to the pause menu with its cursor on the row that
    // opened the page, instead of skipping past it to the street. A page
    // entered by its direct shortcut (F1/F2, a bed's wait) still lands on
    // the street exactly as before: pageOpenedFromPause_ was derived false
    // at its open. The panel never closes across the return -- conversingNow
    // stays true -- so the swap reads as content changing inside one frame,
    // transition rule 2, and no close/open sound pair fires.
    const auto returnToPause = [this](int row) {
        pageOpenedFromPause_ = false;
        pauseOpen_ = true;
        // The row indices are pauseRows()' own order: RESUME, WAIT,
        // CONTROLS, SETTINGS, QUIT.
        pauseCursor_ = row;
        quitArmed_ = false;
        syncPanelAnim();
    };
    if (optionsOpen_) {
        // ESC out of a rebinding first, and out of the page second. A player
        // who opened "press a key" by accident has to be able to get out of it
        // without binding escape to something.
        if (awaitingKey_) {
            awaitingKey_ = false;
            return;
        }
        const bool toPause = pageOpenedFromPause_;
        optionsOpen_ = false;
        optionCursor_ = 0;
        optionPage_ = 0;
        if (toPause) {
            returnToPause(3);  // SETTINGS
            return;
        }
        syncPanelAnim();
        return;
    }
    if (keysOpen_) {
        const bool toPause = pageOpenedFromPause_;
        keysOpen_ = false;
        caseCursor_ = 0;
        casePage_ = 0;
        if (toPause) {
            returnToPause(2);  // CONTROLS
            return;
        }
        syncPanelAnim();
        return;
    }
    if (grimoireOpen_) {
        grimoireOpen_ = false;
        grimoireCursor_ = 0;
        grimoirePage_ = 0;
        syncPanelAnim();
        return;
    }
    if (waitOpen_) {
        const bool toPause = pageOpenedFromPause_;
        waitOpen_ = false;
        waitCursor_ = 0;
        waitPage_ = 0;
        if (toPause) {
            returnToPause(1);  // WAIT
            return;
        }
        syncPanelAnim();
        return;
    }
    if (districtMapOpen_) {
        // ESC closes the ward map the same way it closes every page -- the
        // owner's ask names M as the toggle, and Escape is the universal
        // back-out on top of it.
        districtMapOpen_ = false;
        syncPanelAnim();
        return;
    }
    if (casebookOpen_) {
        // MORROWIND ROUND. If the focused Letters tile has a letter open,
        // ESC steps back to its own title list first -- the SAME "a page
        // reached by drilling in gives you one press back to where you
        // were" rule the options page's awaitingKey_ handling above already
        // has, now scoped to one tile instead of the whole surface.
        if (menuFocus_ == kMenuFocusLetters && lettersEntry_ >= 0) {
            lettersEntry_ = -1;
            lettersBodyPage_ = 0;
            syncPanelAnim();
            return;
        }
        // OTHERWISE ESC CLOSES THE WHOLE TILED MENU, all four tiles at once
        // -- they were never four separate pages to step back out of one at
        // a time, only one surface with four panes on it (see
        // toggleCasebook()'s own header).
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
        syncPanelAnim();
        return;
    }
    // VOICE LANE: the back key says goodbye too. Leaving is a transition
    // and not a cut, whichever key ends it.
    endTalk(false);
}

sim::Reply Session::chooseWardTopic(std::size_t index) {
    sim::DialogueDirector& talk = tavern_->dialogue();
    talk.setPlayerCoin(tavern_->playerCoin());
    sim::Reply reply = talk.choose(index);
    if (!reply.ok) {
        if (reply.closes || !talk.isOpen()) {
            wardTalkId_ = -1;
        }
        return reply;
    }
    if (reply.kind == sim::TopicKind::BuyItem || reply.kind == sim::TopicKind::SellItem) {
        settleCounterReply(reply);
        return reply;
    }

    // A hand in a purse takes coin off a REAL body, and the coin the ward loses
    // is the coin the player gains. Nothing is minted: the ledger's identity is
    // about creation, and a robbery creates nothing.
    if (reply.kind == sim::TopicKind::PickPocket && !reply.offence && reply.coinDelta > 0) {
        reply.coinDelta = people_->takeCoinFrom(wardTalkId_, reply.coinDelta);
        if (reply.coinDelta <= 0) {
            reply.line = "THEIR PURSE IS EMPTY.";
        }
    }
    if (reply.coinDelta != 0) {
        tavern_->setPlayerCoin(std::max(0, tavern_->playerCoin() + reply.coinDelta));
        talk.setPlayerCoin(tavern_->playerCoin());
    }
    // WHETHER ANYBODY SAW IT is a question about the street, and the street has
    // no walls, no bouncer and no door policy. So it is asked the only way it
    // honestly can be out here: were there other living people standing close
    // enough on the same band. The Watch's heat, the roofs' opinion and the
    // guilds' all move through the one call site every criminal act in this
    // build goes through, whether it happened in a taproom or on the Tarwalk.
    if (reply.criminal) {
        const bool watched = people_->witnessesAround(wardTalkId_, kWardWitnessTiles) > 0;
        talk.noteCrime(reply.crime, watched || reply.offence);
    }
    if (reply.closes || !talk.isOpen()) {
        wardTalkId_ = -1;
    }
    return reply;
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
        sayAs(who, reply.line, reply.report);
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
        sayAs(who, reply.line, reply.report);
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
        sayAs(who, reply.line, reply.report);
    }
    haggleOffer_ = 0;
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

DialogueViewState Session::dialogueView() const {
    DialogueViewState view;
    // SHIP NOTE MOVE 3. Assembled fresh every frame, so every wording below
    // switches live the moment the other hand speaks. `confirm`/`back` are
    // the page grammar main.cpp's router hard-codes (ENTER/A, ESC/B); the
    // keyboard strings are character-for-character what this function always
    // printed, so a keyboard session draws byte-identical frames.
    const InputDevice dev = promptDevice_;
    const std::string confirm(promptConfirmKey(dev));
    const std::string back(promptBackKey(dev));
    view.confirmKey = confirm;
    view.backKey = back;
    // NINE AND THE STICKS: the digit column is a keyboard's. A pad walks a
    // list on the D-pad and confirms on A; a printed `6` beside a topic is a
    // key it does not have.
    view.showDigits = dev != InputDevice::Pad;
    // The haggle's third verb rides Action::PageNext in the router ("]"
    // carries no glyph in the 4x6 font; T is the advertised key and now
    // routed too -- see main.cpp's haggle branch); the pad's half is RB.
    if (dev == InputDevice::Pad) {
        view.takeKey = std::string(promptAltCommitKey(dev));
        // The pad has no L; its B BACK already names the way out of a letter.
        view.letterDownLine.clear();
    }
    // The panel's own small motion -- the picked row's highlight breathes
    // with it. The identical role body_->stepCount()/60 already plays for the
    // lamp flicker in drawFrame: a pure function of simulated steps, so a
    // scripted capture still draws the same frame every time it is asked to,
    // and it costs nothing on every page this widget is reused for.
    view.phase = static_cast<float>(body_->stepCount()) / 60.0F;
    if (waitOpen_) {
        // TIME-AND-TENURE BUILD. The same one list widget every page is. The
        // top band carries the ruling's own distinction -- and, in wait mode,
        // the refusal when this spot is not safe, so the page and the pick
        // name the same door before a key is ever pressed.
        view.open = true;
        view.speaker = waitSleep_ ? "SLEEP" : "WAIT";
        view.epithet = confirm + " PASSES THE HOURS  " + back + " BACKS OUT";
        if (waitSleep_) {
            view.line = "THE BED IS PAID FOR. SLEEP MENDS. PICK THE HOUR TO WAKE.";
        } else {
            const std::string refusal = waitRefusal();
            view.line = refusal.empty()
                            ? "TIME PASSES AND NOTHING MENDS. THE DISTRICT KEEPS ITS "
                              "HOURS, AND THE WATCH KEEPS FORGETTING."
                            : refusal;
        }
        for (const std::string& row : waitRows()) {
            view.topics.push_back(row);
        }
        view.cursor = waitCursor_;
        view.page = waitPage_;
        return view;
    }
    if (pauseOpen_) {
        view.open = true;
        view.speaker = "MENU";
        // The resume key is Pause's OWN binding -- ESC, or START, the key
        // that opened the page (this file's "the key that opened it closes
        // it" rule); the cancel on an armed QUIT is the universal back (B on
        // a pad -- see route_menu_key's parity-pass remap).
        view.epithet =
            quitArmed_ ? confirm + " QUITS  " + back + " CANCELS"
                       : confirm + " SELECTS  " +
                             std::string(promptLabel(controls_, Action::Pause, dev)) + " RESUMES";
        // NOT "PAUSED", DELIBERATELY. This page does not stop PhasedEngine --
        // nothing in this build does, not the casebook, not the keys page, not
        // options, and a menu that promised a freeze the game does not deliver
        // would be exactly the class of bug the copy bar exists to catch. Said
        // once, here, where a player opening this for the first time reads it.
        view.line = "THE DOCKS DO NOT WAIT ON YOU. SETTINGS KEEP THEMSELVES.";
        for (const std::string& row : pauseRows()) {
            view.topics.push_back(row);
        }
        view.cursor = pauseCursor_;
        view.page = 0;
        return view;
    }
    if (keysOpen_) {
        view.open = true;
        view.speaker = "CONTROLS";
        // AND THE BUILD, HERE, WHERE A PLAYER GOES LOOKING FOR IT. The version
        // used to be burnt into the top-left corner of every captured frame at
        // full HUD scale, which is prime screen real estate spent on something
        // nobody needs more than once. F1 is the page you open when you want to
        // know how this works; what build it is belongs on it.
        view.epithet = "GRANADAD: THE DARKSTREETS  " + std::string(sim::build_info().version);
        // THREE LINES IS WHAT THE TOP BAND WRAPS TO, so this is written to fit
        // in two. The first version ran to four and lost its own last sentence.
        // NINE AND THE STICKS: no F1 any more -- both hands came in through
        // the pause menu's CONTROLS row and back out on the universal back.
        view.line = "THE DOCKS OF GRANADAD. THE DISTRICT KEEPS ITS OWN HOURS. " + back +
                    " PUTS THIS DOWN.";
        for (const std::string& row : keyRows()) {
            view.topics.push_back(row);
        }
        view.cursor = caseCursor_;
        view.page = casePage_;
        return view;
    }
    if (searchOpen()) {
        // KIT BUILD. The corpse's kit on the same widget: his name on the
        // badge, the verbs on the epithet, TAKE ALL as the last row.
        view.open = true;
        const sim::Actor* corpse = tavern_->actorById(searchActorId_);
        view.speaker = corpse != nullptr ? upperAscii(corpse->name()) : std::string("THE DEAD");
        view.epithet = confirm + " TAKES  " + back + " LEAVES HIM";
        const std::vector<std::string> rows = grimoireRows();
        view.line = rows.empty() ? "NOTHING ON HIM." : "WHAT HE HAD ON HIM. DR IS DRAMS.";
        for (const std::string& row : rows) {
            view.topics.push_back(row);
        }
        view.cursor = grimoireCursor_;
        view.page = grimoirePage_;
        return view;
    }
    if (grimoireOpen_) {
        // SPELLS BUILD. The same one list widget every page is -- see
        // toggleGrimoire()'s own header. The rows carry the difficulty out of
        // the cost model beside every name: what the linkcraft check will be
        // rolled against, which is information, never a discount.
        view.open = true;
        view.speaker = "GRIMOIRE";
        // LEFT RIGHT is honest on both devices -- the D-pad IS the pad's
        // left and right on a list; only the commit verb changes hands.
        view.epithet = "LEFT RIGHT BIND A SLOT  " + confirm + " READIES";
        const std::vector<std::string> rows = grimoireRows();
        if (rows.empty()) {
            // THE COMMON STATE, in the cast refusal's own words: the page and
            // the C key must name the same door or one of them is lying.
            view.line = "NO CRAFTING HELD. THE PRIEST OF THE FLAME TEACHES.";
        } else {
            view.line =
                "EVERY CRAFTING THE HAND KNOWS. D IS WHAT THE LINK ASKS. A SLOT PUTS "
                "IT ON THE NUMBER ROW.";
        }
        for (const std::string& row : rows) {
            view.topics.push_back(row);
        }
        view.cursor = grimoireCursor_;
        view.page = grimoirePage_;
        return view;
    }
    if (casebookOpen_) {
        // MORROWIND ROUND: WHICHEVER TILE CURRENTLY HAS FOCUS. dialogueView()
        // used to check four mutually-exclusive xOpen_ bools in turn; now all
        // four tiles are open together (casebookOpen_ is the one flag), so
        // the dispatch is by menuFocus_ instead -- a caller that only ever
        // asked "what is the open page showing" (every test written before
        // this round) keeps seeing exactly the content it always did,
        // because the Menu still opens focused on the Journal tile by
        // default, same as #85's Menu always opened on the casebook first.
        DialogueViewState tile;
        switch (menuFocus_) {
            case kMenuFocusCharacter:
                tile = characterPanelView();
                break;
            case kMenuFocusMap:
                tile = mapPanelView();
                break;
            case kMenuFocusLetters:
                tile = lettersPanelView();
                break;
            case kMenuFocusJournal:
            default:
                tile = journalPanelView();
                break;
        }
        // SHIP NOTE MOVE 3: the tile views build their own state, so the
        // device wording set at the top of this function has to be restated
        // on the one that is actually returned -- an open letter's foot in
        // particular (backKey/letterDownLine).
        tile.confirmKey = confirm;
        tile.backKey = back;
        if (dev == InputDevice::Pad) {
            tile.takeKey = std::string(promptAltCommitKey(dev));
            tile.letterDownLine.clear();
        }
        return tile;
    }
    if (optionsOpen_) {
        view.open = true;
        view.speaker = "OPTIONS";
        // The awaiting line stays "ESC CANCELS" on BOTH devices, deliberately:
        // the capture is raw (main.cpp's bindAwaited eats the next key ahead
        // of every remap), so a pad's B would be CAPTURED as the binding, and
        // ESC really is the one cancel there is.
        view.epithet = awaitingKey_ ? (dev == InputDevice::Pad ? "PRESS A KEY  (B CANCELS)"
                                                                : "PRESS A KEY  (ESC CANCELS)")
                                    : "LEFT RIGHT CHANGE  " + confirm + " REBIND";
        view.line = "MOUSE LOOK IS RAW -- NO SMOOTHING, NO ACCELERATION. A KEY YOU BIND IS TAKEN "
                    "OFF WHATEVER HAD IT. " +
                    back + " PUTS THIS DOWN.";
        for (const std::string& row : optionRows()) {
            view.topics.push_back(row);
        }
        view.cursor = optionCursor_;
        view.page = optionPage_;
        return view;
    }
    const sim::DialogueDirector& talk = tavern_->dialogue();
    if (!talk.isOpen()) {
        return view;
    }
    view.open = true;
    view.speaker = talk.speaker().name;
    view.epithet = talk.speaker().epithet;
    view.attitude = std::string(sim::attitudeName(talk.attitude()));
    view.line = talk.lastLine();
    // THE TYPEWRITER. speechAgeSteps_ was reset to 0 the step this exact line
    // first became lastLine() -- see step()'s own note -- so this ramps 0..all
    // of it over kSpeechRevealSteps and then reports -1 (all of it, no budget
    // to track) for as long as the line stands, which is most of a
    // conversation's life. Nothing here changes what talk.lastLine() IS; only
    // how much of it the top band has been told to draw this frame.
    view.speechRevealChars =
        speechAgeSteps_ >= kSpeechRevealSteps
            ? -1
            : static_cast<int>((static_cast<std::int64_t>(view.line.size()) * speechAgeSteps_) /
                                kSpeechRevealSteps);
    view.cursor = topicCursor_;
    view.page = topicPage_;
    // VOICE LANE, THE ADDRESS: the worded reaction on the dateline row, in
    // the dim ink reference material takes, while it stands.
    if (reactionSteps_ > 0 && !reactionLine_.empty()) {
        view.caseRef = reactionLine_;
    }
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

namespace {

/// A landed blow of this many hit points or fewer is a GRAZE -- the light
/// audio band. Fists tap for 3-5 at the base sheet, so this catches the
/// glancing end of a bare-handed swing and lets a solid or hard blow land
/// medium instead. Splits the plan's single PunchMedium by band (section 5,
/// channel 4); presentation-only, no sim number depends on it.
constexpr std::int32_t kGrazeDamageBand = 4;

/// BARKS LANE: the gain of the graze that stands in for a found-body whiff
/// (attackUp). Quieter than any landed blow, so the ear ranks it below one.
constexpr float kFoundBodyWhiffGain = 0.35F;

}  // namespace

void Session::attackDown() {
    if (refusedInCustody()) {
        return;
    }
    // The down-edge. A Punch watch-op is recorded here (case_watch replays it
    // as punch()); dismissOverlays and the sim's hold clock start exactly where
    // the legacy punch() started them.
    recordWatchOp(WatchOpKind::Punch);
    const WatchDepthGuard watchGuard(watchDepth_);
    dismissOverlays();
    tavern_->playerAttackDown();
}

void Session::attackUp() {
    if (refusedInCustody()) {
        return;
    }
    const WatchDepthGuard watchGuard(watchDepth_);
    // STREET SENSES leg (b) -- THE STREET PICK, the one client hook in the
    // attack path. ONE RULE, TWO ROSTERS: the street's ray is cast with the
    // identical integer projection the Gull's is (WardPopulation::
    // sightlineTarget), and the nearer body ALONG the ray takes the blow --
    // a docker a tile in front of you is hit before a patron three tiles
    // behind him through the Gull's door. The swing itself is the room's
    // (playerAttackUpStreet: the same head, the same one roll, strike(),
    // classifyFight, the deeds, the murder law); the street only supplies the
    // sheet and takes the answer back (applyStreetBlow: the floor, the death,
    // the rout, the fight-back, the crowd). Witnesses are counted BEFORE the
    // body drops, at the Gull's own witness range, so N SAW IT at the bench
    // is the count of who could see it -- the Watch included.
    sim::Tavern::PlayerSwingResult result;
    const sim::WardActor* street = nullptr;
    std::int64_t streetAlong = -1;
    if (people_ != nullptr) {
        street = people_->sightlineTarget(body_->x(), body_->y(), body_->band(), body_->yaw(),
                                          &streetAlong);
    }
    const std::int64_t roomAlong = tavern_->playerSightlineAlong();
    if (street != nullptr && (roomAlong < 0 || streetAlong < roomAlong)) {
        const std::int32_t wardId = street->id;
        sim::Fighter sheet;
        sheet.actorId = sim::kWardSpeakerIdBase + wardId;
        sheet.weapon = sim::Weapon::Fists;
        sheet.intent = sim::Intent::Subdue;
        sheet.hp = street->hp;
        sheet.hpMax = sim::kActorHealth;
        const std::int32_t witnesses = people_->witnessesInSight(
            street->x, street->y, street->band, sim::kWatchSightTiles, wardId);
        result = tavern_->playerAttackUpStreet(sheet, wardId, people_->identity(wardId).name,
                                               witnesses);
        if (result.swung && result.targetId >= 0) {
            (void)people_->applyStreetBlow(wardId, sheet.hp, result.blow, result.lethal);
            // THE ROUTED DOCKER'S LINE (the street's own row, salted register):
            // a man struck and still standing who is not a fighter and not the
            // Watch has just decided to run, and says so once on the alert row.
            // Presentation only: a string, unhashed.
            if (result.blow.landed && !result.blow.downed && !result.killed &&
                !sim::wardTypeFightsBack(street->type)) {
                const sim::BarkTables& barks = tavern_->dialogue().barks();
                const std::string_view key =
                    barks.resolve({std::string("street.routed"), std::string("crowd.flee")});
                if (!key.empty()) {
                    sayAs(people_->identity(wardId).name,
                          std::string(barks.line(key, static_cast<std::int32_t>(elapsedSeconds_ % 1024) + wardId)),
                          false);
                }
            }
        }
    } else {
        result = tavern_->playerAttackUp();
    }
    if (result.refused) {
        // A hard swing the wind would not buy. A winded TAP still swings (its
        // fatigue whiff band is the penalty), so a refusal is only ever the
        // hard tier saying so, once, on the alert row.
        say("TOO WINDED TO SWING HARD.");
        return;
    }
    if (!result.swung) {
        // A release with no charge behind it -- an edge that arrived in
        // recovery or idle. It throws nothing and says nothing.
        return;
    }
    // A committed HARD swing dips the camera forward on release -- render-only,
    // composed in camera() and never written to sim yaw.
    if (result.hard) {
        hardSwingDipPulse_.trigger();
    }
    // 3D BUILD, V LANE. The hands throw the swing the room just resolved,
    // on the next step, at the tier the room read off the hold.
    viewmodelSwingPending_ = static_cast<std::uint8_t>(result.hard ? 2 : 1);
    // THE LOT PASS: the HARD release has its own air (whoosh_strong / swing
    // sword) under whatever it finds -- a body or nothing; a tap at nothing
    // is the plain whoosh set. Spoken before the band below, so the ear gets
    // swing-then-impact in that order on a hard hit.
    if (audio_ != nullptr && result.hard) {
        audio_->playOneShot(audio::SoundId::WhooshHard);
    }
    if (result.targetId < 0) {
        // Committed and paid, but the crosshair passed through nobody. The arm
        // still swung, so the wind is spent and the swing gets its air.
        say("NOBODY IN REACH.");
        if (audio_ != nullptr) {
            if (!result.hard) {
                audio_->playOneShot(audio::SoundId::Whoosh);
            }
            // A blow thrown keeps the combat music alive; it starts nothing.
            audio_->music().noteSwing();
        }
        return;
    }
    // SAY-ROW DIET (section 5, channel 3). The per-blow "HIT X FOR N."/"MISSED
    // X." log is RETIRED -- a landed, taken or blocked blow speaks through the
    // washes, the reticle and the audio below, not the alert row. The row keeps
    // only EVENTS and REFUSALS: a kill, a crowning, a down. The steel-out flip
    // and its SwordDraw are spoken once on the escalation EDGE in step(), not
    // per swing, so a fight that stays lethal does not re-announce itself.
    if (result.killed) {
        // A killing blow under lethal rules -- the register literal, veto-blessed
        // (section 5). Names the man the boards just took.
        say(result.targetName + " DIES ON THE BOARDS.");
    } else if (result.blow.crowned) {
        // THE EVICTOR'S OWN LINE. crowned is downed by construction and never
        // kills (it was forged to put a man OUT), so it reads before the plain
        // fall and only this weapon ever throws one.
        say("CAUGHT " + result.targetName + " ACROSS THE CROWN. OUT COLD.");
    } else if (result.blow.downed) {
        say(result.targetName + " GOES DOWN.");
    }
    // AUDIO WIRING (section 5, channel 4). One sound per swing, by blow band:
    // the helmet on a crowning, the heavy on a kill or a knockdown, the light
    // graze on the low damage band and the medium above it (the plan's single
    // PunchMedium split), and the whoosh for a swing that found a body but
    // glanced off nothing worth a mark (a fully absorbed hit). SwordDraw is the
    // Lethal FLIP, wired on the escalation edge in step(), not here.
    if (audio_ != nullptr) {
        // THE LOT PASS re-cut the bands over the Malbers set: hit_grave for
        // the blow that ends a man (a crowning or a kill), the heavy band
        // plus fall_land (ThudHeavy) for a body going down, the clash for a
        // guard that caught it, the medium band for a blow that landed.
        if (result.blow.crowned || result.killed) {
            audio_->playOneShot(audio::SoundId::HitGrave);
        } else if (result.blow.downed) {
            audio_->playOneShot(audio::SoundId::PunchHeavy);
            audio_->playOneShot(audio::SoundId::ThudHeavy);
        } else if (result.blow.blocked) {
            audio_->playOneShot(audio::SoundId::SwordClash);
        } else if (result.blow.landed) {
            audio_->playOneShot(result.blow.damage <= kGrazeDamageBand
                                    ? audio::SoundId::GrazeLight
                                    : audio::SoundId::PunchMedium);
        } else {
            // BARKS LANE (feel/build) -- THE HONEST WHIFF. The reticle
            // promised a body on the line and the die (1-in-8, plus the
            // fatigue band -- kept, the owner's ruling) said the arm found
            // nothing worth a mark. That is NOT the same sound as swinging at
            // air (Whoosh, the targetId < 0 branch above): the fist brushed a
            // coat. The light graze at a third of its gain and no wash --
            // presentation only, zero law contact.
            audio_->playOneShot(audio::SoundId::GrazeLight, kFoundBodyWhiffGain);
        }
        // THE LOT PASS: a blow LANDED is the fight's first edge for the
        // music; a whiff on a body only keeps a running fight alive.
        if (result.blow.landed) {
            audio_->music().noteCombat();
        } else {
            audio_->music().noteSwing();
        }
    }
    // A LANDED SWING FINALLY HAS SOME WEIGHT -- the connecting wash. A whiff on
    // a body that was on the line stays silent on the wash, exactly as a miss
    // always did: this is punctuation for a hit, not for the swing.
    if (result.blow.landed) {
        punchLandedPulse_.trigger();
    }
}

void Session::punch() {
    if (refusedInCustody()) {
        return;
    }
    // ACTION-COMBAT BUILD: a TAP of the new verbs -- down then immediately up
    // with no movement step between, so the sim's charge counter never leaves
    // zero and the swing is always the light (Subdue) tier. The six un-migrated
    // call sites (the scripted drives, case_watch, the render suite) drive real
    // sightline combat through this, the same path a mouse press will.
    attackDown();
    attackUp();
}

void Session::castEquipped() {
    if (refusedInCustody()) {
        return;
    }
    dismissOverlays();
    if (talking() || picking()) {
        // The keyboard is a topic list's (or a lock's) right now. Inert, the
        // same way setCrouched already stands down -- a link opened from
        // inside a conversation would be a fact nobody saw being made.
        return;
    }
    // The room resolves the whole of it -- every refusal included -- and
    // always answers with a line, so the key can never silently do nothing.
    // See sim::Tavern::playerCastEquipped()'s own header.
    const std::int64_t coolingBefore = tavern_->castCooldownLeft();
    const sim::Tavern::CastResult result = tavern_->playerCastEquipped();
    say(result.line);
    // 3D BUILD, V LANE. The hand reaches out when the room let it TRY: a
    // link that opened, or one that slipped (a fizzle still starts the
    // recovery clock -- the comparison-not-flag shape lastPlayerHp_ uses,
    // so no sim field is added for a gesture). A refusal moves nothing.
    if (result.cast || tavern_->castCooldownLeft() > coolingBefore) {
        viewmodelCastPending_ = true;
    }
    // NO NEW PULSE, DELIBERATELY. The restraint note in drawFrame() stands:
    // two real moments got a flash and a cast is not being bolted on as a
    // third -- a landed harmful cast already reads on the target and the
    // alert row, and the punch-taken/blocked pair stays the whole of the
    // wash vocabulary.
}

void Session::setBlocking(bool held) {
    // The EDGE only. What the room is told is derived in step(), every step
    // -- see the comment there on why (a page can open or close mid-hold,
    // and no edge event exists for either moment).
    blockHeld_ = held;
}

void Session::applyPlayerAttributes(const sim::AttributeBlock& attributes) {
    // BOTH HALVES OR NEITHER -- see the declaration. The room takes the
    // sheet (and sizes the fatigue pool off it); the body takes the one
    // reader the room cannot hold for it, AGI's gait multiplier. At the
    // base-40 sheet both halves are exactly the shipped behaviour.
    tavern_->setPlayerAttributes(attributes);
    body_->setSpeedScaleQ8(
        sim::agilitySpeedScaleQ8(attributes.value(sim::AttributeId::Agility)));
}

void Session::restHere() {
    if (refusedInCustody()) {
        return;
    }
    dismissOverlays();
    const sim::ServiceResult slept = tavern_->sleep();
    if (slept == sim::ServiceResult::Served) {
        settleSleep();
        return;
    }
    say(std::string(sim::restRefusal(slept)));
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

const sim::Tavern::SentenceReport& Session::serveSentence() {
    // JUSTICE BUILD (SENTENCES LANE). A sentence is a clock jump with a
    // record behind it, and it rides the identical pair every other jump
    // does: the room's own verb, then syncClockAfterSkip() -- the ward's
    // roll to dayNumber() (a day in a cell is a day the land grows, the wage
    // is paid and, on a quarter, the ground penny falls) and the people to
    // the clock. The room fires the arrest's release for the body; step()
    // reads it exactly as it reads the release after the arrest. THE ROPE
    // jumps nothing and releases nothing, so nothing is synced: the clock is
    // where it was, and the end is on tavern().runEnd().
    const sim::Tavern::SentenceReport& served = tavern_->serveSentence();
    if (served.served && !served.terms.rope) {
        syncClockAfterSkip();
    }
    return served;
}

namespace {

// ---------------------------------------------------------------------------
// JUSTICE BUILD (HEARING PAGE LANE): the court, the rope, the end
// ---------------------------------------------------------------------------
//
// Spec sections 5 and 6, as presentation over the SIM the two lanes before
// this one built: the hearing on the crime ledger (justice.hpp's HearingState,
// hashed and codec'd), Tavern::plead as the stepped input, Tavern::serveSentence
// as the clock and the coin, tavern().runEnd() as the end. Nothing here rolls,
// nothing here decides; this file reads the record, draws it in the terminal
// register, and turns keypresses into the two stepped inputs the sim owns.
/// The count words the reading uses: "TWO LIFTS", "THREE SAW IT". One is the
/// article ("A LIFT"), so it is the caller's; past twelve the figure is the
/// word, as the census's own tallies are.
[[nodiscard]] std::string countWord(std::int32_t n) {
    static constexpr std::string_view kWords[] = {
        "",     "ONE", "TWO",   "THREE", "FOUR",  "FIVE",   "SIX",
        "SEVEN", "EIGHT", "NINE", "TEN", "ELEVEN", "TWELVE",
    };
    if (n >= 1 && n <= 12) {
        return std::string(kWords[n]);
    }
    return std::to_string(n);
}

/// "THE FOURTH DAY" -- the ordinal the plate reads. Day one, not day zero:
/// formatCaseDay's own translation, applied to the ordinal here. Past what
/// the words carry the plate says the figure, which is still the ward's roll.
[[nodiscard]] std::string ordinalWord(std::int32_t day) {
    static constexpr std::string_view kOnes[] = {
        "",        "FIRST",   "SECOND",     "THIRD",      "FOURTH",    "FIFTH",     "SIXTH",
        "SEVENTH", "EIGHTH",  "NINTH",      "TENTH",      "ELEVENTH",  "TWELFTH",   "THIRTEENTH",
        "FOURTEENTH", "FIFTEENTH", "SIXTEENTH", "SEVENTEENTH", "EIGHTEENTH", "NINETEENTH",
    };
    static constexpr std::string_view kTens[] = {
        "", "", "TWENTY", "THIRTY", "FORTY", "FIFTY", "SIXTY", "SEVENTY", "EIGHTY", "NINETY",
    };
    static constexpr std::string_view kTensOrdinal[] = {
        "",         "",          "TWENTIETH", "THIRTIETH", "FORTIETH",
        "FIFTIETH", "SIXTIETH",  "SEVENTIETH", "EIGHTIETH", "NINETIETH",
    };
    if (day >= 1 && day < 20) {
        return std::string(kOnes[day]);
    }
    if (day >= 20 && day < 100) {
        const int tens = day / 10;
        const int ones = day % 10;
        if (ones == 0) {
            return std::string(kTensOrdinal[tens]);
        }
        return std::string(kTens[tens]) + "-" + std::string(kOnes[ones]);
    }
    return std::to_string(day) + "TH";
}

/// The six acts, in the reading's own words: "A LIFT" / "TWO LIFTS", "A
/// CRACKED BOX" / "TWO CRACKED BOXES". The tally words crime.hpp's own
/// crimeTally uses (lifts, cracks, runs, fences, leans, roofs), spoken.
[[nodiscard]] std::string crimePhrase(sim::Crime crime, std::int32_t count) {
    const bool one = count == 1;
    const std::string n = one ? std::string("A") : countWord(count);
    switch (crime) {
        case sim::Crime::Lift:
            return n + (one ? " LIFT" : " LIFTS");
        case sim::Crime::Burgle:
            return n + (one ? " CRACKED BOX" : " CRACKED BOXES");
        case sim::Crime::Smuggle:
            return n + (one ? " RUN" : " RUNS");
        case sim::Crime::Fence:
            return n + (one ? " FENCED LOT" : " FENCED LOTS");
        case sim::Crime::Extort:
            return n + (one ? " LEAN" : " LEANS");
        case sim::Crime::RoofRun:
            return n + (one ? " ROOF-RUN" : " ROOF-RUNS");
    }
    return n;
}

/// Which rows the page prints, by kind, so the cursor and the digits and the
/// confirm all read one list.
enum class CourtRowKind : std::uint8_t { Guilty, NotGuilty, Paper, Back, NoPlea, Serve };

[[nodiscard]] std::vector<CourtRowKind> courtRowKindsFor(const sim::HearingState& hearing,
                                                          bool paperOpen, bool offered) {
    std::vector<CourtRowKind> kinds;
    if (hearing.awaitingPlea()) {
        if (hearing.sheet.tier == sim::Sentence::Condemned && hearing.sheet.commutedBefore) {
            // MERCY IS GIVEN ONCE. The page opens, the priest speaks, and
            // the one row is I HAVE NOTHING TO SAY.
            kinds.push_back(CourtRowKind::NoPlea);
            return kinds;
        }
        kinds.push_back(CourtRowKind::Guilty);
        kinds.push_back(CourtRowKind::NotGuilty);
        kinds.push_back(CourtRowKind::Paper);
        if (paperOpen) {
            kinds.push_back(CourtRowKind::Back);
        }
        return kinds;
    }
    if (hearing.judged() && offered) {
        kinds.push_back(CourtRowKind::Serve);
    }
    return kinds;
}

/// The judgment as the badge prints it.
[[nodiscard]] std::string judgmentWord(sim::Judgment judgment) {
    switch (judgment) {
        case sim::Judgment::Spared:
            return "SPARED";
        case sim::Judgment::Fined:
            return "FINED";
        case sim::Judgment::Held:
            return "HELD";
        case sim::Judgment::Bound:
            return "BOUND";
        case sim::Judgment::TheHand:
            return "THE HAND";
        case sim::Judgment::Commuted:
            return "COMMUTED";
        case sim::Judgment::TheRope:
            return "THE ROPE";
        case sim::Judgment::None:
            break;
    }
    return {};
}

/// The verdict badge's own accent: SPARED in the number green, the coin and
/// the cell in the page accent, the hand and the rope in the tag's red.
[[nodiscard]] Rgb judgmentAccent(sim::Judgment judgment) {
    switch (judgment) {
        case sim::Judgment::Spared:
            return panelInk().number;
        case sim::Judgment::TheHand:
        case sim::Judgment::Commuted:
        case sim::Judgment::TheRope:
            return Rgb{0.88F, 0.34F, 0.26F};
        default:
            return panelInk().accent;
    }
}

/// The one row after the judgment: what the player does about it. The drop
/// is taken by the player's own hand, like everything else on this page.
[[nodiscard]] std::string serveWord(sim::Judgment judgment) {
    switch (judgment) {
        case sim::Judgment::Spared:
            return "WALK OUT.";
        case sim::Judgment::Fined:
            return "PAY IT.";
        case sim::Judgment::TheRope:
            return "THE DROP.";
        default:
            return "SERVE IT.";
    }
}

/// "TWO NIGHTS." / "SIX NIGHTS." off the cell's hours -- whole nights, one at
/// least: the shipped one-to-three nights doubled reads as its days.
[[nodiscard]] std::string nightsWord(std::int32_t cellHours) {
    const std::int32_t nights = std::max<std::int32_t>(1, cellHours / 24);
    return countWord(nights) + (nights == 1 ? " NIGHT." : " NIGHTS.");
}

[[nodiscard]] std::string daysWord(std::int32_t days) {
    return "BONDSWORN " + std::to_string(days) + (days == 1 ? " DAY." : " DAYS.");
}

/// THE SENTENCE, IN NUMBERS -- the last row of the check block, the way
/// "HELD HARD -- CUDGEL 14-18" states its span. Off the pure branch.
[[nodiscard]] std::string sentenceLine(const sim::SentenceTerms& terms) {
    if (!terms.served) {
        return {};
    }
    std::string out;
    const auto coin = [&terms]() { return std::to_string(terms.finePaid) + " ROYALS."; };
    switch (terms.judgment) {
        case sim::Judgment::Spared:
            return "NOTHING OWED.";
        case sim::Judgment::Fined:
            out = coin();
            break;
        case sim::Judgment::Held:
            out = nightsWord(terms.cellHours) + " " + coin();
            break;
        case sim::Judgment::Bound:
            return daysWord(terms.bondDays);
        case sim::Judgment::TheHand:
            out = "THE HAND. ";
            if (terms.band == sim::Judgment::Bound) {
                return out + daysWord(terms.bondDays);
            }
            out += nightsWord(terms.cellHours) + " " + coin();
            break;
        case sim::Judgment::Commuted:
            return "THE HAND. " + daysWord(terms.bondDays);
        case sim::Judgment::TheRope:
            // Nothing in numbers here: the rope's row is the post and the
            // hour, which the page composes off the clock (hearingPageState)
            // -- the badge already says THE ROPE and the row does not echo it.
            return {};
        case sim::Judgment::None:
            return {};
    }
    if (terms.shortfallDays > 0) {
        out += " " + daysWord(terms.shortfallDays);
    }
    return out;
}

/// The priest's own words for the answer -- the bark tables keyed like the
/// shipped watch.*, rotated on the count of hearings so a second visit to the
/// bench hears a second row.
[[nodiscard]] std::string_view judgmentTable(sim::Judgment judgment) {
    switch (judgment) {
        case sim::Judgment::Spared:
            return "court.spared";
        case sim::Judgment::Fined:
            return "court.fined";
        case sim::Judgment::Held:
            return "court.held";
        case sim::Judgment::Bound:
            return "court.bound";
        case sim::Judgment::TheHand:
            return "court.hand";
        case sim::Judgment::Commuted:
            return "court.commuted";
        case sim::Judgment::TheRope:
            return "court.rope";
        case sim::Judgment::None:
            break;
    }
    return "court.paper";
}

/// The consequence of the hovered row, in the pane beside it -- the
/// informed choice (UI-REFERENCE: "View X's stats"). Fixed literals.
[[nodiscard]] std::string rowConsequence(CourtRowKind kind, sim::Sentence tier) {
    // EACH TIER'S LITERAL SAYS ONLY WHAT IS ON THE TABLE: the rope tier has
    // two answers and names them; a cell or a hand charge has no rope on the
    // table and does not mention one. A confession is never doubled and never
    // spared on any tier; a denial can be either, on the tiers that have
    // either.
    const bool rope = tier == sim::Sentence::Condemned;
    switch (kind) {
        case CourtRowKind::Guilty:
            return rope ? "A CONFESSION IS WEIGHED AS IT IS GIVEN. THE FLAME'S ANSWER IS FIXED "
                          "BEFORE YOU SPEAK IT. MERCY OR THE ROPE, AND NOTHING ELSE."
                        : "A CONFESSION IS WEIGHED AS IT IS GIVEN. THE FLAME'S ANSWER IS FIXED "
                          "BEFORE YOU SPEAK IT. NEVER DOUBLED, AND NEVER SPARED.";
        case CourtRowKind::NotGuilty:
            return rope ? "A DENIAL IS WEIGHED WITH THE PRIEST'S OWN DOUBT IN IT. TEN POINTS "
                          "EITHER WAY. MERCY OR THE ROPE, AND NEVER SPARED."
                        : "A DENIAL IS WEIGHED WITH THE PRIEST'S OWN DOUBT IN IT. TEN POINTS "
                          "EITHER WAY. DENIED AND DISBELIEVED, THE SENTENCE DOUBLES AND THE "
                          "MISSION REMEMBERS THE LIE.";
        case CourtRowKind::Paper:
            return "THE CHARGE, WHAT THE PAPER ASKS, AND WHAT THE PRIEST WILL WEIGH -- IN "
                   "WORDS, BEFORE YOU SPEAK.";
        case CourtRowKind::Back:
            return "BACK TO THE ROWS.";
        case CourtRowKind::NoPlea:
            return "MERCY WAS GIVEN ONCE. NOTHING IS WEIGHED: THE PRIEST SPEAKS, AND THE WARD "
                   "HAS YOU.";
        case CourtRowKind::Serve:
            break;
    }
    return {};
}

/// THE SHEET AS PHRASES, NEVER NUMBERS (spec 3.2): what the priest will
/// weigh, in words, before the plea. The arithmetic is shown after.
[[nodiscard]] std::vector<std::string> paperPhrases(const sim::ChargeSheet& sheet) {
    std::vector<std::string> out;
    if (sheet.blood) {
        out.push_back("BLOOD ON THE PAPER.");
        if (sheet.witnesses > 0) {
            out.push_back(countWord(sheet.witnesses) + " SAW IT.");
        }
    }
    if (sheet.secondRung) {
        out.push_back("THE ROOFS, A SECOND TIME.");
    } else if (sheet.skyrunner && sheet.tier == sim::Sentence::Maimed) {
        out.push_back("A SKYRUNNER'S FIRST.");
    }
    if (sheet.priors <= 0) {
        out.push_back("NEVER TAKEN BEFORE.");
    } else if (sheet.priors == 1) {
        out.push_back("TAKEN ONCE BEFORE.");
    } else if (sheet.priors == 2) {
        out.push_back("TAKEN TWICE BEFORE.");
    } else {
        out.push_back("TAKEN " + countWord(sheet.priors) + " TIMES BEFORE.");
    }
    if (sheet.heatAtArrest - sim::kWarrantAt >= 16) {
        out.push_back("THE PAPER IS HOT.");
    }
    if (sheet.templeStanding >= 52) {
        out.push_back("THE MISSION KNOWS YOU WELL.");
    } else if (sheet.templeStanding > 0) {
        out.push_back("THE MISSION KNOWS YOUR NAME.");
    } else {
        out.push_back("THE MISSION DOES NOT KNOW YOU.");
    }
    if (sheet.reputation >= 10) {
        out.push_back("THE WARD THINKS WELL OF YOU.");
    } else if (sheet.reputation <= -10) {
        out.push_back("THE WARD THINKS ILL OF YOU.");
    }
    if (sheet.streetwise >= 20) {
        out.push_back("A QUICK TONGUE.");
    } else if (sheet.streetwise <= 0) {
        out.push_back("NO TONGUE TO SPEAK OF.");
    }
    if (sheet.condemnedBefore) {
        out.push_back("THE ROPE PASSED YOU ONCE.");
    }
    if (sheet.commutedBefore) {
        out.push_back("MERCY WAS GIVEN ONCE.");
    }
    return out;
}

}  // namespace

bool Session::courtSentenceOffered() const noexcept {
    return courtOpen_ && tavern_->hearing().judged() && courtJudgmentHold_ <= 0;
}

std::vector<std::string> Session::courtRows() const {
    std::vector<std::string> out;
    const HearingPageState page = hearingPageState();
    out.reserve(page.rows.size());
    for (const HearingRow& row : page.rows) {
        out.push_back(row.key + " - " + row.label);
    }
    return out;
}

bool Session::missionArrivalTile(std::int32_t& outX, std::int32_t& outY,
                                 std::int32_t& outBand) const {
    // THE MISSION IS ALREADY A TRAVEL TARGET with an arrival tile: the sign's
    // own aim point (the door you knock on) snapped to standable ground on
    // the place's band -- exactly the travel verb's landing rule, so the
    // Watch's walk lands where a walked-to Mission lands.
    const int at = mapPlaceIndex("Mission of the Flame");
    if (at < 0) {
        return false;
    }
    const MapPlace& place = mapPlaces()[static_cast<std::size_t>(at)];
    std::int32_t aimX = 0;
    std::int32_t aimY = 0;
    mapAimPoint(place, body_->tileX(), body_->tileY(), aimX, aimY);
    if (!travelStandable(*tiles_, aimX, aimY, place.band, &outX, &outY)) {
        return false;
    }
    outBand = place.band;
    return true;
}

void Session::openCourt() {
    if (courtOpen_) {
        return;
    }
    // CUSTODY. Whatever the player was doing, they are not doing it: the wire
    // out of the lock, the conversation ended, every page and the pause menu
    // down. The TAKEN line follows and outranks any of theirs.
    if (picking()) {
        tavern_->abandonPick();
    }
    if (talking()) {
        tavern_->endConversation();
        wardTalkId_ = -1;
        topicCursor_ = 0;
        topicPage_ = 0;
        haggleOffer_ = 0;
        forgeOpen_ = false;
    }
    dismissOverlays();
    // THE WALK ELIDED. The body to the Mission's arrival tile through
    // placeBodyAt (a relocation like any other: the room you were taken in
    // must not ghost over the Mission's door), the seam dipped to black --
    // the same cloth every scripted cut wears -- and the arrival line.
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t band = 0;
    if (missionArrivalTile(x, y, band)) {
        placeBodyAt(x, y, band);
    }
    awaitingLanding_ = false;
    syncTavernToBody();
    dressInstantCut();
    courtOpen_ = true;
    courtCursor_ = 0;
    courtPaperOpen_ = false;
    courtArmed_ = -1;
    courtServing_ = false;
    // A hearing already judged (a run reopened between the plea and the
    // sentence) holds its badge the same beat a fresh judgment does.
    courtJudgmentHold_ = tavern_->hearing().judged() ? sim::kJudgmentHoldSteps : 0;
    say("TAKEN TO THE MISSION. " + travelClockText(timeOfDay_) + ".");
    syncPanelAnim();
}

void Session::closeCourt() {
    courtOpen_ = false;
    courtServing_ = false;
    courtArmed_ = -1;
    courtPaperOpen_ = false;
    courtJudgmentHold_ = 0;
    courtCursor_ = 0;
    // TURNED LOOSE. Where the sentence left the body: SPARED and FINED walk
    // out of the Mission's door where they stand; everything that cost a
    // cell ends on the shipped Tarwalk, exactly as the paperless search
    // does. The line prints the day and the clock face the sentence ended
    // on -- day one, not day zero, formatCaseDay's own translation.
    const sim::Tavern::SentenceReport& served = tavern_->lastServed();
    const std::string when = "DAY " + std::to_string(served.dayReleased + 1) + ". " +
                             travelClockText(served.timeReleased) + ".";
    if (served.served && served.terms.releaseHere) {
        say("TURNED LOOSE AT THE MISSION'S DOOR. " + when);
    } else {
        placeBodyAt(sim::gull::kStreetX, sim::gull::kStreetY, sim::gull::kGroundBand);
        awaitingLanding_ = false;
        syncTavernToBody();
        dressInstantCut();
        say("TURNED LOOSE ON THE TARWALK. " + when);
    }
    syncPanelAnim();
}

HearingPageState Session::hearingPageState() const {
    HearingPageState out;
    out.open = courtOpen_;
    if (!courtOpen_) {
        return out;
    }
    const sim::HearingState& hearing = tavern_->hearing();
    const sim::ChargeSheet& sheet = hearing.sheet;
    const sim::CrimeLedger& crimes = tavern_->dialogue().crimes();
    const sim::BarkTables& barks = tavern_->dialogue().barks();
    const InputDevice dev = promptDevice_;
    out.confirmKey = std::string(promptConfirmKey(dev));
    out.backKey = std::string(promptBackKey(dev));
    out.tutor = courtTutor_.value();
    out.commitPulse = commitPulse_.value();
    // THE READOUT: the day and the live clock (UI-EA-SPEC sec. 4 #10), the
    // two facts a man at the bench wants -- how long he has been in the
    // ward, and what hour the sentence starts from.
    out.readout = "DAY " + std::to_string(tavern_->dayNumber() + 1) + "  " +
                  travelClockText(timeOfDay_);

    // --- the reading ------------------------------------------------------
    out.laid = (hearing.officer.empty() ? std::string("THE WATCH") : upperAscii(hearing.officer)) +
               " LAYS THE PAPER ON THE TABLE.";
    if (sheet.blood) {
        const std::string slain = tavern_->slainName();
        // STREET SENSES leg (b): WHERE. A killing on the quay is read as the
        // street, not the Gull's boards.
        out.charge = "THE WARD SAYS YOU PUT " + (slain.empty() ? std::string("A MAN") : upperAscii(slain)) +
                     (tavern_->slainOnStreet() ? " DOWN IN THE STREET." : " DOWN IN THE GILDED GULL.");
        if (sheet.witnesses > 0) {
            out.charge += " " + countWord(sheet.witnesses) + " SAW IT.";
        }
    } else {
        // Everything new since the bench last heard you, in the ledger's own
        // order -- "TWO LIFTS AND A CRACKED BOX" reads the way the sheet is
        // written; the worst line is what the paper ASKS off, not a sorting.
        std::vector<std::string> parts;
        for (std::size_t c = 0; c < sim::kSheetCrimes; ++c) {
            if (sheet.since[c] <= 0) {
                continue;
            }
            parts.push_back(crimePhrase(static_cast<sim::Crime>(c), sheet.since[c]));
        }
        if (parts.empty()) {
            out.charge = sheet.secondRung ? std::string("THE WARD HAS YOU FOR THE ROOFS, A SECOND TIME.")
                                          : std::string("THE WARD HAS PAPER ON YOU.");
        } else {
            std::string list;
            for (std::size_t i = 0; i < parts.size(); ++i) {
                if (i > 0) {
                    list += (i + 1 == parts.size()) ? " AND " : ", ";
                }
                list += parts[i];
            }
            out.charge = "THE WARD HAS YOU FOR " + list + ".";
        }
    }
    switch (sheet.tier) {
        case sim::Sentence::Maimed:
            out.asks = "THE PAPER ASKS FOR THE HAND.";
            break;
        case sim::Sentence::Condemned:
            out.asks = "THE PAPER ASKS FOR THE ROPE.";
            break;
        default:
            out.asks = "THE PAPER ASKS FOR A CELL.";
            break;
    }

    // THE OFFICER WHO WALKED YOU IN, and his line as he lays the paper
    // (court.taken), off the record's own officer and rotated with the
    // priest's opening -- one hearing, one rotation, both voices. He stands
    // by the wall UNTIL THE ANSWER: his walking-in line is said as the paper
    // is laid, and a judged page has nothing of his on it -- the bench has
    // spoken, and "sit where he points" beside a sentence is a line from
    // before the sentence.
    if (!hearing.officer.empty() && hearing.awaitingPlea()) {
        out.officerName = upperAscii(hearing.officer);
        out.officerSays = std::string(barks.line("court.taken", crimes.hearings()));
    }

    // --- the rows ---------------------------------------------------------
    const bool offered = courtSentenceOffered();
    const std::vector<CourtRowKind> kinds = courtRowKindsFor(hearing, courtPaperOpen_, offered);
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        HearingRow row;
        switch (kinds[i]) {
            case CourtRowKind::Guilty:
                row.key = "1";
                row.label = "I DID IT.";
                break;
            case CourtRowKind::NotGuilty:
                row.key = "2";
                row.label = "I DID NOT.";
                break;
            case CourtRowKind::Paper:
                row.key = "3";
                row.label = "HEAR THE PAPER";
                break;
            case CourtRowKind::Back:
                row.key = "0";
                row.label = "BACK";
                row.accent = panelInk().dim;
                break;
            case CourtRowKind::NoPlea:
                row.key = "1";
                row.label = "I HAVE NOTHING TO SAY.";
                break;
            case CourtRowKind::Serve:
                row.key = "1";
                row.label = serveWord(hearing.judgment);
                row.accent = judgmentAccent(hearing.judgment);
                break;
        }
        // THE ARMED ROW NAMES THE DEVICE'S OWN CONFIRM -- the QUIT row's
        // exact shape: "1 - I DID IT. -- SURE? ENTER".
        if (static_cast<int>(i) == courtArmed_) {
            row.label += " -- SURE? " + out.confirmKey;
        }
        out.rows.push_back(std::move(row));
    }
    out.cursor = kinds.empty() ? 0 : std::clamp(courtCursor_, 0, static_cast<int>(kinds.size()) - 1);
    out.armed = courtArmed_;

    // --- the detail pane ----------------------------------------------------
    // ROTATED ON THE HEARINGS BEFORE THIS ONE: plead() counts the hearing
    // the moment the plea lands, so the verdict's row reads the count back
    // by one -- the opening and the verdict of one hearing rotate together,
    // and the first hearing hears row zero of both.
    const std::int32_t rotation =
        hearing.judged() ? std::max<std::int32_t>(0, crimes.hearings() - 1) : crimes.hearings();
    const auto bark = [&barks, rotation](std::string_view key) {
        return std::string(barks.line(key, rotation));
    };
    if (hearing.awaitingPlea()) {
        out.view = courtPaperOpen_ ? HearingView::Paper : HearingView::Plea;
        // THE PRIEST OPENS, BY THE CASE, and the rotation runs only within
        // the case: every row of the table chosen is true of the sheet in
        // front of him. A commuted man back for the rope; a killing; a
        // Skyrunner's second rung (the rope with no corpse) or his first (the
        // hand); and a thief -- one the Mission knows from its own door, or
        // one it has never seen (the sheet's own THE DOOR term, and HEAR THE
        // PAPER's own phrase, decide which).
        std::string_view opening = sheet.templeStanding > 0 ? "court.paper.door" : "court.paper";
        if (sheet.tier == sim::Sentence::Condemned && sheet.commutedBefore) {
            opening = "court.nothing";
        } else if (sheet.blood) {
            opening = "court.blood";
        } else if (sheet.skyrunner && sheet.secondRung) {
            opening = "court.roofs.rope";
        } else if (sheet.skyrunner && sheet.tier == sim::Sentence::Maimed) {
            opening = "court.roofs.hand";
        }
        out.priest = bark(opening);
        // THE PRIEST PRESSES once a plea is armed (court.plead): the "-- SURE?"
        // beat in his own mouth, the question the row is about to answer.
        // A commuted man's one row is not a plea and the opening stands.
        if (courtArmed_ >= 0 && courtArmed_ < static_cast<int>(kinds.size())) {
            const CourtRowKind armedKind = kinds[static_cast<std::size_t>(courtArmed_)];
            if (armedKind == CourtRowKind::Guilty || armedKind == CourtRowKind::NotGuilty) {
                out.priest = bark("court.plead");
            }
        }
        if (!kinds.empty()) {
            out.consequence = rowConsequence(kinds[static_cast<std::size_t>(out.cursor)], sheet.tier);
        }
        out.paper = paperPhrases(sheet);
        out.backVerb = courtPaperOpen_ ? "THE ROWS" : (courtArmed_ >= 0 ? "DISARM" : "");
        return out;
    }
    if (hearing.judged()) {
        out.view = HearingView::Judged;
        // THE CHECK BLOCK, RE-DERIVED from the hashed record: the weighing is
        // a pure function of the sheet and the plea (weighArraignment), so
        // the page prints the same lines the sim scored, whenever it is
        // drawn, without the sim keeping a term list.
        // THE SUM READS "MAKES" -- the 4x6 font carries no `=` and the font
        // is not touched (the brief's own line); the ward's arithmetic is
        // spoken, not typeset. The plea's own term (CONFESSED / THE PRIEST IS
        // A MAN) rides the terms list the sim returns and is printed on its
        // OWN line under the record's sum, the reference's threshold-then-
        // verdict order.
        // EACH TERM IS ONE TOKEN with its sign glued on ("- 20 THE WARD"),
        // and the sum is a token of its own ("MAKES 27"): the pane packs the
        // tokens by row and never splits one, so a sign cannot wrap away
        // from its term at any width. The joined string is the same record
        // for the summary and the suite.
        const sim::Arraignment answer = sim::weighArraignment(sheet, hearing.plea);
        std::vector<std::string> terms;
        for (const sim::ArraignmentTerm& term : answer.terms) {
            if (term.name == sim::kTermConfessed || term.name == sim::kTermPriestIsAMan) {
                continue;
            }
            const std::string name = term.name == sim::kTermSawIt
                                         ? countWord(term.count) + " " + std::string(term.name)
                                         : std::string(term.name);
            if (terms.empty()) {
                terms.push_back(std::to_string(term.value) + " " + name);
            } else {
                terms.push_back(std::string(term.value >= 0 ? "+ " : "- ") +
                                std::to_string(std::abs(term.value)) + " " + name);
            }
        }
        if (!terms.empty()) {
            terms.push_back("MAKES " + std::to_string(answer.weight));
        }
        const auto joined = [](const std::vector<std::string>& tokens) {
            std::string out;
            for (const std::string& token : tokens) {
                if (!out.empty()) {
                    out += ' ';
                }
                out += token;
            }
            return out;
        };
        out.arithmeticTerms = terms;
        out.arithmetic = joined(terms);
        std::vector<std::string> pleaTerms;
        switch (answer.plea) {
            case sim::Plea::Guilty:
                pleaTerms.push_back("+ " + std::to_string(answer.pleaTerm) + " " +
                                    std::string(sim::kTermConfessed));
                pleaTerms.push_back("MAKES " + std::to_string(answer.scored));
                break;
            case sim::Plea::NotGuilty:
                pleaTerms.push_back(std::string(answer.pleaTerm >= 0 ? "+ " : "- ") +
                                    std::to_string(std::abs(answer.pleaTerm)) + " " +
                                    std::string(sim::kTermPriestIsAMan));
                pleaTerms.push_back("MAKES " + std::to_string(answer.scored));
                break;
            default:
                break;
        }
        out.pleaTerms = pleaTerms;
        out.pleaTerm = joined(pleaTerms);
        // THE LINES THE SCORE WAS READ AGAINST -- and only those. SPARED is a
        // denial's line and no other plea's (paperBand): a confession's
        // block does not print a line it could never have crossed. The rope
        // tier has the one line. Nothing weighed, nothing printed.
        if (answer.plea == sim::Plea::Guilty || answer.plea == sim::Plea::NotGuilty) {
            if (sheet.tier == sim::Sentence::Condemned) {
                out.lines = "THE LINE: " + std::to_string(sim::kMercyLine) + " MERCY";
            } else {
                // Each tier's own ladder (justice.cpp's substitution): on the
                // PAPER tier 38 buys the fine and 14 the cell; on THE HAND
                // tier 38 buys HELD with the hand spared and 14 THE HAND
                // with nights -- there is no FINED on it, so the row does
                // not print one.
                const bool handTier = sheet.tier == sim::Sentence::Maimed;
                out.lines = "THE LINES: ";
                if (answer.plea == sim::Plea::NotGuilty) {
                    out.lines += std::to_string(sim::kSparedLine) + " SPARED  ";
                }
                out.lines += std::to_string(sim::kFinedLine) + (handTier ? " HELD  " : " FINED  ") +
                             std::to_string(sim::kHeldLine) + (handTier ? " THE HAND" : " HELD");
            }
        }
        out.verdict = judgmentWord(hearing.judgment);
        out.verdictAccent = judgmentAccent(hearing.judgment);
        out.sentence = sentenceLine(sim::sentenceTerms(hearing, tavern_->playerCoin()));
        if (hearing.judgment == sim::Judgment::TheRope) {
            // THE ROPE'S ROW: where and when, the plate's own two facts --
            // the post the ward hangs a man at and the hour on the clock --
            // in the place the other judgments state their nights and coin.
            out.sentence = std::string(sim::kRopePlace) + ". " + travelClockText(timeOfDay_) + ".";
        }
        out.priest = hearing.doubled ? bark("court.lie") : bark(judgmentTable(hearing.judgment));
        out.backVerb.clear();
        return out;
    }
    return out;
}

void Session::moveCourtCursor(int delta) {
    if (!courtOpen_) {
        return;
    }
    // MOVING THE CURSOR DISARMS THE PLEA -- movePauseCursor's own rule for
    // QUIT: a player who backed off the row plainly changed their mind.
    courtArmed_ = -1;
    const std::vector<CourtRowKind> kinds =
        courtRowKindsFor(tavern_->hearing(), courtPaperOpen_, courtSentenceOffered());
    const int count = static_cast<int>(kinds.size());
    if (count <= 0) {
        return;
    }
    if (audio_ != nullptr && delta != 0) {
        audio_->playOneShot(audio::SoundId::UiTick);
    }
    courtCursor_ = ((std::clamp(courtCursor_, 0, count - 1) + delta) % count + count) % count;
}

void Session::chooseCourtVisibleRow(int slot) {
    if (!courtOpen_) {
        return;
    }
    const std::vector<CourtRowKind> kinds =
        courtRowKindsFor(tavern_->hearing(), courtPaperOpen_, courtSentenceOffered());
    if (slot < 0) {
        // THE ZERO KEY: the digit BACK prints while the paper is open, and
        // nothing otherwise.
        for (std::size_t i = 0; i < kinds.size(); ++i) {
            if (kinds[i] == CourtRowKind::Back) {
                courtCursor_ = static_cast<int>(i);
                chooseCourtRow();
                return;
            }
        }
        return;
    }
    // THE PRINTED DIGIT PICKS THE ROW IT PRINTS. BACK prints 0, never a
    // slot digit, so the slots address the rows above it only.
    int printed = 0;
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        if (kinds[i] == CourtRowKind::Back) {
            continue;
        }
        if (printed == slot) {
            // A digit on a row other than the armed one disarms it -- the
            // pause menu's own "picking a different row calls it off".
            if (courtArmed_ >= 0 && courtArmed_ != static_cast<int>(i)) {
                courtArmed_ = -1;
            }
            courtCursor_ = static_cast<int>(i);
            chooseCourtRow();
            return;
        }
        ++printed;
    }
}

void Session::chooseCourtRow() {
    if (!courtOpen_) {
        return;
    }
    const sim::HearingState& hearing = tavern_->hearing();
    const std::vector<CourtRowKind> kinds =
        courtRowKindsFor(hearing, courtPaperOpen_, courtSentenceOffered());
    if (kinds.empty()) {
        return;
    }
    const int at = std::clamp(courtCursor_, 0, static_cast<int>(kinds.size()) - 1);
    courtCursor_ = at;
    // AUDIO: the press is what is acknowledged, arms and refusals included.
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::UiConfirm);
    }
    switch (kinds[static_cast<std::size_t>(at)]) {
        case CourtRowKind::Paper:
            courtArmed_ = -1;
            courtPaperOpen_ = true;
            return;
        case CourtRowKind::Back:
            courtArmed_ = -1;
            courtPaperOpen_ = false;
            // Back to the row that opened the paper.
            courtCursor_ = 2;
            return;
        case CourtRowKind::Guilty:
        case CourtRowKind::NotGuilty:
        case CourtRowKind::NoPlea: {
            // ARMED ON THE FIRST PRESS, CONFIRMED ON THE SECOND -- the QUIT
            // pattern, so a leaned-on ENTER cannot plead.
            if (courtArmed_ != at) {
                courtArmed_ = at;
                return;
            }
            courtArmed_ = -1;
            const CourtRowKind kind = kinds[static_cast<std::size_t>(at)];
            const sim::Plea plea = kind == CourtRowKind::Guilty      ? sim::Plea::Guilty
                                   : kind == CourtRowKind::NotGuilty ? sim::Plea::NotGuilty
                                                                     : sim::Plea::NoPlea;
            // THE PLEA: the sim's own stepped input. Recorded on the watch
            // tape like every other verb the page owns.
            const sim::Arraignment answer = tavern_->plead(plea);
            courtPaperOpen_ = false;
            courtCursor_ = 0;
            if (answer.heard) {
                // The badge lands with the commit beat and is HELD before the
                // row that serves it is offered.
                commitPulse_.trigger();
                courtJudgmentHold_ = sim::kJudgmentHoldSteps;
            }
            return;
        }
        case CourtRowKind::Serve: {
            // THE SENTENCE, SERVED THROUGH THE WAIT MACHINERY (Session::
            // serveSentence): the coin, the clock, the mirror, the record,
            // and the arrest's own release -- which the next step() reads and
            // closes the page on. THE ROPE serves no clock and fires no
            // release: the page comes down here and the ceremony goes up.
            commitPulse_.trigger();
            courtServing_ = true;
            const sim::Tavern::SentenceReport& served = serveSentence();
            if (!served.served) {
                courtServing_ = false;
                return;
            }
            if (served.terms.rope) {
                courtServing_ = false;
                courtOpen_ = false;
                courtArmed_ = -1;
                courtPaperOpen_ = false;
                courtJudgmentHold_ = 0;
                armRopeCeremony();
                syncPanelAnim();
            }
            return;
        }
    }
}

void Session::courtBack() {
    if (!courtOpen_) {
        return;
    }
    // THE GRAMMAR EXCEPTION: ESC disarms an armed plea, closes the paper,
    // and does nothing else. There is no leaving the bench.
    if (courtArmed_ >= 0) {
        courtArmed_ = -1;
        return;
    }
    if (courtPaperOpen_) {
        courtPaperOpen_ = false;
        courtCursor_ = 2;
    }
}

bool Session::routeCourtKey(Key key) {
    if (!inCustody() || key == Key::None) {
        return false;
    }
    if (awaitingKey_) {
        return false;
    }
    const Action action = controls_.actionFor(key);
    const bool up = key == Key::Up || key == Key::PadUp || action == Action::Forward ||
                    action == Action::QuickPrev;
    const bool downward = key == Key::Down || key == Key::PadDown || action == Action::Back ||
                          action == Action::QuickNext;
    const bool confirm = key == Key::Enter || key == Key::PadSouth || action == Action::Interact;
    const bool back = key == Key::Escape || key == Key::PadEast;
    const int slotBase = static_cast<int>(Action::QuickSlot1);
    const int slot = static_cast<int>(action) - slotBase;
    const bool numbered = slot >= 0 && slot < 9;
    const bool zero = action == Action::QuickSlot0;
    if (ropeRowsUp_) {
        // THE END ROWS: the only live input. ESC disarms an armed row and
        // does nothing else on the plate, PAUSE is refused, and nothing
        // reaches the world.
        if (up) {
            moveRopeCursor(-1);
        } else if (downward) {
            moveRopeCursor(1);
        } else if (confirm) {
            chooseRopeRow();
        } else if (numbered) {
            chooseRopeVisibleRow(slot);
        } else if (back) {
            ropeArmed_ = -1;
        }
        return true;
    }
    if (ropeCeremonySteps_ > 0) {
        // THE PLATE: no input accepted.
        return true;
    }
    if (takenHold_ > 0) {
        // THE ARREST BEAT: the officer's hand is on you and the page is not
        // up yet. Every key is swallowed but Pause (a player can always
        // quit the game), which falls through to the client as it does
        // from the page.
        return !(action == Action::Pause || key == Key::Escape);
    }
    // THE PAGE. PAUSE falls through to the client, which opens the pause
    // menu over the court (a player can always quit the game); ESC is Pause's
    // own key, so it backs the page out of the paper or an armed plea FIRST
    // and only reaches the pause menu when there is nothing to back out of.
    if (back && (courtPaperOpen_ || courtArmed_ >= 0)) {
        courtBack();
        return true;
    }
    if (action == Action::Pause || key == Key::Escape) {
        return false;
    }
    if (up) {
        moveCourtCursor(-1);
    } else if (downward) {
        moveCourtCursor(1);
    } else if (confirm) {
        chooseCourtRow();
    } else if (numbered) {
        chooseCourtVisibleRow(slot);
    } else if (zero) {
        chooseCourtVisibleRow(-1);
    }
    // EVERY OTHER KEY IS SWALLOWED: no world verb reaches the body from the
    // bench, and no page opens over the court but the pause menu.
    return true;
}

namespace {

// --- the rope -----------------------------------------------------------------
/// The dip before the plate: one page-ease, the death ceremony's own. The
/// HOLD is the sim's number (kDeathHoldSteps); there is no fade-out.
constexpr std::int32_t kRopeDipInSteps = kPageEaseSteps;

}  // namespace

void Session::armRopeCeremony() {
    // From the run's end, written by the room at the drop off hashed state:
    // the place the ward hangs a man, the ward as the killer, the reason --
    // the corpse's roster name, or THE SECOND RUNG -- and the dateline. The
    // end's grammar, distinct from the revive's on purpose (spec 5): "HANGED
    // AT ... / BY THE WARD. FOR ..." can never be confused with "PUT DOWN IN
    // ... / BY <killer>. BY <weapon>." on screen.
    const sim::RunEnd& end = tavern_->runEnd();
    ropePlateTop_ = "HANGED AT " + (end.place.empty() ? std::string(sim::kRopePlace) : end.place) + ".";
    ropePlateMid_ = "BY THE WARD. FOR " +
                    (end.reason.empty() ? std::string(sim::kRopeForSecondRung) : upperAscii(end.reason)) +
                    ".";
    ropePlateFoot_ = "THE " + ordinalWord(end.day + 1) + " DAY. " + travelClockText(end.secondOfDay) + ".";
    ropeCeremonySteps_ = kRopeDipInSteps + sim::kDeathHoldSteps;
    ropeRowsUp_ = false;
    ropeCursor_ = 0;
    ropeArmed_ = -1;
    // SILENCE. No one-shot: ThudHeavy is "every path to the floor" and this
    // is not the floor. The drop is silent by ruling.
}

namespace {

/// The two end rows, bare. No save row: no save exists, so there is nothing
/// to load and the run is genuinely over. The day a save lands, "1 - THE
/// LAST SAVE" goes first.
constexpr std::string_view kRopeRowLabels[] = {"1 - A NEW MAN", "2 - LEAVE"};

}  // namespace

std::vector<std::string> Session::ropeRows() const {
    // THE ARMED ROW NAMES THE DEVICE'S OWN CONFIRM on its tail -- the QUIT
    // row's exact shape, and the plea rows' -- so the state of the row is
    // in the row.
    std::vector<std::string> out;
    for (std::size_t i = 0; i < std::size(kRopeRowLabels); ++i) {
        std::string row(kRopeRowLabels[i]);
        if (static_cast<int>(i) == ropeArmed_) {
            row += " -- SURE? " + std::string(promptConfirmKey(promptDevice_));
        }
        out.push_back(std::move(row));
    }
    return out;
}

void Session::moveRopeCursor(int delta) {
    if (!ropeRowsUp_) {
        return;
    }
    // Moving the cursor disarms the row -- the pause card's own rule for
    // QUIT: a player who backed off the row plainly changed their mind.
    ropeArmed_ = -1;
    if (audio_ != nullptr && delta != 0) {
        audio_->playOneShot(audio::SoundId::UiTick);
    }
    const int count = static_cast<int>(ropeRows().size());
    ropeCursor_ = ((ropeCursor_ + delta) % count + count) % count;
}

void Session::chooseRopeVisibleRow(int slot) {
    if (!ropeRowsUp_ || slot < 0 || slot >= static_cast<int>(ropeRows().size())) {
        return;
    }
    // A digit on a row other than the armed one disarms it -- the pause
    // menu's own "picking a different row calls it off".
    if (ropeArmed_ >= 0 && ropeArmed_ != slot) {
        ropeArmed_ = -1;
    }
    ropeCursor_ = slot;
    chooseRopeRow();
}

void Session::chooseRopeRow() {
    if (!ropeRowsUp_ || runEnded_) {
        return;
    }
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::UiConfirm);
    }
    // ARMED ON THE FIRST PRESS, CONFIRMED ON THE SECOND -- the QUIT pattern
    // on the two rows that end a run, so a leaned-on ENTER after the plate's
    // hold cannot start a new man or leave the game. The armed row keeps its
    // tail once taken: the run is over and the row that ended it stays lit.
    if (ropeArmed_ != ropeCursor_) {
        ropeArmed_ = ropeCursor_;
        return;
    }
    runEnded_ = true;
    if (ropeCursor_ == 0) {
        // A NEW MAN: a fresh run through the boot path. main.cpp's loop
        // reads this beside quitRequested and goes back to the creation
        // window; there is no save to erase.
        runEndReason_ = RunEndChoice::NewMan;
    } else {
        // LEAVE: the shipped quit, the one exit that has always existed.
        runEndReason_ = RunEndChoice::Leave;
        quitRequested_ = true;
    }
}

void Session::composeRopeCeremony(Framebuffer& target) const {
    if (ropeCeremonySteps_ <= 0 && !ropeRowsUp_) {
        return;
    }
    constexpr std::int32_t kTotal = kRopeDipInSteps + sim::kDeathHoldSteps;
    // THE VEIL: ramps up over the dip and then HOLDS. There is no fade-out
    // phase; the frame under it is never seen again.
    float dip = 1.0F;
    if (ropeCeremonySteps_ > sim::kDeathHoldSteps) {
        const std::int32_t elapsed = kTotal - ropeCeremonySteps_;  // 0 .. kRopeDipInSteps
        dip = static_cast<float>(elapsed) / static_cast<float>(kRopeDipInSteps);
    }
    dip = dip < 0.0F ? 0.0F : (dip > 1.0F ? 1.0F : dip);
    target.fillRect(0, 0, target.width(), target.height(), Rgb{0.0F, 0.0F, 0.0F}, dip);
    // THE PLATE, centred, in the epitaph's own bone register: the place, the
    // ward and the reason; the dateline dim under them, drawRouteCard's foot
    // idiom. The black field IS its backing.
    const int scale = hudScale(target.height());
    const int glyphH = 6 * scale;
    const int lineStep = glyphH + 2 * scale;
    const int cx = target.width() / 2;
    const int topY = target.height() / 2 - 2 * lineStep;
    const Rgb bone{0.86F, 0.82F, 0.72F};
    const Rgb dim{0.56F, 0.53F, 0.46F};
    const auto centre = [&](const std::string& line, int y, const Rgb& ink) {
        if (line.empty()) {
            return;
        }
        const int w = textWidth(line, scale);
        drawText(target, cx - w / 2, y, line, ink, dip, scale);
    };
    centre(ropePlateTop_, topY, bone);
    centre(ropePlateMid_, topY + lineStep, bone);
    centre(ropePlateFoot_, topY + 2 * lineStep, dim);
    if (!ropeRowsUp_) {
        return;
    }
    // THE PENDING-DECISION ZONE RISES UNDER IT once the hold has run:
    // option-list idiom, inverted-fill selection, never an arrow. The rows
    // are drawn in the page grid's own cells so the fill is the same shape
    // the pause card's cursor wears.
    const PanelMetric metric = panelMetric(target.width(), target.height());
    const std::vector<std::string> rows = ropeRows();
    // THE ZONE HOLDS THE ARMED WIDTH WHETHER OR NOT A ROW IS ARMED, so
    // arming one moves no row: the tail every row could grow is measured
    // for all of them.
    const std::string tail = " -- SURE? " + std::string(promptConfirmKey(promptDevice_));
    int widest = 0;
    for (const std::string_view row : kRopeRowLabels) {
        widest = std::max(widest, static_cast<int>(row.size() + tail.size()));
    }
    for (const std::string& row : rows) {
        widest = std::max(widest, static_cast<int>(row.size()));
    }
    const int rowsY = topY + 4 * lineStep;
    const PanelRect zone{cx - metric.widthOf(widest + 2) / 2, rowsY, metric.widthOf(widest + 2),
                         metric.heightOf(static_cast<int>(rows.size()))};
    const PanelInk& ink = panelInk();
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const int r = static_cast<int>(i);
        if (r == ropeCursor_) {
            drawInvertedFill(target, zone, metric, 0, r, widest + 2, ink.accent, 1.0F);
            drawCellTextKnockout(target, zone, metric, 1, r, rows[i], ink.knockout, 1.0F);
        } else {
            drawCellText(target, zone, metric, 1, r, rows[i], ink.prose, 1.0F);
        }
    }
}

void Session::composeTakenPlate(Framebuffer& target) const {
    if (!takenPlateUp()) {
        return;
    }
    // THE CUT: an instant black, the one line on it in the rope plate's own
    // register (bone, centred), at full alpha for the whole hold -- the
    // "TAKEN TO THE MISSION. 23:00." the spec promised, on the frame for as
    // long as it takes to read, and nothing under it (no page yet, no HUD
    // to hide it). The page opens over it when the hold runs out.
    target.fillRect(0, 0, target.width(), target.height(), Rgb{0.0F, 0.0F, 0.0F}, 1.0F);
    const int scale = hudScale(target.height());
    const Rgb bone{0.86F, 0.82F, 0.72F};
    const int w = textWidth(takenPlate_, scale);
    const int glyphH = 6 * scale;
    drawText(target, target.width() / 2 - w / 2, target.height() / 2 - glyphH / 2, takenPlate_,
             bone, 1.0F, scale);
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

/// Clipped to something the bottom-left corner can hold without walking across
/// the frame. The HUD hugs its edge; a quest tracker that runs to the middle of
/// the screen is the exact failure the Java build shipped.
[[nodiscard]] std::string clip(std::string text, std::size_t columns) {
    if (text.size() > columns) {
        text.resize(columns);
    }
    return text;
}

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

std::string Session::guildLine() const {
    // NOTHING WHILE THE WIRE IS IN. The lock row is drawn at
    // height - margin - 31*scale; this one is drawn at 24*scale above the
    // health bar, which IS height - margin - 31*scale. The same pixel row.
    // docs/frames/s10-08-the-lock.png caught the two interleaved, reading "THE
    // 1OCKUNPINS -TENADEPTH ....+....". It is the same class of defect the S7
    // review found with the alert over the topic grid and it gets the same fix:
    // a MODE owns its row. The wire is a mode, and while it is in, which ladder
    // you are highest on is not what the player is reading.
    //
    // It is suppressed HERE and not at the draw call so a case can assert it --
    // a suppression that lives inside drawFrame is only checkable in pixels.
    if (picking()) {
        return {};
    }
    const sim::DialogueDirector& talk = tavern_->dialogue();
    const std::int32_t top = talk.standings().highestRankedFaction();
    if (top < 0) {
        return {};
    }
    const sim::Faction* faction = talk.factions().at(top);
    const std::string name = faction == nullptr ? std::string("GUILD") : faction->displayName;
    return clip(upperAscii(name) + " - " + upperAscii(talk.standings().rankTitle(top)), 34);
}

std::string Session::caseLine() const {
    // COURIER CASE. The orientation row follows the live errand -- the same
    // one rule every case surface keeps (sheetCaseLive), so the corner names
    // the Gull while the sheet is the work and the Bloodletter before and
    // after.
    const sim::CasebookRaws& raws = activeCaseRaws();
    const sim::Casebook& book = activeCasebook();
    if (!book.active() || !raws.loaded()) {
        return {};
    }
    const std::vector<std::int32_t> heard = book.known();
    std::string line = "CASE " + std::to_string(book.readCount()) + "/" +
                       std::to_string(heard.size());
    // WHERE TO GO NEXT, ON THE SAME ROW. This is the orientation line: a player
    // who put the game down for a week and came back to a district of 692
    // people gets one line telling them where they were walking. Until this
    // sprint the corner of a new game was empty, which is exactly the "dropped
    // into a systems demo with no orientation" the demo brief names.
    // THE PULL PACK: the lead the ribbon follows, when it is in this book --
    // the player's pick, or the authored next lead standing in for one -- so
    // the corner and the ribbon never name two different doors.
    const PullTarget followed = pullTarget();
    const std::int32_t lead = followed.set && followed.book == frontedBook() ? followed.lead
                                                                             : book.nextOpen();
    if (lead >= 0 && static_cast<std::size_t>(lead) < raws.leads().size()) {
        line += " > " + raws.leads()[static_cast<std::size_t>(lead)].place;
    } else if (book.closed()) {
        line += " > " + std::string(raws.close());
    } else {
        const std::string_view mood = raws.dreadLabel(book.dread());
        line += "  ";
        line.append(mood);
    }
    return clip(std::move(line), 40);
}

std::string Session::legendLine() const {
    const sim::Legend book = legend();
    if (book.totalRungs() <= 0) {
        return {};
    }
    return clip(std::string(book.title()) + "  " + std::to_string(book.totalRungs()) +
                    (book.totalRungs() == 1 ? " RUNG" : " RUNGS"),
                34);
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

std::string Session::heatLine() const {
    const sim::CrimeLedger& crimes = tavern_->dialogue().crimes();
    const sim::Stash& sack = crimes.stash();
    if (crimes.heat() <= 0 && crimes.loot() <= 0 && !crimes.carryingBale() && sack.empty() &&
        !crimes.maimed() && !crimes.condemned() && !sheetCarry_) {
        return {};
    }
    // THE ROW IS THIRTY-FOUR COLUMNS AND IT FITS THEM BY CONSTRUCTION: the
    // tag and the number are never clipped, and a rider (the loot, the
    // bale, the man on your shoulder) is appended only while the row still
    // holds it whole -- a rider that does not fit is dropped, never cut in
    // half, and the number is never lost to it.
    constexpr std::size_t kHeatColumns = 34;
    std::string line;
    // THE CRIMINAL TAG (JUSTICE BUILD, section 1.2). Three states the ledger
    // keeps, one row. The two the ward has done TO you outrank everything
    // else on the line: a condemned man wants to know he is one before he
    // wants his heat. CONDEMNED is the shipped word with the court's meaning
    // -- the rope passed and commuted, for the rest of the run. WANTED is
    // paper; WANTED FOR BLOOD is paper with a corpse behind it, which only
    // the bench clears. A murderer whose heat has cooled is not WANTED on
    // this row -- the paper lapsed -- but the next arrest on any paper is a
    // rope hearing, and the row does not pretend otherwise by inventing a
    // word for it: standing stays a phrase, heat stays a number. A commuted
    // man with fresh blood on him is WANTED FOR BLOOD and nothing else: the
    // two tags together do not fit the row, and of the two it is the blood
    // that decides what the next arrest is (mercy is given once).
    const bool blood = crimes.warrant() && crimes.murderer();
    if (crimes.condemned() && !blood) {
        line = "CONDEMNED  ";
    } else if (crimes.maimed()) {
        // "MAIMED  WANTED FOR BLOOD  HEAT 100" is the row's full width.
        line = "MAIMED  ";
    }
    if (crimes.warrant()) {
        line += blood ? "WANTED FOR BLOOD  " : "WANTED  ";
    }
    line += "HEAT " + std::to_string(crimes.heat());
    const auto rider = [&line, kHeatColumns](const std::string& tail) {
        if (line.size() + tail.size() <= kHeatColumns) {
            line += tail;
        }
    };
    if (crimes.loot() > 0) {
        rider("  LOOT " + std::to_string(crimes.loot()));
    }
    if (crimes.carryingBale()) {
        rider("  BALE");
    }
    // COURIER CASE. The man in hand rides the same row the bale does -- a
    // carried thing the ward would mind, worn on the HUD until the back room
    // takes him. The rendered over-the-shoulder body is flagged follow-up
    // work; this line is the honest interim. TRAVEL LANE RENAME, flagged for
    // the owner: he read "FINCH IN HAND" as unclear, so the row now says what
    // the body is doing rather than naming an idiom.
    if (sheetCarry_) {
        rider("  CARRYING FINCH");
    }
    return clip(std::move(line), kHeatColumns);
}

bool Session::conversingNow() const noexcept {
    // THE ONE FORMULA. Repeated in drawFrame() as `conversing` until this
    // pass, which is exactly the shape of drift that let the S7 review's
    // overprint findings happen: two places computing the same fact, and
    // nothing catching them when a seventh page joined the list and only one
    // of the two remembered to add it.
    // JUSTICE BUILD: the hearing page is a page like the others for the
    // panel ease and the HUD's stand-down -- and unlike them it is not in
    // dismissOverlays(), which is the whole of its exception.
    return talking() || casebookOpen_ || keysOpen_ || grimoireOpen_ || waitOpen_ ||
           districtMapOpen_ || optionsOpen_ || pauseOpen_ || courtOpen_;
}

void Session::syncVoiceAnim() noexcept {
    // Wanted while the channel holds a line and no panel owns the screen --
    // the partner's lines are the panel's, and the goodbye lands as it
    // closes. The text itself stays behind to fade (clearIfSettled).
    voiceAnim_.setTarget(voice_.up() && !conversingNow());
}

void Session::endTalk(bool walkedOff) {
    sim::DialogueDirector& talk = tavern_->dialogue();
    if (talk.isOpen()) {
        // THE GOODBYE, before the director forgets who it was talking to.
        voice_.speakFarewell(talk.speaker().name, talk.farewell(walkedOff),
                             talk.farewellKey(walkedOff), talk.speaker().actorId,
                             static_cast<std::int32_t>(body_->stepCount()));
        // And the goodbye is the LAST thing they said: a greeting or a reply
        // still on the alert row's clock would resurface under it the moment
        // the panel let the row back -- the same mouth saying two things at
        // once. Their own line stands down -- their voice or the director's
        // report of their deed, both recorded as theirs by sayAs, so "GERTA
        // SALTCOTTE TAKES THE DRINK." goes with her as surely as her
        // greeting does (the 2026-10-06 critics' drink-then-leave frames).
        // A bouncer's warning or anybody else's word is not theirs and keeps
        // its clock.
        if (!messageMouth_.empty() && messageMouth_ == talk.speaker().name) {
            messageSteps_ = 0;
        }
    }
    tavern_->endConversation();
    wardTalkId_ = -1;
    topicCursor_ = 0;
    topicPage_ = 0;
    haggleOffer_ = 0;
    forgeOpen_ = false;
    reactionLine_.clear();
    reactionSteps_ = 0;
    syncPanelAnim();
}

void Session::settleOverlaysForInspection() noexcept {
    syncPanelAnim();
    panelAnim_.snapTo(panelAnim_.target());
    alertAnim_.snapTo(alertAnim_.target());
    if (tavern_->dialogue().isOpen()) {
        lastSpokenLine_ = tavern_->dialogue().lastLine();
        speechAgeSteps_ = kSpeechRevealSteps;
    }
}

void Session::syncPanelAnim() noexcept {
    // AUDIO WIRING: PANEL OPEN AND CLOSE, on the exact edge the plan names --
    // "wherever an EasedToggle target flips is exactly where the matching
    // one-shot belongs". panelAnim_ is the ONE toggle all eight pages share,
    // so this is one hook for every open and close in the game: the tiled
    // Menu speaks as a book (its tiles are the casebook, the letters, the
    // map), everything else in the quiet Ui pair. The flip fires once per
    // actual change however many times this function is re-run per step,
    // because the target itself only changes once.
    const bool panelWanted = conversingNow();
    if (audio_ != nullptr && panelWanted != panelAnim_.target()) {
        if (panelWanted) {
            // SPELLS BUILD: the Grimoire page is paper too -- the same
            // BookOpen/BookClose pair the tiled Menu already speaks, no new
            // sound design.
            // The ward map is paper too -- a chart unrolled reads as a book
            // opening, not as a UI blip.
            audioPanelWasMenu_ = casebookOpen_ || grimoireOpen_ || districtMapOpen_;
            audio_->playOneShot(audioPanelWasMenu_ ? audio::SoundId::BookOpen
                                                   : audio::SoundId::UiOpen);
        } else {
            audio_->playOneShot(audioPanelWasMenu_ ? audio::SoundId::BookClose
                                                   : audio::SoundId::UiClose);
        }
    }
    panelAnim_.setTarget(panelWanted);
    // The same test drawFrame() makes for what the HUD's own alert row is
    // about to show -- bouncerWarned(), the one copy (it used to be written
    // out here and there both, because this path mutates alertAnim_ and
    // that one is const; the accessor is const and both call it).
    const bool warned = bouncerWarned();
    // messageSteps_, NOT message_.empty(). The string is deliberately kept
    // alive a few steps past the countdown reaching zero so the fade-out has
    // something to fade -- see step()'s own note -- so the STRING being
    // non-empty cannot be what decides whether the alert is still wanted, or
    // the two would deadlock each other.
    alertAnim_.setTarget(warned || messageSteps_ > 0);
    // INNOVATION SPRINT ITEM #3. THE RISING EDGE ONLY -- a fresh warning
    // pulses the alert plate's own weight; the SAME warning re-read on every
    // later step (`warned` staying true) must not retrigger it, or the pulse
    // would sit at full strength for as long as the bouncer keeps talking
    // instead of reading as one beat. See alertPulse_'s own header.
    if (warned && !lastWarned_) {
        alertPulse_.trigger();
    }
    lastWarned_ = warned;

    // INNOVATION SPRINT ITEM #2. THE TILED MENU'S OWN FOUR BORDERS. Re-read
    // every call, exactly like every row below -- see this function's own
    // header on why that catches a focus change made through menuPageNext()/
    // menuPagePrev() (neither of which calls this directly) the moment
    // step() next runs it, not only one made through toggleMenuFocused().
    // Closed (casebookOpen_ false) targets every tile unfocused rather than
    // leaving whichever one last had it visibly "focused" underneath a
    // panel that is not there to show it.
    const bool menuUp = casebookOpen_;
    characterFocusAnim_.setTarget(menuUp && menuFocus_ == kMenuFocusCharacter);
    mapFocusAnim_.setTarget(menuUp && menuFocus_ == kMenuFocusMap);
    lettersFocusAnim_.setTarget(menuUp && menuFocus_ == kMenuFocusLetters);
    journalFocusAnim_.setTarget(menuUp && menuFocus_ == kMenuFocusJournal);

    // HARDENING PASS. EVERY OTHER ROW hud.hpp:178-187 NAMED AS STILL
    // SNAPPING, on the identical two-part shape the alert just used above:
    // setTarget() decides whether the row is WANTED, and the cached string
    // is what stays behind to fade once it is not -- see interactCache_'s
    // own header. `conversing` matches exactly what drawFrame() already
    // blanks every one of these rows for while a panel is open (the topic
    // list, the casebook, the keys page, options or pause own the bottom
    // band then), and roomLine()/stealthLine() gate themselves on
    // tavern_->playerInside() the same way drawFrame() used to inline.
    const bool conversing = conversingNow();
    const auto sync = [conversing](EasedToggle& anim, std::string& cache, std::string&& text) {
        const bool visible = !conversing && !text.empty();
        if (visible) {
            cache = std::move(text);
        }
        anim.setTarget(visible);
    };
    // THE CROSSHAIR PASS. THE AIM PROMPT'S FOUR PARTS, ON THE ONE TOGGLE.
    //
    // #85 composed a single "<key>  <label>" row here. The renderer now
    // colours the verb, the subject and the note by three different roles, so
    // Session hands over three strings and a kind and joins nothing -- a
    // pre-joined row would have to be taken apart again in hud.cpp, which is
    // the second-description-of-a-layout defect the panel pass banned.
    //
    // ONE EasedToggle STILL, and interactAnim_ is still it: the four parts
    // appear and disappear together because they are one element. The subject
    // and the note ride interactCache_'s own visibility so a name cannot
    // outlive the verb it belongs to mid-fade.
    const InteractTarget aim = interactTarget();
    const bool aimVisible = !conversing && !aim.verb.empty();
    if (aimVisible) {
        interactCache_ = aim.verb;
        interactSubjectCache_ = aim.subject;
        interactNoteCache_ = aim.note;
        interactKindCache_ = aim.kind;
    }
    interactAnim_.setTarget(aimVisible);
    sync(lockAnim_, lockCache_, lockLine());
    sync(rivalAnim_, rivalCache_, rivalLine());
    sync(stealthAnim_, stealthCache_, tavern_->playerInside() ? stealthLine() : std::string());
    // VOICE LANE. The voice row's own toggle, re-read like every row here.
    syncVoiceAnim();

    // UI-EA (LANE HUD): THE LAW OF EARNED TEXT. Text prints when it CHANGES,
    // when it is aimed at, or when the player hesitates -- never merely
    // because it is true. The four reference rows below (case, room, guild,
    // objective) and the clock/purse pair used to be furniture; each is an
    // EVENT now: an edge detected here arms a ~2.5s countdown
    // (kHudWakeSteps), step() spends it, and the row's own EasedToggle eases
    // it down. THE EDGES ARE DETECTED AGAINST LAST-SEEN VALUES and this
    // function runs several times a step, so every compare below is
    // idempotent by construction -- lastPlaceName_'s own pattern, whole
    // family. The first call ever (the constructor's) SEEDS and arms
    // nothing: a session does not boot with its corner announcing itself.
    const std::string caseNow = caseLine();
    const std::string roomNow = roomLine();
    const std::string guildNow = guildLine();
    const std::string objectiveNow = objectiveLine();
    const bool bookUp = casebookOpen_ || casebookPageOpen();
    const int hourNow = timeOfDay_ / 3600;
    const std::int32_t coinNow = tavern_->playerCoin();
    if (!hudEdgesSeeded_) {
        hudEdgesSeeded_ = true;
        lastClockHour_ = hourNow;
        lastClockTod_ = timeOfDay_;
        lastCoinSeen_ = coinNow;
        lastCaseSeen_ = caseNow;
        lastRoomSeen_ = roomNow;
        lastGuildSeen_ = guildNow;
        lastObjectiveSeen_ = objectiveNow;
        lastBookOpen_ = bookUp;
    }
    // THE CLOCK: an hour turning over is the ward's own bell, and a time
    // CHARGE (travel's restated minute, a wait pick, a sleep) is the one
    // moment a player is owed the hour they just spent. A charge is a jump
    // of a minute or more between two syncs -- the ordinary tick advances by
    // single seconds and never trips it.
    if (hourNow != lastClockHour_) {
        lastClockHour_ = hourNow;
        clockShowSteps_ = kHudWakeSteps;
    }
    {
        int jump = timeOfDay_ - lastClockTod_;
        if (jump < 0) {
            jump += sim::kSecondsPerDay;
        }
        if (jump >= 60) {
            clockShowSteps_ = kHudWakeSteps;
        }
        lastClockTod_ = timeOfDay_;
    }
    // THE PURSE: money is on screen when it moves.
    if (coinNow != lastCoinSeen_) {
        lastCoinSeen_ = coinNow;
        purseShowSteps_ = kHudWakeSteps;
    }
    // THE CASE ROW: a beat or lead moving is news; the book CLOSING is the
    // recap a player putting it down actually wants. And while the
    // lead-opened plate is up the row stays down -- the notice IS the case
    // news (UI-EA-SPEC #16), and one piece of news does not print twice.
    if (caseNow != lastCaseSeen_) {
        if (!caseNow.empty()) {
            caseShowSteps_ = kHudWakeSteps;
        }
        lastCaseSeen_ = caseNow;
    }
    if (lastBookOpen_ && !bookUp) {
        caseShowSteps_ = kHudWakeSteps;
    }
    lastBookOpen_ = bookUp;
    if (casePlateShowSteps_ > 0) {
        caseShowSteps_ = 0;
    }
    // THE ROOM ROW: entry and loudness are events; a head-count drifting by
    // one body is not, so the compare is made with the digits struck out --
    // "THE GULL 14 IN LOUD" and "THE GULL 15 IN LOUD" are one state.
    const auto strippedOfDigits = [](const std::string& text) {
        std::string out;
        out.reserve(text.size());
        for (const char c : text) {
            if (c < '0' || c > '9') {
                out.push_back(c);
            }
        }
        return out;
    };
    if (strippedOfDigits(roomNow) != strippedOfDigits(lastRoomSeen_)) {
        if (!roomNow.empty()) {
            roomShowSteps_ = kHudWakeSteps;
        }
    }
    lastRoomSeen_ = roomNow;
    // THE RUNG AND THE ERRAND: they wake when they change. A title held for
    // a week is reference material, and reference material lives on a page.
    if (guildNow != lastGuildSeen_) {
        if (!guildNow.empty()) {
            guildShowSteps_ = kHudWakeSteps;
        }
        lastGuildSeen_ = guildNow;
    }
    if (objectiveNow != lastObjectiveSeen_) {
        if (!objectiveNow.empty()) {
            objectiveShowSteps_ = kHudWakeSteps;
        }
        lastObjectiveSeen_ = objectiveNow;
    }
    // The four rows ride the sync() shape with one more condition: WANTED
    // means "recently woken", not "true". The caches still hold the last
    // shown words through the fade, exactly as every row above.
    const auto syncWake = [conversing](EasedToggle& anim, std::string& cache,
                                       const std::string& text, int wakeSteps) {
        const bool visible = !conversing && !text.empty() && wakeSteps > 0;
        if (visible) {
            cache = text;
        }
        anim.setTarget(visible);
    };
    syncWake(caseAnim_, caseCache_, caseNow, caseShowSteps_);
    syncWake(roomAnim_, roomCache_, roomNow, roomShowSteps_);
    syncWake(guildAnim_, guildCache_, guildNow, guildShowSteps_);
    syncWake(objectiveAnim_, objectiveCache_, objectiveNow, objectiveShowSteps_);
    // The clock and the purse draw live numbers (no cache -- fatigueAnim_'s
    // reasoning). The clock is additionally up for the life of the wait
    // page (its rows price the very hours it shows) and the pause stack --
    // the spec's own #34 keeps "rows, clock, title" on the pause, and the
    // hour before quitting is exactly a fact a player came to check. Neither
    // is gated on `conversing` -- a full page zeroes the fields at assembly,
    // and an hour striking or a price being paid mid-conversation is still
    // an event.
    clockAnim_.setTarget(waitOpen_ || pauseOpen_ || clockShowSteps_ > 0);
    purseAnim_.setTarget(purseShowSteps_ > 0);
    // PLANNING SPRINT (item #2, the sweep). THE SAME sync() SHAPE, FOR THE
    // TOP-RIGHT STACK'S THREE REMAINING ROWS -- see standingAnim_'s own
    // header on why these three, specifically, were still snapping.
    sync(standingAnim_, standingCache_, standingLine());
    sync(heatAnim_, heatCache_, heatLine());
    sync(stashAnim_, stashCache_, stashLine());
    // FIRST-PERSON COMBAT (S13). THE SAME sync() SHAPE, for the two rows the
    // Cast/Block task added -- each on its own EasedToggle per the pinned
    // convention, because a readied crafting appearing has nothing to do with
    // a guard going up.
    sync(spellAnim_, spellCache_, spellLine());
    sync(blockAnim_, blockCache_, blockLine());
    // ACTION-COMBAT BUILD. THE SAME sync() SHAPE for the HELD HARD charge row,
    // on its own toggle: a swing charging hard has nothing to do with a guard
    // going up, and the two are mutually exclusive anyway (the guard drops the
    // instant the hand leaves Idle).
    sync(chargeAnim_, chargeCache_, chargeLine());
    // STANCE & ROOM BUILD. THE SAME sync() SHAPE for the FISTS UP row, on its
    // own toggle: fighting mode outlives any one guard or swing.
    sync(handsAnim_, handsCache_, handsLine());
    // KIT BUILD. THE SAME sync() SHAPE for the turn row, on its own toggle.
    sync(turnAnim_, turnCache_, turnLine());
    // HELD-EFFECTS BUILD. THE SAME sync() SHAPE, one per active-effect slot
    // -- each on its own EasedToggle per the pinned convention, because a
    // warmth lapsing in slot 0 has nothing to do with a tuning arriving in
    // slot 1.
    for (std::size_t slot = 0; slot < kEffectRows; ++slot) {
        sync(effectAnims_[slot], effectCaches_[slot], effectLine(slot));
    }
    // FATIGUE BUILD. The bar mirrors the health bar's one visibility rule --
    // down for the length of a conversation, up otherwise -- on its OWN
    // toggle per the pinned convention. No cache: the bar draws live numbers,
    // which do not go away while it fades.
    fatigueAnim_.setTarget(!conversing);
    // THE WARD MAP (core action #13). Its own toggle, its own target -- the
    // page's openAmount, per the settled convention (rule 1). No cache: the
    // page draws live off TileQuery, which does not go away when it closes.
    districtMapAnim_.setTarget(districtMapOpen_);
    // SPELLS BUILD. The quick bar strip: wanted while the wheel or the number
    // row was touched inside the last couple of seconds (showQuickBar()'s
    // countdown, run down in step()) and no panel owns the bottom band. The
    // names are cached on the same visible-edge rule every sync() above uses,
    // so the strip finishes its fade with its labels still on it.
    const bool barWanted = !conversing && quickBarShowSteps_ > 0;
    if (barWanted) {
        for (std::int32_t slot = 0; slot < sim::Tavern::kQuickSlotCount; ++slot) {
            const sim::Spell* bound = tavern_->slotSpell(slot);
            // KIT BUILD: a slot is a spell OR an item; the strip prints
            // whichever it holds by name, the same cell either way.
            const sim::ItemDef* thing = tavern_->slotItem(slot);
            quickBarNames_[static_cast<std::size_t>(slot)] =
                bound != nullptr ? upperAscii(bound->displayName)
                : thing != nullptr ? thing->name
                                   : std::string();
        }
    }
    quickBarAnim_.setTarget(barWanted);
    // UI-EA (LANE HUD): THE QUICK BAR'S TUTOR TOAST, on the strip's own
    // rising edge and only its first two ever. NINE AND THE STICKS: the
    // QuickWheel hold is cut, so what is taught is the STEP -- the wheel on
    // a keyboard, D-pad left and right on a pad (QuickPrev/QuickNext's own
    // two halves, through promptLabel, so a rebind re-words it). It rides
    // the strip's countdown so the pair rise and fall as one, and after two
    // showings it is retired for the session.
    if (barWanted && !lastQuickBarUp_ && wheelHintShows_ < kWheelHintShows) {
        ++wheelHintShows_;
        wheelHint_.raise(kQuickBarShowSteps);
        const std::string prev(promptLabel(controls_, Action::QuickPrev, promptDevice_));
        const std::string next(promptLabel(controls_, Action::QuickNext, promptDevice_));
        // The two halves of one wheel print as the one word; anything else
        // prints both keycaps.
        const bool wheel = prev == "WHEELUP" && next == "WHEELDOWN";
        wheelHintText_ = (wheel ? std::string("WHEEL") : prev + " " + next) + " - STEP";
    }
    lastQuickBarUp_ = barWanted;
    wheelHint_.sync(conversing);

    // DISTRICT PHASE D: THE THRESHOLD MOMENT.
    //
    // THE RISING EDGE ONLY, the identical shape alertPulse_ uses above and for
    // the identical reason: `here` is re-read on every call (this function runs
    // once a step AND out of every toggle, see its own header), so what fires
    // the plate has to be the CHANGE and never the state. Standing still in
    // Tarwalk must not re-arm it sixty times a second.
    //
    // NAMED PLACES ONLY. placeNameAt is empty for the two thirds of the
    // district that is compounds, yards and back lanes -- see its own note --
    // and lastPlaceName_ deliberately does not record those (see its header):
    // an unnamed tile is a gap between places, not a place, and treating it as
    // one would re-announce TARWALK every time the player stepped into an
    // alley and back out, and re-announce SALTGATE RISE at every seam between
    // the three rectangles that road is authored as.
    const std::string_view here =
        sim::docks::placeNameAt(body_->tileX(), body_->tileY(), body_->band());
    // THE STAND-DOWN, and it is the same list every other overlay stands down
    // for. A panel owns the screen: the plate does not draw over the topic
    // list, the tiled Menu, the ward map, the keys page or the pause menu, and
    // a crossing walked while one of them is up is simply not announced --
    // stood down rather than queued, because a notice that pops the instant a
    // menu closes is a notice about the menu.
    const bool suppressed = conversingNow();
    if (!here.empty() && here != lastPlaceName_) {
        lastPlaceName_.assign(here);
        if (!suppressed) {
            // THE NEWEST CROSSING WINS, say()'s own rule: a second boundary
            // crossed while the first plate is still up swaps the words and
            // re-arms the hold rather than queueing. It does NOT restart the
            // rise -- the toggle is already open and setTarget is idempotent --
            // so the plate reads as one notice being corrected, not as two
            // notices fighting.
            placePlateName_.assign(here);
            placePlateShowSteps_ = kPlacePlateShowSteps;
        }
    }
    if (suppressed) {
        placePlateShowSteps_ = 0;
    }
    // THE CASEBOOK PASS. THE SAME STAND-DOWN, AND IT IS NOT A COURTESY.
    // examine() is the one thing that arms this notice and examine() refuses
    // while a page or a conversation owns the keyboard, so in practice the
    // plate is armed with the world on screen -- but the rule is stated here
    // anyway, in the one place every other overlay's stand-down is stated, so
    // a later caller that arms it from somewhere else cannot quietly put a
    // notice over a topic list. STOOD DOWN, never queued: a notice that pops
    // the instant a menu closes is a notice about the menu.
    if (suppressed) {
        casePlateShowSteps_ = 0;
    }
    // AND THE TWO NOTICES DO NOT SHARE A FRAME. There is one announcement slot
    // under the ribbon (hud.cpp's drawAnnouncePlate) and the case outranks the
    // crossing, so a lead opened on the step the body walked into a named place
    // takes the band and the crossing is dropped rather than drawn under it.
    if (casePlateShowSteps_ > 0) {
        placePlateShowSteps_ = 0;
    }
    // AND NEITHER SHOWS UNDER THE RUNG PLATE: the slot waits behind it.
    placePlateAnim_.setTarget(placePlateShowSteps_ > 0 && !rungPlateOnScreen());
    casePlateAnim_.setTarget(casePlateShowSteps_ > 0 && !rungPlateOnScreen());
}

std::string Session::rivalLine() const {
    // BOTTOM-LEFT, ON THE EDGE, ONE LINE. The HUD rule is not a preference
    // (COMBAT-FEEL-REFERENCE section 3): the centre stays empty and an
    // inventory, a guild or a rivalry is one row in a corner until it has
    // earned more. This is the only thing in the game that says the man across
    // the room is the man who put you here.
    //
    // UI-EA (LANE HUD): DIETED TO NAME AND COUNT -- the spec's "rank 6 -> 3".
    // "RIVAL " and the title were reference words (his corner and his sheet
    // already say what he is), and HUNTING leaves the label for the row's red
    // ink (HudState::rivalHunts) -- the glance was always the point of that
    // word, and the colour IS the glance. The count is a value; values never
    // get vaguer.
    const sim::Nemesis* worst = tavern_->nemesis().worst();
    if (worst == nullptr) {
        return {};
    }
    std::string line = upperAscii(worst->who) + " x" + std::to_string(worst->wins);
    // 44 columns: 220 pixels at scale 1 against a 320-wide frame with a
    // six-pixel margin, so the longest line this can produce still fits its
    // edge. A HUD line that runs off the frame is the S6 defect, and it is not
    // being reintroduced from a different corner.
    return clip(std::move(line), 44);
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
        line += std::to_string(held) + " " + std::string(sim::contrabandLabelFor(good, held));
    }
    if (sack.illicitWeight() > 0) {
        line += "  " + std::to_string(sack.illicitWeight()) + "DR";
    }
    return clip(std::move(line), 34);
}

std::string Session::spellLine() const {
    // WHAT THE HAND IS HOLDING, one line on the top-right edge -- the same
    // deal the sack got, and for the same reason: a readied crafting is
    // exactly the kind of element that creeps into being a hotbar panel.
    // Empty with an empty grimoire (absence costs nothing), and the cooling
    // clock rides the same line rather than earning a meter.
    const sim::Spell* spell = tavern_->equippedSpell();
    if (spell == nullptr) {
        return {};
    }
    std::string line = "CAST  " + upperAscii(spell->displayName);
    const std::int64_t cooling = tavern_->castCooldownLeft();
    if (cooling > 0) {
        line += " (" + std::to_string(cooling) + "S)";
    }
    return clip(std::move(line), 34);
}

std::string Session::blockLine() const {
    // THE ROOM'S OWN FACT, not the keypress: playerBlocking() is what
    // tickBrawl actually reads, so this row can never say GUARD UP while a
    // menu has quietly lowered it -- see step()'s own derivation.
    return tavern_->playerBlocking() ? "GUARD UP" : std::string();
}

std::string Session::turnLine() const {
    return turnSteps_ > 0 ? turnLine_ : std::string();
}

std::string Session::chargeLine() const {
    // ACTION-COMBAT BUILD (section 5, channel 2). "HELD HARD -- CUDGEL 14-18"
    // while the swing is charged past the hard threshold, and empty otherwise
    // -- the hard tier made visible in the sheet's own NAME low-high grammar.
    // The reticle carries the light charge; this row is only the committed hard
    // one, so it never competes with the guard (the guard holds only in Idle).
    if (!tavern_->playerChargeHard()) {
        return {};
    }
    const sim::Weapon weapon = tavern_->playerHeldWeapon();
    // THE HARD SPAN, which is the swing span through the hard tier: the same
    // ((base + variance) * chargeQ8) >> 8 strike() applies, at kHardSwingChargeQ8
    // -- so the numbers on the row are exactly the numbers a release will throw.
    const std::int32_t base = sim::baseDamage(weapon);
    const std::int32_t lo = (base * sim::kHardSwingChargeQ8) >> 8;
    const std::int32_t hi =
        ((base + sim::kStrikeVarianceMax) * sim::kHardSwingChargeQ8) >> 8;
    // THE WEAPON NAME is weaponSheetLine's own casing (which is why this reads
    // through it rather than keeping a second copy of the name table): take
    // everything up to the sheet's own span, so "THE EVICTOR 7-9 IMPACT" yields
    // "THE EVICTOR" and the numbers below are the doubled hard span, not its.
    std::string sheet = sim::weaponSheetLine(weapon);
    std::size_t span = 0;
    while (span < sheet.size() && (sheet[span] < '0' || sheet[span] > '9')) {
        ++span;
    }
    std::string name = sheet.substr(0, span);
    while (!name.empty() && name.back() == ' ') {
        name.pop_back();
    }
    return "HELD HARD -- " + name + " " + std::to_string(lo) + "-" + std::to_string(hi);
}

std::string Session::handsLine() const {
    // STANCE & ROOM BUILD. THE ROOM'S OWN FACT, the blockLine shape:
    // playerHandsUp() is the hashed bit the room, the street and the Watch
    // react to, so this row can never say FISTS UP while the sim says down.
    // Weapon-named off the sheet's own casing (the same table chargeLine
    // reads through weaponSheetLine), with the register's own word for a
    // blade -- STEEL, as the flip line and the epitaph already say it.
    if (!tavern_->playerHandsUp()) {
        return {};
    }
    // KIT BUILD: a thing worn in the hand is named as itself -- KNIFE UP,
    // MACE UP -- the sheet's own word; the class reading stands for a hand
    // a sheet armed directly.
    if (const sim::ItemDef* held = tavern_->heldItem(); held != nullptr) {
        return held->name + " UP";
    }
    const sim::Weapon weapon = tavern_->playerHeldWeapon();
    if (weapon == sim::Weapon::Edged) {
        return "STEEL UP";
    }
    std::string sheet = sim::weaponSheetLine(weapon);
    std::size_t span = 0;
    while (span < sheet.size() && (sheet[span] < '0' || sheet[span] > '9')) {
        ++span;
    }
    std::string name = sheet.substr(0, span);
    while (!name.empty() && name.back() == ' ') {
        name.pop_back();
    }
    return name + " UP";
}

std::string Session::effectLine(std::size_t slot) const {
    // HELD-EFFECTS BUILD. The slot-th live hold, name and seconds left. The
    // name comes out of the grimoire by the id the hold itself carries --
    // the same one source of truth the CAST row reads -- and the clock is
    // the room's own holdSecondsLeft, re-read every step so the row counts
    // down continuously rather than snapping on expiry.
    const std::vector<sim::Tavern::ActiveHold>& holds = tavern_->heldEffects();
    if (slot >= holds.size() || slot >= kEffectRows) {
        return {};
    }
    const sim::Tavern::ActiveHold& hold = holds[slot];
    const sim::Spell* spell = tavern_->dialogue().grimoire().find(hold.spellId);
    std::string name =
        spell != nullptr ? upperAscii(spell->displayName) : upperAscii(hold.spellId);
    return clip(name + " " + std::to_string(tavern_->holdSecondsLeft(hold)) + "S", 34);
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
    // And the row above it, for the same reason: the lock's own row is bounded
    // by these two and a long lock line is nearly the width of the frame.
    if (picking()) {
        return {};
    }
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
    // THE CASE'S OWN DESTINATION IS NOT PUT HERE. It has its own row lower down
    // the stack -- see caseLine() -- because this one sits at y - 32*scale,
    // which crosses the exclusion rectangle whenever it is non-empty, and the
    // orientation a new player needs must not be the thing that breaks the HUD
    // rule to deliver it.
    return {};
}

std::string Session::standingLine() const {
    // ABSENCE COSTS NOTHING, the identical rule every other row on this stack
    // already keeps: "NOBODY IN PARTICULAR" is the ward having no opinion of
    // you at all, and the row only appears the moment it HAS one -- see
    // drawFrame()'s own comment where this used to be computed inline.
    const std::string_view standing = tavern_->dialogue().ledger().reputationLabel();
    if (standing == sim::kReputationUnremarkable) {
        return {};
    }
    return std::string(standing);
}

namespace {

/// FAST TRAVEL's arrival seam: the finished frame dipped toward black by
/// `amount`, whatever was composed under it -- the world, the map's own close
/// tail, or a page opened mid-fade. The last act of every drawFrame() return
/// path, so the commit frame is already dark and the destination eases up
/// from under it. Render-only; nothing here is read back.
void dipTravelSeam(Framebuffer& target, float amount) {
    if (amount <= 0.0F) {
        return;
    }
    target.fillRect(0, 0, target.width(), target.height(), Rgb{0.0F, 0.0F, 0.0F},
                    amount > 1.0F ? 1.0F : amount);
}

}  // namespace

FrameStats Session::drawFrame(Framebuffer& target, FramePasses passes) const {
    // The flicker phase is a pure function of the body's step count, so the
    // same scripted session captures the same frame every time.
    const float phase = static_cast<float>(body_->stepCount()) / 60.0F;
    std::vector<SpriteInstance> sprites = renderer_->lampSprites(phase);

    // A lamp is not a hole in the sky at noon. The flame billboards fade out as
    // the daylight comes up, so a lit district reads at dusk and disappears
    // into ordinary daylight the way it should. WEATHER: the same sky the
    // world is drawn under, so a foggy noon keeps its lanterns, and a wind
    // takes a little off every flame (SkyState::haloScale).
    const SkyState skyNow = sky();
    const float lampMix = (1.0F - 0.9F * skyNow.daylight) * skyNow.haloScale;
    for (SpriteInstance& sprite : sprites) {
        sprite.glow *= lampMix;
        sprite.halfWidth *= 0.45F + 0.55F * lampMix;
        sprite.halfHeight *= 0.45F + 0.55F * lampMix;
    }

    RenderSettings settings = settings_;
    settings.dynamicLamps = tavernLights();
    settings.weather = weather();

    // The hearth's own flame, and a candle head on each lit table.
    for (const Lamp& light : settings.dynamicLamps) {
        const bool isHearth = light.name == "gull_hearth";
        const bool isLantern = light.name == "gull_lantern";
        SpriteInstance flame;
        flame.x = static_cast<float>(light.x) + 0.5F;
        // Pulled a little out of the wall the hearth is set into, so the flame
        // sits in the mouth of it rather than inside the masonry.
        flame.y = static_cast<float>(light.y) + (isHearth ? -0.05F : 0.5F);
        // A hearth burns at the floor, a candle on a table, a lantern hangs —
        // and now that a storey is three tiles those are three genuinely
        // different heights instead of three points inside one tile. ABSOLUTE
        // tile heights above the floor of the cell, not fractions of the
        // storey: raising a ceiling does not raise a tabletop.
        //
        //   0.35  in the mouth of the fireplace, on the hearthstone
        //   0.95  a candle on a table, about 0.86 m
        //   2.30  a lantern on its hook, well over head height
        constexpr float kHearthFlameHeight = 0.35F;
        constexpr float kTableCandleHeight = 0.95F;
        constexpr float kHangingLanternHeight = 2.30F;
        flame.z = bandSurface(light.z) +
                  (isHearth ? kHearthFlameHeight
                            : (isLantern ? kHangingLanternHeight : kTableCandleHeight));
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
    // #78: AND THE REST OF THE DISTRICT. One roster, two sources: the Gilded
    // Gull's seventeen come out of the Tavern and everybody else out of the
    // population. They are appended and never merged, because the two systems
    // own their own bodies and a renderer that stitched them into one list
    // would be the third place that thinks it knows who is in the ward.
    const std::vector<SpriteInstance> ward = wardSprites(view);
    sprites.insert(sprites.end(), ward.begin(), ward.end());

    FrameStats stats;
    if (passes.world) {
        stats = renderer_->renderFrame(target, view, settings, sprites);
    } else {
        // 3D BUILD. THE WORLD IS THE RAYLIB BACKEND'S FRAME NOW, and this
        // target is the OVERLAY it composites on top: cleared to nothing,
        // then every pass below draws exactly as it always has. The one
        // thing that changes is what the signage sees -- an infinite depth
        // buffer, so no sign is occluded by a wall until the V lane gives
        // signage its line-of-sight projector. The stats are empty because
        // no world was drawn here; the backend reports its own.
        target.clearTransparent();
    }

    // World content, not interface -- drawn like the sprites above rather
    // than gated behind config_.hud, and BEFORE the HUD/menu/dialogue passes
    // below so none of them paint over a legible sign. See
    // signage_renderer.hpp's own header for why occlusion needs the depth
    // buffer renderFrame just wrote and nothing drawn after it yet.
    // AND THE ONE THING ON THE SCREEN THE SIGNS COULD NOT SEE.
    //
    // Signage is drawn before the HUD so nothing paints over a legible sign --
    // and the consequence was that the HUD painted over one. The crosshair
    // prompt sits at the exact centre of the frame, which is where you are
    // looking, which is where the nearest sign projects: on the Tarwalk the
    // ward's own "TARWALK" and the prompt's "E - TALK" landed on the same
    // pixels and the prompt won by being second, cutting a street name through
    // the middle.
    //
    // The signage renderer already knew how to place labels around each other.
    // It just needed to be told the interface is on the screen too. The
    // rectangle is the prompt's OWN footprint this frame -- built from the same
    // aim fields the HUD is about to be handed, four lines below -- and not the
    // whole clamp fence, which would cost every sign in the centre band.
    //
    // A frame with nothing under the reticle hands back an empty rect and the
    // signs are placed exactly as they were before this existed.
    //
    // THE FURNITURE, IN ONE PLACE. `config_.hud` is the --nohud instrument and
    // `hudStandDown_` is a card owning the frame (see Session::setHudStandDown);
    // below, every interface pass reads THIS rather than testing the two
    // itself, so the two switches cannot drift apart one branch at a time.
    const bool furniture = config_.hud && !hudStandDown_;

    SignageSettings signage;
    // ...AND THE EXCLUSION IS COMPUTED ONLY WHEN THERE IS A PROMPT TO EXCLUDE.
    //
    // It used to be computed unconditionally, so under --nohud the signs still
    // stepped around an aim prompt that was never drawn. Zero gameplay impact
    // and a real measurement one: --nohud is the RULER this project takes every
    // interface number with, and a ruler that moves the world it is measuring
    // overstates every reading it gives. 368 to 656 pixels of a 640x360 frame
    // (0.16-0.29%) were counted as interface that were signs standing somewhere
    // else. See docs/HUD-REAL-ESTATE.md.
    if (config_.hud) {
        HudState aim;
        // SHIP NOTE MOVE 3: the half of Interact's row that belongs to the
        // device that last spoke -- "E" or "A" on the shipped table. Must
        // match the HUD's own read four blocks below, or the signage would
        // step around a prompt of a different width than the one drawn.
        aim.aimKey = promptKeyName(promptKey(controls_, Action::Interact, promptDevice_));
        aim.aimVerb = std::string_view{interactCache_};
        aim.aimSubject = std::string_view{interactSubjectCache_};
        aim.aimNote = std::string_view{interactNoteCache_};
        aim.interactFade = interactAnim_.value();
        signage.exclusion = hudAimPromptRect(aim, target.width(), target.height());
        // ...AND THE NAME IT IS ALREADY SAYING. See
        // SignageSettings::namedByPrompt: the prompt carries the verb and the
        // key, so where the two would say the same building the SIGN yields.
        // Gated on the prompt actually being on screen (interactFade), or a
        // sign would vanish for a subject the crosshair had already left.
        if (aim.interactFade > 0.0F) {
            signage.namedByPrompt = aim.aimSubject;
        }
    }
    // Signage is WORLD CONTENT and stays outside config_.hud -- the ruler has
    // to keep measuring it. A card is the one thing it yields to.
    if (!hudStandDown_) {
        drawSignage(target, view, signage);
    }

    // INNOVATION SPRINT ITEM #3. A BRAWL FINALLY HAS SOME PHYSICAL WEIGHT.
    // Two brief, low-alpha washes over the WORLD -- drawn here, before the
    // HUD, so the one readout that matters most while this fires (the health
    // bar) is never sitting on top of the wash instead of over the plain
    // world. RESTRAINED ON PURPOSE: this is a moody, text-forward
    // investigation game and not an arcade brawler, so both peak under a
    // fifth of full strength and are gone within a handful of frames -- see
    // punchLandedPulse_/punchTakenPulse_'s own header for why an
    // ImpactPulse (a one-shot event) rather than a held EasedToggle is what
    // drives this, and why there is no third or fourth flash bolted on: two
    // real moments, executed with restraint, rather than a scattershot pass
    // across every verb that could theoretically want one.
    if (config_.hud) {
        const float landed = punchLandedPulse_.value();
        if (landed > 0.0F) {
            // A CONNECTING PUNCH: pale and warm, the same register the
            // lantern flame and the plate's own bone border already draw in
            // -- an "it worked" beat, not a warning.
            target.fillRect(0, 0, target.width(), target.height(), Rgb{0.92F, 0.86F, 0.70F},
                            0.16F * landed);
        }
        const float taken = punchTakenPulse_.value();
        if (taken > 0.0F) {
            // A BLOW LANDING ON THE PLAYER: a low, blooded red, read as a
            // wince rather than an alarm -- deliberately not brighter than
            // the landed-punch flash above, which is meant to feel like the
            // better of the two moments.
            target.fillRect(0, 0, target.width(), target.height(), Rgb{0.58F, 0.10F, 0.08F},
                            0.18F * taken);
        }
        // FIRST-PERSON COMBAT (S13). A BLOW THE GUARD CAUGHT: cool steel
        // instead of blood, and QUIETER than either flash above -- the guard
        // working is the calmest of the three moments, and step() fires this
        // INSTEAD of the taken-wash for a blocked blow, never as well, so
        // this stays inside the "two real moments, executed with restraint"
        // budget rather than stacking on top of it.
        const float blocked = blockPulse_.value();
        if (blocked > 0.0F) {
            target.fillRect(0, 0, target.width(), target.height(), Rgb{0.52F, 0.60F, 0.70F},
                            0.12F * blocked);
        }
    }

    HudState hud;
    hud.health = tavern_->playerHp();
    hud.healthMax = 100;
    // FATIGUE BUILD: the wind, in points, right beside the hit points it
    // stands under on the frame. The fade is the bar's own EasedToggle
    // (fatigueAnim_), mirroring showHealth's one rule without borrowing its
    // snap -- see the member's header.
    hud.fatigue = tavern_->playerFatigue().currentPoints();
    hud.fatigueMax = tavern_->playerFatigue().maxPoints();
    hud.fatigueFade = fatigueAnim_.value();
    hud.yawBam = body_->yaw();
    // UI-EA (LANE HUD): NO STREET SUB-LABEL. placeLabel() still answers for
    // the map's title and the dialogue epithet; the street's own copy of it
    // is deleted -- the threshold plate announces every crossing, and a row
    // that restated it every frame was the diet's first cut. The clock and
    // the purse are earned text on their own toggles: numbers stay live
    // (they are what wakes), the fades are what sleep.
    hud.timeOfDaySeconds = timeOfDay_;
    hud.clockFade = clockAnim_.value();
    hud.coin = tavern_->playerCoin();
    hud.purseFade = purseAnim_.value();
    // While a conversation is open the bottom band belongs to the topic list,
    // so the room line and the running message stand down rather than draw on
    // top of it.
    // The casebook and the key list stand the HUD down exactly the way a
    // conversation does: all three are drawn in the same two bands, and two
    // things fighting over one row is how the centre-clear rule gets broken by
    // accident.
    //
    // #77 ADDS THE OPTIONS PAGE, and it was found by opening the real window and
    // pressing F2. Every other surface was on this list; the new one was not, so
    // the compass strip, the case row, the health bar and the first-run hint all
    // drew straight over the sliders and the sliders drew straight back, and
    // both were illegible. Nothing in a `--screenshot` capture would ever have
    // shown it, because nothing scripted opens this page.
    const bool conversing = conversingNow();
    // HARDENING PASS. roomLabel READS THE CACHE syncPanelAnim() JUST FILLED,
    // NOT A FRESH roomLine() CALL. The cache is what keeps a room label on
    // screen fading out after tavern_->playerInside() has already gone
    // false -- roomLine() itself would answer empty by then, same as every
    // other row below -- and roomFade is Session's own EasedToggle for this
    // row (roomAnim_), the identical pattern hud.alertFade already set for
    // the alert. See interactCache_'s own header in session.hpp.
    hud.roomLabel = std::string_view{roomCache_};
    hud.roomFade = roomAnim_.value();
    // The ward's opinion of you sits under the purse -- unless somebody is in
    // front of you, in which case THEIR opinion is the one that matters and the
    // panel is already showing it.
    //
    // ABSENCE DOES NOT COST A ROW. "NOBODY IN PARTICULAR" is the ward having no
    // opinion of you at all, and it held twenty characters of the top right in
    // every frame this game has ever produced, saying that nothing had
    // happened. The row appears the moment the ward HAS an opinion, which is
    // the only moment it is worth the sky it stands in.
    //
    // PLANNING SPRINT (item #2, the sweep). READS ITS OWN CACHE AND FADES,
    // THE IDENTICAL SHAPE roomLabel JUST USED ABOVE, instead of the bare
    // `conversing ? empty : text` this used to be. A real sweep of this file
    // found these three rows (standing, heat, the sack) still popping on and
    // off with `conversing` at full strength -- stealthLabel, right below,
    // already got this treatment; these three did not.
    hud.standingLabel = std::string_view{standingCache_};
    hud.standingFade = standingAnim_.value();
    // What the Watch has heard, what is in your coat, and whether you are
    // carrying somebody's bale. Top right under the purse, hugging the edge --
    // the centre of the frame stays empty, which is the rule.
    hud.heatLabel = std::string_view{heatCache_};
    hud.heatFade = heatAnim_.value();
    hud.stashLabel = std::string_view{stashCache_};
    hud.stashFade = stashAnim_.value();
    // FIRST-PERSON COMBAT (S13). What the hand is holding and whether the
    // guard is up -- each reading its own cache and fading the identical way
    // every row above does.
    hud.spellLabel = std::string_view{spellCache_};
    hud.spellFade = spellAnim_.value();
    hud.blockLabel = std::string_view{blockCache_};
    hud.blockFade = blockAnim_.value();
    // ACTION-COMBAT BUILD (section 5, channel 2). The HELD HARD charge row,
    // its own cache and fade the identical shape the guard row just used.
    hud.chargeLabel = std::string_view{chargeCache_};
    hud.chargeFade = chargeAnim_.value();
    // STANCE & ROOM BUILD. The FISTS UP row, its own cache and fade.
    hud.handsLabel = std::string_view{handsCache_};
    hud.handsFade = handsAnim_.value();
    hud.turnLabel = std::string_view{turnCache_};
    hud.turnFade = turnAnim_.value();
    // HELD-EFFECTS BUILD. The live holds, each reading its own cache and
    // fading on its own toggle -- the identical shape every row above uses.
    for (std::size_t slot = 0; slot < kEffectRows; ++slot) {
        hud.effectLabels[slot] = std::string_view{effectCaches_[slot]};
        hud.effectFades[slot] = effectAnims_[slot].value();
    }
    // SPELLS BUILD. The quick bar strip: names out of the cache
    // syncPanelAnim() keeps (so the fade-out still has labels), the selected
    // cell off the same quickSlot_ the number row moves, and the equipped
    // cell derived fresh from the ONE equipped id sim::Tavern holds -- the
    // same id spellLine() just read for the CAST row, so the two can never
    // disagree about what the hand is holding.
    for (std::size_t slot = 0; slot < quickBarNames_.size(); ++slot) {
        hud.quickSlots[slot] = std::string_view{quickBarNames_[slot]};
    }
    hud.quickSelected = quickSlot_;
    hud.quickEquipped = equippedQuickSlot();
    hud.quickBarFade = quickBarAnim_.value();
    // UI-EA (LANE HUD): the Q-hold tutor toast, riding the strip.
    hud.wheelHint = std::string_view{wheelHintText_};
    hud.wheelHintFade = wheelHint_.value();
    // S9. Whether the room can see you, and the lock under the wire. Both on
    // edges, both empty when they have nothing to say -- the right-hand stack
    // for the first, the bottom band for the second. Both read their own
    // cache and fade the identical way roomLabel just did, above.
    hud.stealthLabel = std::string_view{stealthCache_};
    hud.stealthFade = stealthAnim_.value();
    hud.lockLabel = std::string_view{lockCache_};
    hud.lockFade = lockAnim_.value();
    // THE CROSSHAIR PASS. THE AIM PROMPT, on the reticle rather than along the
    // bottom edge -- the owner's own sentence is at the top of hud.hpp. The
    // verb still changes to PICKPOCKET the instant the player crouches facing
    // somebody (#85's whole point, and Eli's brief: "the player must SEE what
    // pressing it will do before they press it"); what the crosshair pass adds
    // is the name of whoever that is.
    //
    // THE KEY NAME IS READ off the same binding row the CONTROLS page
    // prints, so a rebound Interact renames itself on the crosshair by the
    // act of being rebound -- and SHIP NOTE MOVE 3 made it the half of that
    // row belonging to the device that last spoke: "E - TALK" with a
    // keyboard in hand, "A - TALK" the moment a pad button lands, live.
    hud.aimKey = promptKeyName(promptKey(controls_, Action::Interact, promptDevice_));
    hud.aimVerb = std::string_view{interactCache_};
    hud.aimSubject = std::string_view{interactSubjectCache_};
    hud.aimNote = std::string_view{interactNoteCache_};
    hud.aimKind = static_cast<int>(interactKindCache_);
    hud.interactFade = interactAnim_.value();
    // ACTION-COMBAT BUILD (section 5, channel 1). THE RETICLE'S CHARGE READOUT,
    // read straight off the sim's own swing state and fed INDEPENDENT of the
    // interact prompt above -- the reticle draws during a hold even when
    // nothing is in reach (aimVerb empty), which drawAim's interact-only
    // early-return could not do. The fraction is the hold's progress toward the
    // hard threshold, the tier and the on-line flag are the sim's live reads.
    {
        float held = static_cast<float>(tavern_->playerChargeSteps()) /
                     static_cast<float>(sim::kHardSwingHoldSteps);
        held = held < 0.0F ? 0.0F : (held > 1.0F ? 1.0F : held);
        hud.aimChargeFrac = held;
        hud.aimChargeHard = tavern_->playerChargeHard();
        hud.aimChargeOnLine = tavern_->playerSightlineTarget();
    }
    // The rung, and what the line wants next. Bottom-left, over the health bar.
    hud.guildLabel = std::string_view{guildCache_};
    hud.guildFade = guildAnim_.value();
    hud.objectiveLabel = std::string_view{objectiveCache_};
    hud.objectiveFade = objectiveAnim_.value();
    // S10. Where the case stands and where it wants you next: ONE row,
    // bottom-left, in the one slot of that stack provably outside the exclusion
    // rectangle. What the ward CALLS you for the work is not on the HUD at all
    // -- it is on the casebook's own page, because a title is something you
    // look up and not something you need every frame.
    //
    // ONE SLOT, TWO TENANTS -- AND polish-1 GAVE THEM A SLOT EACH.
    //
    // The case row used to sit at y - 16*scale off the health bar, which IS
    // height - margin - 23*scale, the same pixel row the alert has used since
    // S6; the first S10 capture shipped a clue printed straight through "CASE
    // 1/4 > ...", and the fix was to suppress the case row for the six seconds
    // a message is up. The bottom band hands out slots now (see BottomBand in
    // hud.cpp), the alert takes one before the case is offered one, and the two
    // of them can no longer be given the same pixels by arithmetic. So the
    // suppression is gone and both are shown.
    // And who put you on the floor last, which is the one thing on the HUD that
    // is about somebody else rather than about you.
    hud.rivalLabel = std::string_view{rivalCache_};
    hud.rivalFade = rivalAnim_.value();
    // UI-EA (LANE HUD): the HUNTING word is off the label (rank 6 -> 3); the
    // fact rides this bool and the row's red ink, read fresh off the same
    // Nemesis the line is built from so the two can never disagree.
    if (const sim::Nemesis* worstRival = tavern_->nemesis().worst(); worstRival != nullptr) {
        hud.rivalHunts = worstRival->hunts();
    }
    hud.showCompass = !conversing;
    // DISTRICT PHASE D: THE THRESHOLD MOMENT, under the ribbon it shares a
    // band with. See HudState::placePlate and drawPlacePlate.
    //
    // `conversing ? 0 : ...` IS THE SAME LINE showCompass JUST WROTE, on
    // purpose. The plate lives in the compass's own band and it stands down
    // under the compass's own rule: while a panel owns the screen there is no
    // ribbon for it to sit under and nothing up there but somebody else's
    // page. syncPanelAnim() has already put the toggle's target down for the
    // same cases (and zeroed the countdown, so nothing resurfaces when the
    // panel closes); this is what stops the handful of frames it spends easing
    // out from being painted over the panel that suppressed it.
    hud.placePlate = std::string_view{placePlateName_};
    // THE CASEBOOK PASS. The lead-opened notice, on the identical three fields
    // and the identical conversing gate -- see HudState::casePlate.
    hud.casePlate = std::string_view{casePlateText_};
    // AND ZERO THE INSTANT THE RUNG PLATE IS WANTED (casePlateFadeNow): the
    // slot waits behind the plate, and its own ease-out must not cross the
    // plate's rise.
    hud.casePlateFade = conversing ? 0.0F : casePlateFadeNow();
    hud.casePlateDrift = casePlateAnim_.target() ? -(1.0F - casePlateAnim_.value())
                                                 : (1.0F - casePlateAnim_.value());
    hud.placePlateFade = conversing ? 0.0F : placePlateFadeNow();
    composePullHud(hud);  // THE PULL PACK: the one draw hook (render/pull.hpp).
    // THE SIGN COMES OFF THE TOGGLE'S OWN TARGET, so the plate rises THROUGH
    // its settled row rather than sliding back down the way it came -- see
    // HudState::placePlateDrift. Rising (target true): still below, closing on
    // zero. Fading (target false): already past, drifting on up and out.
    hud.placePlateDrift = placePlateAnim_.target() ? -(1.0F - placePlateAnim_.value())
                                                   : (1.0F - placePlateAnim_.value());
    // A bouncer's warning outranks anything the player did to themselves: it is
    // the one line in this game they must not miss.
    const bool warned = bouncerWarned();
    if (warned) {
        hud.alert = std::string_view{tavern_->lastWarning()};
    } else if (!conversing) {
        hud.alert = std::string_view{message_};
    }
    // TASK #83. Eased in step()/syncPanelAnim(), not here -- see
    // HudState::alertFade's own note. 1 when nothing has changed since
    // construction, which is the whole of what every caller before this field
    // existed drew. UNDER THE RUNG PLATE the field keeps this ease and the
    // words take the plate's weight on their ink (alertInkWeightNow -- the
    // product is what alertFadeNow reads back); and the row steps DOWN to
    // the plate's own register while the plate is on screen and for a line
    // capped under it (alertStepsDownNow), never a tier above the plate --
    // the smaller of the plate's register and the HUD's minor scale
    // (alertStepScale; under the height-only register a square window's
    // minor scale was a tier above the card, and since the cells pass no
    // window's is, so in play it is the minor scale).
    hud.alertFade = alertAnim_.value();
    hud.alertInkWeight = alertInkWeightNow();
    hud.alertStepsDown = alertStepsDownNow();
    hud.alertStepScale = rungPlateMetric(target.width(), target.height()).scale;
    // VOICE LANE. The street's own line, on its own toggle, down while a
    // panel owns the screen (the partner's lines are the panel's).
    hud.voiceName = std::string_view{voice_.current().name};
    hud.voiceLine = std::string_view{voice_.current().text};
    // A live line FADES under a panel opening over it rather than cutting
    // to nothing on E: its own toggle is already easing down (the target
    // drops with conversingNow), and the panel's own rise caps it so the
    // two cross rather than overprint. The full-page surfaces below zero
    // it outright, as they do the alert.
    // AND AT THE RUNG PLATE'S WEIGHT: a passer-by's bark or Cull's farewell
    // sat at full strength at the foot of the trade and law frames, wider
    // than the plate, while the alert row beside it was halved (all three
    // critics' finding). The same weight, on the same fade.
    hud.voiceFade = conversing ? std::min(voiceAnim_.value(), 1.0F - panelAnim_.value())
                               : voiceAnim_.value();
    hud.voiceInkWeight = alertInkWeightNow();
    // AND ITS OWN STAND-DOWN: the plate's register and the quarter rails
    // while the plate is on screen and for a line capped under it, so a
    // capped bark finishes small and never swells (the auditor, on the
    // square windows).
    hud.voiceStepsDown = voiceStepsDownNow();
    // INNOVATION SPRINT ITEM #3. See HudState::alertPulse's own header.
    hud.alertPulse = alertPulse_.value();
    hud.caseLabel = std::string_view{caseCache_};
    hud.caseFade = caseAnim_.value();
    hud.showHealth = !conversing;
    // AND THE BOTTOM BAND IS THE TOPIC LIST'S, WHOLE. The alert used to be
    // drawn over it and S7 shipped the frame that proves it -- see
    // DialogueViewState::alert. It moves into the conversation's top band while
    // one is open, which is sized from what it draws, so a warning shouted
    // across the room is still read and nothing is drawn on top of anything.
    hud.showAlert = !conversing;
    // THE WARD MAP (core action #13) -- its own full-screen surface, drawn by
    // map_view.hpp's drawDistrictMap, exempt from the centre-clear rule for
    // exactly the reason the tiled Menu is (menu_view.hpp's own header):
    // there is nobody to look at while a map is up. The close tail draws too
    // (its own EasedToggle still above zero) unless another page has already
    // taken the frame -- conversingNow() is false only when nothing else is
    // up, so a map closed INTO the tiled Menu hands over immediately.
    if (districtMapOpen_ || (districtMapAnim_.value() > 0.0F && !conversing)) {
        // THE MAP PASS. The page is a COMPOSED PANE with a cursor now, so the
        // state it draws from is built in one place (districtMapState()) that a
        // case can read, rather than assembled inline where nothing but a
        // screenshot could ever check it.
        DistrictMapState plan = districtMapState();
        // Cross-lane contract (b), wired at integration: FLOW arms the pulse
        // at commit routing, PAGES renders it off the state field -- this is
        // the one assignment that joins them, per composition.
        plan.commitPulse = commitPulse_.value();
        plan.tutor = mapTutor_.value();  // contract (c): the raised verb words
        // THE HUD STANDS DOWN UNDER IT, which the old static page could get
        // away with not doing and this one cannot. The compass ribbon prints
        // the place name across the top centre and the clock/purse stack sits
        // top right -- exactly where this composition's breadcrumb row and its
        // right-aligned readout now live, and the first capture of this page
        // had the ward's own street name printed straight through its
        // breadcrumb. Same ruling the controls page already made and for the
        // same reason: nothing is being discussed and no door is about to shut
        // while a map is up, and a warning still outranks the page -- it is
        // routed into the page's own header band rather than painted over it.
        hud.timeOfDaySeconds = -1;
        hud.coin = -1;
        hud.showCompass = false;
        hud.placePlateFade = 0.0F;
        hud.casePlateFade = 0.0F;
        hud.showHealth = false;
        hud.showAlert = false;
        hud.voiceFade = 0.0F;
        if (warned) {
            // The bouncer's warning outranks a map read -- the same routing
            // the tiled Menu gives its journal tile, below. It takes the
            // breadcrumb row, which the composition holds open regardless, so
            // nothing under it moves when a bouncer starts talking.
            plan.alert = tavern_->lastWarning();
        }
        if (furniture) {
            drawDistrictMap(target, plan);
            drawHud(target, hud);
            dipTravelSeam(target, travelFadeAnim_.value());
            composeDeathCeremony(target);
            composeRopeCeremony(target);
            composeTakenPlate(target);
        }
        panelTailTiles_ = false;
        panelTailCourt_ = false;
        return stats;
    }
    // PANES PASS: THE CONTROLS PAGE IS THE FIRST SURFACE DRAWN IN THE
    // TERMINAL-PANEL REGISTER (render/keys_page.hpp, on render/panel.hpp's
    // vocabulary) rather than through the single conversation panel. It is a
    // FULL-PAGE composition, like the tiled Menu and like every one of the
    // owner's reference frames, and for the same reason menu_view.hpp already
    // gives: nobody is standing in front of you while you read your own key
    // bindings, so there is nothing in the centre of the frame to leave clear.
    //
    // THE CLOCK AND THE PURSE STAND DOWN WHILE IT IS UP. That is this build's
    // existing rule about overlays, applied to a new one: the top-right stack
    // is drawn AFTER the panel and would otherwise land in the tab row, on top
    // of the build readout that row right-aligns. Nothing is being discussed
    // and no door is about to shut, so the hour and the purse have nothing to
    // say for the few seconds this page is open. Everything else about the HUD
    // is untouched, and a warning still outranks the page -- it is routed into
    // the page's own header band instead of being painted over it.
    if (keysOpen_) {
        KeysPageState page = keysPageState();
        page.commitPulse = commitPulse_.value();  // contract (b), see the map pass
        page.tutor = keysTutor_.value();          // contract (c)
        page.openAmount = panelAnim_.value();
        page.open = page.open || panelAnim_.value() > 0.0F;
        if (warned) {
            page.alert = std::string(tavern_->lastWarning());
        }
        hud.timeOfDaySeconds = -1;
        hud.coin = -1;
        if (furniture) {
            drawKeysPage(target, page);
            drawHud(target, hud);
            dipTravelSeam(target, travelFadeAnim_.value());
            composeDeathCeremony(target);
            composeRopeCeremony(target);
            composeTakenPlate(target);
        }
        panelTailTiles_ = false;
        panelTailCourt_ = false;
        return stats;
    }
    // MORROWIND ROUND: THE TILED MENU IS A DIFFERENT SURFACE FROM THE SINGLE
    // CONVERSATION PANEL, drawn by a different function (menu_view.hpp's
    // drawMenuTiles rather than dialogue_view.hpp's drawDialogue) because it
    // deliberately does NOT respect the HUD's centre-clear rule the way every
    // other overlay in this build still does -- see menu_view.hpp's own
    // header on why a Morrowind-style overview is exempt and a live
    // conversation is not.
    // THE CASEBOOK PASS. THE JOURNAL TILE IS A COMPOSED PAGE NOW, drawn by
    // casebook_page.hpp in the terminal-panel register rather than as the
    // bottom third of four tiles.
    //
    // WHY THE OTHER THREE TILES ARE UNTOUCHED, said plainly rather than left to
    // be discovered: this phase is contracted for the casebook and the casebook
    // alone, and converting Character/Chart/Letters would be three more surfaces
    // this pass has no frames of and no cases for. The seam that leaves is real
    // -- the book is a composed page and its three siblings are still tiles --
    // and a player only ever sees one of the two at a time, because the Menu
    // draws whichever tile has FOCUS. The bumpers still step between them and
    // the book's own nav band says so.
    //
    // THE HUD STANDS DOWN THE SAME WAY IT DOES UNDER THE WARD MAP, and for the
    // same reason: this composition's breadcrumb runs across the top left and
    // its readout is right-aligned in the tab row, which is exactly where the
    // compass ribbon and the clock/purse stack live. A warning still outranks
    // the page -- it is routed into the page's own header band, which the
    // composition holds open regardless, so nothing under it moves when a
    // bouncer starts talking.
    if (casebookPageOpen()) {
        CasebookPageState page = casebookPageState();
        page.commitPulse = commitPulse_.value();  // contract (b), see the map pass
        page.tutor = casebookTutor_.value();      // contract (c)
        page.openAmount = panelAnim_.value();
        page.open = page.open || panelAnim_.value() > 0.0F;
        if (warned) {
            page.alert = std::string(tavern_->lastWarning());
        }
        hud.timeOfDaySeconds = -1;
        hud.coin = -1;
        hud.showCompass = false;
        hud.placePlateFade = 0.0F;
        hud.casePlateFade = 0.0F;
        hud.showHealth = false;
        hud.showAlert = false;
        hud.voiceFade = 0.0F;
        if (furniture) {
            drawCasebookPage(target, page);
            drawHud(target, hud);
            dipTravelSeam(target, travelFadeAnim_.value());
            composeDeathCeremony(target);
            composeRopeCeremony(target);
            composeTakenPlate(target);
        }
        panelTailTiles_ = false;
        panelTailCourt_ = false;
        return stats;
    }
    // UI-EA-SPEC sec. 3 rule 4 -- CLOSE HONESTY: the tiled Menu's close tail
    // draws as the TILES easing out, not as the empty single panel the old
    // handover fell through to (the seam the comment at the dialogue branch
    // below used to admit). panelTailTiles_ is drawFrame's own memo of what
    // it drew the panel fade for last frame -- see its header in session.hpp.
    if (casebookOpen_ ||
        (panelTailTiles_ && !conversing && panelAnim_.value() > 0.0F)) {
        MenuTileState tiles;
        tiles.open = true;
        tiles.character = characterPanelView();
        tiles.map = mapPanelView();
        tiles.letters = lettersPanelView();
        tiles.journal = journalPanelView();
        if (warned) {
            // THE BOUNCER'S WARNING, ROUTED INTO THE JOURNAL TILE. The tiled
            // Menu has no single top band of its own to carry it the way a
            // conversation or one of #85's six pages did -- see
            // DialogueViewState::alert's own header -- so it lands on the
            // one tile that is always on screen regardless of focus and
            // already reads as "your own notes", the same place a warning
            // interrupted a casebook read before this round.
            tiles.journal.alert = tavern_->lastWarning();
        }
        tiles.focus = menuFocus_;
        tiles.phase = phase;
        // NINE AND THE STICKS: the hub's foot, in the live hand's keycaps --
        // the ring is learnable from every page of it, this one included.
        tiles.navMoveKeys = std::string(promptMoveKeys(promptDevice_));
        tiles.navPageKeys = std::string(promptPageKeys(promptDevice_));
        tiles.confirmKey = std::string(promptConfirmKey(promptDevice_));
        tiles.closeKey =
            promptDevice_ == InputDevice::Pad
                ? std::string(promptBackKey(InputDevice::Pad))
                : std::string(promptLabel(controls_, Action::Menu, InputDevice::KeyboardMouse));
        // TASK #83's OWN EASE, REUSED. See the identical note on the
        // single-panel path below.
        tiles.openAmount = panelAnim_.value();
        // INNOVATION SPRINT ITEM #2. See MenuTileState::characterFocus's own
        // header -- each tile's border eases toward or away from focus off
        // its own EasedToggle rather than snapping the instant menuFocus_
        // changes.
        tiles.characterFocus = characterFocusAnim_.value();
        tiles.mapFocus = mapFocusAnim_.value();
        tiles.lettersFocus = lettersFocusAnim_.value();
        tiles.journalFocus = journalFocusAnim_.value();
        if (furniture) {
            drawMenuTiles(target, tiles);
            drawHud(target, hud);
            dipTravelSeam(target, travelFadeAnim_.value());
            composeDeathCeremony(target);
            composeRopeCeremony(target);
            composeTakenPlate(target);
        }
        // The memo: this frame's panel fade belonged to the tiles. Only the
        // LIVE menu re-arms it -- a tail frame keeps it as-is, so the tail
        // keeps drawing tiles until panelAnim_ settles and the condition
        // above goes quiet on its own.
        panelTailTiles_ = panelTailTiles_ || casebookOpen_;
        return stats;
    }
    // UI-EA-SPEC 1.7 (LANE PAGES, strip->card): pause, wait, options and the
    // grimoire draw as the composed card now, not as a HUD strip -- the ship
    // note's one remaining strip surface, converted with its family. Input
    // and close routing are untouched (the flags, cursors and digit windows
    // are exactly the strip's own); the CLOSE fade still runs through the
    // panel path below until the close-honesty pass (sec. 3 rule 4, LANE
    // FLOW) teaches it to fade as what it was.
    if (stripCardOpen()) {
        CreationPage card = stripCard();
        card.alpha = panelAnim_.value();
        // THE CLOCK KEEPS THE PAUSE (spec #34: "rows, clock, title stay") --
        // integration kept HUD's ruling over the family stand-down the other
        // page passes make: the pause stack is where a player stands still to
        // read the hour, and HUD's own earned-text logic already wakes the
        // clock for it. The purse still has nothing to say here.
        hud.coin = -1;
        if (furniture) {
            drawCreationPage(target, card);
            drawHud(target, hud);
            dipTravelSeam(target, travelFadeAnim_.value());
            composeDeathCeremony(target);
            composeRopeCeremony(target);
            composeTakenPlate(target);
        }
        return stats;
    }
    // JUSTICE BUILD (HEARING PAGE LANE). THE HEARING PAGE: a full-page
    // composition in the terminal register (hearing_page.hpp), drawn into
    // the framebuffer exactly as the casebook page is, so the 3D overlay
    // composites it unchanged. Below the pause card on purpose -- PAUSE is
    // the one page allowed over the bench -- and above the conversation
    // panel, which cannot be open under it (openCourt ends any talk). The
    // HUD stands down the way it does under every composed page; a bouncer's
    // warning still outranks it and rides the page's own header band.
    if (courtOpen_ || (panelTailCourt_ && !conversing && panelAnim_.value() > 0.0F)) {
        HearingPageState page = hearingPageState();
        page.openAmount = panelAnim_.value();
        page.open = page.open || panelAnim_.value() > 0.0F;
        if (warned) {
            page.alert = std::string(tavern_->lastWarning());
        }
        hud.timeOfDaySeconds = -1;
        hud.coin = -1;
        hud.showCompass = false;
        hud.placePlateFade = 0.0F;
        hud.casePlateFade = 0.0F;
        hud.showHealth = false;
        hud.showAlert = false;
        hud.voiceFade = 0.0F;
        if (furniture) {
            drawHearingPage(target, page);
            drawHud(target, hud);
            dipTravelSeam(target, travelFadeAnim_.value());
            composeDeathCeremony(target);
            composeRopeCeremony(target);
            composeTakenPlate(target);
        }
        panelTailTiles_ = false;
        // The memo: this frame's panel fade belonged to the bench. Only the
        // LIVE page re-arms it, exactly as the tiles' memo works.
        panelTailCourt_ = panelTailCourt_ || courtOpen_;
        return stats;
    }
    DialogueViewState panel = dialogueView();
    if (conversing && warned) {
        panel.alert = tavern_->lastWarning();
    }
    // TASK #83. THE PANEL EASES OPEN AND CLOSED INSTEAD OF POPPING.
    // panelAnim_.value() is what drawDialogue actually fades against -- see
    // DialogueViewState::openAmount. `panel.open` is ALSO forced true for as
    // long as any of that fade is still on screen, because dialogueView()
    // reports the panel closed the instant the LAST overlay flag goes false,
    // and a close animation needs a few more frames of "yes, still drawing"
    // after that to have anything left to fade. Once panelAnim_ settles at 0
    // this is exactly `panel.open` again, which is the pre-existing behaviour
    // for every caller and every test that never heard of this pass.
    //
    // UI-EA-SPEC sec. 3 rule 4: the tiled Menu's closing tail no longer
    // lands here -- the branch above catches it off panelTailTiles_ and
    // fades the TILES, which is the close-honesty rule ("a page fades as
    // what it was"). What still lands here is every single-panel surface's
    // own tail -- Keys, Options, Wait, a conversation -- which genuinely
    // WAS this panel, so fading as it is honest. The memo is cleared here
    // whenever a live panel draws, so a keys tail after a menu visit cannot
    // resurrect the tiles.
    panel.openAmount = panelAnim_.value();
    if (conversing) {
        panelTailTiles_ = false;
        panelTailCourt_ = false;
    }
    panel.open = panel.open || panelAnim_.value() > 0.0F;
    // The panel FIRST, the HUD over it: a bouncer's warning has to survive
    // being told mid-conversation, and it is the one line that outranks a menu.
    if (furniture) {
        drawDialogue(target, panel);
        drawHud(target, hud);
        // THE RUNG PLATE: over the HUD, under the travel seam, the ceremonies
        // and the court's own plate -- everything below outranks it by being
        // painted after it. Only on THIS path, the world's: every composed
        // page above returned before here, and a page owning the screen is
        // exactly what dismisses the plate (stepRungPlate's rule).
        composeRungPlate(target);
        dipTravelSeam(target, travelFadeAnim_.value());
        composeDeathCeremony(target);
        composeRopeCeremony(target);
        composeTakenPlate(target);
    }
    return stats;
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
