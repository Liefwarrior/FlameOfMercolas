#include "granadad/render3d/ground_items.hpp"

#include "granadad/render3d/chunk_mesher.hpp"
#include "granadad/render3d/world_scene.hpp"

#include <algorithm>
#include <cmath>

#include "granadad/render/lighting.hpp"
#include "granadad/render/session.hpp"
#include "granadad/render/vertical.hpp"
#include "granadad/render/world_renderer.hpp"
#include "granadad/sim/items.hpp"
#include "granadad/sim/tavern.hpp"

namespace granadad::render3d {

namespace {

/// The actor rule's light, for a thing lying where a body would stand --
/// clamped at one, or at `cap` for a lantern's metal after dark
/// (kLampMetalNightCap, the hung lanterns' own rule).
[[nodiscard]] Rgba8 tintFor(const render::SkyState& sky, const render::Rgb& baked,
                            const render::Rgb& dynamic, float cap = 1.0F) noexcept {
    // Clamped by the brightest channel (render::clampLight): the light
    // keeps its hue at saturation.
    // A model's tint is clipped at one per channel by the draw: the clamp
    // is at one, as one colour, so the hue holds in the brightest band.
    const render::Rgb light = render::clampLight(
        render::Rgb{sky.ambient.r + std::max(baked.r, dynamic.r), sky.ambient.g + std::max(baked.g, dynamic.g),
                    sky.ambient.b + std::max(baked.b, dynamic.b)},
        cap);
    const auto ch = [](float l) {
        return static_cast<std::uint8_t>(std::clamp(l * 255.0F, 0.0F, 255.0F));
    };
    return Rgba8{ch(light.r), ch(light.g), ch(light.b), 255};
}

[[nodiscard]] Rgba8 mulTint(const Rgba8& a, const Rgba8& b) noexcept {
    const auto ch = [](std::uint8_t x, std::uint8_t y) {
        return static_cast<std::uint8_t>((static_cast<unsigned>(x) * static_cast<unsigned>(y) + 127U) /
                                         255U);
    };
    return Rgba8{ch(a.r, b.r), ch(a.g, b.g), ch(a.b, b.b), a.a};
}

[[nodiscard]] float planarDistance(const render::Camera& view, float px, float py) noexcept {
    const float dx = px - view.x;
    const float dy = py - view.y;
    return std::sqrt(dx * dx + dy * dy);
}

/// A small, deterministic turn per tile so a row of dropped knives does not
/// lie in parade order: a hash of the tile, never a draw.
[[nodiscard]] float tileYaw(std::int32_t x, std::int32_t y, std::int32_t nth) noexcept {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 0x9E3779B1U ^
                      static_cast<std::uint32_t>(y) * 0x85EBCA77U ^
                      static_cast<std::uint32_t>(nth) * 0xC2B2AE3DU;
    h ^= h >> 15;
    h *= 0x2C1B3C6DU;
    h ^= h >> 12;
    return static_cast<float>(h & 255U) * (6.28318530717958647692F / 256.0F);
}

struct Placed {
    const PieceSpec* spec = nullptr;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t band = 0;
    std::int32_t nth = 0;
};

/// Where a placed thing's origin stands in the scene: the tile's centre (a
/// second thing on the same tile a hand's width off the first, so a knife
/// and a purse read as two things and not one silhouette) at the band's
/// surface plus the row's lift.
[[nodiscard]] Vec3 originOf(const Placed& at) noexcept {
    const float px = static_cast<float>(at.x) + 0.5F;
    const float py = static_cast<float>(at.y) + 0.5F;
    const float nudge = static_cast<float>(at.nth) * 0.22F;
    return toScene(px + nudge, py - nudge * 0.5F, render::bandSurface(at.band) + at.spec->lift);
}

/// A row with glass of its own to light: self-lit, and the catalogue
/// names the glass the adapter cuts (the table lantern).
[[nodiscard]] bool litGlass(const PieceSpec& spec) noexcept {
    return spec.selfLit && spec.glassBandHi > spec.glassBandLo;
}

/// Every thing on a tile, in the room's own order, then the four
/// strongboxes, then the bale: the one list the instances and the halos
/// are both read off.
[[nodiscard]] std::vector<Placed> placedThings(const render::Session& session, const StaticCatalogue& catalogue) {
    const sim::Tavern& tavern = session.tavern();
    const sim::ItemRegistry& items = tavern.items();
    std::vector<Placed> placed;
    placed.reserve(tavern.groundItems().size() + 5);
    for (const sim::GroundItem& entry : tavern.groundItems()) {
        const sim::ItemDef* thing = items.at(entry.item);
        if (thing == nullptr) {
            continue;
        }
        Placed at;
        at.spec = catalogue.itemPiece(thing->id);
        at.x = entry.x;
        at.y = entry.y;
        at.band = entry.band;
        // The nth thing on this tile so far, for the nudge and the turn.
        for (const Placed& earlier : placed) {
            if (earlier.x == at.x && earlier.y == at.y && earlier.band == at.band) {
                ++at.nth;
            }
        }
        placed.push_back(at);
    }
    // --- the fixtures: the four strongboxes, the snug's bale ---------------
    // The box stands ON the bed cell (a solid block the room reaches across
    // from the stand tile); an emptied box is still a box.
    if (const PieceSpec* box = catalogue.itemPiece("strongbox"); box != nullptr) {
        for (std::int32_t room = 0; room < sim::gull::kRoomCount; ++room) {
            Placed at;
            at.spec = box;
            at.x = sim::gull::kRooms[room].bedX;
            at.y = sim::gull::kRooms[room].bedY;
            at.band = sim::gull::kUpperBand;
            placed.push_back(at);
        }
    }
    if (tavern.balesInSnug() > 0) {
        if (const PieceSpec* sack = catalogue.itemPiece("bale"); sack != nullptr) {
            Placed at;
            at.spec = sack;
            at.x = sim::gull::kBaleX;
            at.y = sim::gull::kBaleY;
            at.band = sim::gull::kGroundBand;
            placed.push_back(at);
        }
    }
    return placed;
}

void describe(std::vector<StaticInstance>& out, const StaticCatalogue& catalogue,
              const Placed& at, const render::Session& session, const render::SkyState& sky,
              const std::vector<render::Lamp>& live, const render::Camera& view,
              float maxDistance, const render::LampGlow* pool,
              const std::map<std::uint64_t, render::Rgb>* spill) {
    const PieceSpec* spec = at.spec;
    if (spec == nullptr) {
        return;
    }
    const float px = static_cast<float>(at.x) + 0.5F;
    const float py = static_cast<float>(at.y) + 0.5F;
    const float distance = planarDistance(view, px, py);
    if (distance > std::min(maxDistance, spec->maxDistance)) {
        return;
    }
    const int index = catalogue.pieceIndex(spec->role, spec->variant);
    if (index < 0) {
        return;
    }
    StaticInstance piece;
    piece.piece = static_cast<std::uint16_t>(index);
    piece.role = static_cast<std::uint8_t>(PieceRole::Item);
    piece.position = originOf(at);
    piece.yaw = tileYaw(at.x, at.y, at.nth) + spec->yawOffset;
    piece.pitch = spec->pitch;
    piece.scale = Vec3{spec->scale, spec->scale, spec->scale};
    // The light where the thing lies: the lamps' pool and a lit pane's
    // candle, the same numbers the cobbles under it wear; the law's own
    // glow shaped by pooledGlow() when no pool was built.
    render::Rgb ground;
    render::Rgb dyn;
    if (pool != nullptr) {
        ground = pool->at(at.x, at.y, at.band);
        ground = render::Rgb{ground.r * sky.haloScale, ground.g * sky.haloScale, ground.b * sky.haloScale};
        dyn = render::dynamicPoolAt(live, at.x, at.y, at.band);
        dyn = render::Rgb{dyn.r * sky.haloScale, dyn.g * sky.haloScale, dyn.b * sky.haloScale};
        if (spill != nullptr && !spill->empty()) {
            const auto found = spill->find(cellKey(at.x, at.y, at.band));
            if (found != spill->end()) {
                ground.r += found->second.r;
                ground.g += found->second.g;
                ground.b += found->second.b;
            }
        }
    } else {
        ground = render::pooledGlow(session.renderer().glow().at(at.x, at.y, at.band));
        dyn = render::pooledGlow(render::dynamicGlowAt(live, at.x, at.y, at.band));
    }
    // A self-lit thing is its own light -- and after dark a lantern's
    // metal is lit and capped like the hung lanterns' (kLampMetalNightCap,
    // world_scene.cpp's relight), so its glass is the brightest thing on
    // it. At its full tint the brass out-valued the glass on the Gull's
    // table: a lamp whose dullest part was its light.
    const bool night = sky.daylight < catalogue.knobs().paneDuskBelow;
    const Rgba8 light = !spec->selfLit                ? tintFor(sky, ground, dyn)
                        : (night && litGlass(*spec)) ? tintFor(sky, ground, dyn, kLampMetalNightCap)
                                                     : Rgba8{255, 255, 255, 255};
    piece.tint = mulTint(spec->tint, light);
    // A self-lit thing on the ground (the kit's lantern) has glass the
    // adapter cuts out by the same catalogue rule as the hung lantern's
    // (the row shares the file): hot after dark, dark glass by day.
    if (spec->selfLit) {
        piece.pane = night ? Rgba8{255, 240, 205, 255} : Rgba8{64, 60, 48, 255};
    }
    piece.tint2 = piece.tint;
    piece.tint3 = piece.tint;
    piece.tint4 = piece.tint;
    piece.mode = spec->selfLit ? kDrawPlain : kDrawShaded;
    out.push_back(piece);
}

}  // namespace

std::vector<StaticInstance> groundItemInstances(
    const render::Session& session, const StaticCatalogue& catalogue, const render::Camera& view,
    float maxDistance, const render::LampGlow* pool,
    const std::map<std::uint64_t, render::Rgb>* spill) {
    std::vector<StaticInstance> out;
    if (catalogue.itemIds().empty()) {
        return out;
    }
    // WEATHER: the session's own sky, as the chunks and the people read it.
    const render::SkyState sky = session.sky();
    const std::vector<render::Lamp> live = session.tavernLights();
    for (const Placed& at : placedThings(session, catalogue)) {
        describe(out, catalogue, at, session, sky, live, view, maxDistance, pool, spill);
    }
    return out;
}

std::vector<StaticInstance> groundItemHalos(const render::Session& session, const StaticCatalogue& catalogue,
                                            const render::Camera& view, float maxDistance) {
    std::vector<StaticInstance> out;
    const PieceSpec* flame = catalogue.piece(PieceRole::Flame);
    if (flame == nullptr || catalogue.itemIds().empty()) {
        return out;
    }
    const int flameIndex = catalogue.pieceIndex(PieceRole::Flame, flame->variant);
    if (flameIndex < 0) {
        return out;
    }
    // The hung lantern's scale is what kLanternFlameWidth is the halo OF:
    // a lantern at another scale wears it in proportion.
    const PieceSpec* hung = catalogue.piece(PieceRole::LampWall);
    const float hungScale = hung != nullptr && hung->scale > 0.0F ? hung->scale : 1.0F;
    const render::SkyState sky = session.sky();
    // The flame's own tint times the lanterns', alpha and all (the placer's
    // mulTint multiplies the alpha too: the hung halo's 210 is the knob's),
    // by the hour (the relight's own rule): full at night, a trace at noon.
    const Rgba8 knob = catalogue.knobs().lanternFlame;
    Rgba8 own = mulTint(flame->tint, knob);
    own.a = static_cast<std::uint8_t>(
        (static_cast<unsigned>(flame->tint.a) * static_cast<unsigned>(knob.a) + 127U) / 255U);
    const Rgba8 glow = flameGlowByHour(own, sky);
    if (glow.a == 0) {
        return out;
    }
    const Vec3 eye = toScene(view.x, view.y, view.z);
    for (const Placed& at : placedThings(session, catalogue)) {
        if (at.spec == nullptr || !litGlass(*at.spec)) {
            continue;
        }
        const float px = static_cast<float>(at.x) + 0.5F;
        const float py = static_cast<float>(at.y) + 0.5F;
        if (planarDistance(view, px, py) > std::min(maxDistance, at.spec->maxDistance)) {
            continue;
        }
        // The flame stands in the middle of its glass: the band's centre in
        // the model's own Y at the row's scale, under the origin (the hung
        // lantern's kLanternFlameDrop is the same point on a hung one).
        const Vec3 origin = originOf(at);
        const float drop = 0.5F * (at.spec->glassBandLo + at.spec->glassBandHi) * at.spec->scale;
        const Vec3 anchor{origin.x, origin.y + drop, origin.z};
        const float w = kLanternFlameWidth * at.spec->scale / hungScale;
        const float h = kLanternFlameHeight * at.spec->scale / hungScale;
        StaticInstance halo;
        halo.piece = static_cast<std::uint16_t>(flameIndex);
        halo.role = static_cast<std::uint8_t>(PieceRole::Flame);
        // flameQuad's own scale, on top of the flame row's.
        halo.scale = Vec3{w / std::max(0.01F, flame->width) * flame->scale,
                          h / std::max(0.01F, flame->height) * flame->scale, -1.0F * flame->scale};
        halo.tint = glow;
        halo.tint2 = glow;
        halo.tint3 = glow;
        halo.tint4 = glow;
        halo.mode = kDrawHalo;
        halo.gradientFrom = 0.0F;
        halo.gradientTo = flame->width;
        halo.gradientFromZ = 0.0F;
        halo.gradientToZ = flame->height;
        faceHaloToEye(halo, *flame, anchor, eye, sky.veil);
        out.push_back(halo);
    }
    return out;
}

}  // namespace granadad::render3d
