// Scripted capture drivers: threshold_petition.
#include "scripted.hpp"

namespace granadad::render {

namespace {

/// DISTRICT PHASE D. ONE AUTHORED CROSSING: a pair of world tiles either side
/// of a docks::kPlaces boundary, on one band.
///
/// EVERY ONE OF THESE FOUR WAS MEASURED OFF THE SHIPPED BINARY, not read off
/// the table and hoped for. `granadad --smoke=0 --hold --spawn=X,Y,Z` prints
/// placeLabel() for the tile it stood on, so each pair below is two runs: the
/// near tile answering one thing and the far tile answering another. The
/// crossings a reader might expect to find here and does not are the point of
/// the exercise as much as the ones that are -- see the note on `saltgate`.
struct Threshold {
    const char* word;
    /// The near side: OUTSIDE the place being entered.
    std::int32_t fromX;
    std::int32_t fromY;
    /// The far side: inside it.
    std::int32_t toX;
    std::int32_t toY;
    std::int32_t band;
};

constexpr Threshold kThresholds[] = {
    // THE PAYOFF SHOT. Out of the Quayward compound's courtyard, east through
    // its ring gate, onto Saltgate Rise -- and the gate's mouth (103,136-139)
    // wears DISTRICT PHASE B's own one-band reman frame at world z21, directly
    // overhead as the body passes under it. (104,137) is the first tile of the
    // Rise's mid-slope leg; (103,137) is compound ground and has no name at
    // all, which is why walking IN through this gate announces nothing and
    // walking OUT announces the road.
    //
    // AND IT IS THIS GATE AND NOT THE SALTGATE GATE-HOUSE, which is the
    // structure Phase B named the phase after. That gate-house straddles the
    // Rise at y147-148 -- the road runs THROUGH it, so both sides of it are
    // SALTGATE RISE and crossing it is not crossing a boundary. The plate is
    // honest about that: there is nothing to announce, so it says nothing.
    // Flagged in the phase report rather than worked around here.
    {"saltgate", 101, 137, 105, 137, sim::docks::kBandMidSlope},
    // Off the working spine, through the Gilded Gull's door. The one crossing
    // in this table that is a BUILDING rather than a reach of street, and the
    // one a player meets first: the Gull is four seconds from the spawn.
    {"gull", 153, 64, 153, 67, sim::docks::kBandQuayside},
    // North off the Tarwalk, over the quay lip, onto the finger piers. Two
    // tiles of unnamed apron in between (y58-59), which is exactly the gap
    // lastPlaceName_ is written to ignore.
    {"piers", 156, 60, 156, 57, sim::docks::kBandQuayside},
    // East along Gallows Row onto the head of Saltgate Rise, up where the
    // watch-post and the gibbet are. The only crossing here that starts on
    // NAMED ground, and it is in the table on purpose: it is the one that
    // proves the placement's own plate is run out before the walk begins.
    {"gallows", 103, 153, 107, 153, sim::docks::kBandUpper},
};

}  // namespace

ThresholdLineResult runThresholdLine(Session& session, const std::string& which,
                                     const std::string& end) {
    ThresholdLineResult out;
    const Threshold* crossing = nullptr;
    for (const Threshold& candidate : kThresholds) {
        if (which == candidate.word) {
            crossing = &candidate;
        }
    }
    if (crossing == nullptr) {
        return out;
    }
    out.found = true;
    out.from.assign(sim::docks::placeNameAt(crossing->fromX, crossing->fromY, crossing->band));
    out.to.assign(sim::docks::placeNameAt(crossing->toX, crossing->toY, crossing->band));

    // THE ONE PLACEMENT, and runStreetLine's own reasoning for it applies
    // unchanged: the capture harness's router box is the Gilded Gull and its
    // street, and walking the body to the head of Saltgate Rise would be
    // photographing the pathfinder. Everything after this line is the game.
    session.placeBodyAt(crossing->fromX, crossing->fromY, crossing->band);
    const std::int32_t dx = crossing->toX - crossing->fromX;
    const std::int32_t dy = crossing->toY - crossing->fromY;
    const sim::Angle inward = (dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy)
                                  ? (dx > 0 ? sim::kFacingEast : sim::kFacingWest)
                                  : (dy > 0 ? sim::kFacingSouth : sim::kFacingNorth);
    session.body().setYaw(inward);
    const sim::MoveInput still{};
    session.stepMany(still, 1);

    // THE PLACEMENT IS NOT A CROSSING, AND THE CAPTURE MUST NOT BE ABLE TO
    // CLAIM IT WAS. Landing on named ground -- `gallows` starts on Gallows Row
    // -- is a change of place as far as syncPanelAnim() can tell, and it fires
    // the plate. That is CORRECT for the machinery (a body that teleports has
    // arrived somewhere) and completely wrong for the photograph, which is
    // supposed to be evidence of a walk. So the placement's plate is run all
    // the way out through real steps, and its fade after it, before the walk
    // begins: whatever is on the frame at the shutter got there by walking.
    for (int guard = 0; guard < 400 && session.placePlateWanted(); ++guard) {
        session.stepMany(still, 1);
    }
    session.stepMany(still, 16);

    // THE CROSSING, WALKED -- the same movement steps a held forward key
    // produces, through the same Session::step(), collided against the same
    // geometry. This is the only part of the run the frame is evidence of.
    walkStraightTo(session, crossing->toX, crossing->toY);
    const std::string_view landed = sim::docks::placeNameAt(
        session.body().tileX(), session.body().tileY(), session.body().band());
    out.crossed = !out.to.empty() && landed == out.to;
    out.announced = session.placePlateWanted();
    out.plate.assign(session.placePlateLabel());

    // "back" TURNS AND LOOKS AT WHAT IT JUST CAME THROUGH. For `saltgate` that
    // is Phase B's gate frame standing over the mouth, with the plate up --
    // which is the whole composition this flag exists to make possible. A
    // half-turn only: the body has not moved, so nothing about the crossing
    // this result reports can change.
    if (end == "back") {
        session.body().setYaw(inward + sim::kTurnHalf);
    }
    return out;
}

PetitionLineResult runPetitionLine(Session& session, bool grantCoin) {
    PetitionLineResult out;

    // 1. SOMEBODY OF THE CLOTH, ANSWERABLE FROM COMPOUND GROUND. The roster
    // puts the Mission's priest and disciples where their day puts them, so
    // the run walks the clock an hour at a time -- the same public
    // skipToHour a WAIT pick spends -- until one of them has a standable
    // tile within talking reach that is ON a plot (the roll reading's ground
    // is the PLAYER'S feet). Any plot serves: the reading is about the
    // ground stood on, and the petition is about the roll's own vacant
    // charge wherever the conversation happens.
    const sim::WardActor* target = nullptr;
    std::string_view plotId;
    std::int32_t standX = 0;
    std::int32_t standY = 0;
    // One scan of the CURRENT hour's positions. THE STAND TILE DECIDES, not
    // the priest's own: the ground the topics read is the ground under the
    // PLAYER's feet, so a priest passing a compound gate still answers for
    // its ground when the player stands inside -- which is why the stand
    // candidates run over the conversation's whole reach (kStreetReachTiles,
    // Chebyshev, the exact reach the talk key confirms with below) and not
    // only the four adjacent tiles.
    const auto scanNow = [&]() {
        for (const sim::WardActor& actor : session.people().actors()) {
            if (!actor.visible() || (actor.type != sim::WardType::PriestOfTheFlame &&
                                     actor.type != sim::WardType::DiscipleOfTheFlame)) {
                continue;
            }
            for (std::int32_t oy = -kStreetReachTiles; oy <= kStreetReachTiles; ++oy) {
                for (std::int32_t ox = -kStreetReachTiles; ox <= kStreetReachTiles; ++ox) {
                    if (ox == 0 && oy == 0) {
                        continue;
                    }
                    const std::int32_t sx = actor.x + ox;
                    const std::int32_t sy = actor.y + oy;
                    const std::string_view ground = sim::docks::plotIdUnder(sx, sy);
                    if (ground.empty()) {
                        continue;
                    }
                    const std::int32_t plot = session.ward().plotNamed(ground);
                    if (plot < 0) {
                        continue;
                    }
                    if (!session.tiles().standable(sx, sy, actor.band)) {
                        continue;
                    }
                    const sim::WardActor* answers =
                        session.people().nearestTo(sx, sy, actor.band, kStreetReachTiles);
                    if (answers == nullptr || answers->id != actor.id) {
                        continue;
                    }
                    const sim::WardActor* addressed = addresseeFrom(
                        session, sx, sy, actor.band, cardinalToward(actor.x - sx, actor.y - sy));
                    if (addressed == nullptr || addressed->id != actor.id) {
                        continue;
                    }
                    target = &actor;
                    plotId = ground;
                    standX = sx;
                    standY = sy;
                    return true;
                }
            }
        }
        return false;
    };
    // One pass of the clock. The placement always happens at the hour the
    // body was actually seen, so the scan can never point at where somebody
    // stood three skips ago.
    bool found = false;
    for (int hourStep = 0; hourStep < 24 && !found; ++hourStep) {
        if (hourStep > 0) {
            session.skipToHour((session.timeOfDay() / 3600 + 1) % 24);
        }
        found = scanNow();
    }
    if (!found || target == nullptr) {
        return out;
    }
    out.found = true;
    out.plotId = std::string(plotId);

    // 2. THE PURSE, WHEN ASKED FOR -- capture plumbing, said out loud in the
    // header: the VACANT plot's first quarter's charge-rent through the
    // public setter, so the frame photographs the priest's YES rather than
    // the one refusal (CannotAfford) that proves nothing about the wiring.
    // The real verb still spends it for real.
    if (grantCoin) {
        const std::vector<sim::Plot>& plots = session.ward().plots();
        for (const sim::Plot& roll : plots) {
            if (roll.tenure != sim::Tenure::Vacant || roll.playerIsDuke) {
                continue;
            }
            const sim::PlotRaw& raw =
                session.ward().raws().plots()[static_cast<std::size_t>(roll.raw)];
            session.tavern().setPlayerCoin(
                std::max(session.tavern().playerCoin(), raw.chargeRent));
            break;
        }
    }

    // 3. THE ONE PLACEMENT -- runStreetLine's own move, same reasons.
    session.placeBodyAt(standX, standY, target->band);
    {
        const std::int32_t toX = target->x - standX;
        const std::int32_t toY = target->y - standY;
        sim::Angle look = sim::kFacingNorth;
        if (std::abs(toX) >= std::abs(toY)) {
            look = toX > 0 ? sim::kFacingEast : sim::kFacingWest;
        } else {
            look = toY > 0 ? sim::kFacingSouth : sim::kFacingNorth;
        }
        session.body().setYaw(look);
    }
    session.stepMany(sim::MoveInput{}, 1);

    // 4. EVERYTHING AFTER THIS IS THE GAME: the real interact, the real
    // director building a clergy topic list over real ground, the real
    // Ward::petitionForCharge behind the petition row.
    session.interact();
    out.opened = session.talking() && session.wardTalkingTo() == target->id;
    if (!out.opened) {
        return out;
    }
    out.speaker = session.tavern().dialogue().speaker().name;
    if (pick(session, sim::TopicKind::ReadRoll)) {
        out.rollLine = session.lastMessage();
    }
    if (pick(session, sim::TopicKind::Petition)) {
        out.petitionLine = session.lastMessage();
    }
    // The petition named the roll's vacant plot, which need not be the one
    // stood on -- so the claim checked is the roll's own: SOMEBODY's charge
    // now reads the player.
    for (const sim::Plot& roll : session.ward().plots()) {
        out.becameDuke = out.becameDuke || roll.playerIsDuke;
    }
    return out;
}

}  // namespace granadad::render
