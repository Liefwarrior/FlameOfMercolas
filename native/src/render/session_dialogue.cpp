// Session conversation: talk to a ward, topics, forge, haggle and offer, the dialogue view.
#include "session_internal.hpp"

// THE AUDIO WIRING PASS, as in session.cpp: this file speaks to the audio engine in ids only; see
// setAudio() in session.hpp and the determinism note in audio_engine.hpp.
#include "granadad/audio/audio_engine.hpp"

namespace granadad::render {

namespace {

/// Puts `cursor` on the row this number key names, if that row is actually on
/// the page showing -- chooseVisibleTopic()'s own shape. Out of range is a
/// no-op, the same as every list on this surface gives a number with nothing
/// under it.
void pickCursorIfVisible(int& cursor, int page, int slot, std::size_t total) noexcept {
    const int index = page * kTopicPageSize + slot;
    if (index < static_cast<int>(total)) {
        cursor = index;
    }
}

}  // namespace

void Session::sayAs(const std::string& who, const std::string& line, bool report) {
    // THE SPEAKER'S LINE ON THE ALERT ROW. A REPORT is not their voice: the
    // director answers a deed with a short account of it written in the
    // speaker's own name, and prefixing that put the name on the row twice
    // -- "WATCHMAN CULL: WATCHMAN CULL TAKES THE DRINK.", the 2026-09-25
    // critic's D_law frame. The director says which it wrote (Reply::report,
    // set where the report is composed); the text is never read to guess,
    // so a rumour that opens on the speaker's own name keeps its "<name>: ".
    // say() clears the mouth and the demand flag; the mouth is set back
    // here, and a Watch site marks its demand (messageDemand_) after the
    // call, so a halt in a watchman's name is both his and a demand.
    say(report ? line : who + ": " + line);
    messageMouth_ = who;
}

void Session::say(std::string line) {
    // Clipped to what the bottom edge can hold at the narrowest resolution this
    // game runs at. S4 started routing a questline's journal prose through here
    // -- whole sentences out of the raws -- and the first capture of it ran off
    // the right edge mid-word, which looks like a bug because it is one.
    //
    // S10 RAISED IT FROM 56 TO 92. Fifty-six was the column count of a 320x180
    // frame, and it was the belt to the HUD's own braces -- drawHud clips the
    // alert with clipToWidth against the REAL frame width, which is the fix the
    // S6 review's cut-mid-glyph finding produced. So the guess here was
    // truncating a clue at 640x360, where the row holds far more, for no reason
    // but its own age. The HUD still clips; this only stops a runaway string
    // being carried around.
    constexpr std::size_t kAlertColumns = 92;
    if (line.size() > kAlertColumns) {
        line.resize(kAlertColumns);
        line += "..";
    }
    message_ = std::move(line);
    // Nobody's until sayAs says whose: a bare say() is a narration.
    messageMouth_.clear();
    // An ordinary line. The four Watch sites (the street's three in step()
    // and the Gull's own lastDemand, all through sayAs) set this back after
    // the call -- see rungPlateOutranked.
    messageDemand_ = false;
    // A new line, at whatever register the frame gives it, and news
    // unless the site says otherwise (messageHeld_).
    messageCapped_ = false;
    messageHeld_ = true;
    // UI-EA (LANE HUD): FOUR SECONDS, the spec's own message-row narration
    // hold (sec. 2, EVENT tier). Six was the pre-diet number; a line the
    // player is reading is re-armed by the next line anyway, and the bottom
    // of the frame is meant to be empty more than it is full. A bouncer's
    // WARNING is not on this clock -- `warned` holds the alert up for as
    // long as the house is still saying it (syncPanelAnim's own rule).
    messageSteps_ = 4 * sim::kStepsPerSecond;
    // TASK #83. EAGER, so the very first frame drawn after whichever verb
    // called this -- most of them do not call step() first, and the caller
    // here could be a test that never does -- already shows the prompt easing
    // in rather than waiting for the next real step() to notice messageSteps_
    // went from 0 to nonzero.
    syncPanelAnim();
}

bool Session::talking() const noexcept {
    return tavern_->dialogue().isOpen();
}

bool Session::haggling() const noexcept {
    return tavern_->dialogue().isHaggling();
}

const sim::WardActor* Session::wardAddresseeNow() const { return wardAddressee(); }

bool Session::wardAtCounterNow(const sim::WardActor& who) const noexcept {
    return wardAtCounter(who);
}

bool Session::wardAtCounter(const sim::WardActor& who) const noexcept {
    // SHOPS. At the post the bake gave them, give or take the room it stands
    // in: a keeper is behind the counter while they are working it, and met
    // anywhere else -- the street at midnight, their own doorway -- says so.
    return who.band == who.anchorBand && std::abs(who.x - who.anchorX) <= kCounterReachTiles &&
           std::abs(who.y - who.anchorY) <= kCounterReachTiles;
}

void Session::settleCounterReply(sim::Reply& reply) {
    // SHOPS (2026-10-09). The director said who wants what for how much; the
    // Kit's owner says whether it fits, and the keeper's own purse -- the
    // ward's -- takes the coin or pays it.
    sim::DialogueDirector& talk = tavern_->dialogue();
    const sim::WardActor* keeper = people_->byId(wardTalkId_);
    if (keeper == nullptr) {
        return;
    }
    std::string line;
    if (reply.kind == sim::TopicKind::BuyItem) {
        const sim::ServiceResult sold =
            tavern_->buyFromCounter(reply.itemId, reply.itemPrice, line);
        if (sold == sim::ServiceResult::Served) {
            people_->payCoinTo(wardTalkId_, reply.itemPrice);
        }
        reply.ok = sold == sim::ServiceResult::Served;
    } else {
        // A pawnbroker pays out of his own purse, and a short one is short.
        if (keeper->coin < reply.itemPrice) {
            line = upperAscii(people_->identity(wardTalkId_).name) + " HAS NOT GOT IT TODAY.";
            reply.ok = false;
        } else {
            const sim::ServiceResult bought =
                tavern_->sellToCounter(reply.itemId, reply.itemPrice, line);
            if (bought == sim::ServiceResult::Served) {
                (void)people_->takeCoinFrom(wardTalkId_, reply.itemPrice);
            }
            reply.ok = bought == sim::ServiceResult::Served;
        }
    }
    reply.line = line;
    reply.report = true;
    reply.coinDelta = 0;
    // The shelf and the pawn list read the Kit and the purse: rebuild them.
    talk.setPlayerCoin(tavern_->playerCoin());
    talk.setCounter(tavern_->counterViewFor(talk.speaker().notableId, wardAtCounter(*keeper)));
    talk.refreshTopics();
    if (audio_ != nullptr) {
        audio_->playOneShot(reply.ok ? audio::SoundId::UiConfirm : audio::SoundId::UiError);
    }
}

const sim::WardActor* Session::wardAddressee() const {
    // ERRANDS A HUMAN CAN FINISH (2026-10-09). Whoever E would address from
    // here: the body IN FRONT of the eye within talk reach, and only when
    // nobody stands in front, the nearest -- so a crowd at the Mission's
    // night pot no longer decides who you are talking to. The crosshair's
    // TALK prompt asks this same question, so the name it prints before the
    // press is the one that answers it.
    if (const sim::WardActor* ahead = people_->inFrontOf(
            body_->x(), body_->y(), body_->band(), body_->yaw(), kWardTalkReachTiles);
        ahead != nullptr) {
        return ahead;
    }
    return people_->nearestTo(body_->tileX(), body_->tileY(), body_->band(), kWardTalkReachTiles);
}

bool Session::talkToWard() {
    // #79. THE OTHER SIX HUNDRED AND FORTY-SEVEN.
    //
    // The Gull's fourteen are asked for first, above, because inside K03 the
    // taproom owns the room and its people carry a roster, a mood and a stock;
    // out here nobody does. Everything below is the SAME director, the same
    // authored tables and the same fallback chain -- only the Speaker is built
    // from a body in the street instead of from a body on a stool.
    const sim::WardActor* who = wardAddressee();
    if (who == nullptr) {
        return false;
    }
    sim::DialogueDirector& talk = tavern_->dialogue();
    talk.setPlayerCoin(tavern_->playerCoin());
    const sim::Speaker speaker = sim::wardSpeakerFor(
        *who, people_->identity(who->id), talk.notables(), talk.factions(), talk.barks());
    // SHOPS (2026-10-09). If they keep a counter, the shelf behind it -- open
    // only while they stand at their own post.
    talk.setCounter(tavern_->counterViewFor(speaker.notableId, wardAtCounter(*who)));
    if (!talk.open(speaker, tavern_->timeOfDay())) {
        talk.setCounter(sim::CounterView{});
        return false;
    }
    wardTalkId_ = who->id;
    // They look at you while you talk to them, exactly as the taproom's do.
    people_->faceToward(wardTalkId_, body_->tileX(), body_->tileY());
    return true;
}

void Session::moveTopicCursor(int delta) {
    // AUDIO WIRING: one quiet tick per cursor move, on every list this router
    // serves -- the restrained half of the plan's "focus move -> UiTick".
    if (audio_ != nullptr && delta != 0 && (talking() || casebookOpen_ || keysOpen_)) {
        audio_->playOneShot(audio::SoundId::UiTick);
    }
    if (keysOpen_) {
        wrapCursorAndPage(caseCursor_, casePage_, delta, static_cast<int>(keyRows().size()));
        return;
    }
    // MORROWIND ROUND: ROUTED BY WHICH TILE HAS FOCUS, not by which of four
    // now-simultaneous bools happens to be true -- see menuFocus()'s own
    // header. Each tile keeps its OWN cursor/page (and, for Journal and
    // Letters, its own picked entry), so reading one tile never disturbs
    // where the cursor was left on another.
    if (casebookOpen_) {
        switch (menuFocus_) {
            case kMenuFocusCharacter:
                // THE SAME KEYS, THE SAME PAGING -- see toggleCharacter.
                wrapCursorAndPage(characterCursor_, characterPage_, delta,
                                  static_cast<int>(characterRows().size()));
                // THE PACK'S HEADERS ARE NOT ROWS TO STAND ON: a step that
                // lands on one goes on in the same direction.
                settlePackCursor(delta);
                return;
            case kMenuFocusMap:
                // THE SAME KEYS, THE SAME PAGING -- see toggleMap.
                wrapCursorAndPage(mapCursor_, mapPage_, delta,
                                  static_cast<int>(mapRows().size()));
                return;
            case kMenuFocusLetters:
                if (lettersEntry_ >= 0) {
                    // AN OPEN LETTER HAS NO CURSOR TO MOVE -- lettersBodyPage_
                    // is doing a different job here (which page of the BODY is
                    // showing; see nextTopicPage's own kMenuFocusLetters
                    // branch).
                    return;
                }
                // TASK #82. THE SAME KEYS, THE SAME PAGING -- see toggleLetters.
                wrapCursorAndPage(lettersCursor_, lettersPage_, delta,
                                  static_cast<int>(unlockedLetters().size()));
                return;
            case kMenuFocusJournal:
            default:
                // THE SAME KEYS, THE SAME PAGING. The Journal tile is a
                // conversation with your own notes -- see
                // Session::toggleCasebook on why it borrows the dialogue
                // surface's own vocabulary rather than being a sheet of its
                // own. The count is leads PLUS the work rows under them
                // (journalWorkRows), so the cursor can reach a contract or a
                // log line to read it -- picking one stays a no-op.
                wrapCursorAndPage(caseCursor_, casePage_, delta,
                                  static_cast<int>(activeCasebook().known().size() +
                                                   journalWorkRows().size()));
                return;
        }
    }
    if (!talking()) {
        return;
    }
    // Wraps, so holding one direction walks the whole list. The page FOLLOWS
    // the cursor: walking off the bottom of a page turns it, so the arrow
    // keys reach every topic and the numbers on screen are always the numbers
    // that pick the ones you can see.
    wrapCursorAndPage(topicCursor_, topicPage_, delta,
                       static_cast<int>(tavern_->dialogue().topics().size()));
}

int Session::menuCursor() const noexcept {
    // KIT POLISH. THE SAME ROUTE moveTopicCursor() TAKES, read instead of
    // moved -- see the header. Kept beside it on purpose: a list this
    // router learns to move is a list this has to learn to read.
    if (keysOpen_) {
        return caseCursor_;
    }
    if (casebookOpen_) {
        switch (menuFocus_) {
            case kMenuFocusCharacter:
                return characterCursor_;
            case kMenuFocusMap:
                return mapCursor_;
            case kMenuFocusLetters:
                return lettersCursor_;
            case kMenuFocusJournal:
            default:
                return caseCursor_;
        }
    }
    return topicCursor_;
}

void Session::nextTopicPage() {
    if (optionsOpen_) {
        const int pages = topicPageCount(optionRows().size());
        optionPage_ = (optionPage_ + 1) % pages;
        optionCursor_ = std::min(static_cast<int>(optionRows().size()) - 1,
                                 optionPage_ * kTopicPageSize);
        return;
    }
    if (keysOpen_) {
        const std::size_t rows = keyRows().size();
        const int pages = topicPageCount(rows);
        casePage_ = (casePage_ + 1) % pages;
        caseCursor_ = std::min(static_cast<int>(rows) - 1, casePage_ * kTopicPageSize);
        return;
    }
    // MORROWIND ROUND: ROUTED BY FOCUS -- see moveTopicCursor()'s own note.
    if (casebookOpen_) {
        // AUDIO WIRING: turning a page WITHIN a tile is the same paper the
        // tile cycle is -- one BookFlip, whichever tile pages.
        if (audio_ != nullptr) {
            audio_->playOneShot(audio::SoundId::BookFlip);
        }
        switch (menuFocus_) {
            case kMenuFocusCharacter:
                if (characterPack_) {
                    // PHASE 9: the pack pages over the rows a digit reaches
                    // (packPageNext), not over raw rows with headers in them.
                    packPageNext();
                    return;
                }
                advancePage(characterPage_, characterCursor_, characterRows().size());
                settlePackCursor(1);
                return;
            case kMenuFocusMap:
                advancePage(mapPage_, mapCursor_, mapRows().size());
                return;
            case kMenuFocusLetters:
                if (lettersEntry_ >= 0) {
                    // PAGING THROUGH THE OPEN LETTER'S OWN BODY, not the
                    // title list -- lettersBodyPage_ is a field of its own
                    // (menu_view.cpp works out the true page count off the
                    // actual wrapped rows at the panel's real width, and
                    // CLAMPS it into range every frame, so incrementing past
                    // the end here is never observable).
                    ++lettersBodyPage_;
                    return;
                }
                advancePage(lettersPage_, lettersCursor_, unlockedLetters().size());
                return;
            case kMenuFocusJournal:
            default:
                advancePage(casePage_, caseCursor_,
                            activeCasebook().known().size() + journalWorkRows().size());
                return;
        }
    }
    if (!talking()) {
        return;
    }
    // The cursor comes with the page, onto its first topic, so E never picks
    // something that is not on screen.
    advancePage(topicPage_, topicCursor_, tavern_->dialogue().topics().size());
}

void Session::chooseVisibleTopic(int slot) {
    recordWatchOp(WatchOpKind::Topic, slot);
    const WatchDepthGuard watchGuard(watchDepth_);
    if (slot < 0 || slot >= kTopicPageSize) {
        return;
    }
    if (pauseOpen_) {
        // A MENU ROW IS AN ACTION, NOT A VALUE. Options' own numbered rows only
        // move the cursor -- ENTER is what rebinds or nudges a slider, and a
        // stray tenth-of-a-second double press should not fire a rebind. This
        // page has no sliders, only three rows the numbers already printed
        // beside on screen (topicRowsFor prints them for every list this
        // surface draws), so the honest behaviour for "press the number you
        // read" is the one talking() gets below: the press both moves the
        // cursor and acts, immediately.
        if (slot < static_cast<int>(pauseRows().size())) {
            pauseCursor_ = slot;
            choosePause();
        }
        return;
    }
    if (optionsOpen_) {
        pickCursorIfVisible(optionCursor_, optionPage_, slot, optionRows().size());
        return;
    }
    if (keysOpen_) {
        // A key row is a reference, not a choice. The cursor moves and nothing
        // else happens, which is the honest behaviour for a list you read.
        pickCursorIfVisible(caseCursor_, casePage_, slot, keyRows().size());
        return;
    }
    // MORROWIND ROUND: ROUTED BY FOCUS -- see moveTopicCursor()'s own note.
    if (casebookOpen_) {
        switch (menuFocus_) {
            case kMenuFocusCharacter:
                // A ROW ON THIS TILE IS SOMETHING TO READ, not a choice --
                // the same honest no-op the keys page gives a number press.
                if (characterPack_) {
                    packPickVisible(slot);
                    return;
                }
                pickCursorIfVisible(characterCursor_, characterPage_, slot,
                                    characterRows().size());
                return;
            case kMenuFocusMap:
                // A ROW ON THIS TILE IS SOMETHING TO READ, not a choice -- the
                // identical no-op the character tile and the keys page give a
                // number press.
                pickCursorIfVisible(mapCursor_, mapPage_, slot, mapRows().size());
                return;
            case kMenuFocusLetters: {
                if (lettersEntry_ >= 0) {
                    // THE TITLE LIST IS NOT ON SCREEN WHILE A LETTER IS OPEN
                    // -- see DialogueViewState::letter's own note -- so a
                    // number press here has nothing on this tile to have
                    // picked. ESC (closeConversation) is what steps back to
                    // the list.
                    return;
                }
                // TASK #82. A ROW HERE IS A CHOICE, THE SAME WAY A LEAD IS ON
                // THE JOURNAL TILE -- picking a title opens the letter under
                // it. Still pure UI: nothing in the simulation moves when a
                // letter is opened, exactly as reading a casebook entry does
                // not.
                const int index = lettersPage_ * kTopicPageSize + slot;
                if (index >= static_cast<int>(unlockedLetters().size())) {
                    return;
                }
                lettersCursor_ = index;
                lettersEntry_ = index;
                lettersBodyPage_ = 0;
                return;
            }
            case kMenuFocusJournal:
            default: {
                const int index = casePage_ * kTopicPageSize + slot;
                const int leads = static_cast<int>(activeCasebook().known().size());
                if (index >= leads + static_cast<int>(journalWorkRows().size())) {
                    return;
                }
                caseCursor_ = index;
                if (index < leads) {
                    // Only a LEAD opens as an entry; a work row under the
                    // trail is something to read, so the number press moves
                    // the cursor onto it and nothing else -- the identical
                    // no-op the character tile gives.
                    caseEntry_ = index;
                }
                return;
            }
        }
    }
    if (!talking()) {
        return;
    }
    const int index = topicPage_ * kTopicPageSize + slot;
    if (index >= static_cast<int>(tavern_->dialogue().topics().size())) {
        return;
    }
    topicCursor_ = index;
    chooseTopic(static_cast<std::size_t>(index));
}

void Session::chooseTopic(std::size_t index) {
    // MORROWIND ROUND: ROUTED BY FOCUS -- see moveTopicCursor()'s own note.
    if (casebookOpen_ && menuFocus_ == kMenuFocusJournal) {
        // Reading an entry of your own notes. Nothing in the simulation moves;
        // this is the one place in the game where picking a row is pure UI, and
        // it is pure UI because the trail's state changed when you LOOKED, not
        // when you read your own handwriting back.
        if (index < activeCasebook().known().size()) {
            caseCursor_ = static_cast<int>(index);
            caseEntry_ = static_cast<int>(index);
            // AUDIO WIRING: opening an entry of your own notes is a page, not
            // a menu -- BookFlip, not UiConfirm.
            if (audio_ != nullptr) {
                audio_->playOneShot(audio::SoundId::BookFlip);
            }
        } else if (index < activeCasebook().known().size() + journalWorkRows().size()) {
            // A work row under the trail -- a live contract, a finished
            // stage's log line -- is something to read, not a choice: the
            // cursor moves onto it, no entry opens and no page speaks. The
            // identical no-op the character tile gives a pick.
            caseCursor_ = static_cast<int>(index);
        }
        return;
    }
    if (casebookOpen_ && menuFocus_ == kMenuFocusCharacter) {
        // KIT BUILD. A CARRIED ROW IS A CHOICE NOW: ENTER wears it (or bares
        // it) through the room's own door. A sheet row stays something to
        // read -- the no-op the keys page gives -- so the index passed in
        // is ignored for the tile's OWN cursor, which is the row on screen.
        (void)index;
        wearHighlightedKitRow();
        return;
    }
    if (casebookOpen_ && menuFocus_ == kMenuFocusLetters) {
        // TASK #82. Opening a letter is exactly as pure-UI as opening a
        // casebook entry -- see that branch's own note. A no-op while a
        // letter is already open, for the identical reason
        // chooseVisibleTopic's own kMenuFocusLetters branch gives.
        if (lettersEntry_ < 0 && index < unlockedLetters().size()) {
            lettersCursor_ = static_cast<int>(index);
            lettersEntry_ = static_cast<int>(index);
            lettersBodyPage_ = 0;
            // AUDIO WIRING: unfolding a letter is paper too.
            if (audio_ != nullptr) {
                audio_->playOneShot(audio::SoundId::BookFlip);
            }
        }
        return;
    }
    if (!talking()) {
        return;
    }
    // AUDIO WIRING: the accept, spoken once per picked topic -- the plan's
    // "accept -> UiConfirm". Before the settlement on purpose: the press is
    // what is being acknowledged, and any coin the settlement moves speaks
    // for itself a step later (see the purse watch in step()).
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::UiConfirm);
    }
    // #79. WHICH ROOM OWNS THE CONSEQUENCE. The director decides what was SAID
    // and what the world now owes; whoever owns the body applies it. Inside the
    // Gull that is the Tavern, which has a bar to take a drink off and a
    // bouncer to send over. On the street it is the ward, which has neither --
    // so the ward has its own, much shorter, settlement.
    sim::Reply reply =
        wardTalkId_ >= 0 ? chooseWardTopic(index) : tavern_->chooseTopic(index);
    // TIME-AND-TENURE BUILD. The two ground topics are intents, exactly like
    // Buy: the director declared them and whoever owns the roll -- this
    // session, which borrowed the ward -- resolves them and fills in the
    // line. Settled HERE, after either settlement path above, because a
    // priest can be a street body (the ward path) or the Gull's own (the
    // tavern path) and the roll answers the same on both.
    if (reply.ok && reply.kind == sim::TopicKind::ReadRoll) {
        reply.line = groundRollLine();
        // Voiced into the panel too -- during a conversation the alert row
        // stands down (see drawFrame's hud.alert gate), so lastLine() is the
        // one surface the answer can actually be read on.
        tavern_->dialogue().speakResolved(reply.line);
    } else if (reply.ok && reply.kind == sim::TopicKind::Petition) {
        reply.line = settleGroundPetition();
        tavern_->dialogue().speakResolved(reply.line);
    } else if (reply.ok && reply.kind == sim::TopicKind::TakeWrit) {
        // EVICTION CASE. The director answered the speech (and gated the
        // open-hand bar); the CASE half is the session's, the ReadRoll split
        // one book on. The priest's own line is already in reply.line -- the
        // book opens under it.
        settleTakeWrit();
    } else if (reply.ok && reply.kind == sim::TopicKind::YieldWrit) {
        settleYieldWrit();
    }
    if (!reply.ok && reply.line.empty()) {
        return;
    }
    if (reply.haggling) {
        // Open the argument at what they are asking, so the first press of a
        // key is a concession rather than a guess.
        haggleOffer_ = tavern_->dialogue().haggle().asking();
    }
    if (!reply.line.empty()) {
        if (reply.kind == sim::TopicKind::Leave) {
            // VOICE LANE: THE GOODBYE rides the street channel as the panel
            // closes, in the speaker's name, never the amber alert row (a
            // goodbye is not a warning). The director has closed, but its
            // speaker still says who it was.
            voice_.speakFarewell(tavern_->dialogue().speaker().name, reply.line,
                                 tavern_->dialogue().farewellKey(false),
                                 tavern_->dialogue().speaker().actorId,
                                 static_cast<std::int32_t>(body_->stepCount()));
            syncVoiceAnim();
            // And the goodbye is the LAST thing they said -- endTalk's own
            // rule, for the panel LEAVE closed from inside: their greeting,
            // reply or the report of the drink you stood them, still on the
            // alert row's clock, stands down rather than resurfacing under
            // the goodbye. Whose the row is was recorded when it was said.
            if (!messageMouth_.empty() && messageMouth_ == tavern_->dialogue().speaker().name) {
                messageSteps_ = 0;
            }
        } else {
            sayAs(tavern_->dialogue().speaker().name, reply.line, reply.report);
        }
    }
    // VOICE LANE, THE ADDRESS: A WORDED REACTION when the reply moved them.
    // Never a number (DECISIONS.md: standing is never SHOWN as one): a
    // stage direction on the panel's dateline row for four seconds, so the
    // face reacts to every act, which is the one durable lesson of the
    // persuasion wheel and the whole of what the tone dial gets back.
    if (reply.ok && talking() && reply.dispositionAfter != reply.dispositionBefore) {
        reactionLine_ = wordedReaction(reply.dispositionBefore, reply.dispositionAfter);
        reactionSteps_ = reactionLine_.empty() ? 0 : kReactionHoldSteps;
    }
    if (reply.forging) {
        // The bench opens where the simulation put it.
        forgeOpen_ = true;
    }
    if (talking()) {
        const int count = static_cast<int>(tavern_->dialogue().topics().size());
        if (count > 0 && topicCursor_ >= count) {
            topicCursor_ = count - 1;
        }
        topicPage_ = std::min(topicPageOf(topicCursor_), topicPageCount(
                                                             static_cast<std::size_t>(count)) -
                                                             1);
    } else {
        topicCursor_ = 0;
        topicPage_ = 0;
    }
    if (!reply.journalLine.empty()) {
        say(reply.journalLine);
    }
    // TASK #83. Covers the one path through this function that can close the
    // conversation (reply.closes, when it lands on an empty reply.line) with
    // no call to say() to have carried the sync eagerly -- see say()'s own
    // note. Every other exit already got one from a say() above; this is a
    // no-op for those.
    syncPanelAnim();
}

