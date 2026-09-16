#include "granadad/render3d/ground_items.hpp"

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

/// The actor rule's light, for a thing lying where a body would stand.
[[nodiscard]] Rgba8 tintFor(const render::SkyState& sky, const render::Rgb& baked,
                            const render::Rgb& dynamic) noexcept {
    const auto ch = [](float ambient, float b, float d) {
        const float light = std::min(1.15F, ambient + std::max(b, d));
        return static_cast<std::uint8_t>(std::clamp(light * 255.0F, 0.0F, 255.0F));
    };
    return Rgba8{ch(sky.ambient.r, baked.r, dynamic.r), ch(sky.ambient.g, baked.g, dynamic.g),
                 ch(sky.ambient.b, baked.b, dynamic.b), 255};
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

void describe(std::vector<StaticInstance>& out, const StaticCatalogue& catalogue,
              const Placed& at, const render::Session& session, const render::SkyState& sky,
              const std::vector<render::Lamp>& live, const render::Camera& view,
              float maxDistance) {
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
    // A second thing on the same tile sits a hand's width off the first, so
    // a knife and a purse read as two things and not one silhouette.
    const float nudge = static_cast<float>(at.nth) * 0.22F;
    piece.position = toScene(px + nudge, py - nudge * 0.5F,
                             render::bandSurface(at.band) + spec->lift);
    piece.yaw = tileYaw(at.x, at.y, at.nth) + spec->yawOffset;
    piece.pitch = spec->pitch;
    piece.scale = Vec3{spec->scale, spec->scale, spec->scale};
    const Rgba8 light = spec->selfLit
                            ? Rgba8{255, 255, 255, 255}
                            : tintFor(sky, session.renderer().glow().at(at.x, at.y, at.band),
                                      render::dynamicGlowAt(live, at.x, at.y, at.band));
    piece.tint = mulTint(spec->tint, light);
    piece.tint2 = piece.tint;
    piece.tint3 = piece.tint;
    piece.tint4 = piece.tint;
    piece.mode = spec->selfLit ? kDrawPlain : kDrawShaded;
    out.push_back(piece);
}

}  // namespace

std::vector<StaticInstance> groundItemInstances(const render::Session& session,
                                                const StaticCatalogue& catalogue,
                                                const render::Camera& view, float maxDistance) {
    std::vector<StaticInstance> out;
    if (catalogue.itemIds().empty()) {
        return out;
    }
    const sim::Tavern& tavern = session.tavern();
    const sim::ItemRegistry& items = tavern.items();
    // WEATHER: the session's own sky, as the chunks and the people read it.
    const render::SkyState sky = session.sky();
    const std::vector<render::Lamp> live = session.tavernLights();

    // --- the room's list: stands and drops, in its own order -------------
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
    for (const Placed& at : placed) {
        describe(out, catalogue, at, session, sky, live, view, maxDistance);
    }
    return out;
}

}  // namespace granadad::render3d
