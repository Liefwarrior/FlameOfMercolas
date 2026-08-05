// THE ROSTER: who is in the Docks, where they sleep, and what they do all day.
//
// EVERY COORDINATE IN THIS FILE IS AUTHORED. They are the `script_anchor`
// markers of content/maps/src/docks_surface.tmx, read out of the Tiled source
// and carried across with the world's own offset applied: a local tile (lx, ly)
// at local level lz lands at world (lx + 32, ly + 32, lz + 8), because the baked
// world carries a one-chunk VOID border. Nothing here was invented to make a
// number come out right, and nothing here is a guess about where a warehouse
// is -- the map says where the warehouse is.
//
// The one liberty taken is SNAPPING. An anchor is a marker and a marker can sit
// on a counter, inside a rack, or one tile inside a wall; a body has to stand
// somewhere real. So every anchor is snapped outward to the nearest standable
// cell in a fixed spiral, and a case asserts that every one of them found
// ground within a short radius -- which is also how a future edit to the map
// that seals a shed shows up as a red build instead of as a shopkeeper standing
// in the harbour.
//
// WHAT IS DELIBERATELY ABSENT: the Gilded Gull. K03 is the Tavern system's
// building and its seventeen people are the Tavern's. Nothing here spawns a
// body in it, homes a body in it, or sends a body's wander target into it. The
// ward's own drinkers keep the other two houses -- K04 The Bilge and K05 The
// Lantern Room -- which the Tavern has never staffed, so the district gains two
// full taprooms rather than one contested one.

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <vector>

#include "granadad/sim/docks.hpp"
#include "granadad/sim/rng.hpp"
#include "granadad/sim/ward_actors.hpp"

