// Scripted capture drivers: nemesis.
#include "scripted.hpp"

namespace granadad::render {

namespace scripted {

[[nodiscard]] int runNemesisLine(Session& session, const std::string& ending) {
    // TARN WRENHALE, "Two-Loads": a docker on the evening shift, fists, no
    // rung, nobody's rival. Eli's own example is "killed by a laborer in a fist
    // fight", and this is the labourer.
    constexpr std::string_view kMark = "Tarn Wrenhale";
    int landed = 0;
    gNemesisBeatMask = 0;
    const auto land = [&landed](int beat) {
        gNemesisBeatMask |= 1 << (beat - 1);
        ++landed;
    };

    const sim::Actor* mark = actorNamed(session, kMark);
    if (mark == nullptr) {
        return landed;
    }
    const std::int32_t id = mark->id();

    // 1. HE IS NOBODY. The proof is worth nothing without the before.
    if (session.tavern().nemesis().of(id) == nullptr && mark->weapon() == sim::Weapon::Fists) {
        land(1);
    }

    // A round of the real thing: walk up to him, put him dead on the crosshair,
    // swing ONCE (swingOnceAt -- past two wins he HUNTS, walking onto the
    // player's own tile, so a blind swing from wherever the walk stopped casts
    // past him), and let the room resolve it a second at a time. The loop waits
    // for BOTH his win and the player being back on their feet --
    // Session::step() finds the release flag itself, so a loop that stopped at
    // the win would walk into the next round with the player still on the
    // boards.
    //
    // AND IT PICKS THE FIGHT AGAIN WHEN THE ROOM ENDS IT WITHOUT HIM WINNING.
    // STANCE & ROOM BUILD: now that every loss in this line is delivered by the
    // room's own blows, the line lives or dies on the fight actually reaching
    // its end WITH HIM -- and the house has honest ways of stopping it short.
    // The door policy puts a brawler out (the ejection shove, and tickBouncers
    // clears the brawl the moment the player is off the footprint), tickBrawl
    // clears any fight the player is not in the house for, and a swing that
    // found whoever else stood on the crosshair started a fight he is not in.
    // Either way the drive does what a player who is about to lose does: walks
    // back in, swings at HIM once more, and takes the beating.
    //
    // ONCE, AND ONLY AT A MAN WHO CAN STAND IT. The first draft of this build
    // re-engaged with tapUntilItConnects, and in three of eleven smoke
    // timelines it tapped its own nemesis to the boards: he got up at a
    // quarter of his health (advanceSecond), bloodied, and the next tap on a
    // bloodied man who means Harm is a lethal blow with the floors lifted
    // (brawl.hpp B3) -- the drive killed him, and the third win never came.
    // So: one swing per engagement (the sightline that found him put him in
    // the brawl whether or not the roll landed), never a swing at a bloodied
    // man, only inside the house (he only hunts indoors, and the room clears a
    // fight the player is outside for), and a corpse ends the round -- nobody
    // rises from one. Bounded twice: the seconds, and the re-engagements.
    const auto pickAFight = [&session, id]() {
        const sim::Actor* him = session.tavern().actorById(id);
        if (him == nullptr || !him->present()) {
            return false;
        }
        const sim::Nemesis* before = session.tavern().nemesis().of(id);
        const std::int32_t had = before == nullptr ? 0 : before->wins;
        session.closeConversation();
        constexpr int kReengagements = 4;
        int engaged = 0;
        for (int second = 0; second < 300; ++second) {
            him = session.tavern().actorById(id);
            if (him == nullptr || !him->present() || him->activity() == sim::Activity::Dead) {
                return false;
            }
            if (!session.tavern().playerFloored() && !inTheFight(session, id)) {
                if (!session.tavern().playerInside()) {
                    // Put out, or woken on the quay: back in through the door
                    // first -- by walking up to HIM, no swing, which is the
                    // one walk every spot the smoke leaves a body on has been
                    // proved to route from (a walk to a fixed interior tile
                    // stalled at the threshold from two of them). If he is
                    // outside too, this follows him in.
                    (void)closeOnAndFace(session, id);
                } else if (!sim::isFloored(him->activity()) &&
                           !sim::isBloodied(him->hp(), him->hpMax()) &&
                           sim::gull::insideFootprint(him->tileX(), him->tileY()) &&
                           engaged < kReengagements) {
                    ++engaged;
                    (void)swingOnceAt(session, id, 4);
                }
            }
            session.stepMany(sim::MoveInput{}, sim::kStepsPerSecond);
            const sim::Nemesis* now = session.tavern().nemesis().of(id);
            if (now != nullptr && now->wins > had && !session.tavern().playerFloored()) {
                return true;
            }
        }
        return false;
    };

    // 2. A FIST FIGHT IN A TAPROOM, LOST. Nothing about this beat is staged:
    //    the walk is real movement through real collision, the punch is the
    //    punch key, and the beating is the ordinary brawl the door policy has
    //    been resolving since S2.
    if (pickAFight()) {
        land(2);
    }
    session.skipToHour(20);

    // 3. AND THE REMATCH IS FOUGHT IN THE WORLD, TO THE BOARDS.
    //
    //    THE RULE STILL WORKS; what changed is that the flip is resolved rather
    //    than parked. A nemesis MEANS IT from his first win on (nemesisIntent),
    //    and brawl.hpp's third clause says beating a BLOODIED man while meaning
    //    him Harm is not a bar fight. So the rematch opens as a brawl and flips
    //    the instant the player is under a quarter (the "STEEL OUT. THE ROOM
    //    STANDS BACK." line and the SwordDraw fire on the edge in step()) --
    //    and STANCE & ROOM BUILD, the room keeps swinging under lethal rules
    //    with the floor lifted: he finishes the player himself, the death
    //    ceremony plays over the quay revive, and his second win is the room's
    //    own doing. The beat lands only when BOTH are true: the fight flipped
    //    lethal, and he put the player down in it. clearEscalation() first so
    //    the flip is a fresh edge; it is deliberately NOT cleared again below,
    //    so it stays latched through the defeat that follows and that one
    //    plays the death ceremony too.
    session.tavern().clearEscalation();
    const bool rematchLost = pickAFight();
    if (rematchLost && session.tavern().escalated() && !session.tavern().playerFloored()) {
        land(3);
    }
    if (ending == "death") {
        // 3D SLICE ONE (ship lane). STOP HERE, on the boards of beat 3, with
        // the death ceremony settleDefeat has just armed still playing over
        // the quay revive -- the one shutter that photographs the plate,
        // because beat 4's walk back in steps it out. Nothing past this line
        // is proved by this ending; the summary's beat mask says so.
        return landed;
    }
    session.skipToHour(20);

    // 4. AND THE THIRD LOSS IS FOUGHT AND LOST THE SAME WAY.
    //
    //    One more real fight -- real movement, a real swing, the room resolving
    //    the brawl, its flip, and the killing -- with a cudgel in his hand now
    //    (nemesisWeapon at two wins) and Harm behind it: a bar fight until the
    //    player is bloodied, lethal after, and the room finishes it. Nothing is
    //    conceded: Tavern::concedeTo SURVIVES as the defeat seam (COMBAT-
    //    ACTION-SPEC section 7, and the gate workload still reports through
    //    it) but this line has no use for it, because the room delivers the
    //    rise by its own hand now. Steel is still out from beat 3, so this too
    //    plays the DEATH CEREMONY -- settleDefeat lays the dip and the epitaph
    //    over the quay revive. Three wins is the top of the ladder the arc
    //    climbs (a rung, a house, a charge on the roll), which is why there is
    //    exactly one more fight here and not two.
    {
        const sim::Nemesis* before = session.tavern().nemesis().of(id);
        const std::int32_t had = before == nullptr ? 0 : before->wins;
        if (pickAFight()) {
            const sim::Nemesis* now = session.tavern().nemesis().of(id);
            if (now != nullptr && now->wins == had + 1) {
                land(4);
            }
        }
        session.skipToHour(20);
    }

    // 5. AND HE COMES PREPARED. Three wins put a blade in his hand and Kill
    //    behind it (nemesisWeapon/nemesisIntent) -- the next rematch is lethal
    //    from its first blow, and beats 3 and 4 have just proved the room will
    //    finish one. Read off the roster the way beat 1 read his fists.
    if (const sim::Actor* armed = session.tavern().actorById(id);
        armed != nullptr && armed->weapon() == sim::Weapon::Edged &&
        armed->intent() == sim::Intent::Kill) {
        land(5);
    }

    // 6. A house with members in it, and 7. ground on the ward's own roll --
    //    and none of it came off when the player got up.
    const sim::Nemesis* risen = session.tavern().nemesis().of(id);
    if (risen != nullptr && risen->foundedAHouse() && !risen->members.empty()) {
        land(6);
    }
    if (risen != nullptr && risen->holdsGround()) {
        land(7);
    }

    if (ending == "talk") {
        // RETRIED, because his own post has a neighbour. speakTo opens on
        // whoever is NEAREST, and Tarn Wrenhale's stool and Wick Hempson's are
        // one tile apart -- so the first attempt can photograph the wrong man,
        // which is exactly the class of thing the S4 review caught in the
        // Priest of the Flame's capture. speakTo already answers whether the
        // person who spoke is the person asked for; this believes it.
        for (int attempt = 0; attempt < 6; ++attempt) {
            if (speakTo(session, kMark)) {
                break;
            }
            session.closeConversation();
            // He is hunting by now, so standing still closes the gap for you.
            session.stepMany(sim::MoveInput{}, sim::kStepsPerSecond);
        }
    } else {
        session.closeConversation();
        standBackFrom(session, kMark);
    }
    return landed;
}

[[nodiscard]] int runWatchHaltLine(Session& session, const std::string& ending) {
    int landed = 0;
    gWatchHaltNote.clear();
    const sim::Tavern& tavern = session.tavern();
    // In through the door to the bar, the smoke's own route-walk.
    walkToTile(session, sim::gull::kBartenderX, sim::gull::kBartenderY + 1);
    session.closeConversation();
    const sim::Actor* cull = actorNamed(session, "Watchman Cull");
    if (cull == nullptr || !cull->present()) {
        gWatchHaltNote = " cull=absent";
        return landed;
    }
    const std::int32_t cullId = cull->id();

    // THE MARK, AND THE SPOT: exactly what test_watch_rhythm asks of the room
    // -- an upright PATRON (not the Watch, not the house's bouncer who refuses
    // steel, not the staff behind the bar, not a rat) the player can stand
    // beside, facing him, with Cull able to SEE the player from there by the
    // three-clause notice rule. Asked, not assumed: the first draft stood at
    // the bar and swung, and Cull's line to the bar is a table. Nearest to
    // Cull first, since that is where his line is shortest.
    std::vector<std::pair<std::int32_t, std::int32_t>> candidates;  // distance-to-Cull, id
    for (const sim::Actor& actor : tavern.actors()) {
        if (!actor.present() || sim::isFloored(actor.activity()) ||
            actor.role() != sim::ActorRole::Patron || tavern.isProfessional(actor) ||
            actor.band() != session.body().band()) {
            continue;
        }
        candidates.emplace_back(actor.distanceTo(cull->x(), cull->y()), actor.id());
    }
    std::sort(candidates.begin(), candidates.end());
    std::int32_t markId = -1;
    for (const auto& [distance, id] : candidates) {
        (void)distance;
        const sim::Actor* patron = tavern.actorById(id);
        if (patron == nullptr || !patron->present()) {
            continue;
        }
        const std::int32_t sides[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
        for (const auto& side : sides) {
            const std::int32_t px = patron->tileX() + side[0];
            const std::int32_t py = patron->tileY() + side[1];
            if (!session.tiles().standable(px, py, patron->band())) {
                continue;
            }
            walkToTile(session, px, py);
            if (session.body().tileX() != px || session.body().tileY() != py) {
                continue;
            }
            patron = tavern.actorById(id);
            if (patron == nullptr) {
                break;
            }
            facePlayerAndSync(session, patron->x(), patron->y());
            // A second of standing still, so the walk's own noise is off the
            // rule and he has looked up.
            session.stepMany(sim::MoveInput{}, sim::kStepsPerSecond);
            cull = tavern.actorById(cullId);
            if (cull != nullptr && cull->present() && tavern.noticeBy(*cull).seen) {
                markId = id;
            }
            break;
        }
        if (markId >= 0) {
            break;
        }
    }
    if (markId < 0) {
        gWatchHaltNote = " mark=none-in-culls-sight candidates=" + std::to_string(candidates.size());
        return landed;
    }
    ++landed;  // 1: Cull sees the player, standing beside a patron
    const sim::Actor* mark = tavern.actorById(markId);
    if (mark == nullptr) {
        return landed;
    }
    // THE BLADE. Armed the way test_street_panic arms its client -- the sheet
    // that puts steel in a hand is the weapon lane's; the sim's own setter is
    // what it will call. Kill is what a drawn blade means (intent-by-verb
    // reads the weapon class: Edged is Lethal from the first exchange).
    session.tavern().setPlayerCombat(sim::Weapon::Edged, sim::Intent::Kill);
    const std::int32_t hpBefore = mark->hp();
    const bool swung = swingOnceAt(session, markId, 8);
    if (const sim::Actor* struck = tavern.actorById(markId); struck != nullptr) {
        gWatchHaltNote = " mark=" + struck->name() + " hp=" + std::to_string(hpBefore) + "->" +
                         std::to_string(struck->hp()) + (swung ? " swung" : " no-swing") +
                         (tavern.lethalFightLive() ? " lethal" : " not-lethal") +
                         (tavern.playerHandsUp() ? " hands-up" : " hands-down") +
                         " brawlers=" + std::to_string(tavern.currentFight().size()) +
                         " inside=" + (tavern.playerInside() ? "yes" : "no");
    }
    if (swung && tavern.lethalFightLive() && tavern.playerHandsUp() &&
        tavern.playerWeapon() == sim::Weapon::Edged) {
        ++landed;  // 2
    }

    // THE HALT. Stand still. Within a few seconds he is Closing on Violence
    // and the row reads what he said.
    bool closing = false;
    for (int second = 0; second < 20 && !closing; ++second) {
        session.stepMany(sim::MoveInput{}, sim::kStepsPerSecond);
        closing = tavern.watchStance() == sim::Tavern::WatchStance::Closing &&
                  tavern.watchInterest() == sim::WatchCause::Violence;
    }
    if (closing && !tavern.lastDemand().empty() &&
        tavern.lastDemand().rfind("Watchman Cull: ", 0) == 0 &&
        session.lastMessage() == tavern.lastDemand()) {
        ++landed;  // 3
    }
    if (ending == "halt") {
        return landed;
    }

    // THE ARREST AT REACH -- step() itself consumes the release and says the
    // arrest line, so the proof is the report: it happened, the cause is
    // VIOLENCE and a sentence was passed.
    bool arrested = false;
    for (int second = 0; second < 60 && !arrested; ++second) {
        session.stepMany(sim::MoveInput{}, sim::kStepsPerSecond);
        arrested = tavern.lastArrest().happened &&
                   tavern.lastArrest().cause == sim::WatchCause::Violence;
    }
    if (arrested && tavern.lastArrest().sentence != sim::Sentence::None) {
        ++landed;  // 4
    }
    // AND THE STREET: turned loose outside the walls with his words on the row.
    if (arrested && !tavern.playerInside() && !tavern.lastArrest().line.empty() &&
        session.lastMessage() == tavern.lastArrest().line) {
        ++landed;  // 5
    }
    return landed;
}

}  // namespace scripted

}  // namespace granadad::render
