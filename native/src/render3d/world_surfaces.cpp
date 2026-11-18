#include "granadad/render3d/world_surfaces.hpp"

#include <algorithm>
#include <cstring>

namespace granadad::render3d {

namespace {

constexpr int kN = kSurfacePx;

// ---------------------------------------------------------------------------
// integer noise: periodic over the surface, so every pattern tiles
// ---------------------------------------------------------------------------

[[nodiscard]] std::uint32_t mix(std::uint32_t a, std::uint32_t b, std::uint32_t seed) noexcept {
    std::uint32_t h = a * 0x9E3779B1U ^ (b + 0x7F4A7C15U) * 0x85EBCA77U ^ seed * 0xC2B2AE3DU;
    h ^= h >> 15;
    h *= 0x2C1B3C6DU;
    h ^= h >> 12;
    h *= 0x297A2D39U;
    h ^= h >> 15;
    return h;
}

[[nodiscard]] int wrapN(int v) noexcept {
    v %= kN;
    return v < 0 ? v + kN : v;
}

/// Smoothstep of t in 0..256, result 0..256.
[[nodiscard]] int fade(int t) noexcept {
    return (t * t * (768 - 2 * t)) >> 24;
}

/// Value noise 0..255, lattice cells cx by cy texels (both divide kN).
[[nodiscard]] int vnoise(int x, int y, int cx, int cy, std::uint32_t seed) noexcept {
    x = wrapN(x);
    y = wrapN(y);
    const int gx = x / cx;
    const int gy = y / cy;
    const int sx = fade((x % cx) * 256 / cx);
    const int sy = fade((y % cy) * 256 / cy);
    const int cellsX = kN / cx;
    const int cellsY = kN / cy;
    const int gx1 = (gx + 1) % cellsX;
    const int gy1 = (gy + 1) % cellsY;
    const int v00 = static_cast<int>(mix(static_cast<std::uint32_t>(gx), static_cast<std::uint32_t>(gy), seed) & 255U);
    const int v10 = static_cast<int>(mix(static_cast<std::uint32_t>(gx1), static_cast<std::uint32_t>(gy), seed) & 255U);
    const int v01 = static_cast<int>(mix(static_cast<std::uint32_t>(gx), static_cast<std::uint32_t>(gy1), seed) & 255U);
    const int v11 = static_cast<int>(mix(static_cast<std::uint32_t>(gx1), static_cast<std::uint32_t>(gy1), seed) & 255U);
    const int top = v00 + (v10 - v00) * sx / 256;
    const int bottom = v01 + (v11 - v01) * sx / 256;
    return top + (bottom - top) * sy / 256;
}

[[nodiscard]] int vnoise(int x, int y, int c, std::uint32_t seed) noexcept {
    return vnoise(x, y, c, c, seed);
}

/// Four octaves, 0..255, centred near 128.
[[nodiscard]] int fbm(int x, int y, std::uint32_t seed) noexcept {
    return (vnoise(x, y, 32, seed) * 8 + vnoise(x, y, 16, seed + 1U) * 5 +
            vnoise(x, y, 8, seed + 2U) * 2 + vnoise(x, y, 4, seed + 3U)) /
           16;
}

[[nodiscard]] int isqrt(int v) noexcept {
    int r = 0;
    while ((r + 1) * (r + 1) <= v) {
        ++r;
    }
    return r;
}

struct Cell {
    int edge = 0;           // texels between this stone's rim and the next stone
    std::uint32_t id = 0;   // the stone
};

/// Periodic Voronoi over cells of `c` texels, points jittered by `jitter`
/// texels either side of the cell centre.
[[nodiscard]] Cell voronoi(int x, int y, int c, int jitter, std::uint32_t seed) noexcept {
    const int cells = kN / c;
    const int gx = wrapN(x) / c;
    const int gy = wrapN(y) / c;
    int best = 1 << 30;
    int second = 1 << 30;
    std::uint32_t bestId = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int cxu = gx + dx;
            const int cyu = gy + dy;
            const std::uint32_t wx = static_cast<std::uint32_t>(((cxu % cells) + cells) % cells);
            const std::uint32_t wy = static_cast<std::uint32_t>(((cyu % cells) + cells) % cells);
            const std::uint32_t h = mix(wx, wy, seed);
            const int span = 2 * jitter + 1;
            const int px = cxu * c + c / 2 - jitter + static_cast<int>(h % static_cast<std::uint32_t>(span));
            const int py = cyu * c + c / 2 - jitter + static_cast<int>((h >> 12) % static_cast<std::uint32_t>(span));
            // The query point in the same unwrapped frame as the cell.
            const int qx = wrapN(x);
            const int qy = wrapN(y);
            const int ddx = px - qx;
            const int ddy = py - qy;
            const int d = ddx * ddx + ddy * ddy;
            if (d < best) {
                second = best;
                best = d;
                bestId = mix(wx, wy, seed + 77U);
            } else if (d < second) {
                second = d;
            }
        }
    }
    Cell out;
    out.edge = (isqrt(second) - isqrt(best)) / 2;
    out.id = bestId;
    return out;
}

