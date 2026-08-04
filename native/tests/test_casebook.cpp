// The bloodletter trail: the leads, the dead ends, and the ward's nerve.
//
//   RAWS   the owner's casebook.json on its own -- that it loads, that the
//          trail is connected, and that the dead ends are dead on purpose.
//   WORLD  every lead's site checked against the BAKED DOCKS. A coordinate
//          lifted out of a generator script is a claim, and a claim about
//          where a building is belongs in the build gate, not in a comment.
//   PLAY   the whole trail walked through the key a player presses.

#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/casebook.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/legend.hpp"
#include "granadad/sim/tile_query.hpp"

using namespace granadad::sim;
namespace content = granadad::content;
namespace render = granadad::render;

namespace {

[[nodiscard]] const CasebookRaws& raws() {
    static const CasebookRaws loaded = CasebookRaws::load(content::contentDir());
    return loaded;
}

}  // namespace

// ===========================================================================
// RAWS
// ===========================================================================

TEST_CASE("the casebook loads the owner's own trail, and every lead is reachable from the hook") {
    const CasebookRaws& file = raws();
    REQUIRE(file.loaded());
    CHECK(file.leads().size() >= 10);
    CHECK_FALSE(file.title().empty());
    CHECK_FALSE(file.hook().empty());

    // EXACTLY ONE LEAD STARTS OPEN. Two would be two games; none would be a
    // game with no way into it, which is the failure mode a data-driven trail
    // has and a hard-coded one does not.
    std::int32_t starts = 0;
    for (const Lead& lead : file.leads()) {
        starts += lead.start ? 1 : 0;
    }
    CHECK(starts == 1);

    // EVERY `opens` NAMES A LEAD THAT EXISTS. A typo here is a branch of the
    // trail that silently never appears, and nothing on screen would say so.
    for (const Lead& lead : file.leads()) {
        for (const std::string& id : lead.opens) {
            INFO("lead " << lead.id << " opens " << id);
            CHECK(file.indexOf(id) >= 0);
        }
        // And no lead opens itself, which would be a loop with no exit.
        CHECK(std::find(lead.opens.begin(), lead.opens.end(), lead.id) == lead.opens.end());
    }

    // AND EVERY LEAD IS REACHABLE FROM THE ONE THAT STARTS OPEN. Walked as a
    // graph rather than eyeballed: an unreachable lead is authored content the
    // player can never see, and the file is exactly the shape where that
    // happens by accident.
    std::vector<bool> reached(file.leads().size(), false);
    std::vector<std::int32_t> frontier;
    for (std::size_t i = 0; i < file.leads().size(); ++i) {
        if (file.leads()[i].start) {
            reached[i] = true;
            frontier.push_back(static_cast<std::int32_t>(i));
        }
    }
    while (!frontier.empty()) {
        const std::int32_t at = frontier.back();
        frontier.pop_back();
        for (const std::string& id : file.leads()[static_cast<std::size_t>(at)].opens) {
            const std::int32_t next = file.indexOf(id);
            if (next >= 0 && !reached[static_cast<std::size_t>(next)]) {
                reached[static_cast<std::size_t>(next)] = true;
                frontier.push_back(next);
            }
        }
    }
    for (std::size_t i = 0; i < reached.size(); ++i) {
        INFO("unreachable lead: " << file.leads()[i].id);
        CHECK(reached[i]);
    }

    // THE DEAD ENDS ARE DEAD ON PURPOSE, and the flag agrees with the fact. A
    // lead marked deadEnd that opens something is a mislabelled clue; a lead
    // that opens nothing without the flag is a trail that stops with no notice.
    std::int32_t deadEnds = 0;
    for (const Lead& lead : file.leads()) {
        INFO("lead " << lead.id);
        if (lead.deadEnd) {
            CHECK(lead.opens.empty());
            ++deadEnds;
        } else if (lead.opens.empty()) {
            // The only lead allowed to open nothing without being a dead end is
            // the one that closes the surface trail.
            CHECK(lead.close);
        }
    }
    // At least two, because "clues, witnesses, dead ends" is the brief and one
    // dead end is an accident rather than a design.
    CHECK(deadEnds >= 2);

    // EXACTLY ONE CLOSES IT.
    std::int32_t closers = 0;
    for (const Lead& lead : file.leads()) {
        closers += lead.close ? 1 : 0;
    }
    CHECK(closers == 1);

    // Every lead has words. A blank clue is a walk across the district for
    // nothing, and it would look exactly like a lead that had not been found.
    for (const Lead& lead : file.leads()) {
        INFO("lead " << lead.id);
        CHECK_FALSE(lead.place.empty());
        CHECK_FALSE(lead.what.empty());
        CHECK_FALSE(lead.found.empty());
        CHECK_FALSE(lead.detail.empty());
        // The clue is one HUD line's worth. The message row clips, and a clue
        // that arrives clipped is a clue the player did not get.
        CHECK(lead.found.size() < 96);
        CHECK(lead.dread > 0);
    }
}

