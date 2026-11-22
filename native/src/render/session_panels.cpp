// Session panels: the character, map, letters and journal views, the kit rows, the grimoire and the letters list.
#include "session_internal.hpp"

// THE AUDIO WIRING PASS, as in session.cpp: this file speaks to the audio engine in ids only; see
// setAudio() in session.hpp and the determinism note in audio_engine.hpp.
#include "granadad/audio/audio_engine.hpp"

namespace granadad::render {

// ---------------------------------------------------------------------------
// SPELLS BUILD: the Grimoire page, and the quick bar it loads
// ---------------------------------------------------------------------------

void Session::toggleGrimoire() {
    if (refusedInCustody()) {
        return;
    }
    if (talking() || picking()) {
        return;
    }
    const bool willOpen = !grimoireOpen_;
    grimoireOpen_ = willOpen;
    // KIT BUILD: the widget opened by the wheel's tap is the Grimoire, never
    // a search list left behind by a body walked away from.
    searchActorId_ = -1;
    if (willOpen) {
        // See toggleOptions' comment: an overlay that opens without putting the
        // others down is how a page draws on screen while a different one is
        // still the one reading the keyboard.
        casebookOpen_ = false;
        keysOpen_ = false;
        waitOpen_ = false;
        districtMapOpen_ = false;
        optionsOpen_ = false;
        pauseOpen_ = false;
        quitArmed_ = false;
        awaitingKey_ = false;
        menuFocus_ = kMenuFocusJournal;
    }
    firstRun_ = false;
    grimoireCursor_ = 0;
    grimoirePage_ = 0;
    syncPanelAnim();
}

std::vector<std::string> Session::grimoireRows() const {
    if (searchOpen()) {
        // KIT BUILD: the corpse's kit, less what was taken -- name, weight,
        // worth -- and TAKE ALL as the last row, a row being a choice.
        std::vector<std::string> rows;
        const std::vector<sim::Tavern::CorpseRow> carried = tavern_->corpseRows(searchActorId_);
        for (const sim::Tavern::CorpseRow& row : carried) {
            const sim::ItemDef* thing = tavern_->items().at(row.item);
            if (thing == nullptr) {
                continue;
            }
            std::string line = thing->name + "  " + std::to_string(thing->drams) + "D";
            if (thing->royals > 0) {
                line += "  " + std::to_string(thing->royals) + "C";
            }
            rows.push_back(std::move(line));
        }
        if (!carried.empty()) {
            rows.push_back("TAKE ALL");
        }
        return rows;
    }
    // ONE ROW PER KNOWN CRAFTING, grimoire order -- the same order
    // equipSpellAt counts in, so the number printed beside a row IS the index
    // the equip spends. Name, the cost model's own difficulty (the number the
    // linkcraft check is rolled against -- information, never a buff), FORGED
    // on a composition of your own (forgedSpellId's "forged." prefix is the
    // honest tag; craftedCount() only counts), the slot it is bound to, and
    // READY on the one the hand is holding.
    std::vector<std::string> rows;
    const sim::Grimoire& book = tavern_->dialogue().grimoire();
    const sim::Spell* held = tavern_->equippedSpell();
    rows.reserve(book.spells().size());
    for (const sim::Spell& spell : book.spells()) {
        std::string row = upperAscii(spell.displayName);
        row += "  D" + std::to_string(sim::spellDifficulty(spell));
        if (spell.id.rfind("forged.", 0) == 0) {
            row += "  FORGED";
        }
        for (std::int32_t slot = 0; slot < sim::Tavern::kQuickSlotCount; ++slot) {
            const sim::Spell* bound = tavern_->slotSpell(slot);
            if (bound != nullptr && bound->id == spell.id) {
                row += "  SLOT " + std::to_string(slot + 1);
                break;
            }
        }
        if (held != nullptr && held->id == spell.id) {
            row += "  READY";
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

void Session::moveGrimoireCursor(int delta) {
    if (!grimoireOpen_) {
        return;
    }
    // AUDIO WIRING: the same one quiet tick every other list speaks.
    if (audio_ != nullptr && delta != 0) {
        audio_->playOneShot(audio::SoundId::UiTick);
    }
    wrapCursorAndPage(grimoireCursor_, grimoirePage_, delta,
                      static_cast<int>(searchOpen() ? grimoireRows().size()
                                                    : tavern_->dialogue().grimoire().spells().size()));
}

void Session::nextGrimoirePage() {
    if (!grimoireOpen_) {
        return;
    }
    advancePage(grimoirePage_, grimoireCursor_,
                searchOpen() ? grimoireRows().size()
                             : tavern_->dialogue().grimoire().spells().size());
}

void Session::chooseGrimoireRow(int slot) {
    if (!grimoireOpen_ || slot < 0 || slot >= kTopicPageSize) {
        return;
    }
    const int index = grimoirePage_ * kTopicPageSize + slot;
    if (searchOpen()) {
        // KIT BUILD: TAKE the row, or TAKE ALL on the last one. The list is
        // re-read after every take (a pure function of the room), so the
        // cursor lands on what is left; an emptied body closes the list.
        const std::vector<sim::Tavern::CorpseRow> carried = tavern_->corpseRows(searchActorId_);
        if (carried.empty()) {
            closeConversation();
            return;
        }
        std::string line;
        bool anyTaken = false;
        if (index >= static_cast<int>(carried.size())) {
            if (index != static_cast<int>(carried.size())) {
                return;
            }
            std::int32_t taken = 0;
            for (const sim::Tavern::CorpseRow& row : carried) {
                const sim::Tavern::StealResult took =
                    tavern_->takeFromCorpse(searchActorId_, row.kitRow);
                if (took.result == sim::ServiceResult::Served) {
                    ++taken;
                    anyTaken = true;
                } else {
                    line = took.line;
                    break;
                }
            }
            if (line.empty()) {
                line = "TAKEN - ALL OF IT. " + std::to_string(taken) +
                       (taken == 1 ? " THING." : " THINGS.");
            } else if (taken > 0) {
                line = std::to_string(taken) + " TAKEN, THEN " + line;
            }
        } else {
            const sim::Tavern::StealResult took = tavern_->takeFromCorpse(
                searchActorId_, carried[static_cast<std::size_t>(index)].kitRow);
            anyTaken = took.result == sim::ServiceResult::Served;
            line = took.line;
        }
        if (audio_ != nullptr) {
            audio_->playOneShot(anyTaken ? audio::SoundId::ClothRustle : audio::SoundId::UiTick);
        }
        say(line);
        const int left = static_cast<int>(grimoireRows().size());
        if (left == 0) {
            closeConversation();
            return;
        }
        grimoireCursor_ = std::min(grimoireCursor_, left - 1);
        grimoirePage_ = topicPageOf(grimoireCursor_);
        return;
    }
    const std::vector<sim::Spell>& spells = tavern_->dialogue().grimoire().spells();
    if (index >= static_cast<int>(spells.size())) {
        return;
    }
    grimoireCursor_ = index;
    // THE SAME LOCK THE QUICK BAR TURNS. equipSpellAt is the one door into
    // the hand; the CAST row, the strip's highlight and the next press of C
    // all read the one id it re-points.
    if (tavern_->equipSpellAt(index)) {
        // AUDIO WIRING: the same accept every list speaks.
        if (audio_ != nullptr) {
            audio_->playOneShot(audio::SoundId::UiConfirm);
        }
        say("READY: " + upperAscii(spells[static_cast<std::size_t>(index)].displayName) + ".");
    }
}

void Session::adjustGrimoireSlot(int delta) {
    if (!grimoireOpen_ || delta == 0 || searchOpen()) {
        return;
    }
    const std::vector<sim::Spell>& spells = tavern_->dialogue().grimoire().spells();
    if (grimoireCursor_ < 0 || grimoireCursor_ >= static_cast<int>(spells.size())) {
        return;
    }
    const sim::Spell& spell = spells[static_cast<std::size_t>(grimoireCursor_)];
    // Which slot holds this crafting now, or -1 -- the cycle walks
    // NONE, 1..10 and wraps, so LEFT from NONE is slot 10 and RIGHT off
    // slot 10 is NONE again. Moving it clears the slot it leaves.
    std::int32_t current = -1;
    for (std::int32_t slot = 0; slot < sim::Tavern::kQuickSlotCount; ++slot) {
        const sim::Spell* bound = tavern_->slotSpell(slot);
        if (bound != nullptr && bound->id == spell.id) {
            current = slot;
            break;
        }
    }
    const std::int32_t positions = sim::Tavern::kQuickSlotCount + 1;
    const std::int32_t next =
        ((current + 1 + delta) % positions + positions) % positions - 1;
    if (current >= 0) {
        tavern_->clearSlot(current);
    }
    if (next >= 0) {
        tavern_->bindSpellToSlot(next, spell.id);
    }
    // AUDIO WIRING: the same one quiet tick a slider nudge gets -- the
    // restraint note stands, no new sound design.
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::UiTick);
    }
    say(next >= 0 ? upperAscii(spell.displayName) + " -- SLOT " + std::to_string(next + 1) + "."
                  : upperAscii(spell.displayName) + " -- NO SLOT.");
}

std::vector<std::int32_t> Session::unlockedLetters() const {
    std::vector<std::int32_t> out;
    // A LETTER'S GATE IS THE SAME GATE ITS LEAD ALREADY HAS -- Cold or
    // Followed, never merely Open. An Open lead is a name in the book with
    // nobody standing over it yet; the sentence that says a letter exists at
    // all ("he wrote three unanswered letters...") is inside the lead's OWN
    // `detail` text, which the player has not read until the state has moved
    // past Open. Showing the letter earlier would hand over a document
    // before the game has told the player it exists.
    for (std::size_t i = 0; i < letterRaws_.letters().size(); ++i) {
        const sim::Letter& letter = letterRaws_.letters()[i];
        const std::int32_t lead = caseRaws_.indexOf(letter.lead);
        if (lead < 0) {
            continue;
        }
        const sim::LeadState state = casebook_.state(lead);
        if (state == sim::LeadState::Cold || state == sim::LeadState::Followed) {
            out.push_back(static_cast<std::int32_t>(i));
        }
    }
    // COURIER CASE. The sheet file rides the same shelf in COMBINED indices
    // (see letterAt), gated on ITS OWN book -- and a `handed` document is
    // post the player was SENT, readable the moment its lead is heard: the
    // courier put it in their hand, and a gate that made them walk to the
    // Gull before they could read the sheet that names the Gull would be
    // the tutorial eating itself.
    const std::int32_t base = static_cast<std::int32_t>(letterRaws_.letters().size());
    for (std::size_t i = 0; i < sheetLetterRaws_.letters().size(); ++i) {
        const sim::Letter& letter = sheetLetterRaws_.letters()[i];
        const std::int32_t lead = sheetRaws_.indexOf(letter.lead);
        if (lead < 0) {
            continue;
        }
        const sim::LeadState state = sheetBook_.state(lead);
        const bool readable =
            letter.handed
                ? state != sim::LeadState::Unheard
                : (state == sim::LeadState::Cold || state == sim::LeadState::Followed);
        if (readable) {
            out.push_back(base + static_cast<std::int32_t>(i));
        }
    }
    // EVICTION CASE. The third file rides the same shelf above the sheet's,
    // gated on ITS OWN book by the identical handed/read-not-received rule:
    // the writ is `handed` (readable the moment the hire lead is heard); the
    // widow's petition and the served notice keep the Cold-or-Followed gate,
    // so they surface only once the gate and door leads are actually walked.
    const std::int32_t evictBase =
        base + static_cast<std::int32_t>(sheetLetterRaws_.letters().size());
    for (std::size_t i = 0; i < evictLetterRaws_.letters().size(); ++i) {
        const sim::Letter& letter = evictLetterRaws_.letters()[i];
        const std::int32_t lead = evictRaws_.indexOf(letter.lead);
        if (lead < 0) {
            continue;
        }
        const sim::LeadState state = evictBook_.state(lead);
        const bool readable =
            letter.handed
                ? state != sim::LeadState::Unheard
                : (state == sim::LeadState::Cold || state == sim::LeadState::Followed);
        if (readable) {
            out.push_back(evictBase + static_cast<std::int32_t>(i));
        }
    }
    return out;
}

const sim::Letter& Session::letterAt(std::int32_t combined) const {
    // COURIER + EVICTION CASE. One shelf over THREE files, in fixed member
    // order: the Bloodletter's letters, then the sheet's, then the
    // eviction's above both. Callers only ever hand back indices
    // unlockedLetters() produced, so the subscripts below hold by
    // construction.
    const std::int32_t base = static_cast<std::int32_t>(letterRaws_.letters().size());
    const std::int32_t evictBase =
        base + static_cast<std::int32_t>(sheetLetterRaws_.letters().size());
    if (combined >= evictBase) {
        return evictLetterRaws_.letters()[static_cast<std::size_t>(combined - evictBase)];
    }
    if (combined >= base) {
        return sheetLetterRaws_.letters()[static_cast<std::size_t>(combined - base)];
    }
    return letterRaws_.letters()[static_cast<std::size_t>(combined)];
}

std::int32_t Session::letterCount() const noexcept {
    return static_cast<std::int32_t>(letterRaws_.letters().size() +
                                     sheetLetterRaws_.letters().size() +
                                     evictLetterRaws_.letters().size());
}

// ---------------------------------------------------------------------------
// Morrowind round: the tiled Menu's four panels, each built independently of
// which (if any) currently has focus -- drawMenuTiles() needs all four every
// frame, and dialogueView() returns whichever one menuFocus_ names so a
// caller that only ever asked about "the open page" keeps seeing exactly the
// content it always did. CONTENT UNCHANGED FROM #85's SIX-PAGE MENU; only
// the per-tile cursor/page fields (characterCursor_/mapCursor_/
// lettersCursor_ instead of a shared caseCursor_) are new -- see session.hpp's
// own note on why each tile needs its own now that all four can be mid-read
// at once.
// ---------------------------------------------------------------------------

DialogueViewState Session::characterPanelView() const {
    DialogueViewState view;
    view.phase = static_cast<float>(body_->stepCount()) / 60.0F;
    view.open = true;
    view.speaker = "CHARACTER";
    const std::string title = legendLine();
    view.epithet = title.empty() ? std::string(sim::kReputationUnremarkable) : title;
    // KIT BUILD: over a carried row the epithet is the verbs, in the
    // device's own words -- the Grimoire page's "LEFT RIGHT BIND A SLOT"
    // idiom -- so the tile never advertises a press it will not take. The
    // reputation line stands on every sheet row.
    if (const std::optional<KitRow> row = highlightedKitRow(); row.has_value() && row->inKit) {
        const sim::ItemDef* thing = tavern_->items().at(row->item);
        const InputDevice dev = promptDevice_;
        // The arrows as motifs on a keyboard (the tile is thirty-odd cells
        // wide at 960 and "LEFT RIGHT" pushed DROP off it), the bare cross on
        // a pad -- UI-EA-SPEC sec. 5's own substitutions.
        std::string verbs;
        if (thing != nullptr && thing->slot != sim::ItemSlot::None) {
            verbs = std::string(promptConfirmKey(dev)) +
                    (tavern_->kit().isWorn(row->item) ? " BARE  " : " WEAR  ") +
                    (dev == InputDevice::Pad ? std::string(1, kMotifDPad)
                                             : std::string{kMotifLeft, kMotifRight}) +
                    " SLOT  ";
        } else if (thing != nullptr && !thing->effects.empty()) {
            // A consumable: the one press is its own verb (EAT, DRINK, BIND).
            verbs = std::string(promptConfirmKey(dev)) + " " + thing->useVerb + "  ";
        }
        verbs += "X DROP";
        view.epithet = verbs;
    }
    if (characterPack_) {
        // THE PACK'S EPITHET IS THE LOAD, always -- the one budget the list
        // answers to, where the eye lands first -- and the legs under it the
        // moment they are slowed. The verbs live on the detail pane's foot.
        view.speaker = "PACK";
        view.epithet = "LOAD " + std::to_string(tavern_->loadDrams()) + "/" +
                       std::to_string(tavern_->loadBudget());
        if (const std::int32_t legs = tavern_->loadSpeedQ8(); legs < 256) {
            view.epithet += "  LEGS " + std::to_string(legs * 100 / 256) + "%";
        }
        view.attitude = std::string(promptTabKeys(promptDevice_)) + " SHEET";
    } else {
        view.attitude = std::string(promptTabKeys(promptDevice_)) + " PACK";
    }
    view.line = "WHAT THE STREETS HAVE MADE OF YOU, AND THE HANDS THAT DID IT.";
    for (const std::string& row : characterRows()) {
        view.topics.push_back(row);
    }
    view.cursor = characterCursor_;
    view.page = characterPage_;
    if (characterPack_) {
        const std::vector<PackRow> rows = packRows();
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].kind == PackRow::Kind::Header) {
                view.headerRows.push_back(static_cast<int>(i));
            }
        }
    }
    return view;
}

