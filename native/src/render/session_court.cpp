// Session justice: serving a sentence, the court and hearing page state, the rope ceremony.
#include "session_internal.hpp"

// THE AUDIO WIRING PASS, as in session.cpp: this file speaks to the audio engine in ids only; see
// setAudio() in session.hpp and the determinism note in audio_engine.hpp.
#include "granadad/audio/audio_engine.hpp"

namespace granadad::render {

const sim::Tavern::SentenceReport& Session::serveSentence() {
    // JUSTICE BUILD (SENTENCES LANE). A sentence is a clock jump with a
    // record behind it, and it rides the identical pair every other jump
    // does: the room's own verb, then syncClockAfterSkip() -- the ward's
    // roll to dayNumber() (a day in a cell is a day the land grows, the wage
    // is paid and, on a quarter, the ground penny falls) and the people to
    // the clock. The room fires the arrest's release for the body; step()
    // reads it exactly as it reads the release after the arrest. THE ROPE
    // jumps nothing and releases nothing, so nothing is synced: the clock is
    // where it was, and the end is on tavern().runEnd().
    const sim::Tavern::SentenceReport& served = tavern_->serveSentence();
    if (served.served && !served.terms.rope) {
        syncClockAfterSkip();
    }
    return served;
}

namespace {

// ---------------------------------------------------------------------------
// JUSTICE BUILD (HEARING PAGE LANE): the court, the rope, the end
// ---------------------------------------------------------------------------
//
// Spec sections 5 and 6, as presentation over the SIM the two lanes before
// this one built: the hearing on the crime ledger (justice.hpp's HearingState,
// hashed and codec'd), Tavern::plead as the stepped input, Tavern::serveSentence
// as the clock and the coin, tavern().runEnd() as the end. Nothing here rolls,
// nothing here decides; this file reads the record, draws it in the terminal
// register, and turns keypresses into the two stepped inputs the sim owns.
/// The count words the reading uses: "TWO LIFTS", "THREE SAW IT". One is the
/// article ("A LIFT"), so it is the caller's; past twelve the figure is the
/// word, as the census's own tallies are.
[[nodiscard]] std::string countWord(std::int32_t n) {
    static constexpr std::string_view kWords[] = {
        "",     "ONE", "TWO",   "THREE", "FOUR",  "FIVE",   "SIX",
        "SEVEN", "EIGHT", "NINE", "TEN", "ELEVEN", "TWELVE",
    };
    if (n >= 1 && n <= 12) {
        return std::string(kWords[n]);
    }
    return std::to_string(n);
}

/// "THE FOURTH DAY" -- the ordinal the plate reads. Day one, not day zero:
/// formatCaseDay's own translation, applied to the ordinal here. Past what
/// the words carry the plate says the figure, which is still the ward's roll.
[[nodiscard]] std::string ordinalWord(std::int32_t day) {
    static constexpr std::string_view kOnes[] = {
        "",        "FIRST",   "SECOND",     "THIRD",      "FOURTH",    "FIFTH",     "SIXTH",
        "SEVENTH", "EIGHTH",  "NINTH",      "TENTH",      "ELEVENTH",  "TWELFTH",   "THIRTEENTH",
        "FOURTEENTH", "FIFTEENTH", "SIXTEENTH", "SEVENTEENTH", "EIGHTEENTH", "NINETEENTH",
    };
    static constexpr std::string_view kTens[] = {
        "", "", "TWENTY", "THIRTY", "FORTY", "FIFTY", "SIXTY", "SEVENTY", "EIGHTY", "NINETY",
    };
    static constexpr std::string_view kTensOrdinal[] = {
        "",         "",          "TWENTIETH", "THIRTIETH", "FORTIETH",
        "FIFTIETH", "SIXTIETH",  "SEVENTIETH", "EIGHTIETH", "NINETIETH",
    };
    if (day >= 1 && day < 20) {
        return std::string(kOnes[day]);
    }
    if (day >= 20 && day < 100) {
        const int tens = day / 10;
        const int ones = day % 10;
        if (ones == 0) {
            return std::string(kTensOrdinal[tens]);
        }
        return std::string(kTens[tens]) + "-" + std::string(kOnes[ones]);
    }
    return std::to_string(day) + "TH";
}

/// The six acts, in the reading's own words: "A LIFT" / "TWO LIFTS", "A
/// CRACKED BOX" / "TWO CRACKED BOXES". The tally words crime.hpp's own
/// crimeTally uses (lifts, cracks, runs, fences, leans, roofs), spoken.
[[nodiscard]] std::string crimePhrase(sim::Crime crime, std::int32_t count) {
    const bool one = count == 1;
    const std::string n = one ? std::string("A") : countWord(count);
    switch (crime) {
        case sim::Crime::Lift:
            return n + (one ? " LIFT" : " LIFTS");
        case sim::Crime::Burgle:
            return n + (one ? " CRACKED BOX" : " CRACKED BOXES");
        case sim::Crime::Smuggle:
            return n + (one ? " RUN" : " RUNS");
        case sim::Crime::Fence:
            return n + (one ? " FENCED LOT" : " FENCED LOTS");
        case sim::Crime::Extort:
            return n + (one ? " LEAN" : " LEANS");
        case sim::Crime::RoofRun:
            return n + (one ? " ROOF-RUN" : " ROOF-RUNS");
    }
    return n;
}

/// Which rows the page prints, by kind, so the cursor and the digits and the
/// confirm all read one list.
enum class CourtRowKind : std::uint8_t { Guilty, NotGuilty, Paper, Back, NoPlea, Serve };

[[nodiscard]] std::vector<CourtRowKind> courtRowKindsFor(const sim::HearingState& hearing,
                                                          bool paperOpen, bool offered) {
    std::vector<CourtRowKind> kinds;
    if (hearing.awaitingPlea()) {
        if (hearing.sheet.tier == sim::Sentence::Condemned && hearing.sheet.commutedBefore) {
            // MERCY IS GIVEN ONCE. The page opens, the priest speaks, and
            // the one row is I HAVE NOTHING TO SAY.
            kinds.push_back(CourtRowKind::NoPlea);
            return kinds;
        }
        kinds.push_back(CourtRowKind::Guilty);
        kinds.push_back(CourtRowKind::NotGuilty);
        kinds.push_back(CourtRowKind::Paper);
        if (paperOpen) {
            kinds.push_back(CourtRowKind::Back);
        }
        return kinds;
    }
    if (hearing.judged() && offered) {
        kinds.push_back(CourtRowKind::Serve);
    }
    return kinds;
}

/// The judgment as the badge prints it.
[[nodiscard]] std::string judgmentWord(sim::Judgment judgment) {
    switch (judgment) {
        case sim::Judgment::Spared:
            return "SPARED";
        case sim::Judgment::Fined:
            return "FINED";
        case sim::Judgment::Held:
            return "HELD";
        case sim::Judgment::Bound:
            return "BOUND";
        case sim::Judgment::TheHand:
            return "THE HAND";
        case sim::Judgment::Commuted:
            return "COMMUTED";
        case sim::Judgment::TheRope:
            return "THE ROPE";
        case sim::Judgment::None:
            break;
    }
    return {};
}

/// The verdict badge's own accent: SPARED in the number green, the coin and
/// the cell in the page accent, the hand and the rope in the tag's red.
[[nodiscard]] Rgb judgmentAccent(sim::Judgment judgment) {
    switch (judgment) {
        case sim::Judgment::Spared:
            return panelInk().number;
        case sim::Judgment::TheHand:
        case sim::Judgment::Commuted:
        case sim::Judgment::TheRope:
            return Rgb{0.88F, 0.34F, 0.26F};
        default:
            return panelInk().accent;
    }
}

/// The one row after the judgment: what the player does about it. The drop
/// is taken by the player's own hand, like everything else on this page.
[[nodiscard]] std::string serveWord(sim::Judgment judgment) {
    switch (judgment) {
        case sim::Judgment::Spared:
            return "WALK OUT.";
        case sim::Judgment::Fined:
            return "PAY IT.";
        case sim::Judgment::TheRope:
            return "THE DROP.";
        default:
            return "SERVE IT.";
    }
}

/// "TWO NIGHTS." / "SIX NIGHTS." off the cell's hours -- whole nights, one at
/// least: the shipped one-to-three nights doubled reads as its days.
[[nodiscard]] std::string nightsWord(std::int32_t cellHours) {
    const std::int32_t nights = std::max<std::int32_t>(1, cellHours / 24);
    return countWord(nights) + (nights == 1 ? " NIGHT." : " NIGHTS.");
}

[[nodiscard]] std::string daysWord(std::int32_t days) {
    return "BONDSWORN " + std::to_string(days) + (days == 1 ? " DAY." : " DAYS.");
}

/// THE SENTENCE, IN NUMBERS -- the last row of the check block, the way
/// "HELD HARD -- CUDGEL 14-18" states its span. Off the pure branch.
[[nodiscard]] std::string sentenceLine(const sim::SentenceTerms& terms) {
    if (!terms.served) {
        return {};
    }
    std::string out;
    const auto coin = [&terms]() { return std::to_string(terms.finePaid) + " ROYALS."; };
    switch (terms.judgment) {
        case sim::Judgment::Spared:
            return "NOTHING OWED.";
        case sim::Judgment::Fined:
            out = coin();
            break;
        case sim::Judgment::Held:
            out = nightsWord(terms.cellHours) + " " + coin();
            break;
        case sim::Judgment::Bound:
            return daysWord(terms.bondDays);
        case sim::Judgment::TheHand:
            out = "THE HAND. ";
            if (terms.band == sim::Judgment::Bound) {
                return out + daysWord(terms.bondDays);
            }
            out += nightsWord(terms.cellHours) + " " + coin();
            break;
        case sim::Judgment::Commuted:
            return "THE HAND. " + daysWord(terms.bondDays);
        case sim::Judgment::TheRope:
            // Nothing in numbers here: the rope's row is the post and the
            // hour, which the page composes off the clock (hearingPageState)
            // -- the badge already says THE ROPE and the row does not echo it.
            return {};
        case sim::Judgment::None:
            return {};
    }
    if (terms.shortfallDays > 0) {
        out += " " + daysWord(terms.shortfallDays);
    }
    return out;
}

/// The priest's own words for the answer -- the bark tables keyed like the
/// shipped watch.*, rotated on the count of hearings so a second visit to the
/// bench hears a second row.
[[nodiscard]] std::string_view judgmentTable(sim::Judgment judgment) {
    switch (judgment) {
        case sim::Judgment::Spared:
            return "court.spared";
        case sim::Judgment::Fined:
            return "court.fined";
        case sim::Judgment::Held:
            return "court.held";
        case sim::Judgment::Bound:
            return "court.bound";
        case sim::Judgment::TheHand:
            return "court.hand";
        case sim::Judgment::Commuted:
            return "court.commuted";
        case sim::Judgment::TheRope:
            return "court.rope";
        case sim::Judgment::None:
            break;
    }
    return "court.paper";
}

/// The consequence of the hovered row, in the pane beside it -- the
/// informed choice (UI-REFERENCE: "View X's stats"). Fixed literals.
[[nodiscard]] std::string rowConsequence(CourtRowKind kind, sim::Sentence tier) {
    // EACH TIER'S LITERAL SAYS ONLY WHAT IS ON THE TABLE: the rope tier has
    // two answers and names them; a cell or a hand charge has no rope on the
    // table and does not mention one. A confession is never doubled and never
    // spared on any tier; a denial can be either, on the tiers that have
    // either.
    const bool rope = tier == sim::Sentence::Condemned;
    switch (kind) {
        case CourtRowKind::Guilty:
            return rope ? "A CONFESSION IS WEIGHED AS IT IS GIVEN. THE FLAME'S ANSWER IS FIXED "
                          "BEFORE YOU SPEAK IT. MERCY OR THE ROPE, AND NOTHING ELSE."
                        : "A CONFESSION IS WEIGHED AS IT IS GIVEN. THE FLAME'S ANSWER IS FIXED "
                          "BEFORE YOU SPEAK IT. NEVER DOUBLED, AND NEVER SPARED.";
        case CourtRowKind::NotGuilty:
            return rope ? "A DENIAL IS WEIGHED WITH THE PRIEST'S OWN DOUBT IN IT. TEN POINTS "
                          "EITHER WAY. MERCY OR THE ROPE, AND NEVER SPARED."
                        : "A DENIAL IS WEIGHED WITH THE PRIEST'S OWN DOUBT IN IT. TEN POINTS "
                          "EITHER WAY. DENIED AND DISBELIEVED, THE SENTENCE DOUBLES AND THE "
                          "MISSION REMEMBERS THE LIE.";
        case CourtRowKind::Paper:
            return "THE CHARGE, WHAT THE PAPER ASKS, AND WHAT THE PRIEST WILL WEIGH -- IN "
                   "WORDS, BEFORE YOU SPEAK.";
        case CourtRowKind::Back:
            return "BACK TO THE ROWS.";
        case CourtRowKind::NoPlea:
            return "MERCY WAS GIVEN ONCE. NOTHING IS WEIGHED: THE PRIEST SPEAKS, AND THE WARD "
                   "HAS YOU.";
        case CourtRowKind::Serve:
            break;
    }
    return {};
}

/// THE SHEET AS PHRASES, NEVER NUMBERS (spec 3.2): what the priest will
/// weigh, in words, before the plea. The arithmetic is shown after.
[[nodiscard]] std::vector<std::string> paperPhrases(const sim::ChargeSheet& sheet) {
    std::vector<std::string> out;
    if (sheet.blood) {
        out.push_back("BLOOD ON THE PAPER.");
        if (sheet.witnesses > 0) {
            out.push_back(countWord(sheet.witnesses) + " SAW IT.");
        }
    }
    if (sheet.secondRung) {
        out.push_back("THE ROOFS, A SECOND TIME.");
    } else if (sheet.skyrunner && sheet.tier == sim::Sentence::Maimed) {
        out.push_back("A SKYRUNNER'S FIRST.");
    }
    if (sheet.priors <= 0) {
        out.push_back("NEVER TAKEN BEFORE.");
    } else if (sheet.priors == 1) {
        out.push_back("TAKEN ONCE BEFORE.");
    } else if (sheet.priors == 2) {
        out.push_back("TAKEN TWICE BEFORE.");
    } else {
        out.push_back("TAKEN " + countWord(sheet.priors) + " TIMES BEFORE.");
    }
    if (sheet.heatAtArrest - sim::kWarrantAt >= 16) {
        out.push_back("THE PAPER IS HOT.");
    }
    if (sheet.templeStanding >= 52) {
        out.push_back("THE MISSION KNOWS YOU WELL.");
    } else if (sheet.templeStanding > 0) {
        out.push_back("THE MISSION KNOWS YOUR NAME.");
    } else {
        out.push_back("THE MISSION DOES NOT KNOW YOU.");
    }
    if (sheet.reputation >= 10) {
        out.push_back("THE WARD THINKS WELL OF YOU.");
    } else if (sheet.reputation <= -10) {
        out.push_back("THE WARD THINKS ILL OF YOU.");
    }
    if (sheet.streetwise >= 20) {
        out.push_back("A QUICK TONGUE.");
    } else if (sheet.streetwise <= 0) {
        out.push_back("NO TONGUE TO SPEAK OF.");
    }
    if (sheet.condemnedBefore) {
        out.push_back("THE ROPE PASSED YOU ONCE.");
    }
    if (sheet.commutedBefore) {
        out.push_back("MERCY WAS GIVEN ONCE.");
    }
    return out;
}

}  // namespace

bool Session::courtSentenceOffered() const noexcept {
    return courtOpen_ && tavern_->hearing().judged() && courtJudgmentHold_ <= 0;
}

std::vector<std::string> Session::courtRows() const {
    std::vector<std::string> out;
    const HearingPageState page = hearingPageState();
    out.reserve(page.rows.size());
    for (const HearingRow& row : page.rows) {
        out.push_back(row.key + " - " + row.label);
    }
    return out;
}

bool Session::missionArrivalTile(std::int32_t& outX, std::int32_t& outY,
                                 std::int32_t& outBand) const {
    // THE MISSION IS ALREADY A TRAVEL TARGET with an arrival tile: the sign's
    // own aim point (the door you knock on) snapped to standable ground on
    // the place's band -- exactly the travel verb's landing rule, so the
    // Watch's walk lands where a walked-to Mission lands.
    const int at = mapPlaceIndex("Mission of the Flame");
    if (at < 0) {
        return false;
    }
    const MapPlace& place = mapPlaces()[static_cast<std::size_t>(at)];
    std::int32_t aimX = 0;
    std::int32_t aimY = 0;
    mapAimPoint(place, body_->tileX(), body_->tileY(), aimX, aimY);
    if (!travelStandable(*tiles_, aimX, aimY, place.band, &outX, &outY)) {
        return false;
    }
    outBand = place.band;
    return true;
}

void Session::openCourt() {
    if (courtOpen_) {
        return;
    }
    // CUSTODY. Whatever the player was doing, they are not doing it: the wire
    // out of the lock, the conversation ended, every page and the pause menu
    // down. The TAKEN line follows and outranks any of theirs.
    if (picking()) {
        tavern_->abandonPick();
    }
    if (talking()) {
        tavern_->endConversation();
        wardTalkId_ = -1;
        topicCursor_ = 0;
        topicPage_ = 0;
        haggleOffer_ = 0;
        forgeOpen_ = false;
    }
    dismissOverlays();
    // THE WALK ELIDED. The body to the Mission's arrival tile through
    // placeBodyAt (a relocation like any other: the room you were taken in
    // must not ghost over the Mission's door), the seam dipped to black --
    // the same cloth every scripted cut wears -- and the arrival line.
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t band = 0;
    if (missionArrivalTile(x, y, band)) {
        placeBodyAt(x, y, band);
    }
    awaitingLanding_ = false;
    syncTavernToBody();
    dressInstantCut();
    courtOpen_ = true;
    courtCursor_ = 0;
    courtPaperOpen_ = false;
    courtArmed_ = -1;
    courtServing_ = false;
    // A hearing already judged (a run reopened between the plea and the
    // sentence) holds its badge the same beat a fresh judgment does.
    courtJudgmentHold_ = tavern_->hearing().judged() ? sim::kJudgmentHoldSteps : 0;
    say("TAKEN TO THE MISSION. " + travelClockText(timeOfDay_) + ".");
    syncPanelAnim();
}

void Session::closeCourt() {
    courtOpen_ = false;
    courtServing_ = false;
    courtArmed_ = -1;
    courtPaperOpen_ = false;
    courtJudgmentHold_ = 0;
    courtCursor_ = 0;
    // TURNED LOOSE. Where the sentence left the body: SPARED and FINED walk
    // out of the Mission's door where they stand; everything that cost a
    // cell ends on the shipped Tarwalk, exactly as the paperless search
    // does. The line prints the day and the clock face the sentence ended
    // on -- day one, not day zero, formatCaseDay's own translation.
    const sim::Tavern::SentenceReport& served = tavern_->lastServed();
    const std::string when = "DAY " + std::to_string(served.dayReleased + 1) + ". " +
                             travelClockText(served.timeReleased) + ".";
    if (served.served && served.terms.releaseHere) {
        say("TURNED LOOSE AT THE MISSION'S DOOR. " + when);
    } else {
        placeBodyAt(sim::gull::kStreetX, sim::gull::kStreetY, sim::gull::kGroundBand);
        awaitingLanding_ = false;
        syncTavernToBody();
        dressInstantCut();
        say("TURNED LOOSE ON THE TARWALK. " + when);
    }
    syncPanelAnim();
}

HearingPageState Session::hearingPageState() const {
    HearingPageState out;
    out.open = courtOpen_;
    if (!courtOpen_) {
        return out;
    }
    const sim::HearingState& hearing = tavern_->hearing();
    const sim::ChargeSheet& sheet = hearing.sheet;
    const sim::CrimeLedger& crimes = tavern_->dialogue().crimes();
    const sim::BarkTables& barks = tavern_->dialogue().barks();
    const InputDevice dev = promptDevice_;
    out.confirmKey = std::string(promptConfirmKey(dev));
    out.backKey = std::string(promptBackKey(dev));
    out.tutor = courtTutor_.value();
    out.commitPulse = commitPulse_.value();
    // THE READOUT: the day and the live clock (UI-EA-SPEC sec. 4 #10), the
    // two facts a man at the bench wants -- how long he has been in the
    // ward, and what hour the sentence starts from.
    out.readout = "DAY " + std::to_string(tavern_->dayNumber() + 1) + "  " +
                  travelClockText(timeOfDay_);

    // --- the reading ------------------------------------------------------
    out.laid = (hearing.officer.empty() ? std::string("THE WATCH") : upperAscii(hearing.officer)) +
               " LAYS THE PAPER ON THE TABLE.";
    if (sheet.blood) {
        const std::string slain = tavern_->slainName();
        // STREET SENSES leg (b): WHERE. A killing on the quay is read as the
        // street, not the Gull's boards.
        out.charge = "THE WARD SAYS YOU PUT " + (slain.empty() ? std::string("A MAN") : upperAscii(slain)) +
                     (tavern_->slainOnStreet() ? " DOWN IN THE STREET." : " DOWN IN THE GILDED GULL.");
        if (sheet.witnesses > 0) {
            out.charge += " " + countWord(sheet.witnesses) + " SAW IT.";
        }
    } else {
        // Everything new since the bench last heard you, in the ledger's own
        // order -- "TWO LIFTS AND A CRACKED BOX" reads the way the sheet is
        // written; the worst line is what the paper ASKS off, not a sorting.
        std::vector<std::string> parts;
        for (std::size_t c = 0; c < sim::kSheetCrimes; ++c) {
            if (sheet.since[c] <= 0) {
                continue;
            }
            parts.push_back(crimePhrase(static_cast<sim::Crime>(c), sheet.since[c]));
        }
        if (parts.empty()) {
            out.charge = sheet.secondRung ? std::string("THE WARD HAS YOU FOR THE ROOFS, A SECOND TIME.")
                                          : std::string("THE WARD HAS PAPER ON YOU.");
        } else {
            std::string list;
            for (std::size_t i = 0; i < parts.size(); ++i) {
                if (i > 0) {
                    list += (i + 1 == parts.size()) ? " AND " : ", ";
                }
                list += parts[i];
            }
            out.charge = "THE WARD HAS YOU FOR " + list + ".";
        }
    }
    switch (sheet.tier) {
        case sim::Sentence::Maimed:
            out.asks = "THE PAPER ASKS FOR THE HAND.";
            break;
        case sim::Sentence::Condemned:
            out.asks = "THE PAPER ASKS FOR THE ROPE.";
            break;
        default:
            out.asks = "THE PAPER ASKS FOR A CELL.";
            break;
    }

    // THE OFFICER WHO WALKED YOU IN, and his line as he lays the paper
    // (court.taken), off the record's own officer and rotated with the
    // priest's opening -- one hearing, one rotation, both voices. He stands
    // by the wall UNTIL THE ANSWER: his walking-in line is said as the paper
    // is laid, and a judged page has nothing of his on it -- the bench has
    // spoken, and "sit where he points" beside a sentence is a line from
    // before the sentence.
    if (!hearing.officer.empty() && hearing.awaitingPlea()) {
        out.officerName = upperAscii(hearing.officer);
        out.officerSays = std::string(barks.line("court.taken", crimes.hearings()));
    }

    // --- the rows ---------------------------------------------------------
    const bool offered = courtSentenceOffered();
    const std::vector<CourtRowKind> kinds = courtRowKindsFor(hearing, courtPaperOpen_, offered);
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        HearingRow row;
        switch (kinds[i]) {
            case CourtRowKind::Guilty:
                row.key = "1";
                row.label = "I DID IT.";
                break;
            case CourtRowKind::NotGuilty:
                row.key = "2";
                row.label = "I DID NOT.";
                break;
            case CourtRowKind::Paper:
                row.key = "3";
                row.label = "HEAR THE PAPER";
                break;
            case CourtRowKind::Back:
                row.key = "0";
                row.label = "BACK";
                row.accent = panelInk().dim;
                break;
            case CourtRowKind::NoPlea:
                row.key = "1";
                row.label = "I HAVE NOTHING TO SAY.";
                break;
            case CourtRowKind::Serve:
                row.key = "1";
                row.label = serveWord(hearing.judgment);
                row.accent = judgmentAccent(hearing.judgment);
                break;
        }
        // THE ARMED ROW NAMES THE DEVICE'S OWN CONFIRM -- the QUIT row's
        // exact shape: "1 - I DID IT. -- SURE? ENTER".
        if (static_cast<int>(i) == courtArmed_) {
            row.label += " -- SURE? " + out.confirmKey;
        }
        out.rows.push_back(std::move(row));
    }
    out.cursor = kinds.empty() ? 0 : std::clamp(courtCursor_, 0, static_cast<int>(kinds.size()) - 1);
    out.armed = courtArmed_;

