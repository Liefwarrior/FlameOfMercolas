#include "granadad/render/pull.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

#include "granadad/sim/angle.hpp"
#include "granadad/sim/stealth.hpp"

namespace granadad::render {

namespace {

[[nodiscard]] std::string shout(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return out;
}

[[nodiscard]] std::int32_t openLeadOf(const BookSet& books, CaseBookId id) noexcept {
    // A CLOSED BOOK PULLS NOWHERE: its unread leads are pages, not places --
    // the case is over. Only a live book's newest lead is somewhere to go.
    const sim::Casebook* book = books.book(id);
    if (book == nullptr || !books.live(id)) {
        return -1;
    }
    return newestOpenLead(*book);
}

}  // namespace

std::int32_t newestOpenLead(const sim::Casebook& book) noexcept {
    if (!book.active()) {
        return -1;
    }
    std::int32_t best = -1;
    std::int64_t when = -1;
    for (const std::int32_t index : book.known()) {
        if (book.state(index) != sim::LeadState::Open) {
            continue;
        }
        // STRICTLY NEWER wins; a tie keeps the earlier, which is authored
        // order -- known() is authored order and never sorted.
        const std::int64_t heard = book.heardAt(index);
        if (best < 0 || heard > when) {
            best = index;
            when = heard;
        }
    }
    return best;
}

bool BookSet::begun(CaseBookId id) const noexcept {
    const sim::Casebook* book = this->book(id);
    const sim::CasebookRaws* raw = this->raw(id);
    return book != nullptr && raw != nullptr && book->active() && raw->loaded() &&
           !book->known().empty();
}

bool BookSet::live(CaseBookId id) const noexcept {
    return begun(id) && !book(id)->closed();
}

CaseBookId autoFrontedBook(const BookSet& books) noexcept {
    // Session::activeCaseRaws()'s own precedence: the eviction begins by an
    // explicit deed mid-play, so it is always the newest thing the player
    // did; then the courier's errand; then the Bloodletter.
    if (books.live(CaseBookId::Eviction)) {
        return CaseBookId::Eviction;
    }
    if (books.live(CaseBookId::Courier)) {
        return CaseBookId::Courier;
    }
    return CaseBookId::Bloodletter;
}

PullTarget resolvePull(const FollowedLead& followed, const BookSet& books,
                       CaseBookId fronted) noexcept {
    PullTarget out;
    // THE CHOICE, while it still points at somewhere to go.
    if (followed.chosen()) {
        const sim::Casebook* book = books.book(followed.book);
        const sim::CasebookRaws* raw = books.raw(followed.book);
        if (book != nullptr && raw != nullptr && books.live(followed.book) &&
            static_cast<std::size_t>(followed.lead) < raw->leads().size() &&
            book->state(followed.lead) == sim::LeadState::Open) {
            out.set = true;
            out.book = followed.book;
            out.lead = followed.lead;
            out.chosen = true;
            return out;
        }
    }
    // THE AUTHORED DEFAULT: the fronted book's next open lead...
    if (const std::int32_t lead = openLeadOf(books, fronted); lead >= 0) {
        out.set = true;
        out.book = fronted;
        out.lead = lead;
        return out;
    }
    // ...then any begun book's, in the auto order, so a closed courier case
    // fronted for rereading still leaves the Bloodletter on the street.
    for (const CaseBookId id :
         {CaseBookId::Eviction, CaseBookId::Courier, CaseBookId::Bloodletter}) {
        if (id == fronted) {
            continue;
        }
        if (const std::int32_t lead = openLeadOf(books, id); lead >= 0) {
            out.set = true;
            out.book = id;
            out.lead = lead;
            return out;
        }
    }
    return out;
}

std::string pullBearing(std::int32_t px, std::int32_t py, std::int32_t band,
                        const sim::LeadSite& site, bool here, BandWord bandWord) {
    std::string out;
    if (here) {
        // STANDING ON IT there is nothing to restate -- HERE, the map page's
        // own state label, so the verb beside it (LOOK) is the whole answer.
        out = "HERE";
    } else {
        // THE BEARING IS TO THE LEAD'S OWN SITE and not to the place's door,
        // because the site is where the key works -- the Mission's flagstones
        // and the Mission's back room are two leads in one building, and one
        // bearing to the building would be the same arrow for both.
        const double dx = static_cast<double>(site.x) - static_cast<double>(px);
        const double dy = static_cast<double>(site.y) - static_cast<double>(py);
        const std::int32_t paces =
            static_cast<std::int32_t>(std::lround(std::sqrt(dx * dx + dy * dy)));
        // `NE 40`, the map badge's own form (UI-EA-SPEC sec. 5): the number
        // stays exact, the unit word retires -- paces are the only distance
        // this game ever states, so the unit was decoration.
        out = std::string(sim::compass_point(sim::bearingTo(px, py, site.x, site.y))) + " " +
              std::to_string(paces);
    }
    if (site.band != band) {
        // A LEAD ON ANOTHER PLANE SAYS SO. Two of the twelve are one band
        // down; a bearing and a distance with no band on them would send a
        // player walking into the seawall. The book says which band; the
        // street, which cannot see its own, says which way.
        if (bandWord == BandWord::Relative) {
            out += site.band < band ? "  BELOW" : "  ABOVE";
        } else {
            out += "  BAND " + std::to_string(site.band);
        }
    }
    return out;
}

std::int32_t pullTickBam(std::int32_t px, std::int32_t py, std::int32_t tx,
                         std::int32_t ty) noexcept {
    const std::int32_t dx = tx - px;
    const std::int32_t dy = ty - py;
    if (dx == 0 && dy == 0) {
        return 0;
    }
    // North is -Y and yaw rises clockwise (angle.hpp's compass note): east
    // is +X at a quarter turn, so the angle is atan2(dx, -dy).
    const double radians = std::atan2(static_cast<double>(dx), -static_cast<double>(dy));
    const double turn = radians / (2.0 * 3.14159265358979323846);
    const long bam = std::lround(turn * 65536.0);
    return static_cast<std::int32_t>(((bam % 65536) + 65536) % 65536);
}

std::string pullLine(std::string_view bearing, std::string_view place) {
    if (bearing.empty() || place.empty()) {
        return {};
    }
    // "NE 40  THE WEIGHHOUSE", and off-plane "W 66  BRANN'S CHANDLERY  BAND
    // 18": the band rides AFTER the place on the ribbon (the page puts it
    // after the bearing inside its own parenthesis), so the two numbers a
    // walking body reads first -- the point and the paces -- stay in front.
    const std::size_t band = bearing.find("  BAND ");
    if (band == std::string_view::npos) {
        return std::string(bearing) + "  " + std::string(place);
    }
    return std::string(bearing.substr(0, band)) + "  " + std::string(place) +
           std::string(bearing.substr(band));
}

// ---------------------------------------------------------------------------
// the skill-up toast
// ---------------------------------------------------------------------------

std::vector<SkillRise> SkillRiseWatch::diff(const sim::SkillTrack& track) {
    std::vector<SkillRise> rises;
    const std::vector<sim::SkillTrack::Entry>& entries = track.entries();
    if (!seeded_ || levels_.size() != entries.size()) {
        // SEED, never report: chargen's starting levels, or a track that
        // changed shape under us, are not a rise the player earned this step.
        levels_.resize(entries.size());
        for (std::size_t i = 0; i < entries.size(); ++i) {
            levels_[i] = entries[i].level;
        }
        seeded_ = true;
        return rises;
    }
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const std::int32_t was = levels_[i];
        const std::int32_t now = entries[i].level;
        levels_[i] = now;
        if (now > was) {
            rises.push_back(SkillRise{shout(entries[i].displayName.empty() ? entries[i].id
                                                                            : entries[i].displayName),
                                      now});
        }
    }
    return rises;
}

