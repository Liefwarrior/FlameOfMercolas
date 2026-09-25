// VOICE LANE: THE ONE VOICE CHANNEL, AND WHETHER THE STREET SPEAKS.
//
// Eli, 2026-09-10: "I'd expect the watch and crowd to react appropriately to
// the violence they're witnessing." Until this lane nobody in the Docks said
// a word unless E was pressed. These cases are the proof that they do now,
// and each is written so that it can go red for one reason:
//
//   THE ROWS ARE THE REGISTER   every authored row draws in the 4x6 font,
//                               fits one line beside a name at 960x540, has
//                               no dash in it, and the learned word is in a
//                               priest's mouth and nobody else's -- the
//                               owner's own diction rule, grepped.
//   THE CHAIN FALLS THE RIGHT   a hostile cell borrows cold and never warm;
//   WAY                         an urchin's line is an urchin's before it is
//                               a wastrel's; a cell nobody authored bottoms
//                               out on the floor and never on "...".
//   SELECTION IS DRAW-FREE      the pick is a pure function of (actor, step,
//                               talks); the emitter reads the room through a
//                               const reference and cannot move a draw; the
//                               room's and the ward's hashes are byte for
//                               byte what they were before it ran.
//   LEAVING IS A TRANSITION     LEAVE carries a goodbye; the back key says
//                               one; walking off ends the talk with the word
//                               thrown after you.
//   THE ADDRESS                 the one you talk to holds still for it, and
//                               a reply that moved them shows a worded
//                               reaction and never a number.
//   THE STREET IS NOT A WALL    two pass lines are never closer than the
//                               channel's cooldown.
//   THE SIX ENDINGS LAND        --voice=pass|overheard|farewell|moved|react|
//                               watch each land every beat they owe, through
//                               the real verbs, so a frame cannot pass as a
//                               picture of a line that was not on it.
//   THE FRAME IS OF THE SPEAKER at the shutter of every composed ending the
//                               body the frame is about is in the field of
//                               view, with a clear lens line and nobody
//                               drawn in the way -- the lens's own rule
//                               (cameraSightClear), held against the sim's
//                               corner-grazing one on the taproom table that
//                               made the watch reveal a picture of a wall.

#include <doctest/doctest.h>

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/hud.hpp"
#include "granadad/render/session.hpp"
#include "granadad/render/street_voice.hpp"
#include "granadad/sim/barks.hpp"
#include "granadad/sim/dialogue.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/stealth.hpp"
#include "granadad/sim/world_hash.hpp"

namespace content = granadad::content;
namespace render = granadad::render;
namespace sim = granadad::sim;
using namespace granadad::sim;