DialogueViewState Session::mapPanelView() const {
    // #82. THE DISTRICT MAP. Same reason every other tile is drawn through
    // this shared vocabulary: one content shape, proved once.
    DialogueViewState view;
    view.phase = static_cast<float>(body_->stepCount()) / 60.0F;
    view.open = true;
    view.speaker = "THE CHART";
    // WHERE YOU ARE, right where a map's own "you are here" would go.
    view.epithet = placeLabel();
    view.line = "KNOWN GROUND, OPEN LEADS AND WHO WILL TALK, RECKONED FROM WHERE YOU STAND.";
    // AND WHAT THE BLANK HALF OF THE PANEL IS WAITING FOR. All three sections
    // above are built out of casebook_.known() and nothing else -- see
    // mapRows' own header -- so the honest sentence is that the chart is a
    // reading of the book, and the book is what the player fills. True at
    // every point in a run, which is why it is not gated on the row count:
    // this panel is never finished until the case is.
    // Empty states run six words or fewer now (UI-EA-SPEC): the reading is
    // the book's, said in four.
    view.emptyLine = "WHAT THE CASEBOOK KNOWS.";
    for (const std::string& row : mapRows()) {
        view.topics.push_back(row);
    }
    view.cursor = mapCursor_;
    view.page = mapPage_;
    return view;
}

DialogueViewState Session::lettersPanelView() const {
    // TASK #82. A LETTER, DRAWN AS A DOCUMENT INSTEAD OF A LIST. Same content
    // shape every other tile uses; menu_view.cpp switches to the parchment
    // palette and pages through the body as wrapped prose instead of a row
    // list once DialogueViewState::letter is set -- see that field's own
    // header.
    DialogueViewState view;
    view.phase = static_cast<float>(body_->stepCount()) / 60.0F;
    view.open = true;
    const std::vector<std::int32_t> unlocked = unlockedLetters();
    // COURIER CASE. Whether anything on the shelf right now was put into the
    // player's own hand -- the epithet and the pick line below stop claiming
    // "read, not received" the moment that stops being the whole truth.
    bool anyHanded = false;
    for (const std::int32_t index : unlocked) {
        if (letterAt(index).handed) {
            anyHanded = true;
            break;
        }
    }
    if (lettersEntry_ >= 0 && static_cast<std::size_t>(lettersEntry_) < unlocked.size()) {
        const sim::Letter& read =
            letterAt(unlocked[static_cast<std::size_t>(lettersEntry_)]);
        view.speaker = read.from.empty() ? std::string("A LETTER") : read.from;
        view.line = "A LETTER, READ IN FULL BELOW.";
        view.letter = true;
        // ONE ENTRY A PARAGRAPH, RAW -- see DialogueViewState::letterLines'
        // own header on why WRAPPING happens at draw time rather than
        // here: Session has no window size to wrap prose against, and a
        // machine that pre-decided where a sentence breaks at every
        // resolution is the exact "picks badly" case casebook.hpp's own
        // `brief` field warns about for a proper noun -- prose gets to
        // wrap for real.
        if (!read.to.empty()) {
            view.letterLines.push_back("TO " + read.to);
        }
        if (!read.dateline.empty()) {
            view.letterLines.push_back(read.dateline);
        }
        if (!read.salutation.empty()) {
            view.letterLines.push_back(read.salutation);
        }
        for (const std::string& paragraph : read.body) {
            view.letterLines.push_back(paragraph);
        }
        if (!read.closing.empty()) {
            view.letterLines.push_back(read.closing);
        }
        if (!read.signature.empty()) {
            view.letterLines.push_back("- " + read.signature);
        }
        view.page = lettersBodyPage_;
    } else {
        view.speaker = "THE LETTERS";
        // COURIER CASE. "READ, NOT RECEIVED" survives only while it is true:
        // a handed mission sheet IS received post, so the epithet and the
        // pick line change with the shelf rather than lying about it.
        view.epithet = anyHanded ? "THE WARD'S PAPER" : "READ, NOT RECEIVED";
        view.line = unlocked.empty()
                        ? "YOU HAVE READ NOBODY'S POST YET."
                        : (anyHanded
                               ? "YOUR OWN PAPER, AND OTHER PEOPLE'S. PICK ONE AND READ IT WHOLE."
                               : "OTHER PEOPLE'S PAPER. PICK ONE AND READ IT WHOLE.");
        // THE ONE PANEL IN THE BUILD THAT SHIPS WITH NOTHING IN IT AT ALL, and
        // the tile draws no `line`, so until this the first thing a stranger
        // saw here was a title over a quarter-screen of black. The gate is
        // unlockedLetters()' own -- a letter turns up when its lead has been
        // stood over, never merely heard -- so the sentence names exactly the
        // act that fills the panel, and stops claiming nobody has written to
        // you the moment somebody has.
        //
        // "READ, NOT RECEIVED" is the epithet above it, and the wording keeps
        // faith with it: none of these five documents is addressed to the
        // player, so the copy talks about paper the leads keep, never about
        // post the player was sent.
        // Six words or fewer (UI-EA-SPEC empty-state law), the same paper-
        // not-post wording, shorter.
        view.emptyLine = unlocked.empty() ? "NO PAPER YET. STAND OVER LEADS."
                                          : "STAND OVER MORE LEADS.";
        view.page = lettersPage_;
    }
    for (const std::int32_t index : unlocked) {
        const sim::Letter& letter = letterAt(index);
        // MAELL'S THREE SHARE ONE NAME, so the title list numbers them
        // against every OTHER letter tied to the same lead rather than
        // showing "FATHER MAELL" three times over with no way to tell
        // which press opens which. Combined indices (letterAt) -- lead ids
        // are per-file strings, so the count can never mix the two books.
        std::int32_t total = 0;
        std::int32_t position = 0;
        for (std::int32_t i = 0; i < letterCount(); ++i) {
            if (letterAt(i).lead == letter.lead) {
                ++total;
                if (i == index) {
                    position = total;
                }
            }
        }
        std::string label = letter.from.empty() ? std::string("A LETTER") : letter.from;
        if (total > 1) {
            label += " " + std::to_string(position) + "/" + std::to_string(total);
        }
        view.topics.push_back(label);
    }
    view.cursor = lettersCursor_;
    return view;
}

