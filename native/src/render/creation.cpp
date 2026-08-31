#include "granadad/render/creation.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "granadad/render/creation_page.hpp"
#include "granadad/render/framebuffer.hpp"
#include "granadad/render/panel.hpp"
#include "granadad/sim/chargen_raws.hpp"
#include "granadad/sim/faction.hpp"
#include "granadad/sim/notables.hpp"

namespace granadad::render {

namespace {

// THE WARM POOL AND THE BRONZE BEZEL ARE GONE, AND THAT IS THE POINT.
//
// drawVoidAnchor() lived here: a soft radial light pool and a thin bezel,
// painted by hand to stop the middle of this screen reading as an uncleared
// buffer. It was the right answer to the screen that existed then -- a top band,
// a bottom grid, and seventy per cent of the frame with nothing designed for it.
// There is no such middle any more. The composed frame (render/creation_page.cpp)
// paints a bordered, ruled, textured ground over the whole window, which is what
// the pool was standing in for, and drawing both would have been two different
// ideas about what the backdrop is arguing with each other.
//
// The stipple in the detail pane is the surviving half of that instinct, and it
// is the reference's own: emptiness inside a frame is TEXTURED, not blank.

/// "PRI"/"MAJ"/"MIN"/"-" rather than sim::skillDesignationName's full
/// "Primary"/"Major"/"Minor"/"Undesignated" -- DISPLAY ONLY, this build's
/// own reasoning for the row, chargen.cpp's own words untouched everywhere
/// else. The topic grid's own column is eighteen glyphs INCLUDING the row
/// number (dialogue_view.cpp), so "CRACKSMANSHIP" (thirteen, the longest
/// skill name this raws file has) paired with "Undesignated" (twelve) was
/// never going to fit -- paired with a three-letter code it does, for every
/// skill this build's raws currently name. The one row still too long for
/// its column (a skill name AND a code that together still clear eighteen)
/// falls back on the exact same rescue every other page's overlong row
/// already has -- clipLabel's own word-boundary cut, and the cursor's own
/// row spelled out in full one line up -- rather than a promise this
/// function cannot keep for every skill this raws file will ever name.
/// "THE HAND" -> "HAND": the meter row's own display shortening, the doc's
/// quiz mock's own labels ("HAND  ###....") -- the full authored name stays
/// everywhere else the axes speak. DISPLAY ONLY, like shortDesignation below.
[[nodiscard]] std::string meterName(const std::string& axisName) {
    if (axisName.rfind("THE ", 0) == 0) {
        return axisName.substr(4);
    }
    return axisName;
}

[[nodiscard]] std::string_view shortDesignation(sim::SkillDesignation tier) noexcept {
    switch (tier) {
        case sim::SkillDesignation::Primary:
            return "PRI";
        case sim::SkillDesignation::Major:
            return "MAJ";
        case sim::SkillDesignation::Minor:
            return "MIN";
        case sim::SkillDesignation::None:
            return "-";
    }
    return "-";
}

}  // namespace

const std::vector<OriginTemplate>& originTemplates() {
    // FIVE, IN THE DOC MOCK'S OWN ORDER -- docs/design/CHARGEN-DAGGERFALL-
    // DRAFT.md section 6 (RULING 2): the Daggerfall flow's three doors on
    // top, then GABRI and DEVIN as quick starts. The door tags are the
    // mock's own right-hand descriptions, verbatim; the quick starts keep
    // Eli's own task #80 words. CUSTOM keeps its index (2), which is the
    // one every existing capture flag and test walked to.
    static const std::vector<OriginTemplate> kTemplates = {
        {"calling", "TAKE A CALLING", "THE WARD'S NINE TRADES, PICKED BY EYE"},
        {"quiz", "ANSWER FOR YOURSELF", "TEN QUESTIONS, AND BE TOLD WHAT YOU ARE"},
        {"custom", "WALK YOUR OWN PATH", "EVERY SKILL, EVERY POINT, AND THE DAGGER"},
        {"gabri", "GABRI", "NO-NONSENSE"},
        {"devin", "DEVIN", "SECRETIVE"},
    };
    return kTemplates;
}

std::array<int, 3> quizDisplayOrder(int questionIndex) noexcept {
    // All six permutations of three rows, cycled by question index -- see
    // the header on why this is a fixed table and not a draw. Question 0
    // shows the authored order, which is also what the doc's own mock shows.
    static constexpr std::array<std::array<int, 3>, 6> kPerms = {{
        {0, 1, 2},
        {1, 2, 0},
        {2, 0, 1},
        {0, 2, 1},
        {2, 1, 0},
        {1, 0, 2},
    }};
    return kPerms[static_cast<std::size_t>(((questionIndex % 6) + 6) % 6)];
}

CreationFlow::CreationFlow(const std::filesystem::path& contentDir)
    : skills_(sim::SkillTrack::load(contentDir)),
      callings_(sim::CallingRegistry::load(contentDir, skills_)),
      quiz_(sim::ChargenQuiz::load(contentDir, callings_)),
      // The biography's targets resolve against the SAME registries every other
      // system reads (chargen_raws.hpp's own stance). They used to be discarded
      // the moment the biography validated against them; they are KEPT now
      // because the biography screen names what an answer costs, and an effect
      // naming "watch_docks" at a player instead of "THE HARBOUR WATCH" would be
      // showing them a machine's name for something.
      factions_(sim::FactionRegistry::load(contentDir)),
      notables_(sim::NotableRegistry::load(contentDir)),
      biography_(sim::BiographyRegistry::load(contentDir, skills_, factions_, notables_)),
      devinTemplate_(sim::CompanionTemplate::load(contentDir, "devin")),
      gabriTemplate_(sim::CompanionTemplate::load(contentDir, "gabri")) {
    loadOriginDefaultName();
}

void CreationFlow::moveOriginCursor(int delta) noexcept {
    const int count = static_cast<int>(originTemplates().size());
    originCursor_ = ((originCursor_ + delta) % count + count) % count;
}

// A POINTER DOES NOT ARRIVE AS A DELTA. move*Cursor() ring round because a
// keyboard walking off the end of a five-row list wants the other end; a mouse
// that slid off the end of a list has not asked to be teleported there, so
// these clamp. Hover and click both come through here, which is what makes the
// hover MIRROR the one cursor every input shares rather than run a second
// highlight beside it.
void CreationFlow::setOriginCursor(int row) noexcept {
    const int count = static_cast<int>(originTemplates().size());
    originCursor_ = std::clamp(row, 0, count - 1);
}

void CreationFlow::setChoiceCursor(int row) noexcept {
    const int count = choiceRowCount();
    if (count <= 0) {
        return;
    }
    choiceCursor_ = std::clamp(row, 0, count - 1);
}

void CreationFlow::setCustomizeCursor(int row) noexcept {
    if (editingName_) {
        // Typing owns the input, exactly as moveCustomizeCursor already holds.
        return;
    }
    const int count = static_cast<int>(customizeRowModel().size());
    if (count <= 0) {
        return;
    }
    customizeCursor_ = std::clamp(row, 0, count - 1);
}

sim::ChargenEffects CreationFlow::effectsSoFar() const {
    if (bioDone_) {
        return effects_;
    }
    sim::ChargenEffects out;
    const std::vector<sim::BiographyQuestion>& questions = biography_.questions();
    for (std::size_t i = 0; i < bioAnswers_.size() && i < questions.size(); ++i) {
        const std::int32_t chosen = bioAnswers_[i];
        if (chosen < 0 || chosen >= static_cast<std::int32_t>(questions[i].answers.size())) {
            continue;
        }
        sim::accumulateEffects(out, questions[i].answers[static_cast<std::size_t>(chosen)].effects);
    }
    return out;
}

const sim::CompanionTemplate* CreationFlow::chosenCompanion() const noexcept {
    const std::string& id = chosenOrigin().id;
    if (id == "devin" && devinTemplate_.loaded()) {
        return &devinTemplate_;
    }
    if (id == "gabri" && gabriTemplate_.loaded()) {
        return &gabriTemplate_;
    }
    return nullptr;
}

void CreationFlow::loadOriginDefaultName() noexcept {
    if (!nameIsDefault_) {
        return;
    }
    const std::string& id = chosenOrigin().id;
    // The doors' "name" names the PATH, not the character -- handing "TAKE A
    // CALLING" over as a suggested name would read as a placeholder nobody
    // wrote a real one for, which is a worse first impression than an
    // honestly blank field. Only the two quick starts have a name to offer.
    if (id == "devin" || id == "gabri") {
        name_ = chosenOrigin().name;
    } else {
        name_.clear();
    }
    nameIsDefault_ = true;
}

void CreationFlow::chooseOrigin() noexcept {
    loadOriginDefaultName();
    customizeCursor_ = 0;
    choiceCursor_ = 0;
    editingName_ = false;
    const std::string& id = chosenOrigin().id;
    if (id == "calling" && callings_.loaded()) {
        step_ = CreationStep::Calling;
        return;
    }
    if (id == "quiz" && callings_.loaded() && quiz_.loaded()) {
        // A fresh entry is a fresh quiz -- backing all the way out and
        // walking in again should not resume half an interrogation.
        quizAnswers_.clear();
        verdict_.reset();
        step_ = CreationStep::Quiz;
        return;
    }
    // CUSTOM, the two quick starts -- and either Daggerfall door whose
    // content could not be read, which falls through to the plain sheet
    // rather than a roster of nothing (the header's must-still-boot rule).
    step_ = CreationStep::Customize;
}

const OriginTemplate& CreationFlow::chosenOrigin() const noexcept {
    return originTemplates()[static_cast<std::size_t>(originCursor_)];
}

// ---------------------------------------------------------------------------
// the Daggerfall doors: roster, quiz, verdict, biography
// ---------------------------------------------------------------------------

int CreationFlow::choiceRowCount() const noexcept {
    switch (step_) {
        case CreationStep::Calling:
            return static_cast<int>(callings_.callings().size());
        case CreationStep::Quiz:
            // The verdict card's two rows (take it / another trade), or a
            // question's three answers -- the loader proved exactly three.
            if (verdict_.has_value()) {
                return 2;
            }
            return quizAnswers_.size() < quiz_.questions().size() ? 3 : 0;
        case CreationStep::Background: {
            const std::size_t at = bioAnswers_.size();
            if (at >= biography_.questions().size()) {
                return 0;
            }
            const int answers = static_cast<int>(biography_.questions()[at].answers.size());
            // The first question also offers A PAST AT RANDOM -- Daggerfall's
            // own option, doc section 4 -- as one more row under the real
            // answers. Only the first: half-answered-then-random would make
            // "which questions did I actually answer" a puzzle.
            return at == 0 ? answers + 1 : answers;
        }
        default:
            return 0;
    }
}

void CreationFlow::moveChoiceCursor(int delta) noexcept {
    const int count = choiceRowCount();
    if (count <= 0) {
        return;
    }
    choiceCursor_ = ((choiceCursor_ + delta) % count + count) % count;
}

void CreationFlow::applyCalling(const sim::CallingTemplate& calling) noexcept {
    // A FRESH SHEET, designateInto's own precondition -- and the honest
    // meaning of taking a trade: the calling IS the sheet, not a layer over
    // whatever was half-built before. designateInto goes through Chargen's
    // own refusing calls, so a calling can never hold a combination a player
    // could not have clicked together; loaded content never refuses, and the
    // contract's answer for a partial refusal is to discard the sheet.
    chargen_ = sim::Chargen{};
    if (!calling.designateInto(chargen_, skills_)) {
        chargen_ = sim::Chargen{};
    }
    chosenCallingId_ = calling.id;
    commitPulse_.trigger();
    choiceCursor_ = 0;
    customizeCursor_ = 0;
    // The doc's own flow: calling -> biography -> review. Straight to review
    // when the biography was already answered (a verdict declined after the
    // fact keeps its past) or could not be loaded (must-still-boot).
    step_ = (!bioDone_ && biography_.loaded()) ? CreationStep::Background
                                               : CreationStep::Customize;
}

void CreationFlow::finishBiography() noexcept {
    // Pure and refused-rather-than-half-accumulated -- sim's own contract. A
    // nullopt here would be a caller bug (sizes always line up by
    // construction); the honest response is a no-op biography, not a crash.
    if (const std::optional<sim::ChargenEffects> total =
            sim::accumulateBiography(biography_, bioAnswers_)) {
        effects_ = *total;
    }
    bioDone_ = true;
    choiceCursor_ = 0;
    customizeCursor_ = 0;
    step_ = CreationStep::Customize;
}

void CreationFlow::chooseChoice() noexcept {
    const int count = choiceRowCount();
    if (count <= 0 || choiceCursor_ < 0 || choiceCursor_ >= count) {
        return;
    }
    if (step_ == CreationStep::Calling) {
        applyCalling(callings_.callings()[static_cast<std::size_t>(choiceCursor_)]);
        return;
    }
    if (step_ == CreationStep::Quiz) {
        if (verdict_.has_value()) {
            if (choiceCursor_ == 0) {
                // TAKE THE CALLING. The tally's id is a real roster id -- the
                // quiz loader proved every verdict cell at load.
                if (const sim::CallingTemplate* calling = callings_.find(verdict_->calling)) {
                    applyCalling(*calling);
                }
                return;
            }
            // ANOTHER TRADE: Daggerfall's own never-a-trap rule, doc section
            // 3.2 -- decline lands on the roster, answers kept so ESC can
            // still walk back into the verdict.
            choiceCursor_ = 0;
            step_ = CreationStep::Calling;
            return;
        }
        const std::size_t at = quizAnswers_.size();
        const std::array<int, 3> order = quizDisplayOrder(static_cast<int>(at));
        quizAnswers_.push_back(order[static_cast<std::size_t>(choiceCursor_)]);
        commitPulse_.trigger();
        choiceCursor_ = 0;
        if (quizAnswers_.size() == quiz_.questions().size()) {
            verdict_ = sim::tallyQuiz(quiz_, quizAnswers_);
        }
        return;
    }
    if (step_ == CreationStep::Background) {
        const std::size_t at = bioAnswers_.size();
        const sim::BiographyQuestion& question = biography_.questions()[at];
        if (at == 0 && choiceCursor_ == static_cast<int>(question.answers.size())) {
            // A PAST AT RANDOM. No RNG lives anywhere in this flow, so the
            // seed is the step count -- a pure function of how long the
            // screen has been open, which keeps a scripted capture
            // bit-identical while being as unrepeatable as a dice cup for a
            // human hand. xorshift over it, one draw per question.
            std::uint64_t state = static_cast<std::uint64_t>(stepCount_) * 2654435761ULL + 97ULL;
            for (const sim::BiographyQuestion& each : biography_.questions()) {
                state ^= state << 13U;
                state ^= state >> 7U;
                state ^= state << 17U;
                bioAnswers_.push_back(
                    static_cast<std::int32_t>(state % each.answers.size()));
            }
            commitPulse_.trigger();
            finishBiography();
            return;
        }
        bioAnswers_.push_back(choiceCursor_);
        commitPulse_.trigger();
        choiceCursor_ = 0;
        if (bioAnswers_.size() == biography_.questions().size()) {
            finishBiography();
        }
        return;
    }
}

void CreationFlow::back() noexcept {
    switch (step_) {
        case CreationStep::Origin:
            return;
        case CreationStep::Calling:
            choiceCursor_ = 0;
            step_ = CreationStep::Origin;
            return;
        case CreationStep::Quiz:
            // One commitment back: the verdict re-opens its last question,
            // a question re-opens the one before it, and the front of the
            // quiz is the door back out.
            if (verdict_.has_value()) {
                verdict_.reset();
                if (!quizAnswers_.empty()) {
                    quizAnswers_.pop_back();
                }
                choiceCursor_ = 0;
                return;
            }
            if (!quizAnswers_.empty()) {
                quizAnswers_.pop_back();
                choiceCursor_ = 0;
                return;
            }
            choiceCursor_ = 0;
            step_ = CreationStep::Origin;
            return;
        case CreationStep::Background:
            if (!bioAnswers_.empty()) {
                bioAnswers_.pop_back();
                choiceCursor_ = 0;
                return;
            }
            choiceCursor_ = 0;
            // Back the way the player came in: the custom door's sheet, or
            // the roster (which is also where a declined verdict lands, so
            // the quiz path backs out somewhere it can still act).
            if (chosenOrigin().id == "custom" || !callings_.loaded()) {
                step_ = CreationStep::Customize;
            } else {
                step_ = CreationStep::Calling;
            }
            return;
        case CreationStep::Customize:
            backToOrigin();
            return;
    }
}

std::array<std::int32_t, sim::kChargenAxisCount> CreationFlow::quizTallySoFar() const noexcept {
    std::array<std::int32_t, sim::kChargenAxisCount> counts{};
    const std::vector<sim::QuizQuestion>& questions = quiz_.questions();
    for (std::size_t i = 0; i < quizAnswers_.size() && i < questions.size(); ++i) {
        const std::int32_t answer = quizAnswers_[i];
        if (answer < 0 ||
            answer >= static_cast<std::int32_t>(questions[i].answers.size())) {
            continue;
        }
        const sim::ChargenAxis axis =
            questions[i].answers[static_cast<std::size_t>(answer)].axis;
        ++counts[static_cast<std::size_t>(axis)];
    }
    return counts;
}

std::vector<CreationFlow::CustomizeRow> CreationFlow::customizeRowModel() const {
    std::vector<CustomizeRow> rows;
    rows.push_back(CustomizeRow{CustomizeRow::Kind::Name, "", sim::AttributeId::Might, true});

    const sim::CompanionTemplate* companion = chosenCompanion();
    // LOOK, RIGHT UNDER NAME -- present for CUSTOM always (a player has to
    // land somewhere in sim::appearanceOptions() to be drawn in the ward's
    // own sprite vocabulary at all), present for DEVIN/GABRI only when their
    // own raw actually authors an appearanceType. ABSENCE COSTS NOTHING
    // HERE, the identical rule the skill rows below already hold to: Devin's
    // file does not author one yet, and a row that read "LOOK  NOT SET" for
    // him would claim a gap in HIS content that this screen invented rather
    // than one gabri.json's own provenance already names honestly.
    if (companion == nullptr || companion->appearanceType().has_value()) {
        rows.push_back(CustomizeRow{CustomizeRow::Kind::Appearance, "", sim::AttributeId::Might,
                                   companion == nullptr});
    }
    if (companion != nullptr) {
        // DEVIN/GABRI: read-only, and only the skills their own sheet
        // actually sets -- ABSENCE COSTS NOTHING HERE, the same rule the
        // HUD and the character sheet both already hold to. Listing all
        // nineteen with sixteen of them reading zero would tell a player
        // this sheet is being measured on axes it was never authored
        // against.
        rows.reserve(2 + companion->startingSkills().size() + sim::kAttributeCount);
        for (const sim::CompanionSkill& skill : companion->startingSkills()) {
            // THE FLAME NEVER REACHES THE SHEET HERE EITHER -- the identical
            // rule the CUSTOM branch below applies via Chargen::designate's
            // own refusal. A companion raw is content, not code, and content
            // can drift out of sync with that rule (companions.hpp's own
            // aptitudeTier field lives on SkillTrack::Entry, not on
            // CompanionSkill, so a raw is free to list an id the loader never
            // cross-checks against it) -- checked here, against the loaded
            // SkillTrack, rather than trusted from the raw.
            if (skills_.aptitudeTier(skill.id) == sim::AptitudeTier::Flame) {
                continue;
            }
            rows.push_back(CustomizeRow{CustomizeRow::Kind::Skill, skill.id, sim::AttributeId::Might,
                                       false});
        }
        for (std::size_t i = 0; i < sim::kAttributeCount; ++i) {
            rows.push_back(CustomizeRow{CustomizeRow::Kind::Attribute, "",
                                        static_cast<sim::AttributeId>(i), false});
        }
    } else {
        // CUSTOM (or a companion raw that failed to load, which reads the
        // same as CUSTOM rather than as a broken companion screen).
        rows.reserve(2 + skills_.size() + sim::kAttributeCount);
        for (const sim::SkillTrack::Entry& entry : skills_.entries()) {
            // THE FLAME NEVER REACHES THE SHEET -- chargen.hpp's own header,
            // and Chargen::designate refuses it too; skipping it here as
            // well means the row a player sees and the row LEFT/RIGHT could
            // ever move both agree with what designate() actually allows.
            if (entry.aptitudeTier == sim::AptitudeTier::Flame) {
                continue;
            }
            rows.push_back(
                CustomizeRow{CustomizeRow::Kind::Skill, entry.id, sim::AttributeId::Might, true});
        }
        for (std::size_t i = 0; i < sim::kAttributeCount; ++i) {
            rows.push_back(CustomizeRow{CustomizeRow::Kind::Attribute, "",
                                        static_cast<sim::AttributeId>(i), true});
        }
    }
    rows.push_back(CustomizeRow{CustomizeRow::Kind::Begin, "", sim::AttributeId::Might, true});
    return rows;
}

std::string CreationFlow::labelFor(const CustomizeRow& row) const {
    const sim::CompanionTemplate* companion = chosenCompanion();
    switch (row.kind) {
        case CustomizeRow::Kind::Name: {
            // "UNSET" AND NOT "NAME NOT SET" -- the longer phrase plus the
            // "NAME  " prefix plus the row's own number is twenty-one
            // glyphs into an eighteen-glyph column, the identical class of
            // clip shortDesignation() exists to head off below. A missing
            // name is not itself a defect worth writing a sentence about;
            // one honest word says it and fits.
            const std::string value = name_.empty() ? std::string("UNSET") : name_;
            return "NAME  " + value;
        }
        case CustomizeRow::Kind::Appearance: {
            // COMPANION FIRST: their own fixed look, never the CUSTOM
            // cursor's -- this row is only ever built for them when
            // appearanceType() is set (customizeRowModel()), so the
            // dereference below is safe by construction.
            const sim::WardType type = companion != nullptr
                                          ? *companion->appearanceType()
                                          : sim::appearanceOptions()[static_cast<std::size_t>(
                                                appearanceIndex_)]
                                                .type;
            const sim::AppearanceOption* option = sim::appearanceOptionFor(type);
            return "LOOK  " + (option != nullptr ? std::string(option->label) : std::string());
        }
        case CustomizeRow::Kind::Skill: {
            const sim::SkillTrack::Entry* entry = skills_.find(row.skillId);
            const std::string label = entry != nullptr ? entry->displayName : row.skillId;
            if (companion != nullptr) {
                // BARE NUMBER, ONE SPACE, NOT "  LV 30" -- caught the same
                // way shortDesignation() was: captured a real frame and
                // looked at it. "CRACKSMANSHIP" is thirteen glyphs, the
                // longest of the nineteen non-FLAME skills this raws file
                // names, and a two-digit level plus its row number was
                // still one glyph over the eighteen-glyph column even after
                // dropping "LV" -- "4 CRACKSMANSHIP." on DEVIN's own sheet,
                // the exact same defect this same pass already fixed once
                // for the two-space form on GABRI's "KIT-KEEPING". One
                // space instead of two buys back the one glyph every
                // skill this raws file currently names needs; the attribute
                // rows just below print a bare number with no unit either
                // ("MIGHT  40"), and the column's own header line ("A FIXED
                // SHEET") already says these are levels.
                return label + " " + std::to_string(companion->startingLevel(row.skillId));
            }
            return label + "  " + std::string(shortDesignation(chargen_.designationOf(row.skillId)));
        }
        case CustomizeRow::Kind::Attribute: {
            const std::int32_t value = companion != nullptr
                                          ? companion->derivedAttribute(row.attribute)
                                          : sim::kAttributeBase + chargen_.attributeBonus(row.attribute);
            return std::string(sim::attributeName(row.attribute)) + "  " + std::to_string(value);
        }
        case CustomizeRow::Kind::Begin:
            return "BEGIN";
    }
    return {};
}

std::string CreationFlow::statusLine() const {
    if (chosenCompanion() != nullptr) {
        return "A FIXED SHEET -- THEIRS, NOT YOURS TO SPEND.";
    }
    // THE DAGGER READOUT, doc section 5's one currency, shown wherever the
    // sheet is spendable. Points are 0 in this build -- the advantage shop's
    // prices are still on the owner's desk (see effects() in the header) --
    // and the readout says the neutral truth rather than hiding until a shop
    // exists: the multiplier IS live in the sim (SkillTrack's own Q8
    // advance multiplier), so the pace a player leaves with is worth a word.
    const std::int32_t points = effects_.daggerPoints;
    const std::int32_t q8 = sim::daggerMultiplierQ8ForPoints(points);
    const std::int32_t hundredths = (q8 * 100 + 128) / 256;
    const std::string dagger = "  DAGGER " + std::string(points >= 0 ? "+" : "") +
                               std::to_string(points) + " PACE X" +
                               std::to_string(hundredths / 100) + "." +
                               (hundredths % 100 < 10 ? "0" : "") +
                               std::to_string(hundredths % 100);
    return "PRIMARY " + std::to_string(chargen_.slotsFilled(sim::SkillDesignation::Primary)) + "/" +
          std::to_string(sim::kPrimarySkillSlots) + "  MAJOR " +
          std::to_string(chargen_.slotsFilled(sim::SkillDesignation::Major)) + "/" +
          std::to_string(sim::kMajorSkillSlots) + "  MINOR " +
          std::to_string(chargen_.slotsFilled(sim::SkillDesignation::Minor)) + "/" +
          std::to_string(sim::kMinorSkillSlots) + "  POINTS " +
          std::to_string(chargen_.attributePointsRemaining()) + " LEFT" + dagger;
}

std::vector<std::string> CreationFlow::sheetPreview(const sim::CallingTemplate& calling) const {
    // The roster's own sheet preview: the three tiers by display name, then
    // the spent attributes -- the numbers the review screen will show, shown
    // BEFORE the pick, which is the whole reason a roster can be "picked by
    // eye". Display names off the loaded SkillTrack, the same source every
    // customize row already reads; a name the track cannot find falls back
    // to the id, which loaded content never needs.
    const auto tierLine = [this](const char* label, const std::vector<std::string>& ids) {
        std::string line(label);
        for (std::size_t i = 0; i < ids.size(); ++i) {
            const sim::SkillTrack::Entry* entry = skills_.find(ids[i]);
            line += (i == 0 ? " " : ", ");
            line += entry != nullptr ? entry->displayName : ids[i];
        }
        return line;
    };
    std::vector<std::string> lines;
    lines.push_back(tierLine("PRIMARY", calling.primary));
    lines.push_back(tierLine("MAJOR", calling.major));
    lines.push_back(tierLine("MINOR", calling.minor));
    std::string spend;
    for (std::size_t i = 0; i < sim::kAttributeCount; ++i) {
        const auto attribute = static_cast<sim::AttributeId>(i);
        if (i > 0) {
            spend += "  ";
        }
        spend += std::string(sim::attributeAbbrev(attribute)) + " " +
                 std::to_string(sim::kAttributeBase + calling.attributeSpend[i]);
    }
    lines.push_back(spend);
    return lines;
}

std::vector<std::string> CreationFlow::centreLines() const {
    if (step_ == CreationStep::Calling) {
        const std::vector<sim::CallingTemplate>& roster = callings_.callings();
        if (choiceCursor_ >= 0 && choiceCursor_ < static_cast<int>(roster.size())) {
            return sheetPreview(roster[static_cast<std::size_t>(choiceCursor_)]);
        }
        return {};
    }
    if (step_ == CreationStep::Quiz) {
        if (verdict_.has_value()) {
            // The verdict card previews the sheet it is offering -- the same
            // block the roster shows, so accept-or-decline is an informed
            // choice and not a leap.
            if (const sim::CallingTemplate* calling = callings_.find(verdict_->calling)) {
                return sheetPreview(*calling);
            }
            return {};
        }
        const std::size_t at = quizAnswers_.size();
        if (at >= quiz_.questions().size()) {
            return {};
        }
        const sim::QuizQuestion& question = quiz_.questions()[at];
        const std::array<int, 3> order = quizDisplayOrder(static_cast<int>(at));
        if (choiceCursor_ < 0 || choiceCursor_ >= 3) {
            return {};
        }
        const int authored = order[static_cast<std::size_t>(choiceCursor_)];
        return {question.answers[static_cast<std::size_t>(authored)].text};
    }
    if (step_ == CreationStep::Background) {
        const std::size_t at = bioAnswers_.size();
        if (at >= biography_.questions().size()) {
            return {};
        }
        const sim::BiographyQuestion& question = biography_.questions()[at];
        if (at == 0 && choiceCursor_ == static_cast<int>(question.answers.size())) {
            return {"Answer nothing more. The remaining questions answer themselves."};
        }
        if (choiceCursor_ < 0 ||
            choiceCursor_ >= static_cast<int>(question.answers.size())) {
            return {};
        }
        return {question.answers[static_cast<std::size_t>(choiceCursor_)].text};
    }
    return {};
}

void CreationFlow::moveCustomizeCursor(int delta) noexcept {
    if (editingName_) {
        // Typing owns the keyboard -- arrows do not walk the list while a
        // name is being spelled out, the same way the options page's own
        // awaitingKey() takes the whole keyboard for one binding.
        return;
    }
    const int count = static_cast<int>(customizeRowModel().size());
    if (count <= 0) {
        return;
    }
    customizeCursor_ = ((customizeCursor_ + delta) % count + count) % count;
}

void CreationFlow::chooseCustomizeRow() noexcept {
    const std::vector<CustomizeRow> rows = customizeRowModel();
    if (customizeCursor_ < 0 || customizeCursor_ >= static_cast<int>(rows.size())) {
        return;
    }
    const CustomizeRow& row = rows[static_cast<std::size_t>(customizeCursor_)];
    if (row.kind == CustomizeRow::Kind::Name) {
        editingName_ = !editingName_;
        return;
    }
    if (row.kind == CustomizeRow::Kind::Begin) {
        // THE CUSTOM DOOR'S OWN ORDER, doc section 6: sheet first, then the
        // twelve questions, then back here to review -- so the first BEGIN a
        // make-your-own path presses routes through the biography once, and
        // the second one confirms. The calling and quiz doors arrive with
        // bioDone_ already true; GABRI and DEVIN never see a biography at
        // all ("their history is the raws' own"); and a build whose
        // biography.json could not be read confirms directly, the same
        // must-still-boot fallback every door keeps. Gated on canConfirm()
        // so a blank name refuses HERE, once, rather than after twelve
        // answered questions.
        if (!canConfirm()) {
            // NEVER A DEAD BUTTON. BEGIN used to refuse silently when the name
            // was blank -- press it, nothing happens, no explanation. The
            // reference's rule is that STATE CHANGES THE VERB rather than
            // greying it out, so the review pane's commit line reads
            // "ENTER - NAME YOURSELF FIRST" in that state and pressing it does
            // exactly that: puts the cursor on NAME and opens text entry. The
            // press always does something, and what it does is what it says.
            const std::vector<CustomizeRow> allRows = customizeRowModel();
            for (std::size_t i = 0; i < allRows.size(); ++i) {
                if (allRows[i].kind == CustomizeRow::Kind::Name) {
                    customizeCursor_ = static_cast<int>(i);
                    editingName_ = true;
                    return;
                }
            }
            return;
        }
        if (chosenCompanion() == nullptr && biography_.loaded() && !bioDone_) {
            choiceCursor_ = 0;
            step_ = CreationStep::Background;
            return;
        }
        confirm();
        return;
    }
    // A skill or attribute row: ENTER is deliberately a no-op here.
    // LEFT/RIGHT (adjustCustomizeRow) spend those on CUSTOM -- see the
    // header. DEVIN and GABRI have nothing for either key to do.
}

void CreationFlow::adjustCustomizeRow(int delta) noexcept {
    if (editingName_ || delta == 0) {
        return;
    }
    const std::vector<CustomizeRow> rows = customizeRowModel();
    if (customizeCursor_ < 0 || customizeCursor_ >= static_cast<int>(rows.size())) {
        return;
    }
    const CustomizeRow& row = rows[static_cast<std::size_t>(customizeCursor_)];
    if (!row.editable) {
        // DEVIN's and GABRI's sheets are fixed -- see the header.
        return;
    }
    const int step = delta > 0 ? 1 : -1;
    if (row.kind == CustomizeRow::Kind::Appearance) {
        // WRAPS, the same ring moveOriginCursor uses -- eleven cards on one
        // row have no page to turn to either.
        const int count = static_cast<int>(sim::appearanceOptions().size());
        appearanceIndex_ = ((appearanceIndex_ + step) % count + count) % count;
    } else if (row.kind == CustomizeRow::Kind::Skill) {
        const int current = static_cast<int>(chargen_.designationOf(row.skillId));
        const int next = std::clamp(current + step, 0, 3);
        if (next == current) {
            return;
        }
        if (next == 0) {
            chargen_.clear(row.skillId);
        } else {
            // A refusal (tier already full) leaves designationOf() exactly
            // where it was -- statusLine()'s own slot counts are the
            // player's evidence of why nothing moved.
            (void)chargen_.designate(row.skillId, static_cast<sim::SkillDesignation>(next), skills_);
        }
    } else if (row.kind == CustomizeRow::Kind::Attribute) {
        chargen_.spendAttributePoints(row.attribute, step);
    }
}

void CreationFlow::typeNameChar(char c) noexcept {
    if (!editingName_) {
        return;
    }
    const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    const bool nameChar = (upper >= 'A' && upper <= 'Z') || upper == ' ' || upper == '-' ||
                          upper == '\'';
    if (!nameChar) {
        return;
    }
    if (name_.size() >= kMaxNameLength) {
        return;
    }
    // A NAME CANNOT OPEN ON A SPACE. canConfirm() only checks for
    // non-empty, and " " passes that while naming nobody.
    if (upper == ' ' && name_.empty()) {
        return;
    }
    name_.push_back(upper);
    nameIsDefault_ = false;
    // A REAL KEYSTROKE PUTS THE ON-SCREEN KEYBOARD AWAY. This is the whole of
    // the "someone typing never sees it" guarantee, and it lives here rather
    // than in main.cpp because this is the one funnel every typed glyph goes
    // through -- a second copy of the rule beside the SDL loop is a rule that
    // will eventually disagree with itself. commitOsk() is the one caller that
    // is NOT a keystroke, and it restores the flag over the top.
    osk_ = false;
}

const std::vector<std::string>& CreationFlow::oskCells() {
    static const std::vector<std::string> cells = [] {
        std::vector<std::string> out;
        out.reserve(static_cast<std::size_t>(kOskColumns) * static_cast<std::size_t>(kOskRows));
        for (char c = 'A'; c <= 'Z'; ++c) {
            out.emplace_back(1, c);
        }
        // Exactly what typeNameChar() accepts and nothing else, so no cell on
        // this grid is a cell that does nothing when you press it.
        out.emplace_back("-");
        out.emplace_back("'");
        out.emplace_back("_");
        out.emplace_back("<");
        return out;
    }();
    return cells;
}

void CreationFlow::openOsk() noexcept {
    if (!editingName_) {
        return;
    }
    osk_ = true;
}

void CreationFlow::setOskCursor(int index) noexcept {
    if (index < 0 || index >= static_cast<int>(oskCells().size())) {
        return;
    }
    oskCursor_ = index;
}

void CreationFlow::moveOskCursor(int dx, int dy) noexcept {
    const int col = ((oskCursor_ % kOskColumns) + dx % kOskColumns + kOskColumns) % kOskColumns;
    const int row = ((oskCursor_ / kOskColumns) + dy % kOskRows + kOskRows) % kOskRows;
    oskCursor_ = row * kOskColumns + col;
}

void CreationFlow::commitOsk() noexcept {
    const std::vector<std::string>& cells = oskCells();
    if (oskCursor_ < 0 || oskCursor_ >= static_cast<int>(cells.size())) {
        return;
    }
    const std::string& cell = cells[static_cast<std::size_t>(oskCursor_)];
    if (cell == "<") {
        backspaceName();
        return;
    }
    const bool wasOpen = osk_;
    typeNameChar(cell == "_" ? ' ' : cell[0]);
    osk_ = wasOpen;
}

void CreationFlow::backspaceName() noexcept {
    if (!editingName_ || name_.empty()) {
        return;
    }
    name_.pop_back();
    nameIsDefault_ = false;
}

void CreationFlow::backToOrigin() noexcept {
    if (editingName_) {
        editingName_ = false;
        osk_ = false;
        return;
    }
    osk_ = false;
    step_ = CreationStep::Origin;
}

bool CreationFlow::confirm() noexcept {
    if (!canConfirm()) {
        return false;
    }
    result_.confirmed = true;
    result_.originId = chosenOrigin().id;
    result_.name = name_;
    // Everything the biography added up to, in chargen_raws.hpp's closed
    // vocabulary. Default -- a no-op -- for GABRI, DEVIN and any path that
    // never reached the questions, which is exactly the contract
    // CreationResult::effects documents.
    result_.effects = effects_;
    const sim::CompanionTemplate* companion = chosenCompanion();
    if (companion != nullptr) {
        result_.companion = *companion;
        result_.chargen = sim::Chargen{};
        // THEIR OWN FIXED LOOK, possibly std::nullopt (Devin's file does not
        // author one yet) -- carried through honestly rather than papered
        // over with a default CUSTOM would never actually land on.
        result_.appearance = companion->appearanceType();
    } else {
        result_.chargen = chargen_;
        result_.companion = sim::CompanionTemplate{};
        result_.appearance = sim::appearanceOptions()[static_cast<std::size_t>(appearanceIndex_)].type;
    }
    return true;
}

DialogueViewState CreationFlow::view() const {
    DialogueViewState state;
    state.open = true;
    // FULLY OPEN, NEVER EASING. Every other page this build has grown fades
    // in and out OVER gameplay that keeps running underneath it -- this
    // screen is not an overlay, it IS the screen, so there is nothing for it
    // to ease from and no world underneath it to reveal.
    state.openAmount = 1.0F;
    state.phase = phase();

    if (step_ == CreationStep::Origin) {
        // THE DOC MOCK'S OWN HEADER (section 6): the screen's name, and the
        // ward's one line under it -- carried on the caseRef row, the one
        // top-band line that clips-and-marks rather than silently wrapping
        // away, because the epithet slot beside the speaker has nowhere near
        // fifty-six glyphs of room.
        state.speaker = "A NAME FOR YOURSELF";
        state.epithet = "MAKE YOUR OWN, OR QUICK START";
        state.caseRef = "THE WARD HAD WORK FOR YOU BEFORE YOU HAD A NAME FOR IT.";
        const OriginTemplate& hovered = originTemplates()[static_cast<std::size_t>(originCursor_)];
        // THE HOVERED ROW'S OWN LINE SPEAKS IN THE TOP BAND -- the doc's
        // rule, and the mock's group headers live here too: the topic grid
        // has no non-selectable header row to give MAKE YOUR OWN / QUICK
        // START, so the quick starts announce their group ahead of Eli's own
        // tag and their raws' own voice (companions.hpp's epithet() -- the
        // shorter of the two authored lines, since the top band loses
        // whatever a paragraph will not fit).
        std::string blurb = hovered.tag;
        if (hovered.id == "devin" && devinTemplate_.loaded() && !devinTemplate_.epithet().empty()) {
            blurb = "QUICK START -- " + blurb + " -- " + devinTemplate_.epithet();
        } else if (hovered.id == "gabri" && gabriTemplate_.loaded() &&
                  !gabriTemplate_.epithet().empty()) {
            blurb = "QUICK START -- " + blurb + " -- " + gabriTemplate_.epithet();
        } else if (hovered.id == "devin" || hovered.id == "gabri") {
            blurb = "QUICK START -- " + blurb;
        }
        state.line = blurb;
        // NAME ONLY ON THE CARD ITSELF -- the tag and the real voice belong
        // in the top band above, which has the room and already carries
        // them. Two door names still outrun the eighteen-glyph column and
        // take clipLabel's marked cut; the detail line spells the hovered
        // one out in full, the same rescue every over-long row in this
        // build already rides.
        for (const OriginTemplate& t : originTemplates()) {
            state.topics.push_back(t.name);
        }
        state.cursor = originCursor_;
        state.page = topicPageOf(originCursor_);
        return state;
    }

    if (step_ == CreationStep::Calling) {
        state.speaker = "TAKE A CALLING";
        state.epithet = "THE WARD'S NINE TRADES";
        const std::vector<sim::CallingTemplate>& roster = callings_.callings();
        if (choiceCursor_ >= 0 && choiceCursor_ < static_cast<int>(roster.size())) {
            // The hovered trade's own one line, the doc's own words.
            state.line = roster[static_cast<std::size_t>(choiceCursor_)].oneLine;
        }
        for (const sim::CallingTemplate& calling : roster) {
            state.topics.push_back(calling.name);
        }
        state.cursor = choiceCursor_;
        state.page = topicPageOf(choiceCursor_);
        return state;
    }

    if (step_ == CreationStep::Quiz) {
        if (verdict_.has_value()) {
            // THE VERDICT CARD, doc section 3.2 -- never a trap: take it, or
            // walk back to the roster and choose by eye.
            state.speaker = "THE WARD'S VERDICT";
            const std::array<std::int32_t, sim::kChargenAxisCount> counts = verdict_->counts;
            std::string tally;
            for (std::size_t i = 0; i < quiz_.axes().size(); ++i) {
                if (i > 0) {
                    tally += "  ";
                }
                tally += meterName(quiz_.axes()[i].name) + " " +
                         std::to_string(counts[static_cast<std::size_t>(quiz_.axes()[i].axis)]);
            }
            state.epithet = tally;
            const sim::CallingTemplate* calling = callings_.find(verdict_->calling);
            state.line = calling != nullptr
                             ? calling->name + " -- " + calling->oneLine
                             : verdict_->calling;
            state.topics.push_back("TAKE THE CALLING");
            state.topics.push_back("ANOTHER TRADE");
            state.cursor = choiceCursor_;
            state.page = 0;
            return state;
        }
        const std::size_t at = quizAnswers_.size();
        state.speaker = "THE WARD ASKS";
        state.epithet = std::to_string(at + 1) + " OF " +
                        std::to_string(quiz_.questions().size());
        if (at < quiz_.questions().size()) {
            const sim::QuizQuestion& question = quiz_.questions()[at];
            state.line = question.prompt;
            const std::array<int, 3> order = quizDisplayOrder(static_cast<int>(at));
            for (const int authored : order) {
                state.topics.push_back(question.answers[static_cast<std::size_t>(authored)].text);
            }
        }
        state.cursor = choiceCursor_;
        state.page = 0;
        return state;
    }

    if (step_ == CreationStep::Background) {
        const std::size_t at = bioAnswers_.size();
        state.speaker = "YOUR OWN PAST";
        if (at < biography_.questions().size()) {
            const sim::BiographyQuestion& question = biography_.questions()[at];
            // "B4 - 4 OF 12": the doc's own numbering, so the owner's
            // line-edit and the screen agree about which question this is.
            state.epithet = question.id + " - " + std::to_string(at + 1) + " OF " +
                            std::to_string(biography_.questions().size());
            state.line = question.prompt;
            for (const sim::BiographyAnswer& answer : question.answers) {
                state.topics.push_back(answer.text);
            }
            if (at == 0) {
                state.topics.push_back("A PAST AT RANDOM");
            }
        }
        state.cursor = choiceCursor_;
        state.page = topicPageOf(choiceCursor_);
        return state;
    }

    state.speaker = "CUSTOMIZE";
    // Which path this sheet came in through -- and, once a calling is
    // taken, which one, so the review screen names the trade it is
    // reviewing rather than a generic door.
    if (const sim::CallingTemplate* taken =
            chosenCallingId_.empty() ? nullptr : callings_.find(chosenCallingId_);
        taken != nullptr && chosenCompanion() == nullptr) {
        state.epithet = chosenOrigin().name + " - " + taken->name;
    } else if (chosenCompanion() == nullptr) {
        state.epithet = chosenOrigin().name;
    } else {
        state.epithet = chosenOrigin().name + " - " + chosenOrigin().tag;
    }
    state.line = editingName_ ? std::string("TYPE A NAME, THEN ENTER.") : statusLine();
    const std::vector<CustomizeRow> rows = customizeRowModel();
    state.topics.reserve(rows.size());
    for (const CustomizeRow& row : rows) {
        state.topics.push_back(labelFor(row));
    }
    state.cursor = customizeCursor_;
    state.page = topicPageOf(customizeCursor_);
    return state;
}

// ===========================================================================
// THE COMPOSED PAGE -- what each of the seven steps actually puts on screen
// ===========================================================================
//
// docs/design/UI-REFERENCE-TERMINAL.md is the grammar and it is binding. Every
// step below is the SAME composition -- tab row, breadcrumb, instruction, a
// master list beside a detail pane, a commit verb at the foot of the detail,
// global nav under its own rule -- and differs only in what it puts in each.
// That sameness is the point: seven screens that read as one instrument.
//
// THE ONE THING THIS PASS EXISTS FOR is the detail pane. The owner played the
// quiz and the biography and "spent real choices -- skills, coin, faction
// standing, named-actor disposition -- with no idea what they bought". So on
// every step where a choice costs something, the highlighted row's cost is
// spelled out beside it: flavour first, then the named effect, then the number,
// with the colour doing the sorting.

namespace {

/// The three quiz axes, each in its own colour, so the meters and an answer's
/// consequence are the same hue wherever they appear. Cool for the mudlark
/// (water and the tideline), green for the disciple (the Mission), and the
/// build's own amber for the hand, which is work.
[[nodiscard]] Rgb axisAccent(sim::ChargenAxis axis) noexcept {
    switch (axis) {
        case sim::ChargenAxis::B:
            return Rgb{0.52F, 0.72F, 0.92F};
        case sim::ChargenAxis::C:
            return Rgb{0.52F, 0.82F, 0.48F};
        case sim::ChargenAxis::A:
        default:
            return Rgb{0.98F, 0.86F, 0.42F};
    }
}

/// The five doors. The three Daggerfall doors take the axis palette so the
/// roster, the quiz and the sheet are already the colours the player will meet
/// behind them; the two quick starts share the paper tone, because they are a
/// different KIND of thing rather than a third trade.
[[nodiscard]] Rgb doorAccent(const std::string& id) noexcept {
    if (id == "calling") {
        return Rgb{0.98F, 0.86F, 0.42F};
    }
    if (id == "quiz") {
        return Rgb{0.52F, 0.72F, 0.92F};
    }
    if (id == "custom") {
        return Rgb{0.52F, 0.82F, 0.48F};
    }
    return Rgb{0.86F, 0.74F, 0.52F};
}

/// EVERY STAT CARRIES ITS OWN COLOUR -- the reference's own character sheet, so
/// the sheet is scannable by hue before a word of it is read.
[[nodiscard]] Rgb attributeAccent(sim::AttributeId attribute) noexcept {
    switch (attribute) {
        case sim::AttributeId::Agility:
            return Rgb{0.52F, 0.82F, 0.48F};
        case sim::AttributeId::Vigor:
            return Rgb{0.90F, 0.62F, 0.34F};
        case sim::AttributeId::Wit:
            return Rgb{0.52F, 0.78F, 0.92F};
        case sim::AttributeId::Might:
        default:
            return Rgb{0.88F, 0.44F, 0.38F};
    }
}

[[nodiscard]] Rgb designationAccent(sim::SkillDesignation tier) noexcept {
    switch (tier) {
        case sim::SkillDesignation::Primary:
            return Rgb{0.98F, 0.86F, 0.42F};
        case sim::SkillDesignation::Major:
            return Rgb{0.52F, 0.82F, 0.48F};
        case sim::SkillDesignation::Minor:
            return Rgb{0.52F, 0.72F, 0.92F};
        case sim::SkillDesignation::None:
        default:
            return Rgb{0.66F, 0.64F, 0.58F};
    }
}

[[nodiscard]] std::string signed32(std::int32_t value) {
    return (value >= 0 ? "+" : "") + std::to_string(value);
}

/// WHERE YOU ARE IN THE FLOW, as the reference's tab row.
///
/// A NOTE ON HONESTY, because this is the one place the grammar had to be read
/// rather than copied. The reference's tabs carry keys (`d - Dominions`)
/// because they are VIEWS you may switch to. A creation flow's stages are not:
/// you cannot jump forward into a sheet you have not built. So these carry NO
/// KEY -- drawTabRow prints the bare name when the key is empty -- and they do
/// exactly the job the row exists for: name the four stages, invert the one you
/// are standing in, and keep the resource readout right-aligned beside them.
/// Printing "2 - PATH" would have been advertising a hotkey that does nothing,
/// which is the same defect as a greyed-out button.
[[nodiscard]] std::vector<PanelTab> stageTabs() {
    return {PanelTab{"", "DOOR"}, PanelTab{"", "PATH"}, PanelTab{"", "PAST"},
            PanelTab{"", "SHEET"}};
}

[[nodiscard]] PanelOption navOf(std::string_view key, std::string_view label) {
    PanelOption option;
    option.key = std::string(key);
    option.label = std::string(label);
    option.valueInk = InkRole::Dim;
    option.selectable = false;
    return option;
}

/// SHIP NOTE MOVE 3. The standard three-or-four-verb foot, worded for the
/// device that last spoke -- promptBackKey/promptMoveKeys/promptConfirmKey
/// are the page grammar main.cpp's creation_input actually routes (ESC/B,
/// arrows or stick or D-pad, ENTER/A). The digit entry is KEYBOARD ONLY: a
/// pad has no number row, and a foot advertising "1-9 PICK" at a thumb that
/// cannot press one is the exact defect this move exists to retire.
[[nodiscard]] std::vector<PanelOption> navFeet(InputDevice dev, std::string_view backVerb,
                                               std::string_view confirmVerb,
                                               std::string_view digits) {
    std::vector<PanelOption> nav{navOf(promptBackKey(dev), backVerb),
                                 navOf(promptMoveKeys(dev), "MOVE"),
                                 navOf(promptConfirmKey(dev), confirmVerb)};
    if (dev == InputDevice::KeyboardMouse && !digits.empty()) {
        nav.push_back(navOf(digits, "PICK"));
    }
    return nav;
}

/// "ENTER - X", or "A - X" -- the commit verb's key half, one place.
[[nodiscard]] std::string confirmVerbOf(InputDevice dev, std::string_view verb) {
    return std::string(promptConfirmKey(dev)) + " - " + std::string(verb);
}

}  // namespace

std::string CreationFlow::pageEffectName(const sim::ChargenEffect& effect) const {
    switch (effect.kind) {
        case sim::ChargenEffectKind::SkillDelta: {
            const sim::SkillTrack::Entry* entry = skills_.find(effect.target);
            return entry != nullptr ? entry->displayName : effect.target;
        }
        case sim::ChargenEffectKind::FactionStanding: {
            const sim::Faction* faction = factions_.find(effect.target);
            return faction != nullptr ? faction->displayName : effect.target;
        }
        case sim::ChargenEffectKind::ActorDisposition: {
            const sim::Notable* notable = notables_.find(effect.target);
            return notable != nullptr ? notable->name : effect.target;
        }
        default:
            return {};
    }
}

std::vector<PanelLine> CreationFlow::pageEffectLines(const std::vector<sim::ChargenEffect>& effects,
                                                     const Rgb& accent) const {
    // THE TWO REGISTERS THE REFERENCE SHOWS, and both are correct in their
    // place: an effect with a SUBJECT takes an accent-coloured name, a colon and
    // a green body ("CRACKSMANSHIP: +5 BEFORE YOU START"); an effect that is
    // only a number gets a bare number-led green bullet and no prose at all
    // ("+20 COIN"). Do not pad the terse one into a sentence to match its
    // neighbour.
    std::vector<PanelLine> out;
    out.reserve(effects.size());
    for (const sim::ChargenEffect& effect : effects) {
        PanelLine line;
        line.bullet = Bullet::Dot;
        line.bodyInk = InkRole::Number;
        line.nameInk = accent;
        switch (effect.kind) {
            case sim::ChargenEffectKind::SkillDelta:
                line.name = pageEffectName(effect);
                line.body = signed32(effect.amount) + " BEFORE YOU START";
                break;
            case sim::ChargenEffectKind::CoinDelta:
                line.body = signed32(effect.amount) + " COIN IN YOUR POCKET";
                break;
            case sim::ChargenEffectKind::FactionStanding:
                line.name = pageEffectName(effect);
                line.body = signed32(effect.amount) + " STANDING";
                break;
            case sim::ChargenEffectKind::ActorDisposition:
                line.name = pageEffectName(effect);
                line.body = signed32(effect.amount) + " DISPOSITION";
                break;
            case sim::ChargenEffectKind::Heat:
                line.body = signed32(effect.amount) + " HEAT -- THE WATCH STARTS WARMER";
                break;
            case sim::ChargenEffectKind::HpMax:
                line.body = signed32(effect.amount) + " MAX HEALTH";
                break;
            case sim::ChargenEffectKind::DaggerPoints:
                line.body = signed32(effect.amount) + " DAGGER POINTS";
                break;
        }
        out.push_back(std::move(line));
    }
    if (out.empty()) {
        // EMPTY STATES ARE WORDED, NOT BLANK -- the reference's own "no
        // trinket". Absence is stated so the reader knows it was considered,
        // rather than leaving them to wonder whether the pane failed to draw.
        PanelLine none;
        none.bullet = Bullet::None;
        none.body = "NOTHING CHANGES HANDS. THIS ONE IS ONLY WHO YOU WERE.";
        none.bodyInk = InkRole::Dim;
        out.push_back(std::move(none));
    }
    return out;
}

std::vector<PanelBar> CreationFlow::pageAxisBars(
    const std::array<std::int32_t, sim::kChargenAxisCount>& counts) const {
    std::vector<PanelBar> bars;
    if (!quiz_.loaded()) {
        return bars;
    }
    const int total = static_cast<int>(quiz_.questions().size());
    for (const sim::ChargenAxisIdentity& axis : quiz_.axes()) {
        PanelBar bar;
        bar.label = meterName(axis.name);
        bar.filled = counts[static_cast<std::size_t>(axis.axis)];
        bar.total = std::max(1, total);
        bar.value = std::to_string(bar.filled);
        bar.accent = axisAccent(axis.axis);
        bars.push_back(std::move(bar));
    }
    return bars;
}

std::vector<PanelBar> CreationFlow::pageCallingBars(const sim::CallingTemplate& calling) const {
    std::vector<PanelBar> bars;
    for (std::size_t i = 0; i < sim::kAttributeCount; ++i) {
        const auto attribute = static_cast<sim::AttributeId>(i);
        PanelBar bar;
        bar.label = std::string(sim::attributeName(attribute));
        bar.filled = std::clamp(calling.attributeSpend[i], 0,
                                sim::kAttributeBonusPerAttributeCap);
        bar.total = sim::kAttributeBonusPerAttributeCap;
        bar.value = std::to_string(sim::kAttributeBase + calling.attributeSpend[i]);
        bar.accent = attributeAccent(attribute);
        bars.push_back(std::move(bar));
    }
    return bars;
}

std::vector<PanelBar> CreationFlow::pageSheetBars() const {
    // THE POINT OF DRAWING THESE AT ALL, in the doc's own words: "a player
    // watching a quiz answer move STREETWISE can SEE the bar move. That is a
    // far better answer to 'show the consequence' than a line of text reporting
    // a delta." The bar measures what a player actually MOVES -- the bonus over
    // the base, against the per-attribute cap -- rather than the absolute value
    // against an invented ceiling, where a spent point would be a pixel.
    const sim::CompanionTemplate* companion = chosenCompanion();
    std::vector<PanelBar> bars;
    for (std::size_t i = 0; i < sim::kAttributeCount; ++i) {
        const auto attribute = static_cast<sim::AttributeId>(i);
        const std::int32_t value = companion != nullptr
                                       ? companion->derivedAttribute(attribute)
                                       : sim::kAttributeBase + chargen_.attributeBonus(attribute);
        PanelBar bar;
        bar.label = std::string(sim::attributeName(attribute));
        bar.filled =
            std::clamp(value - sim::kAttributeBase, 0, sim::kAttributeBonusPerAttributeCap);
        bar.total = sim::kAttributeBonusPerAttributeCap;
        bar.value = std::to_string(value);
        bar.accent = attributeAccent(attribute);
        bars.push_back(std::move(bar));
    }
    return bars;
}

std::vector<PanelLine> CreationFlow::pageTierLines(const sim::CallingTemplate& calling,
                                                   const Rgb& accent) const {
    const auto tier = [this, &accent](const char* label, const std::vector<std::string>& ids) {
        PanelLine line;
        line.bullet = Bullet::Dot;
        line.name = label;
        line.bodyInk = InkRole::Number;
        line.nameInk = accent;
        for (std::size_t i = 0; i < ids.size(); ++i) {
            const sim::SkillTrack::Entry* entry = skills_.find(ids[i]);
            if (i > 0) {
                line.body += ", ";
            }
            line.body += entry != nullptr ? entry->displayName : ids[i];
        }
        if (line.body.empty()) {
            line.body = "NONE";
            line.bodyInk = InkRole::Dim;
        }
        return line;
    };
    return {tier("PRIMARY", calling.primary), tier("MAJOR", calling.major),
            tier("MINOR", calling.minor)};
}

CreationPage CreationFlow::page() const {
    CreationPage out;
    out.title = "CREATION";
    out.tabs = stageTabs();
    out.alpha = 1.0F;

    // ------------------------------------------------------------------ door
    if (step_ == CreationStep::Origin) {
        const OriginTemplate& hovered = chosenOrigin();
        out.currentTab = 0;
        out.accent = doorAccent(hovered.id);
        out.crumbs = {"NEW GAME", "THE DOOR"};
        out.instruction = "PICK HOW YOU WANT TO BE MADE. NONE OF THEM LOCKS BEHIND YOU.";
        out.readout = name_.empty() ? "NAMELESS" : name_;
        out.shape = CreationListShape::Columns;
        out.maxColumns = 1;
        out.masterShare = 40;
        // FIVE DOORS IN A TWENTY-EIGHT-ROW PANE was the shape of this screen,
        // and it is the first screen anybody sees. The frame now ends after the
        // taller of its two halves; this floor is what the DETAIL half can
        // reach -- badge, the three-step summary, a line of prose and the
        // commit verb -- held constant so arrowing the doors moves nothing but
        // the fill. See CreationPage::bodyHoldRows.
        out.bodyHoldRows = 13;
        const std::vector<OriginTemplate>& doors = originTemplates();
        for (std::size_t i = 0; i < doors.size(); ++i) {
            CreationPageRow row;
            row.key = std::to_string(i + 1);
            row.label = doors[i].name;
            row.value = (doors[i].id == "gabri" || doors[i].id == "devin") ? "QUICK" : "BUILD";
            row.accent = doorAccent(doors[i].id);
            row.labelTakesAccent = true;
            out.rows.push_back(std::move(row));
        }
        out.cursor = originCursor_;
        out.detailBadge = hovered.name;
        out.detailStatus =
            (hovered.id == "gabri" || hovered.id == "devin") ? "QUICK START" : "MAKE YOUR OWN";
        // THE ROUTE, AS FACTS. A door is a commitment of TIME as much as of
        // character, and the one thing the old cards never said was how long
        // the road behind each of them was.
        if (hovered.id == "calling") {
            out.facts = {PanelFact{"FIRST", "PICK ONE OF NINE TRADES", InkRole::Prose},
                         PanelFact{"THEN", "TWELVE QUESTIONS ABOUT YOUR PAST", InkRole::Prose},
                         PanelFact{"LAST", "REVIEW THE SHEET, AND BEGIN", InkRole::Prose}};
        } else if (hovered.id == "quiz") {
            out.facts = {PanelFact{"FIRST", "TEN QUESTIONS, NO WRONG ANSWERS", InkRole::Prose},
                         PanelFact{"THEN", "THE WARD NAMES YOUR TRADE", InkRole::Prose},
                         PanelFact{"LAST", "YOUR PAST, THE SHEET, AND BEGIN", InkRole::Prose}};
        } else if (hovered.id == "custom") {
            out.facts = {PanelFact{"FIRST", "SPEND THE WHOLE SHEET YOURSELF", InkRole::Prose},
                         PanelFact{"THEN", "TWELVE QUESTIONS ABOUT YOUR PAST", InkRole::Prose},
                         PanelFact{"LAST", "REVIEW, AND BEGIN", InkRole::Prose}};
        } else {
            out.facts = {PanelFact{"SHEET", "FIXED, AND HAND-WRITTEN", InkRole::Prose},
                         PanelFact{"PAST", "ALREADY WRITTEN", InkRole::Prose},
                         PanelFact{"STRAIGHT TO", "THE REVIEW", InkRole::Prose}};
        }
        PanelLine blurb;
        blurb.body = hovered.tag;
        out.lines.push_back(std::move(blurb));
        const sim::CompanionTemplate* companion = chosenCompanion();
        if (companion != nullptr && !companion->bio().empty()) {
            PanelLine bio;
            bio.body = companion->bio();
            out.lines.push_back(std::move(bio));
        }
        out.commitVerb = confirmVerbOf(promptDevice_, "OPEN THIS DOOR");
        out.nav = navFeet(promptDevice_, "LEAVE", "OPEN", "1-5");
        return out;
    }

    // --------------------------------------------------------------- calling
    if (step_ == CreationStep::Calling) {
        const std::vector<sim::CallingTemplate>& roster = callings_.callings();
        out.currentTab = 1;
        out.crumbs = {"NEW GAME", "TAKE A CALLING"};
        out.instruction = "PICK A TRADE. THE SHEET IT COMES WITH IS SPELLED OUT BESIDE IT.";
        out.readout = name_.empty() ? "NAMELESS" : name_;
        out.shape = CreationListShape::Columns;
        out.maxColumns = 1;
        out.masterShare = 40;
        out.accent = panelInk().accent;
        // The tallest sheet any of the nine trades puts in the detail pane:
        // badge, four attribute bars, a line of flavour and three skill
        // bullets that can each wrap once at a narrow window, then the commit
        // verb. Held so the roster does not breathe as you arrow it.
        out.bodyHoldRows = 17;
        for (std::size_t i = 0; i < roster.size(); ++i) {
            CreationPageRow row;
            row.key = std::to_string(i + 1);
            row.label = roster[i].name;
            row.accent = axisAccent(roster[i].dominantAxis);
            row.labelTakesAccent = true;
            // STATE CHANGES THE ROW. A trade already taken drops the axis code
            // and says so instead -- the reference's own "the list row drops its
            // cost" -- and the commit verb below changes with it.
            // THE AXIS BY ITS NAME, never by its code. chargenAxisCode() answers
            // "A"/"B"/"C", which is the raws' own column heading and means
            // nothing to a player -- and this list is where a trade first says
            // what KIND of person it is for, so it has to be a word.
            row.value = roster[i].id == chosenCallingId_
                            ? std::string("TAKEN")
                            : meterName(pageAxisName(roster[i].dominantAxis));
            out.rows.push_back(std::move(row));
        }
        out.cursor = choiceCursor_;
        if (choiceCursor_ >= 0 && choiceCursor_ < static_cast<int>(roster.size())) {
            const sim::CallingTemplate& calling = roster[static_cast<std::size_t>(choiceCursor_)];
            const bool taken = calling.id == chosenCallingId_;
            out.accent = axisAccent(calling.dominantAxis);
            out.detailBadge = calling.name;
            out.detailStatus = taken ? "TAKEN" : "A TRADE";
            PanelLine flavour;
            flavour.body = calling.oneLine;
            out.lines.push_back(std::move(flavour));
            for (PanelLine& line : pageTierLines(calling, out.accent)) {
                out.lines.push_back(std::move(line));
            }
            out.bars = pageCallingBars(calling);
            out.commitVerb =
                confirmVerbOf(promptDevice_, taken ? "KEEP THIS TRADE" : "TAKE THIS TRADE");
            out.commitCost = taken ? "" : "(REPLACES THE WHOLE SHEET)";
        }
        out.nav = navFeet(promptDevice_, "BACK", "TAKE", "1-9");
        return out;
    }

    // --------------------------------------------------------------- verdict
    if (step_ == CreationStep::Quiz && verdict_.has_value()) {
        out.currentTab = 1;
        out.crumbs = {"NEW GAME", "ANSWER FOR YOURSELF", "THE VERDICT"};
        out.instruction =
            "THE WARD HAS MADE UP ITS MIND. TAKE THE TRADE, OR GO AND PICK ONE BY EYE.";
        out.shape = CreationListShape::Columns;
        out.maxColumns = 1;
        out.masterShare = 34;
        // TWO ROWS OF MASTER, so the card in the detail pane sets this height
        // by itself: badge, three tally bars, the reading and its three skill
        // lines, the commit verb. Two rows of master in a twenty-eight-row
        // pane was the worst ratio in the flow.
        out.bodyHoldRows = 13;
        const sim::CallingTemplate* calling = callings_.find(verdict_->calling);
        out.accent = calling != nullptr ? axisAccent(calling->dominantAxis) : panelInk().accent;
        out.readout = pageTallyReadout(verdict_->counts);
        CreationPageRow take;
        take.key = "1";
        take.label = "TAKE THE CALLING";
        take.accent = out.accent;
        take.labelTakesAccent = true;
        out.rows.push_back(std::move(take));
        CreationPageRow eye;
        eye.key = "2";
        eye.label = "PICK ONE BY EYE";
        eye.accent = Rgb{0.66F, 0.64F, 0.58F};
        eye.labelTakesAccent = true;
        out.rows.push_back(std::move(eye));
        out.cursor = choiceCursor_;
        out.bars = pageAxisBars(verdict_->counts);
        if (calling != nullptr) {
            out.detailBadge = calling->name;
            out.detailStatus = verdict_->pure ? "A PURE READING" : "A MIXED READING";
            PanelLine flavour;
            flavour.body = calling->oneLine;
            out.lines.push_back(std::move(flavour));
            for (PanelLine& line : pageTierLines(*calling, out.accent)) {
                out.lines.push_back(std::move(line));
            }
        } else {
            out.detailBadge = verdict_->calling;
            out.detailStatus = "UNREADABLE";
        }
        // THE VERB FOLLOWS THE CURSOR, which is the reference's rule that state
        // moves the row, the label and the verb together. Nothing is ever
        // greyed out; the one action line simply says what ENTER does now.
        out.commitVerb = confirmVerbOf(
            promptDevice_, choiceCursor_ == 1 ? "GO AND PICK ONE BY EYE" : "TAKE THIS TRADE");
        out.commitCost = choiceCursor_ == 1 ? "(THE ANSWERS ARE KEPT)" : "";
        out.nav = navFeet(promptDevice_, "LAST QUESTION", "CHOOSE", "1-2");
        return out;
    }

    // ------------------------------------------------------------------ quiz
    if (step_ == CreationStep::Quiz) {
        const std::size_t at = quizAnswers_.size();
        const std::vector<sim::QuizQuestion>& questions = quiz_.questions();
        out.currentTab = 1;
        out.crumbs = {"NEW GAME", "ANSWER FOR YOURSELF",
                      "QUESTION " + std::to_string(at + 1) + " OF " +
                          std::to_string(questions.size())};
        out.accent = panelInk().accent;
        // THE QUESTION IS THE TASK, so it lives on the instruction row where a
        // task lives -- wrapped, whole, and never clipped into a header.
        out.instruction = at < questions.size() ? questions[at].prompt : std::string{};
        // ANSWERS ARE SENTENCES, SO THEY GET BLOCKS. Numbered, direct-select,
        // inverted-fill selection -- the same grammar as a list of names, at the
        // scale a moral dilemma actually needs. The old grid clipped every one
        // of these at eighteen glyphs INCLUDING the row number.
        out.shape = CreationListShape::Blocks;
        out.masterShare = 58;
        // THE ONE THIS PROGRAM WAS SENT AT. Three answers spend eight to ten
        // rows of a forty-one-row block list at 640x360 -- two thirds of the
        // master pane was flat black on the first screen of the game. The
        // answers are static while the cursor moves within them, so the master
        // half sizes to them; this floor covers what the CONSEQUENCE half can
        // reach for any one of the three -- badge, two facts, three axis bars,
        // and up to three effect lines.
        out.bodyHoldRows = 15;
        const std::array<std::int32_t, sim::kChargenAxisCount> counts = quizTallySoFar();
        out.readout = pageTallyReadout(counts);
        if (at < questions.size()) {
            const sim::QuizQuestion& question = questions[at];
            const std::array<int, 3> order = quizDisplayOrder(static_cast<int>(at));
            for (std::size_t pos = 0; pos < order.size(); ++pos) {
                const sim::QuizAnswer& answer =
                    question.answers[static_cast<std::size_t>(order[pos])];
                CreationPageRow row;
                row.key = std::to_string(pos + 1);
                row.label = answer.text;
                // THE ANSWERS ARE NOT TINTED BY AXIS, and that is deliberate.
                // Every other list on this screen colours its rows by identity
                // because colour makes a list scannable -- but here the identity
                // is the thing the display shuffle (quizDisplayOrder) exists to
                // stop a player pattern-marking. A row in the mudlark's blue
                // would hand back in hue exactly what the shuffle took away in
                // position. The accent still shows up the instant the row is
                // SELECTED, because by then the detail pane has already named
                // the axis out loud.
                row.accent = axisAccent(answer.axis);
                row.labelTakesAccent = false;
                out.rows.push_back(std::move(row));
            }
            if (choiceCursor_ >= 0 && choiceCursor_ < 3) {
                const sim::QuizAnswer& picked = question.answers[static_cast<std::size_t>(
                    order[static_cast<std::size_t>(choiceCursor_)])];
                out.accent = axisAccent(picked.axis);
                std::array<std::int32_t, sim::kChargenAxisCount> after = counts;
                ++after[static_cast<std::size_t>(picked.axis)];
                out.detailBadge = pageAxisName(picked.axis);
                out.detailStatus = "IF YOU SAY THIS";
                out.bars = pageAxisBars(after);
                PanelLine scored;
                scored.bullet = Bullet::Dot;
                scored.name = pageAxisName(picked.axis);
                scored.body = "+1";
                scored.bodyInk = InkRole::Number;
                scored.nameInk = out.accent;
                out.lines.push_back(std::move(scored));
                // THE PROVISIONAL VERDICT. This is the single most direct answer
                // to "he spent his choices blind": the ward's reading of the
                // answers SO FAR, off sim::tallyFromCounts -- the same three
                // rules the real verdict uses, not a second copy of them.
                const sim::QuizTally heading = sim::tallyFromCounts(quiz_, after, picked.axis);
                const sim::CallingTemplate* toward = callings_.find(heading.calling);
                out.facts = {
                    PanelFact{"ANSWERED",
                              std::to_string(at) + " OF " + std::to_string(questions.size()),
                              InkRole::Number},
                    PanelFact{"HEADING FOR", toward != nullptr ? toward->name : heading.calling,
                              InkRole::Accent}};
            }
        }
        out.cursor = choiceCursor_;
        out.commitVerb = confirmVerbOf(promptDevice_, "SAY IT");
        out.commitCost = "(" + std::string(promptBackKey(promptDevice_)) + " TAKES IT BACK)";
        out.nav = navFeet(promptDevice_, "BACK", "ANSWER", "1-3");
        return out;
    }

    // ------------------------------------------------------------- your past
    if (step_ == CreationStep::Background) {
        const std::size_t at = bioAnswers_.size();
        const std::vector<sim::BiographyQuestion>& questions = biography_.questions();
        out.currentTab = 2;
        out.accent = Rgb{0.86F, 0.74F, 0.52F};
        out.shape = CreationListShape::Blocks;
        out.masterShare = 58;
        // Same shape as the quiz, and a biography answer buys named skills, so
        // the detail half can run a row or two longer.
        out.bodyHoldRows = 16;
        out.crumbs = {"NEW GAME", "YOUR OWN PAST",
                      at < questions.size() ? questions[at].id + " - " + std::to_string(at + 1) +
                                                  " OF " + std::to_string(questions.size())
                                            : std::string("DONE")};
        out.readout = pagePastReadout();
        if (at < questions.size()) {
            const sim::BiographyQuestion& question = questions[at];
            out.instruction = question.prompt;
            for (std::size_t i = 0; i < question.answers.size(); ++i) {
                CreationPageRow row;
                row.key = std::to_string(i + 1);
                row.label = question.answers[i].text;
                row.accent = out.accent;
                out.rows.push_back(std::move(row));
            }
            if (at == 0) {
                CreationPageRow row;
                row.key = std::to_string(question.answers.size() + 1);
                row.label = "A PAST AT RANDOM";
                row.accent = Rgb{0.66F, 0.64F, 0.58F};
                row.labelTakesAccent = true;
                out.rows.push_back(std::move(row));
            }
            out.cursor = choiceCursor_;
            const bool random =
                at == 0 && choiceCursor_ == static_cast<int>(question.answers.size());
            if (random) {
                out.detailBadge = "AT RANDOM";
                out.detailStatus = "THE WHOLE PAST";
                PanelLine line;
                line.body = "ANSWER NOTHING MORE. THE REST OF YOUR PAST IS ROLLED IN ONE THROW.";
                out.lines.push_back(std::move(line));
                out.commitVerb = confirmVerbOf(promptDevice_, "ROLL THE REST");
                out.commitCost = "(YOU CANNOT UNROLL IT)";
            } else if (choiceCursor_ >= 0 &&
                       choiceCursor_ < static_cast<int>(question.answers.size())) {
                const sim::BiographyAnswer& answer =
                    question.answers[static_cast<std::size_t>(choiceCursor_)];
                out.detailBadge = question.id;
                out.detailStatus = "WHAT IT COSTS";
                if (!answer.note.empty()) {
                    // FLAVOUR FIRST, THEN THE NAMED EFFECT, THEN THE NUMBER.
                    PanelLine note;
                    note.body = answer.note;
                    out.lines.push_back(std::move(note));
                }
                for (PanelLine& line : pageEffectLines(answer.effects, out.accent)) {
                    out.lines.push_back(std::move(line));
                }
                out.commitVerb = confirmVerbOf(promptDevice_, "THAT IS WHAT HAPPENED");
                out.commitCost =
                    "(" + std::string(promptBackKey(promptDevice_)) + " TAKES IT BACK)";
            }
        }
        out.nav = navFeet(promptDevice_, "BACK", "ANSWER", "1-9");
        return out;
    }

    // ------------------------------------------------------------- the sheet
    if (osk_ && editingName_) {
        return pageForOsk();
    }
    return pageForSheet();
}

CreationPage CreationFlow::pageForOsk() const {
    CreationPage out;
    out.title = "CREATION";
    out.tabs = stageTabs();
    out.currentTab = 3;
    out.accent = panelInk().accent;
    // A KEYBOARD IS A BLOCK, NOT A SPACED LIST. This used to be a Columns list
    // capped at six, which inherited planOptionList's spread rule and came out
    // at 640x360 as six one-glyph columns on a ten-cell stride -- five sparse
    // vertical strings rather than a keyboard. See panel.hpp's KeyGridStyle.
    out.shape = CreationListShape::Keys;
    out.maxColumns = kOskColumns;
    // The block is ten keys wide and the detail pane holds a name, two facts, a
    // paragraph and a verb: the detail is still the wider half of this one, but
    // the master no longer needs half the pane to hold thirty letters.
    out.masterShare = 28;
    // THE GRID IS STATIC UNDER THE CURSOR -- moving does not swap a row for a
    // taller one -- so three rows is the floor and the floor is the content.
    out.bodyHoldRows = kOskRows;
    out.crumbs = {"NEW GAME", chosenOrigin().name, "THE NAME"};
    out.instruction = "PICK THE LETTERS ONE AT A TIME. NO KEYBOARD NEEDED.";
    out.readout = std::to_string(name_.size()) + " OF " + std::to_string(kMaxNameLength);

    // AND THE TRANSPOSE IS GONE. drawOptionList runs column-major, so putting
    // an alphabet through it meant reordering the cells on the way in and
    // reordering the cursor index the same way -- two expressions that had to
    // stay identical, plus a third, inverse one in main.cpp for the mouse. A
    // key grid reads ACROSS by construction, so the cursor index IS the grid
    // position and there is nothing left to keep in step.
    const std::vector<std::string>& cells = oskCells();
    out.rows.reserve(cells.size());
    for (const std::string& cell : cells) {
        CreationPageRow row;
        row.label = cell;
        row.accent = out.accent;
        out.rows.push_back(std::move(row));
    }
    out.cursor = oskCursor_;

    const std::string& under = cells[static_cast<std::size_t>(
        std::clamp(oskCursor_, 0, static_cast<int>(cells.size()) - 1))];
    out.detailBadge = "NAME";
    out.detailStatus = under == "<"   ? "RUB OUT"
                       : under == "_" ? "A SPACE"
                                      : "THE LETTER " + under;
    // The caret is part of the value, not a separate row: the field is one
    // line and what it shows is what has been taken so far.
    out.facts = {PanelFact{"SO FAR", name_.empty() ? std::string("NOTHING YET") : name_ + "_",
                           name_.empty() ? InkRole::Dim : InkRole::Accent},
                 PanelFact{"ROOM FOR",
                           std::to_string(kMaxNameLength - name_.size()) + " MORE",
                           InkRole::Number}};
    PanelLine line;
    line.body = "THE WARD WILL USE IT TO YOUR FACE FROM HERE ON.";
    out.lines.push_back(std::move(line));
    // STATE CHANGES THE VERB. A blank name does not grey this out; it says what
    // is missing, and pressing it does nothing because there is nothing yet to
    // take -- the same rule BEGIN keeps two screens along.
    out.commitVerb = name_.empty() ? "START - PICK A LETTER FIRST" : "START - THAT IS THE NAME";
    out.commitCost = name_.empty() ? "" : "(" + name_ + ", FOR GOOD)";
    out.nav = {navOf("B", "BACK"), navOf("PAD", "MOVE"), navOf("A", "TAKE"),
               navOf("START", "DONE")};
    return out;
}

CreationPage CreationFlow::pageForSheet() const {
    CreationPage out;
    out.title = "CREATION";
    out.tabs = stageTabs();
    out.currentTab = 3;
    out.accent = panelInk().accent;
    out.shape = CreationListShape::Columns;
    out.maxColumns = 2;
    out.masterShare = 52;
    const sim::CompanionTemplate* companion = chosenCompanion();
    const sim::CallingTemplate* taken =
        chosenCallingId_.empty() ? nullptr : callings_.find(chosenCallingId_);
    out.crumbs = {"NEW GAME", chosenOrigin().name,
                  taken != nullptr && companion == nullptr ? taken->name
                                                           : std::string("THE SHEET")};
    out.instruction =
        editingName_
            ? std::string("TYPE A NAME. LETTERS, SPACE, HYPHEN. ENTER WHEN DONE.")
            : (companion != nullptr
                   ? std::string("A FIXED SHEET. READ IT, NAME YOURSELF, AND BEGIN.")
                   : std::string("UP AND DOWN WALK THE SHEET. LEFT AND RIGHT SPEND."));
    out.readout = pageSheetReadout();

    const std::vector<CustomizeRow> rows = customizeRowModel();
    out.rows.reserve(rows.size());
    for (const CustomizeRow& row : rows) {
        out.rows.push_back(pageRowFor(row));
    }
    out.cursor = customizeCursor_;
    // The sheet's four verbs, hand-built (LEFT RIGHT is its own fourth verb
    // and is honest on both devices); the digit entry never applied here.
    out.nav = {navOf(promptBackKey(promptDevice_), editingName_ ? "STOP TYPING" : "BACK"),
               navOf(promptMoveKeys(promptDevice_), "MOVE"), navOf("LEFT RIGHT", "SPEND"),
               navOf(promptConfirmKey(promptDevice_), "OPEN")};

    if (customizeCursor_ < 0 || customizeCursor_ >= static_cast<int>(rows.size())) {
        return out;
    }
    const CustomizeRow& row = rows[static_cast<std::size_t>(customizeCursor_)];
    const bool fixed = companion != nullptr;
    switch (row.kind) {
        case CustomizeRow::Kind::Name: {
            out.detailBadge = "NAME";
            out.detailStatus = editingName_ ? "TYPING" : "WHO YOU ARE";
            out.facts = {PanelFact{"TYPED", name_.empty() ? "NOTHING YET" : name_,
                                   name_.empty() ? InkRole::Dim : InkRole::Accent},
                         PanelFact{"ROOM FOR", std::to_string(kMaxNameLength) + " LETTERS",
                                   InkRole::Number}};
            PanelLine line;
            line.body = "THE WARD WILL USE IT TO YOUR FACE FROM HERE ON.";
            out.lines.push_back(std::move(line));
            out.commitVerb = confirmVerbOf(
                promptDevice_, editingName_ ? "THAT IS MY NAME" : "TYPE A NAME");
            out.commitCost = editingName_ ? "(BACKSPACE RUBS OUT)" : "";
            break;
        }
        case CustomizeRow::Kind::Appearance: {
            const sim::WardType type =
                companion != nullptr
                    ? *companion->appearanceType()
                    : sim::appearanceOptions()[static_cast<std::size_t>(appearanceIndex_)].type;
            const sim::AppearanceOption* option = sim::appearanceOptionFor(type);
            out.detailBadge = "LOOK";
            out.detailStatus = fixed ? "FIXED" : "HOW YOU PRESENT";
            out.facts = {
                PanelFact{"SHOWING",
                          option != nullptr ? std::string(option->label) : std::string("UNKNOWN"),
                          InkRole::Accent},
                PanelFact{"ONE OF",
                          std::to_string(sim::appearanceOptions().size()) + " IN THE WARD",
                          InkRole::Number}};
            PanelLine line;
            line.body = "THE WARD HAS ONLY SO MANY FACES TO GIVE OUT. THIS ONE IS YOURS.";
            out.lines.push_back(std::move(line));
            out.commitVerb = fixed ? "A FIXED LOOK" : "LEFT RIGHT - CHANGE";
            out.commitCost = fixed ? "(THEIRS, NOT YOURS TO MOVE)" : "";
            break;
        }
        case CustomizeRow::Kind::Skill: {
            const sim::SkillTrack::Entry* entry = skills_.find(row.skillId);
            const sim::SkillDesignation tier = chargen_.designationOf(row.skillId);
            out.detailBadge = entry != nullptr ? entry->displayName : row.skillId;
            out.accent = fixed ? panelInk().accent : designationAccent(tier);
            out.detailStatus = "A SKILL";
            const std::string governed =
                entry != nullptr && entry->governingAttribute.has_value()
                    ? std::string(sim::attributeName(*entry->governingAttribute))
                    : std::string("NOTHING");
            if (fixed) {
                out.facts = {
                    PanelFact{"STARTS AT", std::to_string(companion->startingLevel(row.skillId)),
                              InkRole::Number},
                    PanelFact{"GOVERNED BY", governed, InkRole::Prose}};
                out.commitVerb = "A FIXED SHEET";
                out.commitCost = "(NOTHING HERE TO SPEND)";
            } else {
                out.facts = {
                    PanelFact{"DESIGNATION", std::string(sim::skillDesignationName(tier)),
                              InkRole::Accent},
                    PanelFact{"STARTS AT", std::to_string(sim::startingLevelFor(tier)),
                              InkRole::Number},
                    PanelFact{"GOVERNED BY", governed, InkRole::Prose},
                    PanelFact{"APTITUDE",
                              entry != nullptr
                                  ? std::string(sim::aptitudeTierName(entry->aptitudeTier))
                                  : std::string("UNKNOWN"),
                              InkRole::Prose}};
                PanelLine line;
                line.body = "A HIGHER TIER STARTS HIGHER AND CLIMBS FASTER. THE TIERS ARE THE "
                            "SCARCE THING, NOT THE SKILLS.";
                out.lines.push_back(std::move(line));
                // STATE CHANGES THE VERB. An undesignated skill is offered a
                // tier; a designated one is offered the next one and the way
                // back off. Never a greyed-out button.
                out.commitVerb = tier == sim::SkillDesignation::None
                                     ? "RIGHT - MAKE IT PRIMARY"
                                     : "LEFT RIGHT - MOVE THE TIER";
                out.commitCost = pageSlotCost();
            }
            break;
        }
        case CustomizeRow::Kind::Attribute: {
            const std::int32_t value =
                companion != nullptr
                    ? companion->derivedAttribute(row.attribute)
                    : sim::kAttributeBase + chargen_.attributeBonus(row.attribute);
            out.accent = attributeAccent(row.attribute);
            out.detailBadge = std::string(sim::attributeName(row.attribute));
            out.detailStatus = "AN ATTRIBUTE";
            out.bars = pageSheetBars();
            out.facts = {
                PanelFact{"NOW", std::to_string(value), InkRole::Number},
                PanelFact{"OVER BASE", signed32(value - sim::kAttributeBase), InkRole::Number}};
            if (fixed) {
                out.commitVerb = "A FIXED SHEET";
                out.commitCost = "(DERIVED FROM THEIR OWN SKILLS)";
            } else {
                out.commitVerb = "LEFT RIGHT - SPEND A POINT";
                out.commitCost = "(" + std::to_string(chargen_.attributePointsRemaining()) +
                                 " OF " + std::to_string(sim::kAttributeBonusPool) + " LEFT)";
            }
            break;
        }
        case CustomizeRow::Kind::Begin: {
            out.accent = panelInk().key;
            out.detailBadge = "BEGIN";
            out.detailStatus = canConfirm() ? "READY" : "NOT YET";
            out.bars = pageSheetBars();
            out.facts = {
                PanelFact{"NAME", name_.empty() ? "UNSET" : name_,
                          name_.empty() ? InkRole::Dim : InkRole::Accent},
                PanelFact{"PATH", chosenOrigin().name, InkRole::Prose},
                PanelFact{"TRADE",
                          taken != nullptr
                              ? taken->name
                              : (fixed ? std::string("THEIR OWN") : std::string("HAND-BUILT")),
                          InkRole::Prose}};
            for (PanelLine& line : pagePastLines()) {
                out.lines.push_back(std::move(line));
            }
            // NEVER A DEAD BUTTON. With no name typed, the verb says what is
            // missing and pressing it goes and fixes that -- see
            // chooseCustomizeRow().
            if (!canConfirm()) {
                out.commitVerb = confirmVerbOf(promptDevice_, "NAME YOURSELF FIRST");
                out.commitCost = "(IT TAKES YOU TO THE NAME ROW)";
            } else if (companion == nullptr && biography_.loaded() && !bioDone_) {
                out.commitVerb = confirmVerbOf(promptDevice_, "ON TO YOUR PAST");
                out.commitCost = "(TWELVE QUESTIONS, THEN BACK HERE)";
            } else {
                out.commitVerb = confirmVerbOf(promptDevice_, "BEGIN");
                out.commitCost = "(THE WARD IS WAITING)";
            }
            break;
        }
    }
    return out;
}

CreationPageRow CreationFlow::pageRowFor(const CustomizeRow& row) const {
    // LABEL AND VALUE, SPLIT. labelFor() jams the two together ("NAME  UNSET")
    // because the old topic grid had one string per row and no value column.
    // This screen has one, and a common value column is half of what "clean"
    // means -- so the row is built as the two things it always was.
    const sim::CompanionTemplate* companion = chosenCompanion();
    CreationPageRow out;
    switch (row.kind) {
        case CustomizeRow::Kind::Name:
            out.label = "NAME";
            out.value = name_.empty() ? "UNSET" : name_;
            out.accent = panelInk().accent;
            break;
        case CustomizeRow::Kind::Appearance: {
            const sim::WardType type =
                companion != nullptr
                    ? *companion->appearanceType()
                    : sim::appearanceOptions()[static_cast<std::size_t>(appearanceIndex_)].type;
            const sim::AppearanceOption* option = sim::appearanceOptionFor(type);
            out.label = "LOOK";
            out.value = option != nullptr ? std::string(option->label) : std::string();
            out.accent = panelInk().accent;
            break;
        }
        case CustomizeRow::Kind::Skill: {
            const sim::SkillTrack::Entry* entry = skills_.find(row.skillId);
            out.label = entry != nullptr ? entry->displayName : row.skillId;
            if (companion != nullptr) {
                out.value = std::to_string(companion->startingLevel(row.skillId));
                out.accent = panelInk().accent;
            } else {
                const sim::SkillDesignation tier = chargen_.designationOf(row.skillId);
                out.value = std::string(shortDesignation(tier));
                out.accent = designationAccent(tier);
                out.labelTakesAccent = tier != sim::SkillDesignation::None;
            }
            break;
        }
        case CustomizeRow::Kind::Attribute: {
            const std::int32_t value =
                companion != nullptr
                    ? companion->derivedAttribute(row.attribute)
                    : sim::kAttributeBase + chargen_.attributeBonus(row.attribute);
            out.label = std::string(sim::attributeName(row.attribute));
            out.value = std::to_string(value);
            out.accent = attributeAccent(row.attribute);
            out.labelTakesAccent = true;
            break;
        }
        case CustomizeRow::Kind::Begin:
            out.label = "BEGIN";
            out.accent = panelInk().key;
            out.labelTakesAccent = true;
            break;
    }
    return out;
}

std::string CreationFlow::pageAxisName(sim::ChargenAxis axis) const {
    for (const sim::ChargenAxisIdentity& identity : quiz_.axes()) {
        if (identity.axis == axis) {
            return identity.name;
        }
    }
    return std::string(sim::chargenAxisCode(axis));
}

std::string CreationFlow::pageTallyReadout(
    const std::array<std::int32_t, sim::kChargenAxisCount>& counts) const {
    std::string out;
    for (const sim::ChargenAxisIdentity& axis : quiz_.axes()) {
        if (!out.empty()) {
            out += "  ";
        }
        out += meterName(axis.name) + " " +
               std::to_string(counts[static_cast<std::size_t>(axis.axis)]);
    }
    return out;
}

std::string CreationFlow::pagePastReadout() const {
    // THE RUNNING COST OF A PAST. The owner answered twelve questions that each
    // moved coin, heat and standing and was never once shown a total.
    const sim::ChargenEffects so = effectsSoFar();
    std::string out = "COIN " + signed32(so.coinDelta) + "  HEAT " + std::to_string(so.heat);
    if (!so.skillDeltas.empty()) {
        out += "  SKILLS " + std::to_string(so.skillDeltas.size());
    }
    if (!so.factionStandings.empty()) {
        out += "  TIES " + std::to_string(so.factionStandings.size());
    }
    return out;
}

std::vector<PanelLine> CreationFlow::pagePastLines() const {
    const sim::ChargenEffects so = effectsSoFar();
    std::vector<PanelLine> out;
    const auto number = [&out](const std::string& body) {
        PanelLine line;
        line.bullet = Bullet::Dot;
        line.body = body;
        line.bodyInk = InkRole::Number;
        out.push_back(std::move(line));
    };
    if (so.coinDelta != 0) {
        number(signed32(so.coinDelta) + " COIN OUT OF YOUR PAST");
    }
    if (so.heat != 0) {
        number(signed32(so.heat) + " HEAT BEFORE YOU HAVE DONE ANYTHING");
    }
    if (so.hpMaxDelta != 0) {
        number(signed32(so.hpMaxDelta) + " MAX HEALTH");
    }
    if (!so.skillDeltas.empty()) {
        PanelLine line;
        line.bullet = Bullet::Dot;
        line.name = "SKILLS MOVED";
        line.nameInk = panelInk().accent;
        line.bodyInk = InkRole::Number;
        for (std::size_t i = 0; i < so.skillDeltas.size(); ++i) {
            const sim::SkillTrack::Entry* entry = skills_.find(so.skillDeltas[i].first);
            if (i > 0) {
                line.body += ", ";
            }
            line.body += (entry != nullptr ? entry->displayName : so.skillDeltas[i].first) + " " +
                         signed32(so.skillDeltas[i].second);
        }
        out.push_back(std::move(line));
    }
    for (const std::pair<std::string, std::int32_t>& standing : so.factionStandings) {
        if (standing.second == 0) {
            continue;
        }
        PanelLine line;
        line.bullet = Bullet::Ring;
        const sim::Faction* faction = factions_.find(standing.first);
        line.name = faction != nullptr ? faction->displayName : standing.first;
        line.nameInk = panelInk().accent;
        line.body = signed32(standing.second) + " STANDING";
        line.bodyInk = InkRole::Number;
        out.push_back(std::move(line));
    }
    if (out.empty()) {
        PanelLine line;
        line.body = bioDone_ ? "YOUR PAST COST YOU NOTHING THE LEDGER CAN SEE."
                             : "NOTHING FROM YOUR PAST YET.";
        line.bodyInk = InkRole::Dim;
        out.push_back(std::move(line));
    }
    return out;
}

std::string CreationFlow::pageSheetReadout() const {
    if (chosenCompanion() != nullptr) {
        return "A FIXED SHEET";
    }
    return "PRI " + std::to_string(chargen_.slotsFilled(sim::SkillDesignation::Primary)) + "/" +
           std::to_string(sim::kPrimarySkillSlots) + "  MAJ " +
           std::to_string(chargen_.slotsFilled(sim::SkillDesignation::Major)) + "/" +
           std::to_string(sim::kMajorSkillSlots) + "  MIN " +
           std::to_string(chargen_.slotsFilled(sim::SkillDesignation::Minor)) + "/" +
           std::to_string(sim::kMinorSkillSlots) + "  PTS " +
           std::to_string(chargen_.attributePointsRemaining());
}

std::string CreationFlow::pageSlotCost() const {
    // The restatement under the verb is why a refusal is never a mystery:
    // designate() refuses the instant a tier is full, and this says which tiers
    // still have room BEFORE the key is pressed.
    const auto room = [this](const char* label, sim::SkillDesignation which, std::int32_t slots) {
        return std::string(label) + " " + std::to_string(slots - chargen_.slotsFilled(which)) +
               " LEFT";
    };
    return "(" + room("PRI", sim::SkillDesignation::Primary, sim::kPrimarySkillSlots) + ", " +
           room("MAJ", sim::SkillDesignation::Major, sim::kMajorSkillSlots) + ", " +
           room("MIN", sim::SkillDesignation::Minor, sim::kMinorSkillSlots) + ")";
}

void drawCreation(Framebuffer& target, const CreationFlow& flow) {
    // THE SAME NEAR-BLACK dialogue_view.cpp's own panel colour. The composed
    // frame paints its own ground over the whole window on top of this, so the
    // clear is now only the backstop for the handful of pixels a window whose
    // height does not divide into whole rows leaves outside the grid.
    target.clear(Rgb{0.04F, 0.04F, 0.05F});
    // STILL NEVER EASES OPEN -- this file's own documented rule. This screen is
    // not an overlay over a running world; it IS the screen, so there is nothing
    // for it to ease from and nothing underneath it to reveal.
    drawCreationPage(target, flow.page());
}

CreationHit creationHitTest(const CreationFlow& flow, int frameWidth, int frameHeight, int px,
                            int py) {
    return creationPageHitTest(flow.page(), frameWidth, frameHeight, px, py);
}

}  // namespace granadad::render
