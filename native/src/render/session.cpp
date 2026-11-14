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
