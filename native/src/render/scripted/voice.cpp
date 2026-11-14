// Scripted capture drivers: voice.
#include "scripted.hpp"

namespace granadad::render {

namespace {

// ---------------------------------------------------------------------------
// VOICE LANE: the one voice channel, photographed
// ---------------------------------------------------------------------------

/// How long an ambient wait may run before the drive gives up: a Tarwalk
/// afternoon that passes nobody within reach in a minute and a half is not
/// this build's Tarwalk, and the frame that comes out of it says so.
constexpr int kVoiceWaitSteps = 90 * sim::kStepsPerSecond;

/// True while the voice row holds a line off a key with this prefix.
[[nodiscard]] bool voiceRowUpFrom(const Session& session, std::string_view prefix) {
    const VoiceLine& line = session.voice().current();
    return session.voice().up() && line.key.rfind(prefix, 0) == 0;
}

/// Steps `input` until the row holds a line off `prefix`, then lets its own
/// fade settle so the frame is a picture of the line and not of its rise.
/// The hold outlasts the settle by four seconds at least, so nothing is
/// lost to the wait. False when the bound ran out.
[[nodiscard]] bool waitForVoice(Session& session, std::string_view prefix,
                                const sim::MoveInput& input, int boundSteps) {
    for (int i = 0; i < boundSteps; ++i) {
        session.step(input);
        if (voiceRowUpFrom(session, prefix)) {
            session.stepMany(sim::MoveInput{}, kPageEaseSteps + 2);
            return voiceRowUpFrom(session, prefix);
        }
    }
    return false;
}

/// The kind of figure the row's speaker is drawn as, for the face height.
[[nodiscard]] sim::WardType speakerFigure(const Session& session) noexcept {
    const VoiceLine& line = session.voice().current();
    if (line.actorId >= sim::kWardSpeakerIdBase) {
        const sim::WardActor* who = session.people().byId(line.actorId - sim::kWardSpeakerIdBase);
        return who != nullptr ? who->type : sim::WardType::Serf;
    }
    const sim::Actor* who = session.tavern().actorById(line.actorId);
    return who != nullptr ? figureForRole(who->role(), who->id()) : sim::WardType::Serf;
}

/// THE PLATE'S OWN WIDTH, as a fraction of the half-frame in tan space:
/// the crosshair's name plate starts at the middle of the frame and runs
/// RIGHT about four tenths of the way to the edge ("PETRA BARNACRE SERF" at
/// the minor scale). Measured off the frames rather than read out of the
/// HUD, deliberately -- this file composes a camera and has no business
/// linking the layout -- and kept generous, because a longer name is a
/// wider plate.
constexpr double kPlateHalfWidths = 0.46;

/// And how far off the middle a composed subject may stand, as a fraction
/// of the half-frame ANGLE: a sixth of the way out is the thirds a
/// photographer would use, and past a bit over half is a body heading for
/// the edge. Fractions, not degrees, because the field of view is a slider
/// in the options page: twelve degrees is a sixth of a 90-degree frame and
/// most of the way out of a 70-degree one, and the first draft of this
/// pushed the overheard trio clean off a --fov=70 frame.
constexpr double kSubjectOffMin = 0.22;

constexpr double kSubjectOffMax = 0.56;

/// A body's own half-width in radians at that distance -- 0.4 of a tile
/// either side of its middle, the figure width the crowd filter uses.
[[nodiscard]] double bodyHalfAngle(double apartTiles) noexcept {
    return std::atan(0.4 / std::max(0.6, apartTiles));
}

/// THE COMPOSED YAW: which way to turn so the subject stands clear of the
/// crosshair's name plate AND THE PLATE STANDS CLEAR OF EVERYBODY ELSE.
///
/// The first pass at this was a constant twelve degrees to the left, and it
/// solved the wrong half of the problem: the speaker's face came out from
/// under the label and three bystanders' faces went under it instead (the
/// `pass` frame printed "PETRA BARNACRE SERF" across three serfs at the end
/// of the street, naming a fourth who was behind the camera). The plate is a
/// rectangle at the middle of the frame; what matters is whether a DRAWN
/// BODY falls inside it, which is a question about the lens and not about a
/// bearing. So: walk candidate turns out from the smallest that clears the
/// plate, both sides, and take the first that puts the subject (and its
/// group) in the clear with NOBODY under the label. If the street is too
/// full for that -- a queue along a wall can cover every candidate -- take
/// the one that leaves the fewest faces under it, nearest counted worst,
/// and the frame is as good as that corner allows.
///
/// `groupHalf` widens the subject to a pair or a knot of three (the
/// overheard shot aims at a midpoint). Render side; doubles legal.
[[nodiscard]] sim::Angle composeYawOffset(const Session& session, std::int32_t xQ8,
                                          std::int32_t yQ8, std::int32_t band,
                                          std::int32_t speakerId, double groupHalf) noexcept {
    constexpr double kTwoPi = 6.283185307179586;
    const double hfovTan = static_cast<double>(session.camera().hfovTan);
    const double halfFov = std::atan(hfovTan);
    const double plateRight = std::atan(kPlateHalfWidths * hfovTan);
    const auto bamOf = [&](double radians) {
        return static_cast<sim::Angle>(std::lround(radians * static_cast<double>(sim::kTurnFull) / kTwoPi));
    };
    const auto radiansOf = [&](std::int32_t bam) {
        return static_cast<double>(bam) * kTwoPi / static_cast<double>(sim::kTurnFull);
    };
    const sim::Angle subject = bearingToward(session, xQ8, yQ8);
    const double apartSubject =
        std::hypot(static_cast<double>(xQ8 - session.body().x()),
                   static_cast<double>(yQ8 - session.body().y())) / static_cast<double>(sim::kSubOne);
    const double subjectHalf = groupHalf + bodyHalfAngle(apartSubject);

    // Everybody the lens could draw near this shot, with their bearing and
    // their own width: the Gull's roster at the sim's Q8, the ward's on its
    // slide. Eight tiles is the room and then some.
    struct Seen {
        sim::Angle bearing;
        double half;
        double apart;
    };
    std::vector<Seen> seen;
    const auto note = [&](std::int32_t bx, std::int32_t by, std::int32_t id) {
        if (id == speakerId) {
            return;
        }
        const double apart = std::hypot(static_cast<double>(bx - session.body().x()),
                                        static_cast<double>(by - session.body().y())) /
                             static_cast<double>(sim::kSubOne);
        if (apart < 0.3 || apart > 14.0) {
            return;
        }
        seen.push_back(Seen{bearingToward(session, bx, by), bodyHalfAngle(apart), apart});
    };
    for (const sim::Actor& actor : session.tavern().actors()) {
        if (actor.present() && actor.band() == band && actor.role() != sim::ActorRole::Vermin &&
            !sim::isFloored(actor.activity())) {
            note(actor.x(), actor.y(), actor.id());
        }
    }
    for (const sim::WardActor& actor : session.people().actors()) {
        if (actor.visible() && actor.band == band && sim::isPerson(actor.type)) {
            const std::pair<std::int32_t, std::int32_t> at = wardDrawnQ8(session, actor);
            note(at.first, at.second, actor.id + sim::kWardSpeakerIdBase);
        }
    }

    sim::Angle best = bamOf(kSubjectOffMin * halfFov);
    double bestCost = 1.0e18;
    for (int step = 0; step <= 24; ++step) {
        const double fraction =
            kSubjectOffMin + (kSubjectOffMax - kSubjectOffMin) * static_cast<double>(step) / 24.0;
        for (const int side : {1, -1}) {
            const double off = fraction * halfFov;
            // THE SUBJECT'S OWN CLEARANCE, and it is not the same either
            // side: the plate runs RIGHTWARD from the middle of the frame,
            // so a subject stood to the LEFT of it need only clear the
            // middle by its own shoulder, while one stood to the RIGHT has
            // to clear the whole width of the label. (The first draft
            // guarded only the left case, and the search promptly chose the
            // right one and printed the name across the face it was
            // composing.)
            const double clearance = side > 0 ? 0.0 : plateRight;
            if (off - subjectHalf < clearance) {
                continue;
            }
            if (off + subjectHalf > halfFov * 0.92) {
                continue;  // and it must still be inside the frame, whole
            }
            const sim::Angle candidate = static_cast<sim::Angle>(side * bamOf(off));
            const sim::Angle cameraYaw = static_cast<sim::Angle>(subject + candidate);
            // What the plate covers, in this camera: from the middle of the
            // frame rightward. A body whose own width overlaps that band is
            // under the label.
            double cost = 0.0;
            for (const Seen& body : seen) {
                double rel = radiansOf(static_cast<std::int32_t>(
                    ((body.bearing - cameraYaw) & (sim::kTurnFull - 1))));
                if (rel > kTwoPi / 2.0) {
                    rel -= kTwoPi;
                }
                if (std::abs(rel) > halfFov) {
                    continue;  // off frame; the plate cannot reach it
                }
                if (rel + body.half >= 0.0 && rel - body.half <= plateRight) {
                    cost += 8.0 / std::max(1.0, body.apart);  // nearest faces count worst
                }
            }
            // A tie goes to the smaller turn: the subject nearer the thirds
            // than the edge.
            cost += fraction;
            if (cost < bestCost) {
                bestCost = cost;
                best = candidate;
            }
        }
    }
    return best;
}

/// Turns the body to look at whoever the row names -- at their face, and
/// composed so the plate misses every face in the frame (composeYawOffset)
/// -- so the frame is a picture of the speaker and not of the wall the walk
/// ended against; a step to sync the facing, which the row's hold outlasts
/// many times over. A caller with its own reason to stand somewhere else
/// (the watch reveal, which also has a drawn blade to clear) passes its own
/// offset instead.
void faceTheSpeaker(Session& session) {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t band = 0;
    if (speakerDrawnQ8(session, x, y, band)) {
        lookAtFace(session, x, y, speakerFigure(session),
                   composeYawOffset(session, x, y, band, session.voice().current().actorId, 0.0));
    }
}

/// The same, with the turn chosen by the caller.
void faceTheSpeakerAt(Session& session, sim::Angle yawOffset) {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t band = 0;
    if (speakerDrawnQ8(session, x, y, band)) {
        lookAtFace(session, x, y, speakerFigure(session), yawOffset);
    }
}

/// OVERHEARD: the frame wants BOTH halves of the exchange. Looks at the
/// midpoint of the pair's faces -- and when the two stand on one line from
/// the lens (the partner behind the speaker, one body in the picture) it
/// walks, once, to the best vantage a short way off: the standable tile
/// within three of here, two to six tiles from the nearer of the pair,
/// with a clear lens line to both, that opens the widest angle between
/// them -- what a person does to see who is talking. A real walk on the
/// room's own router, then the look. `mayWalk` false only re-aims (the
/// second call, after the row's fade has settled). Nothing when the row
/// names no partner.
void faceThePair(Session& session, bool mayWalk) {
    const VoiceLine& line = session.voice().current();
    if (line.empty() || line.partnerId < 0) {
        faceTheSpeaker(session);
        return;
    }
    const auto drawnQ8 = [&](std::int32_t id, std::int32_t& x, std::int32_t& y) {
        if (id >= sim::kWardSpeakerIdBase) {
            const sim::WardActor* who = session.people().byId(id - sim::kWardSpeakerIdBase);
            if (who == nullptr || !who->visible()) {
                return false;
            }
            const std::pair<std::int32_t, std::int32_t> at = wardDrawnQ8(session, *who);
            x = at.first;
            y = at.second;
            return true;
        }
        const sim::Actor* who = session.tavern().actorById(id);
        if (who == nullptr || !who->present()) {
            return false;
        }
        x = who->x();
        y = who->y();
        return true;
    };
    std::int32_t ax = 0;
    std::int32_t ay = 0;
    std::int32_t bx = 0;
    std::int32_t by = 0;
    if (!drawnQ8(line.actorId, ax, ay) || !drawnQ8(line.partnerId, bx, by)) {
        faceTheSpeaker(session);
        return;
    }
    const sim::WardType figure = speakerFigure(session);
    const std::int32_t band = session.body().band();
    // The angle the pair opens from a Q8 point, in BAM, folded to a half
    // turn: what "both in the picture" measures.
    const auto opening = [&](std::int32_t fromX, std::int32_t fromY) {
        constexpr double kTwoPi = 6.283185307179586;
        const double ta = std::atan2(static_cast<double>(ax - fromX), static_cast<double>(-(ay - fromY)));
        const double tb = std::atan2(static_cast<double>(bx - fromX), static_cast<double>(-(by - fromY)));
        double off = std::abs(ta - tb);
        if (off > kTwoPi / 2.0) {
            off = kTwoPi - off;
        }
        return static_cast<std::int32_t>(off * static_cast<double>(sim::kTurnFull) / kTwoPi);
    };
    // Stacked when the two are inside a figure's width of each other at the
    // nearer one's distance -- one body hides the other.
    const auto stackedFrom = [&](std::int32_t fromX, std::int32_t fromY) {
        const double da = std::hypot(static_cast<double>(ax - fromX), static_cast<double>(ay - fromY));
        const double db = std::hypot(static_cast<double>(bx - fromX), static_cast<double>(by - fromY));
        const double nearer = std::max(1.0, std::min(da, db)) / static_cast<double>(sim::kSubOne);
        constexpr double kFigureWidthTiles = 0.8;
        constexpr double kTwoPi = 6.283185307179586;
        const std::int32_t width = static_cast<std::int32_t>(
            std::atan(kFigureWidthTiles / nearer) * static_cast<double>(sim::kTurnFull) / kTwoPi);
        return opening(fromX, fromY) < width;
    };
    if (mayWalk && stackedFrom(session.body().x(), session.body().y())) {
        const std::int32_t px = session.body().tileX();
        const std::int32_t py = session.body().tileY();
        std::int32_t bestX = px;
        std::int32_t bestY = py;
        std::int32_t bestOpening = -1;
        for (std::int32_t y = py - 3; y <= py + 3; ++y) {
            for (std::int32_t x = px - 3; x <= px + 3; ++x) {
                if (!session.tiles().standable(x, y, band)) {
                    continue;
                }
                const std::int32_t cx = sim::q8_tile_centre(x);
                const std::int32_t cy = sim::q8_tile_centre(y);
                const double da = std::hypot(static_cast<double>(ax - cx), static_cast<double>(ay - cy)) /
                                  static_cast<double>(sim::kSubOne);
                const double db = std::hypot(static_cast<double>(bx - cx), static_cast<double>(by - cy)) /
                                  static_cast<double>(sim::kSubOne);
                const double nearer = std::min(da, db);
                if (nearer < 2.0 || nearer > static_cast<double>(kOverheardEarshotTiles)) {
                    continue;
                }
                if (!cameraSightClear(session.tiles(), cx, cy, ax, ay, band, false) ||
                    !cameraSightClear(session.tiles(), cx, cy, bx, by, band, false)) {
                    continue;
                }
                if (session.people().nearestTo(x, y, band, 0) != nullptr) {
                    continue;
                }
                const std::int32_t opens = opening(cx, cy);
                if (opens > bestOpening) {
                    bestOpening = opens;
                    bestX = x;
                    bestY = y;
                }
            }
        }
        if (bestX != px || bestY != py) {
            walkToTile(session, bestX, bestY);
        }
    }
    // The look is at the pair's MIDPOINT, so the composed turn is given the
    // half-angle they open from here and clears the group as one body --
    // and the search does the rest, including the third serf who stopped to
    // listen, who wore the label when the offset was a constant.
    constexpr double kTwoPi = 6.283185307179586;
    const double groupHalf = static_cast<double>(opening(session.body().x(), session.body().y())) *
                             kTwoPi / static_cast<double>(sim::kTurnFull) / 2.0;
    const std::int32_t midX = (ax + bx) / 2;
    const std::int32_t midY = (ay + by) / 2;
    lookAtFace(session, midX, midY, figure,
               composeYawOffset(session, midX, midY, band, line.actorId, groupHalf));
}

/// Where `--voice=overheard` stands, and the pair the channel hears from there.
struct OverheardStand {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t band = 0;
    std::int32_t speakerId = -1;
    std::int32_t partnerId = -1;
};

/// FIND TWO PEOPLE TALKING, AND A PLACE TO HEAR THEM FROM.
///
/// The overheard ending used to walk blind: forty-five seconds at the spawn,
/// then forward with the smoke's drift of the head until the row came up. On
/// the tree it was tuned on that walk met three serfs by the sacks at the
/// Bilge's corner. The crowd-flow lane moved where the crowd is at any given
/// second (a blocked walker sidesteps now instead of queueing), so the same
/// walk crossed a different street and ended at (146,69) on band 21, inside
/// the Gull's footprint -- where the channel only listens to the taproom
/// (Tavern::playerInside asks x and y, not the band) -- having heard nobody
/// in ninety seconds. The pairs never went anywhere: the crowd's nudge
/// is a draw offset (kPersonalSpaceTiles, 0.15 of a tile, never written back)
/// and the sim keeps no gap between two bodies at rest, so the street case
/// (an overheard snatch comes from a body standing still beside another)
/// still hears them. The walk just never met one.
///
/// So the drive looks first. The pairs are the channel's own
/// (overheardStanding, kOverheardPairTiles), ones that STAY standing first --
/// a crew ringing its post stands there all shift, a walker between two
/// strides doesn't -- nearest the body first. For each, a stand the channel
/// hears a pair from, asked of the channel itself (overheardStreetBodies, then
/// overheardCompany, in the order it takes them), outside the Gull, and the
/// picture checked before the body moves:
///   - two tiles from the nearer of the two at least, faceThePair's own floor,
///     and nobody inside the pass reach, so no good-day takes the row first;
///   - a clear lens line to both, nobody drawn across either;
///   - the two open wider than a figure, so faceThePair has no reason to walk;
///   - the speaker the LEFT of the two: composeYawOffset stands a group left of
///     the name plate, and the outer body of it is the one clear of the
///     crosshair;
///   - and room for that turn with the speaker still inside the frame at the
///     widest one it could take.
/// The nearest such stand wins -- the fewer tiles between you and the
/// speaker, the fewer bodies that can stop in between and be heard first --
/// then the widest margin, then the lower (y, x), so two runs stand in the
/// same place. Four passes, strict to bare, runStreetLine's shape: the sky
/// goes, then the margin narrows, then only the channel's rule is left.
/// Render side; doubles legal, nothing written.
[[nodiscard]] bool findOverheardStand(const Session& session, OverheardStand& out) {
    constexpr double kTwoPi = 6.283185307179586;
    constexpr double kDegree = kTwoPi / 360.0;
    // speakerInView's own edge margin (kEdgeMarginBam, about 5.6 degrees)
    // and two more for the drawn slide and the BAM rounding.
    constexpr double kEdgeMargin = 7.6 * kDegree;
    const sim::WardPopulation& people = session.people();
    const sim::TileQuery& tiles = session.tiles();
    const std::vector<sim::WardActor>& actors = people.actors();
    const std::int32_t talkId = session.wardTalkingTo();
    const double halfFov = std::atan(static_cast<double>(session.camera().hfovTan));
    const std::int32_t fromX = session.body().tileX();
    const std::int32_t fromY = session.body().tileY();
    const auto tilesOf = [](std::int32_t q8) {
        return static_cast<double>(q8) / static_cast<double>(sim::kSubOne);
    };
    // North is -Y, so the bearing of an offset is atan2(dx, -dy): clockwise,
    // the same turn bearingToward measures.
    const auto bearingOf = [](double dx, double dy) { return std::atan2(dx, -dy); };
    const auto wrapped = [&](double radians) {
        while (radians > kTwoPi / 2.0) {
            radians -= kTwoPi;
        }
        while (radians <= -kTwoPi / 2.0) {
            radians += kTwoPi;
        }
        return radians;
    };
    struct Seed {
        std::int32_t apart;
        std::int32_t a;
        std::int32_t b;
    };
    for (int pass = 0; pass < 4; ++pass) {
        const bool stillOnly = pass < 3;
        const bool wantsSky = pass == 0;
        const std::int32_t margin = pass <= 1 ? kPassReachTiles : (pass == 2 ? 1 : 0);
        std::vector<Seed> seeds;
        for (std::size_t i = 0; i < actors.size(); ++i) {
            const sim::WardActor& a = actors[i];
            if (!overheardStanding(a, talkId) || (stillOnly && !wardStandsStill(a))) {
                continue;
            }
            for (std::size_t j = i + 1; j < actors.size(); ++j) {
                const sim::WardActor& b = actors[j];
                if (b.band != a.band || std::abs(b.x - a.x) > kOverheardPairTiles ||
                    std::abs(b.y - a.y) > kOverheardPairTiles || !overheardStanding(b, talkId) ||
                    (stillOnly && !wardStandsStill(b))) {
                    continue;
                }
                seeds.push_back(
                    Seed{std::max(std::abs(a.x - fromX), std::abs(a.y - fromY)), a.id, b.id});
            }
        }
        std::sort(seeds.begin(), seeds.end(), [](const Seed& l, const Seed& r) {
            if (l.apart != r.apart) {
                return l.apart < r.apart;
            }
            return l.a != r.a ? l.a < r.a : l.b < r.b;
        });
        for (const Seed& seed : seeds) {
            const sim::WardActor* a = people.byId(seed.a);
            const sim::WardActor* b = people.byId(seed.b);
            if (a == nullptr || b == nullptr) {
                continue;
            }
            const std::int32_t band = a->band;
            bool found = false;
            std::int32_t bestApart = 0;
            double bestSlack = 0.0;
            OverheardStand best;
            for (std::int32_t sy = std::max(a->y, b->y) - kOverheardEarshotTiles;
                 sy <= std::min(a->y, b->y) + kOverheardEarshotTiles; ++sy) {
                for (std::int32_t sx = std::max(a->x, b->x) - kOverheardEarshotTiles;
                     sx <= std::min(a->x, b->x) + kOverheardEarshotTiles; ++sx) {
                    // THE GROUND: the street, not the Gull (inside its walls
                    // the channel hears the taproom), and clear.
                    if (sim::gull::insideFootprint(sx, sy) || !tiles.standable(sx, sy, band)) {
                        continue;
                    }
                    if (wantsSky && !underOpenSky(tiles, sx, sy, band)) {
                        continue;
                    }
                    if (margin > 0 ? bodiesDrawnWithin(session, sx, sy, band, margin, -1)
                                   : people.nearestTo(sx, sy, band, 0) != nullptr) {
                        continue;
                    }
                    // THE CHANNEL'S OWN PICK from here: the first body in its
                    // order with company, and that company.
                    const std::vector<OverheardBody> near =
                        overheardStreetBodies(people, &tiles, sx, sy, band, talkId);
                    std::int32_t speakerId = -1;
                    std::int32_t partnerId = -1;
                    std::int32_t speakerApart = 0;
                    for (std::size_t k = 0; k < near.size() && speakerId < 0; ++k) {
                        const std::int32_t company = overheardCompany(near, k);
                        if (company >= 0) {
                            speakerId = near[k].speakerId;
                            partnerId = company;
                            speakerApart = near[k].apart;
                        }
                    }
                    if (speakerId < 0) {
                        continue;
                    }
                    const sim::WardActor* speaker = people.byId(speakerId - sim::kWardSpeakerIdBase);
                    const sim::WardActor* partner = people.byId(partnerId - sim::kWardSpeakerIdBase);
                    if (speaker == nullptr || partner == nullptr ||
                        (stillOnly && (!wardStandsStill(*speaker) || !wardStandsStill(*partner)))) {
                        continue;
                    }
                    // THE PICTURE, from the tile's centre (where the placement
                    // puts the eye) to where the two are drawn.
                    const std::int32_t eyeX = sim::q8_tile_centre(sx);
                    const std::int32_t eyeY = sim::q8_tile_centre(sy);
                    const std::pair<std::int32_t, std::int32_t> s = wardDrawnQ8(session, *speaker);
                    const std::pair<std::int32_t, std::int32_t> p = wardDrawnQ8(session, *partner);
                    const double sdx = tilesOf(s.first - eyeX);
                    const double sdy = tilesOf(s.second - eyeY);
                    const double pdx = tilesOf(p.first - eyeX);
                    const double pdy = tilesOf(p.second - eyeY);
                    const double nearer = std::min(std::hypot(sdx, sdy), std::hypot(pdx, pdy));
                    if (nearer < 2.0) {
                        continue;
                    }
                    const double leftOfPartner = wrapped(bearingOf(sdx, sdy) - bearingOf(pdx, pdy));
                    if (leftOfPartner >= 0.0) {
                        continue;  // the speaker would be the inner body, on the crosshair
                    }
                    const double opening = -leftOfPartner;
                    const double figure = std::atan(0.8 / std::max(1.0, nearer));
                    if (opening < figure + 2.0 * kDegree) {
                        continue;  // stacked, or near enough that faceThePair walks
                    }
                    const double mdx = (sdx + pdx) / 2.0;
                    const double mdy = (sdy + pdy) / 2.0;
                    const double subjectHalf = opening / 2.0 + bodyHalfAngle(std::hypot(mdx, mdy));
                    const double offLeast = std::max(kSubjectOffMin * halfFov, subjectHalf);
                    const double offMost =
                        std::min(kSubjectOffMax * halfFov, 0.92 * halfFov - subjectHalf);
                    if (offMost - offLeast < 0.02 * halfFov) {
                        continue;  // no turn of composeYawOffset's fits the group
                    }
                    const double beyond = wrapped(bearingOf(mdx, mdy) - bearingOf(sdx, sdy));
                    const double widest = offMost + beyond;
                    if (widest > halfFov - kEdgeMargin) {
                        continue;
                    }
                    // THE LENS.
                    if (!cameraSightClear(tiles, eyeX, eyeY, s.first, s.second, band, stillOnly) ||
                        !cameraSightClear(tiles, eyeX, eyeY, p.first, p.second, band, false) ||
                        bodyBetweenFrom(session, eyeX, eyeY, s.first, s.second, band, speakerId) ||
                        bodyBetweenFrom(session, eyeX, eyeY, p.first, p.second, band, partnerId)) {
                        continue;
                    }
                    const double slack = std::min(
                        {opening - figure, offMost - offLeast, halfFov - kEdgeMargin - widest});
                    if (!found || speakerApart < bestApart ||
                        (speakerApart == bestApart && slack > bestSlack)) {
                        found = true;
                        bestApart = speakerApart;
                        bestSlack = slack;
                        best = OverheardStand{sx, sy, band, speakerId, partnerId};
                    }
                }
            }
            if (found) {
                out = best;
                return true;
            }
        }
    }
    return false;
}

/// THE WATCH REVEAL: steel drawn across the taproom from Watchman Cull, and
/// his readback as he crosses it to you. Four beats:
///   1. Cull can SEE you where you stand (Tavern::noticeBy, asked), and the
///      stand tile was chosen for the reveal -- as far across the room as
///      his eyes reach, with the LENS's own clear line to him AND to the
///      last tiles of the route he will walk (cameraSightClear over the
///      room's own router), so the frame he crosses into is a frame of him.
///   2. Steel up in his sight: an Edged weapon in a raised hand, no blow.
///      Tavern::violenceInView's own "a blade in a raised hand is cause on
///      its own, before it is swung" -- the same shape --street-assault
///      raises on the Tarwalk.
///   3. He goes Closing on VIOLENCE and the halt is on the alert row in his
///      name (watch.halt).
///   4. His readback lands on the voice row (watch.approach.violence.steel)
///      as he closes -- at reach, replacing the halt -- and the shot is on
///      him, turned the step the row lands and again after its fade has
///      settled, with no idle between the row and the turn for anything
///      else to speak into (the ONE MAN, ONE LINE case pins that window).
///
/// WHY NOT THE WATCH LINE'S OWN PLACEMENT. The feel build's --watch-halt
/// stands beside a patron and swings, because the arrest is its point; the
/// voice ending used to borrow that placement with the mark farthest from
/// Cull, and "farthest with his line of sight" in a room this size was two
/// tiles away, in the pocket between the two tables at (148,69) and (151,69)
/// -- the reveal turned into the west face of the second table, twice. The
/// readback is about the crossing; the crossing wants distance and a view,
/// and steel alone gives Cull his cause without a mark to stand beside.
[[nodiscard]] int runWatchRevealLine(Session& session) {
    int landed = 0;
    gWatchHaltNote.clear();
    const sim::Tavern& tavern = session.tavern();
    // In through the door to the bar, the smoke's own route-walk.
    walkToTile(session, sim::gull::kBartenderX, sim::gull::kBartenderY + 1);
    session.closeConversation();
    const sim::Actor* cull = actorNamed(session, "Watchman Cull");
    if (cull == nullptr || !cull->present()) {
        gWatchHaltNote = " cull=absent";
        return landed;
    }
    const std::int32_t cullId = cull->id();
    const std::int32_t band = cull->band();
    const std::int32_t cullX = cull->tileX();
    const std::int32_t cullY = cull->tileY();

    // THE STAND TILES, RANKED. Every free floor cell of his storey inside the
    // walls that he can see by the sim's rule (his sight radius, his tile
    // line), that the lens can see him from, and whose approach he can be
    // watched walking -- the route the room's router draws from his stool
    // to the tile, its last kRevealTiles steps each on a clear lens line
    // from the tile. Farthest first, so he has a room to cross; ties by
    // (y, x) so two runs stand in the same place.
    constexpr std::int32_t kMinCrossingTiles = 3;
    constexpr std::int32_t kRevealTiles = 3;
    struct Stand {
        std::int32_t apart;
        std::int32_t x;
        std::int32_t y;
    };
    std::vector<Stand> stands;
    sim::RegionPath router(session.tiles(), kCaptureRegion);
    std::vector<sim::PathStep> route;
    for (std::int32_t y = sim::gull::kFootprintY0 + 1; y < sim::gull::kFootprintY1; ++y) {
        for (std::int32_t x = sim::gull::kFootprintX0 + 1; x < sim::gull::kFootprintX1; ++x) {
            if (!session.tiles().standable(x, y, band)) {
                continue;
            }
            const std::int32_t apart = std::max(std::abs(x - cullX), std::abs(y - cullY));
            if (apart < kMinCrossingTiles || apart > sim::kWatchSightTiles) {
                continue;
            }
            bool taken = false;
            for (const sim::Actor& actor : tavern.actors()) {
                if (actor.present() && actor.band() == band && actor.tileX() == x &&
                    actor.tileY() == y) {
                    taken = true;
                    break;
                }
            }
            if (taken || !session.tiles().lineOfSight(cullX, cullY, x, y, band)) {
                continue;
            }
            const std::int32_t sx = sim::q8_tile_centre(x);
            const std::int32_t sy = sim::q8_tile_centre(y);
            if (!cameraSightClear(session.tiles(), sx, sy, cull->x(), cull->y(), band)) {
                continue;
            }
            route.clear();
            if (!router.find(sim::PathStep{cullX, cullY, band}, sim::PathStep{x, y, band}, route)) {
                continue;
            }
            bool approachInView = true;
            for (const sim::PathStep& step : route) {
                if (std::max(std::abs(step.x - x), std::abs(step.y - y)) > kRevealTiles) {
                    continue;
                }
                if (!cameraSightClear(session.tiles(), sx, sy, sim::q8_tile_centre(step.x),
                                      sim::q8_tile_centre(step.y), band)) {
                    approachInView = false;
                    break;
                }
            }
            if (approachInView) {
                stands.push_back(Stand{apart, x, y});
            }
        }
    }
    std::sort(stands.begin(), stands.end(), [](const Stand& a, const Stand& b) {
        if (a.apart != b.apart) {
            return a.apart > b.apart;
        }
        return a.y != b.y ? a.y < b.y : a.x < b.x;
    });

    // Walk to the best, face him, and ASK whether he has you -- a second of
    // standing still so the walk's own noise is off the rule and he has
    // looked up. The next stand down if not.
    constexpr std::size_t kStandsTried = 6;
    bool seen = false;
    for (std::size_t i = 0; i < stands.size() && i < kStandsTried && !seen; ++i) {
        walkToTile(session, stands[i].x, stands[i].y);
        if (session.body().tileX() != stands[i].x || session.body().tileY() != stands[i].y) {
            continue;
        }
        cull = tavern.actorById(cullId);
        if (cull == nullptr || !cull->present()) {
            break;
        }
        facePlayerAndSync(session, cull->x(), cull->y());
        session.stepMany(sim::MoveInput{}, sim::kStepsPerSecond);
        cull = tavern.actorById(cullId);
        seen = cull != nullptr && cull->present() && tavern.noticeBy(*cull).seen;
    }
    if (!seen) {
        gWatchHaltNote = " stand=none-in-culls-sight stands=" + std::to_string(stands.size());
        return landed;
    }
    ++landed;  // 1: Cull sees you, across the room
    gWatchHaltNote = " stand=(" + std::to_string(session.body().tileX()) + "," +
                     std::to_string(session.body().tileY()) + ") apart=" +
                     std::to_string(std::max(std::abs(session.body().tileX() - cull->tileX()),
                                             std::abs(session.body().tileY() - cull->tileY())));

    // STEEL UP, NOBODY IN REACH. The blade in the hand (Kill is what a drawn
    // blade means, the Watch line's own reading) and one tap of the Attack
    // key -- the fight-mode verb, which from hands-down raises them and
    // swings at whoever is on the sightline; nobody is, at this range, so
    // it is the blade coming up and nothing else. The stance bit
    // Tavern::violenceInView reads as cause on its own.
    session.tavern().setPlayerCombat(sim::Weapon::Edged, sim::Intent::Kill);
    session.punch();
    session.stepMany(sim::MoveInput{}, 1);
    if (tavern.playerHandsUp() && tavern.playerWeapon() == sim::Weapon::Edged &&
        tavern.violenceInView()) {
        ++landed;  // 2
    }

    // THE HALT. Stand still. Within a few seconds he is Closing on Violence
    // and the row reads what he said.
    bool closing = false;
    for (int second = 0; second < 20 && !closing; ++second) {
        session.stepMany(sim::MoveInput{}, sim::kStepsPerSecond);
        closing = tavern.watchStance() == sim::Tavern::WatchStance::Closing &&
                  tavern.watchInterest() == sim::WatchCause::Violence;
    }
    if (closing && !tavern.lastDemand().empty() &&
        tavern.lastDemand().rfind("Watchman Cull: ", 0) == 0 &&
        session.lastMessage() == tavern.lastDemand()) {
        ++landed;  // 3
    }

    // THE READBACK, and the shot on him. The turn goes the step the row
    // lands -- before the row's own fade is let settle, not after it -- so
    // the settle runs with the lens already on him, and once more after it
    // for the ground he covered meanwhile. Two steps of facing in all, the
    // same idle the row-then-turn order spent; nothing else gets a window to
    // speak into.
    for (int i = 0; i < 15 * sim::kStepsPerSecond; ++i) {
        session.step(sim::MoveInput{});
        if (voiceRowUpFrom(session, "watch.approach")) {
            break;
        }
    }
    // OFF THE BLADE. Steel is up, and the drawn blade runs from the hand at
    // the lower right up across the middle of the frame; a man dead centre
    // has it through his face. A third of the frame to the RIGHT of centre
    // (eighteen degrees at this field of view) has the blade low across his
    // legs and his face clear above it -- and the lens turned that way looks
    // up the empty row past him rather than at the stools, so nobody else
    // stands where the eye goes first. The left third was tried: face
    // clear too, but the turn put a drinking patron dead centre under the
    // prompt, and the frame read as his.
    constexpr sim::Angle kOffTheBlade = -sim::angle_from_degrees(18);
    if (voiceRowUpFrom(session, "watch.approach")) {
        faceTheSpeakerAt(session, kOffTheBlade);
        session.stepMany(sim::MoveInput{}, kPageEaseSteps + 1);
        faceTheSpeakerAt(session, kOffTheBlade);
        if (voiceRowUpFrom(session, "watch.approach")) {
            ++landed;  // 4
        }
    }
    return landed;
}

}  // namespace

namespace scripted {

/// THE ONE VOICE CHANNEL, played through the real verbs -- the smoke's own
/// walk, the talk key, the topic list, the swing, the Watch line -- and
/// stopped the step the row holds what was asked for. Nothing here composes
/// a line: every word on the row came out of BarkTables through the
/// emitter, off a body the simulation put there.
[[nodiscard]] int runVoiceLine(Session& session, const std::string& ending) {
    int landed = 0;
    if (ending == "pass") {
        // THE TARWALK, STOOD IN. A PASS IS SOMEBODY PASSING, and the first
        // row off the spawn came from a serf standing in the nook beside it
        // before the old forward walk had taken a step -- the critic's "a
        // wall with a head on it". Two walks were tried in its place: the
        // first draft's drift wandered off the quay onto the upper band
        // inside ninety seconds, and a straight walk west ended in the dead
        // end at (141,63) being begged at by a wastrel in a doorway. So the
        // body STANDS where the game puts it, on the Tarwalk at the spawn,
        // and looks west down the street -- the Gull's door, the queue along
        // its wall, the quay beyond -- which is what a person does on a
        // street corner, and waits for a line from a body ON THE MOVE
        // (wardWalking: sliding, or a leg drawn to somewhere else), which is
        // what the frame claims. Past half the bound it takes anybody, so a
        // thin afternoon still gets its line.
        session.body().setYaw(sim::kFacingWest);
        // And a body the lens can reach: a passer-by needs no sight line to
        // say good day (the emitter's own rule), but a frame of one does --
        // the wastrel in that doorway said his line to a wall.
        const auto passingSpoke = [&](bool anybody) {
            if (!voiceRowUpFrom(session, "pass.")) {
                return false;
            }
            const VoiceLine& line = session.voice().current();
            std::int32_t sx = 0;
            std::int32_t sy = 0;
            std::int32_t sband = 0;
            if (!speakerDrawnQ8(session, sx, sy, sband) || sband != session.body().band() ||
                !cameraSightClear(session.tiles(), session.body().x(), session.body().y(), sx, sy,
                                  sband, false) ||
                bodyBetween(session, sx, sy, sband, line.actorId)) {
                return false;
            }
            if (anybody || line.actorId < sim::kWardSpeakerIdBase) {
                return true;
            }
            const sim::WardActor* who = session.people().byId(line.actorId - sim::kWardSpeakerIdBase);
            return who != nullptr && wardWalking(*who);
        };
        for (int i = 0; i < kVoiceWaitSteps; ++i) {
            session.step(sim::MoveInput{});
            if (passingSpoke(i > kVoiceWaitSteps / 2)) {
                break;
            }
        }
        // 1. A body within reach: the row names a speaker, in reach by the
        //    emitter's own rule.
        if (!session.voice().current().name.empty()) {
            ++landed;
        }
        // 2. And the row is up off a pass key. The shot is composed on the
        //    speaker: the look turns to whoever spoke -- to their face --
        //    and again after the row has settled, for the ground a
        //    passer-by covers meanwhile.
        faceTheSpeaker(session);
        session.stepMany(sim::MoveInput{}, kPageEaseSteps + 1);
        faceTheSpeaker(session);
        if (voiceRowUpFrom(session, "pass.")) {
            ++landed;
        }
        return landed;
    }
    if (ending == "overheard") {
        // THE CROWD, STOOD IN, BESIDE TWO PEOPLE TALKING. Not a blind walk
        // any more (findOverheardStand says why): the crowd runs two seconds
        // first, runStreetLine's own reason -- a session's bodies have not
        // been ticked when the drive starts and every one of them reads as
        // standing -- and then the drive finds a pair the channel will hear,
        // stands where it hears them and listens. THE ONE PLACEMENT, the
        // `--street` kind, said out loud: what the row says after it is the
        // channel's own. A pair that breaks up before it speaks gets the
        // search again, inside the same minute and a half the walk had.
        gWatchHaltNote = " stand=none";
        constexpr int kOverheardListenSteps = 3 * sim::kStepsPerSecond;
        bool heard = false;
        int spent = 0;
        const auto listen = [&](int steps) {
            for (int i = 0; i < steps && !heard && spent < kVoiceWaitSteps; ++i, ++spent) {
                session.step(sim::MoveInput{});
                heard = voiceRowUpFrom(session, "overheard");
            }
        };
        listen(2 * sim::kStepsPerSecond);
        while (!heard && spent < kVoiceWaitSteps) {
            OverheardStand stand;
            if (!findOverheardStand(session, stand)) {
                listen(sim::kStepsPerSecond);  // the crowd shifts; ask again a second on
                continue;
            }
            session.placeBodyAt(stand.x, stand.y, stand.band);
            gWatchHaltNote = " stand=(" + std::to_string(stand.x) + "," + std::to_string(stand.y) +
                             ",z" + std::to_string(stand.band) + ") pair=" +
                             std::to_string(stand.speakerId - sim::kWardSpeakerIdBase) + "+" +
                             std::to_string(stand.partnerId - sim::kWardSpeakerIdBase);
            listen(kOverheardListenSteps);
        }
        if (!session.voice().current().name.empty()) {
            ++landed;  // 1. a pair stood together within earshot
        }
        // The shot is of the PAIR -- both halves of the exchange, and a
        // short walk to a vantage when one stands behind the other from here.
        faceThePair(session, true);
        session.stepMany(sim::MoveInput{}, kPageEaseSteps + 1);
        faceThePair(session, false);
        if (heard && voiceRowUpFrom(session, "overheard")) {
            ++landed;  // 2. the row is up off an overheard key
        }
        return landed;
    }
    if (ending == "farewell" || ending == "moved") {
        // THE STREET LINE FIRST: the talk key on a rope hand in the open
        // district, the same placement `--street` makes and says so.
        const StreetLineResult street = runStreetLine(session, "hand", 0);
        if (!street.opened) {
            return landed;
        }
        ++landed;  // 1. the talk opened
        if (ending == "farewell") {
            // LEAVE, BY ITS OWN TOPIC. The reply carries the goodbye and the
            // panel closes under it.
            const int leave = topicOfKind(session, sim::TopicKind::Leave);
            if (leave < 0) {
                return landed;
            }
            session.chooseTopic(static_cast<std::size_t>(leave));
            if (!session.talking()) {
                ++landed;  // 2. LEAVE closed it
            }
            // The panel's own close tail and the row's rise, both settled.
            session.stepMany(sim::MoveInput{}, kPageEaseSteps * 2 + 2);
            if (voiceRowUpFrom(session, "farewell")) {
                ++landed;  // 3. the goodbye is on the row
            }
            // SHE IS ALREADY WALKING OFF. LEAVE ends the talk, and the
            // WardActor hold is deferred (the README's "The address": only
            // the Gull's peaceable activities hold today), so she is free
            // to move the instant the panel closes. runStreetLine's own aim
            // pointed at where she WAS when the body was placed; eighteen
            // steps on, on a real street, she can be well off it, and the
            // fixed shot is left pointed at whatever the street put in that
            // direction meanwhile -- a passer-by, not her. Every other
            // ending re-aims at its speaker right before the shutter
            // (faceTheSpeaker, standBackFrom); this one hadn't.
            if (const sim::WardActor* who = session.people().byId(street.actorId);
                who != nullptr && who->visible()) {
                const std::pair<std::int32_t, std::int32_t> at = wardDrawnQ8(session, *who);
                lookAtFace(session, at.first, at.second, who->type,
                           composeYawOffset(session, at.first, at.second, who->band,
                                            who->id + sim::kWardSpeakerIdBase, 0.0));
            }
            return landed;
        }
        // MOVED: the first topic that is HEARD OUT -- the vanished clerk,
        // which everybody has heard of, or their own business -- records
        // Listened and moves them a hair; the worded reaction stands on the
        // dateline row.
        int heard = topicOfKind(session, sim::TopicKind::Quest);
        if (heard < 0) {
            heard = topicOfKind(session, sim::TopicKind::Personal);
        }
        if (heard < 0) {
            return landed;
        }
        session.chooseTopic(static_cast<std::size_t>(heard));
        if (!session.reactionLine().empty()) {
            ++landed;  // 2. the reply moved them and the reaction is worded
        }
        // The reply's own typewriter reveal, run out, so the frame reads it.
        session.stepMany(sim::MoveInput{}, sim::kStepsPerSecond / 2);
        if (session.talking() && !session.reactionLine().empty()) {
            ++landed;  // 3. still talking, the reaction still standing
        }
        // Same re-aim as farewell's, and the same reason: the WardActor
        // hold is deferred, so even mid-talk she can have drifted off the
        // placement's original aim by the time the reveal's half-second
        // has run.
        if (const sim::WardActor* who = session.people().byId(street.actorId);
            who != nullptr && who->visible()) {
            const std::pair<std::int32_t, std::int32_t> at = wardDrawnQ8(session, *who);
            lookAtFace(session, at.first, at.second, who->type,
                       composeYawOffset(session, at.first, at.second, who->band,
                                        who->id + sim::kWardSpeakerIdBase, 0.0));
        }
        return landed;
    }
    if (ending == "react") {
        // INTO THE GULL AND A TAP ON THE NEAREST PATRON, exactly the beat
        // `--punch` throws (walk up, face him, tap until it connects), and
        // then the room says what it saw.
        walkToTile(session, sim::gull::kBartenderX, sim::gull::kBartenderY + 1);
        session.closeConversation();
        const sim::Actor* mark = nearestMark(session);
        if (mark == nullptr) {
            return landed;
        }
        const std::int32_t markId = mark->id();
        if (tapUntilItConnects(session, markId, 16)) {
            ++landed;  // 1. the tap landed
        }
        // 2. A witness's memory carries the deed: the sim's own three-clause
        //    rule put Struck on somebody who is not the man hit.
        const sim::SocialLedger& ledger = session.tavern().dialogue().ledger();
        bool witnessed = false;
        for (const sim::Memory& memory : ledger.memories()) {
            if (memory.actorId != markId && memory.lastDeed == sim::Deed::Struck) {
                witnessed = true;
                break;
            }
        }
        if (witnessed) {
            ++landed;
        }
        if (waitForVoice(session, "react.", sim::MoveInput{}, 6 * sim::kStepsPerSecond)) {
            ++landed;  // 3. the row is up off a react key
            // The shot turns to the witness who spoke, not the man hit --
            // and waits a second and a half for the room to sort itself out
            // before the shutter. The first draft fired on the row and
            // caught a bouncer crossing the lens half in frame, his head
            // wider at that range than the witness's; the same beat ninety
            // steps on is the same line with the doorway clear, and the row
            // holds four seconds at the least.
            faceTheSpeaker(session);
            session.stepMany(sim::MoveInput{}, 90);
            faceTheSpeaker(session);
        }
        return landed;
    }
    if (ending == "hold") {
        // THE ADDRESS, PHOTOGRAPHED: into the Gull at eight, the talk key on
        // a drinking patron, ten seconds of the room running, then three
        // tiles back for the shot with the talk still open. He is where the
        // talk found him, facing you, drawn standing.
        walkToTile(session, sim::gull::kBartenderX, sim::gull::kBartenderY + 1);
        session.closeConversation();
        std::string name;
        for (const sim::Actor& actor : session.tavern().actors()) {
            if (actor.present() && actor.role() == sim::ActorRole::Patron &&
                actor.activity() == sim::Activity::Drinking &&
                !session.tavern().isProfessional(actor)) {
                name = actor.name();
                break;
            }
        }
        if (name.empty() || !speakTo(session, name)) {
            return landed;
        }
        ++landed;  // 1. the talk opened with him
        const sim::Actor* held = actorNamed(session, name);
        if (held == nullptr) {
            return landed;
        }
        const std::int32_t heldId = held->id();
        const std::int32_t x = held->x();
        const std::int32_t y = held->y();
        session.stepMany(sim::MoveInput{}, 10 * sim::kStepsPerSecond);
        held = session.tavern().actorById(heldId);
        if (session.talking() && held != nullptr && held->x() == x && held->y() == y &&
            session.tavern().holdsForTalk(*held)) {
            ++landed;  // 2. ten seconds on, still talking, still where the talk found him
        }
        standBackFrom(session, name);
        return landed;
    }
    if (ending == "watch") {
        // STEEL ACROSS THE ROOM FROM CULL, his halt, and the readback as he
        // crosses it to you -- see runWatchRevealLine.
        return runWatchRevealLine(session);
    }
    return landed;
}

}  // namespace scripted

}  // namespace granadad::render
