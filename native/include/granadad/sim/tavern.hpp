#pragma once

// The Gilded Gull: a room in the Docks with people in it who have jobs.
//
// K03 in DOCKS-GAZETTEER.md section 3 -- "Captains' tavern: charts on the
// walls, factors doing deals, the good wine", run by Master Venn, who is canon
// in content/raws/names/notables.json along with his six hired staff. It is the
// district's grandest house and the only one with rentable rooms authored
// above it, which is why it and not the Bilge or the Lantern Room is the
// tavern that gets staffed first.
//
// EVERY COORDINATE IN HERE WAS READ OUT OF THE BAKED WORLD, not out of the
// generator that produced it, and test_tavern.cpp re-reads all of them from
// content/maps/baked/docks_surface.trojsav on every build. A tavern whose bar
// is one tile from where the map says it is would put the bartender inside a
// wall on the day somebody re-bakes the district.
//
// WHAT IS SIMULATED
//
//   * six staff and eight patrons, each an Actor with a name, a post, hours,
//     and something it does when the player walks up to it;
//   * an hour of the clock, which decides who is in the room, whether the fire
//     is lit, and how loud it is;
//   * coin: the bartender has stock and takes payment, the innkeeper rents one
//     of the four rooms above the stair and the player sleeps in it;
//   * trouble: brawling, stealing or refusing to leave gets you warned by a
//     bouncer and then physically put out of the door.
//
// WHAT IS NOT, and is not pretended to be: hunger, rest, wages, relationships,
// crime beyond this room, or anything the district does outside these walls.
// The actor simulation ARCHITECTURE.md marks NOT BUILT stays not built; this is
// one building's worth of it, done properly.
//
// NO FLOATS.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/sim/actor.hpp"
#include "granadad/sim/brawl.hpp"
#include "granadad/sim/dialogue.hpp"
#include "granadad/sim/engine.hpp"
// TASK #81. earnedLegend() reads what a Trade or a Wire rung buys back.
#include "granadad/sim/legend.hpp"
// S9. A lock is a thing in the room, and being unseen is a fact about the room:
// both belong to whoever owns the taproom's people and its lamps.
#include "granadad/sim/lockpick.hpp"
#include "granadad/sim/stealth.hpp"
// S8. Being put on the floor is a promotion event for whoever did it, and the
// book that records it lives beside the room that owns the player's hit points
// for exactly the reason the crime ledger lives beside the dialogue: the state
// and the rule that moves it belong together.
#include "granadad/sim/nemesis.hpp"
// The roof moves' own vocabulary: a landing is charged HERE, in the room that
// owns the player's hit points, so RoofResult and safeDropBands are part of
// this header's interface. See Tavern::settleLanding.
#include "granadad/sim/player.hpp"
#include "granadad/sim/region_path.hpp"
#include "granadad/sim/spellbook.hpp"
#include "granadad/sim/tile_query.hpp"

