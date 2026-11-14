// Session district map: the map overlay, the travel plan and placing the body.
#include "session_internal.hpp"

// THE AUDIO WIRING PASS, as in session.cpp: this file speaks to the audio engine in ids only; see
// setAudio() in session.hpp and the determinism note in audio_engine.hpp.
#include "granadad/audio/audio_engine.hpp"

namespace granadad::render {

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

}  // namespace granadad::render