std::vector<std::string> Session::journalWorkRows() const {
    // WHAT THE PLAYER OWES AND WHAT THEY HAVE DONE, under the leads: every
    // LIVE contract off the same board the HUD's contractLine reads -- all of
    // them, where that corner row only ever shows the soonest -- and then
    // every finished stage's own authored log line, in the order they were
    // earned. Both are pure derived reads (the board and the QuestJournal are
    // already hashed sim state; this hashes nothing new), and both rows are
    // SOMETHING TO READ, not a choice -- picking one is the same honest no-op
    // a character-sheet row gives a number press.
    std::vector<std::string> rows;
    const sim::DialogueDirector& talk = tavern_->dialogue();
    const sim::ContractBoard& board = talk.contracts();
    for (const sim::Contract& row : board.contracts()) {
        if (!row.live()) {
            continue;
        }
        // The same have/want fraction contractLine() prints, so the journal
        // and the HUD corner can never disagree about the one job they both
        // show. The deadline rides the row in nights left rather than a raw
        // day index, because "DUE TONIGHT" is the fact a player acts on.
        const std::int32_t have = talk.crimes().stash().count(row.good);
        std::string line = "- " + row.label + " " + std::to_string(have) + "/" +
                           std::to_string(row.units);
        const std::int32_t nights = row.dueOnDay - board.day();
        if (nights <= 0) {
            line += "  DUE TONIGHT";
        } else if (nights == 1) {
            line += "  1 NIGHT";
        } else {
            line += "  " + std::to_string(nights) + " NIGHTS";
        }
        rows.push_back(std::move(line));
    }
    // RADIANT BUILD: the ward's own errands, under the broker's jobs. Every
    // TAKEN radiant objective, with the same have/want fraction a contract
    // row gets (a deliver has no goods to count, so its row names the two
    // ends of the walk instead), and NO deadline -- an errand has none, and
    // printing a fake one would be worse than printing the truth. The same
    // read-only contract as every row here: something to read, not a choice.
    for (const sim::RadiantObjective& row : talk.radiant().objectives()) {
        if (!row.live()) {
            continue;
        }
        std::string line = "- ";
        if (row.isFetch()) {
            const std::int32_t have = talk.crimes().stash().count(row.good);
            line += row.verb + " " + std::to_string(row.units) + " " +
                    std::string(sim::contrabandLabelFor(row.good, row.units)) + " " +
                    std::to_string(have) + "/" + std::to_string(row.units) + "  FOR " +
                    upperAscii(row.giverName);
        } else {
            line += row.verb + " TO " + upperAscii(row.targetName) + "  FROM " +
                    upperAscii(row.giverName);
        }
        rows.push_back(std::move(line));
    }
    // "* " is the journal's own "followed" mark -- a finished stage is a lead
    // that paid off, and it reads with the same glyph.
    for (const std::string& entry : talk.journal().log()) {
        rows.push_back("* " + entry);
    }
    return rows;
}

DialogueViewState Session::journalPanelView() const {
    // THE JOURNAL. Not a new panel and not a sheet: one more content shape on
    // the widget family every other tile already uses.
    // COURIER CASE. The tile shows the live errand while one runs -- the
    // identical active-case rule the composed page keeps; see
    // casebookPageState.
    const sim::CasebookRaws& raws = activeCaseRaws();
    const sim::Casebook& book = activeCasebook();
    DialogueViewState view;
    view.phase = static_cast<float>(body_->stepCount()) / 60.0F;
    view.open = true;
    view.speaker = "THE CASEBOOK";
    view.epithet = std::string(raws.title());
    // THE ATTITUDE FIELD IS SHORT BY CONSTRUCTION -- in a conversation it
    // holds "WARM" or "HOSTILE" -- and the top-right of that band is where
    // the HUD draws the clock over it. The first S10 capture put a
    // twenty-nine character dread band there and the clock landed in the
    // middle of it. The ward's nerve moved down onto the line, where it is
    // the first thing you read in your own notes, which is also better.
    view.attitude = book.closed() ? "CLOSED" : "OPEN";
    const std::vector<std::int32_t> heard = book.known();
    // MERGE FIX: `book` is the active casebook now (courier lane); the legend
    // -- what the ward calls you -- takes its own name.
    const sim::Legend called = legend();
    if (caseEntry_ >= 0 && static_cast<std::size_t>(caseEntry_) < heard.size()) {
        const std::int32_t leadIndex = heard[static_cast<std::size_t>(caseEntry_)];
        const sim::Lead& lead = raws.leads()[static_cast<std::size_t>(leadIndex)];
        const sim::LeadState what = book.state(leadIndex);
        view.line = what == sim::LeadState::Open ? lead.place + ". " + lead.what + "."
                                                  : lead.found + " " + lead.detail;
        // TASK #82. THE DATELINE, AND THE CROSS-REFERENCE -- what a
        // detective's log keeps that a bare topic list does not: when
        // this went in the book, what told you to come here, and (once
        // followed) what it put in the book next. See
        // DialogueViewState::caseRef's own header on why this is a
        // separate row rather than folded into `line`.
        std::string ref = formatCaseDay(book.heardAt(leadIndex));
        const std::vector<std::int32_t> from = raws.openedBy(leadIndex);
        if (from.empty()) {
            // THE ONE LEAD WITH NO OPENER. Not blank: a log that omits
            // the start of its own case reads as missing a page, not as
            // having none to show.
            ref += "  THE CASE OPENED HERE";
        } else {
            ref += "  FROM ";
            for (std::size_t i = 0; i < from.size(); ++i) {
                if (i > 0) {
                    ref += ", ";
                }
                const sim::Lead& opener = raws.leads()[static_cast<std::size_t>(from[i])];
                ref += opener.brief.empty() ? opener.place : opener.brief;
            }
        }
        if (what == sim::LeadState::Followed && !lead.opens.empty()) {
            ref += "  OPENED ";
            for (std::size_t i = 0; i < lead.opens.size(); ++i) {
                if (i > 0) {
                    ref += ", ";
                }
                const std::int32_t opened = raws.indexOf(lead.opens[i]);
                if (opened >= 0) {
                    const sim::Lead& next = raws.leads()[static_cast<std::size_t>(opened)];
                    ref += next.brief.empty() ? next.place : next.brief;
                }
            }
        } else if (what == sim::LeadState::Cold) {
            ref += "  DEAD END";
        }
        // A HINT AT THE LETTERS, when this exact lead unlocked one. The
        // player has already earned the right to read it -- see
        // unlockedLetters() -- so the casebook says where to press
        // rather than making them discover the key by accident.
        for (const std::int32_t li : unlockedLetters()) {
            if (letterAt(li).lead == lead.id) {
                ref += "  L READS HIS LETTERS";
                break;
            }
        }
        view.caseRef = ref;
    } else if (book.readCount() == 0) {
        // THE OPENING PAGE OF A NEW GAME: the hook, and nothing else. It is
        // the first thing a player ever reads in this game and it gets the
        // band to itself.
        view.line = std::string(raws.hook());
    } else {
        // And afterwards: what the ward's nerve is doing, and what it calls
        // you for the work so far. TWO SHORT SENTENCES, because the band
        // wraps to three lines and the S10 capture that ran to four lost
        // "OF THE FLAME" off the end of its own title.
        view.line = std::string(raws.dreadLabel(book.dread())) + ". THEY CALL YOU " +
                    std::string(called.title()) + ".";
    }
    for (const std::int32_t index : heard) {
        const sim::Lead& lead = raws.leads()[static_cast<std::size_t>(index)];
        std::string row;
        switch (book.state(index)) {
            case sim::LeadState::Open:
                row = "? ";
                break;
            case sim::LeadState::Cold:
                row = "X ";
                break;
            case sim::LeadState::Followed:
                row = "* ";
                break;
            default:
                row = "  ";
                break;
        }
        // THE SHORT NAME, and it is authored rather than truncated here.
        // casebook.json carries a `short` for every lead and a case pins
        // that all of them fit.
        view.topics.push_back(row + (lead.brief.empty() ? lead.place : lead.brief));
    }
    // THE WORK, UNDER THE TRAIL: live contracts and the finished stages' own
    // log lines -- see journalWorkRows(). After the leads on purpose, so
    // every existing index into the list (caseEntry_, chooseTopic's own lead
    // guard) still means the lead it always meant.
    for (std::string& row : journalWorkRows()) {
        view.topics.push_back(std::move(row));
    }
    // THE WAITING SENTENCE IS RETIRED HERE TOO (UI-EA-SPEC 1.5, prose 14->0,
    // and the two-surfaces-one-wording rule cuts both ways): the casebook
    // page stopped saying it, so the tile stops with it. The stippled field
    // still says "left on purpose"; the hook on a fresh book still leads.
    view.cursor = caseCursor_;
    view.page = casePage_;
    return view;
}

