#include "granadad/render/map_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>

#include "granadad/content/lanes.hpp"
#include "granadad/render/hud.hpp"
#include "granadad/sim/angle.hpp"
#include "granadad/sim/docks_signs_generated.hpp"

namespace granadad::render {

namespace {

/// How many bands BELOW the player's own the plan still reads for water. Six
/// reaches the harbour surface (z18) from the roof-slum plane (z22) with a
/// band to spare, so "the harbour water is always shown" is true from every
/// walkable band this district has.
constexpr std::int32_t kWaterScanBands = 6;

/// How many bands below the player's own still draw as (dimmed) ground.
/// Three: from the roofs (z22) that reaches the upper and mid-slope streets;
/// from the quayside it reaches the strand under the piers and stops well
/// short of the unbuilt substrate.
constexpr std::int32_t kLowerScanBands = 3;

/// The plate-and-glyph tones every label uses. The bone is hud.cpp's own
/// kPlateBone register (the NES pop-up treatment's flat border colour); way
/// names are a step greyer than door names so the street grid reads as
/// background and the doors -- the things the owner could not locate -- read
/// as figures.
constexpr Rgb kDoorInk{0.92F, 0.89F, 0.78F};
constexpr Rgb kWayInk{0.62F, 0.62F, 0.58F};
constexpr Rgb kDoorDot{0.95F, 0.80F, 0.35F};
constexpr Rgb kPlayerTone{1.0F, 0.97F, 0.88F};
constexpr Rgb kBackdrop{0.03F, 0.028F, 0.04F};

/// Pixel padding a label's plate adds around its glyph box when two labels
/// are tested for overlap. One px: plates may abut edge to edge (each carries
/// its own 1px border, so two touching plates still read apart) but never
/// overprint.
constexpr int kLabelPad = 1;

struct PxRect {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;  // exclusive
    int y1 = 0;

