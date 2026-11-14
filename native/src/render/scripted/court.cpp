// Scripted capture drivers: court.
#include "scripted.hpp"

namespace granadad::render {

namespace {

/// A patron Cull can SEE the player beside, and the body stood there:
/// runWatchHaltLine's own search, nearest to Cull first. Returns the mark's
/// id, or -1 with the note filled.
[[nodiscard]] std::int32_t standBesidePatronInCullsSight(Session& session, std::int32_t cullId) {
    const sim::Tavern& tavern = session.tavern();
    const sim::Actor* cull = tavern.actorById(cullId);
    if (cull == nullptr || !cull->present()) {
        gCourtNote += " cull=absent";
        return -1;
    }
    std::vector<std::pair<std::int32_t, std::int32_t>> candidates;
    for (const sim::Actor& actor : tavern.actors()) {
        if (!actor.present() || sim::isFloored(actor.activity()) ||
            actor.role() != sim::ActorRole::Patron || tavern.isProfessional(actor) ||
            actor.band() != session.body().band()) {
            continue;
        }
        candidates.emplace_back(actor.distanceTo(cull->x(), cull->y()), actor.id());
    }
    std::sort(candidates.begin(), candidates.end());
    for (const auto& [distance, id] : candidates) {
        (void)distance;
        const sim::Actor* patron = tavern.actorById(id);
        if (patron == nullptr || !patron->present()) {
            continue;
        }
        const std::int32_t sides[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
        for (const auto& side : sides) {
            const std::int32_t px = patron->tileX() + side[0];
            const std::int32_t py = patron->tileY() + side[1];
            if (!session.tiles().standable(px, py, patron->band())) {
                continue;
            }
            walkToTile(session, px, py);
            if (session.body().tileX() != px || session.body().tileY() != py) {
                continue;
            }
            patron = tavern.actorById(id);
            if (patron == nullptr) {
                break;
            }
            facePlayerAndSync(session, patron->x(), patron->y());
            session.stepMany(sim::MoveInput{}, sim::kStepsPerSecond);
            cull = tavern.actorById(cullId);
            if (cull != nullptr && cull->present() && tavern.noticeBy(*cull).seen) {
                return id;
            }
            break;
        }
    }
    gCourtNote += " mark=none-in-culls-sight candidates=" + std::to_string(candidates.size());
    return -1;
}

/// Steel up in Cull's sight and stand still until he takes you at reach --
/// the feel build's Closing on VIOLENCE, no glance gate, no die. True once
/// the arrest has happened.
[[nodiscard]] bool standUntilTakenBy(Session& session, std::int32_t cullId) {
    sim::Tavern& tavern = session.tavern();
    tavern.setPlayerCombat(sim::Weapon::Edged, sim::Intent::Subdue);
    session.setBlocking(true);
    session.stepMany(sim::MoveInput{}, 1);
    session.setBlocking(false);
    session.stepMany(sim::MoveInput{}, 1);
    for (int second = 0; second < 90 && !tavern.lastArrest().happened; ++second) {
        if (const sim::Actor* cull = tavern.actorById(cullId); cull != nullptr && cull->present()) {
            // In his face: distance zero is inside reach whatever the room
            // does with the line. The room is told where the body is by the
            // step that follows, exactly as every relocation is.
            session.placeBodyAt(cull->tileX(), cull->tileY(), cull->band());
        }
        session.stepMany(sim::MoveInput{}, sim::kStepsPerSecond);
    }
    return tavern.lastArrest().happened;
}

/// Puts `markId` down for good through the Attack verbs -- hard swings until
/// the roster says Dead. True on a corpse.
[[nodiscard]] bool killWithHardSwings(Session& session, std::int32_t markId) {
    sim::Tavern& tavern = session.tavern();
    tavern.setPlayerCombat(sim::Weapon::Edged, sim::Intent::Kill);
    for (int swings = 0; swings < 40; ++swings) {
        const sim::Actor* mark = tavern.actorById(markId);
        if (mark == nullptr) {
            return false;
        }
        if (mark->activity() == sim::Activity::Dead) {
            return true;
        }
        for (int i = 0; i < sim::kRecoilSteps + sim::kBlockStaggerSteps +
                                sim::kHardSwingRecoverySteps + 8 &&
                        (tavern.playerRecoilSteps() > 0 || tavern.playerBlockStaggerSteps() > 0 ||
                         !tavern.playerCombatIdle());
             ++i) {
            session.stepMany(sim::MoveInput{}, 1);
        }
        if (!closeOnAndFace(session, markId)) {
            return false;
        }
        session.attackDown();
        session.stepMany(sim::MoveInput{}, sim::kHardSwingHoldSteps + 1);
        session.attackUp();
        session.stepMany(sim::MoveInput{}, 1);
    }
    return false;
}

}  // namespace

namespace scripted {

[[nodiscard]] int runCourtLine(Session& session, const std::string& ending) {
    int landed = 0;
    gCourtNote.clear();
    const sim::Tavern& tavern = session.tavern();
    const sim::CrimeLedger& crimes = tavern.dialogue().crimes();
    // In through the door to the bar, the smoke's own route-walk.
    walkToTile(session, sim::gull::kBartenderX, sim::gull::kBartenderY + 1);
    session.closeConversation();
    const bool hangs = courtEndingHangs(ending);

    if (ending == "hand") {
        // THE ROOFS FIRST: the Skyrunners' oath off Finch (the skyrun line's
        // own first beat; he keeps the snug after ten), so the paper the
        // lifts earn asks for the hand -- a Skyrunner's first. Then the same
        // road as everybody else's.
        if (speakTo(session, "Finch")) {
            pick(session, sim::TopicKind::Join);
            session.closeConversation();
        }
        const std::int32_t roofs = tavern.dialogue().factions().indexOf("skyrunners");
        if (roofs < 0 || !tavern.dialogue().standings().isMember(roofs)) {
            gCourtNote += " oath=refused";
            return landed;
        }
        gCourtNote += " oath=sworn";
    }

    if (hangs && ending == "streetblood") {
        // STREET SENSES leg (b): THE KILLING WAS ON THE STREET, already on the
        // record (runStreetAssault's own hard swings on a docker in the
        // crowd). Nothing to kill here; the corpse lies on the quay and the
        // ward knows whose hand.
        if (!crimes.murderer()) {
            gCourtNote += " kill=none";
            return landed;
        }
        gCourtNote += " slew=" + tavern.slainName() + " saw=" + std::to_string(crimes.slewWitnesses()) +
                      (tavern.slainOnStreet() ? " where=street" : " where=gull");
        ++landed;  // 1: a corpse on the street, the ward knows whose hand
    } else if (hangs) {
        // THE KILLING, BEFORE THE WATCH DRINKS: the line starts at eight, the
        // room full and Cull not yet on his stool, so the corpse is made in
        // front of the room and not under a watchman's hand mid-swing. The
        // mark is the upright patron with the most people close enough to
        // see it done.
        std::int32_t markId = -1;
        int bestNear = -1;
        for (const sim::Actor& actor : tavern.actors()) {
            if (!actor.present() || sim::isFloored(actor.activity()) ||
                actor.role() != sim::ActorRole::Patron || tavern.isProfessional(actor) ||
                actor.band() != session.body().band()) {
                continue;
            }
            int near = 0;
            for (const sim::Actor& other : tavern.actors()) {
                if (other.id() != actor.id() && other.present() &&
                    !sim::isFloored(other.activity()) && other.role() != sim::ActorRole::Vermin &&
                    other.band() == actor.band() &&
                    std::max(std::abs(other.tileX() - actor.tileX()),
                             std::abs(other.tileY() - actor.tileY())) <= 4) {
                    ++near;
                }
            }
            if (near > bestNear) {
                bestNear = near;
                markId = actor.id();
            }
        }
        if (markId < 0 || !killWithHardSwings(session, markId) || !crimes.murderer()) {
            gCourtNote += " kill=failed";
            return landed;
        }
        gCourtNote += " slew=" + tavern.slainName() + " saw=" + std::to_string(crimes.slewWitnesses());
        ++landed;  // 1: a corpse on the roster, the ward knows whose hand
        if (ending == "bloodtag") {
            // THE TAG, photographed: WANTED FOR BLOOD on the row, the room
            // still around you, nobody's hand on you yet. The row's own
            // ease, fully up.
            session.stepMany(sim::MoveInput{}, 16);
            gCourtNote += " heat=" + std::to_string(crimes.heat());
            if (session.heatLine().rfind("WANTED FOR BLOOD", 0) != 0) {
                gCourtNote += " tag=none";
                --landed;
            }
            return landed;
        }
    }
    if (hangs) {
        // THE WAIT: the wait page's own jump to the hour the Watch drinks,
        // the body out of the fight's reach first (skipToHour is refused
        // with fists up) and the heat cooling honestly through the hours --
        // a murder's sixty is still paper at ten. STREET SENSES leg (b): a
        // street killing made at four has cooled past the paper by ten, and
        // that is honest -- the blood never cools (murderer_ stands), Cull
        // closes on the steel in your hand, and the sheet asks for the rope
        // off the blood whether or not the paper is out. So the paper check
        // is the Gull line's alone.
        session.tavern().lowerPlayerHands();
        session.tavern().setPlayerCombat(sim::Weapon::Fists, sim::Intent::Subdue);
        session.skipToHour(22);
        session.stepMany(sim::MoveInput{}, sim::kStepsPerSecond);
        if (!crimes.warrant() && ending != "streetblood") {
            gCourtNote += " paper=lapsed";
            return landed;
        }
    }

    const sim::Actor* cull = actorNamed(session, "Watchman Cull");
    if ((cull == nullptr || !cull->present()) && !hangs) {
        // THE WATCH IS NOT ON HIS STOOL YET -- a line before this one (the
        // flame's, at eight) left the clock early. The wait page's own jump
        // to the hour he keeps, hands down first as the rope line does.
        session.tavern().lowerPlayerHands();
        session.tavern().setPlayerCombat(sim::Weapon::Fists, sim::Intent::Subdue);
        session.closeConversation();
        session.skipToHour(23);
        session.stepMany(sim::MoveInput{}, sim::kStepsPerSecond);
        gCourtNote += " waited=23";
        cull = actorNamed(session, "Watchman Cull");
    }
    if (cull == nullptr || !cull->present()) {
        gCourtNote += " cull=absent";
        return landed;
    }
    const std::int32_t cullId = cull->id();
    const std::int32_t markId = standBesidePatronInCullsSight(session, cullId);
    if (markId < 0) {
        return landed;
    }

    if (!hangs) {
        // THE PAPER, THROUGH THE VERB: a hand in a coat in Cull's sight until
        // the row reads WANTED. Eight heat a witnessed lift; a caught hand
        // is witnessed too. The mark's purse runs out before the paper does,
        // so the line moves to the next patron in Cull's sight when it must.
        for (int lifts = 0; lifts < 40 && !crimes.warrant(); ++lifts) {
            session.lift();
            session.stepMany(sim::MoveInput{}, 4);
            if (const sim::Actor* mark = tavern.actorById(markId);
                mark == nullptr || mark->coin() <= 0) {
                if (standBesidePatronInCullsSight(session, cullId) < 0) {
                    break;
                }
            }
        }
        gCourtNote += " heat=" + std::to_string(crimes.heat());
        if (!crimes.warrant() || session.heatLine().rfind("WANTED", 0) != 0) {
            gCourtNote += " paper=none";
            return landed;
        }
        ++landed;  // 1: WANTED on the row, off real lifts
        if (ending == "wanted") {
            // THE TAG, photographed: paper out, nobody's hand on you yet.
            return landed;
        }
    }

    // TAKEN AT REACH, WITH PAPER. The arrest opens the hearing on the ledger
    // and the ARREST BEAT on the screen: the officer's own line on the row
    // with his hand on you, then the plate, then the page.
    if (!standUntilTakenBy(session, cullId) || tavern.lastArrest().sentence == sim::Sentence::Fined) {
        gCourtNote += " arrest=" + std::string(tavern.lastArrest().happened ? "paperless" : "none");
        return landed;
    }
    gCourtNote += " ask=" + std::string(sim::sentenceName(tavern.lastArrest().sentence));
    if (!session.takenBeatUp() || session.courtOpen() || !tavern.playerInside() ||
        session.lastMessage().rfind(tavern.lastArrest().officer + ": ", 0) != 0) {
        gCourtNote += " beat=no";
        return landed;
    }
    ++landed;  // 2: taken with paper, his line on the row, his hand on you
    if (ending == "cull") {
        // The row's own ease, fully up, well inside the beat's hold. A
        // capture of this ending wants --settle-steps=0: the rest steps
        // would walk the beat through to the page.
        session.stepMany(sim::MoveInput{}, 16);
        return landed;
    }
    // THE PLATE: the beat spent to the cut, one step at a time, so the frame
    // is the plate's first.
    for (int step = 0; step < kTakenOfficerSteps + 2 && !session.takenPlateUp(); ++step) {
        session.stepMany(sim::MoveInput{}, 1);
    }
    if (!session.takenPlateUp() || session.courtOpen() ||
        session.lastMessage().rfind("TAKEN TO THE MISSION. ", 0) != 0 ||
        session.takenPlate() != session.lastMessage()) {
        gCourtNote += " plate=no";
        return landed;
    }
    if (ending == "taken") {
        ++landed;  // 3: the plate, TAKEN TO THE MISSION over black
        return landed;
    }
    // THE PAGE, when the plate's hold runs out: the body at the Mission's
    // door, the page up.
    for (int step = 0; step < kTakenPlateSteps + 2 && !session.courtOpen(); ++step) {
        session.stepMany(sim::MoveInput{}, 1);
    }
    if (!session.courtOpen() || tavern.playerInside() || session.takenPlateUp() ||
        session.lastMessage().rfind("TAKEN TO THE MISSION. ", 0) != 0) {
        gCourtNote += " page=no";
        return landed;
    }
    ++landed;  // 3: TAKEN TO THE MISSION, the page up
    // The page's own ease, fully open, so a capture photographs the bench.
    session.stepMany(sim::MoveInput{}, 16);

    if (ending == "paper") {
        (void)session.routeCourtKey(Key::Num3);
        if (session.courtPaperOpen()) {
            ++landed;  // 4: HEAR THE PAPER
        }
        return landed;
    }
    if (ending == "armed") {
        // THE ROW ARMED: one press on I DID IT, SURE on its tail, the priest
        // pressing for the answer (court.plead) where his opening was.
        (void)session.routeCourtKey(Key::Num1);
        const HearingPageState page = session.hearingPageState();
        const std::string pressing =
            std::string(tavern.dialogue().barks().line("court.plead", crimes.hearings()));
        if (session.courtPleaArmed() && page.armed == 0 && !page.rows.empty() &&
            page.rows[0].label.find("-- SURE? ") != std::string::npos && page.priest == pressing) {
            ++landed;  // 4: armed, the question in the priest's mouth
        } else {
            gCourtNote += " armed=no";
        }
        return landed;
    }
    if (ending == "plea" || ending == "deny" || ending == "hand" || ending == "serve" || hangs) {
        // THE PLEA, armed and confirmed -- I DID IT on every ending but the
        // denial's -- then the judgment's own hold so the sentence row is on
        // the list under the check block.
        const Key pleaKey = ending == "deny" ? Key::Num2 : Key::Num1;
        (void)session.routeCourtKey(pleaKey);
        (void)session.routeCourtKey(pleaKey);
        session.stepMany(sim::MoveInput{}, sim::kJudgmentHoldSteps + 1);
        const sim::HearingState& hearing = tavern.hearing();
        gCourtNote += " plea=" + std::string(sim::pleaName(hearing.plea)) +
                      " judgment=" + std::string(sim::judgmentName(hearing.judgment)) +
                      (hearing.doubled ? " doubled=yes" : "");
        if (hearing.judged() && session.courtSentenceOffered()) {
            ++landed;  // 4: pleaded, weighed, the row offered
        }
        if (ending == "hand" && hearing.judgment != sim::Judgment::TheHand) {
            // The oath was sworn and the paper asked for the hand; a
            // judgment that spared it is the bench working, but it is not
            // the frame this ending owes.
            gCourtNote += " hand=no";
            --landed;
        }
        if (ending == "plea" || ending == "deny" || ending == "hand") {
            return landed;
        }
        if (ending == "ropepage" || ending == "streetblood") {
            // THE ROPE HEARING PAGE: the blood reading, THE ROPE on the
            // badge, THE DROP offered and not taken. For streetblood the
            // reading names the docker and the street, and the count the
            // street took.
            if (hearing.judgment != sim::Judgment::TheRope) {
                gCourtNote += " rope=no";
            }
            return landed;
        }
        if (ending == "serve") {
            // THE SENTENCE TAKEN by the player's own row: the coin, the clock,
            // the record closed, and the release -- at the Mission's door or
            // on the Tarwalk -- with the day it ended on in the row.
            if (hearing.judgment == sim::Judgment::TheRope) {
                gCourtNote += " serve=rope";
                return landed;
            }
            const std::int32_t coinBefore = tavern.playerCoin();
            const std::int32_t dayBefore = tavern.dayNumber();
            (void)session.routeCourtKey(Key::Num1);
            session.stepMany(sim::MoveInput{}, 2);
            const sim::Tavern::SentenceReport& served = tavern.lastServed();
            gCourtNote += " served=" + std::string(served.served ? "yes" : "no") +
                          " coin=" + std::to_string(coinBefore) + "->" +
                          std::to_string(tavern.playerCoin()) + " day=" +
                          std::to_string(dayBefore + 1) + "->" +
                          std::to_string(tavern.dayNumber() + 1) +
                          " heat=" + std::to_string(crimes.heat());
            if (served.served && !session.courtOpen() && !tavern.hearingPending() &&
                !crimes.warrant() && session.lastMessage().rfind("TURNED LOOSE ", 0) == 0) {
                ++landed;  // 5: served, the paper off, turned loose with the day on the row
            }
            // The seam's own dip, spent, so a capture photographs the street
            // or the door and not the cut.
            session.stepMany(sim::MoveInput{}, kPageEaseSteps + 1);
            return landed;
        }
        if (hearing.judgment != sim::Judgment::TheRope) {
            gCourtNote += " rope=no";
            return landed;
        }
        // THE DROP, by the player's own hand: the dip, the held plate, then
        // the rows under it -- the frame a capture of the end wants.
        (void)session.routeCourtKey(Key::Num1);
        session.stepMany(sim::MoveInput{}, kPageEaseSteps + sim::kDeathHoldSteps + 1);
        if (tavern.executed() && session.ropeRowsUp()) {
            ++landed;  // 5: hanged, the plate and the rows
        }
        if (ending == "newman" && session.ropeRowsUp()) {
            // A NEW MAN, by the row itself: ARMED on the first press (SURE
            // on its tail -- the one state of the plate that is not the
            // plate), TAKEN on the second: the answer main() reads beside
            // quitRequested to open the creation window again. Not a quit.
            (void)session.routeCourtKey(Key::Num1);
            session.stepMany(sim::MoveInput{}, 1);
            if (session.ropeRowArmed() == 0 && !session.runEnded() &&
                session.ropeRows()[0].find("-- SURE? ") != std::string::npos) {
                ++landed;  // 6: the row armed
                gCourtNote += " armed=new-man";
            } else {
                gCourtNote += " armed=no";
            }
            (void)session.routeCourtKey(Key::Num1);
            session.stepMany(sim::MoveInput{}, 1);
            const Session::RunEndChoice reason = session.runEndReason();
            gCourtNote += std::string(" end=") +
                          (reason == Session::RunEndChoice::NewMan  ? "new-man"
                           : reason == Session::RunEndChoice::Leave ? "leave"
                                                                    : "none");
            if (session.runEnded() && reason == Session::RunEndChoice::NewMan &&
                !session.quitRequested()) {
                ++landed;  // 7: a new man asked for, the window's loop owed
            }
        }
    }
    return landed;
}

}  // namespace scripted

}  // namespace granadad::render