CreationPage Session::stripCard() const {
    // UI-EA-SPEC 1.7 (LANE PAGES, strip->card): the pause stack's four strips
    // -- pause, wait, options, grimoire -- drawn as ONE composed card through
    // the same generic composition every creation step already uses
    // (drawCreationPage): framed, seated, measured, a numbered master list
    // with an inverted-fill cursor. The SHIP NOTE's standing item ("the
    // options page onto the master/detail card"), extended to the family.
    //
    // INPUT IS UNTOUCHED: rows, cursor and the nine-key digit windows keep
    // wrapCursorAndPage's own arithmetic, so every press lands where it
    // always did -- only the drawing changed register. The digits print on
    // the current nine-key window and on nothing else, the same honesty the
    // tiles and the casebook keep.
    CreationPage out;
    out.alpha = 1.0F;
    out.hasDetail = false;
    out.shape = CreationListShape::Columns;
    out.maxColumns = 1;
    out.stipple = false;
    const InputDevice dev = promptDevice_;
    const std::string back(promptBackKey(dev));

    std::vector<std::string> rows;
    int cursor = 0;
    int window = 0;
    if (pauseOpen_) {
        // THE BUILD STAMP RIDES THE PAUSE TITLE (UI-EA-SPEC sec. 2) -- the
        // HUD's own corner is quiet now, and the one page a player opens to
        // stand still is where a version belongs.
        out.title = "GRANADAD " + std::string(sim::build_info().version);
        rows = pauseRows();
        cursor = pauseCursor_;
        out.bodyHoldRows = static_cast<int>(rows.size());
    } else if (waitOpen_) {
        out.title = waitSleep_ ? "SLEEP" : "WAIT";
        // The flavour is four words (spec #38, 15 -> 4) -- or the live
        // refusal, which outranks it and is register, not chrome. Sleep keeps
        // its one honest clause: it is the only wait that mends.
        const std::string refusal = waitSleep_ ? std::string() : waitRefusal();
        if (!refusal.empty()) {
            out.instruction = refusal;
        } else {
            out.instruction = waitSleep_ ? "SLEEP MENDS. PICK THE HOUR." : "TIME PASSES. HEAT COOLS.";
        }
        rows = waitRows();
        cursor = waitCursor_;
        window = waitPage_;
        out.bodyHoldRows = 12;
    } else if (optionsOpen_) {
        // NINE AND THE STICKS: THE KEYS PAGE'S OWN TAB STRIP, so the sibling
        // pair reads as one surface from both sides -- CONTROLS, then KEYS
        // and OPTIONS as tabs with this one lit, exactly what drawKeysPage
        // prints over the other. The sub-tab key that swaps them is named
        // on the foot below.
        out.title = "CONTROLS";
        out.tabs = {PanelTab{"", "KEYS"}, PanelTab{"", "OPTIONS"}};
        out.currentTab = 1;
        // Instruction 22 -> 4 (spec #37): the value hint, or the capture
        // state's own modal line, which is load-bearing while the game is
        // listening for a raw key -- and TRUE on both hands: ESC cancels on
        // a keyboard, B (and START) on a pad, and none of the three can be
        // captured into a verb (main.cpp's pressed() drops them).
        if (awaitingKey_) {
            out.instruction = dev == InputDevice::Pad ? "PRESS A KEY. B CANCELS."
                                                      : "PRESS A KEY. ESC CANCELS.";
        } else {
            out.instruction = "REBIND STEALS THE KEY.";
        }
        rows = optionRows();
        cursor = optionCursor_;
        window = optionPage_;
        out.bodyHoldRows = 14;
    } else if (searchOpen()) {
        // KIT BUILD: the corpse's kit as the composed card.
        const sim::Actor* corpse = tavern_->actorById(searchActorId_);
        out.title = corpse != nullptr ? upperAscii(corpse->name()) : std::string("THE DEAD");
        rows = grimoireRows();
        out.instruction = rows.empty() ? "NOTHING ON HIM." : "D - DRAMS.";
        cursor = grimoireCursor_;
        window = grimoirePage_;
    } else if (grimoireOpen_) {
        out.title = "GRIMOIRE";
        rows = grimoireRows();
        // Instruction: the one column that needs naming, and the one verb
        // this page has that no other list has -- LEFT/RIGHT binds a slot --
        // named in the live hand's keycaps. The empty state is the cast
        // refusal's OWN words -- the page and the C key must name the same
        // door or one of them is lying (test_tavern pins the line), so the
        // register literal outranks the six-word rule here.
        const std::string sideways = dev == InputDevice::Pad ? std::string("\x06\x04\x05")
                                                              : std::string("\x04\x05");
        out.instruction = rows.empty() ? "NO CRAFTING HELD. THE PRIEST OF THE FLAME TEACHES."
                                       : "D IS THE ASK. " + sideways + " BINDS A SLOT.";
        cursor = grimoireCursor_;
        window = grimoirePage_;
        out.bodyHoldRows = 8;
    }

    // THE CARD PAGES AT TWELVE (UI-EA-SPEC 1.7: the wait clock over two
    // pages, the options table likewise) -- a screenful anchored on the
    // cursor, `+N` as the last, unselectable row saying what waits past the
    // fold, and the digit keys printed only on the nine-key window the
    // direct-select arithmetic really answers to.
    constexpr int kCardRows = 12;
    const int total = static_cast<int>(rows.size());
    const int screen = total > kCardRows ? std::clamp(cursor, 0, total - 1) / kCardRows : 0;
    const int first = screen * kCardRows;
    const int last = std::min(total, first + kCardRows);
    out.rows.reserve(static_cast<std::size_t>(last - first) + 1);
    const int windowFirst = window * kTopicPageSize;
    // The wait rows number THEMSELVES (`7 - DAWN 06:00`), so a printed key
    // column would say every digit twice -- digits pick what they print, and
    // those rows already print them.
    // NINE AND THE STICKS: no digit column with a pad in hand -- a pad has
    // no number row, so a printed `1` beside RESUME is a key nothing on the
    // pad answers to. The wait rows keep their number: it is the hours
    // ahead, information, not a key.
    const bool selfNumbered = waitOpen_;
    const bool digits = dev != InputDevice::Pad;
    for (int i = first; i < last; ++i) {
        CreationPageRow row;
        const int slot = i - windowFirst;
        if (digits && !selfNumbered && slot >= 0 && slot < kTopicPageSize) {
            row.key = std::to_string(slot + 1);
        }
        row.label = rows[static_cast<std::size_t>(i)];
        row.accent = panelInk().accent;
        out.rows.push_back(std::move(row));
    }
    if (last < total) {
        CreationPageRow more;
        more.label = "+" + std::to_string(total - last);
        more.accent = panelInk().dim;
        more.selectable = false;
        out.rows.push_back(std::move(more));
    }
    out.cursor = std::clamp(cursor, 0, std::max(0, total - 1)) - first;
    // THE FOOT: the page's own keys, then BACK. Bare keycaps at rest; the
    // words ride the tutor tier, which the pause stack leaves at rest --
    // these pages ARE their rows. NINE AND THE STICKS: the grimoire is a
    // page of NOTES, so its foot carries the page keys that walk on to the
    // sheet (or back to the ward map); the options page names the sub-tab
    // key that swaps to KEYS and the sideways key that changes a value.
    const auto footKey = [&out](std::string key, const char* label) {
        PanelOption foot;
        foot.key = std::move(key);
        foot.label = label;
        foot.valueInk = InkRole::Dim;
        foot.selectable = false;
        out.nav.push_back(std::move(foot));
    };
    if (grimoireOpen_) {
        footKey(std::string(promptPageKeys(dev)), "NOTES");
        footKey(std::string(promptConfirmKey(dev)), "READY");
    } else if (optionsOpen_) {
        footKey(std::string(promptTabKeys(dev)), "KEYS");
        footKey(dev == InputDevice::Pad ? std::string("\x06\x04\x05") : std::string("\x04\x05"),
                "CHANGE");
        footKey(std::string(promptConfirmKey(dev)), "REBIND");
    }
    footKey(back, "BACK");
    return out;
}