namespace granadad::sim {

// ---------------------------------------------------------------------------
// the building, in world tiles
// ---------------------------------------------------------------------------

/// The Gilded Gull's authored geometry. World tiles: the baked world carries a
/// one-chunk VOID border, so authored local (lx, ly, lz) is (lx+32, ly+32,
/// lz+8). Everything here is the WORLD number.
namespace gull {

/// Ground floor -- the taproom -- and the guest floor above it.
inline constexpr std::int32_t kGroundBand = 19;
inline constexpr std::int32_t kUpperBand = 20;
/// And the lead over both of them: fifteen by fourteen of flat roof, authored
/// since S1, standable since S1, and unreachable until S5 taught the body to
/// climb. It is the Skyrunners' front door to the ward's grandest house.
inline constexpr std::int32_t kRoofBand = 21;

/// Footprint including the walls. 15 x 14, the district's largest interior
/// after the Ropewalk.
inline constexpr std::int32_t kFootprintX0 = 146;
inline constexpr std::int32_t kFootprintY0 = 66;
inline constexpr std::int32_t kFootprintX1 = 160;
inline constexpr std::int32_t kFootprintY1 = 79;

/// The two door tiles in the north wall, on the Tarwalk frontage.
inline constexpr std::int32_t kDoorX0 = 153;
inline constexpr std::int32_t kDoorX1 = 154;
inline constexpr std::int32_t kDoorY = 66;

/// Where a body ends up when it is put out: the quay apron, two tiles clear of
/// the threshold, which is far enough that walking straight back in is a
/// decision rather than an accident.
inline constexpr std::int32_t kStreetX = 153;
inline constexpr std::int32_t kStreetY = 63;

/// The bar counter: a seven-tile run of granite across the middle of the room.
inline constexpr std::int32_t kBarX0 = 149;
inline constexpr std::int32_t kBarX1 = 155;
inline constexpr std::int32_t kBarY = 71;
/// Behind it, where the bartender stands.
inline constexpr std::int32_t kBartenderX = 152;
inline constexpr std::int32_t kBartenderY = 72;

/// The hearth in the south wall, and the tile in front of it the fire lights
/// the room from.
inline constexpr std::int32_t kHearthX0 = 149;
inline constexpr std::int32_t kHearthX1 = 150;
inline constexpr std::int32_t kHearthY = 77;

/// The stair to the guest rooms, and the snug it stands in behind the
/// partition wall at x=157.
inline constexpr std::int32_t kStairX = 159;
inline constexpr std::int32_t kStairY = 77;
inline constexpr std::int32_t kSnugX0 = 158;
inline constexpr std::int32_t kSnugX1 = 159;

/// The four rentable rooms above, one bed each. Authored as CLOTH blocks in
/// content/maps/src/docks_surface.tmx; the tile a sleeper stands on is the one
/// beside the bed.
inline constexpr std::int32_t kRoomCount = 4;
struct GuestRoom {
    std::int32_t bedX;
    std::int32_t bedY;
    /// Where a body stands to use the bed.
    std::int32_t standX;
    std::int32_t standY;
};
inline constexpr GuestRoom kRooms[kRoomCount] = {
    {148, 68, 149, 68},  // north-west
    {156, 68, 155, 68},  // north-east
    {148, 76, 149, 76},  // south-west
    {156, 76, 155, 76},  // south-east
};

/// S5. A guest with anything worth keeping keeps it at the foot of the bed, so
/// the cell a strongbox stands on IS the bed cell -- authored as a solid CLOTH
/// block, which makes it furniture a body cannot walk into and can reach across
/// from the one tile beside it. Four rooms, four boxes, and no new geometry:
/// the burglary happens on tiles the map already has.
///
/// A box in a room you RENTED is your own, and cracking your own box is not a
/// crime; the room refuses to call it one.
[[nodiscard]] constexpr std::int32_t roomAtStand(std::int32_t tileX,
                                                 std::int32_t tileY) noexcept {
    for (std::int32_t i = 0; i < kRoomCount; ++i) {
        if (kRooms[i].standX == tileX && kRooms[i].standY == tileY) {
            return i;
        }
    }
    return -1;
}

/// Where a bale of contraband sits between the boat and the buyer: the snug
/// behind the partition, which is the one corner of the taproom the bar cannot
/// see into. It is why the Skyrunners' contact drinks there.
inline constexpr std::int32_t kBaleX = 158;
inline constexpr std::int32_t kBaleY = 70;

/// Everything a pathfinder inside the Gull may touch: the building, both
/// floors, and enough of the Tarwalk in front of it to put somebody out on.
inline constexpr TileBox kRegion{kFootprintX0 - 1, kStreetY - 2, kGroundBand,
                                 kFootprintX1 + 1, kFootprintY1 + 1, kUpperBand};

/// Whether a tile is inside the walls, on either floor.
[[nodiscard]] constexpr bool insideFootprint(std::int32_t tileX, std::int32_t tileY) noexcept {
    return tileX >= kFootprintX0 && tileX <= kFootprintX1 && tileY >= kFootprintY0 &&
           tileY <= kFootprintY1;
}

// --- the hours --------------------------------------------------------------

// --- the lights, DERIVED ----------------------------------------------------
//
// S2 hardcoded four table tiles and three lantern tiles in the renderer, with
// no derivation and no test -- the exact thing the header rule above forbids,
// and the S2 review caught it. They are computed here now, out of the baked
// bytes and out of coordinates that are themselves re-read from the baked bytes
// on every build.

/// A tile, when only a tile is meant.
struct TilePos {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

/// What kind of flame a house light is. The renderer decides what that looks
/// like; the simulation decides where they are and when they burn.
enum class LightKind : std::uint8_t {
    /// The fire in the south wall.
    Hearth = 0,
    /// A candle standing on a piece of furniture.
    Candle = 1,
    /// A lantern hanging from the ceiling.
    Lantern = 2,
};

struct HouseLight {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t band = kGroundBand;
    LightKind kind = LightKind::Candle;
};

/// The taproom's FURNITURE, read out of the world: every solid cell inside the
/// walls on the ground floor that is not the bar counter, not the hearth and
/// not the snug partition. Those are the tables, and a table is where a candle
/// stands. Ascending by (y, x), so the list is the map's and not the caller's.
[[nodiscard]] std::vector<TilePos> taproomTables(const TileQuery& tiles);

/// How many the baked Docks actually has. Pinned so a re-bake that moves the
/// furniture is a red test rather than a room that quietly goes dark.
inline constexpr std::size_t kTableCount = 4;

/// Where the three hanging lanterns are, derived from the door and the bar --
/// both of which test_tavern.cpp re-reads from the baked bytes. A captains'
/// house lights its threshold and its counter; nothing else needs a rule.
[[nodiscard]] std::vector<TilePos> lanternTiles();

/// Doors open at eleven and shut at two. A captains' house keeps late hours
/// because ships come in on the tide, not on the clock.
inline constexpr std::int32_t kOpensAt = hourOfDay(11);
inline constexpr std::int32_t kClosesAt = hourOfDay(2);

/// The fire is banked, not lit, outside these. Lit an hour before the doors so
/// the room is warm when the first crew arrives.
inline constexpr std::int32_t kFireLitFrom = hourOfDay(10);
inline constexpr std::int32_t kFireLitUntil = hourOfDay(3);

}  // namespace gull

// ---------------------------------------------------------------------------
// trade
// ---------------------------------------------------------------------------

/// What a drink and a bed cost, in the smallest coin there is. THE ODDS, not
/// the price: what the player actually pays comes out of drinkPriceForPlayer()
/// and moves with how the bartender feels about them and how well they haggle.
inline constexpr std::int32_t kDrinkPrice = 2;
inline constexpr std::int32_t kRoomPrice = 12;
/// Barrels in the cellar at open. A tavern that never runs dry has no economy.
inline constexpr std::int32_t kOpeningStock = 60;
/// What the player arrives with. S2 is where the purse starts existing.
inline constexpr std::int32_t kPlayerStartingCoin = 40;

/// The answer to any request made across the bar or at the stair.
enum class ServiceResult : std::uint8_t {
    Served = 0,
    /// Nobody is behind the bar, or at the stair, or in the room at all.
    NobodyThere = 1,
    /// The doors are shut.
    Closed = 2,
    /// Not enough coin.
    NoCoin = 3,
    /// The barrels are dry, or every room is let.
    OutOfStock = 4,
    /// You are not close enough to ask.
    TooFar = 5,
    /// You have been put out of this house, and it has not forgotten.
    Barred = 6,
    /// Asked, and refused. The Skyrunner's whole personality.
    Refused = 7,
};

/// The DIAGNOSTIC name of a result: "no coin", "out of stock", "?". For logs,
/// for the twin-run report and for the cases that assert on it.
///
/// NOT FOR THE PLAYER, and it used to be. Tavern::applyReply put this straight
/// into the sentence a landlord says across his own counter, so asking Master
/// Venn for a bed you cannot afford was answered with the word "no coin", and
/// asking after the barrels ran dry was answered with "out of stock" -- an
/// inventory term, out of a shop that does not exist, from a man who has three
/// authored lines about his own cellar two hundred lines further up this file.
/// The `?` arm could reach the screen too. Use counterRefusal/restRefusal.
[[nodiscard]] std::string_view serviceResultName(ServiceResult result) noexcept;

/// What the person behind the counter SAYS when the answer is no. `goods` is
/// what was asked for, because "the barrels are dry" and "every bed is let" are
/// the same ServiceResult and are not the same sentence.
[[nodiscard]] std::string_view counterRefusal(ServiceResult result, Goods goods) noexcept;

/// The same, for a player trying to sleep. Tavern::sleep answers NobodyThere
/// when no room has been taken and TooFar when the player is not at its
/// bed-foot, so the counter's wording would be wrong twice over.
[[nodiscard]] std::string_view restRefusal(ServiceResult result) noexcept;

/// What the player did that the house minds.
enum class Offence : std::uint8_t {
    /// Threw a punch under this roof.
    Brawled = 0,
    /// Took something.
    Stole = 1,
    /// Was told to go, and did not.
    RefusedToLeave = 2,
};

[[nodiscard]] std::string_view offenceName(Offence offence) noexcept;

/// How the house is currently regarding the player.
enum class Standing : std::uint8_t {
    /// Nothing has happened.
    Welcome = 0,
    /// A bouncer is on the way over to say it once.
    BeingWarned = 1,
    /// Said. The clock is running.
    Warned = 2,
    /// Hands on.
    BeingEjected = 3,
    /// Out, and not welcome back for a while.
    Barred = 4,
};

[[nodiscard]] std::string_view standingName(Standing standing) noexcept;

// --- the numbers behind the door policy -------------------------------------

/// How close a bouncer has to get before the warning counts as given, Q8.
inline constexpr std::int32_t kWarnRadius = 2 * kSubOne;
/// Seconds between being warned and being handled. Long enough to finish a
/// drink and leave; short enough that "he warned me" is not a licence.
inline constexpr std::int32_t kGraceSeconds = 20;
/// What the ward's own balance of power can move that to. A house never gives
/// no rope at all and never gives all night -- see graceSecondsForPlayer().
inline constexpr std::int32_t kGraceSecondsFloor = 6;
inline constexpr std::int32_t kGraceSecondsCeiling = 40;
/// Seconds you stay barred after being put out.
inline constexpr std::int32_t kBarredSeconds = 300;
/// Q8 a single shove moves somebody. Five-eighths of a tile, so being frog-
/// marched from the bar to the street is about fifteen seconds of it.
inline constexpr std::int32_t kEjectionShove = 160;

/// The reserved actor id the player answers to inside this room. Zero, so no
/// staff or patron can ever collide with it.
inline constexpr std::int32_t kPlayerActorId = 0;

/// How close a body has to be to a strongbox or a bale to put hands on it, Q8.
inline constexpr std::int32_t kReachQ8 = 2 * kSubOne;

// --- S9: the locks on the four guest boxes ----------------------------------
//
// The boxes were unlocked from S5 until now, and it showed: CRACKSMANSHIP was
// read once, to scale the take out of a box that had opened itself. Each box
// now carries a lock whose id is its room index, so its pins are the same pins
// every time the ward is built from the same seed -- see lockpick.hpp on why a
// lock is not re-rolled between attempts.

/// VERIFICATION GAP (S9): THESE FOUR BOXES ARE THE ONLY LOCKS IN THE GAME.
/// The district's doors, the Chandlery's counting room, the Drowned Hold and
/// the smuggler undercellars have no locks -- because none of them is
/// simulated, and a lock on a room nobody can enter would be a lock nobody can
/// pick. lockpick.hpp's kMaxPins is 6 and Lock carries `wards` precisely so
/// that the day the ward's other interiors exist, nothing in that file changes.
///
/// Pins in a guest's strongbox. Three: it is a travelling box under a bed, not
/// the Chandlery's counting-room door.
inline constexpr std::int32_t kStrongboxPins = 3;
/// And how it is warded. The two rooms on the harbour side are the good rooms
/// -- a captain pays for the window -- so their boxes are the better locks.
[[nodiscard]] constexpr std::int32_t strongboxWards(std::int32_t room) noexcept {
    return (room == 0 || room == 1) ? 2 : 1;
}

/// What a lift out of somebody's coat is worth beyond the coin: one piece of
/// property, which is what a fence exists to buy.
inline constexpr std::int32_t kLiftPieces = 1;
/// How close a hand has to get, Q8. Arm's length and not a tile more.
inline constexpr std::int32_t kLiftReachQ8 = kSubOne + kSubHalf;

// --- what stealth is worth to a hand in a coat -------------------------------
//
// A LIFT IS TWO QUESTIONS AND THEY ARE NOT THE SAME QUESTION. Whether somebody
// can see you standing at their elbow is one thing -- at arm's length, in any
// light, they generally can, and pretending otherwise would be silly. Whether
// they feel the hand is another, and that is CRACKSMANSHIP against their own
// STREETWISE, which is exactly how the dialogue layer's PickPocket topic has
// resolved it since S3 and is not being changed here.
//
// So stealth does not decide the lift. It MOVES THE NUMBER, by a bounded
// amount, in the direction it should: a mark who is staring straight at you in
// a lamp pool is harder, and one who is turned away in a dark room with a
// crowd shouting over him is easier. That bound is what stops either half
// swamping the other -- a rule where being crouched beat a master's wits would
// be a rule where the skill did not matter.

/// Points of notice per point of the mark's guard.
inline constexpr std::int32_t kLiftNoticePerPoint = 3;
/// And the most, either way, that being seen or unseen is worth.
inline constexpr std::int32_t kLiftStealthSwing = 12;

/// Bales in the snug per night.
///
/// S6 SHIPPED A BOARD THE WARD COULD NOT SUPPLY, and this constant plus the
/// one below is where it went wrong. Two bales, three units each, and ONE good
/// drawn for the whole night out of three: a night landed at most six units of
/// a good the player had a one-in-three chance of even wanting. Against that,
/// `flower_loft` asks 3-6 flower in two days, `dust_grey_ledger` asks 1-3 dust
/// TONIGHT and `spirit_row` asks 2-5 quayfire. Most of the nightly board was
/// work a player was structurally unable to take, and only Cull's scalp bounty
/// -- fed by the vermin and not by the boat -- was reliably completable.
///
/// Three bales now, and the good is drawn PER BALE rather than per night, out
/// of what tonight's board actually asked for. See Tavern::drawBaleGoods.
inline constexpr std::int32_t kBalesPerNight = 3;
/// And how many UNITS are in one. S5's bale was a bool; a bale has contents
/// now, and what is in it is what a contract can want and a watchman can find.
inline constexpr std::int32_t kBaleUnits = 3;
/// The three goods a boat actually lands. Scalps come off the ward's own rats
/// and a christening cup comes out of somebody's strongbox; neither is cargo.
inline constexpr std::int32_t kBoatGoodCount = 3;

/// Rats on the skirting after the doors shut, per night. Four, because the
/// ward's smallest bounty asks for two and its largest for four -- a night's
/// work is a night's work, and a fifth rat would make a bounty a formality.
inline constexpr std::int32_t kVerminPerNight = 4;
/// What a rat has. One clean punch.
inline constexpr std::int32_t kVerminHealth = 4;
/// The hours the taproom is quiet enough for them: from eleven at night, when
/// the late crowd has thinned, until the doors open again at eleven in the
/// morning. It OVERLAPS Watchman Cull's drink (nine to one) on purpose -- the
/// man who hands out the ward's bounty and the vermin it is paid for have to be
/// in the same building for two hours or the bounty is not a thing a player can
/// do in one night.
inline constexpr std::int32_t kVerminFrom = hourOfDay(23);
inline constexpr std::int32_t kVerminUntil = hourOfDay(11);

/// A BRAWL NEVER KILLS. That is what makes it a brawl, and it is why the door
/// policy can be enforced with fists at all: the worst a taproom fight does to
/// the player is put them on the floor and then out of it. Hit points stop
/// here, the fight ends, and the bouncers carry on with the ejection.
///
/// Under LETHAL rules this floor is lifted -- the player can reach zero and
/// die, routing through the same applyDefeat/nemesis/quay-revive the brawl KO
/// uses (COMBAT-ACTION-SPEC.md section 4.5). Both halves land now: the
/// player-KILLS path (Tavern::playerAttackUp) and, STANCE & ROOM BUILD, the
/// NPC-KILLS-player path -- stepBrawl resolves the room's blows under lethal
/// rules with this floor lifted to zero, and a blow that empties the player
/// routes applyDefeat exactly as a brawl KO does. Under BRAWL rules the floor
/// is this constant, unchanged.
inline constexpr std::int32_t kPlayerBrawlFloor = 1;

// ---------------------------------------------------------------------------
// ACTION-COMBAT BUILD: the swing state machine and the NPC swing cadence
// ---------------------------------------------------------------------------
//
// The named integer constants of COMBAT-ACTION-SPEC.md sections 1.2 and 1.5,
// all at the 60-steps-per-second spine. The swing charge tiers (kSwingChargeQ8
// / kHardSwingChargeQ8) live in brawl.hpp beside strike(); the two new wind
// costs (kHardSwingFatiguePoints / kBlockCatchFatiguePoints) in fatigue.hpp.

/// The hold, in movement steps, at or above which a swing is a HARD swing.
/// 250 ms. Equal by design to render::HoldToggle::kTapSteps (== 15) so every
/// tap/hold boundary in the game is one number; the client ties them with a
/// static_assert at file scope in main.cpp (sim cannot include render).
inline constexpr std::int32_t kHardSwingHoldSteps = 15;
/// The lockout after a swing / a hard swing, in steps. 600 ms and 900 ms;
/// Attack edges during recovery are dropped, not buffered.
inline constexpr std::int32_t kSwingRecoverySteps = 36;
inline constexpr std::int32_t kHardSwingRecoverySteps = 54;
/// How long the death ceremony holds its epitaph plate before the revive
/// plate, in steps. 4.5 s, Barony's measured hold. The SIM only names it and
/// routes the defeat; the plate itself is PRESENTATION's (section 4.5).
inline constexpr std::int32_t kDeathHoldSteps = 270;

/// A standing brawler's seconds between swings, as a step count: 1.1 s.
inline constexpr std::int32_t kNpcSwingIntervalSteps = 66;
/// The fight-join timer offset, actorId % this, so a crowd never metronomes.
inline constexpr std::int32_t kNpcSwingStaggerSteps = 30;
/// The re-arm when the swing timer expires out of reach: 0.2 s re-check while
/// the actor is still closing the gap.
inline constexpr std::int32_t kNpcSwingRetrySteps = 12;

/// FIGHTING MODE (STANCE & ROOM BUILD, oblivion-roadmap.md section 3.2). The
/// lull that lowers the hands on its own: this many movement steps -- 10 s --
/// in IDLE with no swing thrown or caught, the guard not held and nobody
/// swinging at the player. Counted down in stepPlayerCombat; every raise
/// (a down-edge, a swing thrown, a guard held, a blow caught) refills it. Six
/// hundred and not three hundred, because a brawl's lull is longer than five
/// seconds.
inline constexpr std::int32_t kLowerHandsSteps = 600;

// ---------------------------------------------------------------------------
// WATCH & RHYTHM BUILD: the rhythm bundle (oblivion-gap-combat.json change 5)
// ---------------------------------------------------------------------------
//
// Oblivion's learnable rhythm is three asymmetric rules on top of
// attack/block/hold, and every one of them is a step count on the 60-step
// spine, integer, hashed, drawing nothing. The two bands are carved off the
// NPC's OWN swing roll (the one stepBrawl already draws), read off bits no
// other band claims -- the fatigue build's same-roll discipline, fifth and
// sixth carvings. "No NPC blocking, no NPC hard swings" was the v1 ceiling
// (COMBAT-ACTION-SPEC.md section 10); this build lifts it for the tavern
// roster only, by exactly these numbers.

/// A standing brawler's TELEGRAPH: the steps between his swing timer expiring
/// in reach and the blow landing. 0.3 s -- long enough to read and answer
/// with a tap (which staggers him instead), short enough that a crowd still
/// lands blows. The roll is drawn at the START of the wind-up and kept.
inline constexpr std::int32_t kNpcWindupSteps = 18;
/// And the HARD swing's longer tell: 0.5 s, the Oblivion power-attack
/// pull-back. Long on purpose -- a doubled blow you could not see coming
/// would be a random event, not a rule.
inline constexpr std::int32_t kNpcHardWindupSteps = 30;
static_assert(kNpcHardWindupSteps < kNpcSwingIntervalSteps &&
                  kNpcWindupSteps < kNpcHardWindupSteps,
              "the tell is the last stretch of the swing interval, not added to it: blow to "
              "blow stays kNpcSwingIntervalSteps, and a hard tell is the longer one");
/// Steps a man STAGGERED by a pre-empting hit or a hard swing loses: no
/// closing, no swing, no guard. 0.4 s, enough for one free tap and not two.
inline constexpr std::int32_t kStaggerSteps = 24;
/// Steps the player's arm RECOILS for when a normal swing is caught by an
/// NPC guard. A full second, longer than the swing's own 36-step recovery on
/// purpose -- shorter and it would be no recoil at all -- so a tap into a
/// raised guard costs more than the tap. The Oblivion asymmetry, half one.
inline constexpr std::int32_t kRecoilSteps = 60;
/// Steps a GUARD is broken for when it catches a HARD swing: the blocker --
/// player or NPC -- is block-staggered, his guard is down and his attack is
/// refused. 0.5 s, the other half of the asymmetry: a hard swing is the
/// answer to a turtle, and a block is the answer to a tap.
inline constexpr std::int32_t kBlockStaggerSteps = 30;
/// The GUARD band, in 256ths of the NPC's own swing roll (bits 40-47): the
/// share of a patron's swings that leave his hands up until his next one.
/// About one in five, so a bar fight is mostly open and a caught tap is a
/// surprise, not a wall.
inline constexpr std::uint64_t kNpcGuardBand256 = 48;
/// A PROFESSIONAL's guard (a bouncer, a watchman): half his swings. A man
/// hired to stand in doorways keeps his hands up; that is the whole reason a
/// hard swing exists.
inline constexpr std::uint64_t kNpcProfessionalGuardBand256 = 128;
/// The bits of the NPC's own roll the guard band reads, and the HARD band
/// beside it (bits 48-55): the share of an NPC's swings thrown HARD
/// (kHardSwingChargeQ8, the longer tell). About one in six.
inline constexpr int kNpcGuardRollShift = 40;
inline constexpr int kNpcHardRollShift = 48;
inline constexpr std::uint64_t kNpcHardBand256 = 40;
/// PATRONS STAND BACK: on the escalation edge every non-brawler within this
/// many tiles of the player steps kStandBackTiles tiles directly away (the
/// first standable tile inside the footprint on that line). Three, the reach
/// of a thrown stool; two, so the far cells clear without emptying the room.
inline constexpr std::int32_t kStandBackRadiusTiles = 3;
inline constexpr std::int32_t kStandBackTiles = 2;

/// The player's swing machine, sim-owned and hashed. SWING and HARD resolve
/// instantly on release (we have no viewmodel to animate; recovery carries the
/// cadence), so the persistent states are the three below and the tier is read
/// off the charge step count at release. See Tavern::stepPlayerCombat.
enum class PlayerCombatState : std::uint8_t {
    Idle = 0,
    Charging = 1,
    Recovery = 2,
};

/// THE CAST CHECK. A cast succeeds when a d100 draw lands under
///
///   clamp(kCastBasePercent + kCastPercentPerLevel * linkcraft
///                          - kCastPercentPerDifficulty * spellDifficulty,
///         kCastFloorPercent, kCastCeilPercent)
///
/// -- which is spells.json's own "a cast can still fizzle (by design)" made
/// real, priced by the cost model everything else already uses. The base sits
/// high on purpose: the public shelf is WEAK magic gated shallow, and a Sting
/// (difficulty 5) at linkcraft 0 landing about two casts in three is a
/// beginner's craft, not a slot machine. The ceiling never reaches 100 -- the
/// same "rises with the level and never buys certainty" contract every check
/// in this project carries -- and the floor never reaches 0, because a check
/// that cannot be passed is a button that lies.
inline constexpr std::int32_t kCastBasePercent = 75;
inline constexpr std::int32_t kCastPercentPerLevel = 4;
inline constexpr std::int32_t kCastPercentPerDifficulty = 2;
inline constexpr std::int32_t kCastFloorPercent = 10;
inline constexpr std::int32_t kCastCeilPercent = 95;
/// A slipped link recovers in half a minute; a delivered one waits its
/// authored cooldownTicks. Charging the FULL cooldown on a fizzle would make
/// low skill read as a dead button rather than an unreliable craft.
inline constexpr std::int32_t kFizzleCooldownTicks = 30;

// ---------------------------------------------------------------------------
// the tavern
// ---------------------------------------------------------------------------

/// What a conversation produced.
struct TalkResult {
    ServiceResult result = ServiceResult::NobodyThere;
    /// Who answered, or empty.
    std::string speaker;
    /// What they said.
    std::string line;
};

class Tavern final : public SimulationSystem {
public:
    /// `tiles` and the world behind it must outlive the tavern. `contentDir` is
    /// only used to read the spell raws the priest teaches from; an empty path
    /// or a missing file leaves him with nothing to teach and changes nothing
    /// else.
    ///
    /// The tavern carries its OWN CounterRandomSource rather than only using
    /// the one the engine hands it per tick. Every draw it makes is still the
    /// same pure (seed, salt, tick, key, index) chain -- the source holds no
    /// stream position -- but a player punching somebody happens between ticks,
    /// outside any TickContext, and a fight whose damage roll came from
    /// somewhere unhashed would be a hole in the twin-run gate. The draw index
    /// for those is a counter that IS hashed.
    Tavern(const TileQuery& tiles, std::int32_t timeOfDaySeconds, std::uint64_t worldSeed,
           std::filesystem::path contentDir = {});

