#include "internal.hpp"

// THE ONE PLACE A FRAME IS READ BACK, AND THE PRESENT.

namespace granadad::render3d {

using namespace rl;

void Backend::endFrame(render::Framebuffer* capture) {
    // Everything queued in rlgl's batch is rasterized here, which is what
    // makes the readback below a picture of THIS frame.
    rlDrawRenderBatchActive();
    if (capture != nullptr) {
        const int width = GetRenderWidth();
        const int height = GetRenderHeight();
        if (width > 0 && height > 0) {
            *capture = render::Framebuffer(width, height);
#if defined(PLATFORM_MEMORY)
            // rlsw's colour buffer, top row first, R,G,B,A per pixel (the
            // BGRA output switch is thrown off in Dependencies.cmake) -- which
            // is exactly the framebuffer's own 0xAABBGGRR on a little-endian
            // host. The very call rcore_memory presents with; see the file
            // header on why not LoadImageFromScreen here.
            rlCopyFramebuffer(0, 0, width, height, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
                              capture->pixels().data());
#else
            // The GPU path: glReadPixels off the back buffer, flipped to top
            // row first by raylib, BEFORE the swap below -- the only moment a
            // double-buffered frame is defined.
            Image image = LoadImageFromScreen();
            if (image.data != nullptr && image.width == width && image.height == height) {
                std::memcpy(capture->pixels().data(), image.data,
                            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
                                4U);
            }
            UnloadImage(image);
#endif
            // A frame is opaque by definition. rlsw keeps the blended alpha
            // of every pixel a translucent overlay touched, and a PNG of that
            // would be see-through wherever a HUD wash was.
            for (std::uint32_t& pixel : capture->pixels()) {
                pixel |= 0xFF000000U;
            }
        }
    }
#if defined(PLATFORM_MEMORY)
    // No swap and no input poll -- see the file header. The frame already
    // lives in rlsw's own colour buffer, which is where readback read it.
#else
    EndDrawing();
#endif
}

}  // namespace granadad::render3d
