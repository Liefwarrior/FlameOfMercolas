#include "granadad/sim/chargen_raws.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <utility>

#include <nlohmann/json.hpp>

#include "granadad/sim/barks.hpp"  // foldToAscii -- see stringField below
#include "granadad/sim/faction.hpp"
#include "granadad/sim/notables.hpp"
#include "granadad/sim/social.hpp"

namespace granadad::sim {

namespace {

[[nodiscard]] nlohmann::json readJson(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return nlohmann::json{};
    }
    std::ostringstream text;
    text << file.rdbuf();
    nlohmann::json document = nlohmann::json::parse(text.str(), nullptr, false);
    if (document.is_discarded()) {
        return nlohmann::json{};
    }
    return document;
}

[[nodiscard]] std::string stringField(const nlohmann::json& node, const char* key) {
    const auto found = node.find(key);
    if (found == node.end() || !found->is_string()) {
        return {};
    }
    // Folded on the way in, notables.cpp's own rule for its bios: every prompt
    // and answer here ends up on the creation screens, and the owner's files
    // carry UTF-8 em-dashes the 4x6 font cannot draw. Ids and skill names are
    // 7-bit already, so folding the one funnel every string field passes
    // through changes nothing the validators compare.
    return foldToAscii(found->get<std::string>());
}

[[nodiscard]] std::int32_t intField(const nlohmann::json& node, const char* key,
                                    std::int32_t fallback = 0) {
    const auto found = node.find(key);
    if (found == node.end() || !found->is_number_integer()) {
        return fallback;
    }
    return found->get<std::int32_t>();
}

/// Reads a tier array of skill ids. Shape errors are reported into `errors`
/// -- the loud stance, not companions.cpp's skip-and-carry-on, see the
/// header on why chargen content refuses harder than a companion preset.
void readTier(const nlohmann::json& node, const char* key, std::vector<std::string>& out,
              const std::string& where, std::vector<std::string>& errors) {
    const auto found = node.find(key);
    if (found == node.end() || !found->is_array()) {
        errors.push_back(where + ": missing or non-array '" + key + "' tier");
        return;
    }
    for (const nlohmann::json& entry : *found) {
        if (!entry.is_string() || entry.get<std::string>().empty()) {
            errors.push_back(where + ": non-string entry in '" + key + "' tier");
            continue;
        }
        out.push_back(entry.get<std::string>());
    }
}

/// Sums pairs into a sorted-by-key accumulator vector.
void foldPair(std::vector<std::pair<std::string, std::int32_t>>& into, const std::string& key,
              std::int32_t amount) {
    const auto found = std::lower_bound(
        into.begin(), into.end(), key,
        [](const std::pair<std::string, std::int32_t>& entry, const std::string& probe) {
            return entry.first < probe;
        });
    if (found != into.end() && found->first == key) {
        found->second += amount;
        return;
    }
    into.insert(found, {key, amount});
}

}  // namespace

// ---------------------------------------------------------------------------
// axes
// ---------------------------------------------------------------------------

std::string_view chargenAxisCode(ChargenAxis axis) noexcept {
    switch (axis) {
        case ChargenAxis::A:
            return "A";
        case ChargenAxis::B:
            return "B";
        case ChargenAxis::C:
            return "C";
    }
    return "?";
}

