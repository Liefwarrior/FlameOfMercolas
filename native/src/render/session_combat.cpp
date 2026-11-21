// Session combat: attacks, casting, blocking, defeat and the death ceremony, the watch hooks.
#include "session_internal.hpp"

// THE AUDIO WIRING PASS, as in session.cpp: this file speaks to the audio engine in ids only; see
// setAudio() in session.hpp and the determinism note in audio_engine.hpp.
#include "granadad/audio/audio_engine.hpp"

namespace granadad::render {

void Session::settleDefeat() {
    // THE PLAYER RESPAWNS AS THEMSELVES AND THE WORLD KEEPS THE CONSEQUENCES.
    // Eli ruled 2026-07-31 that the persistent-ward variant -- the city
    // surviving your death while you come back as somebody else -- is not being
    // built from the start. So this is the whole of the respawn: the room gives
    // the hit points back and moves the clock on, and the body wakes up on the
    // quay apron, which is the same two tiles clear of the threshold a man put
    // out of the door ends up on.
    //
    // WHAT IT DOES NOT DO IS UNDO ANYTHING. The rung, the founded house, the
    // toll on every price and the name on the roll all survive this call. That
    // is the design.
    const sim::Rise& rise = tavern_->lastDefeat();
    // ACTION-COMBAT BUILD (section 4.5). A DEFEAT UNDER LETHAL RULES (steel was
    // out -- tavern_->escalated()) is not a plain brawl KO: it earns the
    // OCCASION -- the dip, the epitaph naming killer, weapon and place, the held
    // hush of kDeathHoldSteps, then the reveal of the quay the revive below puts
    // you on. Armed HERE, from where the body fell, before reviveAfterDefeat and
    // placeBodyAt move it; composeDeathCeremony() paces the veil over the frames
    // that follow, render-only, while the sim revives underneath at once. A
    // plain brawl defeat (never escalated) keeps the bare settle it always had.
    if (tavern_->escalated()) {
        armDeathCeremony();
    }
    // AUDIO WIRING: the plan's "knockdown -> ThudHeavy" -- the one call every
    // path to the floor funnels through. THE LOT PASS: fall_land is what
    // ThudHeavy resolves to now, and the DEATH CEREMONY (an escalated
    // defeat, the occasion armed just above) gets the owner's own death vox
    // over it; a plain brawl KO keeps the bare thud.
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::ThudHeavy);
        if (tavern_->escalated()) {
            audio_->playOneShot(audio::SoundId::PlayerDown);
        }
    }
    tavern_->reviveAfterDefeat();
    // Through placeBodyAt: waking on the quay apron is a relocation like any
    // other, and the room you were beaten in must not ghost over the street.
    placeBodyAt(sim::gull::kStreetX, sim::gull::kStreetY, sim::gull::kGroundBand);
    awaitingLanding_ = false;
    syncTavernToBody();
    timeOfDay_ = tavern_->timeOfDay();
    // What he said standing over you, out of the owner's tables -- and the
    // short report of what it made him when there is nothing authored.
    if (!rise.taunt.empty()) {
        sayAs(rise.who, rise.taunt, false);
    } else if (!rise.line.empty()) {
        say(rise.line);
    }
    syncWardToCalendar();
}

namespace {

/// ACTION-COMBAT BUILD (section 4.5). The two eases that bracket the death
/// ceremony's held epitaph: the frame dips to black over kDeathDipInSteps and
/// eases back over kDeathFadeOutSteps, with the sim's kDeathHoldSteps hush
/// between. The eases are one page-ease each; the HOLD is the sim's own number.
constexpr std::int32_t kDeathDipInSteps = kPageEaseSteps;

constexpr std::int32_t kDeathFadeOutSteps = kPageEaseSteps;

}  // namespace

