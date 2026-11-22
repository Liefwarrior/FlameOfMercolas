#pragma once

// The scripted capture drivers (--trail, --flame, --roofs, --skyrun, --burgle, --street*, ...) and
// runSmoke, one file per line under render/scripted/. This header is what they share: the walking
// and talking helpers, the beat masks and notes runSmoke reports, and the drivers runSmoke calls.
// Internal to granadad-render; the public entry points stay declared in session.hpp.

#include "../session_internal.hpp"

namespace granadad::render {

namespace scripted {

[[nodiscard]] bool stepToward(Session& session, std::int32_t goalX, std::int32_t goalY);
void walkStraightTo(Session& session, std::int32_t tileX, std::int32_t tileY);
void climbAndLand(Session& session);

/// Walks the body to a tile with REAL movement steps, along a route THE ROOM'S
/// OWN PATHFINDER produced.
///
/// S5 REPLACED WHAT WAS HERE, and the S4 review is why. The old version was a
/// greedy step-toward-the-goal walk, and its own comment said it was
/// "deliberately NOT a pathfinder" because "the room already has one
/// (RegionPath) and it belongs to the actors". Right instinct, wrong
/// conclusion: it did not avoid reimplementing RegionPath, it reimplemented a
/// worse one -- and it could not get from the authored spawn on the Tarwalk
/// through the Gull's door to Father Maell. So `--flame` worked only from
/// `--spawn=150,74,19`, already inside the room; run as the README documented
/// it, it walked into a wall, photographed a conversation with the wrong
/// person, and exited 0.
///
/// It USES the room's pathfinder now. RegionPath is a breadth-first search over
/// standable tiles inside a box, and gull::kRegion is the building plus the
/// street in front of it -- every tile a capture of this house needs. The body
/// still WALKS: each waypoint is steered to with ordinary movement steps
/// through ordinary collision, so a captured frame is still a picture of a body
/// that got there on its feet.
/// The box the capture harness routes inside: the Gull, both its floors, and
/// enough of the Tarwalk to contain the AUTHORED SPAWN.
///
/// gull::kRegion stops at kStreetY - 2, which is y=61, and the S2 spawn was at
/// y=60. One tile short. A router whose box does not contain the body's own
/// cell refuses outright, so every walk that began at the spawn fell through to
/// the greedy fallback -- which is the S4 behaviour this was supposed to
/// replace, and it is why `--skyrun` reached every patron in the room and never
/// once reached the man in the snug.
///
/// #79 MOVED THE SPAWN AGAIN, so the bound is a min() rather than a subtraction
/// off it. `kSpawnTileY - 2` happened to be right while the spawn was north of
/// the street; it is arithmetic that only works in one direction, and a spawn
/// SOUTH of kStreetY would have quietly cropped the street back out of the box.
/// Both ends are named, so the box contains the frontage and the body wherever
/// the opening shot is aimed next.
///
/// KIT FIX PASS: the drop beat's own photography spot moved to world x=195/
/// 196, well past kFootprintX1 -- the quay's wall line stands a full
/// architectural pillar (not the thin mooring posts elsewhere on it) at
/// x=155/157, and a fixed prop_crate/barrel pair sits at roughly x=167; the
/// beat's own camera has to clear BOTH, not just the wall, to find open
/// boards. +2 no longer reaches that stretch, so it is named for it directly
/// rather than guessed at again by a second body standing somewhere this box
/// still cannot route to.
inline constexpr sim::TileBox kCaptureRegion{
    sim::gull::kFootprintX0 - 2,
    std::min(sim::gull::kStreetY - 2, sim::docks::kSpawnTileY - 1),
    sim::gull::kGroundBand,
    sim::gull::kFootprintX1 + 40, sim::gull::kFootprintY1 + 1, sim::gull::kUpperBand};

static_assert(kCaptureRegion.x0 <= sim::docks::kSpawnTileX &&
                  sim::docks::kSpawnTileX <= kCaptureRegion.x1 &&
                  kCaptureRegion.y0 <= sim::docks::kSpawnTileY &&
                  sim::docks::kSpawnTileY <= kCaptureRegion.y1,
              "the capture router's box must contain the spawn, or every scripted "
              "line falls through to the greedy fallback -- see the note above");
void walkToTile(Session& session, std::int32_t tileX, std::int32_t tileY);
[[nodiscard]] sim::Angle bearingToward(const Session& session, std::int32_t xQ8,
                                        std::int32_t yQ8) noexcept;
void facePlayerAndSync(Session& session, std::int32_t xQ8, std::int32_t yQ8);
[[nodiscard]] bool wardWalking(const sim::WardActor& who) noexcept;
[[nodiscard]] bool wardStandsStill(const sim::WardActor& who) noexcept;
[[nodiscard]] std::pair<std::int32_t, std::int32_t> wardDrawnQ8(const Session& session,
                                                                const sim::WardActor& who) noexcept;
[[nodiscard]] bool speakerDrawnQ8(const Session& session, std::int32_t& xQ8, std::int32_t& yQ8,
                                  std::int32_t& band) noexcept;
[[nodiscard]] bool bodyBetweenFrom(const Session& session, std::int32_t eyeXQ8,
                                   std::int32_t eyeYQ8, std::int32_t xQ8, std::int32_t yQ8,
                                   std::int32_t band, std::int32_t speakerId) noexcept;
[[nodiscard]] bool bodyBetween(const Session& session, std::int32_t xQ8, std::int32_t yQ8,
                               std::int32_t band, std::int32_t speakerId) noexcept;
[[nodiscard]] int topicOfKind(const Session& session, sim::TopicKind kind);
[[nodiscard]] const sim::Actor* actorNamed(const Session& session, std::string_view name);
[[nodiscard]] const sim::Actor* nearestMark(const Session& session);
[[nodiscard]] bool inTheFight(const Session& session, std::int32_t markId);
[[nodiscard]] bool closeOnAndFace(Session& session, std::int32_t markId);
[[nodiscard]] bool tapUntilItConnects(Session& session, std::int32_t markId, int tries);
[[nodiscard]] bool swingOnceAt(Session& session, std::int32_t markId, int tries);
[[nodiscard]] bool speakTo(Session& session, std::string_view name);
void standBackFrom(Session& session, std::string_view name);
bool pick(Session& session, sim::TopicKind kind);
void upOntoTheLead(Session& session);
void comeDownstairs(Session& session);

/// S8. THE NEMESIS ARC, PLAYED: pick a fight with a named labourer, lose it,
/// wake up on the quay, walk back in the next evening and fight it twice more
/// as it goes lethal in the world.
///
/// EVERY BEAT IS A SESSION CALL A KEYPRESS MAKES. The walk is real movement
/// through real collision, the fight is the Attack key (punch() is now a tap of
/// the action-combat verbs) and then the room resolving the brawl and its flip
/// to lethal a second at a time, and the respawn is the same settleDefeat() the
/// client's own step loop reaches -- with the death ceremony over it once steel
/// is out. STANCE & ROOM BUILD: every defeat in the arc is now DELIVERED BY
/// THE ROOM. stepBrawl resolves the NPC's blows under lethal rules with the
/// floor lifted to zero, so the rematch that flips lethal is fought to the
/// boards by the man himself, and Tavern::concedeTo -- the scripted defeat
/// seam -- is no longer used anywhere in this line. A beat that the room
/// cannot finish does not land, which is the proof.
/// STANCE & ROOM BUILD. The mask of which --nemesis beats landed, for the
/// summary -- the eviction line's own gEvictBeatMask shape, so a build log says
/// WHICH beat fell short rather than how many did.
inline std::int32_t gNemesisBeatMask = 0;

/// Who the Watch line swung at and what came of it, for the summary -- so a
/// capture cannot quietly photograph the wrong man (the nemesis line's rule).
inline std::string gWatchHaltNote;
[[nodiscard]] double faceHeightTiles(sim::WardType type) noexcept;
void lookAtFace(Session& session, std::int32_t xQ8, std::int32_t yQ8, sim::WardType type,
                sim::Angle yawOffset);

// A stand's own ground tests, defined beside runStreetLine further down.
[[nodiscard]] bool bodiesDrawnWithin(const Session& session, std::int32_t x, std::int32_t y,
                                     std::int32_t band, std::int32_t radius,
                                     std::int32_t exceptId) noexcept;

[[nodiscard]] bool underOpenSky(const sim::TileQuery& tiles, std::int32_t x, std::int32_t y,
                                std::int32_t band) noexcept;

/// What the Kit line saw, for the summary.
inline std::string gKitNote;
inline std::string gPackNote;
/// PHASE 9: what the gear line saw, for the summary -- the takes, the
/// comparison lines it read, the blow, and the worn set at the shutter.
inline std::string gGearNote;

// The eye and pack helpers the Kit, pack and gear lines share (kit.cpp).
/// Walks to a tile and faces a neighbouring tile, so the crosshair reads
/// what lies there.
void standAtFacing(Session& session, std::int32_t standX, std::int32_t standY,
                   std::int32_t faceX, std::int32_t faceY);
/// The eye fifty degrees down onto the boards a tile ahead.
void lookDownAtBoards(Session& session);
/// The eye near straight down, on a thing underfoot.
void lookAtFeet(Session& session);
/// The eye level again.
void lookLevel(Session& session);
/// Opens the Character tile on its PACK view and walks the cursor onto the
/// first row whose printed label starts with `prefix`. False when none does.
[[nodiscard]] bool openPackRow(Session& session, const std::string& prefix);

constexpr std::int32_t kCourtRopePageBeats = 4;
[[nodiscard]] bool courtEndingHangs(const std::string& ending);

/// What the court line found, for the summary -- the nemesis line's rule: a
/// capture cannot quietly photograph the wrong thing.
inline std::string gCourtNote;

/// Plays a burglary end to end, through the same Session calls a keypress
/// makes: down on the haunches, across the floor, a hand in a coat, up the
/// stair, and the wire into a guest's strongbox.
///
/// EVERY BEAT IS A KEY. Nothing here reaches into the simulation sideways --
/// toggleCrouch, lift, climb, steal, movePick and probeLock are exactly what C,
/// T, SPACE, G, W/S and SPACE do -- which is the only thing that makes a
/// captured frame evidence rather than a diagram.
/// Which beats of the last burglary landed, one bit each, in order. Printed in
/// the summary so a short run says WHICH beat it dropped rather than only how
/// many -- the S4 review's whole complaint about scripted lines that report a
/// number and nothing else.
inline std::int32_t gBurgleBeatMask = 0;

/// COURIER CASE. Which of the errand's beats the last --case run landed, one
/// bit each in order -- the burglary's own count-plus-mask discipline.
inline std::int32_t gCaseBeatMask = 0;

/// And how many people were awake, upright, on this floor and in range when
/// beat 2 was judged. Zero means the beat proved nothing -- see the note there.
inline std::int32_t gBurgleWatchers = 0;

/// Probes made on the SECOND box by `--burgle=lock`, so the case that proves
/// that frame is a live attempt can say so with a number instead of a picture.
inline std::int32_t gLockEndingProbes = 0;
int workTheWire(Session& session, std::int32_t stopAtPins = -1,
                std::int32_t maxProbes = -1);
[[nodiscard]] bool walkAcrossDistrict(Session& session, std::int32_t tileX,
                                      std::int32_t tileY);

inline std::int32_t gTrailWalked = 0;

inline std::int32_t gTrailUnreached = 0;

/// EVICTION CASE. The mask of which --eviction beats landed, for the summary.
inline std::int32_t gEvictBeatMask = 0;

/// The same two tiles Session::kWardTalkReachTiles is, named again here because
/// this file's anonymous namespace cannot see a private static member. Pinned
/// to it by a case: a search that confirmed its candidate against a DIFFERENT
/// reach than the key uses would confirm nothing.
constexpr std::int32_t kStreetReachTiles = 2;
[[nodiscard]] sim::Angle cardinalToward(std::int32_t toX, std::int32_t toY) noexcept;
[[nodiscard]] const sim::WardActor* addresseeFrom(Session& session, std::int32_t sx,
                                                  std::int32_t sy, std::int32_t band,
                                                  sim::Angle yaw);

// The drivers runSmoke dispatches to; each is defined in the file named for its line.
[[nodiscard]] int runFlameLine(Session& session, const std::string& ending);
[[nodiscard]] bool runHeldCast(Session& session, const std::string& spellId);
[[nodiscard]] int runRoofLine(Session& session, const std::string& ending);
[[nodiscard]] int runContractLine(Session& session, const std::string& ending);
[[nodiscard]] int runNemesisLine(Session& session, const std::string& ending);
[[nodiscard]] int runWatchHaltLine(Session& session, const std::string& ending);
[[nodiscard]] int runVoiceLine(Session& session, const std::string& ending);
[[nodiscard]] int runKitLine(Session& session, const std::string& ending);
[[nodiscard]] int runPackLine(Session& session, const std::string& ending);
[[nodiscard]] int packBeatsFor(const std::string& ending);
[[nodiscard]] int runGearLine(Session& session, const std::string& ending);
[[nodiscard]] int gearBeatsFor(const std::string& ending);
/// The worn rows' raws ids in the screen's slot order, comma-joined ("-" when
/// nothing is worn) -- the gear line's summary, so a capture says what the
/// body had on at the shutter.
[[nodiscard]] std::string gearWornIds(const Session& session);
/// What the hand swings, for a summary: the held row's raws id, or the class
/// a sheet armed straight, and the span the blow rolls -- "maul:10-16",
/// never the class a maul swings as.
[[nodiscard]] std::string gearHandLine(const Session& session);
[[nodiscard]] int runCourtLine(Session& session, const std::string& ending);
[[nodiscard]] int runTrailLine(Session& session, const std::string& ending);
[[nodiscard]] int runBurgleLine(Session& session, const std::string& ending);
[[nodiscard]] int runCaseLine(Session& session, const std::string& ending);
[[nodiscard]] int runEvictionLine(Session& session, const std::string& ending);
[[nodiscard]] int runSkyrunLine(Session& session, const std::string& ending);
[[nodiscard]] RungLineResult runRungLine(Session& session, const std::string& word);

}  // namespace scripted

using namespace scripted;

}  // namespace granadad::render