namespace {

// --- #82: the district map --------------------------------------------------

/// The job-family prefix content/raws/factions/factions.json's own
/// memberJobs speaks, for a notable's authored TYPE
/// (content/raws/names/notables.json). Four of the Forty's seven authored
/// types have no family at all here, and that is not a gap this table failed
/// to cover: a wastrel answers to nobody, which is factions.json's own
/// documented rule ("wastrel.streetlife ... deliberately unaffiliated"), and
/// the same silence is the honest answer for anything else this build has
/// not met yet.
[[nodiscard]] std::string_view notableJobFamily(std::string_view type) noexcept {
    if (type == "shopkeeper") {
        return "trade";
    }
    if (type == "militia_watch") {
        return "watch";
    }
    if (type == "priest_of_the_flame" || type == "disciple_of_the_flame") {
        return "clergy";
    }
    if (type == "serf") {
        return "serf";
    }
    if (type == "animal_keeper") {
        return "husbandry";
    }
    return {};
}

/// "NW 31T", or "HERE" standing on it -- the eight-point word and the
/// straight-line tile range from one point to another. This is the whole
/// reason the district map exists over the casebook's own list: a lead there
/// is a NAME; here it is a DIRECTION from where the player is actually
/// standing.
///
/// INTEGER THROUGHOUT, deliberately. sim::bearingTo is the identical call
/// PlayerBody's own stealth arc is judged by (sim/stealth.hpp) -- not a fresh
/// trig call invented for a menu -- and the range is Chebyshev, the tile
/// count a body that can step diagonally actually walks to close it, not a
/// ruler laid across the map.
[[nodiscard]] std::string bearingLabel(std::int32_t fromX, std::int32_t fromY, std::int32_t toX,
                                       std::int32_t toY) {
    const std::int32_t dx = toX - fromX;
    const std::int32_t dy = toY - fromY;
    if (dx == 0 && dy == 0) {
        return "HERE";
    }
    const std::int32_t ax = dx < 0 ? -dx : dx;
    const std::int32_t ay = dy < 0 ? -dy : dy;
    const std::int32_t tiles = ax > ay ? ax : ay;
    return std::string(sim::compass_point(sim::bearingTo(fromX, fromY, toX, toY))) + " " +
           std::to_string(tiles) + "T";
}

/// KITFIX LANE. THE ONE PLACE A NUMBER GETS PADDED. A capture of the Kit
/// tile (docs/frames/kit/) found the DR column starting at four different x
/// and the C column at four more -- KNIFE's 8DR sits three cells left of
/// LANTERN's 30DR because each row's number followed straight off ITS OWN
/// name, and a name is not a column. Every numeric suffix on the sheet is
/// supposed to compose through here instead of a caller hand-padding a
/// std::to_string in place: right-justified into a field exactly as wide as
/// the widest value the CALLER'S OWN LIST can show, so a five-row page never
/// reserves a sixth row's width and a hundred-drams row never crowds a
/// single-digit one.
[[nodiscard]] std::string rightAlignedDigits(std::int32_t value, std::size_t width) {
    std::string digits = std::to_string(value);
    if (digits.size() < width) {
        digits.insert(digits.begin(), width - digits.size(), ' ');
    }
    return digits;
}

/// PHASE 9 -- THE WORN BLOCK'S LABEL COLUMN: every slot's short word
/// (HAND .. TRINKET) padded to the widest of the seven, so the things worn
/// start in one column on both views of the tile. Measured off the words
/// themselves rather than typed as 8: a renamed slot moves the column, not
/// the rows under it.
[[nodiscard]] std::size_t slotWordWidth() {
    std::size_t width = 0;
    for (const sim::ItemSlot slot : sim::kSlotScreenOrder) {
        width = std::max(width, std::string(sim::itemSlotShortWord(slot)).size());
    }
    return width;
}

/// One worn row, the SAME string on the sheet and in the pack: the slot's
/// word, then the thing on it -- a hand item as the sheet's own span line
/// ("CUTLASS 11-13 EDGE"), a piece by name with its DR, "-" for a bare
/// slot. The hand is the one slot that is never "-": D10 ruled fists a real
/// state the moment an equipment model exists, so a bare hand prints what it
/// swings (FISTS 3-5 IMPACT, or the class a sheet put in it directly).
[[nodiscard]] std::string wornSlotLine(const sim::Tavern& tavern, sim::ItemSlot slot) {
    std::string line(sim::itemSlotShortWord(slot));
    line.append(slotWordWidth() - line.size(), ' ');
    line += "  ";
    const sim::ItemDef* worn = tavern.items().at(tavern.kit().worn(slot));
    if (worn != nullptr) {
        line += slot == sim::ItemSlot::Hand ? sim::itemSheetLine(*worn) : worn->name;
        if (worn->dr > 0) {
            line += "  DR " + std::to_string(worn->dr);
        }
    } else if (slot == sim::ItemSlot::Hand) {
        line += sim::weaponSheetLine(tavern.playerWeapon());
    } else {
        line += "-";
    }
    return line;
}

/// PHASE 9 -- THE COMPARISON BLOCK, word for word off the sim's GearCompare
/// and nothing else (the screen never does the arithmetic the wear will),
/// and ONLY WHAT CHANGES: a line that says "2 -> 2" is a line the eye has to
/// read to learn nothing. DR when the press moves it; the span when the hand
/// changes and the span with it; reach and the swing's recovery as words;
/// then one line per thing the press bares, the THING named first so it
/// cannot misread as a slot ("TAKES OFF HAND CUTLASS" read as an off-hand
/// cutlass): PUTS DOWN THE <NAME> for what leaves the hand, TAKES OFF THE
/// <NAME> for anything else. Each line carries its verdict for the ink:
/// more DR, a bigger span (by its sum), a longer reach and a faster swing
/// are better; their opposites, and anything coming off, are worse.
void gearCompareLines(const sim::GearCompare& gear, const sim::Tavern& tavern,
                      InspectPane& pane) {
    pane.compare.clear();
    pane.compareTone.clear();
    if (!gear.applies) {
        return;
    }
    using Tone = InspectPane::Tone;
    const auto add = [&pane](std::string line, Tone tone) {
        pane.compare.push_back(std::move(line));
        pane.compareTone.push_back(tone);
    };
    const auto n = [](std::int32_t value) { return std::to_string(value); };
    if (gear.drThen != gear.drNow) {
        add("DR " + n(gear.drNow) + " -> " + n(gear.drThen),
            gear.drThen > gear.drNow ? Tone::Better : Tone::Worse);
    }
    if (gear.showsDamage &&
        (gear.dmgLoThen != gear.dmgLoNow || gear.dmgHiThen != gear.dmgHiNow)) {
        const std::int32_t now = gear.dmgLoNow + gear.dmgHiNow;
        const std::int32_t then = gear.dmgLoThen + gear.dmgHiThen;
        add("DMG " + n(gear.dmgLoNow) + "-" + n(gear.dmgHiNow) + " -> " + n(gear.dmgLoThen) +
                "-" + n(gear.dmgHiThen),
            then > now ? Tone::Better : (then < now ? Tone::Worse : Tone::Plain));
    }
    if (gear.reachThenQ8 != gear.reachNowQ8) {
        add(gear.reachThenQ8 > gear.reachNowQ8 ? "REACH LONGER" : "REACH SHORTER",
            gear.reachThenQ8 > gear.reachNowQ8 ? Tone::Better : Tone::Worse);
    }
    if (gear.recoveryThen != gear.recoveryNow) {
        add(gear.recoveryThen > gear.recoveryNow ? "SPEED SLOWER" : "SPEED FASTER",
            gear.recoveryThen > gear.recoveryNow ? Tone::Worse : Tone::Better);
    }
    for (const sim::ItemSlot slot : gear.bares) {
        const sim::ItemDef* off = tavern.items().at(tavern.kit().worn(slot));
        if (off == nullptr) {
            continue;
        }
        add((slot == sim::ItemSlot::Hand ? "PUTS DOWN THE " : "TAKES OFF THE ") + off->name,
            Tone::Worse);
    }
}

}  // namespace

std::vector<std::string> Session::characterRows() const {
    if (characterPack_) {
        return packRowLabels();
    }
    return sheetRows(nullptr);
}

std::vector<std::string> Session::sheetRows(std::size_t* kitOffset) const {
    // FIVE ROWS, THEN FOUR, THEN FIVE, THEN THREE: the five Legend tracks
    // (S8's own "who am I in this city yet", derived and thrown away every
    // frame until this page existed -- see the header), the four skills a
    // verb in this build actually levels, the five faction ladders, and what
    // the ward and the purse currently say. The tracks and skills fill page
    // one exactly -- see kTopicPageSize -- and the ladders open page two, so
    // neither block is ever split by a page turn a player has to go looking
    // for.
    std::vector<std::string> rows;
    // The five tracks, up to twenty skills, four attributes, five ladders,
    // the three always-printed rows, the seven worn rows and the load.
    rows.reserve(sim::kLegendTracks + 20 + sim::kAttributeCount + 5 + 3 +
                 std::size(sim::kSlotScreenOrder) + 1);
    const sim::Legend book = legend();
    for (std::size_t i = 0; i < sim::kLegendTracks; ++i) {
        const sim::LegendRow& row = book.rows()[i];
        std::string_view name = sim::legendTrackName(row.track);
        // EVERY TRACK NAME STARTS "THE ", legend.hpp's own table, and five
        // copies of the one word they share is four words spent out of an
        // eighteen-glyph column for nothing. Dropped on the way to the screen
        // only -- legendTrackName() still returns the full "THE WIRE"
        // everywhere else that reads it, because this is a display choice and
        // not a rename.
        if (name.substr(0, 4) == "THE ") {
            name.remove_prefix(4);
        }
        std::string line = std::string(name) + " - " + std::string(row.title);
        // WHAT THE NEXT RUNG WANTS, kept at last. legend.hpp's own header has
        // promised since S8 that "the panel prints what the next one wants in
        // the same units the player already sees" -- LegendRow::nextAt has
        // carried the number all along and no panel ever printed it. Score
        // over threshold, the notation every counted stage already uses; a
        // topped-out track shows its score alone, because a target it has
        // passed forever would read as work still owed.
        line += "  " + std::to_string(row.score);
        if (row.nextAt > 0) {
            line += "/" + std::to_string(row.nextAt);
        }
        rows.push_back(std::move(line));
    }
    const sim::DialogueDirector& talk = tavern_->dialogue();
    // THE HONEST SHEET (THE PULL PACK). The rows come off the SkillTrack
    // itself, not a hand-typed table of four: every skill the track has
    // actually MOVED -- a level above zero, or uses banked toward one --
    // plus the four this build has always shown (the ones a chargen sheet
    // designates and a stranger expects to find), in the raws' own id order.
    // The old constexpr listed four while the combat build was already
    // training shieldwall on a held guard and the scalp skill on a take, so
    // two lived skills never printed; and the sixteen a verb has never
    // touched still stay off the sheet, because a wall of LV 0 would claim
    // the game is watching a skill it is not.
    //
    // "LV 7  NEXT 12": what the next level costs, off the track's OWN
    // scaledUsesForLevel -- aptitude and the difficulty dagger composed
    // exactly as use() charges them -- less what is banked, so the number
    // is the uses still owed and never a second copy of the arithmetic.
    static constexpr std::string_view kAlwaysShown[] = {
        sim::kRoofSkill,
        sim::kThieverySkill,
        sim::kHaggleSkill,
        sim::kCraftingSkill,
    };
    for (const sim::SkillTrack::Entry& skill : talk.skills().entries()) {
        const bool always = std::find(std::begin(kAlwaysShown), std::end(kAlwaysShown),
                                      std::string_view{skill.id}) != std::end(kAlwaysShown);
        if (!always && skill.level <= 0 && skill.uses <= 0) {
            continue;
        }
        // MORROWIND ROUND: ONE SPACE, NOT TWO -- the Character tile's own
        // column is narrower than the old full-width panel's, and at 320x180
        // every glyph matters; CRACKSMANSHIP still cuts there and clipLabel()
        // marks the cut (the contract every clipped row keeps).
        std::string line = upperAscii(skill.displayName.empty() ? skill.id : skill.displayName) +
                           " LV " + std::to_string(skill.level);
        // "3 TO NEXT" -- uses, not standing, so it cannot be read as the
        // ladders' own NEXT price -- and only once the skill has MOVED: a
        // fresh sheet's four bare LV 0 rows stay bare (and stay whole in the
        // narrowest tile), and the cost appears the moment there is a count
        // to set it against.
        if (skill.level < 100 && (skill.level > 0 || skill.uses > 0)) {
            const std::int32_t owed =
                std::max(1, talk.skills().scaledUsesForLevel(skill.id, skill.level) - skill.uses);
            line += "  " + std::to_string(owed) + " TO NEXT";
        }
        rows.push_back(std::move(line));
    }
    // MGT / AGI / VIG / WIT, with the held delta beside the base: the
    // effective sheet (base plus every held tuning, clamped +/-2 -- what the
    // AGI gait and the punch actually read) against the chargen base, so a
    // STEADY THE HAND on the HUD's effect rows is a "(+2)" here and never a
    // number that silently disagrees with the one the world is using.
    const sim::AttributeBlock& base = tavern_->playerAttributes();
    const sim::AttributeBlock live = tavern_->effectiveAttributes();
    for (std::size_t i = 0; i < sim::kAttributeCount; ++i) {
        const sim::AttributeId attribute = static_cast<sim::AttributeId>(i);
        std::string line = std::string(sim::attributeAbbrev(attribute)) + " " +
                           std::to_string(live.value(attribute));
        const std::int32_t delta = live.value(attribute) - base.value(attribute);
        if (delta != 0) {
            line += " (" + std::string(delta > 0 ? "+" : "") + std::to_string(delta) + ")";
        }
        rows.push_back(std::move(line));
    }
    // THE FIVE LADDERS, WITH THE NUMBERS ON. Owner ruling for this build: the
    // player's OWN sheet shows rank title, the standing number and what the
    // next rung costs -- the word-only ruling still governs how NPCs and the
    // ward-reputation lines TALK about you, and nothing there changed. Every
    // figure is read out of FactionLedger and the raws' own ladder: the rung
    // nextRung() answers with is the exact rung join()/advance() will measure
    // (checkRung's), so the sheet can never promise a price the ladder does
    // not charge. Cost notation is standing, then "/LV n" when the rung is
    // also measured in the ladder's own skill -- the same LV the four skill
    // rows above already taught. INFORMATION, NEVER A DISCOUNT: nothing here
    // moves a number, it only stops the climb being a slot machine.
    const sim::FactionLedger& standings = talk.standings();
    if (const sim::FactionRegistry* guilds = standings.registry(); guilds != nullptr) {
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(standings.size()); ++i) {
            const sim::Faction* who = guilds->at(i);
            if (who == nullptr) {
                continue;
            }
            // The id, not the display name: "TEMPLE OF THE FLAME" would spend
            // the whole column on its own name, and the map tile's "who will
            // talk" list already prints these same ids -- one vocabulary.
            std::string line = upperAscii(who->id);
            if (const std::string_view title = standings.rankTitle(i); !title.empty()) {
                line += " " + upperAscii(title);
            }
            line += " " + std::to_string(standings.standing(i));
            if (const sim::FactionRank* next = standings.nextRung(i); next != nullptr) {
                // JOIN for an outsider, NEXT for a member -- the first rung is
                // earned exactly like every later one (faction.hpp's own rule),
                // so both read the same rung the ledger will actually check.
                line += standings.isMember(i) ? "  NEXT " : "  JOIN ";
                line += std::to_string(next->standing);
                if (next->skillLevel > 0) {
                    line += "/LV" + std::to_string(next->skillLevel);
                }
            } else if (standings.rank(i) > 0) {
                // On a ladder with no rung above you. Said plainly rather than
                // left blank, so "nothing after the number" always means "no
                // ladder" and never "top" -- two facts, two spellings.
                line += "  TOP";
            }
            rows.push_back(std::move(line));
        }
    }
    // ABSENCE COSTS NOTHING ON THE HUD; IT COSTS NOTHING HERE EITHER, but for
    // the opposite reason. The HUD drops a row that has nothing to say because
    // it is read every frame; a sheet a player opened ON PURPOSE to look
    // themselves up is the one place a zero is worth printing, so REPUTATION,
    // COIN and HEAT are always here, not just when they are interesting.
    rows.push_back("REPUTATION  " + std::string(talk.ledger().reputationLabel()));
    rows.push_back("COIN  " + std::to_string(tavern_->playerCoin()));
    rows.push_back("HEAT  " + std::to_string(talk.crimes().heat()));
    // THE WORN BLOCK (OBLIVION-PLAN phase 9): all seven slots, always, in the
    // screen's own order -- HAND, OFF-HAND, HEAD, BODY, HANDS, FEET, TRINKET
    // -- the same seven rows the pack's WORN block prints, "-" on a bare
    // one. D10 printed a slot only when the raws held an item for it; with a
    // shield, gloves and a helmet in the raws every slot has one, and a block
    // that is always seven rows is one the eye learns once. The hand keeps
    // D10's other rule (wornSlotLine): fists are a state, never a dash.
    const sim::ItemRegistry& items = tavern_->items();
    for (const sim::ItemSlot slot : sim::kSlotScreenOrder) {
        rows.push_back(wornSlotLine(*tavern_, slot));
    }
    // THE LOAD, the one visible budget, then every carried row.
    rows.push_back(loadLine());
    // Where the carried rows start: the cursor offset the tile's verbs
    // subtract (characterKitOffset), counted off the rows actually pushed --
    // a skill that has moved, a ladder the raws add, a slot -- never typed.
    if (kitOffset != nullptr) {
        *kitOffset = rows.size();
    }
    // KITFIX LANE. THE LIST READ TWICE: once to learn what every row's
    // prefix, drams and worth actually are (and how wide the widest of
    // each runs), once to compose the lines -- rightAlignedDigits()'s own
    // header on why a row cannot pad itself against a list it cannot see.
    // Copied out of kitRows() the one time rather than called twice, which
    // would ask the registry the same question under two different lists if
    // anything about it were ever to change between calls.
    const std::vector<KitRow> kit = kitRows();
    struct KitLine {
        const KitRow* row = nullptr;
        const sim::ItemDef* thing = nullptr;
        std::string prefix;
        std::int32_t drams = 0;
        std::int32_t royals = 0;
    };
    std::vector<KitLine> lines;
    lines.reserve(kit.size());
    std::size_t prefixWidth = 0;
    std::size_t dramsWidth = 0;
    std::size_t royalsWidth = 0;
    for (const KitRow& row : kit) {
        const sim::ItemDef* thing = items.at(row.item);
        if (thing == nullptr) {
            continue;
        }
        KitLine entry;
        entry.row = &row;
        entry.thing = thing;
        if (row.count > 1) {
            entry.prefix = std::to_string(row.count) + " ";
        }
        entry.prefix += thing->name;
        // Weight and worth, the reference's right-aligned numbers, in the
        // font's own letters (no icons): DR is drams, C is coin.
        entry.drams = row.item == tavern_->items().indexOf("bale")
                          ? tavern_->dialogue().crimes().baleUnits() *
                                sim::contrabandWeight(tavern_->dialogue().crimes().baleGood())
                          : thing->drams * row.count;
        entry.royals = thing->royals > 0 ? thing->royals * row.count : 0;
        prefixWidth = std::max(prefixWidth, entry.prefix.size());
        dramsWidth = std::max(dramsWidth, std::to_string(entry.drams).size());
        if (entry.royals > 0) {
            royalsWidth = std::max(royalsWidth, std::to_string(entry.royals).size());
        }
        lines.push_back(std::move(entry));
    }
    for (const KitLine& entry : lines) {
        const KitRow& row = *entry.row;
        const sim::ItemDef& thing = *entry.thing;
        // The name column PADDED to the widest name this page holds, so
        // every row's "  NNDR" starts under the last one -- the fix itself,
        // everything above just measured for it.
        std::string line = entry.prefix;
        line.append(prefixWidth - entry.prefix.size(), ' ');
        line += "  " + rightAlignedDigits(entry.drams, dramsWidth) + "D";
        if (entry.royals > 0) {
            line += "  " + rightAlignedDigits(entry.royals, royalsWidth) + "C";
        }
        if (row.inKit && tavern_->kit().isWorn(row.item)) {
            // HELD, not IN HAND (the cells pass): at the register's floor of
            // 85 cells the Character tile is 27 wide, and `IN HAND` was the
            // word the row lost -- `KNIFE   8DR   4C  IN..` on every common
            // monitor. HELD and WORN are a pair. The SLOT tag below is the
            // column that falls off there, whole (panel.hpp, clipColumns),
            // and the strip says which slot. PHASE 9: a shield is held too.
            line += thing.slot == sim::ItemSlot::Hand || thing.slot == sim::ItemSlot::OffHand
                        ? "  HELD"
                        : "  WORN";
        }
        for (std::int32_t slot = 0; slot < sim::Tavern::kQuickSlotCount; ++slot) {
            if (tavern_->slotItemIndex(slot) == row.item) {
                line += "  SLOT " + std::to_string(slot + 1);
                break;
            }
        }
        if (thing.heat > 0) {
            line += "  HOT";
        }
        rows.push_back(std::move(line));
    }
    return rows;
}