std::string skillToastFor(const SkillRise& rise) {
    return rise.label + " RISES TO " + std::to_string(rise.level);
}

// ---------------------------------------------------------------------------
// the book news
// ---------------------------------------------------------------------------

namespace {

[[nodiscard]] std::int32_t knownCount(const BookSet& books, CaseBookId id) noexcept {
    const sim::Casebook* book = books.book(id);
    return book != nullptr && book->active() ? static_cast<std::int32_t>(book->known().size())
                                             : 0;
}

[[nodiscard]] bool closedNow(const BookSet& books, CaseBookId id) noexcept {
    const sim::Casebook* book = books.book(id);
    return book != nullptr && book->active() && book->closed();
}

[[nodiscard]] std::int32_t questsDoneCount(const sim::QuestJournal& journal) noexcept {
    std::int32_t done = 0;
    for (const sim::QuestProgress& row : journal.progress()) {
        done += row.done ? 1 : 0;
    }
    return done;
}

[[nodiscard]] std::int32_t radiantTakenCount(const sim::RadiantBoard& radiant) noexcept {
    std::int32_t taken = 0;
    for (const sim::RadiantObjective& row : radiant.objectives()) {
        taken += row.state == sim::RadiantState::Taken ? 1 : 0;
    }
    return taken;
}

}  // namespace