[[nodiscard]] int clampLum(int v) noexcept {
    return std::clamp(v, 0, 460);
}

// ---------------------------------------------------------------------------
// patterns: a luminance multiplier at (x, y), 256 = the tone itself
// ---------------------------------------------------------------------------

[[nodiscard]] int ashlar(int x, int y, std::uint32_t seed) noexcept {
    const int r = y / 16;
    const int fy = y % 16;
    const int offset = ((r & 1) * 16 + static_cast<int>(mix(static_cast<std::uint32_t>(r), 3U, seed) & 3U) * 8) % 32;
    const int sx = wrapN(x + kN - offset);
    const int fx = sx % 32;
    const int bi = sx / 32;
    const std::uint32_t h = mix(static_cast<std::uint32_t>(r), static_cast<std::uint32_t>(bi), seed);
    int lum = 226 + static_cast<int>(h % 60U) + (fbm(x, y, seed + 9U) - 128) / 4;
    if (fy < 2 || fx < 2) {
        return 118 + (fbm(x, y, seed + 5U) - 128) / 8;
    }
    if (fy == 2 || fx == 2) {
        lum += 28;
    } else if (fy == 15 || fx == 31) {
        lum -= 34;
    }
    return clampLum(lum);
}

[[nodiscard]] int brick(int x, int y, std::uint32_t seed) noexcept {
    const int r = y / 16;
    const int fy = y % 16;
    const int offset = (r & 1) * 16;
    const int sx = wrapN(x + kN - offset);
    const int fx = sx % 32;
    const int bi = sx / 32;
    if (fy < 3 || fx < 3) {
        return 150 + (vnoise(x, y, 4, seed + 6U) - 128) / 6;  // lime mortar
    }
    const std::uint32_t h = mix(static_cast<std::uint32_t>(r), static_cast<std::uint32_t>(bi), seed);
    int lum = 214 + static_cast<int>(h % 70U) + (vnoise(x, y, 4, seed + 1U) - 128) / 5 +
              (fbm(x, y, seed + 2U) - 128) / 5;
    if (fy == 3 || fx == 3) {
        lum += 20;
    } else if (fy == 15) {
        lum -= 26;
    }
    if (mix(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), seed + 4U) % 61U == 0U) {
        lum -= 56;  // a pit
    }
    return clampLum(lum);
}

[[nodiscard]] int cobble(int x, int y, std::uint32_t seed) noexcept {
    const Cell c = voronoi(x, y, 16, 4, seed);
    int lum = 205 + static_cast<int>(c.id % 80U) + (fbm(x, y, seed + 3U) - 128) / 5;
    if (c.edge < 2) {
        return 84 + (fbm(x, y, seed + 8U) - 128) / 10;
    }
    if (c.edge < 6) {
        lum -= (6 - c.edge) * 9;  // the stone's rounded shoulder
    }
    return clampLum(lum);
}

[[nodiscard]] int flagstone(int x, int y, std::uint32_t seed) noexcept {
    const Cell c = voronoi(x, y, 32, 8, seed);
    int lum = 218 + static_cast<int>(c.id % 70U) + (fbm(x, y, seed + 3U) - 128) / 4;
    if (c.edge < 2) {
        return 96 + (fbm(x, y, seed + 8U) - 128) / 10;
    }
    if (c.edge < 4) {
        lum -= (4 - c.edge) * 8;
    }
    return clampLum(lum);
}

