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
//   LAMPS          every baked lamp: a fire is a brazier on its tile with an
//                  ember tray and a flame in its cage, a lantern beside a
//                  wall hangs from a bracket arm on that wall, a lantern on
//                  a doorstep hangs beside the door, a lantern in the open
//                  is a lamp post -- or, under a roof, a lantern on a chain
//                  from the ceiling. Every flame is a pair of crossed warm
//                  quads drawn as their own light.
//   UNDERSIDES     the plaster quad under every WALL cell that stands over
//                  an open cell (the storey partition over a taproom, the
//                  facade over a doorway) in the room's own ceiling tint, so
//                  no atlas face shows from below.
//   LIPS           the side of every floor slab that faces open air (a pier
//                  deck's edge, a hole in a deck): the plank quad on timber,
//                  the plaster quad tinted on stone, the slab's own height.
//   DOOR INSIDES   behind a door frame on a rendered building the frame's
//                  brick would face the room: the plaster quad stands behind
//                  the header and both jambs. A door leaf hangs open on each
//                  jamb. A gate wider than a door gets a post at each jamb
//                  and a beam across.
//   FURNITURE      round every indoor lantern: tables with benches and mugs
//                  on floor cells two from a wall, shelves with bottles on
//                  indoor masonry faces, barrel racks against walls; a
//                  free-standing two-cell masonry block in a roofed room is
//                  a hearth and wears the fireplace; joists cross every
//                  room's ceiling on the odd grid lines; a timber POST (a
//                  lone 1x1 cell, or one whose single wall neighbour is of
//                  another kind) is a square pillar the cell's size, plaster
//                  indoors and timber out, and beside the water a tarred
//                  boarded core with one pile driven through it; out of
//                  doors with a job (within two cells of a door gap, or one
//                  of a pair that carries a rail) it is the strapped timber
//                  post fitted to the cell instead of the pillar.
//   ROOFS          a building top with sky over it (a floor of a roofing
//                  material, or any floor with a room under it, or one over
//                  a wall at such a floor's side) wears the roof finish
//                  whatever its tile says: the slate-tinted flag piece at
//                  its own module over a tar-dark fill, loose tiles by hash,
//                  an upstand along every edge cell (brick over masonry,
//                  boards over timber), chimneys over masonry walls only,
//                  the odd crate or barrel at the edge. FLAT, always: every
//                  roof cell in the district is standable and the
//                  Skyrunners route across them.
//   HARBOUR        a timber wall with water beside it is a hull: its plank
//                  quad stands plumb (a lean is a knob), tarred, with a
//                  gunwale beam along an open top; a masonry face at the
//                  harbour band with water beside it is a quay wall and
//                  wears the stone piece from the coping down into the
//                  water, no cornice; rowboats moor along quay edges by
//                  hash; a pier head carries a crane.
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
    // --- the second iteration (the critic's fourteen) ---------------------
    /// The side of a timber floor slab facing open air: the plank quad, the
    /// slab's own height.
    LipPlank,
    /// The same on a stone floor: the plaster quad, tinted.
    LipStone,
    /// The plaster quad behind a door frame's header and jambs on a
    /// rendered (plaster-out) building, so the room never sees the frame's
    /// brick side.
    DoorInside,
    /// A door leaf standing open against the reveal, hinged on the jamb.
    DoorLeaf,
    /// A post at each jamb of a gate wider than a door.
    GatePost,
    /// The warm quad inside every lamp: a pair crossed, its own light.
    Flame,
    /// The ember tray in a fire's cage.
    Ember,
    /// The bracket arm a wall lantern hangs from.
    LampBracket,
    /// The chain a ceiling lantern hangs from.
    LampChain,
    /// A beam under a room's ceiling, on the odd grid lines.
    Joist,
    Table,
    Bench,
    Mug,
    Bottle,
    Shelf,
    BarrelRack,
    Fireplace,
    /// A timber post (a lone 1x1 cell, or one against a wall of another
    /// kind): a square pillar the cell's size, plaster indoors, timber out.
    Pillar,
    /// The one pile driven through a timber post beside the water.
    Post,
    /// The upstand along a roof edge cell (brick over masonry; the plank
    /// quad stands in for it over timber).
    Parapet,
    /// Loose tiles scattered on a roof plane.
    RoofTile,
    Rowboat,
    Crane,
    /// The beam along the open top of a hull wall.
    Gunwale,
    /// A hung window on a timber storey.
    WindowTimber,
    /// A mooring line down a hull's boards from its gunwale.
    Rope,
    /// A hull's boards: the plank quad stood across, leaning outward.
    Hull,
    /// A plaster face: the thin plaster quad, one-sided, so a corner it
    /// extends past shows no brick back and no brick end (the kit wall is
    /// brick on its other side and its ends).
    WallPlaster,
    /// A stool beside an indoor pillar (the sim's own table): somewhere to
    /// sit at it.
    Stool,
    /// The stone face of a quay wall at the harbour band, coping to water.
    QuayWall,
    /// The paved deck a roof keeps only where a stair or a ramp arrives on
    /// it: the flagstone piece at its own module. Every other building top
    /// is lead -- the dark fill with battens.
    RoofFlag,
    /// The batten seams of a lead roof: the kit beam laid thin along every
    /// module line of a roof plane, in the roof's own dark.
    RoofBatten,
    /// A shop sign hung on a timber post beside a door gap: the job that
    /// makes a lone post a signpost.
    ShopSign,
    /// The one-wide strip a cobbled street leaves along a frontage, where
    /// no 2 x 2 block fits: the flag piece at a third, its stones as setts.
    FloorStrip,
    /// The rail between a PAIR of lone timber posts two cells apart on a
    /// street: a hitching rail at hip height, the job that makes two
    /// metre-square piers a rail and not a gate to nowhere.
    PostRail,
    /// The kit's strapped timber post, worn two ways.
    ///
    /// A JAMB: one on each side of a door opening, on the wall cell hard
    /// against it (render3d/door_jambs.hpp names the cell), at the piece's
    /// OWN section -- a quarter of a metre -- from the ground to the head,
    /// straddling the frontage's finish, with a beam across the two heads.
    /// The chunk mesher leaves those cells' boxes out under the head, which
    /// is what lets the beam be thin. Exactly two per door; a gate has none
    /// (it keeps its own GatePost and beam).
    ///
    /// STREET FURNITURE: a lone timber cell out of doors and clear of the
    /// water with a job -- the hitching post within two cells of a door,
    /// one half of a pair that carries a rail -- wears the same post FITTED
    /// TO ITS CELL, because that cell's box is still standing round it. A
    /// lone pier with no door and no partner stays the pillar.
    DoorPost,
    /// KIT BUILD. A THING ON A TILE: an item of the player's registry
    /// (sim/items.hpp) lying where the room says it lies -- a dropped
    /// knife, the cudgel under the bar, the four strongboxes, the snug's
    /// bale -- as the Synty static the catalogue's `items` table names for
    /// that item id, one variant per id. NOT placed by placeStaticPieces
    /// (which is pure over the tile bytes): the ground list is SIM STATE,
    /// so the instances are written per frame by groundItemInstances()
    /// off the room's hashed list, and the renderer never removes one --
    /// TAKE does, in the sim, and the next frame simply has fewer.
    Item,
};
inline constexpr std::size_t kPieceRoleCount = 58;

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

