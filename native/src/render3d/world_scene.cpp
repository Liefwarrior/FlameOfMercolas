#include "granadad/render3d/world_scene.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "granadad/render/lighting.hpp"
#include "granadad/render/voxel_classify.hpp"
#include "granadad/render/world_renderer.hpp"
#include "granadad/sim/tile_query.hpp"

namespace granadad::render3d {

namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr int kSkySegments = 32;

[[nodiscard]] std::uint8_t channel8(float value) noexcept {
    const float clamped = std::clamp(value, 0.0F, 1.0F);
    return static_cast<std::uint8_t>(clamped * 255.0F + 0.5F);
}

[[nodiscard]] Rgba8 rgba(const render::Rgb& c) noexcept {
    return Rgba8{channel8(c.r), channel8(c.g), channel8(c.b), 255};
}

void pushVertex(MeshData& mesh, float x, float y, float z, const Rgba8& c) {
    mesh.positions.push_back(x);
    mesh.positions.push_back(y);
    mesh.positions.push_back(z);
    mesh.texcoords.push_back(0.0F);
    mesh.texcoords.push_back(0.0F);
    mesh.colours.push_back(c.r);
    mesh.colours.push_back(c.g);
    mesh.colours.push_back(c.b);
    mesh.colours.push_back(c.a);
}

/// A triangle in both windings: the dome is seen from inside, and a sky
/// that back-face culling could swallow is not worth the winding argument.
void pushDoubleTriangle(MeshData& mesh, std::uint16_t a, std::uint16_t b, std::uint16_t c) {
    mesh.indices.push_back(a);
    mesh.indices.push_back(b);
    mesh.indices.push_back(c);
    mesh.indices.push_back(a);
    mesh.indices.push_back(c);
    mesh.indices.push_back(b);
}

/// Squared distance from a point to an axis-aligned box in the XZ plane.
[[nodiscard]] float boxDistanceSq(float px, float pz, float minX, float minZ, float maxX,
                                  float maxZ) noexcept {
    const float dx = px < minX ? minX - px : (px > maxX ? px - maxX : 0.0F);
    const float dz = pz < minZ ? minZ - pz : (pz > maxZ ? pz - maxZ : 0.0F);
    return dx * dx + dz * dz;
}

/// A surface's light is ambient + glow, and a piece in a lamp's pool is
/// lit rather than blown out: the sum is clamped a little over one.
constexpr float kLightClamp = 1.15F;

/// A flame's halo keeps this much of its alpha at full daylight.
constexpr float kFlameDayAlpha = 0.12F;

/// A flame's halo floats toward the eye along the line of sight by this many
/// METRES (it was half the quad's own height, which was the same 0.29 back
/// when the quad was the flame; the disc is four times that wide now and the
/// float has no business growing with it): a point moved along
/// the eye's own ray lands on the same pixel, so the glow stays centred on
/// the flame, but its plane now clears the lantern's cap, cage and base
/// (all within 0.26 m of the flame) from every side, so the body never
/// slices the glow along a line that walks with the eye -- the glow is
/// drawn over the lamp and the lamp reads through it, a lit glass and not
/// a lit hook. Never more than the cap's fraction of the way to the eye,
/// so the quad's centre stays past the near plane (0.1) even with the
/// lamp at the edge of the view and a body pressed to the wall under it
/// (0.31 m from the flame: the quad 0.19 out, clipped only past 57
/// degrees off axis). Nearer the flame than 0.72 m the cap wins and the
/// base's rim stands in front of the plane -- the underside of a lamp is
/// dark, its rim comes through the glow -- which is the honest limit of a
/// depth-tested sprite against an opaque body.
constexpr float kHaloForward = 0.29F;
constexpr float kHaloForwardCap = 0.4F;

/// A window pane goes warm after dark (the sky under the catalogue's
/// `paneDuskBelow`) when the room behind it glows more than this -- a lamp
/// reaches it -- or the light law says the household is up (paneGlows()).
constexpr float kPaneLitAbove = 0.12F;

/// THE SPILL (the night-light critic's third): the emissive pool a lit
/// pane puts on the boards either side of it, as a glow ADDED under the
/// surface clamp at the board's end that meets the pane -- candle-coloured,
/// about the pool a lantern gives its own wall -- and kSpillFarShare of it
/// at the board's far end, so the blend across the board runs out from the
/// window. Only the halves static_pieces.cpp cut at a hung window carry it
/// (StaticPlacement::spill), and it goes out with the pane.
///
/// ROUND TWO: a third of what it was, and spread. At 0.62 on one cell the
/// wall beside a window came out brighter than the pane that lit it and
/// stepped hard at every cell (the round-two player's first and second):
/// a candle behind glass is a small light. The pane's own wall cell takes
/// this; the cells along the wall and out into the street take the shares
/// below (buildSpill). The chunk colour stage blends the map across each
/// vertex's four cells and a run piece's cuts fall between cells, so
/// nothing steps at a seam.
///
/// ROUND THREE: wider and gentler. The chunk stage blends the map at each
/// vertex, and a quad whose four corners differ by much shows its diagonal
/// (the director's fog diagonal above the Gull's door): the steps between
/// neighbouring cells are small now -- three tiles along the wall and three
/// out into the street, easing to nothing.
constexpr render::Rgb kSpillGlow{0.30F, 0.19F, 0.08F};
/// Along the wall at one, two and three tiles.
constexpr float kSpillSide1 = 0.7F;
constexpr float kSpillSide2 = 0.4F;
constexpr float kSpillSide3 = 0.15F;
/// Out into the street: the row before the window at nought, one and two
/// tiles along; the second row; the third.
constexpr float kSpillOut1 = 0.9F;
constexpr float kSpillOut1Side1 = 0.6F;
constexpr float kSpillOut1Side2 = 0.3F;
constexpr float kSpillOut2 = 0.5F;
constexpr float kSpillOut2Side1 = 0.35F;
constexpr float kSpillOut2Side2 = 0.15F;
constexpr float kSpillOut3 = 0.2F;
constexpr float kSpillOut3Side1 = 0.12F;
/// A kit window on masonry is ONE run piece with its glass in the middle
/// cell, lit at its two cuts a tile and a quarter off the pane; the cuts'
/// own share of the candle is a quarter, so the plaster round the glass
/// stayed dark. The module takes this share of its pane's own cell at both
/// ends instead, so the wall round a lit kit window is lit.
///
/// ROUND THREE: gone. Any extra on the module's ends is a step at its cuts
/// against the module beyond (the round-three director's first: 68 beside
/// 59), because the blend across a run piece is linear between its two
/// ends and cannot peak at the pane. The module reads its cuts exactly as
/// every run piece does and the pool of light round a window is carried by
/// the map's own footprint: the pane's cell and its neighbours along the
/// wall lift the cuts either side of the module, and the modules beyond
/// fall away from them. No seam anywhere on the run (pinned).

/// THE HALO UP CLOSE (the director's first): the fan is a metre and a half
/// across and floats toward the eye, so inside two metres it covered the
/// lamp, the cage, the cap and whoever stood under it in one cream veil. Its
/// alpha fades with the eye's distance now -- full past kHaloNearFull, down
/// to kHaloNearFloor of itself at kHaloNearGone and closer -- so a body at
/// the Gull's door sees the lantern, not the fog.
///
/// ROUND THREE: cubic, to five and a half metres -- a linear ramp to four
/// still laid three quarters of the disc over the cap at three metres, and
/// the cap read cream (the director's third). At three metres the disc is
/// a fifth of itself now, at four two fifths, full past five and a half;
/// the glass carries the lamp at those ranges, the disc the air round it.
constexpr float kHaloNearGone = 0.6F;
constexpr float kHaloNearFull = 5.5F;
constexpr float kHaloNearFloor = 0.08F;
/// THE HALO IN FOG: drawn past the veils now (rl_backend.cpp), so the fog
/// takes its share off the glow here instead -- half the veil's own
/// e-folding over the eye's distance, because a glow scatters and is seen
/// further into a fog than the lamp that makes it. Zero in clear air.
constexpr float kHaloFogShare = 0.5F;

/// THE CANDLE'S OWN STRENGTH (the director's fifth): a lit frontage was one
/// amber at one value. Every household's candle draws its own brightness
/// off its lot -- the family with one rushlight and the one with three --
/// down to (1 - kCandleSpread) of the law's tint; a house lit all night
/// draws per pane, so a tavern's row of windows is not one flat strip.
constexpr float kCandleSpread = 0.32F;
/// THE SMALL HOURS. A house lit all night (the Gull, the Mission, the
/// Watch-Post) keeps a light whatever the hour, but from three in the
/// morning until daylight takes every pane it is the taproom banked and a
/// watchman's candle, not the evening's full row: its panes burn at this
/// share of their strength. The hours are the sim's own tavern's
/// (sim/tavern.hpp): its doors shut at two, its fire burns to three and is
/// banked to ten, its doors stay shut to eleven -- so the panes bank at
/// three and never come back up before first light (the round-four
/// auditor: a first cut banked from one and jumped back to full at half
/// past five, against the sim at both ends). It is also what makes five
/// o'clock a different street from two on a frontage that never goes dark
/// (the director's fourth, three rounds).
constexpr float kLitHouseSmallHours = 0.55F;
constexpr float kSmallHoursFrom = 27.0F;
constexpr float kSmallHoursTo = 36.0F;
/// The banking eases in over the three quarters of an hour before three
/// (the last drinkers out, the fire let down), not in one step on the
/// minute: the relight runs on the minute, and half the light gone in one
/// frame is a pop (the fourth round's director).
constexpr float kSmallHoursEase = 0.75F;

/// How far into the small hours, on the noon-to-noon clock the light law
/// keeps: 0 through the evening, easing to 1 over kSmallHoursEase before
/// kSmallHoursFrom, 1 from there until kSmallHoursTo, 0 after.
[[nodiscard]] float smallHoursAt(float hour) noexcept {
    const float h = hour < 12.0F ? hour + 24.0F : hour;
    if (h >= kSmallHoursTo) {
        return 0.0F;
    }
    const float t = std::clamp((h - (kSmallHoursFrom - kSmallHoursEase)) / kSmallHoursEase, 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

/// THE LANTERN'S METAL AFTER DARK: the cap, the cage and the base are lit
/// like everything else now -- the sky's ambient plus the lamp's own pool
/// -- and capped at this, so the glass (below) out-values the cap at any
/// range. They drew at their full catalogue tint, brighter than the glass,
/// which was the critic's first note on the halo: a lamp whose dullest
/// part was its light. By day they keep the full tint they always had.
constexpr float kLampMetalNightCap = 0.5F;

/// THE CRITIC'S THIRD: how far a lit hung window's own frame (WindowTimber)
/// leans from its ambient tint toward the law's own warm one -- mixed, not
/// replaced, so the timber still reads as timber.
constexpr float kFrameWarmMix = 0.4F;

/// THE LANTERN'S GLASS AFTER DARK: near white-amber, at its own full value,
/// and NOT lit by the sky at all -- a lamp is its own light (p.selfLit), so
/// this tint goes to the pane slot verbatim and stays hot in the dark. It
/// was darkPane() of the lamp's own tint, which is a quarter of it with a
/// blue lean: flat grey-olive, dimmer than the copper cap over it and far
/// dimmer than the door panel a metre below, which is the critic's third
/// note and dead right. A lantern's brightest element is its glass.
///
/// By day the glass goes back to darkPane(): plain glass on a cold lamp.
/// The mechanism is the pane slot the light law already uses (slots[4],
/// drawn on any sub-mesh the adapter classes as glass) -- nothing in
/// paneGlows() or the window rule is touched to get it.
///
/// THIRD PASS: nearer white still, and the adapter draws a pane VERBATIM
/// now (rl_backend.cpp, drawStaticMesh) instead of through the glass
/// material's own base colour, which on the camp lantern is a grey-green
/// that turned this amber to olive.
constexpr Rgba8 kLampGlassNight{255, 240, 205, 255};

/// True for the lamp bodies that HAVE a glass: the hung lantern and the
/// street post. The bracket and the chain are not self-lit, the flame is a
/// glow and not a lamp.
[[nodiscard]] bool hasLampGlass(PieceRole role) noexcept {
    return role == PieceRole::LampWall || role == PieceRole::LampPost;
}

/// The glass of an unlit window: a third of the light, blue-grey -- a dark
/// pane, not a wash over the wall the chunk mesh puts behind it.
[[nodiscard]] Rgba8 darkPane(const Rgba8& lit) noexcept {
    return Rgba8{static_cast<std::uint8_t>(lit.r / 4), static_cast<std::uint8_t>(lit.g / 4 + lit.g / 16),
                 static_cast<std::uint8_t>(lit.b / 3), lit.a};
}

[[nodiscard]] Vec3 sub(const Vec3& a, const Vec3& b) noexcept { return Vec3{a.x - b.x, a.y - b.y, a.z - b.z}; }
[[nodiscard]] float dot(const Vec3& a, const Vec3& b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
[[nodiscard]] Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
    return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
[[nodiscard]] Vec3 normalised(const Vec3& v) noexcept {
    const float len = std::sqrt(dot(v, v));
    return len > 1.0e-6F ? Vec3{v.x / len, v.y / len, v.z / len} : Vec3{0.0F, 0.0F, -1.0F};
}

/// THE FRUSTUM CULL on a piece: its origin, pushed out by its radius,
/// against the camera's forward half-space, the two side planes and the
/// two lid planes, with a margin. A piece behind the eye or well off the
/// sides is not described at all.
struct Frustum {
    Vec3 eye;
    Vec3 forward;
    Vec3 right;
    Vec3 up;
    float tanHalfX = 1.0F;
    float tanHalfY = 1.0F;

    [[nodiscard]] static Frustum of(const SceneCamera& camera, float aspect) noexcept {
        Frustum f;
        f.eye = camera.position;
        f.forward = normalised(sub(camera.target, camera.position));
        f.right = normalised(cross(f.forward, camera.up));
        f.up = cross(f.right, f.forward);
        const float halfY = camera.fovyDegrees * 0.5F * (kPi / 180.0F);
        f.tanHalfY = std::tan(halfY) * 1.15F;
        f.tanHalfX = std::tan(halfY) * std::max(0.1F, aspect) * 1.15F;
        return f;
    }

    [[nodiscard]] bool sees(const Vec3& at, float radius) const noexcept {
        const Vec3 v = sub(at, eye);
        const float ahead = dot(v, forward);
        if (ahead < -radius) {
            return false;
        }
        const float reach = std::max(0.0F, ahead + radius);
        const float side = std::fabs(dot(v, right)) - radius;
        if (side > reach * tanHalfX) {
            return false;
        }
        const float lid = std::fabs(dot(v, up)) - radius;
        return lid <= reach * tanHalfY;
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// the sky
// ---------------------------------------------------------------------------

std::uint32_t skyDomeVersion(int timeOfDaySeconds) noexcept {
    const int minute = ((timeOfDaySeconds % 86400) + 86400) % 86400 / 60;
    return static_cast<std::uint32_t>(minute) + 1U;
}

MeshData buildSkyDome(int timeOfDaySeconds) {
    return buildSkyDome(timeOfDaySeconds, render::Weather{});
}

MeshData buildSkyDome(int timeOfDaySeconds, const render::Weather& weather) {
    MeshData mesh;
    mesh.id = kSkyMeshId;
    mesh.version = skyDomeVersion(timeOfDaySeconds);
    const render::SkyState sky = render::skyAt(timeOfDaySeconds, weather);
    const Rgba8 horizon = rgba(sky.skyHorizon);
    const Rgba8 top = rgba(sky.skyTop);

    // Three rings -- under the eye, at the eye, overhead -- and a lid. The
    // horizon colour holds from below up to eye level, then ramps to the
    // zenith colour, which the lid carries across the top.
    for (int i = 0; i < kSkySegments; ++i) {
        const float angle = 2.0F * kPi * static_cast<float>(i) / static_cast<float>(kSkySegments);
        const float x = kSkyRadius * std::cos(angle);
        const float z = kSkyRadius * std::sin(angle);
        pushVertex(mesh, x, -kSkyHeight, z, horizon);
        pushVertex(mesh, x, 0.0F, z, horizon);
        pushVertex(mesh, x, kSkyHeight, z, top);
    }
    const auto lidCentre = static_cast<std::uint16_t>(mesh.vertexCount());
    pushVertex(mesh, 0.0F, kSkyHeight, 0.0F, top);

    for (int i = 0; i < kSkySegments; ++i) {
        const int next = (i + 1) % kSkySegments;
        const auto a0 = static_cast<std::uint16_t>(i * 3);
        const auto a1 = static_cast<std::uint16_t>(i * 3 + 1);
        const auto a2 = static_cast<std::uint16_t>(i * 3 + 2);
        const auto b0 = static_cast<std::uint16_t>(next * 3);
        const auto b1 = static_cast<std::uint16_t>(next * 3 + 1);
        const auto b2 = static_cast<std::uint16_t>(next * 3 + 2);
        pushDoubleTriangle(mesh, a0, b0, b1);
        pushDoubleTriangle(mesh, a0, b1, a1);
        pushDoubleTriangle(mesh, a1, b1, b2);
        pushDoubleTriangle(mesh, a1, b2, a2);
        pushDoubleTriangle(mesh, a2, b2, lidCentre);
    }
    return mesh;
}

// ---------------------------------------------------------------------------
// the veil (WEATHER)
// ---------------------------------------------------------------------------

namespace {

/// The shells, nearest first, in tiles. Closer together near the eye, where
/// a step in the fog is a step across a wall the eye can read, and opening
/// out with distance, where what contrast is left is small and a step is
/// not. Fifty-six tiles is short of the chunk reach (64) and past anything
/// a frame resolves through a fog worth drawing.
/// NIGHT LIGHT, THIRD PASS: three thin shells inside two metres, then the
/// ladder. A shell cuts every wall and pane nearer than its radius in a
/// hard step of its own alpha -- one shell at 1.5 m was a sixteen-percent
/// edge across the plaster above the Gull's door and a line sliding across
/// every near pane as you walked (three rounds of the player critic, read
/// as the halo's rim). Thinner shells there, smaller steps -- and every
/// spacing DIFFERENT from the last, growing by about a tenth a shell: a
/// first cut of the third pass at twenty-six shells 0.4 m apart blended
/// the same alpha the same way twenty-six times in eight bits and drifted
/// the fogged alley olive (the fourth round's player); shells of unequal
/// alpha round each way and the drift averages out. Twenty-four, against
/// the weather lane's nineteen, with no step over a tenth inside ten
/// metres (the fourth round's director: a twelve-percent band at eight).
constexpr float kVeilRadii[] = {0.7F,  1.2F,  1.8F,  2.5F,  3.3F,  4.2F,  5.1F,  6.0F,
                                6.9F,  7.8F,  8.8F,  9.9F,  11.2F, 12.6F, 14.3F, 16.3F,
                                18.6F, 21.3F, 24.5F, 28.3F, 33.0F, 39.0F, 46.0F, 56.0F};
constexpr std::size_t kVeilShells = sizeof(kVeilRadii) / sizeof(kVeilRadii[0]);
/// Each shell's rings, as elevations in degrees, pole to pole. Denser round
/// the horizon, where the fog's colour turns into the sky's. The poles are
/// full rings of coincident vertices, so every ring indexes alike.
constexpr float kVeilRingDegrees[] = {-90.0F, -50.0F, -25.0F, -10.0F, 0.0F, 8.0F,
                                      16.0F,  25.0F,  35.0F,  50.0F,  90.0F};
constexpr std::size_t kVeilRings = sizeof(kVeilRingDegrees) / sizeof(kVeilRingDegrees[0]);
/// Forty-eight round, so the curve a shell cuts across a wall is a curve.
constexpr std::size_t kVeilSegments = 48;
/// A shell whose inside is already this fogged over is not drawn: nothing
/// it would add can be seen through what is inside it.
constexpr float kVeilFloor = 0.004F;

/// What a veil ring is coloured: the fog at and below the horizon; above
/// it, the sky dome's own colour at that elevation (horizon to top over the
/// dome's rise, kSkyHeight over kSkyRadius; the flat top above that), so a
/// veil over the sky IS the sky and only the world in front of it fogs.
[[nodiscard]] render::Rgb veilRingColour(const render::SkyState& sky, float elevationDegrees) {
    if (elevationDegrees <= 0.0F) {
        return sky.fog;
    }
    if (elevationDegrees >= 90.0F) {
        // Straight up is the dome's lid. Asked of tan, a float's quarter
        // turn is a hair over the true one and the answer is a large number
        // of the WRONG sign, which would clamp to the fog.
        return sky.skyTop;
    }
    const float rise = std::tan(elevationDegrees * (kPi / 180.0F)) * kSkyRadius / kSkyHeight;
    return render::lerp(sky.fog, sky.skyTop, std::clamp(rise, 0.0F, 1.0F));
}

}  // namespace

std::size_t veilShellCount() noexcept { return kVeilShells; }

float veilShellRadius(std::size_t shell) noexcept {
    return shell < kVeilShells ? kVeilRadii[shell] : 0.0F;
}

MeshData buildVeil(const render::SkyState& sky, std::uint32_t version) {
    MeshData mesh;
    mesh.id = kVeilMeshId;
    mesh.version = version;
    if (!(sky.veil > 0.0F)) {
        return mesh;
    }

    // Which shells are worth drawing, and how much fog each one is: the
    // fog between it and the shell inside it. A shell whose inside is
    // already fogged to the floor adds nothing anyone can see.
    std::uint8_t alpha[kVeilShells] = {};
    std::size_t drawn = 0;
    float inner = 0.0F;
    for (std::size_t shell = 0; shell < kVeilShells; ++shell) {
        const float through = std::exp(-sky.veil * inner);
        if (through < kVeilFloor) {
            break;
        }
        const float fog = 1.0F - std::exp(-sky.veil * (kVeilRadii[shell] - inner));
        alpha[shell] = channel8(fog);
        drawn = shell + 1;
        inner = kVeilRadii[shell];
    }

    Rgba8 ring[kVeilRings];
    for (std::size_t i = 0; i < kVeilRings; ++i) {
        ring[i] = rgba(veilRingColour(sky, kVeilRingDegrees[i]));
    }

    // FAR TO NEAR in the index buffer: the outermost shell rasterizes first
    // and every nearer one over it, the depth test on throughout, so the
    // blend composes outside in and a face inside a shell is never fogged by
    // it. Both windings, as the dome: the eye is inside every shell and the
    // culling keeps exactly one.
    for (std::size_t drawnShell = drawn; drawnShell > 0; --drawnShell) {
        const std::size_t shell = drawnShell - 1;
        if (alpha[shell] == 0U) {
            continue;
        }
        const float radius = kVeilRadii[shell];
        const auto base = static_cast<std::uint16_t>(mesh.vertexCount());
        for (std::size_t i = 0; i < kVeilRings; ++i) {
            const float elevation = kVeilRingDegrees[i] * (kPi / 180.0F);
            const float y = radius * std::sin(elevation);
            const float flat = radius * std::cos(elevation);
            const Rgba8 colour{ring[i].r, ring[i].g, ring[i].b, alpha[shell]};
            for (std::size_t s = 0; s < kVeilSegments; ++s) {
                const float azimuth =
                    2.0F * kPi * static_cast<float>(s) / static_cast<float>(kVeilSegments);
                pushVertex(mesh, flat * std::cos(azimuth), y, flat * std::sin(azimuth), colour);
            }
        }
        for (std::size_t i = 0; i + 1 < kVeilRings; ++i) {
            for (std::size_t s = 0; s < kVeilSegments; ++s) {
                const std::size_t next = (s + 1) % kVeilSegments;
                const auto a0 = static_cast<std::uint16_t>(base + i * kVeilSegments + s);
                const auto a1 = static_cast<std::uint16_t>(base + i * kVeilSegments + next);
                const auto b0 = static_cast<std::uint16_t>(base + (i + 1) * kVeilSegments + s);
                const auto b1 = static_cast<std::uint16_t>(base + (i + 1) * kVeilSegments + next);
                pushDoubleTriangle(mesh, a0, b0, b1);
                pushDoubleTriangle(mesh, a0, b1, a1);
            }
        }
    }
    return mesh;
}

// ---------------------------------------------------------------------------
// the world
// ---------------------------------------------------------------------------

WorldScene::WorldScene(const sim::TileQuery& tiles, const render::TileAtlas& atlas,
                       const render::LampGlow* glow, const StaticCatalogue* catalogue,
                       const std::vector<render::Lamp>* lamps)
    : tiles_(&tiles),
      atlas_(&atlas),
      glow_(glow),
      materials_(ChunkMaterials::fromAtlas(atlas)),
      catalogue_(catalogue),
      lamps_(lamps) {
    // THE SURFACE POOL: built once from the lamps, beside the law's own
    // baked field, for every surface this scene colours (lighting.hpp).
    if (lamps_ != nullptr) {
        pool_ = render::LampGlow::buildPool(tiles, *lamps_);
        hasPool_ = true;
    }
    chunksAcross_ = std::min(kChunksAcross, (tiles.sizeX() + kChunkTiles - 1) / kChunkTiles);
    chunksDown_ = (tiles.sizeY() + kChunkTiles - 1) / kChunkTiles;
    slots_.resize(static_cast<std::size_t>(chunksAcross_) * static_cast<std::size_t>(chunksDown_));
}

WorldScene::Slot& WorldScene::slot(ChunkKey key) noexcept {
    return slots_[static_cast<std::size_t>(key.cy) * static_cast<std::size_t>(chunksAcross_) +
                  static_cast<std::size_t>(key.cx)];
}

void WorldScene::build(Slot& s, ChunkKey key) {
    if (s.built) {
        ++s.rebuildCount;
        stats_.trianglesBuilt -= s.geometry.triangleCount();
        if (s.geometry.triangleCount() > 0) {
            --stats_.chunksWithGeometry;
        }
    }
    s.geometry = buildChunkGeometry(*tiles_, *atlas_, materials_, key, s.rebuildCount);
    s.built = true;
    s.stale = false;
    s.putVersion = 0;
    ++stats_.chunksBuilt;
    stats_.trianglesBuilt += s.geometry.triangleCount();
    if (s.geometry.triangleCount() > 0) {
        ++stats_.chunksWithGeometry;
    }
    stats_.anyTruncated = stats_.anyTruncated || s.geometry.truncated;

    s.minX = s.minY = s.minZ = std::numeric_limits<float>::max();
    s.maxX = s.maxY = s.maxZ = std::numeric_limits<float>::lowest();
    const std::vector<float>& p = s.geometry.positions;
    for (std::size_t i = 0; i + 2 < p.size(); i += 3) {
        s.minX = std::min(s.minX, p[i]);
        s.maxX = std::max(s.maxX, p[i]);
        s.minY = std::min(s.minY, p[i + 1]);
        s.maxY = std::max(s.maxY, p[i + 1]);
        s.minZ = std::min(s.minZ, p[i + 2]);
        s.maxZ = std::max(s.maxZ, p[i + 2]);
    }
}

void WorldScene::buildAll() {
    for (std::int32_t cy = 0; cy < chunksDown_; ++cy) {
        for (std::int32_t cx = 0; cx < chunksAcross_; ++cx) {
            const ChunkKey key{cx, cy};
            Slot& s = slot(key);
            if (!s.built || s.stale) {
                build(s, key);
            }
        }
    }
    if (!piecesPlaced_) {
        placePieces();
    }
}

void WorldScene::placePieces() {
    piecesPlaced_ = true;
    placements_ = StaticPlacements{};
    litTints_.clear();
    litVersion_ = 0;
    if (catalogue_ == nullptr || catalogue_->empty()) {
        stats_.piecesPlaced = 0;
        return;
    }
    static const std::vector<render::Lamp> kNoLamps;
    placements_ = placeStaticPieces(*tiles_, *catalogue_, lamps_ != nullptr ? *lamps_ : kNoLamps);
    stats_.piecesPlaced = placements_.placements.size();
}

namespace {

/// THE CANDLE'S OWN STRENGTH (kCandleSpread): by the house's lot, or per
/// pane on a house lit all night. The pane's tint and its spill both scale
/// by it, so a two-thirds candle lays a two-thirds spill.
[[nodiscard]] float candleStrengthOf(const StaticPlacement& p, float hour) noexcept {
    const std::uint32_t draw =
        p.house == HouseKind::Lit
            ? render::variantMix(static_cast<std::uint32_t>(p.insideX) * 73856093U ^
                                 static_cast<std::uint32_t>(p.insideY) * 19349663U ^
                                 static_cast<std::uint32_t>(p.insideZ) * 83492791U ^ 0x9E3779B9U)
            : p.houseLot;
    const float own = 1.0F - kCandleSpread * static_cast<float>((draw >> 4) & 0xFFU) / 255.0F;
    if (p.house != HouseKind::Lit) {
        return own;
    }
    return own * (1.0F - (1.0F - kLitHouseSmallHours) * smallHoursAt(hour));
}

}  // namespace

bool WorldScene::paneUpUnder(const StaticPlacement& p, const ChunkLighting& lighting, bool night,
                             float hour) const noexcept {
    if (!p.hasInside || !night || catalogue_ == nullptr) {
        return false;
    }
    const render::Rgb baked = glow_ != nullptr ? glow_->at(p.insideX, p.insideY, p.insideZ) : render::Rgb{};
    const bool hasDynamic = lighting.dynamicLamps != nullptr && !lighting.dynamicLamps->empty();
    const render::Rgb live = hasDynamic ? render::dynamicGlowAt(*lighting.dynamicLamps, p.insideX,
                                                                  p.insideY, p.insideZ)
                                        : render::Rgb{};
    const float room = std::max({baked.r, baked.g, baked.b, live.r, live.g, live.b});
    return room > kPaneLitAbove || paneGlows(p.house, p.houseLot, hour, catalogue_->knobs());
}

void WorldScene::buildSpill(const ChunkLighting& lighting) {
    spill_.clear();
    if (catalogue_ == nullptr) {
        return;
    }
    const render::SkyState sky = render::skyAt(lighting.timeOfDaySeconds, lighting.weather);
    const bool night = sky.daylight < catalogue_->knobs().paneDuskBelow;
    if (!night) {
        return;
    }
    const int second = ((lighting.timeOfDaySeconds % 86400) + 86400) % 86400;
    const float hour = static_cast<float>(second) / 3600.0F;
    float strength = 1.0F;
    const auto add = [this, &strength](std::int32_t x, std::int32_t y, std::int32_t z, float share) {
        render::Rgb& at = spill_[cellKey(x, y, z)];
        at.r = std::max(at.r, kSpillGlow.r * share * strength);
        at.g = std::max(at.g, kSpillGlow.g * share * strength);
        at.b = std::max(at.b, kSpillGlow.b * share * strength);
    };
    for (const StaticPlacement& p : placements_.placements) {
        // The panes themselves, once each: the kit window wall and the
        // timber pane. Frames and board halves carry the same household
        // but are not the light.
        if (p.role != PieceRole::WallWindow && p.role != PieceRole::PaneTimber) {
            continue;
        }
        if (!paneUpUnder(p, lighting, night, hour)) {
            continue;
        }
        strength = candleStrengthOf(p, hour);
        // The pane's own wall cell; along the wall (the tangent is the
        // normal turned) three tiles easing off; out into the street (the
        // normal) three rows, each easing off along it.
        const std::int32_t nx = p.paneX - p.insideX;
        const std::int32_t ny = p.paneY - p.insideY;
        const std::int32_t tx = -ny;
        const std::int32_t ty = nx;
        const auto at = [&](std::int32_t along, std::int32_t out, float share) {
            add(p.paneX + tx * along + nx * out, p.paneY + ty * along + ny * out, p.insideZ, share);
        };
        at(0, 0, 1.0F);
        for (std::int32_t side = -1; side <= 1; side += 2) {
            at(side, 0, kSpillSide1);
            at(2 * side, 0, kSpillSide2);
            at(3 * side, 0, kSpillSide3);
            at(side, 1, kSpillOut1Side1);
            at(2 * side, 1, kSpillOut1Side2);
            at(side, 2, kSpillOut2Side1);
            at(2 * side, 2, kSpillOut2Side2);
            at(side, 3, kSpillOut3Side1);
        }
        at(0, 1, kSpillOut1);
        at(0, 2, kSpillOut2);
        at(0, 3, kSpillOut3);
    }
}

void WorldScene::relightPieces(const ChunkLighting& lighting) {
    // Without a catalogue nothing was placed and there is nothing to
    // relight (refresh() never asks; this keeps the knobs below honest).
    if (catalogue_ == nullptr) {
        litTints_.clear();
        return;
    }
    // The same surface light the chunk colour stage computes for a cell --
    // ambient + the lamps' pool -- times the piece's facing factor, clamped
    // a little over one so a piece in a lamp's pool is lit rather than
    // blown out, folded into the unlit catalogue tint.
    const render::SkyState sky = render::skyAt(lighting.timeOfDaySeconds, lighting.weather);
    const bool hasDynamic = lighting.dynamicLamps != nullptr && !lighting.dynamicLamps->empty();
    litTints_.resize(placements_.placements.size() * kLitSlots);
    // THE LAW'S LIGHT at a cell: the baked field and the handful of live
    // lamps, the number "does the room behind this pane glow?" reads.
    const auto glowAt = [&](std::int32_t x, std::int32_t y, std::int32_t z) {
        const render::Rgb baked = glow_ != nullptr ? glow_->at(x, y, z) : render::Rgb{};
        const render::Rgb live =
            hasDynamic ? render::dynamicGlowAt(*lighting.dynamicLamps, x, y, z) : render::Rgb{};
        return render::Rgb{std::max(baked.r, live.r), std::max(baked.g, live.g),
                           std::max(baked.b, live.b)};
    };
    // THE SURFACE'S LIGHT at a cell: the pool field where the scene built
    // one (lighting.hpp, THE SURFACE POOL -- a hot core and a skirt to eight
    // tiles) and, without one, the law's own glow shaped by pooledGlow() as
    // it always was. What every piece is coloured with.
    const auto spillAt = [&](std::int32_t x, std::int32_t y, std::int32_t z) {
        if (spill_.empty()) {
            return render::Rgb{};
        }
        const auto found = spill_.find(cellKey(x, y, z));
        return found != spill_.end() ? found->second : render::Rgb{};
    };
    // The lamps' pool alone at a cell, guttering with the wind as the halos
    // do (SkyState::haloScale, 1 in still air).
    const auto poolAt = [&](std::int32_t x, std::int32_t y, std::int32_t z) {
        render::Rgb pool;
        if (!hasPool_) {
            pool = render::pooledGlow(glowAt(x, y, z));
        } else {
            const render::Rgb baked = pool_.at(x, y, z);
            const render::Rgb live =
                hasDynamic ? render::dynamicPoolAt(*lighting.dynamicLamps, x, y, z) : render::Rgb{};
            pool = render::Rgb{std::max(baked.r, live.r), std::max(baked.g, live.g),
                               std::max(baked.b, live.b)};
        }
        return render::Rgb{pool.r * sky.haloScale, pool.g * sky.haloScale, pool.b * sky.haloScale};
    };
    const auto surfaceAt = [&](std::int32_t x, std::int32_t y, std::int32_t z) {
        // THE SPILL: a lit pane's candle on the cells round it, the same
        // number the chunk colour stage adds (buildSpill); a candle behind
        // glass does not gutter with the wind.
        const render::Rgb pool = poolAt(x, y, z);
        const render::Rgb candle = spillAt(x, y, z);
        return render::Rgb{pool.r + candle.r, pool.g + candle.g, pool.b + candle.b};
    };
    // The surface light AT A CUT between two cell centres, `t` of the way
    // from the first to the second, so the two pieces meeting there share
    // one value.
    const auto surfaceBetween = [&](std::int32_t ax, std::int32_t ay, std::int32_t bx,
                                    std::int32_t by, float t, std::int32_t z) {
        const render::Rgb a = surfaceAt(ax, ay, z);
        const render::Rgb b = surfaceAt(bx, by, z);
        return render::Rgb{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
    };
    // The surface light at a grid CORNER POINT (px, py): the average of
    // the four cells that meet there.
    const auto surfaceAtCorner = [&](std::int32_t px, std::int32_t py, std::int32_t z) {
        render::Rgb sum{};
        for (std::int32_t dy = -1; dy <= 0; ++dy) {
            for (std::int32_t dx = -1; dx <= 0; ++dx) {
                const render::Rgb g = surfaceAt(px + dx, py + dy, z);
                sum.r += g.r;
                sum.g += g.g;
                sum.b += g.b;
            }
        }
        return render::Rgb{sum.r * 0.25F, sum.g * 0.25F, sum.b * 0.25F};
    };
    const RuleKnobs& knobs = catalogue_->knobs();
    const Rgba8 litPaneTint = knobs.litPane;
    const bool night = sky.daylight < knobs.paneDuskBelow;
    // The hour with its minutes, for the households' bedtimes: the relight
    // runs on the minute (chunkVersion()), so a house goes dark on the
    // minute its lot names.
    const int second = ((lighting.timeOfDaySeconds % 86400) + 86400) % 86400;
    const float hour = static_cast<float>(second) / 3600.0F;
    for (std::size_t i = 0; i < placements_.placements.size(); ++i) {
        const StaticPlacement& p = placements_.placements[i];
        Rgba8* slots = &litTints_[i * kLitSlots];
        // A window whose room is lit after dark shows it: the pane goes
        // warm and bright, its own light -- lit by a lamp that reaches the
        // room, or because the light law has the household up at this
        // hour (the whole room's lot, so a house's windows agree). Decided
        // first, because the boards either side of the pane read it too.
        //
        // Named `paneUp`, not `lit`: `lit` below is the per-corner light
        // lambda every branch uses.
        const bool paneUp = paneUpUnder(p, lighting, night, hour);
        // The surface's own light (already the pool's shape) plus any
        // emissive `extra` the piece carries, under the clamp, into the
        // catalogue tint.
        const auto litWith = [&p, &sky](const render::Rgb& pool, const render::Rgb& extra,
                                        const Rgba8& tint, float cap) {
            // Clamped by the brightest channel (render::clampLight), so the
            // light keeps its hue at saturation.
            const render::Rgb light = render::clampLight(
                render::Rgb{sky.ambient.r + pool.r + extra.r, sky.ambient.g + pool.g + extra.g,
                            sky.ambient.b + pool.b + extra.b},
                cap);
            // The colour itself is scaled as one where it would clip at 255
            // (the auditor's third: a clamp at 1.15 followed by a clip at
            // 1.0 per channel undid the hue in the brightest band).
            const render::Rgb colour = render::clampLight(
                render::Rgb{light.r * p.facing * static_cast<float>(tint.r),
                            light.g * p.facing * static_cast<float>(tint.g),
                            light.b * p.facing * static_cast<float>(tint.b)},
                255.0F);
            const auto ch = [](float c) { return static_cast<std::uint8_t>(std::clamp(c, 0.0F, 255.0F) + 0.5F); };
            return Rgba8{ch(colour.r), ch(colour.g), ch(colour.b), tint.a};
        };
        const auto lit = [&litWith](const render::Rgb& pool, const Rgba8& tint) {
            return litWith(pool, render::Rgb{}, tint, kLightClamp);
        };
        if (p.selfLit) {
            // A lamp is its own light. A flame's halo (a translucent quad)
            // fades with the daylight: full at night, a third at noon --
            // and WEATHER takes its share off it when the wind is up
            // (SkyState::haloScale, 1 in still air).
            Rgba8 own = p.instance.tint;
            if (p.role == PieceRole::Flame) {
                const float glowAlpha = static_cast<float>(own.a) * (kFlameDayAlpha + (1.0F - kFlameDayAlpha) *
                                                                                      (1.0F - sky.daylight)) *
                                        sky.haloScale;
                own.a = static_cast<std::uint8_t>(std::clamp(glowAlpha, 0.0F, 255.0F) + 0.5F);
            }
            // Only a lamp whose glass the catalogue cuts out (StaticPieceRef
            // glassBand, applyGlass) has a glass to out-value its metal: a
            // lamp without one keeps its whole tint, as it always did.
            const bool cutGlass = p.instance.piece < catalogue_->pieces().size() &&
                                  catalogue_->pieces()[p.instance.piece].glassBandHi >
                                      catalogue_->pieces()[p.instance.piece].glassBandLo;
            if (night && hasLampGlass(p.role) && cutGlass) {
                // THE LANTERN'S METAL AFTER DARK (kLampMetalNightCap): lit
                // by the sky and its own pool like any prop, and capped,
                // so the cap never out-values the glass.
                const Rgba8 metal = litWith(surfaceAt(p.lightX, p.lightY, p.lightZ), render::Rgb{},
                                            own, kLampMetalNightCap);
                slots[0] = slots[1] = slots[2] = slots[3] = metal;
            } else {
                slots[0] = slots[1] = slots[2] = slots[3] = own;
            }
            // The glass, hot after dark and plain by day (kLampGlassNight).
            slots[4] = night && hasLampGlass(p.role) ? kLampGlassNight : darkPane(own);
        } else if (p.gradient) {
            // A run piece: lit at each end AT THE BOUNDARY, blended across
            // by the adapter. The piece's local X runs from the a0 end
            // unless it was turned around to show its other finish.
            //
            // THE SPILL rides in surfaceAt() now, on the cells round a lit
            // pane, so a board half cut at a hung window (StaticPlacement::
            // spill) is hottest at the pane's end and shares the value at
            // the module's cut with the module beyond it -- the light runs
            // on across the seam instead of stopping at a plank.
            const Rgba8 a = lit(surfaceBetween(p.lightX, p.lightY, p.endAX, p.endAY, p.endAT, p.lightZ),
                                p.instance.tint);
            const Rgba8 b = lit(surfaceBetween(p.lightX2, p.lightY2, p.endBX, p.endBY, p.endBT, p.lightZ),
                                p.instance.tint);
            if (p.alongZ) {
                // The blend runs along the piece's Z: its near end (z = 0)
                // first.
                slots[0] = slots[1] = p.flipped ? b : a;
                slots[2] = slots[3] = p.flipped ? a : b;
            } else {
                slots[0] = slots[2] = p.flipped ? b : a;
                slots[1] = slots[3] = p.flipped ? a : b;
            }
            slots[4] = darkPane(slots[0]);
        } else if (p.bilinear) {
            // A block: its four corner points, in the piece's own order.
            slots[0] = lit(surfaceAtCorner(p.lightX, p.lightY, p.lightZ), p.instance.tint);
            slots[1] = lit(surfaceAtCorner(p.lightX2, p.lightY2, p.lightZ), p.instance.tint);
            slots[2] = lit(surfaceAtCorner(p.endAX, p.endAY, p.lightZ), p.instance.tint);
            slots[3] = lit(surfaceAtCorner(p.endBX, p.endBY, p.lightZ), p.instance.tint);
            slots[4] = darkPane(slots[0]);
        } else {
            // A point piece, or a flat block: averaged over the cells it
            // covers.
            const std::int32_t x0 = std::min(p.lightX, p.lightX2);
            const std::int32_t x1 = std::max(p.lightX, p.lightX2);
            const std::int32_t y0 = std::min(p.lightY, p.lightY2);
            const std::int32_t y1 = std::max(p.lightY, p.lightY2);
            render::Rgb sum{};
            int count = 0;
            for (std::int32_t y = y0; y <= y1; ++y) {
                for (std::int32_t x = x0; x <= x1; ++x) {
                    const render::Rgb g = surfaceAt(x, y, p.lightZ);
                    sum.r += g.r;
                    sum.g += g.g;
                    sum.b += g.b;
                    ++count;
                }
            }
            const float inv = 1.0F / static_cast<float>(std::max(1, count));
            const Rgba8 flat = lit(render::Rgb{sum.r * inv, sum.g * inv, sum.b * inv}, p.instance.tint);
            slots[0] = slots[1] = slots[2] = slots[3] = flat;
            slots[4] = darkPane(flat);
        }
        if (paneUp && !p.selfLit) {
            // The candle's own strength (candleStrengthOf): by the house's
            // lot, or per pane on a house lit all night.
            const float strength = candleStrengthOf(p, hour);
            const auto ch = [strength](std::uint8_t c) {
                return static_cast<std::uint8_t>(std::clamp(static_cast<float>(c) * strength, 0.0F, 255.0F) +
                                                 0.5F);
            };
            slots[4] = Rgba8{ch(litPaneTint.r), ch(litPaneTint.g), ch(litPaneTint.b), litPaneTint.a};
        }
        // The pane quad in a hung timber frame IS its pane: the adapter
        // draws paneRampMesh() in the pane tint over its span (kDrawPaneRamp,
        // scene.hpp), hot at the sill and easing to the head -- so the
        // whole quad wears what a glass pane would, dark by day, the law's
        // own candle when the room is up. The corner slots carry the same,
        // for the hash and for any adapter without the ramp.
        if (p.role == PieceRole::PaneTimber) {
            slots[0] = slots[1] = slots[2] = slots[3] = slots[4];
        }
        // THE CRITIC'S THIRD: no spill. A hung window's own timber frame
        // carries its pane's household (static_pieces.cpp), so a lit pane
        // warms its own reveal instead of standing as dark as a wall five
        // tiles off. Mixed toward the law's own warm tint, not replaced by
        // it, so the wood still reads as wood and not as a second pane.
        // The boards past the frame take the spill above.
        if (p.role == PieceRole::WindowTimber && paneUp) {
            const auto warmed = [&](const Rgba8& tint) {
                const auto mix = [](std::uint8_t c, std::uint8_t hot) {
                    const float v = static_cast<float>(c) +
                                    (static_cast<float>(hot) - static_cast<float>(c)) * kFrameWarmMix;
                    return static_cast<std::uint8_t>(std::clamp(v, 0.0F, 255.0F) + 0.5F);
                };
                return Rgba8{mix(tint.r, litPaneTint.r), mix(tint.g, litPaneTint.g),
                            mix(tint.b, litPaneTint.b), tint.a};
            };
            slots[0] = warmed(slots[0]);
            slots[1] = warmed(slots[1]);
            slots[2] = warmed(slots[2]);
            slots[3] = warmed(slots[3]);
        }
    }
    ++stats_.piecesRelit;
}

void WorldScene::invalidate(ChunkKey key) {
    if (key.cx < 0 || key.cy < 0 || key.cx >= chunksAcross_ || key.cy >= chunksDown_) {
        return;
    }
    slot(key).stale = true;
}

void WorldScene::refresh(SceneDescription& scene, const render::Camera& camera, float aspect,
                         const WorldSceneParams& params) {
    buildAll();

    ChunkLighting lighting;
    lighting.timeOfDaySeconds = params.timeOfDaySeconds;
    lighting.weather = params.weather;
    lighting.glow = glow_;
    lighting.dynamicLamps = &params.dynamicLamps;
    lighting.lampKey = dynamicLampKey(params.dynamicLamps);
    lighting.pool = pool();

    // The atlas, once per scene.
    bool haveTexture = false;
    for (const TextureData& texture : scene.textures) {
        if (texture.id == kChunkAtlasTextureId && texture.version == materials_.texture().version) {
            haveTexture = true;
            break;
        }
    }
    if (!haveTexture) {
        scene.putTexture(materials_.texture());
    }

    // The sky, recoloured with the minute -- under the weather. In a live
    // session the weather is a function of the minute AND the day, so its
    // own key rides the version above the minute (zero for clear: a clear
    // dome's version is what it always was), and a day skipped at the same
    // minute rebuilds the dome and the veil rather than keeping them.
    const std::uint32_t skyVersion =
        skyDomeVersion(params.timeOfDaySeconds) ^ (weatherVersionKey(params.weather) << 16);
    const MeshData* sky = scene.findMesh(kSkyMeshId);
    if (sky == nullptr || sky->version != skyVersion) {
        MeshData dome = buildSkyDome(params.timeOfDaySeconds, params.weather);
        dome.version = skyVersion;
        scene.putMesh(std::move(dome));
    }
    // WEATHER: the veil, on the same bucket. Built when the weather adds fog
    // over the clear day, dropped from the description the moment it does
    // not, so a clear frame's description is exactly a clear frame's.
    const render::SkyState skyState = render::skyAt(params.timeOfDaySeconds, params.weather);
    scene.veils.clear();
    bool veiled = false;
    if (skyState.veil > 0.0F) {
        const MeshData* veil = scene.findMesh(kVeilMeshId);
        if (veil == nullptr || veil->version != skyVersion) {
            scene.putMesh(buildVeil(skyState, skyVersion));
            veil = scene.findMesh(kVeilMeshId);
        }
        // A veil too thin for a single shell to round to a step of alpha
        // is no veil: an empty mesh is never uploaded, and an instance of
        // it would draw whatever the adapter last had under the id.
        veiled = veil != nullptr && veil->triangleCount() > 0;
    }
    if (!veiled) {
        scene.removeMesh(kVeilMeshId);
    }

    // THE SPILL MAP, before the chunks: the lighting bucket that moves the
    // relight moves it, and the chunk colour stage reads it under the same
    // version, so a pane that goes dark on the minute takes its candle off
    // the wall on the same minute.
    if (catalogue_ != nullptr && !placements_.placements.empty()) {
        const std::uint32_t litVersion = chunkVersion(0, lighting);
        if (litVersion != litVersion_ ||
            litTints_.size() != placements_.placements.size() * kLitSlots) {
            buildSpill(lighting);
        }
    }
    lighting.spill = &spill_;

    // The chunks: recoloured when their version moved, instanced when near.
    const Vec3 eye = toScene(camera.x, camera.y, camera.z);
    const float reach = std::max(0.0F, params.maxDistance);
    const float reachSq = reach * reach;
    scene.instances.clear();

    Instance skyAt;
    skyAt.meshId = kSkyMeshId;
    skyAt.position = eye;
    scene.instances.push_back(skyAt);
    if (veiled) {
        // The veil rides the eye exactly as the dome does, and is drawn last.
        Instance veilAt;
        veilAt.meshId = kVeilMeshId;
        veilAt.position = eye;
        scene.veils.push_back(veilAt);
    }

    stats_.chunksInstanced = 0;
    for (std::int32_t cy = 0; cy < chunksDown_; ++cy) {
        for (std::int32_t cx = 0; cx < chunksAcross_; ++cx) {
            const ChunkKey key{cx, cy};
            Slot& s = slot(key);
            if (s.geometry.triangleCount() == 0) {
                continue;
            }
            const std::uint32_t version = chunkVersion(s.rebuildCount, lighting);
            const std::uint32_t id = chunkMeshId(key);
            const MeshData* have = scene.findMesh(id);
            if (have == nullptr || have->version != version) {
                scene.putMesh(colourChunk(s.geometry, lighting));
                ++stats_.meshesRecoloured;
            }
            if (boxDistanceSq(eye.x, eye.z, s.minX, s.minZ, s.maxX, s.maxZ) > reachSq) {
                continue;
            }
            Instance at;
            at.meshId = id;
            at.textureId = kChunkAtlasTextureId;
            scene.instances.push_back(at);
            ++stats_.chunksInstanced;
        }
    }

    // The static pieces: relit when the lighting bucket moved, described
    // when inside their role's reach AND inside the frustum (a piece
    // behind the eye or off the sides is not described). The table rides
    // with them.
    scene.camera = cameraFrom(camera, aspect);
    scene.statics.clear();
    stats_.piecesInstanced = 0;
    stats_.piecesInReach = 0;
    if (catalogue_ != nullptr && !placements_.placements.empty()) {
        const std::uint32_t litVersion = chunkVersion(0, lighting);
        if (litVersion != litVersion_ ||
            litTints_.size() != placements_.placements.size() * kLitSlots) {
            relightPieces(lighting);
            litVersion_ = litVersion;
        }
        scene.pieces = catalogue_->pieceRefs();
        const std::vector<PieceSpec>& specs = catalogue_->pieces();
        const Frustum frustum = Frustum::of(scene.camera, aspect);
        for (std::size_t i = 0; i < placements_.placements.size(); ++i) {
            const StaticPlacement& p = placements_.placements[i];
            const float roleReach =
                p.instance.piece < specs.size() ? specs[p.instance.piece].maxDistance : reach;
            // Measured to the piece's nearest reach, not its origin, so a
            // wide block (a water plane, a floor) is not dropped while it
            // still runs under the eye.
            const float limit = std::min(reach, roleReach) + p.radius;
            const float dx = p.instance.position.x - eye.x;
            const float dz = p.instance.position.z - eye.z;
            if (dx * dx + dz * dz > limit * limit) {
                continue;
            }
            ++stats_.piecesInReach;
            if (!frustum.sees(p.instance.position, p.radius)) {
                continue;
            }
            StaticInstance at = p.instance;
            const Rgba8* slots = &litTints_[i * kLitSlots];
            at.tint = slots[0];
            at.tint2 = slots[1];
            at.tint3 = slots[2];
            at.tint4 = slots[3];
            at.pane = slots[4];
            at.mode = p.mode;
            // BRIGHT GLASS THROUGH A FOG: a lit pane's or a lamp's glass is a
            // light, and a light is seen further into a fog than the wall
            // round it. The pane's alpha carries the fog's share of it by
            // the eye's distance (kHaloFogShare, the halo's own rule); the
            // adapter draws such glass once more past the veils at that
            // alpha (rl_backend.cpp, lateGlass). 255 in clear air.
            // A light: a candle behind glass at any strength, or a lamp's
            // glass -- red well over blue, which dark glass (darkPane, a
            // blue lean) never is (the round-three auditor's second: a sum
            // over 450 let two thirds of the candles through the fog as
            // dark panes).
            if (skyState.veil > 0.0F && at.pane.r >= 80 && at.pane.r > at.pane.b + 40) {
                const float gx = p.instance.position.x - eye.x;
                const float gy = p.instance.position.y - eye.y;
                const float gz = p.instance.position.z - eye.z;
                const float glassLen = std::sqrt(gx * gx + gy * gy + gz * gz);
                const float share = std::exp(-kHaloFogShare * skyState.veil * glassLen);
                at.pane.a = static_cast<std::uint8_t>(std::clamp(share * 255.0F, 1.0F, 255.0F) + 0.5F);
            }
            if (p.billboard && p.instance.piece < specs.size()) {
                // A halo faces the eye IN THREE DIMENSIONS, a sphere's
                // billboard: its quad's normal (local +Z) along the whole
                // line to the eye, not just its shadow on the ground. The
                // yaw first -- a clockwise yaw takes +Z to (-sin, cos) in
                // XZ -- then the pitch about the quad's own X (the adapter
                // applies it before the yaw), which tips the normal down
                // to an eye under the lamp and up to one on a roof. A
                // yaw-only turn left the quad standing plumb, so a body
                // under a lantern looking up saw it foreshortened to a
                // bar and, from the roof, to a bright sliver: this is what
                // the placement critic called the halo's edge at arm's
                // length. The origin -- the bottom-left corner of a quad
                // `w` wide and `h` tall -- is half a width back along its
                // own +X from the anchor and half a height down its own
                // +Y, both turned by the pitch and the yaw, so the centre
                // holds on the anchor's ray whichever way the quad tips.
                // The centre itself floats toward the eye along that ray
                // (kHaloForward), clear of the lamp's own body. Facing the
                // eye, the disc lies across the ray and reaches its own
                // radius (0.55 m on a lantern's 1.1 m halo) from the
                // centre, which is wider than the 0.42 m the flame stands
                // off its wall -- so seen from ALONG the wall the far arc
                // does sink into the plaster and the depth test cuts it.
                // That is why the fan's alpha is zero at the rim
                // (haloFanMesh): the cut falls where there is nothing left
                // to cut. Two-sided, so which way along the line is all
                // one.
                const PieceSpec& spec = specs[p.instance.piece];
                const float dx = eye.x - p.anchor.x;
                const float dy = eye.y - p.anchor.y;
                const float dz = eye.z - p.anchor.z;
                const float flat = std::sqrt(dx * dx + dz * dz);
                const float len = std::sqrt(flat * flat + dy * dy);
                // THE HALO UP CLOSE: its alpha by the eye's distance.
                const float nearT = std::clamp((len - kHaloNearGone) / (kHaloNearFull - kHaloNearGone),
                                               0.0F, 1.0F);
                const float near = nearT * nearT * nearT;
                const float fade = (kHaloNearFloor + (1.0F - kHaloNearFloor) * near) *
                                   std::exp(-kHaloFogShare * skyState.veil * len);
                const auto faded = [fade](Rgba8 c) {
                    c.a = static_cast<std::uint8_t>(std::clamp(static_cast<float>(c.a) * fade, 0.0F, 255.0F) +
                                                    0.5F);
                    return c;
                };
                at.tint = faded(at.tint);
                at.tint2 = faded(at.tint2);
                at.tint3 = faded(at.tint3);
                at.tint4 = faded(at.tint4);
                const float yaw = flat > 1.0e-3F ? std::atan2(-dx, dz) : 0.0F;
                // +Z pitched by `pitch` is (0, -sin, cos): positive tips the
                // normal down, toward an eye below the anchor.
                const float pitch = std::atan2(-dy, flat);
                const float w = spec.width * at.scale.x;
                const float h = spec.height * at.scale.y;
                const float c = std::cos(yaw);
                const float s = std::sin(yaw);
                const float cp = std::cos(pitch);
                const float sp = std::sin(pitch);
                // The float toward the eye, as a fraction of the way there.
                const float forward =
                    len > 1.0e-3F ? std::min(kHaloForward, kHaloForwardCap * len) / len : 0.0F;
                const Vec3 centre{p.anchor.x + dx * forward, p.anchor.y + dy * forward,
                                  p.anchor.z + dz * forward};
                at.yaw = yaw < 0.0F ? yaw + 2.0F * kPi : yaw;
                at.pitch = pitch;
                // The half-height step along the quad's own +Y, which the
                // pitch takes to (0, cos, sin) and the yaw then turns.
                at.position = Vec3{centre.x - 0.5F * w * c + 0.5F * h * sp * s,
                                   centre.y - 0.5F * h * cp + spec.lift,
                                   centre.z - 0.5F * w * s - 0.5F * h * sp * c};
            }
            scene.statics.push_back(at);
            ++stats_.piecesInstanced;
        }
    } else {
        scene.pieces.clear();
    }

    scene.clearColour = rgba(skyState.skyHorizon);
}

}  // namespace granadad::render3d
