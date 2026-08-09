#include "granadad/render/creation.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

#include "granadad/render/framebuffer.hpp"

namespace granadad::render {

namespace {

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
    // THREE, AND ONLY THREE -- Eli named exactly these, task #80's own text.
    static const std::vector<OriginTemplate> kTemplates = {
        {"devin", "DEVIN", "SECRETIVE"},
        {"gabri", "GABRI", "NO-NONSENSE"},
        {"custom", "CUSTOM", "YOUR OWN PATH"},
    };
    return kTemplates;
}

CreationFlow::CreationFlow(const std::filesystem::path& contentDir)
    : skills_(sim::SkillTrack::load(contentDir)),
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
    name_ = chosenOrigin().name;
    // CUSTOM's "name" names the PATH, not the character -- handing it over
    // as a suggested name would read as a placeholder nobody wrote a real
    // one for, which is a worse first impression than an honestly blank
    // field.
    if (chosenOrigin().id == "custom") {
        name_.clear();
    }
    nameIsDefault_ = true;
}

void CreationFlow::chooseOrigin() noexcept {
    loadOriginDefaultName();
    step_ = CreationStep::Customize;
    customizeCursor_ = 0;
    editingName_ = false;
}

const OriginTemplate& CreationFlow::chosenOrigin() const noexcept {
    return originTemplates()[static_cast<std::size_t>(originCursor_)];
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
                return label + "  LV " + std::to_string(companion->startingLevel(row.skillId));
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
    return "PRIMARY " + std::to_string(chargen_.slotsFilled(sim::SkillDesignation::Primary)) + "/" +
          std::to_string(sim::kPrimarySkillSlots) + "  MAJOR " +
          std::to_string(chargen_.slotsFilled(sim::SkillDesignation::Major)) + "/" +
          std::to_string(sim::kMajorSkillSlots) + "  MINOR " +
          std::to_string(chargen_.slotsFilled(sim::SkillDesignation::Minor)) + "/" +
          std::to_string(sim::kMinorSkillSlots) + "  POINTS " +
          std::to_string(chargen_.attributePointsRemaining()) + " LEFT";
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
        state.speaker = "NEW GAME";
        state.epithet = "CHOOSE YOUR ORIGIN";
        const OriginTemplate& hovered = originTemplates()[static_cast<std::size_t>(originCursor_)];
        // THE REAL VOICE WHEN THERE IS ONE. companions.hpp's own header
        // names this exact lookup as the "remaining integration work" its
        // content was authored ahead of -- epithet() rather than the much
        // longer bio() because the top band has a few rows before the HUD's
        // centre-clear rule starts eating them, and a paragraph silently
        // losing its back half reads as a rendering bug, not as writing.
        std::string blurb = hovered.tag;
        if (hovered.id == "devin" && devinTemplate_.loaded() && !devinTemplate_.epithet().empty()) {
            blurb += " -- " + devinTemplate_.epithet();
        } else if (hovered.id == "gabri" && gabriTemplate_.loaded() &&
                  !gabriTemplate_.epithet().empty()) {
            blurb += " -- " + gabriTemplate_.epithet();
        }
        state.line = blurb;
        // NAME ONLY ON THE CARD ITSELF. Eli's own tag alongside it --
        // "GABRI  NO-NONSENSE" -- is eighteen characters before the row
        // number even joins it, which is the topic grid's own column width
        // AT BEST and over it the moment "YOUR OWN PATH" or "NO-NONSENSE"
        // is the tag: three cards sharing one row of three columns clipped
        // two of them to "NO-NONSE." and "YOUR." the first time this screen
        // was actually looked at as a picture rather than read as code. The
        // tag and the real voice both belong in the top band above, which
        // has the room and already carries them.
        for (const OriginTemplate& t : originTemplates()) {
            state.topics.push_back(t.name);
        }
        state.cursor = originCursor_;
        state.page = topicPageOf(originCursor_);
        return state;
    }

    state.speaker = "CUSTOMIZE";
    state.epithet = chosenOrigin().name + " - " + chosenOrigin().tag;
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

void drawCreation(Framebuffer& target, const CreationFlow& flow) {
    // THE SAME NEAR-BLACK dialogue_view.cpp's own panel colour, so the
    // untouched centre the HUD rule leaves clear on every other page reads
    // here as a deliberate backdrop and not an uncleared buffer -- this
    // screen has no world and no face to leave the middle open FOR.
    target.clear(Rgb{0.04F, 0.04F, 0.05F});
    drawDialogue(target, flow.view());
}

}  // namespace granadad::render