TEST_CASE("the dread bands rise, and name the ward's nerve at every score") {
    const CasebookRaws& file = raws();
    REQUIRE(file.dreadBands().size() >= 3);
    // Ascending, and the first one covers zero -- so there is never a score
    // with no label, which would put a blank in the middle of a HUD row.
    CHECK(file.dreadBands().front().at == 0);
    for (std::size_t i = 1; i < file.dreadBands().size(); ++i) {
        CHECK(file.dreadBands()[i].at > file.dreadBands()[i - 1].at);
    }
    CHECK_FALSE(file.dreadLabel(0).empty());
    CHECK_FALSE(file.dreadLabel(100).empty());
    CHECK(file.dreadLabel(0) != file.dreadLabel(100));

    // AND THE TRAIL CAN ACTUALLY REACH THE TOP BAND. A band nobody can get to
    // is a string in a file. Every lead's dread added up has to clear it.
    std::int32_t total = 0;
    for (const Lead& lead : file.leads()) {
        total += lead.dread;
    }
    CHECK(total >= file.dreadBands().back().at);
}

// ===========================================================================
// WORLD
// ===========================================================================

TEST_CASE("every lead stands somewhere a body can stand in the baked Docks") {
    // THE COORDINATES ARE A CLAIM AND THIS IS WHERE IT IS CHECKED.
    //
    // casebook.json's sites are the tile coordinates of the map's own authored
    // `script_anchor` markers -- clue_c1_mission_backroom and its neighbours in
    // tools/scripts/gen_docks_surface.py -- converted by the one documented
    // rule (local + 32, local z + 8). Markers are not carried in the baked
    // TROJSAV, so nothing at run time can re-derive them; what CAN be checked,
    // and is checked here, is that each one lands on a cell of the real world
    // that a body could be standing on when it looks.
    //
    // docks.hpp's own kPlaces table was lifted from the same generator and is
    // pinned the same way. This is that precedent, applied.
    const content::World world = content::loadWorldFile(content::bakedMap(docks::kWorldName));
    const TileQuery tiles(world);
    const CasebookRaws& file = raws();
    REQUIRE(file.loaded());

    for (const Lead& lead : file.leads()) {
        INFO("lead " << lead.id << " at (" << lead.site.x << ',' << lead.site.y << ",z"
                     << lead.site.band << ')');
        CHECK(tiles.inBounds(lead.site.x, lead.site.y, lead.site.band));
        // STANDABLE WITHIN THE LOOK RANGE. The anchor itself is sometimes the
        // counter, the flagstone or the sagging floor -- a solid cell by
        // design. What has to be true is that a body can get within
        // kLookRangeTiles of it, which is the rule Casebook::look applies.
        bool standable = false;
        for (std::int32_t dy = -kLookRangeTiles; dy <= kLookRangeTiles && !standable; ++dy) {
            for (std::int32_t dx = -kLookRangeTiles; dx <= kLookRangeTiles; ++dx) {
                if ((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) > kLookRangeTiles) {
                    continue;
                }
                if (tiles.standable(lead.site.x + dx, lead.site.y + dy, lead.site.band)) {
                    standable = true;
                    break;
                }
            }
        }
        CHECK(standable);
    }

    // AND NO TWO LEADS SHARE A SPOT. Two clues within one look of each other
    // means the second is unreachable: look() takes the nearest and the nearer
    // one would answer every time.
    const std::vector<Lead>& leads = file.leads();
    for (std::size_t i = 0; i < leads.size(); ++i) {
        for (std::size_t j = i + 1; j < leads.size(); ++j) {
            if (leads[i].site.band != leads[j].site.band) {
                continue;
            }
            const std::int32_t dx = leads[i].site.x - leads[j].site.x;
            const std::int32_t dy = leads[i].site.y - leads[j].site.y;
            const std::int32_t distance = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
            INFO(leads[i].id << " and " << leads[j].id << " are " << distance << " apart");
            CHECK(distance > kLookRangeTiles);
        }
    }
}