    // --- the detail pane ----------------------------------------------------
    // ROTATED ON THE HEARINGS BEFORE THIS ONE: plead() counts the hearing
    // the moment the plea lands, so the verdict's row reads the count back
    // by one -- the opening and the verdict of one hearing rotate together,
    // and the first hearing hears row zero of both.
    const std::int32_t rotation =
        hearing.judged() ? std::max<std::int32_t>(0, crimes.hearings() - 1) : crimes.hearings();
    const auto bark = [&barks, rotation](std::string_view key) {
        return std::string(barks.line(key, rotation));
    };
    if (hearing.awaitingPlea()) {
        out.view = courtPaperOpen_ ? HearingView::Paper : HearingView::Plea;
        // THE PRIEST OPENS, BY THE CASE, and the rotation runs only within
        // the case: every row of the table chosen is true of the sheet in
        // front of him. A commuted man back for the rope; a killing; a
        // Skyrunner's second rung (the rope with no corpse) or his first (the
        // hand); and a thief -- one the Mission knows from its own door, or
        // one it has never seen (the sheet's own THE DOOR term, and HEAR THE
        // PAPER's own phrase, decide which).
        std::string_view opening = sheet.templeStanding > 0 ? "court.paper.door" : "court.paper";
        if (sheet.tier == sim::Sentence::Condemned && sheet.commutedBefore) {
            opening = "court.nothing";
        } else if (sheet.blood) {
            opening = "court.blood";
        } else if (sheet.skyrunner && sheet.secondRung) {
            opening = "court.roofs.rope";
        } else if (sheet.skyrunner && sheet.tier == sim::Sentence::Maimed) {
            opening = "court.roofs.hand";
        }
        out.priest = bark(opening);
        // THE PRIEST PRESSES once a plea is armed (court.plead): the "-- SURE?"
        // beat in his own mouth, the question the row is about to answer.
        // A commuted man's one row is not a plea and the opening stands.
        if (courtArmed_ >= 0 && courtArmed_ < static_cast<int>(kinds.size())) {
            const CourtRowKind armedKind = kinds[static_cast<std::size_t>(courtArmed_)];
            if (armedKind == CourtRowKind::Guilty || armedKind == CourtRowKind::NotGuilty) {
                out.priest = bark("court.plead");
            }
        }
        if (!kinds.empty()) {
            out.consequence = rowConsequence(kinds[static_cast<std::size_t>(out.cursor)], sheet.tier);
        }
        out.paper = paperPhrases(sheet);
        out.backVerb = courtPaperOpen_ ? "THE ROWS" : (courtArmed_ >= 0 ? "DISARM" : "");
        return out;
    }
    if (hearing.judged()) {
        out.view = HearingView::Judged;
        // THE CHECK BLOCK, RE-DERIVED from the hashed record: the weighing is
        // a pure function of the sheet and the plea (weighArraignment), so
        // the page prints the same lines the sim scored, whenever it is
        // drawn, without the sim keeping a term list.
        // THE SUM READS "MAKES" -- the 4x6 font carries no `=` and the font
        // is not touched (the brief's own line); the ward's arithmetic is
        // spoken, not typeset. The plea's own term (CONFESSED / THE PRIEST IS
        // A MAN) rides the terms list the sim returns and is printed on its
        // OWN line under the record's sum, the reference's threshold-then-
        // verdict order.
        // EACH TERM IS ONE TOKEN with its sign glued on ("- 20 THE WARD"),
        // and the sum is a token of its own ("MAKES 27"): the pane packs the
        // tokens by row and never splits one, so a sign cannot wrap away
        // from its term at any width. The joined string is the same record
        // for the summary and the suite.
        const sim::Arraignment answer = sim::weighArraignment(sheet, hearing.plea);
        std::vector<std::string> terms;
        for (const sim::ArraignmentTerm& term : answer.terms) {
            if (term.name == sim::kTermConfessed || term.name == sim::kTermPriestIsAMan) {
                continue;
            }
            const std::string name = term.name == sim::kTermSawIt
                                         ? countWord(term.count) + " " + std::string(term.name)
                                         : std::string(term.name);
            if (terms.empty()) {
                terms.push_back(std::to_string(term.value) + " " + name);
            } else {
                terms.push_back(std::string(term.value >= 0 ? "+ " : "- ") +
                                std::to_string(std::abs(term.value)) + " " + name);
            }
        }
        if (!terms.empty()) {
            terms.push_back("MAKES " + std::to_string(answer.weight));
        }
        const auto joined = [](const std::vector<std::string>& tokens) {
            std::string out;
            for (const std::string& token : tokens) {
                if (!out.empty()) {
                    out += ' ';
                }
                out += token;
            }
            return out;
        };
        out.arithmeticTerms = terms;
        out.arithmetic = joined(terms);
        std::vector<std::string> pleaTerms;
        switch (answer.plea) {
            case sim::Plea::Guilty:
                pleaTerms.push_back("+ " + std::to_string(answer.pleaTerm) + " " +
                                    std::string(sim::kTermConfessed));
                pleaTerms.push_back("MAKES " + std::to_string(answer.scored));
                break;
            case sim::Plea::NotGuilty:
                pleaTerms.push_back(std::string(answer.pleaTerm >= 0 ? "+ " : "- ") +
                                    std::to_string(std::abs(answer.pleaTerm)) + " " +
                                    std::string(sim::kTermPriestIsAMan));
                pleaTerms.push_back("MAKES " + std::to_string(answer.scored));
                break;
            default:
                break;
        }
        out.pleaTerms = pleaTerms;
        out.pleaTerm = joined(pleaTerms);
        // THE LINES THE SCORE WAS READ AGAINST -- and only those. SPARED is a
        // denial's line and no other plea's (paperBand): a confession's
        // block does not print a line it could never have crossed. The rope
        // tier has the one line. Nothing weighed, nothing printed.
        if (answer.plea == sim::Plea::Guilty || answer.plea == sim::Plea::NotGuilty) {
            if (sheet.tier == sim::Sentence::Condemned) {
                out.lines = "THE LINE: " + std::to_string(sim::kMercyLine) + " MERCY";
            } else {
                // Each tier's own ladder (justice.cpp's substitution): on the
                // PAPER tier 38 buys the fine and 14 the cell; on THE HAND
                // tier 38 buys HELD with the hand spared and 14 THE HAND
                // with nights -- there is no FINED on it, so the row does
                // not print one.
                const bool handTier = sheet.tier == sim::Sentence::Maimed;
                out.lines = "THE LINES: ";
                if (answer.plea == sim::Plea::NotGuilty) {
                    out.lines += std::to_string(sim::kSparedLine) + " SPARED  ";
                }
                out.lines += std::to_string(sim::kFinedLine) + (handTier ? " HELD  " : " FINED  ") +
                             std::to_string(sim::kHeldLine) + (handTier ? " THE HAND" : " HELD");
            }
        }
        out.verdict = judgmentWord(hearing.judgment);
        out.verdictAccent = judgmentAccent(hearing.judgment);
        out.sentence = sentenceLine(sim::sentenceTerms(hearing, tavern_->playerCoin()));
        if (hearing.judgment == sim::Judgment::TheRope) {
            // THE ROPE'S ROW: where and when, the plate's own two facts --
            // the post the ward hangs a man at and the hour on the clock --
            // in the place the other judgments state their nights and coin.
            out.sentence = std::string(sim::kRopePlace) + ". " + travelClockText(timeOfDay_) + ".";
        }
        out.priest = hearing.doubled ? bark("court.lie") : bark(judgmentTable(hearing.judgment));
        out.backVerb.clear();
        return out;
    }
    return out;
}

