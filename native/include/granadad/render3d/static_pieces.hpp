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
//                  has a ceiling) and timber walls wear the plaster side,
//                  tinted per material (a hull of oak is a brown plank wall,
//                  a granite taproom is cream plaster).
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
//                  flat and walkable (the Skyrunners run them; a sloped kit
//                  roof would be a picture of a wall the sim lets you stand
//                  on) and wear the flat fill in thatch; every wall head
//                  with sky above it is capped in the wall's colour.
//   CEILINGS       the flat quad under every floor slab with a room (or the
//                  street, or the water) beneath it, tinted per the floor's
//                  material: a plastered taproom ceiling, the dark underside
//                  of a pier.
//   FLOORS         plank, cobble and flagstone pieces over rectangles of
//                  FLOOR cells of one material (a greedy row-order merge:
//                  the widest run, stacked as deep as it repeats; patterned
//                  pieces take 2..3-tile blocks, planks any rectangle up to
//                  six), then the material's flat fill over every cell the
//                  pattern could not fit, so a dressed floor never shows
//                  the atlas tile; dirt and thatch are the flat fill alone.
//   WATER          the harbour's surface, in rectangles of the topmost wet
//                  cells at one height, the kit's water plane fitted to
//                  each and floated a hair over the chunk's own water.
//   PROPS          a floor cell against exactly one wall, chosen by a hash
//                  of its coordinates (one in `every`), gets a barrel, a
//                  crate or a sack pushed against that wall. Render-only:
//                  the sim knows nothing of them and a body walks through.
//   CHIMNEYS       one roof-plane edge cell in so many that stands over a
//                  wall (by tile hash) carries a chimney stack, turned along
//                  the wall beneath it -- the skyline's punctuation.
//   LAMPS          every baked lamp: a fire is a brazier on its tile, a
//                  lantern beside a wall is a wall lamp on that wall, a
//                  lantern on a doorstep hangs on the jamb beside the door,
//                  a lantern in the open is a lamp post.
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
    /// A flat, uniform quad: the fill under every floor cell a patterned
    /// piece could not cover, and the whole floor of a material that has
    /// no pattern (dirt, thatch) -- tinted per material.
    FloorFill,
    /// The same quad over the top of every wall cell with sky above it (the
    /// parapets and wall heads a roof view sees), in the wall's own colour.
    WallCap,
    /// The quad under every floor slab that has a room, a street or water
    /// beneath it: the ceilings, tinted per the floor's material.
    Ceiling,
    /// A flat plank quad stood upright (`upright` in the catalogue): the
    /// timber walls -- hulls, sheds, the storey over a stone ground floor.
    WallTimber,
    /// A chimney stack on a roof plane, over the wall line beneath it, one
    /// in `chimneyEvery` of the roof's edge cells by tile hash.
    Chimney,
};
inline constexpr std::size_t kPieceRoleCount = 21;

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
    /// Boards both sides: the upright plank quad (or, without one in the
    /// catalogue, the kit wall's plaster side).
    Timber,
    /// The kit wall's plaster side both ways, tinted: a stall's canvas, a
    /// leather awning.
    Canvas,
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
    /// maxZ], fitted to a block of whole tiles between minBlock and
    /// maxBlock on a side (a patterned piece wants 2..3 so its stones stay
    /// stones; a plank or a flat fill takes any rectangle).
    float minX = 0.0F, minZ = 0.0F, maxX = 2.5F, maxZ = 2.5F;
    std::int32_t minBlock = 1;
    std::int32_t maxBlock = 3;
    /// A downward-facing quad laid upward: the piece is mirrored in Y (the
    /// adapter draws a mirrored piece without back-face culling).
    bool flipY = false;
    /// A flat quad (extent in its local XZ) stood on edge as a wall: its
    /// local Z becomes the height, its +Y normal the outward finish.
    bool upright = false;
    /// Drawn at its own colours whatever the light where it stands: a lamp
    /// or a brazier is its own light and should not go black at night.
    bool selfLit = false;
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
    /// Masonry that shows the kit's plaster side to the street (a rendered
    /// stone building) rather than its brick; the corner piece, whose
    /// outside is brick, is then not used on it.
    bool plasterOut = false;
    /// The outward finish's tint (the brick, or a timber wall's plaster).
    Rgba8 tint{255, 255, 255, 255};
    /// The plaster's tint on a masonry wall's indoor face.
    Rgba8 insideTint{255, 255, 255, 255};
    /// Walls of this material below this band keep the chunk face.
    std::int32_t minBand = 0;
    /// The floor piece its FLOOR cells wear, and the flat fill laid over
    /// the cells the floor piece could not fit (None: the atlas tile).
    PieceRole floorRole = PieceRole::None;
    Rgba8 floorTint{255, 255, 255, 255};
    PieceRole fillRole = PieceRole::None;
    Rgba8 fillTint{255, 255, 255, 255};
    /// The wall cap's tint (default: the outward tint) and the tint of a
    /// ceiling under a floor of this material (default: the catalogue's).
    Rgba8 topTint{255, 255, 255, 255};
    bool topTintSet = false;
    Rgba8 ceilingTint{255, 255, 255, 255};
    bool ceilingTintSet = false;
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
    /// One roof-edge cell over a wall in this many carries a chimney; 0 = none.
    [[nodiscard]] std::int32_t chimneyEvery() const noexcept { return chimneyEvery_; }

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
    std::int32_t chimneyEvery_ = 0;
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
    /// The cells the piece spans, inclusive: its light is the average over
    /// them, so a stretched wall segment under a lamp is lit as its middle
    /// and not as one end.
    std::int32_t lightX = 0;
    std::int32_t lightY = 0;
    std::int32_t lightX2 = 0;
    std::int32_t lightY2 = 0;
    std::int32_t lightZ = 0;
    float facing = 1.0F;
    /// A run piece: lit at (lightX, lightY) on its a0 end and (lightX2,
    /// lightY2) on its a1 end, blended between. `flipped` says the piece's
    /// local X runs from the a1 end, so the two swap into tint / tint2.
    bool gradient = false;
    bool flipped = false;
    /// The piece keeps its catalogue tint through the relight.
    bool selfLit = false;
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