/// Boards that run along u (x); the planks of a floor.
[[nodiscard]] int planksAcross(int x, int y, std::uint32_t seed) noexcept {
    const int r = y / 16;
    const int fy = y % 16;
    const std::uint32_t h = mix(static_cast<std::uint32_t>(r), 1U, seed);
    const int joint = static_cast<int>(h % static_cast<std::uint32_t>(kN));
    int lum = 212 + static_cast<int>((h >> 8) % 64U) + (vnoise(x, y, 64, 2, seed + 1U) - 128) / 3 +
              (vnoise(x, y, 16, 2, seed + 2U) - 128) / 6;
    if (fy == 0) {
        return 74;
    }
    if (fy == 1) {
        lum -= 40;
    }
    const int dj = wrapN(x - joint);
    if (dj < 2) {
        return 70;
    }
    if (fy == 8 && (wrapN(x - joint - 6) < 2 || wrapN(x - joint + 6) < 2)) {
        lum -= 70;  // a nail head
    }
    return clampLum(lum);
}

[[nodiscard]] int thatch(int x, int y, std::uint32_t seed) noexcept {
    int lum = 168 + vnoise(x, y, 4, 32, seed) / 2 + (vnoise(x, y, 8, 16, seed + 1U) - 128) / 6 +
              (static_cast<int>(mix(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), seed) & 31U) - 16);
    if (y % 32 < 3) {
        lum = lum * 70 / 100;  // a binding course
    }
    return clampLum(lum);
}

[[nodiscard]] int shingle(int x, int y, std::uint32_t seed) noexcept {
    const int r = y / 16;
    const int fy = y % 16;
    const int offset = (r & 1) * 8;
    const int sx = wrapN(x + kN - offset);
    const int fx = sx % 16;
    const int si = sx / 16;
    const std::uint32_t h = mix(static_cast<std::uint32_t>(r), static_cast<std::uint32_t>(si), seed);
    int lum = 214 + static_cast<int>(h % 64U) + (fbm(x, y, seed + 1U) - 128) / 5;
    if (fy < 2) {
        lum = lum * 55 / 100;  // under the course above
    } else if (fy < 5) {
        lum = lum * 80 / 100;
    } else if (fy > 12) {
        lum += 14;  // the lit lip
    }
    if (fx == 0) {
        lum = lum * 60 / 100;
    }
    return clampLum(lum);
}

[[nodiscard]] int plaster(int x, int y, std::uint32_t seed) noexcept {
    int lum = 250 + (fbm(x, y, seed) - 128) / 5 +
              static_cast<int>(mix(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), seed + 1U) & 15U) - 8;
    // a hairline crack or two
    const int c = vnoise(x, y, 64, 8, seed + 5U);
    if (c > 124 && c < 130) {
        lum -= 46;
    }
    return clampLum(lum);
}

[[nodiscard]] int concrete(int x, int y, std::uint32_t seed) noexcept {
    int lum = 240 + (vnoise(x, y, 4, seed) - 128) / 4 + (vnoise(x, y, 16, seed + 1U) - 128) / 4 +
              static_cast<int>(mix(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), seed + 2U) & 15U) - 8;
    if (x % 64 < 2 || y % 64 < 2) {
        lum = 150;  // a form-work seam
    }
    const int tx = x % 64;
    const int ty = y % 64;
    if (tx >= 30 && tx < 34 && ((ty >= 14 && ty < 18) || (ty >= 46 && ty < 50))) {
        lum = 120;  // a tie hole
    }
    return clampLum(lum);
}

[[nodiscard]] int earth(int x, int y, std::uint32_t seed) noexcept {
    int lum = 230 + (fbm(x, y, seed) - 128) / 2;
    const std::uint32_t h = mix(static_cast<std::uint32_t>(x / 3), static_cast<std::uint32_t>(y / 3), seed + 4U);
    if (h % 19U == 0U) {
        lum += 46;  // a pebble
    } else if (h % 23U == 0U) {
        lum -= 50;
    }
    return clampLum(lum);
}