namespace {

const BarkTables& barks() {
    static const BarkTables tables = BarkTables::load(content::contentDir());
    return tables;
}

[[nodiscard]] bool startsWith(const std::string& text, const char* prefix) {
    return text.rfind(prefix, 0) == 0;
}

/// The keys this lane authored: the four prefixes of the channel, the Watch's
/// tables and the rumor door.
[[nodiscard]] bool voiceKey(const std::string& key) {
    return startsWith(key, "pass") || startsWith(key, "farewell") ||
           startsWith(key, "overheard") || startsWith(key, "react.") ||
           startsWith(key, "watch.notice") || startsWith(key, "watch.heat") ||
           startsWith(key, "watch.paper") || startsWith(key, "watch.wielder") ||
           startsWith(key, "watch.approach") || startsWith(key, "rumor.");
}

[[nodiscard]] std::string lower(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

[[nodiscard]] render::SmokeRunResult play(render::SmokeRunConfig run) {
    run.session.contentDir = content::contentDir();
    run.session.timeOfDay = render::scriptedStartHour(run) * 3600;
    run.steps = 0;
    run.stamp = false;
    return render::runSmoke(run);
}

[[nodiscard]] render::SmokeRunResult playVoice(const char* ending) {
    render::SmokeRunConfig run;
    run.voice = true;
    run.voiceEnd = ending;
    return play(run);
}

/// The room's and the ward's digests, test_street_panic's own shape.
[[nodiscard]] std::pair<std::uint64_t, std::uint64_t> hashesOf(const render::Session& session) {
    HashSink room(0x564F494345524F4Dull);  // "VOICEROM"
    session.tavern().hash_into(room);
    HashSink ward(0x564F494345574152ull);  // "VOICEWAR"
    session.people().hash_into(ward);
    return {room.finished(), ward.finished()};
}

/// A ward body clear of the Gull with a standable tile beside it, the street
/// corner test_ward_voice.cpp stands on.
[[nodiscard]] const WardActor* streetCorner(const render::Session& session) {
    for (const WardActor& actor : session.people().actors()) {
        const bool nearTheGull = actor.x >= gull::kFootprintX0 - 4 &&
                                 actor.x <= gull::kFootprintX1 + 4 &&
                                 actor.y >= gull::kFootprintY0 - 6 &&
                                 actor.y <= gull::kFootprintY1 + 4;
        if (!actor.dead && isPerson(actor.type) && !nearTheGull &&
            actor.band == docks::kBandQuayside &&
            session.tiles().standable(actor.x + 1, actor.y, actor.band)) {
            return &actor;
        }
    }
    return nullptr;
}

}  // namespace

// ===========================================================================
// THE ROWS ARE THE REGISTER
// ===========================================================================

TEST_CASE("every voice row draws, fits beside a name, carries no dash, and keeps the diction rule") {
    REQUIRE(barks().loaded());
    // The four files of this lane are on the sheet: a table from each.
    REQUIRE(barks().has("pass.serf"));
    REQUIRE(barks().has("farewell.away"));
    REQUIRE(barks().has("overheard.maritime"));
    REQUIRE(barks().has("react.serf.struck"));
    REQUIRE(barks().has("watch.approach.violence"));
    REQUIRE(barks().has("rumor.clergy"));
    REQUIRE(barks().has("greet.serf.neutral.evening.2"));

    // A name of up to eighteen glyphs, a colon and a space, and the row: the
    // 92-cell line at 960x540's minor scale (hud.cpp: 960 - 2 * 18 margin,
    // ten pixels a cell). The longest name on the Gull's roster is HOBBIN
    // MASTWRIGHT at seventeen.
    constexpr std::size_t kRowMax = 72;
    std::size_t counted = 0;
    for (const BarkTables::Table& table : barks().tables()) {
        if (!voiceKey(table.key)) {
            continue;
        }
        for (const std::string& row : table.rows) {
            INFO(table.key, ": ", row);
            ++counted;
            for (const char c : row) {
                CHECK(render::isDrawableGlyph(c));
            }
            // No em-dash, no double dash, no dash standing alone: a spoken
            // row is a sentence, and the font's dash is for compounds.
            CHECK(row.find(" - ") == std::string::npos);
            CHECK(row.find("--") == std::string::npos);
            CHECK(row.find("-") != 0);
            if (!startsWith(table.key, "rumor.")) {
                // The rumor rows are the panel's (wrapped); the rest ride
                // the one-line voice row.
                CHECK(row.size() <= kRowMax);
            }
            // THE DICTION RULE (DECISIONS.md, districts-as-chapters): very
            // few people know what a bloodletter is. Only a priest says it.
            if (lower(row).find("bloodletter") != std::string::npos) {
                CHECK(table.key.find(".clergy") != std::string::npos);
            }
            // And nobody in this district talks like an assistant.
            const std::string low = lower(row);
            CHECK(low.find("okay") == std::string::npos);
            CHECK(low.find("gonna") == std::string::npos);
            CHECK(low.find("yeah") == std::string::npos);
        }
    }
    // The lane's own count: pinned so content lost goes red.
    CHECK(counted >= 370);
    // And the priest does say it, once, so the rule is a rule and not an
    // absence.
    bool priestSaysIt = false;
    for (const std::string& row : *barks().rows("rumor.clergy")) {
        if (lower(row).find("bloodletter") != std::string::npos) {
            priestSaysIt = true;
        }
    }
    CHECK(priestSaysIt);
}

TEST_CASE("the table pin: the voice lane's four files are on the sheet and the count moved by exactly them") {
    // test_dialogue.cpp pins the whole sheet at 535 (393 before this lane);
    // this counts only the lane's own keys, so a row lost from a voice file
    // is red here by name.
    std::size_t tables = 0;
    for (const BarkTables::Table& table : barks().tables()) {
        if (voiceKey(table.key) || table.key.find(".2") == table.key.size() - 2) {
            ++tables;
        }
    }
    CHECK(tables == 142);
}

// ===========================================================================
// THE CHAIN FALLS THE RIGHT WAY
// ===========================================================================

TEST_CASE("attitude tiers borrow the nearest authored neighbour and never cross the middle") {
    CHECK(attitudeFallbacks(Attitude::Kin) == std::vector<std::string_view>{"kin", "friend", "warm"});
    CHECK(attitudeFallbacks(Attitude::Friend) == std::vector<std::string_view>{"friend", "warm"});
    CHECK(attitudeFallbacks(Attitude::Warm) == std::vector<std::string_view>{"warm"});
    CHECK(attitudeFallbacks(Attitude::Neutral).empty());
    CHECK(attitudeFallbacks(Attitude::Cold) == std::vector<std::string_view>{"cold"});
    CHECK(attitudeFallbacks(Attitude::Hostile) == std::vector<std::string_view>{"hostile", "cold"});
}

TEST_CASE("the voice chain is the trade, the family, the tiers, the floor -- in that order") {
    const std::vector<std::string> urchin =
        voiceChain("pass", "urchin", JobFamily::Wastrel, Attitude::Hostile);
    const std::vector<std::string> want = {
        "pass.urchin.hostile", "pass.wastrel.hostile", "pass.urchin.cold", "pass.wastrel.cold",
        "pass.hostile",        "pass.cold",            "pass.urchin",      "pass.wastrel",
        "pass",
    };
    CHECK(urchin == want);
    // A type that IS its family does not repeat the rung.
    const std::vector<std::string> serf = voiceChain("pass", "serf", JobFamily::Serf, Attitude::Neutral);
    CHECK(serf == std::vector<std::string>{"pass.serf", "pass"});
    // The Gull's roster has no type at all.
    const std::vector<std::string> gull = voiceChain("react", "", JobFamily::Trade, Attitude::Warm);
    CHECK(gull == std::vector<std::string>{"react.trade.warm", "react.warm", "react.trade", "react"});
}

TEST_CASE("every family at every attitude resolves a pass, a farewell and an overheard row, never the floor of nothing") {
    REQUIRE(barks().loaded());
    for (std::size_t f = 0; f < kJobFamilyCount; ++f) {
        const JobFamily family = static_cast<JobFamily>(f);
        if (family == JobFamily::Beast || family == JobFamily::FlameOfMerc) {
            continue;  // no voice rung: a cat has no goodbye, and the Flame's own is not in the ward
        }
        for (int a = 0; a < 6; ++a) {
            const Attitude attitude = static_cast<Attitude>(a);
            for (const char* prefix : {"pass", "farewell", "overheard"}) {
                const std::string key(barks().resolve(voiceChain(prefix, "", family, attitude)));
                INFO(prefix, " ", jobFamilyKey(family), " ", attitudeKey(attitude), " -> ", key);
                REQUIRE_FALSE(key.empty());
                // A hostile man never borrows a warm row.
                if (attitude == Attitude::Hostile || attitude == Attitude::Cold) {
                    CHECK(key.find("warm") == std::string::npos);
                    CHECK(key.find("friend") == std::string::npos);
                }
                if (attitude >= Attitude::Warm) {
                    CHECK(key.find("cold") == std::string::npos);
                    CHECK(key.find("hostile") == std::string::npos);
                }
            }
        }
        // And a witness of each deed, in this family's own mouth.
        for (const char* deed : {"struck", "steel", "slew"}) {
            const std::string family_key(jobFamilyKey(family));
            CHECK(barks().has("react." + family_key + "." + deed));
        }
    }
    // The trades inside a family that sound like themselves.
    for (const char* type : {"urchin", "thief", "fisher", "sailor", "carter", "disciple"}) {
        CHECK(barks().has(std::string("pass.") + type));
        CHECK(barks().has(std::string("farewell.") + type));
    }
}

// ===========================================================================
// THE GREET CELLS WIDEN WITHOUT THE OWNER'S FILE MOVING
// ===========================================================================

TEST_CASE("a numbered sibling widens a greet cell, and no cell totals seven") {
    REQUIRE(barks().loaded());
    // The canon key still resolves and still holds the owner's own rows.
    REQUIRE(barks().rows("greet.serf.neutral.evening") != nullptr);
    CHECK(barks().rows("greet.serf.neutral.evening")->size() == 3);
    CHECK(barks().rows("greet.serf.neutral.evening.2")->size() == 2);
    CHECK(barks().cellRowCount("greet.serf.neutral.evening") == 5);
    // A key with no sibling reads exactly what line() reads.
    CHECK(barks().cellRowCount("greet.serf.hostile") ==
          static_cast<std::int32_t>(barks().rows("greet.serf.hostile")->size()));
    for (std::int32_t i = 0; i < 9; ++i) {
        CHECK(barks().cellLine("greet.serf.hostile", i) == barks().line("greet.serf.hostile", i));
    }
    // The union rotates over the sibling's rows too.
    std::set<std::string> seen;
    for (std::int32_t i = 0; i < 5; ++i) {
        seen.insert(std::string(barks().cellLine("greet.serf.neutral.evening", i)));
    }
    CHECK(seen.size() == 5);
    CHECK(seen.count((*barks().rows("greet.serf.neutral.evening.2"))[0]) == 1);
    // THE RULE THE ROTATION DEPENDS ON: rowIndexFor steps by seven per
    // conversation, so a cell of seven (or fourteen) would say the same row
    // every time. No greet cell may total it.
    for (const BarkTables::Table& table : barks().tables()) {
        if (!startsWith(table.key, "greet.")) {
            continue;
        }
        INFO(table.key);
        const std::int32_t count = barks().cellRowCount(table.key);
        CHECK(count % 7 != 0);
    }
}

TEST_CASE("the greeting over five conversations reaches a sibling row and the key stays the owner's") {
    DialogueDirector director = DialogueDirector::load(content::contentDir());
    Speaker docker;
    docker.actorId = 4;
    docker.name = "Wick Hempson";
    docker.family = JobFamily::Serf;
    std::set<std::string> heard;
    for (int i = 0; i < 5; ++i) {
        REQUIRE(director.open(docker, hourOfDay(21)));
        CHECK(director.greetingKey() == "greet.serf.neutral.evening");
        heard.insert(director.greeting());
        director.close();
    }
    const std::vector<std::string>& sibling = *barks().rows("greet.serf.neutral.evening.2");
    bool reached = false;
    for (const std::string& row : sibling) {
        reached = reached || heard.count(row) == 1;
    }
    CHECK(reached);
    CHECK(heard.size() == 5);
}

// ===========================================================================
// SELECTION IS DRAW-FREE
// ===========================================================================

TEST_CASE("the pick is a pure function of (actor, step, talks), and the ten-second bucket turns the ROW TEXT on every voice table") {
    REQUIRE(barks().loaded());
    const std::vector<std::string> chain = voiceChain("pass", "", JobFamily::Serf, Attitude::Neutral);
    const render::VoiceLine a = render::pickVoiceLine(barks(), chain, 7, 1200, 0);
    const render::VoiceLine b = render::pickVoiceLine(barks(), chain, 7, 1200, 0);
    CHECK(a.text == b.text);
    CHECK(a.key == "pass.serf");
    CHECK_FALSE(a.text.empty());
    // Inside one bucket the row holds.
    CHECK(render::voiceRowIndex(7, 1200, 0) == render::voiceRowIndex(7, 1799, 0));
    // ACROSS BUCKETS IT TURNS, ON THE TEXT, ON EVERY TABLE THIS LANE
    // AUTHORED. The critic's catch: an index compared instead of a row, on
    // the very five-row tables where a multiplier of five left the bucket
    // dead and a body saying one fixed row forever. The bucket is added
    // now, so the next bucket is the next row whatever the width, and over
    // `rows` buckets a mouth walks the whole cell.
    std::size_t tablesChecked = 0;
    for (const BarkTables::Table& table : barks().tables()) {
        if (!voiceKey(table.key) || table.rows.size() < 2) {
            continue;
        }
        ++tablesChecked;
        for (const std::int32_t actorId : {1, 7, 1000012}) {
            std::set<std::string> seen;
            for (std::int32_t bucket = 0; bucket < static_cast<std::int32_t>(table.rows.size());
                 ++bucket) {
                seen.insert(std::string(
                    barks().line(table.key, render::voiceRowIndex(actorId, bucket * 600, 0))));
            }
            INFO(table.key, " actor ", actorId);
            CHECK(seen.size() == table.rows.size());
            // And two consecutive buckets are never the same row.
            CHECK(barks().line(table.key, render::voiceRowIndex(actorId, 0, 0)) !=
                  barks().line(table.key, render::voiceRowIndex(actorId, 600, 0)));
        }
    }
    CHECK(tablesChecked >= 120);
    // Two mouths in one bucket start at different rows on a table wide
    // enough to tell them apart.
    std::set<std::string> mouths;
    for (const std::int32_t actorId : {1, 2, 3, 4, 5, 6}) {
        mouths.insert(std::string(barks().line("pass.serf", render::voiceRowIndex(actorId, 0, 0))));
    }
    CHECK(mouths.size() >= 3);
    // An unauthored chain is nothing, never "...".
    const render::VoiceLine none = render::pickVoiceLine(barks(), {"nothing.authored"}, 1, 0, 0);
    CHECK(none.empty());
}

TEST_CASE("<victim> is the name the sim recorded, and a row that needs one is skipped when nobody can be named") {
    CHECK(render::fillVictim("You've killed <victim>. In front of all of us.", "Gerta Saltcotte") ==
          "You've killed Gerta Saltcotte. In front of all of us.");
    CHECK(render::fillVictim("Not getting up.", "") == "Not getting up.");
    CHECK(render::fillVictim("Flame keep <victim>.", "").empty());
    // Every slew table has at least one row that needs no name, so a
    // witness always has something to say.
    for (const BarkTables::Table& table : barks().tables()) {
        if (!startsWith(table.key, "react.") || table.key.find(".slew") == std::string::npos) {
            continue;
        }
        bool nameless = false;
        for (const std::string& row : table.rows) {
            nameless = nameless || row.find("<victim>") == std::string::npos;
        }
        INFO(table.key);
        CHECK(nameless);
        // And no slew row says "him" or "he" of the corpse: Gerta and Sella
        // are on the roster.
        for (const std::string& row : table.rows) {
            INFO(row);
            CHECK(row.find(" him") == std::string::npos);
            CHECK(row.find("He ") == std::string::npos);
        }
    }
}

TEST_CASE("a two-minute walk on the Tarwalk never says a row twice, across mouths") {
    // THE CRITIC'S OWN MEASURE: thirteen lines and nine distinct on a
    // hundred and twenty second walk. The channel remembers every row said
    // inside two minutes and a spent cell falls to the next rung or stays
    // quiet, so the count of distinct rows IS the count of rows.
    render::SessionConfig config;
    config.contentDir = content::contentDir();
    config.timeOfDay = 16 * 3600;
    render::Session session(config);
    // Up and down the Tarwalk, ten seconds each way, so the walk stays in
    // the crowd instead of drifting into a corner: the critic's own route.
    sim::MoveInput walk;
    walk.forward = 1;
    std::vector<std::string> heard;
    std::int32_t lastStep = -1;
    for (int i = 0; i < 120 * kStepsPerSecond; ++i) {
        if (i % 600 == 0) {
            session.body().setYaw((i / 600) % 2 == 0 ? sim::kFacingWest : sim::kFacingEast);
        }
        session.step(walk);
        const render::VoiceLine& line = session.voice().current();
        if (!line.empty() && line.step != lastStep) {
            heard.push_back(line.text);
            lastStep = line.step;
        }
    }
    INFO("lines in two minutes: ", heard.size());
    CHECK(heard.size() >= 4);
    std::set<std::string> distinct(heard.begin(), heard.end());
    CHECK(distinct.size() == heard.size());
    // And the memory itself: a row said is a row the channel refuses for
    // two minutes and allows after.
    REQUIRE_FALSE(heard.empty());
    CHECK(session.voice().saidLately(heard.back(), lastStep));
    CHECK_FALSE(session.voice().saidLately(heard.back(), lastStep + render::kRowMemorySteps));
}

TEST_CASE("a Gull actor never draws a street pass row: the stool speaks from pass.room") {
    render::SessionConfig config;
    config.contentDir = content::contentDir();
    config.timeOfDay = 20 * 3600;
    render::Session session(config);
    // Into the room and along the bar, past the stools, for a minute.
    session.placeBodyAt(gull::kBartenderX, gull::kBartenderY + 1, gull::kGroundBand);
    std::vector<render::VoiceLine> passes;
    std::int32_t lastStep = -1;
    sim::MoveInput drift;
    for (int i = 0; i < 60 * kStepsPerSecond; ++i) {
        drift.forward = (i / 120) % 2 == 0 ? 1 : 0;
        drift.turn = (i / 60) % 3 == 0 ? 1 : 0;
        session.step(drift);
        const render::VoiceLine& line = session.voice().lastOn(render::VoiceChannel::Pass);
        if (!line.empty() && line.step != lastStep) {
            passes.push_back(line);
            lastStep = line.step;
        }
    }
    INFO("pass lines in the Gull: ", passes.size());
    CHECK(passes.size() >= 1);
    for (const render::VoiceLine& line : passes) {
        INFO(line.name, ": ", line.text, " (", line.key, ")");
        CHECK(startsWith(line.key, "pass.room"));
        CHECK(line.actorId < kWardSpeakerIdBase);
    }
    // The chain itself: a room chain names no quay cell.
    for (const std::string& key : voiceChain("pass.room", "", JobFamily::Serf, Attitude::Cold)) {
        CHECK(startsWith(key, "pass.room"));
    }
}

TEST_CASE("an overheard snatch on the street comes from a body standing still beside another") {
    render::SessionConfig config;
    config.contentDir = content::contentDir();
    config.timeOfDay = 16 * 3600;
    render::Session session(config);
    std::int32_t heard = 0;
    std::int32_t lastStep = -1;
    sim::MoveInput walk;
    for (int i = 0; i < 120 * kStepsPerSecond; ++i) {
        walk.forward = i >= 45 * kStepsPerSecond ? 1 : 0;
        walk.turn = (i / 90) % 4 == 3 ? 1 : 0;
        session.step(walk);
        const render::VoiceLine& line = session.voice().lastOn(render::VoiceChannel::Overheard);
        if (line.empty() || line.step == lastStep) {
            continue;
        }
        lastStep = line.step;
        if (line.step != static_cast<std::int32_t>(session.body().stepCount())) {
            continue;  // said on an earlier step; the pair's stance then is gone
        }
        ++heard;
        REQUIRE(line.actorId >= kWardSpeakerIdBase);
        const WardActor* speaker = session.people().byId(line.actorId - kWardSpeakerIdBase);
        REQUIRE(speaker != nullptr);
        CHECK(speaker->prevX == speaker->x);
        CHECK(speaker->prevY == speaker->y);
        bool company = false;
        for (const WardActor& other : session.people().actors()) {
            if (other.id != speaker->id && other.visible() && isPerson(other.type) &&
                other.band == speaker->band && std::abs(other.x - speaker->x) <= 1 &&
                std::abs(other.y - speaker->y) <= 1 && other.prevX == other.x &&
                other.prevY == other.y) {
                company = true;
            }
        }
        CHECK(company);
    }
    INFO("overheard on the street in two minutes: ", heard);
    CHECK(heard >= 1);
}

TEST_CASE("a watchman's notice is split by what his eyes see: fists up is not crouching") {
    REQUIRE(barks().has("watch.notice.hands"));
    REQUIRE(barks().has("watch.notice.sneak"));
    CHECK_FALSE(barks().has("watch.notice"));
    for (const std::string& row : *barks().rows("watch.notice.sneak")) {
        INFO(row);
        CHECK(lower(row).find("lower them") == std::string::npos);
        CHECK(lower(row).find("hands") == std::string::npos);
    }
    for (const std::string& row : *barks().rows("watch.notice.hands")) {
        INFO(row);
        CHECK(lower(row).find("crouch") == std::string::npos);
        CHECK(lower(row).find("stand up") == std::string::npos);
    }
    // And the readback names what is actually up.
    REQUIRE(barks().has("watch.approach.violence.steel"));
    for (const std::string& row : *barks().rows("watch.approach.violence")) {
        INFO(row);
        CHECK(lower(row).find("blade") == std::string::npos);
        CHECK(lower(row).find("steel") == std::string::npos);
    }
    for (const std::string& row : *barks().rows("watch.approach.violence.steel")) {
        INFO(row);
        CHECK(lower(row).find("hands down") == std::string::npos);
        CHECK(lower(row).find("fists") == std::string::npos);
    }
}

TEST_CASE("the emitter reads the room through a const reference and moves neither hash") {
    // THE PROOF THAT NO DRAW HIDES IN THE CHANNEL. A standalone emitter is fed
    // the live room and ward for a stretch that makes it speak (a walk down
    // the Tarwalk at four); the room's and the ward's section hashes before
    // and after are byte-identical. A draw is a counter the tavern hashes
    // (playerActionSeq_, npcSwingSeq_) and a witness memory is a ledger row
    // it hashes; a channel that touched either would show here.
    render::SessionConfig config;
    config.contentDir = content::contentDir();
    config.timeOfDay = 16 * 3600;
    render::Session session(config);
    // Walk into the crowd first so there is somebody to pass.
    sim::MoveInput walk;
    walk.forward = 1;
    session.stepMany(walk, 4 * kStepsPerSecond);

    const auto before = hashesOf(session);
    render::StreetVoice voice;
    render::VoiceScene scene;
    scene.room = &session.tavern();
    scene.people = &session.people();
    scene.tiles = &session.tiles();
    scene.playerTileX = session.body().tileX();
    scene.playerTileY = session.body().tileY();
    scene.playerBand = session.body().band();
    scene.playerXQ8 = session.body().x();
    scene.playerYQ8 = session.body().y();
    scene.step = 0;
    for (std::int32_t step = 0; step < 60 * kStepsPerSecond; ++step) {
        scene.step = step;
        voice.step(scene);
    }
    const auto after = hashesOf(session);
    CHECK(before.first == after.first);
    CHECK(before.second == after.second);
    // And it did speak -- an emitter that said nothing proves nothing.
    // (The session's own channel ran during the walk; this one is a second
    // reader of the same room, starting from a quiet row, and the Tarwalk at
    // four has a body within two tiles of the spawn inside a minute.)
    INFO("spoken=", voice.linesSpoken(), " last=", voice.current().key);
    CHECK(voice.linesSpoken() > 0);
}

TEST_CASE("the same walk, run twice, says exactly the same things") {
    const auto walkOnce = []() {
        render::SessionConfig config;
        config.contentDir = content::contentDir();
        config.timeOfDay = 16 * 3600;
        render::Session session(config);
        std::vector<std::string> heard;
        sim::MoveInput walk;
        walk.forward = 1;
        std::string last;
        for (int i = 0; i < 40 * kStepsPerSecond; ++i) {
            walk.turn = (i / 90) % 4 == 3 ? 1 : 0;
            session.step(walk);
            const render::VoiceLine& line = session.voice().current();
            const std::string now = line.name + ": " + line.text;
            if (!line.empty() && now != last) {
                heard.push_back(now);
                last = now;
            }
        }
        return heard;
    };
    const std::vector<std::string> first = walkOnce();
    const std::vector<std::string> second = walkOnce();
    REQUIRE_FALSE(first.empty());
    CHECK(first == second);
}

// ===========================================================================
// THE STREET IS NOT A WALL OF TEXT
// ===========================================================================

TEST_CASE("two pass lines are never closer than the channel's cooldown, and one mouth waits two minutes") {
    render::SessionConfig config;
    config.contentDir = content::contentDir();
    config.timeOfDay = 16 * 3600;
    render::Session session(config);
    sim::MoveInput walk;
    walk.forward = 1;
    std::vector<render::VoiceLine> passes;
    std::int32_t lastStep = -1;
    for (int i = 0; i < 60 * kStepsPerSecond; ++i) {
        walk.turn = (i / 90) % 4 == 3 ? 1 : 0;
        session.step(walk);
        const render::VoiceLine& line = session.voice().lastOn(render::VoiceChannel::Pass);
        if (!line.empty() && line.step != lastStep) {
            passes.push_back(line);
            lastStep = line.step;
        }
    }
    INFO("pass lines in a minute: ", passes.size());
    CHECK(passes.size() >= 1);
    for (std::size_t i = 1; i < passes.size(); ++i) {
        INFO(passes[i - 1].name, " then ", passes[i].name);
        CHECK(passes[i].step - passes[i - 1].step >= render::kPassCooldownSteps);
        if (passes[i].actorId == passes[i - 1].actorId) {
            CHECK(passes[i].step - passes[i - 1].step >= render::kPassSpeakerCooldownSteps);
        }
    }
    // Every pass line is off a pass key, in the speaker's own name.
    for (const render::VoiceLine& line : passes) {
        CHECK(startsWith(line.key, "pass"));
        CHECK_FALSE(line.name.empty());
        CHECK(line.channel == render::VoiceChannel::Pass);
    }
}

// ===========================================================================
// LEAVING IS A TRANSITION, NOT A CUT
// ===========================================================================

TEST_CASE("LEAVE carries a goodbye out of a farewell table, and the back key says one too") {
    DialogueDirector director = DialogueDirector::load(content::contentDir());
    Speaker docker;
    docker.actorId = 4;
    docker.name = "Wick Hempson";
    docker.family = JobFamily::Serf;
    REQUIRE(director.open(docker, hourOfDay(21)));
    CHECK(director.farewellKey() == "farewell.serf");
    const std::string goodbye = director.farewell();
    CHECK_FALSE(goodbye.empty());
    CHECK(goodbye != "...");
    const std::vector<std::string>& rows = *barks().rows("farewell.serf");
    CHECK(std::find(rows.begin(), rows.end(), goodbye) != rows.end());
    // The walk-off is another row, from the away cell.
    CHECK(director.farewellKey(true) == "farewell.away.serf");
    CHECK(director.farewell(true) != goodbye);
    // LEAVE's own reply carries it, and closes.
    std::size_t leave = 0;
    for (std::size_t i = 0; i < director.topics().size(); ++i) {
        if (director.topics()[i].kind == TopicKind::Leave) {
            leave = i;
        }
    }
    const Reply reply = director.choose(leave);
    CHECK(reply.ok);
    CHECK(reply.closes);
    CHECK(reply.line == goodbye);
    CHECK_FALSE(director.isOpen());
    // A hostile docker's goodbye: no serf cell is authored at hostile, so the
    // chain borrows the serf's OWN cold cell before the generic hostile row
    // -- the family outranks the tier, and cold is the nearest neighbour.
    director.ledger().record(4, Deed::Robbed);
    REQUIRE(director.open(docker, hourOfDay(21)));
    CHECK(director.farewellKey() == "farewell.serf.cold");
    director.close();
    // A hostile keeper of beasts has neither, and lands on the generic one.
    Speaker keeper;
    keeper.actorId = kWardSpeakerIdBase + 40;
    keeper.name = "Old Cobb";
    keeper.family = JobFamily::Husbandry;
    keeper.typeKey = "keeper";
    director.ledger().record(keeper.actorId, Deed::Robbed);
    REQUIRE(director.open(keeper, hourOfDay(21)));
    CHECK(director.farewellKey() == "farewell.hostile");
    director.close();
    // A beast has none.
    Speaker cat;
    cat.actorId = 1000009;
    cat.name = "Sprat";
    cat.family = JobFamily::Beast;
    cat.beast = true;
    REQUIRE(director.open(cat, hourOfDay(21)));
    CHECK(director.farewell().empty());
    director.close();
}

TEST_CASE("an urchin's goodbye is the urchin's, and WHAT THE WARD SAYS is in the trade's own mouth") {
    DialogueDirector director = DialogueDirector::load(content::contentDir());
    Speaker urchin;
    urchin.actorId = kWardSpeakerIdBase + 12;
    urchin.name = "Tatter";
    urchin.family = JobFamily::Wastrel;
    urchin.typeKey = "urchin";
    REQUIRE(director.open(urchin, hourOfDay(13)));
    CHECK(director.farewellKey() == "farewell.urchin");
    // The rumor door resolves the family first and the owner's gossip after.
    CHECK(rumorChain(JobFamily::Wastrel) == std::vector<std::string>{"rumor.wastrel", "gossip"});
    bool sawRumor = false;
    // The WardTalk topic sits in the PERSON branch; walk the tree to it.
    for (std::size_t i = 0; i < director.topics().size(); ++i) {
        if (director.topics()[i].kind == TopicKind::Ask) {
            director.choose(i);
            break;
        }
    }
    for (std::size_t i = 0; i < director.topics().size(); ++i) {
        if (director.topics()[i].kind == TopicKind::Category &&
            director.topics()[i].arg == "person") {
            director.choose(i);
            break;
        }
    }
    for (const Topic& topic : director.topics()) {
        if (topic.kind == TopicKind::WardTalk) {
            CHECK(topic.barkKey == "rumor.wastrel");
            sawRumor = true;
        }
    }
    CHECK(sawRumor);
    director.close();
}

TEST_CASE("on the street, LEAVE puts the goodbye on the voice row in the speaker's name, and walking off ends the talk") {
    render::SessionConfig config;
    config.contentDir = content::contentDir();
    config.timeOfDay = 13 * 3600;
    render::Session session(config);
    const WardActor* target = streetCorner(session);
    REQUIRE(target != nullptr);
    const std::int32_t who = target->id;
    session.body().placeAt(target->x + 1, target->y, target->band);
    session.stepMany(sim::MoveInput{}, 1);
    session.interact();
    REQUIRE(session.talking());
    REQUIRE(session.wardTalkingTo() == who);
    const std::string name = session.tavern().dialogue().speaker().name;

    // LEAVE by its own topic.
    std::size_t leave = 0;
    const std::vector<Topic>& topics = session.tavern().dialogue().topics();
    for (std::size_t i = 0; i < topics.size(); ++i) {
        if (topics[i].kind == TopicKind::Leave) {
            leave = i;
        }
    }
    session.chooseTopic(leave);
    CHECK_FALSE(session.talking());
    const render::VoiceLine& goodbye = session.voice().current();
    CHECK(goodbye.channel == render::VoiceChannel::Farewell);
    CHECK(startsWith(goodbye.key, "farewell"));
    CHECK(goodbye.name == name);
    CHECK_FALSE(goodbye.text.empty());
    // Never on the amber alert row: a goodbye is not a warning.
    CHECK(session.lastMessage().find(goodbye.text) == std::string::npos);
    // And the HUD carries it once the panel has closed.
    session.stepMany(sim::MoveInput{}, render::kPageEaseSteps * 2 + 2);
    CHECK(session.voice().up());

    // THE WALK-OFF. Talk again, then put the body five tiles off: the next
    // step ends it with the word thrown after you.
    session.body().placeAt(target->x + 1, target->y, target->band);
    session.stepMany(sim::MoveInput{}, 1);
    session.interact();
    REQUIRE(session.talking());
    const std::int32_t farX = target->x + 7;
    if (session.tiles().standable(farX, target->y, target->band)) {
        session.body().placeAt(farX, target->y, target->band);
    } else {
        session.body().placeAt(target->x + 1, target->y + 7, target->band);
    }
    session.stepMany(sim::MoveInput{}, 1);
    CHECK_FALSE(session.talking());
    CHECK(session.wardTalkingTo() == -1);
    const render::VoiceLine& thrown = session.voice().lastOn(render::VoiceChannel::Farewell);
    CHECK(startsWith(thrown.key, "farewell.away"));
    CHECK(thrown.name == name);
}

// ===========================================================================
// THE ADDRESS
// ===========================================================================

TEST_CASE("the one you talk to in the Gull holds still and faces you, and moves again when it ends") {
    render::SessionConfig config;
    config.contentDir = content::contentDir();
    config.timeOfDay = 20 * 3600;
    render::Session session(config);
    // Somebody drinking, reached by the talk key from a tile beside him --
    // the key talks to the NEAREST body within reach, so a stool beside the
    // bar would reach the bartender first; the search stands on each side
    // of each patron until the key answers with him.
    std::int32_t who = -1;
    for (const Actor& actor : session.tavern().actors()) {
        if (!actor.present() || actor.role() != ActorRole::Patron ||
            actor.activity() != Activity::Drinking) {
            continue;
        }
        const std::int32_t sides[4][2] = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}};
        for (const auto& side : sides) {
            const std::int32_t x = actor.tileX() + side[0];
            const std::int32_t y = actor.tileY() + side[1];
            if (!session.tiles().standable(x, y, actor.band())) {
                continue;
            }
            session.placeBodyAt(x, y, actor.band());
            session.stepMany(sim::MoveInput{}, 1);
            session.interact();
            if (session.talking() && session.tavern().talkingTo() == actor.id()) {
                who = actor.id();
                break;
            }
            session.closeConversation();
        }
        if (who >= 0) {
            break;
        }
    }
    REQUIRE(who >= 0);
    REQUIRE(session.talking());
    REQUIRE(session.tavern().talkingTo() == who);
    CHECK(session.tavern().holdsForTalk(*session.tavern().actorById(who)));
    const std::int32_t x = session.tavern().actorById(who)->x();
    const std::int32_t y = session.tavern().actorById(who)->y();
    // Three seconds of the room running: the schedule keeps him where the
    // talk found him.
    session.stepMany(sim::MoveInput{}, 3 * kStepsPerSecond);
    CHECK(session.tavern().actorById(who)->x() == x);
    CHECK(session.tavern().actorById(who)->y() == y);
    session.closeConversation();
    CHECK_FALSE(session.tavern().holdsForTalk(*session.tavern().actorById(who)));
    // The back key said goodbye.
    const render::VoiceLine& goodbye = session.voice().lastOn(render::VoiceChannel::Farewell);
    CHECK(startsWith(goodbye.key, "farewell"));
    CHECK(goodbye.name == session.tavern().actorById(who)->name());
}

