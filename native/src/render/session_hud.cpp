// Session HUD: the *Line readouts, the panel animation and drawFrame.
#include "session_internal.hpp"

// THE AUDIO WIRING PASS, as in session.cpp: this file speaks to the audio engine in ids only; see
// setAudio() in session.hpp and the determinism note in audio_engine.hpp.
#include "granadad/audio/audio_engine.hpp"

namespace granadad::render {

std::string Session::roomLine() const {
    // HARDENING PASS. Pulled out of drawFrame()'s own inline block so
    // syncPanelAnim() can call the identical logic instead of a second copy
    // -- see that method's own header. Bottom-right, and only when there is
    // a room to describe.
    if (!tavern_->playerInside()) {
        return {};
    }
    std::ostringstream line;
    line << "THE GULL  " << tavern_->presentCount() << " IN  ";
    if (!tavern_->isOpen()) {
        line << "SHUT";
    } else if (tavern_->noise() >= 60) {
        line << "LOUD";
    } else if (tavern_->noise() >= 25) {
        line << "BUSY";
    } else {
        line << "QUIET";
    }
    return line.str();
}

std::string Session::stealthLine() const {
    const sim::Notice worst = tavern_->worstNotice();
    const std::int32_t light = tavern_->lightOnPlayer();
    const std::int32_t noise = tavern_->playerNoise();
    std::string line = worst.seen ? "SEEN" : "HIDDEN";
    if (tavern_->stance() == sim::Stance::Crouched) {
        line += " CROUCH";
    }
    // The two numbers a player can actually do something about, in the words
    // they would use. Deliberately short: this is an EDGE line, not a sheet.
    line += light >= 50 ? "  LIT " : "  DARK ";
    line += std::to_string(light);
    if (noise >= sim::kNoiseRunning) {
        line += "  LOUD";
    } else if (noise > 0) {
        line += "  HEARD";
    } else {
        line += "  QUIET";
    }
    return line;
}

std::string Session::lockLine() const {
    const sim::Lockpicking& wire = tavern_->picking();
    if (!wire.open()) {
        return {};
    }
    // THE WHOLE MINIGAME, IN ONE ROW OF 4x6 GLYPHS. A pin that has dropped is
    // a star and one still up is a dash; the depth track is nine dots with the
    // pick standing on one of them. Nothing here needs a panel, and a panel is
    // what the Java build's first-person view died of.
    std::string line = "LOCK  PINS ";
    for (std::int32_t i = 0; i < wire.lock().pins; ++i) {
        line += i < wire.pinsSet() ? '*' : '-';
    }
    line += "  DEPTH ";
    for (std::int32_t d = 0; d < sim::kPinDepths; ++d) {
        // '+' and not '#': the 4x6 font in hud.cpp has fifty-two glyphs and a
        // hash is not one of them, so the pick's own position drew as a hole in
        // the track. Caught by looking at the frame, which is the point of
        // looking at the frame.
        line += d == wire.depth() ? '+' : '.';
    }
    line += "  STRAIN " + std::to_string(wire.strain()) + "/" +
            std::to_string(wire.strainLimit());
    line += "  PICKS " + std::to_string(tavern_->picks());
    return line;
}

std::string Session::placeLabel() const {
    const std::int32_t x = body_->tileX();
    const std::int32_t y = body_->tileY();
    const std::int32_t band = body_->band();
    for (std::size_t i = 0; i < sim::docks::kPlaceCount; ++i) {
        const sim::docks::Place& place = sim::docks::kPlaces[i];
        if (band == place.band && x >= place.x0 && x <= place.x1 && y >= place.y0 &&
            y <= place.y1) {
            return place.name;
        }
    }
    // No street name, because there is no street name to give. Saying which
    // band you are on is the whole of what the z-level actually knows.
    if (band == sim::docks::kBandQuayside) {
        return "THE DOCKS - QUAYSIDE";
    }
    if (band == sim::docks::kBandMidSlope) {
        return "THE DOCKS - MID SLOPE";
    }
    if (band == sim::docks::kBandUpper) {
        return "THE DOCKS - UPPER";
    }
    if (band < sim::docks::kBandQuayside) {
        return "UNDER THE PIERS";
    }
    return "THE DOCKS";
}

namespace {

/// Clipped to something the bottom-left corner can hold without walking across
/// the frame. The HUD hugs its edge; a quest tracker that runs to the middle of
/// the screen is the exact failure the Java build shipped.
[[nodiscard]] std::string clip(std::string text, std::size_t columns) {
    if (text.size() > columns) {
        text.resize(columns);
    }
    return text;
}

}  // namespace

std::string Session::guildLine() const {
    // NOTHING WHILE THE WIRE IS IN. The lock row is drawn at
    // height - margin - 31*scale; this one is drawn at 24*scale above the
    // health bar, which IS height - margin - 31*scale. The same pixel row.
    // docs/frames/s10-08-the-lock.png caught the two interleaved, reading "THE
    // 1OCKUNPINS -TENADEPTH ....+....". It is the same class of defect the S7
    // review found with the alert over the topic grid and it gets the same fix:
    // a MODE owns its row. The wire is a mode, and while it is in, which ladder
    // you are highest on is not what the player is reading.
    //
    // It is suppressed HERE and not at the draw call so a case can assert it --
    // a suppression that lives inside drawFrame is only checkable in pixels.
    if (picking()) {
        return {};
    }
    const sim::DialogueDirector& talk = tavern_->dialogue();
    const std::int32_t top = talk.standings().highestRankedFaction();
    if (top < 0) {
        return {};
    }
    const sim::Faction* faction = talk.factions().at(top);
    const std::string name = faction == nullptr ? std::string("GUILD") : faction->displayName;
    return clip(upperAscii(name) + " - " + upperAscii(talk.standings().rankTitle(top)), 34);
}

std::string Session::caseLine() const {
    // COURIER CASE. The orientation row follows the live errand -- the same
    // one rule every case surface keeps (sheetCaseLive), so the corner names
    // the Gull while the sheet is the work and the Bloodletter before and
    // after.
    const sim::CasebookRaws& raws = activeCaseRaws();
    const sim::Casebook& book = activeCasebook();
    if (!book.active() || !raws.loaded()) {
        return {};
    }
    const std::vector<std::int32_t> heard = book.known();
    std::string line = "CASE " + std::to_string(book.readCount()) + "/" +
                       std::to_string(heard.size());
    // WHERE TO GO NEXT, ON THE SAME ROW. This is the orientation line: a player
    // who put the game down for a week and came back to a district of 692
    // people gets one line telling them where they were walking. Until this
    // sprint the corner of a new game was empty, which is exactly the "dropped
    // into a systems demo with no orientation" the demo brief names.
    // THE PULL PACK: the lead the ribbon follows, when it is in this book --
    // the player's pick, or the authored next lead standing in for one -- so
    // the corner and the ribbon never name two different doors.
    const PullTarget followed = pullTarget();
    const std::int32_t lead = followed.set && followed.book == frontedBook() ? followed.lead
                                                                             : book.nextOpen();
    if (lead >= 0 && static_cast<std::size_t>(lead) < raws.leads().size()) {
        line += " > " + raws.leads()[static_cast<std::size_t>(lead)].place;
    } else if (book.closed()) {
        line += " > " + std::string(raws.close());
    } else {
        const std::string_view mood = raws.dreadLabel(book.dread());
        line += "  ";
        line.append(mood);
    }
    return clip(std::move(line), 40);
}

std::string Session::legendLine() const {
    const sim::Legend book = legend();
    if (book.totalRungs() <= 0) {
        return {};
    }
    return clip(std::string(book.title()) + "  " + std::to_string(book.totalRungs()) +
                    (book.totalRungs() == 1 ? " RUNG" : " RUNGS"),
                34);
}

std::string Session::heatLine() const {
    const sim::CrimeLedger& crimes = tavern_->dialogue().crimes();
    const sim::Stash& sack = crimes.stash();
    if (crimes.heat() <= 0 && crimes.loot() <= 0 && !crimes.carryingBale() && sack.empty() &&
        !crimes.maimed() && !crimes.condemned() && !sheetCarry_) {
        return {};
    }
    // THE ROW IS THIRTY-FOUR COLUMNS AND IT FITS THEM BY CONSTRUCTION: the
    // tag and the number are never clipped, and a rider (the loot, the
    // bale, the man on your shoulder) is appended only while the row still
    // holds it whole -- a rider that does not fit is dropped, never cut in
    // half, and the number is never lost to it.
    constexpr std::size_t kHeatColumns = 34;
    std::string line;
    // THE CRIMINAL TAG (JUSTICE BUILD, section 1.2). Three states the ledger
    // keeps, one row. The two the ward has done TO you outrank everything
    // else on the line: a condemned man wants to know he is one before he
    // wants his heat. CONDEMNED is the shipped word with the court's meaning
    // -- the rope passed and commuted, for the rest of the run. WANTED is
    // paper; WANTED FOR BLOOD is paper with a corpse behind it, which only
    // the bench clears. A murderer whose heat has cooled is not WANTED on
    // this row -- the paper lapsed -- but the next arrest on any paper is a
    // rope hearing, and the row does not pretend otherwise by inventing a
    // word for it: standing stays a phrase, heat stays a number. A commuted
    // man with fresh blood on him is WANTED FOR BLOOD and nothing else: the
    // two tags together do not fit the row, and of the two it is the blood
    // that decides what the next arrest is (mercy is given once).
    const bool blood = crimes.warrant() && crimes.murderer();
    if (crimes.condemned() && !blood) {
        line = "CONDEMNED  ";
    } else if (crimes.maimed()) {
        // "MAIMED  WANTED FOR BLOOD  HEAT 100" is the row's full width.
        line = "MAIMED  ";
    }
    if (crimes.warrant()) {
        line += blood ? "WANTED FOR BLOOD  " : "WANTED  ";
    }
    line += "HEAT " + std::to_string(crimes.heat());
    const auto rider = [&line, kHeatColumns](const std::string& tail) {
        if (line.size() + tail.size() <= kHeatColumns) {
            line += tail;
        }
    };
    if (crimes.loot() > 0) {
        rider("  LOOT " + std::to_string(crimes.loot()));
    }
    if (crimes.carryingBale()) {
        rider("  BALE");
    }
    // COURIER CASE. The man in hand rides the same row the bale does -- a
    // carried thing the ward would mind, worn on the HUD until the back room
    // takes him. The rendered over-the-shoulder body is flagged follow-up
    // work; this line is the honest interim. TRAVEL LANE RENAME, flagged for
    // the owner: he read "FINCH IN HAND" as unclear, so the row now says what
    // the body is doing rather than naming an idiom.
    if (sheetCarry_) {
        rider("  CARRYING FINCH");
    }
    return clip(std::move(line), kHeatColumns);
}

void Session::settleOverlaysForInspection() noexcept {
    syncPanelAnim();
    panelAnim_.snapTo(panelAnim_.target());
    alertAnim_.snapTo(alertAnim_.target());
    if (tavern_->dialogue().isOpen()) {
        lastSpokenLine_ = tavern_->dialogue().lastLine();
        speechAgeSteps_ = kSpeechRevealSteps;
    }
}

void Session::syncPanelAnim() noexcept {
    // AUDIO WIRING: PANEL OPEN AND CLOSE, on the exact edge the plan names --
    // "wherever an EasedToggle target flips is exactly where the matching
    // one-shot belongs". panelAnim_ is the ONE toggle all eight pages share,
    // so this is one hook for every open and close in the game: the tiled
    // Menu speaks as a book (its tiles are the casebook, the letters, the
    // map), everything else in the quiet Ui pair. The flip fires once per
    // actual change however many times this function is re-run per step,
    // because the target itself only changes once.
    const bool panelWanted = conversingNow();
    if (audio_ != nullptr && panelWanted != panelAnim_.target()) {
        if (panelWanted) {
            // SPELLS BUILD: the Grimoire page is paper too -- the same
            // BookOpen/BookClose pair the tiled Menu already speaks, no new
            // sound design.
            // The ward map is paper too -- a chart unrolled reads as a book
            // opening, not as a UI blip.
            audioPanelWasMenu_ = casebookOpen_ || grimoireOpen_ || districtMapOpen_;
            audio_->playOneShot(audioPanelWasMenu_ ? audio::SoundId::BookOpen
                                                   : audio::SoundId::UiOpen);
        } else {
            audio_->playOneShot(audioPanelWasMenu_ ? audio::SoundId::BookClose
                                                   : audio::SoundId::UiClose);
        }
    }
    panelAnim_.setTarget(panelWanted);
    // The same test drawFrame() makes for what the HUD's own alert row is
    // about to show -- bouncerWarned(), the one copy (it used to be written
    // out here and there both, because this path mutates alertAnim_ and
    // that one is const; the accessor is const and both call it).
    const bool warned = bouncerWarned();
    // messageSteps_, NOT message_.empty(). The string is deliberately kept
    // alive a few steps past the countdown reaching zero so the fade-out has
    // something to fade -- see step()'s own note -- so the STRING being
    // non-empty cannot be what decides whether the alert is still wanted, or
    // the two would deadlock each other.
    alertAnim_.setTarget(warned || messageSteps_ > 0);
    // INNOVATION SPRINT ITEM #3. THE RISING EDGE ONLY -- a fresh warning
    // pulses the alert plate's own weight; the SAME warning re-read on every
    // later step (`warned` staying true) must not retrigger it, or the pulse
    // would sit at full strength for as long as the bouncer keeps talking
    // instead of reading as one beat. See alertPulse_'s own header.
    if (warned && !lastWarned_) {
        alertPulse_.trigger();
    }
    lastWarned_ = warned;

    // INNOVATION SPRINT ITEM #2. THE TILED MENU'S OWN FOUR BORDERS. Re-read
    // every call, exactly like every row below -- see this function's own
    // header on why that catches a focus change made through menuPageNext()/
    // menuPagePrev() (neither of which calls this directly) the moment
    // step() next runs it, not only one made through toggleMenuFocused().
    // Closed (casebookOpen_ false) targets every tile unfocused rather than
    // leaving whichever one last had it visibly "focused" underneath a
    // panel that is not there to show it.
    const bool menuUp = casebookOpen_;
    characterFocusAnim_.setTarget(menuUp && menuFocus_ == kMenuFocusCharacter);
    mapFocusAnim_.setTarget(menuUp && menuFocus_ == kMenuFocusMap);
    lettersFocusAnim_.setTarget(menuUp && menuFocus_ == kMenuFocusLetters);
    journalFocusAnim_.setTarget(menuUp && menuFocus_ == kMenuFocusJournal);

    // HARDENING PASS. EVERY OTHER ROW hud.hpp:178-187 NAMED AS STILL
    // SNAPPING, on the identical two-part shape the alert just used above:
    // setTarget() decides whether the row is WANTED, and the cached string
    // is what stays behind to fade once it is not -- see interactCache_'s
    // own header. `conversing` matches exactly what drawFrame() already
    // blanks every one of these rows for while a panel is open (the topic
    // list, the casebook, the keys page, options or pause own the bottom
    // band then), and roomLine()/stealthLine() gate themselves on
    // tavern_->playerInside() the same way drawFrame() used to inline.
    const bool conversing = conversingNow();
    const auto sync = [conversing](EasedToggle& anim, std::string& cache, std::string&& text) {
        const bool visible = !conversing && !text.empty();
        if (visible) {
            cache = std::move(text);
        }
        anim.setTarget(visible);
    };
    // THE CROSSHAIR PASS. THE AIM PROMPT'S FOUR PARTS, ON THE ONE TOGGLE.
    //
    // #85 composed a single "<key>  <label>" row here. The renderer now
    // colours the verb, the subject and the note by three different roles, so
    // Session hands over three strings and a kind and joins nothing -- a
    // pre-joined row would have to be taken apart again in hud.cpp, which is
    // the second-description-of-a-layout defect the panel pass banned.
    //
    // ONE EasedToggle STILL, and interactAnim_ is still it: the four parts
    // appear and disappear together because they are one element. The subject
    // and the note ride interactCache_'s own visibility so a name cannot
    // outlive the verb it belongs to mid-fade.
    const InteractTarget aim = interactTarget();
    const bool aimVisible = !conversing && !aim.verb.empty();
    if (aimVisible) {
        interactCache_ = aim.verb;
        interactSubjectCache_ = aim.subject;
        interactNoteCache_ = aim.note;
        interactKindCache_ = aim.kind;
    }
    interactAnim_.setTarget(aimVisible);
    sync(lockAnim_, lockCache_, lockLine());
    sync(rivalAnim_, rivalCache_, rivalLine());
    sync(stealthAnim_, stealthCache_, tavern_->playerInside() ? stealthLine() : std::string());
    // VOICE LANE. The voice row's own toggle, re-read like every row here.
    syncVoiceAnim();

    // UI-EA (LANE HUD): THE LAW OF EARNED TEXT. Text prints when it CHANGES,
    // when it is aimed at, or when the player hesitates -- never merely
    // because it is true. The four reference rows below (case, room, guild,
    // objective) and the clock/purse pair used to be furniture; each is an
    // EVENT now: an edge detected here arms a ~2.5s countdown
    // (kHudWakeSteps), step() spends it, and the row's own EasedToggle eases
    // it down. THE EDGES ARE DETECTED AGAINST LAST-SEEN VALUES and this
    // function runs several times a step, so every compare below is
    // idempotent by construction -- lastPlaceName_'s own pattern, whole
    // family. The first call ever (the constructor's) SEEDS and arms
    // nothing: a session does not boot with its corner announcing itself.
    const std::string caseNow = caseLine();
    const std::string roomNow = roomLine();
    const std::string guildNow = guildLine();
    const std::string objectiveNow = objectiveLine();
    const bool bookUp = casebookOpen_ || casebookPageOpen();
    const int hourNow = timeOfDay_ / 3600;
    const std::int32_t coinNow = tavern_->playerCoin();
    if (!hudEdgesSeeded_) {
        hudEdgesSeeded_ = true;
        lastClockHour_ = hourNow;
        lastClockTod_ = timeOfDay_;
        lastCoinSeen_ = coinNow;
        lastCaseSeen_ = caseNow;
        lastRoomSeen_ = roomNow;
        lastGuildSeen_ = guildNow;
        lastObjectiveSeen_ = objectiveNow;
        lastBookOpen_ = bookUp;
    }
    // THE CLOCK: an hour turning over is the ward's own bell, and a time
    // CHARGE (travel's restated minute, a wait pick, a sleep) is the one
    // moment a player is owed the hour they just spent. A charge is a jump
    // of a minute or more between two syncs -- the ordinary tick advances by
    // single seconds and never trips it.
    if (hourNow != lastClockHour_) {
        lastClockHour_ = hourNow;
        clockShowSteps_ = kHudWakeSteps;
    }
    {
        int jump = timeOfDay_ - lastClockTod_;
        if (jump < 0) {
            jump += sim::kSecondsPerDay;
        }
        if (jump >= 60) {
            clockShowSteps_ = kHudWakeSteps;
        }
        lastClockTod_ = timeOfDay_;
    }
    // THE PURSE: money is on screen when it moves.
    if (coinNow != lastCoinSeen_) {
        lastCoinSeen_ = coinNow;
        purseShowSteps_ = kHudWakeSteps;
    }
    // THE CASE ROW: a beat or lead moving is news; the book CLOSING is the
    // recap a player putting it down actually wants. And while the
    // lead-opened plate is up the row stays down -- the notice IS the case
    // news (UI-EA-SPEC #16), and one piece of news does not print twice.
    if (caseNow != lastCaseSeen_) {
        if (!caseNow.empty()) {
            caseShowSteps_ = kHudWakeSteps;
        }
        lastCaseSeen_ = caseNow;
    }
    if (lastBookOpen_ && !bookUp) {
        caseShowSteps_ = kHudWakeSteps;
    }
    lastBookOpen_ = bookUp;
    if (casePlateShowSteps_ > 0) {
        caseShowSteps_ = 0;
    }
    // THE ROOM ROW: entry and loudness are events; a head-count drifting by
    // one body is not, so the compare is made with the digits struck out --
    // "THE GULL 14 IN LOUD" and "THE GULL 15 IN LOUD" are one state.
    const auto strippedOfDigits = [](const std::string& text) {
        std::string out;
        out.reserve(text.size());
        for (const char c : text) {
            if (c < '0' || c > '9') {
                out.push_back(c);
            }
        }
        return out;
    };
    if (strippedOfDigits(roomNow) != strippedOfDigits(lastRoomSeen_)) {
        if (!roomNow.empty()) {
            roomShowSteps_ = kHudWakeSteps;
        }
    }
    lastRoomSeen_ = roomNow;
    // THE RUNG AND THE ERRAND: they wake when they change. A title held for
    // a week is reference material, and reference material lives on a page.
    if (guildNow != lastGuildSeen_) {
        if (!guildNow.empty()) {
            guildShowSteps_ = kHudWakeSteps;
        }
        lastGuildSeen_ = guildNow;
    }
    if (objectiveNow != lastObjectiveSeen_) {
        if (!objectiveNow.empty()) {
            objectiveShowSteps_ = kHudWakeSteps;
        }
        lastObjectiveSeen_ = objectiveNow;
    }
    // The four rows ride the sync() shape with one more condition: WANTED
    // means "recently woken", not "true". The caches still hold the last
    // shown words through the fade, exactly as every row above.
    const auto syncWake = [conversing](EasedToggle& anim, std::string& cache,
                                       const std::string& text, int wakeSteps) {
        const bool visible = !conversing && !text.empty() && wakeSteps > 0;
        if (visible) {
            cache = text;
        }
        anim.setTarget(visible);
    };
    syncWake(caseAnim_, caseCache_, caseNow, caseShowSteps_);
    syncWake(roomAnim_, roomCache_, roomNow, roomShowSteps_);
    syncWake(guildAnim_, guildCache_, guildNow, guildShowSteps_);
    syncWake(objectiveAnim_, objectiveCache_, objectiveNow, objectiveShowSteps_);
    // The clock and the purse draw live numbers (no cache -- fatigueAnim_'s
    // reasoning). The clock is additionally up for the life of the wait
    // page (its rows price the very hours it shows) and the pause stack --
    // the spec's own #34 keeps "rows, clock, title" on the pause, and the
    // hour before quitting is exactly a fact a player came to check. Neither
    // is gated on `conversing` -- a full page zeroes the fields at assembly,
    // and an hour striking or a price being paid mid-conversation is still
    // an event.
    clockAnim_.setTarget(waitOpen_ || pauseOpen_ || clockShowSteps_ > 0);
    purseAnim_.setTarget(purseShowSteps_ > 0);
    // PLANNING SPRINT (item #2, the sweep). THE SAME sync() SHAPE, FOR THE
    // TOP-RIGHT STACK'S THREE REMAINING ROWS -- see standingAnim_'s own
    // header on why these three, specifically, were still snapping.
    sync(standingAnim_, standingCache_, standingLine());
    sync(heatAnim_, heatCache_, heatLine());
    sync(stashAnim_, stashCache_, stashLine());
    // FIRST-PERSON COMBAT (S13). THE SAME sync() SHAPE, for the two rows the
    // Cast/Block task added -- each on its own EasedToggle per the pinned
    // convention, because a readied crafting appearing has nothing to do with
    // a guard going up.
    sync(spellAnim_, spellCache_, spellLine());
    sync(blockAnim_, blockCache_, blockLine());
    // ACTION-COMBAT BUILD. THE SAME sync() SHAPE for the HELD HARD charge row,
    // on its own toggle: a swing charging hard has nothing to do with a guard
    // going up, and the two are mutually exclusive anyway (the guard drops the
    // instant the hand leaves Idle).
    sync(chargeAnim_, chargeCache_, chargeLine());
    // STANCE & ROOM BUILD. THE SAME sync() SHAPE for the FISTS UP row, on its
    // own toggle: fighting mode outlives any one guard or swing.
    sync(handsAnim_, handsCache_, handsLine());
    // KIT BUILD. THE SAME sync() SHAPE for the turn row, on its own toggle.
    sync(turnAnim_, turnCache_, turnLine());
    // HELD-EFFECTS BUILD. THE SAME sync() SHAPE, one per active-effect slot
    // -- each on its own EasedToggle per the pinned convention, because a
    // warmth lapsing in slot 0 has nothing to do with a tuning arriving in
    // slot 1.
    for (std::size_t slot = 0; slot < kEffectRows; ++slot) {
        sync(effectAnims_[slot], effectCaches_[slot], effectLine(slot));
    }
    // FATIGUE BUILD. The bar mirrors the health bar's one visibility rule --
    // down for the length of a conversation, up otherwise -- on its OWN
    // toggle per the pinned convention. No cache: the bar draws live numbers,
    // which do not go away while it fades.
    fatigueAnim_.setTarget(!conversing);
    // THE WARD MAP (core action #13). Its own toggle, its own target -- the
    // page's openAmount, per the settled convention (rule 1). No cache: the
    // page draws live off TileQuery, which does not go away when it closes.
    districtMapAnim_.setTarget(districtMapOpen_);
    // SPELLS BUILD. The quick bar strip: wanted while the wheel or the number
    // row was touched inside the last couple of seconds (showQuickBar()'s
    // countdown, run down in step()) and no panel owns the bottom band. The
    // names are cached on the same visible-edge rule every sync() above uses,
    // so the strip finishes its fade with its labels still on it.
    const bool barWanted = !conversing && quickBarShowSteps_ > 0;
    if (barWanted) {
        for (std::int32_t slot = 0; slot < sim::Tavern::kQuickSlotCount; ++slot) {
            const sim::Spell* bound = tavern_->slotSpell(slot);
            // KIT BUILD: a slot is a spell OR an item; the strip prints
            // whichever it holds by name, the same cell either way.
            const sim::ItemDef* thing = tavern_->slotItem(slot);
            quickBarNames_[static_cast<std::size_t>(slot)] =
                bound != nullptr ? upperAscii(bound->displayName)
                : thing != nullptr ? thing->name
                                   : std::string();
        }
    }
    quickBarAnim_.setTarget(barWanted);
    // UI-EA (LANE HUD): THE QUICK BAR'S TUTOR TOAST, on the strip's own
    // rising edge and only its first two ever. NINE AND THE STICKS: the
    // QuickWheel hold is cut, so what is taught is the STEP -- the wheel on
    // a keyboard, D-pad left and right on a pad (QuickPrev/QuickNext's own
    // two halves, through promptLabel, so a rebind re-words it). It rides
    // the strip's countdown so the pair rise and fall as one, and after two
    // showings it is retired for the session.
    if (barWanted && !lastQuickBarUp_ && wheelHintShows_ < kWheelHintShows) {
        ++wheelHintShows_;
        wheelHint_.raise(kQuickBarShowSteps);
        const std::string prev(promptLabel(controls_, Action::QuickPrev, promptDevice_));
        const std::string next(promptLabel(controls_, Action::QuickNext, promptDevice_));
        // The two halves of one wheel print as the one word; anything else
        // prints both keycaps.
        const bool wheel = prev == "WHEELUP" && next == "WHEELDOWN";
        wheelHintText_ = (wheel ? std::string("WHEEL") : prev + " " + next) + " - STEP";
    }
    lastQuickBarUp_ = barWanted;
    wheelHint_.sync(conversing);

    // DISTRICT PHASE D: THE THRESHOLD MOMENT.
    //
    // THE RISING EDGE ONLY, the identical shape alertPulse_ uses above and for
    // the identical reason: `here` is re-read on every call (this function runs
    // once a step AND out of every toggle, see its own header), so what fires
    // the plate has to be the CHANGE and never the state. Standing still in
    // Tarwalk must not re-arm it sixty times a second.
    //
    // NAMED PLACES ONLY. placeNameAt is empty for the two thirds of the
    // district that is compounds, yards and back lanes -- see its own note --
    // and lastPlaceName_ deliberately does not record those (see its header):
    // an unnamed tile is a gap between places, not a place, and treating it as
    // one would re-announce TARWALK every time the player stepped into an
    // alley and back out, and re-announce SALTGATE RISE at every seam between
    // the three rectangles that road is authored as.
    const std::string_view here =
        sim::docks::placeNameAt(body_->tileX(), body_->tileY(), body_->band());
    // THE STAND-DOWN, and it is the same list every other overlay stands down
    // for. A panel owns the screen: the plate does not draw over the topic
    // list, the tiled Menu, the ward map, the keys page or the pause menu, and
    // a crossing walked while one of them is up is simply not announced --
    // stood down rather than queued, because a notice that pops the instant a
    // menu closes is a notice about the menu.
    const bool suppressed = conversingNow();
    if (!here.empty() && here != lastPlaceName_) {
        lastPlaceName_.assign(here);
        if (!suppressed) {
            // THE NEWEST CROSSING WINS, say()'s own rule: a second boundary
            // crossed while the first plate is still up swaps the words and
            // re-arms the hold rather than queueing. It does NOT restart the
            // rise -- the toggle is already open and setTarget is idempotent --
            // so the plate reads as one notice being corrected, not as two
            // notices fighting.
            placePlateName_.assign(here);
            placePlateShowSteps_ = kPlacePlateShowSteps;
        }
    }
    if (suppressed) {
        placePlateShowSteps_ = 0;
    }
    // THE CASEBOOK PASS. THE SAME STAND-DOWN, AND IT IS NOT A COURTESY.
    // examine() is the one thing that arms this notice and examine() refuses
    // while a page or a conversation owns the keyboard, so in practice the
    // plate is armed with the world on screen -- but the rule is stated here
    // anyway, in the one place every other overlay's stand-down is stated, so
    // a later caller that arms it from somewhere else cannot quietly put a
    // notice over a topic list. STOOD DOWN, never queued: a notice that pops
    // the instant a menu closes is a notice about the menu.
    if (suppressed) {
        casePlateShowSteps_ = 0;
    }
    // AND THE TWO NOTICES DO NOT SHARE A FRAME. There is one announcement slot
    // under the ribbon (hud.cpp's drawAnnouncePlate) and the case outranks the
    // crossing, so a lead opened on the step the body walked into a named place
    // takes the band and the crossing is dropped rather than drawn under it.
    if (casePlateShowSteps_ > 0) {
        placePlateShowSteps_ = 0;
    }
    // AND NEITHER SHOWS UNDER THE RUNG PLATE: the slot waits behind it.
    placePlateAnim_.setTarget(placePlateShowSteps_ > 0 && !rungPlateOnScreen());
    casePlateAnim_.setTarget(casePlateShowSteps_ > 0 && !rungPlateOnScreen());
}

std::string Session::rivalLine() const {
    // BOTTOM-LEFT, ON THE EDGE, ONE LINE. The HUD rule is not a preference
    // (COMBAT-FEEL-REFERENCE section 3): the centre stays empty and an
    // inventory, a guild or a rivalry is one row in a corner until it has
    // earned more. This is the only thing in the game that says the man across
    // the room is the man who put you here.
    //
    // UI-EA (LANE HUD): DIETED TO NAME AND COUNT -- the spec's "rank 6 -> 3".
    // "RIVAL " and the title were reference words (his corner and his sheet
    // already say what he is), and HUNTING leaves the label for the row's red
    // ink (HudState::rivalHunts) -- the glance was always the point of that
    // word, and the colour IS the glance. The count is a value; values never
    // get vaguer.
    const sim::Nemesis* worst = tavern_->nemesis().worst();
    if (worst == nullptr) {
        return {};
    }
    std::string line = upperAscii(worst->who) + " x" + std::to_string(worst->wins);
    // 44 columns: 220 pixels at scale 1 against a 320-wide frame with a
    // six-pixel margin, so the longest line this can produce still fits its
    // edge. A HUD line that runs off the frame is the S6 defect, and it is not
    // being reintroduced from a different corner.
    return clip(std::move(line), 44);
}

std::string Session::stashLine() const {
    // WHAT IS ON YOU, AND WHAT IT WEIGHS. The weight is the number that matters
    // -- it is what a watchman's eye is on -- so it is on the line beside the
    // count rather than buried in a sheet.
    const sim::Stash& sack = tavern_->dialogue().crimes().stash();
    if (sack.empty()) {
        return {};
    }
    std::string line;
    for (std::size_t i = 0; i < sim::kContrabandCount; ++i) {
        const sim::Contraband good = static_cast<sim::Contraband>(i);
        const std::int32_t held = sack.count(good);
        if (held <= 0) {
            continue;
        }
        if (!line.empty()) {
            line += "  ";
        }
        line += std::to_string(held) + " " + std::string(sim::contrabandLabelFor(good, held));
    }
    if (sack.illicitWeight() > 0) {
        line += "  " + std::to_string(sack.illicitWeight()) + "DR";
    }
    return clip(std::move(line), 34);
}

std::string Session::spellLine() const {
    // WHAT THE HAND IS HOLDING, one line on the top-right edge -- the same
    // deal the sack got, and for the same reason: a readied crafting is
    // exactly the kind of element that creeps into being a hotbar panel.
    // Empty with an empty grimoire (absence costs nothing), and the cooling
    // clock rides the same line rather than earning a meter.
    const sim::Spell* spell = tavern_->equippedSpell();
    if (spell == nullptr) {
        return {};
    }
    std::string line = "CAST  " + upperAscii(spell->displayName);
    const std::int64_t cooling = tavern_->castCooldownLeft();
    if (cooling > 0) {
        line += " (" + std::to_string(cooling) + "S)";
    }
    return clip(std::move(line), 34);
}

std::string Session::blockLine() const {
    // THE ROOM'S OWN FACT, not the keypress: playerBlocking() is what
    // tickBrawl actually reads, so this row can never say GUARD UP while a
    // menu has quietly lowered it -- see step()'s own derivation.
    return tavern_->playerBlocking() ? "GUARD UP" : std::string();
}

std::string Session::turnLine() const {
    return turnSteps_ > 0 ? turnLine_ : std::string();
}

std::string Session::chargeLine() const {
    // ACTION-COMBAT BUILD (section 5, channel 2). "HELD HARD -- CUDGEL 14-18"
    // while the swing is charged past the hard threshold, and empty otherwise
    // -- the hard tier made visible in the sheet's own NAME low-high grammar.
    // The reticle carries the light charge; this row is only the committed hard
    // one, so it never competes with the guard (the guard holds only in Idle).
    if (!tavern_->playerChargeHard()) {
        return {};
    }
    const sim::Weapon weapon = tavern_->playerHeldWeapon();
    // THE HARD SPAN, which is the swing span through the hard tier: the same
    // ((base + variance) * chargeQ8) >> 8 strike() applies, at kHardSwingChargeQ8
    // -- so the numbers on the row are exactly the numbers a release will throw.
    const std::int32_t base = sim::baseDamage(weapon);
    const std::int32_t lo = (base * sim::kHardSwingChargeQ8) >> 8;
    const std::int32_t hi =
        ((base + sim::kStrikeVarianceMax) * sim::kHardSwingChargeQ8) >> 8;
    // THE WEAPON NAME is weaponSheetLine's own casing (which is why this reads
    // through it rather than keeping a second copy of the name table): take
    // everything up to the sheet's own span, so "THE EVICTOR 7-9 IMPACT" yields
    // "THE EVICTOR" and the numbers below are the doubled hard span, not its.
    std::string sheet = sim::weaponSheetLine(weapon);
    std::size_t span = 0;
    while (span < sheet.size() && (sheet[span] < '0' || sheet[span] > '9')) {
        ++span;
    }
    std::string name = sheet.substr(0, span);
    while (!name.empty() && name.back() == ' ') {
        name.pop_back();
    }
    return "HELD HARD -- " + name + " " + std::to_string(lo) + "-" + std::to_string(hi);
}

std::string Session::handsLine() const {
    // STANCE & ROOM BUILD. THE ROOM'S OWN FACT, the blockLine shape:
    // playerHandsUp() is the hashed bit the room, the street and the Watch
    // react to, so this row can never say FISTS UP while the sim says down.
    // Weapon-named off the sheet's own casing (the same table chargeLine
    // reads through weaponSheetLine), with the register's own word for a
    // blade -- STEEL, as the flip line and the epitaph already say it.
    if (!tavern_->playerHandsUp()) {
        return {};
    }
    // KIT BUILD: a thing worn in the hand is named as itself -- KNIFE UP,
    // MACE UP -- the sheet's own word; the class reading stands for a hand
    // a sheet armed directly.
    if (const sim::ItemDef* held = tavern_->heldItem(); held != nullptr) {
        return held->name + " UP";
    }
    const sim::Weapon weapon = tavern_->playerHeldWeapon();
    if (weapon == sim::Weapon::Edged) {
        return "STEEL UP";
    }
    std::string sheet = sim::weaponSheetLine(weapon);
    std::size_t span = 0;
    while (span < sheet.size() && (sheet[span] < '0' || sheet[span] > '9')) {
        ++span;
    }
    std::string name = sheet.substr(0, span);
    while (!name.empty() && name.back() == ' ') {
        name.pop_back();
    }
    return name + " UP";
}

std::string Session::effectLine(std::size_t slot) const {
    // HELD-EFFECTS BUILD. The slot-th live hold, name and seconds left. The
    // name comes out of the grimoire by the id the hold itself carries --
    // the same one source of truth the CAST row reads -- and the clock is
    // the room's own holdSecondsLeft, re-read every step so the row counts
    // down continuously rather than snapping on expiry.
    const std::vector<sim::Tavern::ActiveHold>& holds = tavern_->heldEffects();
    if (slot >= holds.size() || slot >= kEffectRows) {
        return {};
    }
    const sim::Tavern::ActiveHold& hold = holds[slot];
    const sim::Spell* spell = tavern_->dialogue().grimoire().find(hold.spellId);
    std::string name =
        spell != nullptr ? upperAscii(spell->displayName) : upperAscii(hold.spellId);
    return clip(name + " " + std::to_string(tavern_->holdSecondsLeft(hold)) + "S", 34);
}

std::string Session::contractLine() const {
    // The job with the least time left on it, because that is the one a player
    // needs to be reminded about.
    const sim::ContractBoard& board = tavern_->dialogue().contracts();
    const sim::Contract* soonest = nullptr;
    for (const sim::Contract& row : board.contracts()) {
        if (!row.live()) {
            continue;
        }
        if (soonest == nullptr || row.dueOnDay < soonest->dueOnDay ||
            (row.dueOnDay == soonest->dueOnDay && row.id < soonest->id)) {
            soonest = &row;
        }
    }
    if (soonest == nullptr) {
        return {};
    }
    const std::int32_t have =
        tavern_->dialogue().crimes().stash().count(soonest->good);
    return clip(soonest->label + " " + std::to_string(have) + "/" +
                    std::to_string(soonest->units),
                34);
}

std::string Session::objectiveLine() const {
    // And the row above it, for the same reason: the lock's own row is bounded
    // by these two and a long lock line is nearly the width of the frame.
    if (picking()) {
        return {};
    }
    const sim::DialogueDirector& talk = tavern_->dialogue();
    // A TAKEN JOB OUTRANKS AN AUTHORED STAGE, because a job has a deadline and
    // a questline does not. The Skyrunner line graduates into contract work,
    // so by the time a player is holding one the line is finished anyway.
    if (const std::string work = contractLine(); !work.empty()) {
        return work;
    }
    for (const sim::Questline& line : talk.quests().lines()) {
        if (!talk.journal().started(line.id) || talk.journal().done(line.id)) {
            continue;
        }
        const std::int32_t at = talk.journal().stage(line.id);
        if (at < 0 || static_cast<std::size_t>(at) >= line.stages.size()) {
            continue;
        }
        // The stage LABEL, not its objective: the label is already short, upper
        // case menu furniture, and the objective is a paragraph that belongs in
        // a journal rather than in the corner of a frame.
        return clip(line.stages[static_cast<std::size_t>(at)].label, 34);
    }
    // THE CASE'S OWN DESTINATION IS NOT PUT HERE. It has its own row lower down
    // the stack -- see caseLine() -- because this one sits at y - 32*scale,
    // which crosses the exclusion rectangle whenever it is non-empty, and the
    // orientation a new player needs must not be the thing that breaks the HUD
    // rule to deliver it.
    return {};
}

std::string Session::standingLine() const {
    // ABSENCE COSTS NOTHING, the identical rule every other row on this stack
    // already keeps: "NOBODY IN PARTICULAR" is the ward having no opinion of
    // you at all, and the row only appears the moment it HAS one -- see
    // drawFrame()'s own comment where this used to be computed inline.
    const std::string_view standing = tavern_->dialogue().ledger().reputationLabel();
    if (standing == sim::kReputationUnremarkable) {
        return {};
    }
    return std::string(standing);
}

namespace {

/// FAST TRAVEL's arrival seam: the finished frame dipped toward black by
/// `amount`, whatever was composed under it -- the world, the map's own close
/// tail, or a page opened mid-fade. The last act of every drawFrame() return
/// path, so the commit frame is already dark and the destination eases up
/// from under it. Render-only; nothing here is read back.
void dipTravelSeam(Framebuffer& target, float amount) {
    if (amount <= 0.0F) {
        return;
    }
    target.fillRect(0, 0, target.width(), target.height(), Rgb{0.0F, 0.0F, 0.0F},
                    amount > 1.0F ? 1.0F : amount);
}

}  // namespace

void Session::drawFrame(Framebuffer& target) const {
    // The flicker phase is a pure function of the body's step count, so the
    // same scripted session captures the same frame every time.
    const float phase = static_cast<float>(body_->stepCount()) / 60.0F;
    const Camera view = camera();
    // THE WORLD IS THE RAYLIB BACKEND'S FRAME, and this target is the OVERLAY
    // it composites on top: cleared to nothing, then every pass below draws
    // over it. There is no depth under it, which is why the signage is
    // handed a line of sight instead (below).
    target.clearTransparent();

    // World content, not interface -- drawn rather than gated behind
    // config_.hud, and BEFORE the HUD/menu/dialogue passes below so none of
    // them paint over a legible sign.
    // AND THE ONE THING ON THE SCREEN THE SIGNS COULD NOT SEE.
    //
    // Signage is drawn before the HUD so nothing paints over a legible sign --
    // and the consequence was that the HUD painted over one. The crosshair
    // prompt sits at the exact centre of the frame, which is where you are
    // looking, which is where the nearest sign projects: on the Tarwalk the
    // ward's own "TARWALK" and the prompt's "E - TALK" landed on the same
    // pixels and the prompt won by being second, cutting a street name through
    // the middle.
    //
    // The signage renderer already knew how to place labels around each other.
    // It just needed to be told the interface is on the screen too. The
    // rectangle is the prompt's OWN footprint this frame -- built from the same
    // aim fields the HUD is about to be handed, four lines below -- and not the
    // whole clamp fence, which would cost every sign in the centre band.
    //
    // A frame with nothing under the reticle hands back an empty rect and the
    // signs are placed exactly as they were before this existed.
    //
    // THE FURNITURE, IN ONE PLACE. `config_.hud` is the --nohud instrument and
    // `hudStandDown_` is a card owning the frame (see Session::setHudStandDown);
    // below, every interface pass reads THIS rather than testing the two
    // itself, so the two switches cannot drift apart one branch at a time.
    const bool furniture = config_.hud && !hudStandDown_;

    SignageSettings signage;
    // ...AND THE EXCLUSION IS COMPUTED ONLY WHEN THERE IS A PROMPT TO EXCLUDE.
    //
    // It used to be computed unconditionally, so under --nohud the signs still
    // stepped around an aim prompt that was never drawn. Zero gameplay impact
    // and a real measurement one: --nohud is the RULER this project takes every
    // interface number with, and a ruler that moves the world it is measuring
    // overstates every reading it gives. 368 to 656 pixels of a 640x360 frame
    // (0.16-0.29%) were counted as interface that were signs standing somewhere
    // else. See docs/HUD-REAL-ESTATE.md.
    if (config_.hud) {
        HudState aim;
        // SHIP NOTE MOVE 3: the half of Interact's row that belongs to the
        // device that last spoke -- "E" or "A" on the shipped table. Must
        // match the HUD's own read four blocks below, or the signage would
        // step around a prompt of a different width than the one drawn.
        aim.aimKey = promptKeyName(promptKey(controls_, Action::Interact, promptDevice_));
        aim.aimVerb = std::string_view{interactCache_};
        aim.aimSubject = std::string_view{interactSubjectCache_};
        aim.aimNote = std::string_view{interactNoteCache_};
        aim.interactFade = interactAnim_.value();
        signage.exclusion = hudAimPromptRect(aim, target.width(), target.height());
        // ...AND THE NAME IT IS ALREADY SAYING. See
        // SignageSettings::namedByPrompt: the prompt carries the verb and the
        // key, so where the two would say the same building the SIGN yields.
        // Gated on the prompt actually being on screen (interactFade), or a
        // sign would vanish for a subject the crosshair had already left.
        if (aim.interactFade > 0.0F) {
            signage.namedByPrompt = aim.aimSubject;
        }
    }
    // Signage is WORLD CONTENT and stays outside config_.hud -- the ruler has
    // to keep measuring it. A card is the one thing it yields to.
    //
    // WHICH SIGNS THE EYE CAN SEE is the lens's own line of sight over the
    // tiles (cameraSightClear: the walk a camera needs, not the sim's
    // person-to-person one), because this frame is an overlay with no depth
    // under it. The wall a sign is nailed to is the walk's last cell, which a
    // body or a sign standing in a cell never blocks.
    signage.eyeBand = body_->band();
    signage.sightClear = [this](float anchorX, float anchorY, int band) {
        return cameraSightClear(*tiles_, body_->x(), body_->y(),
                                static_cast<std::int32_t>(std::lround(anchorX * 256.0F)),
                                static_cast<std::int32_t>(std::lround(anchorY * 256.0F)), band,
                                false);
    };
    if (!hudStandDown_) {
        drawSignage(target, view, signage);
    }

    // INNOVATION SPRINT ITEM #3. A BRAWL FINALLY HAS SOME PHYSICAL WEIGHT.
    // Two brief, low-alpha washes over the WORLD -- drawn here, before the
    // HUD, so the one readout that matters most while this fires (the health
    // bar) is never sitting on top of the wash instead of over the plain
    // world. RESTRAINED ON PURPOSE: this is a moody, text-forward
    // investigation game and not an arcade brawler, so both peak under a
    // fifth of full strength and are gone within a handful of frames -- see
    // punchLandedPulse_/punchTakenPulse_'s own header for why an
    // ImpactPulse (a one-shot event) rather than a held EasedToggle is what
    // drives this, and why there is no third or fourth flash bolted on: two
    // real moments, executed with restraint, rather than a scattershot pass
    // across every verb that could theoretically want one.
    if (config_.hud) {
        const float landed = punchLandedPulse_.value();
        if (landed > 0.0F) {
            // A CONNECTING PUNCH: pale and warm, the same register the
            // lantern flame and the plate's own bone border already draw in
            // -- an "it worked" beat, not a warning.
            target.fillRect(0, 0, target.width(), target.height(), Rgb{0.92F, 0.86F, 0.70F},
                            0.16F * landed);
        }
        const float taken = punchTakenPulse_.value();
        if (taken > 0.0F) {
            // A BLOW LANDING ON THE PLAYER: a low, blooded red, read as a
            // wince rather than an alarm -- deliberately not brighter than
            // the landed-punch flash above, which is meant to feel like the
            // better of the two moments.
            target.fillRect(0, 0, target.width(), target.height(), Rgb{0.58F, 0.10F, 0.08F},
                            0.18F * taken);
        }
        // FIRST-PERSON COMBAT (S13). A BLOW THE GUARD CAUGHT: cool steel
        // instead of blood, and QUIETER than either flash above -- the guard
        // working is the calmest of the three moments, and step() fires this
        // INSTEAD of the taken-wash for a blocked blow, never as well, so
        // this stays inside the "two real moments, executed with restraint"
        // budget rather than stacking on top of it.
        const float blocked = blockPulse_.value();
        if (blocked > 0.0F) {
            target.fillRect(0, 0, target.width(), target.height(), Rgb{0.52F, 0.60F, 0.70F},
                            0.12F * blocked);
        }
    }

    HudState hud;
    hud.health = tavern_->playerHp();
    hud.healthMax = 100;
    // FATIGUE BUILD: the wind, in points, right beside the hit points it
    // stands under on the frame. The fade is the bar's own EasedToggle
    // (fatigueAnim_), mirroring showHealth's one rule without borrowing its
    // snap -- see the member's header.
    hud.fatigue = tavern_->playerFatigue().currentPoints();
    hud.fatigueMax = tavern_->playerFatigue().maxPoints();
    hud.fatigueFade = fatigueAnim_.value();
    hud.yawBam = body_->yaw();
    // UI-EA (LANE HUD): NO STREET SUB-LABEL. placeLabel() still answers for
    // the map's title and the dialogue epithet; the street's own copy of it
    // is deleted -- the threshold plate announces every crossing, and a row
    // that restated it every frame was the diet's first cut. The clock and
    // the purse are earned text on their own toggles: numbers stay live
    // (they are what wakes), the fades are what sleep.
    hud.timeOfDaySeconds = timeOfDay_;
    hud.clockFade = clockAnim_.value();
    hud.coin = tavern_->playerCoin();
    hud.purseFade = purseAnim_.value();
    // While a conversation is open the bottom band belongs to the topic list,
    // so the room line and the running message stand down rather than draw on
    // top of it.
    // The casebook and the key list stand the HUD down exactly the way a
    // conversation does: all three are drawn in the same two bands, and two
    // things fighting over one row is how the centre-clear rule gets broken by
    // accident.
    //
    // #77 ADDS THE OPTIONS PAGE, and it was found by opening the real window and
    // pressing F2. Every other surface was on this list; the new one was not, so
    // the compass strip, the case row, the health bar and the first-run hint all
    // drew straight over the sliders and the sliders drew straight back, and
    // both were illegible. Nothing in a `--screenshot` capture would ever have
    // shown it, because nothing scripted opens this page.
    const bool conversing = conversingNow();
    // HARDENING PASS. roomLabel READS THE CACHE syncPanelAnim() JUST FILLED,
    // NOT A FRESH roomLine() CALL. The cache is what keeps a room label on
    // screen fading out after tavern_->playerInside() has already gone
    // false -- roomLine() itself would answer empty by then, same as every
    // other row below -- and roomFade is Session's own EasedToggle for this
    // row (roomAnim_), the identical pattern hud.alertFade already set for
    // the alert. See interactCache_'s own header in session.hpp.
    hud.roomLabel = std::string_view{roomCache_};
    hud.roomFade = roomAnim_.value();
    // The ward's opinion of you sits under the purse -- unless somebody is in
    // front of you, in which case THEIR opinion is the one that matters and the
    // panel is already showing it.
    //
    // ABSENCE DOES NOT COST A ROW. "NOBODY IN PARTICULAR" is the ward having no
    // opinion of you at all, and it held twenty characters of the top right in
    // every frame this game has ever produced, saying that nothing had
    // happened. The row appears the moment the ward HAS an opinion, which is
    // the only moment it is worth the sky it stands in.
    //
    // PLANNING SPRINT (item #2, the sweep). READS ITS OWN CACHE AND FADES,
    // THE IDENTICAL SHAPE roomLabel JUST USED ABOVE, instead of the bare
    // `conversing ? empty : text` this used to be. A real sweep of this file
    // found these three rows (standing, heat, the sack) still popping on and
    // off with `conversing` at full strength -- stealthLabel, right below,
    // already got this treatment; these three did not.
    hud.standingLabel = std::string_view{standingCache_};
    hud.standingFade = standingAnim_.value();
    // What the Watch has heard, what is in your coat, and whether you are
    // carrying somebody's bale. Top right under the purse, hugging the edge --
    // the centre of the frame stays empty, which is the rule.
    hud.heatLabel = std::string_view{heatCache_};
    hud.heatFade = heatAnim_.value();
    hud.stashLabel = std::string_view{stashCache_};
    hud.stashFade = stashAnim_.value();
    // FIRST-PERSON COMBAT (S13). What the hand is holding and whether the
    // guard is up -- each reading its own cache and fading the identical way
    // every row above does.
    hud.spellLabel = std::string_view{spellCache_};
    hud.spellFade = spellAnim_.value();
    hud.blockLabel = std::string_view{blockCache_};
    hud.blockFade = blockAnim_.value();
    // ACTION-COMBAT BUILD (section 5, channel 2). The HELD HARD charge row,
    // its own cache and fade the identical shape the guard row just used.
    hud.chargeLabel = std::string_view{chargeCache_};
    hud.chargeFade = chargeAnim_.value();
    // STANCE & ROOM BUILD. The FISTS UP row, its own cache and fade.
    hud.handsLabel = std::string_view{handsCache_};
    hud.handsFade = handsAnim_.value();
    hud.turnLabel = std::string_view{turnCache_};
    hud.turnFade = turnAnim_.value();
    // HELD-EFFECTS BUILD. The live holds, each reading its own cache and
    // fading on its own toggle -- the identical shape every row above uses.
    for (std::size_t slot = 0; slot < kEffectRows; ++slot) {
        hud.effectLabels[slot] = std::string_view{effectCaches_[slot]};
        hud.effectFades[slot] = effectAnims_[slot].value();
    }
    // SPELLS BUILD. The quick bar strip: names out of the cache
    // syncPanelAnim() keeps (so the fade-out still has labels), the selected
    // cell off the same quickSlot_ the number row moves, and the equipped
    // cell derived fresh from the ONE equipped id sim::Tavern holds -- the
    // same id spellLine() just read for the CAST row, so the two can never
    // disagree about what the hand is holding.
    for (std::size_t slot = 0; slot < quickBarNames_.size(); ++slot) {
        hud.quickSlots[slot] = std::string_view{quickBarNames_[slot]};
    }
    hud.quickSelected = quickSlot_;
    hud.quickEquipped = equippedQuickSlot();
    hud.quickBarFade = quickBarAnim_.value();
    // UI-EA (LANE HUD): the Q-hold tutor toast, riding the strip.
    hud.wheelHint = std::string_view{wheelHintText_};
    hud.wheelHintFade = wheelHint_.value();
    // S9. Whether the room can see you, and the lock under the wire. Both on
    // edges, both empty when they have nothing to say -- the right-hand stack
    // for the first, the bottom band for the second. Both read their own
    // cache and fade the identical way roomLabel just did, above.
    hud.stealthLabel = std::string_view{stealthCache_};
    hud.stealthFade = stealthAnim_.value();
    hud.lockLabel = std::string_view{lockCache_};
    hud.lockFade = lockAnim_.value();
    // THE CROSSHAIR PASS. THE AIM PROMPT, on the reticle rather than along the
    // bottom edge -- the owner's own sentence is at the top of hud.hpp. The
    // verb still changes to PICKPOCKET the instant the player crouches facing
    // somebody (#85's whole point, and Eli's brief: "the player must SEE what
    // pressing it will do before they press it"); what the crosshair pass adds
    // is the name of whoever that is.
    //
    // THE KEY NAME IS READ off the same binding row the CONTROLS page
    // prints, so a rebound Interact renames itself on the crosshair by the
    // act of being rebound -- and SHIP NOTE MOVE 3 made it the half of that
    // row belonging to the device that last spoke: "E - TALK" with a
    // keyboard in hand, "A - TALK" the moment a pad button lands, live.
    hud.aimKey = promptKeyName(promptKey(controls_, Action::Interact, promptDevice_));
    hud.aimVerb = std::string_view{interactCache_};
    hud.aimSubject = std::string_view{interactSubjectCache_};
    hud.aimNote = std::string_view{interactNoteCache_};
    hud.aimKind = static_cast<int>(interactKindCache_);
    hud.interactFade = interactAnim_.value();
    // ACTION-COMBAT BUILD (section 5, channel 1). THE RETICLE'S CHARGE READOUT,
    // read straight off the sim's own swing state and fed INDEPENDENT of the
    // interact prompt above -- the reticle draws during a hold even when
    // nothing is in reach (aimVerb empty), which drawAim's interact-only
    // early-return could not do. The fraction is the hold's progress toward the
    // hard threshold, the tier and the on-line flag are the sim's live reads.
    {
        float held = static_cast<float>(tavern_->playerChargeSteps()) /
                     static_cast<float>(sim::kHardSwingHoldSteps);
        held = held < 0.0F ? 0.0F : (held > 1.0F ? 1.0F : held);
        hud.aimChargeFrac = held;
        hud.aimChargeHard = tavern_->playerChargeHard();
        hud.aimChargeOnLine = tavern_->playerSightlineTarget();
    }
    // The rung, and what the line wants next. Bottom-left, over the health bar.
    hud.guildLabel = std::string_view{guildCache_};
    hud.guildFade = guildAnim_.value();
    hud.objectiveLabel = std::string_view{objectiveCache_};
    hud.objectiveFade = objectiveAnim_.value();
    // S10. Where the case stands and where it wants you next: ONE row,
    // bottom-left, in the one slot of that stack provably outside the exclusion
    // rectangle. What the ward CALLS you for the work is not on the HUD at all
    // -- it is on the casebook's own page, because a title is something you
    // look up and not something you need every frame.
    //
    // ONE SLOT, TWO TENANTS -- AND polish-1 GAVE THEM A SLOT EACH.
    //
    // The case row used to sit at y - 16*scale off the health bar, which IS
    // height - margin - 23*scale, the same pixel row the alert has used since
    // S6; the first S10 capture shipped a clue printed straight through "CASE
    // 1/4 > ...", and the fix was to suppress the case row for the six seconds
    // a message is up. The bottom band hands out slots now (see BottomBand in
    // hud.cpp), the alert takes one before the case is offered one, and the two
    // of them can no longer be given the same pixels by arithmetic. So the
    // suppression is gone and both are shown.
    // And who put you on the floor last, which is the one thing on the HUD that
    // is about somebody else rather than about you.
    hud.rivalLabel = std::string_view{rivalCache_};
    hud.rivalFade = rivalAnim_.value();
    // UI-EA (LANE HUD): the HUNTING word is off the label (rank 6 -> 3); the
    // fact rides this bool and the row's red ink, read fresh off the same
    // Nemesis the line is built from so the two can never disagree.
    if (const sim::Nemesis* worstRival = tavern_->nemesis().worst(); worstRival != nullptr) {
        hud.rivalHunts = worstRival->hunts();
    }
    hud.showCompass = !conversing;
    // DISTRICT PHASE D: THE THRESHOLD MOMENT, under the ribbon it shares a
    // band with. See HudState::placePlate and drawPlacePlate.
    //
    // `conversing ? 0 : ...` IS THE SAME LINE showCompass JUST WROTE, on
    // purpose. The plate lives in the compass's own band and it stands down
    // under the compass's own rule: while a panel owns the screen there is no
    // ribbon for it to sit under and nothing up there but somebody else's
    // page. syncPanelAnim() has already put the toggle's target down for the
    // same cases (and zeroed the countdown, so nothing resurfaces when the
    // panel closes); this is what stops the handful of frames it spends easing
    // out from being painted over the panel that suppressed it.
    hud.placePlate = std::string_view{placePlateName_};
    // THE CASEBOOK PASS. The lead-opened notice, on the identical three fields
    // and the identical conversing gate -- see HudState::casePlate.
    hud.casePlate = std::string_view{casePlateText_};
    // AND ZERO THE INSTANT THE RUNG PLATE IS WANTED (casePlateFadeNow): the
    // slot waits behind the plate, and its own ease-out must not cross the
    // plate's rise.
    hud.casePlateFade = conversing ? 0.0F : casePlateFadeNow();
    hud.casePlateDrift = casePlateAnim_.target() ? -(1.0F - casePlateAnim_.value())
                                                 : (1.0F - casePlateAnim_.value());
    hud.placePlateFade = conversing ? 0.0F : placePlateFadeNow();
    composePullHud(hud);  // THE PULL PACK: the one draw hook (render/pull.hpp).
    // THE SIGN COMES OFF THE TOGGLE'S OWN TARGET, so the plate rises THROUGH
    // its settled row rather than sliding back down the way it came -- see
    // HudState::placePlateDrift. Rising (target true): still below, closing on
    // zero. Fading (target false): already past, drifting on up and out.
    hud.placePlateDrift = placePlateAnim_.target() ? -(1.0F - placePlateAnim_.value())
                                                   : (1.0F - placePlateAnim_.value());
    // A bouncer's warning outranks anything the player did to themselves: it is
    // the one line in this game they must not miss.
    const bool warned = bouncerWarned();
    if (warned) {
        hud.alert = std::string_view{tavern_->lastWarning()};
    } else if (!conversing) {
        hud.alert = std::string_view{message_};
    }
    // TASK #83. Eased in step()/syncPanelAnim(), not here -- see
    // HudState::alertFade's own note. 1 when nothing has changed since
    // construction, which is the whole of what every caller before this field
    // existed drew. UNDER THE RUNG PLATE the field keeps this ease and the
    // words take the plate's weight on their ink (alertInkWeightNow -- the
    // product is what alertFadeNow reads back); and the row steps DOWN to
    // the plate's own register while the plate is on screen and for a line
    // capped under it (alertStepsDownNow), never a tier above the plate --
    // the smaller of the plate's register and the HUD's minor scale
    // (alertStepScale; under the height-only register a square window's
    // minor scale was a tier above the card, and since the cells pass no
    // window's is, so in play it is the minor scale).
    hud.alertFade = alertAnim_.value();
    hud.alertInkWeight = alertInkWeightNow();
    hud.alertStepsDown = alertStepsDownNow();
    hud.alertStepScale = rungPlateMetric(target.width(), target.height()).scale;
    // VOICE LANE. The street's own line, on its own toggle, down while a
    // panel owns the screen (the partner's lines are the panel's).
    hud.voiceName = std::string_view{voice_.current().name};
    hud.voiceLine = std::string_view{voice_.current().text};
    // A live line FADES under a panel opening over it rather than cutting
    // to nothing on E: its own toggle is already easing down (the target
    // drops with conversingNow), and the panel's own rise caps it so the
    // two cross rather than overprint. The full-page surfaces below zero
    // it outright, as they do the alert.
    // AND AT THE RUNG PLATE'S WEIGHT: a passer-by's bark or Cull's farewell
    // sat at full strength at the foot of the trade and law frames, wider
    // than the plate, while the alert row beside it was halved (all three
    // critics' finding). The same weight, on the same fade.
    hud.voiceFade = conversing ? std::min(voiceAnim_.value(), 1.0F - panelAnim_.value())
                               : voiceAnim_.value();
    hud.voiceInkWeight = alertInkWeightNow();
    // AND ITS OWN STAND-DOWN: the plate's register and the quarter rails
    // while the plate is on screen and for a line capped under it, so a
    // capped bark finishes small and never swells (the auditor, on the
    // square windows).
    hud.voiceStepsDown = voiceStepsDownNow();
    // INNOVATION SPRINT ITEM #3. See HudState::alertPulse's own header.
    hud.alertPulse = alertPulse_.value();
    hud.caseLabel = std::string_view{caseCache_};
    hud.caseFade = caseAnim_.value();
    hud.showHealth = !conversing;
    // AND THE BOTTOM BAND IS THE TOPIC LIST'S, WHOLE. The alert used to be
    // drawn over it and S7 shipped the frame that proves it -- see
    // DialogueViewState::alert. It moves into the conversation's top band while
    // one is open, which is sized from what it draws, so a warning shouted
    // across the room is still read and nothing is drawn on top of anything.
    hud.showAlert = !conversing;
    // THE WARD MAP (core action #13) -- its own full-screen surface, drawn by
    // map_view.hpp's drawDistrictMap, exempt from the centre-clear rule for
    // exactly the reason the tiled Menu is (menu_view.hpp's own header):
    // there is nobody to look at while a map is up. The close tail draws too
    // (its own EasedToggle still above zero) unless another page has already
    // taken the frame -- conversingNow() is false only when nothing else is
    // up, so a map closed INTO the tiled Menu hands over immediately.
    if (districtMapOpen_ || (districtMapAnim_.value() > 0.0F && !conversing)) {
        // THE MAP PASS. The page is a COMPOSED PANE with a cursor now, so the
        // state it draws from is built in one place (districtMapState()) that a
        // case can read, rather than assembled inline where nothing but a
        // screenshot could ever check it.
        DistrictMapState plan = districtMapState();
        // Cross-lane contract (b), wired at integration: FLOW arms the pulse
        // at commit routing, PAGES renders it off the state field -- this is
        // the one assignment that joins them, per composition.
        plan.commitPulse = commitPulse_.value();
        plan.tutor = mapTutor_.value();  // contract (c): the raised verb words
        // THE HUD STANDS DOWN UNDER IT, which the old static page could get
        // away with not doing and this one cannot. The compass ribbon prints
        // the place name across the top centre and the clock/purse stack sits
        // top right -- exactly where this composition's breadcrumb row and its
        // right-aligned readout now live, and the first capture of this page
        // had the ward's own street name printed straight through its
        // breadcrumb. Same ruling the controls page already made and for the
        // same reason: nothing is being discussed and no door is about to shut
        // while a map is up, and a warning still outranks the page -- it is
        // routed into the page's own header band rather than painted over it.
        hud.timeOfDaySeconds = -1;
        hud.coin = -1;
        hud.showCompass = false;
        hud.placePlateFade = 0.0F;
        hud.casePlateFade = 0.0F;
        hud.showHealth = false;
        hud.showAlert = false;
        hud.voiceFade = 0.0F;
        if (warned) {
            // The bouncer's warning outranks a map read -- the same routing
            // the tiled Menu gives its journal tile, below. It takes the
            // breadcrumb row, which the composition holds open regardless, so
            // nothing under it moves when a bouncer starts talking.
            plan.alert = tavern_->lastWarning();
        }
        if (furniture) {
            drawDistrictMap(target, plan);
            drawHud(target, hud);
            dipTravelSeam(target, travelFadeAnim_.value());
            composeDeathCeremony(target);
            composeRopeCeremony(target);
            composeTakenPlate(target);
        }
        panelTailTiles_ = false;
        panelTailCourt_ = false;
        return;
    }
    // PANES PASS: THE CONTROLS PAGE IS THE FIRST SURFACE DRAWN IN THE
    // TERMINAL-PANEL REGISTER (render/keys_page.hpp, on render/panel.hpp's
    // vocabulary) rather than through the single conversation panel. It is a
    // FULL-PAGE composition, like the tiled Menu and like every one of the
    // owner's reference frames, and for the same reason menu_view.hpp already
    // gives: nobody is standing in front of you while you read your own key
    // bindings, so there is nothing in the centre of the frame to leave clear.
    //
    // THE CLOCK AND THE PURSE STAND DOWN WHILE IT IS UP. That is this build's
    // existing rule about overlays, applied to a new one: the top-right stack
    // is drawn AFTER the panel and would otherwise land in the tab row, on top
    // of the build readout that row right-aligns. Nothing is being discussed
    // and no door is about to shut, so the hour and the purse have nothing to
    // say for the few seconds this page is open. Everything else about the HUD
    // is untouched, and a warning still outranks the page -- it is routed into
    // the page's own header band instead of being painted over it.
    if (keysOpen_) {
        KeysPageState page = keysPageState();
        page.commitPulse = commitPulse_.value();  // contract (b), see the map pass
        page.tutor = keysTutor_.value();          // contract (c)
        page.openAmount = panelAnim_.value();
        page.open = page.open || panelAnim_.value() > 0.0F;
        if (warned) {
            page.alert = std::string(tavern_->lastWarning());
        }
        hud.timeOfDaySeconds = -1;
        hud.coin = -1;
        if (furniture) {
            drawKeysPage(target, page);
            drawHud(target, hud);
            dipTravelSeam(target, travelFadeAnim_.value());
            composeDeathCeremony(target);
            composeRopeCeremony(target);
            composeTakenPlate(target);
        }
        panelTailTiles_ = false;
        panelTailCourt_ = false;
        return;
    }
    // MORROWIND ROUND: THE TILED MENU IS A DIFFERENT SURFACE FROM THE SINGLE
    // CONVERSATION PANEL, drawn by a different function (menu_view.hpp's
    // drawMenuTiles rather than dialogue_view.hpp's drawDialogue) because it
    // deliberately does NOT respect the HUD's centre-clear rule the way every
    // other overlay in this build still does -- see menu_view.hpp's own
    // header on why a Morrowind-style overview is exempt and a live
    // conversation is not.
    // THE CASEBOOK PASS. THE JOURNAL TILE IS A COMPOSED PAGE NOW, drawn by
    // casebook_page.hpp in the terminal-panel register rather than as the
    // bottom third of four tiles.
    //
    // WHY THE OTHER THREE TILES ARE UNTOUCHED, said plainly rather than left to
    // be discovered: this phase is contracted for the casebook and the casebook
    // alone, and converting Character/Chart/Letters would be three more surfaces
    // this pass has no frames of and no cases for. The seam that leaves is real
    // -- the book is a composed page and its three siblings are still tiles --
    // and a player only ever sees one of the two at a time, because the Menu
    // draws whichever tile has FOCUS. The bumpers still step between them and
    // the book's own nav band says so.
    //
    // THE HUD STANDS DOWN THE SAME WAY IT DOES UNDER THE WARD MAP, and for the
    // same reason: this composition's breadcrumb runs across the top left and
    // its readout is right-aligned in the tab row, which is exactly where the
    // compass ribbon and the clock/purse stack live. A warning still outranks
    // the page -- it is routed into the page's own header band, which the
    // composition holds open regardless, so nothing under it moves when a
    // bouncer starts talking.
    if (casebookPageOpen()) {
        CasebookPageState page = casebookPageState();
        page.commitPulse = commitPulse_.value();  // contract (b), see the map pass
        page.tutor = casebookTutor_.value();      // contract (c)
        page.openAmount = panelAnim_.value();
        page.open = page.open || panelAnim_.value() > 0.0F;
        if (warned) {
            page.alert = std::string(tavern_->lastWarning());
        }
        hud.timeOfDaySeconds = -1;
        hud.coin = -1;
        hud.showCompass = false;
        hud.placePlateFade = 0.0F;
        hud.casePlateFade = 0.0F;
        hud.showHealth = false;
        hud.showAlert = false;
        hud.voiceFade = 0.0F;
        if (furniture) {
            drawCasebookPage(target, page);
            drawHud(target, hud);
            dipTravelSeam(target, travelFadeAnim_.value());
            composeDeathCeremony(target);
            composeRopeCeremony(target);
            composeTakenPlate(target);
        }
        panelTailTiles_ = false;
        panelTailCourt_ = false;
        return;
    }
    // UI-EA-SPEC sec. 3 rule 4 -- CLOSE HONESTY: the tiled Menu's close tail
    // draws as the TILES easing out, not as the empty single panel the old
    // handover fell through to (the seam the comment at the dialogue branch
    // below used to admit). panelTailTiles_ is drawFrame's own memo of what
    // it drew the panel fade for last frame -- see its header in session.hpp.
    if (casebookOpen_ ||
        (panelTailTiles_ && !conversing && panelAnim_.value() > 0.0F)) {
        MenuTileState tiles;
        tiles.open = true;
        tiles.character = characterPanelView();
        tiles.map = mapPanelView();
        tiles.letters = lettersPanelView();
        tiles.journal = journalPanelView();
        if (warned) {
            // THE BOUNCER'S WARNING, ROUTED INTO THE JOURNAL TILE. The tiled
            // Menu has no single top band of its own to carry it the way a
            // conversation or one of #85's six pages did -- see
            // DialogueViewState::alert's own header -- so it lands on the
            // one tile that is always on screen regardless of focus and
            // already reads as "your own notes", the same place a warning
            // interrupted a casebook read before this round.
            tiles.journal.alert = tavern_->lastWarning();
        }
        tiles.focus = menuFocus_;
        tiles.phase = phase;
        // NINE AND THE STICKS: the hub's foot, in the live hand's keycaps --
        // the ring is learnable from every page of it, this one included.
        tiles.navMoveKeys = std::string(promptMoveKeys(promptDevice_));
        tiles.navPageKeys = std::string(promptPageKeys(promptDevice_));
        tiles.confirmKey = std::string(promptConfirmKey(promptDevice_));
        tiles.closeKey =
            promptDevice_ == InputDevice::Pad
                ? std::string(promptBackKey(InputDevice::Pad))
                : std::string(promptLabel(controls_, Action::Menu, InputDevice::KeyboardMouse));
        // TASK #83's OWN EASE, REUSED. See the identical note on the
        // single-panel path below.
        tiles.openAmount = panelAnim_.value();
        // INNOVATION SPRINT ITEM #2. See MenuTileState::characterFocus's own
        // header -- each tile's border eases toward or away from focus off
        // its own EasedToggle rather than snapping the instant menuFocus_
        // changes.
        tiles.characterFocus = characterFocusAnim_.value();
        tiles.mapFocus = mapFocusAnim_.value();
        tiles.lettersFocus = lettersFocusAnim_.value();
        tiles.journalFocus = journalFocusAnim_.value();
        if (furniture) {
            drawMenuTiles(target, tiles);
            drawHud(target, hud);
            dipTravelSeam(target, travelFadeAnim_.value());
            composeDeathCeremony(target);
            composeRopeCeremony(target);
            composeTakenPlate(target);
        }
        // The memo: this frame's panel fade belonged to the tiles. Only the
        // LIVE menu re-arms it -- a tail frame keeps it as-is, so the tail
        // keeps drawing tiles until panelAnim_ settles and the condition
        // above goes quiet on its own.
        panelTailTiles_ = panelTailTiles_ || casebookOpen_;
        return;
    }
    // UI-EA-SPEC 1.7 (LANE PAGES, strip->card): pause, wait, options and the
    // grimoire draw as the composed card now, not as a HUD strip -- the ship
    // note's one remaining strip surface, converted with its family. Input
    // and close routing are untouched (the flags, cursors and digit windows
    // are exactly the strip's own); the CLOSE fade still runs through the
    // panel path below until the close-honesty pass (sec. 3 rule 4, LANE
    // FLOW) teaches it to fade as what it was.
    if (stripCardOpen()) {
        CreationPage card = stripCard();
        card.alpha = panelAnim_.value();
        // THE CLOCK KEEPS THE PAUSE (spec #34: "rows, clock, title stay") --
        // integration kept HUD's ruling over the family stand-down the other
        // page passes make: the pause stack is where a player stands still to
        // read the hour, and HUD's own earned-text logic already wakes the
        // clock for it. The purse still has nothing to say here.
        hud.coin = -1;
        if (furniture) {
            drawCreationPage(target, card);
            drawHud(target, hud);
            dipTravelSeam(target, travelFadeAnim_.value());
            composeDeathCeremony(target);
            composeRopeCeremony(target);
            composeTakenPlate(target);
        }
        return;
    }
    // JUSTICE BUILD (HEARING PAGE LANE). THE HEARING PAGE: a full-page
    // composition in the terminal register (hearing_page.hpp), drawn into
    // the framebuffer exactly as the casebook page is, so the 3D overlay
    // composites it unchanged. Below the pause card on purpose -- PAUSE is
    // the one page allowed over the bench -- and above the conversation
    // panel, which cannot be open under it (openCourt ends any talk). The
    // HUD stands down the way it does under every composed page; a bouncer's
    // warning still outranks it and rides the page's own header band.
    if (courtOpen_ || (panelTailCourt_ && !conversing && panelAnim_.value() > 0.0F)) {
        HearingPageState page = hearingPageState();
        page.openAmount = panelAnim_.value();
        page.open = page.open || panelAnim_.value() > 0.0F;
        if (warned) {
            page.alert = std::string(tavern_->lastWarning());
        }
        hud.timeOfDaySeconds = -1;
        hud.coin = -1;
        hud.showCompass = false;
        hud.placePlateFade = 0.0F;
        hud.casePlateFade = 0.0F;
        hud.showHealth = false;
        hud.showAlert = false;
        hud.voiceFade = 0.0F;
        if (furniture) {
            drawHearingPage(target, page);
            drawHud(target, hud);
            dipTravelSeam(target, travelFadeAnim_.value());
            composeDeathCeremony(target);
            composeRopeCeremony(target);
            composeTakenPlate(target);
        }
        panelTailTiles_ = false;
        // The memo: this frame's panel fade belonged to the bench. Only the
        // LIVE page re-arms it, exactly as the tiles' memo works.
        panelTailCourt_ = panelTailCourt_ || courtOpen_;
        return;
    }
    DialogueViewState panel = dialogueView();
    if (conversing && warned) {
        panel.alert = tavern_->lastWarning();
    }
    // TASK #83. THE PANEL EASES OPEN AND CLOSED INSTEAD OF POPPING.
    // panelAnim_.value() is what drawDialogue actually fades against -- see
    // DialogueViewState::openAmount. `panel.open` is ALSO forced true for as
    // long as any of that fade is still on screen, because dialogueView()
    // reports the panel closed the instant the LAST overlay flag goes false,
    // and a close animation needs a few more frames of "yes, still drawing"
    // after that to have anything left to fade. Once panelAnim_ settles at 0
    // this is exactly `panel.open` again, which is the pre-existing behaviour
    // for every caller and every test that never heard of this pass.
    //
    // UI-EA-SPEC sec. 3 rule 4: the tiled Menu's closing tail no longer
    // lands here -- the branch above catches it off panelTailTiles_ and
    // fades the TILES, which is the close-honesty rule ("a page fades as
    // what it was"). What still lands here is every single-panel surface's
    // own tail -- Keys, Options, Wait, a conversation -- which genuinely
    // WAS this panel, so fading as it is honest. The memo is cleared here
    // whenever a live panel draws, so a keys tail after a menu visit cannot
    // resurrect the tiles.
    panel.openAmount = panelAnim_.value();
    if (conversing) {
        panelTailTiles_ = false;
        panelTailCourt_ = false;
    }
    panel.open = panel.open || panelAnim_.value() > 0.0F;
    // The panel FIRST, the HUD over it: a bouncer's warning has to survive
    // being told mid-conversation, and it is the one line that outranks a menu.
    if (furniture) {
        drawDialogue(target, panel);
        drawHud(target, hud);
        // THE RUNG PLATE: over the HUD, under the travel seam, the ceremonies
        // and the court's own plate -- everything below outranks it by being
        // painted after it. Only on THIS path, the world's: every composed
        // page above returned before here, and a page owning the screen is
        // exactly what dismisses the plate (stepRungPlate's rule).
        composeRungPlate(target);
        dipTravelSeam(target, travelFadeAnim_.value());
        composeDeathCeremony(target);
        composeRopeCeremony(target);
        composeTakenPlate(target);
    }
}

}  // namespace granadad::render
