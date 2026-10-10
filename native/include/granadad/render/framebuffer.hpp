#pragma once

// The surface everything is drawn into, and the depth buffer that lets sprites
// sit inside the world rather than on top of it.
//
// The whole renderer is SOFTWARE, on purpose, and that decision buys three
// things this project specifically needs:
//
//   * a frame can be captured with no window, no GPU and no display server, so
//     `--screenshot` works over ssh, in CI and inside the docker gate. Every
//     sprint after this one has to be able to prove visually that it works,
//     and a capture path that needs a desktop is a capture path nobody runs.
//   * the output is exactly reproducible for a given camera and world, which
//     makes a rendered frame a testable artifact instead of a screenshot
//     somebody eyeballed once.
//   * the chunky low-resolution look the visual target asks for is the NATIVE
//     result rather than something faked with a shader: render at 640x360 and
//     let the window scale it with nearest-neighbour.
//
// Floats are legal here and only here. Nothing in this file or its neighbours
// under include/granadad/render is allowed anywhere near simulation state.

#include <cstdint>
#include <vector>

namespace granadad::render {

/// Linear-ish RGB in 0..1. Shading works in float and packs once at the end.
struct Rgb {
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
};

[[nodiscard]] constexpr Rgb operator*(const Rgb& c, float k) noexcept {
    return Rgb{c.r * k, c.g * k, c.b * k};
}
[[nodiscard]] constexpr Rgb operator+(const Rgb& a, const Rgb& b) noexcept {
    return Rgb{a.r + b.r, a.g + b.g, a.b + b.b};
}
[[nodiscard]] constexpr Rgb lerp(const Rgb& a, const Rgb& b, float t) noexcept {
    return Rgb{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

/// Packs to 0xAABBGGRR, which is R,G,B,A in memory on a little-endian machine —
/// what stb_image_write wants, what SDL calls PIXELFORMAT_ABGR8888, and what
/// raylib calls PIXELFORMAT_UNCOMPRESSED_R8G8B8A8. Alpha is always 0xFF.
[[nodiscard]] std::uint32_t packRgb(const Rgb& colour) noexcept;
/// The same, with a coverage alpha (0..1). 3D BUILD: the HUD overlay is a
/// transparent surface composited over the raylib frame, and this is how a
/// pixel says how much of it is there. See Framebuffer::clearTransparent.
[[nodiscard]] std::uint32_t packRgba(const Rgb& colour, float alpha) noexcept;
[[nodiscard]] Rgb unpackRgb(std::uint32_t pixel) noexcept;
/// The coverage alpha of a packed pixel, 0..1. 1 for anything packRgb wrote.
[[nodiscard]] float unpackAlpha(std::uint32_t pixel) noexcept;

/// A drawable surface. Pixels and depth are parallel, row-major, top row first.
class Framebuffer {
public:
    Framebuffer(int width, int height);

    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }

    [[nodiscard]] std::vector<std::uint32_t>& pixels() noexcept { return pixels_; }
    [[nodiscard]] const std::vector<std::uint32_t>& pixels() const noexcept { return pixels_; }

    /// Perpendicular distance from the eye, in world tiles. Infinity where
    /// nothing has been drawn.
    [[nodiscard]] std::vector<float>& depth() noexcept { return depth_; }
    [[nodiscard]] const std::vector<float>& depth() const noexcept { return depth_; }

    [[nodiscard]] std::size_t index(int x, int y) const noexcept {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
               static_cast<std::size_t>(x);
    }

    [[nodiscard]] bool contains(int x, int y) const noexcept {
        return x >= 0 && y >= 0 && x < width_ && y < height_;
    }

    void clear(const Rgb& colour);

    /// 3D BUILD. Clears to NOTHING -- every pixel fully transparent, depth
    /// infinite -- so what the HUD, the pages and the washes then draw is an
    /// overlay the 3D backend composites over its own frame. Nothing about
    /// how they draw changes: set() writes opaque pixels exactly as before,
    /// and blend() over an OPAQUE pixel is bit-identical to what it always
    /// was, which is what keeps every pixel-exact HUD/page test green. Only
    /// a blend over a pixel that is not yet opaque -- a state no software
    /// frame ever had -- takes the coverage path.
    void clearTransparent();

    void set(int x, int y, const Rgb& colour, float depthValue) noexcept {
        const std::size_t i = index(x, y);
        pixels_[i] = packRgb(colour);
        depth_[i] = depthValue;
    }

    /// Blends `colour` over what is there, with no depth write. For the HUD and
    /// for additive sprite glow. Straight-alpha "over": on an opaque pixel the
    /// RGB result is the same lerp it has always been; on a transparent or
    /// partial pixel the alpha accumulates as coverage (a + A(1 - a)).
    void blend(int x, int y, const Rgb& colour, float alpha) noexcept;

    /// Fills an axis-aligned rectangle, clipped. HUD workhorse.
    void fillRect(int x, int y, int w, int h, const Rgb& colour, float alpha);

private:
    int width_;
    int height_;
    std::vector<std::uint32_t> pixels_;
    std::vector<float> depth_;
};

}  // namespace granadad::render