std::optional<ChargenAxis> chargenAxisFromCode(std::string_view code) noexcept {
    if (code == "A") {
        return ChargenAxis::A;
    }
    if (code == "B") {
        return ChargenAxis::B;
    }
    if (code == "C") {
        return ChargenAxis::C;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// callings
// ---------------------------------------------------------------------------

std::filesystem::path callingRawsPath(const std::filesystem::path& contentDir) {
    return contentDir / "raws" / "chargen" / "callings.json";
}

std::filesystem::path quizRawsPath(const std::filesystem::path& contentDir) {
    return contentDir / "raws" / "chargen" / "questions.json";
}

std::filesystem::path biographyRawsPath(const std::filesystem::path& contentDir) {
    return contentDir / "raws" / "chargen" / "biography.json";
}

bool CallingTemplate::designateInto(Chargen& sheet, const SkillTrack& raws) const {
    // Through the public, refusing calls only -- see the header. Order does
    // not matter to Chargen; tier by tier reads like the sheet does.
    for (const std::string& id : primary) {
        if (!sheet.designate(id, SkillDesignation::Primary, raws)) {
            return false;
        }
    }
    for (const std::string& id : major) {
        if (!sheet.designate(id, SkillDesignation::Major, raws)) {
            return false;
        }
    }
    for (const std::string& id : minor) {
        if (!sheet.designate(id, SkillDesignation::Minor, raws)) {
            return false;
        }
    }
    for (std::size_t i = 0; i < kAttributeCount; ++i) {
        const std::int32_t spend = attributeSpend[i];
        if (spend == 0) {
            continue;
        }
        if (!sheet.spendAttributePoints(static_cast<AttributeId>(i), spend)) {
            return false;
        }
    }
    return true;
}

CallingRegistry CallingRegistry::load(const std::filesystem::path& contentDir,
                                      const SkillTrack& skills) {
    return loadFromFile(callingRawsPath(contentDir), skills);
}

CallingRegistry CallingRegistry::loadFromFile(const std::filesystem::path& file,
                                              const SkillTrack& skills) {
    CallingRegistry out;
    const nlohmann::json document = readJson(file);
    if (!document.is_object()) {
        return out;
    }
    const auto rows = document.find("callings");
    if (rows == document.end() || !rows->is_array()) {
        return out;
    }

    for (const nlohmann::json& node : *rows) {
        if (!node.is_object()) {
            out.errors_.push_back("callings.json: non-object calling row");
            continue;
        }
        CallingTemplate calling;
        calling.id = stringField(node, "id");
        if (calling.id.empty()) {
            out.errors_.push_back("callings.json: calling with no id");
            continue;
        }
        const std::string where = "callings.json: " + calling.id;
        if (out.find(calling.id) != nullptr) {
            out.errors_.push_back(where + ": duplicate calling id");
            continue;
        }
        calling.name = stringField(node, "name");
        calling.oneLine = stringField(node, "oneLine");
        calling.anchor = stringField(node, "anchor");

        // The axis codes, the doc's own (dominant/secondary) notation.
        const auto axis = node.find("axis");
        if (axis == node.end() || !axis->is_object()) {
            out.errors_.push_back(where + ": missing axis block");
        } else {
            const auto dominant = chargenAxisFromCode(stringField(*axis, "dominant"));
            if (!dominant.has_value()) {
                out.errors_.push_back(where + ": bad dominant axis code '" +
                                      stringField(*axis, "dominant") + "'");
            } else {
                calling.dominantAxis = *dominant;
            }
            const std::string secondary = stringField(*axis, "secondary");
            if (!secondary.empty()) {
                const auto parsed = chargenAxisFromCode(secondary);
                if (!parsed.has_value() || (dominant.has_value() && parsed == dominant)) {
                    out.errors_.push_back(where + ": bad secondary axis code '" + secondary + "'");
                } else {
                    calling.secondaryAxis = parsed;
                }
            }
        }

        readTier(node, "primary", calling.primary, where, out.errors_);
        readTier(node, "major", calling.major, where, out.errors_);
        readTier(node, "minor", calling.minor, where, out.errors_);

        // The sheet's shape, against chargen.hpp's own slot counts. Short
        // rows are legal; overfull ones are not.
        if (static_cast<std::int32_t>(calling.primary.size()) != kPrimarySkillSlots) {
            out.errors_.push_back(where + ": primary tier must fill exactly its slots");
        }
        if (static_cast<std::int32_t>(calling.major.size()) != kMajorSkillSlots) {
            out.errors_.push_back(where + ": major tier must fill exactly its slots");
        }
        if (static_cast<std::int32_t>(calling.minor.size()) > kMinorSkillSlots) {
            out.errors_.push_back(where + ": minor tier overfull");
        }

        // Every named skill: known to the raws, not THE FLAME, named once.
        std::vector<std::string> seen;
        for (const std::vector<std::string>* tier :
             {&calling.primary, &calling.major, &calling.minor}) {
            for (const std::string& id : *tier) {
                if (skills.find(id) == nullptr) {
                    out.errors_.push_back(where + ": unknown skill id '" + id + "'");
                }
                if (skills.aptitudeTier(id) == AptitudeTier::Flame) {
                    out.errors_.push_back(where + ": '" + id + "' never lands on a sheet");
                }
                if (std::find(seen.begin(), seen.end(), id) != seen.end()) {
                    out.errors_.push_back(where + ": skill '" + id + "' named twice");
                }
                seen.push_back(id);
            }
        }

        // The spend: exactly the pool, nothing over the cap, nothing
        // negative -- Chargen::spendAttributePoints's own refusals, checked
        // here so a calling can never strand the review screen.
        const auto spend = node.find("attributeBonusSpend");
        if (spend == node.end() || !spend->is_object()) {
            out.errors_.push_back(where + ": missing attributeBonusSpend");
        } else {
            static constexpr std::array<const char*, kAttributeCount> kKeys = {"MGT", "AGI", "VIG",
                                                                               "WIT"};
            std::int32_t total = 0;
            for (std::size_t i = 0; i < kAttributeCount; ++i) {
                const std::int32_t value = intField(*spend, kKeys[i]);
                calling.attributeSpend[i] = value;
                total += value;
                if (value < 0 || value > kAttributeBonusPerAttributeCap) {
                    out.errors_.push_back(where + ": " + kKeys[i] + " spend outside [0, cap]");
                }
            }
            if (total != kAttributeBonusPool) {
                out.errors_.push_back(where + ": attribute spend does not sum to the pool");
            }
        }

        out.callings_.push_back(std::move(calling));
    }

    // REFUSE WHOLE, see the header: a roster with a hole where a calling
    // should be is a different roster than the owner authored.
    if (!out.errors_.empty()) {
        out.callings_.clear();
    }
    return out;
}

const CallingTemplate* CallingRegistry::find(std::string_view id) const noexcept {
    for (const CallingTemplate& calling : callings_) {
        if (calling.id == id) {
            return &calling;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// the quiz
// ---------------------------------------------------------------------------

ChargenQuiz ChargenQuiz::load(const std::filesystem::path& contentDir,
                              const CallingRegistry& callings) {
    return loadFromFile(quizRawsPath(contentDir), callings);
}

ChargenQuiz ChargenQuiz::loadFromFile(const std::filesystem::path& file,
                                      const CallingRegistry& callings) {
    ChargenQuiz out;
    const nlohmann::json document = readJson(file);
    if (!document.is_object()) {
        return out;
    }

    // The three identity cards, in axis order.
    const auto axes = document.find("axes");
    if (axes == document.end() || !axes->is_array() || axes->size() != kChargenAxisCount) {
        out.errors_.push_back("questions.json: 'axes' must name exactly the three axes");
    } else {
        std::array<bool, kChargenAxisCount> seen{};
        for (const nlohmann::json& node : *axes) {
            ChargenAxisIdentity identity;
            const auto axis = chargenAxisFromCode(stringField(node, "code"));
            if (!axis.has_value()) {
                out.errors_.push_back("questions.json: axis with bad code '" +
                                      stringField(node, "code") + "'");
                continue;
            }
            if (seen[static_cast<std::size_t>(*axis)]) {
                out.errors_.push_back("questions.json: axis code repeated");
                continue;
            }
            seen[static_cast<std::size_t>(*axis)] = true;
            identity.axis = *axis;
            identity.id = stringField(node, "id");
            identity.name = stringField(node, "name");
            if (identity.id.empty() || identity.name.empty()) {
                out.errors_.push_back("questions.json: axis missing id or name");
            }
            out.axes_.push_back(std::move(identity));
        }
        std::sort(out.axes_.begin(), out.axes_.end(),
                  [](const ChargenAxisIdentity& a, const ChargenAxisIdentity& b) {
                      return a.axis < b.axis;
                  });
    }

    // The questions: each with exactly one answer per axis.
    const auto questions = document.find("questions");
    if (questions == document.end() || !questions->is_array()) {
        out.errors_.push_back("questions.json: missing 'questions'");
    } else {
        std::int32_t number = 0;
        for (const nlohmann::json& node : *questions) {
            ++number;
            const std::string where = "questions.json: Q" + std::to_string(number);
            QuizQuestion question;
            question.prompt = stringField(node, "prompt");
            if (question.prompt.empty()) {
                out.errors_.push_back(where + ": empty prompt");
            }
            const auto answers = node.find("answers");
            if (answers == node.end() || !answers->is_array()) {
                out.errors_.push_back(where + ": missing answers");
                continue;
            }
            std::array<std::int32_t, kChargenAxisCount> perAxis{};
            for (const nlohmann::json& row : *answers) {
                QuizAnswer answer;
                const auto axis = chargenAxisFromCode(stringField(row, "axis"));
                if (!axis.has_value()) {
                    out.errors_.push_back(where + ": answer with bad axis code");
                    continue;
                }
                answer.axis = *axis;
                ++perAxis[static_cast<std::size_t>(*axis)];
                answer.text = stringField(row, "text");
                if (answer.text.empty()) {
                    out.errors_.push_back(where + ": empty answer text");
                }
                question.answers.push_back(std::move(answer));
            }
            if (perAxis != std::array<std::int32_t, kChargenAxisCount>{1, 1, 1}) {
                out.errors_.push_back(where + ": must score each axis exactly once");
            }
            out.questions_.push_back(std::move(question));
        }
    }

    // The verdict table, cross-checked against the callings TWICE: the id
    // must exist, and its own authored axis codes must agree with the cell.
    out.pureAt_ = 6;
    const auto verdicts = document.find("verdicts");
    if (verdicts == document.end() || !verdicts->is_object()) {
        out.errors_.push_back("questions.json: missing 'verdicts'");
    } else {
        out.pureAt_ = intField(*verdicts, "pureAt", 6);
        if (out.pureAt_ < 1) {
            out.errors_.push_back("questions.json: pureAt must be at least 1");
        }
        for (std::size_t d = 0; d < kChargenAxisCount; ++d) {
            const ChargenAxis dominant = static_cast<ChargenAxis>(d);
            const std::string code(chargenAxisCode(dominant));
            const auto row = verdicts->find(code);
            if (row == verdicts->end() || !row->is_object()) {
                out.errors_.push_back("questions.json: verdicts missing axis " + code);
                continue;
            }
            QuizVerdictRow& into = out.verdicts_[d];
            const auto check = [&](const std::string& cell, const std::string& callingId,
                                   std::optional<ChargenAxis> wantSecondary) {
                const CallingTemplate* calling = callings.find(callingId);
                if (calling == nullptr) {
                    out.errors_.push_back("questions.json: verdict " + cell +
                                          " names unknown calling '" + callingId + "'");
                    return;
                }
                if (calling->dominantAxis != dominant ||
                    calling->secondaryAxis != wantSecondary) {
                    out.errors_.push_back("questions.json: verdict " + cell + " and calling '" +
                                          callingId + "' disagree about its axes");
                }
            };
            into.pure = stringField(*row, "pure");
            check(code + ".pure", into.pure, std::nullopt);
            for (std::size_t s = 0; s < kChargenAxisCount; ++s) {
                if (s == d) {
                    continue;
                }
                const ChargenAxis secondary = static_cast<ChargenAxis>(s);
                const std::string key = "with" + std::string(chargenAxisCode(secondary));
                into.withSecondary[s] = stringField(*row, key.c_str());
                check(code + "." + key, into.withSecondary[s], secondary);
            }
        }
    }

    if (!out.errors_.empty()) {
        out.questions_.clear();
        out.axes_.clear();
    }
    return out;
}

std::optional<QuizTally> tallyQuiz(const ChargenQuiz& quiz,
                                   const std::vector<std::int32_t>& chosenAnswers) {
    const std::vector<QuizQuestion>& questions = quiz.questions();
    if (questions.empty() || chosenAnswers.size() != questions.size()) {
        return std::nullopt;
    }

    QuizTally tally;
    std::optional<ChargenAxis> lastAxis;
    for (std::size_t i = 0; i < questions.size(); ++i) {
        const std::int32_t chosen = chosenAnswers[i];
        if (chosen < 0 ||
            chosen >= static_cast<std::int32_t>(questions[i].answers.size())) {
            return std::nullopt;
        }
        const ChargenAxis axis = questions[i].answers[static_cast<std::size_t>(chosen)].axis;
        ++tally.counts[static_cast<std::size_t>(axis)];
        lastAxis = axis;
    }

    // Dominant: the highest count. Tie: the tied axis the last question's
    // answer scored; else fixed order A > B > C -- which is exactly what the
    // ascending scan below leaves standing when the last-answer rule does
    // not bite.
    std::int32_t best = -1;
    for (std::size_t i = 0; i < kChargenAxisCount; ++i) {
        if (tally.counts[i] > best) {
            best = tally.counts[i];
            tally.dominant = static_cast<ChargenAxis>(i);
        }
    }
    if (lastAxis.has_value() &&
        tally.counts[static_cast<std::size_t>(*lastAxis)] == best) {
        tally.dominant = *lastAxis;
    }

    const QuizVerdictRow& row = quiz.verdictRow(tally.dominant);
    tally.pure = best >= quiz.pureAt();
    if (tally.pure) {
        tally.calling = row.pure;
        return tally;
    }

    // The secondary: of the two other axes, the higher count -- and on a
    // tie, the EARLIER axis in A/B/C order, which is the doc's own ">=" in
    // every row of the table ("B >= C -> Deckhand").
    std::optional<ChargenAxis> secondary;
    std::int32_t secondBest = -1;
    for (std::size_t i = 0; i < kChargenAxisCount; ++i) {
        if (static_cast<ChargenAxis>(i) == tally.dominant) {
            continue;
        }
        if (tally.counts[i] > secondBest) {
            secondBest = tally.counts[i];
            secondary = static_cast<ChargenAxis>(i);
        }
    }
    tally.calling = row.withSecondary[static_cast<std::size_t>(*secondary)];
    return tally;
}

// ---------------------------------------------------------------------------
// the biography
// ---------------------------------------------------------------------------

std::string_view chargenEffectKindId(ChargenEffectKind kind) noexcept {
    switch (kind) {
        case ChargenEffectKind::SkillDelta:
            return "skillDelta";
        case ChargenEffectKind::CoinDelta:
            return "coinDelta";
        case ChargenEffectKind::FactionStanding:
            return "factionStanding";
        case ChargenEffectKind::ActorDisposition:
            return "actorDisposition";
        case ChargenEffectKind::Heat:
            return "heat";
        case ChargenEffectKind::HpMax:
            return "hpMax";
        case ChargenEffectKind::DaggerPoints:
            return "daggerPoints";
    }
    return "?";
}

std::optional<ChargenEffectKind> chargenEffectKindFromId(std::string_view id) noexcept {
    for (std::uint8_t k = 0; k <= static_cast<std::uint8_t>(ChargenEffectKind::DaggerPoints);
         ++k) {
        const ChargenEffectKind kind = static_cast<ChargenEffectKind>(k);
        if (chargenEffectKindId(kind) == id) {
            return kind;
        }
    }
    return std::nullopt;
}

BiographyRegistry BiographyRegistry::load(const std::filesystem::path& contentDir,
                                          const SkillTrack& skills,
                                          const FactionRegistry& factions,
                                          const NotableRegistry& notables) {
    return loadFromFile(biographyRawsPath(contentDir), skills, factions, notables);
}

BiographyRegistry BiographyRegistry::loadFromFile(const std::filesystem::path& file,
                                                  const SkillTrack& skills,
                                                  const FactionRegistry& factions,
                                                  const NotableRegistry& notables) {
    BiographyRegistry out;
    const nlohmann::json document = readJson(file);
    if (!document.is_object()) {
        return out;
    }
    const auto questions = document.find("questions");
    if (questions == document.end() || !questions->is_array()) {
        return out;
    }

    for (const nlohmann::json& node : *questions) {
        if (!node.is_object()) {
            out.errors_.push_back("biography.json: non-object question row");
            continue;
        }
        BiographyQuestion question;
        question.id = stringField(node, "id");
        question.prompt = stringField(node, "prompt");
        const std::string where = "biography.json: " +
                                  (question.id.empty() ? std::string("(no id)") : question.id);
        if (question.id.empty() || question.prompt.empty()) {
            out.errors_.push_back(where + ": missing id or prompt");
        }

        const auto answers = node.find("answers");
        if (answers == node.end() || !answers->is_array() || answers->empty()) {
            out.errors_.push_back(where + ": missing answers");
            continue;
        }
        char letter = 'a';
        for (const nlohmann::json& row : *answers) {
            const std::string at = where + std::string(1, letter++);
            BiographyAnswer answer;
            answer.text = stringField(row, "text");
            answer.note = stringField(row, "note");
            if (answer.text.empty()) {
                out.errors_.push_back(at + ": empty answer text");
            }

            std::int32_t factionSum = 0;
            const auto effects = row.find("effects");
            if (effects != row.end() && effects->is_array()) {
                for (const nlohmann::json& fx : *effects) {
                    ChargenEffect effect;
                    const std::string kindId = stringField(fx, "kind");
                    const auto kind = chargenEffectKindFromId(kindId);
                    if (!kind.has_value()) {
                        // THE CLOSED VOCABULARY'S TEETH: an id this build has
                        // no lever for refuses the file, never ships as a
                        // silent no-op promise.
                        out.errors_.push_back(at + ": unknown effect kind '" + kindId + "'");
                        continue;
                    }
                    effect.kind = *kind;
                    effect.target = stringField(fx, "target");
                    effect.amount = intField(fx, "amount");

                    switch (effect.kind) {
                        case ChargenEffectKind::SkillDelta:
                            if (skills.find(effect.target) == nullptr) {
                                out.errors_.push_back(at + ": unknown skill '" + effect.target +
                                                      "'");
                            } else if (skills.aptitudeTier(effect.target) ==
                                       AptitudeTier::Flame) {
                                out.errors_.push_back(at + ": '" + effect.target +
                                                      "' never lands on a sheet");
                            }
                            break;
                        case ChargenEffectKind::FactionStanding:
                            if (factions.indexOf(effect.target) < 0) {
                                out.errors_.push_back(at + ": unknown faction '" +
                                                      effect.target + "'");
                            }
                            factionSum += effect.amount;
                            break;
                        case ChargenEffectKind::ActorDisposition:
                            if (notables.find(effect.target) == nullptr) {
                                out.errors_.push_back(at + ": unknown notable '" +
                                                      effect.target + "'");
                            }
                            break;
                        case ChargenEffectKind::CoinDelta:
                        case ChargenEffectKind::Heat:
                        case ChargenEffectKind::HpMax:
                        case ChargenEffectKind::DaggerPoints:
                            if (!effect.target.empty()) {
                                out.errors_.push_back(at + ": '" + kindId +
                                                      "' takes no target");
                            }
                            break;
                    }
                    if ((effect.kind == ChargenEffectKind::SkillDelta ||
                         effect.kind == ChargenEffectKind::FactionStanding ||
                         effect.kind == ChargenEffectKind::ActorDisposition) &&
                        effect.target.empty()) {
                        out.errors_.push_back(at + ": '" + kindId + "' needs a target");
                    }
                    answer.effects.push_back(std::move(effect));
                }
            }

            // The zero-sum bookkeeping, at the authoring seam the doc chose.
            if (factionSum != 0) {
                out.errors_.push_back(at + ": faction deltas sum to " +
                                      std::to_string(factionSum) + ", not zero");
            }
            question.answers.push_back(std::move(answer));
        }
        out.questions_.push_back(std::move(question));
    }

    if (!out.errors_.empty()) {
        out.questions_.clear();
    }
    return out;
}

// ---------------------------------------------------------------------------
// the accumulator
// ---------------------------------------------------------------------------

void accumulateEffects(ChargenEffects& into, const std::vector<ChargenEffect>& effects) {
    for (const ChargenEffect& effect : effects) {
        switch (effect.kind) {
            case ChargenEffectKind::SkillDelta:
                foldPair(into.skillDeltas, effect.target, effect.amount);
                break;
            case ChargenEffectKind::CoinDelta:
                into.coinDelta += effect.amount;
                break;
            case ChargenEffectKind::FactionStanding:
                foldPair(into.factionStandings, effect.target, effect.amount);
                break;
            case ChargenEffectKind::ActorDisposition:
                foldPair(into.dispositionSeeds, effect.target, effect.amount);
                break;
            case ChargenEffectKind::Heat:
                into.heat += effect.amount;
                break;
            case ChargenEffectKind::HpMax:
                into.hpMaxDelta += effect.amount;
                break;
            case ChargenEffectKind::DaggerPoints:
                into.daggerPoints += effect.amount;
                break;
        }
    }
}

std::optional<ChargenEffects> accumulateBiography(
    const BiographyRegistry& biography, const std::vector<std::int32_t>& chosenAnswers) {
    const std::vector<BiographyQuestion>& questions = biography.questions();
    if (questions.empty() || chosenAnswers.size() != questions.size()) {
        return std::nullopt;
    }
    ChargenEffects out;
    for (std::size_t i = 0; i < questions.size(); ++i) {
        const std::int32_t chosen = chosenAnswers[i];
        if (chosen < 0 ||
            chosen >= static_cast<std::int32_t>(questions[i].answers.size())) {
            return std::nullopt;
        }
        accumulateEffects(out, questions[i].answers[static_cast<std::size_t>(chosen)].effects);
    }
    return out;
}

std::int32_t applySkillDeltas(SkillTrack& track, const ChargenEffects& effects) {
    std::int32_t known = 0;
    for (const auto& [id, delta] : effects.skillDeltas) {
        // level(id) is the DESIGNATED START the sheet just wrote (or 0 for a
        // skill the sheet never touched); setLevel clamps into [0, 100] the
        // same as every other caller.
        if (track.setLevel(id, track.level(id) + delta)) {
            ++known;
        }
    }
    return known;
}

// ---------------------------------------------------------------------------
// dagger points -> Q8
// ---------------------------------------------------------------------------

std::int32_t daggerMultiplierQ8ForPoints(std::int32_t points) noexcept {
    const std::int32_t p = std::clamp(points, kDaggerPointsMin, kDaggerPointsMax);
    // 1.0 + 0.05 x P == (20 + P) / 20, in Q8 with half-up rounding. At P = 0
    // this is (5120 + 10) / 20 == 256 exactly -- the neutral guarantee.
    const std::int32_t q8 = (kDaggerNeutralQ8 * (20 + p) + 10) / 20;
    return std::clamp(q8, kDaggerMinQ8, kDaggerMaxQ8);
}

}  // namespace granadad::sim
