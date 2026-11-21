// The scripted drivers' shared walking, talking, aiming and sight helpers (declared in scripted.hpp).
#include "scripted.hpp"

namespace granadad::render {

namespace scripted {

/// Steers the body one movement step toward a Q8 point, faced and collided
/// against exactly the geometry a player walks into. True once it has arrived.
[[nodiscard]] bool stepToward(Session& session, std::int32_t goalX, std::int32_t goalY) {
    const std::int32_t dx = goalX - session.body().x();
    const std::int32_t dy = goalY - session.body().y();
    // A QUARTER of a tile of slop, not a half. Half a tile leaves the eye
    // pressed against the next cell's face, and a capture framed from there is
    // a photograph of a wall -- which is what the first version of this made.
    //
    // AN EIGHTH, still: this walker sets MoveInput::snapVelocity, so it has no
    // momentum to glide past its own goal on and the S5 framing argument above
    // stands unchanged.
    const std::int32_t tolerance = sim::kSubOne / 8;
    const bool closeX = dx > -tolerance && dx < tolerance;
    const bool closeY = dy > -tolerance && dy < tolerance;
    if (closeX && closeY) {
        return true;
    }
    const sim::Angle eastWest = dx > 0 ? sim::kFacingEast : sim::kFacingWest;
    const sim::Angle northSouth = dy > 0 ? sim::kFacingSouth : sim::kFacingNorth;
    // The axis with more ground left to cover goes first, then the other, then
    // either perpendicular -- which is enough to get round one table.
    const bool xFirst = (dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy);
    const sim::Angle tries[4] = {
        xFirst ? eastWest : northSouth,
        xFirst ? northSouth : eastWest,
        xFirst ? sim::kFacingNorth : sim::kFacingEast,
        xFirst ? sim::kFacingSouth : sim::kFacingWest,
    };
    for (const sim::Angle facing : tries) {
        if ((facing == eastWest && closeX) || (facing == northSouth && closeY)) {
            continue;
        }
        session.body().setYaw(facing);
        const std::int32_t beforeX = session.body().x();
        const std::int32_t beforeY = session.body().y();
        sim::MoveInput input;
        input.forward = 1;
        // #77. THE CAPTURE SCRIPT MUST NOT CLIMB. This walker STEERS BY WALKING
        // INTO WALLS -- it tries each compass direction in turn and reads "did
        // the body move" as "was that way open" -- so with contextual traversal
        // on it would haul itself up the first warehouse it probed and
        // photograph the wrong district from the wrong height. A player walking
        // at a ledge means "get me up there"; this loop means "is there a wall".
        input.autoTraverse = false;
        // AND IT HAS NO LEGS EITHER. Same reason and the same sentence: this
        // loop reads "did the body move" as "is that way open", and momentum
        // from the last direction would answer for the next one. See
        // MoveInput::snapVelocity.
        input.snapVelocity = true;
        session.step(input);
        if (session.body().x() != beforeX || session.body().y() != beforeY) {
            return false;
        }
    }
    return false;
}

/// Greedy, and only the last few Q8 units of a walk. Gives up rather than
/// grinding.
void walkStraightTo(Session& session, std::int32_t tileX, std::int32_t tileY) {
    const std::int32_t goalX = sim::q8_tile_centre(tileX);
    const std::int32_t goalY = sim::q8_tile_centre(tileY);
    std::int32_t stuckFor = 0;
    for (int guard = 0; guard < 600; ++guard) {
        const std::int32_t beforeX = session.body().x();
        const std::int32_t beforeY = session.body().y();
        if (stepToward(session, goalX, goalY)) {
            return;
        }
        if (session.body().x() == beforeX && session.body().y() == beforeY) {
            if (++stuckFor > 4) {
                return;
            }
        } else {
            stuckFor = 0;
        }
    }
}

/// Presses the up-key and then WATCHES the jump. A mantle resolves under the
/// hand; a leap arms an arc and the ordinary step pump flies it, so a scripted
/// capture spends the same twenty-four movement steps in the air a player does
/// rather than arriving instantly. See Session::climb on why the press stopped
/// draining the arc itself.
void climbAndLand(Session& session) {
    session.climb();
    (void)session.flyOutLeap();
}

void walkToTile(Session& session, std::int32_t tileX, std::int32_t tileY) {
    sim::RegionPath router(session.tiles(), kCaptureRegion);
    std::vector<sim::PathStep> route;
    const sim::PathStep from{session.body().tileX(), session.body().tileY(),
                             session.body().band()};
    const sim::PathStep to{tileX, tileY, session.body().band()};
    if (router.find(from, to, route)) {
        for (const sim::PathStep& waypoint : route) {
            const std::int32_t wx = sim::q8_tile_centre(waypoint.x);
            const std::int32_t wy = sim::q8_tile_centre(waypoint.y);
            bool arrived = false;
            for (int guard = 0; guard < 120 && !arrived; ++guard) {
                arrived = stepToward(session, wx, wy);
            }
            if (!arrived) {
                break;
            }
        }
    }
    // Whatever the route left, and the whole walk when the router refused --
    // which is what happens for a goal outside the box, the roof being the
    // obvious one.
    walkStraightTo(session, tileX, tileY);
}

/// The BAM bearing from the player's CURRENT tile to a Q8 world point --
/// facePlayerAndSync's own atan2, pulled out so a caller can WEIGH a turn
/// before committing it. RENDER-SIDE, so the atan2 is legal here -- it
/// never touches the sim's hashed yaw math, it only reasons about the
/// body's own facing.
[[nodiscard]] sim::Angle bearingToward(const Session& session, std::int32_t xQ8,
                                        std::int32_t yQ8) noexcept {
    const std::int32_t dx = xQ8 - session.body().x();
    const std::int32_t dy = yQ8 - session.body().y();
    if (dx == 0 && dy == 0) {
        return session.body().yaw();
    }
    // North is -Y and forward is (sin(yaw), -cos(yaw)), so the bearing to
    // (dx, dy) is atan2(dx, -dy), turned from radians into the BAM turn.
    constexpr double kTwoPi = 6.283185307179586;
    const double ang = std::atan2(static_cast<double>(dx), static_cast<double>(-dy));
    return static_cast<sim::Angle>(std::lround(ang * static_cast<double>(sim::kTurnFull) / kTwoPi)) &
           (sim::kTurnFull - 1);
}

/// Turns the player's body to look STRAIGHT at a Q8 world point, then steps once
/// so the room hears the new facing (setPlayerYaw runs in syncTavernToBody, which
/// only a step calls). The scripted drives use this to put a body DEAD ON the
/// crosshair before a swing: the old radial punch hit by proximity, but the
/// action-combat swing hits the first body on the look-ray, so a man who is
/// trading blows and shifting his feet has to be re-faced or the swing casts
/// past him. One step of the target's own drift is far inside the beam's
/// half-cell, so the aim holds.
void facePlayerAndSync(Session& session, std::int32_t xQ8, std::int32_t yQ8) {
    if (xQ8 != session.body().x() || yQ8 != session.body().y()) {
        session.body().setYaw(bearingToward(session, xQ8, yQ8));
    }
    session.stepMany(sim::MoveInput{}, 1);
}

/// A ward body on the move: sliding between two tiles this second, or
/// walking a leg whose corner is somewhere else (targetBand == 0 is the
/// population's own "no leg yet"). A body that has not been ticked at all
/// looks still by both, which is why runStreetLine lets the crowd walk a
/// couple of seconds before it asks.
[[nodiscard]] bool wardWalking(const sim::WardActor& who) noexcept {
    const bool sliding = who.prevX != who.x || who.prevY != who.y || who.prevBand != who.band;
    const bool onALeg = who.targetBand != 0 && (who.targetX != who.x || who.targetY != who.y);
    return sliding || onALeg;
}

/// A ward body standing on its tile: drawn at its centre, not sliding out
/// of one cell into the next, and going nowhere this leg.
[[nodiscard]] bool wardStandsStill(const sim::WardActor& who) noexcept {
    return !wardWalking(who);
}

/// Where a ward body is DRAWN this step, Q8: the slide from its last tile
/// to its current one by the session's own fraction of the second -- the
/// identical arithmetic the 3D instancer uses, so a shot
/// aimed here is aimed at the figure and not at the cell its feet will
/// reach at the top of the second. A body standing still is its centre.
[[nodiscard]] std::pair<std::int32_t, std::int32_t> wardDrawnQ8(const Session& session,
                                                                const sim::WardActor& who) noexcept {
    const std::int32_t slide = session.stepsThisSecond();
    const std::int32_t x = sim::q8_tile_centre(who.prevX) +
                           (who.x - who.prevX) * sim::kSubOne * slide / sim::kStepsPerSecond;
    const std::int32_t y = sim::q8_tile_centre(who.prevY) +
                           (who.y - who.prevY) * sim::kSubOne * slide / sim::kStepsPerSecond;
    return {x, y};
}

/// The speaker the row names, where it is drawn: a Gull speaker by roster
/// id at the sim's own Q8, a ward speaker by its lifted id on its slide.
/// False with the row empty or the body gone.
[[nodiscard]] bool speakerDrawnQ8(const Session& session, std::int32_t& xQ8, std::int32_t& yQ8,
                                  std::int32_t& band) noexcept {
    const VoiceLine& line = session.voice().current();
    if (line.empty()) {
        return false;
    }
    if (line.actorId >= sim::kWardSpeakerIdBase) {
        const sim::WardActor* who = session.people().byId(line.actorId - sim::kWardSpeakerIdBase);
        if (who == nullptr || !who->visible()) {
            return false;
        }
        const std::pair<std::int32_t, std::int32_t> at = wardDrawnQ8(session, *who);
        xQ8 = at.first;
        yQ8 = at.second;
        band = who->band;
        return true;
    }
    const sim::Actor* who = session.tavern().actorById(line.actorId);
    if (who == nullptr || !who->present()) {
        return false;
    }
    xQ8 = who->x();
    yQ8 = who->y();
    band = who->band();
    return true;
}

/// Whether some OTHER body is drawn on the line from the eye to a body at
/// (xQ8, yQ8): nearer than it along the line and within half a figure's
/// width of it -- the bartender who stepped in front of the witness. The
/// Gull's roster at the sim's own Q8, the ward's on its slide; `speakerId`
/// is the body being looked at, by the row's own id (a roster id below
/// kWardSpeakerIdBase, a ward id lifted above it). `bodyBetweenFrom` asks
/// it from an eye at (eyeXQ8, eyeYQ8) the body is not standing at yet -- a
/// stand being weighed before the placement.
[[nodiscard]] bool bodyBetweenFrom(const Session& session, std::int32_t eyeXQ8,
                                   std::int32_t eyeYQ8, std::int32_t xQ8, std::int32_t yQ8,
                                   std::int32_t band, std::int32_t speakerId) noexcept {
    const double ax = static_cast<double>(eyeXQ8);
    const double ay = static_cast<double>(eyeYQ8);
    const double dx = static_cast<double>(xQ8) - ax;
    const double dy = static_cast<double>(yQ8) - ay;
    const double length = std::sqrt(dx * dx + dy * dy);
    if (length < 1.0) {
        return false;
    }
    constexpr double kHalfFigure = 0.4 * static_cast<double>(sim::kSubOne);
    const auto blocks = [&](std::int32_t bx, std::int32_t by) {
        const double px = static_cast<double>(bx) - ax;
        const double py = static_cast<double>(by) - ay;
        const double along = (px * dx + py * dy) / length;
        if (along < kHalfFigure || along > length - kHalfFigure) {
            return false;
        }
        const double across = std::abs(px * dy - py * dx) / length;
        return across <= kHalfFigure;
    };
    for (const sim::Actor& actor : session.tavern().actors()) {
        if (!actor.present() || actor.band() != band || actor.id() == speakerId ||
            actor.role() == sim::ActorRole::Vermin || sim::isFloored(actor.activity())) {
            continue;
        }
        if (blocks(actor.x(), actor.y())) {
            return true;
        }
    }
    for (const sim::WardActor& actor : session.people().actors()) {
        if (!actor.visible() || actor.band != band || !sim::isPerson(actor.type) ||
            actor.id + sim::kWardSpeakerIdBase == speakerId) {
            continue;
        }
        const std::pair<std::int32_t, std::int32_t> at = wardDrawnQ8(session, actor);
        if (blocks(at.first, at.second)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool bodyBetween(const Session& session, std::int32_t xQ8, std::int32_t yQ8,
                               std::int32_t band, std::int32_t speakerId) noexcept {
    return bodyBetweenFrom(session, session.body().x(), session.body().y(), xQ8, yQ8, band,
                           speakerId);
}

/// The index of the first topic of this kind, or -1.
[[nodiscard]] int topicOfKind(const Session& session, sim::TopicKind kind) {
    const std::vector<sim::Topic>& topics = session.tavern().dialogue().topics();
    for (std::size_t i = 0; i < topics.size(); ++i) {
        if (topics[i].kind == kind) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

/// The present actor of that name, or nullptr.
[[nodiscard]] const sim::Actor* actorNamed(const Session& session, std::string_view name) {
    for (const sim::Actor& actor : session.tavern().actors()) {
        if (actor.present() && actor.name() == name) {
            return &actor;
        }
    }
    return nullptr;
}

/// The nearest PERSON on the player's own band -- present, on their feet, not
/// a rat -- or nullptr. What `--punch` and `--block` mean by "whoever is
/// nearest": the pre-sightline radial punch picked by proximity, and the two
/// flags keep that meaning, they just walk it over now. Same band, because the
/// scripted walker routes on the body's band and a stool upstairs is nearer by
/// a count that means nothing to a fist. Ties break on the lower id (roster
/// order), so two runs cannot disagree about who got hit.
[[nodiscard]] const sim::Actor* nearestMark(const Session& session) {
    const sim::Actor* best = nullptr;
    std::int32_t bestDistance = 0;
    for (const sim::Actor& actor : session.tavern().actors()) {
        if (!actor.present() || sim::isFloored(actor.activity()) ||
            actor.role() == sim::ActorRole::Vermin ||
            actor.band() != session.body().band()) {
            continue;
        }
        const std::int32_t distance =
            actor.distanceTo(session.body().x(), session.body().y());
        if (best == nullptr || distance < bestDistance) {
            best = &actor;
            bestDistance = distance;
        }
    }
    return best;
}

/// Whether this body is in the fight the room is resolving -- currentFight()
/// is the player plus brawlers_, by actor id. The one honest reading of "the
/// fight is on with HIM": the house minds a swing the instant the sightline
/// finds a person (joinBrawl fires before the roll), landed or not.
[[nodiscard]] bool inTheFight(const Session& session, std::int32_t markId) {
    for (const sim::Fighter& fighter : session.tavern().currentFight()) {
        if (fighter.actorId == markId) {
            return true;
        }
    }
    return false;
}

/// The first half of every scripted swing: walks up to a body if it is out of
/// reach, steps out of its tile if the walk ended IN it, and puts it DEAD ON
/// the crosshair. False when the mark is gone or on the floor.
[[nodiscard]] bool closeOnAndFace(Session& session, std::int32_t markId) {
    const sim::Actor* mark = session.tavern().actorById(markId);
    if (mark == nullptr || !mark->present() || sim::isFloored(mark->activity())) {
        return false;
    }
    if (mark->distanceTo(session.body().x(), session.body().y()) > sim::kMeleeReach) {
        walkToTile(session, mark->tileX(), mark->tileY());
        mark = session.tavern().actorById(markId);
        if (mark == nullptr) {
            return false;
        }
    }
    if (mark->x() == session.body().x() && mark->y() == session.body().y()) {
        // Standing IN him: `along` is zero and the ray finds nobody. One
        // step of clear ground opens the gap the crosshair needs; tried
        // in each direction until the body actually moved, since the
        // first way may be a wall.
        const std::int32_t beforeX = session.body().x();
        const std::int32_t beforeY = session.body().y();
        const std::int32_t nudges[4][2] = {{-1, 0}, {0, -1}, {0, 1}, {1, 0}};
        for (const auto& nudge : nudges) {
            sim::MoveInput back;
            back.forward = nudge[0];
            back.strafe = nudge[1];
            back.autoTraverse = false;
            back.snapVelocity = true;
            session.step(back);
            if (session.body().x() != beforeX || session.body().y() != beforeY) {
                break;
            }
        }
        mark = session.tavern().actorById(markId);
        if (mark == nullptr) {
            return false;
        }
    }
    facePlayerAndSync(session, mark->x(), mark->y());
    return true;
}

/// Walks up to a body, puts it DEAD ON the crosshair and taps -- the same
/// walk-up / re-face / tap / step-past-recovery beat the tenant and rat drives
/// throw -- until a blow CONNECTS or the tries run out. Connection is read
/// from the body itself: its hit points moving, or it going to the floor,
/// which is strike()'s own output and the one thing a whiffed roll never
/// changes (the house minds a swing the moment the sightline finds a person,
/// landed or not, so the player's standing is no proof of a blow). Between
/// taps the man may shift his feet or trade back, so each tap re-closes if he
/// left reach and re-faces him regardless; and the lockout is stepped through
/// in full first, because a down-edge in recovery is dropped, not buffered.
[[nodiscard]] bool tapUntilItConnects(Session& session, std::int32_t markId, int tries) {
    for (int attempt = 0; attempt < tries; ++attempt) {
        if (!closeOnAndFace(session, markId)) {
            return false;
        }
        const sim::Actor* mark = session.tavern().actorById(markId);
        if (mark == nullptr) {
            return false;
        }
        const std::int32_t hpBefore = mark->hp();
        session.punch();
        const sim::Actor* struck = session.tavern().actorById(markId);
        if (struck != nullptr &&
            (struck->hp() < hpBefore || sim::isFloored(struck->activity()))) {
            return true;
        }
        session.stepMany(sim::MoveInput{}, sim::kSwingRecoverySteps + 1);
    }
    return false;
}

/// Walks up to a body, puts it on the crosshair and swings ONCE -- the swing a
/// fight needs and not one more. STANCE & ROOM BUILD: the nemesis line throws
/// this and not tapUntilItConnects, because every CONNECTED tap is hit points
/// off a man the line needs standing, and the fight does not need the blow to
/// land: the house minds a swing the instant the sightline finds a person
/// (joinBrawl fires before the roll), so one swing that found him has the room
/// resolving the fight with him whether or not the roll did. Retried only
/// while the ray found nobody, or somebody else (he shifted his feet), which
/// costs him nothing; the lockout is stepped through between tries.
[[nodiscard]] bool swingOnceAt(Session& session, std::int32_t markId, int tries) {
    for (int attempt = 0; attempt < tries && !inTheFight(session, markId); ++attempt) {
        if (!closeOnAndFace(session, markId)) {
            return false;
        }
        session.punch();
        if (inTheFight(session, markId)) {
            return true;
        }
        session.stepMany(sim::MoveInput{}, sim::kSwingRecoverySteps + 1);
    }
    return inTheFight(session, markId);
}

/// Walks to a named person and opens a conversation with them.
///
/// Onto their OWN tile, and that is not laziness. "Who answers" is the nearest
/// body within two tiles with ties broken on the lower id, and the Gull's cast
/// stand shoulder to shoulder: from the tile south of Captain Wake, Edda
/// Pierpont is exactly as near and has the lower id, so a capture aiming at the
/// captain would quietly photograph her instead. Distance zero has no tie to
/// break. The frame is composed afterwards, by stepping back off them.
[[nodiscard]] bool speakTo(Session& session, std::string_view name) {
    const sim::Actor* who = actorNamed(session, name);
    if (who == nullptr) {
        return false;
    }
    walkToTile(session, who->tileX(), who->tileY());
    session.closeConversation();
    session.interact();
    return session.talking() && session.tavern().dialogue().speaker().name == name;
}

/// Steps back off somebody and turns to look at them, so the captured frame has
/// a person in it rather than the inside of their coat.
void standBackFrom(Session& session, std::string_view name) {
    const sim::Actor* who = actorNamed(session, name);
    if (who == nullptr) {
        return;
    }
    // A spot the body can stand in AND see them from. Asked of the same two
    // functions the simulation asks -- standable() and lineOfSight() -- because
    // a capture that framed itself inside a table would be a picture of the
    // inside of a table, and the first version of this was.
    const std::int32_t band = who->band();
    const std::int32_t offsets[6][2] = {{0, -3}, {0, 3}, {-3, 0}, {3, 0}, {0, -2}, {2, 0}};
    // A CLEAR LINE TO THEM FIRST (rung plate, fifth pass). lineOfSight is
    // the simulation's: it sees over a table and through a standing man,
    // which is right for a sightline and wrong for a photograph -- the trade
    // and law frames showed a patron's back and a hood on a table under the
    // reticle with Cull, the man the card is about, behind them. So the
    // first pass over the offsets wants every tile between the spot and
    // them to be floor with nobody standing on it; only if no offset gives
    // that does the old rule stand in.
    const auto clearBetween = [&session, who, band](std::int32_t x, std::int32_t y) {
        const std::int32_t sx = who->tileX() > x ? 1 : (who->tileX() < x ? -1 : 0);
        const std::int32_t sy = who->tileY() > y ? 1 : (who->tileY() < y ? -1 : 0);
        for (std::int32_t tx = x + sx, ty = y + sy; tx != who->tileX() || ty != who->tileY();
             tx += sx, ty += sy) {
            if (!session.tiles().standable(tx, ty, band)) {
                return false;
            }
            for (const sim::Actor& other : session.tavern().actors()) {
                if (&other != who && other.present() && other.band() == band &&
                    other.tileX() == tx && other.tileY() == ty) {
                    return false;
                }
            }
        }
        return true;
    };
    for (const bool strict : {true, false}) {
        for (const auto& offset : offsets) {
            const std::int32_t x = who->tileX() + offset[0];
            const std::int32_t y = who->tileY() + offset[1];
            if (!session.tiles().standable(x, y, band) ||
                !session.tiles().lineOfSight(x, y, who->tileX(), who->tileY(), band)) {
                continue;
            }
            if (strict && !clearBetween(x, y)) {
                continue;
            }
            walkToTile(session, x, y);
            if (session.body().tileX() != x || session.body().tileY() != y) {
                continue;
            }
            // Faced by their own bearing, which for these cardinal offsets
            // is the cardinal the old rule chose.
            session.body().setYaw(sim::bearingTo(x, y, who->tileX(), who->tileY()));
            return;
        }
    }
}

/// Picks the first topic of a kind, if it is on the list.
bool pick(Session& session, sim::TopicKind kind) {
    const int at = topicOfKind(session, kind);
    if (at < 0) {
        return false;
    }
    session.chooseTopic(static_cast<std::size_t>(at));
    return true;
}

/// In at the door, up the stair, across the guest floor, and over the north
/// wall onto the lead.
void upOntoTheLead(Session& session) {
    walkToTile(session, sim::gull::kStairX, sim::gull::kStairY);
    climbAndLand(session);
    walkToTile(session, 150, sim::gull::kFootprintY0 + 1);
    session.body().setYaw(sim::kFacingNorth);
    climbAndLand(session);
}

/// Back down the flight, for when the body is on the guest floor rather than
/// the roof.
void comeDownstairs(Session& session) {
    walkToTile(session, sim::gull::kStairX, sim::gull::kStairY);
    session.dropDown();
}

/// Where a face is, above the feet, in tiles. The rigs are scaled by
/// render::figureScaleOf's height against the person's 1.875 (the 3D
/// instancer's own rule), and a licensed rig's crown stands about eight
/// tenths of that figure with the face a little under it: a serf's face
/// at a tile and a third, a watchman's a hair higher, an urchin's at a
/// tile. Measured off the frames, not the sheet.
[[nodiscard]] double faceHeightTiles(sim::WardType type) noexcept {
    return 0.72 * static_cast<double>(figureScaleOf(type).heightTiles);
}

/// LOOKS AT SOMEBODY'S FACE: the yaw to the body (bearingToward), and the
/// pitch to its face -- the eye stands a tile and three quarters up
/// (kEyeHeightTilesQ8) and a face is lower, so a level look at a person
/// two tiles off has their crown a hand under the horizon and their body
/// under the frame; a person looking at a person tilts. `yawOffset` puts
/// them off centre on purpose (the watch reveal keeps Cull clear of the
/// drawn blade). One step to sync, as facePlayerAndSync.
void lookAtFace(Session& session, std::int32_t xQ8, std::int32_t yQ8, sim::WardType type,
                sim::Angle yawOffset) {
    const std::int32_t dx = xQ8 - session.body().x();
    const std::int32_t dy = yQ8 - session.body().y();
    if (dx != 0 || dy != 0) {
        session.body().setYaw(static_cast<sim::Angle>((bearingToward(session, xQ8, yQ8) + yawOffset) &
                                                      (sim::kTurnFull - 1)));
    }
    const double apart = std::sqrt(static_cast<double>(dx) * static_cast<double>(dx) +
                                   static_cast<double>(dy) * static_cast<double>(dy)) /
                         static_cast<double>(sim::kSubOne);
    const double eye = static_cast<double>(sim::kEyeHeightTilesQ8) / static_cast<double>(sim::kSubOne);
    constexpr double kTwoPi = 6.283185307179586;
    if (apart > 0.05) {
        const double tilt = std::atan2(faceHeightTiles(type) - eye, apart);
        session.body().setPitch(static_cast<sim::Angle>(
            std::lround(tilt * static_cast<double>(sim::kTurnFull) / kTwoPi)));
    }
    session.stepMany(sim::MoveInput{}, 1);
}

/// The endings whose line is the killing's: the tag it leaves, the rope
/// hearing, the rope, and the new man after it. STREET SENSES leg (b):
/// "streetblood" is the rope hearing for a killing ALREADY on the record --
/// the street's (--street-assault=hearing makes it on the Tarwalk first) --
/// so the line skips the Gull's own killing and reads the blood the street
/// counted.
[[nodiscard]] bool courtEndingHangs(const std::string& ending) {
    return ending == "bloodtag" || ending == "ropepage" || ending == "rope" || ending == "newman" ||
           ending == "streetblood";
}

/// WORKS THE LOCK UNDER THE WIRE WITH WHAT A PLAYER HAS, AND NOTHING ELSE.
///
/// The S9 review's fifth finding, verbatim: "there is no test and no scripted
/// run anywhere in which a lock is picked open without foreknowledge of its
/// pins." Every case that opened one cleanly called `pinDepth()` first and
/// drove the pick straight to the answer; every shipped `--burgle` ended
/// `jammed=4 forced=4`. That is not a minigame with a hard tuning, it is a
/// minigame with no win condition on the board.
///
/// So: this is THE STRATEGY, written as a player would play it. It may look at
/// exactly four things, all of them on screen in the HUD's own lock row --
/// where the pick is being held, how many pins have dropped, how much strain is
/// on the wire, and what the last probe felt like. It never calls pinDepth, it
/// never touches the Lock, and it never reads the seed. Give it a hand with no
/// feel and it sweeps the track, which is all an apprentice can do; give it a
/// hand at kFeelLevel and it bisects, which is what the feel is FOR.
///
/// `stopAtPins` lets a capture halt mid-attempt so the shutter catches the
/// surface with pins down and the wire still in. Negative works it to the end.
/// Returns the number of probes made.
int workTheWire(Session& session, std::int32_t stopAtPins,
                std::int32_t maxProbes) {
    // The window the pin is known to be inside, in depth notches. Reset every
    // time a pin drops or a pick snaps, because both mean the wire is now on a
    // pin this strategy knows nothing about.
    std::int32_t low = 0;
    std::int32_t high = sim::kPinDepths - 1;
    // Which notches have already been ruled out for the pin under the wire. A
    // player's memory of the last few seconds, and nothing more.
    std::uint32_t tried = 0;
    const auto forget = [&]() {
        low = 0;
        high = sim::kPinDepths - 1;
        tried = 0;
    };
    std::int32_t pinsSeen = session.picking() ? session.lockpicking().pinsSet() : 0;
    int probes = 0;
    // A generous guard. A bisect finishes in four probes a pin; a blind sweep
    // takes nine and breaks wire doing it, and the roll runs out long before
    // this does.
    for (int guard = 0; guard < 400 && session.picking(); ++guard) {
        const sim::Lockpicking& wire = session.lockpicking();
        if (stopAtPins >= 0 && wire.pinsSet() >= stopAtPins) {
            break;
        }
        if (maxProbes >= 0 && probes >= maxProbes) {
            break;
        }
        if (wire.pinsSet() != pinsSeen) {
            pinsSeen = wire.pinsSet();
            forget();
        }
        if (low > high) {
            forget();
        }
        // The midpoint of what is left, or -- when the midpoint has already
        // been tried, which is the no-feel case -- the nearest notch to it that
        // has not been. THE SWEEP HAS TO REACH EVERY DEPTH: a version of this
        // that only ever walked the window upward looped over four of the nine
        // notches forever and could not open a lock at all.
        std::int32_t aim = low + (high - low) / 2;
        if ((tried & (1U << aim)) != 0U) {
            aim = -1;
            for (std::int32_t spread = 1; spread < sim::kPinDepths && aim < 0; ++spread) {
                const std::int32_t mid = low + (high - low) / 2;
                const std::int32_t down = mid - spread;
                const std::int32_t up = mid + spread;
                if (down >= low && (tried & (1U << down)) == 0U) {
                    aim = down;
                } else if (up <= high && (tried & (1U << up)) == 0U) {
                    aim = up;
                }
            }
            if (aim < 0) {
                // Every notch in the window is spent. Widen to the whole track,
                // and if that is spent too the pin moved under us -- forget it
                // all and start again.
                forget();
                aim = 0;
                while (aim < sim::kPinDepths && (tried & (1U << aim)) != 0U) {
                    ++aim;
                }
                if (aim >= sim::kPinDepths) {
                    aim = 0;
                }
            }
        }
        session.movePick(aim - wire.depth());
        session.probeLock();
        ++probes;
        tried |= 1U << aim;
        switch (wire.lastFeel()) {
            case sim::Feel::TooShallow:
                // The pin is DEEPER than where the pick was held.
                low = aim + 1;
                break;
            case sim::Feel::TooDeep:
                high = aim - 1;
                break;
            case sim::Feel::NoFeel:
                // No information at all. The notch is crossed off and nothing
                // else is learned. This is the apprentice's whole game, and it
                // is why an apprentice forces boxes.
                break;
            case sim::Feel::Set:
            case sim::Feel::Broke:
            case sim::Feel::Jammed:
            case sim::Feel::Open:
            default:
                forget();
                break;
        }
    }
    return probes;
}

// ---------------------------------------------------------------------------
// S10: --trail, the investigation walked
// ---------------------------------------------------------------------------

/// THE WHOLE DISTRICT, ONE BAND. kCaptureRegion is the Gilded Gull and the
/// pavement outside it, which is the right box for a scripted burglary and far
/// too small for a walk to the Mission. The trail crosses the ward, so it gets
/// a box that is the ward -- the world's own dimensions, one band deep.
///
/// It is still a BOX and still the same exact breadth-first search that
/// region_path.hpp has run since S2: nothing here is a new pathfinder.
///
/// Walks the body across the DISTRICT to a tile, on the band it is already on.
/// True when it got there -- and a false is a real answer, not a warning: a
/// lead nobody can walk to is a lead nobody can read.
[[nodiscard]] bool walkAcrossDistrict(Session& session, std::int32_t tileX,
                                      std::int32_t tileY) {
    sim::TileBox box;
    box.x0 = 0;
    box.y0 = 0;
    box.x1 = session.tiles().sizeX() - 1;
    box.y1 = session.tiles().sizeY() - 1;
    box.z0 = session.body().band();
    box.z1 = session.body().band();
    sim::RegionPath router(session.tiles(), box);
    std::vector<sim::PathStep> route;
    const sim::PathStep from{session.body().tileX(), session.body().tileY(),
                             session.body().band()};
    const sim::PathStep to{tileX, tileY, session.body().band()};
    if (!router.find(from, to, route)) {
        return false;
    }
    for (const sim::PathStep& waypoint : route) {
        const std::int32_t wx = sim::q8_tile_centre(waypoint.x);
        const std::int32_t wy = sim::q8_tile_centre(waypoint.y);
        bool arrived = false;
        // A generous guard: a tile is eight movement steps at a walk and a
        // corner costs a few more. Sixty is slack, not a licence to wander.
        for (int guard = 0; guard < 60 && !arrived; ++guard) {
            arrived = stepToward(session, wx, wy);
        }
        if (!arrived) {
            return false;
        }
    }
    return true;
}

/// ERRANDS A HUMAN CAN FINISH (2026-10-09). The facing every placement in
/// this file has always turned the body to: the dominant axis toward the
/// body it means to address.
[[nodiscard]] sim::Angle cardinalToward(std::int32_t toX, std::int32_t toY) noexcept {
    if (std::abs(toX) >= std::abs(toY)) {
        return toX > 0 ? sim::kFacingEast : sim::kFacingWest;
    }
    return toY > 0 ? sim::kFacingSouth : sim::kFacingNorth;
}

/// Who the talk key would address from the centre of (sx, sy) looking along
/// `yaw` -- Session::wardAddressee's own rule (in front first, nearest only
/// when nobody is in front), asked of a stand tile BEFORE the body is put on
/// it, so a scan cannot pick a tile the press would answer from somebody
/// else.
[[nodiscard]] const sim::WardActor* addresseeFrom(Session& session, std::int32_t sx,
                                                  std::int32_t sy, std::int32_t band,
                                                  sim::Angle yaw) {
    if (const sim::WardActor* ahead =
            session.people().inFrontOf(sim::q8_tile_centre(sx), sim::q8_tile_centre(sy), band,
                                       yaw, kStreetReachTiles);
        ahead != nullptr) {
        return ahead;
    }
    return session.people().nearestTo(sx, sy, band, kStreetReachTiles);
}

/// True when some OTHER visible ward body is DRAWN within `radius` tiles of
/// (x, y) -- its tile, or the tile it is sliding from, a walker being
/// somewhere between the two for the whole second -- or is WALKING within
/// `radius + kWalkerLead` of it: the shot is taken a breath after the
/// placement, and a body a tile a second down the quay covers that breath.
/// Close enough, either way, that a first-person turn toward the target
/// puts a second head in the lens beside whoever the frame means to be
/// about, or a body walks through the lens while the talk is open. The
/// crowd was never asked to keep a scripted camera's spot clear (nor should
/// it be: that is population state, not render state -- the crowd walking
/// THROUGH the player is the population's own open item, declared in the
/// voice README), so this is asked of the PLACEMENT.
[[nodiscard]] bool bodiesDrawnWithin(const Session& session, std::int32_t x, std::int32_t y,
                                     std::int32_t band, std::int32_t radius,
                                     std::int32_t exceptId) noexcept {
    constexpr std::int32_t kWalkerLead = 2;
    for (const sim::WardActor& other : session.people().actors()) {
        if (other.id == exceptId || !other.visible() || other.band != band) {
            continue;
        }
        const std::int32_t reach = wardWalking(other) ? radius + kWalkerLead : radius;
        const bool here = std::abs(other.x - x) <= reach && std::abs(other.y - y) <= reach;
        const bool from = std::abs(other.prevX - x) <= reach && std::abs(other.prevY - y) <= reach;
        if (here || from) {
            return true;
        }
    }
    return false;
}

/// Under the open sky: nothing baked in the cell over this one's head. The
/// street, a quay, a yard -- not a bunkhouse floor with a storey over it.
/// (render3d's cellRoofed asks the same cell, from the other side.)
[[nodiscard]] bool underOpenSky(const sim::TileQuery& tiles, std::int32_t x, std::int32_t y,
                                std::int32_t band) noexcept {
    const content::TileForm above = tiles.form(x, y, band + 1);
    return above == content::TileForm::Open || above == content::TileForm::Void;
}

}  // namespace scripted

}  // namespace granadad::render