void Session::armDeathCeremony() {
    // From the last defeat, captured before the revive moves the body: the
    // killer's name in the terminal register, a weapon word off his own hand,
    // and the place it happened. Two lines, the veto-blessed epitaph grammar
    // (section 5): "PUT DOWN IN THE GILDED GULL. / BY CANNIC. BY STEEL."
    const sim::Rise& rise = tavern_->lastDefeat();
    const std::string killer = rise.who.empty() ? std::string("THE DARK") : upperAscii(rise.who);
    // The weapon word is honest to what put you down, read off the killer's
    // rival record. A lethal fight is usually steel, but a bloodied man beaten
    // to death by fists is lethal too (B3), so the word follows the hand.
    std::string weaponWord = "STEEL";
    if (const sim::Nemesis* rival = tavern_->nemesis().of(rise.actorId); rival != nullptr) {
        switch (rival->weapon()) {
            case sim::Weapon::Edged:
                weaponWord = "STEEL";
                break;
            case sim::Weapon::Blunt:
            case sim::Weapon::Evictor:
                weaponWord = "THE CUDGEL";
                break;
            case sim::Weapon::Improvised:
                weaponWord = "WHAT CAME TO HAND";
                break;
            case sim::Weapon::Fists:
                weaponWord = "BARE HANDS";
                break;
        }
    }
    // Inside the house it is the Gull by name (the veto's own literal); on the
    // street it is the ground placeLabel() answers for.
    const std::string place =
        tavern_->playerInside() ? std::string("THE GILDED GULL") : placeLabel();
    deathEpitaphTop_ = "PUT DOWN IN " + place + ".";
    deathEpitaphBottom_ = "BY " + killer + ". BY " + weaponWord + ".";
    deathCeremonySteps_ = kDeathDipInSteps + sim::kDeathHoldSteps + kDeathFadeOutSteps;
}

void Session::composeDeathCeremony(Framebuffer& target) const {
    if (deathCeremonySteps_ <= 0) {
        return;
    }
    constexpr std::int32_t kTotal =
        kDeathDipInSteps + sim::kDeathHoldSteps + kDeathFadeOutSteps;
    const std::int32_t remaining = deathCeremonySteps_;
    // The veil's strength across the three phases: ramp up as it comes down,
    // full through the hush, ramp back as the quay comes up under it.
    float dip = 1.0F;
    if (remaining > sim::kDeathHoldSteps + kDeathFadeOutSteps) {
        const std::int32_t elapsed = kTotal - remaining;  // 0 .. kDeathDipInSteps
        dip = static_cast<float>(elapsed) / static_cast<float>(kDeathDipInSteps);
    } else if (remaining <= kDeathFadeOutSteps) {
        dip = static_cast<float>(remaining) / static_cast<float>(kDeathFadeOutSteps);
    }
    dip = dip < 0.0F ? 0.0F : (dip > 1.0F ? 1.0F : dip);
    // The veil itself -- the same black-fill primitive dipTravelSeam draws with,
    // over the whole finished frame (HUD included).
    target.fillRect(0, 0, target.width(), target.height(), Rgb{0.0F, 0.0F, 0.0F}, dip);
    // The epitaph, centred, in the plate's own bone register. The black field IS
    // its backing, so it needs no plate of its own; the text alpha rides the dip
    // so the words arrive as the room goes dark and leave as it comes back.
    const int scale = hudScale(target.height());
    const int glyphH = 6 * scale;  // the 4x6 font's own height, drawText's grid
    const int lineStep = glyphH + 2 * scale;
    const int cx = target.width() / 2;
    const int topY = target.height() / 2 - lineStep;
    const Rgb bone{0.86F, 0.82F, 0.72F};
    const auto centre = [&](const std::string& line, int y) {
        if (line.empty()) {
            return;
        }
        const int w = textWidth(line, scale);
        drawText(target, cx - w / 2, y, line, bone, dip, scale);
    };
    centre(deathEpitaphTop_, topY);
    centre(deathEpitaphBottom_, topY + lineStep);
}

void Session::recordWatchOp(WatchOpKind kind, std::int32_t a, bool ok) {
    if (watchTape_ == nullptr || watchDepth_ != 0) {
        return;
    }
    WatchOp op;
    op.kind = kind;
    op.a = a;
    op.ok = ok;
    watchTape_->push_back(op);
}

void Session::watchChapter(std::int32_t index) { recordWatchOp(WatchOpKind::Chapter, index); }

void Session::watchBeatLanded(std::int32_t index, bool ok) {
    recordWatchOp(WatchOpKind::BeatLanded, index, ok);
}

