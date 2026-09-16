#pragma once

// VOICE LANE: THE ONE VOICE CHANNEL.
//
// Eli, 2026-09-10: "I'd expect the watch and crowd to react appropriately to
// the violence they're witnessing." And Oblivion's feel of talking to a
// person: people speak unprompted, say goodbye, turn to you, react in words.
// Until this lane nobody in the Docks said a word unless E was pressed --
// zero pass, farewell, overheard or react tables across ten bark files, LEAVE
// answering with an empty line -- and the only unprompted rows were the
// combat build's halt, join and flee.
//
// WHAT THIS IS. A subtitle EMITTER on the render side, over events the
// simulation already emits. It reads the room (the social ledger's memories,
// who is present and where, the Watch's stance and cause, the crime ledger's
// heat and paper) and the ward (who is standing within reach of whom), picks
// ONE authored row through BarkTables::line, and hands Session a name and a
// line for the HUD's voice row. Five channels, ranked, the top first:
//
//     Watch      a watchman on you: his eyes landing on you crouched
//                (watch.notice.sneak) or with your hands up
//                (watch.notice.hands), passing a man the post has heard of
//                (watch.heat) or has paper on (watch.paper), the deference
//                table to a presented Wielder (watch.wielder), and ONE
//                readback as a Closing watchman crosses the room, after his
//                halt or in its place and never beside it
//                (watch.approach.<cause>, the violence cause split by what
//                is actually up: .steel with a blade, the bare key with fists)
//     React      a WITNESS to a landed blow, a drawn blade, a killing --
//                the bodies the sim's own three-clause witness rule put the
//                deed on (SocialLedger::witness, watched here by comparison
//                of public state, the lastPlayerHp_ shape), never the man in
//                the fight, never a bouncer (his ladder speaks) and never the
//                Closing watchman (his halt does). A row may carry <victim>
//                and it is filled with the name the sim recorded
//     Farewell   the goodbye: the LEAVE topic's own reply, the back key, and
//                the word thrown after you when you walk off mid-talk
//     Pass       the one line as a body passes within reach (Oblivion's
//                "one line as you pass"), pass.<type>.<attitude> on the
//                street and pass.room.<family>.<attitude> from a stool --
//                nothing about crane lines or wet planks is said in the Gull
//     Overheard  one side of an exchange between two bodies STANDING
//                together, overheard.<family>, so the street talks to itself
//
// WHAT IT IS NOT. It moves nothing. No sim state is written, no draw is made,
// no baseline can see it: the selection is a pure function of (actor id,
// step, talks) and the cooldowns below are render-side integers the hasher
// never reads -- exactly the standing lastJoinSpoken_ / lastFleeSpoken_ shape
// the barks lane already keeps in Session. A twin run with this channel on
// and one with it off hash byte-identical, and test_street_voice.cpp pins it.
//
// THE STREET IS NEVER A WALL OF TEXT, AND NEVER THE SAME LINE TWICE. One line
// at a time; a step-count cooldown per channel and per speaker; an event
// line (react, watch, farewell) replaces an ambient one (pass, overheard) and
// an ambient line waits for an event line to finish and then a breath more.
// A body says one unprompted thing to you in two minutes, the room says one
// thing about a fight every four seconds, and a killing is not talked over by
// the weather. And the channel REMEMBERS WHAT WAS SAID: no row is said again
// by any mouth inside kRowMemorySteps (two minutes) -- a cell that is spent
// falls to the next rung of its chain, and a chain that is spent stays
// quiet rather than repeat.
//
// THE HONEST BOUND. The player's swing reaches the Gilded Gull's roster
// today and nothing else (COMBAT-ACTION-SPEC.md section 10 defers WardActor
// combat sheets), so react.* is PROVEN on the tavern's witnesses; the street
// half rides the same tables and the same emitter the day a street body can
// be hit, which is the STREET SENSES lane's declared population move and not
// this one's.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/sim/barks.hpp"
#include "granadad/sim/social.hpp"

namespace granadad::sim {
class Tavern;
class TileQuery;
class WardPopulation;
}  // namespace granadad::sim