[[nodiscard]] int iron(int x, int y, std::uint32_t seed) noexcept {
    int lum = 196 + (vnoise(x, y, 8, seed) - 128) / 3 + (vnoise(x, y, 32, seed + 1U) - 128) / 4;
    if (x % 64 < 2 || y % 32 < 2) {
        lum = 112;
    }
    const int rx = x % 32 - 16;
    const int ry = y % 32 - 16;
    if (rx * rx + ry * ry <= 6) {
        lum += 70;  // a rivet
    }
    return clampLum(lum);
}

[[nodiscard]] int cloth(int x, int y, std::uint32_t seed) noexcept {
    int lum = (((x >> 1) + (y >> 1)) & 1) != 0 ? 226 : 270;
    lum += (vnoise(x, y, 8, seed) - 128) / 6 +
           static_cast<int>(mix(static_cast<std::uint32_t>(x >> 1), static_cast<std::uint32_t>(y >> 1), seed) & 15U) - 8;
    return clampLum(lum);
}

enum class Pattern : std::uint8_t {
    Flat, Ashlar, Flag, Cobble, Brick, PlankH, PlankV, Plaster, Concrete, Thatch, Shingle, Earth, Iron, Cloth
};

struct SurfaceSpec {
    std::string_view name;
    Pattern pattern;
    std::uint16_t toneMaterial;  // registry id, or 0xFFFF for the explicit tone
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
    int percent;  // the tone scaled
};

// Registry ids (render::materialIds()): 0 ash, 1 brick, 2 brick_facade, 5 cloth,
// 6 dirt, 8 granite, 14 oak, 16 reman_concrete, 17 reman_facade, 18 steel,
// 19 thatch, 20 trudgeon_wood. test_world_surfaces.cpp checks the names.
constexpr std::uint16_t kExplicit = 0xFFFF;
constexpr SurfaceSpec kSpecs[kSurfaceCount] = {
    {"white", Pattern::Flat, kExplicit, 255, 255, 255, 100},
    {"granite_ashlar", Pattern::Ashlar, 8, 0, 0, 0, 100},
    {"granite_flag", Pattern::Flag, 8, 0, 0, 0, 108},
    {"cobble", Pattern::Cobble, 8, 0, 0, 0, 94},
    {"brick_wall", Pattern::Brick, 1, 0, 0, 0, 100},
    {"brick_facade", Pattern::Brick, 2, 0, 0, 0, 104},
    {"plank_deck", Pattern::PlankH, 14, 0, 0, 0, 112},
    {"plank_wall", Pattern::PlankV, 14, 0, 0, 0, 100},
    {"wet_wood", Pattern::PlankV, 20, 0, 0, 0, 84},
    {"plaster", Pattern::Plaster, kExplicit, 168, 154, 128, 100},
    {"concrete", Pattern::Concrete, 16, 0, 0, 0, 100},
    {"reman_facade", Pattern::Concrete, 17, 0, 0, 0, 104},
    {"thatch", Pattern::Thatch, 19, 0, 0, 0, 100},
    {"slate_roof", Pattern::Shingle, kExplicit, 78, 86, 98, 100},
    {"clay_roof", Pattern::Shingle, kExplicit, 128, 66, 46, 100},
    {"earth", Pattern::Earth, 6, 0, 0, 0, 100},
    {"ash", Pattern::Earth, 0, 0, 0, 0, 100},
    {"iron", Pattern::Iron, 18, 0, 0, 0, 72},
    {"cloth", Pattern::Cloth, 5, 0, 0, 0, 100},
    {"beam", Pattern::PlankV, kExplicit, 58, 40, 24, 100},
};

[[nodiscard]] int evalPattern(Pattern p, int x, int y, std::uint32_t seed) noexcept {
    switch (p) {
        case Pattern::Flat: return 256;
        case Pattern::Ashlar: return ashlar(x, y, seed);
        case Pattern::Flag: return flagstone(x, y, seed);
        case Pattern::Cobble: return cobble(x, y, seed);
        case Pattern::Brick: return brick(x, y, seed);
        case Pattern::PlankH: return planksAcross(x, y, seed);
        case Pattern::PlankV: return planksAcross(y, x, seed);
        case Pattern::Plaster: return plaster(x, y, seed);
        case Pattern::Concrete: return concrete(x, y, seed);
        case Pattern::Thatch: return thatch(x, y, seed);
        case Pattern::Shingle: return shingle(x, y, seed);
        case Pattern::Earth: return earth(x, y, seed);
        case Pattern::Iron: return iron(x, y, seed);
        case Pattern::Cloth: return cloth(x, y, seed);
    }
    return 256;
}

