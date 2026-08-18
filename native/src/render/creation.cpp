#include "granadad/render/creation.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string_view>

#include "granadad/render/framebuffer.hpp"
#include "granadad/render/hud.hpp"
#include "granadad/sim/faction.hpp"
#include "granadad/sim/notables.hpp"

namespace granadad::render {

namespace {

// INNOVATION SPRINT ITEM #4. THE VOID GETS SOME ANCHORING. drawCreation()
// used to clear straight to near-black and hand the whole frame to
// drawDialogue(), which only ever fills the top ~15% and bottom ~40% or so
// with its own two bands -- the same "70% flat true-black void" the brief
// names, with nothing at all designed for the space between them. That black
// is not the problem and is not changing (docs/design/DECISIONS.md's own art
// register: "true black voids in unbuilt/unlit space" is this build's
// settled muse) -- the problem is that nothing else this game's world ever
// draws over that black without ALSO drawing the one thing that makes it
// read as deliberate: "warm torch/brazier light pools cutting through the
// black" (DECISIONS.md, verbatim). WorldRenderer's own lamps use
// {1.00, 0.58, 0.24}-ish warm colours for exactly this (lighting.cpp's
// kFireColour); this screen has no lamp and no WorldRenderer, so the same
// warmth is painted here by hand, once, as a soft radial pool behind the
// options the player is actually reading -- plus a thin bronze frame border
// in dialogue_view.cpp's own kEdge tone, the identical "this was built, not
// left blank" cue every panel edge in this game already carries.
//
// BOTH ARE STATIC. `state.openAmount` stays 1.0 with no easing, exactly as
// creation.cpp's own header already argues -- "there is nothing for it to
// ease from and no world underneath it to reveal" -- and this treatment
// follows that reasoning exactly: it is not a transition, it is the room's
// own furniture, so it is painted once a frame and never animated.
//
// KEPT DIM ON PURPOSE. The character options -- NAME, LOOK, the skill and
// attribute rows -- are the actual content of this screen, and drawDialogue
// draws them last, on top of this. Both the pool and the border sit well
// under the panel's own text alpha (0.94-0.98F elsewhere in this file) so
// neither competes with a word.
[[nodiscard]] Rgb voidLightPoolColour() noexcept { return Rgb{1.00F, 0.58F, 0.24F}; }
[[nodiscard]] Rgb voidBorderColour() noexcept { return Rgb{0.44F, 0.40F, 0.31F}; }

void drawVoidAnchor(Framebuffer& target) {
    const int width = target.width();
    const int height = target.height();
    const int scale = std::max(1, height / 180);

    // THE LIGHT POOL: a soft, warm radial falloff centred on the frame, the
    // way a brazier's own glow pools rather than cutting a hard edge. Kept
    // well short of the corners (radius a little under half the shorter
    // side) so it reads as light falling on the middle of the room and never
    // reaches the panel bands drawDialogue paints over the top and bottom
    // edges. Squared falloff (t*t) for a soft core that fades fast toward
    // its own rim rather than a linear cone, and a peak alpha under a fifth
    // of full strength -- restrained the same way the alert plate's own
    // pulse and the brawl flash both are elsewhere this sprint, so a warm
    // wash reads as "considered" rather than "a spotlight".
    const float cx = static_cast<float>(width) * 0.5F;
    const float cy = static_cast<float>(height) * 0.5F;
    const float radius = static_cast<float>(std::min(width, height)) * 0.46F;
    const Rgb pool = voidLightPoolColour();
    constexpr float kPoolPeakAlpha = 0.16F;
    for (int y = 0; y < height; ++y) {
        const float dy = static_cast<float>(y) - cy;
        for (int x = 0; x < width; ++x) {
            const float dx = static_cast<float>(x) - cx;
            const float dist = std::sqrt(dx * dx + dy * dy);
            const float t = std::clamp(1.0F - dist / radius, 0.0F, 1.0F);
            if (t <= 0.0F) {
                continue;
            }
            target.blend(x, y, pool, kPoolPeakAlpha * t * t);
        }
    }

    // THE FRAME: a thin bronze bezel a few pixels in from every edge, the
    // same near-neutral tone dialogue_view.cpp's own panel edge (kEdge)
    // already uses everywhere else this build draws a border. Deliberately
    // NOT the panel's own bright picked-row gold -- this is furniture, not
    // something to press a key on.
    const int inset = 3 * scale;
    const Rgb border = voidBorderColour();
    constexpr float kBorderAlpha = 0.30F;
    const int thickness = std::max(1, scale / 2);
    target.fillRect(inset, inset, width - 2 * inset, thickness, border, kBorderAlpha);
    target.fillRect(inset, height - inset - thickness, width - 2 * inset, thickness, border,
                    kBorderAlpha);
    target.fillRect(inset, inset, thickness, height - 2 * inset, border, kBorderAlpha);
    target.fillRect(width - inset - thickness, inset, thickness, height - 2 * inset, border,
                    kBorderAlpha);
}

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
      biography_([&contentDir, this] {
          // The biography's targets resolve against the SAME registries every
          // other system reads (chargen_raws.hpp's own stance); both are
          // load-time validators only, so neither is kept.
          const sim::FactionRegistry factions = sim::FactionRegistry::load(contentDir);
          const sim::NotableRegistry notables = sim::NotableRegistry::load(contentDir);
          return sim::BiographyRegistry::load(contentDir, skills_, factions, notables);
      }()),
      devinTemplate_(sim::CompanionTemplate::load(contentDir, "devin")),
      gabriTemplate_(sim::CompanionTemplate::load(contentDir, "gabri")) {
    loadOriginDefaultName();
}

void CreationFlow::moveOriginCursor(int delta) noexcept {
    const int count = static_cast<int>(originTemplates().size());
    originCursor_ = ((originCursor_ + delta) % count + count) % count;
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
        return "A FIXED SHEET -- NOT ADJUSTABLE HERE.";
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
        if (chosenCompanion() == nullptr && biography_.loaded() && !bioDone_ && canConfirm()) {
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
        return;
    }
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

namespace {

/// THE SCREEN'S OWN CENTRE FURNITURE, task #92: the full hovered answer (or
/// the hovered calling's whole sheet) written out in the one region this
/// screen has no world or face to keep clear for, and -- on the quiz -- the
/// three axis meters along the panel's foot. Both drawn BEFORE drawDialogue,
/// like drawVoidAnchor, so the bands paint over whatever runs long.
///
/// WHY THE CENTRE AT ALL: a quiz answer is a hundred-and-thirty-glyph
/// sentence and the topic grid's column is eighteen. clipLabel and the
/// detail line both mark their cuts honestly, but a quiz nobody can READ is
/// a broken screen, so the hovered row is spelled out whole here -- the same
/// job the detail line does for a label, at the scale a moral dilemma needs.
void drawCreationCentre(Framebuffer& target, const CreationFlow& flow,
                        const DialogueViewState& view) {
    const CreationStep step = flow.step();
    if (step != CreationStep::Calling && step != CreationStep::Quiz &&
        step != CreationStep::Background) {
        return;
    }
    const int width = target.width();
    const int height = target.height();
    const int scale = std::max(1, height / 180);
    const int margin = 5 * scale;
    const int rowStep = 8 * scale;
    const int glyphAdvance = 5 * scale;
    const CentreRect centre = hudCentreRect(width, height);

    // The bottom band's top edge, dialogue_view.cpp's own arithmetic over
    // the rows this screen actually shows (every list here fits one page).
    const int gridRows = std::max(
        1, (static_cast<int>(view.topics.size()) + kTopicColumns - 1) / kTopicColumns);
    const int bottomTop = height - 2 * scale - gridRows * rowStep;

    // ---- the meters: three identities, filling as answers land ------------
    //
    // CONTINUOUS STATE -> CONTINUOUS VISUALS (DECISIONS.md rule 4): the fill
    // is the running tally itself, the glow breathes with the panel's own
    // phase, and the one EVENT -- an answer committing -- rides the
    // restrained ImpactPulse (rule 3), a brief brightening and nothing held.
    int metersTop = bottomTop;
    const bool quizMeters = step == CreationStep::Quiz && flow.quiz().loaded();
    if (quizMeters) {
        const std::array<std::int32_t, sim::kChargenAxisCount> counts =
            flow.quizVerdict().has_value() ? flow.quizVerdict()->counts : flow.quizTallySoFar();
        const int total = static_cast<int>(flow.quiz().questions().size());
        const float breathe = 0.5F + 0.5F * std::sin(flow.phase() * 2.0F);
        const float pulse = flow.commitPulse();
        const int cellW = 3 * scale;
        const int cellH = 4 * scale;
        const int cellGap = scale;
        const int columnWidth = (width - 2 * margin) / 3;
        metersTop = bottomTop - rowStep - 2 * scale;
        const Rgb lit{0.98F, 0.86F, 0.42F};   // the picked-row gold
        const Rgb unlit{0.44F, 0.40F, 0.31F}; // the panel edge bronze
        for (std::size_t axis = 0; axis < flow.quiz().axes().size() && axis < 3; ++axis) {
            const int x = margin + static_cast<int>(axis) * columnWidth;
            const std::string label = meterName(flow.quiz().axes()[axis].name);
            const int labelWidth = drawText(target, x, metersTop, label,
                                            Rgb{0.66F, 0.68F, 0.66F}, 0.85F, scale);
            const std::int32_t filled =
                counts[static_cast<std::size_t>(flow.quiz().axes()[axis].axis)];
            int cellX = x + labelWidth + glyphAdvance;
            for (int cell = 0; cell < total; ++cell) {
                if (cellX + cellW > x + columnWidth) {
                    break;  // a narrow frame keeps whole cells, never slivers
                }
                if (cell < filled) {
                    const float alpha =
                        std::min(1.0F, 0.55F + 0.18F * breathe + 0.35F * pulse);
                    target.fillRect(cellX, metersTop, cellW, cellH, lit, alpha);
                } else {
                    target.fillRect(cellX, metersTop, cellW, cellH, unlit, 0.22F);
                }
                cellX += cellW + cellGap;
            }
        }
    }

    // ---- the hovered row, spelled out whole -------------------------------
    const std::vector<std::string> lines = flow.centreLines();
    if (lines.empty()) {
        return;
    }
    const std::size_t columns =
        static_cast<std::size_t>(std::max(8, (width - 4 * margin) / glyphAdvance));
    std::vector<std::string> wrapped;
    for (const std::string& line : lines) {
        for (std::string& row : wrapText(line, columns)) {
            wrapped.push_back(std::move(row));
        }
    }
    // Between the top band's floor and the meters (or the bottom band):
    // whole rows only, and the tail is CUT AND MARKED, never silently gone
    // -- the same honesty rule every clipped label in this build keeps.
    const int textTop = centre.y0 + rowStep;
    const int rowsAvail = std::max(0, (metersTop - 2 * scale - textTop) / rowStep);
    if (rowsAvail <= 0) {
        return;
    }
    if (static_cast<int>(wrapped.size()) > rowsAvail) {
        wrapped.resize(static_cast<std::size_t>(rowsAvail));
        if (!wrapped.back().empty()) {
            wrapped.back() += " ...";
        }
    }
    for (std::size_t i = 0; i < wrapped.size(); ++i) {
        drawText(target, 2 * margin, textTop + rowStep * static_cast<int>(i), wrapped[i],
                 Rgb{0.88F, 0.86F, 0.80F}, 0.92F, scale);
    }
}

}  // namespace

void drawCreation(Framebuffer& target, const CreationFlow& flow) {
    // THE SAME NEAR-BLACK dialogue_view.cpp's own panel colour, so the
    // untouched centre the HUD rule leaves clear on every other page reads
    // here as a deliberate backdrop and not an uncleared buffer -- this
    // screen has no world and no face to leave the middle open FOR.
    target.clear(Rgb{0.04F, 0.04F, 0.05F});
    // INNOVATION SPRINT ITEM #4. THE VOID'S OWN ANCHORING, BEFORE THE PANEL.
    // See drawVoidAnchor's own header. Drawn first so the panel's opaque top
    // and bottom bands paint over whatever the pool and the border put under
    // them, exactly as the world's own signage draws under the HUD. STILL
    // NEVER EASES OPEN -- task #92 keeps this file's own documented rule.
    drawVoidAnchor(target);
    const DialogueViewState view = flow.view();
    // Task #92's centre furniture, between the anchor and the panel.
    drawCreationCentre(target, flow, view);
    drawDialogue(target, view);
}

}  // namespace granadad::render