    [[nodiscard]] bool overlaps(const PxRect& other) const noexcept {
        return x0 < other.x1 && other.x0 < x1 && y0 < other.y1 && other.y0 < y1;
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// classification
// ---------------------------------------------------------------------------

MapCell mapCellAt(const sim::TileQuery& tiles, std::int32_t x, std::int32_t y,
                  std::int32_t band) noexcept {
    // Masonry at the player's own band. form(), not solid(): solid() also
    // answers true for VOID, and the world's own border ring drawn as a wall
    // would put a rampart around the district that no body can walk to.
    if (tiles.form(x, y, band) == content::TileForm::Wall) {
        return MapCell::Wall;
    }
    if (tiles.walkable(x, y, band)) {
        return MapCell::Floor;
    }
    // Water before lower ground: the harbour surface at z18 read from the
    // quay (z19) must say WATER, not "there is a seabed down there somewhere".
    for (std::int32_t z = band; z >= band - kWaterScanBands; --z) {
        if (tiles.fluidDepth(x, y, z) >= sim::kBlockingFluidDepth) {
            return MapCell::Water;
        }
    }
    for (std::int32_t z = band - 1; z >= band - kLowerScanBands; --z) {
        if (tiles.walkable(x, y, z)) {
            return MapCell::LowerFloor;
        }
    }
    return MapCell::Void;
}

// ---------------------------------------------------------------------------
// the palette
// ---------------------------------------------------------------------------

MapPalette MapPalette::fromAtlas(const TileAtlas& atlas) {
    MapPalette out;
    const std::size_t materials = materialIds().size();
    out.wall.reserve(materials);
    out.floor.reserve(materials);
    for (std::size_t m = 0; m < materials; ++m) {
        const std::uint16_t material = static_cast<std::uint16_t>(m);
        // The material's OWN tones, off the same tile images the first-person
        // pass draws -- variantKey 0, which is a pure function of nothing, so
        // the plan cannot shimmer between frames. Walls sit LOW and floors
        // sit LIFTED so the ground reads light and the masonry dark whatever
        // the material's own albedo is; the material tone survives inside
        // that register, which is what makes brick and granite tell apart.
        const Rgb side = atlas.averageOf(atlas.tileFor(material, FaceKind::Side, 0));
        const Rgb top = atlas.averageOf(atlas.tileFor(material, FaceKind::FloorTop, 0));
        out.wall.push_back(side * 0.52F);
        const Rgb lifted = top * 0.85F + Rgb{0.10F, 0.10F, 0.09F};
        out.floor.push_back(Rgb{std::min(1.0F, lifted.r), std::min(1.0F, lifted.g),
                                std::min(1.0F, lifted.b)});
    }
    // The water region is material-independent in the pack; tilt its average
    // toward blue-green so depth-7 harbour reads as water even out of a pack
    // whose water tile is mostly foam.
    const Rgb wet = atlas.averageOf(atlas.tileFor(0, FaceKind::Water, 0));
    out.water = Rgb{wet.r * 0.35F, wet.g * 0.55F + 0.06F, wet.b * 0.75F + 0.12F};
    out.voidTone = atlas.voidColour();
    return out;
}

Rgb MapPalette::wallTone(std::uint16_t material) const noexcept {
    const std::size_t index = material;
    return index < wall.size() ? wall[index] : Rgb{0.22F, 0.20F, 0.20F};
}

Rgb MapPalette::floorTone(std::uint16_t material) const noexcept {
    const std::size_t index = material;
    return index < floor.size() ? floor[index] : Rgb{0.55F, 0.52F, 0.48F};
}

// ---------------------------------------------------------------------------
// frame geometry
// ---------------------------------------------------------------------------

MapBounds mapContentBounds(const sim::TileQuery& tiles) {
    MapBounds bounds;
    bounds.minX = tiles.sizeX();
    bounds.minY = tiles.sizeY();
    bounds.maxX = 0;
    bounds.maxY = 0;
    for (std::int32_t y = 0; y < tiles.sizeY(); ++y) {
        for (std::int32_t x = 0; x < tiles.sizeX(); ++x) {
            bool authored = false;
            for (std::int32_t z = 0; z < tiles.sizeZ() && !authored; ++z) {
                const content::TileForm form = tiles.form(x, y, z);
                authored = form != content::TileForm::Void;
            }
            if (!authored) {
                continue;
            }
            bounds.minX = std::min(bounds.minX, x);
            bounds.minY = std::min(bounds.minY, y);
            bounds.maxX = std::max(bounds.maxX, x);
            bounds.maxY = std::max(bounds.maxY, y);
        }
    }
    if (bounds.maxX < bounds.minX) {
        // An empty world (a unit test's ten-tile box). One tile of nothing.
        bounds = MapBounds{0, 0, 0, 0};
    }
    return bounds;
}

MapFrame mapFrameFor(int width, int height, const MapBounds& bounds) noexcept {
    MapFrame frame;
    frame.bounds = bounds;
    const int tilesX = bounds.maxX - bounds.minX + 1;
    const int tilesY = bounds.maxY - bounds.minY + 1;
    // Margins: a title row above, a hint row below, a sliver either side --
    // scaled off the same height rule the HUD's own registers use.
    const int marginY = 11 * hudScale(height);
    const int marginX = 4 * hudScale(height);
    const int availW = std::max(1, width - 2 * marginX);
    const int availH = std::max(1, height - 2 * marginY);
    frame.scale = std::max(1, std::min(availW / std::max(1, tilesX),
                                       availH / std::max(1, tilesY)));
    frame.originX = (width - tilesX * frame.scale) / 2;
    frame.originY = (height - tilesY * frame.scale) / 2;
    return frame;
}

// ---------------------------------------------------------------------------
// the declutter pass
// ---------------------------------------------------------------------------

std::vector<PlacedMapLabel> placeMapLabels(const MapFrame& frame, float playerX, float playerY,
                                           int textScale) {
    namespace signs = sim::docks;

    const int mapW = (frame.bounds.maxX - frame.bounds.minX + 1) * frame.scale;
    const int mapH = (frame.bounds.maxY - frame.bounds.minY + 1) * frame.scale;
    // Labels may lean a little past the plan's own edge -- the frame is
    // centred with margins, and a quay-edge name half in the margin is
    // better than a quay-edge name dropped.
    const PxRect mapRect{frame.originX - 10 * textScale, frame.originY - 8 * textScale,
                         frame.originX + mapW + 10 * textScale,
                         frame.originY + mapH + 8 * textScale};

    struct Candidate {
        const signs::Sign* sign = nullptr;
        float sortKey = 0.0F;
    };

    // WAY NAMES, ONE PER DISTINCT NAME. A street signed six times along its
    // reach ("Tarwalk" has nine posts) gets one label, at the post nearest
    // the cluster's own centroid -- stable as the player walks, which a
    // nearest-to-player pick would not be.
    //
    // ...AMONG THE POSTS THAT ARE NOT ON SOMEBODY'S DOORSTEP. The first gate
    // run of this file dropped "Mission of the Flame" -- the acceptance
    // sentence's own label -- because Ropewynd's centroid post stands three
    // tiles from the Mission's door, and a way label planted there walls off
    // every seat the door's own name could take. Ways place FIRST (they must
    // always land), so they choose their post politely: the candidate pool
    // is the same-named posts at least kDoorstepClearance tiles from every
    // door anchor, and only a street with no such post falls back to its
    // whole post list. Still player-independent, so the label cannot wander.
    constexpr float kDoorstepClearanceSq = 25.0F;  // 5 world tiles, squared
    const auto clearOfDoors = [](const signs::Sign& post) {
        for (std::size_t j = 0; j < signs::kSignCount; ++j) {
            const signs::Sign& door = signs::kSigns[j];
            if (door.kind != signs::SignKind::Door) {
                continue;
            }
            const float dx = post.anchorX - door.anchorX;
            const float dy = post.anchorY - door.anchorY;
            if (dx * dx + dy * dy < kDoorstepClearanceSq) {
                return false;
            }
        }
        return true;
    };
    std::vector<Candidate> ways;
    for (std::size_t i = 0; i < signs::kSignCount; ++i) {
        const signs::Sign& sign = signs::kSigns[i];
        if (sign.kind != signs::SignKind::Way) {
            continue;
        }
        bool seen = false;
        for (const Candidate& earlier : ways) {
            if (std::string_view(earlier.sign->place) == sign.place) {
                seen = true;
                break;
            }
        }
        if (seen) {
            continue;
        }
        // The centroid of every same-named post, then the post nearest it.
        float sumX = 0.0F;
        float sumY = 0.0F;
        int count = 0;
        for (std::size_t j = 0; j < signs::kSignCount; ++j) {
            const signs::Sign& other = signs::kSigns[j];
            if (other.kind == signs::SignKind::Way &&
                std::string_view(other.place) == sign.place) {
                sumX += other.anchorX;
                sumY += other.anchorY;
                ++count;
            }
        }
        const float cx = sumX / static_cast<float>(count);
        const float cy = sumY / static_cast<float>(count);
        // Two passes: the polite posts first, the whole list only if no post
        // of this street stands clear of every door.
        const signs::Sign* best = nullptr;
        for (int pass = 0; pass < 2 && best == nullptr; ++pass) {
            const bool requireClear = pass == 0;
            float bestD = 1.0e30F;
            for (std::size_t j = 0; j < signs::kSignCount; ++j) {
                const signs::Sign& other = signs::kSigns[j];
                if (other.kind != signs::SignKind::Way ||
                    std::string_view(other.place) != sign.place) {
                    continue;
                }
                if (requireClear && !clearOfDoors(other)) {
                    continue;
                }
                const float dx = other.anchorX - cx;
                const float dy = other.anchorY - cy;
                const float d = dx * dx + dy * dy;
                if (best == nullptr || d < bestD ||
                    (d == bestD && std::strcmp(other.id, best->id) < 0)) {
                    bestD = d;
                    best = &other;
                }
            }
        }
        ways.push_back(Candidate{best, 0.0F});
    }
    std::sort(ways.begin(), ways.end(), [](const Candidate& a, const Candidate& b) {
        return std::strcmp(a.sign->place, b.sign->place) < 0;
    });

    // DOOR NAMES, NEAREST TO THE PLAYER FIRST -- the door being hunted is
    // almost always the one near you -- ties broken by the sign's own id so
    // two equidistant doors place in the same order every run.
    std::vector<Candidate> doors;
    for (std::size_t i = 0; i < signs::kSignCount; ++i) {
        const signs::Sign& sign = signs::kSigns[i];
        if (sign.kind != signs::SignKind::Door) {
            continue;
        }
        const float dx = sign.anchorX - playerX;
        const float dy = sign.anchorY - playerY;
        doors.push_back(Candidate{&sign, dx * dx + dy * dy});
    }
    std::sort(doors.begin(), doors.end(), [](const Candidate& a, const Candidate& b) {
        if (a.sortKey != b.sortKey) {
            return a.sortKey < b.sortKey;
        }
        return std::strcmp(a.sign->id, b.sign->id) < 0;
    });

    std::vector<PlacedMapLabel> placed;
    std::vector<PxRect> taken;
    placed.reserve(ways.size() + doors.size());

    const auto tryPlace = [&](const signs::Sign& sign, bool way) {
        const std::string text(sign.place);
        const int w = textWidth(text, textScale);
        const int h = 6 * textScale;
        const int ax = frame.pxOfX(sign.anchorX);
        const int ay = frame.pxOfY(sign.anchorY);
        // Twenty-four positions around the anchor, tried in order. RING ONE:
        // centred above first (a nameplate over a door is where the eye
        // expects one), then centred below, the two sides, and the four
        // corner-leaning variants that let a label slide sideways out of a
        // crowded row. RING TWO steps the same vertical seats one label-row
        // further out, and RING THREE slides the mid-row seats laterally --
        // both added by the first gate run of this file, which proved eight
        // seats are not enough for the Ropewynd strip: 83 signs into one
        // frame is a packing problem, and the extra seats are what keep that
        // dense row from dropping the very door the owner could not find
        // (the door DOT still marks the exact entrance a displaced label
        // names).
        const int step = h + 3;
        const int candidates[24][2] = {
            // ring one -- tight around the anchor
            {ax - w / 2, ay - h - 2},
            {ax - w / 2, ay + 3},
            {ax + 3, ay - h / 2},
            {ax - w - 3, ay - h / 2},
            {ax + 2, ay - h - 2},
            {ax - w - 2, ay - h - 2},
            {ax + 2, ay + 3},
            {ax - w - 2, ay + 3},
            // ring two -- one label-row further up or down
            {ax - w / 2, ay - h - 2 - step},
            {ax - w / 2, ay + 3 + step},
            {ax + 2, ay - h - 2 - step},
            {ax - w - 2, ay - h - 2 - step},
            {ax + 2, ay + 3 + step},
            {ax - w - 2, ay + 3 + step},
            {ax + 3, ay - h / 2 - step},
            {ax + 3, ay - h / 2 + step},
            {ax - w - 3, ay - h / 2 - step},
            {ax - w - 3, ay - h / 2 + step},
            // ring three -- the mid-row seats slid laterally
            {ax + 3 + 12, ay - h / 2},
            {ax - w - 3 - 12, ay - h / 2},
            {ax + 3 + 24, ay - h / 2},
            {ax - w - 3 - 24, ay - h / 2},
            {ax + 3 + 36, ay - h / 2},
            {ax - w - 3 - 36, ay - h / 2},
        };
        for (const auto& at : candidates) {
            const PxRect rect{at[0] - kLabelPad, at[1] - kLabelPad, at[0] + w + kLabelPad,
                              at[1] + h + kLabelPad};
            if (rect.x0 < mapRect.x0 || rect.y0 < mapRect.y0 || rect.x1 > mapRect.x1 ||
                rect.y1 > mapRect.y1) {
                continue;
            }
            bool clear = true;
            for (const PxRect& held : taken) {
                if (rect.overlaps(held)) {
                    clear = false;
                    break;
                }
            }
            if (!clear) {
                continue;
            }
            taken.push_back(rect);
            PlacedMapLabel label;
            label.text = text;
            label.x = at[0];
            label.y = at[1];
            label.way = way;
            label.anchorPx = ax;
            label.anchorPy = ay;
            placed.push_back(std::move(label));
            return;
        }
        // No clean position: DROPPED, never overprinted.
    };

    for (const Candidate& candidate : ways) {
        tryPlace(*candidate.sign, /*way=*/true);
    }
    for (const Candidate& candidate : doors) {
        tryPlace(*candidate.sign, /*way=*/false);
    }
    return placed;
}

// ---------------------------------------------------------------------------
// drawing
// ---------------------------------------------------------------------------

namespace {

/// A tiny filled triangle, for the one wedge this page draws. Edge functions
/// over the bounding box; render-side float is legal here.
void fillTriangle(Framebuffer& target, float x0, float y0, float x1, float y1, float x2, float y2,
                  const Rgb& colour, float alpha) {
    const auto edge = [](float ax, float ay, float bx, float by, float px, float py) {
        return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
    };
    const float area = edge(x0, y0, x1, y1, x2, y2);
    if (area == 0.0F) {
        return;
    }
    const int minX = static_cast<int>(std::floor(std::min({x0, x1, x2})));
    const int maxX = static_cast<int>(std::ceil(std::max({x0, x1, x2})));
    const int minY = static_cast<int>(std::floor(std::min({y0, y1, y2})));
    const int maxY = static_cast<int>(std::ceil(std::max({y0, y1, y2})));
    const float sign = area > 0.0F ? 1.0F : -1.0F;
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            if (!target.contains(x, y)) {
                continue;
            }
            const float px = static_cast<float>(x) + 0.5F;
            const float py = static_cast<float>(y) + 0.5F;
            if (edge(x0, y0, x1, y1, px, py) * sign >= 0.0F &&
                edge(x1, y1, x2, y2, px, py) * sign >= 0.0F &&
                edge(x2, y2, x0, y0, px, py) * sign >= 0.0F) {
                target.blend(x, y, colour, alpha);
            }
        }
    }
}

}  // namespace

void drawDistrictMap(Framebuffer& target, const DistrictMapState& state) {
    if (state.tiles == nullptr || state.palette == nullptr || state.openAmount <= 0.0F) {
        return;
    }
    const float open = std::min(1.0F, state.openAmount);
    const int width = target.width();
    const int height = target.height();

    // The page's own field first -- the world dims behind the plan the way it
    // dims behind the tiled Menu, and everything after draws over this.
    target.fillRect(0, 0, width, height, kBackdrop, 0.88F * open);

    const MapFrame frame = mapFrameFor(width, height, state.bounds);
    const sim::TileQuery& tiles = *state.tiles;
    const MapPalette& palette = *state.palette;

    for (std::int32_t y = state.bounds.minY; y <= state.bounds.maxY; ++y) {
        for (std::int32_t x = state.bounds.minX; x <= state.bounds.maxX; ++x) {
            const MapCell cell = mapCellAt(tiles, x, y, state.band);
            if (cell == MapCell::Void) {
                continue;  // the backdrop IS the void
            }
            Rgb tone;
            switch (cell) {
                case MapCell::Wall:
                    tone = palette.wallTone(tiles.material(x, y, state.band));
                    break;
                case MapCell::Floor:
                    tone = palette.floorTone(tiles.material(x, y, state.band));
                    break;
                case MapCell::LowerFloor: {
                    // The band below's own material, dimmed to read as lower
                    // ground -- see MapCell::LowerFloor's header.
                    std::uint16_t material = 0;
                    for (std::int32_t z = state.band - 1; z >= state.band - kLowerScanBands;
                         --z) {
                        if (tiles.walkable(x, y, z)) {
                            material = tiles.material(x, y, z);
                            break;
                        }
                    }
                    tone = palette.floorTone(material) * 0.42F;
                    break;
                }
                case MapCell::Water:
                    tone = palette.water;
                    break;
                default:
                    continue;
            }
            target.fillRect(frame.pxOfX(static_cast<float>(x)),
                            frame.pxOfY(static_cast<float>(y)), frame.scale, frame.scale, tone,
                            0.94F * open);
        }
    }

    // Door dots -- every authored door anchor gets its tick whether or not
    // its label found room, so a dropped name is still a marked entrance.
    for (std::size_t i = 0; i < sim::docks::kSignCount; ++i) {
        const sim::docks::Sign& sign = sim::docks::kSigns[i];
        if (sign.kind != sim::docks::SignKind::Door) {
            continue;
        }
        const int dot = std::max(2, frame.scale);
        target.fillRect(frame.pxOfX(sign.anchorX) - dot / 2, frame.pxOfY(sign.anchorY) - dot / 2,
                        dot, dot, kDoorDot, 0.9F * open);
    }

    // The names -- the entire point of the page. Plate first, then the text,
    // per the settled fill-then-border-then-text convention (rule 2: text
    // over a backdrop the layout cannot predict gets a plate).
    const int textScale = std::max(1, hudMinorScale(height));
    const std::vector<PlacedMapLabel> labels =
        placeMapLabels(frame, state.playerX, state.playerY, textScale);
    for (const PlacedMapLabel& label : labels) {
        const int w = textWidth(label.text, textScale);
        const int h = 6 * textScale;
        drawTextPlate(target, label.x - 2, label.y - 2, label.x + w + 1, label.y + h + 1, 1,
                      0.82F * open);
        drawText(target, label.x, label.y, label.text, label.way ? kWayInk : kDoorInk, open,
                 textScale);
    }

    // THE PLAYER, as a facing wedge -- tip forward, two heels back, the same
    // compass convention angle.hpp fixes: yaw 0 is north (-Y), clockwise.
    {
        const float rad = static_cast<float>(state.yawBam) *
                          (6.28318530718F / static_cast<float>(sim::kTurnFull));
        const float fx = std::sin(rad);
        const float fy = -std::cos(rad);
        const float px = static_cast<float>(frame.pxOfX(state.playerX));
        const float py = static_cast<float>(frame.pxOfY(state.playerY));
        const float size = 3.0F + 1.6F * static_cast<float>(frame.scale);
        const float tipX = px + fx * size;
        const float tipY = py + fy * size;
        const float backX = px - fx * size * 0.7F;
        const float backY = py - fy * size * 0.7F;
        const float sideX = -fy * size * 0.55F;
        const float sideY = fx * size * 0.55F;
        // A dark underlay one pixel proud, so the wedge reads over a light
        // street as well as over the harbour.
        fillTriangle(target, tipX + fx, tipY + fy, backX + sideX * 1.5F, backY + sideY * 1.5F,
                     backX - sideX * 1.5F, backY - sideY * 1.5F, Rgb{0.05F, 0.04F, 0.05F},
                     0.85F * open);
        fillTriangle(target, tipX, tipY, backX + sideX, backY + sideY, backX - sideX,
                     backY - sideY, kPlayerTone, open);
    }

    // Title -- where the plan says you are, in the HUD's own words -- and the
    // hint row, so the key that opened the page is the key the page names.
    {
        const int titleScale = hudScale(height);
        const std::string title = state.title.empty() ? std::string("THE DOCKS") : state.title;
        const int w = textWidth(title, titleScale);
        const int x = (width - w) / 2;
        const int y = 3;
        drawTextPlate(target, x - 3, y - 2, x + w + 2, y + 6 * titleScale + 1, 1, 0.9F * open);
        drawText(target, x, y, title, kDoorInk, open, titleScale);

        const std::string hint = "M CLOSES   NORTH IS UP";
        const int hw = textWidth(hint, textScale);
        const int hx = (width - hw) / 2;
        const int hy = height - 6 * textScale - 4;
        drawTextPlate(target, hx - 3, hy - 2, hx + hw + 2, hy + 6 * textScale + 1, 1,
                      0.82F * open);
        drawText(target, hx, hy, hint, kWayInk, open, textScale);
    }

    // A bouncer's warning outranks a map read -- the same routing the tiled
    // Menu gives its journal tile. One row, above the hint.
    if (!state.alert.empty()) {
        const int alertScale = hudScale(height);
        const std::string line = clipToWidth(state.alert, width - 8, alertScale);
        const int w = textWidth(line, alertScale);
        const int x = (width - w) / 2;
        const int y = height - 6 * alertScale - 6 * textScale - 10;
        drawTextPlate(target, x - 3, y - 2, x + w + 2, y + 6 * alertScale + 1, 1, 0.94F * open);
        drawText(target, x, y, line, Rgb{0.95F, 0.62F, 0.35F}, open, alertScale);
    }
}

}  // namespace granadad::render