    // --- SimulationSystem ---------------------------------------------------

    [[nodiscard]] const SystemId& id() const noexcept override { return id_; }
    [[nodiscard]] TickPhase phase() const noexcept override { return TickPhase::Actors; }
    /// One simulated second: the clock moves, schedules fire, drinks are
    /// poured, bouncers decide, and a fight resolves one exchange.
    void tick(const TickContext& context) override;
    void hash_into(HashSink& sink) const override;

    // --- the movement clock -------------------------------------------------

    /// One movement step for every actor. Called kStepsPerSecond times between
    /// ticks, by whoever owns the loop.
    void stepMovement();

    /// Tells the room where the player's body is, and what is in its hands.
    /// Q8, the same units the body uses -- see actor.hpp on why this is not
    /// reconstructed at draw time.
    void setPlayer(std::int32_t xQ8, std::int32_t yQ8, std::int32_t band) noexcept;
    void setPlayerCombat(Weapon weapon, Intent intent) noexcept;

    /// Tells the room WHICH WAY the body is facing, Q16-BAM yaw, pushed every
    /// step beside setPlayer. The sightline raycast (VETO 1) casts down it, so
    /// it is authoritative facing the same way setPlayer's position is
    /// authoritative -- and hashed for the same reason position is. A room the
    /// client never tells stays at its default facing, which is what every
    /// pre-combat capture and the workload do; targeting simply reads that.
    void setPlayerYaw(Angle yaw) noexcept { playerYaw_ = yaw & (kTurnFull - 1); }
    [[nodiscard]] Angle playerYaw() const noexcept { return playerYaw_; }

    /// Arms the player by an authored weapon id -- kEvictorWeaponId is the
    /// whole roster today -- and touches NOTHING else: intent stays where it
    /// stands, because a case beat that hands a man a cudgel has no business
    /// deciding what he means to do with it (setPlayerCombat couples the two
    /// on purpose for the tests that stage whole fights; a reward beat must
    /// not). Unknown ids refuse rather than disarm, so a case sheet that
    /// misspells its own reward fails its test instead of quietly emptying
    /// the player's hand. playerWeapon_ is already a hashed byte, so a grant
    /// is twin-run-visible the second it lands, and identical in both runs
    /// because the beat that calls it is.
    bool grantPlayerWeapon(std::string_view weaponId) noexcept;
    [[nodiscard]] Weapon playerWeapon() const noexcept { return playerWeapon_; }

    /// A shove the room wants applied to the player's body, in Q8, or (0,0).
    /// Read and CLEARED by the caller, which owns the body -- the tavern never
    /// holds a pointer to it.
    [[nodiscard]] std::int32_t takePlayerShoveX() noexcept;
    [[nodiscard]] std::int32_t takePlayerShoveY() noexcept;

    // --- the clock ----------------------------------------------------------

    [[nodiscard]] std::int32_t timeOfDay() const noexcept { return timeOfDay_; }
    void setTimeOfDay(std::int32_t secondOfDay) noexcept;
    /// Moves the clock forward and re-seats everybody, without running the
    /// intervening seconds. What sleeping in a rented room does.
    void skipTo(std::int32_t secondOfDay);

    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] bool fireLit() const noexcept;

    /// Every flame the house is showing right now: the hearth while the fire is
    /// lit, a candle on every derived table and the hanging lanterns while the
    /// doors are open, nothing at all when the house is dark. The SIMULATION
    /// owns this, not the renderer -- see the derivation note on gull::.
    [[nodiscard]] std::vector<gull::HouseLight> houseLights() const;

    // --- who is in the room -------------------------------------------------

    [[nodiscard]] const std::vector<Actor>& actors() const noexcept { return actors_; }
    [[nodiscard]] const Actor* actorById(std::int32_t id) const noexcept;
    /// PEOPLE in the room. A rat is not one of the fourteen.
    [[nodiscard]] std::int32_t presentCount() const noexcept;
    /// And rats, counted apart for the same reason.
    [[nodiscard]] std::int32_t verminPresent() const noexcept;
    [[nodiscard]] std::int32_t patronCount() const noexcept;
    /// 0 (empty) to 100 (pay night). What the room SOUNDS like.
    [[nodiscard]] std::int32_t noise() const noexcept;

    /// The nearest present actor to the player, within `reachQ8`, or nullptr.
    [[nodiscard]] const Actor* nearestTo(std::int32_t xQ8, std::int32_t yQ8,
                                         std::int32_t reachQ8) const noexcept;

    [[nodiscard]] bool playerInside() const noexcept;

    // --- trade --------------------------------------------------------------

    [[nodiscard]] std::int32_t playerCoin() const noexcept { return playerCoin_; }
    void setPlayerCoin(std::int32_t coin) noexcept { playerCoin_ = coin; }
    [[nodiscard]] std::int32_t drinkStock() const noexcept { return drinkStock_; }
    [[nodiscard]] std::int32_t drinksPlayerHasHad() const noexcept { return playerDrinks_; }

    /// What the bar and the stair will charge the player RIGHT NOW.
    ///
    /// kDrinkPrice and kRoomPrice are what the goods are worth. What you pay is
    /// what the person behind the counter thinks of you, plus what your
    /// streetwise is worth against theirs, plus whatever you last argued them
    /// down to. This is the single most legible place standing shows up: a warm
    /// bartender charges less than a cold one for the same mug, every time,
    /// with no dialogue open.
    [[nodiscard]] std::int32_t drinkPriceForPlayer() const;
    [[nodiscard]] std::int32_t roomPriceForPlayer() const;
    /// The price last settled by haggling, or -1. Cleared when it is used, so
    /// one argument buys one drink.
    [[nodiscard]] std::int32_t negotiatedDrinkPrice() const noexcept {
        return negotiatedDrink_;
    }
    [[nodiscard]] std::int32_t negotiatedRoomPrice() const noexcept { return negotiatedRoom_; }

    /// Buys a drink across the bar. The player must be within reach of the
    /// bartender, who must be on shift, with stock, and be paid.
    ///
    /// Returns Refused when the bartender is HOSTILE. That is the one refusal
    /// in the trade path, and it is deliberate: DOCKS-GAZETTEER section 5.3
    /// forbids refusing INFORMATION, and says nothing about a landlord who
    /// caught you with a hand in his till.
    ServiceResult buyDrink();

    /// Rents one of the four rooms above the stair from the innkeeper.
    [[nodiscard]] std::int32_t rentedRoom() const noexcept { return rentedRoom_; }
    ServiceResult rentRoom();

    /// Sleeps until seven in the morning. Requires a rented room and a body in
    /// it. Returns Served and moves the clock; the caller moves the body.
    /// TIME-AND-TENURE BUILD: sleeping now MENDS -- see sleepUntil.
    ServiceResult sleep();

    /// TIME-AND-TENURE BUILD. The same rented bed, waking at a chosen hour
    /// instead of the fixed seven. This is where the owner's ruling lands in
    /// code: SLEEP heals and is bed-only (these checks are sleep()'s own,
    /// unchanged); WAIT is the render layer's verb, passes time anywhere safe,
    /// and mends nothing. The mend is total -- the same full restore
    /// reviveAfterDefeat has always given, because a night in a paid bed and a
    /// blackout on the quay are the two ways this build sleeps.
    ServiceResult sleepUntil(std::int32_t hour);

    /// sleep()'s own checks with the hands kept still: would the bed answer,
    /// without moving the clock. Lets the render layer offer an hour-select
    /// page only where sleeping would actually be Served.
    [[nodiscard]] ServiceResult sleepReadiness() const noexcept;

    /// TIME-AND-TENURE BUILD. True while anybody is actually swinging at the
    /// player -- brawlers_ is non-empty. The render layer's "anywhere safe"
    /// rule for WAIT reads it; a pure derived read, nothing new hashed.
    [[nodiscard]] bool playerInBrawl() const noexcept { return !brawlers_.empty(); }

    /// Talks to whoever is nearest. Every role answers differently, and the
    /// Skyrunner contact answers by not answering.
    ///
    /// KEPT because the roles' service answers are real -- "barrels are dry
    /// until the doors open again" is the bartender's own stock talking. It is
    /// no longer the whole of a conversation: talkTo() is.
    [[nodiscard]] TalkResult talkToNearest();

    // --- conversation -------------------------------------------------------
    //
    // The Gull owns the ward's social state for now, because the Gull is the
    // only room with people in it. When the district's other houses are staffed
    // the director moves up a level and every one of them borrows the same one;
    // nothing below depends on it living here.

    [[nodiscard]] DialogueDirector& dialogue() noexcept { return dialogue_; }
    [[nodiscard]] const DialogueDirector& dialogue() const noexcept { return dialogue_; }

    /// Describes an actor to the dialogue layer: who they are, which of the
    /// Forty they are (if any), what they will talk shop about, what they sell.
    [[nodiscard]] Speaker speakerFor(const Actor& actor) const;

    /// Opens a conversation with whoever is in reach. False when nobody is.
    bool talkTo();
    /// Picks a topic. The reply's coin and offences are applied HERE, so the
    /// dialogue layer never has to know what a tavern is.
    Reply chooseTopic(std::size_t index);
    /// Haggling moves, legal only while a haggle is open.
    Reply offerPrice(std::int32_t coins);
    Reply takeAskingPrice();
    /// Composing moves, legal only while a workbench is open. Routed through
    /// the room for the same reason the haggle is: the reply may move coin,
    /// standing or a rung, and the room is what applies those.
    Reply commitForge();
    Reply endForge();
    void endConversation();

    // --- S5: the crimes that are an ACT and not a conversation --------------
    //
    // Leaning on somebody and selling stolen property happen across a table and
    // live in the dialogue layer. These happen in the room, so the room owns
    // them -- and each reports through the same DialogueDirector::noteCrime the
    // topics do, so a tally, a heat and a faction number can never be moved by
    // one path and missed by the other.

    /// What an act in the room produced.
    struct StealResult {
        ServiceResult result = ServiceResult::NobodyThere;
        /// Coin taken.
        std::int32_t coin = 0;
        /// Pieces of property taken -- the thing a fence exists to buy.
        std::int32_t loot = 0;
        /// True when somebody present could see it happen.
        bool seen = false;
        /// A short report for the message line.
        std::string line;
    };

    /// Cracks the strongbox at the foot of the bed in the guest room the body
    /// is standing in. Refused for a room the player rented, for a box already
    /// emptied, and from the wrong floor.
    ///
    /// S9 PUTS A LOCK ON IT. This now refuses a box whose lock is still shut,
    /// with ServiceResult::Refused and a line that says so. The way through is
    /// beginPick() and probeLock(), or forceLock() and the noise that comes
    /// with it.
    StealResult crackStrongbox();
    /// Which of the four boxes have been emptied, as a bitmask. Hashed.
    [[nodiscard]] std::int32_t crackedBoxes() const noexcept { return crackedBoxes_; }

    // --- S9: stealth ---------------------------------------------------------
    //
    // See stealth.hpp for the rule and every number in it. What lives here is
    // the ROOM'S half: which lamps are burning, how loud the taproom is, and
    // who is in a position to notice.

    [[nodiscard]] Stance stance() const noexcept { return stealth_.stance(); }
    void setStance(Stance stance) noexcept { stealth_.setStance(stance); }
    /// C. Down on your haunches, or back up.
    void toggleStance() noexcept { stealth_.toggleStance(); }
    /// Told once a movement step by whoever owns the body: is it moving, and
    /// is it running. This is how footfalls reach the notice rule.
    void setPlayerMotion(bool moving, bool running) noexcept;
    [[nodiscard]] const StealthState& stealth() const noexcept { return stealth_; }
    /// What the body is doing to the air right now, 0..kNoiseMax.
    [[nodiscard]] std::int32_t playerNoise() const noexcept { return stealth_.noise(); }

    /// Every flame the SIMULATION counts, as integer light sources. The same
    /// list houseLights() gives the renderer, in the units stealth.hpp weighs.
    [[nodiscard]] std::vector<SimLight> simLights() const;
    /// What is falling on the tile the player is standing on, 0..kLightMax.
    [[nodiscard]] std::int32_t lightOnPlayer() const noexcept;
    /// The same for any tile in the room.
    [[nodiscard]] std::int32_t lightAt(std::int32_t tileX, std::int32_t tileY,
                                       std::int32_t band) const noexcept;

