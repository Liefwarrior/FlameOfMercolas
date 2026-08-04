#include "granadad/render/capture.hpp"

#include <stb_image_write.h>

#include <system_error>

namespace granadad::render {

bool writePng(const Framebuffer& frame, const std::filesystem::path& file) {
    std::error_code error;
    if (file.has_parent_path() && !file.parent_path().empty()) {
        std::filesystem::create_directories(file.parent_path(), error);
    }
    // Row stride in bytes; pixels are packed R,G,B,A on a little-endian host,
    // which is exactly what stb wants for comp == 4.
    const int stride = frame.width() * 4;
    const int ok = stbi_write_png(file.string().c_str(), frame.width(), frame.height(), 4,
                                  frame.pixels().data(), stride);
    return ok != 0;
}

Framebuffer upscaleNearest(const Framebuffer& source, int factor) {
    const int scale = factor < 1 ? 1 : factor;
    Framebuffer target(source.width() * scale, source.height() * scale);
    for (int y = 0; y < target.height(); ++y) {
        const int sy = y / scale;
        for (int x = 0; x < target.width(); ++x) {
            const int sx = x / scale;
            target.pixels()[target.index(x, y)] = source.pixels()[source.index(sx, sy)];
            target.depth()[target.index(x, y)] = source.depth()[source.index(sx, sy)];
        }
    }
    return target;
}

}  // namespace granadad::render