void Session::closeConversation() {
    if (pauseOpen_) {
        // ESC out of QUIT's second press first, and out of the menu second --
        // the same shape as awaitingKey_ below, for the same reason: a player
        // who leant on ENTER by accident has to be able to back off it without
        // the whole page vanishing under them.
        if (quitArmed_) {
            quitArmed_ = false;
            return;
        }
        pauseOpen_ = false;
        pauseCursor_ = 0;
        syncPanelAnim();
        return;
    }
    // UI-EA-SPEC sec. 4 violation #4: BACK RETURNS TO THE OPENER. A page
    // entered through the pause menu -- CONTROLS, SETTINGS, WAIT -- backs
    // out ONE layer, to the pause menu with its cursor on the row that
    // opened the page, instead of skipping past it to the street. A page
    // entered by its direct shortcut (F1/F2, a bed's wait) still lands on
    // the street exactly as before: pageOpenedFromPause_ was derived false
    // at its open. The panel never closes across the return -- conversingNow
    // stays true -- so the swap reads as content changing inside one frame,
    // transition rule 2, and no close/open sound pair fires.
    const auto returnToPause = [this](int row) {
        pageOpenedFromPause_ = false;
        pauseOpen_ = true;
        // The row indices are pauseRows()' own order: RESUME, WAIT,
        // CONTROLS, SETTINGS, QUIT.
        pauseCursor_ = row;
        quitArmed_ = false;
        syncPanelAnim();
    };
    if (optionsOpen_) {
        // ESC out of a rebinding first, and out of the page second. A player
        // who opened "press a key" by accident has to be able to get out of it
        // without binding escape to something.
        if (awaitingKey_) {
            awaitingKey_ = false;
            return;
        }
        const bool toPause = pageOpenedFromPause_;
        optionsOpen_ = false;
        optionCursor_ = 0;
        optionPage_ = 0;
        if (toPause) {
            returnToPause(3);  // SETTINGS
            return;
        }
        syncPanelAnim();
        return;
    }
    if (keysOpen_) {
        const bool toPause = pageOpenedFromPause_;
        keysOpen_ = false;
        caseCursor_ = 0;
        casePage_ = 0;
        if (toPause) {
            returnToPause(2);  // CONTROLS
            return;
        }
        syncPanelAnim();
        return;
    }
    if (grimoireOpen_) {
        grimoireOpen_ = false;
        grimoireCursor_ = 0;
        grimoirePage_ = 0;
        syncPanelAnim();
        return;
    }
    if (waitOpen_) {
        const bool toPause = pageOpenedFromPause_;
        waitOpen_ = false;
        waitCursor_ = 0;
        waitPage_ = 0;
        if (toPause) {
            returnToPause(1);  // WAIT
            return;
        }
        syncPanelAnim();
        return;
    }
    if (districtMapOpen_) {
        // ESC closes the ward map the same way it closes every page -- the
        // owner's ask names M as the toggle, and Escape is the universal
        // back-out on top of it.
        districtMapOpen_ = false;
        syncPanelAnim();
        return;
    }
    if (casebookOpen_) {
        // MORROWIND ROUND. If the focused Letters tile has a letter open,
        // ESC steps back to its own title list first -- the SAME "a page
        // reached by drilling in gives you one press back to where you
        // were" rule the options page's awaitingKey_ handling above already
        // has, now scoped to one tile instead of the whole surface.
        if (menuFocus_ == kMenuFocusLetters && lettersEntry_ >= 0) {
            lettersEntry_ = -1;
            lettersBodyPage_ = 0;
            syncPanelAnim();
            return;
        }
        // OTHERWISE ESC CLOSES THE WHOLE TILED MENU, all four tiles at once
        // -- they were never four separate pages to step back out of one at
        // a time, only one surface with four panes on it (see
        // toggleCasebook()'s own header).
        casebookOpen_ = false;
        caseCursor_ = 0;
        casePage_ = 0;
        caseEntry_ = -1;
        characterCursor_ = 0;
        characterPage_ = 0;
        characterPack_ = false;
        mapCursor_ = 0;
        mapPage_ = 0;
        lettersCursor_ = 0;
        lettersPage_ = 0;
        lettersEntry_ = -1;
        lettersBodyPage_ = 0;
        menuFocus_ = kMenuFocusJournal;
        syncPanelAnim();
        return;
    }
    // VOICE LANE: the back key says goodbye too. Leaving is a transition
    // and not a cut, whichever key ends it.
    endTalk(false);
}

