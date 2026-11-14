// Scripted capture drivers: street.
#include "scripted.hpp"

namespace granadad::render {

namespace {

// ---------------------------------------------------------------------------
// #79 -- the street
// ---------------------------------------------------------------------------
/// The trades a `--street` run knows how to ask for. Named the way a person
/// would say them out loud, not the way the enum spells them: "watch", not
/// "MilitiaWatch", and "hand" for the six hundred rope-and-crate bodies the
/// district actually runs on.
struct StreetTrade {
    const char* word;
    sim::WardType type;
};

constexpr StreetTrade kStreetTrades[] = {
    {"hand", sim::WardType::Serf},        {"watch", sim::WardType::MilitiaWatch},
    {"priest", sim::WardType::PriestOfTheFlame},
    {"disciple", sim::WardType::DiscipleOfTheFlame},
    {"keeper", sim::WardType::Shopkeeper}, {"fisher", sim::WardType::Fisher},
    {"sailor", sim::WardType::Sailor},    {"carter", sim::WardType::Carter},
    {"wastrel", sim::WardType::Wastrel},  {"urchin", sim::WardType::Urchin},
    {"thief", sim::WardType::Thief},      {"drover", sim::WardType::AnimalKeeper},
    {"cat", sim::WardType::Cat},          {"dog", sim::WardType::Dog},
};

}  // namespace

StreetLineResult runStreetLine(Session& session, const std::string& who, int topic) {
    StreetLineResult out;

    sim::WardType wanted = sim::WardType::Serf;
    for (const StreetTrade& trade : kStreetTrades) {
        if (who == trade.word) {
            wanted = trade.type;
        }
    }

    // THE CROWD SHOWS ITS HAND FIRST. A session's bodies have not been
    // ticked when the drive starts: every one of them reads as standing
    // (last tile is this tile, no leg drawn) and the composed passes below
    // would choose a stand in a stream of dockers about to walk through it.
    // Two seconds of the world running -- two population ticks -- and the
    // walkers are walking, their legs drawn, before anybody is asked. And
    // asked again a second later, up to ten times, when the whole search
    // comes up empty: a crowd shifts (the Mission's faithful close round
    // Father Maell at one and open again), and a trade with nowhere to
    // stand THIS second has somewhere the next.
    constexpr int kStandAttempts = 10;

    // The first body of that trade you can stand beside AND BE TALKING TO.
    //
    // The second half of that is the whole of the search, and the first attempt
    // did not have it: it took the first free tile next to the first body of
    // the trade, and at a muster where fourteen spare hands share one commons
    // the key reached whichever of them the reach rule picked -- a body of the
    // right trade, chosen by nobody, and sometimes a different one. So the
    // candidate is confirmed against the SAME question Session::interact asks
    // before the body is moved: "who would answer from here". If the answer is
    // not this person, this is not the tile.
    //
    // Clear of the Gilded Gull as well, because the taproom's own roster is
    // asked first and reaches two tiles: a capture that stood outside K03's
    // door would photograph a bouncer and call it the ward.
    const sim::WardActor* target = nullptr;
    std::int32_t standX = 0;
    std::int32_t standY = 0;
    // THE STAND: any standable, empty tile within the talk reach of the
    // body (kStreetReachTiles, Chebyshev, the key's own square) from which
    // the key reaches THAT body and the lens has a clear line to it
    // (cameraSightClear) -- not only the four tiles beside it. The four were
    // the first draft, and beside a body is where the lens is in their hair:
    // the eye stands a tile and three quarters up and a person's crown is a
    // tile and a half, so at one tile the crown is a hand under the horizon
    // and the rest of them is under the frame. At two the frame is a head
    // and shoulders. The farther stand wins, then the lower y, then x, so
    // two runs stand in the same place.
    //
    // FOUR PASSES, composed to bare. The first asks that the body is
    // STANDING this second (drawn at its centre; a body sliding out of the
    // very tile the camera takes is drawn through the lens, which is what
    // the rope hand's frames were), that nobody else is DRAWN within two
    // tiles of the stand, their tile or the one they are sliding from
    // (bodiesDrawnWithin), and that the stand is under the open sky -- a
    // street line on the street, not on a bunkhouse floor. A framing
    // complaint, not a population one, so it costs the crowd nothing and
    // only changes WHICH tile the camera takes. The second lets the roof
    // go, the third relaxes the margin to one tile, and the fourth is the
    // bare question alone, so a trade with nowhere composed to stand still
    // gets its line rather than none.
    for (int attempt = 0; attempt < kStandAttempts && target == nullptr; ++attempt) {
        session.stepMany(sim::MoveInput{}, (attempt == 0 ? 2 : 1) * sim::kStepsPerSecond);
        for (int pass = 0; pass < 4 && target == nullptr; ++pass) {
            const std::int32_t margin = pass <= 1 ? 2 : (pass == 2 ? 1 : 0);
            const bool wantsSky = pass == 0;
            for (const sim::WardActor& actor : session.people().actors()) {
                if (!actor.visible() || actor.type != wanted) {
                    continue;
                }
                if (actor.x >= sim::gull::kFootprintX0 - 4 && actor.x <= sim::gull::kFootprintX1 + 4 &&
                    actor.y >= sim::gull::kFootprintY0 - 6 && actor.y <= sim::gull::kFootprintY1 + 4) {
                    continue;
                }
                if (pass < 3 && !wardStandsStill(actor)) {
                    continue;
                }
                std::int32_t bestApart = -1;
                for (std::int32_t sy = actor.y - kStreetReachTiles; sy <= actor.y + kStreetReachTiles; ++sy) {
                    for (std::int32_t sx = actor.x - kStreetReachTiles; sx <= actor.x + kStreetReachTiles;
                         ++sx) {
                        if ((sx == actor.x && sy == actor.y) ||
                            !session.tiles().standable(sx, sy, actor.band)) {
                            continue;
                        }
                        if (session.people().nearestTo(sx, sy, actor.band, 0) != nullptr) {
                            continue;  // somebody is standing there
                        }
                        if (wantsSky && !underOpenSky(session.tiles(), sx, sy, actor.band)) {
                            continue;
                        }
                        if (margin > 0 && bodiesDrawnWithin(session, sx, sy, actor.band, margin, actor.id)) {
                            continue;
                        }
                        const sim::WardActor* answers =
                            session.people().nearestTo(sx, sy, actor.band, kStreetReachTiles);
                        if (answers == nullptr || answers->id != actor.id) {
                            continue;
                        }
                        // And the talk key's own rule, from the facing the
                        // placement below will turn to (see addresseeFrom).
                        const sim::WardActor* addressed = addresseeFrom(
                            session, sx, sy, actor.band, cardinalToward(actor.x - sx, actor.y - sy));
                        if (addressed == nullptr || addressed->id != actor.id) {
                            continue;
                        }
                        if (!cameraSightClear(session.tiles(), sim::q8_tile_centre(sx),
                                              sim::q8_tile_centre(sy), sim::q8_tile_centre(actor.x),
                                              sim::q8_tile_centre(actor.y), actor.band)) {
                            continue;
                        }
                        const std::int32_t apart =
                            (sx - actor.x) * (sx - actor.x) + (sy - actor.y) * (sy - actor.y);
                        if (apart > bestApart) {
                            bestApart = apart;
                            target = &actor;
                            standX = sx;
                            standY = sy;
                        }
                    }
                }
                if (target != nullptr) {
                    break;
                }
            }
        }
    }
    if (target == nullptr) {
        return out;
    }
    out.found = true;
    out.actorId = target->id;

    // THE ONE PLACEMENT, and see SmokeRunConfig::street on why it is a placement
    // and not a walk. Everything after this line is the game.
    session.placeBodyAt(standX, standY, target->band);
    // Facing them, so the frame is a picture of a conversation: the bearing
    // to where they are drawn (bearingToward, the scripted drives' own
    // render-side atan2 over the body's facing), a stand on the diagonal
    // included.
    // The step lookAtFace takes is the one movement step the room needs, so
    // its own idea of where the player is standing catches up with the body
    // before the key is pressed.
    lookAtFace(session, sim::q8_tile_centre(target->x), sim::q8_tile_centre(target->y),
               target->type, 0);
    session.interact();
    out.opened = session.talking() && session.wardTalkingTo() == out.actorId;
    if (!out.opened) {
        return out;
    }
    if (topic > 0) {
        session.chooseTopic(static_cast<std::size_t>(topic - 1));
    }
    const sim::DialogueDirector& talk = session.tavern().dialogue();
    out.name = talk.speaker().name;
    out.barkKey = talk.greetingKey();
    out.line = talk.lastLine();
    return out;
}

/// STREET SENSES leg (c), played through the real verbs and no other door: a
/// watchman on the beats sees cause on his own two feet -- `alarm()` fires
/// from EXACTLY the places it already fires for legs (a)/(b)
/// (`WardActor::applyStreetBlow`'s own tail for a landed blow, the periodic
/// once-a-second check in `Session::step` for a raised blade) -- and
/// `Session::step`'s leg (c) block turns his `WatchEvent`s into the alert
/// row and the arrest. Nothing here calls `WardPopulation::alarm` or
/// `takeWatchEvents` by hand.
///
/// "watch-halt": a fist lands on a docker within a nearby watchman's sight,
/// and the frame stops the moment he starts Closing -- HALT in his own name
/// on the row. "watch-sheathe": steel goes up within a watchman's sight
/// instead, and the frame stops on his SHEATHE IT demand. "watch-offence":
/// the same demand held past its six-second grace, so the frame stops on the
/// row that follows it -- heat, and no arrest by itself.
StreetLineResult runStreetWatchEnding(Session& session, const std::string& where) {
    StreetLineResult out;
    const bool steel = where == "watch-sheathe" || where == "watch-offence";

    // THE WATCHMAN AND HIS WITNESS: the first MilitiaWatch on the roster with
    // a non-Watch body within kWatchSightTiles and a line to it, and a free
    // standable tile beside that body for the player. Ascending roster order,
    // deterministic and draw-free -- runStreetAssault's own shape, asked of a
    // pair instead of a crowd.
    static constexpr std::int32_t dx[4] = {1, -1, 0, 0};
    static constexpr std::int32_t dy[4] = {0, 0, 1, -1};
    const sim::WardActor* cop = nullptr;
    const sim::WardActor* victim = nullptr;
    std::int32_t standX = 0;
    std::int32_t standY = 0;
    std::int32_t standBand = 0;
    for (const sim::WardActor& watch : session.people().actors()) {
        if (!watch.visible() || watch.type != sim::WardType::MilitiaWatch) {
            continue;
        }
        for (const sim::WardActor& body : session.people().actors()) {
            if (!body.visible() || !sim::isPerson(body.type) ||
                body.type == sim::WardType::MilitiaWatch || body.band != watch.band) {
                continue;
            }
            if (std::max(std::abs(watch.x - body.x), std::abs(watch.y - body.y)) >
                sim::kWatchSightTiles) {
                continue;
            }
            if ((watch.x != body.x || watch.y != body.y) &&
                !session.tiles().lineOfSight(watch.x, watch.y, body.x, body.y, watch.band)) {
                continue;
            }
            bool stood = false;
            for (int n = 0; n < 4 && !stood; ++n) {
                const std::int32_t sx = body.x + dx[n];
                const std::int32_t sy = body.y + dy[n];
                if (!session.tiles().standable(sx, sy, body.band) ||
                    session.people().nearestTo(sx, sy, body.band, 0) != nullptr) {
                    continue;
                }
                // THE WATCHMAN MUST NOT BE ON THE RAY HIMSELF: within his own
                // sight of the victim is fine (that is the whole point), but
                // standing within melee reach of the STAND TILE would make
                // him, not the victim, the nearer body along it -- a fist
                // meant for the victim landing on the watchman instead
                // (wardTypeFightsBack now includes him, so it would still
                // "connect", just never on the tracked id). Two tiles clears
                // sightlineTarget's own reach with room to spare.
                if (std::max(std::abs(watch.x - sx), std::abs(watch.y - sy)) <= 2) {
                    continue;
                }
                standX = sx;
                standY = sy;
                standBand = body.band;
                stood = true;
            }
            if (!stood) {
                continue;
            }
            cop = &watch;
            victim = &body;
            break;
        }
        if (cop != nullptr) {
            break;
        }
    }
    if (cop == nullptr || victim == nullptr) {
        return out;
    }
    out.found = true;
    out.actorId = cop->id;
    const std::int32_t copId = cop->id;
    const std::int32_t victimId = victim->id;

    // THE ONE PLACEMENT, runStreetAssault's own reasoning: a tile off the
    // victim, facing in.
    session.placeBodyAt(standX, standY, standBand);
    {
        const std::int32_t toX = victim->x - standX;
        const std::int32_t toY = victim->y - standY;
        sim::Angle look = sim::kFacingNorth;
        if (std::abs(toX) >= std::abs(toY)) {
            look = toX > 0 ? sim::kFacingEast : sim::kFacingWest;
        } else {
            look = toY > 0 ? sim::kFacingSouth : sim::kFacingNorth;
        }
        session.body().setYaw(look);
    }

    if (steel) {
        // STEEL UP, NO BLOW: an Edged weapon and the hands raised, the exact
        // stance Session::step's own once-a-second check reads as Steel.
        session.tavern().setPlayerCombat(sim::Weapon::Edged, sim::Intent::Subdue);
        session.setBlocking(true);
        session.stepMany(sim::MoveInput{}, 1);
    } else {
        // ONE FIST, LANDED: the leg (b) swing over the street roster,
        // unchanged -- applyStreetBlow's own tail is what alarms him.
        // runStreetAssault's own swingAt shape: re-stand and re-face each tap
        // since a body not yet struck may still have drifted a tile between
        // them (its own ordinary loiter, not a reaction to anything here).
        session.tavern().setPlayerCombat(sim::Weapon::Fists, sim::Intent::Subdue);
        session.stepMany(sim::MoveInput{}, 1);
        bool landed = false;
        for (int tap = 0; tap < 6 && !landed; ++tap) {
            const sim::WardActor* mark = session.people().byId(victimId);
            if (mark == nullptr || !mark->visible()) {
                break;
            }
            std::int32_t sx = mark->x;
            std::int32_t sy = mark->y;
            bool stoodHere = false;
            for (int n = 0; n < 4 && !stoodHere; ++n) {
                sx = mark->x + dx[n];
                sy = mark->y + dy[n];
                if (session.tiles().standable(sx, sy, mark->band) &&
                    session.people().nearestTo(sx, sy, mark->band, 0) == nullptr) {
                    stoodHere = true;
                }
            }
            if (!stoodHere) {
                break;
            }
            const std::int32_t hpBefore = mark->hp;
            session.placeBodyAt(sx, sy, mark->band);
            {
                const std::int32_t toX = mark->x - sx;
                const std::int32_t toY = mark->y - sy;
                sim::Angle look = sim::kFacingNorth;
                if (std::abs(toX) >= std::abs(toY)) {
                    look = toX > 0 ? sim::kFacingEast : sim::kFacingWest;
                } else {
                    look = toY > 0 ? sim::kFacingSouth : sim::kFacingNorth;
                }
                session.body().setYaw(look);
            }
            session.stepMany(sim::MoveInput{}, 1);
            session.attackDown();
            session.stepMany(sim::MoveInput{}, 1);
            session.attackUp();
            const sim::WardActor* after = session.people().byId(victimId);
            landed = after != nullptr && (after->hp < hpBefore || after->slain);
            session.stepMany(sim::MoveInput{}, sim::kSwingRecoverySteps + 1);
        }
        if (!landed) {
            out.line = "no-blow-landed";
            return out;
        }
    }

    // THE CLOSING: real steps only, waiting for Session::step's own leg (c)
    // block to notice -- never a hand-fed alarm() or takeWatchEvents().
    bool closing = false;
    for (int second = 0; second < 20 && !closing; ++second) {
        session.stepMany(sim::MoveInput{}, sim::kStepsPerSecond);
        closing = session.people().watchmanClosing(copId);
    }
    out.opened = closing;
    out.line = session.lastMessage();
    if (where != "watch-offence" || !closing) {
        return out;
    }

    // "watch-offence": hold the demand past its six-second grace -- the row
    // that follows it, said once.
    for (std::int64_t second = 0; second < sim::kSheatheGraceSeconds + 3 && closing; ++second) {
        session.stepMany(sim::MoveInput{}, sim::kStepsPerSecond);
        closing = session.people().watchmanClosing(copId);
    }
    out.line = session.lastMessage();
    out.opened = closing;
    return out;
}

StreetLineResult runStreetAssault(Session& session, const std::string& where) {
    StreetLineResult out;
    if (where == "watch-halt" || where == "watch-sheathe" || where == "watch-offence") {
        return runStreetWatchEnding(session, where);
    }
    // THE ENDINGS. Empty (or "steel"): the blade up, no blow -- leg (a)'s
    // panic. "blow": fists, one tap that lands on a docker. "down": taps until
    // he is on the floor. "up": the same, then his six seconds, so he is on
    // his feet again, bloodied and running. "kill": steel meaning it, hard
    // swings until he is a corpse -- WANTED FOR BLOOD on the row. "hearing":
    // the killing, then the Gull at the hour the Watch drinks, taken at reach
    // with steel up, and the rope hearing page reading the street's own
    // count. Every blow is the Attack key's own down and up.
    const bool kills = where == "kill" || where == "hearing";
    const bool blows = where == "blow" || where == "down" || where == "up" || kills;

    // THE FULLEST STRETCH OF THE TARWALK the hour offers: the visible non-Watch
    // person with the most people in SIGHT within the blade's radius, and a
    // standable tile beside them to stand the player on -- the same
    // (same band, in range, line of sight) rule the alarm keeps, computed here
    // so the frame is a picture of a crowd rather than of the spawn tile.
    // Deterministic (ascending id, first standable neighbour), draw-free. A
    // blow ending stands beside a SERF: he routs rather than swings back, so
    // the floor and the stand-up are the frame and not a fist fight.
    const sim::WardActor* best = nullptr;
    std::int32_t bestSeen = -1;
    std::int32_t standX = 0;
    std::int32_t standY = 0;
    std::int32_t standBand = 0;
    static constexpr std::int32_t dx[4] = {1, -1, 0, 0};
    static constexpr std::int32_t dy[4] = {0, 0, 1, -1};
    for (const sim::WardActor& centre : session.people().actors()) {
        if (!centre.visible() || !sim::isPerson(centre.type) ||
            centre.type == sim::WardType::MilitiaWatch) {
            continue;
        }
        if (blows && centre.type != sim::WardType::Serf) {
            continue;
        }
        if (centre.x < sim::wardplaces::kTarwalkX0 || centre.x > sim::wardplaces::kTarwalkX1 ||
            centre.y < sim::wardplaces::kTarwalkY0 || centre.y > sim::wardplaces::kTarwalkY1) {
            continue;
        }
        for (int n = 0; n < 4; ++n) {
            const std::int32_t sx = centre.x + dx[n];
            const std::int32_t sy = centre.y + dy[n];
            if (!session.tiles().standable(sx, sy, centre.band)) {
                continue;
            }
            std::int32_t seen = 0;
            for (const sim::WardActor& other : session.people().actors()) {
                if (other.id == centre.id || !other.visible() || !sim::isPerson(other.type) ||
                    other.band != centre.band) {
                    continue;
                }
                if (std::max(std::abs(other.x - sx), std::abs(other.y - sy)) >
                    sim::kAlarmRadiusSteel) {
                    continue;
                }
                if ((other.x != sx || other.y != sy) &&
                    !session.tiles().lineOfSight(other.x, other.y, sx, sy, centre.band)) {
                    continue;
                }
                ++seen;
            }
            if (seen > bestSeen) {
                bestSeen = seen;
                best = &centre;
                standX = sx;
                standY = sy;
                standBand = centre.band;
            }
            break;  // the first standable neighbour is the stand tile
        }
    }
    if (best == nullptr) {
        return out;
    }
    out.found = true;
    out.actorId = best->id;

    // THE ONE PLACEMENT (runStreetLine's own move and its reasons): stand a
    // tile off the crowd, facing in, so the shutter looks at the street.
    session.placeBodyAt(standX, standY, standBand);
    {
        const std::int32_t toX = best->x - standX;
        const std::int32_t toY = best->y - standY;
        sim::Angle look = sim::kFacingNorth;
        if (std::abs(toX) >= std::abs(toY)) {
            look = toX > 0 ? sim::kFacingEast : sim::kFacingWest;
        } else {
            look = toY > 0 ? sim::kFacingSouth : sim::kFacingNorth;
        }
        session.body().setYaw(look);
    }

    if (!blows) {
        // STEEL UP, NO BLOW. An Edged weapon in the hand and the hands raised
        // (a held guard raises them without violence, the stance's own rule)
        // -- the exact cause Tavern::violenceInView reads, and the exact
        // shape test_street_panic's "steel up on the Tarwalk" drives. The
        // alarm that scatters the crowd fires from Session::step's one call
        // site while the settle window runs; nothing here alarms anybody by
        // hand.
        session.tavern().setPlayerCombat(sim::Weapon::Edged, sim::Intent::Subdue);
        session.setBlocking(true);
        // One step so the room learns the new position and the hands come up
        // before the settle window's ticks carry the first alarm.
        session.stepMany(sim::MoveInput{}, 1);
        return out;
    }

    // STREET SENSES leg (b): THE BLOW. Fists for a docker beaten (a brawl,
    // nobody dies); steel meaning it for a killing (Lethal by B1 and B2).
    session.tavern().setPlayerCombat(kills ? sim::Weapon::Edged : sim::Weapon::Fists,
                                     kills ? sim::Intent::Kill : sim::Intent::Subdue);
    session.stepMany(sim::MoveInput{}, 1);
    const std::int32_t markId = best->id;

    // ONE SWING AT HIM -- re-stood beside him and re-faced first (a struck man
    // moves), then the Attack key DOWN, held past the hard threshold for a
    // killing and not for a beating, and UP: the sightline picks him (the
    // street's ray, the nearer along), the room resolves it, the street takes
    // the answer. Returns whether the blow LANDED (his sheet moved).
    const auto swingAt = [&](bool hard) {
        const sim::WardActor* mark = session.people().byId(markId);
        if (mark == nullptr || !mark->visible()) {
            return false;
        }
        std::int32_t sx = mark->x;
        std::int32_t sy = mark->y;
        bool stood = false;
        for (int n = 0; n < 4 && !stood; ++n) {
            sx = mark->x + dx[n];
            sy = mark->y + dy[n];
            if (session.tiles().standable(sx, sy, mark->band) &&
                session.people().nearestTo(sx, sy, mark->band, 0) == nullptr) {
                stood = true;
            }
        }
        if (!stood) {
            return false;
        }
        const std::int32_t hpBefore = mark->hp;
        session.placeBodyAt(sx, sy, mark->band);
        {
            const std::int32_t toX = mark->x - sx;
            const std::int32_t toY = mark->y - sy;
            sim::Angle look = sim::kFacingNorth;
            if (std::abs(toX) >= std::abs(toY)) {
                look = toX > 0 ? sim::kFacingEast : sim::kFacingWest;
            } else {
                look = toY > 0 ? sim::kFacingSouth : sim::kFacingNorth;
            }
            session.body().setYaw(look);
        }
        session.stepMany(sim::MoveInput{}, 1);
        session.attackDown();
        session.stepMany(sim::MoveInput{}, hard ? sim::kHardSwingHoldSteps + 1 : 1);
        session.attackUp();
        const sim::WardActor* after = session.people().byId(markId);
        const bool landed = after != nullptr && (after->hp < hpBefore || after->slain);
        // The recovery lockout, through real steps, so the next press is not
        // dropped.
        session.stepMany(sim::MoveInput{},
                         (hard ? sim::kHardSwingRecoverySteps : sim::kSwingRecoverySteps) + 1);
        return landed;
    };

    if (where == "blow") {
        // The blow LANDING: the first tap that connects (the die whiffs one in
        // eight), and the frame right after it -- the wash, the crowd turning.
        bool landed = false;
        for (int tap = 0; tap < 6 && !landed; ++tap) {
            landed = swingAt(false);
        }
        out.opened = landed;
        return out;
    }
    if (where == "down" || where == "up") {
        // Taps until he drops: 3-5 a tap against 24, the brawl floor, off the
        // board where he fell.
        bool floored = false;
        for (int tap = 0; tap < 16 && !floored; ++tap) {
            (void)swingAt(false);
            const sim::WardActor* mark = session.people().byId(markId);
            floored = mark != nullptr && mark->downedUntil >= 0;
        }
        if (where == "up" && floored) {
            // HIS SIX SECONDS, and up at a quarter -- on his feet, bloodied,
            // and running (struck, he routs). Two extra ticks so the stand
            // and the first flee step are both on the frame.
            session.stepMany(sim::MoveInput{},
                             static_cast<int>(sim::kStreetFloorSeconds + 2) * sim::kStepsPerSecond);
            const sim::WardActor* mark = session.people().byId(markId);
            out.opened = mark != nullptr && mark->visible() && mark->hp == sim::kStreetStandHp;
            return out;
        }
        out.opened = floored;
        return out;
    }
    // A KILLING: hard swings with steel until he is a corpse.
    bool slain = false;
    for (int swing = 0; swing < 8 && !slain; ++swing) {
        (void)swingAt(true);
        const sim::WardActor* mark = session.people().byId(markId);
        slain = mark != nullptr && mark->slain;
    }
    if (!slain) {
        return out;
    }
    if (where == "kill") {
        // The corpse on the quay and WANTED FOR BLOOD on the row, the row's
        // own ease fully up.
        session.stepMany(sim::MoveInput{}, 16);
        out.opened = session.tavern().dialogue().crimes().murderer();
        return out;
    }
    // THE HEARING. The body to the Gull's own street tile (the one placement,
    // said out loud: the court line's router is that building and the street
    // in front of it, and the Tarwalk's far end is not), then the court line
    // for a killing already on the record: the wait to the hour the Watch
    // drinks, taken at reach with steel up, the rope hearing page.
    session.placeBodyAt(sim::gull::kStreetX, sim::gull::kStreetY, sim::gull::kGroundBand);
    session.stepMany(sim::MoveInput{}, 1);
    const int beats = runCourtLine(session, "streetblood");
    out.opened = beats >= kCourtRopePageBeats;
    out.line = gCourtNote;
    return out;
}

}  // namespace granadad::render
