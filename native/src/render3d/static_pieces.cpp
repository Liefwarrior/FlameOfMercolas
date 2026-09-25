#include "granadad/render3d/static_pieces.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <fstream>
#include <map>
#include <span>
#include <sstream>

#include <nlohmann/json.hpp>

#include "granadad/render/atlas.hpp"
#include "granadad/render/vertical.hpp"
#include "granadad/render/voxel_classify.hpp"
#include "granadad/sim/tile_query.hpp"

namespace granadad::render3d {

namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kHalfPi = kPi / 2.0F;
constexpr float kDegToRad = kPi / 180.0F;

/// The JSON keys, in PieceRole order.
constexpr std::string_view kRoleNames[kPieceRoleCount] = {
    "none",        "wall",         "wall_corner", "wall_window",  "wall_door",   "roof_edge",
    "floor_plank", "floor_cobble", "floor_flag",  "water",        "prop_barrel", "prop_crate",
    "prop_sack",   "lamp_wall",    "lamp_post",   "brazier",      "floor_fill",  "wall_cap",
    "ceiling",     "wall_timber",  "chimney",     "lip_plank",    "lip_stone",   "door_inside",
    "door_leaf",   "gate_post",    "flame",       "ember",        "lamp_bracket", "lamp_chain",
    "joist",       "table",        "bench",       "mug",          "bottle",      "shelf",
    "barrel_rack", "fireplace",    "pillar",      "post",         "parapet",     "roof_tile",
    "rowboat",     "crane",        "gunwale",     "window_timber", "rope",
    "hull",        "wall_plaster", "stool",       "quay_wall",     "roof_flag",
    "roof_batten", "shop_sign",    "floor_strip",  "post_rail",    "door_post",
    "item",
};

// ---------------------------------------------------------------------------
// the grid
// ---------------------------------------------------------------------------

/// The four sides of a cell, and the yaw that turns a piece's local -Z (the
/// wall kit's brick side) to face each: north 0, then clockwise.
enum Side : int { kNorth = 0, kEast = 1, kSouth = 2, kWest = 3 };

constexpr std::int32_t kSideDx[4] = {0, 1, 0, -1};
constexpr std::int32_t kSideDy[4] = {-1, 0, 1, 0};
/// The side's outward normal in scene XZ (Z = south).
constexpr float kNormalX[4] = {0.0F, 1.0F, 0.0F, -1.0F};
constexpr float kNormalZ[4] = {-1.0F, 0.0F, 1.0F, 0.0F};
/// A piece's local +X in scene XZ when yawed to face the side -- the
/// direction its module runs along the face ("the tangent").
constexpr float kTangentX[4] = {1.0F, 0.0F, -1.0F, 0.0F};
constexpr float kTangentZ[4] = {0.0F, 1.0F, 0.0F, -1.0F};

/// The hash salts, one per rule that draws lots: named so a reader can tell
/// which rule a number belongs to, and so two rules on one cell never share
/// a draw.
constexpr std::uint32_t kSaltProp = 0x50524F50U;      // "PROP"
constexpr std::uint32_t kSaltChimney = 0x4348494DU;   // "CHIM"
constexpr std::uint32_t kSaltCobble = 0x434F4242U;    // "COBB"
constexpr std::uint32_t kSaltTable = 0x5441424CU;     // "TABL"
constexpr std::uint32_t kSaltShelf = 0x5348454CU;     // "SHEL"
constexpr std::uint32_t kSaltRoofTile = 0x54494C45U;  // "TILE"
constexpr std::uint32_t kSaltRoofProp = 0x524F4F46U;  // "ROOF"
constexpr std::uint32_t kSaltBoat = 0x424F4154U;      // "BOAT"
constexpr std::uint32_t kSaltCrane = 0x4352414EU;     // "CRAN"
constexpr std::uint32_t kSaltRope = 0x524F5045U;      // "ROPE"
constexpr std::uint32_t kSaltPane = 0x50414E45U;      // "PANE"
constexpr std::uint32_t kSaltStool = 0x53544F4CU;     // "STOL"
/// One window in this many is dark at night whatever the room (a bed made,
/// a candle out).
constexpr std::uint32_t kPaneDarkEvery = 3U;
/// One hull cell in this many hangs a mooring line.
constexpr std::uint32_t kRopeEvery = 3U;

/// The harbour's FLUID lane runs 1..7 (tile_query.hpp); the water rule asks
/// each depth for its own surface height.
constexpr int kWaterMaxDepth = 7;

/// A flame quad's size, a lantern's and a fire's: the warm square a lamp
/// draws inside its cage, in metres.
constexpr float kLanternFlameWidth = 0.46F;
constexpr float kLanternFlameHeight = 0.58F;
constexpr float kFireFlameWidth = 0.34F;
constexpr float kFireFlameHeight = 0.42F;
/// Where the flame sits in a lantern hung from `lift` (the lantern's origin
/// is the top of its hanging rod, 0.4 above the ring; the cap is 0.7 to 0.8
/// down and the glass 0.8 to 1.0, so the flame sits at 0.9 -- at 0.66 the
/// glow was centred on the ring and read as a lit hook over a dark lamp)
/// and in a brazier's cage (the cage floor is at 1.2 m of the 1.9 m stand),
/// the brazier's figures at the stand's own scale.
constexpr float kLanternFlameDrop = 0.9F;
constexpr float kFireFlameLift = 1.55F;
constexpr float kEmberLift = 1.24F;
/// The ember tray is fitted to this square inside the cage, in metres, at
/// the stand's own scale.
constexpr float kEmberSize = 0.5F;
/// A pile's head stands this far over the boarded core it is driven
/// through, in metres.
constexpr float kPileHeadOver = 0.7F;
/// A pile's thickness, in metres.
constexpr float kPileThickness = 0.3F;
/// How many cells from the water a timber post is still a pile.
constexpr std::int32_t kWaterReach = 2;
/// A timber post's tint is the material's top tint lifted this much, in
/// 1/64ths: a post in the light, not a wall head in the shade.
constexpr unsigned kPostLift = 90U;
/// A thin plaster quad overlaps whatever it meets -- the next quad, a door
/// frame, a corner's leg -- by this much, so no hairline of the chunk shows
/// at a seam; it stands this much proud of the kit pieces so the overlap
/// never fights, and every second one along a run a step further.
constexpr float kCutOverlap = 0.005F;
constexpr float kCutStagger = 0.002F;
/// A ceiling lantern hangs its chain from a hair under the ceiling; one
/// beside an overhang from this far inside the overhang's edge.
constexpr float kChainDrop = 0.04F;
constexpr float kOverhangIn = 0.12F;

/// A roof upstand's height: a coping, not a wall -- a body leaping the alley
/// passes over it, not through a chest-high parapet.
constexpr float kParapetHeight = 0.5F;
/// A window piece is never squeezed narrower than this (a run's odd last
/// cell carries plain plaster).
constexpr float kWindowMinLength = 1.8F;
/// A hung timber window's sill height, and a shelf's.
constexpr float kTimberWindowLift = 1.15F;
constexpr float kShelfLift = 1.55F;
/// The door leaf's clearance off the reveal it hangs open against, and off
/// the facade plane.
constexpr float kLeafOffReveal = 0.14F;
constexpr float kLeafOffFacade = 0.06F;
/// The return quad over a frame's end stops this far behind the frame's
/// outer face, inside its jamb post.
constexpr float kReturnInset = 0.004F;
/// A gate's lintel beam height.
constexpr float kGateLintelLift = 2.85F;
/// The joist's half-height under a ceiling (the kit beam's section is 0.36).
constexpr float kJoistHalfHeight = 0.18F;
/// The table's and bench's placing: a bench sits this far off the table's
/// long axis (a mug stands on the table's own height).
constexpr float kBenchOffset = 0.80F;
/// A rowboat moors its centre this far out from the water's edge.
constexpr float kBoatOffshore = 1.45F;
/// How many tiles of clear water a boat wants, across and along.
constexpr std::int32_t kBoatAcross = 3;
constexpr std::int32_t kBoatAlong = 4;
/// A fireplace's depth as a fraction of its own: a relief on the hearth
/// block, not a hood the bodies at the fire stand inside.
constexpr float kFireplaceDepth = 0.5F;
/// The furniture rule's reach round an indoor lantern, in tiles.
constexpr std::int32_t kFurnitureReach = 7;
/// A roof cell this many cells (Chebyshev) from a hatch is a paved deck.
constexpr std::int32_t kDeckReach = 2;
/// A shop sign hangs on a door post at this height, at this scale of the
/// kit's own (its bracket is 1.9 m long at one).
constexpr float kSignLift = 2.35F;
constexpr float kSignScale = 0.55F;
/// A hitching rail between a pair of posts runs at this height.
constexpr float kRailLift = 1.05F;
/// A timber post this many cells (Chebyshev) from a door gap's cell is a
/// door post: the jamb itself, or the hitching post against the wall
/// beside the door.
constexpr std::int32_t kDoorReach = 2;

[[nodiscard]] constexpr float yawOf(int side) noexcept {
    return static_cast<float>(side) * kHalfPi;
}

[[nodiscard]] constexpr int opposite(int side) noexcept { return (side + 2) & 3; }

/// Yaws land in [0, 2 pi): a half turn added to a half turn is zero, so the
/// same facing is the same bytes whichever rule produced it.
[[nodiscard]] float wrapYaw(float yaw) noexcept {
    while (yaw >= 2.0F * kPi) {
        yaw -= 2.0F * kPi;
    }
    while (yaw < 0.0F) {
        yaw += 2.0F * kPi;
    }
    return yaw;
}

struct Vec2 {
    float x = 0.0F;
    float z = 0.0F;
};

/// Rotates a scene-XZ offset by `quarterTurns` clockwise-from-above quarter
/// turns -- the exact integer form of the yaw the adapter applies, so a
/// corner lands on the tile corner and not a float's breath off it.
[[nodiscard]] constexpr Vec2 turn(Vec2 v, int quarterTurns) noexcept {
    switch (quarterTurns & 3) {
        case 1: return Vec2{-v.z, v.x};
        case 2: return Vec2{-v.x, -v.z};
        case 3: return Vec2{v.z, -v.x};
        default: return v;
    }
}

[[nodiscard]] bool isWall(const sim::TileQuery& tiles, std::int32_t x, std::int32_t y,
                          std::int32_t z) noexcept {
    return tiles.form(x, y, z) == content::TileForm::Wall;
}

[[nodiscard]] bool isFloor(const sim::TileQuery& tiles, std::int32_t x, std::int32_t y,
                           std::int32_t z) noexcept {
    return tiles.form(x, y, z) == content::TileForm::Floor;
}

/// Air or the void: nothing built in the cell at all.
[[nodiscard]] bool isAir(const sim::TileQuery& tiles, std::int32_t x, std::int32_t y,
                         std::int32_t z) noexcept {
    const content::TileForm form = tiles.form(x, y, z);
    return form == content::TileForm::Open || form == content::TileForm::Void;
}

/// Not a wall and not the void: air, a floor, a ramp, a stair -- a cell a
/// face can be seen from.
[[nodiscard]] bool isOpenish(const sim::TileQuery& tiles, std::int32_t x, std::int32_t y,
                             std::int32_t z) noexcept {
    const content::TileForm form = tiles.form(x, y, z);
    return form != content::TileForm::Wall && form != content::TileForm::Void;
}

[[nodiscard]] bool isWalkableForm(const sim::TileQuery& tiles, std::int32_t x, std::int32_t y,
                                  std::int32_t z) noexcept {
    const content::TileForm form = tiles.form(x, y, z);
    return form == content::TileForm::Floor || form == content::TileForm::Ramp ||
           form == content::TileForm::Stair;
}

/// Open air holding water: the harbour at this cell.
[[nodiscard]] bool isWater(const sim::TileQuery& tiles, std::int32_t x, std::int32_t y,
                           std::int32_t z) noexcept {
    return tiles.form(x, y, z) == content::TileForm::Open && tiles.fluidDepth(x, y, z) > 0;
}

[[nodiscard]] std::uint32_t cellHash(std::int32_t x, std::int32_t y, std::int32_t z,
                                     std::uint32_t salt) noexcept {
    return render::variantMix(static_cast<std::uint32_t>(x) * 73856093U ^
                              static_cast<std::uint32_t>(y) * 19349663U ^
                              static_cast<std::uint32_t>(z) * 83492791U ^ salt);
}

[[nodiscard]] Rgba8 mulTint(const Rgba8& a, const Rgba8& b) noexcept {
    const auto ch = [](std::uint8_t p, std::uint8_t q) {
        return static_cast<std::uint8_t>((static_cast<unsigned>(p) * static_cast<unsigned>(q) + 127U) /
                                         255U);
    };
    return Rgba8{ch(a.r, b.r), ch(a.g, b.g), ch(a.b, b.b), ch(a.a, b.a)};
}

/// A tint's colour lifted by a fixed ratio in 1/64ths (the alpha as is),
/// clamped: integers only, so every machine lifts it the same way.
[[nodiscard]] Rgba8 liftTint(const Rgba8& a, unsigned sixtyFourths) noexcept {
    const auto ch = [sixtyFourths](std::uint8_t p) {
        return static_cast<std::uint8_t>(std::min(255U, (static_cast<unsigned>(p) * sixtyFourths + 32U) / 64U));
    };
    return Rgba8{ch(a.r), ch(a.g), ch(a.b), a.a};
}

/// [r, g, b] or [r, g, b, a]: a fourth number is the alpha (a translucent
/// flame), else opaque.
[[nodiscard]] Rgba8 tintFromJson(const nlohmann::json& value, Rgba8 fallback) {
    if (!value.is_array() || value.size() < 3) {
        return fallback;
    }
    const auto ch = [&value](std::size_t i) {
        const int v = value[i].is_number() ? value[i].get<int>() : 255;
        return static_cast<std::uint8_t>(std::clamp(v, 0, 255));
    };
    return Rgba8{ch(0), ch(1), ch(2), value.size() >= 4 ? ch(3) : std::uint8_t{255}};
}

[[nodiscard]] float floatOf(const nlohmann::json& object, const char* key, float fallback) {
    if (!object.contains(key) || !object[key].is_number()) {
        return fallback;
    }
    return object[key].get<float>();
}

[[nodiscard]] std::int32_t intOf(const nlohmann::json& object, const char* key,
                                 std::int32_t fallback) {
    if (!object.contains(key) || !object[key].is_number()) {
        return fallback;
    }
    return object[key].get<std::int32_t>();
}

// ---------------------------------------------------------------------------
// the placement machinery
// ---------------------------------------------------------------------------

/// A run of exposed faces on one line: `a` is the coordinate along the
/// run's tangent (so every run reads left to right in its own frame), and
/// p(a) = base + tangent * a is the point on the face plane.
struct FaceRun {
    std::int32_t z = 0;
    int side = kNorth;
    float baseX = 0.0F;
    float baseZ = 0.0F;
    float a0 = 0.0F;
    float a1 = 0.0F;
    /// The first and last cells in a-order.
    std::int32_t firstX = 0, firstY = 0, lastX = 0, lastY = 0;
    std::uint16_t material = 0;
    WallClass cls = WallClass::None;
    bool outdoor = false;
    /// The kit's brick side faces out (outdoor masonry not rendered).
    bool brickOut = false;
    Rgba8 tint{255, 255, 255, 255};
    /// The wall line ends there (a convex corner of the plan) -- never at
    /// a door gap, where the frame takes the corner.
    bool convexA0 = false;
    bool convexA1 = false;
    /// The run ends at a door gap's jamb (the gap cell is the next cell
    /// along the line).
    bool gapA0 = false;
    bool gapA1 = false;
    /// The run faces into a door gap (a jamb's reveal); the street is at
    /// the a0 or the a1 end.
    bool reveal = false;
    bool revealA0 = false;
    bool revealA1 = false;
    /// The corner piece that takes over that end, or -1.
    int cornerA0 = -1;
    int cornerA1 = -1;
    /// A hull: a timber wall with the harbour beside it. Its boards lean
    /// outward and wear tar; an open top carries the gunwale.
    bool hull = false;
    /// A lone 1x1 timber cell (no wall neighbours): a post, not a wall.
    bool post = false;
};

struct Corner {
    std::int32_t x = 0, y = 0, z = 0;
    /// The two exposed sides, s and (s + 1) & 3.
    int side = kNorth;
    std::uint16_t material = 0;
    Rgba8 tint{255, 255, 255, 255};
    std::size_t runA = 0;  // the run of `side`, ending at this cell (its a1 end)
    std::size_t runB = 0;  // the run of `side + 1`, starting at this cell (its a0 end)
    float s = 1.0F;
};

[[nodiscard]] std::uint64_t cellKey(std::int32_t z, int side, std::int32_t x,
                                    std::int32_t y) noexcept {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(z)) << 40) |
           (static_cast<std::uint64_t>(static_cast<std::uint32_t>(side)) << 36) |
           (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 18) |
           static_cast<std::uint64_t>(static_cast<std::uint32_t>(y));
}

/// What a face piece is lit by: at each end, the two cells whose centres
/// the cut falls between and where between them it falls -- the value at
/// the cut, shared with the neighbour piece that ends there.
struct FaceLight {
    std::int32_t firstX = 0, firstY = 0, lastX = 0, lastY = 0;
    std::int32_t beforeX = 0, beforeY = 0, afterX = 0, afterY = 0;
    float firstT = 0.0F, lastT = 0.0F;
    bool gradient = false;
};

/// Everything a piece laid along a face can ask for beyond the run and the
/// span: which finish faces out, the standoff, the base height, its own
/// height (0 = the storey fit), a lean, a tint, the lights.
struct FaceOpts {
    bool frontOut = true;
    float standoff = 0.0F;
    float yBase = 0.0F;
    /// A height in metres for a piece fitted to something other than a
    /// storey (a lip, a parapet, a door header); 0 = the storey.
    float height = 0.0F;
    /// Degrees the top of an upright quad leans outward (a hull's flare).
    float flareDegrees = 0.0F;
    Rgba8 tint{255, 255, 255, 255};
    FaceLight light;
    bool selfLit = false;
};

class Placer {
public:
    Placer(const sim::TileQuery& tiles, const StaticCatalogue& catalogue,
           const std::vector<render::Lamp>& lamps)
        : tiles_(tiles), catalogue_(catalogue), lamps_(lamps) {}

    StaticPlacements run() {
        if (catalogue_.empty()) {
            return std::move(out_);
        }
        findDoors();
        walls();
        posts();
        roofEdges();
        doors();
        floors();
        lips();
        wallCaps();
        ceilings();
        water();
        props();
        furniture();
        roofs();
        roofBattens();
        harbour();
        lampPieces();
        return std::move(out_);
    }

private:
    // --- shared -----------------------------------------------------------

    [[nodiscard]] const MaterialRule* rule(std::int32_t x, std::int32_t y,
                                           std::int32_t z) const noexcept {
        return catalogue_.material(tiles_.material(x, y, z));
    }

    /// The wall class of a WALL cell, band rule applied.
    [[nodiscard]] WallClass wallClassAt(std::int32_t x, std::int32_t y,
                                        std::int32_t z) const noexcept {
        const MaterialRule* r = rule(x, y, z);
        if (r == nullptr || z < r->minBand) {
            return WallClass::None;
        }
        return r->wallClass;
    }

    [[nodiscard]] bool faceExposed(std::int32_t x, std::int32_t y, std::int32_t z,
                                   int side) const noexcept {
        return isOpenish(tiles_, x + kSideDx[side], y + kSideDy[side], z);
    }

    [[nodiscard]] bool faceOutdoor(std::int32_t x, std::int32_t y, std::int32_t z,
                                   int side) const noexcept {
        return !cellRoofed(tiles_, x + kSideDx[side], y + kSideDy[side], z);
    }

    /// How many WALL neighbours a cell has on its own level.
    [[nodiscard]] int wallNeighbours(std::int32_t x, std::int32_t y,
                                     std::int32_t z) const noexcept {
        int n = 0;
        for (int s = 0; s < 4; ++s) {
            n += isWall(tiles_, x + kSideDx[s], y + kSideDy[s], z) ? 1 : 0;
        }
        return n;
    }

