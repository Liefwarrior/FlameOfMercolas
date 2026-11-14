// Scripted capture drivers: rung.
#include "scripted.hpp"

namespace granadad::render {

namespace {

/// THE EYE DIPPED TOWARD A MAN ON A STOOL from where the body stands: faced
/// by his bearing (which, from standBackFrom's cardinal spots, is the yaw it
/// already has) and pitched down by his distance -- thirty degrees at a
/// tile, a third of that at three -- so a seated man three tiles off is in
/// the picture and not under the eye's own horizon. What the passes learnt
/// at Cull's stool, gate by gate, is in the rung README ("Cull's corner");
/// the summary's `aim=` is the INTERACT target (Session::aimSubjectNow:
/// what the E key would act on from where the body stands, resolved by
/// position and reach, never by the reticle's ray), printed so the record
/// says what the frame's prompt names.
void dipToward(Session& session, std::string_view name) {
    const sim::Actor* who = actorNamed(session, name);
    if (who == nullptr) {
        return;
    }
    const std::int32_t dx = who->tileX() - session.body().tileX();
    const std::int32_t dy = who->tileY() - session.body().tileY();
    const std::int32_t tiles = std::max(1, std::max(dx < 0 ? -dx : dx, dy < 0 ? -dy : dy));
    session.body().setYaw(sim::bearingTo(session.body().tileX(), session.body().tileY(),
                                         who->tileX(), who->tileY()));
    session.body().setPitch(sim::angle_from_degrees(-30 / tiles));
}

}  // namespace

namespace scripted {

/// PLAYS THE SHORTEST REAL PATH TO A RUNG ON ONE TRACK and stops on the
/// plate. Every beat is a Session call a keypress makes -- most of them the
/// smoke's own lines' beats reused whole -- and nothing reaches into the
/// simulation sideways, so the plate on the frame is the plate a player
/// would see. Each track's path, and why it is the shortest:
///
///   wire   ONE CRACKED STRONGBOX. A burgle is eight points of THE WIRE
///          (legend.cpp), which is the first threshold exactly: the
///          burglary line's own box beat at two in the morning, crouched,
///          the doors barred. Four caught lifts would do it too, and every
///          caught lift is a bouncer's warning, which OUTRANKS the plate.
///          The eye goes up the landing BEFORE the lid does, because the
///          lid is the rung and nothing walks after a rung (below).
///   roofs  THE FIRST ROOF-RUN (three) AND SKYRUNNING (two a level): in at
///          the Gull's door and up onto its lead the roof line's own way,
///          then the alley leapt west and east -- a leap is two uses --
///          until the roofs will have it. Stopped on the roof it rose on,
///          the alley under the eye.
///   flame  TWO LEADS READ (six apiece): the trail's own first two, the
///          Mission's back room and its flagstones, walked to by the
///          district's router and read with the same Q, at the trail's own
///          hour (nine, the Mission's door open) -- each of them read from
///          the trail's own stand-back rather than off it, so no step
///          falls after the rung (the frame is the back room's wall a tile
///          off the anchor; the README declares it).
///   trade  THE WARD'S BOUNTY, PAID: runContractLine whole. Six for the job
///          and the coin over four clears the threshold at the smallest
///          bounty the board deals (bounty_kennel, two units at four: eight
///          coin, two points). The rung rises at the turn-in, across Cull's
///          table, and the plate comes up once the talk's close tail is
///          down; the line's own stand-back walks after it.
///   law    FOUR DRINKS STOOD TO WATCHMAN CULL. A bought drink is a deed
///          worth two of standing with the drinker's own guild
///          (factionDeedWeight), and the Watch at eight on a clean sheet is
///          KNOWN TO THE WATCH. The honest short road: the bounty alone
///          leaves a man three short of it.
///
/// AND NOTHING WALKS AFTER A RUNG on the wire, the roofs and the flame (the
/// trade and the law stand back after theirs, about twenty-five steps of
/// the hold, and the README says so). The two that broke it are why it is
/// written down: the plate holds five seconds (kRungPlateHoldSteps) and a
/// scripted walk spends them faster than anything else in this file --
/// fifty tiles out of the Mission and round it, or a crouched fight for the
/// last tile of the Gull's landing -- so the rung comes up on a body that
/// is already framed and the drive stops there. Same rule `--trail=opened`
/// states for the lead-opened notice, and for the same three-second reason.
///
/// The line ends by stepping the plate all the way up (its own eight-step
/// rise), bounded, so `--settle-steps=0` photographs it at full strength and
/// the default rest (kCaptureRestSteps, four seconds) still photographs the
/// card: the wire's lift starts about fifty steps later, the trade's and the
/// law's -- whose stand-back spent about twenty-five steps of the hold first
/// -- about thirty.
[[nodiscard]] RungLineResult runRungLine(Session& session, const std::string& word) {
    RungLineResult out;
    if (!legendTrackFromKey(word, out.track)) {
        return out;
    }
    out.found = true;
    out.from = session.legend().row(out.track).rung;
    // The trade and the law need the man himself: whether he is in the
    // Gull's roster when the line starts is the reason a run can give for
    // no rung.
    const bool cullPresent = actorNamed(session, "Watchman Cull") != nullptr;
    const auto rung = [&session, &out]() { return session.legend().row(out.track).rung; };
    const auto risen = [&rung, &out]() { return rung() > out.from; };

    switch (out.track) {
        case sim::LegendTrack::Wire: {
            // The burglary's beats 1, 4, 5, 6 and 7 -- crouched, in, up, the
            // wire in a guest's box, the pins worked (a shoulder if the wire
            // ruined it), the box emptied. The empty IS the burgle.
            session.toggleCrouch();
            walkToTile(session, sim::gull::kDoorX0, sim::gull::kDoorY + 1);
            walkToTile(session, sim::gull::kStairX, sim::gull::kStairY);
            climbAndLand(session);
            const sim::gull::GuestRoom& box = sim::gull::kRooms[2];
            walkToTile(session, box.standX, box.standY);
            session.steal();
            (void)workTheWire(session);
            if (session.picking()) {
                session.stopPicking();
            }
            if ((session.tavern().openedLocks() & (1 << 2)) == 0) {
                session.forceLock();
            }
            // THE FRAME IS AIMED BEFORE THE LID GOES, AND NOTHING WALKS
            // AFTER IT. The burglary shoots this room from the landing, four
            // tiles out, and walks there once the box is empty -- which is
            // right for `--burgle` and wrong here, because here the box IS
            // the rung. The plate holds five seconds (kRungPlateHoldSteps)
            // and that walk spends them: off the bed-foot, crouched at half
            // pace, with the last tile fought for and never won. Same rule
            // `--trail=opened` states in as many words -- stop on the step
            // the notice fires and do nothing afterwards -- so the eye is
            // put on the landing the burglar came up FIRST, with the room he
            // is doing behind him, and then the lid.
            session.body().setYaw(sim::bearingTo(session.body().tileX(), session.body().tileY(),
                                                 152, 72));
            session.body().setPitch(sim::angle_from_degrees(-8));
            // The empty IS the burgle, so this is the step the rung rises on
            // and the last step this track takes.
            session.steal();
            break;
        }
        case sim::LegendTrack::Roofs: {
            // In at the door first -- the roof line's own first beat, because
            // the house router is asked for the stair from the street
            // otherwise -- then up the lead the way every roof line goes up.
            walkToTile(session, sim::gull::kDoorX0, sim::gull::kDoorY + 1);
            upOntoTheLead(session);
            walkToTile(session, sim::gull::kFootprintX0, 70);
            // West over the two tiles of air and back east, the skyrun's own
            // alley beat, until the roofs will have it. Bounded exactly as
            // that line bounds it.
            for (int i = 0; i < 24 && !risen(); ++i) {
                session.body().setYaw(sim::kFacingWest);
                climbAndLand(session);
                if (risen()) {
                    break;
                }
                session.body().setYaw(sim::kFacingEast);
                climbAndLand(session);
            }
            // The roof line's leap shot: the alley under the eye.
            session.body().setPitch(sim::angle_from_degrees(-10));
            break;
        }
        case sim::LegendTrack::Flame: {
            // The trail's own loop for its first two leads: the nearest OPEN
            // lead on this band, walked to by the district's router, read
            // with the same Q -- stopped the moment the track rises.
            //
            // WITH THE TRAIL'S STAND-BACK MOVED AHEAD OF THE KEY, and that
            // is the whole of the difference. The trail reads standing on
            // the anchor and walks off it afterwards, which is right for
            // `--trail` and fatal here: the Mission's flagstones sit inside
            // the almshouse, the tile seven south of them is out on the
            // quay, and the router's honest answer is out of the north
            // door, round the building and back down -- fifty-odd tiles,
            // where the plate holds five seconds (kRungPlateHoldSteps).
            // kLookRangeTiles is four and the trail's own tail says so:
            // four tiles off, the clue still reads. So the body backs off
            // FIRST -- toward the side it is already on, which is the side
            // the door is on -- turns, and reads from there. The rung rises
            // with the frame already made and the drive takes no step after
            // it. `--trail=opened`'s rule, applied to a rung.
            const sim::CasebookRaws* raws = session.casebook().raws();
            for (int guard = 0; raws != nullptr && guard < 4 && !risen(); ++guard) {
                std::int32_t best = -1;
                std::int32_t bestDistance = 0;
                for (const std::int32_t index : session.casebook().known()) {
                    if (session.casebook().state(index) != sim::LeadState::Open) {
                        continue;
                    }
                    const sim::Lead& lead = raws->leads()[static_cast<std::size_t>(index)];
                    if (lead.site.band != session.body().band()) {
                        continue;
                    }
                    const std::int32_t dx = lead.site.x - session.body().tileX();
                    const std::int32_t dy = lead.site.y - session.body().tileY();
                    const std::int32_t distance = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
                    if (best < 0 || distance < bestDistance) {
                        best = index;
                        bestDistance = distance;
                    }
                }
                if (best < 0) {
                    break;
                }
                const sim::Lead& lead = raws->leads()[static_cast<std::size_t>(best)];
                const std::int32_t before = session.casebook().readCount();
                // 1. OFF THE ANCHOR, THE WAY THE BODY CAME. Four tiles, then
                //    three -- both inside kLookRangeTiles -- and the sign is
                //    taken off where the body already stands, so the step
                //    back is into the room it is walking into rather than
                //    out of its door.
                const std::int32_t awayX = session.body().tileX() < lead.site.x ? -1 : 1;
                const std::int32_t awayY = session.body().tileY() < lead.site.y ? -1 : 1;
                for (const std::int32_t back : {4, 3}) {
                    if (walkAcrossDistrict(session, lead.site.x, lead.site.y + awayY * back) ||
                        walkAcrossDistrict(session, lead.site.x + awayX * back, lead.site.y) ||
                        walkAcrossDistrict(session, lead.site.x, lead.site.y - awayY * back) ||
                        walkAcrossDistrict(session, lead.site.x - awayX * back, lead.site.y)) {
                        break;
                    }
                }
                session.body().setYaw(sim::bearingTo(session.body().tileX(),
                                                     session.body().tileY(), lead.site.x,
                                                     lead.site.y));
                session.examine();
                // 2. AND ONTO IT IF IT WOULD NOT READ FROM THERE -- the
                //    trail's own walk, unchanged, so a lead that cannot be
                //    stood off is still read where the trail reads it. A
                //    nose against masonry is a worse frame than the landing
                //    and a better one than no plate at all.
                if (session.casebook().readCount() <= before) {
                    bool got = walkAcrossDistrict(session, lead.site.x, lead.site.y);
                    for (const std::int32_t offset : {1, -1, 2, -2}) {
                        if (got) {
                            break;
                        }
                        got = walkAcrossDistrict(session, lead.site.x + offset, lead.site.y) ||
                              walkAcrossDistrict(session, lead.site.x, lead.site.y + offset);
                    }
                    session.body().setYaw(sim::bearingTo(session.body().tileX(),
                                                         session.body().tileY(), lead.site.x,
                                                         lead.site.y));
                    session.examine();
                }
                if (session.casebook().readCount() <= before) {
                    break;
                }
            }
            // THE EYE DOWN ONTO THE FLAGSTONES once the last read has tipped
            // the track: from four tiles back at eye level the frame was the
            // room's far wall and the flagstones the card is about were under
            // the bottom edge (both critics). Set after the loop, because the
            // walk to the second lead reset it. A turn, not a walk.
            session.body().setPitch(sim::angle_from_degrees(-18));
            break;
        }
        case sim::LegendTrack::Trade: {
            // The bounty, whole, and closed at the end so the plate that
            // queued behind Cull's table can come up; the line's own
            // stand-back (three tiles, a walk the plate's hold pays for),
            // then the eye dipped to him from there. The prompt names the
            // patron whose stool is in reach of the stand-back tile -- the
            // summary's `aim=` says so.
            (void)runContractLine(session, "away");
            dipToward(session, "Watchman Cull");
            break;
        }
        case sim::LegendTrack::Law: {
            if (speakTo(session, "Watchman Cull")) {
                for (int i = 0; i < 8 && !risen(); ++i) {
                    if (!pick(session, sim::TopicKind::BuyDrinkFor)) {
                        break;
                    }
                }
                session.closeConversation();
            }
            // The stand-back, and the eye level and a touch up: the man on
            // his stool three tiles off is in the lower half of the frame.
            // The prompt names the HOOD on the stand a tile south of the
            // stand-back tile, in reach; no pitch can change that (the
            // prompt is the interact target, not the reticle's ray), and
            // the summary's `aim=` says so.
            standBackFrom(session, "Watchman Cull");
            session.body().setPitch(sim::angle_from_degrees(4));
            break;
        }
    }

    // THE PLATE, ALL THE WAY UP. One step arms it off the diff, eight ease it
    // in; bounded, and a rung that did not rise leaves nothing to wait for.
    const auto plateFullyUp = [&session]() {
        return session.rungPlateWanted() && session.rungPlateFade() >= 1.0F;
    };
    for (int i = 0; i < 2 * kPageEaseSteps + 4 && risen() && !plateFullyUp(); ++i) {
        session.stepMany(sim::MoveInput{}, 1);
    }
    out.to = rung();
    out.rose = out.to > out.from;
    out.plateUp = plateFullyUp();
    out.head = session.rungPlateText().head;
    out.row = session.rungPlateText().prose;
    out.aim = std::string(session.aimSubjectNow());
    if (!out.rose && !cullPresent &&
        (out.track == sim::LegendTrack::Trade || out.track == sim::LegendTrack::Law)) {
        out.reason = "Watchman Cull is not in the Gull when the line starts";
    }
    return out;
}

}  // namespace scripted

}  // namespace granadad::render
