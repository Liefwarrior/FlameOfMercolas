// THE WORLD'S SURFACES (render3d/world_surfaces.hpp): the procedural material
// set of OBLIVION-PLAN section 7. What these cases pin:
//
//   * every surface is a pure function of nothing (two generations are the
//     same bytes), opaque, and not a flat colour;
//   * the atlas lays each surface out with a gutter of WRAPPED texels, which is
//     what lets the shader filter at a repeat without reading its neighbour
//     (every period in the generator divides the surface, so it tiles by
//     construction; the PR carries a contact sheet of two-by-two repeats);
//   * the committed PNGs under content/art/oblivion/materials are exactly what
//     the generator writes (when the art tree is present: a gate that has no
//     content tree says so and skips, a checkout that has it fails loudly);
//   * the material-to-surface table answers for every material in the registry.

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/atlas.hpp"
#include "granadad/render/capture.hpp"
#include "granadad/render3d/world_surfaces.hpp"

using namespace granadad::render3d;
namespace render = granadad::render;

namespace {

[[nodiscard]] int texel(const std::vector<std::uint8_t>& image, int x, int y, int channel) {
    const int wx = ((x % kSurfacePx) + kSurfacePx) % kSurfacePx;
    const int wy = ((y % kSurfacePx) + kSurfacePx) % kSurfacePx;
    return image[(static_cast<std::size_t>(wy) * kSurfacePx + static_cast<std::size_t>(wx)) * 4U +
                 static_cast<std::size_t>(channel)];
}

}  // namespace

TEST_CASE("every surface is deterministic, opaque and not a flat colour") {
    for (std::size_t s = 1; s < kSurfaceCount; ++s) {
        const auto surface = static_cast<Surface>(s);
        INFO("surface " << surfaceName(surface));
        const std::vector<std::uint8_t> a = generateSurface(surface);
        const std::vector<std::uint8_t> b = generateSurface(surface);
        REQUIRE(a.size() == static_cast<std::size_t>(kSurfacePx) * kSurfacePx * 4U);
        CHECK(a == b);
        for (std::size_t i = 3; i < a.size(); i += 4) {
            REQUIRE(a[i] == 255U);
        }
        // And it is not a flat colour.
        int lo = 255;
        int hi = 0;
        for (std::size_t i = 1; i < a.size(); i += 4) {
            lo = std::min<int>(lo, a[i]);
            hi = std::max<int>(hi, a[i]);
        }
        CHECK(hi - lo > 20);
    }
}

TEST_CASE("the surface atlas rings each cell with the texels of the repeat beside it") {
    const SurfaceAtlas atlas = SurfaceAtlas::build();
    const TextureData& tex = atlas.texture();
    REQUIRE(tex.tileU > 0.0F);
    REQUIRE(tex.mipmapped);
    REQUIRE(tex.pixels.size() == static_cast<std::size_t>(tex.width) * tex.height * 4U);
    for (std::size_t s = 1; s < kSurfaceCount; s += 3) {
        const auto surface = static_cast<Surface>(s);
        INFO("surface " << surfaceName(surface));
        const std::vector<std::uint8_t> image = generateSurface(surface);
        const std::array<float, 2> origin = atlas.origin(surface);
        const int ox = static_cast<int>(origin[0] * static_cast<float>(tex.width) + 0.5F);
        const int oy = static_cast<int>(origin[1] * static_cast<float>(tex.height) + 0.5F);
        for (int d : {-kSurfaceGutter, -1, 0, kSurfacePx - 1, kSurfacePx, kSurfacePx + kSurfaceGutter - 1}) {
            for (int e : {0, 17, kSurfacePx - 1}) {
                const std::size_t at = (static_cast<std::size_t>(oy + e) * static_cast<std::size_t>(tex.width) +
                                        static_cast<std::size_t>(ox + d)) * 4U;
                CHECK(tex.pixels[at + 1] == texel(image, d, e, 1));
                const std::size_t atV = (static_cast<std::size_t>(oy + d) * static_cast<std::size_t>(tex.width) +
                                         static_cast<std::size_t>(ox + e)) * 4U;
                CHECK(tex.pixels[atV + 1] == texel(image, e, d, 1));
            }
        }
    }
    // White is the flat cell: every texel white.
    const std::array<float, 2> white = atlas.origin(Surface::White);
    const int wx = static_cast<int>(white[0] * static_cast<float>(tex.width) + 0.5F);
    const int wy = static_cast<int>(white[1] * static_cast<float>(tex.height) + 0.5F);
    CHECK(tex.pixels[(static_cast<std::size_t>(wy + 40) * static_cast<std::size_t>(tex.width) + static_cast<std::size_t>(wx + 40)) * 4U] == 255U);
}