sim::Reply Session::chooseWardTopic(std::size_t index) {
    sim::DialogueDirector& talk = tavern_->dialogue();
    talk.setPlayerCoin(tavern_->playerCoin());
    sim::Reply reply = talk.choose(index);
    if (!reply.ok) {
        if (reply.closes || !talk.isOpen()) {
            wardTalkId_ = -1;
        }
        return reply;
    }
    if (reply.kind == sim::TopicKind::BuyItem || reply.kind == sim::TopicKind::SellItem) {
        settleCounterReply(reply);
        return reply;
    }

    // A hand in a purse takes coin off a REAL body, and the coin the ward loses
    // is the coin the player gains. Nothing is minted: the ledger's identity is
    // about creation, and a robbery creates nothing.
    if (reply.kind == sim::TopicKind::PickPocket && !reply.offence && reply.coinDelta > 0) {
        reply.coinDelta = people_->takeCoinFrom(wardTalkId_, reply.coinDelta);
        if (reply.coinDelta <= 0) {
            reply.line = "THEIR PURSE IS EMPTY.";
        }
    }
    if (reply.coinDelta != 0) {
        tavern_->setPlayerCoin(std::max(0, tavern_->playerCoin() + reply.coinDelta));
        talk.setPlayerCoin(tavern_->playerCoin());
    }
    // WHETHER ANYBODY SAW IT is a question about the street, and the street has
    // no walls, no bouncer and no door policy. So it is asked the only way it
    // honestly can be out here: were there other living people standing close
    // enough on the same band. The Watch's heat, the roofs' opinion and the
    // guilds' all move through the one call site every criminal act in this
    // build goes through, whether it happened in a taproom or on the Tarwalk.
    if (reply.criminal) {
        const bool watched = people_->witnessesAround(wardTalkId_, kWardWitnessTiles) > 0;
        talk.noteCrime(reply.crime, watched || reply.offence);
    }
    if (reply.closes || !talk.isOpen()) {
        wardTalkId_ = -1;
    }
    return reply;
}