namespace granadad::render {

/// Which channel a line came out of. ORDERED BY RANK: a line from a higher
/// channel replaces a lower one on the row; a lower one waits its turn.
enum class VoiceChannel : std::uint8_t {
    Overheard = 0,
    Pass = 1,
    Farewell = 2,
    React = 3,
    /// The Watch outranks the crowd: a witness's word about the fight waits
    /// for the watchman's word about you, never the other way round.
    Watch = 4,
};

[[nodiscard]] std::string_view voiceChannelName(VoiceChannel channel) noexcept;

/// One spoken line, as the HUD draws it and as the summary reports it.
struct VoiceLine {
    /// "Gerta Saltcotte" -- the HUD draws the colon.
    std::string name;
    /// The authored row, verbatim, <victim> filled.
    std::string text;
    /// The bark key the row came out of.
    std::string key;
    /// The speaker's ledger id: the Gull's 1..17, or kWardSpeakerIdBase + id.
    std::int32_t actorId = -1;
    VoiceChannel channel = VoiceChannel::Pass;
    /// The step it was said on.
    std::int32_t step = 0;
    [[nodiscard]] bool empty() const noexcept { return text.empty(); }
};

// ---------------------------------------------------------------------------
// the numbers, every one with its reason
// ---------------------------------------------------------------------------

/// Movement steps a line stays up, at least. A short row holds this long.
inline constexpr std::int32_t kVoiceHoldMinSteps = 240;
/// And the longest any row holds. Reading speed, not decoration: a
/// seventy-glyph row wants about eight seconds and a four-glyph one four.
inline constexpr std::int32_t kVoiceHoldMaxSteps = 540;
/// Steps of hold per glyph, added to the minimum.
inline constexpr std::int32_t kVoiceHoldPerGlyphSteps = 4;

/// THE ROW MEMORY. No row is said again, by any mouth, inside two minutes.
/// The critic's live count on a two-minute walk was thirteen lines and nine
/// distinct; the fix is a memory, not a wider table. Thirty-two rows
/// remembered is more than two minutes can hold at the channels' cadence.
inline constexpr std::int32_t kRowMemorySteps = 7200;
inline constexpr std::size_t kRowMemoryRows = 32;

/// PASS. Steps between two pass lines on the channel (twelve seconds), and
/// steps before the SAME body says another unprompted thing to you (two
/// minutes). Walking a crowded Tarwalk gets a line every dozen seconds from
/// a different mouth each time; standing beside one shopkeeper gets one
/// line, and standing in a crowd gets each neighbour once.
inline constexpr std::int32_t kPassCooldownSteps = 720;
inline constexpr std::int32_t kPassSpeakerCooldownSteps = 7200;
/// A body has to be THIS close to speak as it passes: the talk reach, so the
/// one who speaks to you is one you could answer.
inline constexpr std::int32_t kPassReachTiles = 2;

/// OVERHEARD. Twenty-four seconds between snatches, a minute and a half per
/// mouth, the pair STANDING together (Chebyshev 1, and neither moved this
/// tick -- three walkers in file are not a conversation) within earshot of
/// you and in a sight line.
inline constexpr std::int32_t kOverheardCooldownSteps = 1440;
inline constexpr std::int32_t kOverheardSpeakerCooldownSteps = 5400;
inline constexpr std::int32_t kOverheardEarshotTiles = 6;

/// REACT. Four seconds between witnesses' lines -- enough to read one -- and
/// fifteen before the same witness says another thing about the same fight.
/// A witness who could not speak when the blow landed keeps the right to for
/// kReactQueueSteps and then it is old news.
inline constexpr std::int32_t kReactCooldownSteps = 240;
inline constexpr std::int32_t kReactSpeakerCooldownSteps = 900;
inline constexpr std::int32_t kReactQueueSteps = 240;

/// WATCH. Five seconds between the Watch's lines and twenty before the same
/// watchman remarks on you again. The readback of a Closing watchman is ONE
/// line: it waits for his halt to leave the alert row and follows as he
/// crosses, or replaces the halt once it has had its second and he is at
/// reach; it is never stacked beside the halt in his name.
inline constexpr std::int32_t kWatchCooldownSteps = 300;
inline constexpr std::int32_t kWatchSpeakerCooldownSteps = 1200;
/// Manhattan tiles inside which a Closing watchman is "at reach" for the
/// readback: the arrest's own reach is a tile, this is one more so the line
/// lands before his hand does.
inline constexpr std::int32_t kWatchApproachNearTiles = 2;
/// Steps the halt must have had on the alert row before the readback may
/// replace it: a second, enough to register that he shouted.
inline constexpr std::int32_t kHaltReadSteps = 60;
/// Heat at which a passing watchman has HEARD of you (watch.heat) though no
/// paper is out. Under kWarrantAt (60) and above the lapse line (20), so a
/// man the paper lapsed on is still talked about.
inline constexpr std::int32_t kWatchHeatNoticedAt = 24;

/// After an event line (react, watch, farewell) the ambient channels wait
/// this long past the line's own hold before speaking again -- the street
/// does not remark on the weather over a killing.
inline constexpr std::int32_t kAmbientAfterEventSteps = 480;

/// How many speakers' last-spoken steps are remembered. Sixty-four covers a
/// full taproom and a crowded quay twice over; the oldest entry is
/// overwritten past it, which only shortens one cooldown.
inline constexpr std::size_t kVoiceSpeakerMemory = 64;

// ---------------------------------------------------------------------------
// the selection, as pure functions
// ---------------------------------------------------------------------------

/// Which row of a table this speaker says at this step, having been talked to
/// `talks` times: an integer MIX of the three (Fibonacci hashing, the
/// wardTradeLevel shape), so the ten-second bucket of the step turns the row
/// on every table width -- a multiplier of five over a five-row table left
/// the bucket dead and a body saying one row forever, which the critic heard.
/// NO DRAW, and nothing hashed goes in: the twin gate cannot see it and does
/// not need to. `skip` walks on from the picked row, for a caller avoiding
/// rows said lately.
[[nodiscard]] std::int32_t voiceRowIndex(std::int32_t actorId, std::int32_t step,
                                         std::int32_t talks, std::int32_t skip = 0) noexcept;

/// The first authored key of `chain` and its row for this speaker, or an
/// empty line. Pure: two calls with equal inputs answer the same string.
[[nodiscard]] VoiceLine pickVoiceLine(const sim::BarkTables& barks,
                                      const std::vector<std::string>& chain, std::int32_t actorId,
                                      std::int32_t step, std::int32_t talks);

/// What a stranger thinks of you is what the ward thinks of you. A body the
/// ledger has met answers with its own memory; one it has not answers with
/// the ward's reputation, so a man the district wants gone is passed coldly
/// by people who have never spoken to him -- Oblivion's disposition read off
/// fame and infamy, in this build's own integer.
[[nodiscard]] sim::Attitude attitudeToward(const sim::SocialLedger& ledger,
                                           std::int32_t actorId) noexcept;

/// "struck" / "steel" / "slew" for the three deeds a witness reacts to;
/// empty for every other deed, which a witness does not remark on.
[[nodiscard]] std::string_view reactDeedKey(sim::Deed deed) noexcept;

/// VOICE LANE, THE ADDRESS: the worded reaction when a reply moved somebody's
/// disposition -- never a number (DECISIONS.md: standing is never SHOWN as
/// one), a stage direction in the register the owner's own barks use for
/// what a face does ("(a grunt, and a turned shoulder)"). Empty when nothing
/// moved. `before`/`after` are Reply::dispositionBefore/After.
[[nodiscard]] std::string wordedReaction(std::int32_t before, std::int32_t after);

/// Fills every `<victim>` in a row with the name the sim recorded. A row that
/// names a victim nobody can name (empty) comes back empty, so the caller
/// walks on to a row that does not.
[[nodiscard]] std::string fillVictim(std::string_view row, std::string_view victim);

// ---------------------------------------------------------------------------
// the emitter
// ---------------------------------------------------------------------------

/// Everything the emitter reads in one step. Built by Session from public
/// state; nothing here is a pointer the emitter writes through.
struct VoiceScene {
    const sim::Tavern* room = nullptr;
    /// Null in a session without a district (the tavern fixture).
    const sim::WardPopulation* people = nullptr;
    /// The tiles, for the one sight line the channel asks: a snatch between
    /// two bodies is heard across open ground, not through a wall. Null
    /// means every line is open, which is what a test without a world gets.
    const sim::TileQuery* tiles = nullptr;
    /// The player's tile and band, and the same in Q8 for the room's metric.
    std::int32_t playerTileX = 0;
    std::int32_t playerTileY = 0;
    std::int32_t playerBand = 0;
    std::int32_t playerXQ8 = 0;
    std::int32_t playerYQ8 = 0;
    /// The body's step count: the clock every cooldown is measured on.
    std::int32_t step = 0;
    /// A conversation is open: the ambient channels hold their tongue, the
    /// partner's lines are the panel's.
    bool talking = false;
    /// Which ward body the conversation is with, or -1 -- never a passer.
    std::int32_t wardTalkId = -1;
    /// The player's stance and hands, for watch.notice and the steel key.
    bool crouched = false;
    bool handsUp = false;
    /// Who the alert row is quoting right now ("Watchman Cull" while his
    /// halt stands), or empty. The readback waits for the halt to leave the
    /// row rather than stacking a second line under the same name.
    std::string_view alertSpeaker;
};

class StreetVoice {
public:
    /// Reads the scene and says at most one line. Call once per movement
    /// step, after the simulation stepped and before the HUD is composed.
    void step(const VoiceScene& scene);