// ---------------------------------------------------------------------------
// KIT BUILD: the Kit on the Character tile, and the search list
// ---------------------------------------------------------------------------

std::vector<Session::KitRow> Session::kitRows() const {
    // ONE LIST OUT OF THREE STORES, in the registry's document order: the
    // Kit's own count, or the sack's for a contraband row, or the bale, or
    // the picks. Rows with nothing on them are not rows.
    std::vector<KitRow> rows;
    const sim::ItemRegistry& items = tavern_->items();
    const sim::CrimeLedger& crimes = tavern_->dialogue().crimes();
    const std::int32_t picks = items.indexOf("picks");
    const std::int32_t bale = items.indexOf("bale");
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(items.size()); ++i) {
        const sim::ItemDef& thing = items.items()[static_cast<std::size_t>(i)];
        KitRow row;
        row.item = i;
        if (!thing.contraband.empty()) {
            sim::Contraband good = sim::Contraband::Scalp;
            if (sim::contrabandFromSymbol(thing.contraband, good)) {
                row.count = crimes.stash().count(good);
            }
        } else if (i == picks) {
            row.count = tavern_->picks();
        } else if (i == bale) {
            row.count = crimes.carryingBale() ? 1 : 0;
        } else {
            row.count = tavern_->kit().count(i);
            row.inKit = row.count > 0;
        }
        if (row.count > 0) {
            rows.push_back(row);
        }
    }
    return rows;
}

std::size_t Session::characterKitOffset() const {
    if (characterPack_) {
        // The pack's rows are the list: nothing precedes them.
        return 0;
    }
    // Read off the sheet itself, not re-added here. MERGE FIX's lesson, twice
    // over: this was a typed sum (tracks + four skills + attributes + five
    // ladders + ward rows + IN HAND + the raws' slots + LOAD) that fell out of
    // step with characterRows() when the attribute rows went in, and stayed
    // out of step for any skill a verb had moved past the four always shown.
    // PHASE 9 made the worn block seven rows; the count below cannot miss it.
    std::size_t offset = 0;
    (void)sheetRows(&offset);
    return offset;
}

std::optional<Session::KitRow> Session::highlightedKitRow() const {
    if (characterPack_) {
        const std::vector<PackRow> rows = packRows();
        if (characterCursor_ < 0 || static_cast<std::size_t>(characterCursor_) >= rows.size()) {
            return std::nullopt;
        }
        const PackRow& at = rows[static_cast<std::size_t>(characterCursor_)];
        if (at.kind == PackRow::Kind::Header || at.item < 0) {
            return std::nullopt;
        }
        KitRow row;
        row.item = at.item;
        // A worn row's count is the whole stack the Kit holds of it (the
        // verbs act on the Kit, which does not split a worn unit off).
        row.count = at.kind == PackRow::Kind::Slot ? tavern_->kit().count(at.item) : at.count;
        row.inKit = at.inKit;
        return row;
    }
    const std::size_t offset = characterKitOffset();
    if (characterCursor_ < 0 || static_cast<std::size_t>(characterCursor_) < offset) {
        return std::nullopt;
    }
    const std::vector<KitRow> rows = kitRows();
    const std::size_t at = static_cast<std::size_t>(characterCursor_) - offset;
    if (at >= rows.size()) {
        return std::nullopt;
    }
    return rows[at];
}

std::string Session::loadLine() const {
    return "LOAD  " + std::to_string(tavern_->loadDrams()) + " / " +
           std::to_string(tavern_->loadBudget()) + " DRAMS";
}