TEST_CASE("a witness to a blow turns to look at it, and a witness to a bought drink does not") {
    // THE REACT FRAME'S OWN RULE, in the room: Tavern::spreadWitness turns
    // every body that SAW violence (Struck, DrewSteel, Slew) toward it --
    // one at its post, drinking, working or watching -- and turns nobody
    // for a bought drink or a lifted purse, so the gate's tavern driver
    // (which buys and talks and never swings) hashes as it always did. The
    // critic's frame had the witness in profile to the bar while his line
    // said he saw it.
    render::SessionConfig config;
    config.contentDir = content::contentDir();
    config.timeOfDay = 20 * 3600;
    render::Session session(config);
    session.placeBodyAt(gull::kBartenderX, gull::kBartenderY + 1, gull::kGroundBand);
    session.stepMany(sim::MoveInput{}, kStepsPerSecond);
    // A drinking patron at his stool who is NOT the one about to be hit and
    // who can see the player: he keeps his stool's facing until the blow.
    const sim::Actor* mark = session.tavern().nearestTo(session.body().x(), session.body().y(),
                                                        2 * kSubOne);
    REQUIRE(mark != nullptr);
    std::int32_t witnessId = -1;
    Angle before = 0;
    for (const Actor& actor : session.tavern().actors()) {
        if (!actor.present() || actor.id() == mark->id() || actor.role() != ActorRole::Patron ||
            actor.activity() != Activity::Drinking || !session.tavern().noticeBy(actor).seen) {
            continue;
        }
        witnessId = actor.id();
        before = actor.facing();
        break;
    }
    REQUIRE(witnessId >= 0);
    const sim::Actor* witness = session.tavern().actorById(witnessId);
    const Angle towardPlayer = sim::bearingTo(witness->tileX(), witness->tileY(),
                                              session.body().tileX(), session.body().tileY());
    // A fist on the man beside you, in his sight -- the Attack key's own tap,
    // the mark on the crosshair, until one lands (his sheet moves): Struck
    // spreads, and the witness turns to it.
    const std::int32_t markId = mark->id();
    bool landed = false;
    for (int attempt = 0; attempt < 16 && !landed; ++attempt) {
        const sim::Actor* now = session.tavern().actorById(markId);
        REQUIRE(now != nullptr);
        session.body().setYaw(sim::bearingTo(session.body().tileX(), session.body().tileY(),
                                             now->tileX(), now->tileY()));
        session.stepMany(sim::MoveInput{}, 1);
        const std::int32_t hpBefore = now->hp();
        session.punch();
        const sim::Actor* struck = session.tavern().actorById(markId);
        landed = struck != nullptr && (struck->hp() < hpBefore || isFloored(struck->activity()));
        session.stepMany(sim::MoveInput{}, kSwingRecoverySteps + 1);
    }
    REQUIRE(landed);
    witness = session.tavern().actorById(witnessId);
    REQUIRE(witness != nullptr);
    INFO("witness ", witness->name(), " before=", before, " after=", witness->facing(),
         " toward=", towardPlayer);
    // Eight-point facing, so "toward" is the nearest of the eight to the
    // bearing: within an eighth of a turn.
    std::int32_t off = static_cast<std::int32_t>((witness->facing() - towardPlayer) & (kTurnFull - 1));
    if (off > kTurnFull / 2) {
        off -= kTurnFull;
    }
    CHECK(std::abs(off) <= kTurnFull / 8);
    const sim::Memory* memory = session.tavern().dialogue().ledger().memoryOf(witnessId);
    REQUIRE(memory != nullptr);
    CHECK(memory->lastDeed == Deed::Struck);

    // AND THE OTHER HALF, which is what keeps the gate's own tavern workload
    // (it buys and talks and never swings) hashing as it always did: a
    // BOUGHT DRINK spreads through the same door and turns nobody. Every
    // facing in the room is what it was, bar the one you are talking to --
    // talkTo's own rule, older than this lane.
    render::SessionConfig quiet;
    quiet.contentDir = content::contentDir();
    quiet.timeOfDay = 20 * 3600;
    render::Session peaceable(quiet);
    peaceable.placeBodyAt(gull::kBartenderX, gull::kBartenderY + 1, gull::kGroundBand);
    peaceable.stepMany(sim::MoveInput{}, kStepsPerSecond);
    peaceable.interact();
    REQUIRE(peaceable.talking());
    const std::int32_t partner = peaceable.tavern().talkingTo();
    std::vector<std::pair<std::int32_t, Angle>> facings;
    for (const Actor& actor : peaceable.tavern().actors()) {
        if (actor.present() && actor.id() != partner) {
            facings.emplace_back(actor.id(), actor.facing());
        }
    }
    REQUIRE(facings.size() > 4);
    std::size_t buy = peaceable.tavern().dialogue().topics().size();
    for (std::size_t i = 0; i < peaceable.tavern().dialogue().topics().size(); ++i) {
        if (peaceable.tavern().dialogue().topics()[i].kind == TopicKind::BuyDrinkFor) {
            buy = i;
            break;
        }
    }
    REQUIRE(buy < peaceable.tavern().dialogue().topics().size());
    peaceable.chooseTopic(buy);
    const sim::Memory* bought = peaceable.tavern().dialogue().ledger().memoryOf(partner);
    INFO("partner ", partner, " deed=", bought == nullptr ? -1 : static_cast<int>(bought->lastDeed));
    for (const auto& [id, was] : facings) {
        const sim::Actor* now = peaceable.tavern().actorById(id);
        REQUIRE(now != nullptr);
        INFO("actor ", now->name(), " was=", was, " now=", now->facing());
        CHECK(now->facing() == was);
    }
}