    /// EVENTS Session pushes from inside a verb, where the step cannot see
    /// them: the goodbye (the LEAVE reply's line, the back key, the walk-off).
    /// `line` is the director's own farewell(); empty says nothing.
    void speakFarewell(std::string name, std::string line, std::string key, std::int32_t actorId,
                       std::int32_t step);

    /// What is on the row right now, or an empty line. The strings live here
    /// so the HUD can hold string_views of them across a frame.
    [[nodiscard]] const VoiceLine& current() const noexcept { return current_; }
    /// True while the line still wants the row -- its hold has not run out.
    [[nodiscard]] bool up() const noexcept { return holdSteps_ > 0 && !current_.empty(); }
    /// Clears the row's text once its fade has finished, so a stale line
    /// cannot claim a slot it finished fading out of (clearIfClosed's rule).
    void clearIfSettled(bool faded) noexcept;
    /// Takes the row down when the alert row starts quoting the same mouth
    /// -- the arrest line landing under a watchman's readback -- so one man
    /// never says two things at once. True when a line was dropped.
    bool dropIfSpeaker(std::string_view name) noexcept;
    /// Takes the row down for good, no fade: the body was PLACED somewhere
    /// else (a travel, a waking, a scripted stand) and what somebody said
    /// within reach of the old feet is not on the row over the new ones.
    /// The channel's clocks and its memory of who spoke are untouched -- a
    /// journey is not an amnesty for a mouth's two minutes.
    void standDown() noexcept;