/// One catalogue row. A row that lists several `files` becomes one spec per
/// file, `variant` 0..n-1, sharing every other field: the rule picks a
/// variant by tile hash.
struct PieceSpec {
    PieceRole role = PieceRole::None;
    std::uint8_t variant = 0;
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
    /// An upright quad stood the other way: its local X becomes the height
    /// and its local Z runs along the face, so a plank quad's boards lie
    /// across (a hull's strakes). Implies upright.
    bool across = false;
    /// Drawn at its own colours whatever the light where it stands: a lamp
    /// or a brazier is its own light and should not go black at night.
    bool selfLit = false;
    /// Y offset above the surface it stands on.
    float lift = 0.0F;
    /// Radians added to the rule's yaw.
    float yawOffset = 0.0F;
    /// KIT BUILD. Radians about the piece's own X (StaticInstance::pitch):
    /// what lays a sword, authored point-up, flat on the boards. Read by
    /// the item path only.
    float pitch = 0.0F;
    /// A uniform scale on top of the fit (a crate shrunk a little).
    float scale = 1.0F;
    /// Multiplies the light; 255 = the piece's own colours.
    Rgba8 tint{255, 255, 255, 255};
    /// The cheap cull: pieces farther than this from the eye are not
    /// described. Props draw closer than walls.
    float maxDistance = 64.0F;
    /// A door frame's opening in its own module: the jamb width on each
    /// side (`cutX`, from either end) and the opening's height (`cutY`).
    /// What the inside plaster is cut around.
    float cutX = 0.0F;
    float cutY = 0.0F;
    /// A flat quad drawn on both sides (its scale is mirrored, which the
    /// adapter draws without back-face culling).
    bool twoSided = false;
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
    /// The quad on the side of this material's floor slabs where they face
    /// open air (LipPlank / LipStone), and its tint; None: the atlas side.
    PieceRole lipRole = PieceRole::None;
    Rgba8 lipTint{255, 255, 255, 255};
    /// A roofing material: its floor planes with sky above are roofs and
    /// get the roof rules (upstands, loose tiles, chimneys, clutter).
    bool roof = false;
};

