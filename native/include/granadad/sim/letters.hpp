#pragma once

// #82. REAL LETTERS FOR THE BLOODLETTER TRAIL.
//
// content/raws/quests/casebook.json (S10, unedited by this file) tells the
// player ABOUT documents it never actually shows: Father Maell's three
// unanswered letters to the monastery are a sentence in a lead's `detail`
// field, not something a player ever reads. This file is what a player
// actually reads -- the letters themselves, in the hand of whoever wrote
// them, as their own content file.
//
// WHY A SECOND FILE AND NOT AN EDIT TO casebook.json
//
// content/ is the owner's authored canon; casebook.json shipped complete in
// S10 and this build never edits a shipped content file, the same rule
// flame_disciple.json and flame_barks.json were built under in S4 (see
// either file's own `notes` field). content/raws/quests/bloodletter_letters.json
// is a THIRD file in that directory, cross-referenced to casebook.json's own
// lead ids by string rather than merged into its schema.
//
// WHAT IS AUTHORED
//
// Five documents. Three are Father Maell's own retained drafts of the
// letters DOCKS-GAZETTEER.md section 4.3 and this build's own
// flame_disciple.json already establish he sent -- a parish priest keeping a
// fair copy of what he posted is the ordinary, unremarkable practice that
// lets these exist as physical pages a player can find at all, and their
// escalating, unanswered arc is written to cost nothing new: every fact in
// them (the warm water on the ebb, Wake's boat not holding a line, the
// Drowned-Name Wall getting ahead of the bodies) is already authored
// elsewhere in this build's raws and barks. One is Harbormaster Crell's own
// covering memo on the struck ledger line -- notables.json's own bio for him
// ("he will show the Wielder even that -- while making him ask for it item
// by item") written out as the memo that bio implies exists. The last is the
// Wielder's own dispatch to Minister John at the close of the surface trail,
// which is DOCKS-GAZETTEER.md section 6's own named beat ("Report to Father
// Maell -> letter to Minister John -> MVP narrative close, sequel hook
// armed") given its actual words for the first time.
//
// WHAT IS DELIBERATELY NOT CLAIMED
//
// The Wielder's dispatch reports exactly what the SURFACE trail (this
// build's own casebook.json, ending at the Drowned Hold) has found: a second
// body, a breach into an unmapped cellar, and the trail's own closing
// thesis. It does not name "the Y'marr" or anything DOCKS-GAZETTEER.md
// section 6's L1-L3 undercellar delve would find -- that dungeon is design
// documentation only and nothing in this build simulates it, so a letter
// claiming its discoveries would be authoring content the game cannot back
// up. test_letters.cpp checks this restraint directly.
//
// NO FLOATS, NO UNORDERED CONTAINERS, same as every other raws loader in
// this build. Letters carry no simulation state of their own -- nothing
// here is hashed into the twin-run gate, for the same reason barks.hpp's own
// header gives for its own table file: reading a letter cannot touch the
// RNG stream or branch the simulation, so two runs that disagree about
// whether a letter was read still agree about the world.
//
// VERIFICATION GAP (S-letters): this loader is proven against the real
// content directory and cross-referenced against the real casebook.json by
// test_letters.cpp, but no in-game screen draws a Letter yet.
// dialogue_view.cpp's top band (session.cpp's `view.line`) is sized for a
// sentence or two -- see its own wrapText/maxSpeechRows comments -- and
// cramming a five-paragraph letter through it would silently clip, the
// exact bug class this build's own quality bar exists to catch. A real
// parchment-style reading screen with its own paging is the natural
// follow-up rendering task; this WRITING task authored the letters and the
// loader, not that screen.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace granadad::sim {

/// One authored document. Every field is prose a player reads; none of it is
/// computed. Loader-folded to plain ASCII the same way every other raws
/// string in this build is (letters.cpp's own stringField).
struct Letter {
    std::string id;
    /// casebook.json's own lead id -- content/raws/quests/casebook.json,
    /// unedited -- this letter is read alongside. Never validated against the
    /// real trail by this struct itself; test_letters.cpp cross-loads both
    /// files and checks every one resolves.
    std::string lead;
    /// Who wrote it, in their own name.
    std::string from;
    /// Who it was written to, or who it was handed to -- Crell's memo is
    /// never posted anywhere, so this reads "handed to" rather than
    /// "addressed to" for that one entry.
    std::string to;
    /// Where it was written, with no invented calendar date -- this build's
    /// raws and barks never date anything more precisely than "three
    /// nights" or "this month" (grep content/raws and content/barks for
    /// yourself), so a dateline here follows the same house style: place,
    /// not a day this world has never named.
    std::string dateline;
    /// The opening line, e.g. "Minister John,". Empty is legal for a memo
    /// that opens straight into its business, which is exactly Crell's.
    std::string salutation;
    /// The body, one paragraph per entry, in authored order. Never one long
    /// string: a letter is a shape on the page as much as it is words, and a
    /// future reading screen will want the paragraph breaks a single blob
    /// would throw away.
    std::vector<std::string> body;
    /// The valediction, e.g. "In the Flame's service,". Kept separate from
    /// `signature` because some hands (Gabri's) sign directly under a
    /// closing sentence that is not a valediction at all.
    std::string closing;
    std::string signature;
    /// COURIER CASE. A HANDED document is one somebody put in the player's
    /// own hand, so it is readable the moment its lead is merely HEARD --
    /// state Open counts -- where an ordinary letter keeps the read-not-
    /// received gate (Cold or Followed only; see Session::unlockedLetters).
    /// False for every letter of the Bloodletter file, whose five documents
    /// are all paper the leads keep rather than post the player was sent.
    bool handed = false;
};

/// The authored file, loaded. NEVER throws and never refuses to boot,
/// exactly CasebookRaws's own contract: a missing or malformed letters file
/// leaves an empty list and the game still runs.
class LetterRaws {
public:
    [[nodiscard]] static LetterRaws load(const std::filesystem::path& contentDir);
    /// COURIER CASE. The same parse over an exact file path -- load() above is
    /// this with letterRawsPath() filled in. A second document file is a
    /// second FILE, bloodletter_letters.json byte-untouched, per the
    /// file-per-content-block law this header states.
    [[nodiscard]] static LetterRaws loadFile(const std::filesystem::path& file);

    [[nodiscard]] bool loaded() const noexcept { return !letters_.empty(); }
    [[nodiscard]] const std::vector<Letter>& letters() const noexcept { return letters_; }

    /// Index of a letter by id, or -1.
    [[nodiscard]] std::int32_t indexOf(std::string_view id) const noexcept;
    /// Every letter tied to one lead, in authored order -- Maell's three
    /// come back in the order he wrote them because that is the order this
    /// file lists them in and nothing here ever sorts.
    [[nodiscard]] std::vector<std::int32_t> forLead(std::string_view leadId) const;

private:
    std::vector<Letter> letters_;
};

[[nodiscard]] std::filesystem::path letterRawsPath(const std::filesystem::path& contentDir);
/// content/raws/quests/mission_sheet_letters.json -- the courier case's own
/// paper (top level "letters", same shape as the Bloodletter file, plus the
/// per-letter `handed` flag above).
[[nodiscard]] std::filesystem::path missionSheetLetterRawsPath(
    const std::filesystem::path& contentDir);

}  // namespace granadad::sim