void Session::wearHighlightedKitRow() {
    const std::optional<KitRow> row = highlightedKitRow();
    if (!row.has_value()) {
        return;
    }
    const sim::ItemDef* thing = tavern_->items().at(row->item);
    if (thing == nullptr) {
        return;
    }
    if (!row->inKit) {
        // The sack, the bale, the picks: not worn, and the room says so in
        // the words wearItem would.
        if (audio_ != nullptr) {
            audio_->playOneShot(audio::SoundId::UiTick);
        }
        say("THE " + thing->name + " IS NOT WORN.");
        return;
    }
    if (!thing->effects.empty() && thing->slot == sim::ItemSlot::None) {
        // THE ONE PRESS IS THE ROW'S OWN VERB: a thing with a use is used
        // (one unit spent, the line says what it did), a thing with a slot is
        // worn. Refusals keep the unit and say why.
        const sim::Tavern::StealResult used = tavern_->useItem(row->item);
        if (audio_ != nullptr) {
            audio_->playOneShot(used.result == sim::ServiceResult::Served
                                    ? audio::SoundId::UiConfirm
                                    : audio::SoundId::UiTick);
        }
        say(used.line);
        const int count = static_cast<int>(characterRows().size());
        if (count > 0 && characterCursor_ >= count) {
            characterCursor_ = count - 1;
            characterPage_ = topicPageOf(characterCursor_);
        }
        settlePackCursor(-1);
        return;
    }
    const bool wasWorn = tavern_->kit().isWorn(row->item);
    const sim::Tavern::StealResult done = tavern_->wearItem(row->item);
    if (audio_ != nullptr) {
        if (done.result != sim::ServiceResult::Served) {
            audio_->playOneShot(audio::SoundId::UiTick);
        } else if (thing->slot == sim::ItemSlot::Hand) {
            // A blade leaves its sheath; a cudgel is picked up off the table.
            audio_->playOneShot(thing->weaponClass >= sim::kFirstLethalWeapon
                                    ? audio::SoundId::KnifeDraw
                                    : audio::SoundId::MetalClick);
        } else if (thing->slot == sim::ItemSlot::OffHand) {
            // PHASE 9: a shield is taken up off the floor, not shrugged on.
            audio_->playOneShot(audio::SoundId::MetalClick);
        } else {
            audio_->playOneShot(audio::SoundId::ClothRustle);
        }
    }
    (void)wasWorn;
    say(done.line);
}

void Session::dropHighlightedKitRow() {
    const std::optional<KitRow> row = highlightedKitRow();
    if (!row.has_value()) {
        return;
    }
    syncTavernToBody();
    const sim::Tavern::StealResult done = tavern_->dropItem(row->item);
    if (audio_ != nullptr) {
        audio_->playOneShot(done.result == sim::ServiceResult::Served
                                ? audio::SoundId::ThudMedium
                                : audio::SoundId::UiTick);
    }
    say(done.line);
    // The cursor stays on the row; a row that emptied is gone from the
    // list, so keep the cursor inside what is left.
    const int count = static_cast<int>(characterRows().size());
    if (count > 0 && characterCursor_ >= count) {
        characterCursor_ = count - 1;
        characterPage_ = topicPageOf(characterCursor_);
    }
    settlePackCursor(-1);
}

void Session::adjustKitSlot(int delta) {
    if (delta == 0) {
        return;
    }
    const std::optional<KitRow> row = highlightedKitRow();
    if (!row.has_value() || !row->inKit) {
        return;
    }
    const sim::ItemDef* thing = tavern_->items().at(row->item);
    if (thing == nullptr) {
        return;
    }
    if (thing->slot == sim::ItemSlot::None) {
        if (audio_ != nullptr) {
            audio_->playOneShot(audio::SoundId::UiTick);
        }
        say("THE " + thing->name + " IS NOT WORN. NO SLOT.");
        return;
    }
    // Which slot holds it now, or -1 -- the Grimoire's own cycle: NONE,
    // 1..10 and round. Moving it clears the slot it leaves.
    std::int32_t current = -1;
    for (std::int32_t slot = 0; slot < sim::Tavern::kQuickSlotCount; ++slot) {
        if (tavern_->slotItemIndex(slot) == row->item) {
            current = slot;
            break;
        }
    }
    const std::int32_t positions = sim::Tavern::kQuickSlotCount + 1;
    const std::int32_t next = ((current + 1 + delta) % positions + positions) % positions - 1;
    if (current >= 0) {
        tavern_->clearSlot(current);
    }
    if (next >= 0) {
        tavern_->bindItemToSlot(next, row->item);
    }
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::UiTick);
    }
    say(next >= 0 ? thing->name + " -- SLOT " + std::to_string(next + 1) + "."
                  : thing->name + " -- NO SLOT.");
}

// ---------------------------------------------------------------------------
// THE PACK (OBLIVION-PLAN phase 8)
// ---------------------------------------------------------------------------

std::vector<Session::PackRow> Session::packRows() const {
    std::vector<PackRow> rows;
    const sim::ItemRegistry& items = tavern_->items();
    const sim::Kit& kit = tavern_->kit();
    // WORN: all seven slots in the screen's order (PHASE 9 -- HAND, OFF-HAND,
    // HEAD, BODY, HANDS, FEET, TRINKET), the thing in it or a bare slot. A
    // bare slot is a row the cursor can stand on and read what it is for.
    PackRow worn;
    worn.kind = PackRow::Kind::Header;
    worn.text = "WORN";
    rows.push_back(worn);
    for (const sim::ItemSlot slot : sim::kSlotScreenOrder) {
        PackRow row;
        row.kind = PackRow::Kind::Slot;
        row.slot = slot;
        row.item = kit.worn(slot);
        row.count = row.item >= 0 ? 1 : 0;
        row.inKit = row.item >= 0;
        rows.push_back(std::move(row));
    }
    // CARRIED: the composed list (Kit, sack, bale, picks), less the unit that
    // is on the body, grouped by what a thing is and in registry order inside
    // a group -- a stable sort, so the order is the raws' and never a shop's.
    std::vector<PackRow> carried;
    for (const KitRow& kitRow : kitRows()) {
        PackRow row;
        row.kind = PackRow::Kind::Item;
        row.item = kitRow.item;
        row.count = kitRow.count - ((kitRow.inKit && kit.isWorn(kitRow.item)) ? 1 : 0);
        row.inKit = kitRow.inKit;
        if (row.count > 0) {
            carried.push_back(std::move(row));
        }
    }
    const auto categoryOf = [&items](const PackRow& row) {
        const sim::ItemDef* thing = items.at(row.item);
        return thing != nullptr ? static_cast<int>(sim::itemCategory(*thing)) : 0;
    };
    std::stable_sort(carried.begin(), carried.end(),
                     [&categoryOf](const PackRow& a, const PackRow& b) {
                         return categoryOf(a) < categoryOf(b);
                     });
    PackRow carriedHeader;
    carriedHeader.kind = PackRow::Kind::Header;
    carriedHeader.text = "CARRIED";
    rows.push_back(carriedHeader);
    int category = -1;
    for (PackRow& row : carried) {
        if (categoryOf(row) != category) {
            category = categoryOf(row);
            PackRow header;
            header.kind = PackRow::Kind::Header;
            header.text = std::string(
                sim::itemCategoryName(static_cast<sim::ItemCategory>(category)));
            rows.push_back(std::move(header));
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

std::vector<std::string> Session::packRowLabels() const {
    const sim::ItemRegistry& items = tavern_->items();
    const std::vector<PackRow> rows = packRows();
    std::vector<std::string> out;
    out.reserve(rows.size());
    // Widths read off the list first, the sheet's own two-pass rule, so every
    // "NNDR" starts under the last one. The worn rows' own column is
    // wornSlotLine's (the sheet prints the same seven strings).
    std::size_t prefixWidth = 0;
    std::size_t dramsWidth = 0;
    for (const PackRow& row : rows) {
        if (row.kind == PackRow::Kind::Item) {
            const sim::ItemDef* thing = items.at(row.item);
            if (thing == nullptr) {
                continue;
            }
            std::string prefix = thing->name;
            if (row.count > 1) {
                prefix += " x" + std::to_string(row.count);
            }
            prefixWidth = std::max(prefixWidth, prefix.size());
            std::int32_t drams = thing->drams * row.count;
            if (row.item == items.indexOf("bale")) {
                drams = tavern_->dialogue().crimes().baleUnits() *
                        sim::contrabandWeight(tavern_->dialogue().crimes().baleGood());
            }
            dramsWidth = std::max(dramsWidth, std::to_string(drams).size());
        }
    }
    for (const PackRow& row : rows) {
        if (row.kind == PackRow::Kind::Header) {
            out.push_back(row.text);
            continue;
        }
        if (row.kind == PackRow::Kind::Slot) {
            out.push_back(wornSlotLine(*tavern_, row.slot));
            continue;
        }
        const sim::ItemDef* thing = items.at(row.item);
        if (thing == nullptr) {
            out.emplace_back();
            continue;
        }
        std::string prefix = thing->name;
        if (row.count > 1) {
            prefix += " x" + std::to_string(row.count);
        }
        std::int32_t drams = thing->drams * row.count;
        if (row.item == items.indexOf("bale")) {
            drams = tavern_->dialogue().crimes().baleUnits() *
                    sim::contrabandWeight(tavern_->dialogue().crimes().baleGood());
        }
        std::string line = prefix;
        line.append(prefixWidth - prefix.size(), ' ');
        line += "  " + rightAlignedDigits(drams, dramsWidth) + "D";
        if (row.inKit && tavern_->kit().stolen(row.item) > 0) {
            line += "  THEIRS";
        }
        for (std::int32_t slot = 0; slot < sim::Tavern::kQuickSlotCount; ++slot) {
            if (tavern_->slotItemIndex(slot) == row.item) {
                line += "  SLOT " + std::to_string(slot + 1);
                break;
            }
        }
        if (thing->heat > 0) {
            line += "  HOT";
        }
        out.push_back(std::move(line));
    }
    return out;
}

void Session::settlePackCursor(int direction) {
    if (!characterPack_) {
        return;
    }
    const std::vector<PackRow> rows = packRows();
    if (rows.empty()) {
        return;
    }
    int cursor = std::clamp(characterCursor_, 0, static_cast<int>(rows.size()) - 1);
    const int step = direction < 0 ? -1 : 1;
    // Off a header, in the direction asked, and back the other way if the
    // list ends on one (it cannot, but a cursor must never rest on a header).
    for (int guard = 0; guard < static_cast<int>(rows.size()); ++guard) {
        if (rows[static_cast<std::size_t>(cursor)].kind != PackRow::Kind::Header) {
            break;
        }
        const int next = cursor + step;
        cursor = next < 0 || next >= static_cast<int>(rows.size()) ? cursor - step : next;
    }
    characterCursor_ = cursor;
    // The digit window the cursor's row sits in, counted over keyed rows.
    characterPage_ = packKeyedOrdinal(characterCursor_) / kTopicPageSize;
}

int Session::packKeyedOrdinal(int row) const {
    const std::vector<PackRow> rows = packRows();
    int ordinal = 0;
    for (int i = 0; i < row && i < static_cast<int>(rows.size()); ++i) {
        if (rows[static_cast<std::size_t>(i)].kind != PackRow::Kind::Header) {
            ++ordinal;
        }
    }
    return ordinal;
}

void Session::packPickVisible(int slot) {
    const std::vector<PackRow> rows = packRows();
    const int wanted = characterPage_ * kTopicPageSize + slot;
    int ordinal = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].kind == PackRow::Kind::Header) {
            continue;
        }
        if (ordinal == wanted) {
            characterCursor_ = static_cast<int>(i);
            return;
        }
        ++ordinal;
    }
}

