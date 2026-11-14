// Scripted capture drivers: flame.
#include "scripted.hpp"

namespace granadad::render {

namespace scripted {

/// THE SCRIPTED PLAYTHROUGH the sprint is judged on, driven through exactly the
/// calls a keypress makes: walk to Father Maell, take the oath, stand three
/// people a drink, turn the night pot in, ask Captain Wake about the water,
/// bring it back, be taught a crafting, and compose one.
///
/// It reports how many stages actually landed rather than asserting anything --
/// the assertions live in the test suite, where a red is a red. This is the
/// path that produces a PICTURE of it.
[[nodiscard]] int runFlameLine(Session& session, const std::string& ending) {
    const sim::DialogueDirector& talk = session.tavern().dialogue();
    const std::string questId = "flame-disciple";

    // 1. the oath, at Maell's evening table
    if (speakTo(session, "Father Maell")) {
        pick(session, sim::TopicKind::Join);
    }
    // 2. the night pot: three drinks out of the player's own purse, then turned
    //    in to the priest.
    for (int i = 0; i < 3; ++i) {
        if (session.talking()) {
            pick(session, sim::TopicKind::BuyDrinkFor);
        }
    }
    if (session.talking()) {
        pick(session, sim::TopicKind::QuestBeat);
    }
    // 3. the captain, who was out past the fishbone the night of it.
    if (speakTo(session, "Captain Ivo Wake")) {
        pick(session, sim::TopicKind::QuestBeat);
    }
    // 4. back to the priest with it, 5. be taught, 6. compose.
    if (speakTo(session, "Father Maell")) {
        pick(session, sim::TopicKind::QuestBeat);
        pick(session, sim::TopicKind::Learn);
        // 7. Keep sitting with him. Every crafting off the shallow shelf is
        //    worth two uses of linkcraft, and the Mission's third rung is
        //    measured in exactly that: the workshop opens to somebody who has
        //    learned everything the public edition can teach. Bounded, because
        //    the topic stays on the list after there is nothing left to hand
        //    over and answers with the authored teaching.beyond line.
        for (int i = 0; i < 16; ++i) {
            if (!pick(session, sim::TopicKind::Learn)) {
                break;
            }
        }
        for (int i = 0; i < 4; ++i) {
            if (!pick(session, sim::TopicKind::Advance)) {
                break;
            }
        }
        if (pick(session, sim::TopicKind::Forge)) {
            // The smallest legal composition a novice can hold: one point of
            // vitality across a bridged link, which is the bench's own opening
            // shape. Committed with the same call the ENTER key makes.
            session.commitForge();
        }
    }
    // Compose the shot: back off the priest, looking at him, with whatever he
    // last said still on the panel.
    if (ending == "bench") {
        // The workshop an Acolyte's rung opened, standing open. Nothing is
        // committed: this is the bench mid-composition, which is the thing
        // worth photographing.
        pick(session, sim::TopicKind::Forge);
    } else if (ending == "away") {
        session.closeConversation();
    }
    standBackFrom(session, "Father Maell");
    return talk.journal().stagesDone(questId);
}

/// HELD-EFFECTS BUILD. Equips one crafting BY ID -- through the same
/// grimoire-order door the number row uses -- and presses Cast until its
/// link opens, waiting out fizzle and success cooldowns through real steps.
/// Returns true when the crafting's hold is genuinely live on the player at
/// the end, which is the thing the capture exists to photograph.
[[nodiscard]] bool runHeldCast(Session& session, const std::string& spellId) {
    // The index is looked up fresh: ids sort ascending in the grimoire, so an
    // index is only good the moment it is asked for.
    const std::vector<sim::Spell>& spells = session.tavern().dialogue().grimoire().spells();
    std::int32_t index = -1;
    for (std::size_t i = 0; i < spells.size(); ++i) {
        if (spells[i].id == spellId) {
            index = static_cast<std::int32_t>(i);
            break;
        }
    }
    if (index < 0 || !session.tavern().equipSpellAt(index)) {
        return false;
    }
    const auto held = [&]() {
        for (const sim::Tavern::ActiveHold& hold : session.tavern().heldEffects()) {
            if (hold.spellId == spellId) {
                return true;
            }
        }
        return false;
    };
    for (int attempt = 0; attempt < 12 && !held(); ++attempt) {
        // Wait out whatever the last press left cooling -- a prior crafting's
        // recovery, or this one's own fizzle -- through real steps, bounded so
        // a pathological clock cannot hang the capture.
        for (int waited = 0; session.tavern().castCooldownLeft() > 0 && waited < 600;
             ++waited) {
            session.stepMany(sim::MoveInput{}, 60);
        }
        session.castEquipped();
    }
    return held();
}

}  // namespace scripted

}  // namespace granadad::render
