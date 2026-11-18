#include "internal.hpp"

// THE ONE PLACE A FRAME IS READ BACK, AND THE PRESENT.

namespace granadad::render3d {

using namespace rl;

void Backend::endFrame(render::Framebuffer* capture) {
    // A frame with no overlay still has its scene to put on the window.
    impl_->resolveScene();
    // Everything queued in rlgl's batch is rasterized here, which is what
    // makes the readback below a picture of THIS frame.
    rlDrawRenderBatchActive();
    if (capture != nullptr) {
        const int width = GetRenderWidth();
        const int height = GetRenderHeight();
        if (width > 0 && height > 0) {
            *capture = render::Framebuffer(width, height);
            // glReadPixels off the back buffer, flipped to top row first by
            // raylib, BEFORE the swap below -- the only moment a
            // double-buffered frame is defined. The same call on a window
            // and on the headless llvmpipe context.
            Image image = LoadImageFromScreen();
            if (image.data != nullptr && image.width == width && image.height == height) {
                std::memcpy(capture->pixels().data(), image.data,
                            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
                                4U);
            }
            UnloadImage(image);
            // A frame is opaque by definition: the back buffer keeps the
            // blended alpha of every pixel a translucent overlay touched, and
            // a PNG of that would be see-through wherever a HUD wash was.
            for (std::uint32_t& pixel : capture->pixels()) {
                pixel |= 0xFF000000U;
            }
        }
    }
    EndDrawing();
}

}  // namespace granadad::render3d