TEST_CASE("the worded reaction is never a number, and it stands on the panel's dateline row") {
    CHECK(render::wordedReaction(0, 0).empty());
    CHECK(render::wordedReaction(0, 2) == "(THE FACE SOFTENS A HAIR)");
    CHECK(render::wordedReaction(0, 6) == "(A NOD. THAT WENT DOWN WELL)");
    CHECK(render::wordedReaction(0, -2) == "(THAT LANDED BADLY)");
    CHECK(render::wordedReaction(0, -6) == "(THE SHOULDER TURNS)");
    // A band crossed names the band in the header's own word.
    CHECK(render::wordedReaction(18, 21) == "(THAT WENT DOWN WELL. WARM NOW)");
    CHECK(render::wordedReaction(-9, -12) == "(THAT LANDED BADLY. COLD NOW)");
    for (const std::int32_t before : {-100, -50, -12, -9, 0, 18, 21, 60, 90}) {
        for (const std::int32_t delta : {-45, -8, -2, 1, 2, 6, 12}) {
            const std::string row = render::wordedReaction(before, before + delta);
            INFO(before, " -> ", before + delta, ": ", row);
            for (const char c : row) {
                const bool digit = c >= '0' && c <= '9';
                CHECK_FALSE(digit);
                CHECK(render::isDrawableGlyph(c));
            }
        }
    }

    // Through the session: hear a rope hand out and the row stands.
    render::SessionConfig config;
    config.contentDir = content::contentDir();
    config.timeOfDay = 13 * 3600;
    render::Session session(config);
    const WardActor* target = streetCorner(session);
    REQUIRE(target != nullptr);
    session.body().placeAt(target->x + 1, target->y, target->band);
    session.stepMany(sim::MoveInput{}, 1);
    session.interact();
    REQUIRE(session.talking());
    CHECK(session.reactionLine().empty());
    const std::vector<Topic>& topics = session.tavern().dialogue().topics();
    std::size_t heard = topics.size();
    for (std::size_t i = 0; i < topics.size(); ++i) {
        if (topics[i].kind == TopicKind::Quest || topics[i].kind == TopicKind::Personal) {
            heard = i;
            break;
        }
    }
    REQUIRE(heard < topics.size());
    session.chooseTopic(heard);
    CHECK(session.reactionLine() == "(THE FACE SOFTENS A HAIR)");
    CHECK(session.dialogueView().caseRef == session.reactionLine());
    // It stands four seconds and goes.
    session.stepMany(sim::MoveInput{}, 4 * kStepsPerSecond + 1);
    CHECK(session.reactionLine().empty());
    CHECK(session.dialogueView().caseRef.empty());
}

