// Session casebook: cursor and tabs, leads, the pull, the rung plate and the case plates and fades.
#include "session_internal.hpp"

// THE AUDIO WIRING PASS, as in session.cpp: this file speaks to the audio engine in ids only; see
// setAudio() in session.hpp and the determinism note in audio_engine.hpp.
#include "granadad/audio/audio_engine.hpp"

namespace granadad::render {

std::int32_t Session::closedBookCount() const noexcept {
    return (casebook_.closed() ? 1 : 0) + (sheetBook_.closed() ? 1 : 0) +
           (evictBook_.closed() ? 1 : 0);
}

// ---------------------------------------------------------------------------
// S10: the investigation
// ---------------------------------------------------------------------------

sim::Legend Session::legend() const {
    const sim::DialogueDirector& talk = tavern_->dialogue();
    return sim::legendOf(talk.crimes(), talk.skills(), talk.standings(), talk.contracts(),
                         casebook_);
}

std::int64_t Session::caseNowSeconds() const noexcept {
    // TASK #82. THE STARTING HOUR PLUS EVERYTHING SIMULATED SINCE. timeOfDay_
    // wraps at kSecondsPerDay (see the constructor and skipToHour), so this is
    // NOT itself a day-and-hour pair -- it is a strictly increasing count a
    // caller turns into one, exactly the way elapsedSeconds() already is one
    // ingredient of `DAY N` today. Session never wraps this back down: two
    // moments a day apart have to compare as a day apart, which a wrapped
    // value could not do.
    return static_cast<std::int64_t>(config_.timeOfDay) + elapsedSeconds_;
}

int Session::leadInLookReach() const {
    // THE SAME WALK examine() MAKES, WITH THE HANDS KEPT STILL. See this
    // method's own header in session.hpp on why it cannot simply call look().
    //
    // Casebook::look picks the nearest lead by MANHATTAN distance within
    // kLookRangeTiles, at the body's own band, ties broken on the earlier
    // authored index; and examine() widens that by standing the look at four
    // axis offsets, one ring at a time, up to the Flame's own bonus. Both
    // halves are reproduced here rather than approximated, because a
    // crosshair that named a lead the key would not find is worse than one
    // that named nothing.
    if (casebook_.raws() == nullptr) {
        return -1;
    }
    const std::vector<sim::Lead>& leads = casebook_.raws()->leads();
    const std::int32_t band = body_->band();
    const auto nearestFrom = [&](std::int32_t fromX, std::int32_t fromY) {
        int best = -1;
        std::int32_t bestDistance = 0;
        for (std::size_t i = 0; i < leads.size(); ++i) {
            const sim::Lead& lead = leads[i];
            if (lead.site.band != band) {
                continue;
            }
            const std::int32_t dx = lead.site.x - fromX;
            const std::int32_t dy = lead.site.y - fromY;
            const std::int32_t distance = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
            if (distance > sim::kLookRangeTiles) {
                continue;
            }
            if (best < 0 || distance < bestDistance) {
                best = static_cast<int>(i);
                bestDistance = distance;
            }
        }
        return best;
    };
    const std::int32_t tileX = body_->tileX();
    const std::int32_t tileY = body_->tileY();
    if (const int here = nearestFrom(tileX, tileY); here >= 0) {
        return here;
    }
    const std::int32_t bonus = legend().lookRangeBonus();
    for (std::int32_t ring = 1; ring <= bonus; ++ring) {
        const std::int32_t offsets[4][2] = {{ring, 0}, {-ring, 0}, {0, ring}, {0, -ring}};
        for (const auto& offset : offsets) {
            if (const int found = nearestFrom(tileX + offset[0], tileY + offset[1]); found >= 0) {
                return found;
            }
        }
    }
    return -1;
}

int Session::sheetLeadInLookReach() const {
    // COURIER CASE. leadInLookReach()'s exact walk over the second book --
    // the same Manhattan pick, the same ties-to-earlier-index rule, the same
    // ring widening -- so the crosshair can name the errand's own sites with
    // the identical honesty. Kept a sibling rather than a parameter because
    // the two books are two members, and a helper taking "which book" would
    // be the only call site in the build that has to name one.
    if (sheetBook_.raws() == nullptr) {
        return -1;
    }
    const std::vector<sim::Lead>& leads = sheetBook_.raws()->leads();
    const std::int32_t band = body_->band();
    const auto nearestFrom = [&](std::int32_t fromX, std::int32_t fromY) {
        int best = -1;
        std::int32_t bestDistance = 0;
        for (std::size_t i = 0; i < leads.size(); ++i) {
            const sim::Lead& lead = leads[i];
            if (lead.site.band != band) {
                continue;
            }
            const std::int32_t dx = lead.site.x - fromX;
            const std::int32_t dy = lead.site.y - fromY;
            const std::int32_t distance = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
            if (distance > sim::kLookRangeTiles) {
                continue;
            }
            if (best < 0 || distance < bestDistance) {
                best = static_cast<int>(i);
                bestDistance = distance;
            }
        }
        return best;
    };
    const std::int32_t tileX = body_->tileX();
    const std::int32_t tileY = body_->tileY();
    if (const int here = nearestFrom(tileX, tileY); here >= 0) {
        return here;
    }
    const std::int32_t bonus = legend().lookRangeBonus();
    for (std::int32_t ring = 1; ring <= bonus; ++ring) {
        const std::int32_t offsets[4][2] = {{ring, 0}, {-ring, 0}, {0, ring}, {0, -ring}};
        for (const auto& offset : offsets) {
            if (const int found = nearestFrom(tileX + offset[0], tileY + offset[1]); found >= 0) {
                return found;
            }
        }
    }
    return -1;
}

void Session::armCasePlate(std::string news) {
    // UI-EA (LANE HUD): see the declaration. The dash and the live key are
    // the whole chrome a notice gets; the news is the ward's own words and
    // arrives untouched.
    casePlateText_ = std::move(news) + " - " +
                     std::string(promptLabel(controls_, Action::Menu, promptDevice_));
    casePlateShowSteps_ = kCasePlateShowSteps;
    // THE PULL PACK: told, so the book-change watcher (stepPull) does not say
    // the same change a second time on the step that follows.
    casePlateArmedThisStep_ = true;
    // THE PULL PACK: ONE CUE ON EVERY BOOK CHANGE. Every site that arms this
    // plate is the book changing -- leads opening, a sheet or a writ handed
    // over, the errand paid, the Evictor granted, the watcher's own news --
    // so the cue lives here, once, rather than beside each of them. Ids
    // only, deterministic, one-way (audio_engine.hpp's own rule): the sim
    // cannot hear it. EXCEPT ON A CLOSE: a plate armed on the step a book
    // closed ("THE ERRAND IS PAID", "THE EVICTOR IS YOURS") rides the
    // CaseClosed sting step() is about to play, and two cues for one close
    // is two cues.
    if (audio_ != nullptr && closedBookCount() == lastClosedBooks_) {
        audio_->playOneShot(audio::SoundId::CaseNews);
    }
    syncPanelAnim();
}

namespace {

// ---------------------------------------------------------------------------
// THE CASEBOOK PASS: the book as a composed master/detail page, and the route
// out of it to where a lead actually is
// ---------------------------------------------------------------------------
//
// See render/casebook_page.hpp for the defect and the composition. Everything
// below is pure UI state plus two handoffs -- examine() (the look key's own
// call) and the ward map's own cursor -- so nothing here can move the world
// hash and nothing here is a second way to do anything.
/// SHOUTED, because every authored string on this surface already is and a
/// notable's name is the one that is not.
[[nodiscard]] std::string shoutName(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// THE PULL PACK (render/pull.hpp) -- the street says where next
// ---------------------------------------------------------------------------

BookSet Session::bookSet() const noexcept {
    BookSet set;
    set.books = {&casebook_, &sheetBook_, &evictBook_};
    set.raws = {&caseRaws_, &sheetRaws_, &evictRaws_};
    return set;
}

const sim::Casebook& Session::bookOf(CaseBookId id) const noexcept {
    switch (id) {
        case CaseBookId::Courier:
            return sheetBook_;
        case CaseBookId::Eviction:
            return evictBook_;
        case CaseBookId::Bloodletter:
        default:
            return casebook_;
    }
}

const sim::CasebookRaws& Session::rawsOf(CaseBookId id) const noexcept {
    switch (id) {
        case CaseBookId::Courier:
            return sheetRaws_;
        case CaseBookId::Eviction:
            return evictRaws_;
        case CaseBookId::Bloodletter:
        default:
            return caseRaws_;
    }
}

CaseBookId Session::frontedBook() const noexcept {
    // THE PLAYER'S PICK, while the book is one they have been handed. The
    // close edge clears caseFront_ (stepPull), so a fronted book that just
    // closed hands the page back to whatever is live -- and can be fronted
    // again off the shelf to be reread, which is what a shelf is for.
    if (caseFront_ >= 0 && caseFront_ < kCaseBookCount) {
        const CaseBookId picked = static_cast<CaseBookId>(caseFront_);
        if (bookSet().begun(picked)) {
            return picked;
        }
    }
    return autoFrontedBook(bookSet());
}

bool Session::frontCase(CaseBookId book) {
    if (!bookSet().begun(book)) {
        return false;
    }
    caseFront_ = static_cast<int>(book);
    // The page's own cursor is an index into the fronted book's known list,
    // and that list just changed under it: back to the top, exactly as a
    // book put down on the ledger opens on the ledger only while it is the
    // same book.
    casePageCursor_ = 0;
    pullLineCache_ = pullLineNow();
    return true;
}

PullTarget Session::pullTarget() const noexcept {
    return resolvePull(followedLead_, bookSet(), frontedBook());
}

bool Session::followLead(CaseBookId book, std::int32_t lead) {
    const BookSet books = bookSet();
    const sim::Casebook* target = books.book(book);
    const sim::CasebookRaws* raws = books.raw(book);
    if (!books.begun(book) || lead < 0 || static_cast<std::size_t>(lead) >= raws->leads().size()) {
        return false;
    }
    if (target->state(lead) != sim::LeadState::Open) {
        // NOWHERE TO GO: a lead already stood over is a page in the book, not
        // a place on the street. Said where it can be read -- the page's own
        // band while the book is up, the row once it is down.
        notePage(target->state(lead) == sim::LeadState::Cold
                     ? "A DEAD END IS NOT A DIRECTION."
                     : "YOU HAVE BEEN THERE. FOLLOW WHAT IT OPENED.");
        if (audio_ != nullptr) {
            audio_->playOneShot(audio::SoundId::UiError);
        }
        return false;
    }
    const sim::Lead& where = raws->leads()[static_cast<std::size_t>(lead)];
    if (followedLead_.chosen() && followedLead_.book == book && followedLead_.lead == lead) {
        // THE ONE VERB IS ITS OWN UNDO: picking the followed lead again lets
        // it go, and the newest-heard default takes the compass back.
        followedLead_.clear();
        notePage("THE COMPASS LETS IT GO.");
    } else {
        followedLead_.book = book;
        followedLead_.lead = lead;
        notePage("THE COMPASS HOLDS " + where.place + ".");
    }
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::UiConfirm);
    }
    // The case row restates where next on the same press, and the ribbon
    // line is rebuilt now rather than on the next step, so the frame after
    // the press already carries it.
    caseShowSteps_ = kHudWakeSteps;
    pullLineCache_ = pullLineNow();
    syncPanelAnim();
    return true;
}

void Session::notePage(std::string line) {
    // ONE LINE, TWO SURFACES: the page's own header band while the book is
    // up (the alert row is stood down under a page, so a line said only
    // there would answer the key in sound alone), and the message row the
    // moment the book goes down. The band holds it the plate's own ~3 s.
    pageNote_ = line;
    pageNoteSteps_ = kCasePlateShowSteps;
    say(std::move(line));
}

void Session::followCasebookSelection() {
    if (casebookTab_ == CasebookTab::Cases) {
        // ON THE SHELF: follow the highlighted case -- its own newest lead.
        // Never the undo from here: the shelf is where a case is picked up,
        // and a press that quietly let one go would be the surprise sec. 4
        // exists to kill.
        const std::vector<CasebookShelfRow> shelf = casebookShelfRows();
        if (shelf.empty()) {
            return;
        }
        const int at = std::clamp(caseShelfCursor_, 0, static_cast<int>(shelf.size()) - 1);
        const CasebookShelfRow& row = shelf[static_cast<std::size_t>(at)];
        if (row.errand) {
            // ERRANDS A HUMAN CAN FINISH. Nothing tracks a body live -- the
            // shelf says where they were; the ward map says who is there now.
            notePage(std::string(kErrandFindLine));
            return;
        }
        if (!row.book || row.bookId < 0 || row.bookId >= kCaseBookCount) {
            notePage("A LINE, NOT A BOOK. THE JOURNAL HAS IT.");
            return;
        }
        const CaseBookId book = static_cast<CaseBookId>(row.bookId);
        const std::int32_t next = newestOpenLead(bookOf(book));
        if (next < 0) {
            notePage("NOTHING OPEN IN IT.");
            return;
        }
        if (followedLead_.chosen() && followedLead_.book == book && followedLead_.lead == next) {
            notePage("THE COMPASS HOLDS IT ALREADY.");
            return;
        }
        (void)followLead(book, next);
        return;
    }
    const sim::Casebook& book = activeCasebook();
    const std::vector<std::int32_t> heard = book.known();
    if (heard.empty() || book.raws() == nullptr) {
        return;
    }
    const int at = std::clamp(casePageCursor_, 0, static_cast<int>(heard.size()) - 1);
    (void)followLead(frontedBook(), heard[static_cast<std::size_t>(at)]);
}

std::string Session::pullLineNow() const {
    const PullTarget target = pullTarget();
    if (!target.set) {
        return {};
    }
    const sim::CasebookRaws& raws = rawsOf(target.book);
    if (static_cast<std::size_t>(target.lead) >= raws.leads().size()) {
        return {};
    }
    const sim::Lead& lead = raws.leads()[static_cast<std::size_t>(target.lead)];
    const bool here = bodyCanLookAt(lead);
    // TWO LEADS IN ONE BUILDING BOTH WEAR THE BUILDING'S SIGN. Standing in
    // the Mission, a line reading "W 13  MISSION OF THE FLAME" names the
    // house you are in; the lead's own short name -- FLAGSTONES -- is the
    // spot in it. So when the lead's sign is the sign of the place the body
    // stands in (and the body is not on the lead itself, where HERE and the
    // crosshair carry it), the line carries the brief.
    const int under = mapPlaceUnder(body_->tileX(), body_->tileY());
    const bool sameSign = !here && under >= 0 && !lead.brief.empty() &&
                          placeIndexOfLead(lead) == under;
    // BELOW / ABOVE on the street: a walking body cannot see its own band
    // number, so the ribbon says which way; the book keeps the number.
    return pullLine(pullBearing(body_->tileX(), body_->tileY(), body_->band(), lead.site, here,
                                BandWord::Relative),
                    sameSign ? lead.brief : lead.place);
}

int Session::placeIndexOfLead(const sim::Lead& lead) const {
    const std::vector<MapPlace>& places = mapPlaces();
    const std::string want = shoutName(lead.place);
    for (std::size_t i = 0; i < places.size(); ++i) {
        if (shoutName(places[i].name) == want) {
            return static_cast<int>(i);
        }
    }
    return mapPlaceUnder(lead.site.x, lead.site.y);
}

std::vector<CasebookShelfRow> Session::casebookShelfRows() const {
    std::vector<CasebookShelfRow> rows;
    const BookSet books = bookSet();
    const CaseBookId fronted = frontedBook();
    const PullTarget target = pullTarget();
    // THE BOOKS IN HAND FIRST, in fixed member order, each carrying its own
    // CaseBookId -- a book the player has not been handed is counted in the
    // badge and never named (the journal never lists an unstarted quest).
    for (int i = 0; i < kCaseBookCount; ++i) {
        const CaseBookId id = static_cast<CaseBookId>(i);
        if (!books.begun(id)) {
            continue;
        }
        const sim::Casebook& book = bookOf(id);
        const sim::CasebookRaws& raws = rawsOf(id);
        CasebookShelfRow row;
        row.book = true;
        row.bookId = i;
        row.title = std::string(raws.title());
        row.fronted = id == fronted;
        row.followed = target.set && target.book == id;
        if (book.closed()) {
            row.state = "CLOSED";
            row.tally = "ALL";
            row.next = std::string(raws.close());
        } else {
            row.tally = std::to_string(book.readCount()) + "/" +
                        std::to_string(book.known().size());
            row.state = "READ " + row.tally;
            const std::int32_t next = newestOpenLead(book);
            if (next >= 0 && static_cast<std::size_t>(next) < raws.leads().size()) {
                row.next = raws.leads()[static_cast<std::size_t>(next)].place;
            }
        }
        rows.push_back(std::move(row));
    }
    // THEN THE STARTED QUESTLINES, read-only: where each stands, by stage.
    const sim::DialogueDirector& talk = tavern_->dialogue();
    for (const sim::Questline& line : talk.quests().lines()) {
        if (!talk.journal().started(line.id)) {
            continue;
        }
        CasebookShelfRow row;
        row.book = false;
        row.title = upperAscii(line.title);
        const std::int32_t total = static_cast<std::int32_t>(line.stages.size());
        if (talk.journal().done(line.id)) {
            row.state = "DONE";
            row.tally = std::to_string(total) + "/" + std::to_string(total);
        } else {
            const std::int32_t at = talk.journal().stage(line.id);
            row.tally = std::to_string(std::min(total, at + 1)) + "/" + std::to_string(total);
            row.state = "STAGE " + row.tally;
            if (at >= 0 && at < total) {
                row.next = line.stages[static_cast<std::size_t>(at)].label;
            }
        }
        rows.push_back(std::move(row));
    }
    // ERRANDS A HUMAN CAN FINISH (2026-10-09). THEN EVERY TAKEN ERRAND, read-
    // only like a questline: who settles it, where they were when the job
    // was posted, what it pays. The brief that said all of this used to be
    // spoken once on the alert row and gone; a player who walked off with
    // CARRY WORD TO KESSA INKWATER had a name and six hundred people to ask.
    // A fetch names its GIVER -- the goods come from the ward's own sources
    // (the snug, a strongbox, a rat), never out of a WardActor's pocket.
    for (const sim::RadiantObjective& errand : talk.radiant().objectives()) {
        if (!errand.live()) {
            continue;
        }
        CasebookShelfRow row;
        row.errand = true;
        row.pays = std::to_string(errand.pay) + "C";
        if (errand.isFetch()) {
            const std::int32_t have = talk.crimes().stash().count(errand.good);
            row.title = errand.verb + " " + std::to_string(errand.units) + " " +
                        std::string(sim::contrabandLabelFor(errand.good, errand.units));
            row.tally = std::to_string(std::min(have, errand.units)) + "/" +
                        std::to_string(errand.units);
            row.state = "HAVE " + row.tally;
            row.whoLabel = "BRING IT TO";
            row.who = upperAscii(errand.giverName);
            row.where = upperAscii(sim::radiantPlacePhrase(errand.giverPlace).at);
        } else {
            row.title = errand.verb + " TO " + upperAscii(errand.targetName);
            row.state = "ERRAND";
            row.whoLabel = "SAY IT TO";
            row.who = upperAscii(errand.targetName);
            row.where = upperAscii(sim::radiantPlacePhrase(errand.targetPlace).at);
            row.pays += " ON ARRIVAL";
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

void Session::stepPull() {
    // 1. THE FRONTED BOOK'S CLOSE EDGE, read before the watcher advances.
    if (caseFront_ >= 0 && caseFront_ < kCaseBookCount &&
        bookClosedThisStep(bookNews_, bookSet(), static_cast<CaseBookId>(caseFront_))) {
        caseFront_ = -1;
    }
    // 2. THE BOOK NEWS: the plates the courier and eviction cases' scripted
    // hears never armed, a questline stage, an errand taken -- one watcher,
    // one plate, one cue. examine() has already said its own leads.
    const sim::DialogueDirector& talk = tavern_->dialogue();
    const bool said = casePlateArmedThisStep_;
    casePlateArmedThisStep_ = false;
    const std::string news = diffBookNews(bookNews_, bookSet(), talk.quests(), talk.journal(),
                                          talk.radiant(), said);
    if (!news.empty()) {
        armCasePlate(news);
        casePlateArmedThisStep_ = false;
    }
    // 3. THE SKILL-UP TOAST. SkillTrack::use() has returned `levelled` since
    // S17 and every caller threw it away; the levels themselves are hashed
    // state the render side may read, so the diff happens here, once a
    // step, in situ. A rise while one is up queues behind it.
    for (const SkillRise& rise : skillRise_.diff(talk.skills())) {
        // THE LATEST LEVEL SUPERSEDES. A toast still waiting for the same
        // skill -- SKYRUNNING RISES TO 2 queued behind RISES TO 1's own hold
        // when the leap that tips THE ROOFS levels it to 3 -- is stale the
        // moment a newer level exists: it would play after the plate with a
        // number going backwards (both critics, fourth pass). The queue
        // keeps one entry per skill, the newest; the Character tile has the
        // rest.
        const std::string label = skillToastFor(rise);
        const std::string_view skill = std::string_view{label}.substr(0, label.rfind(" RISES TO "));
        skillToastQueue_.erase(
            std::remove_if(skillToastQueue_.begin(), skillToastQueue_.end(),
                           [skill](const std::string& queued) {
                               return std::string_view{queued}.substr(
                                          0, queued.rfind(" RISES TO ")) == skill;
                           }),
            skillToastQueue_.end());
        // FOLDED INTO THE PLATE when the rung rose on this same step (the
        // leap that tips THE ROOFS levels SKYRUNNING; the box that tips THE
        // WIRE levels CRACKSMANSHIP; the bounty that tips THE TRADE levels
        // FIELDCRAFT): the plate's own dim last row, the same climb told
        // once, on the card -- never a toast five seconds late in bigger
        // type. The plate that rose this step is the one on screen if it
        // started, else the last one queued -- and when two tracks rise on
        // one step, the tick rides the second, queued plate, whichever
        // track that is. A second skill on the same step, or one later in
        // the hold, queues as a toast. AND ONLY WHERE THE CARD HAS A ROW FOR
        // IT (rungPlateTickFits, at the session's own frame size): at
        // 320x180 the card drops its tick row, and a level folded in there
        // was told nowhere (the player, eighth pass) -- it queues as a
        // toast behind the plate instead.
        if (rungRoseThisStep_) {
            RungPlateText& plate =
                rungPlateQueue_.empty() ? rungPlateText_ : rungPlateQueue_.back();
            if (plate.tick.empty()) {
                RungPlateState probe;
                probe.head = plate.head;
                probe.prose = plate.prose;
                probe.top = plate.top;
                probe.tick = label;
                if (rungPlateTickFits(config_.width, config_.height, probe)) {
                    plate.tick = label;
                    continue;
                }
            }
        }
        skillToastQueue_.push_back(label);
    }
    if (skillToastShowSteps_ > 0) {
        --skillToastShowSteps_;
    }
    // THE RUNG PLATE OUTRANKS THE TOAST, ONE ALREADY ON SCREEN INCLUDED: a
    // rung is bigger than a tick, and a rung and its matching skill can be
    // the SAME EVENT SAID TWICE (a leap that both tips THE ROOFS and levels
    // SKYRUNNING) -- the toast used to keep counting down and drawing right
    // through the plate landing on top of it, two notices piled on the one
    // corner. Cut its hold the moment the plate wants the screen, the
    // identical idiom rungPlateOutranked() already applies to the plate's
    // own hold under a warning: dismissed, not paused, because the plate has
    // already told the moment. A toast still in the queue is never dropped,
    // it simply cannot start while the plate wants the screen either.
    if (skillToastShowSteps_ > 0 && rungPlateWanted()) {
        skillToastShowSteps_ = 0;
    }
    // AND ONLY ONCE THE PLATE HAS LIFTED ALL THE WAY: a toast that started
    // the step the hold ended crossed the plate's own lift (the player's and
    // the director's finding), so it waits for the ease to settle at zero.
    if (skillToastShowSteps_ == 0 && !skillToastQueue_.empty() && skillToastAnim_.settled() &&
        !rungPlateWanted() && rungPlateAnim_.settled() && !rungPlateAnim_.target() &&
        rungPlateQueue_.empty()) {
        skillToastText_ = std::move(skillToastQueue_.front());
        skillToastQueue_.erase(skillToastQueue_.begin());
        skillToastShowSteps_ = kCasePlateShowSteps;
        // Its own cue -- the manifest's held progression chime -- not the
        // menu's confirm: a level is news, not a button.
        if (audio_ != nullptr) {
            audio_->playOneShot(audio::SoundId::SkillUp);
        }
    }
    // 3b. THE PAGE NOTE's own countdown, the plate family's rule.
    if (pageNoteSteps_ > 0) {
        --pageNoteSteps_;
    }
    skillToastAnim_.setTarget(skillToastShowSteps_ > 0);
    skillToastAnim_.advance();
    // 4. THE PLACES STOOD IN -- the other half of "discovered" for the ribbon
    // ticks. One footprint lookup per tile the body actually changes.
    const std::int32_t tx = body_->tileX();
    const std::int32_t ty = body_->tileY();
    if (tx != placesStoodTileX_ || ty != placesStoodTileY_) {
        placesStoodTileX_ = tx;
        placesStoodTileY_ = ty;
        const std::vector<MapPlace>& places = mapPlaces();
        if (placesStood_.size() != places.size()) {
            placesStood_.assign(places.size(), 0);
        }
        const int here = mapPlaceUnder(tx, ty);
        if (here >= 0 && static_cast<std::size_t>(here) < places.size() &&
            !places[static_cast<std::size_t>(here)].way) {
            placesStood_[static_cast<std::size_t>(here)] = 1;
        }
    }
    // 5. THE RIBBON LINE, rebuilt once a step: the paces run down as the body
    // walks, and the choice can lapse the moment a lead is stood over. Held
    // in a member so HudState's string_view has something to point at.
    pullLineCache_ = pullLineNow();
}

HudState Session::pullHud() const {
    HudState hud;
    composePullHud(hud);
    return hud;
}

void Session::composePullHud(HudState& hud) const {
    const std::int32_t px = body_->tileX();
    const std::int32_t py = body_->tileY();
    // THE FOLLOWED LEAD: the line, and its amber tick.
    hud.pullLabel = std::string_view{pullLineCache_};
    const PullTarget target = pullTarget();
    // The place the amber notch already points at draws no bone notch of
    // its own: the two sat a few degrees apart (the site against the door)
    // and read as two places.
    int amberPlace = -1;
    if (target.set) {
        const sim::CasebookRaws& raws = rawsOf(target.book);
        if (static_cast<std::size_t>(target.lead) < raws.leads().size()) {
            const sim::Lead& lead = raws.leads()[static_cast<std::size_t>(target.lead)];
            if (lead.site.x != px || lead.site.y != py) {
                // THE TRUE BEARING, not the stealth pass's eight-point one:
                // a notch belongs on the bearing, not under a letter.
                hud.pullTickBam = pullTickBam(px, py, lead.site.x, lead.site.y);
            }
            amberPlace = placeIndexOfLead(lead);
        }
    }
    // THE DISCOVERED NAMED PLACES: known ground (a heard lead in any book
    // names it -- mapRows()'s own definition, over all three books) and the
    // places the body has stood in. Named places only, never a person --
    // the doctrine -- and streets are ground, not destinations.
    const std::vector<MapPlace>& places = mapPlaces();
    std::vector<std::uint8_t> discovered(places.size(), 0);
    for (std::size_t i = 0; i < placesStood_.size() && i < discovered.size(); ++i) {
        discovered[i] = placesStood_[i];
    }
    for (int b = 0; b < kCaseBookCount; ++b) {
        const CaseBookId id = static_cast<CaseBookId>(b);
        const sim::Casebook& book = bookOf(id);
        const sim::CasebookRaws& raws = rawsOf(id);
        if (!book.active() || !raws.loaded()) {
            continue;
        }
        for (const std::int32_t index : book.known()) {
            const int at = placeIndexOfLead(raws.leads()[static_cast<std::size_t>(index)]);
            if (at >= 0 && static_cast<std::size_t>(at) < discovered.size()) {
                discovered[static_cast<std::size_t>(at)] = 1;
            }
        }
    }
    for (std::size_t i = 0; i < discovered.size(); ++i) {
        if (!discovered[i] || places[i].way || static_cast<int>(i) == amberPlace) {
            continue;
        }
        std::int32_t ax = 0;
        std::int32_t ay = 0;
        mapAimPoint(places[i], px, py, ax, ay);
        if (ax == px && ay == py) {
            continue;
        }
        hud.placeTickBams.push_back(pullTickBam(px, py, ax, ay));
    }
    // THE TOAST. Zero the instant the plate wants the screen -- stepPull()
    // has already cut the hold for every step after this one; this is what
    // keeps the toast's own eased fade-out from painting through the frame
    // the plate first lands on (rungPlateState's identical `outranked ? 0`
    // rule, mirrored here for the corner the plate is outranking).
    hud.skillToast = std::string_view{skillToastText_};
    hud.skillToastFade = rungPlateWanted() ? 0.0F : skillToastAnim_.value();
    hud.skillToastDrift = skillToastAnim_.target() ? -(1.0F - skillToastAnim_.value())
                                                   : (1.0F - skillToastAnim_.value());
}

// ---------------------------------------------------------------------------
// THE RUNG PLATE (render/rung_plate.hpp) -- the ward says so
// ---------------------------------------------------------------------------

bool Session::rungPlateOutranked() const noexcept {
    // THE ALERT ROW'S OWN PRIORITY RULE, applied whole. `warned` is
    // bouncerWarned(), the one copy of the three-line test drawFrame() and
    // syncPanelAnim() used to each carry (since S6). Then
    // everything else that owns the screen: a page or a talk (the one
    // formula, conversingNow), the court's beats, plate and page and the
    // rope's ceremony (inCustody), and the death ceremony's veil.
    const bool warned = bouncerWarned();
    // THE WATCH'S WORD (fourth pass, the auditor's finding): a street
    // watchman's HALT, SHEATHE or OFFENCE rides the alert row through say()
    // and an ignored halt ends in an arrest at reach -- exactly the row a
    // burglar earns THE WIRE under. A demand on the row outranks the plate
    // for as long as the row holds it, the warning's own shape, so it is
    // never the line dimmed.
    const bool demanded = messageDemand_ && messageSteps_ > 0;
    // A PAGE'S CLOSE TAIL (the auditor's other finding): the plate is
    // composed on the world path only, and the tiled Menu, the map and the
    // court each draw their own eight-step tail without it. A rung that
    // started the step a page key went down spent its whole rise under that
    // tail unseen and popped in at full strength. The tail outranks it, so
    // it rises where it can be seen.
    const bool tailing = panelAnim_.value() > 0.0F || districtMapAnim_.value() > 0.0F;
    return warned || demanded || tailing || conversingNow() || inCustody() ||
           deathCeremonySteps_ > 0;
}

void Session::stepRungPlate() {
    // 1. THE DIFF, once a step, off the same pure legend() the sheet prints
    //    -- every rung is a function of counters the sim already hashes, so
    //    remembering five integers on this side of the boundary is the whole
    //    of the detection. A rise on any track queues its plate, worded out
    //    of the authored sheet and never by this file.
    const std::vector<LegendRise> rises = legendRise_.diff(legend());
    // Told to stepPull, which runs after this and folds a skill that levelled
    // on this same step into the plate's own tick row rather than a toast.
    rungRoseThisStep_ = !rises.empty();
    if (!rises.empty()) {
        const sim::BarkTables& barks = tavern_->dialogue().barks();
        for (const LegendRise& rise : rises) {
            rungPlateQueue_.push_back(rungPlateFor(rise, barks));
        }
    }
    // 2. THE HOLD, spent HERE and only here, once a step -- the plate
    //    family's rule (placePlateShowSteps_'s own note).
    if (rungPlateShowSteps_ > 0) {
        --rungPlateShowSteps_;
        // THE HOLD SPENT: the alert row's own clock, stopped under the plate
        // (step()'s countdown), gets what it has left -- capped at two
        // seconds, so a leap's length is not five seconds late for long.
        if (rungPlateShowSteps_ == 0) {
            // EVERY LINE STILL ON THE ROW -- news held under the plate, or a
            // moment that outlived the lift -- keeps two seconds at most and
            // finishes small (the auditor: an uncapped moment swelled). A
            // WATCH DEMAND is the one line never touched: it dismissed the
            // plate, and it keeps its whole hold at the glance size.
            if (!messageDemand_) {
                messageSteps_ = std::min(messageSteps_, kRungPlateRowAfterSteps);
                messageCapped_ = messageSteps_ > 0 || alertAnim_.value() > 0.0F;
            }
            voice_.capHold(kRungPlateRowAfterSteps);
        }
    }
    // 3. DISMISSED UNDER ANYTHING THAT OWNS THE SCREEN. A page key, a talk,
    //    the court taking you, a bouncer's warning: the plate that was up is
    //    done, its hold cut to nothing, and the ease-out hides under whatever
    //    took the frame (rungPlateState zeroes the fade while outranked).
    //    Never held for later: a warning can last as long as the house minds
    //    you, and a plate surfacing minutes after its rung would be a
    //    notice out of its moment.
    //    THE LIFT IS NO EXCEPTION: a card still easing out when a page opens
    //    is dismissed too (the auditor: left to ease under a three-step
    //    page, it came back at full ink after the page's tail).
    const bool outranked = rungPlateOutranked();
    if (outranked && (rungPlateShowSteps_ > 0 || rungPlateAnim_.value() > 0.0F)) {
        rungPlateShowSteps_ = 0;
        // AND THE ROW'S CLOCK IS CAPPED HERE TOO: a live hold cut by a page
        // or a warning left the frozen clock whole, so a leap's length
        // could surface six seconds after the leap (the auditor's finding).
        if (!messageDemand_) {
            messageSteps_ = std::min(messageSteps_, kRungPlateRowAfterSteps);
            messageCapped_ = messageSteps_ > 0 || alertAnim_.value() > 0.0F;
        }
        voice_.capHold(kRungPlateRowAfterSteps);
        // AND THE LIFT IS CUT WITH THE HOLD: dismissed is gone. Left easing,
        // a card dismissed by a three-step page came back at three quarters
        // and lifted out after it, re-arming every stand-down (the auditor).
        rungPlateAnim_.snapTo(false);
    }
    // 4. THE NEXT ONE UP, off the queue, on a free screen, and only once the
    //    last has eased all the way down -- `!target()` is what puts a beat
    //    of black between two plates instead of swapping the words mid-hold.
    //    Queued rises wait through pages, talks and warnings and are never
    //    dropped. Its own cue, the heavy bell, the step it actually shows.
    if (rungPlateShowSteps_ == 0 && !rungPlateQueue_.empty() && !outranked &&
        rungPlateAnim_.settled() && !rungPlateAnim_.target()) {
        rungPlateText_ = std::move(rungPlateQueue_.front());
        rungPlateQueue_.erase(rungPlateQueue_.begin());
        rungPlateShowSteps_ = kRungPlateHoldSteps;
        if (audio_ != nullptr) {
            audio_->playOneShot(audio::SoundId::LegendRung);
        }
    }
    rungPlateAnim_.setTarget(rungPlateShowSteps_ > 0);
    rungPlateAnim_.advance();
    // 5. THE PLATE'S LAST FRAME. The two caps above fire when the hold ends
    //    and when the card is dismissed; a line said DURING the lift had
    //    its cap cleared by say() and swelled to the glance size the frame
    //    the card left (the auditor). So the cap fires once more on the
    //    frame the drawn fade reaches zero, whatever put a line on the row
    //    in between -- the demand excepted, as everywhere.
    const bool onScreen = rungPlateOnScreen();
    if (rungPlateWasOnScreen_ && !onScreen) {
        if (!messageDemand_) {
            messageSteps_ = std::min(messageSteps_, kRungPlateRowAfterSteps);
            messageCapped_ = messageSteps_ > 0 || alertAnim_.value() > 0.0F;
        }
        voice_.capHold(kRungPlateRowAfterSteps);
    }
    rungPlateWasOnScreen_ = onScreen;
}

RungPlateState Session::rungPlateState() const noexcept {
    RungPlateState state;
    state.head = std::string_view{rungPlateText_.head};
    state.prose = std::string_view{rungPlateText_.prose};
    state.top = std::string_view{rungPlateText_.top};
    state.tick = std::string_view{rungPlateText_.tick};
    // ZERO UNDER ANYTHING THAT OWNS THE SCREEN -- the case plate's own
    // `conversing ? 0` rule. stepRungPlate has already cut the hold; this is
    // what keeps the handful of frames the ease-out spends from being painted
    // over the page, the warning or the court's plate that dismissed it.
    state.fade = rungPlateOutranked() ? 0.0F : rungPlateAnim_.value();
    // THE SIGN COMES OFF THE TOGGLE'S OWN TARGET, the announce plates' contract:
    // rising (target true) it is still below its seat, closing on zero;
    // fading (target false) it is past and drifting on up and out.
    state.drift = rungPlateAnim_.target() ? -(1.0F - rungPlateAnim_.value())
                                          : (1.0F - rungPlateAnim_.value());
    return state;
}

void Session::composeRungPlate(Framebuffer& target) const {
    if (rungPlateText_.empty()) {
        return;
    }
    drawRungPlate(target, rungPlateState());
}

bool Session::bouncerWarned() const noexcept {
    return !tavern_->lastWarning().empty() &&
           tavern_->playerStanding() != sim::Standing::Welcome;
}

bool Session::rungPlateOnScreen() const noexcept {
    // THE DRAWN FADE AND NOTHING ELSE. rungPlateState() re-reads the
    // outranking live, so on the very frame a warning or a demand lands --
    // which advanceSecond says after stepRungPlate has run -- this is
    // already false, and the row that carries the one line the player must
    // not miss is drawn at its full size from its first frame (the auditor's
    // finding). Otherwise, while the plate is wanted its fade is above zero.
    return rungPlateState().fade > 0.0F;
}

float Session::alertInkWeightNow() const noexcept {
    return alertWeightUnderRungPlate(rungPlateState().fade);
}

bool Session::alertStepsDownNow() const noexcept {
    // A line capped under the plate stays small; a WARNING on the row is
    // never small (drawFrame puts the warning on the row in the line's
    // place, and its own first frame is the one that matters), and neither
    // is a watchman's DEMAND, which is never capped in the first place.
    return rungPlateOnScreen() || (messageCapped_ && !bouncerWarned() && !messageDemand_);
}

bool Session::voiceStepsDownNow() const noexcept {
    return rungPlateOnScreen() || voice_.capped();
}

float Session::casePlateFadeNow() const noexcept {
    // THE ANNOUNCE SLOT WAITS BEHIND THE PLATE, AND IT IS NOT SEEN GOING.
    // syncPanelAnim drops the slot's target while the plate is wanted and
    // step() holds its clock, but an ease-out is eight frames and the
    // plate's rise is eight frames, and the flame drive's first fully-up
    // frame carried `1 NEW LEAD` at full strength above the plate (the
    // director's finding, third pass). The plate's own rule for itself
    // under a page -- drawn at zero the instant something outranks it --
    // applied to the slot the plate outranks.
    return rungPlateOnScreen() ? 0.0F : casePlateAnim_.value();
}

float Session::placePlateFadeNow() const noexcept {
    return rungPlateOnScreen() ? 0.0F : placePlateAnim_.value();
}

float Session::alertFadeNow() const noexcept {
    // THE ALERT ROW STANDS AT HALF WEIGHT UNDER THE PLATE. The critic's
    // finding on the record plate: the amber line at the foot of the frame
    // fired in the same frame as the plate, wider and equally bright, and
    // outweighed it. Not dismissed, the way stepPull cuts the skill toast: the
    // row is news of its own (the take off a cracked box, a leap's length, a
    // watchman's word across the table) and its four-second hold would be
    // lost whole under a five-second plate. Not deferred either: a leap's
    // length said five seconds after the leap is a notice out of its moment.
    // Scaled instead, on the plate's OWN fade, so the two cross rather than
    // snap and the plate is the one notice on the frame at full strength
    // while it holds. rungPlateState() zeroes that fade under anything that
    // outranks the plate -- a bouncer's WARNING rides this same row and
    // dismisses the plate outright -- so the one line the player must not
    // miss is never the one dimmed.
    return alertAnim_.value() * alertWeightUnderRungPlate(rungPlateState().fade);
}

int Session::mapPlaceForLead(std::int32_t leadIndex) const {
    // COURIER CASE. THE ACTIVE BOOK'S INDEX SPACE, because every caller
    // (the page's routable flag, commitCasebookLead, showLeadOnMap) indexes
    // the book the page is showing -- one rule, sheetCaseLive(), applied
    // everywhere a surface reads "the case".
    const sim::Casebook& book = activeCasebook();
    if (book.raws() == nullptr || leadIndex < 0 ||
        static_cast<std::size_t>(leadIndex) >= book.raws()->leads().size()) {
        return -1;
    }
    const sim::Lead& lead = book.raws()->leads()[static_cast<std::size_t>(leadIndex)];
    // BY THE AUTHORED NAME FIRST, case-folded -- because that is what the two
    // files actually agree on. casebook.json shouts its `place` ("THE
    // WEIGHHOUSE") and the .tmx sign table spells it as a proper noun ("The
    // Weighhouse"); every one of the twelve leads matches a sign that way, and
    // a case pins that so a renamed building goes red here rather than silently
    // routing a player to the wrong door.
    const std::vector<MapPlace>& places = mapPlaces();
    const std::string want = shoutName(lead.place);
    for (std::size_t i = 0; i < places.size(); ++i) {
        if (shoutName(places[i].name) == want) {
            return static_cast<int>(i);
        }
    }
    // AND THE GEOMETRY AS THE FALLBACK: the smallest authored footprint the
    // lead's own site stands in. A name that has drifted is still a coordinate
    // that has not.
    return mapPlaceUnder(lead.site.x, lead.site.y);
}

bool Session::bodyCanLookAt(const sim::Lead& lead) const {
    if (lead.site.band != body_->band()) {
        return false;
    }
    // THE SAME REACH examine() ACTUALLY USES, boon included -- this flag changes
    // a verb that says LOOK, so it has to mean what LOOK means or the page
    // offers a key that would do nothing.
    const std::int32_t reach = sim::kLookRangeTiles + legend().lookRangeBonus();
    const std::int32_t dx = lead.site.x - body_->tileX();
    const std::int32_t dy = lead.site.y - body_->tileY();
    return (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) <= reach;
}

void Session::moveCasebookCursor(int delta) {
    if (casebookTab_ == CasebookTab::Cases) {
        // THE PULL PACK: on the shelf the cursor walks the cases, not the
        // leads -- the list under it is the fronted book's and holds still.
        const int shelf = static_cast<int>(casebookShelfRows().size());
        caseShelfCursor_ = shelf <= 0 ? 0 : ((caseShelfCursor_ + delta) % shelf + shelf) % shelf;
        return;
    }
    const int count = static_cast<int>(activeCasebook().known().size());
    if (count <= 0) {
        casePageCursor_ = 0;
        return;
    }
    // WRAPS, so holding one direction walks the whole book -- the same rule
    // every other list in this build keeps.
    casePageCursor_ = ((casePageCursor_ + delta) % count + count) % count;
}

void Session::setCasebookCursor(int index) {
    const int count = static_cast<int>(activeCasebook().known().size());
    if (count <= 0 || index < 0 || index >= count) {
        return;
    }
    casePageCursor_ = index;
}

void Session::cycleCasebookTab(int delta) {
    int at = static_cast<int>(casebookTab_) + delta;
    while (at < 0) {
        at += kCasebookTabCount;
    }
    casebookTab_ = static_cast<CasebookTab>(at % kCasebookTabCount);
}

bool Session::selectCasebookLead(std::string_view leadId) {
    const sim::Casebook& book = activeCasebook();
    if (book.raws() == nullptr) {
        return false;
    }
    const std::int32_t want = book.raws()->indexOf(leadId);
    if (want < 0) {
        return false;
    }
    const std::vector<std::int32_t> heard = book.known();
    for (std::size_t i = 0; i < heard.size(); ++i) {
        if (heard[i] == want) {
            casePageCursor_ = static_cast<int>(i);
            return true;
        }
    }
    // IN THE FILE BUT NOT IN THE BOOK is a real answer and not a typo: a lead
    // the player has not been told about is not selectable, which is the whole
    // gate the investigation runs on.
    return false;
}

bool Session::showLeadOnMap(std::int32_t leadIndex) {
    const int place = mapPlaceForLead(leadIndex);
    if (place < 0) {
        return false;
    }
    // THE HANDOFF, AND IT IS THE MAP'S OWN CURSOR. The map pass made a PLACE
    // the unit of selection, gave it a plan with the names inside the shapes, a
    // detail pane and a People view; the casebook's answer to "where is Harl's
    // Yard" is that page, arrived at from the book, rather than a second
    // navigation surface that would drift from it.
    //
    // The stand-down list is toggleDistrictMap()'s own, minus the part that
    // would undo the point: that method puts the cursor where the BODY is
    // standing, which is exactly what a player who just pressed "show me" does
    // not want.
    casebookOpen_ = false;
    keysOpen_ = false;
    grimoireOpen_ = false;
    waitOpen_ = false;
    optionsOpen_ = false;
    pauseOpen_ = false;
    quitArmed_ = false;
    awaitingKey_ = false;
    menuFocus_ = kMenuFocusJournal;
    districtMapOpen_ = true;
    districtMapSelected_ = place;
    // OVERVIEW, ALWAYS, whatever tab the map was left on. The question this
    // route asks is "where is it", and Overview is the view that answers it --
    // arriving on the Legend because that is where the map was last put down
    // would be the page answering somebody else's question.
    districtMapTab_ = MapTab::Overview;
    districtMapDetailFirst_ = 0;
    firstRun_ = false;
    syncPanelAnim();
    return true;
}

void Session::commitCasebookLead() {
    if (casebookTab_ == CasebookTab::Cases) {
        // THE PULL PACK: READ IT -- the shelf's commit fronts the highlighted
        // book for the page and steps back onto its leads. A questline row
        // or a book not yet in hand is refused out loud (the pane already
        // says so), the page staying up.
        const std::vector<CasebookShelfRow> shelf = casebookShelfRows();
        if (shelf.empty()) {
            return;
        }
        const int at = std::clamp(caseShelfCursor_, 0, static_cast<int>(shelf.size()) - 1);
        const CasebookShelfRow& row = shelf[static_cast<std::size_t>(at)];
        if (!row.book || row.bookId < 0 || row.bookId >= kCaseBookCount) {
            notePage(row.errand ? std::string(kErrandFindLine)
                                : std::string("A LINE, NOT A BOOK. THE JOURNAL HAS IT."));
            if (audio_ != nullptr) {
                audio_->playOneShot(audio::SoundId::UiError);
            }
            return;
        }
        if (frontCase(static_cast<CaseBookId>(row.bookId))) {
            casebookTab_ = CasebookTab::Leads;
            if (audio_ != nullptr) {
                audio_->playOneShot(audio::SoundId::BookFlip);
            }
        }
        return;
    }
    const sim::Casebook& book = activeCasebook();
    const std::vector<std::int32_t> heard = book.known();
    if (heard.empty() || book.raws() == nullptr) {
        return;
    }
    const int at = std::clamp(casePageCursor_, 0, static_cast<int>(heard.size()) - 1);
    const std::int32_t index = heard[static_cast<std::size_t>(at)];
    const sim::Lead& lead = book.raws()->leads()[static_cast<std::size_t>(index)];
    // STATE CHOOSES THE VERB, and the page has already printed which one this
    // is going to be -- see drawLeadDetail. Standing in reach of a lead nobody
    // has stood over yet, the useful act is to LOOK; the map would be the page
    // telling you to go where you already are.
    if (bodyCanLookAt(lead) && book.state(index) == sim::LeadState::Open) {
        // CLOSED FIRST, because examine() refuses while the book is up (it
        // treats the look key as "put the notes down") -- so the page hands
        // over rather than fighting for the same key.
        casebookOpen_ = false;
        syncPanelAnim();
        examine();
        return;
    }
    (void)showLeadOnMap(index);
}

CasebookPageState Session::casebookPageState() const {
    // COURIER CASE. THE PAGE SHOWS THE LIVE ERRAND while one runs and the
    // Bloodletter otherwise -- sheetCaseLive()'s one rule, no switcher UI
    // (flagged scope cut). Every row, cross-reference and readout below is
    // built off this pair, so the page can never mix two books' indices.
    const sim::CasebookRaws& raws = activeCaseRaws();
    const sim::Casebook& book = activeCasebook();
    CasebookPageState page;
    page.open = true;
    page.openAmount = panelAnim_.value();
    page.title = "THE CASEBOOK";
    page.caseTitle = std::string(raws.title());
    page.tab = casebookTab_;
    page.hook = std::string(raws.hook());
    page.closeLine = std::string(raws.close());
    page.closed = book.closed();
    page.dread = book.dread();
    page.dreadBand = std::string(raws.dreadLabel(book.dread()));
    page.read = book.readCount();
    page.cold = book.coldCount();
    page.calledYou = std::string(legend().title());
    // SHIP NOTE MOVE 3. NOT promptLabel(Menu, Pad) for the pad's close key,
    // deliberately: Menu's pad half is D-PAD UP, but while the book is open
    // the parity pass reads the D-pad as list movement, raw, ahead of the
    // binding (route_menu_key's own header) -- so the key that ACTUALLY
    // closes the book on a pad is B, the universal back. The keyboard half
    // keeps the live Menu binding, exactly as before.
    page.closeKey =
        promptDevice_ == InputDevice::Pad
            ? std::string(promptBackKey(InputDevice::Pad))
            : std::string(promptLabel(controls_, Action::Menu, InputDevice::KeyboardMouse));
    // NINE AND THE STICKS: the page keys walk on to the ward map and the
    // grimoire from this page (the Journal tile of NOTES) -- said on the
    // foot for both hands, `< >` and LB RB. And the sub-tab keycap the other
    // pages print (TAB / LT RT) for the LEADS / THE CASE swap, ONE keycap
    // everywhere; the arrows and the D-pad still work and the foot's cross
    // already says so under LEAD.
    page.navPageKeys = std::string(promptPageKeys(promptDevice_));
    page.navTabKeys = std::string(promptTabKeys(promptDevice_));
    page.lookKey = std::string(promptLabel(controls_, Action::Interact, promptDevice_));
    // The page grammar's confirm, in the live hand's vocabulary -- "ENTER" /
    // "A" -- for the commit verb and the nav band's GO TO IT. The last two
    // keyboard literals on this page rode along as "ENTER" until now. (Both
    // lanes wrote this line; one field, commitKey, survives the merge.)
    page.commitKey = std::string(promptConfirmKey(promptDevice_));
    // THE PULL PACK: the FOLLOW verb's key -- F on a keyboard (a raw page
    // key, the map's own T precedent), X on a pad -- the one face button
    // unclaimed on this page (A is the commit, B backs out) and, since NINE
    // AND THE STICKS moved Attack's own pad half off X onto RT, no longer a
    // button any live verb claims at all. Fixed to PadWest rather than
    // derived from Action::Attack's current binding, so this page's own key
    // doesn't silently follow Attack wherever a future rebind moves it.
    page.followKey = promptDevice_ == InputDevice::Pad
                         ? std::string(promptKeyName(Key::PadWest))
                         : std::string("F");
    page.shelf = casebookShelfRows();
    page.shelfCursor =
        page.shelf.empty() ? 0
                           : std::clamp(caseShelfCursor_, 0, static_cast<int>(page.shelf.size()) - 1);
    page.shelfBookTotal = kCaseBookCount;
    // THE FOLLOW VERB ANSWERS WHERE IT CAN BE READ: the page's own header
    // band, for the plate's own hold. A bouncer's warning still outranks it
    // (drawFrame overwrites the field while one stands).
    if (pageNoteSteps_ > 0 && !pageNote_.empty()) {
        page.alert = pageNote_;
    }
    const PullTarget followed = pullTarget();
    const CaseBookId fronted = frontedBook();

    const std::vector<std::int32_t> heard = book.known();
    page.known = static_cast<std::int32_t>(heard.size());
    page.total = static_cast<std::int32_t>(raws.leads().size());
    page.rows.reserve(heard.size());

    const std::int32_t px = body_->tileX();
    const std::int32_t py = body_->tileY();
    for (const std::int32_t index : heard) {
        const sim::Lead& lead = raws.leads()[static_cast<std::size_t>(index)];
        const sim::LeadState what = book.state(index);
        CasebookLeadRow row;
        row.brief = lead.brief.empty() ? lead.place : lead.brief;
        row.place = lead.place;
        row.what = lead.what;
        row.close = lead.close;
        row.followed = followed.set && followed.book == fronted && followed.lead == index;
        row.chosen = row.followed && followed.chosen;
        row.state = what == sim::LeadState::Followed ? CasebookLeadState::Followed
                    : what == sim::LeadState::Cold   ? CasebookLeadState::Cold
                                                     : CasebookLeadState::Open;
        if (!lead.who.empty() && who_) {
            if (const sim::Notable* person = who_->find(lead.who); person != nullptr) {
                row.who = shoutName(person->name);
                row.whoWhat = shoutName(person->epithet);
            }
        }
        // THE CLUE IS NOT SHOWN UNTIL IT HAS BEEN STOOD OVER, and that is the
        // same rule the crosshair pass wrote down: a book that printed
        // `found`/`detail` for an Open lead would hand the player the answer
        // for having been TOLD the lead exists, which turns an investigation
        // into a reading exercise. Nothing on this page calls look().
        if (what != sim::LeadState::Open) {
            row.found = lead.found;
            row.detail = lead.detail;
            for (const std::string& id : lead.opens) {
                const std::int32_t opened = raws.indexOf(id);
                if (opened < 0) {
                    continue;
                }
                const sim::Lead& next = raws.leads()[static_cast<std::size_t>(opened)];
                row.opened.push_back(next.brief.empty() ? next.place : next.brief);
            }
        }
        row.heard = formatCaseDay(book.heardAt(index));
        for (const std::int32_t from : raws.openedBy(index)) {
            const sim::Lead& opener = raws.leads()[static_cast<std::size_t>(from)];
            if (!row.from.empty()) {
                row.from += ", ";
            }
            row.from += opener.brief.empty() ? opener.place : opener.brief;
        }
        row.here = bodyCanLookAt(lead);
        // THE BEARING: pull.hpp's pullBearing, THE ONE arithmetic -- to the
        // lead's own site, Euclidean paces, BAND n off-plane -- so this
        // page and the ribbon line under the compass print the same string
        // for the same lead. (Not "HERE" on this row: the commit verb words
        // that itself off row.here.)
        row.bearing = pullBearing(px, py, body_->band(), lead.site, false);
        row.routable = mapPlaceForLead(index) >= 0;
        page.rows.push_back(std::move(row));
    }

    page.cursor = page.rows.empty()
                      ? 0
                      : std::clamp(casePageCursor_, 0, static_cast<int>(page.rows.size()) - 1);
    // THE READOUT: how far the case has got and what the ward's nerve is at.
    // The reference's persistent resource readout, and on this surface the
    // resource being spent is the investigation itself.
    page.readout = "READ " + std::to_string(page.read) + "/" + std::to_string(page.known) +
                   "   DREAD " + std::to_string(page.dread);
    // THE INSTRUCTION ROW SAYS WHAT THE PAGE IS FOR rather than repeating the
    // tab row, and it changes with the state of the book because the useful
    // sentence changes with it.
    if (page.rows.empty()) {
        page.instruction = "NOTHING IN THE BOOK YET.";
    } else if (page.closed) {
        page.instruction = "THE TRAIL IS WALKED OUT.";
    } else if (page.read == 0) {
        page.instruction = "PICK A LEAD. " + std::string(promptConfirmKey(promptDevice_)) +
                           " SHOWS YOU WHERE.";
    } else {
        const std::int32_t waiting = page.known - page.read > 0 ? page.known - page.read : 0;
        page.instruction = std::to_string(waiting) +
                           (waiting == 1 ? " LEAD IS STILL WAITING TO BE WALKED TO."
                                         : " LEADS ARE STILL WAITING TO BE WALKED TO.");
    }
    return page;
}

}  // namespace granadad::render