    /// Whether one actor can currently make the player out, and the working.
    /// Everything witnessCount and spreadWitness decide goes through here.
    [[nodiscard]] Notice noticeBy(const Actor& actor) const noexcept;
    /// The loudest read anybody in the room has on the player right now, for a
    /// HUD that wants to say HIDDEN or SEEN. Empty room reads zero.
    ///
    /// SCOPE, MARKED HERE AND NOT ONLY IN A SPRINT REPORT (S9 review, finding
    /// 7): THE WHOLE STEALTH SYSTEM IS THIS BUILDING'S. It walks `actors_` --
    /// the Gilded Gull's eleven to sixteen -- because they are the only bodies
    /// in the game with tiles, hours and facings. The 692 actors of the ward
    /// live in Ward's economic roll and have no position to be seen from. So in
    /// the Docks proper there is nobody to hide from and the HUD's stealth row
    /// does not draw (Session::stealthLine returns empty outside the room). The
    /// rule itself is building-agnostic -- sim/stealth.hpp takes an observer and
    /// a body and knows nothing about taverns -- so what a second room costs is
    /// a second cast, not a second system.
    [[nodiscard]] Notice worstNotice() const noexcept;
    /// True when nobody present can make the player out.
    [[nodiscard]] bool hidden() const noexcept { return !worstNotice().seen; }

    /// How many people are close enough, awake enough and on the right floor to
    /// have a chance of noticing the player at all -- everyone `noticeBy` does
    /// NOT rule out as oblivious before it weighs a single clause.
    ///
    /// S10, and the S9 review is why. Its second finding: the burglary
    /// acceptance asserted `hidden()` at a doorway that is EMPTY at two in the
    /// morning, so the beat landed whether or not stealth worked at all -- and
    /// it duly passed with the notice rule hard-wired to `seen = true`. "Nobody
    /// saw me" is only a claim about stealth when somebody was there to. This
    /// is the somebody-was-there half, and it is on the simulation rather than
    /// counted by the test so both the scripted line and the case read it the
    /// same way.
    [[nodiscard]] std::int32_t watchersInReach() const noexcept;

    // --- S9: the locks -------------------------------------------------------

    /// Picks the body is carrying.
    [[nodiscard]] std::int32_t picks() const noexcept { return picks_; }
    void setPicks(std::int32_t picks) noexcept;
    /// Which of the four boxes have been opened, and which are jammed shut.
    /// Both hashed: a jammed lock is permanent world change.
    [[nodiscard]] std::int32_t openedLocks() const noexcept { return openedLocks_; }
    [[nodiscard]] std::int32_t jammedLocks() const noexcept { return jammedLocks_; }
    [[nodiscard]] std::int32_t forcedLocks() const noexcept { return forcedLocks_; }

    /// The lock on the box at the foot of the bed in room `room`.
    [[nodiscard]] static Lock strongboxLock(std::int32_t room) noexcept;

    /// The attempt currently under the wire, for whoever draws it.
    [[nodiscard]] const Lockpicking& picking() const noexcept { return picking_; }

    /// G on a locked box. Puts the wire in. Refused off the guest floor, away
    /// from a bed, on your own rented room, on a box already open, on a jammed
    /// one, and with no picks left.
    struct PickResult {
        ServiceResult result = ServiceResult::NobodyThere;
        Feel feel = Feel::Idle;
        /// True when the lock came open on this call.
        bool opened = false;
        /// True when somebody heard or saw it.
        bool seen = false;
        std::string line;
    };
    PickResult beginPick();
    /// SPACE while the wire is in. One probe at the depth the pick is held at.
    PickResult probeLock();
    /// W/S while the wire is in.
    void movePick(std::int32_t delta) noexcept;
    /// ESC. Takes the wire out; the lock relocks itself.
    void abandonPick() noexcept;
    /// F while the wire is in, or on a jammed lock. Always opens it, is the
    /// loudest thing in the building, and bends what is inside.
    PickResult forceLock();

    // --- S9: the hand in the coat --------------------------------------------

    /// T. Lifts from whoever is nearest, IN THE ROOM, with no conversation
    /// open. The dialogue layer's own PickPocket topic is still there and still
    /// works across a table; this is the same crime committed from behind.
    ///
    /// TWO SKILLS, and the split is deliberate. Getting close enough unseen is
    /// SKYRUNNING -- whose row in the owner's skills.json says in as many words
    /// that it covers "sneak, pickpocket, takedown" -- and what the fingers do
    /// once they are there is CRACKSMANSHIP, which is what the ward's ledger
    /// has charged a lift to since S4. A lift charges both, and the mark
    /// noticing you is a stealth question before it is a skill contest.
    StealResult liftFrom();

    /// Buys a set of picks off the Skyrunners' own contact. Wants membership,
    /// reach and coin, exactly as taking the bale does.
    StealResult buyPicks();

    /// What a landing off the roofs cost.
    struct LandingResult {
        /// Hit points the fall took, or 0. See human_scale.hpp's fallInjury:
        /// this is the square of how much faster than you can take it you were
        /// going when you arrived, and it comes out of gravity rather than out
        /// of a table.
        std::int32_t hurt = 0;
        /// How far the body actually fell, millimetres. Handed back so a HUD, a
        /// case or a report can say "you fell five metres" rather than "you
        /// fell two of the units the map is made of".
        std::int32_t fellMm = 0;
        /// True when there was water or deep mud where the body came down.
        bool softLanding = false;
        /// True when this landing was the first arrival somewhere new and high,
        /// and therefore a roof-run the ward could have minded.
        bool roofRun = false;
        /// The counted verb the questline was told about: "climbs" or "leaps".
        std::string_view counted;
    };

    /// Charges a roof landing: the craft it takes, the fall it cost, the
    /// roof-run it was, and the counted verb a questline stage might want.
    ///
    /// MOVED HERE IN S6, out of render::Session, and it is a correction rather
    /// than a tidy-up. The S5 review's third finding was that this glue -- which
    /// owns fall damage, a skill use, a crime and two questline tallies -- had
    /// no test that could go red, because it lived in the client layer and the
    /// simulation suite could not reach it: test_crime.cpp had to hand-call
    /// noteTally() and noteCrime() to walk the Skyrunner line past its roof
    /// beats, which proved the questline and proved nothing about the landing.
    ///
    /// It is simulation state -- hit points, a tally, heat, two faction numbers
    /// -- so it belongs in the room that owns them. Session now hands the room
    /// the fall the body reported and draws whatever comes back.
    ///
    /// #77 ADDS THE LANDING TILE, defaulted so the twenty existing call sites
    /// still compile. It is optional because the SURFACE softens a fall -- eight
    /// metres into the harbour is a very different afternoon from eight metres
    /// onto the Long Quay -- and a caller that does not say where the body came
    /// down gets the hard answer, which is the safe one to be wrong about.
    LandingResult settleLanding(const RoofResult& move, std::int32_t fellBands,
                                std::int32_t landedBand,
                                std::int32_t landedX = INT32_MIN,
                                std::int32_t landedY = INT32_MIN);
    /// The highest band the player has ever stood on in this room's memory. A
    /// roof-run is counted once per ARRIVAL somewhere new and high rather than
    /// once per step, and this is what tells the two apart.
    [[nodiscard]] std::int32_t highestBandReached() const noexcept { return highestBand_; }

    /// Skins the downed vermin within reach. THE CULL VERB, and it is the Java
    /// build's own rule read across: stand beside a body, spend the act, and
    /// there is a scalp in your hand. A rat that has been skinned does not get
    /// up again, which is what a per-night count of them is FOR -- without it
    /// the ward's bounty is a coin faucet with whiskers.
    ///
    /// Refused for a rat still on its feet: this build has no killing, and a
    /// brawl is what puts a thing on the floor.
    StealResult takeScalp();
    /// Which of tonight's vermin have been skinned, as a bitmask. Hashed.
    [[nodiscard]] std::int32_t scalpedVermin() const noexcept { return scalpedVermin_; }

    // --- S6: the Watch --------------------------------------------------------

    /// Where a watchman is in the business of taking you.
    enum class WatchStance : std::uint8_t {
        /// Having a drink.
        Idle = 0,
        /// Has seen something and is crossing the room about it. THIS IS THE
        /// WINDOW: out of the door, out of his sight, and it is over.
        Closing = 1,
        /// Hands on. Resolved in the same second it is reached.
        Taken = 2,
    };

    /// What an arrest came to.
    struct ArrestReport {
        bool happened = false;
        /// WHAT THE PAPER ASKS FOR: the shipped ladder's answer, the murder
        /// override with it. JUSTICE BUILD: with paper (Held / Maimed /
        /// Condemned) this is the Watch's petition and the bench's answer is
        /// the hearing's (Tavern::hearing); only a paperless search (Fined)
        /// is settled here at the door.
        Sentence sentence = Sentence::None;
        WatchCause cause = WatchCause::None;
        std::int32_t unitsSeized = 0;
        /// The door's fine and the cell's hours. Written ONLY for the
        /// paperless search: with paper both are the court's, nothing is
        /// served at the arrest, and these read zero.
        std::int32_t fine = 0;
        std::int32_t heldHours = 0;
        /// Contracts that died with the goods.
        std::int32_t contractsLost = 0;
        std::string officer;
        std::string line;
    };

    [[nodiscard]] WatchStance watchStance() const noexcept { return watchStance_; }
    [[nodiscard]] WatchCause watchInterest() const noexcept { return watchCause_; }
    /// The watchman currently interested in the player, or nullptr.
    [[nodiscard]] const Actor* respondingWatchman() const noexcept;
    /// What the last watchman to look at you said, or empty.
    [[nodiscard]] const std::string& lastDemand() const noexcept { return lastDemand_; }
    /// BARKS LANE (feel/build). What the last man to step INTO a fight the
    /// player started said as he did it -- `<name>: <brawl.join row>` -- or
    /// empty. A rat says nothing; the Closing watchman keeps his own halt
    /// (lastDemand). Presentation only: not hashed, never read by the sim.
    [[nodiscard]] const std::string& lastJoin() const noexcept { return lastJoin_; }
    /// BARKS LANE (feel/build). The last panic line the room threw --
    /// `<name>: <crowd.flee row>` -- from the first patron sent back on the
    /// escalation edge (standBack) or a bloodied man breaking for the street
    /// (the rout), or empty. Presentation only, exactly like lastJoin.
    [[nodiscard]] const std::string& lastFlee() const noexcept { return lastFlee_; }
    /// The last arrest, whether or not it has been read.
    [[nodiscard]] const ArrestReport& lastArrest() const noexcept { return lastArrest_; }
    /// True once, after an arrest, so whoever owns the body can move it: to
    /// the street where the impound turns people loose after a paperless
    /// search, or -- JUSTICE BUILD -- to the Mission's door when the arrest
    /// left a hearing open (hearingPending()), where the bench is waiting.
    /// Read and CLEARED.
    [[nodiscard]] bool takeArrestRelease() noexcept;

    // --- JUSTICE BUILD: the court ---------------------------------------------
    //
    // An arrest WITH PAPER no longer resolves to an inert sentence and a clock
    // jump. The officer takes you to the Mission: applyArrest writes the
    // charge sheet off the ledger (draw-free), empties the sack into the
    // impound, spends its one draw where it always did, and opens a HEARING
    // on the ledger (CrimeLedger::hearing, hashed and codec'd). The page that
    // attends it is presentation's; the plea is a stepped input here; what a
    // judgment then DOES (coin, clock, ledger, the rope) is the sentence's own
    // step after this one. A paperless search (Sentence::Fined) is the shipped
    // fast path at the door, untouched, and never reaches the bench.

    /// A hearing is open: taken with paper and not yet sentenced. See
    /// HearingState::stage for whether it awaits the plea or the sentence.
    [[nodiscard]] bool hearingPending() const noexcept;
    /// The hearing: the sheet, the officer who laid it, the arrest's draw,
    /// and -- once pleaded -- the plea, the sum and the judgment.
    [[nodiscard]] const HearingState& hearing() const noexcept;
    /// THE PLEA. I DID IT (draw-free, never doubled, never spared) or I DID
    /// NOT (the arrest draw's band, spared at the top, doubled below); a
    /// commuted man before a rope bench gets only I HAVE NOTHING TO SAY. The
    /// priest's weighing comes back line by line (justice.hpp) and is written
    /// on the hearing. A plea is a haggle with your neck on the table: it
    /// trains streetwise, win or lose. Refused -- `heard` false, nothing
    /// changed -- when no hearing awaits one.
    Arraignment plead(Plea plea);
    /// THE ROPE was passed and carried out. The one true game over. While it
    /// is set the room refuses what a corpse cannot do: no swing is armed, no
    /// blow lands on the body, no defeat is applied and nothing revives
    /// (SENTENCES LANE -- the gates on playerAttackDown, injurePlayer,
    /// applyDefeat, reviveAfterDefeat and tickWatch). The plate over it is
    /// presentation's.
    [[nodiscard]] bool executed() const noexcept;

    // --- SENTENCES LANE: what a judgment does ------------------------------
    //
    // THE SENTENCE IS THE ROOM'S TO SERVE, because the room owns the purse,
    // the clock, the standings and the body; the record is the ledger's
    // (CrimeLedger::sentence), written AFTER the skip so the heat it leaves is
    // not cooled to nothing. Jail is the shipped skipHours widened through
    // systems that exist -- see justice.hpp's sentence section. Turned loose,
    // the body is somebody else's to move: the SAME takeArrestRelease() the
    // arrest fires, fired again -- to the Mission's door for SPARED and FINED
    // (SentenceTerms::releaseHere), the shipped Tarwalk for everything that
    // cost a cell. THE ROPE fires no release at all: executed() is read
    // beside the two release flags, and shares a line with neither.