void Session::moveCourtCursor(int delta) {
    if (!courtOpen_) {
        return;
    }
    // MOVING THE CURSOR DISARMS THE PLEA -- movePauseCursor's own rule for
    // QUIT: a player who backed off the row plainly changed their mind.
    courtArmed_ = -1;
    const std::vector<CourtRowKind> kinds =
        courtRowKindsFor(tavern_->hearing(), courtPaperOpen_, courtSentenceOffered());
    const int count = static_cast<int>(kinds.size());
    if (count <= 0) {
        return;
    }
    if (audio_ != nullptr && delta != 0) {
        audio_->playOneShot(audio::SoundId::UiTick);
    }
    courtCursor_ = ((std::clamp(courtCursor_, 0, count - 1) + delta) % count + count) % count;
}

void Session::chooseCourtVisibleRow(int slot) {
    if (!courtOpen_) {
        return;
    }
    const std::vector<CourtRowKind> kinds =
        courtRowKindsFor(tavern_->hearing(), courtPaperOpen_, courtSentenceOffered());
    if (slot < 0) {
        // THE ZERO KEY: the digit BACK prints while the paper is open, and
        // nothing otherwise.
        for (std::size_t i = 0; i < kinds.size(); ++i) {
            if (kinds[i] == CourtRowKind::Back) {
                courtCursor_ = static_cast<int>(i);
                chooseCourtRow();
                return;
            }
        }
        return;
    }
    // THE PRINTED DIGIT PICKS THE ROW IT PRINTS. BACK prints 0, never a
    // slot digit, so the slots address the rows above it only.
    int printed = 0;
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        if (kinds[i] == CourtRowKind::Back) {
            continue;
        }
        if (printed == slot) {
            // A digit on a row other than the armed one disarms it -- the
            // pause menu's own "picking a different row calls it off".
            if (courtArmed_ >= 0 && courtArmed_ != static_cast<int>(i)) {
                courtArmed_ = -1;
            }
            courtCursor_ = static_cast<int>(i);
            chooseCourtRow();
            return;
        }
        ++printed;
    }
}

