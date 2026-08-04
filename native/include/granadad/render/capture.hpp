#pragma once

// The frame capture path — the thing every later sprint needs.
//
// The Java build had `--smoke=N --screenshot=path`, and it was the single most
// useful piece of tooling in the project: it is how a sprint proves it drew
// what it says it drew. Losing it in the rewrite would mean nine more sprints
// of "it looks right on my machine".
//
// The version here is strictly better in one respect: because the renderer is
// software, capture needs NO WINDOW, no GPU and no display server. It runs over
// ssh, in a container, and inside the test suite — which is why the docker gate
// can now render a frame of the Docks and assert facts about the pixels on
// every single build.

#include <cstdint>
#include <filesystem>
#include <string>

#include "granadad/render/framebuffer.hpp"

namespace granadad::render {

/// Writes a framebuffer as an 8-bit RGBA PNG. Creates parent directories.
/// Returns false and leaves no partial file on failure.
bool writePng(const Framebuffer& frame, const std::filesystem::path& file);

/// Scales a framebuffer up by an integer factor with NEAREST NEIGHBOUR, never
/// interpolation. The chunkiness is the art direction, not an artefact, and a
/// smoothing upscale would throw it away.
[[nodiscard]] Framebuffer upscaleNearest(const Framebuffer& source, int factor);

}  // namespace granadad::render
