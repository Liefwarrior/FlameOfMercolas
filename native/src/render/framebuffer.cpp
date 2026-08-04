#include "granadad/render/framebuffer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace granadad::render {

namespace {

[[nodiscard]] std::uint32_t channel(float value) noexcept {
    const float clamped = value < 0.0F ? 0.0F : (value > 1.0F ? 1.0F : value);
    return static_cast<std::uint32_t>(clamped * 255.0F + 0.5F);
}

}  // namespace

std::uint32_t packRgb(const Rgb& colour) noexcept {
    return channel(colour.r) | (channel(colour.g) << 8) | (channel(colour.b) << 16) |
           0xFF000000U;
}

Rgb unpackRgb(std::uint32_t pixel) noexcept {
    return Rgb{static_cast<float>(pixel & 0xFFU) / 255.0F,
               static_cast<float>((pixel >> 8) & 0xFFU) / 255.0F,
               static_cast<float>((pixel >> 16) & 0xFFU) / 255.0F};
}

Framebuffer::Framebuffer(int width, int height)
    : width_(width < 1 ? 1 : width),
      height_(height < 1 ? 1 : height),
      pixels_(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), 0xFF000000U),
      depth_(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_),
             std::numeric_limits<float>::infinity()) {}

void Framebuffer::clear(const Rgb& colour) {
    std::fill(pixels_.begin(), pixels_.end(), packRgb(colour));
    std::fill(depth_.begin(), depth_.end(), std::numeric_limits<float>::infinity());
}

void Framebuffer::blend(int x, int y, const Rgb& colour, float alpha) noexcept {
    if (!contains(x, y) || alpha <= 0.0F) {
        return;
    }
    const std::size_t i = index(x, y);
    if (alpha >= 1.0F) {
        pixels_[i] = packRgb(colour);
        return;
    }
    pixels_[i] = packRgb(lerp(unpackRgb(pixels_[i]), colour, alpha));
}

void Framebuffer::fillRect(int x, int y, int w, int h, const Rgb& colour, float alpha) {
    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(width_, x + w);
    const int y1 = std::min(height_, y + h);
    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            blend(px, py, colour, alpha);
        }
    }
}

}  // namespace granadad::render