/// The rule knobs: one in how many. 0 turns a rule off.
struct RuleKnobs {
    /// One outdoor wall piece in this many, along a run, is a window: the
    /// rhythm is by piece index, never a coin flip.
    std::int32_t windowEvery = 0;
    /// One roof-edge cell over a masonry wall in this many carries a chimney.
    std::int32_t chimneyEvery = 0;
    /// One floor cell against exactly one wall in this many gets a prop.
    std::int32_t propEvery = 0;
    std::int32_t propBarrelPercent = 40;
    std::int32_t propCratePercent = 35;
    /// The lattice pitch of the tables round an indoor lantern: every
    /// this-many cells both ways (2 = every other), where a 3 x 3 of clear
    /// floor allows.
    std::int32_t tableEvery = 0;
    /// One indoor masonry face cell near an indoor lantern in this many
    /// gets a shelf.
    std::int32_t shelfEvery = 0;
    /// One roof cell in this many gets loose tiles; one roof-edge cell in
    /// this many gets a crate or a barrel.
    std::int32_t roofTileEvery = 0;
    std::int32_t roofPropEvery = 0;
    /// One quay-edge cell over open water in this many moors a rowboat.
    std::int32_t boatEvery = 0;
    /// A hull's plank quad leans outward from the waterline by this many
    /// degrees; its tarred tint.
    float hullFlareDegrees = 0.0F;
    Rgba8 hullTint{255, 255, 255, 255};
    /// The warm tints of a lantern's flame, a fire's flame, and a lit
    /// window pane at night.
    Rgba8 lanternFlame{255, 214, 150, 255};
    Rgba8 fireFlame{255, 150, 64, 255};
    Rgba8 litPane{255, 196, 120, 255};
    /// The roof finish: the tint of the RoofFlag piece and of the flat fill
    /// under it, on every building top with sky over it.
    Rgba8 roofTint{255, 255, 255, 255};
    Rgba8 roofFillTint{255, 255, 255, 255};
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
    /// The row for a role (its `variant`, 0 by default), or null when the
    /// catalogue has none.
    [[nodiscard]] const PieceSpec* piece(PieceRole role, std::uint8_t variant = 0) const noexcept;
    /// The row's index in pieces() (== the StaticInstance::piece value), or
    /// -1 when absent.
    [[nodiscard]] int pieceIndex(PieceRole role, std::uint8_t variant = 0) const noexcept;
    /// How many variants a role has (0 when absent).
    [[nodiscard]] std::uint8_t variantCount(PieceRole role) const noexcept;
    /// KIT BUILD. The Item row for a registry item id, or null when the
    /// `items` table does not dress it (the placeholder rule: nothing is
    /// drawn, the sim's list is untouched).
    [[nodiscard]] const PieceSpec* itemPiece(std::string_view itemId) const noexcept;
    /// The item ids the table dresses, in the variant order the rows carry.
    [[nodiscard]] const std::vector<std::string>& itemIds() const noexcept { return itemIds_; }
    /// The material rule for a registry material id (render::materialIds()
    /// order), or null for a material the catalogue does not dress.
    [[nodiscard]] const MaterialRule* material(std::uint16_t materialId) const noexcept;
    [[nodiscard]] const MaterialRule* materialByName(std::string_view id) const noexcept;
    /// The table as the description carries it.
    [[nodiscard]] std::vector<StaticPieceRef> pieceRefs() const;

