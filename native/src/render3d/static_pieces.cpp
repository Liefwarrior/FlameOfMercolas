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

/// The JSON keys, in PieceRole order.
constexpr std::string_view kRoleNames[kPieceRoleCount] = {
    "none",        "wall",        "wall_corner", "wall_window", "wall_door", "roof_edge",
    "floor_plank", "floor_cobble", "floor_flag", "water",       "prop_barrel", "prop_crate",
    "prop_sack",   "lamp_wall",   "lamp_post",   "brazier",     "floor_fill",  "wall_cap",
    "ceiling",
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

[[nodiscard]] Rgba8 tintFromJson(const nlohmann::json& value, Rgba8 fallback) {
    if (!value.is_array() || value.size() < 3) {
        return fallback;
    }
    const auto ch = [&value](std::size_t i) {
        const int v = value[i].is_number() ? value[i].get<int>() : 255;
        return static_cast<std::uint8_t>(std::clamp(v, 0, 255));
    };
    return Rgba8{ch(0), ch(1), ch(2), 255};
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
    /// The wall line ends there (a convex corner of the plan).
    bool convexA0 = false;
    bool convexA1 = false;
    /// The corner piece that takes over that end, or -1.
    int cornerA0 = -1;
    int cornerA1 = -1;
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

class Placer {
public:
    Placer(const sim::TileQuery& tiles, const StaticCatalogue& catalogue,
           const std::vector<render::Lamp>& lamps)
        : tiles_(tiles), catalogue_(catalogue), lamps_(lamps) {}

    StaticPlacements run() {
        if (catalogue_.empty()) {
            return std::move(out_);
        }
        walls();
        roofEdges();
        doors();
        floors();
        wallCaps();
        ceilings();
        water();
        props();
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

    /// A storey's piece is fitted to the band less a centimetre, so its top
    /// cap sits just under the chunk's own wall top and the two never fight.
    [[nodiscard]] float heightScale(const PieceSpec& spec) const noexcept {
        return spec.height > 0.01F ? (render::kBandHeight - 0.01F) / spec.height : 1.0F;
    }

    void emit(StaticPlacement placement) {
        ++out_.stats.byRole[static_cast<std::size_t>(placement.role)];
        placement.instance.role = static_cast<std::uint8_t>(placement.role);
        out_.placements.push_back(std::move(placement));
    }

    /// A piece laid along a face run over [a0, a1] with its outdoor finish
    /// facing out when `frontOut`, its local z = 0 plane `standoff` out from
    /// the face, standing on `yBase`.
    void facePiece(const FaceRun& run, PieceRole role, const PieceSpec& spec, float a0, float a1,
                   bool frontOut, float standoff, float yBase, std::int32_t lightX,
                   std::int32_t lightY, const Rgba8& tint, std::int32_t lightX2 = INT32_MIN,
                   std::int32_t lightY2 = INT32_MIN) {
        const bool flip = spec.frontNegZ != frontOut;
        const float aOrigin = flip ? a1 : a0;
        StaticPlacement p;
        p.role = role;
        p.lightX2 = lightX2 == INT32_MIN ? lightX : lightX2;
        p.lightY2 = lightY2 == INT32_MIN ? lightY : lightY2;
        p.instance.piece = static_cast<std::uint16_t>(catalogue_.pieceIndex(role));
        p.instance.position =
            Vec3{run.baseX + kTangentX[run.side] * aOrigin + kNormalX[run.side] * standoff,
                 yBase + spec.lift,
                 run.baseZ + kTangentZ[run.side] * aOrigin + kNormalZ[run.side] * standoff};
        p.instance.yaw = wrapYaw(yawOf(run.side) + (flip ? kPi : 0.0F) + spec.yawOffset);
        p.instance.scale = Vec3{(a1 - a0) / spec.width * spec.scale,
                                heightScale(spec) * spec.scale, spec.scale};
        p.instance.tint = mulTint(spec.tint, tint);
        p.lightX = lightX;
        p.lightY = lightY;
        p.lightZ = run.z;
        p.facing = (run.side == kNorth || run.side == kSouth) ? render::kFacingY
                                                              : render::kFacingX;
        emit(std::move(p));
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

    // --- walls ------------------------------------------------------------

    void walls() {
        const PieceSpec* wall = catalogue_.piece(PieceRole::Wall);
        if (wall == nullptr) {
            return;
        }
        const PieceSpec* window = catalogue_.piece(PieceRole::WallWindow);
        const PieceSpec* cornerSpec = catalogue_.piece(PieceRole::WallCorner);

        std::vector<FaceRun> runs;
        std::map<std::uint64_t, std::size_t> runOf;
        scanRuns(runs, runOf, /*roofEdge=*/false);
        out_.stats.wallRuns = runs.size();

        // Corners: a cell with exactly two exposed adjacent sides, both out
        // of doors, masonry, that ends a run on each.
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
            // The leg length: a corner's legs are the kit's own 2.5 m unless
            // a run is too short to carry them, in which case both legs
            // shrink to the run's share.
            const auto share = [&runs](std::size_t runIndex) {
                const FaceRun& r = runs[runIndex];
                const float len = r.a1 - r.a0;
                const int cornersOnRun = (r.cornerA0 >= 0 ? 1 : 0) + (r.cornerA1 >= 0 ? 1 : 0);
                return std::min(1.0F, len / (2.5F * static_cast<float>(std::max(1, cornersOnRun))));
            };
            for (Corner& c : corners) {
                c.s = std::min(share(c.runA), share(c.runB));
                placeCorner(c, *cornerSpec);
            }
        }

        for (const FaceRun& r : runs) {
            const float ext = wall->thickness;
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
            if (hi - lo < 0.05F) {
                continue;
            }
            const int k = std::max(1, static_cast<int>(std::lround((hi - lo) / wall->width)));
            const float len = (hi - lo) / static_cast<float>(k);
            const bool brickOut = r.brickOut;
            const bool windowOk = r.outdoor && r.cls == WallClass::Masonry;
            const std::int32_t cells = std::max(1, static_cast<std::int32_t>(std::lround(r.a1 - r.a0)));
            std::int32_t dx = 0, dy = 0;
            runStep(r.side, dx, dy);
            for (int i = 0; i < k; ++i) {
                const float pa0 = lo + len * static_cast<float>(i);
                const float pa1 = i + 1 == k ? hi : pa0 + len;
                // The cell under the piece's middle: its light, and the
                // window draw.
                const std::int32_t along = std::clamp(
                    static_cast<std::int32_t>(std::floor(pa0 + 0.5F * len - r.a0)), 0, cells - 1);
                const std::int32_t first = std::clamp(
                    static_cast<std::int32_t>(std::floor(pa0 - r.a0 + 0.01F)), 0, cells - 1);
                const std::int32_t last = std::clamp(
                    static_cast<std::int32_t>(std::floor(pa1 - r.a0 - 0.01F)), 0, cells - 1);
                const std::int32_t lx = r.firstX + dx * along;
                const std::int32_t ly = r.firstY + dy * along;
                PieceRole role = PieceRole::Wall;
                const PieceSpec* spec = wall;
                if (windowOk && window != nullptr && catalogue_.windowEvery() > 0 &&
                    len >= 1.6F) {
                    const std::uint32_t h =
                        cellHash(lx, ly, r.z, 0x57494E44U + static_cast<std::uint32_t>(r.side));
                    if (h % static_cast<std::uint32_t>(catalogue_.windowEvery()) == 0U) {
                        role = PieceRole::WallWindow;
                        spec = window;
                    }
                }
                // Lit over the cells it spans, first to last.
                facePiece(r, role, *spec, pa0, pa1, brickOut,
                          spec->standoffSet ? spec->standoff : spec->thickness * 0.5F,
                          render::bandSurface(r.z), r.firstX + dx * first, r.firstY + dy * first,
                          r.tint, r.firstX + dx * last, r.firstY + dy * last);
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
        p.instance.piece = static_cast<std::uint16_t>(catalogue_.pieceIndex(PieceRole::WallCorner));
        p.instance.position = Vec3{corner.x + offset.x, render::bandSurface(c.z) + spec.lift,
                                   corner.z + offset.z};
        p.instance.yaw = wrapYaw(yawOf(c.side) + spec.yawOffset);
        p.instance.scale = Vec3{c.s * spec.scale, heightScale(spec) * spec.scale, c.s * spec.scale};
        p.instance.tint = mulTint(spec.tint, c.tint);
        p.lightX = c.x;
        p.lightY = c.y;
        p.lightX2 = c.x;
        p.lightY2 = c.y;
        p.lightZ = c.z;
        p.facing = (render::kFacingX + render::kFacingY) * 0.5F;
        emit(std::move(p));
    }

    /// Every exposed, classed wall face grouped into runs. `roofEdge` asks
    /// for the outdoor faces of top-storey walls instead (one run class).
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
                        std::uint16_t material = 0;
                        if (qualifies) {
                            cls = wallClassAt(x, y, z);
                            qualifies = cls != WallClass::None;
                        }
                        if (qualifies) {
                            outdoor = faceOutdoor(x, y, z, side);
                            material = tiles_.material(x, y, z);
                            if (roofEdge) {
                                // A cornice belongs to masonry: a hull or a
                                // shed of timber has no roof line to trim.
                                qualifies = outdoor && cls == WallClass::Masonry &&
                                            !isWall(tiles_, x, y, z + 1);
                            }
                        }
                        const bool joins = qualifies && open && current.material == material &&
                                           current.outdoor == outdoor && current.cls == cls;
                        if (open && !joins) {
                            // Close: convex at the a1 end when the wall line
                            // itself stops there.
                            current.convexA1 = !isWall(tiles_, current.lastX + dx, current.lastY + dy, z);
                            runs.push_back(current);
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
                            const MaterialRule* r = rule(x, y, z);
                            // Out of doors the face wears the material's
                            // tint on its finish (brick, or plaster for a
                            // rendered building); an indoor masonry face is
                            // plaster in its own tint.
                            const bool plaster = cls == WallClass::Masonry && !outdoor;
                            current.tint = r == nullptr ? Rgba8{} : (plaster ? r->insideTint : r->tint);
                            current.brickOut = outdoor && cls == WallClass::Masonry &&
                                               (r == nullptr || !r->plasterOut);
                            current.convexA0 = !isWall(tiles_, x - dx, y - dy, z);
                            open = true;
                        }
                        current.a1 = a1;
                        current.lastX = x;
                        current.lastY = y;
                        runOf.emplace(cellKey(z, side, x, y), runs.size());
                    }
                    if (open) {
                        current.convexA1 = !isWall(tiles_, current.lastX + dx, current.lastY + dy, z);
                        runs.push_back(current);
                    }
                }
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
                facePiece(r, PieceRole::RoofEdge, *trim, pa0, pa1, true,
                          trim->standoffSet ? trim->standoff : wallProud,
                          render::bandSurface(r.z + 1), r.firstX, r.firstY, r.tint);
            }
        }
    }

    // --- doors ------------------------------------------------------------

    void doors() {
        const PieceSpec* door = catalogue_.piece(PieceRole::WallDoor);
        if (door == nullptr) {
            return;
        }
        const std::int32_t zLo = std::max(0, catalogue_.minBand());
        for (std::int32_t z = zLo; z < tiles_.sizeZ(); ++z) {
            // Gaps in wall lines that run along X (a row of walls with a hole).
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
                    if (w >= 2 && w <= 3 && isWall(tiles_, x - 1, y, z) && isWall(tiles_, x1 + 1, y, z) &&
                        isWall(tiles_, x - 2, y, z) && isWall(tiles_, x1 + 2, y, z) &&
                        wallClassAt(x - 1, y, z) != WallClass::None) {
                        const bool outN = !cellRoofed(tiles_, x, y - 1, z);
                        const bool outS = !cellRoofed(tiles_, x, y + 1, z);
                        if (outN != outS) {
                            FaceRun r;
                            r.z = z;
                            r.side = outN ? kNorth : kSouth;
                            setBase(r, static_cast<float>(y) + 0.5F);
                            float lo = 0.0F, hi = 0.0F, dummy = 0.0F;
                            if (r.side == kNorth) {
                                cellInterval(kNorth, x, y, lo, dummy);
                                cellInterval(kNorth, x1, y, dummy, hi);
                            } else {
                                cellInterval(kSouth, x1, y, lo, dummy);
                                cellInterval(kSouth, x, y, dummy, hi);
                            }
                            const MaterialRule* jamb = rule(x - 1, y, z);
                            facePiece(r, PieceRole::WallDoor, *door, lo, hi, true,
                                      door->standoffSet ? door->standoff : 0.0F,
                                      render::bandSurface(z), x, y,
                                      jamb != nullptr ? jamb->tint : Rgba8{});
                            ++out_.stats.doorGaps;
                        }
                    }
                    x = x1 + 1;
                }
            }
            // Gaps in wall lines that run along Y.
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
                    if (w >= 2 && w <= 3 && isWall(tiles_, x, y - 1, z) && isWall(tiles_, x, y1 + 1, z) &&
                        isWall(tiles_, x, y - 2, z) && isWall(tiles_, x, y1 + 2, z) &&
                        wallClassAt(x, y - 1, z) != WallClass::None) {
                        const bool outW = !cellRoofed(tiles_, x - 1, y, z);
                        const bool outE = !cellRoofed(tiles_, x + 1, y, z);
                        if (outW != outE) {
                            FaceRun r;
                            r.z = z;
                            r.side = outE ? kEast : kWest;
                            setBase(r, static_cast<float>(x) + 0.5F);
                            float lo = 0.0F, hi = 0.0F, dummy = 0.0F;
                            if (r.side == kEast) {
                                cellInterval(kEast, x, y, lo, dummy);
                                cellInterval(kEast, x, y1, dummy, hi);
                            } else {
                                cellInterval(kWest, x, y1, lo, dummy);
                                cellInterval(kWest, x, y, dummy, hi);
                            }
                            const MaterialRule* jamb = rule(x, y - 1, z);
                            facePiece(r, PieceRole::WallDoor, *door, lo, hi, true,
                                      door->standoffSet ? door->standoff : 0.0F,
                                      render::bandSurface(z), x, y,
                                      jamb != nullptr ? jamb->tint : Rgba8{});
                            ++out_.stats.doorGaps;
                        }
                    }
                    y = y1 + 1;
                }
            }
        }
    }

    /// A walkable cell open to both north and south: a hole in an X-line.
    [[nodiscard]] bool gapCellX(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept {
        return isWalkableForm(tiles_, x, y, z) && isOpenish(tiles_, x, y - 1, z) &&
               isOpenish(tiles_, x, y + 1, z);
    }

    [[nodiscard]] bool gapCellY(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept {
        return isWalkableForm(tiles_, x, y, z) && isOpenish(tiles_, x - 1, y, z) &&
               isOpenish(tiles_, x + 1, y, z);
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

    /// A flat piece fitted to the w x h block whose north-west tile is
    /// (x, y): the piece's local footprint is mapped onto the block.
    void blockPiece(PieceRole role, const PieceSpec& spec, std::int32_t x, std::int32_t y,
                    std::int32_t z, std::int32_t w, std::int32_t h, float surface,
                    const Rgba8& tint) {
        const float sx = static_cast<float>(w) / std::max(0.01F, spec.maxX - spec.minX);
        const float sz = static_cast<float>(h) / std::max(0.01F, spec.maxZ - spec.minZ);
        StaticPlacement p;
        p.role = role;
        p.instance.piece = static_cast<std::uint16_t>(catalogue_.pieceIndex(role));
        p.instance.position = Vec3{static_cast<float>(x) - spec.minX * sx, surface + spec.lift,
                                   static_cast<float>(y) - spec.minZ * sz};
        p.instance.yaw = wrapYaw(spec.yawOffset);
        p.instance.scale =
            Vec3{sx * spec.scale, spec.flipY ? -spec.scale : spec.scale, sz * spec.scale};
        p.instance.tint = mulTint(spec.tint, tint);
        p.lightX = x;
        p.lightY = y;
        p.lightX2 = x + w - 1;
        p.lightY2 = y + h - 1;
        p.lightZ = z;
        p.facing = 1.0F;
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
                covered_[coverIndex(xx, yy, z)] = 1U;
            }
        }
    }

    [[nodiscard]] bool floorCell(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept {
        return tiles_.form(x, y, z) == content::TileForm::Floor && tiles_.fluidDepth(x, y, z) == 0;
    }

    /// Floors, per material and level: the material's own floor piece
    /// first (a patterned block piece wants whole 2..3 tile blocks; planks
    /// and flat fills take any rectangle), then the material's fill piece
    /// over whatever the first pass could not fit -- so no cell of a
    /// dressed material keeps the atlas tile unless the catalogue says so.
    void floors() {
        ensureCover();
        const std::int32_t zLo = std::max(0, catalogue_.minBand());
        const std::span<const std::string_view> ids = render::materialIds();
        for (std::int32_t z = zLo; z < tiles_.sizeZ(); ++z) {
            for (std::size_t m = 0; m < ids.size(); ++m) {
                const auto material = static_cast<std::uint16_t>(m);
                const MaterialRule* r = catalogue_.material(material);
                if (r == nullptr || (r->floorRole == PieceRole::None && r->fillRole == PieceRole::None)) {
                    continue;
                }
                const auto same = [&](std::int32_t x, std::int32_t y) {
                    return floorCell(x, y, z) && tiles_.material(x, y, z) == material;
                };
                const PieceSpec* spec =
                    r->floorRole != PieceRole::None ? catalogue_.piece(r->floorRole) : nullptr;
                if (spec != nullptr) {
                    const PieceRole role = r->floorRole;
                    const Rgba8 tint = r->floorTint;
                    mergeRectangles(z, std::max(1, spec->minBlock), std::max(1, spec->maxBlock), same,
                                    [&](std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h) {
                                        blockPiece(role, *spec, x, y, z, w, h, render::bandSurface(z), tint);
                                    });
                }
                const PieceSpec* fill =
                    r->fillRole != PieceRole::None ? catalogue_.piece(r->fillRole) : nullptr;
                if (fill != nullptr) {
                    const PieceRole role = r->fillRole;
                    const Rgba8 tint = r->fillTint;
                    mergeRectangles(z, 1, std::max(1, fill->maxBlock), same,
                                    [&](std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h) {
                                        blockPiece(role, *fill, x, y, z, w, h, render::bandSurface(z), tint);
                                    });
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

    /// The quad under every floor slab with something other than solid
    /// under it: a room's ceiling, a pier's underside.
    void ceilings() {
        const PieceSpec* ceiling = catalogue_.piece(PieceRole::Ceiling);
        if (ceiling == nullptr) {
            return;
        }
        resetCover();
        const std::int32_t zLo = std::max(1, catalogue_.minBand());
        const std::span<const std::string_view> ids = render::materialIds();
        for (std::int32_t z = zLo; z < tiles_.sizeZ(); ++z) {
            for (std::size_t m = 0; m < ids.size(); ++m) {
                const auto material = static_cast<std::uint16_t>(m);
                const MaterialRule* r = catalogue_.material(material);
                const auto same = [&](std::int32_t x, std::int32_t y) {
                    return tiles_.form(x, y, z) == content::TileForm::Floor &&
                           tiles_.material(x, y, z) == material && isOpenish(tiles_, x, y, z - 1);
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
                                });
            }
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
            for (int depth = 1; depth <= 7; ++depth) {
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

    void props() {
        const std::int32_t every = catalogue_.propEvery();
        if (every <= 0) {
            return;
        }
        const PieceSpec* barrel = catalogue_.piece(PieceRole::PropBarrel);
        const PieceSpec* crate = catalogue_.piece(PieceRole::PropCrate);
        const PieceSpec* sack = catalogue_.piece(PieceRole::PropSack);
        if (barrel == nullptr && crate == nullptr && sack == nullptr) {
            return;
        }
        const std::int32_t zLo = std::max(0, catalogue_.minBand());
        for (std::int32_t z = zLo; z < tiles_.sizeZ(); ++z) {
            for (std::int32_t y = 1; y + 1 < tiles_.sizeY(); ++y) {
                for (std::int32_t x = 1; x + 1 < tiles_.sizeX(); ++x) {
                    if (!floorCell(x, y, z)) {
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
                    const std::uint32_t h = cellHash(x, y, z, 0x50524F50U);
                    if (h % static_cast<std::uint32_t>(every) != 0U) {
                        continue;
                    }
                    const std::uint32_t pick = (h / static_cast<std::uint32_t>(every)) % 100U;
                    PieceRole role = PieceRole::PropSack;
                    const PieceSpec* spec = sack;
                    if (pick < static_cast<std::uint32_t>(std::max(0, catalogue_.propBarrelPercent()))) {
                        role = PieceRole::PropBarrel;
                        spec = barrel;
                    } else if (pick < static_cast<std::uint32_t>(std::max(
                                          0, catalogue_.propBarrelPercent() + catalogue_.propCratePercent()))) {
                        role = PieceRole::PropCrate;
                        spec = crate;
                    }
                    if (spec == nullptr) {
                        // The catalogue lacks that prop: the barrel stands in,
                        // or nothing does.
                        if (barrel == nullptr) {
                            continue;
                        }
                        role = PieceRole::PropBarrel;
                        spec = barrel;
                    }
                    StaticPlacement p;
                    p.role = role;
                    p.instance.piece = static_cast<std::uint16_t>(catalogue_.pieceIndex(role));
                    // Against the wall: the tile centre pushed a little toward it.
                    p.instance.position =
                        Vec3{static_cast<float>(x) + 0.5F + kNormalX[wallSide] * 0.12F,
                             render::bandSurface(z) + spec->lift,
                             static_cast<float>(y) + 0.5F + kNormalZ[wallSide] * 0.12F};
                    p.instance.yaw = wrapYaw(static_cast<float>((h >> 12) & 3U) * kHalfPi +
                                             (static_cast<float>((h >> 16) & 15U) - 7.5F) * 0.02F +
                                             spec->yawOffset);
                    p.instance.scale = Vec3{spec->scale, spec->scale, spec->scale};
                    p.instance.tint = spec->tint;
                    p.lightX = x;
                    p.lightY = y;
                    p.lightX2 = x;
                    p.lightY2 = y;
                    p.lightZ = z;
                    p.facing = 1.0F;
                    emit(std::move(p));
                }
            }
        }
    }

    // --- lamps ------------------------------------------------------------

    void lampPieces() {
        const PieceSpec* brazier = catalogue_.piece(PieceRole::Brazier);
        const PieceSpec* wallLamp = catalogue_.piece(PieceRole::LampWall);
        const PieceSpec* post = catalogue_.piece(PieceRole::LampPost);
        for (const render::Lamp& lamp : lamps_) {
            if (!tiles_.inBounds(lamp.x, lamp.y, lamp.z)) {
                continue;
            }
            if (lamp.warmth == render::LampWarmth::Fire) {
                if (brazier != nullptr) {
                    centrePiece(PieceRole::Brazier, *brazier, lamp.x, lamp.y, lamp.z, 0.0F);
                }
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
                        // Toward the gap: 0.3 of a tile in from the jamb's
                        // edge nearest the door.
                        shift = -0.2F * static_cast<float>(dir);
                    }
                }
            }
            if (wallSide >= 0 && wallLamp != nullptr) {
                // Hung on that wall's face, which looks back at the lamp's
                // own row or column: the piece's local +Z (its lamp) points
                // away from the wall.
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
                const float centre = (a0 + a1) * 0.5F +
                                     ((face == kNorth || face == kEast) ? shift : -shift);
                const bool flip = !wallLamp->frontNegZ;
                StaticPlacement p;
                p.role = PieceRole::LampWall;
                p.instance.piece = static_cast<std::uint16_t>(catalogue_.pieceIndex(PieceRole::LampWall));
                const float standoff = wallLamp->standoffSet ? wallLamp->standoff : 0.0F;
                p.instance.position = Vec3{r.baseX + kTangentX[face] * centre + kNormalX[face] * standoff,
                                           render::bandSurface(lamp.z) + wallLamp->lift,
                                           r.baseZ + kTangentZ[face] * centre + kNormalZ[face] * standoff};
                p.instance.yaw = wrapYaw(yawOf(face) + (flip ? kPi : 0.0F) + wallLamp->yawOffset);
                p.instance.scale = Vec3{wallLamp->scale, wallLamp->scale, wallLamp->scale};
                p.instance.tint = wallLamp->tint;
                p.lightX = lamp.x;
                p.lightY = lamp.y;
                p.lightX2 = lamp.x;
                p.lightY2 = lamp.y;
                p.lightZ = lamp.z;
                p.facing = 1.0F;
                emit(std::move(p));
            } else if (post != nullptr) {
                centrePiece(PieceRole::LampPost, *post, lamp.x, lamp.y, lamp.z,
                            static_cast<float>((lamp.x + lamp.y) & 3) * kHalfPi);
            }
        }
    }

    void centrePiece(PieceRole role, const PieceSpec& spec, std::int32_t x, std::int32_t y,
                     std::int32_t z, float yaw) {
        StaticPlacement p;
        p.role = role;
        p.instance.piece = static_cast<std::uint16_t>(catalogue_.pieceIndex(role));
        p.instance.position = Vec3{static_cast<float>(x) + 0.5F, render::bandSurface(z) + spec.lift,
                                   static_cast<float>(y) + 0.5F};
        p.instance.yaw = wrapYaw(yaw + spec.yawOffset);
        p.instance.scale = Vec3{spec.scale, spec.scale, spec.scale};
        p.instance.tint = spec.tint;
        p.lightX = x;
        p.lightY = y;
        p.lightX2 = x;
        p.lightY2 = y;
        p.lightZ = z;
        p.facing = 1.0F;
        emit(std::move(p));
    }

    const sim::TileQuery& tiles_;
    const StaticCatalogue& catalogue_;
    const std::vector<render::Lamp>& lamps_;
    std::vector<std::uint8_t> covered_;
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
    out.windowEvery_ = intOf(parsed, "windowEvery", 0);
    if (parsed.contains("props") && parsed["props"].is_object()) {
        const nlohmann::json& props = parsed["props"];
        out.propEvery_ = intOf(props, "every", 0);
        out.propBarrel_ = intOf(props, "barrel", 40);
        out.propCrate_ = intOf(props, "crate", 35);
    }

    // Pieces in role order, whatever order the file lists them in.
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
            const std::string file = row.value("file", std::string());
            if (file.empty()) {
                out.warnings_.push_back("piece without a file: " + key);
                continue;
            }
            PieceSpec spec;
            spec.role = static_cast<PieceRole>(i);
            spec.file = pack.empty() ? file : pack + "/" + file;
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
            spec.lift = floatOf(row, "lift", 0.0F);
            spec.yawOffset = floatOf(row, "yawOffsetDegrees", 0.0F) * (kPi / 180.0F);
            spec.scale = floatOf(row, "scale", 1.0F);
            spec.tint = tintFromJson(row.contains("tint") ? row["tint"] : nlohmann::json(), Rgba8{});
            spec.maxDistance = floatOf(row, "maxDistance", 64.0F);
            out.pieces_.push_back(std::move(spec));
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

const PieceSpec* StaticCatalogue::piece(PieceRole role) const noexcept {
    for (const PieceSpec& spec : pieces_) {
        if (spec.role == role) {
            return &spec;
        }
    }
    return nullptr;
}

int StaticCatalogue::pieceIndex(PieceRole role) const noexcept {
    for (std::size_t i = 0; i < pieces_.size(); ++i) {
        if (pieces_[i].role == role) {
            return static_cast<int>(i);
        }
    }
    return -1;
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
        h.mix(spec.file.data(), spec.file.size());
        h.mixF32(spec.width);
        h.mixF32(spec.height);
        h.mixF32(spec.thickness);
        h.mixF32(spec.standoff);
        h.mixU8(static_cast<std::uint8_t>(spec.frontNegZ ? 1 : 0));
        h.mixF32(spec.minX);
        h.mixF32(spec.minZ);
        h.mixF32(spec.maxX);
        h.mixF32(spec.maxZ);
        h.mixI32(spec.minBlock);
        h.mixI32(spec.maxBlock);
        h.mixU8(static_cast<std::uint8_t>(spec.flipY ? 1 : 0));
        h.mixF32(spec.lift);
        h.mixF32(spec.yawOffset);
        h.mixF32(spec.scale);
        h.mixU8(spec.tint.r);
        h.mixU8(spec.tint.g);
        h.mixU8(spec.tint.b);
        h.mixF32(spec.maxDistance);
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
    }
    h.mixI32(minBand_);
    h.mixI32(propEvery_);
    h.mixI32(propBarrel_);
    h.mixI32(propCrate_);
    h.mixI32(windowEvery_);
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
