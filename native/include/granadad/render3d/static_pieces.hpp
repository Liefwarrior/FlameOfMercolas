#pragma once

// STATIC PIECES -- the Synty building kit placed from the tile map.
//
// The chunk mesher (chunk_mesher.hpp) turns every cell into atlas-textured
// box faces: the building you collide with, drawn exactly. This lane dresses
// that geometry with REAL PIECES from the licensed export under
// content/art/lot-3d/static -- wall segments, corners, window walls, door
// frames, cornices, plank and cobble floors, the harbour's water plane,
// crates and barrels, the lamps -- chosen by rules over the integer tile grid
// and placed as StaticInstances (scene.hpp) beside the chunk instances.
//
// THE CHUNK MESH STAYS. A piece is a SKIN: a wall segment stands 0.225 m
// proud of the box face it dresses, a plank lies a centimetre over the floor
// top it covers, a cornice hangs on the wall's top edge. Nothing is removed
// from the chunk mesh, so a build without the licensed files -- every test,
// the docker gate -- draws the placements as nothing at all and the district
// looks exactly as it did before this lane; and a piece that fits no tile
// (a one-wide gap, a roof plane a body can walk on, a hull of oak) simply
// is not placed and the atlas face stands. That is the placeholder rule the
// tests pin: missing file, missing role, placeholder chunk.
//
// THE CATALOGUE IS CONTENT. content/raws/world3d/docks-pieces.json maps a
// role (wall, wall_corner, wall_door, floor_plank...) to a piece (pack, file,
// its module width, how thick it stands, which local side wears the outdoor
// finish, its tint, how far it draws) and a tile MATERIAL to a wall class
// (masonry / timber / none), a tint and a floor role. Swap a piece, retint
// the granite, turn the windows off: an edit to the JSON, never to the code.
// A missing or malformed catalogue is an empty one (no pieces, no placements)
// and the game boots exactly as before.
//
// THE RULES ARE PURE OVER THE TILE BYTES. placeStaticPieces() reads
// sim::TileQuery forms, materials and fluid depths, the catalogue, and the
// baked lamp list; it never reads a clock, an actor or a random number (the
// prop scatter is a hash of the tile coordinates). The same world and the
// same catalogue place the same pieces in the same order, so the placements
// hash into the SceneDescription and twin-run identical -- the tests say so.
//
//   WALL FACES     every exposed side of a WALL cell whose material class is
//                  masonry or timber, grouped into RUNS (same side, level,
//                  material, finish, contiguous). A run of N tiles gets
//                  round(N / 2.5) pieces stretched to fit, so the kit's 2.5 m
//                  brick module reads at 0.8..1.33 of its authored width
//                  rather than being chopped per tile. Outdoor masonry faces
//                  wear the brick side out; indoor faces (the neighbour cell
//                  has a ceiling) and timber walls wear the plaster side.
//   CORNERS        a wall tile with exactly two exposed ADJACENT faces, both
//                  outdoors, is a convex corner: the corner piece (an L,
//                  brick on the outside of the bend) goes there and the two
//                  runs it ends shorten to meet its legs. Other convex ends
//                  extend by the wall's thickness so the two faces' pieces
//                  close the corner between them.
//   WINDOWS        on outdoor masonry runs, every third stretched piece (by
//                  tile hash) is the window-wall piece instead.
//   DOORS          a gap of two or three walkable tiles in a wall line, open
//                  on both sides, one side roofed and one not, with the wall
//                  continuing two tiles past either jamb: the double door
//                  frame, fitted to the gap, brick side to the street.
//   ROOF EDGES     the cornice along every outdoor face of a wall cell with
//                  nothing solid above it -- the roof line. Roof PLANES stay
//                  the chunk's own flat, walkable thatch: the Skyrunners run
//                  them, and a sloped kit roof would be a picture of a wall
//                  the sim lets you stand on.
//   FLOORS         plank, cobble and flagstone pieces over square blocks of
//                  FLOOR cells of one material (3x3 first, then 2x2, greedy
//                  in row order); cells no block fits keep the atlas tile.
//   WATER          the harbour's surface, in square blocks of the topmost
//                  wet cells at one height, the kit's water plane fitted to
//                  the block and floated a hair over the chunk's own water.
//   PROPS          a floor cell against exactly one wall, chosen by a hash
//                  of its coordinates (one in `every`), gets a barrel, a
//                  crate or a sack pushed against that wall. Render-only:
//                  the sim knows nothing of them and a body walks through.
//   LAMPS          every baked lamp: a fire is a brazier on its tile, a
//                  lantern beside a wall is a wall lamp on that wall, a
//                  lantern in the open is a lamp post.
//
// Floats are legal here (render-side); nothing in this file is read by the
// simulation.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/render/lamps.hpp"
#include "granadad/render3d/scene.hpp"