    /// Below this band nothing is placed (the substrate under the harbour).
    [[nodiscard]] std::int32_t minBand() const noexcept { return minBand_; }
    /// The rule knobs (see RuleKnobs).
    [[nodiscard]] const RuleKnobs& knobs() const noexcept { return knobs_; }
    /// One floor cell in this many (against a wall) gets a prop; 0 = none.
    [[nodiscard]] std::int32_t propEvery() const noexcept { return knobs_.propEvery; }
    /// One outdoor wall piece in this many is the window wall; 0 = none.
    [[nodiscard]] std::int32_t windowEvery() const noexcept { return knobs_.windowEvery; }
    /// One roof-edge cell over a masonry wall in this many carries a
    /// chimney; 0 = none.
    [[nodiscard]] std::int32_t chimneyEvery() const noexcept { return knobs_.chimneyEvery; }

    [[nodiscard]] const std::string& error() const noexcept { return error_; }
    [[nodiscard]] const std::vector<std::string>& warnings() const noexcept { return warnings_; }

    /// FNV-1a over every row: the catalogue's identity, for a report.
    [[nodiscard]] std::uint64_t digest() const noexcept;

private:
    std::vector<PieceSpec> pieces_;
    std::vector<MaterialRule> materials_;
    /// KIT BUILD. itemIds_[v] is the item id of the Item row with variant v.
    std::vector<std::string> itemIds_;
    std::int32_t minBand_ = 0;
    RuleKnobs knobs_;
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
    /// local X runs from the a1 end, so the two swap into tint / tint2;
    /// `alongZ` says the blend runs along the piece's local Z instead
    /// (a quad stood across).
    bool gradient = false;
    bool flipped = false;
    bool alongZ = false;
    /// Each end of a run piece is lit AT THE CUT: the light between the
    /// two cell centres the cut falls between, blended by where it falls
    /// (endAT / endBT, 0 = at the first cell's centre, 1 = at the second's),
    /// so two pieces meeting at a cut share one value and the light runs on
    /// across the seam. (endAX, endAY) pairs with (lightX, lightY); (endBX,
    /// endBY) with (lightX2, lightY2). The same cell twice, weight 0, at a
    /// run's own end.
    std::int32_t endAX = 0, endAY = 0, endBX = 0, endBY = 0;
    float endAT = 0.0F, endBT = 0.0F;
    /// A block (a floor, a ceiling): lit at its four corner POINTS, each
    /// the average of the cells that meet there, blended bilinearly by the
    /// adapter -- so two blocks that share a corner share its light.
    bool bilinear = false;
    /// A window: the cell behind it, whose light says whether the room is
    /// lit (the pane goes warm at night). `homely` says the room behind is
    /// a roofed floor and the tile hash keeps a candle in it: the pane goes
    /// warm at night whether or not a lamp reaches the cell, two windows
    /// in three, so the ward is not dead after dark.
    bool hasInside = false;
    bool homely = false;
    std::int32_t insideX = 0, insideY = 0, insideZ = 0;
    /// The piece keeps its catalogue tint through the relight.
    bool selfLit = false;
    /// How far the piece reaches from its origin, in tiles: the frustum
    /// cull's margin.
    float radius = 1.0F;
    /// The adapter's draw mode (scene.hpp): plain, a halo, or shaded by
    /// the mesh's own normals.
    std::uint8_t mode = 0;
    /// A billboard (a flame's halo): one quad turned to face the eye every
    /// frame by the world scene, about `anchor` (its centre) -- so a halo
    /// is never seen edge-on as a bright bar. The instance's yaw and
    /// position are rewritten at refresh; the placement keeps the centre.
    bool billboard = false;
    Vec3 anchor;
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