void Session::chooseCourtRow() {
    if (!courtOpen_) {
        return;
    }
    const sim::HearingState& hearing = tavern_->hearing();
    const std::vector<CourtRowKind> kinds =
        courtRowKindsFor(hearing, courtPaperOpen_, courtSentenceOffered());
    if (kinds.empty()) {
        return;
    }
    const int at = std::clamp(courtCursor_, 0, static_cast<int>(kinds.size()) - 1);
    courtCursor_ = at;
    // AUDIO: the press is what is acknowledged, arms and refusals included.
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::UiConfirm);
    }
    switch (kinds[static_cast<std::size_t>(at)]) {
        case CourtRowKind::Paper:
            courtArmed_ = -1;
            courtPaperOpen_ = true;
            return;
        case CourtRowKind::Back:
            courtArmed_ = -1;
            courtPaperOpen_ = false;
            // Back to the row that opened the paper.
            courtCursor_ = 2;
            return;
        case CourtRowKind::Guilty:
        case CourtRowKind::NotGuilty:
        case CourtRowKind::NoPlea: {
            // ARMED ON THE FIRST PRESS, CONFIRMED ON THE SECOND -- the QUIT
            // pattern, so a leaned-on ENTER cannot plead.
            if (courtArmed_ != at) {
                courtArmed_ = at;
                return;
            }
            courtArmed_ = -1;
            const CourtRowKind kind = kinds[static_cast<std::size_t>(at)];
            const sim::Plea plea = kind == CourtRowKind::Guilty      ? sim::Plea::Guilty
                                   : kind == CourtRowKind::NotGuilty ? sim::Plea::NotGuilty
                                                                     : sim::Plea::NoPlea;
            // THE PLEA: the sim's own stepped input. Recorded on the watch
            // tape like every other verb the page owns.
            const sim::Arraignment answer = tavern_->plead(plea);
            courtPaperOpen_ = false;
            courtCursor_ = 0;
            if (answer.heard) {
                // The badge lands with the commit beat and is HELD before the
                // row that serves it is offered.
                commitPulse_.trigger();
                courtJudgmentHold_ = sim::kJudgmentHoldSteps;
            }
            return;
        }
        case CourtRowKind::Serve: {
            // THE SENTENCE, SERVED THROUGH THE WAIT MACHINERY (Session::
            // serveSentence): the coin, the clock, the mirror, the record,
            // and the arrest's own release -- which the next step() reads and
            // closes the page on. THE ROPE serves no clock and fires no
            // release: the page comes down here and the ceremony goes up.
            commitPulse_.trigger();
            courtServing_ = true;
            const sim::Tavern::SentenceReport& served = serveSentence();
            if (!served.served) {
                courtServing_ = false;
                return;
            }
            if (served.terms.rope) {
                courtServing_ = false;
                courtOpen_ = false;
                courtArmed_ = -1;
                courtPaperOpen_ = false;
                courtJudgmentHold_ = 0;
                armRopeCeremony();
                syncPanelAnim();
            }
            return;
        }
    }
}

