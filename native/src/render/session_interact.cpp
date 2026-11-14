// Session interaction: examine, interact and its target resolution, take, search, lift, steal, lock picking.
#include "session_internal.hpp"

// THE AUDIO WIRING PASS, as in session.cpp: this file speaks to the audio engine in ids only; see
// setAudio() in session.hpp and the determinism note in audio_engine.hpp.
#include "granadad/audio/audio_engine.hpp"

namespace granadad::render {

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

}  // namespace granadad::render
