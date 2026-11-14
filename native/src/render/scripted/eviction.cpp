// Scripted capture drivers: eviction.
#include "scripted.hpp"

namespace granadad::render {

namespace scripted {

/// EVICTION CASE. PLAYS THE OWNER'S THIRD CASE END TO END, both paths, through
/// exactly the Session verbs a keypress makes -- speakTo/pick for the hire and
/// the yield, walkAcrossDistrict for the ward crossings, skipToHour for the
/// evening the rota brings the family home, interact for the knock and the
/// serve. Nothing reaches into the sim sideways: the open-hand skill is set
/// the one honest way a hired hand has it (the brawl does not train it yet --
/// the flagged gap), and every other beat is the real machinery.
[[nodiscard]] int runEvictionLine(Session& session, const std::string& ending) {
    gEvictBeatMask = 0;
    int landed = 0;
    std::int32_t beat = 0;
    const auto mark = [&](bool ok) {
        session.watchBeatLanded(beat, ok);
        if (ok) {
            gEvictBeatMask |= 1 << beat;
            ++landed;
        }
        ++beat;
    };
    const auto chapter = [&]() { session.watchChapter(beat); };
    const sim::CasebookRaws& raws = session.evictRaws();
    if (!raws.loaded()) {
        return 0;
    }

    // THE HIRED HAND HAS THE SKILL. The priest's bar is open hand at fifteen
    // of the hundred, the major-designation line; a hired hand qualifies, so
    // the drive seeds it the one way it can be seeded today -- chargen writes
    // this level and nothing in play yet raises it (the flagged progression
    // gap). test_eviction_line proves the gate REFUSES below the bar
    // separately, so this is arming a qualifying character, not dodging a
    // check.
    (void)session.tavern().dialogue().skills().setLevel(sim::kOpenHandSkill,
                                                        sim::kEvictionOpenHandBar);

    chapter();
    // 1. THE HIRE, at Maell's evening table in the Gull. The writ tops his
    // topic list only for the case's own priest and only while it is dormant
    // -- speakTo opens the conversation (syncEvictionTopics ran in interact),
    // pick chooses TakeWrit, and the session settles the case half.
    if (speakTo(session, "Father Maell")) {
        pick(session, sim::TopicKind::TakeWrit);
        session.closeConversation();
    }
    mark(session.evictCaseLive());

    chapter();
    // 2. THE WRIT READ. The handed document unfolds on the Letters tile the
    // moment the hire lead is heard -- no walk owed, the mission sheet's own
    // contract. A `writ` shutter stops here with it open.
    session.toggleLetters();
    session.chooseVisibleTopic(0);
    const bool writRead = !session.unlockedLetters().empty();
    mark(writRead);

    if (ending == "writ") {
        return landed;
    }
    session.toggleLetters();  // put the paper down, get the world back

    chapter();
    // 3. THE GATE. Walk east to the Netters' gate and look -- the roll and
    // the pledge, the compound the writ names. Reading it opens the roof.
    const sim::Lead& gate = raws.leads()[static_cast<std::size_t>(raws.indexOf("netters-gate"))];
    (void)walkAcrossDistrict(session, gate.site.x, gate.site.y);
    session.examine();
    mark(session.evictBook().state(raws.indexOf("netters-gate")) == sim::LeadState::Followed);

    if (ending == "gate") {
        session.toggleCasebook();
        return landed;
    }

    chapter();
    // 4. THE WAIT. To eight in the evening, when the rota has walked the
    // family home off the quay -- skipToHour, the clock a WAIT pick spends,
    // the brief's "once people are home" reached the way the game reaches it.
    session.skipToHour(20);
    mark(session.timeOfDay() / 3600 == 20);

    chapter();
    // 5. THE DOOR LEAD. Walk to the Marrow door on the Gullet lane and look:
    // the family, the four heads, the arrears -- the case's turn read as a
    // place stood over, and the lead the knock verb watches for.
    const sim::Lead& door = raws.leads()[static_cast<std::size_t>(raws.indexOf("family-door"))];
    (void)walkAcrossDistrict(session, door.site.x, door.site.y);
    session.examine();
    mark(session.evictBook().state(raws.indexOf("family-door")) == sim::LeadState::Followed);

    chapter();
    // 6. THE KNOCK. Stand at the door in the evening and press interact --
    // evictDoorReady is live (case running, writ unserved, in reach, the
    // hour home), so the first press knocks and the door answers.
    walkToTile(session, door.site.x, door.site.y);
    session.interact();
    mark(session.evictKnocked());

    if (ending == "knock") {
        return landed;
    }

    // THE FORK. "refused" is the disrupt path: carry the writ back to Maell
    // whole and give it up. Everything else serves.
    if (ending == "refused") {
        chapter();
        // 7r. THE WALK BACK, UNSERVED, and the yield in the priest's own
        // conversation -- YieldWrit tops his list only while the case runs
        // unserved. Maell keeps his evening hour in the Gull, so the
        // walk-back is to wherever his body actually stands: the district
        // router carries the long leg, and speakTo makes the final approach
        // and opens the conversation. The book closes on the stood-down
        // lead; no weapon.
        if (const sim::Actor* priest = actorNamed(session, "Father Maell")) {
            (void)walkAcrossDistrict(session, priest->tileX(), priest->tileY());
        }
        if (speakTo(session, "Father Maell")) {
            pick(session, sim::TopicKind::YieldWrit);
            session.closeConversation();
        }
        mark(session.evictBook().closed());
        // 8r. AND NO EVICTOR: the disrupt path's whole point, asserted as a
        // beat so a run that quietly armed the player would go red.
        mark(session.tavern().playerWeapon() == sim::Weapon::Fists);
        return landed;
    }

    chapter();
    // 7. THE SERVE. A second press at the answered door: the paper changes
    // hands, nobody swings, and the objective swings to the Mission.
    session.interact();
    mark(session.writServed());

    if (ending == "served") {
        return landed;
    }

    chapter();
    // 8. TO THE MISSION, THE WRIT SIGNED. Walking into the back room IS the
    // delivery -- stepEvictCase closes the book and pays The Evictor on
    // arrival, no press owed -- so the run steps the pump a beat at the
    // anchor to let that fire.
    const sim::Lead& close = raws.leads()[static_cast<std::size_t>(raws.indexOf("served"))];
    (void)walkAcrossDistrict(session, close.site.x, close.site.y);
    for (int guard = 0; guard < 8 && !session.evictBook().closed(); ++guard) {
        session.stepMany(sim::MoveInput{}, 1);
    }
    mark(session.evictBook().closed());

    chapter();
    // 9. THE REWARD IN HAND. The Evictor granted through the one seam -- the
    // participate path's payoff, asserted so the grant cannot silently drop.
    mark(session.tavern().playerWeapon() != sim::Weapon::Fists);
    return landed;
}

}  // namespace scripted

}  // namespace granadad::render
