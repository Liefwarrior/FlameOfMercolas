// Session cases: the courier sheet and the eviction case steps.
#include "session_internal.hpp"

namespace granadad::render {

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

}  // namespace granadad::render