// ===========================================================================
// THE TRAIL
// ===========================================================================

TEST_CASE("you cannot read a clue nobody has pointed you at") {
    // THE DESIGN LAW, TESTED. Gazetteer section 5.3: the gate is knowing WHERE
    // to ask. So a player who walks the whole district on day one collects
    // nothing -- every site but the first reads as an ordinary corner of the
    // ward until somebody has named it.
    Casebook notes;
    notes.begin(raws());
    const std::vector<Lead>& leads = raws().leads();

    std::int32_t start = -1;
    for (std::size_t i = 0; i < leads.size(); ++i) {
        if (leads[i].start) {
            start = static_cast<std::int32_t>(i);
        }
    }
    REQUIRE(start >= 0);

    for (std::size_t i = 0; i < leads.size(); ++i) {
        if (static_cast<std::int32_t>(i) == start) {
            continue;
        }
        const Lead& lead = leads[i];
        const LookResult saw = notes.look(lead.site.x, lead.site.y, lead.site.band);
        INFO("standing on " << lead.id);
        CHECK_FALSE(saw.found);
        // And the ward's nerve did not move, because nothing was learned.
        CHECK(notes.dread() == 0);
    }
    CHECK(notes.readCount() == 0);
}

TEST_CASE("the trail is walked end to end, and the dead ends cost a walk and pay a clue") {
    Casebook notes;
    notes.begin(raws());
    const std::vector<Lead>& leads = raws().leads();

    // WALK IT. Repeatedly: look at every lead the book currently holds open,
    // which is exactly what a player does -- go where the last clue pointed.
    // Bounded by the lead count, so a trail with a cycle in it fails rather
    // than hanging the suite.
    std::int32_t rounds = 0;
    while (notes.nextOpen() >= 0 && rounds <= static_cast<std::int32_t>(leads.size())) {
        const std::int32_t at = notes.nextOpen();
        const Lead& lead = leads[static_cast<std::size_t>(at)];
        const LookResult saw = notes.look(lead.site.x, lead.site.y, lead.site.band);
        INFO("looking at " << lead.id);
        CHECK(saw.found);
        CHECK(saw.lead == at);
        CHECK(saw.line == lead.found);
        // A DEAD END OPENS NOTHING. The converse is deliberately not asserted,
        // and that is the trail's shape rather than a weaker claim: the trail
        // CONVERGES -- four separate leads all end up pointing at the Drowned
        // Hold, which is what corroboration is -- so the second of those to be
        // read opens no NEW entry and is still not a dead end.
        if (lead.deadEnd) {
            CHECK(saw.opened == 0);
        }
        ++rounds;
    }
    CHECK(notes.nextOpen() == -1);

    // EVERY LEAD READ, and the case closed.
    CHECK(notes.readCount() == static_cast<std::int32_t>(leads.size()));
    CHECK(notes.closed());
    CHECK(notes.coldCount() >= 2);
    // The ward is frightened by the end of it, and the label says so.
    CHECK(notes.dread() >= raws().dreadBands().back().at);
    CHECK(raws().dreadLabel(notes.dread()) == raws().dreadBands().back().label);

    // AND A SECOND LOOK CHANGES NOTHING. A player who comes back to a site to
    // re-read what they found gets it, and the ward's nerve does not move
    // again -- which is what stops a trail being farmable.
    const std::int32_t dread = notes.dread();
    const Lead& first = leads.front();
    const LookResult again = notes.look(first.site.x, first.site.y, first.site.band);
    CHECK_FALSE(again.found);
    CHECK(again.line == first.found);
    CHECK(notes.dread() == dread);
}

TEST_CASE("a dead end goes in the book struck through, and a followed lead does not") {
    Casebook notes;
    notes.begin(raws());
    const std::vector<Lead>& leads = raws().leads();

    std::int32_t rounds = 0;
    while (notes.nextOpen() >= 0 && rounds <= static_cast<std::int32_t>(leads.size())) {
        const std::int32_t at = notes.nextOpen();
        const Lead& lead = leads[static_cast<std::size_t>(at)];
        (void)notes.look(lead.site.x, lead.site.y, lead.site.band);
        CHECK(notes.state(at) ==
              (lead.deadEnd ? LeadState::Cold
                            : (lead.opens.empty() ? LeadState::Cold : LeadState::Followed)));
        ++rounds;
    }
    // The closing lead opens nothing, so it too reads Cold -- which is correct
    // and is why `closed()` is a separate fact from a lead's state.
    CHECK(notes.closed());
}