// ===========================================================================
// THE WATCH READS YOU BEFORE IT GREETS YOU
// ===========================================================================

TEST_CASE("a stranger's attitude is the ward's, and a passing watchman reads paper before heat before eyes") {
    DialogueDirector director = DialogueDirector::load(content::contentDir());
    // A stranger reads the ward's opinion; a man you robbed reads his own.
    CHECK(render::attitudeToward(director.ledger(), 99) == director.ledger().wardAttitude());
    director.ledger().record(4, Deed::Robbed);
    CHECK(render::attitudeToward(director.ledger(), 4) == Attitude::Hostile);

    // A STREET watchman at one in the afternoon, the player put in front of
    // him with paper out: his passing word is the paper's, and nothing else
    // happens -- the street Watch has no arrest to make (the STREET SENSES
    // lane's ceiling), so the line is a demand and only a demand. In the
    // Gull the same paper is Cull's cause and he closes, which is the court
    // build's own case and not this one's.
    render::SessionConfig config;
    config.contentDir = content::contentDir();
    config.timeOfDay = 13 * 3600;
    render::Session session(config);
    session.tavern().dialogue().crimes().addHeat(70);
    REQUIRE(session.tavern().dialogue().crimes().warrant());
    bool spoken = false;
    std::string spokenName;
    for (const WardActor& actor : session.people().actors()) {
        if (actor.type != WardType::MilitiaWatch || !actor.visible()) {
            continue;
        }
        const std::int32_t sides[4][2] = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}};
        for (const auto& side : sides) {
            const std::int32_t x = actor.x + side[0];
            const std::int32_t y = actor.y + side[1];
            if (!session.tiles().standable(x, y, actor.band)) {
                continue;
            }
            session.body().placeAt(x, y, actor.band);
            // A second in front of him: the line lands the step his eyes
            // do, or not at all from this side.
            session.stepMany(sim::MoveInput{}, kStepsPerSecond);
            const render::VoiceLine& word = session.voice().lastOn(render::VoiceChannel::Watch);
            if (!word.empty()) {
                INFO("watch line: ", word.key, " ", word.text);
                CHECK(word.key == "watch.paper");
                CHECK(word.name == session.people().identity(actor.id).name);
                spoken = true;
                spokenName = word.name;
                break;
            }
        }
        if (spoken) {
            break;
        }
    }
    CHECK(spoken);
    CHECK_FALSE(session.tavern().lastArrest().happened);
    CHECK_FALSE(spokenName.empty());
}