namespace {

/// A landed blow of this many hit points or fewer is a GRAZE -- the light
/// audio band. Fists tap for 3-5 at the base sheet, so this catches the
/// glancing end of a bare-handed swing and lets a solid or hard blow land
/// medium instead. Splits the plan's single PunchMedium by band (section 5,
/// channel 4); presentation-only, no sim number depends on it.
constexpr std::int32_t kGrazeDamageBand = 4;

/// BARKS LANE: the gain of the graze that stands in for a found-body whiff
/// (attackUp). Quieter than any landed blow, so the ear ranks it below one.
constexpr float kFoundBodyWhiffGain = 0.35F;

}  // namespace

void Session::attackDown() {
    if (refusedInCustody()) {
        return;
    }
    // The down-edge. A Punch watch-op is recorded here (case_watch replays it
    // as punch()); dismissOverlays and the sim's hold clock start exactly where
    // the legacy punch() started them.
    recordWatchOp(WatchOpKind::Punch);
    const WatchDepthGuard watchGuard(watchDepth_);
    dismissOverlays();
    tavern_->playerAttackDown();
}

void Session::attackUp() {
    if (refusedInCustody()) {
        return;
    }
    const WatchDepthGuard watchGuard(watchDepth_);
    // STREET SENSES leg (b) -- THE STREET PICK, the one client hook in the
    // attack path. ONE RULE, TWO ROSTERS: the street's ray is cast with the
    // identical integer projection the Gull's is (WardPopulation::
    // sightlineTarget), and the nearer body ALONG the ray takes the blow --
    // a docker a tile in front of you is hit before a patron three tiles
    // behind him through the Gull's door. The swing itself is the room's
    // (playerAttackUpStreet: the same head, the same one roll, strike(),
    // classifyFight, the deeds, the murder law); the street only supplies the
    // sheet and takes the answer back (applyStreetBlow: the floor, the death,
    // the rout, the fight-back, the crowd). Witnesses are counted BEFORE the
    // body drops, at the Gull's own witness range, so N SAW IT at the bench
    // is the count of who could see it -- the Watch included.
    sim::Tavern::PlayerSwingResult result;
    const sim::WardActor* street = nullptr;
    std::int64_t streetAlong = -1;
    if (people_ != nullptr) {
        street = people_->sightlineTarget(body_->x(), body_->y(), body_->band(), body_->yaw(),
                                          &streetAlong);
    }
    const std::int64_t roomAlong = tavern_->playerSightlineAlong();
    if (street != nullptr && (roomAlong < 0 || streetAlong < roomAlong)) {
        const std::int32_t wardId = street->id;
        sim::Fighter sheet;
        sheet.actorId = sim::kWardSpeakerIdBase + wardId;
        sheet.weapon = sim::Weapon::Fists;
        sheet.intent = sim::Intent::Subdue;
        sheet.hp = street->hp;
        sheet.hpMax = sim::kActorHealth;
        const std::int32_t witnesses = people_->witnessesInSight(
            street->x, street->y, street->band, sim::kWatchSightTiles, wardId);
        result = tavern_->playerAttackUpStreet(sheet, wardId, people_->identity(wardId).name,
                                               witnesses);
        if (result.swung && result.targetId >= 0) {
            (void)people_->applyStreetBlow(wardId, sheet.hp, result.blow, result.lethal);
            // THE ROUTED DOCKER'S LINE (the street's own row, salted register):
            // a man struck and still standing who is not a fighter and not the
            // Watch has just decided to run, and says so once on the alert row.
            // Presentation only: a string, unhashed.
            if (result.blow.landed && !result.blow.downed && !result.killed &&
                !sim::wardTypeFightsBack(street->type)) {
                const sim::BarkTables& barks = tavern_->dialogue().barks();
                const std::string_view key =
                    barks.resolve({std::string("street.routed"), std::string("crowd.flee")});
                if (!key.empty()) {
                    sayAs(people_->identity(wardId).name,
                          std::string(barks.line(key, static_cast<std::int32_t>(elapsedSeconds_ % 1024) + wardId)),
                          false);
                }
            }
        }
    } else {
        result = tavern_->playerAttackUp();
    }
    if (result.refused) {
        // A hard swing the wind would not buy. A winded TAP still swings (its
        // fatigue whiff band is the penalty), so a refusal is only ever the
        // hard tier saying so, once, on the alert row.
        say("TOO WINDED TO SWING HARD.");
        return;
    }
    if (!result.swung) {
        // A release with no charge behind it -- an edge that arrived in
        // recovery or idle. It throws nothing and says nothing.
        return;
    }
    // A committed HARD swing dips the camera forward on release -- render-only,
    // composed in camera() and never written to sim yaw.
    if (result.hard) {
        hardSwingDipPulse_.trigger();
    }
    // 3D BUILD, V LANE. The hands throw the swing the room just resolved,
    // on the next step, at the tier the room read off the hold.
    viewmodelSwingPending_ = static_cast<std::uint8_t>(result.hard ? 2 : 1);
    // THE LOT PASS: the HARD release has its own air (whoosh_strong / swing
    // sword) under whatever it finds -- a body or nothing; a tap at nothing
    // is the plain whoosh set. Spoken before the band below, so the ear gets
    // swing-then-impact in that order on a hard hit.
    if (audio_ != nullptr && result.hard) {
        audio_->playOneShot(audio::SoundId::WhooshHard);
    }
    if (result.targetId < 0) {
        // Committed and paid, but the crosshair passed through nobody. The arm
        // still swung, so the wind is spent and the swing gets its air.
        say("NOBODY IN REACH.");
        if (audio_ != nullptr) {
            if (!result.hard) {
                audio_->playOneShot(audio::SoundId::Whoosh);
            }
            // A blow thrown keeps the combat music alive; it starts nothing.
            audio_->music().noteSwing();
        }
        return;
    }
    // SAY-ROW DIET (section 5, channel 3). The per-blow "HIT X FOR N."/"MISSED
    // X." log is RETIRED -- a landed, taken or blocked blow speaks through the
    // washes, the reticle and the audio below, not the alert row. The row keeps
    // only EVENTS and REFUSALS: a kill, a crowning, a down. The steel-out flip
    // and its SwordDraw are spoken once on the escalation EDGE in step(), not
    // per swing, so a fight that stays lethal does not re-announce itself.
    if (result.killed) {
        // A killing blow under lethal rules -- the register literal, veto-blessed
        // (section 5). Names the man the boards just took.
        say(result.targetName + " DIES ON THE BOARDS.");
    } else if (result.blow.crowned) {
        // THE EVICTOR'S OWN LINE. crowned is downed by construction and never
        // kills (it was forged to put a man OUT), so it reads before the plain
        // fall and only this weapon ever throws one.
        say("CAUGHT " + result.targetName + " ACROSS THE CROWN. OUT COLD.");
    } else if (result.blow.downed) {
        say(result.targetName + " GOES DOWN.");
    }
    // AUDIO WIRING (section 5, channel 4). One sound per swing, by blow band:
    // the helmet on a crowning, the heavy on a kill or a knockdown, the light
    // graze on the low damage band and the medium above it (the plan's single
    // PunchMedium split), and the whoosh for a swing that found a body but
    // glanced off nothing worth a mark (a fully absorbed hit). SwordDraw is the
    // Lethal FLIP, wired on the escalation edge in step(), not here.
    if (audio_ != nullptr) {
        // THE LOT PASS re-cut the bands over the Malbers set: hit_grave for
        // the blow that ends a man (a crowning or a kill), the heavy band
        // plus fall_land (ThudHeavy) for a body going down, the clash for a
        // guard that caught it, the medium band for a blow that landed.
        if (result.blow.crowned || result.killed) {
            audio_->playOneShot(audio::SoundId::HitGrave);
        } else if (result.blow.downed) {
            audio_->playOneShot(audio::SoundId::PunchHeavy);
            audio_->playOneShot(audio::SoundId::ThudHeavy);
        } else if (result.blow.blocked) {
            audio_->playOneShot(audio::SoundId::SwordClash);
        } else if (result.blow.landed) {
            audio_->playOneShot(result.blow.damage <= kGrazeDamageBand
                                    ? audio::SoundId::GrazeLight
                                    : audio::SoundId::PunchMedium);
        } else {
            // BARKS LANE (feel/build) -- THE HONEST WHIFF. The reticle
            // promised a body on the line and the die (1-in-8, plus the
            // fatigue band -- kept, the owner's ruling) said the arm found
            // nothing worth a mark. That is NOT the same sound as swinging at
            // air (Whoosh, the targetId < 0 branch above): the fist brushed a
            // coat. The light graze at a third of its gain and no wash --
            // presentation only, zero law contact.
            audio_->playOneShot(audio::SoundId::GrazeLight, kFoundBodyWhiffGain);
        }
        // THE LOT PASS: a blow LANDED is the fight's first edge for the
        // music; a whiff on a body only keeps a running fight alive.
        if (result.blow.landed) {
            audio_->music().noteCombat();
        } else {
            audio_->music().noteSwing();
        }
    }
    // A LANDED SWING FINALLY HAS SOME WEIGHT -- the connecting wash. A whiff on
    // a body that was on the line stays silent on the wash, exactly as a miss
    // always did: this is punctuation for a hit, not for the swing.
    if (result.blow.landed) {
        punchLandedPulse_.trigger();
    }
}