bool bookClosedThisStep(const BookNewsWatch& watch, const BookSet& books,
                        CaseBookId id) noexcept {
    return watch.seeded && !watch.closed[static_cast<std::size_t>(id)] && closedNow(books, id);
}

std::string diffBookNews(BookNewsWatch& watch, const BookSet& books, const sim::QuestBook& quests,
                         const sim::QuestJournal& journal, const sim::RadiantBoard& radiant,
                         bool plateArmedThisStep) {
    std::array<std::int32_t, 3> known{};
    std::array<bool, 3> closed{};
    for (int i = 0; i < kCaseBookCount; ++i) {
        const CaseBookId id = static_cast<CaseBookId>(i);
        known[static_cast<std::size_t>(i)] = knownCount(books, id);
        closed[static_cast<std::size_t>(i)] = closedNow(books, id);
    }
    const std::size_t journalLines = journal.log().size();
    const std::int32_t questsDone = questsDoneCount(journal);
    const std::int32_t radiantTaken = radiantTakenCount(radiant);
    // ONE STAGE COUNT PER AUTHORED LINE, in the book's own (id-sorted, never
    // reordered) order -- so the line that moved is the one whose count
    // rose, and its own title is the headline rather than a guess.
    std::vector<std::int32_t> stages;
    stages.reserve(quests.lines().size());
    for (const sim::Questline& line : quests.lines()) {
        stages.push_back(journal.started(line.id) ? journal.stagesDone(line.id) +
                                                        (journal.done(line.id) ? 1 : 0)
                                                  : 0);
    }

    std::string news;
    if (watch.seeded && watch.stages.size() == stages.size() && !plateArmedThisStep) {
        std::int32_t heard = 0;
        for (std::size_t i = 0; i < known.size(); ++i) {
            heard += std::max(0, known[i] - watch.known[i]);
        }
        if (heard > 0) {
            news = std::to_string(heard) + (heard == 1 ? " NEW LEAD" : " NEW LEADS");
        } else if (journalLines > watch.journalLines || questsDone > watch.questsDone) {
            for (std::size_t i = 0; i < stages.size(); ++i) {
                if (stages[i] <= watch.stages[i]) {
                    continue;
                }
                const sim::Questline& line = quests.lines()[i];
                news = shout(line.title) + (journal.done(line.id) ? " IS DONE" : " MOVES ON");
                break;
            }
        } else if (radiantTaken > watch.radiantTaken) {
            news = "AN ERRAND TAKEN";
        }
    }
    watch.known = known;
    watch.closed = closed;
    watch.journalLines = journalLines;
    watch.questsDone = questsDone;
    watch.radiantTaken = radiantTaken;
    watch.stages = std::move(stages);
    watch.seeded = true;
    return news;
}

}  // namespace granadad::render