// ===========================================================================
// PLAY
// ===========================================================================

TEST_CASE("the look key finds the body, and the district's other corners stay quiet") {
    const CasebookRaws& file = raws();
    const std::int32_t mission = file.indexOf("mission-backroom");
    REQUIRE(mission >= 0);
    const LeadSite site = file.leads()[static_cast<std::size_t>(mission)].site;

    render::SessionConfig config;
    config.contentDir = content::contentDir();
    config.timeOfDay = 8 * 3600;
    config.spawnX = site.x;
    config.spawnY = site.y;
    config.spawnBand = site.band;
    render::Session session(config);
    session.stepMany(MoveInput{}, 2);

    // The case is live from the first frame -- see the first-run card.
    REQUIRE(session.casebook().active());
    CHECK(session.casebook().readCount() == 0);
    CHECK(session.casebook().nextOpen() == mission);

    // Q, and the body is in the book.
    session.examine();
    CHECK(session.casebook().readCount() == 1);
    CHECK(session.casebook().state(mission) == LeadState::Followed);
    CHECK(session.casebook().dread() > 0);
    // The clue itself is the message, whole.
    INFO(session.lastMessage());
    CHECK(session.lastMessage() ==
          raws().leads()[static_cast<std::size_t>(mission)].found);

    // AND THE HUD SAYS SO, ON ONE ROW, ON AN EDGE.
    // And the CASE row is where the count went: one read, and the denominator
    // has jumped because three new leads went in the book.
    const std::string row = session.caseLine();
    REQUIRE_FALSE(row.empty());
    CHECK(row.substr(0, 6) == "CASE 1");
    CHECK(row.find("/4") != std::string::npos);
    CHECK(row.find(" > ") != std::string::npos);
    CHECK(row.find('\n') == std::string::npos);
    CHECK(row.size() <= 40);

    // Pressing it again on the same spot re-reads the note and moves nothing.
    const std::int32_t dread = session.casebook().dread();
    session.examine();
    CHECK(session.casebook().dread() == dread);
}

TEST_CASE("the casebook opens in the conversation's own bands and leaves the middle alone") {
    render::SessionConfig config;
    config.contentDir = content::contentDir();
    config.timeOfDay = 8 * 3600;
    render::Session session(config);
    session.stepMany(MoveInput{}, 2);

    // A Session built without SessionConfig::openingPage starts with the notes
    // down; J is what a player presses.
    CHECK_FALSE(session.casebookOpen());
    session.toggleCasebook();
    REQUIRE(session.casebookOpen());
    const render::DialogueViewState view = session.dialogueView();
    CHECK(view.open);
    CHECK(view.speaker == "THE CASEBOOK");
    CHECK_FALSE(view.topics.empty());
    // The one lead you start with, and the hook above it.
    CHECK(view.topics.size() == 1);
    CHECK_FALSE(view.line.empty());

    // THE CENTRE OF THE SCREEN STAYS EMPTY WITH IT OPEN. Same claim the
    // dialogue surface makes and the same way of proving it: draw the frame
    // with the book up and with it down, and require the exclusion rectangle to
    // be pixel-identical.
    render::Framebuffer withBook(config.width, config.height);
    session.drawFrame(withBook);
    session.toggleCasebook();
    REQUIRE_FALSE(session.casebookOpen());
    render::Framebuffer without(config.width, config.height);
    session.drawFrame(without);
    const render::CentreRect centre = render::hudCentreRect(config.width, config.height);
    for (int y = centre.y0; y < centre.y1; ++y) {
        for (int x = centre.x0; x < centre.x1; ++x) {
            REQUIRE(withBook.pixels()[withBook.index(x, y)] ==
                    without.pixels()[without.index(x, y)]);
        }
    }
}

// ===========================================================================
// THE LONG GAME
// ===========================================================================

TEST_CASE("the ward has no opinion of a man who arrived this morning") {
    render::SessionConfig config;
    config.contentDir = content::contentDir();
    config.timeOfDay = 8 * 3600;
    render::Session session(config);
    session.stepMany(MoveInput{}, 2);

    const Legend fresh = session.legend();
    CHECK(fresh.totalRungs() == 0);
    CHECK(fresh.title() == "NOBODY");
    // And the row is empty rather than saying NOBODY on the HUD, because a
    // corner of the screen telling you that you are nobody every frame is not
    // information.
    CHECK(session.legendLine().empty());
    // No rung, no boon.
    CHECK(fresh.picksPerSetBonus() == 0);
    CHECK(fresh.lookRangeBonus() == 0);
    CHECK(fresh.pricePercent() == 0);
}