// ---------------------------------------------------------------------------
// the workbench
// ---------------------------------------------------------------------------

bool Session::forging() const noexcept {
    return tavern_->dialogue().isForging();
}

void Session::moveForgeField(int delta) {
    tavern_->dialogue().moveForgeField(delta);
}

void Session::adjustForge(int delta) {
    tavern_->dialogue().adjustForge(delta);
}

void Session::commitForge() {
    if (!forging()) {
        return;
    }
    const std::string who = tavern_->dialogue().speaker().name;
    const sim::Reply reply = tavern_->commitForge();
    if (!reply.line.empty()) {
        sayAs(who, reply.line, reply.report);
    }
    if (!reply.journalLine.empty()) {
        say(reply.journalLine);
    }
    forgeOpen_ = forging();
}

void Session::endForge() {
    if (!forging()) {
        return;
    }
    const sim::Reply reply = tavern_->endForge();
    if (!reply.line.empty()) {
        say(reply.line);
    }
    forgeOpen_ = false;
}

void Session::adjustOffer(int delta) {
    if (!haggling()) {
        return;
    }
    const int ceiling = std::max(1, tavern_->dialogue().haggle().asking());
    haggleOffer_ = std::clamp(haggleOffer_ + delta, 0, ceiling);
}

void Session::makeOffer() {
    if (!haggling()) {
        return;
    }
    const std::string who = tavern_->dialogue().speaker().name;
    const sim::Reply reply = tavern_->offerPrice(haggleOffer_);
    if (!reply.line.empty()) {
        sayAs(who, reply.line, reply.report);
    }
    haggleOffer_ = haggling() ? std::min(haggleOffer_, tavern_->dialogue().haggle().asking()) : 0;
}