// ===========================================================================
// THE SIX ENDINGS LAND, THROUGH THE REAL VERBS
// ===========================================================================

TEST_CASE("--voice=pass: a body passing on the Tarwalk says its one line, off a pass key") {
    const render::SmokeRunResult played = playVoice("pass");
    INFO(played.summary);
    CHECK(played.ok);
    CHECK_FALSE(played.scriptFellShort());
    CHECK(played.voiceBeats == 2);
    CHECK(startsWith(played.voiceKey, "pass"));
    CHECK_FALSE(played.voiceSpeaker.empty());
    CHECK_FALSE(played.voiceRow.empty());
    // THE FRAME IS OF THE SPEAKER: the look turned to whoever spoke, and
    // they are in the lens -- a passer-by, so within the talk reach when the
    // row landed and a few tiles further on by the time the row's fade has
    // settled and the shot is taken (a second of walking at most).
    CHECK(played.voiceSpeakerInView);
    CHECK(played.voiceSpeakerTenths >= 0);
    CHECK(played.voiceSpeakerTenths <= 10 * (render::kPassReachTiles + 2));
    // AND COMPOSED, not merely in view: OFF THE PLATE and inside the frame.
    // The crosshair's own name plate is drawn at the middle of the frame, so
    // a subject dead centre wears it (and the prompt names the nearest body
    // in REACH, which is not always the one talking); a composed look stands
    // its subject a sixth of the frame off centre. Between a tenth of the
    // frame and a third of it -- a body further out than that is heading for
    // the edge.
    CHECK(played.voiceSpeakerOffDeg >= 8);
    CHECK(played.voiceSpeakerOffDeg <= 32);
}