namespace granadad::sim {
class TileQuery;
}

namespace granadad::render3d {

/// What rule a piece serves. THE ORDER IS THE TABLE ORDER: the catalogue's
/// pieces are put into SceneDescription::pieces in this sequence, so a
/// StaticInstance::role byte and its piece index agree on every machine.
enum class PieceRole : std::uint8_t {
    None = 0,
    Wall,
    WallCorner,
    WallWindow,
    WallDoor,
    RoofEdge,
    FloorPlank,
    FloorCobble,
    FloorFlag,
    Water,
    PropBarrel,
    PropCrate,
    PropSack,
    LampWall,
    LampPost,
    Brazier,
};
inline constexpr std::size_t kPieceRoleCount = 16;

/// The JSON key of a role ("wall", "wall_corner", ...), and back. None for
/// an unknown key.
[[nodiscard]] std::string_view pieceRoleName(PieceRole role) noexcept;
[[nodiscard]] PieceRole pieceRoleFromName(std::string_view name) noexcept;

/// What a tile material's WALL cells are dressed as.
enum class WallClass : std::uint8_t {
    /// No piece: the chunk face stands (dirt, steel, cloth, a hull of oak).
    None = 0,
    /// Brick out of doors, plaster indoors.
    Masonry,
    /// Plaster both sides -- a lime-washed timber storey.
    Timber,
};

/// One catalogue row.
struct PieceSpec {
    PieceRole role = PieceRole::None;
    /// "<pack>/<file>" under the static model directory.
    std::string file;
    /// The piece's module along its local X, metres: what one tile-run is
    /// fitted against (2.5 for the Generic kit).
    float width = 2.5F;
    /// Its authored height (walls: scaled to the storey).
    float height = 0.0F;
    /// Walls: how far the piece stands proud of the face it dresses (its
    /// whole local-Z extent); the corner extension too.
    float thickness = 0.225F;
    /// Where the piece's local z = 0 plane sits outward from the face, for
    /// a piece hung on a wall (a cornice, a lamp). Walls: thickness / 2.
    float standoff = 0.0F;
    bool standoffSet = false;
    /// True when the outdoor finish (the brick) is on the piece's local -Z
    /// side; false when it is on +Z (the cornice, the wall lamp).
    bool frontNegZ = true;
    /// Floors and water: the piece's local footprint [minX, minZ, maxX,
    /// maxZ], fitted to a square block of `footprintTiles` tiles.
    float minX = 0.0F, minZ = 0.0F, maxX = 2.5F, maxZ = 2.5F;
    std::int32_t footprintTiles = 3;
    /// Y offset above the surface it stands on.
    float lift = 0.0F;
    /// Radians added to the rule's yaw.
    float yawOffset = 0.0F;
    /// A uniform scale on top of the fit (a crate shrunk a little).
    float scale = 1.0F;
    /// Multiplies the light; 255 = the piece's own colours.
    Rgba8 tint{255, 255, 255, 255};
    /// The cheap cull: pieces farther than this from the eye are not
    /// described. Props draw closer than walls.
    float maxDistance = 64.0F;
};

/// One material row: how a tile material's walls and floors are dressed.
struct MaterialRule {
    std::string material;
    WallClass wallClass = WallClass::None;
    Rgba8 tint{255, 255, 255, 255};
    /// Walls of this material below this band keep the chunk face (a hull
    /// on the piers is oak; a storey over a stone ground floor is timber).
    std::int32_t minBand = 0;
    PieceRole floorRole = PieceRole::None;
    Rgba8 floorTint{255, 255, 255, 255};
};

/// THE CATALOGUE. Loaded from JSON, queried by role and by material.
class StaticCatalogue {
public:
    /// Parses the catalogue text. A malformed document yields an empty
    /// catalogue with `error()` set; unknown roles and materials are skipped
    /// (named in `warnings()`), never fatal.
    [[nodiscard]] static StaticCatalogue fromJson(std::string_view json);
    /// Reads and parses a file; an absent file is an empty catalogue.
    [[nodiscard]] static StaticCatalogue load(const std::filesystem::path& file);