void Session::courtBack() {
    if (!courtOpen_) {
        return;
    }
    // THE GRAMMAR EXCEPTION: ESC disarms an armed plea, closes the paper,
    // and does nothing else. There is no leaving the bench.
    if (courtArmed_ >= 0) {
        courtArmed_ = -1;
        return;
    }
    if (courtPaperOpen_) {
        courtPaperOpen_ = false;
        courtCursor_ = 2;
    }
}

bool Session::routeCourtKey(Key key) {
    if (!inCustody() || key == Key::None) {
        return false;
    }
    if (awaitingKey_) {
        return false;
    }
    const Action action = controls_.actionFor(key);
    const bool up = key == Key::Up || key == Key::PadUp || action == Action::Forward ||
                    action == Action::QuickPrev;
    const bool downward = key == Key::Down || key == Key::PadDown || action == Action::Back ||
                          action == Action::QuickNext;
    const bool confirm = key == Key::Enter || key == Key::PadSouth || action == Action::Interact;
    const bool back = key == Key::Escape || key == Key::PadEast;
    const int slotBase = static_cast<int>(Action::QuickSlot1);
    const int slot = static_cast<int>(action) - slotBase;
    const bool numbered = slot >= 0 && slot < 9;
    const bool zero = action == Action::QuickSlot0;
    if (ropeRowsUp_) {
        // THE END ROWS: the only live input. ESC disarms an armed row and
        // does nothing else on the plate, PAUSE is refused, and nothing
        // reaches the world.
        if (up) {
            moveRopeCursor(-1);
        } else if (downward) {
            moveRopeCursor(1);
        } else if (confirm) {
            chooseRopeRow();
        } else if (numbered) {
            chooseRopeVisibleRow(slot);
        } else if (back) {
            ropeArmed_ = -1;
        }
        return true;
    }
    if (ropeCeremonySteps_ > 0) {
        // THE PLATE: no input accepted.
        return true;
    }
    if (takenHold_ > 0) {
        // THE ARREST BEAT: the officer's hand is on you and the page is not
        // up yet. Every key is swallowed but Pause (a player can always
        // quit the game), which falls through to the client as it does
        // from the page.
        return !(action == Action::Pause || key == Key::Escape);
    }
    // THE PAGE. PAUSE falls through to the client, which opens the pause
    // menu over the court (a player can always quit the game); ESC is Pause's
    // own key, so it backs the page out of the paper or an armed plea FIRST
    // and only reaches the pause menu when there is nothing to back out of.
    if (back && (courtPaperOpen_ || courtArmed_ >= 0)) {
        courtBack();
        return true;
    }
    if (action == Action::Pause || key == Key::Escape) {
        return false;
    }
    if (up) {
        moveCourtCursor(-1);
    } else if (downward) {
        moveCourtCursor(1);
    } else if (confirm) {
        chooseCourtRow();
    } else if (numbered) {
        chooseCourtVisibleRow(slot);
    } else if (zero) {
        chooseCourtVisibleRow(-1);
    }
    // EVERY OTHER KEY IS SWALLOWED: no world verb reaches the body from the
    // bench, and no page opens over the court but the pause menu.
    return true;
}