    /// True when the line said THIS step is on the Watch channel -- the edge
    /// Session stands the halt down on when the readback replaced it.
    [[nodiscard]] bool watchSpokeThisStep(std::int32_t step) const noexcept {
        return up() && current_.channel == VoiceChannel::Watch && current_.step == step;
    }
    /// How many lines have been said since construction, for the summary.
    [[nodiscard]] std::int32_t linesSpoken() const noexcept { return spoken_; }
    /// The last line said on each channel, for the summary and the suite --
    /// a pass line that was replaced by a react line is still recorded here.
    [[nodiscard]] const VoiceLine& lastOn(VoiceChannel channel) const noexcept {
        return last_[static_cast<std::size_t>(channel)];
    }
    /// True when this row text was said inside kRowMemorySteps of `step`.
    [[nodiscard]] bool saidLately(std::string_view text, std::int32_t step) const noexcept;

private:
    struct Spoken {
        std::int32_t actorId = -1;
        std::int32_t step = -1000000;
    };
    struct Witness {
        std::int32_t actorId = 0;
        sim::Deed deed = sim::Deed::Spoke;
        std::int32_t step = 0;
        /// The one the deed was done to, by the same read, or -1.
        std::int32_t victimId = -1;
    };
    /// The ledger as it read last step: (actor, disposition, lastDeed,
    /// injuries), so a witness's memory MOVING is the event.
    struct MemoryRead {
        std::int32_t actorId = 0;
        std::int32_t disposition = 0;
        sim::Deed lastDeed = sim::Deed::Spoke;
        std::int32_t injuries = 0;
    };
    struct Said {
        std::string text;
        std::int32_t step = -1000000;
    };

    void say(VoiceLine line);
    /// The pick with the row memory: the chain's rungs in order, each rung's
    /// rows from the mixed start, the first row not said lately; a chain
    /// with nothing fresh answers empty. `victim` fills <victim>.
    [[nodiscard]] VoiceLine pickFresh(const sim::BarkTables& barks,
                                      const std::vector<std::string>& chain, std::int32_t actorId,
                                      std::int32_t step, std::int32_t talks,
                                      std::string_view victim = {}) const;
    [[nodiscard]] bool channelFree(VoiceChannel channel, std::int32_t step) const noexcept;
    [[nodiscard]] bool speakerFree(std::int32_t actorId, std::int32_t step,
                                   std::int32_t cooldown) const noexcept;
    void noteSpeaker(std::int32_t actorId, std::int32_t step);
    void noteSaid(const std::string& text, std::int32_t step);

    void readWitnesses(const VoiceScene& scene);
    bool stepReact(const VoiceScene& scene);
    bool stepWatch(const VoiceScene& scene);
    bool stepPass(const VoiceScene& scene);
    bool stepOverheard(const VoiceScene& scene);

    VoiceLine current_;
    std::int32_t holdSteps_ = 0;
    std::int32_t spoken_ = 0;
    VoiceLine last_[5];
    std::int32_t lastOnChannel_[5] = {-1000000, -1000000, -1000000, -1000000, -1000000};
    /// The step the last EVENT line (react, watch, farewell) went up, so the
    /// ambient channels can wait past it.
    std::int32_t lastEventStep_ = -1000000;
    std::vector<Spoken> speakers_;
    std::vector<Said> said_;
    std::vector<MemoryRead> memories_;
    bool primed_ = false;
    std::vector<Witness> queue_;
    /// The readback's own state: whether this Closing episode has had its
    /// line, and the step it began. Reset when the stance drops.
    bool closingSeen_ = false;
    std::int32_t closingSince_ = 0;
    bool approachSaid_ = false;
};

}  // namespace granadad::render