    /// What the last serveSentence() call did. `served` is false until there
    /// has been one, and false again after a refused call (no judged hearing,
    /// or the rope already passed): the report is the last call's answer.
    struct SentenceReport {
        bool served = false;
        /// The integers, exactly as sentenceTerms() computed them off the
        /// hearing and the purse it was paid from.
        SentenceTerms terms;
        /// The day and the clock face the sentence ended on. The Tarwalk's
        /// line prints them: "TURNED LOOSE ON THE TARWALK. DAY 4. 23:40."
        std::int32_t dayReleased = 0;
        std::int32_t timeReleased = 0;
        /// The purse before and after.
        std::int32_t coinBefore = 0;
        std::int32_t coinAfter = 0;
    };

    /// THE SENTENCE. Serves the judged hearing: the fine out of the purse, the
    /// clock forward by the cell and the yard (skipHours, the shipped jump --
    /// heat cools through it, every taken job past its night dies at the next
    /// refresh, the days count), the roofs' and the Flame's regard moved, the
    /// body mended over a day or more, then the ledger's own half
    /// (CrimeLedger::sentence) and the release. THE ROPE: no clock, no coin,
    /// no release -- executed_ is set, the run's end is written (runEnd) and
    /// the room refuses the world from then on. A stepped input: refused --
    /// `served` false, nothing changed -- when no hearing has been judged or
    /// the rope has already passed. Whoever owns the ward's calendar runs it
    /// to dayNumber() afterwards, exactly as after any other skip.
    const SentenceReport& serveSentence();
    /// The last serveSentence() call's answer, whether or not it has been read.
    [[nodiscard]] const SentenceReport& lastServed() const noexcept { return lastServed_; }
    /// The end of the run, once THE ROPE has been served: the place, the
    /// reason and the dateline the plate reads. `ended` is false while the
    /// player lives. runEnded() is executed() by another name, for the seam
    /// beside quitRequested_ to read.
    [[nodiscard]] const RunEnd& runEnd() const noexcept { return runEnd_; }
    [[nodiscard]] bool runEnded() const noexcept { return executed(); }
    /// HEARING PAGE LANE. Who the blood on the paper is FOR: the corpse on
    /// the roster (Activity::Dead, never removed) whose memory carries
    /// Deed::Slew -- the last such man in roster order, exactly the rule the
    /// rope's own RunEnd reads at the drop, so the reading of the charge
    /// ("THE WARD SAYS YOU PUT CANNIC DOWN...") and the plate ("FOR CANNIC.")
    /// can never name two different men. Empty when no corpse of the
    /// player's is on the roster. Pure, no draw, reads hashed state only.
    [[nodiscard]] std::string slainName() const;
    /// STREET SENSES leg (b). Whether the corpse the blood is for lies on the
    /// STREET (a ward body, Deed::Slew under kWardSpeakerIdBase) rather than
    /// on this room's boards -- so the reading says where. Pure, no draw.
    [[nodiscard]] bool slainOnStreet() const noexcept;

    /// Which day of the world this is. Monotonic across midnight and across a
    /// night in a cell, because a deadline that wrapped with the wall clock
    /// would be a deadline nobody could miss.
    [[nodiscard]] std::int32_t dayNumber() const noexcept;

    /// Moves the clock on by whole hours, days included. What a sentence does.
    void skipHours(std::int32_t hours);

    /// Takes the bale in the snug, or puts it back down. The roofs do not hand
    /// a bale to a stranger, so it wants membership; carrying it OUT of the
    /// house past somebody who would mind is the run, and the room notices that
    /// on its own -- see stepMovement.
    StealResult handleBale();

    /// Everybody present who could see it remembers that they saw it. This is
    /// what makes a robbery in a full taproom different from one in an empty
    /// one, and it is what moves the ward's own opinion.
    void spreadWitness(std::int32_t victimId, Deed deed);

    /// How many present actors, other than `exceptId`, can actually see the
    /// player right now. Same range and same sight rule spreadWitness uses,
    /// exposed because a CRIME needs the count rather than the side effect:
    /// heat is what the Watch heard, and nobody heard an empty room.
    [[nodiscard]] std::int32_t witnessCount(std::int32_t exceptId) const noexcept;
    /// How far across a room a deed carries, in tiles.
    static constexpr std::int32_t kWitnessRangeTiles = 8;
    /// Inside this, you do not need a sight line: you are in arm's reach and
    /// there is nothing between you but air.
    ///
    /// This is not a softening of the line-of-sight rule, it is what keeps the
    /// rule from being absurd. The Gull's bar counter is authored as SOLID
    /// masonry -- correctly, a body cannot walk through it -- so a strict ray
    /// makes the bartender blind to the hand in her own till, one tile away
    /// across her own bar. A counter is waist high; a wall is not; the tile
    /// lanes cannot tell them apart, and two tiles of grace is the honest way
    /// to say so until they can.
    static constexpr std::int32_t kWitnessReachTiles = 2;

    /// What the priest of the Flame will teach a student at this level of
    /// LINKCRAFT -- the skill the eleven authored spells are actually cast
    /// with -- straight out of the raws. Empty when he is not in the room or
    /// the raws were not found.
    [[nodiscard]] std::vector<const Spell*> priestTeaches(std::int32_t linkcraftLevel) const;
    /// ONE spell shelf, owned by the dialogue layer. The tavern used to load a
    /// second copy of the same eleven rows; two loaders reading one file is two
    /// chances to disagree about it.
    [[nodiscard]] const Spellbook& spellbook() const noexcept { return dialogue_.spellbook(); }

    // --- the guilds ---------------------------------------------------------

    /// Which faction claims an actor, or -1. DERIVED, not tabulated: the room
    /// knows the actor's job family, and the owner's own factions.json says
    /// which faction claims that family's jobs. Nobody wrote a second table and
    /// so nobody can let one drift.
    [[nodiscard]] std::int32_t factionOf(const Actor& actor) const noexcept;

    /// How many people in this room right now belong to a faction that is the
    /// declared rival of one the player holds rank in.
    ///
    /// THIS IS THE "ENEMY PRESENCE" HALF of the faction requirement, and it is
    /// a count of BODIES rather than a mood: a Watch runner who walks into the
    /// Gull at midnight is standing in a room with a Skyrunner in it, and the
    /// Skyrunner knows what the arm-band means.
    ///
    /// VERIFICATION GAP (S4): the COUNT has no consumer outside the test suite.
    /// What the room actually does with a rival present is seed them hostile
    /// (applyRivalHostility), which is real and proved; a number that says how
    /// many is a readout waiting for the thing that reads it.
    [[nodiscard]] std::int32_t enemyPresence() const noexcept;

    /// Seeds every present rival's opinion of the player to HOSTILE. Called
    /// whenever the player's standing on a ladder changes, because that is the
    /// moment the room learns whose side they are on.
    ///
    /// VERIFICATION GAP (S4): it fires on a RANK CHANGE and not on arrival. A
    /// rival who walks in an hour after you signed the Watch's roll greets you
    /// as a stranger until something else moves your standing. The right fix is
    /// a check when an actor becomes present, and it wants a schedule hook this
    /// room does not have yet.
    void applyRivalHostility();

    /// How many seconds of rope the house gives a warned player RIGHT NOW.
    ///
    /// THIS IS THE OTHER HALF, and it is the one that is visible from inside
    /// this room: when the Watch's influence over the ward falls and the roofs'
    /// rises, the houses stop waiting for the Watch and handle it themselves.
    /// A guild's weight in the district is not a number on a sheet -- it is how
    /// long a bouncer lets you finish your drink.
    [[nodiscard]] std::int32_t graceSecondsForPlayer() const noexcept;

    // --- S8: the nemesis ------------------------------------------------------
    //
    // KILLING THE PLAYER IS A PROMOTION EVENT. See nemesis.hpp for the whole
    // design; what lives here is the four places the room touches it -- who
    // landed the blow, what a win does to the roster, what a win does to a
    // price, and putting the player back on their feet afterwards.

    [[nodiscard]] const NemesisBook& nemesis() const noexcept { return nemesis_; }

    /// Joins the room to the ward's own roll, so a risen rival can take a
    /// vacant charge on it. May be null and usually is in a synthetic test:
    /// a rise that cannot reach the roll still ranks, still founds a house and
    /// still remembers -- see NemesisBook::recordDefeat.
    ///
    /// THIS IS ALSO THE S7 REVIEW'S EIGHTH FINDING, ANSWERED. S7 built 3,303
    /// lines of ward economy that the windowed client never constructed. It
    /// constructs one now, and the first thing that reaches into it is the man
    /// who put the player on the floor.
    void attachRoll(Ward* roll) noexcept { roll_ = roll; }
    [[nodiscard]] const Ward* roll() const noexcept { return roll_; }

    /// RADIANT BUILD. Joins the room to the district's own people, so the
    /// radiant board can be posted off the live roster -- attachRoll's exact
    /// shape, for TASK #81's exact gap: a generator nothing constructs is a
    /// generator nobody can reach. Borrowed, never owned; may be null and
    /// usually is in a synthetic test, where the board simply stays empty and
    /// every conversation is exactly what it was before this build.
    ///
    /// Posts today's errands at the moment of attach and again at every day
    /// turn (advanceSecond, beside postContracts), so a session that opens at
    /// ten at night opens on a board that has been up since the doors did.
    void attachPeople(const WardPopulation* people);
    [[nodiscard]] const WardPopulation* people() const noexcept { return wardPeople_; }

    /// What the last defeat did. `happened` is false until there has been one.
    [[nodiscard]] const Rise& lastDefeat() const noexcept { return lastDefeat_; }

    /// True once, after the player has been put down, so whoever owns the body
    /// can put it back on the street. Read and CLEARED -- the same shape the
    /// arrest release has, and for the same reason.
    [[nodiscard]] bool takeDefeatRelease() noexcept;

    /// The player comes to. Moves the clock on by kBlackoutHours, gives the hit
    /// points back and clears the floor; the CALLER moves the body, because the
    /// room never holds a pointer to it.
    ///
    /// It does NOT clear anything the rival gained. That is the whole design.
    void reviveAfterDefeat();

    /// Puts the player on the floor at somebody's hands, with no fight around
    /// it. The DEFEAT SEAM: the same applyDefeat a real in-world beating routes
    /// through, exposed so a scripted proof can drive the rise through exactly
    /// the code the game uses. (There is no combat screen -- lethal fights
    /// resolve in the world now; this is a direct-defeat convenience, not a
    /// venue.)
    void concedeTo(std::int32_t actorId);

    // --- trouble ------------------------------------------------------------

    [[nodiscard]] Standing playerStanding() const noexcept { return standing_; }
    /// The player's hit points as this room has been keeping them. Floors at
    /// kPlayerBrawlFloor -- see the constant.
    [[nodiscard]] std::int32_t playerHp() const noexcept { return playerHp_; }
    /// And the ceiling those points heal back to. Exposed for the chargen
    /// seam below, and for anything that wants to draw "hp of hpMax".
    [[nodiscard]] std::int32_t playerHpMax() const noexcept { return playerHpMax_; }
    /// The chargen seam: sets the player's hit points and ceiling outright,
    /// hpMax floored at 1 and hp clamped into [0, hpMax] -- the biography's
    /// hpMax lever and the custom path's Stout/Tender land through this, once,
    /// at boot, over the base the fields themselves declare. Not for
    /// gameplay: fights go through injurePlayer/reviveAfterDefeat, exactly as
    /// setPlayerCoin sits beside the purse the barter verbs move.
    void setPlayerHealth(std::int32_t hp, std::int32_t hpMax) noexcept {
        playerHpMax_ = hpMax < 1 ? 1 : hpMax;
        playerHp_ = hp < 0 ? 0 : (hp > playerHpMax_ ? playerHpMax_ : hp);
    }
    /// Hurts the player by `amount`, floored exactly the way a brawl is. S5's
    /// one caller is a landing off a roof that was higher than the legs allow,
    /// and it lives here because this is where the player's hit points live.
    void injurePlayer(std::int32_t amount);

    // --- the sheet and the wind (fatigue build) ------------------------------
    //
    // ATTRIBUTES GET THEIR RUNTIME READER HERE, closing the seam #84 left
    // "deliberately uncrossed": the room holds the AttributeBlock chargen
    // built, and every reader of it (MGT into the punch, AGI into the legs
    // and the climb costs, VIG into this pool, WIT into the cast) is exactly
    // neutral at the base-40 sheet -- see fatigue.hpp. The pool itself is the
    // reference doc's one-bar-everything economy, adapted; its own header
    // carries the mapping.

    /// The boot seam, the same shape setPlayerHealth is: sets the sheet and
    /// REFILLS the pool to the max the sheet derives. Not for gameplay.
    void setPlayerAttributes(const AttributeBlock& attributes) noexcept;
    [[nodiscard]] const AttributeBlock& playerAttributes() const noexcept {
        return playerAttributes_;
    }
    [[nodiscard]] const PlayerFatigue& playerFatigue() const noexcept { return fatigue_; }
    /// True while the pool is empty and has not yet recovered -- what the
    /// sprint gate and the climb verbs read. See PlayerFatigue::winded.
    [[nodiscard]] bool playerWinded() const noexcept { return fatigue_.winded(); }
    /// One movement step of the pool's own economy: the sprint drain when the
    /// legs are flat out, regen otherwise (halved while moving). Called by
    /// whoever owns the step loop, right beside setPlayerMotion, with the
    /// SAME flags -- the pool and the stealth model must never disagree about
    /// what the legs were doing.
    void stepPlayerFatigue(bool moving, bool sprinting) noexcept;
    /// The verbs' own drains, AGI- and skyrunning-scaled (fatigue.hpp).
    /// Charged by the caller that owns the verb, on success only -- a refused
    /// climb costs nothing, exactly as it costs no time.
    void chargePlayerJump() noexcept;
    void chargePlayerMantle() noexcept;
    void chargePlayerLeap() noexcept;
    /// True once a brawl has taken the player to the floor.
    [[nodiscard]] bool playerFloored() const noexcept { return playerFloored_; }
    [[nodiscard]] std::int32_t warningsGiven() const noexcept { return warningsGiven_; }
    [[nodiscard]] std::int32_t timesEjected() const noexcept { return timesEjected_; }
    /// What the bouncer said, or empty. Cleared when a new one is issued.
    [[nodiscard]] const std::string& lastWarning() const noexcept { return lastWarning_; }
    /// The bouncer currently dealing with the player, or nullptr.
    [[nodiscard]] const Actor* respondingBouncer() const noexcept;