namespace {

// --- the rope -----------------------------------------------------------------
/// The dip before the plate: one page-ease, the death ceremony's own. The
/// HOLD is the sim's number (kDeathHoldSteps); there is no fade-out.
constexpr std::int32_t kRopeDipInSteps = kPageEaseSteps;

}  // namespace

void Session::armRopeCeremony() {
    // From the run's end, written by the room at the drop off hashed state:
    // the place the ward hangs a man, the ward as the killer, the reason --
    // the corpse's roster name, or THE SECOND RUNG -- and the dateline. The
    // end's grammar, distinct from the revive's on purpose (spec 5): "HANGED
    // AT ... / BY THE WARD. FOR ..." can never be confused with "PUT DOWN IN
    // ... / BY <killer>. BY <weapon>." on screen.
    const sim::RunEnd& end = tavern_->runEnd();
    ropePlateTop_ = "HANGED AT " + (end.place.empty() ? std::string(sim::kRopePlace) : end.place) + ".";
    ropePlateMid_ = "BY THE WARD. FOR " +
                    (end.reason.empty() ? std::string(sim::kRopeForSecondRung) : upperAscii(end.reason)) +
                    ".";
    ropePlateFoot_ = "THE " + ordinalWord(end.day + 1) + " DAY. " + travelClockText(end.secondOfDay) + ".";
    ropeCeremonySteps_ = kRopeDipInSteps + sim::kDeathHoldSteps;
    ropeRowsUp_ = false;
    ropeCursor_ = 0;
    ropeArmed_ = -1;
    // SILENCE. No one-shot: ThudHeavy is "every path to the floor" and this
    // is not the floor. The drop is silent by ruling.
}