TEST_CASE("the committed material PNGs are what the generator writes") {
    const std::filesystem::path dir =
        granadad::content::contentDir() / "art" / "oblivion" / "materials";
    if (!std::filesystem::exists(dir)) {
        MESSAGE("no content/art/oblivion/materials here (a gate with no art tree): skipped");
        return;
    }
    for (std::size_t s = 1; s < kSurfaceCount; ++s) {
        const auto surface = static_cast<Surface>(s);
        const std::filesystem::path file = dir / (std::string(surfaceName(surface)) + ".png");
        INFO(file.string());
        const auto png = render::readPng(file);
        REQUIRE(png.has_value());
        REQUIRE(png->width() == kSurfacePx);
        REQUIRE(png->height() == kSurfacePx);
        const std::vector<std::uint8_t> generated = generateSurface(surface);
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(png->pixels().data());
        bool same = true;
        for (std::size_t i = 0; i < generated.size(); ++i) {
            if (bytes[i] != generated[i]) {
                same = false;
                break;
            }
        }
        CHECK_MESSAGE(same, "regenerate with granadad-gen-world-materials");
    }
}

TEST_CASE("every material in the registry has a surface for each role or is drawn flat in its tone") {
    const auto ids = render::materialIds();
    std::size_t withSurface = 0;
    for (std::size_t m = 0; m < ids.size(); ++m) {
        const auto material = static_cast<std::uint16_t>(m);
        const SurfaceTone tone = materialToneOf(material);
        CHECK(tone.r > 0.0F);
        for (Role role : {Role::Wall, Role::Inside, Role::Floor, Role::Indoor, Role::Cap, Role::Slab}) {
            const Surface surface = surfaceFor(material, role);
            CHECK(static_cast<std::size_t>(surface) < kSurfaceCount);
            if (surface != Surface::White) {
                ++withSurface;
            }
        }
    }
    CHECK(withSurface > 40);
    // The ones the Docks is built of wear what their name says.
    const auto idOf = [&](std::string_view name) {
        for (std::size_t m = 0; m < ids.size(); ++m) {
            if (ids[m] == name) {
                return static_cast<std::uint16_t>(m);
            }
        }
        FAIL("no such material");
        return std::uint16_t{0};
    };
    CHECK(surfaceFor(idOf("granite"), Role::Wall) == Surface::GraniteAshlar);
    CHECK(surfaceFor(idOf("brick"), Role::Wall) == Surface::BrickWall);
    CHECK(surfaceFor(idOf("brick"), Role::Inside) == Surface::Plaster);
    CHECK(surfaceFor(idOf("oak"), Role::Floor) == Surface::PlankDeck);
    CHECK(surfaceFor(idOf("thatch"), Role::Cap) == Surface::Thatch);
    CHECK(surfaceFor(idOf("dirt"), Role::Floor) == Surface::Earth);
    CHECK(surfaceFor(idOf("granite"), Role::Floor) == Surface::Cobble);
    CHECK(surfaceFor(idOf("granite"), Role::Indoor) == Surface::GraniteFlag);
}
