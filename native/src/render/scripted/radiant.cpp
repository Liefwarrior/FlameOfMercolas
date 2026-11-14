// Scripted capture drivers: radiant.
#include "scripted.hpp"

namespace granadad::render {

RadiantLineResult runRadiantLine(Session& session, const std::string& end) {
    RadiantLineResult out;
    if (end == "ask") {
        // ERRANDS A HUMAN CAN FINISH (2026-10-09). THE FIRST THING A PLAYER
        // DOES: ask a stranger. Somebody on their feet who neither gives nor
        // receives an errand today, answerable where they stand, asked
        // through TELL ME ABOUT and A QUEST the way the keys walk it.
        const auto inTheBoard = [&session](std::int32_t id) {
            for (const sim::RadiantObjective& row :
                 session.tavern().dialogue().radiant().objectives()) {
                if (row.giverActorId == id || row.targetActorId == id) {
                    return true;
                }
            }
            return false;
        };
        const sim::WardActor* stranger = nullptr;
        std::int32_t standX = 0;
        std::int32_t standY = 0;
        for (const sim::WardActor& actor : session.people().actors()) {
            if (stranger != nullptr) {
                break;
            }
            if (!actor.visible() || !sim::isPerson(actor.type) || inTheBoard(actor.id)) {
                continue;
            }
            if (actor.x >= sim::gull::kFootprintX0 - 4 && actor.x <= sim::gull::kFootprintX1 + 4 &&
                actor.y >= sim::gull::kFootprintY0 - 6 && actor.y <= sim::gull::kFootprintY1 + 4) {
                continue;
            }
            for (std::int32_t oy = -kStreetReachTiles; oy <= kStreetReachTiles && !stranger; ++oy) {
                for (std::int32_t ox = -kStreetReachTiles; ox <= kStreetReachTiles && !stranger;
                     ++ox) {
                    const std::int32_t sx = actor.x + ox;
                    const std::int32_t sy = actor.y + oy;
                    if ((ox == 0 && oy == 0) || !session.tiles().standable(sx, sy, actor.band)) {
                        continue;
                    }
                    const sim::WardActor* addressed = addresseeFrom(
                        session, sx, sy, actor.band, cardinalToward(actor.x - sx, actor.y - sy));
                    if (addressed != nullptr && addressed->id == actor.id) {
                        stranger = &actor;
                        standX = sx;
                        standY = sy;
                    }
                }
            }
        }
        if (stranger == nullptr) {
            return out;
        }
        const std::int32_t strangerId = stranger->id;
        session.placeBodyAt(standX, standY, stranger->band);
        session.body().setYaw(cardinalToward(stranger->x - standX, stranger->y - standY));
        session.stepMany(sim::MoveInput{}, 1);
        session.interact();
        out.opened = session.talking() && session.wardTalkingTo() == strangerId;
        if (!out.opened) {
            return out;
        }
        const auto pickKind = [&session](sim::TopicKind kind, std::string_view arg) {
            const std::vector<sim::Topic>& topics = session.tavern().dialogue().topics();
            for (std::size_t i = 0; i < topics.size(); ++i) {
                if (topics[i].kind == kind && (arg.empty() || topics[i].arg == arg)) {
                    session.chooseTopic(i);
                    return true;
                }
            }
            return false;
        };
        if (pickKind(sim::TopicKind::Ask, {}) &&
            pickKind(sim::TopicKind::Category, sim::kAskQuest) &&
            pickKind(sim::TopicKind::AskErrand, {})) {
            out.answer = session.tavern().dialogue().lastLine();
            out.asked = out.answer.find("WANTS SOMEBODY TO") != std::string::npos;
        }
        // Left talking: the answer on the conversation surface is the shutter.
        return out;
    }
    const bool takeIt = end != "offer";
    // ERRANDS A HUMAN CAN FINISH: the settled end wants a DELIVER -- the one
    // errand a body can walk start to finish without first robbing somebody
    // for the goods (a fetch's goods come from the snug, a strongbox or a
    // rat; --skyrun and --burgle already prove those sources).
    const bool wantDeliver = end == "settled";

    // 1. AN OFFERED ERRAND WHOSE GIVER ANSWERS FROM WHERE THEY NOW STAND.
    // The board bound its nouns at posting time; the giver has been living
    // their day since. So the search is the petition line's own: scan the
    // CURRENT hour's positions for a stand tile the talk key would confirm,
    // and walk the clock an hour at a time until somebody's day allows it --
    // re-reading the board each hour, because a skip across midnight posts a
    // fresh one and yesterday's untaken offers go with it. Clear of the
    // Gilded Gull, runStreetLine's own exclusion for its own reason.
    const sim::WardActor* giver = nullptr;
    const sim::RadiantObjective* row = nullptr;
    std::int32_t standX = 0;
    std::int32_t standY = 0;
    const auto scanNow = [&]() {
        for (const sim::RadiantObjective& offer :
             session.tavern().dialogue().radiant().objectives()) {
            if (offer.state != sim::RadiantState::Offered) {
                continue;
            }
            if (wantDeliver && offer.isFetch()) {
                continue;
            }
            const sim::WardActor* body = session.people().byId(offer.giverActorId);
            if (body == nullptr || !body->visible()) {
                continue;
            }
            if (body->x >= sim::gull::kFootprintX0 - 4 && body->x <= sim::gull::kFootprintX1 + 4 &&
                body->y >= sim::gull::kFootprintY0 - 6 && body->y <= sim::gull::kFootprintY1 + 4) {
                continue;
            }
            for (std::int32_t oy = -kStreetReachTiles; oy <= kStreetReachTiles; ++oy) {
                for (std::int32_t ox = -kStreetReachTiles; ox <= kStreetReachTiles; ++ox) {
                    if (ox == 0 && oy == 0) {
                        continue;
                    }
                    const std::int32_t sx = body->x + ox;
                    const std::int32_t sy = body->y + oy;
                    if (!session.tiles().standable(sx, sy, body->band)) {
                        continue;
                    }
                    const sim::WardActor* answers =
                        session.people().nearestTo(sx, sy, body->band, kStreetReachTiles);
                    if (answers == nullptr || answers->id != body->id) {
                        continue;
                    }
                    const sim::WardActor* addressed = addresseeFrom(
                        session, sx, sy, body->band, cardinalToward(body->x - sx, body->y - sy));
                    if (addressed == nullptr || addressed->id != body->id) {
                        continue;
                    }
                    giver = body;
                    row = &offer;
                    standX = sx;
                    standY = sy;
                    return true;
                }
            }
        }
        return false;
    };
    bool found = false;
    for (int hourStep = 0; hourStep < 24 && !found; ++hourStep) {
        if (hourStep > 0) {
            session.skipToHour((session.timeOfDay() / 3600 + 1) % 24);
            // ONE SETTLING STEP BEFORE THE SCAN, and it is load-bearing: a
            // skip that crossed midnight leaves yesterday's board standing
            // until the next advanceSecond posts the new day's -- so a scan
            // straight off the skip could pick a row the very next step
            // sweeps, and the conversation would open on a giver whose offer
            // no longer exists. The step runs the posting first; the scan
            // reads the board the interact below will actually see.
            session.stepMany(sim::MoveInput{}, 1);
        }
        found = scanNow();
    }
    if (!found || giver == nullptr || row == nullptr) {
        return out;
    }
    out.found = true;
    out.objectiveId = row->id;
    out.giver = row->giverName;
    out.brief = row->brief;

    // 2. THE ONE PLACEMENT -- runStreetLine's own move, same reasons.
    session.placeBodyAt(standX, standY, giver->band);
    {
        const std::int32_t toX = giver->x - standX;
        const std::int32_t toY = giver->y - standY;
        sim::Angle look = sim::kFacingNorth;
        if (std::abs(toX) >= std::abs(toY)) {
            look = toX > 0 ? sim::kFacingEast : sim::kFacingWest;
        } else {
            look = toY > 0 ? sim::kFacingSouth : sim::kFacingNorth;
        }
        session.body().setYaw(look);
    }
    session.stepMany(sim::MoveInput{}, 1);

    // 3. EVERYTHING AFTER THIS IS THE GAME: the real interact, the real
    // director recognising its own giver, the real board behind the press.
    session.interact();
    out.opened = session.talking() && session.wardTalkingTo() == giver->id;
    if (!out.opened) {
        return out;
    }
    const std::vector<sim::Topic>& topics = session.tavern().dialogue().topics();
    std::int32_t offerIndex = -1;
    for (std::size_t i = 0; i < topics.size(); ++i) {
        if (topics[i].kind == sim::TopicKind::TakeRadiant && topics[i].payload == out.objectiveId) {
            offerIndex = static_cast<std::int32_t>(i);
            break;
        }
    }
    out.offered = offerIndex >= 0;
    if (!out.offered || !takeIt) {
        // The "offer" end: the shutter gets the giver's own row on the list.
        return out;
    }
    session.chooseTopic(static_cast<std::size_t>(offerIndex));
    const sim::RadiantObjective* moved =
        session.tavern().dialogue().radiant().find(out.objectiveId);
    out.taken = moved != nullptr && moved->state == sim::RadiantState::Taken;
    // 4. AND THE JOURNAL SHOWS IT -- the same rows the Journal tile draws,
    // asked for the errand's own cast rather than eyeballed off a PNG: the
    // row carries the verb and names the errand's own party.
    const std::string party = row->isFetch() ? "FOR " + upperAscii(row->giverName)
                                             : "TO " + upperAscii(row->targetName);
    for (const std::string& line : session.journalWorkRows()) {
        if (line.rfind("- ", 0) == 0 && line.find(row->verb) != std::string::npos &&
            line.find(party) != std::string::npos) {
            out.journal = true;
            break;
        }
    }
    // The taken end is DONE talking -- its evidence is the board and the
    // journal, and the menu-opening flags below runSmoke's scripted lines
    // (--refocus=journal in particular) cannot open a page over a
    // conversation still holding the keys. The "offer" end above returns
    // with the list up because the list IS its photograph.
    session.closeConversation();
    if (end != "shelf" && end != "settled") {
        return out;
    }

    // ERRANDS A HUMAN CAN FINISH (2026-10-09). From here the row is held BY
    // VALUE: the walk below steps the ward, and a step across midnight
    // rebuilds the board's vector under any pointer into it.
    const sim::RadiantObjective errand = *row;
    out.target = errand.targetName;
    out.pay = errand.pay;

    // 5. THE NOTES, opened the way J opens them, turned to the CASES shelf
    // the way the tab key turns it, and the cursor walked the way the arrows
    // walk it until the errand's own row is under it -- with who settles
    // it, where they were and what it pays on the facts.
    session.toggleCasebook();
    for (int turns = 0; turns < 4 && session.casebookTab() != CasebookTab::Cases; ++turns) {
        session.cycleCasebookTab(1);
    }
    const std::string wantTitle = errand.isFetch()
                                      ? std::string()
                                      : errand.verb + " TO " + upperAscii(errand.targetName);
    const std::string wantWho =
        upperAscii(errand.isFetch() ? errand.giverName : errand.targetName);
    {
        const std::size_t rows = session.casebookPageState().shelf.size();
        for (std::size_t walked = 0; walked < rows && !out.shelf; ++walked) {
            const CasebookPageState page = session.casebookPageState();
            if (!page.shelf.empty()) {
                const int at =
                    std::clamp(page.shelfCursor, 0, static_cast<int>(page.shelf.size()) - 1);
                const CasebookShelfRow& shown = page.shelf[static_cast<std::size_t>(at)];
                out.shelf = shown.errand && shown.who == wantWho && !shown.where.empty() &&
                            (wantTitle.empty() || shown.title == wantTitle);
            }
            if (!out.shelf) {
                session.moveCasebookCursor(1);
            }
        }
    }
    if (end == "shelf") {
        // The page stays up: the shelf row IS this end's photograph.
        return out;
    }
    session.toggleCasebook();

    // 6. THE TARGET, WHERE THEY STAND NOW. The shelf says where they were
    // when the job was posted; the body has lived its day since, so the walk
    // re-finds them each round, the way a player asks the street. Walked on
    // the district's own router when they share a band; PLACED (and printed
    // walked=no) when they do not -- this line does not climb, the trail's
    // own stated limit.
    const std::int32_t coinBefore = session.tavern().playerCoin();
    for (int round = 0; round < 8 && !out.reached; ++round) {
        const sim::WardActor* body = session.people().byId(errand.targetActorId);
        if (body == nullptr || !body->visible()) {
            session.skipToHour((session.timeOfDay() / 3600 + 1) % 24);
            session.stepMany(sim::MoveInput{}, 1);
            continue;
        }
        std::int32_t standX = 0;
        std::int32_t standY = 0;
        bool stand = false;
        for (std::int32_t oy = -kStreetReachTiles; oy <= kStreetReachTiles && !stand; ++oy) {
            for (std::int32_t ox = -kStreetReachTiles; ox <= kStreetReachTiles && !stand; ++ox) {
                if (ox == 0 && oy == 0) {
                    continue;
                }
                const std::int32_t sx = body->x + ox;
                const std::int32_t sy = body->y + oy;
                if (!session.tiles().standable(sx, sy, body->band)) {
                    continue;
                }
                const sim::WardActor* answers = addresseeFrom(
                    session, sx, sy, body->band, cardinalToward(body->x - sx, body->y - sy));
                if (answers != nullptr && answers->id == body->id) {
                    standX = sx;
                    standY = sy;
                    stand = true;
                }
            }
        }
        if (!stand) {
            session.stepMany(sim::MoveInput{}, 30);
            continue;
        }
        bool onFoot = false;
        if (body->band == session.body().band()) {
            onFoot = walkAcrossDistrict(session, standX, standY);
        }
        if (!onFoot) {
            session.placeBodyAt(standX, standY, body->band);
            session.stepMany(sim::MoveInput{}, 1);
        }
        // The walk took time and the target kept theirs: ask again who
        // answers from where the body actually stands.
        body = session.people().byId(errand.targetActorId);
        if (body == nullptr) {
            continue;
        }
        session.body().setYaw(cardinalToward(body->x - session.body().tileX(),
                                             body->y - session.body().tileY()));
        session.stepMany(sim::MoveInput{}, 1);
        body = session.people().byId(errand.targetActorId);
        const sim::WardActor* answers = session.wardAddresseeNow();
        if (body == nullptr || answers == nullptr || answers->id != body->id) {
            continue;
        }
        out.reached = true;
        out.walked = onFoot;
    }
    if (!out.reached) {
        return out;
    }

    // 7. THE GAME: the real interact, the target's own list, the press.
    session.interact();
    if (!session.talking() || session.wardTalkingTo() != errand.targetActorId) {
        return out;
    }
    const std::vector<sim::Topic>& settleTopics = session.tavern().dialogue().topics();
    std::int32_t settleIndex = -1;
    for (std::size_t i = 0; i < settleTopics.size(); ++i) {
        if (settleTopics[i].kind == sim::TopicKind::SettleRadiant &&
            settleTopics[i].payload == errand.id) {
            settleIndex = static_cast<std::int32_t>(i);
            break;
        }
    }
    out.passed = settleIndex >= 0;
    if (!out.passed) {
        return out;
    }
    session.chooseTopic(static_cast<std::size_t>(settleIndex));
    const sim::RadiantObjective* paid = session.tavern().dialogue().radiant().find(errand.id);
    out.settled = paid != nullptr && paid->state == sim::RadiantState::Paid &&
                  session.tavern().playerCoin() - coinBefore == errand.pay;
    // Left talking: the target's own answer on the alert row is the shutter.
    return out;
}

}  // namespace granadad::render