namespace {

/// The two end rows, bare. No save row: no save exists, so there is nothing
/// to load and the run is genuinely over. The day a save lands, "1 - THE
/// LAST SAVE" goes first.
constexpr std::string_view kRopeRowLabels[] = {"1 - A NEW MAN", "2 - LEAVE"};

}  // namespace

std::vector<std::string> Session::ropeRows() const {
    // THE ARMED ROW NAMES THE DEVICE'S OWN CONFIRM on its tail -- the QUIT
    // row's exact shape, and the plea rows' -- so the state of the row is
    // in the row.
    std::vector<std::string> out;
    for (std::size_t i = 0; i < std::size(kRopeRowLabels); ++i) {
        std::string row(kRopeRowLabels[i]);
        if (static_cast<int>(i) == ropeArmed_) {
            row += " -- SURE? " + std::string(promptConfirmKey(promptDevice_));
        }
        out.push_back(std::move(row));
    }
    return out;
}

void Session::moveRopeCursor(int delta) {
    if (!ropeRowsUp_) {
        return;
    }
    // Moving the cursor disarms the row -- the pause card's own rule for
    // QUIT: a player who backed off the row plainly changed their mind.
    ropeArmed_ = -1;
    if (audio_ != nullptr && delta != 0) {
        audio_->playOneShot(audio::SoundId::UiTick);
    }
    const int count = static_cast<int>(ropeRows().size());
    ropeCursor_ = ((ropeCursor_ + delta) % count + count) % count;
}

