// Scripted capture drivers: burgle.
#include "scripted.hpp"

namespace granadad::render {

namespace scripted {

[[nodiscard]] int runBurgleLine(Session& session, const std::string& ending) {
    int landed = 0;
    gBurgleWatchers = 0;
    gLockEndingProbes = 0;
    gBurgleBeatMask = 0;
    std::int32_t beat = 0;
    const auto mark = [&](bool ok) {
        if (ok) {
            gBurgleBeatMask |= 1 << beat;
            ++landed;
        }
        ++beat;
    };

    // 1. down on the haunches. Half speed and worth more than twenty levels.
    session.toggleCrouch();
    mark(session.stance() == sim::Stance::Crouched);

    // 2. IN AT THE DOOR, AND NOT MADE OUT STANDING IN IT. Crouched, at two in
    //    the morning, with the doors just barred and the lanterns out: the room
    //    is dark and nearly empty, which is the whole reason a burglar keeps
    //    these hours.
    //
    //    THE CHECK IS HERE AND NOT LATER, deliberately. A lift that goes wrong
    //    is an offence, and an offence puts a bouncer across the room at you --
    //    at which point being seen is the game working rather than the stealth
    //    failing, and a beat that could not tell those two apart would be a
    //    beat worth nothing.
    //
    //    S10: AND SOMEBODY HAS TO BE THERE TO MISS YOU. The S9 review proved
    //    this beat landed with the notice rule hard-wired to seen -- at two in
    //    the morning the doorway is empty, everyone in reach reads oblivious,
    //    and "nobody saw me" was a fact about the hour rather than about
    //    stealth. The bit now needs a body in range that is awake, upright and
    //    on this floor, so it is a claim that can fail. If the night staff have
    //    all gone to bed, the burglar walks in until one of them is in reach.
    walkToTile(session, sim::gull::kDoorX0, sim::gull::kDoorY + 1);
    if (session.tavern().watchersInReach() == 0) {
        // Nobody at the door. Go and stand near whoever is still up -- being
        // unseen next to a man is the beat; being unseen in an empty room is
        // not, and the run should fail rather than quietly pass if there is
        // nobody in the building at all.
        const sim::Actor* awake = nullptr;
        for (const sim::Actor& actor : session.tavern().actors()) {
            if (actor.present() && actor.band() == session.body().band() &&
                actor.activity() != sim::Activity::Downed &&
                actor.role() != sim::ActorRole::Vermin) {
                awake = &actor;
                break;
            }
        }
        if (awake != nullptr) {
            walkToTile(session, awake->tileX() + 2, awake->tileY());
        }
    }
    gBurgleWatchers = session.tavern().watchersInReach();
    mark(session.hidden() && gBurgleWatchers > 0);

    // 3. a hand in the coat of whoever is still on a stool.
    //
    //    THE BEAT IS THE HAND, NOT THE COIN, and that is stated rather than
    //    quietly assumed. Whether a lift SUCCEEDS is CRACKSMANSHIP against the
    //    mark's own STREETWISE (Tavern::liftFrom), and a scripted burglar
    //    starts at level zero, so against the night staff of a captains' house
    //    it fails -- correctly, and it still teaches the hands. What this beat
    //    proves is that the verb is reachable from the keys and reached a body;
    //    the summary prints which way it went, so a reader is never told a lift
    //    landed when it did not.
    const std::int32_t purseBefore = session.tavern().playerCoin();
    bool handWentIn = false;
    for (const char* target : {"Kled Tarbeck", "Finch", "Gerta Saltcotte", "Master Venn"}) {
        const sim::Actor* who = nullptr;
        for (const sim::Actor& actor : session.tavern().actors()) {
            if (actor.name() == target && actor.present() && actor.coin() > 0) {
                who = &actor;
                break;
            }
        }
        if (who == nullptr) {
            continue;
        }
        walkToTile(session, who->tileX(), who->tileY());
        const sim::Tavern::StealResult tried = session.tavern().liftFrom();
        if (tried.result != sim::ServiceResult::TooFar) {
            handWentIn = true;
        }
        if (session.tavern().playerCoin() > purseBefore) {
            break;
        }
    }
    mark(handWentIn);

    // 4. away across the room to the foot of the stair, and up it.
    walkToTile(session, sim::gull::kStairX, sim::gull::kStairY);
    climbAndLand(session);
    mark(session.body().band() == sim::gull::kUpperBand);

    // 5. to a bed-foot that is not yours, and the wire in.
    const sim::gull::GuestRoom& box = sim::gull::kRooms[2];
    walkToTile(session, box.standX, box.standY);
    session.steal();
    mark(session.picking());

    // 6. WORK THE LOCK, the way the player in this body can. workTheWire() is
    //    a strategy and not a cheat: it reads the depth the pick is at, how
    //    many pins have dropped, and what the last probe FELT LIKE, and it
    //    reads nothing else. Below kFeelLevel that degenerates to the same
    //    blind sweep S9 shipped, because a hand with no feel has no better
    //    move; at and above it, it bisects.
    workTheWire(session);
    const std::int32_t roomBit = 1 << 2;
    if ((session.tavern().openedLocks() & roomBit) == 0) {
        // The wire is gone and the lock is ruined. A burglar with a jammed lock
        // and a job to do puts a shoulder to it, which always works and is the
        // loudest thing in the building.
        session.forceLock();
    }
    mark((session.tavern().openedLocks() & roomBit) != 0);

    // 7. and the box, emptied.
    const std::int32_t before = session.tavern().crackedBoxes();
    session.steal();
    mark(session.tavern().crackedBoxes() != before);

    if (ending == "lock") {
        // AND HE STARTS ON THE NEXT ONE, WITH THE HANDS THE FIRST ONE GAVE HIM.
        //
        // S9 SHIPPED THIS FRAME POSED AND SAID IT WAS NOT. Its comment read
        // "Nothing is faked" directly above a setPicks() that refilled the roll
        // out of nowhere and a pinDepth() lookup that drove the pick to the
        // answer -- the S9 review's fourth finding, and it was right: the CALLS
        // were the ones a keyboard reaches, the STATE was not, and the captured
        // PNG read PICKS 5 STRAIN 0/3 after a burglary that had just spent
        // every pick in the roll.
        //
        // What happens instead is the arc the retuning exists for. Beat 6 has
        // just cost this burglar most of his wire and taught his hands a great
        // deal doing it -- every probe is a use, and usesForLevel charges 18 of
        // them for CRACKSMANSHIP 3, which is kFeelLevel. So he goes back down
        // to the snug, buys wire off the Skyrunners' own contact with the same
        // G a keyboard presses, comes back up, and works the box across the
        // landing with a hand that can now hear it. Nothing here knows where a
        // pin is.
        walkToTile(session, sim::gull::kStairX, sim::gull::kStairY);
        session.dropDown();
        // #85. STANDS UP FIRST. Beat 1 crouched this body and nothing since
        // has stood it back up; interact() now resolves a person in reach to
        // PICKPOCKET while crouched rather than TALK, and speakTo() below
        // needs the latter to reach Finch's Join topic at all. A burglar
        // squaring an honest deal for more wire stands up to have the
        // conversation, which is the in-fiction reading and not only the
        // mechanical one.
        session.setCrouched(false);
        // The oath first. Nobody sells a stranger wire -- Tavern::buyPicks
        // refuses anyone off the Skyrunners' ladder in as many words -- so the
        // burglar takes their first rung off Finch through the same Join topic
        // the roof line uses, and then buys. Both are keys.
        if (speakTo(session, "Finch")) {
            pick(session, sim::TopicKind::Join);
            session.closeConversation();
        }
        const sim::Actor* contact = nullptr;
        for (const sim::Actor& actor : session.tavern().actors()) {
            if (actor.role() == sim::ActorRole::SkyrunnerContact && actor.present()) {
                contact = &actor;
                break;
            }
        }
        if (contact != nullptr) {
            walkToTile(session, contact->tileX(), contact->tileY());
            // Two sets if he can afford them: a bisect wants four notches of
            // slack a pin and the roll is the only thing that buys patience.
            session.tavern().buyPicks();
            session.tavern().buyPicks();
        }
        walkToTile(session, sim::gull::kStairX, sim::gull::kStairY);
        climbAndLand(session);
        const sim::gull::GuestRoom& next = sim::gull::kRooms[3];
        walkToTile(session, next.standX, next.standY);
        session.steal();
        // Worked, and then STOPPED WHILE IT IS STILL BEING WORKED -- either
        // when the last pin is one away or after a handful of probes, whichever
        // comes first. That is what makes the shutter catch the minigame rather
        // than its aftermath, and it is a budget on the PLAYER'S side of the
        // wire, not a hand on the lock's side.
        gLockEndingProbes = workTheWire(session, sim::kStrongboxPins - 1, 8);
        session.body().setYaw(sim::bearingTo(session.body().tileX(), session.body().tileY(),
                                             next.bedX, next.bedY));
        session.body().setPitch(sim::angle_from_degrees(-14));
    } else if (ending == "street") {
        session.dropDown();
        walkToTile(session, sim::gull::kStreetX, sim::gull::kStreetY);
    } else if (ending == "taproom") {
        walkToTile(session, sim::gull::kStairX, sim::gull::kStairY);
        session.dropDown();
        walkToTile(session, sim::gull::kBartenderX, sim::gull::kBarY + 2);
        session.body().setYaw(sim::kFacingNorth);
    } else {
        // Back down the landing, looking at the room that has just been done.
        // NOT nose-first against the bed block: standing on the tile you
        // cracked the box from fills the frame with one CLOTH face and shows
        // nothing, which is what the first shipped attempt at this frame did.
        walkToTile(session, 152, 72);
        session.body().setYaw(sim::bearingTo(session.body().tileX(), session.body().tileY(),
                                             box.bedX, box.bedY));
        session.body().setPitch(sim::angle_from_degrees(-8));
    }
    return landed;
}

}  // namespace scripted

}  // namespace granadad::render