void Session::packPageNext() {
    const std::vector<PackRow> rows = packRows();
    int keyed = 0;
    for (const PackRow& row : rows) {
        if (row.kind != PackRow::Kind::Header) {
            ++keyed;
        }
    }
    if (keyed <= 0) {
        return;
    }
    const int pages = (keyed + kTopicPageSize - 1) / kTopicPageSize;
    characterPage_ = (characterPage_ + 1) % pages;
    packPickVisible(0);
    settlePackCursor(1);
}

void Session::toggleCharacterPack() {
    characterPack_ = !characterPack_;
    characterCursor_ = 0;
    characterPage_ = 0;
    settlePackCursor(1);
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::BookFlip);
    }
}

InspectPane Session::inspectPaneView() const {
    InspectPane pane;
    if (!characterPack_) {
        return pane;
    }
    const std::vector<PackRow> rows = packRows();
    if (characterCursor_ < 0 || static_cast<std::size_t>(characterCursor_) >= rows.size()) {
        return pane;
    }
    const PackRow& at = rows[static_cast<std::size_t>(characterCursor_)];
    const sim::ItemRegistry& items = tavern_->items();
    const sim::Kit& kit = tavern_->kit();
    const InputDevice dev = promptDevice_;
    pane.open = true;
    const auto digits = [](std::int32_t value) { return std::to_string(value); };

    const sim::ItemDef* thing = items.at(at.item);
    if (at.kind == PackRow::Kind::Header || (at.kind == PackRow::Kind::Slot && thing == nullptr)) {
        // A group or a bare slot: what it is for, and the sums over it.
        if (at.kind == PackRow::Kind::Slot) {
            pane.title = std::string(sim::itemSlotSheetLabel(at.slot));
            pane.kind = "NOTHING WORN";
            if (at.slot == sim::ItemSlot::Hand) {
                pane.title = "BARE HANDS";
                pane.kind = "IN HAND";
                pane.prose = "NOTHING IN THE HAND BUT THE HAND.";
                pane.facts.push_back({"DAMAGE", sim::weaponSheetLine(tavern_->playerWeapon())});
            } else if (const sim::ItemDef* held = items.at(kit.worn(sim::ItemSlot::Hand));
                       at.slot == sim::ItemSlot::OffHand && held != nullptr &&
                       held->grip == sim::WeaponGrip::TwoHanded) {
                // PHASE 9: the other hand is not empty, it is on the haft.
                pane.prose = "BOTH HANDS ARE ON THE " + held->name + ". A SHIELD TAKES IT OFF.";
            } else {
                pane.prose = "NOTHING WORN HERE. A PIECE FOR THIS SLOT TURNS A BLOW.";
            }
        } else {
            pane.title = at.text;
            pane.kind = "";
            pane.prose = at.text == "WORN"
                             ? "WHAT IS ON YOUR BODY. IT TURNS BLOWS AND IT FILLS THE HAND."
                             : "WHAT YOU CARRY. EVERY DRAM COUNTS AGAINST THE LOAD.";
            pane.facts.push_back({"DR WORN", digits(tavern_->wornDr())});
            pane.facts.push_back({"LOAD", digits(tavern_->loadDrams()) + " / " +
                                              digits(tavern_->loadBudget()) + " DRAMS"});
        }
        return pane;
    }
    if (thing == nullptr) {
        pane.open = false;
        return pane;
    }
    const std::int32_t carriedUnits =
        at.kind == PackRow::Kind::Slot ? kit.count(at.item) : at.count;
    const bool worn = at.kind == PackRow::Kind::Slot;
    const std::int32_t lifted = at.inKit ? kit.stolen(at.item) : 0;
    const sim::ItemInspect record = sim::inspectItem(*thing, carriedUnits, worn, lifted);
    pane.title = record.name;
    pane.kind = std::string(sim::itemCategoryName(record.category));
    if (worn) {
        pane.kind += std::string("  ") + std::string(sim::itemSlotSheetLabel(thing->slot));
    }
    if (record.stolen > 0) {
        pane.alert = "THEIRS - LIFTED OFF SOMEBODY";
        if (record.count > 1) {
            pane.alert += ", " + digits(record.stolen) + " OF " + digits(record.count);
        }
    }
    pane.prose = record.desc;
    if (record.count > 1 || (at.kind == PackRow::Kind::Item && record.count > 0)) {
        pane.facts.push_back({"CARRIED", "x" + digits(record.count)});
    }
    std::int32_t unitDrams = record.unitDrams;
    if (at.item == items.indexOf("bale")) {
        unitDrams = tavern_->dialogue().crimes().baleUnits() *
                    sim::contrabandWeight(tavern_->dialogue().crimes().baleGood());
    }
    // DRAMS IN FULL: this pane also prints DR for what a piece turns, and
    // "80 DR" beside "DR 2 -> 4" reads as armour. The LOAD line's own word.
    std::string weight = digits(unitDrams) + " DRAMS";
    if (record.count > 1) {
        weight += "  (" + digits(unitDrams * record.count) + " FOR " + digits(record.count) + ")";
    }
    pane.facts.push_back({"WEIGHT", std::move(weight)});
    if (record.unitRoyals > 0) {
        std::string worth = digits(record.unitRoyals) + " C";
        if (record.count > 1) {
            worth += "  (" + digits(record.unitRoyals * record.count) + " FOR " +
                     digits(record.count) + ")";
        }
        pane.facts.push_back({"WORTH", std::move(worth)});
    }
    if (record.damageHi > 0) {
        pane.facts.push_back(
            {"DAMAGE", digits(record.damageLo) + "-" + digits(record.damageHi) + " " +
                           record.damageWord});
    }
    // PHASE 9: a two-hander says so before it is worn -- it is why the
    // shield row goes bare (the comparison line names what comes off).
    if (thing->slot == sim::ItemSlot::Hand && record.grip == sim::WeaponGrip::TwoHanded) {
        pane.facts.push_back({"GRIP", "TWO-HANDED"});
    }
    if (record.dr > 0) {
        pane.facts.push_back({"ARMOUR", "TURNS " + digits(record.dr) + " A BLOW"});
    }
    if (!record.useLine.empty()) {
        pane.facts.push_back({"EFFECT", record.useLine});
    }
    if (record.heat > 0) {
        pane.facts.push_back({"HEAT", "+" + digits(record.heat) + " WITH THE WATCH, EACH"});
    }
    if (at.inKit) {
        for (std::int32_t slot = 0; slot < sim::Tavern::kQuickSlotCount; ++slot) {
            if (tavern_->slotItemIndex(slot) == at.item) {
                pane.facts.push_back({"QUICK SLOT", digits(slot + 1)});
                break;
            }
        }
    }
    // PHASE 9 -- WHAT THE PRESS WOULD CHANGE, for a carried thing that goes
    // on the body: the sim's own comparison (compareGear), printed and never
    // recomputed. A worn row compares with nothing (it IS what is worn).
    if (at.kind == PackRow::Kind::Item && at.inKit && thing->slot != sim::ItemSlot::None &&
        !thing->fixed) {
        gearCompareLines(tavern_->compareGear(at.item), *tavern_, pane);
    }
    // THE KEYS, on the foot -- the same presses the list takes.
    if (at.inKit) {
        const std::string confirm(promptConfirmKey(dev));
        if (thing->slot != sim::ItemSlot::None && !thing->fixed) {
            pane.verbs.push_back({confirm, worn ? "BARE" : "WEAR"});
            pane.verbs.push_back({dev == InputDevice::Pad ? std::string(1, kMotifDPad)
                                                          : std::string{kMotifLeft, kMotifRight},
                                  "SLOT"});
        } else if (!thing->effects.empty()) {
            pane.verbs.push_back({confirm, thing->useVerb});
        }
        pane.verbs.push_back({"X", "DROP"});
    }
    return pane;
}

std::vector<std::string> Session::mapRows() const {
    // THREE SECTIONS, EACH BUILT FROM WHAT THE CASEBOOK ALREADY KNOWS -- see
    // toggleMap's own header on why nothing here is new state. All three grow
    // by the identical act: hearing a lead. There is no separate "have you
    // been here" or "have you met them" flag to invent, forget to update, or
    // let drift from the trail itself.
    std::vector<std::string> rows;
    // COURIER CASE. The map's three sections follow the live errand, the one
    // active-case rule again -- see casebookPageState.
    const sim::CasebookRaws& raws = activeCaseRaws();
    const sim::Casebook& book = activeCasebook();
    if (!book.active()) {
        return rows;
    }
    const std::vector<std::int32_t> heard = book.known();
    const std::int32_t px = body_->tileX();
    const std::int32_t py = body_->tileY();

    // -- known ground: the named places a heard lead has put in the book,
    // once each, in the order they were first heard of. The casebook's own
    // `place` field, authored, never derived -- these are the district's
    // named sites and the street table (docks::kPlaces) only knows the
    // streets between them.
    std::vector<std::string_view> places;
    for (const std::int32_t index : heard) {
        const std::string_view place = raws.leads()[static_cast<std::size_t>(index)].place;
        if (std::find(places.begin(), places.end(), place) == places.end()) {
            places.push_back(place);
        }
    }
    for (const std::string_view place : places) {
        // "- ", not "@ " -- the 4x6 font carries no glyph for '@' (hud.cpp's
        // own table), and a hole punched through a place name by a symbol
        // nobody chose to support is exactly the class of bug the standing
        // quality bar exists to catch. See test_map.cpp's own copy case.
        rows.push_back("- " + std::string(place));
    }

    // -- open leads: heard, not yet looked at, each with a bearing from HERE.
    // The one thing this page has that the casebook's own list does not: the
    // casebook prints WHAT you have heard, unplaced; this prints WHERE it is
    // from where you are standing right now.
    for (const std::int32_t index : heard) {
        if (book.state(index) != sim::LeadState::Open) {
            continue;
        }
        const sim::Lead& lead = raws.leads()[static_cast<std::size_t>(index)];
        rows.push_back(bearingLabel(px, py, lead.site.x, lead.site.y) + "  " +
                       (lead.brief.empty() ? lead.place : lead.brief));
    }

    // -- who will talk: every named person a heard lead has pointed you at,
    // once each, who actually answers to somebody -- a wastrel answers to
    // nobody, which is factions.json's own rule (see notableJobFamily) and
    // not a gap in this list.
    const sim::NotableRegistry& notables = tavern_->dialogue().notables();
    const sim::FactionRegistry& factions = tavern_->dialogue().factions();
    std::vector<std::string_view> named;
    for (const std::int32_t index : heard) {
        const std::string_view who = raws.leads()[static_cast<std::size_t>(index)].who;
        if (who.empty() || std::find(named.begin(), named.end(), who) != named.end()) {
            continue;
        }
        named.push_back(who);
        const sim::Notable* notable = notables.find(who);
        if (notable == nullptr) {
            continue;
        }
        const sim::Faction* faction =
            factions.at(factions.factionForJobPrefix(notableJobFamily(notable->type)));
        if (faction == nullptr) {
            continue;
        }
        rows.push_back("! " + notable->name + "  " + upperAscii(faction->id));
    }
    return rows;
}

}  // namespace granadad::render