void Session::chooseRopeVisibleRow(int slot) {
    if (!ropeRowsUp_ || slot < 0 || slot >= static_cast<int>(ropeRows().size())) {
        return;
    }
    // A digit on a row other than the armed one disarms it -- the pause
    // menu's own "picking a different row calls it off".
    if (ropeArmed_ >= 0 && ropeArmed_ != slot) {
        ropeArmed_ = -1;
    }
    ropeCursor_ = slot;
    chooseRopeRow();
}

void Session::chooseRopeRow() {
    if (!ropeRowsUp_ || runEnded_) {
        return;
    }
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::UiConfirm);
    }
    // ARMED ON THE FIRST PRESS, CONFIRMED ON THE SECOND -- the QUIT pattern
    // on the two rows that end a run, so a leaned-on ENTER after the plate's
    // hold cannot start a new man or leave the game. The armed row keeps its
    // tail once taken: the run is over and the row that ended it stays lit.
    if (ropeArmed_ != ropeCursor_) {
        ropeArmed_ = ropeCursor_;
        return;
    }
    runEnded_ = true;
    if (ropeCursor_ == 0) {
        // A NEW MAN: a fresh run through the boot path. main.cpp's loop
        // reads this beside quitRequested and goes back to the creation
        // window; there is no save to erase.
        runEndReason_ = RunEndChoice::NewMan;
    } else {
        // LEAVE: the shipped quit, the one exit that has always existed.
        runEndReason_ = RunEndChoice::Leave;
        quitRequested_ = true;
    }
}

void Session::composeRopeCeremony(Framebuffer& target) const {
    if (ropeCeremonySteps_ <= 0 && !ropeRowsUp_) {
        return;
    }
    constexpr std::int32_t kTotal = kRopeDipInSteps + sim::kDeathHoldSteps;
    // THE VEIL: ramps up over the dip and then HOLDS. There is no fade-out
    // phase; the frame under it is never seen again.
    float dip = 1.0F;
    if (ropeCeremonySteps_ > sim::kDeathHoldSteps) {
        const std::int32_t elapsed = kTotal - ropeCeremonySteps_;  // 0 .. kRopeDipInSteps
        dip = static_cast<float>(elapsed) / static_cast<float>(kRopeDipInSteps);
    }
    dip = dip < 0.0F ? 0.0F : (dip > 1.0F ? 1.0F : dip);
    target.fillRect(0, 0, target.width(), target.height(), Rgb{0.0F, 0.0F, 0.0F}, dip);
    // THE PLATE, centred, in the epitaph's own bone register: the place, the
    // ward and the reason; the dateline dim under them, drawRouteCard's foot
    // idiom. The black field IS its backing.
    const int scale = hudScale(target.height());
    const int glyphH = 6 * scale;
    const int lineStep = glyphH + 2 * scale;
    const int cx = target.width() / 2;
    const int topY = target.height() / 2 - 2 * lineStep;
    const Rgb bone{0.86F, 0.82F, 0.72F};
    const Rgb dim{0.56F, 0.53F, 0.46F};
    const auto centre = [&](const std::string& line, int y, const Rgb& ink) {
        if (line.empty()) {
            return;
        }
        const int w = textWidth(line, scale);
        drawText(target, cx - w / 2, y, line, ink, dip, scale);
    };
    centre(ropePlateTop_, topY, bone);
    centre(ropePlateMid_, topY + lineStep, bone);
    centre(ropePlateFoot_, topY + 2 * lineStep, dim);
    if (!ropeRowsUp_) {
        return;
    }
    // THE PENDING-DECISION ZONE RISES UNDER IT once the hold has run:
    // option-list idiom, inverted-fill selection, never an arrow. The rows
    // are drawn in the page grid's own cells so the fill is the same shape
    // the pause card's cursor wears.
    const PanelMetric metric = panelMetric(target.width(), target.height());
    const std::vector<std::string> rows = ropeRows();
    // THE ZONE HOLDS THE ARMED WIDTH WHETHER OR NOT A ROW IS ARMED, so
    // arming one moves no row: the tail every row could grow is measured
    // for all of them.
    const std::string tail = " -- SURE? " + std::string(promptConfirmKey(promptDevice_));
    int widest = 0;
    for (const std::string_view row : kRopeRowLabels) {
        widest = std::max(widest, static_cast<int>(row.size() + tail.size()));
    }
    for (const std::string& row : rows) {
        widest = std::max(widest, static_cast<int>(row.size()));
    }
    const int rowsY = topY + 4 * lineStep;
    const PanelRect zone{cx - metric.widthOf(widest + 2) / 2, rowsY, metric.widthOf(widest + 2),
                         metric.heightOf(static_cast<int>(rows.size()))};
    const PanelInk& ink = panelInk();
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const int r = static_cast<int>(i);
        if (r == ropeCursor_) {
            drawInvertedFill(target, zone, metric, 0, r, widest + 2, ink.accent, 1.0F);
            drawCellTextKnockout(target, zone, metric, 1, r, rows[i], ink.knockout, 1.0F);
        } else {
            drawCellText(target, zone, metric, 1, r, rows[i], ink.prose, 1.0F);
        }
    }
}

void Session::composeTakenPlate(Framebuffer& target) const {
    if (!takenPlateUp()) {
        return;
    }
    // THE CUT: an instant black, the one line on it in the rope plate's own
    // register (bone, centred), at full alpha for the whole hold -- the
    // "TAKEN TO THE MISSION. 23:00." the spec promised, on the frame for as
    // long as it takes to read, and nothing under it (no page yet, no HUD
    // to hide it). The page opens over it when the hold runs out.
    target.fillRect(0, 0, target.width(), target.height(), Rgb{0.0F, 0.0F, 0.0F}, 1.0F);
    const int scale = hudScale(target.height());
    const Rgb bone{0.86F, 0.82F, 0.72F};
    const int w = textWidth(takenPlate_, scale);
    const int glyphH = 6 * scale;
    drawText(target, target.width() / 2 - w / 2, target.height() / 2 - glyphH / 2, takenPlate_,
             bone, 1.0F, scale);
}

}  // namespace granadad::render