TEST_CASE("working the trail is what the Flame's track is made of, and it pays a rung") {
    render::SessionConfig config;
    config.contentDir = content::contentDir();
    config.timeOfDay = 8 * 3600;
    render::Session session(config);
    session.stepMany(MoveInput{}, 2);

    const std::int32_t before = session.legend().row(LegendTrack::Flame).score;
    // Walk the whole trail through the simulation, standing at each site the
    // book has open -- the same call Session::examine makes.
    const std::vector<Lead>& leads = raws().leads();
    std::int32_t rounds = 0;
    while (session.casebook().nextOpen() >= 0 &&
           rounds <= static_cast<std::int32_t>(leads.size())) {
        const std::int32_t at = session.casebook().nextOpen();
        const Lead& lead = leads[static_cast<std::size_t>(at)];
        (void)session.casebook().look(lead.site.x, lead.site.y, lead.site.band);
        ++rounds;
    }
    REQUIRE(session.casebook().closed());

    const Legend after = session.legend();
    CHECK(after.row(LegendTrack::Flame).score > before);
    CHECK(after.row(LegendTrack::Flame).rung > 0);
    CHECK(after.totalRungs() > 0);
    CHECK(after.title() != "NOBODY");
    CHECK_FALSE(session.legendLine().empty());

    // A DEAD END PAYS TOO, AND THAT IS THE DESIGN. Walking to the King's Bond
    // to find the struck line never came there is the Flame's own work; a
    // system that paid nothing for it would be a system telling the player not
    // to look. Four against six -- less than a lead that opened something, and
    // never nothing.
    CHECK(session.casebook().coldCount() >= 2);
}

TEST_CASE("the five tracks are five different people, and the titles never collide") {
    // Every rung of every track has a name, none of them is blank, and no two
    // tracks share one -- otherwise the HUD could tell two different players
    // the ward calls them the same thing for opposite lives.
    std::set<std::string> seen;
    for (std::int32_t t = 0; t < static_cast<std::int32_t>(kLegendTracks); ++t) {
        const LegendTrack track = static_cast<LegendTrack>(t);
        CHECK_FALSE(legendTrackName(track).empty());
        for (std::int32_t rung = 1; rung <= kLegendRungs; ++rung) {
            const std::string title(legendTitle(track, rung));
            INFO(legendTrackName(track) << " rung " << rung << " = " << title);
            CHECK_FALSE(title.empty());
            CHECK(title != "NOBODY");
            CHECK(seen.insert(title).second);
        }
        // Rung zero is the same for all five: the ward has not noticed you.
        CHECK(legendTitle(track, 0) == "NOBODY");
    }
    // And the thresholds climb, so a rung is always harder than the last.
    for (std::int32_t i = 1; i < kLegendRungs; ++i) {
        CHECK(kLegendThresholds[i] > kLegendThresholds[i - 1]);
    }
}

TEST_CASE("the trail is walked across the real district, on foot, by the router") {
    // THE CLAIM THIS FILE'S `WORLD` CASE CANNOT MAKE. That one proves a body
    // could stand near every site; this one proves a body can WALK to them from
    // the authored spawn, through the district's own breadth-first router, with
    // no teleport anywhere in the path. A coordinate that is standable and
    // walled off from the quay is a lead nobody can reach.
    render::SmokeRunConfig run;
    run.session.contentDir = content::contentDir();
    run.trail = true;
    run.session.timeOfDay = 9 * 3600;
    run.steps = 0;
    run.stamp = false;

    const render::SmokeRunResult played = render::runSmoke(run);
    INFO(played.summary);
    // EVERY SITE IT WALKED TO, IT READ. The two differing means the walk got
    // near a lead and could not get near enough, which is the failure a drifted
    // coordinate produces and the one this run exists to catch.
    CHECK(played.trailRead == played.trailWalked);
    CHECK(played.trailWalked >= 6);
    CHECK(played.ok);
    CHECK(played.summary.find("unreached=0") != std::string::npos);
    // AND IT GETS TO THE END OF THE SURFACE TRAIL ON FOOT.
    CHECK(played.summary.find("closed=yes") != std::string::npos);
    // The ward is frightened by then, and the Flame's track has paid a rung.
    CHECK(played.summary.find("dread=0 ") == std::string::npos);
    CHECK(played.summary.find("called=NOBODY") == std::string::npos);
}
