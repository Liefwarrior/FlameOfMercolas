// Session: construction, the setters, the camera/sky/weather views, the lamps and the world hash.
#include "session_internal.hpp"

// THE AUDIO WIRING PASS. The one render file that speaks to the audio engine,
// and it speaks only ids -- see setAudio()'s header in session.hpp, and the
// determinism note in audio_engine.hpp for why this include may never spread
// to a header or to anything under src/sim.
#include "granadad/audio/audio_engine.hpp"

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

Session::Session(const SessionConfig& config)
    : config_(resolved(config)),
      world_(content::loadWorldFile(config_.contentDir / "maps" / "baked" /
                                    (config_.world + ".trojsav"))),
      tiles_(std::make_unique<sim::TileQuery>(world_)),
      atlas_(TileAtlas::load(config_.contentDir)) {
    lamps_ = loadLamps(config_.contentDir, config_.world);
    glow_ = LampGlow::build(*tiles_, lamps_);
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

}  // namespace granadad::render
