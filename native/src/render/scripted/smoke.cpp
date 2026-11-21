// Scripted capture drivers: smoke.
#include "scripted.hpp"

namespace granadad::render {

namespace {

/// Whether a body at a Q8 point is IN THE FRAME from where the player
/// stands: inside the horizontal field of view the live settings give the
/// camera, with a clear lens line to it (cameraSightClear) and nobody drawn
/// in the way (bodyBetween). The proof a composed shot owes -- a frame of a
/// voice line is a frame of its speaker, or the summary says it is not.
[[nodiscard]] bool speakerInView(const Session& session, std::int32_t xQ8, std::int32_t yQ8,
                                 std::int32_t band, std::int32_t speakerId) noexcept {
    if (band != session.body().band()) {
        return false;
    }
    const std::int32_t dx = xQ8 - session.body().x();
    const std::int32_t dy = yQ8 - session.body().y();
    if (dx == 0 && dy == 0) {
        return false;
    }
    // The bearing to the body against the facing, in BAM, folded to a
    // signed half turn; inside the half field of view with a small margin so
    // a body on the very edge of the frame does not count as in it.
    const sim::Angle bearing = bearingToward(session, xQ8, yQ8);
    std::int32_t off = static_cast<std::int32_t>((bearing - session.body().yaw()) & (sim::kTurnFull - 1));
    if (off > sim::kTurnFull / 2) {
        off -= sim::kTurnFull;
    }
    // hfovTan is tan(half the horizontal field of view): the half angle
    // back out of it, in BAM.
    constexpr double kTwoPi = 6.283185307179586;
    const double halfFovRadians = std::atan(static_cast<double>(session.camera().hfovTan));
    const std::int32_t halfFov =
        static_cast<std::int32_t>(halfFovRadians * static_cast<double>(sim::kTurnFull) / kTwoPi);
    constexpr std::int32_t kEdgeMarginBam = 1024;  // ~5.6 degrees
    if (std::abs(off) > halfFov - kEdgeMarginBam) {
        return false;
    }
    return cameraSightClear(session.tiles(), session.body().x(), session.body().y(), xQ8, yQ8, band,
                            false) &&
           !bodyBetween(session, xQ8, yQ8, band, speakerId);
}

/// How many beats runContractLine tries to land.
constexpr std::int32_t kContractBeats = 6;

/// And how many the `held` ending stops after -- taken and sanctioned, the
/// job still live. See the ending's own note inside runContractLine.
constexpr std::int32_t kContractHeldBeats = 2;

/// How many beats runNemesisLine tries to land: he was nobody, a fist fight
/// lost in the world, a rematch fought to the lethal flip AND lost to the
/// room's own blows, a third loss fought and lost the same way with a cudgel
/// in his hand (each with the death ceremony over the revive), the blade and
/// the Kill three wins put in his hands, a house, and a charge on the roll.
constexpr std::int32_t kNemesisBeats = 7;

/// 3D SLICE ONE (ship lane). What `--nemesis=death` owes: it stops ON PURPOSE
/// on the boards of the rematch (beats 1-3), the death ceremony still
/// playing, so three is the whole of what that ending can land.
constexpr std::int32_t kNemesisDeathBeats = 3;

/// BARKS LANE (feel/build). THE WATCH ON SEEN VIOLENCE, played through the
/// real verbs: the walk in, the blade, one swing at a man Cull can see, the
/// halt on the alert row, the arrest at reach, the street. Five beats:
///   1. Watchman Cull is in the house and can SEE the player where they stand
///      (Tavern::noticeBy, the three-clause rule -- asked, not assumed).
///   2. Steel drawn and a swing thrown at a patron: a LETHAL-class fight is
///      live with the fists-up stance holding a blade.
///   3. He goes Closing on VIOLENCE within seconds -- no glance gate, no die --
///      and the halt is on the alert row in HIS name, out of watch.halt.
///   4. The arrest at reach, the report's cause VIOLENCE (the contraband path,
///      applyArrest), the sentence in the report.
///   5. Turned loose on the Tarwalk with the arrest line on the row.
/// "halt" ends the drive at beat 3 so a capture holds the halt itself.
constexpr std::int32_t kWatchHaltBeats = 5;

/// What `--watch-halt=halt` owes: it stops the step he starts Closing.
constexpr std::int32_t kWatchHaltStopBeats = 3;

/// What `--voice=watch` owes. See runWatchRevealLine.
constexpr std::int32_t kWatchRevealBeats = 4;

/// How many beats each `--voice=` ending owes. See runVoiceLine.
[[nodiscard]] std::int32_t voiceBeatsFor(const std::string& ending) noexcept {
    if (ending == "farewell" || ending == "moved" || ending == "react") {
        return 3;
    }
    if (ending == "watch") {
        return kWatchRevealBeats;
    }
    return 2;  // pass, overheard, hold
}

// ---------------------------------------------------------------------------
// KIT BUILD: the Kit, played
// ---------------------------------------------------------------------------

/// THE KIT LINE, through the real verbs. Two in the afternoon on the Tarwalk
/// (daylight for the coil on the boards), the clock forward to four the next
/// morning for the lifts (the house empty, so a thing on a chair can be
/// named and taken with nobody to see it and no bouncer's ladder started),
/// and forward again to two for Ox on the door. Ten beats, each an ending:
///   1. take     the crosshair reads TAKE ROPE  48DR over the coil on the
///               Tarwalk (nobody's), and the press takes it
///   2. theirs   in the house at four: the lantern on Hobbin's table from
///               Maell's chair -- TAKE LANTERN  THEIRS in the Owned accent --
///               then the knife on Edda's and the coat on Colm's, each a lift
///               under the witness rule with nobody to witness it
///   3. sheet    the clock at two; the Character tile open on the carried
///               rows: IN HAND FISTS, the four worn slots at NOTHING, LOAD n
///               / 240 DRAMS, the rows with their weights and worths
///   4. equip    the coat WORN and the knife IN HAND by the tile's own press
///   5. slot     the knife bound to slot 3 by LEFT/RIGHT, the number pressed,
///               the strip up with KNIFE on it
///   6. dr       the knife bared, fists on Ox Gullbane, his blow turned by the
///               coat -- COAT TURNS n on the row, stopped on that step
///   7. search   the knife back in hand, Ox put down for good, SEARCH OX
///               GULLBANE  DEAD on the crosshair and his kit on the list
///   8. load     everything off him and the bottle and the hood off the
///               tables: the body near its budget, the legs at half, the tile
///               open on the LOAD line
///   9. drop     out to the Tarwalk, the coil put down through the tile's own
///               X, the body stepped back to look at it on the boards -- the
///               3D frame
///  10. done     the tile again on what is left (the default)
constexpr std::int32_t kKitBeats = 10;

[[nodiscard]] std::int32_t kitBeatsFor(const std::string& ending) {
    if (ending == "take") {
        return 1;
    }
    if (ending == "theirs") {
        return 2;
    }
    if (ending == "sheet") {
        return 3;
    }
    if (ending == "equip") {
        return 4;
    }
    if (ending == "pack") {
        return 5;
    }
    if (ending == "slot") {
        return 5;
    }
    if (ending == "dr") {
        return 6;
    }
    if (ending == "search") {
        return 7;
    }
    if (ending == "load") {
        return 8;
    }
    if (ending == "drop") {
        return 9;
    }
    return kKitBeats;
}

// ---------------------------------------------------------------------------
// JUSTICE BUILD (HEARING PAGE LANE): the court, played
// ---------------------------------------------------------------------------

/// How many beats runCourtLine tries to land, by ending. THE PAGE (the
/// default): Cull can SEE the player beside a patron; the paper -- lifts in
/// his sight until the row reads WANTED; taken at reach with paper, his own
/// line on the row with his hand on you; the plate, and the body at the
/// Mission's door with the page up. Three. "cull" stops on the second beat,
/// his line on the alert row in the room (two); "taken" stops on the plate,
/// TAKEN TO THE MISSION over black before the page (three); "paper" adds
/// HEAR THE PAPER open (four); "armed" presses I DID IT once, the row armed
/// with SURE on its tail and the priest pressing for the answer (four);
/// "plea" adds I DID IT pleaded and the check block on the page (four);
/// "deny" is the other plea, I DID NOT, weighed with THE PRIEST IS A MAN in
/// the block (four); "hand" takes the Skyrunners' oath off Finch first and
/// then the same road, so the paper asks for the hand and I DID IT lands
/// THE HAND (four); "serve" pleads I DID IT and TAKES the sentence row --
/// the coin paid, the nights or the days on the world clock, the record
/// closed, TURNED LOOSE on the row with the day it ended on (five).
///
/// The killing's endings: "bloodtag" stops on the corpse, WANTED FOR BLOOD
/// on the HUD (one); "ropepage" is a killing before the Watch drinks, the
/// paper with blood on it, taken to a rope hearing, I DID IT and THE ROPE
/// passed -- the rope hearing page, THE DROP offered and not taken (four);
/// "rope" takes the drop: the plate with the end rows under it (five);
/// "newman" arms the rope's own first row after the plate (six) and takes
/// it, the answer main() reads to open the creation window again (seven).
/// "wanted" stops on the first beat: the tag on the HUD, paper out, nobody's
/// hand on you yet (one).
///
/// BARKS & GATE LANE. A line before this one may leave the clock at eight
/// (--flame, the Mission's own hour: what you gave at the door is the only
/// coin the Flame reads, so the fine's tier is reached by GIVING first); the
/// court then waits for Cull through the wait page's own jump, exactly as
/// the rope line does after its killing.
constexpr std::int32_t kCourtBeats = 3;

constexpr std::int32_t kCourtCullBeats = 2;

constexpr std::int32_t kCourtTakenBeats = 3;

constexpr std::int32_t kCourtPaperBeats = 4;

constexpr std::int32_t kCourtArmedBeats = 4;

constexpr std::int32_t kCourtPleaBeats = 4;

constexpr std::int32_t kCourtServeBeats = 5;

constexpr std::int32_t kCourtBloodTagBeats = 1;

constexpr std::int32_t kCourtRopeBeats = 5;

constexpr std::int32_t kCourtNewManBeats = 7;

[[nodiscard]] std::int32_t courtBeatsFor(const std::string& ending) {
    if (ending == "wanted" || ending == "bloodtag") {
        return ending == "wanted" ? 1 : kCourtBloodTagBeats;
    }
    if (ending == "cull") {
        return kCourtCullBeats;
    }
    if (ending == "taken") {
        return kCourtTakenBeats;
    }
    if (ending == "paper") {
        return kCourtPaperBeats;
    }
    if (ending == "armed") {
        return kCourtArmedBeats;
    }
    if (ending == "plea" || ending == "deny" || ending == "hand") {
        return kCourtPleaBeats;
    }
    if (ending == "serve") {
        return kCourtServeBeats;
    }
    if (ending == "ropepage" || ending == "streetblood") {
        return kCourtRopePageBeats;
    }
    if (ending == "rope") {
        return kCourtRopeBeats;
    }
    if (ending == "newman") {
        return kCourtNewManBeats;
    }
    return kCourtBeats;
}

// ---------------------------------------------------------------------------
// S9: the burglary, played
// ---------------------------------------------------------------------------

/// How many beats runBurgleLine tries to land: crouch, stand in a dark doorway
/// unseen, a hand in a coat, up the stair, wire into a guest's box, the lock
/// open, and the box emptied.
constexpr std::int32_t kBurgleBeats = 7;

/// How many beats runCaseLine tries to land: the sheet in hand, the sheet
/// read, the Gull door lead, the wait to the small hours, the box lead on the
/// guest floor, Finch put down with fists, taken up, and delivered to the
/// Mission. Eight.
constexpr std::int32_t kCaseBeats = 8;

/// How many beats runEvictionLine tries to land on the PARTICIPATE path: the
/// hire off Maell (the writ heard, the open-hand bar passed), the writ read
/// on the Letters tile, the Netters' gate lead read, the wait to the evening
/// the door will answer, the Marrow door lead read, the knock, the serve, and
/// the walk back to the Mission with the writ -- The Evictor in hand and the
/// book closed. Nine.
constexpr std::int32_t kEvictBeats = 9;

/// The DISRUPT path owes fewer: everything up to and including the knock (six
/// beats), then the walk-back-unserved to Maell and the yield -- eight, and
/// no weapon.
constexpr std::int32_t kEvictRefuseBeats = 8;

// ---------------------------------------------------------------------------
// THE RUNG PLATE: --rung, a rung raised through the real verbs
// ---------------------------------------------------------------------------

/// The two beats a `--rung` run owes: the rung rose, and the plate is up.
constexpr std::int32_t kRungBeats = 2;

}  // namespace

int scriptedStartHour(const SmokeRunConfig& config) noexcept {
    // ONE IN THE MORNING, and the hour is the point.
    //
    // Finch keeps the snug from ten at night, so the line cannot start before
    // then -- but S6 put the ward's law in the same taproom between nine and
    // one, and a man who commits all six of the ward's crimes in one evening
    // with the impound keeper three tables away is a man who loses a hand for
    // it. That is the game working: the run was arrested on its way back from
    // the last delivery and lost two of its nine beats, with `sentence=maimed`
    // printed in its own summary.
    //
    // So the scripted burglar does what a burglar does and starts after the
    // Watch has gone home. Finch is still in the snug until three; Watchman
    // Cull left at one. Nothing about the line changed -- the hour did, and the
    // hour is now part of what the line teaches.
    if (config.skyrun) {
        return 1;
    }
    // THE RUNG PLATE. Each track's shortest path is one of the smoke's own
    // lines' beats, so each borrows that line's hour -- see runRungLine.
    if (!config.rung.empty()) {
        sim::LegendTrack track = sim::LegendTrack::Wire;
        if (legendTrackFromKey(config.rung, track)) {
            switch (track) {
                case sim::LegendTrack::Wire:
                    return 2;  // the burglar's hour: doors barred, lanterns out
                case sim::LegendTrack::Roofs:
                    return 18;  // six in the evening: the lead, the Gull and the harbour under the plate, not a black sky (the skyrun's own one in the morning is a card over nothing)
                case sim::LegendTrack::Flame:
                    return 9;  // the trail's: nine, the Mission's door open (test_casebook's own hour for it)
                case sim::LegendTrack::Trade:
                    return 21;  // the bounty's: Cull and Maell in one room
                case sim::LegendTrack::Law:
                    return 23;  // Cull on his stool, the Watch tests' own hour
            }
        }
    }
    // KIT BUILD. Two in the afternoon: daylight on the quay for the coil
    // the crosshair names and the coil put down at the end. The line moves
    // its own clock to four the next morning for the lifts (the house
    // empty, nobody to see them, no bouncer's ladder started) and back to
    // two for Ox and the coat's turn -- see runKitLine.
    if (config.kit || config.pack) {
        return 14;
    }
    // Father Maell takes an evening hour in the Gull between seven and half
    // past nine. Eight is the middle of it, which is also the default.
    if (config.flame) {
        return 20;
    }
    // The bounty wants two men in one room: Watchman Cull comes in at nine and
    // Father Maell leaves at half past. Quarter past is the only quarter of an
    // hour in the day when the ward will both sell you the work and sign for
    // it.
    if (config.contract) {
        return 21;
    }
    // The nemesis arc wants a labourer on shift and a room with his own guild
    // in it: Tarn Wrenhale keeps the taproom from six in the evening until one,
    // and the crowd he is enlisted out of is there from seven. Eight.
    if (config.nemesis) {
        return 20;
    }
    // The Watch line wants the impound keeper in the room and the room full
    // enough to swing at somebody in his sight: Watchman Cull keeps the Gull
    // from nine until one. Eleven, the watch tests' own hour.
    if (config.watchHalt) {
        return 23;
    }
    // VOICE LANE. The Watch ending is the Watch line's own hour; the Gull
    // ending wants the room full (eight); the street endings want the
    // Tarwalk crowded, which it is at four in the afternoon.
    if (config.voice) {
        if (config.voiceEnd == "watch") {
            return 23;
        }
        return (config.voiceEnd == "react" || config.voiceEnd == "hold") ? 20 : 16;
    }
    // STREET SENSES (9a completion). The street assault wants the Tarwalk at
    // its fullest: the day trades start at seven and work the quay through the
    // afternoon. Sixteen, the panic tests' and the gate's violence leg's own
    // hour -- a crowd to scatter.
    if (config.streetAssault) {
        return 16;
    }
    // JUSTICE BUILD. The court line wants Cull on his stool for the arrest
    // (eleven, the Watch line's own hour) -- except the rope, whose killing
    // is made at eight before he arrives and waits for him through the wait
    // page's own jump.
    if (config.court) {
        return courtEndingHangs(config.courtEnd) ? 20 : 23;
    }
    // TWO IN THE MORNING, and the hour is the whole point of the line.
    //
    // The Gull shuts at two: the doors are barred, the lanterns and the table
    // candles are out (Tavern::houseLights only shows them while isOpen()), the
    // hearth is banked from three, and the crowd has gone. What is left is a
    // dark room with a handful of night staff in it -- which is the only state
    // of this building a burglary is actually possible in, and proving that is
    // the point of `--burgle`. Later than three and there is nobody left to
    // creep past at all; earlier than two and the doors are open and the room
    // is lit. Run it at eight in the evening and it lands fewer beats and says
    // so, which is the game working.
    if (config.burgle) {
        return 2;
    }
    // COURIER CASE. Eight in the evening: the courier hail and the door lead
    // want the Gull open and the ward awake, and runCaseLine waits to two on
    // its own for the break-in -- demonstrating the clock the tutorial
    // teaches rather than skipping the lesson by starting in the dark.
    if (config.caseRun) {
        return 20;
    }
    // EVICTION CASE. Eight in the evening: Father Maell takes his hour in the
    // Gull between seven and half past nine (the courier and flame lines lean
    // on the same fact), so the hire is answerable at the start, and the
    // family-door beat wants the rota to have walked the serfs home -- which
    // eight is past. runEvictionLine skips forward to the door hour on its
    // own if the run started earlier, but starting IN Maell's hour is what
    // lets the hire be the first thing that happens.
    if (config.evictionRun) {
        return 20;
    }
    // The roof line needs the door open and nobody in particular.
    return -1;
}

SmokeRunResult runSmoke(const SmokeRunConfig& config) {
    SmokeRunResult result;
    SessionConfig started = config.session;
    if (!started.timeOfDayGiven) {
        const int hour = scriptedStartHour(config);
        if (hour >= 0) {
            started.timeOfDay = hour * 3600;
        }
    }
    Session session(started);
    if (!config.controlsFile.empty()) {
        // The one door the window has: loadControls, then setControls. The
        // file's own fov rides with it, as it does at the window's boot.
        ControlSettings controls = loadControls(config.controlsFile);
        controls.sanitise();
        session.setControls(controls);
    }

    // A scripted walk, so a capture at N steps is a picture of the game moving
    // rather than a picture of the spawn. Forward, with a slow drift of the
    // head, which is enough to exercise collision and band changes. `walk` off
    // holds position and lets the ROOM move instead, which is what a capture of
    // a tavern at two different hours wants.
    sim::MoveInput input;
    input.forward = config.walk ? 1 : 0;
    for (int i = 0; i < config.steps; ++i) {
        input.turn = (config.walk && (i / 90) % 4 == 3) ? 1 : 0;
        session.step(input);
    }

    // A capture OF a conversation, not of somebody standing beside one. Driven
    // through exactly the calls a keypress makes, so the frame a sprint proves
    // itself with is a picture of the game and not of a test harness.
    if (config.talk) {
        session.interact();
        result.talking = session.talking();
        for (const int topic : config.topics) {
            if (topic >= 0) {
                session.chooseTopic(static_cast<std::size_t>(topic));
            }
        }
        if (config.offer >= 0 && session.haggling()) {
            const int delta = config.offer - session.haggleOffer();
            session.adjustOffer(delta);
        }
        if (config.again) {
            session.closeConversation();
            session.interact();
        }
        result.talking = session.talking();
    }

    if (config.pause) {
        // THE SAME THREE CALLS ESC, DOWN, DOWN AND ENTER MAKE -- see
        // Session::togglePause's own header on why this exists at all.
        session.togglePause();
        bool landed = false;
        if (config.pauseEnd == "settings") {
            // TIME-AND-TENURE BUILD: one further press than before -- WAIT
            // took the second row, so every door below it moved down one.
            session.movePauseCursor(3);  // RESUME -> WAIT -> CONTROLS -> SETTINGS
            session.choosePause();
            landed = session.optionsOpen();
        } else if (config.pauseEnd == "controls") {
            // MORROWIND ROUND: KEYS' OWN NEW PAUSE-SIDE DOOR.
            session.movePauseCursor(2);  // RESUME -> WAIT -> CONTROLS
            session.choosePause();
            landed = session.keysOpen();
        } else if (config.pauseEnd == "armed") {
            session.movePauseCursor(4);  // ... -> SETTINGS -> QUIT
            session.choosePause();       // arms it; does not fire on one press
            landed = session.pauseOpen() && session.quitArmed();
        } else {
            landed = session.pauseOpen();
        }
        result.scriptedWanted += 1;
        result.scriptedLanded += landed ? 1 : 0;
    }

    // SHEETS BUILD: THE MENU-OPENING FLAGS (`--character`, `--map`,
    // `--refocus`) MOVED BELOW THE SCRIPTED LINES. They used to run here,
    // before them, and the combination was silently useless: every scripted
    // line closes whatever page is up on its way through, so
    // `--flame --character` photographed a CLOSED menu over a finished line,
    // and there was no way at all to photograph a tile WITH the state a line
    // had just driven into existence. Opening the page is now the last thing
    // the script does before the shutter, which is also the order a player's
    // own evening runs in.

    if (config.sprintSteps > 0) {
        // VERIFICATION ONLY. See SmokeRunConfig::sprintSteps's own header:
        // real sprint steps through the real gate, so the frame taken after
        // this shows whatever the pool honestly reads -- mid at a thousand
        // steps, empty-plus-refusal past two thousand.
        sim::MoveInput sprintInput;
        sprintInput.forward = 1;
        sprintInput.sprint = true;
        session.stepMany(sprintInput, config.sprintSteps);
    }

    if (config.punch) {
        // VERIFICATION ONLY. See SmokeRunConfig::punch's own header. The
        // SAME key F makes -- Session::punch() -- thrown at whoever is
        // nearest and retried until it actually connects, since a swing that
        // misses leaves nothing on screen for punchLandedPulse_ to draw.
        //
        // ACTION-COMBAT BUILD: a swing hits the first body ON THE LOOK-RAY
        // (Tavern::sightlineTarget), not the nearest body in reach, so a tap
        // thrown from wherever the smoke walk left the body is a tap at air --
        // this drive landed 0/1 at the Tarwalk spawn for exactly that reason.
        // It now throws the beat the nemesis and tenant lines throw: walk up
        // to the mark, put him dead on the crosshair, tap, step the recovery
        // lockout through, re-face him (a man who is hit back shifts his
        // feet), tap again. See tapUntilItConnects.
        session.closeConversation();
        bool landed = false;
        if (const sim::Actor* mark = nearestMark(session)) {
            landed = tapUntilItConnects(session, mark->id(), 16);
        }
        result.scriptedWanted += 1;
        result.scriptedLanded += landed ? 1 : 0;
    }

    if (config.chargeSteps > 0) {
        // VERIFICATION ONLY. See SmokeRunConfig::chargeSteps. The same
        // walk-up and re-face --punch uses, then the Attack DOWN-EDGE a held
        // left mouse button makes and NO release: the room's own hold clock
        // (Tavern::playerAttackDown) measures the charge in steps, and the
        // shutter goes with the key still down. Landed means the man was
        // closed on and faced, so the wind-up is photographed over a person.
        session.closeConversation();
        bool faced = false;
        if (const sim::Actor* mark = nearestMark(session)) {
            faced = closeOnAndFace(session, mark->id());
        }
        session.attackDown();
        session.stepMany(sim::MoveInput{}, config.chargeSteps);
        result.scriptedWanted += 1;
        result.scriptedLanded += faced ? 1 : 0;
    }

    if (config.block) {
        // VERIFICATION ONLY. See SmokeRunConfig::block's own header. The
        // brawl starts the way --punch starts one -- walked up to, faced and
        // tapped until the tap connects, so there is a man IN the fight to
        // throw the blows the guard is meant to catch -- the guard goes up
        // through the SAME Session::setBlocking() the right mouse button
        // calls, and the wait is until the room itself says a blow was
        // softened -- blowsBlocked() moving -- because a run of whiffs leaves
        // a GUARD UP row over a fight the guard never actually worked in.
        // The wait runs even if no tap connected: a sightline that found him
        // put him in the brawl whether or not the roll landed, and his blows
        // are what the beat is about.
        session.closeConversation();
        if (const sim::Actor* mark = nearestMark(session)) {
            (void)tapUntilItConnects(session, mark->id(), 16);
        }
        // ACTION-COMBAT BUILD: the swing dropped the guard clause to false for
        // its recovery window (section 1.3), so let the hand return to idle
        // before raising the guard -- otherwise the first steps of the wait
        // below would be spent with the guard derived down.
        session.stepMany(sim::MoveInput{}, sim::kSwingRecoverySteps + 1);
        session.setBlocking(true);
        const std::int32_t before = session.tavern().blowsBlocked();
        // Blows land once a simulated second; a dozen seconds of held guard
        // is enough for several, and the bound keeps a pathological room from
        // hanging the capture.
        // THE PULL PACK: `--block=N` waits for N softened blows (a dozen
        // seconds each), so shieldwall's first level -- five -- is earned in
        // the fight and the toast is on the frame with the GUARD UP row.
        const std::int32_t wanted = std::max(1, config.blockBlows);
        const int kBlockWaitSteps = 12 * 60 * wanted;
        int waited = 0;
        while (session.tavern().blowsBlocked() < before + wanted && waited < kBlockWaitSteps) {
            session.stepMany(sim::MoveInput{}, 1);
            ++waited;
        }
        result.scriptedWanted += 1;
        result.scriptedLanded += session.tavern().blowsBlocked() >= before + wanted ? 1 : 0;
    }

    if (config.street) {
        const StreetLineResult street = runStreetLine(session, config.streetWho,
                                                      config.streetTopic);
        result.talking = session.talking();
        // ONE BEAT, AND IT EITHER LANDED OR IT DID NOT. The whole claim of #79
        // is that a body in the open district answers, so the run reports it
        // the same way every other scripted line reports its beats and a
        // capture that photographed nobody cannot pass as one that did.
        result.scriptedWanted += 1;
        result.scriptedLanded += street.opened ? 1 : 0;
        result.streetSpeaker = street.name;
        result.streetKey = street.barkKey;
    }

    if (!config.tone.empty()) {
        // THE TONE DIAL. The row a player presses, pressed until the register
        // reads what was asked -- at most three presses, the dial's length.
        result.scriptedWanted += 1;
        // toneName answers in capitals; the flag is typed however it is typed.
        const std::string want = upperAscii(config.tone);
        bool landed = false;
        for (int press = 0; press < 3 && session.talking() && !landed; ++press) {
            std::string now(sim::toneName(session.tavern().dialogue().tone()));
            landed = now == want;
            if (landed) {
                break;
            }
            const std::vector<sim::Topic>& topics = session.tavern().dialogue().topics();
            for (std::size_t i = 0; i < topics.size(); ++i) {
                if (topics[i].kind == sim::TopicKind::Tone) {
                    session.chooseTopic(i);
                    break;
                }
            }
            landed = std::string(sim::toneName(session.tavern().dialogue().tone())) == want;
        }
        result.scriptedLanded += landed ? 1 : 0;
        result.talking = session.talking();
    }

    if (config.flame) {
        result.flameStages = runFlameLine(session, config.flameEnd);
        result.talking = session.talking();
        const sim::Questline* line = session.tavern().dialogue().quests().find("flame-disciple");
        result.scriptedWanted += line == nullptr ? 0 : static_cast<std::int32_t>(
                                                          line->stages.size());
        result.scriptedLanded += result.flameStages;
    }

    if (config.petition) {
        // TIME-AND-TENURE BUILD. See SmokeRunConfig::petition's own header.
        // Three beats: somebody of the cloth found ON a plot, the roll read,
        // the charge petitioned -- and the third only counts when the ROLL
        // says it did (playerIsDuke), not when a sentence was merely said.
        result.petitionResult = runPetitionLine(session, true);
        result.talking = session.talking();
        result.scriptedWanted += 3;
        result.scriptedLanded += (result.petitionResult.opened ? 1 : 0) +
                                 (result.petitionResult.rollLine.empty() ? 0 : 1) +
                                 (result.petitionResult.becameDuke ? 1 : 0);
    }

    if (config.shop) {
        // SHOPS. See SmokeRunConfig::shop: the beats each end owes.
        result.shopResult = runShopLine(session, config.shopEnd);
        result.talking = session.talking();
        const ShopLineResult& shop = result.shopResult;
        const int wanted = config.shopEnd == "wares"    ? 2
                           : config.shopEnd == "bought" ? 3
                           : config.shopEnd == "pawn"   ? 5
                                                        : 6;
        const int landed = (shop.chandler ? 1 : 0) + (shop.wares ? 1 : 0) +
                           (shop.bought ? 1 : 0) + (shop.pawnbroker ? 1 : 0) +
                           (shop.pawnList ? 1 : 0) + (shop.sold ? 1 : 0);
        result.scriptedWanted += wanted;
        result.scriptedLanded += std::min(wanted, landed);
    }

    if (config.radiant) {
        // RADIANT BUILD. See SmokeRunConfig::radiant. The "offer" end owes
        // one beat (the giver's own row on the visible list, conversation
        // left open for the shutter); "taken" owes three -- offered, the
        // board moved, the journal shows it.
        const bool takeIt = config.radiantEnd != "offer";
        result.radiantResult = runRadiantLine(session, config.radiantEnd);
        result.talking = session.talking();
        const RadiantLineResult& job = result.radiantResult;
        if (config.radiantEnd == "ask") {
            result.scriptedWanted += 2;
            result.scriptedLanded += (job.opened ? 1 : 0) + (job.asked ? 1 : 0);
        } else if (config.radiantEnd == "settled") {
            // ERRANDS A HUMAN CAN FINISH: offered, taken, the journal, the
            // shelf, the target reached, the word passed, the board and the
            // purse agreeing it was paid. `walked` is printed, not owed: a
            // target on another band is placed beside, said so.
            result.scriptedWanted += 7;
            result.scriptedLanded += (job.offered ? 1 : 0) + (job.taken ? 1 : 0) +
                                     (job.journal ? 1 : 0) + (job.shelf ? 1 : 0) +
                                     (job.reached ? 1 : 0) + (job.passed ? 1 : 0) +
                                     (job.settled ? 1 : 0);
        } else if (config.radiantEnd == "shelf") {
            result.scriptedWanted += 4;
            result.scriptedLanded += (job.offered ? 1 : 0) + (job.taken ? 1 : 0) +
                                     (job.journal ? 1 : 0) + (job.shelf ? 1 : 0);
        } else if (takeIt) {
            result.scriptedWanted += 3;
            result.scriptedLanded += (result.radiantResult.offered ? 1 : 0) +
                                     (result.radiantResult.taken ? 1 : 0) +
                                     (result.radiantResult.journal ? 1 : 0);
        } else {
            result.scriptedWanted += 1;
            result.scriptedLanded += result.radiantResult.offered ? 1 : 0;
        }
    }

    if (config.quickbar) {
        // VERIFICATION ONLY. See SmokeRunConfig::quickbar's own header: the
        // same public verbs a keypress calls, end to end -- page open, the
        // cursor's crafting walked onto slot 3 (three RIGHT presses from
        // NONE), page down, number pressed. The beat lands only if the slot
        // genuinely holds a crafting AND the hand genuinely equipped it,
        // which is the round-trip the whole task exists to close. BEFORE the
        // cast block below, deliberately, so `--flame --quickbar --cast` is
        // a true equip-then-cast sequence: what C spends is what the number
        // row just readied.
        session.closeConversation();
        session.toggleGrimoire();
        bool landed = false;
        if (session.grimoireOpen()) {
            session.adjustGrimoireSlot(3);
            session.closeConversation();
            // THE CELLS PASS: the Grimoire's own binding line (CLEAR THE HEAD
            // -- SLOT 3.) holds say()'s four seconds on the alert row, and
            // with the press a step behind it every strip frame carried that
            // shout under the strip, so no frame ever showed the quiet the
            // press has now. In play the binding and the press are seconds
            // apart; the drive waits the line out (say()'s own clock, not
            // the plates'), then presses.
            session.stepMany(sim::MoveInput{}, 4 * sim::kStepsPerSecond + 10);
            session.selectQuickSlot(2);
            const sim::Spell* bound = session.tavern().slotSpell(2);
            const sim::Spell* held = session.tavern().equippedSpell();
            landed = bound != nullptr && held != nullptr && bound->id == held->id;
        }
        result.scriptedWanted += 1;
        result.scriptedLanded += landed ? 1 : 0;
    }

    if (config.cast) {
        // VERIFICATION ONLY. See SmokeRunConfig::cast's own header: the same
        // call C makes, once. AFTER the flame block above, deliberately --
        // that line's teaching beat is what stocks the grimoire, so
        // `--flame --flameEnd=away --cast` photographs the CAST row with a
        // crafting actually in the hand; --cast alone photographs the
        // empty-grimoire refusal on the alert row, the common state.
        session.closeConversation();
        session.castEquipped();
        result.scriptedWanted += 1;
        result.scriptedLanded += session.lastMessage().empty() ? 0 : 1;
    }

    if (!config.heldSpells.empty()) {
        // HELD-EFFECTS BUILD. See SmokeRunConfig::heldSpells's own header:
        // each id equipped and cast until its link opens, in order, so the
        // frame photographs live holds with their clocks -- and, for the
        // two-id run, the second crafting's recovery printed off a mind the
        // first is still holding. One beat per id.
        session.closeConversation();
        std::size_t start = 0;
        while (start <= config.heldSpells.size()) {
            const std::size_t comma = config.heldSpells.find(',', start);
            const std::string id =
                comma == std::string::npos
                    ? config.heldSpells.substr(start)
                    : config.heldSpells.substr(start, comma - start);
            if (!id.empty()) {
                result.scriptedWanted += 1;
                result.scriptedLanded += runHeldCast(session, id) ? 1 : 0;
            }
            if (comma == std::string::npos) {
                break;
            }
            start = comma + 1;
        }
    }

    if (config.grimoire) {
        // VERIFICATION ONLY. See SmokeRunConfig::grimoire's own header. After
        // the quickbar block, deliberately, so `--flame --quickbar --grimoire`
        // photographs the page WITH a slot column on it.
        session.closeConversation();
        session.toggleGrimoire();
        result.scriptedWanted += 1;
        result.scriptedLanded += session.grimoireOpen() ? 1 : 0;
    }

    if (config.wait) {
        // TIME-AND-TENURE BUILD. See SmokeRunConfig::wait's own header: the
        // pause menu's own WAIT row, pressed the way a hand presses it, so
        // the capture is a picture of the DOOR working and not only of the
        // page existing.
        session.closeConversation();
        session.togglePause();
        session.movePauseCursor(1);  // RESUME -> WAIT
        session.choosePause();
        result.scriptedWanted += 1;
        result.scriptedLanded += session.waitOpen() && !session.waitSleeping() ? 1 : 0;
    }

    if (config.roofs) {
        // Four beats: the stair, the floor above it, the wall, the lead.
        constexpr std::int32_t kRoofBeats = 4;
        const std::int32_t landed = static_cast<std::int32_t>(
            runRoofLine(session, config.roofsEnd));
        result.scriptedWanted += kRoofBeats;
        result.scriptedLanded += landed;
        result.talking = session.talking();
    }

    if (config.contract) {
        const std::int32_t landed =
            static_cast<std::int32_t>(runContractLine(session, config.contractEnd));
        result.contractBeats = landed;
        // `held` stops the line after two beats ON PURPOSE (see its note in
        // runContractLine), so two is what it owes -- asking for six would
        // print the fell-short warning over a run that did exactly what was
        // asked of it.
        result.scriptedWanted +=
            config.contractEnd == "held" ? kContractHeldBeats : kContractBeats;
        result.scriptedLanded += landed;
        result.talking = session.talking();
    }

    if (config.nemesis) {
        const std::int32_t landed =
            static_cast<std::int32_t>(runNemesisLine(session, config.nemesisEnd));
        result.nemesisBeats = landed;
        result.scriptedWanted +=
            config.nemesisEnd == "death" ? kNemesisDeathBeats : kNemesisBeats;
        result.scriptedLanded += landed;
        result.talking = session.talking();
    }

    if (config.watchHalt) {
        const std::int32_t landed =
            static_cast<std::int32_t>(runWatchHaltLine(session, config.watchHaltEnd));
        result.watchHaltBeats = landed;
        // "halt" stops on the third beat by design; it owes three, not five.
        result.scriptedWanted += config.watchHaltEnd == "halt" ? kWatchHaltStopBeats : kWatchHaltBeats;
        result.scriptedLanded += landed;
    }

    if (config.voice) {
        // VOICE LANE. The one voice channel, played -- see runVoiceLine.
        const std::int32_t landed =
            static_cast<std::int32_t>(runVoiceLine(session, config.voiceEnd));
        result.voiceBeats = landed;
        result.scriptedWanted += voiceBeatsFor(config.voiceEnd);
        result.scriptedLanded += landed;
        result.talking = session.talking();
        const VoiceLine& line = session.voice().current();
        result.voiceChannel = std::string(voiceChannelName(line.channel));
        result.voiceKey = line.key;
        result.voiceSpeaker = line.name;
        result.voiceRow = line.text;
        result.voiceAlertSpeaker = std::string(session.alertSpeakerNow());
        // AND WHETHER THE SPEAKER IS IN THE FRAME, from where the body stands
        // and faces at the shutter: the field of view, a clear lens line and
        // nobody drawn in the way (speakerInView), and the distance in
        // tenths of a tile. The body the frame is ABOUT: the row's speaker,
        // or with a panel open (the moved ending) the one you are talking
        // to, whose reply and worded reaction the panel is showing.
        std::int32_t sx = 0;
        std::int32_t sy = 0;
        std::int32_t sband = 0;
        std::int32_t speakerId = session.voice().current().actorId;
        bool framed = false;
        if (session.talking() && session.wardTalkingTo() >= 0) {
            if (const sim::WardActor* who = session.people().byId(session.wardTalkingTo());
                who != nullptr && who->visible()) {
                const std::pair<std::int32_t, std::int32_t> at = wardDrawnQ8(session, *who);
                sx = at.first;
                sy = at.second;
                sband = who->band;
                speakerId = who->id + sim::kWardSpeakerIdBase;
                framed = true;
            }
        } else {
            framed = speakerDrawnQ8(session, sx, sy, sband);
        }
        if (framed) {
            result.voiceSpeakerInView = speakerInView(session, sx, sy, sband, speakerId);
            const double dx = static_cast<double>(sx - session.body().x());
            const double dy = static_cast<double>(sy - session.body().y());
            result.voiceSpeakerTenths = static_cast<std::int32_t>(
                std::lround(std::sqrt(dx * dx + dy * dy) * 10.0 / static_cast<double>(sim::kSubOne)));
            std::int32_t off = static_cast<std::int32_t>(
                (bearingToward(session, sx, sy) - session.body().yaw()) & (sim::kTurnFull - 1));
            if (off > sim::kTurnFull / 2) {
                off -= sim::kTurnFull;
            }
            result.voiceSpeakerOffDeg =
                std::abs(off) * 360 / sim::kTurnFull;
        }
    }

    if (config.kit) {
        // KIT BUILD. The Kit, played through the real verbs -- see
        // runKitLine. Each ending owes its own count.
        const std::int32_t landed =
            static_cast<std::int32_t>(runKitLine(session, config.kitEnd));
        result.kitBeats = landed;
        result.scriptedWanted += kitBeatsFor(config.kitEnd);
        result.scriptedLanded += landed;
    }

    if (config.pack) {
        // OBLIVION-PLAN phase 8. The pack, played through the real verbs --
        // see runPackLine. Each ending owes its own count.
        const std::int32_t landed =
            static_cast<std::int32_t>(runPackLine(session, config.packEnd));
        result.packBeats = landed;
        result.scriptedWanted += packBeatsFor(config.packEnd);
        result.scriptedLanded += landed;
    }

    if (config.streetAssault) {
        // STREET SENSES. PROOF. Stand in the crowd and raise steel (the settle
        // window below scatters the street), or -- leg (b) -- put a docker
        // down, up, or dead, or take the killing to the bench. Landed = a
        // crowd tile was found and stood in, and the ending's own beat came
        // true (a blow that landed, a man floored, a man up, a corpse, the
        // rope hearing page).
        const StreetLineResult assault = runStreetAssault(session, config.streetAssaultWhere);
        const bool steelOnly = config.streetAssaultWhere.empty() || config.streetAssaultWhere == "steel";
        result.scriptedWanted += 1;
        result.scriptedLanded += (assault.found && (steelOnly || assault.opened)) ? 1 : 0;
    }

    if (config.court) {
        // JUSTICE BUILD (HEARING PAGE LANE). The bench, played through the
        // real verbs -- see runCourtLine. Each ending owes its own count.
        const std::int32_t landed =
            static_cast<std::int32_t>(runCourtLine(session, config.courtEnd));
        result.courtBeats = landed;
        result.scriptedWanted += courtBeatsFor(config.courtEnd);
        result.scriptedLanded += landed;
    }

    if (config.trail) {
        // NOT A FIXED BEAT COUNT. The trail is authored data; how many leads
        // there are is casebook.json's business, and a target typed in here
        // would be a second source of truth for it. What the run owes is that
        // every lead it WALKED TO it also read -- an unreachable lead fails the
        // run, which is exactly the failure a coordinate drifting would cause.
        result.trailRead = static_cast<std::int32_t>(runTrailLine(session, config.trailEnd));
        result.trailWalked = gTrailWalked;
        result.scriptedWanted += gTrailWalked;
        result.scriptedLanded += result.trailRead;
        result.talking = session.talking();
    }

    if (!config.follow.empty()) {
        // THE PULL PACK. FOLLOW, through the page's own verbs -- see
        // SmokeRunConfig::follow. What LANDED is the ribbon actually carrying
        // the lead asked for: a follow that quietly fell back to the default
        // is the failure this counter exists to make loud.
        if (!session.casebookPageOpen()) {
            session.toggleCasebook();
        }
        result.scriptedWanted += 1;
        if (session.casebookPageOpen() && session.selectCasebookLead(config.follow)) {
            session.followCasebookSelection();
            const PullTarget target = session.pullTarget();
            const sim::CasebookRaws& raws =
                target.book == CaseBookId::Courier    ? session.sheetRaws()
                : target.book == CaseBookId::Eviction ? session.evictRaws()
                                                      : *session.casebook().raws();
            const bool took = target.set && target.chosen &&
                              static_cast<std::size_t>(target.lead) < raws.leads().size() &&
                              raws.leads()[static_cast<std::size_t>(target.lead)].id == config.follow;
            // A REFUSAL THAT WAS READ is the page answering too: a lead
            // already stood over refuses in the page's own band.
            const bool refused = !took && !session.casebookPageState().alert.empty();
            result.scriptedLanded += (took || refused) ? 1 : 0;
        }
        if (config.followEnd == "street") {
            // THE BOOK DOWN, the same call the Menu key makes, and a walk
            // forward so the paces on the ribbon are a picture of movement.
            session.toggleCasebook();
            sim::MoveInput walk;
            walk.forward = 1;
            for (int i = 0; i < config.followWalk; ++i) {
                session.step(walk);
            }
        }
    }

    if (config.burgle) {
        const std::int32_t landed =
            static_cast<std::int32_t>(runBurgleLine(session, config.burgleEnd));
        result.burgleBeats = landed;
        result.burgleBeatMask = gBurgleBeatMask;
        result.watchersInReach = gBurgleWatchers;
        result.scriptedWanted += kBurgleBeats;
        result.scriptedLanded += landed;
        result.talking = session.talking();
    }

    if (config.caseRun) {
        // COURIER CASE. A short `ending` owes only the beats up to its shutter,
        // exactly runContractLine's `held` rule -- so the fell-short warning
        // never fires over a run that stopped where it was told to.
        const std::int32_t landed =
            static_cast<std::int32_t>(runCaseLine(session, config.caseEnd));
        result.caseBeats = landed;
        result.caseBeatMask = gCaseBeatMask;
        const std::int32_t owed = config.caseEnd == "sheet"  ? 2
                                  : config.caseEnd == "gull"  ? 3
                                  : config.caseEnd == "night" ? 5
                                  : config.caseEnd == "down"  ? 6
                                  : config.caseEnd == "taken" ? 7
                                                              : kCaseBeats;
        result.scriptedWanted += owed;
        result.scriptedLanded += landed;
        result.talking = session.talking();
    }

    if (config.evictionRun) {
        // EVICTION CASE. A short `ending` owes only the beats up to its
        // shutter, and the two paths owe different totals -- the participate
        // path nine, the disrupt path eight -- so the fell-short warning
        // never fires over a run that stopped where it was told to, on either
        // fork.
        const std::int32_t landed =
            static_cast<std::int32_t>(runEvictionLine(session, config.evictionEnd));
        result.evictBeats = landed;
        result.evictBeatMask = gEvictBeatMask;
        const std::int32_t owed = config.evictionEnd == "writ"    ? 2
                                  : config.evictionEnd == "gate"  ? 3
                                  : config.evictionEnd == "knock" ? 6
                                  : config.evictionEnd == "served" ? 7
                                  : config.evictionEnd == "refused" ? kEvictRefuseBeats
                                                                    : kEvictBeats;
        result.scriptedWanted += owed;
        result.scriptedLanded += landed;
        result.talking = session.talking();
    }

    if (config.skyrun) {
        result.skyrunStages = runSkyrunLine(session, config.skyrunEnd);
        result.talking = session.talking();
        const sim::Questline* line =
            session.tavern().dialogue().quests().find("skyrunner-tenant");
        result.scriptedWanted += line == nullptr ? 0 : static_cast<std::int32_t>(
                                                           line->stages.size());
        result.scriptedLanded += result.skyrunStages;
    }

    if (!config.rung.empty()) {
        // THE RUNG PLATE. See SmokeRunConfig::rung and runRungLine. Two
        // beats: the track's rung genuinely rose off the real verbs, and the
        // plate is up, fully risen, with that rung's own words on it. A
        // track word that is none of the five lands neither and says so.
        result.rungResult = runRungLine(session, config.rung);
        result.scriptedWanted += kRungBeats;
        result.scriptedLanded += (result.rungResult.rose ? 1 : 0) +
                                 (result.rungResult.plateUp ? 1 : 0);
        result.talking = session.talking();
    }

    // DISTRICT PHASE D. THE CROSSING, AFTER every scripted line above it and
    // BEFORE the menu-opening flags below -- which is the only order that lets
    // `--threshold=saltgate --map-overlay` photograph the stand-down rule
    // itself: the plate is armed by a real walk and then a page takes the
    // screen, and the frame proves it does not draw over one. See
    // SmokeRunConfig::threshold.
    if (!config.threshold.empty()) {
        result.thresholdResult =
            runThresholdLine(session, config.threshold, config.thresholdEnd);
        // THREE BEATS, AND THE THIRD IS THE DELIVERABLE: the crossing is in
        // the table, the body genuinely ended up on the far side of it, and
        // the plate was live when the shutter went. A capture that walks the
        // walk and announces nothing is the failure this counter exists to
        // make loud rather than leave for somebody to notice in a PNG.
        result.scriptedWanted += 3;
        result.scriptedLanded += result.thresholdResult.found ? 1 : 0;
        result.scriptedLanded += result.thresholdResult.crossed ? 1 : 0;
        result.scriptedLanded += result.thresholdResult.announced ? 1 : 0;
    }

    // SHEETS BUILD: THE MENU-OPENING FLAGS RUN HERE NOW, AFTER every scripted
    // line -- see the note where they used to sit, above `--punch` -- so a
    // capture can finally photograph a tile WITH the state a line just drove
    // into existence (`--flame --flameEnd=away --refocus=character` is a
    // sheet with rungs on it; `--contract --contractEnd=held
    // --refocus=journal` is a journal with a live job on it). `--character`/
    // `--map` alone behave exactly as they always did: nothing above runs
    // without its own flag.
    if (config.character) {
        // THE SAME CALL `C` MAKES -- see SmokeRunConfig::character's own
        // header on why this exists at all.
        session.toggleCharacter();
        result.scriptedWanted += 1;
        result.scriptedLanded += session.characterOpen() ? 1 : 0;
    }

    if (config.map) {
        // THE TILED MENU'S CHART TILE -- see SmokeRunConfig::map's own header.
        // (M no longer reaches this: core action #13 gave M to the ward map
        // below; the Chart tile is reached through the Menu key as ever.)
        session.toggleMap();
        result.scriptedWanted += 1;
        result.scriptedLanded += session.mapOpen() ? 1 : 0;
    }

    // THE CASEBOOK PASS. The book's own cursor, view and commit verb, each
    // through the same public method a key press calls. Runs BEFORE the ward
    // map's flags on purpose: `--case-route` OPENS the ward map, so a command
    // line that asks for both gets the route's answer rather than a map the
    // route then overwrote.
    if (!config.caseLead.empty() || !config.caseTab.empty() || config.caseRoute) {
        if (!session.casebookPageOpen()) {
            // THE SAME CALL THE MENU KEY MAKES.
            session.toggleCasebook();
        }
        result.scriptedWanted += 1;
        result.scriptedLanded += session.casebookPageOpen() ? 1 : 0;
        if (!config.caseLead.empty()) {
            result.scriptedWanted += 1;
            result.scriptedLanded += session.selectCasebookLead(config.caseLead) ? 1 : 0;
        }
        if (!config.caseTab.empty()) {
            std::string want = config.caseTab;
            for (char& c : want) {
                if (c >= 'A' && c <= 'Z') {
                    c = static_cast<char>(c - 'A' + 'a');
                }
            }
            result.scriptedWanted += 1;
            if (want == "leads") {
                while (session.casebookTab() != CasebookTab::Leads) {
                    session.cycleCasebookTab(1);
                }
                result.scriptedLanded += 1;
            } else if (want == "case") {
                while (session.casebookTab() != CasebookTab::Case) {
                    session.cycleCasebookTab(1);
                }
                result.scriptedLanded += 1;
            } else if (want == "cases") {
                // THE PULL PACK: the shelf.
                while (session.casebookTab() != CasebookTab::Cases) {
                    session.cycleCasebookTab(1);
                }
                result.scriptedLanded += 1;
            }
        }
        if (config.caseRoute) {
            // ENTER ON THE COMMIT VERB. What LANDED is the ward map being open
            // on the lead's own place -- a route that quietly did nothing is
            // exactly the failure this flag exists to catch.
            const std::vector<std::int32_t> book = session.casebook().known();
            std::int32_t lead = -1;
            if (!book.empty()) {
                const int at = std::clamp(session.casebookLeadCursor(), 0,
                                          static_cast<int>(book.size()) - 1);
                lead = book[static_cast<std::size_t>(at)];
            }
            const int wanted = session.mapPlaceForLead(lead);
            session.commitCasebookLead();
            result.scriptedWanted += 1;
            result.scriptedLanded +=
                (wanted >= 0 && session.districtMapOpen() &&
                 session.districtMapSelected() == wanted)
                    ? 1
                    : 0;
        }
    }

    if (config.mapOverlay) {
        // THE WARD MAP -- THE SAME CALL `M` MAKES. See
        // SmokeRunConfig::mapOverlay's own header.
        session.toggleDistrictMap();
        result.scriptedWanted += 1;
        result.scriptedLanded += session.districtMapOpen() ? 1 : 0;
        // THE MAP PASS. The page has a cursor, four views and a zoom ladder,
        // and every one of them goes through the SAME public method a key
        // press calls -- selectDistrictMapPlace, setDistrictMapTab,
        // adjustDistrictMapZoom -- so a captured frame is a picture of the game
        // and not of a capture path that happens to look like it.
        if (!config.mapPlace.empty()) {
            result.scriptedWanted += 1;
            result.scriptedLanded += session.selectDistrictMapPlace(config.mapPlace) ? 1 : 0;
        }
        if (!config.mapTab.empty()) {
            std::string want = config.mapTab;
            for (char& c : want) {
                if (c >= 'A' && c <= 'Z') {
                    c = static_cast<char>(c - 'A' + 'a');
                }
            }
            const int index = want == "overview" ? 0
                              : want == "people" ? 1
                              : want == "index"  ? 2
                              : want == "legend" ? 3
                                                 : -1;
            result.scriptedWanted += 1;
            if (index >= 0) {
                session.setDistrictMapTab(index);
                result.scriptedLanded += 1;
            }
        }
        if (config.mapZoom > 0) {
            session.adjustDistrictMapZoom(config.mapZoom);
            result.scriptedWanted += 1;
            result.scriptedLanded += session.districtMapZoom() == config.mapZoom ? 1 : 0;
        }
    }

    // FAST TRAVEL (TRAVEL lane). The probe: cursor onto the named place and
    // the TRAVEL verb pressed, through the same public methods the T key
    // spends. AFTER the scripted lines on purpose, so a carry or a stance a
    // line just drove is what the press is refused against (`--case=taken
    // --travel=...` is the carry refusal probed for real); BEFORE `--face`,
    // which closes the page this needs open. Two beats: the name matched,
    // and the press RESOLVED HONESTLY -- an available plan must land the
    // body on its own tile with the clock advanced by exactly the restated
    // minutes, and a refused one must move nothing at all with the page
    // still up. Anything between those is the failure this probe exists to
    // make loud.
    if (!config.travelTo.empty()) {
        TravelLineResult& probe = result.travelResult;
        if (!session.districtMapOpen()) {
            session.toggleDistrictMap();
        }
        probe.found = session.selectDistrictMapPlace(config.travelTo);
        result.scriptedWanted += 2;
        if (probe.found) {
            result.scriptedLanded += 1;
            probe.plan = session.districtMapTravelPlan();
            probe.clockFrom = session.timeOfDay();
            session.travelDistrictMapSelection();
            probe.clockTo = session.timeOfDay();
            probe.endX = session.body().tileX();
            probe.endY = session.body().tileY();
            probe.endBand = session.body().band();
            probe.plateUp = session.placePlateWanted();
            probe.plate = std::string(session.placePlateLabel());
            probe.moved = probe.plan.available && !session.districtMapOpen() &&
                          probe.endX == probe.plan.toX && probe.endY == probe.plan.toY &&
                          probe.endBand == probe.plan.toBand;
            const bool clockExact =
                (probe.clockFrom + probe.plan.minutes * 60) % sim::kSecondsPerDay ==
                probe.clockTo;
            const bool refusedClean = !probe.plan.available && session.districtMapOpen() &&
                                      probe.clockFrom == probe.clockTo;
            result.scriptedLanded +=
                (probe.plan.available ? (probe.moved && clockExact) : refusedClean) ? 1 : 0;
        }
    }

    // THE CROSSHAIR PASS. TURN AND STAND. See SmokeRunConfig::face's own
    // header: this is how a door gets photographed with its name on the
    // crosshair. It runs AFTER the menu flags and closes the ward map by
    // construction (faceDistrictMapSelection does), so `--face` and
    // `--map-overlay` in the same command line resolve the way a player
    // pressing ENTER on the map does rather than fighting.
    if (!config.face.empty()) {
        result.scriptedWanted += 1;
        if (session.selectDistrictMapPlace(config.face)) {
            session.faceDistrictMapSelection();
            result.scriptedLanded += 1;
        }
    }

    // PLANNING SPRINT (item #1). THE GAP `--character --map` COULD NEVER
    // CLOSE, closed. See SmokeRunConfig::refocus's own header for what this
    // fixes and why: with nothing here, a caller wanting a mid-crossfade
    // frame had to fire both toggle*() calls back to back (`config.character`
    // and `config.map` above, with zero step() calls between them) and could
    // only ever photograph the SECOND tile's animation from a cold start.
    //
    // THIS RUNS BEFORE `settleSteps`' OWN GENERAL LOOP FURTHER DOWN, on
    // purpose: it spends its own settle budget getting the ORIGIN tile
    // (`character`/`map` above) genuinely open first, switches focus, spends
    // a SECOND, separate budget (`refocusSteps`) easing the switch partway
    // (or all the way -- the caller's choice), and only then falls through to
    // the ordinary settle logic below, which sees `config.refocus` non-empty
    // and stands down rather than spending a third, redundant round of steps.
    if (!config.refocus.empty()) {
        // UI-EA-SPEC sec. 0 (FLOW): the settle default is kCaptureRestSteps
        // now -- see the general loop below for the whole argument.
        const int preSteps = config.settleSteps >= 0 ? config.settleSteps
                                                     : (config.settle ? kCaptureRestSteps : 0);
        const sim::MoveInput still{};
        for (int i = 0; i < preSteps; ++i) {
            session.step(still);
        }
        // THE SAME FOUR PUBLIC TOGGLES A KEYPRESS CALLS -- toggleCharacter()/
        // toggleMap()/toggleLetters()/toggleCasebook() -- never
        // toggleMenuFocused() directly, so this capture proves nothing a
        // player's own keyboard could not also have produced.
        bool refocused = false;
        if (config.refocus == "character") {
            session.toggleCharacter();
            refocused = true;
        } else if (config.refocus == "map") {
            session.toggleMap();
            refocused = true;
        } else if (config.refocus == "letters") {
            session.toggleLetters();
            refocused = true;
        } else if (config.refocus == "journal") {
            session.toggleCasebook();
            refocused = true;
        }
        result.scriptedWanted += 1;
        result.scriptedLanded += refocused ? 1 : 0;
        for (int i = 0; i < config.refocusSteps; ++i) {
            session.step(still);
        }
    }

    // SHEETS BUILD. See SmokeRunConfig::tilePage's own header: N presses of
    // the same public call the 0/MORE key makes, so a tile's second page --
    // where the faction-ladder rows live -- can actually be photographed.
    // After the menu-opening flags above, which is the only order in which
    // there is a page to turn.
    for (int i = 0; i < config.tilePage; ++i) {
        session.nextTopicPage();
    }

    // S7. The cursor, last, so it survives every scripted line above it. This
    // is the flag that lets a frame be taken OF the row that does not fit --
    // see SmokeRunConfig::cursorRow on why that had to be possible.
    if (config.cursorRow > 0 && session.talking()) {
        const int want = config.cursorRow - 1;
        session.moveTopicCursor(want - session.topicCursor());
    }
    // PANES PASS: THE SAME IDEA ON THE CONVERTED CONTROLS PAGE -- see
    // SmokeRunConfig::keysRow on why it is its own flag. moveTopicCursor
    // already routes to the keys page's own cursor (its keysOpen_ branch), so
    // this is the identical call the arrow keys make and there is no
    // capture-only path through the drawing.
    if (config.keysRow > 0 && session.keysOpen()) {
        session.moveTopicCursor(config.keysRow - 1);
    }

    // VERIFICATION. See SmokeRunConfig::settle's own header. Zero-input
    // steps, same as `--hold`'s own loop -- nothing walks, nothing turns --
    // run comfortably past EasedToggle's default riseSteps (8) so the panel
    // this capture opened is fully open, not caught on its first tick.
    //
    // GATED ON A SCREENSHOT ACTUALLY BEING TAKEN, not on config.settle alone.
    // The field defaults true, but a scripted run that only reads back
    // scriptedWanted/scriptedLanded counters (most of this suite) never asked
    // for a picture, and the twelve-odd extra ticks of a live world this loop
    // runs are twelve-odd ticks a counter-only test has no reason to absorb.
    // "the default behavior for screenshot captures" is what was asked for,
    // not "sixteen more ticks of every scripted line in this project" --
    // those are the same sixteen ticks that were always safe for the frame
    // this exists to fix and never free for a hash or a count nobody asked to
    // move.
    // PLANNING SPRINT (item #1). SKIPPED WHEN `refocus` ALREADY SPENT ITS OWN
    // TWO BUDGETS, above -- running this too would spend a THIRD, unasked-for
    // round of settle steps past the deliberately mid-crossfade point a
    // `refocusSteps` capture asked to stop at, silently finishing the swap
    // the caller wanted photographed partway through.
    if (!config.screenshot.empty() && config.refocus.empty()) {
        // UI-EA-SPEC sec. 0 (FLOW lane): THE DEFAULT SHUTTER IS THE AT-REST
        // FRAME. kCaptureRestSteps (~4s of zero-input steps, anim.hpp) is
        // past every disclosure countdown -- the tutor bands' page-open
        // raise has held its kTutorHoldSteps and fully eased down -- and
        // short of the kIdleWakeSteps re-raise, so what a default
        // screenshot shows is the state the word budgets bind. It was 16:
        // enough for the panel's own rise, nothing else. --settle-steps=N
        // still overrides exactly (0 photographs the raised state;
        // --no-settle still means the opening bump).
        const int steps = config.settleSteps >= 0 ? config.settleSteps
                                                  : (config.settle ? kCaptureRestSteps : 0);
        const sim::MoveInput still{};
        for (int i = 0; i < steps; ++i) {
            session.step(still);
        }
    }

    Framebuffer frame(config.session.width, config.session.height);
    session.drawFrame(frame);
    // PLANNING SPRINT (item #1). THE NUMBERS BESIDE THE PICTURE -- see
    // SmokeRunResult::characterFocusAtCapture's own header. Read AFTER every
    // step() above has already run, so this is the exact value the frame
    // just drawn was a picture of.
    result.characterFocusAtCapture = session.characterFocusValue();
    result.pullLineAtCapture = session.pullLineNow();
    result.mapFocusAtCapture = session.mapFocusValue();
    result.lettersFocusAtCapture = session.lettersFocusValue();
    result.journalFocusAtCapture = session.journalFocusValue();
    result.lampCount = session.lampCount();
    result.endTileX = session.body().tileX();
    result.endTileY = session.body().tileY();
    result.endBand = session.body().band();
    result.gullPresent = session.tavern().presentCount();
    {
        // How much of the ward is about: the roll, and how many of it stand
        // within twelve tiles of the camera whether or not the camera is
        // pointed at them. How many the frame DREW is the 3D shutter's own
        // line (`bodies`), counted where they are drawn.
        const sim::WardCensus roll = session.people().census();
        result.wardRoll = roll.alive;
        result.roofHomed = roll.roofHomed;
        result.onRoofNow = roll.onRoofNow;
        result.prey = roll.prey;
        result.preyUp = roll.preyUp;
        result.catches = session.people().catches();
        const Camera view = session.camera();
        for (const sim::WardActor& actor : session.people().actors()) {
            if (!actor.visible()) {
                continue;
            }
            const float dx = static_cast<float>(actor.x) + 0.5F - view.x;
            const float dy = static_cast<float>(actor.y) + 0.5F - view.y;
            if (dx * dx + dy * dy <= 144.0F) {
                ++result.wardNear;
            }
        }
    }
    result.craftLevel = session.tavern().dialogue().skills().level(sim::kThieverySkill);
    result.pinsSet = session.picking() ? session.lockpicking().pinsSet() : 0;

    const int hour = session.timeOfDay() / 3600;
    const int minute = (session.timeOfDay() / 60) % 60;
    // WEATHER: what sky the frame was shot under, beside the hour it was shot
    // at, so a picture says what it shows. `weather=fog 0.83` is a fog most
    // of the way in; `weather=clear 0.00` is the sky every earlier frame had.
    // Two decimals, by integer arithmetic, so the line needs no <iomanip>.
    const Weather shotUnder = session.weather();
    const int hundredths = static_cast<int>(shotUnder.intensity * 100.0F + 0.5F);
    std::ostringstream summary;
    summary << "steps=" << config.steps << " at (" << result.endTileX << ',' << result.endTileY
            << ",z" << result.endBand << ") facing " << sim::compass_point(session.body().yaw())
            << " | " << (hour < 10 ? "0" : "") << hour << ':' << (minute < 10 ? "0" : "") << minute
            << ' ' << session.placeLabel() << " weather=" << weatherKindName(shotUnder.kind) << ' '
            << hundredths / 100 << '.' << (hundredths % 100 < 10 ? "0" : "") << hundredths % 100
            << " | lamps=" << result.lampCount
            // #79: `gull=`, and it used to say `actors=`. Printed one space from
            // `ward=661` it read as "one of the six hundred is on screen", which
            // it never was: it is how many of the Gilded Gull's own seventeen
            // are inside K03, a different population in a different building,
            // and it has no relationship to the frame at all. The HUD has always
            // called it "THE GULL n IN"; so does this now.
            << " gull=" << result.gullPresent
            << " art=" << (session.atlas().fromAuthoredArt() ? "custom" : "procedural")
            // #78: THE WARD'S OWN ROLL, PRINTED BESIDE THE TAPROOM'S. `ward` is
            // how many bodies the district holds and `near` is how many stand
            // within twelve tiles of the camera. The two differing is the
            // normal case and is exactly what the owner ruled OUT as a gate: a
            // cellar or a back lane at four in the morning is legitimately
            // empty, so this is EVIDENCE for a person to read and never a
            // threshold a build fails on.
            << " ward=" << result.wardRoll
            << " near=" << result.wardNear
            // #80. THE TWO THINGS THIS ROUND ADDED, ON THE FRAME'S OWN LINE.
            // `roof` is beds-on-a-deck / bodies-standing-on-one-right-now, so a
            // capture at midnight and a capture at noon can be told apart by
            // reading it. `mice` is prey on the board out of prey on the roll,
            // and `ate` is how many the ward's cats and strays have caught
            // since the roster was baked -- which is the difference between a
            // dock district with a food chain and one with vermin nobody eats.
            << " roof=" << result.roofHomed << '/' << result.onRoofNow
            << " mice=" << result.preyUp << '/' << result.prey
            << " ate=" << result.catches
            << " figures=" << (session.actorSheet().fromAuthoredArt() ? "sheet" : "procedural");
    if (config.street) {
        // WHO ANSWERED AND OUT OF WHICH TABLE, printed beside the frame. A
        // capture that says only "a conversation is open" cannot tell a
        // dockhand from a watchman, which is the entire claim being made.
        summary << " | street who=" << config.streetWho
                << " speaker=\"" << result.streetSpeaker << "\""
                << " key=" << (result.streetKey.empty() ? "-" : result.streetKey)
                << " talking=" << (result.talking ? "yes" : "no");
    }
    if (config.petition) {
        // TIME-AND-TENURE BUILD: which ground, who read the roll, and whether
        // the ROLL says the charge moved -- the claim is a changed register,
        // so the register's own answer is what gets printed.
        const PetitionLineResult& pet = result.petitionResult;
        summary << " | petition plot=" << (pet.plotId.empty() ? "-" : pet.plotId)
                << " speaker=\"" << pet.speaker << "\""
                << " opened=" << (pet.opened ? "yes" : "no")
                << " roll=\"" << pet.rollLine << "\""
                << " answer=\"" << pet.petitionLine << "\""
                << " duke=" << (pet.becameDuke ? "yes" : "no");
    }
    if (!config.tone.empty()) {
        summary << " | tone=" << sim::toneName(session.tavern().dialogue().tone());
    }
    if (config.shop) {
        const ShopLineResult& shop = result.shopResult;
        summary << " | shop chandler=" << (shop.chandler ? "yes" : "no")
                << " wares=" << (shop.wares ? "yes" : "no") << " price=" << shop.price
                << " bought=" << (shop.bought ? "yes" : "no")
                << " pawnbroker=" << (shop.pawnbroker ? "yes" : "no")
                << " listed=" << (shop.pawnList ? "yes" : "no") << " offer=" << shop.offer
                << " sold=" << (shop.sold ? "yes" : "no") << " coin=" << shop.coinStart << "->"
                << shop.coinEnd << " granted=" << shop.granted << " bought_line=\""
                << shop.boughtLine << "\" sold_line=\"" << shop.soldLine << "\"";
    }
    if (config.radiant) {
        // RADIANT BUILD: which errand, whose word it is, and whether the
        // BOARD says it moved -- the claim is a reachable generator, so the
        // board's own answer is what gets printed.
        const RadiantLineResult& job = result.radiantResult;
        summary << " | radiant id=" << job.objectiveId
                << " giver=\"" << job.giver << "\""
                << " opened=" << (job.opened ? "yes" : "no")
                << " offered=" << (job.offered ? "yes" : "no")
                << " taken=" << (job.taken ? "yes" : "no")
                << " journal=" << (job.journal ? "yes" : "no");
        if (config.radiantEnd == "shelf" || config.radiantEnd == "settled") {
            summary << " shelf=" << (job.shelf ? "yes" : "no");
        }
        if (config.radiantEnd == "settled") {
            summary << " target=\"" << job.target << "\""
                    << " reached=" << (job.reached ? "yes" : "no")
                    << " walked=" << (job.walked ? "yes" : "no")
                    << " passed=" << (job.passed ? "yes" : "no")
                    << " settled=" << (job.settled ? "yes" : "no") << " pay=" << job.pay;
        }
        if (config.radiantEnd == "ask") {
            summary << " asked=" << (job.asked ? "yes" : "no") << " answer=\"" << job.answer
                    << "\"";
        }
        summary << " brief=\"" << job.brief << "\"";
    }
    if (!config.threshold.empty()) {
        // DISTRICT PHASE D: WHICH BOUNDARY, WHICH WAY, AND WHETHER THE PLATE
        // WAS ACTUALLY UP. A screenshot of a plate that never fired is
        // indistinguishable from a screenshot of a street, so the one thing a
        // PNG cannot report about itself is printed beside it.
        const ThresholdLineResult& cross = result.thresholdResult;
        summary << " | threshold " << config.threshold
                << " from=\"" << (cross.from.empty() ? "-" : cross.from) << "\""
                << " to=\"" << (cross.to.empty() ? "-" : cross.to) << "\""
                << " crossed=" << (cross.crossed ? "yes" : "no")
                << " plate=\"" << cross.plate << "\""
                << " up=" << (cross.announced ? "yes" : "no")
                << " end=" << config.thresholdEnd;
    }
    if (config.wait) {
        summary << " | wait open=" << (session.waitOpen() ? "yes" : "no")
                << " rows=" << session.waitRows().size()
                << " refusal=\"" << session.waitRefusal() << "\"";
    }
    if (!config.travelTo.empty()) {
        // TRAVEL lane. The whole claim on one line, byte-comparable across
        // two runs: the plan (route steps, octile units, honest seconds, the
        // minutes actually charged), the clock either side of the press, the
        // landing, the plate, and any refusal in its exact words.
        const TravelLineResult& probe = result.travelResult;
        summary << " | travel to=\"" << config.travelTo << "\""
                << " found=" << (probe.found ? "yes" : "no")
                << " route=" << probe.plan.routeSteps
                << " units=" << probe.plan.units
                << " walk=" << probe.plan.seconds << "s"
                << " charged=" << probe.plan.minutes << "min"
                << " clock=" << probe.clockFrom << "->" << probe.clockTo
                << " body=(" << probe.endX << "," << probe.endY << "," << probe.endBand
                << ")"
                << " moved=" << (probe.moved ? "yes" : "no")
                << " plate=\"" << probe.plate << "\""
                << " up=" << (probe.plateUp ? "yes" : "no")
                << " refusal=\"" << probe.plan.refusal << "\"";
    }
    if (config.trail) {
        const sim::Casebook& notes = session.casebook();
        summary << " | trail read=" << result.trailRead << '/' << result.trailWalked
                << " leads=" << notes.known().size()
                << " cold=" << notes.coldCount()
                << " unreached=" << gTrailUnreached
                << " dread=" << notes.dread()
                << " closed=" << (notes.closed() ? "yes" : "no")
                << " flame=" << session.legend().row(sim::LegendTrack::Flame).rung
                << " called=" << session.legend().title()
                << " notes=" << (session.casebookOpen() ? "open" : "shut");
    }
    if (config.burgle) {
        summary << " | burgle beats=" << result.burgleBeats << '/' << kBurgleBeats
                << " mask=" << gBurgleBeatMask
                << " lift=" << (session.tavern().dialogue().crimes().tally(sim::Crime::Lift) > 0
                                    ? "tried"
                                    : "none")
                << " light=" << session.tavern().lightOnPlayer()
                << " noise=" << session.tavern().playerNoise()
                // WHETHER THE STEALTH BEAT PROVED ANYTHING, printed beside it.
                // `watchers=` is how many awake, upright bodies were in range
                // when beat 2 was judged; a `hidden` with `watchers=0` beside it
                // is a fact about the hour and not about being unseen, and the
                // S9 review had to read the source to work that out. Now it is
                // one word away from the claim.
                << " watchers=" << gBurgleWatchers
                << " " << (session.hidden() ? "hidden" : "seen") << " "
                << " picking=" << (session.picking() ? "yes" : "no")
                << " pins=" << (session.picking() ? session.lockpicking().pinsSet() : 0)
                << " nextprobes=" << gLockEndingProbes
                << " picks=" << session.picks()
                << " locks open=" << session.tavern().openedLocks()
                << " jammed=" << session.tavern().jammedLocks()
                << " forced=" << session.tavern().forcedLocks()
                << " cracked=" << session.tavern().crackedBoxes()
                << " cracksmanship="
                << session.tavern().dialogue().skills().level(sim::kThieverySkill)
                << " skyrunning="
                << session.tavern().dialogue().skills().level(sim::kRoofSkill);
    }
    if (config.caseRun) {
        // COURIER CASE. The errand's own summary: beats and the mask (which
        // one dropped, not only how many), the live book's read/known, whether
        // the tenant is down and whether he is in hand, and where the case
        // stands -- so a short run says exactly what it proved.
        const sim::Casebook& book = session.sheetBook();
        summary << " | case beats=" << result.caseBeats << '/' << kCaseBeats
                << " mask=" << gCaseBeatMask
                << " read=" << book.readCount() << '/' << book.known().size()
                << " dread=" << book.dread()
                << " tenant=" << (session.tenantDown() ? "down" : "up")
                << " carry=" << (session.sheetCarry() ? "yes" : "no")
                << " closed=" << (book.closed() ? "yes" : "no")
                << " live=" << (session.sheetCaseLive() ? "yes" : "no")
                << " letters=" << session.unlockedLetters().size();
    }
    if (config.evictionRun) {
        // EVICTION CASE. The errand's own summary, the courier segment's
        // twin: beats and the mask, the live book's read/known, whether the
        // door was knocked and the writ served, whether the book closed, and
        // -- the fact both paths are judged on -- what is in the player's
        // hands. `served=no weapon=fists closed=yes` is the disrupt path
        // proved; `served=yes weapon=armed closed=yes` is the participate
        // path proved.
        const sim::Casebook& book = session.evictBook();
        summary << " | evict beats=" << result.evictBeats << '/' << kEvictBeats
                << " mask=" << gEvictBeatMask
                << " read=" << book.readCount() << '/' << book.known().size()
                << " dread=" << book.dread()
                << " knocked=" << (session.evictEverKnocked() ? "yes" : "no")
                << " served=" << (session.writServed() ? "yes" : "no")
                << " closed=" << (book.closed() ? "yes" : "no")
                << " live=" << (session.evictCaseLive() ? "yes" : "no")
                << " weapon="
                << (session.tavern().playerWeapon() == sim::Weapon::Fists ? "fists" : "armed")
                << " letters=" << session.unlockedLetters().size();
    }
    if (config.watchHalt) {
        summary << " | watch-halt beats=" << result.watchHaltBeats << '/' << kWatchHaltBeats
                << " stance="
                << (session.tavern().watchStance() == sim::Tavern::WatchStance::Closing
                        ? "closing"
                        : "idle")
                << " cause=" << sim::watchCauseName(session.tavern().watchInterest())
                << " arrest="
                << (session.tavern().lastArrest().happened
                        ? sim::watchCauseName(session.tavern().lastArrest().cause)
                        : "no")
                << gWatchHaltNote << " row=\"" << session.lastMessage() << '"';
    }
    if (config.voice) {
        // WHO SPOKE, OUT OF WHICH KEY, AND THE ROW ITSELF, beside the frame:
        // a capture that says only "a line is up" cannot tell a goodbye from
        // a witness, which is the whole claim being made. The reaction row
        // is printed too, for the `moved` ending.
        summary << " | voice end=" << config.voiceEnd << " beats=" << result.voiceBeats << '/'
                << voiceBeatsFor(config.voiceEnd)
                << " up=" << (session.voice().up() ? "yes" : "no")
                << " channel=" << (result.voiceChannel.empty() ? "-" : result.voiceChannel)
                << " key=" << (result.voiceKey.empty() ? "-" : result.voiceKey)
                << " speaker=\"" << result.voiceSpeaker << "\""
                << " row=\"" << result.voiceRow << "\""
                << " spoken=" << session.voice().linesSpoken()
                << " inview=" << (result.voiceSpeakerTenths < 0 ? "-" : (result.voiceSpeakerInView ? "yes" : "no"))
                << " off=" << (result.voiceSpeakerOffDeg < 0 ? std::string("-")
                                                             : std::to_string(result.voiceSpeakerOffDeg) + "deg")
                << " apart=" << (result.voiceSpeakerTenths < 0 ? std::string("-")
                                                              : std::to_string(result.voiceSpeakerTenths / 10) + "." +
                                                                    std::to_string(result.voiceSpeakerTenths % 10))
                << gWatchHaltNote
                << " alert=\"" << session.alertSpeakerNow() << "\""
                << " reaction=\"" << session.reactionLine() << "\""
                << " talking=" << (session.talking() ? "yes" : "no");
    }
    if (config.kit) {
        summary << " | kit beats=" << result.kitBeats << '/' << kitBeatsFor(config.kitEnd)
                << gKitNote << " hand=" << sim::weaponName(session.tavern().playerWeapon())
                << " dr=" << session.tavern().wornDr()
                << " ground=" << session.tavern().groundItems().size();
    }
    if (config.pack) {
        summary << " | pack beats=" << result.packBeats << '/' << packBeatsFor(config.packEnd)
                << gPackNote << " load=" << session.tavern().loadDrams() << '/'
                << session.tavern().loadBudget() << " hp=" << session.tavern().playerHp()
                << " ground=" << session.tavern().groundItems().size();
    }
    if (config.court) {
        summary << " | court beats=" << result.courtBeats << '/' << courtBeatsFor(config.courtEnd)
                << " page=" << (session.courtOpen() ? "up" : "down")
                << " plate=" << (session.takenPlateUp() ? "up" : (session.takenBeatUp() ? "beat" : "down"))
                << " hearing=" << (session.tavern().hearingPending() ? "pending" : "none")
                << " executed=" << (session.tavern().executed() ? "yes" : "no")
                << " rows=" << (session.ropeRowsUp() ? "up" : "down")
                << " armed=" << (session.courtPleaArmed() ? "yes" : (session.ropeRowArmed() >= 0 ? "end-row" : "no"))
                << gCourtNote << " row=\""
                << session.lastMessage() << '"';
    }
    if (config.nemesis) {
        const sim::Nemesis* worst = session.tavern().nemesis().worst();
        summary << " | nemesis beats=" << result.nemesisBeats << '/'
                << (config.nemesisEnd == "death" ? kNemesisDeathBeats : kNemesisBeats)
                << " mask=" << gNemesisBeatMask;
        // WHO IS ON THE FRAME, named, so a capture cannot quietly photograph
        // the wrong docker. The first shipped attempt at the `talk` ending did
        // exactly that -- his stool and Wick Hempson's are one tile apart.
        if (session.talking()) {
            summary << " talking to " << session.tavern().dialogue().speaker().name;
        }
        if (worst != nullptr) {
            summary << " " << worst->who << " x" << worst->wins;
            if (!worst->title.empty()) {
                summary << " " << worst->title;
            }
            const sim::ChapterRaw* house =
                session.tavern().nemesis().chapters().at(worst->chapter);
            if (house != nullptr) {
                summary << " of " << house->displayName << " (" << worst->members.size()
                        << " members, toll "
                        << session.tavern().nemesis().tollPercent(worst->faction) << "%)";
            }
            if (worst->holdsGround()) {
                summary << " holds "
                        << session.ward()
                               .raws()
                               .plots()[static_cast<std::size_t>(
                                   session.ward().plots()[static_cast<std::size_t>(worst->plot)]
                                       .raw)]
                               .name;
            }
        }
    }
    if (config.roofs || config.skyrun) {
        const sim::DialogueDirector& talk = session.tavern().dialogue();
        const std::int32_t roofs = talk.factions().indexOf("skyrunners");
        const std::int32_t watch = talk.factions().indexOf("watch");
        summary << " | roofs stages=" << result.skyrunStages << " rank="
                << talk.standings().rank(roofs) << ' ' << talk.standings().rankTitle(roofs)
                << " standing=" << talk.standings().standing(roofs)
                << " watch=" << talk.standings().standing(watch)
                << " climbs=" << talk.crimes().tally(sim::Crime::RoofRun)
                << " lifts=" << talk.crimes().tally(sim::Crime::Lift)
                << " cracks=" << talk.crimes().tally(sim::Crime::Burgle)
                << " leans=" << talk.crimes().tally(sim::Crime::Extort)
                << " fences=" << talk.crimes().tally(sim::Crime::Fence)
                << " runs=" << talk.crimes().tally(sim::Crime::Smuggle)
                << " heat=" << talk.crimes().heat()
                << " warrant=" << (talk.crimes().warrant() ? "yes" : "no")
                << " skyrunning=" << talk.skills().level(sim::kRoofSkill)
                // S6: and whether the ward took him for any of it. A scripted
                // line that fell short because a watchman crossed the room is a
                // very different failure from one that fell short because a
                // beat is broken, and the summary has to be able to tell them
                // apart.
                << " arrests=" << talk.crimes().arrests() << " sentence="
                << sim::sentenceName(talk.crimes().lastSentence());
    }
    if (config.contract) {
        const sim::DialogueDirector& talk = session.tavern().dialogue();
        const sim::Stash& sack = talk.crimes().stash();
        summary << " | work beats=" << result.contractBeats << '/' << kContractBeats
                << " day=" << talk.contracts().day()
                << " open=" << talk.contracts().contracts().size()
                << " taken=" << talk.contracts().takenCount()
                << " paid=" << talk.contracts().paidCount()
                << " lost=" << talk.contracts().failedCount()
                << " earned=" << talk.contracts().coinEarned()
                << " scalps=" << sack.count(sim::Contraband::Scalp)
                << " load=" << sack.illicitWeight() << "dr"
                << " heat=" << talk.crimes().heat();
    }
    if (config.flame) {
        const sim::DialogueDirector& talk = session.tavern().dialogue();
        const std::int32_t temple = talk.factions().indexOf("temple");
        summary << " | flame stages=" << result.flameStages << '/'
                << (talk.quests().find("flame-disciple") == nullptr
                        ? 0
                        : static_cast<int>(talk.quests().find("flame-disciple")->stages.size()))
                << " rank=" << talk.standings().rank(temple) << ' '
                << talk.standings().rankTitle(temple)
                << " standing=" << talk.standings().standing(temple)
                << " influence=" << talk.standings().influence(temple)
                << " known=" << talk.grimoire().size() << " forged="
                << talk.grimoire().craftedCount()
                << " linkcraft=" << talk.skills().level(sim::kCraftingSkill);
    }
    if (!config.rung.empty()) {
        // THE RUNG PLATE. The track, the rung either side of the line, and
        // THE WORDS ON THE PLATE -- a frame of a plate is a frame of words,
        // and the words are the claim: the head off legend.cpp's own tables,
        // the row out of legend_barks.json, whether the plate is genuinely
        // up at the shutter, and whether the word named a track at all.
        const RungLineResult& rung = result.rungResult;
        summary << " | rung=" << config.rung << ' ' << rung.from << "->" << rung.to
                << " row=\"" << rung.row << '"'
                << " head=\"" << rung.head << '"'
                << " plate=" << (rung.plateUp ? "up" : "down")
                << " found=" << (rung.found ? "yes" : "no")
                << " called=" << session.legend().title() << " aim=\"" << rung.aim << '"'
                << " rose=" << (rung.rose ? "yes" : "no");
        if (!rung.reason.empty()) {
            summary << " reason=\"" << rung.reason << '"';
        }
    }
    // A SCRIPTED RUN THAT FELL SHORT SAYS SO, AND FAILS.
    //
    // S4's did neither: runFlameLine returned a stage count that runSmoke threw
    // away, and a run that landed ZERO of six stages photographed the wrong
    // person and exited 0. The S4 review found it. A capture tool that reports
    // success while photographing the wrong thing will mislabel a future
    // sprint's evidence, so this is a hard failure and not a warning.
    // THE PULL PACK: what the compass read at the shutter, so a run's own
    // summary says which door the street was pointing at.
    if (!result.pullLineAtCapture.empty()) {
        summary << " | compass \"" << result.pullLineAtCapture << "\"";
    }
    if (result.scriptFellShort()) {
        summary << " | WARNING: scripted run landed " << result.scriptedLanded << " of "
                << result.scriptedWanted << " beats";
    }
    result.summary = summary.str();

    // UI-EA (LANE HUD): THE CORNER STAMP IS GONE, from captures too. The
    // spec's row table deletes "GRANADAD 0.10.0" from every frame -- the
    // version rides the pause/keys title rule instead (dialogueView already
    // prints it in the CONTROLS epithet, and the pause title carries it under
    // the PAGES lane), which is where a player actually goes to ask what
    // build this is. The census transcribes captures, so the two words had to
    // leave the capture path as well as the HUD; provenance lives in the PNG
    // filenames, the ship notes and the F1 page, not in the sky. config.stamp
    // is kept as an accepted no-op so every existing caller and capture
    // script still parses.
    (void)config.stamp;

    result.ok = !result.scriptFellShort();
    if (!config.screenshot.empty()) {
        // The PNG is still written. A frame of a run that fell short is
        // evidence OF the shortfall, and deleting it would make the failure
        // harder to diagnose rather than easier -- but ok stays false.
        if (config.shutter) {
            // 3D BUILD: the client's shutter composites this frame through
            // the raylib backend and writes what the window shows.
            result.ok = config.shutter(session, frame) && result.ok;
        } else {
            const Framebuffer output =
                config.captureScale > 1 ? upscaleNearest(frame, config.captureScale) : frame;
            result.ok = writePng(output, config.screenshot) && result.ok;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// CASE WATCH -- the drive, recorded
// ---------------------------------------------------------------------------

WatchFingerprint watchFingerprintOf(const Session& session) {
    WatchFingerprint print;
    print.timeOfDay = session.timeOfDay();
    print.elapsedSeconds = session.elapsedSeconds();
    print.tileX = session.body().tileX();
    print.tileY = session.body().tileY();
    print.band = session.body().band();
    print.yaw = session.body().yaw();
    const sim::Casebook& book = session.sheetBook();
    print.read = book.readCount();
    print.known = static_cast<std::int32_t>(book.known().size());
    print.dread = book.dread();
    print.letters = static_cast<std::int32_t>(session.unlockedLetters().size());
    print.closed = book.closed();
    print.live = session.sheetCaseLive();
    print.carry = session.sheetCarry();
    print.tenantDown = session.tenantDown();
    return print;
}

CaseWatchDrive recordCaseDrive(const SmokeRunConfig& config) {
    CaseWatchDrive drive;
    // THE SAME SESSION runSmoke BUILDS for --case: same config, and the same
    // scripted hour unless the caller named one -- a tape recorded against a
    // different ward would be a tape of a different errand.
    SessionConfig started = config.session;
    if (!started.timeOfDayGiven) {
        const int hour = scriptedStartHour(config);
        if (hour >= 0) {
            started.timeOfDay = hour * 3600;
        }
    }
    Session session(started);
    session.setWatchRecorder(&drive.ops);
    drive.beats = static_cast<std::int32_t>(runCaseLine(session, config.caseEnd));
    session.setWatchRecorder(nullptr);
    drive.mask = gCaseBeatMask;
    // A short ending owes only the beats up to its shutter -- runSmoke's own
    // rule, restated here so the watch's end line says X/owed and not X/8.
    drive.beatsWanted = config.caseEnd == "sheet"   ? 2
                        : config.caseEnd == "gull"  ? 3
                        : config.caseEnd == "night" ? 5
                        : config.caseEnd == "down"  ? 6
                                                    : kCaseBeats;
    for (const WatchOp& op : drive.ops) {
        if (op.kind == WatchOpKind::Step) {
            ++drive.stepCount;
        }
    }
    drive.end = watchFingerprintOf(session);
    return drive;
}

}  // namespace granadad::render
