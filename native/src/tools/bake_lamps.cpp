// granadad-bake-lamps — turns a fixture's authored light_source markers into
// the baked sidecar the game reads.
//
//     granadad-bake-lamps <fixture.tmx> <out.lamps.json> [world-name]
//
// Run once per fixture, by hand, and commit the output beside the baked world.
// It exists so that the 27 lamps of the Docks are DERIVED from canon rather
// than transcribed, and so re-deriving them after an authoring change is one
// command instead of an afternoon.
//
// This deliberately does not run inside the docker gate: content/maps/src is
// 2 MB of authored art that the compiler has no use for, and admitting it to
// the build context to re-generate a file that is already committed would be
// paying that cost on every build forever. The scanner it calls IS covered
// there -- test_lamps.cpp drives it over an inline fixture, and over the
// committed sidecar.

#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "granadad/render/lamps.hpp"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: granadad-bake-lamps <fixture.tmx> <out.lamps.json> [world-name]\n");
        return 2;
    }
    const std::filesystem::path source = argv[1];
    const std::filesystem::path target = argv[2];
    const std::string world = argc > 3 ? argv[3] : source.stem().string();

    try {
        const std::vector<granadad::render::Lamp> lamps = granadad::render::scanTmxFile(source);
        const std::string text = granadad::render::writeLampBake(
            world, "maps/src/" + source.filename().string(), lamps);

        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (!out) {
            std::fprintf(stderr, "granadad-bake-lamps: cannot write %s\n", target.string().c_str());
            return 1;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.close();
        if (!out) {
            std::fprintf(stderr, "granadad-bake-lamps: write failed for %s\n",
                         target.string().c_str());
            return 1;
        }

        std::size_t fire = 0;
        for (const granadad::render::Lamp& lamp : lamps) {
            if (lamp.warmth == granadad::render::LampWarmth::Fire) {
                ++fire;
            }
        }
        std::printf("granadad-bake-lamps: %zu light source(s) from %s -> %s (%zu fire, %zu lantern)\n",
                    lamps.size(), source.filename().string().c_str(),
                    target.filename().string().c_str(), fire, lamps.size() - fire);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "granadad-bake-lamps: %s\n", error.what());
        return 1;
    }
}
