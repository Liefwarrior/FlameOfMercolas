// #82. THE BLOODLETTER TRAIL'S OWN LETTERS.
//
//   RAWS    the owner's bloodletter_letters.json on its own -- that it
//           loads, that every field a player would read is a sentence and
//           not a hole, and that it never sorts what it was authored in.
//   CROSS   every `lead` a letter names is checked against the REAL
//           casebook.json, loaded independently -- the same cross-file
//           discipline test_casebook.cpp already holds `opens` to.
//   SCOPE   the Wielder's own closing dispatch never claims a discovery
//           only the unbuilt undercellar delve would have made.

#include <doctest/doctest.h>

#include <algorithm>
#include <set>
#include <string>

#include "granadad/content/content_dir.hpp"
#include "granadad/sim/casebook.hpp"
#include "granadad/sim/letters.hpp"

using namespace granadad::sim;
namespace content = granadad::content;

namespace {

[[nodiscard]] const LetterRaws& raws() {
    static const LetterRaws loaded = LetterRaws::load(content::contentDir());
    return loaded;
}

[[nodiscard]] const CasebookRaws& casebookRaws() {
    static const CasebookRaws loaded = CasebookRaws::load(content::contentDir());
    return loaded;
}

/// The same bar test_companions.cpp's own checkReadableProse holds
/// companion sheets to, and test_character.cpp holds session.cpp's own
/// strings to: no control characters, and no raw id leaking through where a
/// sentence was meant -- the exact bug class this build's standing quality
/// bar exists to catch.
void checkReadableProse(const std::string& text) {
    INFO("text: ", text);
    CHECK_FALSE(text.empty());
    for (const char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        CHECK(u >= 0x20);
        CHECK(u < 0x7F);
    }
    CHECK(text.find('_') == std::string::npos);
}

}  // namespace

// ===========================================================================
// RAWS
// ===========================================================================

TEST_CASE("the letters file loads, and every field a player would read is a sentence") {
    const LetterRaws& file = raws();
    REQUIRE(file.loaded());
    CHECK(file.letters().size() >= 4);

    std::set<std::string> ids;
    for (const Letter& letter : file.letters()) {
        INFO("letter " << letter.id);
        CHECK_FALSE(letter.id.empty());
        CHECK(ids.insert(letter.id).second);
        CHECK_FALSE(letter.lead.empty());
        CHECK_FALSE(letter.from.empty());
        CHECK_FALSE(letter.to.empty());
        CHECK_FALSE(letter.dateline.empty());
        // A memo may open straight into its business (Crell's), so
        // salutation alone is allowed to be empty -- everything else that a
        // player would actually read may not be.
        checkReadableProse(letter.dateline);
        checkReadableProse(letter.from);
        checkReadableProse(letter.to);
        if (!letter.salutation.empty()) {
            checkReadableProse(letter.salutation);
        }
        CHECK_FALSE(letter.closing.empty());
        checkReadableProse(letter.closing);
        CHECK_FALSE(letter.signature.empty());
        checkReadableProse(letter.signature);

        // A LETTER HAS SHAPE. One paragraph is a note; a letter earns its
        // name by having more than one thing to say.
        CHECK(letter.body.size() >= 2);
        for (const std::string& paragraph : letter.body) {
            checkReadableProse(paragraph);
            // Sane upper bound -- not a rendering budget (nothing here draws
            // to the tight top band yet, see letters.hpp's own header), just
            // a guard against a paragraph that is actually a whole letter
            // pasted into one array entry by mistake.
            CHECK(paragraph.size() < 600);
        }
    }
}

TEST_CASE("indexOf finds a real letter and says -1 for one nobody wrote") {
    const LetterRaws& file = raws();
    REQUIRE(file.loaded());
    const std::int32_t first = file.indexOf(file.letters().front().id);
    CHECK(first == 0);
    CHECK(file.indexOf("nobody-wrote-this") == -1);
}