void Session::takeAskingPrice() {
    if (!haggling()) {
        return;
    }
    const std::string who = tavern_->dialogue().speaker().name;
    const sim::Reply reply = tavern_->takeAskingPrice();
    if (!reply.line.empty()) {
        sayAs(who, reply.line, reply.report);
    }
    haggleOffer_ = 0;
}

DialogueViewState Session::dialogueView() const {
    DialogueViewState view;
    // SHIP NOTE MOVE 3. Assembled fresh every frame, so every wording below
    // switches live the moment the other hand speaks. `confirm`/`back` are
    // the page grammar main.cpp's router hard-codes (ENTER/A, ESC/B); the
    // keyboard strings are character-for-character what this function always
    // printed, so a keyboard session draws byte-identical frames.
    const InputDevice dev = promptDevice_;
    const std::string confirm(promptConfirmKey(dev));
    const std::string back(promptBackKey(dev));
    view.confirmKey = confirm;
    view.backKey = back;
    // NINE AND THE STICKS: the digit column is a keyboard's. A pad walks a
    // list on the D-pad and confirms on A; a printed `6` beside a topic is a
    // key it does not have.
    view.showDigits = dev != InputDevice::Pad;
    // The haggle's third verb rides Action::PageNext in the router ("]"
    // carries no glyph in the 4x6 font; T is the advertised key and now
    // routed too -- see main.cpp's haggle branch); the pad's half is RB.
    if (dev == InputDevice::Pad) {
        view.takeKey = std::string(promptAltCommitKey(dev));
        // The pad has no L; its B BACK already names the way out of a letter.
        view.letterDownLine.clear();
    }
    // The panel's own small motion -- the picked row's highlight breathes
    // with it. The identical role body_->stepCount()/60 already plays for the
    // lamp flicker in drawFrame: a pure function of simulated steps, so a
    // scripted capture still draws the same frame every time it is asked to,
    // and it costs nothing on every page this widget is reused for.
    view.phase = static_cast<float>(body_->stepCount()) / 60.0F;
    if (waitOpen_) {
        // TIME-AND-TENURE BUILD. The same one list widget every page is. The
        // top band carries the ruling's own distinction -- and, in wait mode,
        // the refusal when this spot is not safe, so the page and the pick
        // name the same door before a key is ever pressed.
        view.open = true;
        view.speaker = waitSleep_ ? "SLEEP" : "WAIT";
        view.epithet = confirm + " PASSES THE HOURS  " + back + " BACKS OUT";
        if (waitSleep_) {
            view.line = "THE BED IS PAID FOR. SLEEP MENDS. PICK THE HOUR TO WAKE.";
        } else {
            const std::string refusal = waitRefusal();
            view.line = refusal.empty()
                            ? "TIME PASSES AND NOTHING MENDS. THE DISTRICT KEEPS ITS "
                              "HOURS, AND THE WATCH KEEPS FORGETTING."
                            : refusal;
        }
        for (const std::string& row : waitRows()) {
            view.topics.push_back(row);
        }
        view.cursor = waitCursor_;
        view.page = waitPage_;
        return view;
    }
    if (pauseOpen_) {
        view.open = true;
        view.speaker = "MENU";
        // The resume key is Pause's OWN binding -- ESC, or START, the key
        // that opened the page (this file's "the key that opened it closes
        // it" rule); the cancel on an armed QUIT is the universal back (B on
        // a pad -- see route_menu_key's parity-pass remap).
        view.epithet =
            quitArmed_ ? confirm + " QUITS  " + back + " CANCELS"
                       : confirm + " SELECTS  " +
                             std::string(promptLabel(controls_, Action::Pause, dev)) + " RESUMES";
        // NOT "PAUSED", DELIBERATELY. This page does not stop PhasedEngine --
        // nothing in this build does, not the casebook, not the keys page, not
        // options, and a menu that promised a freeze the game does not deliver
        // would be exactly the class of bug the copy bar exists to catch. Said
        // once, here, where a player opening this for the first time reads it.
        view.line = "THE DOCKS DO NOT WAIT ON YOU. SETTINGS KEEP THEMSELVES.";
        for (const std::string& row : pauseRows()) {
            view.topics.push_back(row);
        }
        view.cursor = pauseCursor_;
        view.page = 0;
        return view;
    }
    if (keysOpen_) {
        view.open = true;
        view.speaker = "CONTROLS";
        // AND THE BUILD, HERE, WHERE A PLAYER GOES LOOKING FOR IT. The version
        // used to be burnt into the top-left corner of every captured frame at
        // full HUD scale, which is prime screen real estate spent on something
        // nobody needs more than once. F1 is the page you open when you want to
        // know how this works; what build it is belongs on it.
        view.epithet = "GRANADAD: THE DARKSTREETS  " + std::string(sim::build_info().version);
        // THREE LINES IS WHAT THE TOP BAND WRAPS TO, so this is written to fit
        // in two. The first version ran to four and lost its own last sentence.
        // NINE AND THE STICKS: no F1 any more -- both hands came in through
        // the pause menu's CONTROLS row and back out on the universal back.
        view.line = "THE DOCKS OF GRANADAD. THE DISTRICT KEEPS ITS OWN HOURS. " + back +
                    " PUTS THIS DOWN.";
        for (const std::string& row : keyRows()) {
            view.topics.push_back(row);
        }
        view.cursor = caseCursor_;
        view.page = casePage_;
        return view;
    }
    if (searchOpen()) {
        // KIT BUILD. The corpse's kit on the same widget: his name on the
        // badge, the verbs on the epithet, TAKE ALL as the last row.
        view.open = true;
        const sim::Actor* corpse = tavern_->actorById(searchActorId_);
        view.speaker = corpse != nullptr ? upperAscii(corpse->name()) : std::string("THE DEAD");
        view.epithet = confirm + " TAKES  " + back + " LEAVES HIM";
        const std::vector<std::string> rows = grimoireRows();
        view.line = rows.empty() ? "NOTHING ON HIM." : "WHAT HE HAD ON HIM. DR IS DRAMS.";
        for (const std::string& row : rows) {
            view.topics.push_back(row);
        }
        view.cursor = grimoireCursor_;
        view.page = grimoirePage_;
        return view;
    }
    if (grimoireOpen_) {
        // SPELLS BUILD. The same one list widget every page is -- see
        // toggleGrimoire()'s own header. The rows carry the difficulty out of
        // the cost model beside every name: what the linkcraft check will be
        // rolled against, which is information, never a discount.
        view.open = true;
        view.speaker = "GRIMOIRE";
        // LEFT RIGHT is honest on both devices -- the D-pad IS the pad's
        // left and right on a list; only the commit verb changes hands.
        view.epithet = "LEFT RIGHT BIND A SLOT  " + confirm + " READIES";
        const std::vector<std::string> rows = grimoireRows();
        if (rows.empty()) {
            // THE COMMON STATE, in the cast refusal's own words: the page and
            // the C key must name the same door or one of them is lying.
            view.line = "NO CRAFTING HELD. THE PRIEST OF THE FLAME TEACHES.";
        } else {
            view.line =
                "EVERY CRAFTING THE HAND KNOWS. D IS WHAT THE LINK ASKS. A SLOT PUTS "
                "IT ON THE NUMBER ROW.";
        }
        for (const std::string& row : rows) {
            view.topics.push_back(row);
        }
        view.cursor = grimoireCursor_;
        view.page = grimoirePage_;
        return view;
    }
    if (casebookOpen_) {
        // MORROWIND ROUND: WHICHEVER TILE CURRENTLY HAS FOCUS. dialogueView()
        // used to check four mutually-exclusive xOpen_ bools in turn; now all
        // four tiles are open together (casebookOpen_ is the one flag), so
        // the dispatch is by menuFocus_ instead -- a caller that only ever
        // asked "what is the open page showing" (every test written before
        // this round) keeps seeing exactly the content it always did,
        // because the Menu still opens focused on the Journal tile by
        // default, same as #85's Menu always opened on the casebook first.
        DialogueViewState tile;
        switch (menuFocus_) {
            case kMenuFocusCharacter:
                tile = characterPanelView();
                break;
            case kMenuFocusMap:
                tile = mapPanelView();
                break;
            case kMenuFocusLetters:
                tile = lettersPanelView();
                break;
            case kMenuFocusJournal:
            default:
                tile = journalPanelView();
                break;
        }
        // SHIP NOTE MOVE 3: the tile views build their own state, so the
        // device wording set at the top of this function has to be restated
        // on the one that is actually returned -- an open letter's foot in
        // particular (backKey/letterDownLine).
        tile.confirmKey = confirm;
        tile.backKey = back;
        if (dev == InputDevice::Pad) {
            tile.takeKey = std::string(promptAltCommitKey(dev));
            tile.letterDownLine.clear();
        }
        return tile;
    }
    if (optionsOpen_) {
        view.open = true;
        view.speaker = "OPTIONS";
        // The awaiting line stays "ESC CANCELS" on BOTH devices, deliberately:
        // the capture is raw (main.cpp's bindAwaited eats the next key ahead
        // of every remap), so a pad's B would be CAPTURED as the binding, and
        // ESC really is the one cancel there is.
        view.epithet = awaitingKey_ ? (dev == InputDevice::Pad ? "PRESS A KEY  (B CANCELS)"
                                                                : "PRESS A KEY  (ESC CANCELS)")
                                    : "LEFT RIGHT CHANGE  " + confirm + " REBIND";
        view.line = "MOUSE LOOK IS RAW -- NO SMOOTHING, NO ACCELERATION. A KEY YOU BIND IS TAKEN "
                    "OFF WHATEVER HAD IT. " +
                    back + " PUTS THIS DOWN.";
        for (const std::string& row : optionRows()) {
            view.topics.push_back(row);
        }
        view.cursor = optionCursor_;
        view.page = optionPage_;
        return view;
    }
    const sim::DialogueDirector& talk = tavern_->dialogue();
    if (!talk.isOpen()) {
        return view;
    }
    view.open = true;
    view.speaker = talk.speaker().name;
    view.epithet = talk.speaker().epithet;
    view.attitude = std::string(sim::attitudeName(talk.attitude()));
    view.line = talk.lastLine();
    // THE TYPEWRITER. speechAgeSteps_ was reset to 0 the step this exact line
    // first became lastLine() -- see step()'s own note -- so this ramps 0..all
    // of it over kSpeechRevealSteps and then reports -1 (all of it, no budget
    // to track) for as long as the line stands, which is most of a
    // conversation's life. Nothing here changes what talk.lastLine() IS; only
    // how much of it the top band has been told to draw this frame.
    view.speechRevealChars =
        speechAgeSteps_ >= kSpeechRevealSteps
            ? -1
            : static_cast<int>((static_cast<std::int64_t>(view.line.size()) * speechAgeSteps_) /
                                kSpeechRevealSteps);
    view.cursor = topicCursor_;
    view.page = topicPage_;
    // VOICE LANE, THE ADDRESS: the worded reaction on the dateline row, in
    // the dim ink reference material takes, while it stands.
    if (reactionSteps_ > 0 && !reactionLine_.empty()) {
        view.caseRef = reactionLine_;
    }
    for (const sim::Topic& topic : talk.topics()) {
        view.topics.push_back(topic.label);
    }
    if (talk.isForging()) {
        const sim::ForgeBench& bench = talk.bench();
        view.forging = true;
        view.forgeCursor = bench.field;
        view.forgeDifficulty = bench.difficulty();
        view.forgeCeiling = sim::forgeCeilingFor(talk.skills().level(sim::kCraftingSkill));
        const sim::ForgeError problem = bench.error();
        if (problem != sim::ForgeError::None) {
            view.forgeProblem = std::string(sim::forgeErrorReason(problem));
        } else if (view.forgeDifficulty > view.forgeCeiling) {
            view.forgeProblem = std::string(sim::forgeErrorReason(sim::ForgeError::BeyondSkill));
        }
        for (int i = 0; i < sim::kForgeFieldCount; ++i) {
            view.forgeFields.push_back(bench.fieldLabel(i) + ": " + bench.fieldValue(i));
        }
    }
    if (talk.isHaggling()) {
        view.haggling = true;
        view.asking = talk.haggle().asking();
        view.offer = haggleOffer_;
        view.patience = talk.haggle().patience();
        view.goods = std::string(sim::goodsName(talk.haggle().terms().goods));
    }
    return view;
}