    /// The side on which a roofed cell stands beside an open one (a gate's
    /// lintel over the next cell), or -1: a lantern there hangs from it.
    [[nodiscard]] int overhangSide(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept {
        for (int s = 0; s < 4; ++s) {
            const std::int32_t nx = x + kSideDx[s];
            const std::int32_t ny = y + kSideDy[s];
            if (isWalkableForm(tiles_, nx, ny, z) && cellRoofed(tiles_, nx, ny, z)) {
                return s;
            }
        }
        return -1;
    }

    /// A lone 1x1 timber cell: a mooring post, a pile, a taproom's table.
    [[nodiscard]] bool lonePost(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept {
        return isWall(tiles_, x, y, z) && wallClassAt(x, y, z) == WallClass::Timber &&
               wallNeighbours(x, y, z) == 0;
    }

    /// A timber POST: a lone timber cell, or one whose single wall
    /// neighbour is of another class (a pilaster against a stone wall) or
    /// is itself a lone pair's other half. The end cell of a timber run
    /// has a timber neighbour with neighbours of its own, and is a wall.
    [[nodiscard]] bool isPost(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept {
        if (!isWall(tiles_, x, y, z) || wallClassAt(x, y, z) != WallClass::Timber) {
            return false;
        }
        int walls = 0;
        bool runBeside = false;
        for (int s = 0; s < 4; ++s) {
            const std::int32_t nx = x + kSideDx[s];
            const std::int32_t ny = y + kSideDy[s];
            if (!isWall(tiles_, nx, ny, z)) {
                continue;
            }
            ++walls;
            if (wallClassAt(nx, ny, z) == WallClass::Timber && wallNeighbours(nx, ny, z) > 1) {
                runBeside = true;
            }
        }
        return walls == 0 || (walls == 1 && !runBeside);
    }

    /// The harbour within two cells of a cell, on its own level or the one
    /// under it: a mooring post stands a cell back from the coping.
    [[nodiscard]] bool besideWater(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept {
        for (std::int32_t dy = -kWaterReach; dy <= kWaterReach; ++dy) {
            for (std::int32_t dx = -kWaterReach; dx <= kWaterReach; ++dx) {
                if (std::abs(dx) + std::abs(dy) > kWaterReach || (dx == 0 && dy == 0)) {
                    continue;
                }
                if (isWater(tiles_, x + dx, y + dy, z) || isWater(tiles_, x + dx, y + dy, z - 1)) {
                    return true;
                }
            }
        }
        return false;
    }

    /// A post that keeps its boards: beside the water, where the tarred
    /// core carries the pile -- on the quay, or under a pier's deck.
    [[nodiscard]] bool boardedPost(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept {
        return isPost(x, y, z) && besideWater(x, y, z);
    }

    /// A timber post WITH A JOB ON THE STREET, out of doors and clear of
    /// the water: within kDoorReach cells of a door gap's cell (the jamb
    /// itself, the hitching post against the wall beside the door), or one
    /// half of a pair two cells apart with a walkable cell between and sky
    /// over both -- the pair railBetweenPosts() rails, read from either
    /// end. Such a post is the strapped timber post fitted to its cell
    /// (posts()); a lone pier with neither is the pillar. The caller has
    /// already asked isPost().
    [[nodiscard]] bool doorPostAt(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept {
        if (cellRoofed(tiles_, x, y, z) || boardedPost(x, y, z)) {
            return false;
        }
        for (std::int32_t dy = -kDoorReach; dy <= kDoorReach; ++dy) {
            for (std::int32_t dx = -kDoorReach; dx <= kDoorReach; ++dx) {
                if (gapAt(x + dx, y + dy, z) != nullptr) {
                    return true;
                }
            }
        }
        for (int s = 0; s < 4; ++s) {
            const std::int32_t px = x + 2 * kSideDx[s];
            const std::int32_t py = y + 2 * kSideDy[s];
            if (!isWalkableForm(tiles_, x + kSideDx[s], y + kSideDy[s], z) || cellRoofed(tiles_, px, py, z)) {
                continue;
            }
            // The first of the pair rails east or south to a lone post; the
            // second is the lone post a first (no pile) rails to.
            const bool first = (s == kEast || s == kSouth) && lonePost(px, py, z);
            const bool second = (s == kWest || s == kNorth) && lonePost(x, y, z) && isPost(px, py, z) &&
                                !boardedPost(px, py, z);
            if (first || second) {
                return true;
            }
        }
        return false;
    }

    /// The harbour at a cell: open water, or one of the piles that stand in
    /// it (a lone timber cell).
    [[nodiscard]] bool harbourAt(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept {
        return isWater(tiles_, x, y, z) || lonePost(x, y, z);
    }

    /// A roofed room touches a wall cell: one of its eight neighbours is a
    /// floor with something over it -- the cell behind a wall's middle,
    /// the cell across the diagonal at its corner. A timber shed on the
    /// quay's edge has one against every cell of its ring; a moored hull
    /// has its open deck, roofed by nothing.
    [[nodiscard]] bool roomBeside(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept {
        for (std::int32_t dy = -1; dy <= 1; ++dy) {
            for (std::int32_t dx = -1; dx <= 1; ++dx) {
                if ((dx == 0 && dy == 0) ||
                    !isWalkableForm(tiles_, x + dx, y + dy, z) || !cellRoofed(tiles_, x + dx, y + dy, z)) {
                    continue;
                }
                return true;
            }
        }
        return false;
    }

    /// A hull face: a timber wall (not a lone post) with the harbour beside
    /// it on this level, or standing on a timber wall with the harbour
    /// beside THAT (the gunwale over a hull) -- and a BUILDING'S wall is
    /// never one, boarded upright like any other: a roofed room against
    /// it (a shed on the quay), or an open floor behind it while it stands
    /// on no hull (a fenced yard on piles, a boathouse's deck). A moored
    /// hull has a roofed hold of air behind its lower boards and its open
    /// deck behind a gunwale that stands on those boards.
    [[nodiscard]] bool hullFace(std::int32_t x, std::int32_t y, std::int32_t z,
                                int side) const noexcept {
        if (wallClassAt(x, y, z) != WallClass::Timber || isPost(x, y, z) || roomBeside(x, y, z)) {
            return false;
        }
        const bool onHull = isWall(tiles_, x, y, z - 1) && wallClassAt(x, y, z - 1) == WallClass::Timber;
        const std::int32_t bx = x - kSideDx[side];
        const std::int32_t by = y - kSideDy[side];
        const bool deckBehind = isWalkableForm(tiles_, bx, by, z) && !cellRoofed(tiles_, bx, by, z);
        if (deckBehind && !onHull) {
            return false;
        }
        const std::int32_t nx = x + kSideDx[side];
        const std::int32_t ny = y + kSideDy[side];
        if (harbourAt(nx, ny, z)) {
            return true;
        }
        return onHull && harbourAt(nx, ny, z - 1);
    }

    /// A storey's piece is fitted to the band less a centimetre, so its top
    /// cap sits just under the chunk's own wall top and the two never fight.
    [[nodiscard]] float heightScale(const PieceSpec& spec) const noexcept {
        return spec.height > 0.01F ? (render::kBandHeight - 0.01F) / spec.height : 1.0F;
    }

    [[nodiscard]] std::uint16_t indexOf(PieceRole role, std::uint8_t variant = 0) const noexcept {
        return static_cast<std::uint16_t>(std::max(0, catalogue_.pieceIndex(role, variant)));
    }

    void emit(StaticPlacement placement) {
        ++out_.stats.byRole[static_cast<std::size_t>(placement.role)];
        placement.instance.role = static_cast<std::uint8_t>(placement.role);
        placement.instance.mode = placement.mode;
        if (!placement.gradient && !placement.bilinear) {
            placement.instance.tint2 = placement.instance.tint;
            placement.instance.gradientFrom = 0.0F;
            placement.instance.gradientTo = 0.0F;
        }
        if (!placement.bilinear && !placement.alongZ) {
            placement.instance.tint3 = placement.instance.tint;
            placement.instance.tint4 = placement.instance.tint2;
            placement.instance.gradientFromZ = 0.0F;
            placement.instance.gradientToZ = 0.0F;
        }
        out_.placements.push_back(std::move(placement));
    }

    /// A piece laid along a face run over [a0, a1] with its outdoor finish
    /// facing out when `frontOut`, its local z = 0 plane `standoff` out from
    /// the face, standing on `yBase`.
    void facePiece(const FaceRun& run, PieceRole role, const PieceSpec& spec, float a0, float a1,
                   const FaceOpts& o) {
        // A quad stood ACROSS always shows its one face out and runs from
        // the a1 end along its local Z; the others turn to show the finish
        // asked for.
        const bool flip = spec.across || spec.frontNegZ != o.frontOut;
        const float aOrigin = flip ? a1 : a0;
        StaticPlacement p;
        p.role = role;
        p.gradient = o.light.gradient;
        p.flipped = flip;
        p.alongZ = spec.across;
        p.instance.gradientFrom = spec.across ? 0.0F : (spec.upright ? spec.minX : 0.0F);
        p.instance.gradientTo = spec.across ? 0.0F : (spec.upright ? spec.maxX : spec.width);
        p.instance.piece = indexOf(spec.role, spec.variant);
        p.instance.position =
            Vec3{run.baseX + kTangentX[run.side] * aOrigin + kNormalX[run.side] * o.standoff,
                 o.yBase + spec.lift,
                 run.baseZ + kTangentZ[run.side] * aOrigin + kNormalZ[run.side] * o.standoff};
        p.instance.yaw = wrapYaw(yawOf(run.side) + (spec.across ? kHalfPi : (flip ? kPi : 0.0F)) +
                                 spec.yawOffset);
        const float height = o.height > 0.0F ? o.height : render::kBandHeight - 0.01F;
        if (spec.across) {
            // Rolled a quarter turn about its Z: local X up, local Z along
            // the face (from the a1 end), its face out; the flare leans the
            // top outward. The blend runs along Z.
            p.instance.roll = kHalfPi + o.flareDegrees * kDegToRad;
            const float thick = spec.twoSided ? -spec.scale : spec.scale;
            p.instance.scale = Vec3{height / std::max(0.01F, spec.maxX - spec.minX) * spec.scale, thick,
                                    (a1 - a0) / std::max(0.01F, spec.maxZ - spec.minZ) * spec.scale};
            p.instance.gradientFromZ = spec.minZ;
            p.instance.gradientToZ = spec.maxZ;
        } else if (spec.upright) {
            // A flat quad on its edge: local Z up (a quarter turn about X
            // brings +Y, its face, to -Z), so the height is its Z scale. A
            // flare tips the top outward. An extent that does not start at
            // the origin (the flag piece) is brought back to it.
            p.instance.pitch = -kHalfPi - o.flareDegrees * kDegToRad;
            const float depth = std::max(0.01F, spec.maxZ - spec.minZ);
            const float thick = spec.twoSided ? -spec.scale : spec.scale;
            const float sx = (a1 - a0) / std::max(0.01F, spec.maxX - spec.minX) * spec.scale;
            const float sz = height / depth * spec.scale;
            p.instance.scale = Vec3{sx, thick, sz};
            const float back = (flip ? 1.0F : -1.0F) * spec.minX * sx;
            p.instance.position.x += kTangentX[run.side] * back;
            p.instance.position.z += kTangentZ[run.side] * back;
            p.instance.position.y -= spec.minZ * sz;
        } else {
            const float ys = spec.height > 0.01F ? height / spec.height : 1.0F;
            const float thick = spec.twoSided ? -spec.scale : spec.scale;
            p.instance.scale = Vec3{(a1 - a0) / spec.width * spec.scale, ys * spec.scale, thick};
        }
        p.instance.tint = mulTint(spec.tint, o.tint);
        p.instance.tint2 = p.instance.tint;
        p.selfLit = o.selfLit || spec.selfLit;
        p.lightX = o.light.firstX;
        p.lightY = o.light.firstY;
        p.lightX2 = o.light.gradient ? o.light.lastX : o.light.firstX;
        p.lightY2 = o.light.gradient ? o.light.lastY : o.light.firstY;
        p.endAX = o.light.gradient ? o.light.beforeX : o.light.firstX;
        p.endAY = o.light.gradient ? o.light.beforeY : o.light.firstY;
        p.endBX = o.light.gradient ? o.light.afterX : o.light.firstX;
        p.endBY = o.light.gradient ? o.light.afterY : o.light.firstY;
        p.endAT = o.light.gradient ? o.light.firstT : 0.0F;
        p.endBT = o.light.gradient ? o.light.lastT : 0.0F;
        p.lightZ = run.z;
        p.facing = (run.side == kNorth || run.side == kSouth) ? render::kFacingY
                                                              : render::kFacingX;
        p.radius = std::max(a1 - a0, height) + 1.0F;
        emit(std::move(p));
    }

    /// The light of a piece over ONE cell (no blend).
    [[nodiscard]] static FaceLight cellLight(std::int32_t x, std::int32_t y) noexcept {
        FaceLight l;
        l.firstX = l.lastX = l.beforeX = l.afterX = x;
        l.firstY = l.lastY = l.beforeY = l.afterY = y;
        return l;
    }

    /// The light of a piece cut from `from` to `to` along a run of `cells`
    /// cells from (x0, y0) stepping (dx, dy), in tiles from the run's start:
    /// at each cut, the two cell centres it falls between and its place
    /// between them. Past the run's ends, the end cell alone -- unless that
    /// end is `beyondA0` / `beyondA1` (a door jamb): then the cut falls
    /// between the end cell and the cell beyond it, which is the value the
    /// door frame's own light takes at its edge, so the two meet.
    [[nodiscard]] static FaceLight runLight(std::int32_t x0, std::int32_t y0, std::int32_t dx,
                                            std::int32_t dy, std::int32_t cells, float from,
                                            float to, bool beyondA0 = false,
                                            bool beyondA1 = false) noexcept {
        FaceLight l;
        const auto at = [&](float cut, std::int32_t& ax, std::int32_t& ay, std::int32_t& bx,
                            std::int32_t& by, float& t) {
            const float u = cut - 0.5F;
            std::int32_t a = static_cast<std::int32_t>(std::floor(u));
            std::int32_t b = a + 1;
            t = u - static_cast<float>(a);
            if (a < 0 && !beyondA0) {
                a = b = 0;
                t = 0.0F;
            } else if (b > cells - 1 && !beyondA1) {
                a = b = cells - 1;
                t = 0.0F;
            }
            ax = x0 + dx * a;
            ay = y0 + dy * a;
            bx = x0 + dx * b;
            by = y0 + dy * b;
        };
        at(from, l.firstX, l.firstY, l.beforeX, l.beforeY, l.firstT);
        at(to, l.lastX, l.lastY, l.afterX, l.afterY, l.lastT);
        l.gradient = true;
        return l;
    }

    /// The base point of a face plane: p(a) = base + tangent * a. For north
    /// and south faces the line is a row (its z coordinate); for east and
    /// west it is a column (its x coordinate). `lineCoord` is the plane's
    /// own coordinate (y for the north face of row y, y + 1 for its south
    /// face; x for the west face of column x, x + 1 for its east face).
    static void setBase(FaceRun& run, float lineCoord) noexcept {
        if (run.side == kNorth || run.side == kSouth) {
            run.baseX = 0.0F;
            run.baseZ = lineCoord;
        } else {
            run.baseX = lineCoord;
            run.baseZ = 0.0F;
        }
    }

    /// A face run set up on one cell's face (base plane and interval), for
    /// a piece hung on that face.
    [[nodiscard]] static FaceRun cellFace(std::int32_t x, std::int32_t y, std::int32_t z,
                                          int side) noexcept {
        FaceRun r;
        r.z = z;
        r.side = side;
        const bool rows = side == kNorth || side == kSouth;
        setBase(r, rows ? static_cast<float>(side == kNorth ? y : y + 1)
                        : static_cast<float>(side == kWest ? x : x + 1));
        cellInterval(side, x, y, r.a0, r.a1);
        r.firstX = r.lastX = x;
        r.firstY = r.lastY = y;
        return r;
    }

    /// The tangent interval of one cell's face: north faces read with x,
    /// south faces against it (so the run's local +X is always the piece's
    /// +X), east with y, west against it.
    static void cellInterval(int side, std::int32_t x, std::int32_t y, float& a0,
                             float& a1) noexcept {
        switch (side) {
            case kNorth:
                a0 = static_cast<float>(x);
                break;
            case kSouth:
                a0 = -static_cast<float>(x + 1);
                break;
            case kEast:
                a0 = static_cast<float>(y);
                break;
            default:
                a0 = -static_cast<float>(y + 1);
                break;
        }
        a1 = a0 + 1.0F;
    }

    /// The step along a side's run in a-order.
    static void runStep(int side, std::int32_t& dx, std::int32_t& dy) noexcept {
        switch (side) {
            case kNorth: dx = 1; dy = 0; break;
            case kSouth: dx = -1; dy = 0; break;
            case kEast: dx = 0; dy = 1; break;
            default: dx = 0; dy = -1; break;
        }
    }

    /// A piece stood at a point with a yaw: props, lamps, furniture. The
    /// scale is uniform unless given.
    void pointPiece(PieceRole role, const PieceSpec& spec, Vec3 at, float yaw,
                    std::int32_t lx, std::int32_t ly, std::int32_t lz, Rgba8 tint = Rgba8{},
                    Vec3 scale = Vec3{1.0F, 1.0F, 1.0F}, bool selfLit = false,
                    float radius = 2.0F) {
        StaticPlacement p;
        p.role = role;
        p.instance.piece = indexOf(spec.role, spec.variant);
        p.instance.position = Vec3{at.x, at.y + spec.lift, at.z};
        p.instance.yaw = wrapYaw(yaw + spec.yawOffset);
        p.instance.scale =
            Vec3{scale.x * spec.scale, scale.y * spec.scale, scale.z * spec.scale};
        p.instance.tint = mulTint(spec.tint, tint);
        p.selfLit = selfLit || spec.selfLit;
        p.lightX = p.lightX2 = p.endAX = p.endBX = lx;
        p.lightY = p.lightY2 = p.endAY = p.endBY = ly;
        p.lightZ = lz;
        p.facing = 1.0F;
        p.radius = radius;
        // A thing with volume is shaded by its own normals; a lamp is its
        // own light and stays flat.
        p.mode = p.selfLit ? kDrawPlain : kDrawShaded;
        emit(std::move(p));
    }

    /// A flat quad (the thin plaster wall: 2.5 x 3 in its local XY, its face
    /// on +Z) centred on a point, `w` wide and `h` tall, turned to `yaw`,
    /// drawn both sides: a flame.
    void flameQuad(const PieceSpec& spec, Vec3 centre, float w, float h, float yaw, Rgba8 tint,
                   std::int32_t lx, std::int32_t ly, std::int32_t lz) {
        const float sx = w / std::max(0.01F, spec.width);
        const float sy = h / std::max(0.01F, spec.height);
        // The quad's origin is its bottom-left corner: half a width back
        // along its own +X (turned by the yaw), half a height down.
        const float c = std::cos(yaw);
        const float s = std::sin(yaw);
        // Local +X at clockwise yaw: (cos, sin) in scene XZ.
        const Vec3 at{centre.x - 0.5F * w * c, centre.y - 0.5F * h, centre.z - 0.5F * w * s};
        pointPiece(PieceRole::Flame, spec, at, yaw, lx, ly, lz, tint, Vec3{sx, sy, -1.0F}, true,
                   1.5F);
        // A halo: its alpha falls off radially over the quad, whose local X
        // spans its width and local Y its height (the adapter reads the Z
        // span as Y for this mode). A billboard: the world scene turns it
        // to the eye about its centre every frame, so it is never seen
        // edge-on as a bright bar at arm's length.
        StaticPlacement& p = out_.placements.back();
        p.mode = kDrawHalo;
        p.instance.mode = kDrawHalo;
        p.instance.gradientFrom = 0.0F;
        p.instance.gradientTo = spec.width;
        p.instance.gradientFromZ = 0.0F;
        p.instance.gradientToZ = spec.height;
        p.billboard = true;
        p.anchor = centre;
    }

    /// One flame quad at a point, faced to the eye by the world scene: the
    /// light in a lamp. (It was a crossed pair; the second quad's edge-on
    /// bar was what a body saw walking past a lantern.)
    void flameAt(Vec3 centre, float w, float h, Rgba8 tint, std::int32_t lx, std::int32_t ly,
                 std::int32_t lz) {
        const PieceSpec* flame = catalogue_.piece(PieceRole::Flame);
        if (flame == nullptr) {
            return;
        }
        flameQuad(*flame, centre, w, h, 0.0F, tint, lx, ly, lz);
    }

    // --- doors, found first ---------------------------------------------

    /// One door gap: `w` walkable cells from (x, y) along X (or along Y),
    /// in a wall line, with the street on `side`. Two or three cells wide
    /// is a door; four to six is a gate.
    struct DoorGap {
        std::int32_t x = 0, y = 0, z = 0, w = 0;
        bool alongX = true;
        int side = kNorth;
        [[nodiscard]] bool gate() const noexcept { return w > 3; }
    };

    static constexpr std::int32_t kMaxGate = 8;

    /// A walkable cell open to both north and south: a hole in an X-line.
    [[nodiscard]] bool gapCellX(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept {
        return isWalkableForm(tiles_, x, y, z) && isOpenish(tiles_, x, y - 1, z) &&
               isOpenish(tiles_, x, y + 1, z);
    }

    [[nodiscard]] bool gapCellY(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept {
        return isWalkableForm(tiles_, x, y, z) && isOpenish(tiles_, x - 1, y, z) &&
               isOpenish(tiles_, x + 1, y, z);
    }

    /// The door rule, run before the walls so the wall runs can stop dead at
    /// a jamb and the reveal pieces can start behind the frame.
    void findDoors() {
        const std::int32_t zLo = std::max(0, catalogue_.minBand());
        for (std::int32_t z = zLo; z < tiles_.sizeZ(); ++z) {
            for (std::int32_t y = 1; y + 1 < tiles_.sizeY(); ++y) {
                std::int32_t x = 0;
                while (x < tiles_.sizeX()) {
                    if (!gapCellX(x, y, z)) {
                        ++x;
                        continue;
                    }
                    std::int32_t x1 = x;
                    while (x1 + 1 < tiles_.sizeX() && gapCellX(x1 + 1, y, z)) {
                        ++x1;
                    }
                    const std::int32_t w = x1 - x + 1;
                    if (w >= 2 && w <= kMaxGate && isWall(tiles_, x - 1, y, z) &&
                        isWall(tiles_, x1 + 1, y, z) && isWall(tiles_, x - 2, y, z) &&
                        isWall(tiles_, x1 + 2, y, z) && wallClassAt(x - 1, y, z) != WallClass::None) {
                        const bool outN = !cellRoofed(tiles_, x, y - 1, z);
                        const bool outS = !cellRoofed(tiles_, x, y + 1, z);
                        if (outN != outS) {
                            DoorGap gap;
                            gap.x = x;
                            gap.y = y;
                            gap.z = z;
                            gap.w = w;
                            gap.alongX = true;
                            gap.side = outN ? kNorth : kSouth;
                            for (std::int32_t gx = x; gx <= x1; ++gx) {
                                gapOf_.emplace(cellKey(z, 0, gx, y), doors_.size());
                            }
                            doors_.push_back(gap);
                        }
                    }
                    x = x1 + 1;
                }
            }
            for (std::int32_t x = 1; x + 1 < tiles_.sizeX(); ++x) {
                std::int32_t y = 0;
                while (y < tiles_.sizeY()) {
                    if (!gapCellY(x, y, z)) {
                        ++y;
                        continue;
                    }
                    std::int32_t y1 = y;
                    while (y1 + 1 < tiles_.sizeY() && gapCellY(x, y1 + 1, z)) {
                        ++y1;
                    }
                    const std::int32_t w = y1 - y + 1;
                    if (w >= 2 && w <= kMaxGate && isWall(tiles_, x, y - 1, z) &&
                        isWall(tiles_, x, y1 + 1, z) && isWall(tiles_, x, y - 2, z) &&
                        isWall(tiles_, x, y1 + 2, z) && wallClassAt(x, y - 1, z) != WallClass::None) {
                        const bool outW = !cellRoofed(tiles_, x - 1, y, z);
                        const bool outE = !cellRoofed(tiles_, x + 1, y, z);
                        if (outW != outE) {
                            DoorGap gap;
                            gap.x = x;
                            gap.y = y;
                            gap.z = z;
                            gap.w = w;
                            gap.alongX = false;
                            gap.side = outE ? kEast : kWest;
                            for (std::int32_t gy = y; gy <= y1; ++gy) {
                                gapOf_.emplace(cellKey(z, 0, x, gy), doors_.size());
                            }
                            doors_.push_back(gap);
                        }
                    }
                    y = y1 + 1;
                }
            }
        }
    }

    /// The door gap a cell belongs to, or null.
    [[nodiscard]] const DoorGap* gapAt(std::int32_t x, std::int32_t y, std::int32_t z) const {
        const auto found = gapOf_.find(cellKey(z, 0, x, y));
        return found == gapOf_.end() ? nullptr : &doors_[found->second];
    }

    /// The facade plane of a gap as a face run over the gap's interval
    /// [lo, hi], and the jamb cell at its a0 end.
    [[nodiscard]] FaceRun gapFace(const DoorGap& gap, float& lo, float& hi, std::int32_t& jambX,
                                  std::int32_t& jambY) const noexcept {
        FaceRun r;
        r.z = gap.z;
        r.side = gap.side;
        float dummy = 0.0F;
        jambX = gap.x;
        jambY = gap.y;
        if (gap.alongX) {
            // The facade plane: the gap row's north or south edge.
            setBase(r, static_cast<float>(gap.side == kNorth ? gap.y : gap.y + 1));
            const std::int32_t x1 = gap.x + gap.w - 1;
            if (gap.side == kNorth) {
                cellInterval(kNorth, gap.x, gap.y, lo, dummy);
                cellInterval(kNorth, x1, gap.y, dummy, hi);
            } else {
                cellInterval(kSouth, x1, gap.y, lo, dummy);
                cellInterval(kSouth, gap.x, gap.y, dummy, hi);
            }
            jambX = gap.x - 1;
        } else {
            setBase(r, static_cast<float>(gap.side == kWest ? gap.x : gap.x + 1));
            const std::int32_t y1 = gap.y + gap.w - 1;
            if (gap.side == kEast) {
                cellInterval(kEast, gap.x, gap.y, lo, dummy);
                cellInterval(kEast, gap.x, y1, dummy, hi);
            } else {
                cellInterval(kWest, gap.x, y1, lo, dummy);
                cellInterval(kWest, gap.x, gap.y, dummy, hi);
            }
            jambY = gap.y - 1;
        }
        r.firstX = r.lastX = gap.x;
        r.firstY = r.lastY = gap.y;
        return r;
    }

    /// The frames: one per gap, in the plane of the facade, the building's
    /// own finish to the street; behind it, on a rendered building, the
    /// plaster the room should see; a leaf hung open on each jamb; a post
    /// and a lintel for a gate.
    void doors() {
        const PieceSpec* door = catalogue_.piece(PieceRole::WallDoor);
        const PieceSpec* wall = catalogue_.piece(PieceRole::Wall);
        const PieceSpec* inside = catalogue_.piece(PieceRole::DoorInside);
        const PieceSpec* leaf = catalogue_.piece(PieceRole::DoorLeaf);
        const PieceSpec* gatePost = catalogue_.piece(PieceRole::GatePost);
        const PieceSpec* lintel = catalogue_.piece(PieceRole::Joist);
        const float wallThick = wall != nullptr ? wall->thickness : 0.225F;
        for (const DoorGap& gap : doors_) {
            float lo = 0.0F, hi = 0.0F;
            std::int32_t jambX = 0, jambY = 0;
            const FaceRun r = gapFace(gap, lo, hi, jambX, jambY);
            const MaterialRule* jamb = rule(jambX, jambY, gap.z);
            const bool brickOut = jamb == nullptr || !jamb->plasterOut;
            const std::int32_t farX = gap.x + (gap.alongX ? gap.w - 1 : 0);
            const std::int32_t farY = gap.y + (gap.alongX ? 0 : gap.w - 1);
            // Lit across the gap in the facade's own a-order, and at each
            // edge between the last gap cell and the jamb beyond it: the
            // value the wall's piece takes where it meets the frame.
            std::int32_t gdx = 0, gdy = 0;
            runStep(gap.side, gdx, gdy);
            const bool forward = gdx + gdy > 0;
            const FaceLight light = runLight(forward ? gap.x : farX, forward ? gap.y : farY, gdx, gdy, gap.w,
                                             0.0F, static_cast<float>(gap.w), true, true);
            const Rgba8 outTint = jamb != nullptr ? jamb->tint : Rgba8{};
            const Rgba8 inTint = jamb != nullptr ? jamb->insideTint : Rgba8{};
            const float yBase = render::bandSurface(gap.z);
            ++out_.stats.doorGaps;

            if (gap.gate()) {
                // A gate: no frame fits a four-tile mouth without walling
                // half of it. A post at each jamb, a beam across.
                if (gatePost != nullptr) {
                    for (int end = 0; end < 2; ++end) {
                        const float a = end == 0 ? lo + 0.2F : hi - 0.2F;
                        const Vec3 at{r.baseX + kTangentX[r.side] * a + kNormalX[r.side] * 0.1F, yBase,
                                      r.baseZ + kTangentZ[r.side] * a + kNormalZ[r.side] * 0.1F};
                        pointPiece(PieceRole::GatePost, *gatePost, at, yawOf(r.side),
                                   end == 0 ? gap.x : farX, end == 0 ? gap.y : farY, gap.z, outTint,
                                   Vec3{1.0F, (render::kBandHeight - 0.05F) /
                                                  std::max(0.1F, gatePost->height),
                                        1.0F},
                                   false, 3.5F);
                    }
                }
                if (lintel != nullptr) {
                    beamAlong(r, lo, hi, yBase + kGateLintelLift, 0.1F, outTint, light, gap.z);
                }
                continue;
            }
            if (door == nullptr) {
                continue;
            }
            const float standoff = door->standoffSet ? door->standoff : wallThick * 0.5F;
            FaceOpts o;
            o.frontOut = brickOut;
            o.standoff = standoff;
            o.yBase = yBase;
            o.tint = outTint;
            o.light = light;
            facePiece(r, PieceRole::WallDoor, *door, lo, hi, o);

            // The inside: on a rendered building the frame was turned to
            // show its plaster to the street, so its brick faces the room.
            // The thin plaster wall stands a hair behind the frame's inner
            // face, cut to the frame's header and jambs so the opening
            // stays open.
            const float sx = (hi - lo) / std::max(0.01F, door->width);
            const float hs = heightScale(*door);
            const float jambW = door->cutX * sx;
            const float openTop = door->cutY * hs;
            if (!brickOut && inside != nullptr && door->cutX > 0.0F && door->cutY > 0.0F) {
                FaceOpts in;
                in.frontOut = false;
                in.standoff = standoff - door->thickness * 0.5F - 0.015F;
                in.yBase = yBase;
                in.tint = inTint;
                in.light = light;
                // The header, over the whole gap.
                in.height = render::kBandHeight - 0.01F - openTop - 0.005F;
                in.yBase = yBase + openTop + 0.005F;
                facePiece(r, PieceRole::DoorInside, *inside, lo, hi, in);
                // The jambs, from the floor to the header.
                in.height = openTop + 0.005F;
                in.yBase = yBase;
                if (jambW > 0.02F) {
                    facePiece(r, PieceRole::DoorInside, *inside, lo, lo + jambW - 0.005F, in);
                    facePiece(r, PieceRole::DoorInside, *inside, hi - jambW + 0.005F, hi, in);
                }
            }
            // The frame's ends stand in the reveal planes, their brick
            // cross-section facing into the doorway: a plaster quad over
            // each, inside the frame's hollow end, over the WALL's thickness
            // (the frame's own thickness counts its trim, which stands
            // proud of the wall on both sides but does not reach the end)
            // less a hair each way -- proud of either face, the quad's
            // edge is a bright hairline beside the frame from the street.
            if (inside != nullptr) {
                const float outer = standoff + wallThick * 0.5F - kReturnInset;
                const float inner = standoff - wallThick * 0.5F + kReturnInset;
                for (int end = 0; end < 2; ++end) {
                    // The reveal plane at the a0 end faces +tangent (into
                    // the gap); at the a1 end -tangent. As a face run: the
                    // side one quarter turn from the facade's.
                    const int face = end == 0 ? (r.side + 1) & 3 : (r.side + 3) & 3;
                    FaceRun jambFace;
                    jambFace.z = gap.z;
                    jambFace.side = face;
                    const float a = end == 0 ? lo : hi;
                    // The plane's base point is the jamb edge on the facade
                    // plane; the quad runs from `outer` out of doors to the
                    // reveal inset in.
                    const float edgeX = r.baseX + kTangentX[r.side] * a;
                    const float edgeZ = r.baseZ + kTangentZ[r.side] * a;
                    jambFace.baseX = edgeX;
                    jambFace.baseZ = edgeZ;
                    // Along this face's tangent, the street lies toward the
                    // facade's normal: for the a0-end face (a quarter turn
                    // on) that is -tangent; for the a1-end face +tangent.
                    const float sign = end == 0 ? -1.0F : 1.0F;
                    const float b0 = std::min(sign * outer, sign * inner);
                    const float b1 = std::max(sign * outer, sign * inner);
                    FaceOpts ret;
                    ret.frontOut = true;
                    ret.standoff = 0.006F;
                    ret.yBase = yBase;
                    ret.tint = inTint;
                    ret.light = cellLight(gap.x, gap.y);
                    facePiece(jambFace, PieceRole::DoorInside, *inside, b0, b1, ret);
                }
            }
            // The leaves: one on each jamb, hinged at the jamb's inner
            // edge, swung inward to lie along the reveal.
            if (leaf != nullptr) {
                const float leafLength = std::max(0.1F, leaf->maxX - leaf->minX);
                for (int end = 0; end < 2; ++end) {
                    // The hinge: just inside the jamb's edge, a little off
                    // the reveal, at the facade plane.
                    const float a = end == 0 ? lo + jambW + kLeafOffReveal : hi - jambW - kLeafOffReveal;
                    Vec3 hinge{r.baseX + kTangentX[r.side] * a - kNormalX[r.side] * kLeafOffFacade,
                               yBase + leaf->lift,
                               r.baseZ + kTangentZ[r.side] * a - kNormalZ[r.side] * kLeafOffFacade};
                    // The leaf's local +X runs from the hinge along the
                    // leaf: a quarter turn from the facade's tangent, into
                    // the building. The far jamb's leaf is TURNED a half
                    // turn and hung from its far end rather than mirrored:
                    // a mirrored piece is drawn both-sided, and the back
                    // faces of the leaf's edge showed as a red seam from
                    // the street. Turned, it is culled like everything
                    // else and shows its other face, which a swung-open
                    // pair does anyway.
                    float yaw = yawOf(r.side) + kHalfPi;
                    if (end == 1) {
                        yaw += kPi;
                        hinge.x -= kNormalX[r.side] * leafLength;
                        hinge.z -= kNormalZ[r.side] * leafLength;
                    }
                    pointPiece(PieceRole::DoorLeaf, *leaf, hinge, yaw, end == 0 ? gap.x : farX,
                               end == 0 ? gap.y : farY, gap.z, outTint, Vec3{1.0F, 1.0F, 1.0F}, false,
                               2.5F);
                }
            }
        }
    }

    /// A beam laid along a face run at [a0, a1] on the run's plane
    /// (`standoff` out), its centre at `y`: a gate's lintel, a gunwale.
    void beamAlong(const FaceRun& r, float a0, float a1, float y, float standoff, Rgba8 tint,
                   const FaceLight& light, std::int32_t z, PieceRole role = PieceRole::Joist) {
        const PieceSpec* beam = catalogue_.piece(role);
        if (beam == nullptr) {
            return;
        }
        // The kit beam runs along its local +Z from its origin; at yaw 0
        // that is south, so three quarter turns clockwise put it along the
        // run's tangent.
        StaticPlacement p;
        p.role = role;
        p.instance.piece = indexOf(role);
        p.instance.position = Vec3{r.baseX + kTangentX[r.side] * a0 + kNormalX[r.side] * standoff,
                                   y + beam->lift,
                                   r.baseZ + kTangentZ[r.side] * a0 + kNormalZ[r.side] * standoff};
        p.instance.yaw = wrapYaw(yawOf(r.side) + 3.0F * kHalfPi + beam->yawOffset);
        // Its section at the catalogue scale; its length the span exactly.
        p.instance.scale = Vec3{beam->scale, beam->scale, (a1 - a0) / std::max(0.01F, beam->maxZ - beam->minZ)};
        p.instance.tint = mulTint(beam->tint, tint);
        p.gradient = light.gradient;
        // The beam's local Z runs the span: the blend is along it, which
        // the adapter takes as a Z gradient.
        p.instance.gradientFrom = 0.0F;
        p.instance.gradientTo = 0.0F;
        p.bilinear = light.gradient;
        p.instance.gradientFromZ = beam->minZ;
        p.instance.gradientToZ = beam->maxZ;
        // Its four corners in the piece's order (min/min, max/min,
        // min/max, max/max): the a0 end at local Z = 0, the a1 end at
        // the far end.
        p.lightX = p.lightX2 = light.firstX;
        p.lightY = p.lightY2 = light.firstY;
        p.endAX = p.endBX = light.lastX;
        p.endAY = p.endBY = light.lastY;
        p.lightZ = z;
        p.facing = 1.0F;
        p.radius = (a1 - a0) + 1.0F;
        emit(std::move(p));
    }

    // --- walls ------------------------------------------------------------

    /// How far a reveal (a jamb face inside a door gap) starts behind the
    /// facade plane, so it clears the frame that stands in that plane.
    static constexpr float kRevealInset = 0.05F;

    void walls() {
        const PieceSpec* wall = catalogue_.piece(PieceRole::Wall);
        if (wall == nullptr) {
            return;
        }
        const PieceSpec* window = catalogue_.piece(PieceRole::WallWindow);
        const PieceSpec* cornerSpec = catalogue_.piece(PieceRole::WallCorner);
        const PieceSpec* timber = catalogue_.piece(PieceRole::WallTimber);
        const PieceSpec* hullSpec = catalogue_.piece(PieceRole::Hull);
        const PieceSpec* timberWindow = catalogue_.piece(PieceRole::WindowTimber);
        const PieceSpec* plasterQuad = catalogue_.piece(PieceRole::WallPlaster);
        const PieceSpec* quayWall = catalogue_.piece(PieceRole::QuayWall);
        const RuleKnobs& knobs = catalogue_.knobs();

        std::vector<FaceRun> runs;
        std::map<std::uint64_t, std::size_t> runOf;
        scanRuns(runs, runOf, /*roofEdge=*/false);
        out_.stats.wallRuns = runs.size();

        // Corners: a cell with exactly two exposed adjacent sides, both out
        // of doors, brick-finished masonry, that ends a run on each.
        std::vector<Corner> corners;
        if (cornerSpec != nullptr) {
            for (std::int32_t z = std::max(0, catalogue_.minBand()); z < tiles_.sizeZ(); ++z) {
                for (std::int32_t y = 0; y < tiles_.sizeY(); ++y) {
                    for (std::int32_t x = 0; x < tiles_.sizeX(); ++x) {
                        if (!isWall(tiles_, x, y, z) || wallClassAt(x, y, z) != WallClass::Masonry ||
                            rule(x, y, z)->plasterOut) {
                            continue;
                        }
                        int exposed = 0;
                        bool has[4] = {false, false, false, false};
                        for (int s = 0; s < 4; ++s) {
                            has[s] = faceExposed(x, y, z, s);
                            exposed += has[s] ? 1 : 0;
                        }
                        if (exposed != 2) {
                            continue;
                        }
                        int side = -1;
                        for (int s = 0; s < 4; ++s) {
                            if (has[s] && has[(s + 1) & 3]) {
                                side = s;
                            }
                        }
                        if (side < 0 || !faceOutdoor(x, y, z, side) ||
                            !faceOutdoor(x, y, z, (side + 1) & 3)) {
                            continue;
                        }
                        const auto a = runOf.find(cellKey(z, side, x, y));
                        const auto b = runOf.find(cellKey(z, (side + 1) & 3, x, y));
                        if (a == runOf.end() || b == runOf.end()) {
                            continue;
                        }
                        // Both runs must end here as convex ends: a jamb is
                        // not a corner.
                        if (!runs[a->second].convexA1 || !runs[b->second].convexA0) {
                            continue;
                        }
                        Corner c;
                        c.x = x;
                        c.y = y;
                        c.z = z;
                        c.side = side;
                        c.material = tiles_.material(x, y, z);
                        const MaterialRule* r = rule(x, y, z);
                        c.tint = r != nullptr ? r->tint : Rgba8{};
                        c.runA = a->second;
                        c.runB = b->second;
                        runs[c.runA].cornerA1 = static_cast<int>(corners.size());
                        runs[c.runB].cornerA0 = static_cast<int>(corners.size());
                        corners.push_back(c);
                    }
                }
            }
            // The leg length: a corner's legs are the kit's own module
            // unless a run is too short to carry them, in which case both
            // legs shrink to the run's share.
            const auto share = [&runs, wall](std::size_t runIndex) {
                const FaceRun& r = runs[runIndex];
                const float len = r.a1 - r.a0;
                const int cornersOnRun = (r.cornerA0 >= 0 ? 1 : 0) + (r.cornerA1 >= 0 ? 1 : 0);
                return std::min(1.0F, len / (wall->width * static_cast<float>(std::max(1, cornersOnRun))));
            };
            for (Corner& c : corners) {
                c.s = std::min(share(c.runA), share(c.runB));
                placeCorner(c, *cornerSpec);
            }
        }

        for (const FaceRun& r : runs) {
            // A timber post is a pillar (posts()) and is not boarded, unless
            // it stands beside the water: a tarred core under its pile.
            if (r.post && !boardedPost(r.firstX, r.firstY, r.z)) {
                continue;
            }
            // The piece this run wears: boards for timber, the kit wall
            // otherwise -- and a plaster face is cut per tile, since plaster
            // has no pattern to stretch and the light then reads per cell
            // exactly as the chunk's own faces do.
            const bool boards = r.cls == WallClass::Timber && timber != nullptr;
            // A hull wears its own quad, boards across, when the catalogue
            // has one.
            const bool hullBoards = boards && r.hull && hullSpec != nullptr;
            const bool brickOut = r.brickOut;
            const bool plasterFace = !boards && !brickOut;
            // A masonry face at the harbour band with the water beside it
            // is a quay wall: the stone piece stood on edge, coping to
            // water, when the catalogue has one.
            const bool quay = r.cls == WallClass::Masonry && r.z == catalogue_.minBand() && quayWall != nullptr &&
                              isWater(tiles_, r.firstX + kSideDx[r.side], r.firstY + kSideDy[r.side], r.z);
            // A plaster face is the thin one-sided quad when the catalogue
            // has one (no brick back, no brick end at a corner), else the
            // kit wall's plaster side.
            const bool thinPlaster = plasterFace && plasterQuad != nullptr && !quay;
            const PieceSpec& piece = quay          ? *quayWall
                                     : hullBoards  ? *hullSpec
                                     : boards      ? *timber
                                     : thinPlaster ? *plasterQuad
                                                   : *wall;
            // A convex end extends by the piece's thickness less a hair, so
            // the end cap sits INSIDE the perpendicular piece's body rather
            // than on its surface plane, where the two would fight; a thin
            // quad extends to its standoff, where the perpendicular quad's
            // plane is, and the two meet at the corner.
            const float standoffOf = piece.standoffSet ? piece.standoff : piece.thickness * 0.5F;
            const float ext = thinPlaster ? standoffOf + 0.005F : std::max(0.0F, piece.thickness - 0.01F);
            const float legLen = wall->width - wall->thickness * 0.5F;
            float lo = r.a0;
            float hi = r.a1;
            if (r.cornerA0 >= 0) {
                lo += legLen * corners[static_cast<std::size_t>(r.cornerA0)].s;
            } else if (r.convexA0) {
                lo -= ext;
            }
            if (r.cornerA1 >= 0) {
                hi -= legLen * corners[static_cast<std::size_t>(r.cornerA1)].s;
            } else if (r.convexA1) {
                hi += ext;
            }
            if (r.revealA0) {
                lo = r.a0 + kRevealInset;
            }
            if (r.revealA1) {
                hi = r.a1 - kRevealInset;
            }
            // A one-sided quad on an INDOOR face that ends at a door's jamb
            // extends past it too: the reveal's quad closes the slot behind
            // this one, and this one closes the slot behind the reveal's --
            // out of doors the frame's own body does that.
            if ((boards || thinPlaster) && !r.outdoor) {
                if (r.gapA0 && !r.reveal) {
                    lo -= ext;
                }
                if (r.gapA1 && !r.reveal) {
                    hi += ext;
                }
            }
            if (hi - lo < 0.05F) {
                continue;
            }
            const std::int32_t cells = std::max(1, static_cast<std::int32_t>(std::lround(r.a1 - r.a0)));
            std::int32_t dx = 0, dy = 0;
            runStep(r.side, dx, dy);

            // The cut. Plaster is cut every two tiles: no pattern to
            // stretch, the light close to the chunk's own per-cell steps.
            // Brick is laid AT ITS OWN MODULE so the courses meet across
            // every seam: whole pieces at 1.0 and one filler at the far
            // end, or the last piece stretched a little when the remainder
            // is too short to be a piece of its own. Boards are cut to the
            // module too.
            // A run that ends at a door's jamb (and starts at no door) is
            // cut from the door, so the bay beside the door is a whole one
            // and the remainder falls at the far end.
            const bool fromFar = r.gapA1 && !r.gapA0;
            std::vector<float> cuts;
            cuts.push_back(lo);
            if (plasterFace) {
                // Every two cells, the cuts on the grid: a piece per pair,
                // the last one a single cell when the run is odd.
                for (std::int32_t c = 2; c < cells; c += 2) {
                    const float cut = fromFar ? r.a1 - static_cast<float>(c) : r.a0 + static_cast<float>(c);
                    if (cut > lo + 0.3F && cut < hi - 0.3F) {
                        cuts.push_back(cut);
                    }
                }
            } else {
                const float module = std::max(0.5F, piece.width);
                const float total = hi - lo;
                const int whole = static_cast<int>(std::floor(total / module + 1.0e-4F));
                const float rem = total - static_cast<float>(whole) * module;
                const auto cutAt = [&](int i) {
                    return fromFar ? hi - module * static_cast<float>(i) : lo + module * static_cast<float>(i);
                };
                if (whole == 0) {
                    // Shorter than a module: one piece, squeezed.
                } else if (rem < 0.35F * module) {
                    // The last whole piece stretches over the remainder.
                    for (int i = 1; i < whole; ++i) {
                        cuts.push_back(cutAt(i));
                    }
                } else {
                    for (int i = 1; i <= whole; ++i) {
                        cuts.push_back(cutAt(i));
                    }
                }
            }
            if (fromFar) {
                std::reverse(cuts.begin() + 1, cuts.end());
            }
            cuts.push_back(hi);
            const int k = static_cast<int>(cuts.size()) - 1;
            const bool windowOk = r.outdoor && r.cls == WallClass::Masonry && !r.hull && !quay;
            const bool timberWindowOk =
                r.outdoor && boards && !r.hull && !r.post && timberWindow != nullptr;
            const bool hullTopOpen =
                r.hull && tiles_.form(r.firstX, r.firstY, r.z + 1) == content::TileForm::Open;
            // The window rhythm counts the pieces that could carry one: on
            // a patterned (brick, board) run the whole modules only, so a
            // window is never a squeezed one; on plaster every piece wide
            // enough; never a piece carrying a corner extension (the window
            // piece is a box, and its brick end would show). Ranked from
            // the door's end when the run ends at a jamb, and there the
            // rhythm starts on the door's own bay: a window beside the door.
            const auto eligibleAt = [&](int i) {
                const float w = cuts[static_cast<std::size_t>(i) + 1] - cuts[static_cast<std::size_t>(i)];
                const bool whole = plasterFace ? w >= kWindowMinLength : std::fabs(w - piece.width) < 0.05F;
                const bool onExtension = (i == 0 && lo < r.a0 - 0.001F) || (i + 1 == k && hi > r.a1 + 0.001F);
                return whole && !onExtension;
            };
            std::vector<int> rank(static_cast<std::size_t>(k), -1);
            for (int j = 0, n = 0; j < k; ++j) {
                const int i = fromFar ? k - 1 - j : j;
                if (eligibleAt(i)) {
                    rank[static_cast<std::size_t>(i)] = n++;
                }
            }
            const int phase = (r.gapA0 || r.gapA1) ? 0 : knobs.windowEvery / 2;
            for (int i = 0; i < k; ++i) {
                const float pa0 = cuts[static_cast<std::size_t>(i)];
                const float pa1 = cuts[static_cast<std::size_t>(i) + 1];
                const float len = pa1 - pa0;
                // The cells under the piece: its light, and the window sits
                // over its middle one.
                const std::int32_t along = std::clamp(
                    static_cast<std::int32_t>(std::floor(pa0 + 0.5F * len - r.a0)), 0, cells - 1);
                const std::int32_t lx = r.firstX + dx * along;
                const std::int32_t ly = r.firstY + dy * along;
                PieceRole role = quay           ? PieceRole::QuayWall
                                 : hullBoards   ? PieceRole::Hull
                                 : boards       ? PieceRole::WallTimber
                                 : thinPlaster  ? PieceRole::WallPlaster
                                                : PieceRole::Wall;
                const PieceSpec* spec = &piece;
                // The window rhythm: one eligible piece in `windowEvery`
                // along the run, by rank -- the second, the fourth from a
                // plain end; the first, the third from a door -- and only
                // where a room stands behind the wall.
                const int rk = rank[static_cast<std::size_t>(i)];
                const bool rhythm = rk >= 0 && knobs.windowEvery > 0 && (rk % knobs.windowEvery) == phase;
                const bool roomBehind = isOpenish(tiles_, lx - kSideDx[r.side], ly - kSideDy[r.side], r.z);
                if (windowOk && window != nullptr && rhythm && roomBehind) {
                    role = PieceRole::WallWindow;
                    spec = window;
                }
                FaceOpts o;
                // A board or plaster quad has one face and it looks out; the
                // kit wall (and the window piece in it) shows its brick or
                // its plaster.
                o.frontOut = boards || brickOut || quay || (thinPlaster && spec == &piece);
                o.standoff = spec->standoffSet ? spec->standoff : spec->thickness * 0.5F;
                o.yBase = render::bandSurface(r.z);
                o.tint = r.tint;
                // Lit at each cut between the two cells it falls between;
                // at a jamb, between the jamb and the gap, where the frame
                // takes the same value.
                o.light = runLight(r.firstX, r.firstY, dx, dy, cells, pa0 - r.a0, pa1 - r.a0, r.gapA0,
                                   r.gapA1);
                if (r.hull) {
                    // The boards lean out only under an open top (the
                    // gunwale); under a deck they stand plumb.
                    o.flareDegrees = hullTopOpen ? knobs.hullFlareDegrees : 0.0F;
                    o.tint = mulTint(r.tint, knobs.hullTint);
                } else if (r.post) {
                    // A pile out of doors: tarred like a hull.
                    o.tint = mulTint(r.tint, knobs.hullTint);
                }
                // A thin quad overlaps whatever it meets by a hair, proud of
                // the kit pieces by a hair, every second one a hair more.
                float qa0 = pa0;
                float qa1 = pa1;
                if (thinPlaster && spec == &piece) {
                    qa0 -= kCutOverlap;
                    qa1 += kCutOverlap;
                    o.standoff += (i & 1) != 0 ? 2.0F * kCutStagger : kCutStagger;
                }
                facePiece(r, role, *spec, qa0, qa1, o);
                if (role == PieceRole::WallWindow) {
                    StaticPlacement& p = out_.placements.back();
                    p.hasInside = true;
                    p.insideX = lx - kSideDx[r.side];
                    p.insideY = ly - kSideDy[r.side];
                    p.insideZ = r.z;
                    p.homely = isWalkableForm(tiles_, p.insideX, p.insideY, r.z) &&
                               cellRoofed(tiles_, p.insideX, p.insideY, r.z) &&
                               cellHash(p.insideX, p.insideY, r.z, kSaltPane) % kPaneDarkEvery != 0U;
                }
                // A hung window on a timber storey, on the same rhythm.
                if (timberWindowOk && rhythm && roomBehind) {
                    const float mid = 0.5F * (pa0 + pa1);
                    const float out = o.standoff + timber->thickness + timberWindow->standoff;
                    const Vec3 at{r.baseX + kTangentX[r.side] * mid + kNormalX[r.side] * out,
                                  o.yBase + kTimberWindowLift,
                                  r.baseZ + kTangentZ[r.side] * mid + kNormalZ[r.side] * out};
                    // Its front is on +Z: turned to face out.
                    pointPiece(PieceRole::WindowTimber, *timberWindow, at, yawOf(r.side) + kPi, lx,
                               ly, r.z, Rgba8{}, Vec3{1.0F, 1.0F, 1.0F}, false, 2.0F);
                    StaticPlacement& p = out_.placements.back();
                    p.hasInside = true;
                    p.insideX = lx - kSideDx[r.side];
                    p.insideY = ly - kSideDy[r.side];
                    p.insideZ = r.z;
                    p.facing = (r.side == kNorth || r.side == kSouth) ? render::kFacingY
                                                                      : render::kFacingX;
                }
            }
            // The gunwale: a beam along a hull run's open top, and the
            // mooring lines: a rope down the boards from it every few
            // cells, by the cell's own hash.
            if (r.hull && tiles_.form(r.firstX, r.firstY, r.z + 1) == content::TileForm::Open) {
                const float lean = std::sin(knobs.hullFlareDegrees * kDegToRad) * render::kBandHeight;
                beamAlong(r, r.a0, r.a1, render::bandSurface(r.z + 1) + kJoistHalfHeight,
                          lean + 0.05F, mulTint(r.tint, knobs.hullTint),
                          runLight(r.firstX, r.firstY, dx, dy, cells, 0.0F, static_cast<float>(cells)), r.z,
                          PieceRole::Gunwale);
                const PieceSpec* rope = catalogue_.piece(PieceRole::Rope);
                if (rope != nullptr) {
                    for (std::int32_t c = 0; c < cells; ++c) {
                        const std::int32_t cx = r.firstX + dx * c;
                        const std::int32_t cy = r.firstY + dy * c;
                        if (cellHash(cx, cy, r.z, kSaltRope) % kRopeEvery != 0U) {
                            continue;
                        }
                        // From the beam's outer face down to the foot of the
                        // boards, following the lean.
                        const float a = r.a0 + static_cast<float>(c) + 0.5F;
                        const float top = render::bandSurface(r.z + 1) + 0.02F;
                        const float drop = render::kBandHeight + 0.4F;
                        const float out = lean + 0.18F;
                        const Vec3 at{r.baseX + kTangentX[r.side] * a + kNormalX[r.side] * out, top - drop,
                                      r.baseZ + kTangentZ[r.side] * a + kNormalZ[r.side] * out};
                        pointPiece(PieceRole::Rope, *rope, at, yawOf(r.side), cx, cy, r.z, Rgba8{},
                                   Vec3{1.0F, drop / std::max(0.1F, rope->height), 1.0F}, false, 2.0F);
                    }
                }
            }
        }
    }

    void placeCorner(const Corner& c, const PieceSpec& spec) {
        // The cell's corner point between the two exposed sides, and the
        // piece's own brick corner (its +X/-Z corner) put on it `thickness`
        // proud both ways: at yaw 0 the offset from that point to the piece
        // origin is (t - (width + t/2), -t + t/2) scaled by the leg share.
        const float t = spec.thickness;
        const float h = t * 0.5F;
        Vec2 corner;
        switch (c.side) {
            case kNorth: corner = Vec2{static_cast<float>(c.x + 1), static_cast<float>(c.y)}; break;
            case kEast: corner = Vec2{static_cast<float>(c.x + 1), static_cast<float>(c.y + 1)}; break;
            case kSouth: corner = Vec2{static_cast<float>(c.x), static_cast<float>(c.y + 1)}; break;
            default: corner = Vec2{static_cast<float>(c.x), static_cast<float>(c.y)}; break;
        }
        const Vec2 local{c.s * (t - (spec.width + h)), c.s * (h - t)};
        const Vec2 offset = turn(local, c.side);
        StaticPlacement p;
        p.role = PieceRole::WallCorner;
        p.instance.piece = indexOf(PieceRole::WallCorner);
        p.instance.position = Vec3{corner.x + offset.x, render::bandSurface(c.z) + spec.lift,
                                   corner.z + offset.z};
        p.instance.yaw = wrapYaw(yawOf(c.side) + spec.yawOffset);
        p.instance.scale = Vec3{c.s * spec.scale, heightScale(spec) * spec.scale, c.s * spec.scale};
        p.instance.tint = mulTint(spec.tint, c.tint);
        p.lightX = p.lightX2 = p.endAX = p.endBX = c.x;
        p.lightY = p.lightY2 = p.endAY = p.endBY = c.y;
        p.lightZ = c.z;
        p.facing = (render::kFacingX + render::kFacingY) * 0.5F;
        p.radius = spec.width + render::kBandHeight;
        emit(std::move(p));
    }

    /// Every exposed, classed wall face grouped into runs. `roofEdge` asks
    /// for the outdoor faces of top-storey masonry instead (one run class).
    /// A run stops dead at a door gap (no corner extension there), and a
    /// run that faces INTO a gap -- the jamb's reveal -- is marked so its
    /// street end starts behind the frame. Hull faces and lone posts are
    /// their own runs.
    void scanRuns(std::vector<FaceRun>& runs, std::map<std::uint64_t, std::size_t>& runOf,
                  bool roofEdge) {
        const std::int32_t zLo = std::max(0, catalogue_.minBand());
        for (std::int32_t z = zLo; z < tiles_.sizeZ(); ++z) {
            for (int side = 0; side < 4; ++side) {
                const bool rows = side == kNorth || side == kSouth;
                const std::int32_t lines = rows ? tiles_.sizeY() : tiles_.sizeX();
                const std::int32_t along = rows ? tiles_.sizeX() : tiles_.sizeY();
                std::int32_t dx = 0, dy = 0;
                runStep(side, dx, dy);
                const auto close = [&](FaceRun& run) {
                    const std::int32_t nx = run.lastX + dx;
                    const std::int32_t ny = run.lastY + dy;
                    run.gapA1 = gapAt(nx, ny, z) != nullptr;
                    run.convexA1 = !isWall(tiles_, nx, ny, z) && !run.gapA1;
                    runs.push_back(run);
                };
                for (std::int32_t line = 0; line < lines; ++line) {
                    bool open = false;
                    FaceRun current;
                    for (std::int32_t i = 0; i < along; ++i) {
                        // In a-order: north/east ascend, south/west descend.
                        const std::int32_t k = (dx + dy) > 0 ? i : along - 1 - i;
                        const std::int32_t x = rows ? k : line;
                        const std::int32_t y = rows ? line : k;
                        bool qualifies = isWall(tiles_, x, y, z) && faceExposed(x, y, z, side);
                        WallClass cls = WallClass::None;
                        bool outdoor = false;
                        bool hull = false;
                        bool post = false;
                        std::uint16_t material = 0;
                        const DoorGap* facingGap = nullptr;
                        if (qualifies) {
                            cls = wallClassAt(x, y, z);
                            qualifies = cls != WallClass::None;
                        }
                        if (qualifies) {
                            outdoor = faceOutdoor(x, y, z, side);
                            material = tiles_.material(x, y, z);
                            facingGap = gapAt(x + kSideDx[side], y + kSideDy[side], z);
                            hull = hullFace(x, y, z, side);
                            post = isPost(x, y, z);
                            if (roofEdge) {
                                // A cornice belongs to masonry with a storey
                                // under it: a hull or a shed of timber has no
                                // roof line to trim, and the quay wall at
                                // the harbour band is a wall, not a roof.
                                qualifies = outdoor && cls == WallClass::Masonry &&
                                            !isWall(tiles_, x, y, z + 1) && z > catalogue_.minBand();
                            }
                        }
                        const bool joins = qualifies && open && current.material == material &&
                                           current.outdoor == outdoor && current.cls == cls &&
                                           current.hull == hull && !post && !current.post &&
                                           (facingGap != nullptr) == current.reveal;
                        if (open && !joins) {
                            close(current);
                            open = false;
                        }
                        if (!qualifies) {
                            continue;
                        }
                        float a0 = 0.0F, a1 = 0.0F;
                        cellInterval(side, x, y, a0, a1);
                        if (!open) {
                            current = FaceRun{};
                            current.z = z;
                            current.side = side;
                            const float lineCoord = rows ? static_cast<float>(side == kNorth ? y : y + 1)
                                                         : static_cast<float>(side == kWest ? x : x + 1);
                            setBase(current, lineCoord);
                            current.a0 = a0;
                            current.firstX = x;
                            current.firstY = y;
                            current.material = material;
                            current.cls = cls;
                            current.outdoor = outdoor;
                            current.hull = hull;
                            current.post = post;
                            const MaterialRule* r = rule(x, y, z);
                            // Out of doors the face wears the material's
                            // tint on its finish (brick, or plaster for a
                            // rendered building); an indoor masonry face is
                            // plaster in its own tint.
                            const bool plaster = cls == WallClass::Masonry && !outdoor;
                            current.tint = r == nullptr ? Rgba8{} : (plaster ? r->insideTint : r->tint);
                            current.brickOut = outdoor && cls == WallClass::Masonry &&
                                               (r == nullptr || !r->plasterOut);
                            const std::int32_t px = x - dx;
                            const std::int32_t py = y - dy;
                            current.gapA0 = gapAt(px, py, z) != nullptr;
                            current.convexA0 = !isWall(tiles_, px, py, z) && !current.gapA0;
                            current.reveal = facingGap != nullptr;
                            if (current.reveal) {
                                // The street is the way the gap's outdoor
                                // side points; the run reads toward it or
                                // away from it.
                                const std::int32_t gx = kSideDx[facingGap->side];
                                const std::int32_t gy = kSideDy[facingGap->side];
                                const bool streetAtA0 = dx * gx + dy * gy < 0;
                                current.revealA0 = streetAtA0;
                                current.revealA1 = !streetAtA0;
                            }
                            open = true;
                        }
                        current.a1 = a1;
                        current.lastX = x;
                        current.lastY = y;
                        runOf.emplace(cellKey(z, side, x, y), runs.size());
                    }
                    if (open) {
                        close(current);
                    }
                }
            }
        }
    }

    // --- posts and pillars ------------------------------------------------

    /// A timber post (a lone 1x1 cell, or one against a wall of another
    /// kind): a square pillar the cell's own size (the chunk box inside it),
    /// plaster indoors and timber out of doors; beside the water the tarred
    /// boarded core keeps its boards (walls()) and one pile is driven
    /// through it, its head over the top. Out of doors WITH A JOB -- within
    /// two cells of a door gap, or one of a pair that carries a rail
    /// (doorPostAt()) -- it is the strapped timber post fitted to the cell
    /// the same way: a door frame's own timber, not a concrete column,
    /// beside the Gull's door. The cell stays a metre square and a storey
    /// tall whatever stands in it -- that is the sim's own wall cell, and
    /// the chunk box inside it is drawn whatever the catalogue says -- so
    /// the post is fitted to it edge to edge like the pillar, never thin.
    void posts() {
        const PieceSpec* pillar = catalogue_.piece(PieceRole::Pillar);
        const PieceSpec* pile = catalogue_.piece(PieceRole::Post);
        const PieceSpec* doorPost = catalogue_.piece(PieceRole::DoorPost);
        if (pillar == nullptr && pile == nullptr && doorPost == nullptr) {
            return;
        }
        const std::int32_t zLo = std::max(0, catalogue_.minBand());
        for (std::int32_t z = zLo; z < tiles_.sizeZ(); ++z) {
            for (std::int32_t y = 0; y < tiles_.sizeY(); ++y) {
                for (std::int32_t x = 0; x < tiles_.sizeX(); ++x) {
                    if (!isPost(x, y, z)) {
                        continue;
                    }
                    const Vec3 centre{static_cast<float>(x) + 0.5F, render::bandSurface(z),
                                      static_cast<float>(y) + 0.5F};
                    if (boardedPost(x, y, z)) {
                        if (pile != nullptr) {
                            // On its foot in the cell's centre, the storey
                            // tall and its head over the core -- under a
                            // deck, a hair short of it.
                            const bool decked = cellRoofed(tiles_, x, y, z);
                            const float tall = render::kBandHeight + (decked ? -0.01F : kPileHeadOver);
                            const float sy = tall / std::max(0.01F, pile->height);
                            const float sxz = kPileThickness / std::max(0.01F, pile->thickness);
                            pointPiece(PieceRole::Post, *pile, centre,
                                       static_cast<float>((x + y) & 3) * kHalfPi, x, y, z,
                                       catalogue_.knobs().hullTint, Vec3{sxz, sy, sxz}, false, 3.0F);
                        }
                        continue;
                    }
                    const MaterialRule* r = rule(x, y, z);
                    const bool indoors = cellRoofed(tiles_, x, y, z);
                    if (doorPost != nullptr && doorPostAt(x, y, z)) {
                        // THE DOOR POST. The kit's strapped timber post
                        // fitted to the cell as the pillar is (its section
                        // the cell's width plus a hair, so the chunk box is
                        // inside it), the storey tall, turned by cell, in
                        // the material's own tint -- what its boards wear.
                        // The sign and the rail hang off the cell's faces
                        // exactly as they do off the pillar's.
                        const float w = std::max(0.01F, doorPost->width);
                        const float s = (1.0F + 2.0F * doorPost->thickness) / w;
                        pointPiece(PieceRole::DoorPost, *doorPost, centre,
                                   static_cast<float>((x + y) & 3) * kHalfPi, x, y, z,
                                   r != nullptr ? r->tint : Rgba8{}, Vec3{s, heightScale(*doorPost), s},
                                   false, 3.0F);
                        signOnPost(x, y, z, indoors);
                        railBetweenPosts(x, y, z, indoors);
                        continue;
                    }
                    if (pillar == nullptr) {
                        continue;
                    }
                    // Fitted to the cell: its shaft the cell's width plus a
                    // hair, the storey its height; the catalogue's own
                    // colour indoors (a plastered pier), the material's top
                    // tint out of doors (a timber post).
                    const float w = std::max(0.01F, pillar->width);
                    const float s = (1.0F + 2.0F * pillar->thickness) / w;
                    const Rgba8 tint = indoors ? Rgba8{} : (r != nullptr ? liftTint(r->topTint, kPostLift) : Rgba8{});
                    pointPiece(PieceRole::Pillar, *pillar, centre, 0.0F, x, y, z, tint,
                               Vec3{s, heightScale(*pillar), s}, false, 3.0F);
                    signOnPost(x, y, z, indoors);
                    railBetweenPosts(x, y, z, indoors);
                }
            }
        }
    }

    /// A LONE TIMBER POST BESIDE A DOOR GAP HAS A JOB. The sim's cell is a
    /// metre square and three tall, and a pillar that size beside a door
    /// reads as nothing until it carries something: the first post along
    /// the gap's own line (the one before the gap in a-order) hangs the
    /// shop sign from its street face, out over the street -- a gatepost
    /// with the house's board on it. Indoors, or with no gap beside it, a
    /// post stays a plain pier.
    void signOnPost(std::int32_t x, std::int32_t y, std::int32_t z, bool indoors) {
        const PieceSpec* sign = catalogue_.piece(PieceRole::ShopSign);
        if (sign == nullptr || indoors) {
            return;
        }
        for (int s = 0; s < 4; ++s) {
            const DoorGap* gap = gapAt(x + kSideDx[s], y + kSideDy[s], z);
            if (gap == nullptr) {
                continue;
            }
            // The post before the gap along its line, not the one after.
            const bool before = gap->alongX ? x < gap->x : y < gap->y;
            if (!before) {
                continue;
            }
            // Hung on the face toward the street (the gap's outdoor side),
            // its bracket out along that face's normal.
            const int face = gap->side;
            const Vec3 at{static_cast<float>(x) + 0.5F + kNormalX[face] * 0.53F,
                          render::bandSurface(z) + kSignLift,
                          static_cast<float>(y) + 0.5F + kNormalZ[face] * 0.53F};
            pointPiece(PieceRole::ShopSign, *sign, at, wrapYaw(yawOf(face) + 3.0F * kHalfPi), x, y, z,
                       Rgba8{}, Vec3{kSignScale / std::max(0.01F, sign->scale),
                                     kSignScale / std::max(0.01F, sign->scale),
                                     kSignScale / std::max(0.01F, sign->scale)},
                       false, 2.5F);
            return;
        }
    }

    /// A PAIR OF POSTS ON A STREET IS A SIGN FRAME WITH A HITCHING RAIL.
    /// Two lone timber cells two apart on a row or a column, out of doors,
    /// with a walkable cell between them (the Tarwalk's pairs before the
    /// Gull): the rail beam runs from face to face at hip height, and the
    /// shop sign hangs from the first post's inner face out across the gap
    /// at sign height -- the frame an inn hangs its board in. Placed once,
    /// from the lower post of the pair. Indoors (the taproom's tables)
    /// nothing.
    void railBetweenPosts(std::int32_t x, std::int32_t y, std::int32_t z, bool indoors) {
        const PieceSpec* rail = catalogue_.piece(PieceRole::PostRail);
        const PieceSpec* sign = catalogue_.piece(PieceRole::ShopSign);
        if (rail == nullptr || indoors) {
            return;
        }
        for (int axis = 0; axis < 2; ++axis) {
            const std::int32_t dx = axis == 0 ? 1 : 0;
            const std::int32_t dy = axis == 0 ? 0 : 1;
            const std::int32_t px = x + 2 * dx;
            const std::int32_t py = y + 2 * dy;
            if (!lonePost(px, py, z) || !isWalkableForm(tiles_, x + dx, y + dy, z) ||
                cellRoofed(tiles_, px, py, z)) {
                continue;
            }
            // Along the pair's own line, read east or south, from this
            // post's far face to the other's near face.
            FaceRun r;
            r.z = z;
            r.side = axis == 0 ? kNorth : kEast;
            setBase(r, axis == 0 ? static_cast<float>(y) + 0.5F : static_cast<float>(x) + 0.5F);
            const float a0 = (axis == 0 ? static_cast<float>(x) : static_cast<float>(y)) + 1.0F;
            beamAlong(r, a0, a0 + 1.0F, render::bandSurface(z) + kRailLift, 0.0F, Rgba8{},
                      runLight(x + dx, y + dy, 0, 0, 1, 0.0F, 1.0F), z, PieceRole::PostRail);
            if (sign != nullptr) {
                // The board: its bracket's +X along the pair's line into the
                // gap, from the first post's inner face.
                const int face = axis == 0 ? kEast : kSouth;
                const Vec3 at{static_cast<float>(x) + 0.5F + kNormalX[face] * 0.53F,
                              render::bandSurface(z) + kSignLift,
                              static_cast<float>(y) + 0.5F + kNormalZ[face] * 0.53F};
                const float k = kSignScale / std::max(0.01F, sign->scale);
                pointPiece(PieceRole::ShopSign, *sign, at, wrapYaw(yawOf(face) + 3.0F * kHalfPi), x, y, z,
                           Rgba8{}, Vec3{k, k, k}, false, 2.5F);
            }
        }
    }

    // --- roof edges -------------------------------------------------------

    void roofEdges() {
        const PieceSpec* trim = catalogue_.piece(PieceRole::RoofEdge);
        const PieceSpec* wall = catalogue_.piece(PieceRole::Wall);
        if (trim == nullptr) {
            return;
        }
        std::vector<FaceRun> runs;
        std::map<std::uint64_t, std::size_t> runOf;
        scanRuns(runs, runOf, /*roofEdge=*/true);
        const float wallProud = wall != nullptr ? wall->thickness : 0.0F;
        for (const FaceRun& r : runs) {
            const float lo = r.a0 - (r.convexA0 ? wallProud : 0.0F);
            const float hi = r.a1 + (r.convexA1 ? wallProud : 0.0F);
            const int k = std::max(1, static_cast<int>(std::lround((hi - lo) / trim->width)));
            const float len = (hi - lo) / static_cast<float>(k);
            for (int i = 0; i < k; ++i) {
                const float pa0 = lo + len * static_cast<float>(i);
                const float pa1 = i + 1 == k ? hi : pa0 + len;
                FaceOpts o;
                o.frontOut = true;
                o.standoff = trim->standoffSet ? trim->standoff : wallProud;
                o.yBase = render::bandSurface(r.z + 1);
                o.tint = r.tint;
                o.light = cellLight(r.firstX, r.firstY);
                facePiece(r, PieceRole::RoofEdge, *trim, pa0, pa1, o);
            }
        }
    }

    // --- floors and water -------------------------------------------------

    [[nodiscard]] std::size_t coverIndex(std::int32_t x, std::int32_t y,
                                         std::int32_t z) const noexcept {
        return (static_cast<std::size_t>(z) * static_cast<std::size_t>(tiles_.sizeY()) +
                static_cast<std::size_t>(y)) *
                   static_cast<std::size_t>(tiles_.sizeX()) +
               static_cast<std::size_t>(x);
    }

    void ensureCover() {
        if (covered_.empty()) {
            covered_.assign(static_cast<std::size_t>(tiles_.sizeX()) *
                                static_cast<std::size_t>(tiles_.sizeY()) *
                                static_cast<std::size_t>(tiles_.sizeZ()),
                            0U);
        }
    }

    /// A fresh cover map for a pass that fits its own kind of cell (caps on
    /// wall heads, ceilings under slabs the floor pass already covered).
    void resetCover() {
        covered_.clear();
        ensureCover();
    }

    [[nodiscard]] bool covered(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept {
        return tiles_.inBounds(x, y, z) && covered_[coverIndex(x, y, z)] != 0U;
    }

    /// A flat piece fitted to the w x h block whose north-west tile is
    /// (x, y): the piece's local footprint is mapped onto the block, turned
    /// `quarterTurns` about the block's centre (a square block only). Lit
    /// at its four corner points.
    void blockPiece(PieceRole role, const PieceSpec& spec, std::int32_t x, std::int32_t y,
                    std::int32_t z, std::int32_t w, std::int32_t h, float surface,
                    const Rgba8& tint, int quarterTurns = 0) {
        const float sx = static_cast<float>(w) / std::max(0.01F, spec.maxX - spec.minX);
        const float sz = static_cast<float>(h) / std::max(0.01F, spec.maxZ - spec.minZ);
        StaticPlacement p;
        p.role = role;
        p.instance.piece = indexOf(spec.role, spec.variant);
        // The block's centre, and the piece's local footprint centre scaled:
        // the origin sits so the two coincide after the turn.
        const Vec2 centre{static_cast<float>(x) + 0.5F * static_cast<float>(w),
                          static_cast<float>(y) + 0.5F * static_cast<float>(h)};
        const Vec2 localCentre{0.5F * (spec.minX + spec.maxX) * sx, 0.5F * (spec.minZ + spec.maxZ) * sz};
        const Vec2 turned = turn(localCentre, quarterTurns);
        p.instance.position = Vec3{centre.x - turned.x, surface + spec.lift, centre.z - turned.z};
        p.instance.yaw = wrapYaw(static_cast<float>(quarterTurns) * kHalfPi + spec.yawOffset);
        p.instance.scale =
            Vec3{sx * spec.scale, spec.flipY ? -spec.scale : spec.scale, sz * spec.scale};
        p.instance.tint = mulTint(spec.tint, tint);
        // The four corner POINTS of the block in the piece's own order
        // (local min/min, max/min, min/max, max/max), turned with it.
        const Vec2 localCorners[4] = {Vec2{spec.minX * sx, spec.minZ * sz}, Vec2{spec.maxX * sx, spec.minZ * sz},
                                      Vec2{spec.minX * sx, spec.maxZ * sz}, Vec2{spec.maxX * sx, spec.maxZ * sz}};
        std::int32_t cx[4] = {}, cy[4] = {};
        for (int c = 0; c < 4; ++c) {
            const Vec2 t = turn(localCorners[c], quarterTurns);
            cx[c] = static_cast<std::int32_t>(std::lround(p.instance.position.x + t.x));
            cy[c] = static_cast<std::int32_t>(std::lround(p.instance.position.z + t.z));
        }
        p.bilinear = true;
        p.instance.gradientFrom = spec.minX;
        p.instance.gradientTo = spec.maxX;
        p.instance.gradientFromZ = spec.minZ;
        p.instance.gradientToZ = spec.maxZ;
        p.lightX = cx[0];
        p.lightY = cy[0];
        p.lightX2 = cx[1];
        p.lightY2 = cy[1];
        p.endAX = cx[2];
        p.endAY = cy[2];
        p.endBX = cx[3];
        p.endBY = cy[3];
        p.lightZ = z;
        p.facing = 1.0F;
        p.radius = static_cast<float>(std::max(w, h)) + 1.0F;
        emit(std::move(p));
    }

    /// How many rows from `y` down a w-wide run at x stacks (every cell
    /// uncovered and accepted), at most maxSide.
    template <typename Same>
    [[nodiscard]] std::int32_t stackHeight(std::int32_t x, std::int32_t y, std::int32_t z,
                                           std::int32_t w, std::int32_t maxSide,
                                           const Same& same) const {
        std::int32_t h = 1;
        while (h < maxSide && y + h < tiles_.sizeY()) {
            for (std::int32_t xx = x; xx < x + w; ++xx) {
                if (covered_[coverIndex(xx, y + h, z)] != 0U || !same(xx, y + h)) {
                    return h;
                }
            }
            ++h;
        }
        return h;
    }

    /// THE RECTANGLE MERGE, the one floor-fitting rule. Over one level, in
    /// row order, every uncovered cell that `same` accepts anchors the
    /// widest run to its east (up to `maxSide`), then the run is extended
    /// south while every cell of the next row also qualifies -- a block of
    /// w x h whole tiles, marked covered and placed with the piece fitted
    /// to it. A patterned piece asks for `minSide` on both axes; a block
    /// short of it tries narrower runs (which may stack taller) and
    /// otherwise leaves its cells for a later pass -- the flat fill.
    template <typename Same, typename Place>
    void mergeRectangles(std::int32_t z, std::int32_t minSide, std::int32_t maxSide,
                         const Same& same, const Place& place) {
        for (std::int32_t y = 0; y < tiles_.sizeY(); ++y) {
            for (std::int32_t x = 0; x < tiles_.sizeX(); ++x) {
                if (covered_[coverIndex(x, y, z)] != 0U || !same(x, y)) {
                    continue;
                }
                std::int32_t w = 1;
                while (w < maxSide && x + w < tiles_.sizeX() &&
                       covered_[coverIndex(x + w, y, z)] == 0U && same(x + w, y)) {
                    ++w;
                }
                if (w < minSide) {
                    continue;
                }
                for (std::int32_t ww = w; ww >= minSide; --ww) {
                    const std::int32_t hh = stackHeight(x, y, z, ww, maxSide, same);
                    if (hh >= minSide) {
                        markCovered(x, y, z, ww, hh);
                        place(x, y, ww, hh);
                        break;
                    }
                }
            }
        }
    }

    void markCovered(std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t w,
                     std::int32_t h) noexcept {
        for (std::int32_t yy = y; yy < y + h; ++yy) {
            for (std::int32_t xx = x; xx < x + w; ++xx) {
                if (tiles_.inBounds(xx, yy, z)) {
                    covered_[coverIndex(xx, yy, z)] = 1U;
                }
            }
        }
    }

    [[nodiscard]] bool floorCell(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept {
        return tiles_.form(x, y, z) == content::TileForm::Floor && tiles_.fluidDepth(x, y, z) == 0;
    }

    /// A floor cell of a material the catalogue dresses.
    [[nodiscard]] bool dressedFloor(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept {
        if (!floorCell(x, y, z)) {
            return false;
        }
        const MaterialRule* r = rule(x, y, z);
        return r != nullptr && (r->floorRole != PieceRole::None || r->fillRole != PieceRole::None);
    }

    /// Floors, per material and level: the material's own floor piece
    /// first (a patterned block piece wants whole 2..3 tile blocks; planks
    /// and flat fills take any rectangle), then the material's fill piece
    /// over whatever the first pass could not fit -- so no cell of a
    /// dressed material keeps the atlas tile unless the catalogue says so.
    /// A patterned piece with variants alternates them by tile hash and
    /// turns them by hash too, so the ground never reads as one repeated
    /// block.
    void floors() {
        const std::int32_t zLo = std::max(0, catalogue_.minBand());
        const std::span<const std::string_view> ids = render::materialIds();
        // Pass one: the flat fill under EVERY cell of a material that has
        // one (a patterned piece has gaps between its stones; the fill is
        // what shows through them). Pass two: the patterned pieces over it.
        // The roofs first, when the catalogue has the roof flag: every
        // building top with sky over it wears the roof finish whatever its
        // tile says -- the dark lead fill under every cell (the battens go
        // over it in roofBattens()), and the flag piece at its own module
        // (whole 3 x 3 blocks) only on a DECK, a roof a stair or a ramp
        // arrives on: every other top is lead, not paving.
        const PieceSpec* roofFlag = catalogue_.piece(PieceRole::RoofFlag);
        const PieceSpec* roofFill = catalogue_.piece(PieceRole::FloorFill);
        const RuleKnobs& knobs = catalogue_.knobs();
        for (int pass = 0; pass < 2; ++pass) {
            resetCover();
            for (std::int32_t z = zLo; z < tiles_.sizeZ(); ++z) {
                const bool roofs = roofFlag != nullptr && z >= 1;
                if (roofs) {
                    const PieceRole role = pass == 0 ? PieceRole::FloorFill : PieceRole::RoofFlag;
                    const PieceSpec* spec = pass == 0 ? roofFill : roofFlag;
                    const Rgba8 tint = pass == 0 ? knobs.roofFillTint : knobs.roofTint;
                    if (spec != nullptr) {
                        const auto roof = [&](std::int32_t x, std::int32_t y) {
                            return pass == 0 ? roofCell(x, y, z) : deckCell(x, y, z);
                        };
                        mergeRectangles(z, std::max(1, spec->minBlock), std::max(1, spec->maxBlock), roof,
                                        [&](std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h) {
                                            const int turns =
                                                w == h && spec->minBlock > 1
                                                    ? static_cast<int>((cellHash(x, y, z, kSaltCobble) >> 8) & 3U)
                                                    : 0;
                                            blockPiece(role, *spec, x, y, z, w, h, render::bandSurface(z), tint,
                                                       turns);
                                        });
                    }
                }
                for (std::size_t m = 0; m < ids.size(); ++m) {
                    const auto material = static_cast<std::uint16_t>(m);
                    const MaterialRule* r = catalogue_.material(material);
                    if (r == nullptr) {
                        continue;
                    }
                    const PieceRole role = pass == 0 ? r->fillRole : r->floorRole;
                    const PieceSpec* spec = role != PieceRole::None ? catalogue_.piece(role) : nullptr;
                    if (spec == nullptr) {
                        continue;
                    }
                    const std::uint8_t variants = catalogue_.variantCount(role);
                    const Rgba8 tint = pass == 0 ? r->fillTint : r->floorTint;
                    const auto same = [&](std::int32_t x, std::int32_t y) {
                        return floorCell(x, y, z) && tiles_.material(x, y, z) == material &&
                               !(roofs && roofCell(x, y, z));
                    };
                    mergeRectangles(
                        z, std::max(1, spec->minBlock), std::max(1, spec->maxBlock), same,
                        [&](std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h) {
                            const PieceSpec* pick = spec;
                            int turns = 0;
                            if (variants > 1 || spec->minBlock > 1) {
                                const std::uint32_t hash = cellHash(x, y, z, kSaltCobble);
                                if (variants > 1) {
                                    pick = catalogue_.piece(role, static_cast<std::uint8_t>(hash % variants));
                                }
                                // A square block turns; a patterned piece
                                // only (a plain quad is the same any way).
                                if (w == h && spec->minBlock > 1) {
                                    turns = static_cast<int>((hash >> 8) & 3U);
                                }
                            }
                            blockPiece(role, *pick, x, y, z, w, h, render::bandSurface(z), tint, turns);
                        });
                }
            }
        }
        // THE STRIPS. A cobbled street's one-wide leftovers along a
        // frontage -- the cells no 2 x 2 block could take, still covered
        // by nothing but the flat fill after pass two -- wear the flag
        // piece fitted to the one cell, its stones a third of their size:
        // setts along the kerb, turned by the cell's hash.
        const PieceSpec* strip = catalogue_.piece(PieceRole::FloorStrip);
        if (strip != nullptr) {
            for (std::int32_t z = zLo; z < tiles_.sizeZ(); ++z) {
                const bool roofs = roofFlag != nullptr && z >= 1;
                for (std::int32_t y = 0; y < tiles_.sizeY(); ++y) {
                    for (std::int32_t x = 0; x < tiles_.sizeX(); ++x) {
                        if (covered_[coverIndex(x, y, z)] != 0U || !floorCell(x, y, z) ||
                            (roofs && roofCell(x, y, z))) {
                            continue;
                        }
                        const MaterialRule* r = rule(x, y, z);
                        if (r == nullptr || r->floorRole != PieceRole::FloorCobble) {
                            continue;
                        }
                        markCovered(x, y, z, 1, 1);
                        const std::uint32_t hash = cellHash(x, y, z, kSaltCobble);
                        blockPiece(PieceRole::FloorStrip, *strip, x, y, z, 1, 1, render::bandSurface(z),
                                   r->floorTint, static_cast<int>((hash >> 8) & 3U));
                    }
                }
            }
        }
    }

    /// The side of every dressed floor slab that faces open air: the plank
    /// or plaster quad, the slab's own height, so a pier's edge or a hole
    /// in a deck never shows the atlas. Merged into runs along each side.
    void lips() {
        const std::int32_t zLo = std::max(0, catalogue_.minBand());
        const bool roofFlagged = catalogue_.piece(PieceRole::RoofFlag) != nullptr;
        for (std::int32_t z = zLo; z < tiles_.sizeZ(); ++z) {
            for (int side = 0; side < 4; ++side) {
                const bool rows = side == kNorth || side == kSouth;
                const std::int32_t lines = rows ? tiles_.sizeY() : tiles_.sizeX();
                const std::int32_t along = rows ? tiles_.sizeX() : tiles_.sizeY();
                std::int32_t dx = 0, dy = 0;
                runStep(side, dx, dy);
                for (std::int32_t line = 0; line < lines; ++line) {
                    bool open = false;
                    FaceRun current;
                    const MaterialRule* currentRule = nullptr;
                    bool currentRoof = false;
                    const auto flush = [&]() {
                        if (!open) {
                            return;
                        }
                        open = false;
                        const PieceSpec* spec = catalogue_.piece(currentRule->lipRole);
                        if (spec == nullptr) {
                            return;
                        }
                        const std::int32_t cells =
                            std::max(1, static_cast<std::int32_t>(std::lround(current.a1 - current.a0)));
                        FaceOpts o;
                        o.frontOut = true;
                        o.standoff = spec->standoffSet ? spec->standoff : 0.01F;
                        o.height = render::kFloorSlab - 0.01F;
                        o.yBase = render::bandSurface(current.z) - render::kFloorSlab + 0.005F;
                        // A roof's edge is the roof's own dark, whatever the
                        // tile under it says.
                        o.tint = currentRoof ? catalogue_.knobs().roofFillTint : currentRule->lipTint;
                        o.light = runLight(current.firstX, current.firstY, dx, dy, cells, 0.0F,
                                           static_cast<float>(cells));
                        facePiece(current, currentRule->lipRole, *spec, current.a0, current.a1, o);
                    };
                    for (std::int32_t i = 0; i < along; ++i) {
                        const std::int32_t k = (dx + dy) > 0 ? i : along - 1 - i;
                        const std::int32_t x = rows ? k : line;
                        const std::int32_t y = rows ? line : k;
                        const std::int32_t nx = x + kSideDx[side];
                        const std::int32_t ny = y + kSideDy[side];
                        const MaterialRule* r = dressedFloor(x, y, z) ? rule(x, y, z) : nullptr;
                        // Exposed: air beside the slab, and nothing solid
                        // under that air to hide the slab's side.
                        const bool exposed = r != nullptr && r->lipRole != PieceRole::None &&
                                             isAir(tiles_, nx, ny, z) && !isWall(tiles_, nx, ny, z - 1);
                        const bool roof = exposed && roofFlagged && roofCell(x, y, z);
                        if (!exposed || (open && (r != currentRule || roof != currentRoof))) {
                            flush();
                        }
                        if (!exposed) {
                            continue;
                        }
                        float a0 = 0.0F, a1 = 0.0F;
                        cellInterval(side, x, y, a0, a1);
                        if (!open) {
                            current = cellFace(x, y, z, side);
                            currentRule = r;
                            currentRoof = roof;
                            open = true;
                        }
                        current.a1 = a1;
                        current.lastX = x;
                        current.lastY = y;
                    }
                    flush();
                }
            }
        }
    }

    /// Every wall head with the sky over it, capped in the wall's colour.
    void wallCaps() {
        const PieceSpec* cap = catalogue_.piece(PieceRole::WallCap);
        if (cap == nullptr) {
            return;
        }
        resetCover();
        const std::int32_t zLo = std::max(0, catalogue_.minBand());
        const std::span<const std::string_view> ids = render::materialIds();
        for (std::int32_t z = zLo; z < tiles_.sizeZ(); ++z) {
            for (std::size_t m = 0; m < ids.size(); ++m) {
                const auto material = static_cast<std::uint16_t>(m);
                const MaterialRule* r = catalogue_.material(material);
                if (r == nullptr || r->wallClass == WallClass::None || z < r->minBand) {
                    continue;
                }
                const auto same = [&](std::int32_t x, std::int32_t y) {
                    return isWall(tiles_, x, y, z) && tiles_.material(x, y, z) == material &&
                           tiles_.form(x, y, z + 1) == content::TileForm::Open;
                };
                const Rgba8 tint = r->topTintSet ? r->topTint : r->tint;
                mergeRectangles(z, 1, std::max(1, cap->maxBlock), same,
                                [&](std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h) {
                                    blockPiece(PieceRole::WallCap, *cap, x, y, z, w, h,
                                               render::bandSurface(z + 1), tint);
                                });
            }
        }
    }

    /// A floor cell over a dressed WALL that stands inside a room: every
    /// open neighbour of the wall on its own level is a roofed walkable
    /// cell, and there is at least one. A pillar, a counter, a hearth; not
    /// the room's own outer wall.
    [[nodiscard]] bool overRoomWall(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept {
        if (!isWall(tiles_, x, y, z - 1) || wallClassAt(x, y, z - 1) == WallClass::None) {
            return false;
        }
        bool room = false;
        for (int s = 0; s < 4; ++s) {
            const std::int32_t nx = x + kSideDx[s];
            const std::int32_t ny = y + kSideDy[s];
            if (!isOpenish(tiles_, nx, ny, z - 1)) {
                continue;
            }
            if (!isWalkableForm(tiles_, nx, ny, z - 1) || !cellRoofed(tiles_, nx, ny, z - 1)) {
                return false;
            }
            room = true;
        }
        return room;
    }

    /// The tint a wall cell's underside wears: the ceiling tint of the
    /// storey's floor beside it (so a room's ceiling is one colour whether
    /// a floor or a partition stands over each cell), else the wall's own.
    [[nodiscard]] Rgba8 undersideTint(std::int32_t x, std::int32_t y, std::int32_t z,
                                      std::uint16_t& source) const noexcept {
        for (int s = 0; s < 4; ++s) {
            const std::int32_t nx = x + kSideDx[s];
            const std::int32_t ny = y + kSideDy[s];
            if (!isFloor(tiles_, nx, ny, z)) {
                continue;
            }
            const MaterialRule* r = rule(nx, ny, z);
            if (r != nullptr && r->ceilingTintSet) {
                source = tiles_.material(nx, ny, z);
                return r->ceilingTint;
            }
        }
        const MaterialRule* own = rule(x, y, z);
        source = tiles_.material(x, y, z);
        if (own == nullptr) {
            return Rgba8{};
        }
        return own->ceilingTintSet ? own->ceilingTint : own->insideTint;
    }

    /// The quad under every floor slab with something other than solid
    /// under it -- a room's ceiling, a pier's underside -- and under every
    /// WALL cell that stands over an open cell: the partition over a
    /// taproom, the facade over a doorway. Joists cross the rooms.
    void ceilings() {
        const PieceSpec* ceiling = catalogue_.piece(PieceRole::Ceiling);
        if (ceiling == nullptr) {
            return;
        }
        const PieceSpec* joist = catalogue_.piece(PieceRole::Joist);
        resetCover();
        const std::int32_t zLo = std::max(1, catalogue_.minBand());
        const std::span<const std::string_view> ids = render::materialIds();
        for (std::int32_t z = zLo; z < tiles_.sizeZ(); ++z) {
            for (std::size_t m = 0; m < ids.size(); ++m) {
                const auto material = static_cast<std::uint16_t>(m);
                const MaterialRule* r = catalogue_.material(material);
                // Over rooms and over the open (a pier over the water)
                // separately: only a room gets joists. A floor over a WALL
                // that stands inside a room -- the slab over a taproom's
                // pillar, its bar, its hearth -- is the room's ceiling too,
                // or the ceiling would have a hole over every one of them.
                for (int room = 1; room >= 0; --room) {
                    const auto same = [&](std::int32_t x, std::int32_t y) {
                        if (tiles_.form(x, y, z) != content::TileForm::Floor ||
                            tiles_.material(x, y, z) != material) {
                            return false;
                        }
                        if (isOpenish(tiles_, x, y, z - 1)) {
                            return (isWalkableForm(tiles_, x, y, z - 1) ? 1 : 0) == room;
                        }
                        return room == 1 && overRoomWall(x, y, z);
                    };
                    // The material's own ceiling tint, or the piece's plain
                    // plaster for a material the catalogue does not dress.
                    const Rgba8 tint = (r != nullptr && r->ceilingTintSet) ? r->ceilingTint : Rgba8{};
                    mergeRectangles(z, 1, std::max(1, ceiling->maxBlock), same,
                                    [&](std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h) {
                                        // Lit by the room under it, as an underside.
                                        blockPiece(PieceRole::Ceiling, *ceiling, x, y, z, w, h,
                                                   render::bandSurface(z) - render::kFloorSlab, tint);
                                        StaticPlacement& p = out_.placements.back();
                                        p.lightZ = z - 1;
                                        p.facing = render::kUndersideLift;
                                        if (room == 1 && joist != nullptr) {
                                            joists(x, y, z, w, h);
                                        }
                                    });
                }
            }
        }
        // The undersides of walls over open cells, in the storey's ceiling
        // tint, at the slab plane the floor ceilings round them hang at --
        // not the wall's own foot, a slab higher, where the quad would sit
        // in a recess whose sides are the slabs' atlas.
        for (std::int32_t z = zLo; z < tiles_.sizeZ(); ++z) {
            for (std::size_t m = 0; m < ids.size(); ++m) {
                const auto source = static_cast<std::uint16_t>(m);
                Rgba8 tint{};
                bool any = false;
                const auto same = [&](std::int32_t x, std::int32_t y) {
                    // A dressed wall (one with a class) over an open cell;
                    // the substrate is nobody's ceiling.
                    if (!isWall(tiles_, x, y, z) || wallClassAt(x, y, z) == WallClass::None ||
                        !isOpenish(tiles_, x, y, z - 1)) {
                        return false;
                    }
                    std::uint16_t from = 0;
                    const Rgba8 t = undersideTint(x, y, z, from);
                    if (from != source) {
                        return false;
                    }
                    if (!any) {
                        tint = t;
                        any = true;
                    }
                    return true;
                };
                mergeRectangles(z, 1, std::max(1, ceiling->maxBlock), same,
                                [&](std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h) {
                                    blockPiece(PieceRole::Ceiling, *ceiling, x, y, z, w, h,
                                               render::bandSurface(z) - render::kFloorSlab, tint);
                                    StaticPlacement& p = out_.placements.back();
                                    p.lightZ = z - 1;
                                    p.facing = render::kUndersideLift;
                                });
            }
        }
    }

    /// Beams under a ceiling rectangle, east-west on the odd grid lines
    /// (so two rectangles' joists meet), spanning the rectangle.
    void joists(std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t w, std::int32_t h) {
        const float top = render::bandSurface(z) - render::kFloorSlab;
        for (std::int32_t line = y; line <= y + h; ++line) {
            if ((line & 1) == 0 || line == y || line == y + h) {
                continue;
            }
            // A run along the north face of row `line`, its tangent +X:
            // the beam from x to x + w.
            FaceRun r;
            r.z = z;
            r.side = kNorth;
            setBase(r, static_cast<float>(line));
            r.firstX = x;
            r.firstY = std::clamp(line, y, y + h - 1);
            r.lastX = x + w - 1;
            r.lastY = r.firstY;
            FaceLight light;
            light.gradient = true;
            light.firstX = light.beforeX = x;
            light.lastX = light.afterX = x + w - 1;
            light.firstY = light.beforeY = light.lastY = light.afterY = r.firstY;
            beamAlong(r, static_cast<float>(x), static_cast<float>(x + w), top - kJoistHalfHeight, 0.0F,
                      Rgba8{}, light, z - 1);
            StaticPlacement& p = out_.placements.back();
            p.facing = render::kUndersideLift;
        }
    }

    /// The topmost wet OPEN cell of a column: the harbour's surface.
    [[nodiscard]] int waterSurfaceDepth(std::int32_t x, std::int32_t y,
                                        std::int32_t z) const noexcept {
        const content::TileForm form = tiles_.form(x, y, z);
        if (form != content::TileForm::Open) {
            return 0;
        }
        const int depth = tiles_.fluidDepth(x, y, z);
        if (depth <= 0 || tiles_.fluidDepth(x, y, z + 1) != 0) {
            return 0;
        }
        return depth;
    }

    void water() {
        const PieceSpec* spec = catalogue_.piece(PieceRole::Water);
        if (spec == nullptr) {
            return;
        }
        resetCover();
        for (std::int32_t z = 0; z < tiles_.sizeZ(); ++z) {
            for (int depth = 1; depth <= kWaterMaxDepth; ++depth) {
                const auto same = [&](std::int32_t x, std::int32_t y) {
                    return waterSurfaceDepth(x, y, z) == depth;
                };
                const float surface = render::bandSurface(z) + render::waterSurface(depth);
                mergeRectangles(z, 1, std::max(1, spec->maxBlock), same,
                                [&](std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h) {
                                    blockPiece(PieceRole::Water, *spec, x, y, z, w, h, surface, Rgba8{});
                                });
            }
        }
    }

    // --- props ------------------------------------------------------------

    /// The prop a hash picks: a barrel, a crate or a sack by the catalogue's
    /// percentages, the barrel standing in for a missing one.
    [[nodiscard]] const PieceSpec* pickProp(std::uint32_t pick, PieceRole& role) const noexcept {
        const RuleKnobs& knobs = catalogue_.knobs();
        const PieceSpec* barrel = catalogue_.piece(PieceRole::PropBarrel);
        const PieceSpec* crate = catalogue_.piece(PieceRole::PropCrate);
        const PieceSpec* sack = catalogue_.piece(PieceRole::PropSack);
        role = PieceRole::PropSack;
        const PieceSpec* spec = sack;
        if (pick < static_cast<std::uint32_t>(std::max(0, knobs.propBarrelPercent))) {
            role = PieceRole::PropBarrel;
            spec = barrel;
        } else if (pick < static_cast<std::uint32_t>(
                              std::max(0, knobs.propBarrelPercent + knobs.propCratePercent))) {
            role = PieceRole::PropCrate;
            spec = crate;
        }
        if (spec == nullptr) {
            role = PieceRole::PropBarrel;
            spec = barrel;
        }
        return spec;
    }

    /// A prop stood on a cell, pushed a little toward `side` (a wall, a
    /// roof's edge), turned by hash.
    void propAt(PieceRole role, const PieceSpec& spec, std::int32_t x, std::int32_t y,
                std::int32_t z, int side, std::uint32_t h, float push) {
        const Vec3 at{static_cast<float>(x) + 0.5F + kNormalX[side] * push, render::bandSurface(z),
                      static_cast<float>(y) + 0.5F + kNormalZ[side] * push};
        const float yaw = static_cast<float>((h >> 12) & 3U) * kHalfPi +
                          (static_cast<float>((h >> 16) & 15U) - 7.5F) * 0.02F;
        pointPiece(role, spec, at, yaw, x, y, z);
    }

    void props() {
        const RuleKnobs& knobs = catalogue_.knobs();
        const std::int32_t every = knobs.propEvery;
        if (every <= 0) {
            return;
        }
        const PieceSpec* barrel = catalogue_.piece(PieceRole::PropBarrel);
        const PieceSpec* rack = catalogue_.piece(PieceRole::BarrelRack);
        if (barrel == nullptr) {
            return;
        }
        const PieceSpec* wall = catalogue_.piece(PieceRole::Wall);
        // Against the wall: the tile centre pushed toward it, but not into
        // the wall piece that stands proud of the chunk face.
        const float proud = wall != nullptr ? wall->thickness : 0.0F;
        const std::int32_t zLo = std::max(0, catalogue_.minBand());
        resetCover();
        for (std::int32_t z = zLo; z < tiles_.sizeZ(); ++z) {
            for (std::int32_t y = 1; y + 1 < tiles_.sizeY(); ++y) {
                for (std::int32_t x = 1; x + 1 < tiles_.sizeX(); ++x) {
                    if (!floorCell(x, y, z) || covered(x, y, z) || gapAt(x, y, z) != nullptr) {
                        continue;
                    }
                    int wallSide = -1;
                    int wallCount = 0;
                    for (int s = 0; s < 4; ++s) {
                        if (isWall(tiles_, x + kSideDx[s], y + kSideDy[s], z)) {
                            wallSide = s;
                            ++wallCount;
                        }
                    }
                    if (wallCount != 1) {
                        continue;
                    }
                    const std::uint32_t h = cellHash(x, y, z, kSaltProp);
                    if (h % static_cast<std::uint32_t>(every) != 0U) {
                        continue;
                    }
                    const std::uint32_t pick = (h / static_cast<std::uint32_t>(every)) % 100U;
                    PieceRole role = PieceRole::PropSack;
                    const PieceSpec* spec = pickProp(pick, role);
                    // Indoors a sack's turn becomes a barrel rack when the
                    // next cell along the wall is free against it too.
                    if (role == PieceRole::PropSack && rack != nullptr && cellRoofed(tiles_, x, y, z)) {
                        const std::int32_t tx = kSideDy[wallSide] != 0 ? 1 : 0;
                        const std::int32_t ty = kSideDx[wallSide] != 0 ? 1 : 0;
                        const std::int32_t nx = x + tx;
                        const std::int32_t ny = y + ty;
                        if (floorCell(nx, ny, z) && !covered(nx, ny, z) && gapAt(nx, ny, z) == nullptr &&
                            isWall(tiles_, nx + kSideDx[wallSide], ny + kSideDy[wallSide], z)) {
                            const float push = 0.5F - proud - rack->thickness;
                            const Vec3 at{static_cast<float>(x) + 0.5F + 0.5F * static_cast<float>(tx) +
                                              kNormalX[wallSide] * push,
                                          render::bandSurface(z),
                                          static_cast<float>(y) + 0.5F + 0.5F * static_cast<float>(ty) +
                                              kNormalZ[wallSide] * push};
                            // Its back to the wall: local -Z faces the wall.
                            pointPiece(PieceRole::BarrelRack, *rack, at, yawOf(opposite(wallSide)), x, y,
                                       z);
                            markCovered(x, y, z, 1, 1);
                            markCovered(nx, ny, z, 1, 1);
                            continue;
                        }
                    }
                    propAt(role, *spec, x, y, z, wallSide, h, 0.5F - proud - spec->thickness);
                    markCovered(x, y, z, 1, 1);
                }
            }
        }
    }

    // --- furniture --------------------------------------------------------

    /// Round every indoor lantern: tables with benches and mugs on clear
    /// floor, shelves with bottles on the indoor masonry faces, and a
    /// fireplace on any free-standing two-cell masonry block in a roofed
    /// room. All by tile hash, none in a doorway.
    void furniture() {
        const RuleKnobs& knobs = catalogue_.knobs();
        const PieceSpec* table = catalogue_.piece(PieceRole::Table);
        const PieceSpec* bench = catalogue_.piece(PieceRole::Bench);
        const PieceSpec* mug = catalogue_.piece(PieceRole::Mug);
        const PieceSpec* bottle = catalogue_.piece(PieceRole::Bottle);
        const PieceSpec* shelf = catalogue_.piece(PieceRole::Shelf);
        const PieceSpec* wall = catalogue_.piece(PieceRole::Wall);
        const float proud = wall != nullptr ? wall->thickness : 0.0F;
        // covered_ still holds the props' cells: nothing lands on a barrel.
        ensureCover();
        for (const render::Lamp& lamp : lamps_) {
            if (lamp.warmth != render::LampWarmth::Lantern || !tiles_.inBounds(lamp.x, lamp.y, lamp.z) ||
                !cellRoofed(tiles_, lamp.x, lamp.y, lamp.z)) {
                continue;
            }
            const std::int32_t z = lamp.z;
            for (std::int32_t y = lamp.y - kFurnitureReach; y <= lamp.y + kFurnitureReach; ++y) {
                for (std::int32_t x = lamp.x - kFurnitureReach; x <= lamp.x + kFurnitureReach; ++x) {
                    if (!tiles_.inBounds(x, y, z) || !floorCell(x, y, z) || !cellRoofed(tiles_, x, y, z) ||
                        covered(x, y, z) || gapAt(x, y, z) != nullptr) {
                        continue;
                    }
                    // A table wants a clear 3 x 3 of floor round it, on the
                    // lattice anchored at the lamp: every `tableEvery`th
                    // cell both ways, offset so the lamp's own cell is not
                    // one -- a spread, not a coin flip.
                    const std::int32_t pitch = knobs.tableEvery;
                    const bool onLattice =
                        pitch > 0 && (((x - lamp.x) % pitch + pitch) % pitch) == pitch / 2 &&
                        (((y - lamp.y) % pitch + pitch) % pitch) == pitch / 2;
                    bool clear = table != nullptr && onLattice;
                    for (std::int32_t oy = -1; oy <= 1 && clear; ++oy) {
                        for (std::int32_t ox = -1; ox <= 1 && clear; ++ox) {
                            clear = floorCell(x + ox, y + oy, z) && !covered(x + ox, y + oy, z) &&
                                    gapAt(x + ox, y + oy, z) == nullptr;
                        }
                    }
                    if (clear) {
                        tableAt(*table, bench, mug, x, y, z, cellHash(x, y, z, kSaltTable));
                        markCovered(x - 1, y - 1, z, 3, 3);
                        continue;
                    }
                    // A shelf on the one indoor masonry wall the cell stands
                    // against.
                    if (shelf == nullptr || knobs.shelfEvery <= 0) {
                        continue;
                    }
                    int wallSide = -1;
                    int wallCount = 0;
                    for (int s = 0; s < 4; ++s) {
                        if (isWall(tiles_, x + kSideDx[s], y + kSideDy[s], z)) {
                            wallSide = s;
                            ++wallCount;
                        }
                    }
                    // On a masonry face of a real wall, never on a free-standing
                    // block (a hearth wears its fireplace, not a shelf).
                    const std::int32_t wx = x + kSideDx[wallSide];
                    const std::int32_t wy = y + kSideDy[wallSide];
                    if (wallCount != 1 || wallClassAt(wx, wy, z) != WallClass::Masonry ||
                        wallNeighbours(wx, wy, z) < 2) {
                        continue;
                    }
                    const std::uint32_t h = cellHash(x, y, z, kSaltShelf);
                    if (h % static_cast<std::uint32_t>(knobs.shelfEvery) != 0U) {
                        continue;
                    }
                    shelfAt(*shelf, bottle, mug, x, y, z, wallSide, proud, h);
                    markCovered(x, y, z, 1, 1);
                }
            }
        }
        fireplaces();
        stools();
    }

    /// Stools round the indoor pillars: the free floor cell east and west of
    /// a lone indoor timber cell gets a stool pushed up to the pillar,
    /// turned a little by hash. The pillar is the sim's own table (a candle
    /// stands on it), so this is where the room sits.
    void stools() {
        const PieceSpec* stool = catalogue_.piece(PieceRole::Stool);
        const PieceSpec* pillar = catalogue_.piece(PieceRole::Pillar);
        if (stool == nullptr || pillar == nullptr) {
            return;
        }
        const std::int32_t zLo = std::max(0, catalogue_.minBand());
        for (std::int32_t z = zLo; z < tiles_.sizeZ(); ++z) {
            for (std::int32_t y = 1; y + 1 < tiles_.sizeY(); ++y) {
                for (std::int32_t x = 1; x + 1 < tiles_.sizeX(); ++x) {
                    if (!isPost(x, y, z) || !cellRoofed(tiles_, x, y, z)) {
                        continue;
                    }
                    for (int s = kEast; s <= kWest; s += 2) {
                        const std::int32_t nx = x + kSideDx[s];
                        const std::int32_t ny = y + kSideDy[s];
                        if (!floorCell(nx, ny, z) || covered(nx, ny, z) || gapAt(nx, ny, z) != nullptr) {
                            continue;
                        }
                        const std::uint32_t h = cellHash(nx, ny, z, kSaltStool);
                        // Against the pillar's face (the cell's edge plus the
                        // pillar's own bulge), the stool's own radius back.
                        const float push = 0.5F - pillar->thickness - stool->thickness;
                        propAt(PieceRole::Stool, *stool, nx, ny, z, opposite(s), h, push);
                        markCovered(nx, ny, z, 1, 1);
                    }
                }
            }
        }
    }

    /// A table on a cell, turned by hash, a bench each side, a mug or three.
    void tableAt(const PieceSpec& table, const PieceSpec* bench, const PieceSpec* mug, std::int32_t x,
                 std::int32_t y, std::int32_t z, std::uint32_t h) {
        const int q = static_cast<int>((h >> 8) & 1U);
        const float yaw = static_cast<float>(q) * kHalfPi;
        const Vec3 centre{static_cast<float>(x) + 0.5F, render::bandSurface(z), static_cast<float>(y) + 0.5F};
        pointPiece(PieceRole::Table, table, centre, yaw, x, y, z);
        if (bench != nullptr) {
            for (int side = -1; side <= 1; side += 2) {
                const Vec2 off = turn(Vec2{0.0F, kBenchOffset * static_cast<float>(side)}, q);
                pointPiece(PieceRole::Bench, *bench, Vec3{centre.x + off.x, centre.y, centre.z + off.z}, yaw,
                           x, y, z);
            }
        }
        if (mug != nullptr) {
            const float top = table.height * table.scale;
            const int mugs = 1 + static_cast<int>((h >> 10) & 1U) + static_cast<int>((h >> 11) & 1U);
            for (int i = 0; i < mugs; ++i) {
                const float along = (static_cast<float>(i) - 0.5F * static_cast<float>(mugs - 1)) * 0.55F;
                const float across = (static_cast<float>((h >> (13 + i)) & 1U) - 0.5F) * 0.3F;
                const Vec2 off = turn(Vec2{along, across}, q);
                pointPiece(PieceRole::Mug, *mug, Vec3{centre.x + off.x, centre.y + top, centre.z + off.z},
                           yaw + static_cast<float>((h >> (20 + i)) & 3U) * kHalfPi, x, y, z);
            }
        }
    }

    /// A shelf hung on the wall face beside a cell, a bottle or two and a
    /// mug on it.
    void shelfAt(const PieceSpec& shelf, const PieceSpec* bottle, const PieceSpec* mug, std::int32_t x,
                 std::int32_t y, std::int32_t z, int wallSide, float proud, std::uint32_t h) {
        // The wall's face looks back at the cell: the shelf's local +Z (its
        // depth) points away from the wall.
        const int face = opposite(wallSide);
        const FaceRun r = cellFace(x + kSideDx[wallSide], y + kSideDy[wallSide], z, face);
        const float mid = 0.5F * (r.a0 + r.a1);
        const float standoff = proud + 0.01F;
        const Vec3 at{r.baseX + kTangentX[face] * mid + kNormalX[face] * standoff,
                      render::bandSurface(z) + kShelfLift,
                      r.baseZ + kTangentZ[face] * mid + kNormalZ[face] * standoff};
        // Its +Z is its depth: turned so +Z points off the wall.
        pointPiece(PieceRole::Shelf, shelf, at, yawOf(face) + kPi, x, y, z);
        const float depth = 0.5F * std::max(0.1F, shelf.maxZ - shelf.minZ) * shelf.scale;
        int slot = 0;
        const auto onShelf = [&](PieceRole role, const PieceSpec& spec, float along) {
            const Vec3 p{at.x + kTangentX[face] * along + kNormalX[face] * depth, at.y,
                         at.z + kTangentZ[face] * along + kNormalZ[face] * depth};
            pointPiece(role, spec, p, yawOf(face) + static_cast<float>((h >> (16 + slot)) & 3U) * kHalfPi, x,
                       y, z);
            ++slot;
        };
        if (bottle != nullptr) {
            onShelf(PieceRole::Bottle, *bottle, -0.28F);
            if (((h >> 9) & 1U) != 0U) {
                onShelf(PieceRole::Bottle, *bottle, 0.02F);
            }
        }
        if (mug != nullptr) {
            onShelf(PieceRole::Mug, *mug, 0.27F);
        }
    }

    /// A free-standing two-cell masonry block in a roofed room is a hearth:
    /// the fireplace stands against the side with the most room before it.
    void fireplaces() {
        const PieceSpec* fireplace = catalogue_.piece(PieceRole::Fireplace);
        if (fireplace == nullptr) {
            return;
        }
        const std::int32_t zLo = std::max(0, catalogue_.minBand());
        for (std::int32_t z = zLo; z < tiles_.sizeZ(); ++z) {
            for (std::int32_t y = 1; y + 1 < tiles_.sizeY(); ++y) {
                for (std::int32_t x = 1; x + 1 < tiles_.sizeX(); ++x) {
                    // The pair: this cell and the one east or south of it,
                    // both masonry, roofed, each other's only wall
                    // neighbour.
                    for (int axis = 0; axis < 2; ++axis) {
                        const std::int32_t px = x + (axis == 0 ? 1 : 0);
                        const std::int32_t py = y + (axis == 0 ? 0 : 1);
                        if (!isWall(tiles_, x, y, z) || !isWall(tiles_, px, py, z) ||
                            wallClassAt(x, y, z) != WallClass::Masonry ||
                            wallClassAt(px, py, z) != WallClass::Masonry || !cellRoofed(tiles_, x, y, z) ||
                            !cellRoofed(tiles_, px, py, z) || wallNeighbours(x, y, z) != 1 ||
                            wallNeighbours(px, py, z) != 1) {
                            continue;
                        }
                        // The two sides across the pair's axis; the one with
                        // more floor straight out wins.
                        const int sideA = axis == 0 ? kNorth : kWest;
                        const int sideB = opposite(sideA);
                        const auto room = [&](int side) {
                            int n = 0;
                            for (int d = 1; d <= 5; ++d) {
                                if (!floorCell(x + kSideDx[side] * d, y + kSideDy[side] * d, z)) {
                                    break;
                                }
                                ++n;
                            }
                            return n;
                        };
                        const int side = room(sideA) >= room(sideB) ? sideA : sideB;
                        if (room(side) < 2) {
                            continue;
                        }
                        // Centred on the pair, against the chosen face, its
                        // opening to the room: the piece's own footprint
                        // centre (its extent) is put on the point, turned
                        // with the piece.
                        // Its depth is halved: the hood stands as a relief
                        // against the block, not a metre into the room where
                        // the bodies warm themselves.
                        const float cx = 0.5F * static_cast<float>(x + px) + 0.5F;
                        const float cz = 0.5F * static_cast<float>(y + py) + 0.5F;
                        const float depthScale = fireplace->scale * kFireplaceDepth;
                        const float half = 0.5F * (fireplace->maxZ - fireplace->minZ) * depthScale;
                        const float push = 0.5F + half + 0.01F;
                        const int turns = (side + static_cast<int>(std::lround(fireplace->yawOffset / kHalfPi))) & 3;
                        const Vec2 off = turn(Vec2{0.5F * (fireplace->minX + fireplace->maxX) * fireplace->scale,
                                                   0.5F * (fireplace->minZ + fireplace->maxZ) * depthScale},
                                              turns);
                        const Vec3 at{cx + kNormalX[side] * push - off.x, render::bandSurface(z),
                                      cz + kNormalZ[side] * push - off.z};
                        pointPiece(PieceRole::Fireplace, *fireplace, at, yawOf(side), x + kSideDx[side],
                                   y + kSideDy[side], z, Rgba8{}, Vec3{1.0F, 1.0F, kFireplaceDepth}, false, 3.0F);
                        flameAt(Vec3{cx + kNormalX[side] * (0.5F + half * 0.6F), at.y + 0.35F,
                                     cz + kNormalZ[side] * (0.5F + half * 0.6F)},
                                kFireFlameWidth, kFireFlameHeight, catalogue_.knobs().fireFlame,
                                x + kSideDx[side], y + kSideDy[side], z);
                    }
                }
            }
        }
    }

    // --- roofs ------------------------------------------------------------

    /// A building top with the sky over it: a floor of a roofing material,
    /// or any floor with a room (a walkable cell) under it, or one over a
    /// dressed wall with such a floor beside or cornerwise to it (the ring
    /// over a building's walls, its corners too) -- whatever the tile's
    /// material. Never a street over the ground, never a deck over water.
    [[nodiscard]] bool roofCell(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept {
        if (!floorCell(x, y, z) || tiles_.form(x, y, z + 1) != content::TileForm::Open) {
            return false;
        }
        const MaterialRule* r = rule(x, y, z);
        if (r != nullptr && r->roof) {
            return true;
        }
        if (isWalkableForm(tiles_, x, y, z - 1)) {
            return true;
        }
        if (!isWall(tiles_, x, y, z - 1) || wallClassAt(x, y, z - 1) == WallClass::None) {
            return false;
        }
        for (std::int32_t dy = -1; dy <= 1; ++dy) {
            for (std::int32_t dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) {
                    continue;
                }
                const std::int32_t nx = x + dx;
                const std::int32_t ny = y + dy;
                if (floorCell(nx, ny, z) && tiles_.form(nx, ny, z + 1) == content::TileForm::Open &&
                    isWalkableForm(tiles_, nx, ny, z - 1)) {
                    return true;
                }
            }
        }
        return false;
    }

    /// A hatch: a stair or a ramp that arrives on a roof plane -- the cell
    /// under a roof cell, or one beside it on its own level. A roof within
    /// two cells of one is a DECK somebody climbs to, and keeps its flags;
    /// every other top is lead.
    [[nodiscard]] bool hatchAt(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept {
        const auto climb = [this](std::int32_t cx, std::int32_t cy, std::int32_t cz) {
            const content::TileForm form = tiles_.form(cx, cy, cz);
            return form == content::TileForm::Stair || form == content::TileForm::Ramp;
        };
        if (climb(x, y, z - 1)) {
            return true;
        }
        for (int s = 0; s < 4; ++s) {
            if (climb(x + kSideDx[s], y + kSideDy[s], z)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool deckCell(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept {
        if (!roofCell(x, y, z)) {
            return false;
        }
        for (std::int32_t dy = -kDeckReach; dy <= kDeckReach; ++dy) {
            for (std::int32_t dx = -kDeckReach; dx <= kDeckReach; ++dx) {
                if (hatchAt(x + dx, y + dy, z)) {
                    return true;
                }
            }
        }
        return false;
    }

    /// The batten seams of a lead roof: along every north-south module
    /// line with a roof cell on both sides of it, the kit beam laid thin
    /// from the line's first cell to its last, in the roof's own dark. The
    /// upstand takes the edges. A deck keeps its flags and gets none.
    void roofBattens() {
        const PieceSpec* batten = catalogue_.piece(PieceRole::RoofBatten);
        if (batten == nullptr) {
            return;
        }
        const RuleKnobs& knobs = catalogue_.knobs();
        const std::int32_t zLo = std::max(1, catalogue_.minBand());
        const float lift = 0.5F * std::max(0.01F, batten->maxX - batten->minX) * batten->scale;
        for (std::int32_t z = zLo; z < tiles_.sizeZ(); ++z) {
            for (std::int32_t x = 1; x < tiles_.sizeX(); ++x) {
                std::int32_t y = 0;
                while (y < tiles_.sizeY()) {
                    const auto seam = [&](std::int32_t yy) {
                        return roofCell(x - 1, yy, z) && roofCell(x, yy, z) && !deckCell(x - 1, yy, z) &&
                               !deckCell(x, yy, z);
                    };
                    if (!seam(y)) {
                        ++y;
                        continue;
                    }
                    std::int32_t y1 = y;
                    while (y1 + 1 < tiles_.sizeY() && seam(y1 + 1)) {
                        ++y1;
                    }
                    // The line x, read south along the east face's tangent
                    // (a-order is +y there), lit by the cells east of it.
                    FaceRun r;
                    r.z = z;
                    r.side = kEast;
                    setBase(r, static_cast<float>(x));
                    const std::int32_t cells = y1 - y + 1;
                    beamAlong(r, static_cast<float>(y), static_cast<float>(y1 + 1),
                              render::bandSurface(z) + lift, 0.0F, knobs.roofTint,
                              runLight(x, y, 0, 1, cells, 0.0F, static_cast<float>(cells)), z,
                              PieceRole::RoofBatten);
                    y = y1 + 1;
                }
            }
        }
    }

    /// Whether a roof edge cell's upstand is brick: the wall under it is
    /// masonry, or -- over a doorway, where a floor is under it -- the wall
    /// under either neighbour along the edge is.
    [[nodiscard]] bool upstandMasonry(std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t dx,
                                      std::int32_t dy) const noexcept {
        if (isWall(tiles_, x, y, z - 1)) {
            return wallClassAt(x, y, z - 1) == WallClass::Masonry;
        }
        for (int dir = -1; dir <= 1; dir += 2) {
            const std::int32_t nx = x + dx * dir;
            const std::int32_t ny = y + dy * dir;
            if (isWall(tiles_, nx, ny, z - 1) && wallClassAt(nx, ny, z - 1) == WallClass::Masonry) {
                return true;
            }
        }
        return false;
    }

    /// The roof rules: loose tiles by hash, an upstand along every edge,
    /// chimneys over masonry, the odd crate at the edge. The planes stay
    /// FLAT: every one of them is standable and the Skyrunners run them.
    void roofs() {
        const RuleKnobs& knobs = catalogue_.knobs();
        const PieceSpec* tile = catalogue_.piece(PieceRole::RoofTile);
        const PieceSpec* parapet = catalogue_.piece(PieceRole::Parapet);
        const PieceSpec* timber = catalogue_.piece(PieceRole::WallTimber);
        const PieceSpec* chimney = catalogue_.piece(PieceRole::Chimney);
        const std::uint8_t tileVariants = catalogue_.variantCount(PieceRole::RoofTile);
        const std::uint8_t chimneyVariants = catalogue_.variantCount(PieceRole::Chimney);
        const std::int32_t zLo = std::max(1, catalogue_.minBand());
        for (std::int32_t z = zLo; z < tiles_.sizeZ(); ++z) {
            // The upstands, merged into runs per side.
            for (int side = 0; side < 4; ++side) {
                const bool rows = side == kNorth || side == kSouth;
                const std::int32_t lines = rows ? tiles_.sizeY() : tiles_.sizeX();
                const std::int32_t along = rows ? tiles_.sizeX() : tiles_.sizeY();
                std::int32_t dx = 0, dy = 0;
                runStep(side, dx, dy);
                for (std::int32_t line = 0; line < lines; ++line) {
                    bool open = false;
                    bool masonryRun = false;
                    FaceRun current;
                    const auto flush = [&]() {
                        if (!open) {
                            return;
                        }
                        open = false;
                        const PieceSpec* spec = masonryRun ? parapet : timber;
                        if (spec == nullptr) {
                            return;
                        }
                        const std::int32_t cells =
                            std::max(1, static_cast<std::int32_t>(std::lround(current.a1 - current.a0)));
                        // The wall below's own colours.
                        const MaterialRule* below = rule(current.firstX, current.firstY, z - 1);
                        FaceOpts o;
                        o.frontOut = true;
                        // Inside the cell, its outer face on the edge.
                        o.standoff = -(spec->standoffSet ? spec->standoff : spec->thickness * 0.5F);
                        o.height = kParapetHeight;
                        o.yBase = render::bandSurface(z);
                        o.tint = below != nullptr ? below->tint : Rgba8{};
                        o.light = runLight(current.firstX, current.firstY, dx, dy, cells, 0.0F,
                                           static_cast<float>(cells));
                        facePiece(current, PieceRole::Parapet, *spec, current.a0, current.a1, o);
                    };
                    for (std::int32_t i = 0; i < along; ++i) {
                        const std::int32_t k = (dx + dy) > 0 ? i : along - 1 - i;
                        const std::int32_t x = rows ? k : line;
                        const std::int32_t y = rows ? line : k;
                        const bool edge = roofCell(x, y, z) &&
                                          isAir(tiles_, x + kSideDx[side], y + kSideDy[side], z);
                        const bool masonry = edge && upstandMasonry(x, y, z, dx, dy);
                        if (!edge || (open && masonry != masonryRun)) {
                            flush();
                        }
                        if (!edge) {
                            continue;
                        }
                        float a0 = 0.0F, a1 = 0.0F;
                        cellInterval(side, x, y, a0, a1);
                        if (!open) {
                            current = cellFace(x, y, z, side);
                            masonryRun = masonry;
                            open = true;
                        }
                        current.a1 = a1;
                        current.lastX = x;
                        current.lastY = y;
                    }
                    flush();
                }
            }
            // Per cell: chimneys (any top with sky over it and a masonry
            // wall under it, at its edge -- a roof, a brick terrace), then
            // on the roofing planes tiles and clutter.
            for (std::int32_t y = 1; y + 1 < tiles_.sizeY(); ++y) {
                for (std::int32_t x = 1; x + 1 < tiles_.sizeX(); ++x) {
                    const bool top = dressedFloor(x, y, z) && tiles_.form(x, y, z + 1) == content::TileForm::Open;
                    if (!top) {
                        continue;
                    }
                    const bool roof = roofCell(x, y, z);
                    int edgeSide = -1;
                    for (int s = 0; s < 4; ++s) {
                        if (isAir(tiles_, x + kSideDx[s], y + kSideDy[s], z)) {
                            edgeSide = s;
                        }
                    }
                    bool placed = false;
                    if (edgeSide >= 0 && chimney != nullptr && knobs.chimneyEvery > 0 &&
                        isWall(tiles_, x, y, z - 1) && wallClassAt(x, y, z - 1) == WallClass::Masonry) {
                        const std::uint32_t h = cellHash(x, y, z, kSaltChimney);
                        if (h % static_cast<std::uint32_t>(knobs.chimneyEvery) == 0U) {
                            // Along the wall under it: east-west when the
                            // wall runs that way, else north-south.
                            const bool eastWest =
                                isWall(tiles_, x - 1, y, z - 1) || isWall(tiles_, x + 1, y, z - 1);
                            const PieceSpec* pick =
                                chimneyVariants > 1
                                    ? catalogue_.piece(PieceRole::Chimney,
                                                       static_cast<std::uint8_t>((h >> 8) % chimneyVariants))
                                    : chimney;
                            pointPiece(PieceRole::Chimney, *pick,
                                       Vec3{static_cast<float>(x) + 0.5F, render::bandSurface(z),
                                            static_cast<float>(y) + 0.5F},
                                       eastWest ? 0.0F : kHalfPi, x, y, z, Rgba8{}, Vec3{1.0F, 1.0F, 1.0F},
                                       false, 3.0F);
                            placed = true;
                        }
                    }
                    if (!roof) {
                        continue;
                    }
                    if (!placed && edgeSide >= 0 && knobs.roofPropEvery > 0) {
                        const std::uint32_t h = cellHash(x, y, z, kSaltRoofProp);
                        if (h % static_cast<std::uint32_t>(knobs.roofPropEvery) == 0U) {
                            PieceRole role = PieceRole::PropSack;
                            const PieceSpec* spec = pickProp((h >> 4) % 100U, role);
                            if (spec != nullptr) {
                                propAt(role, *spec, x, y, z, edgeSide, h, 0.5F - spec->thickness - 0.1F);
                                placed = true;
                            }
                        }
                    }
                    if (!placed && tile != nullptr && knobs.roofTileEvery > 0) {
                        const std::uint32_t h = cellHash(x, y, z, kSaltRoofTile);
                        if (h % static_cast<std::uint32_t>(knobs.roofTileEvery) == 0U) {
                            const PieceSpec* pick =
                                tileVariants > 1
                                    ? catalogue_.piece(PieceRole::RoofTile,
                                                       static_cast<std::uint8_t>((h >> 8) % tileVariants))
                                    : tile;
                            pointPiece(PieceRole::RoofTile, *pick,
                                       Vec3{static_cast<float>(x) + 0.5F, render::bandSurface(z),
                                            static_cast<float>(y) + 0.5F},
                                       static_cast<float>((h >> 12) & 3U) * kHalfPi +
                                           (static_cast<float>((h >> 16) & 15U) - 7.5F) * 0.05F,
                                       x, y, z);
                        }
                    }
                }
            }
        }
    }

    // --- the harbour ------------------------------------------------------

    /// Rowboats moored along the quay edges, a crane at a pier head. The
    /// water they float on is one band under the deck, never below the
    /// catalogue's floor band.
    void harbour() {
        const RuleKnobs& knobs = catalogue_.knobs();
        const PieceSpec* boat = catalogue_.piece(PieceRole::Rowboat);
        const PieceSpec* crane = catalogue_.piece(PieceRole::Crane);
        const std::int32_t zLo = std::max(1, catalogue_.minBand() + 1);
        resetCover();
        for (std::int32_t z = zLo; z < tiles_.sizeZ(); ++z) {
            for (std::int32_t y = 1; y + 1 < tiles_.sizeY(); ++y) {
                for (std::int32_t x = 1; x + 1 < tiles_.sizeX(); ++x) {
                    if (!dressedFloor(x, y, z)) {
                        continue;
                    }
                    for (int side = 0; side < 4; ++side) {
                        const std::int32_t dx = kSideDx[side];
                        const std::int32_t dy = kSideDy[side];
                        // The water's edge: the first column out from the
                        // cell, across at most one quay wall, that is open
                        // above and water below.
                        std::int32_t edge = 0;
                        for (std::int32_t d = 1; d <= 2 && edge == 0; ++d) {
                            const std::int32_t nx = x + dx * d;
                            const std::int32_t ny = y + dy * d;
                            if (!isAir(tiles_, nx, ny, z)) {
                                break;
                            }
                            if (isWater(tiles_, nx, ny, z - 1)) {
                                edge = d;
                            } else if (!isWall(tiles_, nx, ny, z - 1)) {
                                break;
                            }
                        }
                        if (edge == 0) {
                            continue;
                        }
                        if (boat != nullptr && knobs.boatEvery > 0) {
                            const std::uint32_t h = cellHash(x, y, z, kSaltBoat + static_cast<std::uint32_t>(side));
                            if (h % static_cast<std::uint32_t>(knobs.boatEvery) == 0U) {
                                boatAt(*boat, x, y, z, side, edge, h);
                            }
                        }
                        if (crane != nullptr && pierHead(x, y, z, side) && edge == 1) {
                            const std::uint32_t h = cellHash(x, y, z, kSaltCrane);
                            if ((h & 1U) == 0U) {
                                // One cell back from the head, so its
                                // wheels stand on the deck and its jib
                                // reaches out over the water.
                                pointPiece(PieceRole::Crane, *crane,
                                           Vec3{static_cast<float>(x) + 0.5F - kNormalX[side],
                                                render::bandSurface(z),
                                                static_cast<float>(y) + 0.5F - kNormalZ[side]},
                                           yawOf(side), x, y, z, Rgba8{}, Vec3{1.0F, 1.0F, 1.0F}, false,
                                           8.0F);
                            }
                        }
                    }
                }
            }
        }
    }

    /// A pier head: a timber deck cell over water at the END of a strip --
    /// open water three deep off `side` (a hole in a deck is one), deck two
    /// deep behind it, and the edge it sits on short (the strip's width,
    /// two to kPierHeadWidth cells, never its length) with this cell in the
    /// middle of it.
    static constexpr std::int32_t kPierHeadWidth = 4;

    [[nodiscard]] bool pierHead(std::int32_t x, std::int32_t y, std::int32_t z, int side) const noexcept {
        const MaterialRule* r = rule(x, y, z);
        if (r == nullptr || r->lipRole != PieceRole::LipPlank || !isWater(tiles_, x, y, z - 1)) {
            return false;
        }
        const std::int32_t dx = kSideDx[side];
        const std::int32_t dy = kSideDy[side];
        if (!floorCell(x - dx, y - dy, z) || !floorCell(x - 2 * dx, y - 2 * dy, z)) {
            return false;
        }
        for (std::int32_t d = 1; d <= 3; ++d) {
            if (!isAir(tiles_, x + dx * d, y + dy * d, z) || !isWater(tiles_, x + dx * d, y + dy * d, z - 1)) {
                return false;
            }
        }
        // The edge run through this cell, across the strip: deck cells
        // with open water ahead of them.
        const std::int32_t tx = dy != 0 ? 1 : 0;
        const std::int32_t ty = dx != 0 ? 1 : 0;
        const auto headCell = [&](std::int32_t cx, std::int32_t cy) {
            return floorCell(cx, cy, z) && isAir(tiles_, cx + dx, cy + dy, z) &&
                   isWater(tiles_, cx + dx, cy + dy, z - 1);
        };
        std::int32_t before = 0;
        while (before <= kPierHeadWidth && headCell(x - tx * (before + 1), y - ty * (before + 1))) {
            ++before;
        }
        std::int32_t after = 0;
        while (after <= kPierHeadWidth && headCell(x + tx * (after + 1), y + ty * (after + 1))) {
            ++after;
        }
        const std::int32_t width = before + after + 1;
        return width >= 2 && width <= kPierHeadWidth && before == width / 2;
    }

    /// A boat moored off a cell's side: its centre kBoatOffshore beyond the
    /// water's edge, its length along the quay, floating on the surface,
    /// over clear water only (kBoatAcross x kBoatAlong cells, none taken).
    void boatAt(const PieceSpec& boat, std::int32_t x, std::int32_t y, std::int32_t z, int side,
                std::int32_t edge, std::uint32_t h) {
        const std::int32_t dx = kSideDx[side];
        const std::int32_t dy = kSideDy[side];
        const std::int32_t tx = dy != 0 ? 1 : 0;
        const std::int32_t ty = dx != 0 ? 1 : 0;
        int depth = 0;
        for (std::int32_t a = -1; a < kBoatAlong - 1; ++a) {
            for (std::int32_t d = edge; d < edge + kBoatAcross; ++d) {
                const std::int32_t wx = x + dx * d + tx * a;
                const std::int32_t wy = y + dy * d + ty * a;
                if (!isAir(tiles_, wx, wy, z) || !isWater(tiles_, wx, wy, z - 1) || covered(wx, wy, z - 1)) {
                    return;
                }
                depth = std::max(depth, tiles_.fluidDepth(wx, wy, z - 1));
            }
        }
        for (std::int32_t a = -1; a < kBoatAlong - 1; ++a) {
            for (std::int32_t d = edge; d < edge + kBoatAcross; ++d) {
                markCovered(x + dx * d + tx * a, y + dy * d + ty * a, z - 1, 1, 1);
            }
        }
        const float out = 0.5F + static_cast<float>(edge - 1) + kBoatOffshore;
        const float along = 0.5F + (static_cast<float>((h >> 8) & 15U) - 7.5F) * 0.03F;
        const float surface = render::bandSurface(z - 1) + render::waterSurface(depth);
        const Vec3 at{static_cast<float>(x) + 0.5F + kNormalX[side] * out + static_cast<float>(tx) * along,
                      surface,
                      static_cast<float>(y) + 0.5F + kNormalZ[side] * out + static_cast<float>(ty) * along};
        // The boat's length is its local Z: three quarter turns from the
        // side's yaw lay it along the quay, a hash's breath off true.
        const float yaw = yawOf(side) + 3.0F * kHalfPi + (static_cast<float>((h >> 12) & 15U) - 7.5F) * 0.012F;
        pointPiece(PieceRole::Rowboat, boat, at, yaw, x + dx * edge, y + dy * edge, z - 1, Rgba8{},
                   Vec3{1.0F, 1.0F, 1.0F}, false, 4.0F);
    }

    // --- lamps ------------------------------------------------------------

    void lampPieces() {
        const RuleKnobs& knobs = catalogue_.knobs();
        const PieceSpec* brazier = catalogue_.piece(PieceRole::Brazier);
        const PieceSpec* ember = catalogue_.piece(PieceRole::Ember);
        const PieceSpec* lantern = catalogue_.piece(PieceRole::LampWall);
        const PieceSpec* bracket = catalogue_.piece(PieceRole::LampBracket);
        const PieceSpec* chain = catalogue_.piece(PieceRole::LampChain);
        const PieceSpec* post = catalogue_.piece(PieceRole::LampPost);
        for (const render::Lamp& lamp : lamps_) {
            if (!tiles_.inBounds(lamp.x, lamp.y, lamp.z)) {
                continue;
            }
            const Vec3 centre{static_cast<float>(lamp.x) + 0.5F, render::bandSurface(lamp.z),
                              static_cast<float>(lamp.y) + 0.5F};
            if (lamp.warmth == render::LampWarmth::Fire) {
                // A brazier: the stand, the ember tray in its cage, the
                // flame over it -- the tray and the flame at the stand's
                // own scale, so a small brazier keeps its fire in its cage.
                const float bs = brazier != nullptr ? brazier->scale : 1.0F;
                if (brazier != nullptr) {
                    pointPiece(PieceRole::Brazier, *brazier, centre, 0.0F, lamp.x, lamp.y, lamp.z);
                }
                if (ember != nullptr) {
                    // The tray fitted to the cage, whatever its own shape.
                    const Vec3 fit{kEmberSize * bs / std::max(0.05F, ember->maxX - ember->minX), bs,
                                   kEmberSize * bs / std::max(0.05F, ember->maxZ - ember->minZ)};
                    pointPiece(PieceRole::Ember, *ember, Vec3{centre.x, centre.y + kEmberLift * bs, centre.z},
                               static_cast<float>((lamp.x + lamp.y) & 1) * kHalfPi, lamp.x, lamp.y, lamp.z,
                               Rgba8{}, fit, true);
                }
                flameAt(Vec3{centre.x, centre.y + kFireFlameLift * bs, centre.z}, kFireFlameWidth * bs,
                        kFireFlameHeight * bs, knobs.fireFlame, lamp.x, lamp.y, lamp.z);
                continue;
            }
            // The wall it hangs on: the wall beside the lamp's tile, or --
            // a door lamp, the tile in front of a door -- the jamb beside
            // the gap, with the lamp hung toward the doorway.
            int wallSide = -1;
            float shift = 0.0F;
            std::int32_t jambX = lamp.x;
            std::int32_t jambY = lamp.y;
            for (int s = 0; s < 4 && wallSide < 0; ++s) {
                if (isWall(tiles_, lamp.x + kSideDx[s], lamp.y + kSideDy[s], lamp.z)) {
                    wallSide = s;
                }
            }
            for (int s = 0; s < 4 && wallSide < 0; ++s) {
                const std::int32_t ax = lamp.x + kSideDx[s];
                const std::int32_t ay = lamp.y + kSideDy[s];
                if (!isWalkableForm(tiles_, ax, ay, lamp.z)) {
                    continue;
                }
                // Across the gap cell, along the wall line either way.
                const std::int32_t tx = kSideDy[s] != 0 ? 1 : 0;
                const std::int32_t ty = kSideDx[s] != 0 ? 1 : 0;
                for (int dir = -1; dir <= 1 && wallSide < 0; dir += 2) {
                    if (isWall(tiles_, ax + tx * dir, ay + ty * dir, lamp.z)) {
                        wallSide = s;
                        jambX = lamp.x + tx * dir;
                        jambY = lamp.y + ty * dir;
                        // Toward the gap: a fifth of a tile in from the
                        // jamb's edge nearest the door.
                        shift = -0.2F * static_cast<float>(dir);
                    }
                }
            }
            if (wallSide >= 0 && lantern != nullptr) {
                // Hung on that wall's face, which looks back at the lamp's
                // own row or column, from a bracket arm.
                const int face = opposite(wallSide);
                FaceRun r;
                r.z = lamp.z;
                r.side = face;
                const bool rows = face == kNorth || face == kSouth;
                const float lineCoord = rows ? static_cast<float>(face == kNorth ? lamp.y + 1 : lamp.y)
                                             : static_cast<float>(face == kWest ? lamp.x + 1 : lamp.x);
                setBase(r, lineCoord);
                float a0 = 0.0F, a1 = 0.0F;
                cellInterval(face, jambX, jambY, a0, a1);
                // In tangent terms the shift is along +x or +y of the world:
                // north and east faces read with the axis, south and west
                // against it.
                const float along = (a0 + a1) * 0.5F +
                                    ((face == kNorth || face == kEast) ? shift : -shift);
                const float standoff = lantern->standoffSet ? lantern->standoff : 0.0F;
                if (bracket != nullptr) {
                    // The arm from the wall face, its origin on the face,
                    // its length (local +Z) pointing out.
                    const float armStandoff = bracket->standoffSet ? bracket->standoff : 0.0F;
                    const Vec3 at{r.baseX + kTangentX[face] * along + kNormalX[face] * armStandoff,
                                  render::bandSurface(lamp.z), r.baseZ + kTangentZ[face] * along +
                                                                  kNormalZ[face] * armStandoff};
                    // Its length (local Z) is the standoff exactly, whatever
                    // its own scale.
                    const float armLength = std::max(0.05F, bracket->maxZ - bracket->minZ) *
                                            std::max(0.01F, bracket->scale);
                    pointPiece(PieceRole::LampBracket, *bracket, at, yawOf(face) + kPi, lamp.x, lamp.y, lamp.z,
                               Rgba8{}, Vec3{1.0F, 1.0F, standoff / armLength}, false, 1.5F);
                }
                const Vec3 hang{r.baseX + kTangentX[face] * along + kNormalX[face] * standoff,
                                render::bandSurface(lamp.z),
                                r.baseZ + kTangentZ[face] * along + kNormalZ[face] * standoff};
                pointPiece(PieceRole::LampWall, *lantern, hang, yawOf(face), lamp.x, lamp.y, lamp.z, Rgba8{},
                           Vec3{1.0F, 1.0F, 1.0F}, true);
                flameAt(Vec3{hang.x, hang.y + lantern->lift - kLanternFlameDrop * lantern->scale, hang.z},
                        kLanternFlameWidth, kLanternFlameHeight, knobs.lanternFlame, lamp.x, lamp.y, lamp.z);
            } else if (lantern != nullptr && (cellRoofed(tiles_, lamp.x, lamp.y, lamp.z) ||
                                              overhangSide(lamp.x, lamp.y, lamp.z) >= 0)) {
                // Under a roof, or beside one (a gate's lintel): a lantern
                // on a chain from the ceiling -- or from the overhang's
                // edge, a hair inside it -- its top ring where the chain
                // ends.
                const float ceiling = render::bandSurface(lamp.z) + render::kBandHeight - kChainDrop;
                Vec3 from = centre;
                if (!cellRoofed(tiles_, lamp.x, lamp.y, lamp.z)) {
                    const int over = overhangSide(lamp.x, lamp.y, lamp.z);
                    from = Vec3{centre.x + kNormalX[over] * (0.5F + kOverhangIn), centre.y,
                                centre.z + kNormalZ[over] * (0.5F + kOverhangIn)};
                }
                float ring = ceiling - 0.3F;
                if (chain != nullptr) {
                    const float drop = std::max(0.1F, chain->height) * chain->scale;
                    ring = ceiling - drop;
                    pointPiece(PieceRole::LampChain, *chain, Vec3{from.x, ceiling, from.z}, 0.0F, lamp.x,
                               lamp.y, lamp.z, Rgba8{}, Vec3{1.0F, 1.0F, 1.0F}, false, 1.5F);
                }
                const Vec3 hang{from.x, ring - lantern->lift, from.z};
                pointPiece(PieceRole::LampWall, *lantern, hang, static_cast<float>((lamp.x + lamp.y) & 3) * kHalfPi,
                           lamp.x, lamp.y, lamp.z, Rgba8{}, Vec3{1.0F, 1.0F, 1.0F}, true);
                flameAt(Vec3{hang.x, ring - kLanternFlameDrop * lantern->scale, hang.z}, kLanternFlameWidth,
                        kLanternFlameHeight, knobs.lanternFlame, lamp.x, lamp.y, lamp.z);
            } else if (post != nullptr) {
                pointPiece(PieceRole::LampPost, *post, centre, static_cast<float>((lamp.x + lamp.y) & 3) * kHalfPi,
                           lamp.x, lamp.y, lamp.z, Rgba8{}, Vec3{1.0F, 1.0F, 1.0F}, true, 4.5F);
            }
        }
    }

    const sim::TileQuery& tiles_;
    const StaticCatalogue& catalogue_;
    const std::vector<render::Lamp>& lamps_;
    std::vector<std::uint8_t> covered_;
    std::vector<DoorGap> doors_;
    std::map<std::uint64_t, std::size_t> gapOf_;
    StaticPlacements out_;
};

}  // namespace

// ---------------------------------------------------------------------------
// roles
// ---------------------------------------------------------------------------

std::string_view pieceRoleName(PieceRole role) noexcept {
    const auto index = static_cast<std::size_t>(role);
    return index < kPieceRoleCount ? kRoleNames[index] : kRoleNames[0];
}

PieceRole pieceRoleFromName(std::string_view name) noexcept {
    for (std::size_t i = 1; i < kPieceRoleCount; ++i) {
        if (kRoleNames[i] == name) {
            return static_cast<PieceRole>(i);
        }
    }
    return PieceRole::None;
}

bool cellRoofed(const sim::TileQuery& tiles, std::int32_t x, std::int32_t y,
                std::int32_t z) noexcept {
    const content::TileForm above = tiles.form(x, y, z + 1);
    return above != content::TileForm::Open && above != content::TileForm::Void;
}

std::filesystem::path staticCataloguePath(const std::filesystem::path& contentDir) {
    return contentDir / "raws" / "world3d" / "docks-pieces.json";
}

// ---------------------------------------------------------------------------
// the catalogue
// ---------------------------------------------------------------------------

StaticCatalogue StaticCatalogue::fromJson(std::string_view json) {
    StaticCatalogue out;
    const nlohmann::json parsed = nlohmann::json::parse(json, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        out.error_ = "the piece catalogue is not a JSON object";
        return out;
    }
    out.minBand_ = intOf(parsed, "minBand", 0);
    // The knobs: under "rules", with the older top-level spellings still
    // read for a catalogue that has them.
    RuleKnobs& k = out.knobs_;
    k.windowEvery = intOf(parsed, "windowEvery", 0);
    k.chimneyEvery = intOf(parsed, "chimneyEvery", 0);
    if (parsed.contains("props") && parsed["props"].is_object()) {
        const nlohmann::json& props = parsed["props"];
        k.propEvery = intOf(props, "every", 0);
        k.propBarrelPercent = intOf(props, "barrel", k.propBarrelPercent);
        k.propCratePercent = intOf(props, "crate", k.propCratePercent);
    }
    if (parsed.contains("rules") && parsed["rules"].is_object()) {
        const nlohmann::json& rules = parsed["rules"];
        k.windowEvery = intOf(rules, "windowEvery", k.windowEvery);
        k.chimneyEvery = intOf(rules, "chimneyEvery", k.chimneyEvery);
        k.propEvery = intOf(rules, "propEvery", k.propEvery);
        k.propBarrelPercent = intOf(rules, "propBarrelPercent", k.propBarrelPercent);
        k.propCratePercent = intOf(rules, "propCratePercent", k.propCratePercent);
        k.tableEvery = intOf(rules, "tableEvery", 0);
        k.shelfEvery = intOf(rules, "shelfEvery", 0);
        k.roofTileEvery = intOf(rules, "roofTileEvery", 0);
        k.roofPropEvery = intOf(rules, "roofPropEvery", 0);
        k.boatEvery = intOf(rules, "boatEvery", 0);
        k.hullFlareDegrees = floatOf(rules, "hullFlareDegrees", 0.0F);
        k.hullTint = tintFromJson(rules.contains("hullTint") ? rules["hullTint"] : nlohmann::json(), Rgba8{});
        k.lanternFlame =
            tintFromJson(rules.contains("lanternFlame") ? rules["lanternFlame"] : nlohmann::json(), k.lanternFlame);
        k.fireFlame = tintFromJson(rules.contains("fireFlame") ? rules["fireFlame"] : nlohmann::json(), k.fireFlame);
        k.litPane = tintFromJson(rules.contains("litPane") ? rules["litPane"] : nlohmann::json(), k.litPane);
        k.roofTint = tintFromJson(rules.contains("roofTint") ? rules["roofTint"] : nlohmann::json(), k.roofTint);
        k.roofFillTint =
            tintFromJson(rules.contains("roofFillTint") ? rules["roofFillTint"] : nlohmann::json(), k.roofFillTint);
    }

    // Pieces in role order, whatever order the file lists them in; a row
    // with several files is one variant per file.
    if (parsed.contains("pieces") && parsed["pieces"].is_object()) {
        const nlohmann::json& pieces = parsed["pieces"];
        for (const auto& [key, value] : pieces.items()) {
            (void)value;
            if (pieceRoleFromName(key) == PieceRole::None) {
                out.warnings_.push_back("unknown piece role: " + key);
            }
        }
        for (std::size_t i = 1; i < kPieceRoleCount; ++i) {
            const std::string key(kRoleNames[i]);
            if (!pieces.contains(key) || !pieces[key].is_object()) {
                continue;
            }
            const nlohmann::json& row = pieces[key];
            const std::string pack = row.value("pack", std::string());
            std::vector<std::string> files;
            if (row.contains("files") && row["files"].is_array()) {
                for (const nlohmann::json& f : row["files"]) {
                    if (f.is_string()) {
                        files.push_back(f.get<std::string>());
                    }
                }
            } else {
                const std::string file = row.value("file", std::string());
                if (!file.empty()) {
                    files.push_back(file);
                }
            }
            if (files.empty()) {
                out.warnings_.push_back("piece without a file: " + key);
                continue;
            }
            for (std::size_t v = 0; v < files.size() && v < 255; ++v) {
                PieceSpec spec;
                spec.role = static_cast<PieceRole>(i);
                spec.variant = static_cast<std::uint8_t>(v);
                spec.file = pack.empty() ? files[v] : pack + "/" + files[v];
                spec.width = floatOf(row, "width", 2.5F);
                spec.height = floatOf(row, "height", 0.0F);
                spec.thickness = floatOf(row, "thickness", 0.225F);
                if (row.contains("standoff") && row["standoff"].is_number()) {
                    spec.standoff = row["standoff"].get<float>();
                    spec.standoffSet = true;
                }
                spec.frontNegZ = row.value("front", std::string("-z")) != "+z";
                if (row.contains("extent") && row["extent"].is_array() && row["extent"].size() >= 4) {
                    const nlohmann::json& e = row["extent"];
                    spec.minX = e[0].is_number() ? e[0].get<float>() : 0.0F;
                    spec.minZ = e[1].is_number() ? e[1].get<float>() : 0.0F;
                    spec.maxX = e[2].is_number() ? e[2].get<float>() : 2.5F;
                    spec.maxZ = e[3].is_number() ? e[3].get<float>() : 2.5F;
                }
                spec.minBlock = intOf(row, "minBlock", 1);
                spec.maxBlock = intOf(row, "maxBlock", 3);
                spec.flipY = row.value("flipY", false);
                spec.upright = row.value("upright", false);
        spec.across = row.value("across", false);
        spec.upright = spec.upright || spec.across;
                spec.selfLit = row.value("selfLit", false);
                spec.twoSided = row.value("twoSided", false);
                spec.lift = floatOf(row, "lift", 0.0F);
                spec.yawOffset = floatOf(row, "yawOffsetDegrees", 0.0F) * (kPi / 180.0F);
                spec.scale = floatOf(row, "scale", 1.0F);
                spec.tint = tintFromJson(row.contains("tint") ? row["tint"] : nlohmann::json(), Rgba8{});
                spec.maxDistance = floatOf(row, "maxDistance", 64.0F);
                spec.cutX = floatOf(row, "cutX", 0.0F);
                spec.cutY = floatOf(row, "cutY", 0.0F);
                out.pieces_.push_back(std::move(spec));
            }
        }
    }

    // KIT BUILD. THE ITEM TABLE: one Item row per registry item id the
    // world may draw on a tile -- "<pack>/<file>", a lift off the floor, a
    // pitch to lay a point-up weapon flat, a scale, a yaw. Sorted by id (the
    // parser's own object order), so the variant an id gets is the same on
    // every machine. An id the sim's registry does not know is still a row
    // here (the catalogue does not read the raws); the instancer simply
    // never asks for it.
    if (parsed.contains("items") && parsed["items"].is_object()) {
        const nlohmann::json& items = parsed["items"];
        std::size_t variant = 0;
        for (const auto& [id, row] : items.items()) {
            if (!row.is_object() || variant >= 255) {
                continue;
            }
            const std::string pack = row.value("pack", std::string());
            const std::string file = row.value("file", std::string());
            if (file.empty()) {
                out.warnings_.push_back("item without a file: " + id);
                continue;
            }
            PieceSpec spec;
            spec.role = PieceRole::Item;
            spec.variant = static_cast<std::uint8_t>(variant);
            spec.file = pack.empty() ? file : pack + "/" + file;
            spec.lift = floatOf(row, "lift", 0.0F);
            spec.yawOffset = floatOf(row, "yawOffsetDegrees", 0.0F) * (kPi / 180.0F);
            spec.pitch = floatOf(row, "pitchDegrees", 0.0F) * (kPi / 180.0F);
            spec.scale = floatOf(row, "scale", 1.0F);
            spec.maxDistance = floatOf(row, "maxDistance", 32.0F);
            spec.selfLit = row.value("selfLit", false);
            spec.tint = tintFromJson(row.contains("tint") ? row["tint"] : nlohmann::json(), Rgba8{});
            out.pieces_.push_back(std::move(spec));
            out.itemIds_.push_back(id);
            ++variant;
        }
    }

    if (parsed.contains("materials") && parsed["materials"].is_object()) {
        const std::span<const std::string_view> known = render::materialIds();
        for (const auto& [id, row] : parsed["materials"].items()) {
            if (!row.is_object()) {
                continue;
            }
            const bool recognised = std::find(known.begin(), known.end(), std::string_view(id)) != known.end();
            if (!recognised) {
                out.warnings_.push_back("unknown material: " + id);
                continue;
            }
            MaterialRule rule;
            rule.material = id;
            const std::string cls = row.value("wall", std::string("none"));
            rule.wallClass = cls == "masonry" ? WallClass::Masonry
                             : cls == "timber" ? WallClass::Timber
                             : cls == "canvas" ? WallClass::Canvas
                                               : WallClass::None;
            rule.plasterOut = row.value("finish", std::string("brick")) == "plaster";
            rule.tint = tintFromJson(row.contains("tint") ? row["tint"] : nlohmann::json(), Rgba8{});
            rule.insideTint =
                tintFromJson(row.contains("insideTint") ? row["insideTint"] : nlohmann::json(), Rgba8{});
            rule.minBand = intOf(row, "minBand", 0);
            rule.floorRole = pieceRoleFromName(row.value("floor", std::string("none")));
            rule.floorTint =
                tintFromJson(row.contains("floorTint") ? row["floorTint"] : nlohmann::json(), Rgba8{});
            rule.fillRole = pieceRoleFromName(row.value("fill", std::string("none")));
            rule.fillTint =
                tintFromJson(row.contains("fillTint") ? row["fillTint"] : nlohmann::json(), Rgba8{});
            if (row.contains("topTint")) {
                rule.topTint = tintFromJson(row["topTint"], Rgba8{});
                rule.topTintSet = true;
            }
            if (row.contains("ceilingTint")) {
                rule.ceilingTint = tintFromJson(row["ceilingTint"], Rgba8{});
                rule.ceilingTintSet = true;
            }
            rule.lipRole = pieceRoleFromName(row.value("lip", std::string("none")));
            rule.lipTint = tintFromJson(row.contains("lipTint") ? row["lipTint"] : nlohmann::json(), Rgba8{});
            rule.roof = row.value("roof", false);
            out.materials_.push_back(std::move(rule));
        }
        // By name, so lookups by id below are stable whatever the file's order.
        std::sort(out.materials_.begin(), out.materials_.end(),
                  [](const MaterialRule& a, const MaterialRule& b) { return a.material < b.material; });
    }
    return out;
}

StaticCatalogue StaticCatalogue::load(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        StaticCatalogue empty;
        return empty;
    }
    std::ostringstream text;
    text << in.rdbuf();
    return fromJson(text.str());
}

const PieceSpec* StaticCatalogue::piece(PieceRole role, std::uint8_t variant) const noexcept {
    for (const PieceSpec& spec : pieces_) {
        if (spec.role == role && spec.variant == variant) {
            return &spec;
        }
    }
    return nullptr;
}

int StaticCatalogue::pieceIndex(PieceRole role, std::uint8_t variant) const noexcept {
    for (std::size_t i = 0; i < pieces_.size(); ++i) {
        if (pieces_[i].role == role && pieces_[i].variant == variant) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::uint8_t StaticCatalogue::variantCount(PieceRole role) const noexcept {
    std::uint8_t n = 0;
    for (const PieceSpec& spec : pieces_) {
        if (spec.role == role) {
            ++n;
        }
    }
    return n;
}

const PieceSpec* StaticCatalogue::itemPiece(std::string_view itemId) const noexcept {
    for (std::size_t v = 0; v < itemIds_.size(); ++v) {
        if (itemIds_[v] == itemId) {
            return piece(PieceRole::Item, static_cast<std::uint8_t>(v));
        }
    }
    return nullptr;
}

const MaterialRule* StaticCatalogue::materialByName(std::string_view id) const noexcept {
    for (const MaterialRule& rule : materials_) {
        if (rule.material == id) {
            return &rule;
        }
    }
    return nullptr;
}

const MaterialRule* StaticCatalogue::material(std::uint16_t materialId) const noexcept {
    const std::span<const std::string_view> ids = render::materialIds();
    if (materialId >= ids.size()) {
        return nullptr;
    }
    return materialByName(ids[materialId]);
}

std::vector<StaticPieceRef> StaticCatalogue::pieceRefs() const {
    std::vector<StaticPieceRef> refs;
    refs.reserve(pieces_.size());
    for (const PieceSpec& spec : pieces_) {
        refs.push_back(StaticPieceRef{spec.file});
    }
    return refs;
}

std::uint64_t StaticCatalogue::digest() const noexcept {
    Fnv1a64 h;
    h.mixU32(static_cast<std::uint32_t>(pieces_.size()));
    for (const PieceSpec& spec : pieces_) {
        h.mixU8(static_cast<std::uint8_t>(spec.role));
        h.mixU8(spec.variant);
        h.mix(spec.file.data(), spec.file.size());
        h.mixF32(spec.width);
        h.mixF32(spec.height);
        h.mixF32(spec.thickness);
        h.mixF32(spec.standoff);
        h.mixF32(spec.pitch);
        h.mixU8(static_cast<std::uint8_t>(spec.frontNegZ ? 1 : 0));
        h.mixF32(spec.minX);
        h.mixF32(spec.minZ);
        h.mixF32(spec.maxX);
        h.mixF32(spec.maxZ);
        h.mixI32(spec.minBlock);
        h.mixI32(spec.maxBlock);
        h.mixU8(static_cast<std::uint8_t>(spec.flipY ? 1 : 0));
        h.mixU8(static_cast<std::uint8_t>(spec.upright ? 1 : 0));
        h.mixU8(static_cast<std::uint8_t>(spec.across ? 1 : 0));
        h.mixU8(static_cast<std::uint8_t>(spec.selfLit ? 1 : 0));
        h.mixU8(static_cast<std::uint8_t>(spec.twoSided ? 1 : 0));
        h.mixF32(spec.lift);
        h.mixF32(spec.yawOffset);
        h.mixF32(spec.scale);
        h.mixU8(spec.tint.r);
        h.mixU8(spec.tint.g);
        h.mixU8(spec.tint.b);
        h.mixF32(spec.maxDistance);
        h.mixF32(spec.cutX);
        h.mixF32(spec.cutY);
    }
    h.mixU32(static_cast<std::uint32_t>(materials_.size()));
    for (const MaterialRule& rule : materials_) {
        h.mix(rule.material.data(), rule.material.size());
        h.mixU8(static_cast<std::uint8_t>(rule.wallClass));
        h.mixU8(static_cast<std::uint8_t>(rule.plasterOut ? 1 : 0));
        h.mixU8(rule.tint.r);
        h.mixU8(rule.tint.g);
        h.mixU8(rule.tint.b);
        h.mixU8(rule.insideTint.r);
        h.mixU8(rule.insideTint.g);
        h.mixU8(rule.insideTint.b);
        h.mixI32(rule.minBand);
        h.mixU8(static_cast<std::uint8_t>(rule.floorRole));
        h.mixU8(rule.floorTint.r);
        h.mixU8(rule.floorTint.g);
        h.mixU8(rule.floorTint.b);
        h.mixU8(static_cast<std::uint8_t>(rule.fillRole));
        h.mixU8(rule.fillTint.r);
        h.mixU8(rule.fillTint.g);
        h.mixU8(rule.fillTint.b);
        h.mixU8(static_cast<std::uint8_t>(rule.topTintSet ? 1 : 0));
        h.mixU8(rule.topTint.r);
        h.mixU8(rule.topTint.g);
        h.mixU8(rule.topTint.b);
        h.mixU8(static_cast<std::uint8_t>(rule.ceilingTintSet ? 1 : 0));
        h.mixU8(rule.ceilingTint.r);
        h.mixU8(rule.ceilingTint.g);
        h.mixU8(rule.ceilingTint.b);
        h.mixU8(static_cast<std::uint8_t>(rule.lipRole));
        h.mixU8(rule.lipTint.r);
        h.mixU8(rule.lipTint.g);
        h.mixU8(rule.lipTint.b);
        h.mixU8(static_cast<std::uint8_t>(rule.roof ? 1 : 0));
    }
    h.mixI32(minBand_);
    h.mixI32(knobs_.propEvery);
    h.mixI32(knobs_.propBarrelPercent);
    h.mixI32(knobs_.propCratePercent);
    h.mixI32(knobs_.windowEvery);
    h.mixI32(knobs_.chimneyEvery);
    h.mixI32(knobs_.tableEvery);
    h.mixI32(knobs_.shelfEvery);
    h.mixI32(knobs_.roofTileEvery);
    h.mixI32(knobs_.roofPropEvery);
    h.mixI32(knobs_.boatEvery);
    h.mixF32(knobs_.hullFlareDegrees);
    h.mixU8(knobs_.hullTint.r);
    h.mixU8(knobs_.hullTint.g);
    h.mixU8(knobs_.hullTint.b);
    h.mixU8(knobs_.lanternFlame.r);
    h.mixU8(knobs_.lanternFlame.g);
    h.mixU8(knobs_.lanternFlame.b);
    h.mixU8(knobs_.fireFlame.r);
    h.mixU8(knobs_.fireFlame.g);
    h.mixU8(knobs_.fireFlame.b);
    h.mixU8(knobs_.litPane.r);
    h.mixU8(knobs_.litPane.g);
    h.mixU8(knobs_.litPane.b);
    h.mixU8(knobs_.roofTint.r);
    h.mixU8(knobs_.roofTint.g);
    h.mixU8(knobs_.roofTint.b);
    h.mixU8(knobs_.roofFillTint.r);
    h.mixU8(knobs_.roofFillTint.g);
    h.mixU8(knobs_.roofFillTint.b);
    return h.value();
}

// ---------------------------------------------------------------------------
// the rules
// ---------------------------------------------------------------------------

StaticPlacements placeStaticPieces(const sim::TileQuery& tiles, const StaticCatalogue& catalogue,
                                   const std::vector<render::Lamp>& lamps) {
    Placer placer(tiles, catalogue, lamps);
    return placer.run();
}

}  // namespace granadad::render3d