    /// Tells the house the player did something. Idempotent within a second.
    void reportOffence(Offence offence);

    /// LEGACY. The player throws a tap-punch at the nearest body (radial, with
    /// the rat preference). Resolves IN WORLD when classifyFight says brawl, and
    /// still REFUSES -- raising escalation() -- when it says lethal, the
    /// pre-veto behaviour kept for the un-migrated call sites. The action-combat
    /// player-swing path is playerAttackUp(), which casts the sightline and
    /// resolves lethal in the world.
    struct PunchResult {
        bool swung = false;
        FightClass fight = FightClass::Brawl;
        Blow blow;
        std::int32_t targetId = -1;
        std::string targetName;
    };
    /// LEGACY TAP, kept for the call sites that have not migrated to the
    /// attackDown/attackUp state machine (Session::punch and the scripted
    /// arcs). Radial-nearest with the rat-vs-person preference, and it REFUSES
    /// to resolve a lethal fight -- the pre-veto model. The action-combat
    /// player-swing path is playerAttackUp() below, which casts the sightline,
    /// retires the species preference, and resolves lethal in the world; when
    /// the presentation lane repoints Session::punch() to a tap of the new
    /// verbs this becomes the last reader of the old shape.
    PunchResult playerPunchNearest();

    // --- the swing (Attack, ACTION-COMBAT BUILD) -----------------------------
    //
    // The state machine of COMBAT-ACTION-SPEC.md section 1.2, sim-owned so the
    // charge tier is deterministic by construction. INPUT reports the two edges
    // (down starts the hold clock, release resolves) and the SIM owns the step
    // counter, the sightline, the intent-by-verb, the wind, and the rules.

    /// What one press-to-release of Attack resolved. `swung` is a committed
    /// swing (the wind was paid), `refused` a hard swing the wind would not
    /// buy (a winded tap still swings), `hard` the tier, `killed` a blow that
    /// took the target to death under lethal rules. `fight` is the class the
    /// blow resolved under; `blow`/`targetId`/`targetName` the hit itself.
    struct PlayerSwingResult {
        bool swung = false;
        bool hard = false;
        bool refused = false;
        bool killed = false;
        /// STREET SENSES leg (b): the swing landed on the OTHER roster
        /// (playerAttackUpStreet) -- targetId is a ward id, not a roster id.
        bool street = false;
        /// And the class the blow resolved under, for the caller that applies
        /// it to the street body (Lethal kills; Brawl floors).
        bool lethal = false;
        /// WATCH & RHYTHM BUILD: the target's guard caught it (blow.blocked),
        /// and what that cost -- a normal swing recoiled the player's arm, a
        /// hard swing broke the guard and staggered the man; a hit inside his
        /// wind-up or any hard hit staggers him too.
        bool recoiled = false;
        bool staggered = false;
        FightClass fight = FightClass::Brawl;
        Blow blow;
        std::int32_t targetId = -1;
        std::string targetName;
    };
    /// Attack down-edge: from IDLE, start charging (the hold clock is the sim's
    /// own step counter). A down-edge during CHARGING or RECOVERY is dropped --
    /// no queue-buffered instant hards. Cancelled with no cost by a cast, a
    /// page, talking or picking (the hand does one thing) via cancelPlayerCharge.
    void playerAttackDown() noexcept;
    /// Attack release-edge: resolve the swing. HARD iff the charge reached
    /// kHardSwingHoldSteps; the sightline raycast picks the target (first body
    /// on the look-ray, VETO 1); strike() runs with the tier's chargeQ8; the
    /// first hard swing in a fight sets Intent::Harm (intent-by-verb); the
    /// class is computed and lethal blows kill (Activity::Dead), floors lifted,
    /// murder heat and the Condemned hook fire on a witnessed kill; the wind is
    /// paid. A hard swing while winded is REFUSED (a refusal in the result, no
    /// cost). Returns to RECOVERY. A release outside CHARGING is a no-op result.
    PlayerSwingResult playerAttackUp();
    /// STREET SENSES leg (b). THE SWING, ON A STREET BODY -- ONE RULE, TWO
    /// ROSTERS. The client cast the street's ray with the same integer
    /// projection (WardPopulation::sightlineTarget), found a body nearer along
    /// it than anybody on this roster (playerSightlineAlong), and hands in its
    /// sheet as a Fighter (kActorHealth, Fists, Subdue) with the witnesses the
    /// street counted (WardPopulation::witnessesInSight, before the blow). The
    /// head of the swing is playerAttackUp's exactly (armPlayerSwing: the
    /// charge tier, the winded refusal, the recovery, the wind), the ONE roll
    /// is drawForPlayerAction at the same stream position the roster swing
    /// would have spent it, strike() resolves it on the sheet, classifyFight
    /// with this fighter in the fight picks the rules (the Subdue floor and
    /// the Lethal rules unchanged), intent-by-verb applies, the deed lands on
    /// the social ledger under kWardSpeakerIdBase + wardId, the offence is
    /// reported, and a KILLING (lethal, downed, not crowned) goes through the
    /// murder law: Deed::Slew, and CrimeLedger::markMurderer(witnesses) when
    /// anybody saw. `sheet.hp` comes back as what strike() left; the caller
    /// applies it to the WardActor (WardPopulation::applyStreetBlow). The
    /// tavern roster is never touched -- one person, one roster. `targetId`
    /// in the result is the WARD id and `street` is set.
    PlayerSwingResult playerAttackUpStreet(Fighter& sheet, std::int32_t wardId,
                                           std::string_view name, std::int32_t witnesses);
    /// STREET SENSES leg (b). The `along` (Q8, down the look-ray) of the body
    /// on the line on THIS roster, or -1 for an empty line -- so the client can
    /// pick the nearer of the Gull's target and the street's. Draw-free.
    [[nodiscard]] std::int64_t playerSightlineAlong() const noexcept;
    /// STREET SENSES leg (b). A BLOW THROWN AT THE PLAYER by a street body,
    /// landed on the player's sheet through the Gull's own player-side rules
    /// (stepBrawl's landing, in one place): strike() with the thrower's own
    /// roll, the HARD band carved off it (kNpcHardBand256, the same bits an
    /// NPC swing reads), the held guard softening it (blockedDamage, the
    /// shieldwall use, the catch wind, a hard swing breaking the guard), the
    /// floor by class (classifyFight over the player and this fighter -- a
    /// fists-and-Subdue sailor is a brawl, the player's own steel or intent
    /// flips it), the hands up, and the defeat seam (applyDefeat with no
    /// roster winner: floored, the quay revive, NO rise -- a street winner's
    /// nemesis rise is ceiling, the book keys on the roster). The client has
    /// already checked reach. Returns whether it landed.
    bool takeStreetBlow(const Fighter& attacker, std::uint64_t roll);
    /// Drops a charge with no swing and no cost -- what a cast, a page, a
    /// conversation or a pick does to a raised hand. IDLE and RECOVERY are
    /// untouched. Idempotent.
    void cancelPlayerCharge() noexcept;
    /// One movement step of the swing machine: increments the charge while
    /// CHARGING, counts the recovery lockout down while RECOVERING, and
    /// recomputes the live sightline-target flag every step so the reticle can
    /// read it. Driven from stepMovement, once per movement step.
    void stepPlayerCombat() noexcept;

    /// True while the hand is free to start a swing (IDLE) -- not charging, not
    /// in the recovery lockout.
    [[nodiscard]] bool playerCombatIdle() const noexcept {
        return combatState_ == PlayerCombatState::Idle;
    }
    /// Steps the current charge has held, 0 when not charging. The client draws
    /// the reticle retracting across this toward kHardSwingHoldSteps.
    [[nodiscard]] std::int32_t playerChargeSteps() const noexcept { return chargeSteps_; }
    /// True while charging AND the hold has reached the hard threshold -- what
    /// the reticle's warm accent and the HELD HARD row read.
    [[nodiscard]] bool playerChargeHard() const noexcept {
        return combatState_ == PlayerCombatState::Charging && chargeSteps_ >= kHardSwingHoldSteps;
    }
    /// Whether a body sits on the look-ray right now (recomputed each step).
    /// The reticle brightens on it; a swing thrown with it false hits nobody.
    [[nodiscard]] bool playerSightlineTarget() const noexcept { return sightlineFlag_; }
    /// What the hand is holding. A thin accessor beside playerWeapon(), named
    /// as the cross-lane contract names it.
    [[nodiscard]] Weapon playerHeldWeapon() const noexcept { return playerWeapon_; }

    /// DEFERENCE (VETO / section 4.4). Whether the player is PRESENTING as a
    /// Wielder right now. When true the Watch never goes hostile to him -- no
    /// arrest-close, no joining a fight against him. Not reachable in current
    /// play (the player has no way to present as Wielder yet), so it lives as a
    /// settable sim rule with a test, honestly, against the day the Persona
    /// seam sets it. Hashed, since it changes what the Watch does.
    void setPlayerPresentsAsWielder(bool presents) noexcept {
        playerPresentsAsWielder_ = presents;
    }
    [[nodiscard]] bool playerPresentsAsWielder() const noexcept {
        return playerPresentsAsWielder_;
    }

    /// Latched to Lethal at the moment a fight crosses the brawl line. There is
    /// no combat screen to route to -- the fight keeps resolving in the world
    /// under lethal rules -- so the client reads this for the flip's feedback
    /// (the SwordDraw, the "STEEL OUT" line) rather than for a transition. The
    /// escalation is a once-per-fight social latch, cleared by clearEscalation.
    [[nodiscard]] FightClass escalation() const noexcept { return escalation_; }
    [[nodiscard]] bool escalated() const noexcept { return escalation_ == FightClass::Lethal; }
    void clearEscalation() noexcept {
        escalation_ = FightClass::Brawl;
        escalationSeen_ = false;
    }

    /// The fight as the rule sees it right now: the player plus everybody
    /// currently swinging.
    [[nodiscard]] std::vector<Fighter> currentFight() const;

    // --- the guard (Block, held) ---------------------------------------------

    /// Down is blocking, up is not. Pushed by the client every frame the Block
    /// key changes, the same seam setPlayerMotion is. SIM STATE, hashed: a
    /// held guard changes what every landed blow in tickBrawl costs, so two
    /// runs that disagreed about it would be two different fights.
    /// STANCE & ROOM BUILD: a guard going down puts the hands UP (fists up
    /// without violence -- rule 2 of the raise table); a guard coming back up
    /// does NOT lower them (rule 4 of the lower table).
    void setPlayerBlocking(bool blocking) noexcept {
        playerBlocking_ = blocking;
        if (blocking) {
            raisePlayerHands();
        }
    }
    [[nodiscard]] bool playerBlocking() const noexcept { return playerBlocking_; }
    /// Every blow a guard has ever softened. MONOTONIC on purpose: the client
    /// pulses on the increase, so the sim never keeps read-and-clear feedback
    /// state the way takeDefeatRelease has to.
    [[nodiscard]] std::int32_t blowsBlocked() const noexcept { return blowsBlocked_; }

    // --- the stance (FIGHTING MODE, STANCE & ROOM BUILD) --------------------
    //
    // The owner's sentence: "When I press LMB I want to enter fighting mode and
    // hit whoever is in front of me." A SIM STATE, not a client flag
    // (oblivion-roadmap.md section 3.2): hashed beside the swing machine, read
    // by the HUD, by the reaction tiers and by the Watch, and by nothing that
    // draws a roll. The stance and its timer draw nothing.
    //
    // HANDS COME UP when: (1) Attack goes down from IDLE -- the same press
    // charges, so a tap raises and swings and a hold raises and swings hard,
    // ONE press; (2) the guard goes down (setPlayerBlocking(true)); (3) an
    // NPC blow lands on the player in stepBrawl -- without this a player being
    // punched would have to press SWING before GUARD meant anything. Every one
    // of those also refills the lull timer, as does a swing thrown.
    //
    // HANDS GO DOWN when: (1) USE with nothing in reach -- the client's LOWER
    // HANDS slot of the interact walk calls lowerPlayerHands(); (2) the lull:
    // kLowerHandsSteps in IDLE with the guard not held and brawlers_ empty;
    // (3) the cancel list -- talking, picking, sleeping (any clock skip), an
    // arrest, a defeat -- lowers them here in the sim, and the client lowers
    // them for any page it opens; (4) a guard RELEASE does not lower them.

    /// True while the hands are up -- fighting mode.
    [[nodiscard]] bool playerHandsUp() const noexcept { return handsUp_; }
    /// Steps of lull left before the hands come down on their own; 0 with the
    /// hands down. Refilled to kLowerHandsSteps by every raise.
    [[nodiscard]] std::int32_t playerLowerTimer() const noexcept { return lowerTimer_; }
    /// Hands down, now. The LOWER HANDS verb, and the cancel list. Idempotent;
    /// touches nothing else -- a live charge is cancelPlayerCharge's business.
    void lowerPlayerHands() noexcept;