TEST_CASE("--voice=overheard: two bodies standing together are overheard, off an overheard key") {
    const render::SmokeRunResult played = playVoice("overheard");
    INFO(played.summary);
    CHECK(played.ok);
    CHECK_FALSE(played.scriptFellShort());
    CHECK(played.voiceBeats == 2);
    CHECK(startsWith(played.voiceKey, "overheard"));
    // The look is at the MIDPOINT of the pair and the plate has to clear the
    // whole group, so the speaker sits further out than a lone subject does
    // -- still inside the frame's own half-angle with room to spare.
    CHECK(played.voiceSpeakerInView);
    CHECK(played.voiceSpeakerOffDeg >= 8);
    CHECK(played.voiceSpeakerOffDeg <= 38);
}

TEST_CASE("--voice=farewell: the street line, then LEAVE, and the goodbye on the row as the panel closes") {
    const render::SmokeRunResult played = playVoice("farewell");
    INFO(played.summary);
    CHECK(played.ok);
    CHECK_FALSE(played.scriptFellShort());
    CHECK(played.voiceBeats == 3);
    CHECK(startsWith(played.voiceKey, "farewell"));
    CHECK_FALSE(played.talking);
    // THE FRAME IS OF THE ONE WHO SAID GOODBYE, and from far enough to be a
    // person and not a hairline: the composed stand is never the tile beside
    // them (the lens in their hair, the critic's rope-hand frames) -- a tile
    // and a half at the least, the talk reach at the most.
    CHECK(played.voiceSpeakerInView);
    CHECK(played.voiceSpeakerOffDeg >= 8);
    CHECK(played.voiceSpeakerOffDeg <= 32);
    CHECK(played.voiceSpeakerTenths >= 14);
    CHECK(played.voiceSpeakerTenths <= 29);
}

TEST_CASE("--voice=moved: a reply that moved them shows a worded reaction on the panel") {
    const render::SmokeRunResult played = playVoice("moved");
    INFO(played.summary);
    CHECK(played.ok);
    CHECK_FALSE(played.scriptFellShort());
    CHECK(played.voiceBeats == 3);
    CHECK(played.talking);
    CHECK(played.summary.find("reaction=\"(") != std::string::npos);
    // With the panel open the frame is about the one you are talking to:
    // in the lens, at a talking distance.
    CHECK(played.voiceSpeakerInView);
    CHECK(played.voiceSpeakerOffDeg >= 8);
    CHECK(played.voiceSpeakerOffDeg <= 32);
    CHECK(played.voiceSpeakerTenths >= 14);
    CHECK(played.voiceSpeakerTenths <= 29);
}

TEST_CASE("--voice=react: a witness to a tap in the Gull says so, and it is not the man hit") {
    const render::SmokeRunResult played = playVoice("react");
    INFO(played.summary);
    CHECK(played.ok);
    CHECK_FALSE(played.scriptFellShort());
    CHECK(played.voiceBeats == 3);
    CHECK(startsWith(played.voiceKey, "react."));
    CHECK(played.voiceChannel == "react");
    // The shot turned to the witness: he is in it, and in the middle of it.
    CHECK(played.voiceSpeakerInView);
    CHECK(played.voiceSpeakerOffDeg >= 8);
    CHECK(played.voiceSpeakerOffDeg <= 32);
}