[[nodiscard]] int toByte(float unit) noexcept {
    return std::clamp(static_cast<int>(unit * 255.0F + 0.5F), 0, 255);
}

}  // namespace

std::string_view surfaceName(Surface surface) noexcept {
    const auto index = static_cast<std::size_t>(surface);
    return index < kSurfaceCount ? kSpecs[index].name : std::string_view{};
}

std::vector<std::uint8_t> generateSurface(Surface surface) {
    const auto index = static_cast<std::size_t>(surface);
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kN) * static_cast<std::size_t>(kN) * 4U, 255U);
    if (index >= kSurfaceCount) {
        return pixels;
    }
    const SurfaceSpec& spec = kSpecs[index];
    int tone[3] = {spec.r, spec.g, spec.b};
    if (spec.toneMaterial != kExplicit) {
        const render::Rgb t = render::materialTone(spec.toneMaterial);
        tone[0] = toByte(t.r);
        tone[1] = toByte(t.g);
        tone[2] = toByte(t.b);
    }
    for (int c = 0; c < 3; ++c) {
        tone[c] = tone[c] * spec.percent / 100;
    }
    // Every surface has its own seed, so two surfaces of one pattern differ.
    const std::uint32_t seed = 0x51A7U + static_cast<std::uint32_t>(index) * 7919U;
    for (int y = 0; y < kN; ++y) {
        for (int x = 0; x < kN; ++x) {
            const int lum = evalPattern(spec.pattern, x, y, seed);
            const std::size_t at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(kN) +
                                    static_cast<std::size_t>(x)) * 4U;
            for (int c = 0; c < 3; ++c) {
                pixels[at + static_cast<std::size_t>(c)] =
                    static_cast<std::uint8_t>(std::clamp(tone[c] * lum / 256, 0, 255));
            }
            pixels[at + 3] = 255U;
        }
    }
    return pixels;
}

SurfaceAtlas SurfaceAtlas::build() {
    SurfaceAtlas atlas;
    TextureData& tex = atlas.texture_;
    tex.id = kChunkMeshIdBase;  // chunk_mesher.hpp's kChunkAtlasTextureId
    tex.version = 2;
    tex.width = kSurfaceColumns * kSurfaceStride;
    tex.height = kSurfaceRows * kSurfaceStride;
    tex.pixels.assign(static_cast<std::size_t>(tex.width) * static_cast<std::size_t>(tex.height) * 4U, 255U);
    tex.tileU = static_cast<float>(kSurfacePx) / static_cast<float>(tex.width);
    tex.tileV = static_cast<float>(kSurfacePx) / static_cast<float>(tex.height);
    tex.mipmapped = true;
    for (std::size_t s = 0; s < kSurfaceCount; ++s) {
        const std::vector<std::uint8_t> image = generateSurface(static_cast<Surface>(s));
        const int col = static_cast<int>(s) % kSurfaceColumns;
        const int row = static_cast<int>(s) / kSurfaceColumns;
        std::uint64_t sum[3] = {0, 0, 0};
        for (int v = 0; v < kN; ++v) {
            for (int u = 0; u < kN; ++u) {
                const std::size_t from = (static_cast<std::size_t>(v) * kN + static_cast<std::size_t>(u)) * 4U;
                for (int c = 0; c < 3; ++c) {
                    sum[c] += image[from + static_cast<std::size_t>(c)];
                }
            }
        }
        const double area = static_cast<double>(kN) * static_cast<double>(kN) * 255.0;
        atlas.tones_[s] = SurfaceTone{static_cast<float>(static_cast<double>(sum[0]) / area),
                                      static_cast<float>(static_cast<double>(sum[1]) / area),
                                      static_cast<float>(static_cast<double>(sum[2]) / area)};
        // The cell with its wrapped gutter.
        for (int v = -kSurfaceGutter; v < kN + kSurfaceGutter; ++v) {
            for (int u = -kSurfaceGutter; u < kN + kSurfaceGutter; ++u) {
                const std::size_t from = (static_cast<std::size_t>(wrapN(v)) * kN + static_cast<std::size_t>(wrapN(u))) * 4U;
                const int px = col * kSurfaceStride + kSurfaceGutter + u;
                const int py = row * kSurfaceStride + kSurfaceGutter + v;
                const std::size_t to = (static_cast<std::size_t>(py) * static_cast<std::size_t>(tex.width) +
                                        static_cast<std::size_t>(px)) * 4U;
                std::memcpy(&tex.pixels[to], &image[from], 4);
            }
        }
    }
    return atlas;
}

