#include "granadad/render/capture.hpp"

#include <stb_image.h>
#include <stb_image_write.h>

#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

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

std::optional<Framebuffer> readPng(const std::filesystem::path& file) {
    // stb_impl.cpp builds stb_image with STBI_NO_STDIO, so the bytes are read
    // here and handed over.
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    const std::vector<char> bytes((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
    int width = 0;
    int height = 0;
    int comps = 0;
    stbi_uc* data = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
                                          static_cast<int>(bytes.size()), &width, &height, &comps, 4);
    if (data == nullptr || width <= 0 || height <= 0) {
        stbi_image_free(data);
        return std::nullopt;
    }
    Framebuffer frame(width, height);
    std::memcpy(frame.pixels().data(), data,
                static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
    stbi_image_free(data);
    return frame;
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
