// Scripted capture drivers: contract.
#include "scripted.hpp"

namespace granadad::render {

namespace scripted {

/// THE SKYRUNNER LINE, played the way a player plays it and nothing reaching
/// into the simulation sideways. Sign on with Finch in the snug, take two
/// purses, crack a box above the stair, get on the roof, cross the alley, sell
/// what was taken, lean on somebody, and run a bale out past the Watch.
/// THE BOUNTY, played the way a player plays it: take it off the Watch, get the
/// Flame's mark before the priest goes home, wait for the room to quiet, and
/// bring back what the ward asked for.
///
/// Six beats, and every one of them is a Session call a keypress makes -- the
/// walk is real movement through real collision, the punches are the punch key
/// and the skinning is the same G that opens a strongbox.
[[nodiscard]] int runContractLine(Session& session, const std::string& ending) {
    sim::DialogueDirector& talk = session.tavern().dialogue();
    int landed = 0;
    std::int32_t job = -1;
    std::int32_t wanted = 0;

    // 1. ACCEPT. The ward's bounty is public work: no rung and no oath.
    if (speakTo(session, "Watchman Cull")) {
        const std::vector<sim::Topic>& topics = talk.topics();
        for (std::size_t i = 0; i < topics.size(); ++i) {
            if (topics[i].kind != sim::TopicKind::TakeContract || topics[i].payload < 0) {
                continue;
            }
            const sim::Contract* row = talk.contracts().find(topics[i].payload);
            if (row == nullptr || row->good != sim::Contraband::Scalp) {
                continue;
            }
            job = row->id;
            wanted = row->units;
            session.chooseTopic(i);
            break;
        }
        session.closeConversation();
    }
    if (job >= 0 && talk.contracts().find(job) != nullptr && talk.contracts().find(job)->live()) {
        ++landed;
    }

    // 2. THE MARK. A scalp is redeemed under the Flame's own sanction, and the
    //    priest keeps an evening hour that ends at half past nine.
    if (speakTo(session, "Father Maell")) {
        (void)pick(session, sim::TopicKind::Sanction);
        session.closeConversation();
    }
    if (job >= 0 && talk.contracts().find(job) != nullptr &&
        talk.contracts().find(job)->sanctioned) {
        ++landed;
    }

    // SHEETS BUILD: `held` STOPS HERE, WITH THE JOB STILL IN HAND. The full
    // line ends with the contract PAID, which is exactly the one state the
    // Journal tile's live-contract rows have nothing to show for -- so a
    // capture of those rows needs the line to stop while the work is still
    // owed. Two beats (taken, sanctioned), both already counted above;
    // runSmoke() asks for two instead of six when this ending is picked, so
    // a held run that landed both does not read as a line that fell short.
    if (ending == "held") {
        session.closeConversation();
        return landed;
    }

    // 3. THE HOUR. Eleven at night: the late crowd has thinned and the skirting
    //    is busy.
    session.skipToHour(23);
    if (session.tavern().verminPresent() > 0) {
        ++landed;
    }

    // 4. THE WORK. A fist, and then a knife.
    std::int32_t taken = 0;
    for (int attempt = 0; attempt < 40 && taken < wanted; ++attempt) {
        std::int32_t ratId = -1;
        std::int32_t ratX = 0;
        std::int32_t ratY = 0;
        for (const sim::Actor& actor : session.tavern().actors()) {
            if (actor.role() == sim::ActorRole::Vermin && actor.present() &&
                actor.activity() != sim::Activity::Downed) {
                ratId = actor.id();
                ratX = actor.tileX();
                ratY = actor.tileY();
                break;
            }
        }
        if (ratId < 0) {
            break;
        }
        walkToTile(session, ratX, ratY);
        for (int swing = 0; swing < 12; ++swing) {
            const sim::Actor* still = session.tavern().actorById(ratId);
            if (still == nullptr || still->activity() == sim::Activity::Downed) {
                break;
            }
            // ACTION-COMBAT BUILD: the swing is a sightline TAP with a recovery
            // lockout now, so re-approach the rat each swing (re-facing the
            // crosshair onto it, since it can scurry between blows) and let the
            // lockout clear before the next lands.
            walkToTile(session, still->tileX(), still->tileY());
            session.punch();
            session.stepMany(sim::MoveInput{}, sim::kSwingRecoverySteps + 1);
        }
        const sim::Actor* down = session.tavern().actorById(ratId);
        if (down == nullptr || down->activity() != sim::Activity::Downed) {
            // It moved off before the fist landed. Try the next one.
            continue;
        }
        walkToTile(session, down->tileX(), down->tileY());
        session.steal();
        const std::int32_t held = talk.crimes().stash().count(sim::Contraband::Scalp);
        if (held > taken) {
            taken = held;
        }
    }
    if (wanted > 0 && taken >= wanted) {
        ++landed;
    }

    // 5. GET PAID, over the same table it was taken across.
    if (speakTo(session, "Watchman Cull")) {
        (void)pick(session, sim::TopicKind::TurnIn);
    }
    if (job >= 0 && talk.contracts().find(job) != nullptr &&
        talk.contracts().find(job)->state == sim::ContractState::Paid) {
        ++landed;
    }
    // 6. And the ward paid for it.
    if (talk.contracts().coinEarned() > 0) {
        ++landed;
    }

    if (ending == "away") {
        session.closeConversation();
    }
    standBackFrom(session, "Watchman Cull");
    return landed;
}

}  // namespace scripted

}  // namespace granadad::render