    [[nodiscard]] bool empty() const noexcept { return pieces_.empty(); }
    /// The rows in table order (PieceRole order, only the roles present).
    [[nodiscard]] const std::vector<PieceSpec>& pieces() const noexcept { return pieces_; }
    /// The row for a role, or null when the catalogue has none.
    [[nodiscard]] const PieceSpec* piece(PieceRole role) const noexcept;
    /// The row's index in pieces() (== the StaticInstance::piece value), or
    /// -1 when absent.
    [[nodiscard]] int pieceIndex(PieceRole role) const noexcept;
    /// The material rule for a registry material id (render::materialIds()
    /// order), or null for a material the catalogue does not dress.
    [[nodiscard]] const MaterialRule* material(std::uint16_t materialId) const noexcept;
    [[nodiscard]] const MaterialRule* materialByName(std::string_view id) const noexcept;
    /// The table as the description carries it.
    [[nodiscard]] std::vector<StaticPieceRef> pieceRefs() const;

    /// Below this band nothing is placed (the substrate under the harbour).
    [[nodiscard]] std::int32_t minBand() const noexcept { return minBand_; }
    /// One floor cell in this many (against a wall) gets a prop; 0 = none.
    [[nodiscard]] std::int32_t propEvery() const noexcept { return propEvery_; }
    /// Percent weights of barrel / crate / sack among the props.
    [[nodiscard]] std::int32_t propBarrelPercent() const noexcept { return propBarrel_; }
    [[nodiscard]] std::int32_t propCratePercent() const noexcept { return propCrate_; }
    /// One outdoor wall piece in this many is the window wall; 0 = none.
    [[nodiscard]] std::int32_t windowEvery() const noexcept { return windowEvery_; }

    [[nodiscard]] const std::string& error() const noexcept { return error_; }
    [[nodiscard]] const std::vector<std::string>& warnings() const noexcept { return warnings_; }

    /// FNV-1a over every row: the catalogue's identity, for a report.
    [[nodiscard]] std::uint64_t digest() const noexcept;

private:
    std::vector<PieceSpec> pieces_;
    std::vector<MaterialRule> materials_;
    std::int32_t minBand_ = 0;
    std::int32_t propEvery_ = 0;
    std::int32_t propBarrel_ = 40;
    std::int32_t propCrate_ = 35;
    std::int32_t windowEvery_ = 0;
    std::string error_;
    std::vector<std::string> warnings_;
};

/// One placed piece plus what the colour stage needs: the cell whose light
/// it wears and its facing factor (kFacingX / kFacingY / 1). `instance.tint`
/// holds the UNLIT tint (role x material); the world scene multiplies the
/// light in per lighting bucket.
struct StaticPlacement {
    StaticInstance instance;
    PieceRole role = PieceRole::None;
    std::int32_t lightX = 0;
    std::int32_t lightY = 0;
    std::int32_t lightZ = 0;
    float facing = 1.0F;
};

struct StaticPlacementStats {
    std::size_t byRole[kPieceRoleCount] = {};
    std::size_t wallRuns = 0;
    std::size_t doorGaps = 0;
    [[nodiscard]] std::size_t total() const noexcept {
        std::size_t n = 0;
        for (const std::size_t count : byRole) {
            n += count;
        }
        return n;
    }
};

struct StaticPlacements {
    std::vector<StaticPlacement> placements;
    StaticPlacementStats stats;
};

/// THE RULES, run over the whole world once. Pure: same tiles, same
/// catalogue, same lamps -> same vector, in the same order. An empty
/// catalogue places nothing.
[[nodiscard]] StaticPlacements placeStaticPieces(const sim::TileQuery& tiles,
                                                const StaticCatalogue& catalogue,
                                                const std::vector<render::Lamp>& lamps);

/// The catalogue's own place in the content tree.
[[nodiscard]] std::filesystem::path staticCataloguePath(const std::filesystem::path& contentDir);

/// Whether the cell at (x, y, z) has something built over it -- a floor or a
/// wall one level up -- which is what makes a face beside it an indoor face.
[[nodiscard]] bool cellRoofed(const sim::TileQuery& tiles, std::int32_t x, std::int32_t y,
                              std::int32_t z) noexcept;

}  // namespace granadad::render3d
