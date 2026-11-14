// Scripted capture drivers: trail.
#include "scripted.hpp"

namespace granadad::render {

namespace {

/// How many leads the walked trail READ. Not a fixed beat count: the trail is
/// authored data and the number of leads is a property of casebook.json, so a
/// hard-coded target here would be a second source of truth for the same thing.
std::int32_t gTrailRead = 0;

}  // namespace

namespace scripted {

/// WALKS THE BLOODLETTER TRAIL, through the same two calls a keyboard makes.
///
/// Every beat is: find the nearest lead the casebook currently holds OPEN on
/// this band, walk to it with the district's own breadth-first router, and
/// press Q. Nothing here reads a clue it has not walked to and nothing here
/// opens a lead the simulation did not open.
///
/// It stops when there is nothing open left that it can reach, which is an
/// honest end condition: the two leads on the strand plane (z10) are one band
/// down and this line does not climb, so it leaves them in the book and says so
/// in the summary rather than pretending the case is finished.
[[nodiscard]] int runTrailLine(Session& session, const std::string& ending) {
    gTrailRead = 0;
    gTrailWalked = 0;
    gTrailUnreached = 0;
    const sim::CasebookRaws* raws = session.casebook().raws();
    if (raws == nullptr) {
        return 0;
    }
    std::int32_t last = -1;
    // `start` walks nowhere: it is the FIRST FRAME of a new game, which is the
    // one state a capture could not otherwise reach -- SessionConfig's opening
    // page is set by the client and not by the smoke path, so a scripted run
    // has to ask for it. Nothing else about the session differs.
    const int rounds = ending == "start" ? 0 : 32;
    for (int guard = 0; guard < rounds; ++guard) {
        // The nearest OPEN lead on this band. Nearest, because that is what a
        // player does, and because it makes the walk short enough to watch.
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
        // Stand a tile short of the anchor: the anchors are counters, flagstones
        // and sagging floors, and several of them are solid cells by design.
        // kLookRangeTiles is four, so anywhere in the room will do.
        bool got = walkAcrossDistrict(session, lead.site.x, lead.site.y);
        if (!got) {
            for (const std::int32_t offset : {1, -1, 2, -2}) {
                got = walkAcrossDistrict(session, lead.site.x + offset, lead.site.y);
                if (got) {
                    break;
                }
                got = walkAcrossDistrict(session, lead.site.x, lead.site.y + offset);
                if (got) {
                    break;
                }
            }
        }
        ++gTrailWalked;
        const std::int32_t before = session.casebook().readCount();
        session.examine();
        if (session.casebook().readCount() > before) {
            ++gTrailRead;
            last = best;
        } else {
            // Could not get near enough. Take it out of the running so the loop
            // cannot spin on it, and count it -- a lead the walk could not reach
            // is the single most useful thing this run can report.
            ++gTrailUnreached;
            (void)session.casebook().look(lead.site.x, lead.site.y, lead.site.band);
            break;
        }
        if (ending == "mission" && lead.id == "mission-backroom") {
            break;
        }
        // TASK #82. `letters` needs the SECOND Mission lead read, not the
        // first -- mission-flagstones' own detail is the sentence that says
        // Maell's letters exist at all (see unlockedLetters()'s own note),
        // and mission-backroom alone leaves them still Open, not Followed.
        if (ending == "letters" && lead.id == "mission-flagstones") {
            break;
        }
        if (ending == "weighhouse" && lead.id == "weighhouse-ledger") {
            break;
        }
        // THE CASEBOOK PASS. `opened` STOPS ON THE STEP THE NOTICE FIRES and
        // does nothing afterwards -- no walk back, no page opened. It exists
        // because the lead-opened plate is up for three seconds and for no
        // other reason, so without it there is no headless path to a picture of
        // the one moment this whole pass is about. Same hole `--threshold`
        // states for the crossing plate, same answer.
        if (ending == "opened" && lead.id == "weighhouse-ledger") {
            return gTrailRead;
        }
        if (ending == "hold" && lead.id == "drowned-hold") {
            break;
        }
    }
    // And where the shutter goes.
    if (ending == "notes" || ending == "start") {
        session.toggleCasebook();
    } else if (ending == "keys") {
        session.toggleKeys();
    } else if (ending == "letters") {
        // TASK #82. THE LETTERS THEMSELVES, not the place they were earned
        // at -- proving the panel exists is the point of this ending, and a
        // frame of the flagstones says nothing about whether a page reads.
        session.toggleLetters();
        // Picks the first one, so the capture shows an actual document
        // rather than only the title list a player sees before choosing.
        session.chooseVisibleTopic(0);
    } else if (last >= 0) {
        // STAND BACK, THEN LOOK AT IT. The anchors are counters, flagstones and
        // sagging floors, so a body that has walked onto one is standing with
        // its nose against masonry -- the first S10 capture of the Mission is a
        // photograph of a brown wall. Four tiles back is inside kLookRangeTiles,
        // so the clue is still readable from there, and it is the difference
        // between a frame of a place and a frame of a surface.
        const sim::Lead& lead = raws->leads()[static_cast<std::size_t>(last)];
        for (const std::int32_t back : {7, 6, 5, 4, 3}) {
            if (walkAcrossDistrict(session, lead.site.x, lead.site.y + back) ||
                walkAcrossDistrict(session, lead.site.x + back, lead.site.y) ||
                walkAcrossDistrict(session, lead.site.x, lead.site.y - back) ||
                walkAcrossDistrict(session, lead.site.x - back, lead.site.y)) {
                break;
            }
        }
        session.body().setYaw(sim::bearingTo(session.body().tileX(), session.body().tileY(),
                                             lead.site.x, lead.site.y));
        // AND THE CLUE IS NOT RE-PRINTED FOR THE SHUTTER. Backing off puts the
        // body outside kLookRangeTiles, which is the correct answer -- seven
        // tiles is out of reach of a flagstone -- so pressing Q again from here
        // says "NOTHING HERE WORTH WRITING DOWN", which it should. The clue is
        // in the casebook, which is where a clue belongs; what this frame is of
        // is the PLACE, with the CASE row under it saying where the trail
        // stands. `--trail=notes` is the frame of the writing.
    }
    return gTrailRead;
}

}  // namespace scripted

}  // namespace granadad::render