bool Session::conversingNow() const noexcept {
    // THE ONE FORMULA. Repeated in drawFrame() as `conversing` until this
    // pass, which is exactly the shape of drift that let the S7 review's
    // overprint findings happen: two places computing the same fact, and
    // nothing catching them when a seventh page joined the list and only one
    // of the two remembered to add it.
    // JUSTICE BUILD: the hearing page is a page like the others for the
    // panel ease and the HUD's stand-down -- and unlike them it is not in
    // dismissOverlays(), which is the whole of its exception.
    return talking() || casebookOpen_ || keysOpen_ || grimoireOpen_ || waitOpen_ ||
           districtMapOpen_ || optionsOpen_ || pauseOpen_ || courtOpen_;
}

void Session::syncVoiceAnim() noexcept {
    // Wanted while the channel holds a line and no panel owns the screen --
    // the partner's lines are the panel's, and the goodbye lands as it
    // closes. The text itself stays behind to fade (clearIfSettled).
    voiceAnim_.setTarget(voice_.up() && !conversingNow());
}

void Session::endTalk(bool walkedOff) {
    sim::DialogueDirector& talk = tavern_->dialogue();
    if (talk.isOpen()) {
        // THE GOODBYE, before the director forgets who it was talking to.
        voice_.speakFarewell(talk.speaker().name, talk.farewell(walkedOff),
                             talk.farewellKey(walkedOff), talk.speaker().actorId,
                             static_cast<std::int32_t>(body_->stepCount()));
        // And the goodbye is the LAST thing they said: a greeting or a reply
        // still on the alert row's clock would resurface under it the moment
        // the panel let the row back -- the same mouth saying two things at
        // once. Their own line stands down -- their voice or the director's
        // report of their deed, both recorded as theirs by sayAs, so "GERTA
        // SALTCOTTE TAKES THE DRINK." goes with her as surely as her
        // greeting does (the 2026-10-06 critics' drink-then-leave frames).
        // A bouncer's warning or anybody else's word is not theirs and keeps
        // its clock.
        if (!messageMouth_.empty() && messageMouth_ == talk.speaker().name) {
            messageSteps_ = 0;
        }
    }
    tavern_->endConversation();
    wardTalkId_ = -1;
    topicCursor_ = 0;
    topicPage_ = 0;
    haggleOffer_ = 0;
    forgeOpen_ = false;
    reactionLine_.clear();
    reactionSteps_ = 0;
    syncPanelAnim();
}

}  // namespace granadad::render