namespace granadad::sim {

namespace {

/// A place in the district, already in world tiles.
struct Anchor {
    std::int32_t x;
    std::int32_t y;
    std::int32_t band;
};

// --- the establishments -----------------------------------------------------
//
// Twenty-eight of the thirty-five K-sites, at their own authored anchors. K03
// is the Tavern's; K13 The Drowned Hold is condemned and officially empty (its
// only occupants are the ones the case is about); K16 the shrine is tended and
// unstaffed; K30-K33 are hulls and a wreck, crewed below rather than staffed.

struct Establishment {
    const char* name;
    Anchor post;
    WardType keeper;
    /// How many hands the site keeps, and what they are.
    WardType hand;
    std::int32_t hands;
    /// A guard on the door, when the site's authored guard-post marker exists.
    Anchor guard;
    bool hasGuard;
};

constexpr Anchor kNoAnchor{0, 0, 0};

constexpr Establishment kEstablishments[] = {
    // The harbormaster's, and the district's truth-database.
    {"weighhouse", {96, 73, 19}, WardType::Shopkeeper, WardType::Serf, 5, kNoAnchor, false},
    {"impound", {95, 88, 19}, WardType::MilitiaWatch, WardType::AnimalKeeper, 1, kNoAnchor, false},
    // K04 and K05: the two houses the ward drinks in. The Gull is the Tavern's.
    {"the-bilge", {138, 70, 19}, WardType::Shopkeeper, WardType::Serf, 3, kNoAnchor, false},
    {"lantern-room", {96, 103, 19}, WardType::Shopkeeper, WardType::Serf, 3, kNoAnchor, false},
    {"harls-yard", {173, 73, 19}, WardType::Shopkeeper, WardType::Serf, 6, kNoAnchor, false},
    {"ropewalk", {68, 117, 19}, WardType::Shopkeeper, WardType::Serf, 6, kNoAnchor, false},
    {"branns", {59, 101, 19}, WardType::Shopkeeper, WardType::Serf, 2, {60, 97, 19}, true},
    {"pitchfield", {42, 78, 19}, WardType::Shopkeeper, WardType::Serf, 3, kNoAnchor, false},
    {"dawnstalls", {78, 73, 19}, WardType::Shopkeeper, WardType::Fisher, 8, kNoAnchor, false},
    {"salt-row", {77, 83, 19}, WardType::Shopkeeper, WardType::Serf, 6, kNoAnchor, false},
    {"kings-bond", {122, 72, 19}, WardType::MilitiaWatch, WardType::Serf, 5, {122, 65, 19}, true},
    {"wrackhouse", {200, 69, 19}, WardType::Shopkeeper, WardType::Serf, 2, {200, 65, 19}, true},
    {"fenners", {157, 88, 19}, WardType::Shopkeeper, WardType::Serf, 1, {158, 83, 19}, true},
    {"mission", {120, 103, 19}, WardType::PriestOfTheFlame, WardType::DiscipleOfTheFlame, 4,
     kNoAnchor, false},
    {"bathhouse", {139, 105, 19}, WardType::Shopkeeper, WardType::Serf, 2, kNoAnchor, false},
    {"the-rows", {141, 87, 19}, WardType::Shopkeeper, WardType::Serf, 1, kNoAnchor, false},
    {"merles-boats", {120, 50, 19}, WardType::Shopkeeper, WardType::Sailor, 2, kNoAnchor, false},
    {"netmenders", {77, 66, 19}, WardType::Serf, WardType::Serf, 5, kNoAnchor, false},
    {"coopers", {79, 102, 19}, WardType::Shopkeeper, WardType::Serf, 4, {79, 97, 19}, true},
    // K24 The Eel-Pots: night food stalls, and the reason the Tarwalk is not
    // dark at midnight. Its hands work the Scavenge window on purpose.
    {"eel-pots", {129, 63, 19}, WardType::Shopkeeper, WardType::Serf, 3, kNoAnchor, false},
    {"kennel-row", {199, 86, 19}, WardType::AnimalKeeper, WardType::Serf, 2, kNoAnchor, false},
    {"sailmaker", {43, 105, 19}, WardType::Shopkeeper, WardType::Serf, 3, {44, 101, 19}, true},
    {"hardtack-oven", {67, 106, 19}, WardType::Shopkeeper, WardType::Serf, 3, {67, 101, 19}, true},
    {"slop-chest", {165, 94, 19}, WardType::Shopkeeper, WardType::Serf, 1, {165, 89, 19}, true},
    {"long-store", {120, 119, 19}, WardType::Shopkeeper, WardType::Serf, 7, kNoAnchor, false},
    {"bank", {186, 85, 19}, WardType::Shopkeeper, WardType::MilitiaWatch, 2, kNoAnchor, false},
    {"timber-yard", {185, 73, 19}, WardType::Shopkeeper, WardType::Serf, 3, kNoAnchor, false},
    {"west-garden", {51, 103, 19}, WardType::Serf, WardType::Serf, 2, kNoAnchor, false},
};

// --- the Watch --------------------------------------------------------------
//
// Two garrisons, as DOCKS-GAZETTEER section 2.4 pairs them: K21 at the head of
// Saltgate Rise and K34 at its foot. The gazetteer's own dossier says the day
// presence is four to six with a two-man patrol, and that the night collapses
// to the post -- which this build deliberately does NOT do, because the owner
// played it and asked for guards at night. What it does instead is roster a
// THIRD of the garrison onto the night beat and send the rest home: the street
// is not empty and it is not as busy as it is at noon, which is the reading the
// gazetteer and the complaint can both live with.

constexpr Anchor kWatchPostHead{99, 154, 21};
constexpr Anchor kWatchPostFoot{138, 117, 19};

/// The four authored patrol posts, and the beats hung off them.
constexpr Anchor kPatrolPosts[] = {
    {62, 62, 19},    // tarwalk west
    {132, 62, 19},   // tarwalk mid
    {107, 66, 19},   // the Rise's foot
    {107, 150, 21},  // the Rise's head
};

/// The Tarwalk beat: five authored waypoints along the working spine.
constexpr Anchor kTarwalkRoute[] = {
    {52, 65, 19}, {77, 65, 19}, {102, 65, 19}, {128, 65, 19}, {154, 65, 19},
};
/// The quay beat, one band down the harbour edge.
constexpr Anchor kQuayRoute[] = {
    {46, 62, 19}, {60, 62, 19}, {74, 62, 19}, {88, 62, 19}, {100, 62, 19},
};
/// The Ropewynd beat, which is the one that matters at two in the morning: it
/// runs the length of the lower-middle road past every compound gate.
constexpr Anchor kRopewyndRoute[] = {
    {42, 97, 19},  {67, 97, 19},  {87, 97, 19},  {110, 97, 19}, {132, 97, 19},
    {154, 97, 19}, {161, 90, 19}, {176, 90, 19}, {177, 97, 19},
};
/// The Rise, foot to head, crossing all three bands.
constexpr Anchor kSaltgateRoute[] = {
    {107, 66, 19}, {107, 77, 19}, {107, 104, 19}, {107, 122, 19}, {107, 150, 21},
};

// --- the carters' round -----------------------------------------------------

constexpr Anchor kCarterRoute[] = {
    {82, 62, 19},   // the carter stand
    {157, 65, 19},  // the Gull's hitch, on the street outside it
    {134, 65, 19},  // the Bilge's hitch
    {141, 83, 19},  // the Rows' hitch
    {57, 62, 19},   // dock load, west
    {132, 64, 19},  // dock load, east
};

// --- the commons ------------------------------------------------------------
//
// WHERE SOMEBODY WITH NO WORK TODAY STANDS. This is the difference between a
// district that has poor people in it and a district whose poor people are all
// indoors: a wastrel's anchor is not its bed, it is the kerb.
//
// Every one of these is an authored street cell -- patrol waypoints, market
// stalls, the Mission's own kerb, the quay muster, the well on Gallows Row --
// so a loiterer is loitering somewhere the map says is a street.
constexpr Anchor kCommons[] = {
    {52, 65, 19},   {77, 65, 19},   {102, 65, 19}, {128, 65, 19},  {154, 65, 19},
    {77, 62, 19},   {107, 66, 19},  {42, 97, 19},  {67, 97, 19},   {87, 97, 19},
    {110, 97, 19},  {132, 97, 19},  {154, 97, 19}, {177, 97, 19},  {121, 82, 19},
    {136, 82, 19},  {151, 82, 19},  {113, 105, 19}, {107, 77, 19}, {107, 104, 19},
    {107, 122, 19}, {133, 154, 21}, {105, 151, 21}, {51, 103, 19}, {209, 112, 19},
    {169, 112, 19}, {75, 139, 20},  {143, 136, 20},
};

// --- the bins ---------------------------------------------------------------
//
// Sixteen authored garbage bins across the three bands. They are the urchins'
// round and the mice's dens, which is the whole of the ward's bottom rung: a
// child works them at night and a mouse works them all day.

constexpr Anchor kBins[] = {
    {150, 65, 19},  {140, 65, 19},  {98, 97, 19},   {86, 76, 19},
    {84, 84, 19},   {123, 97, 19},  {69, 101, 19},  {121, 64, 19},
    {134, 130, 20}, {109, 137, 20}, {202, 134, 20}, {186, 142, 20},
    {53, 153, 21},  {107, 152, 21}, {131, 154, 21}, {166, 157, 21},
};

// --- the market, and the fishing ground -------------------------------------

constexpr Anchor kMarketStalls[] = {
    {121, 82, 19}, {123, 82, 19}, {136, 82, 19},
    {138, 82, 19}, {151, 82, 19}, {153, 82, 19},
};

constexpr Anchor kFisherStands[] = {
    {107, 41, 19}, {107, 49, 19}, {107, 55, 19}, {107, 44, 19}, {109, 52, 19},
};

/// The three deep-water berths and the three hulls moored at them. A sailor
/// sleeps aboard, which is why the hulls are homes as well as posts.
constexpr Anchor kBerths[] = {{53, 59, 19}, {73, 60, 19}, {93, 59, 19}};
constexpr Anchor kHulls[] = {{98, 52, 19}, {96, 45, 19}, {95, 36, 19}};

// --- where the ward sleeps --------------------------------------------------

/// One dwelling site: an authored anchor, and how many households it holds.
///
/// A MANSION IS NOT ONE HOME. DOCKS-GAZETTEER section 2.5's compound ruling is
/// that a "dozen homes" is a dozen dwelling UNITS inside one walled compound,
/// and the owning family's mansion houses six or seven servant households
/// besides its own. So a site is expanded into that many home CELLS by walking
/// outward from its anchor over standable floor, and one household is formed
/// per cell.
struct Dwelling {
    Anchor at;
    std::int32_t households;
    /// What the people who live here mostly are.
    WardType flavour;
};

constexpr Dwelling kDwellings[] = {
    // C1 -- the west compound, mid-slope and upper.
    {{51, 138, 20}, 11, WardType::Shopkeeper},
    {{69, 132, 20}, 4, WardType::Serf},   {{81, 132, 20}, 4, WardType::Serf},
    {{93, 132, 20}, 4, WardType::Serf},   {{69, 144, 20}, 4, WardType::Serf},
    {{81, 144, 20}, 4, WardType::Serf},   {{93, 144, 20}, 4, WardType::Serf},
    {{69, 132, 21}, 4, WardType::Serf},   {{81, 132, 21}, 4, WardType::Serf},
    {{93, 132, 21}, 4, WardType::Serf},   {{69, 144, 21}, 4, WardType::Serf},
    {{81, 144, 21}, 4, WardType::Serf},   {{93, 144, 21}, 4, WardType::Wastrel},
    // C2 -- the east compound, quayside, with a roof slum on top of it.
    {{153, 111, 19}, 11, WardType::Shopkeeper},
    {{163, 102, 19}, 4, WardType::Serf},  {{179, 102, 19}, 4, WardType::Serf},
    {{169, 121, 19}, 4, WardType::Serf},  {{189, 102, 19}, 4, WardType::Serf},
    {{189, 112, 19}, 4, WardType::Serf},  {{189, 121, 19}, 4, WardType::Serf},
    {{189, 102, 20}, 4, WardType::Serf},  {{189, 112, 20}, 4, WardType::Wastrel},
    {{189, 121, 20}, 4, WardType::Wastrel},
    {{187, 102, 21}, 2, WardType::Urchin}, {{190, 111, 21}, 2, WardType::Thief},
    {{187, 120, 21}, 2, WardType::Urchin},
    // C3 -- the south compound.
    {{121, 140, 20}, 11, WardType::Shopkeeper},
    {{159, 136, 20}, 4, WardType::Serf},  {{167, 136, 20}, 4, WardType::Serf},
    {{133, 143, 20}, 4, WardType::Serf},  {{144, 143, 20}, 4, WardType::Serf},
    {{155, 143, 20}, 4, WardType::Serf},  {{166, 143, 20}, 4, WardType::Serf},
    {{133, 143, 21}, 4, WardType::Wastrel}, {{144, 143, 21}, 4, WardType::Serf},
    {{155, 143, 21}, 4, WardType::Wastrel}, {{166, 143, 21}, 4, WardType::Serf},
    {{134, 144, 22}, 2, WardType::Urchin}, {{160, 143, 22}, 2, WardType::Thief},
    // C4 -- the Gullet. The poorest ground in the district, and the one the
    // Watch does not go into.
    // THE GULLET'S OWN TRADE, AND WHY IT IS ON THE GROUND. Canon puts the
    // ward's burglars on the roof-slum deck (DOCKS-GAZETTEER section 2.5:
    // rooftops are the burglar's highway precisely because they are socially
    // unseemly) -- but a ward actor has no climb verb, so a body homed up there
    // could never walk to its own bed. The roof huts are therefore
    // UNPOPULATED, said plainly, and the Gullet's thieves keep two of its
    // ground-level condos instead. Give an actor a mantle and they move back up.
    {{200, 104, 19}, 4, WardType::Wastrel}, {{200, 118, 19}, 4, WardType::Wastrel},
    {{205, 102, 19}, 4, WardType::Thief},   {{217, 102, 19}, 4, WardType::Serf},
    {{219, 116, 19}, 4, WardType::Thief},   {{219, 116, 20}, 4, WardType::Wastrel},
    {{209, 121, 19}, 4, WardType::Urchin},
    {{217, 111, 21}, 2, WardType::Thief},  {{220, 115, 21}, 2, WardType::Urchin},
    {{217, 120, 21}, 2, WardType::Wastrel}, {{221, 124, 21}, 2, WardType::Urchin},
    {{221, 120, 21}, 1, WardType::Thief},  // the Skyrunner's Roost, unmarked
    // The forty-five hovels, in their authored order.
    {{116, 87, 19}, 1, WardType::Wastrel},  {{121, 86, 19}, 1, WardType::Serf},
    {{127, 88, 19}, 1, WardType::Serf},     {{42, 125, 19}, 1, WardType::Serf},
    {{54, 125, 19}, 1, WardType::Serf},     {{216, 95, 19}, 1, WardType::Wastrel},
    {{221, 94, 19}, 1, WardType::Wastrel},  {{178, 135, 20}, 1, WardType::Serf},
    {{185, 136, 20}, 1, WardType::Serf},    {{193, 135, 20}, 1, WardType::Serf},
    {{202, 137, 20}, 1, WardType::Wastrel}, {{212, 135, 20}, 1, WardType::Serf},
    {{218, 140, 20}, 1, WardType::Wastrel}, {{44, 149, 21}, 1, WardType::Serf},
    {{51, 149, 21}, 1, WardType::Serf},     {{60, 149, 21}, 1, WardType::Wastrel},
    {{68, 149, 21}, 1, WardType::Serf},     {{78, 149, 21}, 1, WardType::Serf},
    {{86, 149, 21}, 1, WardType::Urchin},   {{118, 149, 21}, 1, WardType::Serf},
    {{126, 149, 21}, 1, WardType::Serf},    {{142, 149, 21}, 1, WardType::Wastrel},
    {{152, 149, 21}, 1, WardType::Serf},    {{162, 149, 21}, 1, WardType::Serf},
    {{172, 149, 21}, 1, WardType::Thief},   {{182, 149, 21}, 1, WardType::Serf},
    {{200, 149, 21}, 1, WardType::Serf},    {{210, 149, 21}, 1, WardType::Wastrel},
    {{42, 156, 21}, 1, WardType::Serf},     {{52, 156, 21}, 1, WardType::Serf},
    {{62, 156, 21}, 1, WardType::Urchin},   {{74, 156, 21}, 1, WardType::Serf},
    {{84, 156, 21}, 1, WardType::Serf},     {{118, 156, 21}, 1, WardType::Wastrel},
    {{126, 156, 21}, 1, WardType::Serf},    {{140, 156, 21}, 1, WardType::Serf},
    {{150, 156, 21}, 1, WardType::Thief},   {{160, 156, 21}, 1, WardType::Serf},
    {{170, 156, 21}, 1, WardType::Serf},    {{180, 156, 21}, 1, WardType::Wastrel},
    {{190, 156, 21}, 1, WardType::Serf},    {{200, 156, 21}, 1, WardType::Serf},
    {{210, 156, 21}, 1, WardType::Urchin},  {{168, 84, 19}, 1, WardType::Serf},
    {{178, 85, 19}, 1, WardType::Serf},
    // The Mission's bunkroom and the flophouse: the ward's casual-labour pool,
    // which is what K17's canon has always said it is.
    {{117, 110, 19}, 6, WardType::Wastrel},
    {{141, 87, 19}, 6, WardType::Sailor},
};

/// The household-size weights of content/raws/actors/household.json, which are
/// the same numbers DOCKS-GAZETTEER section 2.5 cites when it derives the ward.
/// Compiled here rather than read, because a size distribution is not something
/// a missing file may quietly change: the roll would move and nothing would say
/// so. The file is still the authority for the compound system that reads it.
constexpr std::int32_t kHouseholdSizeWeights[5] = {20, 35, 25, 15, 5};

/// The beasts. Every predator anchor is an OPEN STREET CELL and never a crewed
/// interior: a warehouse with a work crew in it crowd-locks any beast that
/// follows prey inside, and a beast that cannot get out of a bunkroom starves
/// in it where nobody can see.
constexpr Anchor kCatAnchors[] = {
    {150, 65, 19}, {98, 97, 19}, {86, 76, 19}, {123, 97, 19},
    {121, 64, 19}, {69, 101, 19}, {134, 130, 20}, {107, 152, 21},
};
constexpr Anchor kStrayAnchors[] = {
    {77, 62, 19}, {107, 44, 19}, {132, 62, 19}, {200, 65, 19}, {57, 62, 19},
};
constexpr Anchor kGoatPen{184, 143, 20};
constexpr Anchor kKennelDogs[] = {{197, 86, 19}, {203, 82, 19}, {203, 85, 19}};
constexpr Anchor kImpoundDog{92, 89, 19};

// --- who the Forty already are ----------------------------------------------
//
// #79. TWENTY-NINE OF THE FORTY WERE ALREADY STANDING HERE AND NOBODY HAD SAID
// SO. content/raws/names/notables.json binds each of them to an authored map
// site -- Ottavan Crell to K01_WEIGHHOUSE, Redda to K04_BILGE, Mother Sethra to
// K05_LANTERN_ROOM -- and section 4 of this file has been claiming a keeper for
// every one of those sites since #78. The two facts had never been introduced,
// so the district's harbourmaster was an anonymous shopkeeper standing in the
// Weighhouse and the owner's forty-two bios, fifteen micro-histories and whole
// rumor domain reached exactly one building.
//
// Binding them costs nothing at run time and changes no id, no draw and no
// anchor: the roster claims the same body it always claimed and now records
// what the raws already called it.
//
// WHAT IS DELIBERATELY ABSENT. venn and finch keep the Gilded Gull, which is
// the Tavern's building and whose people are the Tavern's -- naming a second
// Master Venn out here would put two of him in the district. herdis (PEN_GOATS)
// and the C1/C2/C3 mansion heads have no body in this roster to be: the pen
// holds goats and no keeper, and a compound's head of house is a row on the
// compound roll rather than a walker. Both are gaps, and they are named here
// rather than papered over by binding a name to whoever happened to be nearest.
struct Keeper {
    /// The establishment, spelled exactly as kEstablishments spells it.
    const char* site;
    /// The notable who keeps it, or "" -- a site the raws never named.
    const char* keeper;
    /// The notable who is the site's FIRST HAND, or "". Two sites have one:
    /// Onna is Father Maell's night-soup disciple, and Watchman Cull is the
    /// impound's beast-keeper under a militia post he does not hold.
    const char* firstHand;
};

constexpr Keeper kKeepers[] = {
    {"weighhouse", "crell", ""},      {"impound", "", "cull"},
    {"the-bilge", "redda", ""},       {"lantern-room", "sethra", ""},
    {"harls-yard", "harl", ""},       {"ropewalk", "hemp", ""},
    {"branns", "brann", ""},          {"pitchfield", "ulwer", ""},
    {"dawnstalls", "", ""},           {"salt-row", "salla", ""},
    {"kings-bond", "grieve", ""},     {"wrackhouse", "dagny", ""},
    {"fenners", "fenner", ""},        {"mission", "maell", "onna"},
    {"bathhouse", "squall", ""},      {"the-rows", "vetch", ""},
    {"merles-boats", "merle", ""},    {"netmenders", "withy", ""},
    {"coopers", "stave", ""},         {"eel-pots", "", ""},
    {"kennel-row", "cobb", ""},       {"sailmaker", "luff", ""},
    {"hardtack-oven", "crumb", ""},   {"slop-chest", "neddry", ""},
    {"long-store", "dray", ""},       {"bank", "gilt", ""},
    {"timber-yard", "", ""},          {"west-garden", "", ""},
};
static_assert(std::size(kKeepers) == std::size(kEstablishments),
              "one row per establishment, in the same order -- the bake walks them together");

/// The three deep-water hulls, and the captain who sleeps aboard each. Same
/// order as kHulls, and notables.json binds each of them by SHIP.
constexpr const char* kCaptains[] = {"wake", "bregga", "vane"};
static_assert(std::size(kCaptains) == std::size(kHulls));

}  // namespace

// ---------------------------------------------------------------------------
// the bake
// ---------------------------------------------------------------------------

void WardPopulation::bakeRoster(const std::filesystem::path& contentDir) {
    // THE ORDER OF THIS FUNCTION IS THE ID ASSIGNMENT and therefore part of the
    // world hash. Adding a group in the middle renumbers everybody after it and
    // re-rolls every draw keyed on an actor id. Append; do not insert.
    actors_.reserve(760);
    homes_.reserve(280);

    const auto spawn = [&](WardType type, WardJob job, Anchor post, std::int32_t homeIndex,
                           std::int32_t route) {
        std::int32_t px = post.x;
        std::int32_t py = post.y;
        std::int32_t pb = post.band;
        if (!snapToStandable(px, py, pb, 6)) {
            return;
        }
        WardActor actor;
        actor.id = static_cast<std::int32_t>(actors_.size());
        actor.type = type;
        actor.job = job;
        actor.anchorX = px;
        actor.anchorY = py;
        actor.anchorBand = pb;
        if (homeIndex >= 0) {
            const Home& home = homes_[static_cast<std::size_t>(homeIndex)];
            actor.homeX = home.x;
            actor.homeY = home.y;
            actor.homeBand = home.band;
        } else {
            actor.homeX = px;
            actor.homeY = py;
            actor.homeBand = pb;
        }
        // EVERYBODY STARTS AT HOME, whatever the hour. A roster that spawned
        // its whole day shift standing at its posts would show a district that
        // had already walked to work, and the first thing a player saw at seven
        // in the morning would be six hundred people teleporting.
        actor.x = actor.homeX;
        actor.y = actor.homeY;
        actor.band = actor.homeBand;
        actor.prevX = actor.x;
        actor.prevY = actor.y;
        actor.prevBand = actor.band;
        actors_.push_back(actor);
        homeOf_.push_back(homeIndex);
        routeOf_.push_back(route);
    };

    // --- 1. the homes -------------------------------------------------------
    //
    // Built first, because everybody in the ward sleeps somewhere and the
    // spawn above reads a home index. A dwelling site is expanded into its
    // households by walking outward from the anchor over standable floor in a
    // fixed spiral, so twelve households in a compound condo are twelve real
    // adjacent cells of that condo's own floor rather than twelve bodies on one
    // tile.
    struct Site {
        std::int32_t firstHome;
        std::int32_t homes;
        WardType flavour;
    };
    std::vector<Site> sites;
    sites.reserve(sizeof(kDwellings) / sizeof(kDwellings[0]));
    for (const Dwelling& dwelling : kDwellings) {
        Site site{static_cast<std::int32_t>(homes_.size()), 0, dwelling.flavour};
        std::int32_t ax = dwelling.at.x;
        std::int32_t ay = dwelling.at.y;
        std::int32_t ab = dwelling.at.band;
        // TEN AND NOT SIX, because of the roof huts. A rooftop tenant's own
        // anchor is on a plane the walking rules cannot reach, so the snap has
        // to come down off it far enough to find the compound underneath --
        // and a site that still finds nothing is dropped rather than given a
        // bed nobody can get to.
        if (!snapToStandable(ax, ay, ab, 10)) {
            continue;
        }
        // The spiral: ring by ring, and inside a ring in raster order. Fixed,
        // so which cells become homes is a fact about the map.
        for (std::int32_t r = 0; r <= 6 && site.homes < dwelling.households; ++r) {
            for (std::int32_t dy = -r; dy <= r && site.homes < dwelling.households; ++dy) {
                for (std::int32_t dx = -r; dx <= r && site.homes < dwelling.households; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) != r) {
                        continue;
                    }
                    const std::int32_t hx = ax + dx;
                    const std::int32_t hy = ay + dy;
                    // Standable AND on the ward's own walking island. The
                    // roof-slum planes of DOCKS-GAZETTEER section 2.6 are
                    // standable and reachable only by climbing, and a body
                    // homed up there could never walk to its own bed -- so its
                    // household is placed on the compound's ground instead.
                    // The roof slum itself stays unpopulated until an actor has
                    // a climb verb, which is a real gap and is stated here
                    // rather than papered over with a bed nobody can reach.
                    if (!tiles_->standable(hx, hy, ab) ||
                        componentAt(hx, hy, ab) != mainComponent_) {
                        continue;
                    }
                    // Never inside the Gilded Gull. K03 is the Tavern's
                    // building and its people are the Tavern's.
                    if (ab == docks::kBandQuayside && hx >= 146 && hx <= 160 && hy >= 66 &&
                        hy <= 79) {
                        continue;
                    }
                    homes_.push_back(Home{hx, hy, ab, 0});
                    ++site.homes;
                }
            }
        }
        sites.push_back(site);
    }

    // --- 2. the households --------------------------------------------------
    //
    // One household per home cell, sized off the canon weights. The draw is a
    // pure function of the home index and the world seed, so the ward's roll is
    // the same number every run of a seed and a different one for a different
    // seed -- which is what a seeded bake is for.
    std::vector<std::int32_t> residentsOf(homes_.size(), 0);
    for (std::size_t h = 0; h < homes_.size(); ++h) {
        const std::uint64_t roll =
            derive_draw(worldSeed_, 0, stream_salt("ward.household"), h, 0);
        residentsOf[h] = static_cast<std::int32_t>(
            weighted_pick(roll, kHouseholdSizeWeights, 5) + 1);
        homes_[h].residents = residentsOf[h];
    }

    // --- 3. the people ------------------------------------------------------
    //
    // Spawned home by home, in home order, which is site order, which is the
    // authored order of kDwellings. Their POSTS are filled in afterwards: at
    // this point everybody has a bed and a trade and nowhere to be.
    for (const Site& site : sites) {
        for (std::int32_t i = 0; i < site.homes; ++i) {
            const std::int32_t home = site.firstHome + i;
            const Home& where = homes_[static_cast<std::size_t>(home)];
            for (std::int32_t member = 0; member < residentsOf[static_cast<std::size_t>(home)];
                 ++member) {
                // The head of a household carries the site's flavour; the rest
                // of it is the ward's own mix, drawn once and never re-rolled.
                WardType type = site.flavour;
                if (member > 0) {
                    const std::uint64_t roll = derive_draw(
                        worldSeed_, 0, stream_salt("ward.member"),
                        static_cast<std::uint64_t>(home) * 8 + static_cast<std::uint64_t>(member), 0);
                    switch (roll % 10) {
                        case 0:
                        case 1: type = WardType::Urchin; break;
                        case 2: type = WardType::Wastrel; break;
                        case 3:
                            type = site.flavour == WardType::Serf ? WardType::Serf
                                                                  : site.flavour;
                            break;
                        default: type = WardType::Serf; break;
                    }
                }
                const WardJob job = type == WardType::Urchin   ? WardJob::Scavenge
                                    : type == WardType::Thief  ? WardJob::Thieving
                                    : type == WardType::Wastrel ? WardJob::Streetlife
                                                                : WardJob::Anchor;
                spawn(type, job, Anchor{where.x, where.y, where.band}, home, -1);
            }
        }
    }

    // --- 4. the posts -------------------------------------------------------
    //
    // Every establishment claims its keeper and its hands off the residents
    // already spawned, walking a cursor in ascending id and taking the first
    // body of the right trade that has not been claimed yet. Nobody is created
    // here: a shopkeeper is somebody's neighbour who keeps a counter, which is
    // the difference between a district and a set of shop fronts.
    std::vector<bool> claimed(actors_.size(), false);
    std::size_t cursor = 0;
    /// Answers WHICH body took the post, or -1. Returning the index rather
    /// than a bool is what lets the night roster be a list of real ids instead
    /// of an arithmetic guess about which body was claimed last.
    const auto claim = [&](WardType want, WardJob job, Anchor post,
                           std::int32_t route) -> std::int32_t {
        std::int32_t px = post.x;
        std::int32_t py = post.y;
        std::int32_t pb = post.band;
        if (!snapToStandable(px, py, pb, 6)) {
            return -1;
        }
        for (std::size_t pass = 0; pass < 2; ++pass) {
            for (std::size_t i = 0; i < actors_.size(); ++i) {
                const std::size_t at = (cursor + i) % actors_.size();
                if (claimed[at] || actors_[at].type == WardType::Thief) {
                    continue;
                }
                // First pass takes the trade that fits; the second takes spare
                // LABOUR only -- a serf or a wastrel -- because a shed with
                // nobody in it is worse than a shed with the wrong hands in it,
                // and because a second pass that took anybody would quietly
                // employ every child in the district and empty the bins.
                const bool spare = actors_[at].type == WardType::Serf ||
                                   actors_[at].type == WardType::Wastrel;
                const bool fits = actors_[at].type == want || (pass == 1 && spare);
                if (!fits) {
                    continue;
                }
                claimed[at] = true;
                cursor = at + 1;
                actors_[at].type = want;
                actors_[at].job = job;
                actors_[at].anchorX = px;
                actors_[at].anchorY = py;
                actors_[at].anchorBand = pb;
                routeOf_[at] = route;
                return static_cast<std::int32_t>(at);
            }
        }
        return -1;
    };

    // #79. Records who the raws already called the body that took the post. The
    // roster is not changed by this in any way -- same claim, same id, same
    // anchor -- so the ward's whole behavioural bake is bit-identical and only
    // the name over a conversation is new.
    const auto nameThem = [&](std::int32_t who, const char* notableId) {
        if (who < 0 || notableId == nullptr || notableId[0] == '\0') {
            return;
        }
        // Grown rather than pre-sized: the beasts are spawned after this block
        // and bakeIdentities() extends the vector to the final roll. Claiming
        // only ever touches a body that already exists, so this can only ever
        // reach backwards.
        if (identities_.size() <= static_cast<std::size_t>(who)) {
            identities_.resize(static_cast<std::size_t>(who) + 1);
        }
        identities_[static_cast<std::size_t>(who)].notableId = notableId;
    };

    for (std::size_t s = 0; s < std::size(kEstablishments); ++s) {
        const Establishment& site = kEstablishments[s];
        const Keeper& named = kKeepers[s];
        const bool nightHouse = site.post.x == 129 && site.post.y == 63;  // the Eel-Pots
        nameThem(claim(site.keeper, WardJob::Anchor, site.post, -1), named.keeper);
        for (std::int32_t i = 0; i < site.hands; ++i) {
            const std::int32_t hand =
                claim(site.hand, nightHouse ? WardJob::Scavenge : WardJob::Anchor, site.post, -1);
            if (i == 0) {
                nameThem(hand, named.firstHand);
            }
        }
        if (site.hasGuard) {
            claim(WardType::MilitiaWatch, WardJob::Anchor, site.guard, -1);
        }
    }

    // The market stalls, the fishing stands, the timber stations and the
    // berths: posts with nobody's name on them until somebody stands at one.
    for (const Anchor& stall : kMarketStalls) {
        claim(WardType::Shopkeeper, WardJob::Anchor, stall, -1);
        std::int32_t sx = stall.x;
        std::int32_t sy = stall.y;
        std::int32_t sb = stall.band;
        if (snapToStandable(sx, sy, sb, 4)) {
            marketStalls_.push_back(PathStep{sx, sy, sb});
        }
    }
    for (std::size_t f = 0; f < std::size(kFisherStands); ++f) {
        for (int i = 0; i < 2; ++i) {
            const std::int32_t who = claim(WardType::Fisher, WardJob::Fish, kFisherStands[f], -1);
            // FISHBONE_FINGER_03: Haddie Longline works the third finger, and
            // the raws say which one.
            if (f == 2 && i == 0) {
                nameThem(who, "haddie");
            }
        }
    }
    for (const Anchor& berth : kBerths) {
        for (int i = 0; i < 4; ++i) {
            claim(WardType::Sailor, WardJob::Anchor, berth, -1);
        }
    }
    for (std::size_t h = 0; h < std::size(kHulls); ++h) {
        for (int i = 0; i < 4; ++i) {
            const std::int32_t who = claim(WardType::Sailor, WardJob::Anchor, kHulls[h], -1);
            // The captain sleeps aboard, and is the first hand off each hull.
            if (i == 0) {
                nameThem(who, kCaptains[h]);
            }
        }
    }

    // --- 5. the routes, and who walks them ----------------------------------
    const auto addRoute = [&](const Anchor* points, std::size_t count) -> std::int32_t {
        Route route{static_cast<std::int32_t>(waypoints_.size()), 0};
        for (std::size_t i = 0; i < count; ++i) {
            std::int32_t wx = points[i].x;
            std::int32_t wy = points[i].y;
            std::int32_t wb = points[i].band;
            if (!snapToStandable(wx, wy, wb, 6)) {
                continue;
            }
            waypoints_.push_back(PathStep{wx, wy, wb});
            ++route.count;
        }
        routes_.push_back(route);
        return static_cast<std::int32_t>(routes_.size()) - 1;
    };

    const std::int32_t tarwalkBeat = addRoute(kTarwalkRoute, std::size(kTarwalkRoute));
    const std::int32_t quayBeat = addRoute(kQuayRoute, std::size(kQuayRoute));
    const std::int32_t ropewyndBeat = addRoute(kRopewyndRoute, std::size(kRopewyndRoute));
    const std::int32_t saltgateBeat = addRoute(kSaltgateRoute, std::size(kSaltgateRoute));
    const std::int32_t cartRound = addRoute(kCarterRoute, std::size(kCarterRoute));
    const std::int32_t binRound = addRoute(kBins, std::size(kBins));

    // THE GARRISONS. Nineteen watchmen, which is the Java build's own number,
    // and the split between day and night is the whole answer to "even at night
    // there should be people like guards".
    //
    // Six on the day beats, six holding the two posts by day, and SEVEN on the
    // night roster -- the Ropewynd, the Tarwalk and the Rise, walked from six
    // in the evening until six in the morning. The night seven carry
    // worksThroughTheNight, which is the one flag that stops RETURN_HOME's
    // night term dragging them off their beat every tick.
    claim(WardType::MilitiaWatch, WardJob::Patrol, kPatrolPosts[0], tarwalkBeat);
    claim(WardType::MilitiaWatch, WardJob::Patrol, kPatrolPosts[1], tarwalkBeat);
    claim(WardType::MilitiaWatch, WardJob::Patrol, kPatrolPosts[0], quayBeat);
    claim(WardType::MilitiaWatch, WardJob::Patrol, kPatrolPosts[2], saltgateBeat);
    claim(WardType::MilitiaWatch, WardJob::Patrol, kPatrolPosts[3], ropewyndBeat);
    claim(WardType::MilitiaWatch, WardJob::Patrol, kPatrolPosts[2], ropewyndBeat);
    // WATCHPOST_K21 and K34_GUARDHOUSE, which notables.json binds by name:
    // Sergeant Vess quarters at the head of Saltgate Rise and Sergeant Brakk at
    // the guardhouse at its foot. Both are the first body to hold their post.
    nameThem(claim(WardType::MilitiaWatch, WardJob::Anchor, kWatchPostHead, -1), "vess");
    claim(WardType::MilitiaWatch, WardJob::Anchor, kWatchPostHead, -1);
    nameThem(claim(WardType::MilitiaWatch, WardJob::Anchor, kWatchPostFoot, -1), "brakk");
    claim(WardType::MilitiaWatch, WardJob::Anchor, kWatchPostFoot, -1);
    claim(WardType::MilitiaWatch, WardJob::Anchor, kWatchPostFoot, -1);
    claim(WardType::MilitiaWatch, WardJob::Anchor, kWatchPostHead, -1);
    // THE NIGHT SEVEN, and they are the whole of the owner's complaint.
    //
    // Three walk the Ropewynd -- the road every compound gate opens onto and
    // the one a player crossing the ward at two in the morning is most likely
    // to be on. Two walk the Tarwalk, which is where the Eel-Pots are lit. Two
    // walk the Rise, which is the only way in or out of the district on foot.
    // Their job is NightWatch and not Patrol, so the window is eighteen to six
    // and worksThroughTheNight is true for them and for nobody else.
    static constexpr std::int32_t kNightPosts[7] = {2, 3, 2, 0, 1, 3, 2};
    for (int i = 0; i < 7; ++i) {
        const std::int32_t post = kNightPosts[i];
        const std::int32_t beat = post == 0 || post == 1 ? tarwalkBeat
                                  : post == 2           ? ropewyndBeat
                                                        : saltgateBeat;
        const std::int32_t who = claim(WardType::MilitiaWatch, WardJob::NightWatch,
                                       kPatrolPosts[static_cast<std::size_t>(post)], beat);
        if (who >= 0) {
            // Staggered starting waypoints, draw-free, so seven watchmen do not
            // walk the same beat in single file.
            actors_[static_cast<std::size_t>(who)].leg = static_cast<std::int16_t>(i);
            nightRoster_.push_back(actors_[static_cast<std::size_t>(who)].id);
        }
    }

    // The carters, and the children who work the bins. CARTER_STAND is Carter
    // Weyland's own post in notables.json, and he is the first man on the round.
    for (int i = 0; i < 4; ++i) {
        const std::int32_t who =
            claim(WardType::Carter, WardJob::Rounds, kCarterRoute[0], cartRound);
        if (i == 0) {
            nameThem(who, "weyland");
        }
    }
    // THE COMMONS, AND THE MUSTER, handed out round-robin in id order to
    // everybody the establishments did not want.
    //
    // TWO GROUPS AND ONE REASON. A wastrel's post is a kerb; a wastrel whose
    // post was its own bed would spend its whole life indoors, which is
    // precisely the district the owner complained about. And an unclaimed
    // LABOURER is not unemployed, it is casual labour -- DOCKS-GAZETTEER's own
    // reading of K17, "the almshouse pool is the ward's casual-labor pool now,
    // as K17's canon always said" -- so its post is the muster it stands at
    // waiting for a day's work, not the room it slept in.
    //
    // Without this the day shift is four hundred people working at home with
    // the doors shut, and the street at noon is emptier than the street at
    // midnight.
    {
        std::size_t next = 0;
        for (std::size_t i = 0; i < actors_.size(); ++i) {
            const bool loiterer = actors_[i].job == WardJob::Streetlife;
            const bool unhired = !claimed[i] && actors_[i].job == WardJob::Anchor &&
                                 isPerson(actors_[i].type);
            if (!loiterer && !unhired) {
                continue;
            }
            const std::size_t slot = next++;
            const Anchor& spot = kCommons[slot % std::size(kCommons)];
            // FANNED OUT ALONG THE ROAD, not heaped on the marker. Four hundred
            // spare hands over twenty-eight commons is fourteen apiece, and
            // fourteen bodies inside one post's two-tile reach is a scrum the
            // shove would spend all day untangling. Each time round the list
            // the muster steps a few tiles further along the street it is on,
            // so a road reads as a road with people on it rather than as
            // twenty-eight knots.
            const std::int32_t ring = static_cast<std::int32_t>(slot / std::size(kCommons));
            const std::int32_t drift = (ring / 2 + 1) * 3;
            std::int32_t cx = spot.x + ((ring % 2 == 0) ? drift : -drift);
            std::int32_t cy = spot.y;
            std::int32_t cb = spot.band;
            if (!snapToStandable(cx, cy, cb, 5)) {
                continue;
            }
            actors_[i].anchorX = cx;
            actors_[i].anchorY = cy;
            actors_[i].anchorBand = cb;
        }
    }

    for (std::size_t i = 0; i < actors_.size(); ++i) {
        if (actors_[i].type == WardType::Urchin && routeOf_[i] < 0) {
            routeOf_[i] = binRound;
            actors_[i].job = WardJob::Scavenge;
            // A child works the bins nearest its own bed, so the round starts
            // at a different waypoint for each of them -- draw-free, so two
            // urchins never walk the ward in lockstep.
            actors_[i].leg = static_cast<std::int16_t>(
                actors_[i].id % static_cast<std::int32_t>(std::size(kBins)));
        }
        if (actors_[i].type == WardType::Thief && routeOf_[i] < 0) {
            actors_[i].job = WardJob::Thieving;
            actors_[i].leg = static_cast<std::int16_t>(actors_[i].id % 5);
        }
    }

    // --- 6. the beasts ------------------------------------------------------
    //
    // Spawned LAST so that every id below this point is a person, which is what
    // lets a census split the roll without a per-actor branch, and so that
    // adding a cat cannot renumber a single member of the ward.
    for (const Anchor& at : kCatAnchors) {
        spawn(WardType::Cat, WardJob::Wander, at, -1, -1);
    }
    for (const Anchor& at : kStrayAnchors) {
        spawn(WardType::Stray, WardJob::Wander, at, -1, -1);
    }
    for (const Anchor& at : kKennelDogs) {
        spawn(WardType::Dog, WardJob::Wander, at, -1, -1);
    }
    spawn(WardType::Dog, WardJob::Wander, kImpoundDog, -1, -1);
    for (int i = 0; i < 4; ++i) {
        spawn(WardType::Dog, WardJob::Wander, kGoatPen, -1, -1);
    }
    // The mice: two to a bin, which is what a bin in this district actually
    // holds, plus the Gullet's own cluster.
    for (const Anchor& bin : kBins) {
        for (int i = 0; i < 2; ++i) {
            spawn(WardType::Mouse, WardJob::Wander, bin, -1, -1);
        }
    }

    // --- 7. and only now, the needs -----------------------------------------
    //
    // LAST, because claiming a post changes what somebody IS: the spare hand
    // who ends up keeping the Slop-Chest is a shopkeeper by the time the ward
    // opens, and reading a serf's appetite for him would be reading the row of
    // a trade he no longer has. Nothing before this point may depend on a need.
    for (WardActor& actor : actors_) {
        const WardTypeStats& stats = types_[actor.type];
        for (std::size_t n = 0; n < kNeedCount; ++n) {
            actor.needs[n] = static_cast<std::int16_t>(stats.needs[n].start);
        }
        actor.rations = isPerson(actor.type) ? 2 : 0;
        actor.coin = isPerson(actor.type) ? 12 : 0;
        ledger_.foodMinted += actor.rations;
        ledger_.coinMinted += actor.coin;
    }

    // --- 8. and the names ---------------------------------------------------
    //
    // LAST, and after the needs, for the same reason the needs are last:
    // claiming a post changes what somebody IS, and a body is named out of the
    // pool of the trade it will actually be doing when the ward opens.
    bakeIdentities(contentDir);
}

}  // namespace granadad::sim