std::array<float, 2> SurfaceAtlas::origin(Surface surface) const noexcept {
    const auto s = static_cast<std::size_t>(surface);
    const int col = static_cast<int>(s) % kSurfaceColumns;
    const int row = static_cast<int>(s) / kSurfaceColumns;
    return {static_cast<float>(col * kSurfaceStride + kSurfaceGutter) / static_cast<float>(texture_.width),
            static_cast<float>(row * kSurfaceStride + kSurfaceGutter) / static_cast<float>(texture_.height)};
}

SurfaceTone SurfaceAtlas::tone(Surface surface) const noexcept {
    const auto s = static_cast<std::size_t>(surface);
    return s < kSurfaceCount ? tones_[s] : SurfaceTone{};
}

// ---------------------------------------------------------------------------
// materials to surfaces
// ---------------------------------------------------------------------------

SurfaceTone materialToneOf(std::uint16_t material) noexcept {
    const render::Rgb t = render::materialTone(material);
    return SurfaceTone{t.r, t.g, t.b};
}

Surface surfaceFor(std::uint16_t material, Role role) noexcept {
    const auto ids = render::materialIds();
    if (material >= ids.size()) {
        return Surface::White;
    }
    const std::string_view name = ids[material];
    const auto pick = [role](Surface wall, Surface inside, Surface floor, Surface indoor,
                             Surface cap) noexcept {
        switch (role) {
            case Role::Wall: return wall;
            case Role::Inside: return inside;
            case Role::Floor: return floor;
            case Role::Indoor: return indoor;
            case Role::Cap: return cap;
            case Role::Slab: return wall;
        }
        return Surface::White;
    };
    if (name == "granite" || name == "granite_facade") {
        return pick(Surface::GraniteAshlar, Surface::Plaster, Surface::Cobble, Surface::GraniteFlag,
                    Surface::GraniteAshlar);
    }
    if (name == "brick") {
        return pick(Surface::BrickWall, Surface::Plaster, Surface::Cobble, Surface::GraniteFlag,
                    Surface::BrickWall);
    }
    if (name == "brick_facade") {
        return pick(Surface::BrickFacade, Surface::Plaster, Surface::Cobble, Surface::GraniteFlag,
                    Surface::BrickFacade);
    }
    if (name == "reman_concrete") {
        return pick(Surface::Concrete, Surface::Concrete, Surface::Concrete, Surface::Concrete,
                    Surface::Concrete);
    }
    if (name == "reman_facade") {
        return pick(Surface::RemanFacade, Surface::RemanFacade, Surface::RemanFacade,
                    Surface::RemanFacade, Surface::RemanFacade);
    }
    if (name == "oak") {
        return pick(Surface::PlankWall, Surface::PlankWall, Surface::PlankDeck, Surface::PlankDeck,
                    Surface::PlankDeck);
    }
    if (name == "trudgeon_wood" || name == "trudgeon_wood@getilia_soak") {
        return pick(Surface::WetWood, Surface::PlankWall, Surface::WetWood, Surface::PlankDeck,
                    Surface::WetWood);
    }
    if (name == "thatch") {
        return Surface::Thatch;
    }
    if (name == "dirt") {
        return Surface::Earth;
    }
    if (name == "ash") {
        return Surface::Ash;
    }
    if (name == "cloth") {
        return Surface::Cloth;
    }
    if (name == "steel") {
        return Surface::Iron;
    }
    return Surface::White;
}

}  // namespace granadad::render3d