void Session::punch() {
    if (refusedInCustody()) {
        return;
    }
    // ACTION-COMBAT BUILD: a TAP of the new verbs -- down then immediately up
    // with no movement step between, so the sim's charge counter never leaves
    // zero and the swing is always the light (Subdue) tier. The six un-migrated
    // call sites (the scripted drives, case_watch, the render suite) drive real
    // sightline combat through this, the same path a mouse press will.
    attackDown();
    attackUp();
}

void Session::castEquipped() {
    if (refusedInCustody()) {
        return;
    }
    dismissOverlays();
    if (talking() || picking()) {
        // The keyboard is a topic list's (or a lock's) right now. Inert, the
        // same way setCrouched already stands down -- a link opened from
        // inside a conversation would be a fact nobody saw being made.
        return;
    }
    // The room resolves the whole of it -- every refusal included -- and
    // always answers with a line, so the key can never silently do nothing.
    // See sim::Tavern::playerCastEquipped()'s own header.
    const std::int64_t coolingBefore = tavern_->castCooldownLeft();
    const sim::Tavern::CastResult result = tavern_->playerCastEquipped();
    say(result.line);
    // 3D BUILD, V LANE. The hand reaches out when the room let it TRY: a
    // link that opened, or one that slipped (a fizzle still starts the
    // recovery clock -- the comparison-not-flag shape lastPlayerHp_ uses,
    // so no sim field is added for a gesture). A refusal moves nothing.
    if (result.cast || tavern_->castCooldownLeft() > coolingBefore) {
        viewmodelCastPending_ = true;
    }
    // NO NEW PULSE, DELIBERATELY. The restraint note in drawFrame() stands:
    // two real moments got a flash and a cast is not being bolted on as a
    // third -- a landed harmful cast already reads on the target and the
    // alert row, and the punch-taken/blocked pair stays the whole of the
    // wash vocabulary.
}

void Session::setBlocking(bool held) {
    // The EDGE only. What the room is told is derived in step(), every step
    // -- see the comment there on why (a page can open or close mid-hold,
    // and no edge event exists for either moment).
    blockHeld_ = held;
}

void Session::applyPlayerAttributes(const sim::AttributeBlock& attributes) {
    // BOTH HALVES OR NEITHER -- see the declaration. The room takes the
    // sheet (and sizes the fatigue pool off it); the body takes the one
    // reader the room cannot hold for it, AGI's gait multiplier. At the
    // base-40 sheet both halves are exactly the shipped behaviour.
    tavern_->setPlayerAttributes(attributes);
    body_->setSpeedScaleQ8(
        sim::agilitySpeedScaleQ8(attributes.value(sim::AttributeId::Agility)));
}

void Session::restHere() {
    if (refusedInCustody()) {
        return;
    }
    dismissOverlays();
    const sim::ServiceResult slept = tavern_->sleep();
    if (slept == sim::ServiceResult::Served) {
        settleSleep();
        return;
    }
    say(std::string(sim::restRefusal(slept)));
}

}  // namespace granadad::render
