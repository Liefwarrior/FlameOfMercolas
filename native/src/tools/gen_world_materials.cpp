// THE MATERIAL GENERATOR (OBLIVION-PLAN section 7, item 1).
//
//     granadad-gen-world-materials [output-dir] [--tiled sheet.png]
//
// With --tiled, also writes a sheet of every surface repeated two by two, so a
// seam where a repeat meets the next shows (there is none: every period in the
// generator divides the surface).
//
// Writes every surface of render3d/world_surfaces.hpp as a tileable 128x128 PNG
// (two metres of wall, floor or roof at 64 texels to the metre) and a contact
// sheet of all of them, by default into content/art/oblivion/materials. The
// output is committed; the game does not read it (it generates the same texels
// in process, from the same code, in integers), and
// tests/test_world_surfaces.cpp fails if the committed files stop being what
// this program would write.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/capture.hpp"
#include "granadad/render/framebuffer.hpp"
#include "granadad/render3d/world_surfaces.hpp"

using namespace granadad;

int main(int argc, char** argv) {
    std::filesystem::path out = content::contentDir() / "art" / "oblivion" / "materials";
    std::filesystem::path tiledSheet;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--tiled") == 0 && i + 1 < argc) {
            tiledSheet = argv[++i];
        } else {
            out = argv[i];
        }
    }
    std::filesystem::create_directories(out);
    constexpr int kGap = 8;
    constexpr int kColumns = 5;
    const int cell = render3d::kSurfacePx + kGap;
    const int rows = (static_cast<int>(render3d::kSurfaceCount) + kColumns - 1) / kColumns;
    render::Framebuffer sheet(kColumns * cell + kGap, rows * cell + kGap);
    for (std::uint32_t& px : sheet.pixels()) {
        px = 0xFF202020U;
    }
    int written = 0;
    for (std::size_t s = 0; s < render3d::kSurfaceCount; ++s) {
        const auto surface = static_cast<render3d::Surface>(s);
        const std::vector<std::uint8_t> image = render3d::generateSurface(surface);
        render::Framebuffer frame(render3d::kSurfacePx, render3d::kSurfacePx);
        std::memcpy(frame.pixels().data(), image.data(), image.size());
        const int col = static_cast<int>(s) % kColumns;
        const int row = static_cast<int>(s) / kColumns;
        for (int y = 0; y < render3d::kSurfacePx; ++y) {
            for (int x = 0; x < render3d::kSurfacePx; ++x) {
                sheet.pixels()[static_cast<std::size_t>(kGap + row * cell + y) *
                                   static_cast<std::size_t>(sheet.width()) +
                               static_cast<std::size_t>(kGap + col * cell + x)] =
                    frame.pixels()[static_cast<std::size_t>(y) * render3d::kSurfacePx +
                                   static_cast<std::size_t>(x)];
            }
        }
        if (surface == render3d::Surface::White) {
            continue;  // flat white is not an image anybody needs on disk
        }
        const std::filesystem::path file =
            out / (std::string(render3d::surfaceName(surface)) + ".png");
        if (!render::writePng(frame, file)) {
            std::fprintf(stderr, "could not write %s\n", file.string().c_str());
            return 1;
        }
        ++written;
    }
    if (!tiledSheet.empty()) {
        constexpr int kSide = render3d::kSurfacePx * 2;
        render::Framebuffer tiled(kColumns * (kSide + kGap) + kGap, rows * (kSide + kGap) + kGap);
        for (std::uint32_t& px : tiled.pixels()) {
            px = 0xFF202020U;
        }
        for (std::size_t s = 0; s < render3d::kSurfaceCount; ++s) {
            const std::vector<std::uint8_t> image = render3d::generateSurface(static_cast<render3d::Surface>(s));
            const auto* texels = reinterpret_cast<const std::uint32_t*>(image.data());
            const int col = static_cast<int>(s) % kColumns;
            const int row = static_cast<int>(s) / kColumns;
            for (int y = 0; y < kSide; ++y) {
                for (int x = 0; x < kSide; ++x) {
                    tiled.pixels()[static_cast<std::size_t>(kGap + row * (kSide + kGap) + y) *
                                       static_cast<std::size_t>(tiled.width()) +
                                   static_cast<std::size_t>(kGap + col * (kSide + kGap) + x)] =
                        texels[static_cast<std::size_t>(y % render3d::kSurfacePx) * render3d::kSurfacePx +
                               static_cast<std::size_t>(x % render3d::kSurfacePx)];
                }
            }
        }
        if (!render::writePng(tiled, tiledSheet)) {
            std::fprintf(stderr, "could not write the tiled sheet\n");
            return 1;
        }
    }
    if (!render::writePng(sheet, out / "contact-sheet.png")) {
        std::fprintf(stderr, "could not write the contact sheet\n");
        return 1;
    }
    std::printf("wrote %d surfaces and the contact sheet to %s\n", written, out.string().c_str());
    return 0;
}