TEST_CASE("--voice=watch: steel across the room from Cull, his halt, and his readback as he crosses it, in his name and never beside his halt") {
    const render::SmokeRunResult played = playVoice("watch");
    INFO(played.summary);
    CHECK(played.ok);
    CHECK_FALSE(played.scriptFellShort());
    CHECK(played.voiceBeats == 4);
    CHECK(startsWith(played.voiceKey, "watch.approach.violence"));
    CHECK(played.voiceSpeaker == "Watchman Cull");
    // ONE MAN, ONE LINE: with his readback on the voice row, the alert row
    // is not quoting him -- the halt left the row before the readback
    // followed, or stood down the step the readback replaced it (the
    // summary reads the row after its fade, kPageEaseSteps on).
    CHECK(played.voiceAlertSpeaker != "Watchman Cull");
    // And the readback is worded by what is up: the blade is drawn, so it is
    // the steel key, and no "hands down" over a STEEL UP row.
    CHECK(played.voiceKey == "watch.approach.violence.steel");
    CHECK(lower(played.voiceRow).find("hands down") == std::string::npos);
    // THE REVEAL IS OF HIM. The stand was chosen across the room (three
    // tiles from his stool at the least -- the summary prints it) and the
    // lens has him at the shutter: in the field of view, a clear line, nobody
    // in the way. The two earlier passes at this frame turned the camera
    // onto the west face of the table at (151,69); see the case below on why
    // the sim's own line of sight said otherwise.
    CHECK(played.voiceSpeakerInView);
    // OFF THE BLADE as well as off the plate, and deliberately so: the drawn
    // sword crosses the middle of the frame, so he stands a third of the way
    // out of it and not at the edge -- between ten and twenty-five degrees.
    CHECK(played.voiceSpeakerOffDeg >= 10);
    CHECK(played.voiceSpeakerOffDeg <= 25);
    const bool acrossTheRoom = played.summary.find(" apart=5") != std::string::npos ||
                               played.summary.find(" apart=4") != std::string::npos ||
                               played.summary.find(" apart=3") != std::string::npos;
    CHECK(acrossTheRoom);
}

TEST_CASE("the lens's line is not the sim's: the taproom table at (151,69) lets Cull see past its corner and stops a camera dead") {
    // The watch reveal's own defect, held still. The pocket at (150,69)
    // between the tables at (148,69) and (151,69) is where the feel build's
    // Watch line stands the player (beside Wick Hempson's stool at (150,70)),
    // and Cull's stool is at (152,68). TileQuery::lineOfSight walks tile
    // centres and passes a lattice corner when only one grazed cell is solid
    // -- so Cull SEES the player there, which is the right rule between
    // people. The camera at the tile's centre looking at his, drawing every
    // solid cell as a block, looks into the table's west face -- and
    // cameraSightClear says so. Both rules are pinned; the second is the
    // one a composed shot has to ask.
    render::SessionConfig config;
    config.contentDir = content::contentDir();
    config.timeOfDay = 23 * 3600;
    render::Session session(config);
    const sim::TileQuery& tiles = session.tiles();
    REQUIRE(tiles.solid(151, 69, gull::kGroundBand));
    REQUIRE_FALSE(tiles.solid(150, 68, gull::kGroundBand));
    REQUIRE_FALSE(tiles.solid(151, 68, gull::kGroundBand));
    CHECK(tiles.lineOfSight(152, 68, 150, 69, gull::kGroundBand));
    CHECK_FALSE(render::cameraSightClear(tiles, q8_tile_centre(150), q8_tile_centre(69),
                                         q8_tile_centre(152), q8_tile_centre(68),
                                         gull::kGroundBand));
    // Along the open row north of the tables the two agree: the reveal's own
    // stand at (147,67) has him in the clear, whole body, all the way to his
    // stool at (152,68).
    CHECK(tiles.lineOfSight(152, 68, 147, 67, gull::kGroundBand));
    CHECK(render::cameraSightClear(tiles, q8_tile_centre(147), q8_tile_centre(67),
                                   q8_tile_centre(152), q8_tile_centre(68), gull::kGroundBand));
    // Symmetric, as a line is, and a point sees itself.
    CHECK(render::cameraSightClear(tiles, q8_tile_centre(152), q8_tile_centre(68),
                                   q8_tile_centre(147), q8_tile_centre(67), gull::kGroundBand));
    CHECK(render::cameraSightClear(tiles, q8_tile_centre(147), q8_tile_centre(67),
                                   q8_tile_centre(147), q8_tile_centre(67), gull::kGroundBand));
    // And a body walking along a wall is seen by the half rule where the
    // whole rule loses a shoulder: straight along the north wall's face,
    // from (147,67) to (155,67), the middle and the room side are clear.
    CHECK(render::cameraSightClear(tiles, q8_tile_centre(147), q8_tile_centre(67),
                                   q8_tile_centre(155), q8_tile_centre(67), gull::kGroundBand,
                                   false));
}

TEST_CASE("a placed body leaves the street's last word behind it: the voice row stands down on a relocation") {
    // A line is what a body within reach said; a jump (a travel, a waking, a
    // scripted stand) leaves it with the street it was said on. The channel's
    // memory of who spoke and when survives the jump -- a journey is not an
    // amnesty for a mouth's two minutes.
    render::SessionConfig config;
    config.contentDir = content::contentDir();
    config.timeOfDay = 16 * 3600;
    render::Session session(config);
    sim::MoveInput input;
    input.forward = 1;
    bool spoke = false;
    for (int i = 0; i < 90 * kStepsPerSecond && !spoke; ++i) {
        input.turn = (i / 90) % 4 == 3 ? 1 : 0;
        session.step(input);
        spoke = session.voice().up();
    }
    REQUIRE(spoke);
    const render::VoiceLine before = session.voice().current();
    const std::int32_t said = session.voice().linesSpoken();
    session.placeBodyAt(39, 75, docks::kBandQuayside);
    CHECK_FALSE(session.voice().up());
    CHECK(session.voice().current().empty());
    CHECK(session.voice().linesSpoken() == said);
    // The row's last word on its channel is still on record.
    CHECK(session.voice().lastOn(before.channel).text == before.text);
}

TEST_CASE("--voice=hold: the one you talk to in the Gull holds ten seconds and is drawn standing") {
    const render::SmokeRunResult played = playVoice("hold");
    INFO(played.summary);
    CHECK(played.ok);
    CHECK_FALSE(played.scriptFellShort());
    CHECK(played.voiceBeats == 2);
    CHECK(played.talking);
}

// ===========================================================================
// THE HUD ROW
// ===========================================================================

TEST_CASE("the voice row draws its speaker and its line, and a hand-built HudState without it is untouched") {
    render::Framebuffer plain(960, 540);
    render::HudState quiet;
    render::drawHud(plain, quiet);
    render::Framebuffer spoken(960, 540);
    render::HudState loud;
    loud.voiceName = "Gerta Saltcotte";
    loud.voiceLine = "Mind the step on your way out.";
    loud.voiceFade = 1.0F;
    render::drawHud(spoken, loud);
    // Something was drawn, and only in the bottom band under the centre.
    const render::CentreRect centre = render::hudCentreRect(960, 540);
    bool differs = false;
    for (int y = 0; y < 540; ++y) {
        for (int x = 0; x < 960; ++x) {
            if (plain.pixels()[plain.index(x, y)] != spoken.pixels()[spoken.index(x, y)]) {
                differs = true;
                CHECK(y >= centre.y1);
            }
        }
    }
    CHECK(differs);
    // A zero fade draws nothing at all.
    render::Framebuffer faded(960, 540);
    render::HudState gone = loud;
    gone.voiceFade = 0.0F;
    render::drawHud(faded, gone);
    CHECK(plain.pixels() == faded.pixels());
}