    // --- the rhythm (WATCH & RHYTHM BUILD) ------------------------------------
    //
    // The player's two stagger clocks, hashed, integer, drawing nothing. While
    // either runs an Attack down-edge is DROPPED (as in recovery); while the
    // block-stagger runs the held guard is not honoured either -- the guard is
    // broken, which is what a hard swing is for. See the kRecoilSteps and
    // kBlockStaggerSteps headers for the asymmetry.
    [[nodiscard]] std::int32_t playerRecoilSteps() const noexcept { return recoilSteps_; }
    [[nodiscard]] std::int32_t playerBlockStaggerSteps() const noexcept {
        return blockStaggerSteps_;
    }
    /// True while a lethal-class fight with the player in it is live: somebody
    /// is on the brawl list and the classifier says Lethal. The one predicate
    /// the bouncers (refuse steel), the rota (the room stands back), the rout
    /// and the Watch's violence cause all read, so no caller re-derives it.
    /// Draw-free.
    [[nodiscard]] bool lethalFightLive() const noexcept;
    /// What a watchman with sight would have cause to halt: a live lethal
    /// fight, steel up in the player's hands, or the player's hands up over a
    /// corpse within reach. NEVER a brawl-class fist fight, and never with the
    /// hands down over a body somebody else left. Draw-free; the sight test is
    /// the caller's (tickWatch asks canSeePlayer first).
    [[nodiscard]] bool violenceInView() const noexcept;
    /// Whether this actor is a professional the rhythm rules treat apart: a
    /// bouncer, or a watchman by the derived faction. Professionals keep the
    /// wider guard band, never stand back, and never rout.
    [[nodiscard]] bool isProfessional(const Actor& actor) const noexcept;

    // --- the cast (Cast, one press) ------------------------------------------

    /// What one press of the Cast key did, or why it did nothing. `line` is
    /// always set -- menu furniture for the alert row, refusal or success --
    /// because a key that can silently do nothing is a key the player reads
    /// as broken.
    struct CastResult {
        bool cast = false;
        std::string line;
        /// Mirror of PunchResult::fight: the class a harmful link resolved
        /// under. This legacy touch-cast still refuses when steel is out (the
        /// player's lethal vector is the swing); the flip is the room standing
        /// back, not a screen taking over.
        FightClass fight = FightClass::Brawl;
        std::int32_t targetId = -1;
    };
    /// Casts the equipped crafting. Refuses, in order and out loud: an empty
    /// grimoire, a link still cooling, an axis or target nothing can hold yet
    /// (temperature, another body's tuning, a forged tuning with no named
    /// string -- see the VERIFICATION GAP (S15) in the .cpp), the unbridged
    /// link, an empty sightline. A touch bridges the FIRST BODY ON THE
    /// LOOK-RAY (sightlineTarget -- VETO 1's one targeting rule, shared with
    /// the swing: two verbs, one rule), person or rat, whoever the crosshair
    /// passes through first. A harmful touch on a person carries a punch's
    /// own consequences -- the brawl list, the ledger, the offence -- because
    /// a scald is an assault whatever the hand was holding; a rat on the line
    /// is touched in the silence a fist keeps for vermin. The cast check is
    /// linkcraft against spellDifficulty: skill raises the odds and never
    /// buys certainty. A WHILE_ACTIVE self tuning that opens lands as a live
    /// row on heldEffects() -- recast refreshes it whole -- and is felt
    /// through effectiveAttributes() until its clock runs out.
    CastResult playerCastEquipped();

    // --- the held craftings (HELD-EFFECTS BUILD) ------------------------------
    //
    // WHAT S13 LEFT REFUSING, MADE REAL FOR THE ONE AXIS SOMETHING READS.
    // A WHILE_ACTIVE component is a live row on the player: laid by a cast,
    // refreshed by a recast of the same crafting, lapsing on the room's own
    // clock. ATTRIBUTE rows are the whole point of sequencing this after the
    // fatigue build -- the temporary delta flows through the exact runtime
    // readers that build just crossed (MGT into the punch and the pool, AGI
    // into the gait and the climb costs, VIG into the pool, WIT into the cast
    // and its recovery) via effectiveAttributes(), so one crafting is felt
    // everywhere an attribute already is. TEMPERATURE rows still refuse:
    // nothing in the live sim reads heat on a body (the baked map's
    // temperature lane is bake-time data), and holding a row nothing reads
    // would be a success toast over a no-op -- see playerCastEquipped().

    /// One live hold. INTEGER STATE, hashed -- see hash_into's declared note.
    struct ActiveHold {
        /// The crafting that laid it -- the refresh/replace key, and how the
        /// HUD finds its display name in the grimoire.
        std::string spellId;
        /// The string it tunes.
        AttributeId attribute = AttributeId::Might;
        /// Signed, the component's own.
        std::int32_t magnitude = 0;
        /// The elapsed_ tick it lapses on -- the same absolute-clock shape
        /// castCoolUntil_ uses, so a night asleep (skipTo) runs it out
        /// honestly instead of pausing it.
        std::int64_t expiresAt = 0;
    };
    /// Every live hold, in the order they were laid (refresh keeps a
    /// crafting's place). Deterministic: casts are the only writer.
    [[nodiscard]] const std::vector<ActiveHold>& heldEffects() const noexcept {
        return heldEffects_;
    }
    /// Seconds of room time this hold has left. 0 means it lapses this second.
    [[nodiscard]] std::int64_t holdSecondsLeft(const ActiveHold& hold) const noexcept {
        return hold.expiresAt > elapsed_ ? hold.expiresAt - elapsed_ : 0;
    }
    /// THE SHEET EVERY RUNTIME READER ACTUALLY READS: the chargen base plus
    /// every live tuning, the per-attribute total clamped to spellforge's
    /// +/-kAttributeModifierLimit ("held to +/-2 however many rows stack" --
    /// spells.json's own words) and then to the attribute floor/ceiling.
    /// playerAttributes() stays the BASE sheet; this is the derived read, so
    /// a lapsed hold restores yesterday's numbers exactly.
    [[nodiscard]] AttributeBlock effectiveAttributes() const noexcept;

    /// The crafting the next cast will spend, or nullptr with an empty
    /// grimoire. When nothing has been picked the FIRST known crafting is the
    /// default -- grimoire order, ascending id -- so learning your first spell
    /// readies it without a menu trip.
    [[nodiscard]] const Spell* equippedSpell() const noexcept;
    /// Equips the INDEXth known crafting, grimoire order. False when there is
    /// no such row; the equipped id is untouched.
    bool equipSpellAt(std::int32_t index);
    /// Seconds of room time before the next cast may open a link. 0 is ready.
    [[nodiscard]] std::int64_t castCooldownLeft() const noexcept {
        return castCoolUntil_ > elapsed_ ? castCoolUntil_ - elapsed_ : 0;
    }

    // --- the quick bar (slots 1-0, SPELLS BUILD) -----------------------------

    /// Ten slots off the number row, matching render::Action::QuickSlot1-0.
    static constexpr std::int32_t kQuickSlotCount = 10;
    /// Binds a KNOWN crafting to one slot. False when the slot is out of
    /// range or the grimoire does not know the id -- a slot can never hold a
    /// crafting the hand could not equip. Kept as the ID, not a grimoire
    /// index, for the identical reason equippedSpellId_ is: the grimoire
    /// inserts in id order and learning something new must never silently
    /// re-point a slot.
    bool bindSpellToSlot(std::int32_t slot, std::string_view spellId);
    /// The crafting a slot holds, or nullptr for an empty slot.
    [[nodiscard]] const Spell* slotSpell(std::int32_t slot) const noexcept;
    /// Empties one slot. False when the slot is out of range.
    bool clearSlot(std::int32_t slot);
    /// Equips whatever the slot holds, THROUGH equipSpellAt (grimoire order)
    /// so the two doors into the hand share one lock. False for an empty
    /// slot; the equipped id is untouched.
    bool equipSlot(std::int32_t slot);

private:
    void buildRoster();
    void applySchedules();
    void tickBouncers();
    void tickWatch();
    void tickVermin();
    /// The watchman who can see the player right now, or nullptr.
    [[nodiscard]] Actor* watchmanWatchingPlayer() noexcept;
    /// True when this actor can see the player: present, same band, in range,
    /// and with a line to them. The same rule witnessCount uses, asked about
    /// one person.
    [[nodiscard]] bool canSeePlayer(const Actor& actor) const noexcept;
    /// The Watch puts its hands on you. One call site.
    void applyArrest(Actor& officer);
    /// The nearest downed vermin within reach, or nullptr.
    [[nodiscard]] Actor* downedVerminInReach() noexcept;
    /// The nearest rat on its feet within reach, or nullptr.
    [[nodiscard]] const Actor* nearestVerminTo(std::int32_t xQ8, std::int32_t yQ8,
                                               std::int32_t reachQ8) const noexcept;
    /// THE SIGHTLINE RAYCAST (VETO 1). The first body the crosshair passes
    /// through: over every present body on its feet -- person OR rat, no
    /// species preference -- project the offset onto the look-ray via
    /// forward_x_q16/forward_y_q16(playerYaw_); a body is on the line iff
    /// 0 < along <= kMeleeReach and |perp| <= kBodyHalfWidth, and the target is
    /// the on-line body with the smallest `along`. Draw-free integer, the S9
    /// no-rolls law applied to targeting. Ties on `along` break on the lower id
    /// so two runs cannot disagree. nullptr for an empty line. ONE RULE, TWO
    /// VERBS: the swing (playerAttackUp) and the touch-cast
    /// (playerCastEquipped) both target through here and nowhere else.
    [[nodiscard]] const Actor* sightlineTarget() const noexcept;
    /// The actor id of the first rat. Roster order: every person, then every
    /// rat, so this plus an index is a rat's id and the bitmask has a home.
    [[nodiscard]] std::int32_t verminFirstId() const noexcept;
    /// What tonight's boat brought. Drawn once a night, so both bales in the
    /// snug are the same cargo -- because they came off the same hull.
    /// What the boat lands tonight, one good per bale.
    ///
    /// NOT UNIFORM, AND THAT IS THE POINT. A smuggler's boat is not a lottery:
    /// it lands what somebody on the ward has already paid to have landed. So
    /// each bale's good is drawn from the goods TONIGHT'S OWN BOARD asks for,
    /// restricted to the three a hull actually carries, and only falls back to
    /// a flat draw over those three when the board wants none of them. That is
    /// what makes the nightly work completable without making it free: the
    /// player still has to carry it out past a watchman whose eye is on the
    /// WEIGHT of the sack, and three bales of quayfire is the most conspicuous
    /// load in the game.
    void drawBaleGoods() noexcept;
    /// The authored skill level of a roster body, without building a Speaker.
    [[nodiscard]] std::int32_t rosterSkillOf(const Actor& actor) const noexcept;
    void tickBrawl();
    /// ACTION-COMBAT BUILD. The per-step NPC swing cadence (section 1.5),
    /// moved out of tickBrawl's 1 Hz exchange: per brawler, count the swing
    /// timer down, close when out of reach, and on expiry within reach throw a
    /// blow re-keyed to the actor's own npcSwingSeq_. Applies the guard, the
    /// caught-blow wind, and the defeat check. Driven from stepMovement.
    /// STANCE & ROOM BUILD -- THE ROOM FIGHTS BACK: the blows resolve under
    /// BOTH rule sets now. Under BRAWL the player floors at kPlayerBrawlFloor
    /// exactly as shipped; under LETHAL the floor is lifted to zero and the
    /// blow that empties the player routes applyDefeat (the death ceremony
    /// hangs off escalated() client-side). A landed blow also raises the
    /// player's hands.
    void stepBrawl() noexcept;
    /// STANCE & ROOM BUILD. Hands up and the lull timer refilled -- the one
    /// door into handsUp_ = true, so every raise rule refills the same clock.
    void raisePlayerHands() noexcept;
    /// WATCH & RHYTHM BUILD. Resolves one player blow against a roster actor:
    /// strike() on the actor's sheet, then the NPC's side of the rhythm -- his
    /// guard softens a landed blow (blockedDamage at level 0) and recoils the
    /// player on a normal swing or breaks and staggers him on a hard one; a
    /// hit inside his wind-up, or any hard hit, staggers him. Applies the
    /// health and returns the blow; the caller sets the activity and the
    /// deeds. `recoiled` / `staggered` report which rule fired.
    Blow landPlayerBlow(Actor& target, bool hard, std::int32_t bonus, std::int32_t swingTerm,
                        std::int32_t chargeQ8, bool& recoiled, bool& staggered);
    /// STREET SENSES leg (b). THE HEAD OF EVERY PLAYER SWING, in one place so
    /// the roster swing and the street swing share ONE charge tier, ONE winded
    /// refusal, ONE recovery and ONE wind cost: from CHARGING, read the tier,
    /// refuse a hard swing while winded (result.refused, nothing thrown),
    /// enter recovery, mark the swing committed, raise the hands, read the
    /// fatigue term and pay the wind. Fills `chargeQ8` and `swingTerm` for the
    /// strike. Returns false with `result` filled when nothing is thrown (a
    /// release with no charge, or the refusal).
    bool armPlayerSwing(PlayerSwingResult& result, std::int32_t& chargeQ8,
                        std::int32_t& swingTerm);
    /// PATRONS STAND BACK, fired once on the escalation edge (noteEscalation):
    /// every present non-brawler who is not a professional and is within
    /// kStandBackRadiusTiles of the player is sent kStandBackTiles tiles
    /// directly away, to the first standable tile inside the footprint on
    /// that line. Deterministic, draw-free; the rota holds them there while
    /// lethalFightLive().
    void standBack();
    /// A brawler under lethal rules who is bloodied, not a professional and
    /// does not mean Kill breaks off for the street (gull::kStreetX/Y). He
    /// stops swinging at once and leaves brawlers_ on arrival.
    [[nodiscard]] bool shouldRout(const Actor& actor) const noexcept;
    void tickPatrons();
    /// Advances every trickle still delivering. One second per call.
    void tickSpellwork();
    /// HELD-EFFECTS BUILD. Drops every hold whose clock has run out and, when
    /// any did, re-derives what the pool's ceiling reads off the thinner
    /// sheet. Called every advanceSecond beside tickSpellwork, and at the end
    /// of every clock jump (skipTo) -- a hold is on the absolute clock, so a
    /// night asleep runs it out rather than pausing it.
    void sweepHeldEffects();
    /// The one place a hold change lands on the wind: the pool ceiling is
    /// 2*VIG + MGT + AGI of the EFFECTIVE sheet, resized without a refill --
    /// see PlayerFatigue::resizeFor on why a recast can never buy wind back.
    void applyHeldEffects() noexcept;
    /// One dose of vitality onto a body. Heals cap at hpMax; harm floors at
    /// spellforge's kVitalityFloor and NEVER downs or floors anybody -- no
    /// crafting on the public shelf can put a body on the ground, and the
    /// floor is structural rather than a balance choice.
    void applySpellDose(std::int32_t targetId, std::int32_t magnitude);
    void advanceSecond();
    [[nodiscard]] Actor* findRole(ActorRole role, bool presentOnly) noexcept;
    [[nodiscard]] const Actor* findRole(ActorRole role, bool presentOnly) const noexcept;
    [[nodiscard]] Actor* mutableActorById(std::int32_t id) noexcept;
    [[nodiscard]] Actor* onDutyBouncerNearestPlayer() noexcept;
    [[nodiscard]] std::int32_t speedFor(const Actor& actor) const noexcept;
    /// A draw for an action the player took between ticks. Bumps and hashes the
    /// sequence counter, so replaying the same actions reproduces the same
    /// rolls.
    [[nodiscard]] std::uint64_t drawForPlayerAction() noexcept;

