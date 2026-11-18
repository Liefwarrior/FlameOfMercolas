#pragma once

// GOLDEN-IMAGE COMPARISON, WITH A STATED TOLERANCE.
//
// The headless 3D frame is drawn by Mesa's llvmpipe. In one process it is
// deterministic (the frame tests still say "twice, byte for byte"), but Mesa
// and LLVM move low bits between versions, so a frame is compared with a
// committed PNG by TOLERANCE and never by hash. The scene hash
// (render3d::sceneHash) stays the exact cross-machine proof; a frame is the
// picture of that description, judged by eye and by these two numbers.
//
//   maxChannel  the largest per-channel difference any pixel may have;
//   meanError   the mean absolute per-channel difference over the whole frame;
//   badPixels   the share of pixels allowed past maxChannel (edges move by a
//               pixel when a rasterizer rounds differently).
//
// A golden lives in native/tests/golden/<name>.png. It is REGENERATED, never
// hand-edited:
//
//     GRANADAD_BLESS_GOLDEN=1 ./granadad-tests --source-file=*test_golden.cpp
//
// writes every golden the run compares into the source tree
// (GRANADAD_GOLDEN_DIR), and the diff of the PNGs is the review. Without that
// variable a missing golden FAILS, loudly: a golden that silently passes
// because it does not exist proves nothing.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <string>

#include "granadad/render/capture.hpp"
#include "granadad/render/framebuffer.hpp"

#ifndef GRANADAD_GOLDEN_DIR
#error "GRANADAD_GOLDEN_DIR is defined by native/CMakeLists.txt"
#endif

namespace granadad::testing {

struct GoldenTolerance {
    int maxChannel = 24;
    double meanError = 1.5;
    double badPixelShare = 0.01;
};

struct GoldenReport {
    bool ok = false;
    bool blessed = false;
    int worstChannel = 0;
    double meanError = 0.0;
    double badPixelShare = 0.0;
    std::string why;
};

inline GoldenReport compareWithGolden(const render::Framebuffer& frame, const std::string& name,
                                      const GoldenTolerance& tol = {}) {
    GoldenReport report;
    const std::filesystem::path file = std::filesystem::path(GRANADAD_GOLDEN_DIR) / (name + ".png");
    const char* bless = std::getenv("GRANADAD_BLESS_GOLDEN");
    if (bless != nullptr && bless[0] != '\0' && bless[0] != '0') {
        // Opaque, like every captured frame.
        render::Framebuffer opaque = frame;
        for (std::uint32_t& pixel : opaque.pixels()) {
            pixel |= 0xFF000000U;
        }
        report.ok = render::writePng(opaque, file);
        report.blessed = true;
        report.why = report.ok ? "blessed " + file.string() : "could not write " + file.string();
        return report;
    }
    const std::optional<render::Framebuffer> golden = render::readPng(file);
    if (!golden) {
        report.why = "no golden image at " + file.string() +
                     " (regenerate with GRANADAD_BLESS_GOLDEN=1)";
        return report;
    }
    if (golden->width() != frame.width() || golden->height() != frame.height()) {
        report.why = "golden is " + std::to_string(golden->width()) + "x" +
                     std::to_string(golden->height()) + ", frame is " +
                     std::to_string(frame.width()) + "x" + std::to_string(frame.height());
        return report;
    }
    std::uint64_t sum = 0;
    std::size_t bad = 0;
    const std::size_t count = frame.pixels().size();
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint32_t a = frame.pixels()[i];
        const std::uint32_t b = golden->pixels()[i];
        int worst = 0;
        for (int shift = 0; shift < 24; shift += 8) {  // R, G, B; alpha is not compared
            const int d = std::abs(static_cast<int>((a >> shift) & 0xFFU) -
                                   static_cast<int>((b >> shift) & 0xFFU));
            sum += static_cast<std::uint64_t>(d);
            worst = std::max(worst, d);
        }
        report.worstChannel = std::max(report.worstChannel, worst);
        bad += worst > tol.maxChannel ? 1U : 0U;
    }
    report.meanError = static_cast<double>(sum) / (3.0 * static_cast<double>(count));
    report.badPixelShare = static_cast<double>(bad) / static_cast<double>(count);
    report.ok = report.meanError <= tol.meanError && report.badPixelShare <= tol.badPixelShare;
    char text[256];
    std::snprintf(text, sizeof text,
                  "%s: worst channel %d, mean error %.3f (limit %.3f), %.3f%% of pixels past %d "
                  "(limit %.3f%%)",
                  name.c_str(), report.worstChannel, report.meanError, tol.meanError,
                  100.0 * report.badPixelShare, tol.maxChannel, 100.0 * tol.badPixelShare);
    report.why = text;
    return report;
}

}  // namespace granadad::testing