TEST_CASE("forLead keeps authored order, and a lead with no letter answers empty") {
    const LetterRaws& file = raws();
    REQUIRE(file.loaded());

    // MAELL'S THREE COME BACK IN THE ORDER HE WROTE THEM. LetterRaws never
    // sorts (letters.hpp's own header), so this is authored order, checked
    // rather than assumed.
    const std::vector<std::int32_t> maell = file.forLead("mission-flagstones");
    REQUIRE(maell.size() == 3);
    for (std::size_t i = 0; i < maell.size(); ++i) {
        INFO("position " << i);
        CHECK(file.letters()[static_cast<std::size_t>(maell[i])].id ==
              "maell-letter-" + std::to_string(i + 1));
    }

    // A lead nobody wrote a letter about (this build authored five letters
    // against a twelve-lead trail on purpose -- see the content file's own
    // `notes` field) answers empty rather than a bogus match.
    CHECK(file.forLead("the-outfall").empty());
    CHECK(file.forLead("nobody-wrote-this-lead").empty());
}

TEST_CASE("a missing letters file answers loaded() == false, not a crash") {
    const LetterRaws missing = LetterRaws::load(std::filesystem::path("does") / "not" / "exist");
    CHECK_FALSE(missing.loaded());
    CHECK(missing.letters().empty());
    CHECK(missing.indexOf("anything") == -1);
    CHECK(missing.forLead("anything").empty());
}

// ===========================================================================
// CROSS
// ===========================================================================

TEST_CASE("every letter's lead is a real lead in the owner's own casebook.json") {
    // THE CLAIM THIS FILE MAKES AND casebook.json NEVER HEARS ABOUT. Two
    // files, loaded independently, cross-checked here -- the same discipline
    // test_casebook.cpp already holds a lead's own `opens` list to.
    const LetterRaws& letters = raws();
    const CasebookRaws& casebook = casebookRaws();
    REQUIRE(letters.loaded());
    REQUIRE(casebook.loaded());

    for (const Letter& letter : letters.letters()) {
        INFO("letter " << letter.id << " -> lead " << letter.lead);
        CHECK(casebook.indexOf(letter.lead) >= 0);
    }
}

TEST_CASE("the Wielder's own dispatch is tied to the trail's own closing lead") {
    const LetterRaws& letters = raws();
    const CasebookRaws& casebook = casebookRaws();
    REQUIRE(letters.loaded());
    REQUIRE(casebook.loaded());

    const std::int32_t dispatch = letters.indexOf("gabri-dispatch");
    REQUIRE(dispatch >= 0);
    const Letter& letter = letters.letters()[static_cast<std::size_t>(dispatch)];
    const std::int32_t lead = casebook.indexOf(letter.lead);
    REQUIRE(lead >= 0);
    // DOCKS-GAZETTEER.md section 6's own beat is "letter to Minister John"
    // AFTER the surface trail closes -- and the trail's own `close` flag is
    // exactly the fact that names which lead that is.
    CHECK(casebook.leads()[static_cast<std::size_t>(lead)].close);
}

// ===========================================================================
// SCOPE
// ===========================================================================

TEST_CASE("the Wielder's dispatch never claims a discovery only the unbuilt delve would make") {
    // DOCKS-GAZETTEER.md section 6's L1-L3 undercellar is design
    // documentation only -- nothing in this build simulates it, and "the
    // mark of the Y'marr" is that dungeon's own bottom beat, not this
    // build's surface trail's. A letter that named it would be authoring a
    // discovery the game itself cannot back up.
    const LetterRaws& letters = raws();
    REQUIRE(letters.loaded());
    const std::int32_t dispatch = letters.indexOf("gabri-dispatch");
    REQUIRE(dispatch >= 0);
    const Letter& letter = letters.letters()[static_cast<std::size_t>(dispatch)];
    for (const std::string& paragraph : letter.body) {
        INFO("paragraph: " << paragraph);
        CHECK(paragraph.find("Y'marr") == std::string::npos);
        CHECK(paragraph.find("Ymarr") == std::string::npos);
    }
}

TEST_CASE("no letter says the count of magic systems out loud") {
    // MAGIC-CANON.md section 5.4's hard content rule: any in-world text a
    // player can read says six, never seven, and never hints there is a
    // secret to hint at. None of these five letters needed to talk about
    // magic systems at all -- checked here rather than left to luck.
    const LetterRaws& file = raws();
    REQUIRE(file.loaded());
    for (const Letter& letter : file.letters()) {
        for (const std::string& paragraph : letter.body) {
            INFO("letter " << letter.id << ": " << paragraph);
            CHECK(paragraph.find("seven") == std::string::npos);
            CHECK(paragraph.find("Seven") == std::string::npos);
        }
    }
}