    void applyReply(Reply& reply);
    /// What a drawn blade does to a room full of people, exactly once.
    void noteEscalation(std::int32_t targetId);
    /// Adds `actor` to the brawl list (idempotent, kept sorted) and staggers
    /// its first swing (npcSwingTimer_ = id % kNpcSwingStaggerSteps) so a crowd
    /// joining at once never metronomes. The one door into brawlers_.
    void joinBrawl(Actor& actor);
    /// BARKS LANE: `<name>: <crowd.flee row>` for whoever is running.
    [[nodiscard]] std::string crowdFleeLine(const Actor& actor) const;
    /// ACTION-COMBAT BUILD. Kills a body: Activity::Dead (a Downed that never
    /// stands), the victim's Deed::Slew, the witness spread, and -- WITNESSED
    /// (the three-clause rule via witnessCount) -- the murder law's heat and
    /// the Condemned arrest hook (CrimeLedger::markMurderer). Only the player
    /// kills in v1, so this is only ever reached from a player blow or link.
    void slayActor(Actor& target);

    // --- S8 -------------------------------------------------------------------
    /// THE ONE CALL SITE A DEFEAT GOES THROUGH, wherever the player went down.
    /// Builds the Defeat out of the roster, hands it to the book, and applies
    /// what comes back to the things the ROOM owns: the purse, the roster's
    /// weapons, and the ejection that follows a man being carried out.
    void applyDefeat(std::int32_t winnerId);
    /// Puts the right thing in every rival's hands and the right intent behind
    /// it. Called wherever the room re-seats itself, so a rival who walked out
    /// and came back is still carrying what he earned.
    void armRivals();
    /// Every present actor and the faction that claims each, in roster order.
    /// What a founding chapter enlists out of.
    [[nodiscard]] RiseWorld riseWorld() noexcept;
    /// TWO OF THE THREE BOONS legend.hpp's own file header promises: a Trade
    /// rung is a cheaper counter and a Wire rung is more wire in a set, the
    /// same way a Flame rung already buys Session::examine a longer look.
    /// Flame needs a Casebook, which lives on Session above this room in the
    /// layering, so it is never asked for here -- Wire and Trade are pure
    /// functions of ledgers this room already owns (dialogue_'s crimes,
    /// skills, standings and contracts), and a default Casebook{} is unbound,
    /// contributing rung 0 to the one row nothing here reads.
    [[nodiscard]] Legend earnedLegend() const;

    SystemId id_;
    const TileQuery* tiles_;
    RegionPath path_;
    DialogueDirector dialogue_;
    CounterRandomSource rng_;
    /// The world's own seed, kept because a LOCK's pins are a pure function of
    /// it and of the lock's id -- no tick, no stream position, no draw. See
    /// lockpick.hpp on why a lock is not re-rolled between attempts.
    std::uint64_t worldSeed_ = 0;
    std::int32_t playerActionSeq_ = 0;

    std::vector<Actor> actors_;
    std::int32_t timeOfDay_;
    std::int64_t tick_ = 0;

    // the player, as this room sees them
    std::int32_t playerX_ = 0;
    std::int32_t playerY_ = 0;
    std::int32_t playerBand_ = gull::kGroundBand;
    bool playerKnown_ = false;
    Weapon playerWeapon_ = Weapon::Fists;
    Intent playerIntent_ = Intent::Subdue;
    std::int32_t playerHp_ = 100;
    std::int32_t playerHpMax_ = 100;
    bool playerFloored_ = false;
    std::int32_t playerCoin_ = kPlayerStartingCoin;
    std::int32_t playerDrinks_ = 0;
    std::int32_t shoveX_ = 0;
    std::int32_t shoveY_ = 0;

    // the sheet and the wind -- fatigue build. Both hashed (a declared
    // structure change, stated at the hash site): the sheet decides what
    // every punch, gait, climb and cast is worth, and the pool decides
    // whether the next sprint step is refused, so two runs that disagreed
    // about either would be two different games.
    AttributeBlock playerAttributes_{};
    PlayerFatigue fatigue_{};

    // the guard and the cast -- first-person combat's own player state
    bool playerBlocking_ = false;
    std::int32_t blowsBlocked_ = 0;

    // the swing machine and the sightline -- ACTION-COMBAT BUILD. All hashed
    // (the declared tavern-baseline move): the charge tier decides what the
    // next release does, the recovery lockout whether a press is even heard,
    // the facing who a swing hits, and the deference bit what the Watch does --
    // so two runs that disagreed about any of them would be two different
    // fights. playerYaw_ is authoritative facing pushed from the body, exactly
    // as playerX_/Y_ are authoritative position.
    Angle playerYaw_ = 0;
    PlayerCombatState combatState_ = PlayerCombatState::Idle;
    std::int32_t chargeSteps_ = 0;
    std::int32_t recoverySteps_ = 0;
    /// Recomputed every stepPlayerCombat off position + yaw + roster; the
    /// reticle reads it. Hashed with the rest for gate visibility.
    bool sightlineFlag_ = false;
    /// DEFERENCE: the player presents as a Wielder. Default false; no play path
    /// sets it yet (section 4.4). Hashed -- it changes the Watch.
    bool playerPresentsAsWielder_ = false;
    /// STANCE & ROOM BUILD: fighting mode and its lull countdown. Both hashed
    /// (part of the declared tavern/gate-workload baseline move this build
    /// makes): the bit is what the room, the street and the Watch react to,
    /// and the countdown decides the step it flips back, so two runs that
    /// disagreed about either would be two different fights. See the public
    /// stance block for the raise/lower table.
    bool handsUp_ = false;
    std::int32_t lowerTimer_ = 0;
    /// WATCH & RHYTHM BUILD: the player's recoil (a normal swing caught by a
    /// guard) and block-stagger (a hard swing caught by the player's guard)
    /// clocks, in steps. Both hashed (the second half of the declared
    /// tavern/gate-workload move): each decides whether the next press is
    /// heard and whether the next blow is softened.
    std::int32_t recoilSteps_ = 0;
    std::int32_t blockStaggerSteps_ = 0;
    /// Empty means "nothing picked" and equippedSpell() defaults to the first
    /// known crafting. Kept as the ID rather than an index because the
    /// grimoire inserts in id order: learning a new crafting must never
    /// silently re-point what the hand is holding.
    std::string equippedSpellId_;
    /// SPELLS BUILD: what each of the ten number-row slots holds, by id --
    /// empty string is an empty slot. Hashed (a deliberate structure change,
    /// stated at the hash site): which crafting a keypress readies decides
    /// what the next cast does, so it is state the twin-run gate compares.
    std::array<std::string, kQuickSlotCount> quickSlotIds_{};
    /// Room-time (elapsed_) tick the next cast is allowed at.
    std::int64_t castCoolUntil_ = 0;
    /// One OVER_TIME component still delivering: a scald working through, a
    /// cut knitting shut. Doses land every kOverTimePeriodTicks. A target who
    /// leaves the room takes the link down with them.
    struct SpellTrickle {
        std::int32_t targetId = -1;  ///< -1 is the player.
        std::int32_t magnitude = 0;  ///< per dose, signed.
        std::int32_t dosesLeft = 0;
        std::int32_t cadenceLeft = 0;
    };
    std::vector<SpellTrickle> trickles_;
    /// HELD-EFFECTS BUILD: every WHILE_ACTIVE row currently live on the
    /// player. Hashed (a deliberate structure change, stated at the hash
    /// site): a live tuning is read by every attribute reader in the game,
    /// so two runs that disagreed about one would be two different games.
    std::vector<ActiveHold> heldEffects_;

    // trade
    std::int32_t drinkStock_ = kOpeningStock;
    std::int32_t rentedRoom_ = -1;
    /// -1 when nothing has been argued down. Set by a struck haggle and spent
    /// by the purchase that follows it.
    std::int32_t negotiatedDrink_ = -1;
    std::int32_t negotiatedRoom_ = -1;
    /// Which actor the open conversation is with, or -1.
    std::int32_t talkingToId_ = -1;
    /// Set once when a blade is drawn, so the room reacts to it exactly once
    /// rather than every second the classifier keeps saying "lethal".
    bool escalationSeen_ = false;

    // trouble
    Standing standing_ = Standing::Welcome;
    std::int32_t offences_ = 0;
    std::int32_t warningsGiven_ = 0;
    std::int32_t timesEjected_ = 0;
    std::int64_t warnedAtTick_ = -1;
    std::int64_t barredUntilTick_ = -1;
    std::int32_t respondingBouncerId_ = -1;
    std::string lastWarning_;
    FightClass escalation_ = FightClass::Brawl;
    /// Ids currently swinging at the player, in ascending order.
    std::vector<std::int32_t> brawlers_;

    // --- S5 -----------------------------------------------------------------
    /// How many bales are still in the snug tonight. Restocked when the doors
    /// open, exactly like the cellar -- WITHOUT it, the run is a coin faucet: a
    /// bale delivered leaves the snug empty of nothing, so a player can carry
    /// the same bale out of the same door until the guild loves them. A boat
    /// brings what a boat brings.
    std::int32_t balesInSnug_ = kBalesPerNight;
    /// And what is in each of them tonight, one entry per bale. Indexed by
    /// `balesInSnug_ - 1`, so the snug empties from the top of the stack and a
    /// bale put back down is the bale picked up again.
    std::array<Contraband, static_cast<std::size_t>(kBalesPerNight)> baleGoods_{
        Contraband::Moonshine, Contraband::Dust, Contraband::Flower};
    /// Bit i is set once room i's strongbox has been emptied.
    std::int32_t crackedBoxes_ = 0;
    /// The highest band the player has stood on. See settleLanding.
    std::int32_t highestBand_ = 0;
    // --- S6 -----------------------------------------------------------------
    /// Bit i is set once tonight's i-th rat has been skinned.
    std::int32_t scalpedVermin_ = 0;
    WatchStance watchStance_ = WatchStance::Idle;
    WatchCause watchCause_ = WatchCause::None;
    std::int32_t watchmanId_ = -1;
    std::int64_t noticedAtTick_ = -1;
    std::string lastDemand_;
    /// BARKS LANE: see lastJoin() / lastFlee(). Strings, unhashed, like
    /// lastDemand_ and lastWarning_ -- what was SAID, never what happened.
    std::string lastJoin_;
    std::string lastFlee_;
    ArrestReport lastArrest_;
    bool arrestRelease_ = false;
    /// SENTENCES LANE. Reports, like lastArrest_ and lastDefeat_: what the
    /// last served judgment did, and the end of the run once the rope has
    /// passed. Neither hashed nor codec'd, and neither needs to be -- every
    /// fact in them is derived at the moment of serving from state that is
    /// (the ledger's record, the purse, the clock, the standings), and the
    /// end's own bit is CrimeLedger::executed_.
    SentenceReport lastServed_;
    RunEnd runEnd_;
    /// The second-of-day this room was constructed at, so dayNumber() can be
    /// monotonic without the wall clock's midnight in it.
    std::int32_t startedAt_ = 0;
    /// Whether the player was inside the walls on the previous movement step.
    /// A bale is DELIVERED by crossing the threshold with it, and a crossing is
    /// a transition and not a state.
    bool wasInside_ = false;
    /// Simulated seconds this room has been running, INCLUDING the ones a
    /// skipTo jumped. The clock on the wall wraps at midnight and the Watch's
    /// memory does not, so heat is charged against this and not timeOfDay_.
    std::int64_t elapsed_ = 0;

    // --- S8 -------------------------------------------------------------------
    NemesisBook nemesis_;
    /// The ward's roll, borrowed. Never owned; may be null.
    Ward* roll_ = nullptr;
    /// RADIANT BUILD. The district's people, borrowed the way the roll is.
    /// Never owned; may be null, and the radiant board stays empty then.
    const WardPopulation* wardPeople_ = nullptr;
    /// Who landed the blow that is currently taking the player down. -1 when
    /// nobody has hit them since the last defeat was settled.
    std::int32_t lastBlowBy_ = -1;
    Rise lastDefeat_;
    bool defeatRelease_ = false;

    // --- S9 -------------------------------------------------------------------
    /// How the body is carrying itself and what it is doing to the air. Hashed:
    /// crouching changes who sees a crime, and a thing that decides a crime is
    /// state.
    StealthState stealth_;
    /// The lock under the wire right now, if any.
    Lockpicking picking_;
    /// Which of the four boxes the wire is in, or -1.
    std::int32_t pickingRoom_ = -1;
    /// Picks in the roll. Finite, spendable, and buyable off Finch.
    std::int32_t picks_ = kStartingPicks;
    /// Bit i set once room i's LOCK is open (which is not the same as its box
    /// being emptied -- crackedBoxes_ is that), jammed shut for good, or
    /// forced. All three are permanent world change, so all three are hashed.
    std::int32_t openedLocks_ = 0;
    std::int32_t jammedLocks_ = 0;
    std::int32_t forcedLocks_ = 0;
};

}  // namespace granadad::sim
